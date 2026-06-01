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
    loadPairedExtensions();

    // Start HTTP bridge for the Chrome extension (localhost:7355)
    httpBridge_ = new KeeperHttpBridge(this, this);
    bridgeRunning_ = httpBridge_->listen(7355);

    // Defer client init + queue resume so the event loop is running.
    // Pre-initialising stashClient_ and beaconClient_ HERE (before any
    // downloads start) avoids calling getClient() after download callbacks
    // and QRO event emissions, where std::bad_alloc has been observed.
    QTimer::singleShot(2000, this, [this]{
        try {
            if (logosAPI) {
                qDebug() << "KeeperPlugin: acquiring stash client";
                stashClient_  = logosAPI->getClient("stash");
                qDebug() << "KeeperPlugin: acquiring beacon client";
                beaconClient_ = logosAPI->getClient("logos_beacon");
                qDebug() << "KeeperPlugin: clients acquired";
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
        } catch (const std::exception& e) {
            qCritical() << "KeeperPlugin: deferred init threw:" << e.what();
        } catch (...) {
            qCritical() << "KeeperPlugin: deferred init threw unknown exception";
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

QString KeeperPlugin::clearLog()
{
    log_.clear();
    QFile::remove(persistPath("keeper-log.json"));
    return R"({"ok":true})";
}

QString KeeperPlugin::clearQueue()
{
    queue_.clear();
    QFile::remove(persistPath("keeper-queue.json"));
    return R"({"ok":true})";
}

QString KeeperPlugin::getBridgeStatus() const
{
    QJsonObject o;
    o[QStringLiteral("running")] = bridgeRunning_;
    o[QStringLiteral("port")]    = bridgePort_;
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

// ── Paired-extension management ───────────────────────────────────────────────

QString KeeperPlugin::addPairedExtension(const QString& hexPubkey)
{
    // Must be exactly 64 hex chars (32-byte Ed25519 public key)
    if (hexPubkey.length() != 64 ||
        !QRegularExpression("^[0-9a-fA-F]{64}$").match(hexPubkey).hasMatch()) {
        return R"({"success":false,"error":"pubkey must be 64 hex characters"})";
    }
    QString normalized = hexPubkey.toLower();
    if (m_pairedPubkeys.contains(normalized))
        return R"({"success":false,"error":"already paired"})";
    m_pairedPubkeys.append(normalized);
    savePairedExtensions();
    qDebug() << "KeeperPlugin: paired extension" << normalized.left(16) << "...";
    return R"({"success":true})";
}

QString KeeperPlugin::removePairedExtension(const QString& hexPubkey)
{
    QString normalized = hexPubkey.toLower();
    if (!m_pairedPubkeys.removeOne(normalized))
        return R"({"success":false,"error":"not found"})";
    savePairedExtensions();
    qDebug() << "KeeperPlugin: unpaired extension" << normalized.left(16) << "...";
    return R"({"success":true})";
}

QString KeeperPlugin::getPairedExtensions() const
{
    QJsonArray arr;
    for (const QString& pk : m_pairedPubkeys)
        arr.append(pk);
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// ── Request verification ──────────────────────────────────────────────────────

bool KeeperPlugin::verifyPreserveRequest(const QJsonObject& msg, QString& outError)
{
    // 1. Required fields
    static const QStringList required = {"action", "identifier", "url", "timestamp", "pubkey", "sig"};
    for (const QString& field : required) {
        if (!msg.contains(field)) {
            outError = "missing field: " + field;
            return false;
        }
    }

    const QString action     = msg["action"].toString();
    const QString identifier = msg["identifier"].toString();
    const QString url        = msg["url"].toString();
    const qint64  ts         = msg["timestamp"].toInteger();
    const QString pubkey     = msg["pubkey"].toString().toLower();
    const QString sig        = msg["sig"].toString();

    if (action != "preserve") {
        outError = "unknown action";
        return false;
    }

    // 2. Pubkey format: exactly 64 hex chars (32 bytes)
    if (pubkey.length() != 64) {
        outError = "invalid pubkey";
        return false;
    }

    // 3. Pubkey must be in the paired list
    if (!m_pairedPubkeys.contains(pubkey)) {
        qDebug() << "KeeperPlugin: rejected — unknown pubkey" << pubkey.left(16) << "...";
        outError = "unauthorized";
        return false;
    }

    // 4. Timestamp freshness (±60 s)
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (qAbs(now - ts) > 60) {
        qDebug() << "KeeperPlugin: rejected — stale timestamp, delta=" << (now - ts) << "s";
        outError = "stale timestamp";
        return false;
    }

    // 5. Replay: same sig must not have been used before
    pruneNonceCache();
    if (m_usedNonces.contains(sig)) {
        qDebug() << "KeeperPlugin: rejected — replay detected for" << identifier;
        outError = "replay detected";
        return false;
    }

    // 6. Ed25519 signature — canonical message matches JS JSON.stringify key order
    QJsonObject canonical;
    canonical["action"]     = action;
    canonical["identifier"] = identifier;
    canonical["url"]        = url;
    canonical["timestamp"]  = ts;
    canonical["pubkey"]     = pubkey;
    const QByteArray message    = QJsonDocument(canonical).toJson(QJsonDocument::Compact);
    const QByteArray pubkeyBytes = QByteArray::fromHex(pubkey.toUtf8());
    const QByteArray sigBytes    = QByteArray::fromBase64(sig.toUtf8());

    if (pubkeyBytes.size() != crypto_sign_PUBLICKEYBYTES ||
        sigBytes.size()    != crypto_sign_BYTES) {
        outError = "malformed key or signature";
        return false;
    }

    if (crypto_sign_verify_detached(
            reinterpret_cast<const unsigned char*>(sigBytes.constData()),
            reinterpret_cast<const unsigned char*>(message.constData()),
            static_cast<unsigned long long>(message.size()),
            reinterpret_cast<const unsigned char*>(pubkeyBytes.constData())) != 0) {
        qDebug() << "KeeperPlugin: rejected — bad signature for" << identifier;
        outError = "invalid signature";
        return false;
    }

    m_usedNonces[sig] = now;
    qDebug() << "KeeperPlugin: verified preserve request for" << identifier
             << "from pubkey" << pubkey.left(16) << "...";
    return true;
}

void KeeperPlugin::pruneNonceCache()
{
    const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 120;
    for (auto it = m_usedNonces.begin(); it != m_usedNonces.end(); ) {
        if (it.value() < cutoff)
            it = m_usedNonces.erase(it);
        else
            ++it;
    }
}

// ── Paired-extension persistence ──────────────────────────────────────────────

void KeeperPlugin::loadPairedExtensions()
{
    QFile f(persistPath("keeper-paired-extensions.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;
    for (const auto& v : doc.array()) {
        QString pk = v.toString().toLower();
        if (pk.length() == 64 && !m_pairedPubkeys.contains(pk))
            m_pairedPubkeys.append(pk);
    }
    qDebug() << "KeeperPlugin: loaded" << m_pairedPubkeys.size() << "paired extension(s)";
}

void KeeperPlugin::savePairedExtensions()
{
    QJsonArray arr;
    for (const QString& pk : m_pairedPubkeys) arr.append(pk);
    QFile f(persistPath("keeper-paired-extensions.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
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
