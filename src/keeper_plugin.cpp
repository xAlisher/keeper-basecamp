#include "keeper_plugin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QUrl>


// ── Lifecycle ────────────────────────────────────────────────────────────────

KeeperPlugin::KeeperPlugin()
    : nam_(new QNetworkAccessManager(this))
{}

void KeeperPlugin::initLogos(LogosAPI* api)
{
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "initLogos called\n"; }
    logosAPI = api;
    loadQueue();
    // Defer queue resume + client init so the event loop is running
    QTimer::singleShot(2000, this, [this]{ advanceQueue(); });
    qDebug() << "KeeperPlugin: initialized";
}

// ── Public API ───────────────────────────────────────────────────────────────

QString KeeperPlugin::preserveItem(const QString& urlOrId)
{
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "preserveItem called: " << urlOrId << "\n"; }
    QString id = parseIdentifier(urlOrId);
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "parsed id: [" << id << "]\n"; }
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

    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "before emitEvent\n"; }
    emitEvent("itemQueued", {QVariantMap{{"id", id}, {"title", id}}});
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "after emitEvent\n"; }
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
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "getQueue called\n"; }
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
        auto dbg = [](const char* msg){ QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << msg << "\n"; };
        dbg("fetchMetadata lambda start");

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
            dbg("fetchMetadata network error");
            item->status = "failed";
            emitEvent("itemFailed", {QVariantMap{{"id", identifier}, {"error", reply->errorString()}}});
            reply->deleteLater();
            busy_ = false;
            saveQueue();
            advanceQueue();
            return;
        }

        dbg("fetchMetadata parsing response");
        QByteArray rawMeta = reply->readAll();
        // Save raw IA metadata for collection manifest upload
        { QFile mf(QDir::tempPath() + "/keeper-" + identifier + "-metadata.json");
          if (mf.open(QIODevice::WriteOnly)) { mf.write(rawMeta); mf.close(); } }
        QJsonDocument doc  = QJsonDocument::fromJson(rawMeta);
        QJsonObject   root = doc.object();
        reply->deleteLater();

        QString title = root.value("metadata").toObject().value("title").toString();
        if (!title.isEmpty()) item->title = title;

        // Clear any files from previous run before re-populating
        item->files.clear();

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

        for (const auto& kf : item->files)
            dbg(QString("  file: [%1]").arg(kf.name).toUtf8().constData());
        dbg(QString("fetchMetadata found %1 files").arg(item->files.size()).toUtf8().constData());

        if (item->files.isEmpty()) {
            item->status = "failed";
            emitEvent("itemFailed", {QVariantMap{{"id", identifier}, {"error", "no original files found"}}});
            busy_ = false;
            saveQueue();
            advanceQueue();
            return;
        }

        dbg("fetchMetadata emitting itemQueued");
        emitEvent("itemQueued", {QVariantMap{
            {"id", identifier}, {"title", item->title}, {"fileCount", item->files.size()}
        }});
        dbg("fetchMetadata after emitEvent, calling saveQueue");
        saveQueue();
        item->currentFile = 0;
        dbg("fetchMetadata calling processNextFile");
        processNextFile();
        dbg("fetchMetadata done");
    });
}

void KeeperPlugin::processNextFile()
{
    { QFile f("/tmp/keeper_debug.log"); if(f.open(QIODevice::Append|QIODevice::Text)) QTextStream(&f) << "processNextFile start\n"; }
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
        // All files downloaded — upload IA metadata as collection manifest, then inscribe one CID
        item->status = "inscribing";
        saveQueue();
        uploadCollectionManifest(item->identifier);
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

    // Temp file — sanitize only the filename component, never the directory
    QString safeId   = QString(identifier).replace('/', '_');
    QString safeName = QString(file.name).replace('/', '_');
    QString tmpPath  = QDir::tempPath() + QString("/keeper-%1-%2").arg(safeId, safeName);
    auto* f = new QFile(tmpPath, this);
    f->open(QIODevice::WriteOnly);

    connect(reply, &QNetworkReply::readyRead, this, [reply, f]{
        { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text)) QTextStream(&dbg) << "readyRead fired\n"; }
        f->write(reply->readAll());
        { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text)) QTextStream(&dbg) << "readyRead done\n"; }
    });

    QString fileName = file.name;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [this, identifier, fileName](qint64 recv, qint64 total) {
            { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text)) QTextStream(&dbg) << "downloadProgress fired recv=" << recv << " total=" << total << "\n"; }
            int pct = total > 0 ? int(recv * 100 / total) : 0;
            emitEvent("itemProgress", {QVariantMap{
                {"id", identifier}, {"file", fileName}, {"phase", "download"}, {"pct", pct}
            }});
            { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text)) QTextStream(&dbg) << "downloadProgress done\n"; }
        });
    connect(reply, &QNetworkReply::finished, this, [this, reply, f, tmpPath, identifier, fileName] {
        { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text)) QTextStream(&dbg) << "download finished callback, error=" << reply->error() << "\n"; }
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
    // NOTE: do NOT delete localPath here — stash defers the upload via QTimer::singleShot(0).
    // Deleting the file before stash's deferred upload runs causes "file does not exist".
    // Files in /tmp are cleaned up by the OS; we also clean them in finishItem.

    if (!stashClient_ && logosAPI)
        stashClient_ = logosAPI->getClient("stash");

    KeeperItem* item = nullptr;
    for (auto& i : queue_)
        if (i.identifier == identifier) { item = &i; break; }
    if (!item) return;

    KeeperFile* kf = nullptr;
    for (auto& ff : item->files)
        if (ff.name == fileName) { kf = &ff; break; }

    if (stashClient_) {
        { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text))
            QTextStream(&dbg) << "uploadToStash: path=[" << localPath << "] exists="
                              << QFile::exists(localPath) << " size=" << QFileInfo(localPath).size() << "\n"; }
        stashClient_->invokeRemoteMethod("stash", "upload", localPath, "keeper");
        // Returns {"queued":true} — actual upload is async; we do NOT wait for CID here.
        // Collection CID is obtained via uploadCollectionManifest after all files are done.
        if (kf) kf->status = "uploaded";
    } else {
        if (kf) { kf->status = "failed"; kf->error = "no stash client"; }
    }

    item->currentFile++;
    processNextFile();
}

void KeeperPlugin::uploadCollectionManifest(const QString& identifier)
{
    // Upload the raw IA metadata JSON saved during fetchMetadata.
    // This single file represents the whole collection; its CID is inscribed to beacon.
    QString manifestPath = QDir::tempPath() + "/keeper-" + identifier + "-metadata.json";
    { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text))
        QTextStream(&dbg) << "uploadCollectionManifest: path=[" << manifestPath
                          << "] exists=" << QFile::exists(manifestPath) << "\n"; }

    if (!stashClient_ && logosAPI)
        stashClient_ = logosAPI->getClient("stash");

    if (!stashClient_ || !QFile::exists(manifestPath)) {
        finishItem(identifier, {});
        return;
    }

    stashClient_->invokeRemoteMethod("stash", "upload", manifestPath, "keeper");
    // Poll for the resulting CID (stash stores it in getLatestLogosResult after async upload)
    QString manifestFile = QFileInfo(manifestPath).fileName();
    pollForManifestCid(identifier, manifestFile, 60);
}

void KeeperPlugin::pollForManifestCid(const QString& identifier, const QString& manifestFile,
                                       int attempts)
{
    if (!stashClient_ && logosAPI)
        stashClient_ = logosAPI->getClient("stash");

    if (stashClient_) {
        QString latestJson = stashClient_->invokeRemoteMethod("stash", "getLatestLogosResult").toString();
        QJsonObject latest = QJsonDocument::fromJson(latestJson.toUtf8()).object();
        QString latestFile = QFileInfo(latest.value("file").toString()).fileName();
        QString latestCid  = latest.value("cid").toString();

        { QFile dbg("/tmp/keeper_debug.log"); if(dbg.open(QIODevice::Append|QIODevice::Text))
            QTextStream(&dbg) << "pollForManifestCid: attempts=" << attempts
                              << " latestFile=[" << latestFile << "] wantFile=[" << manifestFile
                              << "] cid=[" << latestCid << "]\n"; }

        if (latestFile == manifestFile && !latestCid.isEmpty()) {
            inscribeToBeacon(identifier, latestCid);
            return;
        }
    }

    if (attempts <= 0) {
        finishItem(identifier, {});
        return;
    }

    QTimer::singleShot(1000, this, [this, identifier, manifestFile, attempts]() {
        pollForManifestCid(identifier, manifestFile, attempts - 1);
    });
}

void KeeperPlugin::inscribeToBeacon(const QString& identifier, const QString& cid)
{
    QString label = QString("ia:%1").arg(identifier);

    if (!beaconClient_ && logosAPI)
        beaconClient_ = logosAPI->getClient("beacon");
    if (beaconClient_) {
        // Pass source="keeper" so beacon deduplicates correctly:
        // the QML stash watcher would otherwise re-register the same CID as source="notes".
        beaconClient_->invokeRemoteMethod("beacon", "pinCid", cid, label, QString("keeper"));
    } else {
        qDebug() << "KeeperPlugin: no beacon client — skipping inscription for" << cid;
    }

    emitEvent("itemPreserved", {QVariantMap{{"id", identifier}, {"cid", cid}}});
    finishItem(identifier, cid);
}

void KeeperPlugin::finishItem(const QString& identifier, const QString& collectionCid)
{
    // Clean up metadata temp file
    QFile::remove(QDir::tempPath() + "/keeper-" + identifier + "-metadata.json");

    for (auto& i : queue_) {
        if (i.identifier == identifier) {
            i.status       = collectionCid.isEmpty() ? "failed" : "done";
            i.collectionCid = collectionCid;
            appendLog(i);
            break;
        }
    }
    queue_.removeIf([&](const KeeperItem& i){ return i.identifier == identifier; });
    saveQueue();
    busy_ = false;
    advanceQueue();
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
    emit eventResponse(name, data);
}

QString KeeperPlugin::itemsToJson(const QList<KeeperItem>& items)
{
    QJsonArray arr;
    for (const auto& item : items) {
        QJsonObject obj;
        obj["id"]     = item.identifier;
        obj["title"]  = item.title;
        obj["status"] = item.status;
        if (!item.collectionCid.isEmpty()) obj["collectionCid"] = item.collectionCid;
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
        item.identifier   = obj["id"].toString();
        item.title        = obj["title"].toString();
        item.status       = obj["status"].toString();
        item.collectionCid = obj["collectionCid"].toString();
        item.currentFile  = obj["currentFile"].toInt();
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
        if (item.status == "active" || item.status == "inscribing") item.status = "queued";
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
        obj["id"]           = item.identifier;
        obj["title"]        = item.title;
        obj["status"]       = item.status;
        obj["collectionCid"] = item.collectionCid;
        obj["currentFile"]  = item.currentFile;
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
