#include "keeper_http_bridge.h"
#include "keeper_plugin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

KeeperHttpBridge::KeeperHttpBridge(KeeperPlugin* plugin, QObject* parent)
    : QObject(parent), plugin_(plugin)
{}

bool KeeperHttpBridge::listen(quint16 port)
{
    using M = QHttpServerRequest::Method;

    // POST /preserve — queue a single IA item
    server_.route("/preserve", M::Post, [this](const QHttpServerRequest& req) {
        return handlePreserve(req);
    });
    server_.route("/preserve", M::Options, [](const QHttpServerRequest&) {
        return corsPreflight();
    });

    // GET /queue — return current queue state
    server_.route("/queue", M::Get, [this](const QHttpServerRequest&) {
        return handleQueue();
    });
    server_.route("/queue", M::Options, [](const QHttpServerRequest&) {
        return corsPreflight();
    });

    // GET /status/<identifier> — item status (queue + log)
    server_.route("/status/<arg>", M::Get,
        [this](const QString& id, const QHttpServerRequest&) {
            return handleStatus(id);
        });
    server_.route("/status/<arg>", M::Options,
        [](const QString&, const QHttpServerRequest&) {
            return corsPreflight();
        });

    quint16 bound = server_.listen(QHostAddress::LocalHost, port);
    if (!bound) {
        qWarning() << "KeeperHttpBridge: failed to listen on port" << port;
        return false;
    }
    qDebug() << "KeeperHttpBridge: listening on http://127.0.0.1:" << bound;
    return true;
}

// ── Route handlers ────────────────────────────────────────────────────────────

QHttpServerResponse KeeperHttpBridge::handlePreserve(const QHttpServerRequest& req)
{
    QJsonObject body = QJsonDocument::fromJson(req.body()).object();
    QString url = body.value("url").toString();
    if (url.isEmpty()) {
        return jsonErr(R"({"success":false,"error":"missing 'url' field"})");
    }

    QString result = plugin_->preserveItem(url);
    auto resp = jsonOk(result.toUtf8());
    addCors(resp);
    return resp;
}

QHttpServerResponse KeeperHttpBridge::handleQueue()
{
    auto resp = jsonOk(plugin_->getQueue().toUtf8());
    addCors(resp);
    return resp;
}

QHttpServerResponse KeeperHttpBridge::handleStatus(const QString& identifier)
{
    // 1. Search the live queue
    QJsonArray queue = QJsonDocument::fromJson(plugin_->getQueue().toUtf8()).array();
    for (const auto& v : queue) {
        QJsonObject item = v.toObject();
        if (item.value("id").toString() == identifier) {
            QJsonObject out;
            out["status"] = item.value("status");
            out["cid"]    = item.value("collectionCid");
            auto resp = jsonOk(QJsonDocument(out).toJson(QJsonDocument::Compact));
            addCors(resp);
            return resp;
        }
    }

    // 2. Fall back to completed log
    QJsonArray log = QJsonDocument::fromJson(plugin_->getLog().toUtf8()).array();
    for (const auto& v : log) {
        QJsonObject item = v.toObject();
        if (item.value("id").toString() == identifier) {
            QJsonObject out;
            out["status"] = QStringLiteral("done");
            out["cid"]    = item.value("collectionCid");
            // Propagate beacon inscription metadata when present
            if (item.contains("inscription_status"))
                out["inscription_status"] = item.value("inscription_status");
            if (item.contains("inscription_id"))
                out["inscription_id"] = item.value("inscription_id");
            auto resp = jsonOk(QJsonDocument(out).toJson(QJsonDocument::Compact));
            addCors(resp);
            return resp;
        }
    }

    // 3. Unknown
    QJsonObject out;
    out["status"] = QStringLiteral("unknown");
    auto resp = jsonOk(QJsonDocument(out).toJson(QJsonDocument::Compact));
    addCors(resp);
    return resp;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

QHttpServerResponse KeeperHttpBridge::jsonOk(const QByteArray& body)
{
    return QHttpServerResponse("application/json", body,
                               QHttpServerResponse::StatusCode::Ok);
}

QHttpServerResponse KeeperHttpBridge::jsonErr(const QByteArray& body,
                                               QHttpServerResponse::StatusCode code)
{
    auto resp = QHttpServerResponse("application/json", body, code);
    addCors(resp);
    return resp;
}

QHttpServerResponse KeeperHttpBridge::corsPreflight()
{
    QHttpServerResponse resp("text/plain", QByteArray{},
                             QHttpServerResponse::StatusCode::NoContent);
    addCors(resp);
    return resp;
}

void KeeperHttpBridge::addCors(QHttpServerResponse& resp)
{
    resp.addHeader("Access-Control-Allow-Origin",  "*");
    resp.addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp.addHeader("Access-Control-Allow-Headers", "Content-Type");
}
