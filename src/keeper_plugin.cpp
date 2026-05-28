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
#include <QDateTime>

#include <sodium.h>


// ── Lifecycle ────────────────────────────────────────────────────────────────

KeeperPlugin::KeeperPlugin()
    : nam_(new QNetworkAccessManager(this))
{}

void KeeperPlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;

    // Persistence path injected by platform (same pattern as beacon).
    QVariant prop = property("instancePersistencePath");
    if (prop.isValid() && !prop.toString().isEmpty())
        m_persistencePath = prop.toString();
    else
        m_persistencePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + "/keeper";
    QDir().mkpath(m_persistencePath);

    loadQueue();

    // Defer client init + queue resume so the event loop is running.
    // Pre-initialising stashClient_ and beaconClient_ HERE (before any
    // downloads start) avoids calling getClient() after download callbacks
    // and QRO event emissions, where std::bad_alloc has been observed.
    QTimer::singleShot(2000, this, [this]{
        if (logosAPI) {
            stashClient_  = logosAPI->getClient("stash");
            beaconClient_ = logosAPI->getClient("logos_beacon");
            initDeliveryModule();
        }
        advanceQueue();
        // Resume tx hash polling for log entries that have a collection CID but no tx hash yet
        // (handles crash/restart while "confirming…")
        for (const auto& obj : std::as_const(log_)) {
            QString id     = obj["id"].toString();
            QString cid    = obj["collectionCid"].toString();
            QString txHash = obj["txHash"].toString();
            if (!id.isEmpty() && !cid.isEmpty() && txHash.isEmpty()) {
                QString idCopy  = id;
                QString cidCopy = cid;
                QTimer::singleShot(3000, this, [this, idCopy, cidCopy]() {
                    pollForTxHash(idCopy, cidCopy, 72);
                });
            }
        }
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
    publishStatus(id, "queued");
    qDebug() << "KeeperPlugin: queued" << id;

    if (!busy_) advanceQueue();
    return QString(R"({"success":true,"id":"%1"})").arg(id);
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

QString KeeperPlugin::clearLog()
{
    log_.clear();
    const QString path = persistPath("keeper-log.json");
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "KeeperPlugin: failed to remove log file:" << path;
        return R"({"success":false,"error":"log file remove failed"})";
    }
    return R"({"success":true})";
}

QString KeeperPlugin::clearQueue()
{
    if (busy_)
        return R"({"success":false,"error":"cannot clear queue while an item is active"})";
    queue_.clear();
    const QString path = persistPath("keeper-queue.json");
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "KeeperPlugin: failed to remove queue file:" << path;
        return R"({"success":false,"error":"queue file remove failed"})";
    }
    return R"({"success":true})";
}

QString KeeperPlugin::getLogosMsgStatus() const
{
    QJsonObject o;
    o[QStringLiteral("status")]      = lmStatus_.isEmpty() ? QStringLiteral("offline") : lmStatus_;
    o[QStringLiteral("ready")]       = lmReady_;
    o[QStringLiteral("pairedCount")] = m_pairedPubkeys.size();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString KeeperPlugin::getInscriptionQueue() const
{
    QJsonArray arr;
    for (const auto& e : inscriptionQueue_) arr.append(e);
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QString KeeperPlugin::markInscribed(const QString& cid)
{
    inscriptionQueue_.removeIf([&](const QJsonObject& e){ return e["cid"].toString() == cid; });
    saveInscriptionQueue();
    return R"({"ok":true})";
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
        // All files uploaded — inscribe directly with ia:{identifier} as the stable key
        item->status = "inscribing";
        saveQueue();
        inscribeToBeacon(item->identifier, "ia:" + item->identifier);
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
        // Upload is async — poll getLatestLogosResult until we see this file's CID.
        if (kf) kf->status = "uploading";
        QString id2   = identifier;
        QString name2 = fileName;
        QString path2 = localPath;
        QTimer::singleShot(2000, this, [this, id2, name2, path2]() {
            pollForFileCid(id2, name2, path2, 300);
        });
    } else {
        // Stash not yet loaded — retry in 5 s rather than failing immediately.
        qDebug() << "KeeperPlugin: stash not ready, retrying upload in 5s for" << fileName;
        QString id2   = identifier;
        QString path2 = localPath;
        QString name2 = fileName;
        QTimer::singleShot(5000, this, [this, id2, path2, name2]() {
            uploadToStash(id2, path2, name2);
        });
    }
}

void KeeperPlugin::pollForFileCid(const QString& identifier, const QString& fileName,
                                   const QString& tmpPath, int attempts)
{
    if (!stashClient_ && logosAPI)
        stashClient_ = logosAPI->getClient("stash");

    if (stashClient_) {
        QString latestJson = stashClient_->invokeRemoteMethod("stash", "getLatestLogosResult").toString();
        QJsonObject latest = QJsonDocument::fromJson(latestJson.toUtf8()).object();
        QString latestFile = QFileInfo(latest.value("file").toString()).fileName();
        QString latestCid  = latest.value("cid").toString();

        if (latestFile == QFileInfo(tmpPath).fileName() && !latestCid.isEmpty()) {
            KeeperItem* item = nullptr;
            for (auto& i : queue_)
                if (i.identifier == identifier) { item = &i; break; }
            if (item) {
                for (auto& ff : item->files) {
                    if (ff.name == fileName) {
                        ff.cid    = latestCid;
                        ff.status = "done";
                        break;
                    }
                }
                saveQueue();
                item->currentFile++;
            }
            QFile::remove(tmpPath);
            processNextFile();
            return;
        }
    }

    if (attempts <= 0) {
        // Give up on this file's CID — mark done without CID and advance
        KeeperItem* item = nullptr;
        for (auto& i : queue_)
            if (i.identifier == identifier) { item = &i; break; }
        if (item) {
            for (auto& ff : item->files) {
                if (ff.name == fileName) { ff.status = "done"; break; }
            }
            item->currentFile++;
        }
        QFile::remove(tmpPath);
        processNextFile();
        return;
    }

    QTimer::singleShot(2000, this, [this, identifier, fileName, tmpPath, attempts]() {
        pollForFileCid(identifier, fileName, tmpPath, attempts - 1);
    });
}

void KeeperPlugin::inscribeToBeacon(const QString& identifier, const QString& cid)
{
    // Build rich label from the item still in queue_
    QString label;
    for (const auto& item : std::as_const(queue_)) {
        if (item.identifier != identifier) continue;
        QJsonObject labelObj;
        labelObj["module"] = "keeper";
        labelObj["source"] = "internet_archive";
        labelObj["id"]     = identifier;
        if (!item.title.isEmpty() && item.title != identifier)
            labelObj["title"] = item.title;
        qint64 totalSize = 0;
        QJsonArray fileArr;
        for (const auto& f : item.files) {
            if (f.status == "done" && !f.cid.isEmpty())
                fileArr.append(QJsonObject{{"name", f.name}, {"cid", f.cid}});
            totalSize += f.size.toLongLong();
        }
        if (totalSize > 0) labelObj["totalSize"] = totalSize;
        if (!fileArr.isEmpty()) labelObj["files"] = fileArr;
        label = QJsonDocument(labelObj).toJson(QJsonDocument::Compact);
        break;
    }
    if (label.isEmpty())
        label = QString("ia:%1").arg(identifier);

    // Add to inscription queue — beacon polls getInscriptionQueue() and inscribes.
    QJsonObject entry;
    entry["cid"]   = cid;
    entry["label"] = label;
    inscriptionQueue_.append(entry);
    saveInscriptionQueue();

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
    // Clean up temp files
    QFile::remove(QDir::tempPath() + "/keeper-" + identifier + "-metadata.json");

    for (auto& i : queue_) {
        if (i.identifier == identifier) {
            i.status       = collectionCid.isEmpty() ? "failed" : "done";
            i.collectionCid = collectionCid;
            appendLog(i);
            publishStatus(identifier, i.status, collectionCid);
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
    return m_persistencePath + "/" + filename;
}

void KeeperPlugin::saveInscriptionQueue()
{
    QJsonArray arr;
    for (const auto& e : inscriptionQueue_) arr.append(e);
    QFile f(persistPath("keeper-inscription-queue.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void KeeperPlugin::loadQueue()
{
    // Load paired extension pubkeys
    QFile pf(persistPath("keeper-paired-extensions.json"));
    if (pf.open(QIODevice::ReadOnly)) {
        QJsonDocument pdoc = QJsonDocument::fromJson(pf.readAll());
        if (pdoc.isArray())
            for (const auto& v : pdoc.array())
                m_pairedPubkeys.append(v.toString().trimmed().toLower());
    }

    // Load inscription queue (survives crash/restart)
    QFile iqf(persistPath("keeper-inscription-queue.json"));
    if (iqf.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(iqf.readAll());
        if (doc.isArray())
            for (const auto& v : doc.array())
                inscriptionQueue_.append(v.toObject());
    }

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
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "KeeperPlugin: failed to open queue file for writing:" << f.errorString();
        return;
    }
    const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    if (f.write(data) != data.size())
        qWarning() << "KeeperPlugin: short write on queue file:" << f.errorString();
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
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "KeeperPlugin: failed to open log file for writing:" << f.errorString();
        return;
    }
    const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    if (f.write(data) != data.size())
        qWarning() << "KeeperPlugin: short write on log file:" << f.errorString();
}

// ── Logos Messaging (delivery_module) ────────────────────────────────────────

void KeeperPlugin::initDeliveryModule()
{
    deliveryClient_ = logosAPI ? logosAPI->getClient("delivery_module") : nullptr;
    if (!deliveryClient_) {
        qWarning() << "KeeperPlugin: delivery_module not available — retrying in 5s";
        QTimer::singleShot(5000, this, [this]{ initDeliveryModule(); });
        return;
    }

    // Random nodeKey — avoids PeerID collisions across restarts
    QString nodeKey;
    { QFile rf("/dev/urandom");
      if (rf.open(QIODevice::ReadOnly)) nodeKey = rf.read(32).toHex(); }
    if (nodeKey.size() != 64) {
        quint64 t = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
        nodeKey = QString("%1%2").arg(t, 16, 16, QChar('0')).arg(~t, 16, 16, QChar('0'));
    }

    QString cfg = QString(
        R"({"logLevel":"INFO","mode":"Core","preset":"logos.dev","relay":true,"nodeKey":"%1"})"
    ).arg(nodeKey);

    deliveryClient_->invokeRemoteMethod("delivery_module", "createNode", cfg);

    // Register event handlers
    deliveryObject_ = deliveryClient_->requestObject("delivery_module");
    if (deliveryObject_) {
        deliveryClient_->onEvent(deliveryObject_, "messageReceived",
            [this](const QString&, const QVariantList& data) {
                onWakuMessageReceived(data);
            });
        deliveryClient_->onEvent(deliveryObject_, "connectionStateChanged",
            [this](const QString&, const QVariantList& data) {
                onWakuConnectionChanged(data);
            });
    }

    deliveryClient_->invokeRemoteMethod("delivery_module", "start");
    deliveryClient_->invokeRemoteMethod("delivery_module", "subscribe",
        QStringLiteral("/keeper/1/preserve/json"));

    qDebug() << "KeeperPlugin: Logos Messaging initialised — subscribed /keeper/1/preserve/json";
}

void KeeperPlugin::onWakuConnectionChanged(const QVariantList& data)
{
    QString state = data.size() > 0 ? data[0].toString() : QString();
    if (!state.isEmpty()) {
        lmStatus_ = state;
        lmReady_  = (state == "CONNECTED");
        qDebug() << "KeeperPlugin: Logos Messaging connection state:" << state;
    }
}

void KeeperPlugin::onWakuMessageReceived(const QVariantList& data)
{
    // data[] layout (delivery-module-messaging skill):
    //   [0] = message hash, [1] = content topic, [2] = base64(payload), [3] = timestamp ns
    if (data.size() < 3) return;

    // Single base64 decode (confirmed: delivery_module adds exactly one layer)
    QByteArray payloadBytes = QByteArray::fromBase64(data[2].toString().toLatin1());
    QJsonDocument doc = QJsonDocument::fromJson(payloadBytes);
    if (!doc.isObject()) {
        qDebug() << "KeeperPlugin: Logos Messaging — non-JSON payload, ignored";
        return;
    }
    QJsonObject msg = doc.object();

    QString action     = msg["action"].toString();
    QString identifier = msg["identifier"].toString();
    QString url        = msg["url"].toString();
    QString pubkeyHex  = msg["pubkey"].toString().toLower();  // normalise case
    QString sigB64     = msg["sig"].toString();

    if (action != "preserve" || identifier.isEmpty() || url.isEmpty() ||
        pubkeyHex.isEmpty() || sigB64.isEmpty()) {
        qDebug() << "KeeperPlugin: Logos Messaging — missing or unsupported fields, ignored";
        return;
    }

    // Timestamp freshness check (±30 seconds)
    qint64 ts  = msg["timestamp"].toVariant().toLongLong();
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (qAbs(now - ts) > 30) {
        qDebug() << "KeeperPlugin: Logos Messaging — stale/future timestamp, ignored";
        return;
    }

    // Whitelist check
    if (!m_pairedPubkeys.contains(pubkeyHex)) {
        qDebug() << "KeeperPlugin: Logos Messaging — unknown pubkey"
                 << pubkeyHex.left(16) << "..., ignored";
        return;
    }

    // Ed25519 signature verification
    // Canonical JSON must match JS JSON.stringify key insertion order:
    //   {action, identifier, url, timestamp, pubkey}
    // Build as raw string — QJsonObject serialises alphabetically (incompatible with JS order).
    auto jv = [](const QJsonValue& v) -> QByteArray {
        // Serialize a single QJsonValue via a one-element array, then strip brackets.
        return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact).mid(1).chopped(1);
    };
    QByteArray canonical =
        "{\"action\":"     + jv(msg["action"])     +
        ",\"identifier\":" + jv(msg["identifier"]) +
        ",\"url\":"        + jv(msg["url"])         +
        ",\"timestamp\":"  + jv(msg["timestamp"])   +
        ",\"pubkey\":"     + jv(msg["pubkey"])      + "}";

    QByteArray pubkeyBytes = QByteArray::fromHex(pubkeyHex.toLatin1());
    QByteArray sigBytes    = QByteArray::fromBase64(sigB64.toLatin1());

    if (pubkeyBytes.size() != static_cast<int>(crypto_sign_PUBLICKEYBYTES) ||
        sigBytes.size()    != static_cast<int>(crypto_sign_BYTES)) {
        qDebug() << "KeeperPlugin: Logos Messaging — wrong pubkey/sig byte length, ignored";
        return;
    }

    int rc = crypto_sign_verify_detached(
        reinterpret_cast<const unsigned char*>(sigBytes.constData()),
        reinterpret_cast<const unsigned char*>(canonical.constData()),
        static_cast<unsigned long long>(canonical.size()),
        reinterpret_cast<const unsigned char*>(pubkeyBytes.constData())
    );

    if (rc != 0) {
        qDebug() << "KeeperPlugin: Logos Messaging — signature verification FAILED for"
                 << identifier;
        return;
    }

    // Replay dedup — reject already-seen signatures (bounded at 1000 entries)
    if (m_seenSigs.contains(sigB64)) {
        qDebug() << "KeeperPlugin: Logos Messaging — replay detected, ignored";
        return;
    }
    constexpr int kMaxSeenSigs = 1000;
    if (m_seenSigOrder.size() >= kMaxSeenSigs)
        m_seenSigs.remove(m_seenSigOrder.takeFirst());
    m_seenSigs.insert(sigB64);
    m_seenSigOrder.append(sigB64);

    qDebug() << "KeeperPlugin: Logos Messaging — verified preserve request for" << identifier;
    preserveItem(url);
}

void KeeperPlugin::publishStatus(const QString& identifier, const QString& status,
                                  const QString& cid, double progress, const QString& error)
{
    if (!deliveryClient_ || !lmReady_) return;

    QJsonObject obj;
    obj["identifier"] = identifier;
    obj["status"]     = status;
    if (!cid.isEmpty())   obj["cid"]      = cid;
    if (progress >= 0.0)  obj["progress"] = progress;
    if (!error.isEmpty()) obj["error"]    = error;

    QString payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    deliveryClient_->invokeRemoteMethod("delivery_module", "send",
        QStringLiteral("/keeper/1/status/json"), payload);
}

// ── Paired extension management ──────────────────────────────────────────────

QString KeeperPlugin::addPairedExtension(const QString& hexPubkey)
{
    QString key = hexPubkey.trimmed().toLower();
    // Must be 64 hex chars = 32 bytes (Ed25519 public key)
    if (key.size() != 64) {
        return R"({"success":false,"error":"pubkey must be 64 hex chars (32 bytes)"})";
    }
    QByteArray check = QByteArray::fromHex(key.toLatin1());
    if (check.size() != static_cast<int>(crypto_sign_PUBLICKEYBYTES)) {
        return R"({"success":false,"error":"invalid hex pubkey"})";
    }
    if (m_pairedPubkeys.contains(key)) {
        return R"({"success":true,"note":"already paired"})";
    }
    m_pairedPubkeys.append(key);
    if (!savePairedExtensions()) {
        m_pairedPubkeys.removeLast();
        return R"({"success":false,"error":"persistence write failed"})";
    }
    qDebug() << "KeeperPlugin: paired extension" << key.left(16) << "...";
    return R"({"success":true})";
}

QString KeeperPlugin::removePairedExtension(const QString& hexPubkey)
{
    const QString key = hexPubkey.trimmed().toLower();
    const int idx = m_pairedPubkeys.indexOf(key);
    if (idx < 0) return R"({"success":false,"error":"not found"})";
    m_pairedPubkeys.removeAt(idx);
    if (!savePairedExtensions()) {
        m_pairedPubkeys.insert(idx, key);
        return R"({"success":false,"error":"persistence write failed"})";
    }
    return R"({"success":true})";
}

QString KeeperPlugin::getPairedExtensions()
{
    // Return redacted fingerprints only — full pubkeys stay internal
    QJsonArray arr;
    for (int i = 0; i < m_pairedPubkeys.size(); ++i) {
        const QString& pk = m_pairedPubkeys.at(i);
        QString fp = pk.left(8) + QStringLiteral("...") + pk.right(8);
        QJsonObject obj;
        obj[QStringLiteral("fp")]  = fp;
        obj[QStringLiteral("idx")] = i;
        arr.append(obj);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QString KeeperPlugin::removePairedExtensionAt(int idx)
{
    if (idx < 0 || idx >= m_pairedPubkeys.size())
        return R"({"success":false,"error":"index out of range"})";
    const QString removed = m_pairedPubkeys.takeAt(idx);
    if (!savePairedExtensions()) {
        m_pairedPubkeys.insert(idx, removed);
        return R"({"success":false,"error":"persistence write failed"})";
    }
    return R"({"success":true})";
}

bool KeeperPlugin::savePairedExtensions()
{
    QJsonArray arr;
    for (const auto& pk : m_pairedPubkeys) arr.append(pk);
    QFile f(persistPath("keeper-paired-extensions.json"));
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "KeeperPlugin: failed to open paired-extensions store for writing:" << f.errorString();
        return false;
    }
    const QByteArray data = QJsonDocument(arr).toJson(QJsonDocument::Compact);
    if (f.write(data) != data.size()) {
        qWarning() << "KeeperPlugin: short write on paired-extensions store:" << f.errorString();
        return false;
    }
    return true;
}
