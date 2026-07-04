#include "keeper_ui_backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVariant>

#include "logos_sdk.h"   // generated: modules().keeper / .logos_beacon / .stash

namespace {
constexpr int kPollIntervalMs = 2000;   // poll cadence for stash's CID result
constexpr int kMaxPollTicks   = 60;     // ~120s ceiling → advance without a CID
}

// Map a Qt LogosResult to the QString contract QML parses (callModuleParse):
//  - failure       → {"error": "..."}  (QML checks parsed.error)
//  - scalar/string → the raw value string
//  - object/array  → compact JSON of the value
// Errors resolve through logos.watch's SUCCESS callback (the bridge only rejects on
// QtRO transport failure), so QML inspects the returned string, not the reject path.
static QString resultToJson(const LogosResult& r)
{
    if (!r.success) {
        QJsonObject o;
        o["error"] = r.getError();
        return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    QVariant v = r.value;
    if (v.typeId() == QMetaType::QString)
        return v.toString();
    QJsonDocument doc = QJsonDocument::fromVariant(v);
    return doc.isNull() ? r.getString()
                        : QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

void KeeperUiBackend::onContextReady()
{
    setStatus(QStringLiteral("keeper + logos_beacon wired"));
}

// ── keeper (universal → SYNC forward) ───────────────────────────────────────────
QString KeeperUiBackend::getBridgeStatus()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.getBridgeStatus());
}

QString KeeperUiBackend::getQueue()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.getQueue());
}

QString KeeperUiBackend::getPendingUpload()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.getPendingUpload());
}

QString KeeperUiBackend::getLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.getLog());
}

QString KeeperUiBackend::preserveItem(QString urlOrId)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.preserveItem(urlOrId));
}

QString KeeperUiBackend::clearQueue()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.clearQueue());
}

QString KeeperUiBackend::cancelItem(QString id)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.cancelItem(id));
}

QString KeeperUiBackend::clearLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().keeper.clearLog());
}

// ── logos_beacon (universal → SYNC forward) ─────────────────────────────────────
QString KeeperUiBackend::getNodeInfo()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getNodeInfo());
}

QString KeeperUiBackend::getBeaconConfig()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getBeaconConfig());
}

QString KeeperUiBackend::getInscriptionLog()
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    return resultToJson(modules().logos_beacon.getInscriptionLog());
}

// ── stash (legacy → ASYNC fire-and-forget + internal poll) ──────────────────────
// A SYNC modules().stash.* call deadlocks the ui-host loop; drive stash async only.
QString KeeperUiBackend::startUpload(QString path, QString id, QString file)
{
    if (!isContextReady()) return "{\"error\":\"context not ready\"}";
    if (m_uploadInFlight)  return "{\"error\":\"upload already in flight\"}";

    m_uploadInFlight = true;
    m_uploadAttempts = 0;
    m_uploadId       = id;
    m_uploadFile     = file;
    m_uploadBase     = path.section('/', -1).section('\\', -1);   // basename (CID-match key)

    // Fire-and-forget: the async SEND reaches stash even if we ignore the reply.
    // stash is legacy → its generated wrapper returns plain QString (not LogosResult).
    modules().stash.uploadAsync(path, QStringLiteral("keeper"),
                                [](QString){}, Timeout());

    ensurePollTimer();
    m_uploadPoll->start();
    return "{\"ok\":true}";
}

void KeeperUiBackend::ensurePollTimer()
{
    if (m_uploadPoll) return;
    m_uploadPoll = new QTimer(this);
    m_uploadPoll->setInterval(kPollIntervalMs);
    QObject::connect(m_uploadPoll, &QTimer::timeout,
                     this, &KeeperUiBackend::pollStashResult);
}

void KeeperUiBackend::pollStashResult()
{
    if (!m_uploadInFlight) { m_uploadPoll->stop(); return; }
    if (++m_uploadAttempts > kMaxPollTicks) {           // ~120s ceiling
        finishUpload(QString(), QStringLiteral("timeout"));
        return;
    }
    // ASYNC poll — sync getLatestLogosResult() would deadlock the ui-host loop.
    // stash is legacy → the callback delivers the plain QString result directly.
    modules().stash.getLatestLogosResultAsync([this](QString raw) {
        if (!m_uploadInFlight) return;
        const QJsonObject o = QJsonDocument::fromJson(raw.toUtf8()).object();
        const QString cid  = o.value(QStringLiteral("cid")).toString();
        const QString rf   = o.value(QStringLiteral("file")).toString();
        if (cid.isEmpty() || rf.isEmpty()) return;
        const QString rbase = rf.section('/', -1).section('\\', -1);
        if (rbase == m_uploadBase)
            finishUpload(cid, QString());
    }, Timeout());
}

void KeeperUiBackend::finishUpload(const QString& cid, const QString& error)
{
    if (m_uploadPoll) m_uploadPoll->stop();
    m_uploadInFlight = false;

    // On a CID match, hand the result back to keeper (universal → SYNC forward).
    if (!cid.isEmpty() && isContextReady())
        modules().keeper.onUploadResult(m_uploadId, m_uploadFile, cid);

    QJsonObject o;
    o["id"]   = m_uploadId;
    o["file"] = m_uploadFile;
    if (!cid.isEmpty()) o["cid"] = cid;
    else                o["error"] = error.isEmpty() ? QStringLiteral("upload failed") : error;
    setLastUpload(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

QString KeeperUiBackend::ping()
{
    return QStringLiteral("{\"ok\":true,\"module\":\"keeper_ui\",\"ctxReady\":%1}")
        .arg(isContextReady() ? QStringLiteral("true") : QStringLiteral("false"));
}
