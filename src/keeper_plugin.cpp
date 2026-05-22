#include "keeper_plugin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QUrl>


// ── Lifecycle ────────────────────────────────────────────────────────────────

KeeperPlugin::KeeperPlugin()
    : nam_(new QNetworkAccessManager(this))
{}

void KeeperPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    loadQueue();
    // Defer queue resume + client init so the event loop is running
    QTimer::singleShot(2000, this, [this]{ advanceQueue(); });
    qDebug() << "KeeperPlugin: initialized";
}

// ── Public API ───────────────────────────────────────────────────────────────

QString KeeperPlugin::preserveItem(const QString& urlOrId)
{
    QString id = parseIdentifier(urlOrId);
    if (id.isEmpty())
        return R"({"success":false,"error":"could not parse identifier"})";

    // Deduplicate
    for (const auto& item : queue_)
        if (item.identifier == id && item.status != "failed" && item.status != "cancelled")
            return R"({"success":false,"error":"already in queue"})";

    KeeperItem item;
    item.identifier = id;
    item.title      = id;
    item.status     = "queued";
    queue_.append(item);
    saveQueue();

    emitEvent("itemQueued", {QVariantMap{{"id", id}, {"title", id}}});
    qDebug() << "KeeperPlugin: queued" << id;

    if (!busy_) advanceQueue();
    return QString(R"({"queued":true,"id":"%1"})").arg(id);
}

QString KeeperPlugin::preserveCollection(const QString& name, int limit)
{
    if (!logosAPI)
        return R"({"success":false,"error":"not initialized"})";

    // Fetch collection via IA scraping API
    QString url = QString("https://archive.org/services/search/v1/scrape"
                          "?q=collection:%1&fields=identifier&count=100").arg(name);

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "keeper-basecamp/0.1");
    auto* reply = nam_->get(req);

    int* queued = new int(0);
    int* cap    = new int(limit);

    connect(reply, &QNetworkReply::finished, this, [this, reply, queued, cap] {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonArray items  = doc.object().value("items").toArray();
            for (const auto& v : items) {
                if (*queued >= *cap) break;
                QString id = v.toObject().value("identifier").toString();
                if (!id.isEmpty()) {
                    preserveItem(id);
                    (*queued)++;
                }
            }
        } else {
            qDebug() << "KeeperPlugin: collection fetch error" << reply->errorString();
        }
        reply->deleteLater();
        delete queued; delete cap;
    });

    return QString(R"({"queued":"pending","collection":"%1"})").arg(name);
}

QString KeeperPlugin::cancelItem(const QString& identifier)
{
    for (auto& item : queue_) {
        if (item.identifier == identifier) {
            item.status = "cancelled";
            saveQueue();
            return R"({"success":true})";
        }
    }
    return R"({"success":false,"error":"not found"})";
}

QString KeeperPlugin::getQueue()
{
    return itemsToJson(queue_);
}

QString KeeperPlugin::getLog()
{
    QJsonArray arr;
    for (const auto& obj : log_) arr.append(obj);
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QString KeeperPlugin::getConfig()
{
    return QString(R"({"maxFilesPerItem":%1,"skipDerivatives":%2})")
        .arg(maxFilesPerItem_)
        .arg(skipDerivatives_ ? "true" : "false");
}

QString KeeperPlugin::setConfig(const QString& json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return R"({"success":false,"error":"invalid json"})";
    QJsonObject obj = doc.object();
    if (obj.contains("maxFilesPerItem"))  maxFilesPerItem_  = obj["maxFilesPerItem"].toInt(maxFilesPerItem_);
    if (obj.contains("skipDerivatives"))  skipDerivatives_  = obj["skipDerivatives"].toBool(skipDerivatives_);
    return R"({"success":true})";
}

// ── Queue engine ─────────────────────────────────────────────────────────────

void KeeperPlugin::advanceQueue()
{
    if (busy_) return;

    // Find the next queued item
    for (auto& item : queue_) {
        if (item.status == "queued") {
            item.status = "active";
            busy_       = true;
            fetchMetadata(item.identifier);
            return;
        }
    }
}

void KeeperPlugin::fetchMetadata(const QString& identifier)
{
    QString url = QString("https://archive.org/metadata/%1").arg(identifier);
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "keeper-basecamp/0.1");
    auto* reply = nam_->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, identifier] {
        // Find the active item
        KeeperItem* item = nullptr;
        for (auto& i : queue_)
            if (i.identifier == identifier) { item = &i; break; }

        if (!item || item->status == "cancelled") {
            reply->deleteLater();
            busy_ = false;
            advanceQueue();
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            item->status = "failed";
            emitEvent("itemFailed", {QVariantMap{{"id", identifier}, {"error", reply->errorString()}}});
            reply->deleteLater();
            busy_ = false;
            saveQueue();
            advanceQueue();
            return;
        }

        QJsonDocument doc  = QJsonDocument::fromJson(reply->readAll());
        QJsonObject   root = doc.object();
        reply->deleteLater();

        // Title
        QString title = root.value("metadata").toObject().value("title").toString();
        if (!title.isEmpty()) item->title = title;

        // File list — originals only (skip derivatives)
        QJsonArray files = root.value("files").toArray();
        int count = 0;
        for (const auto& fv : files) {
            QJsonObject f = fv.toObject();
            if (skipDerivatives_ && f.value("source").toString() != "original") continue;
            if (count >= maxFilesPerItem_) break;
            KeeperFile kf;
            kf.name   = f.value("name").toString();
            kf.size   = f.value("size").toString();
            kf.status = "pending";
            item->files.append(kf);
            count++;
        }

        if (item->files.isEmpty()) {
            item->status = "failed";
            emitEvent("itemFailed", {QVariantMap{{"id", identifier}, {"error", "no original files found"}}});
            busy_ = false;
            saveQueue();
            advanceQueue();
            return;
        }

        emitEvent("itemQueued", {QVariantMap{
            {"id", identifier}, {"title", item->title}, {"fileCount", item->files.size()}
        }});

        saveQueue();
        item->currentFile = 0;
        processNextFile();
    });
}

void KeeperPlugin::processNextFile()
{
    // Find the active item
    KeeperItem* item = nullptr;
    for (auto& i : queue_)
        if (i.status == "active") { item = &i; break; }

    if (!item || item->status == "cancelled") {
        busy_ = false;
        advanceQueue();
        return;
    }

    // Advance past already-done files
    while (item->currentFile < item->files.size() &&
           item->files[item->currentFile].status == "done")
        item->currentFile++;

    if (item->currentFile >= item->files.size()) {
        // All files done
        item->status = "done";
        appendLog(*item);
        // Remove from queue
        queue_.removeIf([&](const KeeperItem& i){ return i.identifier == item->identifier; });
        saveQueue();
        busy_ = false;
        advanceQueue();
        return;
    }

    KeeperFile& f = item->files[item->currentFile];
    f.status = "downloading";
    emitEvent("itemProgress", {QVariantMap{
        {"id", item->identifier}, {"file", f.name}, {"phase", "download"}, {"pct", 0}
    }});
    downloadFile(item->identifier, f);
}

void KeeperPlugin::downloadFile(const QString& identifier, const KeeperFile& file)
{
    QString url = QString("https://archive.org/download/%1/%2")
                      .arg(identifier, file.name);
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "keeper-basecamp/0.1");
    auto* reply = nam_->get(req);

    // Temp file
    QString tmpPath = QDir::tempPath() + QString("/keeper-%1-%2").arg(identifier, file.name);
    tmpPath.replace('/', '-');
    tmpPath = QDir::tempPath() + "/" + tmpPath;
    auto* f = new QFile(tmpPath, this);
    f->open(QIODevice::WriteOnly);

    connect(reply, &QNetworkReply::readyRead, this, [reply, f]{ f->write(reply->readAll()); });

    connect(reply, &QNetworkReply::downloadProgress, this,
        [this, identifier, &file](qint64 recv, qint64 total) {
            int pct = total > 0 ? int(recv * 100 / total) : 0;
            emitEvent("itemProgress", {QVariantMap{
                {"id", identifier}, {"file", file.name}, {"phase", "download"}, {"pct", pct}
            }});
        });

    QString fileName = file.name;
    connect(reply, &QNetworkReply::finished, this, [this, reply, f, tmpPath, identifier, fileName] {
        f->close();
        reply->deleteLater();

        KeeperItem* item = nullptr;
        for (auto& i : queue_)
            if (i.identifier == identifier) { item = &i; break; }

        if (!item || item->status == "cancelled") {
            QFile::remove(tmpPath);
            delete f;
            busy_ = false;
            advanceQueue();
            return;
        }

        KeeperFile* kf = nullptr;
        for (auto& ff : item->files)
            if (ff.name == fileName) { kf = &ff; break; }

        if (reply->error() != QNetworkReply::NoError) {
            if (kf) { kf->status = "failed"; kf->error = reply->errorString(); }
            QFile::remove(tmpPath);
            delete f;
            // Skip to next file rather than failing the whole item
            item->currentFile++;
            processNextFile();
            return;
        }

        delete f;
        if (kf) kf->status = "uploading";
        emitEvent("itemProgress", {QVariantMap{
            {"id", identifier}, {"file", fileName}, {"phase", "upload"}, {"pct", 0}
        }});
        uploadToStash(identifier, tmpPath, fileName);
    });
}

void KeeperPlugin::uploadToStash(const QString& identifier, const QString& localPath,
                                  const QString& fileName)
{
    if (!stashClient_ && logosAPI)
        stashClient_ = logosAPI->getClient("stash");
    if (!stashClient_) {
        qDebug() << "KeeperPlugin: no stash client";
        KeeperItem* item = nullptr;
        for (auto& i : queue_)
            if (i.identifier == identifier) { item = &i; break; }
        if (item) item->currentFile++;
        QFile::remove(localPath);
        processNextFile();
        return;
    }

    QString result = stashClient_->invokeRemoteMethod("stash", "upload", localPath, "keeper").toString();
    QFile::remove(localPath);

    QJsonDocument doc = QJsonDocument::fromJson(result.toUtf8());
    QString cid;
    if (doc.isObject()) cid = doc.object().value("cid").toString();

    KeeperItem* item = nullptr;
    for (auto& i : queue_)
        if (i.identifier == identifier) { item = &i; break; }

    if (!item) return;

    KeeperFile* kf = nullptr;
    for (auto& ff : item->files)
        if (ff.name == fileName) { kf = &ff; break; }

    if (cid.isEmpty()) {
        if (kf) { kf->status = "failed"; kf->error = "upload returned no CID"; }
        item->currentFile++;
        processNextFile();
        return;
    }

    if (kf) { kf->cid = cid; kf->status = "inscribing"; }
    saveQueue();
    inscribeToBeacon(identifier, fileName, cid);
}

void KeeperPlugin::inscribeToBeacon(const QString& identifier, const QString& fileName,
                                     const QString& cid)
{
    QString label = QString("ia:%1/%2").arg(identifier, fileName);

    if (!beaconClient_ && logosAPI)
        beaconClient_ = logosAPI->getClient("beacon");
    if (beaconClient_) {
        beaconClient_->invokeRemoteMethod("beacon", "pinCid", cid, label);
    } else {
        qDebug() << "KeeperPlugin: no beacon client — skipping inscription for" << cid;
    }

    KeeperItem* item = nullptr;
    for (auto& i : queue_)
        if (i.identifier == identifier) { item = &i; break; }

    if (!item) return;

    KeeperFile* kf = nullptr;
    for (auto& ff : item->files)
        if (ff.name == fileName) { kf = &ff; break; }

    if (kf) kf->status = "done";

    emitEvent("itemPreserved", {QVariantMap{{"id", identifier}, {"file", fileName}, {"cid", cid}}});
    saveQueue();

    item->currentFile++;
    processNextFile();
}

// ── Helpers ──────────────────────────────────────────────────────────────────

QString KeeperPlugin::parseIdentifier(const QString& urlOrId)
{
    // archive.org/details/{id}  or  archive.org/download/{id}  or  bare id
    QString s = urlOrId.trimmed();
    QRegularExpression re(R"(archive\.org/(?:details|download)/([^/?#\s]+))");
    auto m = re.match(s);
    if (m.hasMatch()) return m.captured(1);
    // bare identifier: no slashes, no dots that look like a domain
    if (!s.contains('/') && !s.contains(' ')) return s;
    return {};
}

void KeeperPlugin::emitEvent(const QString& name, const QVariantList& data)
{
    if (logosAPI)
        if (auto* c = logosAPI->getClient("keeper"))
            c->onEventResponse(this, name, data);
}

QString KeeperPlugin::itemsToJson(const QList<KeeperItem>& items)
{
    QJsonArray arr;
    for (const auto& item : items) {
        QJsonObject obj;
        obj["id"]     = item.identifier;
        obj["title"]  = item.title;
        obj["status"] = item.status;
        QJsonArray files;
        for (const auto& f : item.files) {
            QJsonObject fo;
            fo["name"]   = f.name;
            fo["cid"]    = f.cid;
            fo["status"] = f.status;
            if (!f.error.isEmpty()) fo["error"] = f.error;
            files.append(fo);
        }
        obj["files"] = files;
        arr.append(obj);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── Persistence ───────────────────────────────────────────────────────────────

QString KeeperPlugin::persistPath(const QString& filename)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + "/keeper";
    QDir().mkpath(base);
    return base + "/" + filename;
}

void KeeperPlugin::loadQueue()
{
    QFile f(persistPath("queue.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;
    for (const auto& v : doc.array()) {
        QJsonObject obj = v.toObject();
        KeeperItem item;
        item.identifier  = obj["id"].toString();
        item.title       = obj["title"].toString();
        item.status      = obj["status"].toString();
        item.currentFile = obj["currentFile"].toInt();
        for (const auto& fv : obj["files"].toArray()) {
            QJsonObject fo = fv.toObject();
            KeeperFile kf;
            kf.name   = fo["name"].toString();
            kf.cid    = fo["cid"].toString();
            kf.status = fo["status"].toString();
            kf.error  = fo["error"].toString();
            item.files.append(kf);
        }
        // Reset any mid-flight states so they resume cleanly
        if (item.status == "active") item.status = "queued";
        for (auto& kf : item.files)
            if (kf.status == "downloading" || kf.status == "uploading" || kf.status == "inscribing")
                kf.status = "pending";
        queue_.append(item);
    }
}

void KeeperPlugin::saveQueue()
{
    QJsonArray arr;
    for (const auto& item : queue_) {
        QJsonObject obj;
        obj["id"]          = item.identifier;
        obj["title"]       = item.title;
        obj["status"]      = item.status;
        obj["currentFile"] = item.currentFile;
        QJsonArray files;
        for (const auto& f : item.files) {
            QJsonObject fo;
            fo["name"]   = f.name;
            fo["cid"]    = f.cid;
            fo["status"] = f.status;
            fo["error"]  = f.error;
            files.append(fo);
        }
        obj["files"] = files;
        arr.append(obj);
    }
    QFile f(persistPath("queue.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void KeeperPlugin::appendLog(const KeeperItem& item)
{
    QJsonObject obj;
    obj["id"]     = item.identifier;
    obj["title"]  = item.title;
    QJsonArray files;
    for (const auto& f : item.files) {
        if (f.status == "done")
            files.append(QJsonObject{{"name", f.name}, {"cid", f.cid}});
    }
    obj["files"] = files;
    obj["ts"]    = QDateTime::currentMSecsSinceEpoch();
    log_.prepend(obj);
    while (log_.size() > 200) log_.removeLast();

    QJsonArray arr;
    for (const auto& o : log_) arr.append(o);
    QFile f(persistPath("keeper-log.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
