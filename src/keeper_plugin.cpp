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
    logosAPI = api;
    loadQueue();

    // Start HTTP bridge for the Chrome extension (localhost:7355)
    httpBridge_ = new KeeperHttpBridge(this, this);
    httpBridge_->listen(7355);

    // Defer client init + queue resume so the event loop is running.
    // Pre-initialising stashClient_ and beaconClient_ HERE (before any
    // downloads start) avoids calling getClient() after download callbacks
    // and QRO event emissions, where std::bad_alloc has been observed.
    QTimer::singleShot(2000, this, [this]{
        if (logosAPI) {
            stashClient_  = logosAPI->getClient("stash");
            beaconClient_ = logosAPI->getClient("logos_beacon");
        }
        advanceQueue();
    });
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
        f->write(reply->readAll());
    });

    QString fileName = file.name;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [this, identifier, fileName](qint64 recv, qint64 total) {
            int pct = total > 0 ? int(recv * 100 / total) : 0;
            emitEvent("itemProgress", {QVariantMap{
                {"id", identifier}, {"file", fileName}, {"phase", "download"}, {"pct", pct}
            }});
        });

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
        // Defer uploadToStash so this callback returns to the event loop first.
        QString id2   = identifier;
        QString path2 = tmpPath;
        QString name2 = fileName;
        QTimer::singleShot(0, this, [this, id2, path2, name2](){
            uploadToStash(id2, path2, name2);
        });
    });
}

void KeeperPlugin::uploadToStash(const QString& identifier, const QString& localPath,
                                  const QString& fileName)
{
    // NOTE: do NOT delete localPath here — stash defers the upload via QTimer::singleShot(0).
    // Deleting the file before stash's deferred upload runs causes "file does not exist".
    // Files in /tmp are cleaned up by the OS; we also clean them in finishItem.

    // stashClient_ is pre-initialised in initLogos; this guard is a safety net only.
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
        stashClient_->invokeRemoteMethod("stash", "upload", localPath, "keeper");
        // Returns {"queued":true} — actual upload is async; we do NOT wait for CID here.
        // Collection CID is obtained via uploadCollectionManifest after all files are done.
        if (kf) kf->status = "done";
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

    // stashClient_ is pre-initialised in initLogos; this guard is a safety net only.
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

    // beaconClient_ is pre-initialised in initLogos; this guard is a safety net only.
    if (!beaconClient_ && logosAPI)
        beaconClient_ = logosAPI->getClient("logos_beacon");
    if (beaconClient_) {
        // Pass source="keeper" so beacon deduplicates correctly:
        // the QML stash watcher would otherwise re-register the same CID as source="notes".
        beaconClient_->invokeRemoteMethod("logos_beacon", "pinCid", cid, label, QString("keeper"));
    } else {
        qDebug() << "KeeperPlugin: no beacon client — skipping inscription for" << cid;
    }

    emitEvent("itemPreserved", {QVariantMap{{"id", identifier}, {"cid", cid}}});
    finishItem(identifier, cid);

    // Poll beacon for the tx hash (inscriptionId) once the inscription confirms.
    // pinCid queues async — the tx hash is set later by confirmInscription.
    if (beaconClient_ && !cid.isEmpty()) {
        QString cidCopy = cid;
        QString idCopy  = identifier;
        QTimer::singleShot(5000, this, [this, idCopy, cidCopy]() {
            pollForTxHash(idCopy, cidCopy, 24); // up to 24 × 5s = 2 min
        });
    }
}

void KeeperPlugin::pollForTxHash(const QString& identifier, const QString& cid, int attempts)
{
    if (!beaconClient_) return;

    QString logJson = beaconClient_->invokeRemoteMethod("logos_beacon", "getInscriptionLog").toString();
    QJsonArray entries = QJsonDocument::fromJson(logJson.toUtf8()).array();

    for (const auto& v : entries) {
        QJsonObject e = v.toObject();
        if (e["cid"].toString() == cid) {
            QString txHash = e["inscriptionId"].toString();
            if (!txHash.isEmpty()) {
                // Update the log entry with the tx hash and re-save
                for (auto& obj : log_) {
                    if (obj["id"].toString() == identifier) {
                        obj["txHash"] = txHash;
                        break;
                    }
                }
                QJsonArray arr;
                for (const auto& o : log_) arr.append(o);
                QFile f(persistPath("keeper-log.json"));
                if (f.open(QIODevice::WriteOnly))
                    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
                emitEvent("logUpdated", {QVariantMap{{"id", identifier}, {"txHash", txHash}}});
                return;
            }
            break; // found the entry but no tx hash yet — keep polling
        }
    }

    if (attempts > 0) {
        QTimer::singleShot(5000, this, [this, identifier, cid, attempts]() {
            pollForTxHash(identifier, cid, attempts - 1);
        });
    }
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
    // Load completed log
    QFile lf(persistPath("keeper-log.json"));
    if (lf.open(QIODevice::ReadOnly)) {
        QJsonDocument ldoc = QJsonDocument::fromJson(lf.readAll());
        if (ldoc.isArray())
            for (const auto& v : ldoc.array())
                log_.append(v.toObject());
    }

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
    obj["id"]    = item.identifier;
    obj["title"] = item.title;
    obj["ts"]    = QDateTime::currentMSecsSinceEpoch();
    if (!item.collectionCid.isEmpty())
        obj["collectionCid"] = item.collectionCid;

    qint64 totalBytes = 0;
    QJsonArray files;
    for (const auto& f : item.files) {
        if (f.status == "done") {
            files.append(QJsonObject{{"name", f.name}, {"cid", f.cid}});
            totalBytes += f.size.toLongLong();
        }
    }
    obj["files"]     = files;
    obj["totalSize"] = totalBytes;

    log_.append(obj);
    while (log_.size() > 200) log_.removeFirst();

    QJsonArray arr;
    for (const auto& o : log_) arr.append(o);
    QFile f(persistPath("keeper-log.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
