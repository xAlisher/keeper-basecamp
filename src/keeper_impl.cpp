#include "keeper_impl.h"
#include "keeper_http_bridge.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDateTime>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <cstdio>
#include <utility>

#include <nlohmann/json.hpp>

#define KTRACE(msg) do { fprintf(stderr, "[keeper-trace] " msg "\n"); fflush(stderr); } while(0)
#define KTRACEF(fmt, ...) do { fprintf(stderr, "[keeper-trace] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ── Value types (were in keeper_plugin.h) ──────────────────────────────────────
struct KeeperFile {
    QString name;
    QString size;
    QString cid;
    QString status;   // "pending" | "downloading" | "uploading" | "inscribing" | "done" | "failed"
    QString error;
};

struct KeeperItem {
    QString identifier;
    QString title;
    QString status;         // "queued" | "active" | "inscribing" | "done" | "failed" | "cancelled"
    QString collectionCid;  // final CID inscribed to beacon
    QList<KeeperFile> files;
    int currentFile = 0;
};

// ── pimpl: the Qt internals, preserved verbatim from the legacy KeeperPlugin ─────
//
// Impl is a QObject so it can parent the QNetworkAccessManager + KeeperHttpBridge,
// serve as the context object for the async connect() lambdas, and own the QFile
// download sinks — exactly as the legacy plugin (a QObject) did. It also implements
// KeeperBridgeHost so the QTcpServer bridge can call back into the state machine.
struct KeeperImpl::Impl : public QObject, public KeeperBridgeHost
{
    // Pending upload handed off to QML (sequential — one at a time)
    struct PendingUpload {
        QString identifier;
        QString fileName;
        QString localPath;
        bool    active = false;
    } pendingUpload_;

    QList<KeeperItem>  queue_;
    QList<QJsonObject> log_;
    QList<QJsonObject> inscriptionQueue_;
    bool               busy_        = false;

    QNetworkAccessManager* nam_ = nullptr;

    KeeperHttpBridge* httpBridge_    = nullptr;
    bool              bridgeRunning_ = false;
    quint16           bridgePort_    = 7355;

    // Config
    int  maxFilesPerItem_    = 20;
    bool skipDerivatives_    = true;
    QString inscriptionFormat_ = "collection";  // "collection" (IA-Archiver) | "cid" (Legacy)

    QString m_persistencePath;

    // ── ctor: the old initLogos() work (universal cores get no LogosAPI*) ────────
    Impl()
        : nam_(new QNetworkAccessManager(this))
    {
        // Persistence path — canonical module_data location (no instancePersistencePath
        // property on universal modules; same default the legacy plugin fell back to).
        m_persistencePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + "/keeper";
        QDir().mkpath(m_persistencePath);

        loadQueue();

        // HTTP bridge — plain QTcpServer, no Qt6HttpServer dependency.
        httpBridge_    = new KeeperHttpBridge(this, this);
        bridgeRunning_ = httpBridge_->listen(7355);

        qDebug() << "KeeperImpl: initialized";
    }

    // ── Public API ───────────────────────────────────────────────────────────────

    QString preserveItem(const QString& urlOrId) override
    {
        KTRACE("preserveItem called");
        QString id = parseIdentifier(urlOrId);
        if (id.isEmpty())
            return R"({"success":false,"error":"could not parse identifier"})";

        KTRACEF("parsed id = %s", qPrintable(id));

        // Deduplicate
        for (const auto& item : queue_)
            if (item.identifier == id && item.status != "failed" && item.status != "cancelled")
                return R"({"success":false,"error":"already in queue"})";

        KeeperItem item;
        item.identifier = id;
        item.title      = id;
        item.status     = "queued";
        queue_.append(item);
        KTRACE("appended to queue, saving...");
        saveQueue();
        KTRACE("saveQueue done, emitting event...");

        emitEvent("itemQueued", {QVariantMap{{"id", id}, {"title", id}}});
        qDebug() << "KeeperImpl: queued" << id;

        if (!busy_) advanceQueue();
        KTRACE("preserveItem returning");
        return QString(R"({"queued":true,"id":"%1"})").arg(id);
    }

    QString preserveCollection(const QString& name, int limit)
    {
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
                qDebug() << "KeeperImpl: collection fetch error" << reply->errorString();
            }
            reply->deleteLater();
            delete queued; delete cap;
        });

        return QString(R"({"queued":"pending","collection":"%1"})").arg(name);
    }

    QString cancelItem(const QString& identifier)
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

    QString getQueue() override
    {
        return itemsToJson(queue_);
    }

    QString getLog() override
    {
        QJsonArray arr;
        for (const auto& obj : log_) arr.append(obj);
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    QString clearLog()
    {
        log_.clear();
        QFile::remove(persistPath("keeper-log.json"));
        return R"({"ok":true})";
    }

    QString clearQueue()
    {
        queue_.clear();
        QFile::remove(persistPath("keeper-queue.json"));
        return R"({"ok":true})";
    }

    QString getBridgeStatus() const
    {
        QJsonObject o;
        o[QStringLiteral("running")] = bridgeRunning_;
        o[QStringLiteral("port")]    = bridgePort_;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString getInscriptionQueue() const
    {
        QJsonArray arr;
        for (const auto& e : inscriptionQueue_) arr.append(e);
        return QJsonDocument(arr).toJson(QJsonDocument::Compact);
    }

    QString markInscribed(const QString& cid)
    {
        inscriptionQueue_.removeIf([&](const QJsonObject& e){ return e["cid"].toString() == cid; });
        saveInscriptionQueue();
        return R"({"ok":true})";
    }

    QString getConfig()
    {
        return QString(R"({"maxFilesPerItem":%1,"skipDerivatives":%2,"inscriptionFormat":"%3"})")
            .arg(maxFilesPerItem_)
            .arg(skipDerivatives_ ? "true" : "false")
            .arg(inscriptionFormat_);
    }

    QString setConfig(const QString& json)
    {
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject()) return R"({"success":false,"error":"invalid json"})";
        QJsonObject obj = doc.object();
        if (obj.contains("maxFilesPerItem"))  maxFilesPerItem_  = obj["maxFilesPerItem"].toInt(maxFilesPerItem_);
        if (obj.contains("skipDerivatives"))  skipDerivatives_  = obj["skipDerivatives"].toBool(skipDerivatives_);
        if (obj.contains("inscriptionFormat")) {
            const QString fmt = obj["inscriptionFormat"].toString();
            if (fmt == "cid" || fmt == "collection") inscriptionFormat_ = fmt;
        }
        return R"({"success":true})";
    }

    // ── Queue engine ─────────────────────────────────────────────────────────────

    void advanceQueue()
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

    void fetchMetadata(const QString& identifier)
    {
        KTRACEF("fetchMetadata for %s", qPrintable(identifier));
        QString url = QString("https://archive.org/metadata/%1").arg(identifier);
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("User-Agent", "keeper-basecamp/0.1");
        KTRACE("calling nam_->get() for metadata");
        auto* reply = nam_->get(req);
        KTRACEF("nam_->get() returned reply=%p", (void*)reply);

        connect(reply, &QNetworkReply::finished, this, [this, reply, identifier] {
            KTRACEF("fetchMetadata reply finished for %s", qPrintable(identifier));
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

    void processNextFile()
    {
        KTRACE("processNextFile called");
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
            // All files uploaded — inscribe per the configured format (keeper decides; beacon relays).
            item->status = "inscribing";
            saveQueue();
            if (inscriptionFormat_ == "cid") {
                // CID-based (Legacy): inscribe each file's CID on-chain.
                for (const auto& f : item->files)
                    if (f.status == "done" && !f.cid.isEmpty())
                        inscribeToBeacon(item->identifier, f.cid);
            } else {
                // Collection ID (IA-Archiver): one inscription of the stable collection key; CIDs stay in Storage.
                inscribeToBeacon(item->identifier, "ia:" + item->identifier);
            }
            return;
        }

        KeeperFile& f = item->files[item->currentFile];
        f.status = "downloading";
        emitEvent("itemProgress", {QVariantMap{
            {"id", item->identifier}, {"file", f.name}, {"phase", "download"}, {"pct", 0}
        }});
        downloadFile(item->identifier, f);
    }

    void downloadFile(const QString& identifier, const KeeperFile& file)
    {
        KTRACEF("downloadFile %s/%s", qPrintable(identifier), qPrintable(file.name));
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

    void uploadToStash(const QString& identifier, const QString& localPath,
                       const QString& fileName)
    {
        KTRACEF("uploadToStash %s %s — handing off to QML", qPrintable(identifier), qPrintable(fileName));

        KeeperItem* item = nullptr;
        for (auto& i : queue_) if (i.identifier == identifier) { item = &i; break; }
        if (!item) return;

        for (auto& ff : item->files)
            if (ff.name == fileName) { ff.status = "uploading"; break; }

        // Store for QML to pick up via getPendingUpload().
        // QML calls callModule("stash", "upload", ...) then reports back via onUploadResult().
        pendingUpload_ = {identifier, fileName, localPath, true};
        saveQueue();
    }

    QString getPendingUpload() const
    {
        if (!pendingUpload_.active) return QStringLiteral("{}");
        QJsonObject o;
        o[QStringLiteral("id")]   = pendingUpload_.identifier;
        o[QStringLiteral("file")] = pendingUpload_.fileName;
        o[QStringLiteral("path")] = pendingUpload_.localPath;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QString onUploadResult(const QString& identifier, const QString& fileName,
                           const QString& cid)
    {
        KTRACEF("onUploadResult %s %s cid=%s",
                qPrintable(identifier), qPrintable(fileName), qPrintable(cid));

        pendingUpload_.active = false;

        KeeperItem* item = nullptr;
        for (auto& i : queue_) if (i.identifier == identifier) { item = &i; break; }
        if (!item) return R"({"ok":false,"error":"item not found"})";

        for (auto& ff : item->files) {
            if (ff.name == fileName) {
                ff.cid    = cid;
                ff.status = "done";
                break;
            }
        }
        QFile::remove(pendingUpload_.localPath);
        saveQueue();
        item->currentFile++;
        processNextFile();
        return R"({"ok":true})";
    }

    void inscribeToBeacon(const QString& identifier, const QString& cid)
    {
        // Build rich label from the item still in queue_
        QString label;
        for (const auto& item : std::as_const(queue_)) {
            if (item.identifier != identifier) continue;
            QJsonObject labelObj;
            // ── legacy fields (kept so existing decoders keep working during transition) ──
            labelObj["module"] = "keeper";
            labelObj["source"] = "internet_archive";
            labelObj["id"]     = identifier;
            if (!item.title.isEmpty() && item.title != identifier)
                labelObj["title"] = item.title;
            qint64 totalSize = 0;
            QJsonArray fileArr;
            QString thumbCid;
            for (const auto& f : item.files) {
                if (f.status == "done" && !f.cid.isEmpty()) {
                    fileArr.append(QJsonObject{{"name", f.name}, {"cid", f.cid}});
                    if (f.name.contains("__ia_thumb")) thumbCid = f.cid;   // cover image
                }
                totalSize += f.size.toLongLong();
            }
            if (totalSize > 0) labelObj["totalSize"] = totalSize;
            if (!fileArr.isEmpty()) labelObj["files"] = fileArr;
            // ── standard metadata (keeper#43) — OpenSea-NFT-metadata shape, self-describing so
            //    any indexer/explorer (zonescan, ours) decodes it without custom support.
            //    STRAWMAN pending a LEZ-specific standard co-designed with Paradox. ──
            labelObj["name"]         = (!item.title.isEmpty() && item.title != identifier)
                                           ? item.title : identifier;
            labelObj["description"]  = "Internet Archive item preserved on Logos Storage via keeper.";
            labelObj["external_url"] = "https://archive.org/details/" + identifier;
            if (!thumbCid.isEmpty()) labelObj["image"] = thumbCid;   // Logos Storage CID (URI scheme TBD w/ LEZ std)
            QJsonArray attrs;
            attrs.append(QJsonObject{{"trait_type", "source"}, {"value", "internet_archive"}});
            attrs.append(QJsonObject{{"trait_type", "item"},   {"value", identifier}});
            attrs.append(QJsonObject{{"trait_type", "files"},  {"value", fileArr.size()}});
            if (totalSize > 0)
                attrs.append(QJsonObject{{"trait_type", "totalSize"}, {"value", QString::number(totalSize)}});
            labelObj["attributes"] = attrs;
            if (!fileArr.isEmpty()) labelObj["content"] = fileArr;   // pinned files under the standard key
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
        // txHash tracking is handled by keeper QML reading beacon's getInscriptionLog().
    }

    void finishItem(const QString& identifier, const QString& collectionCid)
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

    QString parseIdentifier(const QString& urlOrId)
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

    // Events had no request/reply consumer in the universal core (progress was a
    // legacy QML signal). The UI drives keeper by polling getQueue/getPendingUpload/
    // getInscriptionQueue, so this is a no-op — the 15 request/reply methods'
    // return values and persistence are unchanged.
    void emitEvent(const QString& name, const QVariantList& data)
    {
        Q_UNUSED(name);
        Q_UNUSED(data);
    }

    QString itemsToJson(const QList<KeeperItem>& items)
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

    QString persistPath(const QString& filename)
    {
        return m_persistencePath + "/" + filename;
    }

    void saveInscriptionQueue()
    {
        QJsonArray arr;
        for (const auto& e : inscriptionQueue_) arr.append(e);
        QFile f(persistPath("keeper-inscription-queue.json"));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }

    void loadQueue()
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

    void saveQueue()
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

    void appendLog(const KeeperItem& item)
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
};

// ── Adapter: QString-JSON → StdLogosResult (verbatim from beacon_impl) ───────────
static StdLogosResult adapt(const QString& jsonStr)
{
    auto j = nlohmann::json::parse(jsonStr.toStdString(), nullptr, false);
    if (j.is_discarded()) return {false, {}, "invalid module response"};
    if (j.is_object() && j.contains("error"))
        return {false, {}, j["error"].get<std::string>()};
    return {true, j};
}
static QString qs(const std::string& s) { return QString::fromStdString(s); }

// ── ctor/dtor ───────────────────────────────────────────────────────────────────
KeeperImpl::KeeperImpl() : d(std::make_unique<Impl>()) {}
KeeperImpl::~KeeperImpl() = default;

// ── Public API (thin adapters over the Qt pimpl) ────────────────────────────────
StdLogosResult KeeperImpl::preserveItem(const std::string& urlOrId)                { return adapt(d->preserveItem(qs(urlOrId))); }
StdLogosResult KeeperImpl::preserveCollection(const std::string& name, int limit)  { return adapt(d->preserveCollection(qs(name), limit)); }
StdLogosResult KeeperImpl::cancelItem(const std::string& identifier)               { return adapt(d->cancelItem(qs(identifier))); }
StdLogosResult KeeperImpl::getQueue()                                             { return adapt(d->getQueue()); }
StdLogosResult KeeperImpl::getLog()                                              { return adapt(d->getLog()); }
StdLogosResult KeeperImpl::getConfig()                                            { return adapt(d->getConfig()); }
StdLogosResult KeeperImpl::setConfig(const std::string& json)                     { return adapt(d->setConfig(qs(json))); }
StdLogosResult KeeperImpl::clearLog()                                             { return adapt(d->clearLog()); }
StdLogosResult KeeperImpl::clearQueue()                                           { return adapt(d->clearQueue()); }
StdLogosResult KeeperImpl::getBridgeStatus()                                      { return adapt(d->getBridgeStatus()); }
StdLogosResult KeeperImpl::getInscriptionQueue()                                  { return adapt(d->getInscriptionQueue()); }
StdLogosResult KeeperImpl::markInscribed(const std::string& cid)                  { return adapt(d->markInscribed(qs(cid))); }
StdLogosResult KeeperImpl::getPendingUpload()                                     { return adapt(d->getPendingUpload()); }
StdLogosResult KeeperImpl::onUploadResult(const std::string& identifier, const std::string& fileName, const std::string& cid) { return adapt(d->onUploadResult(qs(identifier), qs(fileName), qs(cid))); }
