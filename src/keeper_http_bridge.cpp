#include "keeper_http_bridge.h"
#include "keeper_plugin.h"

#include <QHttpHeaders>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QHostAddress>

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
    server_.route("/preserve", M::Options, [](const QHttpServerRequest& req) {
        return corsPreflight(req);
    });

    // GET /queue — return current queue state
    server_.route("/queue", M::Get, [this](const QHttpServerRequest& req) {
        return handleQueue(req);
    });
    server_.route("/queue", M::Options, [](const QHttpServerRequest& req) {
        return corsPreflight(req);
    });

    // GET /status/<identifier> — item status (queue + log)
    server_.route("/status/<arg>", M::Get,
        [this](const QString& id, const QHttpServerRequest& req) {
            return handleStatus(id, req);
        });
    server_.route("/status/<arg>", M::Options,
        [](const QString&, const QHttpServerRequest& req) {
            return corsPreflight(req);
        });

    auto* tcp = new QTcpServer(this);
    if (!tcp->listen(QHostAddress::LocalHost, port)) {
        qWarning() << "KeeperHttpBridge: failed to listen on port" << port;
        delete tcp;
        return false;
    }
    server_.bind(tcp);
    qDebug() << "KeeperHttpBridge: listening on http://127.0.0.1:" << tcp->serverPort();
    return true;
}

// ── Route handlers ────────────────────────────────────────────────────────────

QHttpServerResponse KeeperHttpBridge::handlePreserve(const QHttpServerRequest& req)
{
    QJsonObject body = QJsonDocument::fromJson(req.body()).object();

    QString errMsg;
    if (!plugin_->verifyPreserveRequest(body, errMsg)) {
        QJsonObject errObj;
        errObj["success"] = false;
        errObj["error"]   = errMsg;
        return jsonErr(QJsonDocument(errObj).toJson(QJsonDocument::Compact),
                       QHttpServerResponse::StatusCode::Unauthorized);
    }

    const QString origin = req.value("Origin");
    QString result = plugin_->preserveItem(body.value("url").toString());
    auto resp = jsonOk(result.toUtf8());
    addCors(resp, origin);
    return resp;
}

QHttpServerResponse KeeperHttpBridge::handleQueue(const QHttpServerRequest& req)
{
    const QString origin = req.value("Origin");
    auto resp = jsonOk(plugin_->getQueue().toUtf8());
    addCors(resp, origin);
    return resp;
}

QHttpServerResponse KeeperHttpBridge::handleStatus(const QString& identifier,
                                                    const QHttpServerRequest& req)
{
    const QString origin = req.value("Origin");

    // 1. Search the live queue
    QJsonArray queue = QJsonDocument::fromJson(plugin_->getQueue().toUtf8()).array();
    for (const auto& v : queue) {
        QJsonObject item = v.toObject();
        if (item.value("id").toString() == identifier) {
            QJsonObject out;
            out["status"] = item.value("status");
            out["cid"]    = item.value("collectionCid");
            auto resp = jsonOk(QJsonDocument(out).toJson(QJsonDocument::Compact));
            addCors(resp, origin);
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
            auto resp = jsonOk(QJsonDocument(out).toJson(QJsonDocument::Compact));
            addCors(resp, origin);
            return resp;
        }
    }

    // 3. Unknown — 404
    return jsonErr(R"({"success":false,"error":"not found"})",
                   QHttpServerResponse::StatusCode::NotFound);
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
    return QHttpServerResponse("application/json", body, code);
}

QHttpServerResponse KeeperHttpBridge::corsPreflight(const QHttpServerRequest& req)
{
    const QString origin = req.value("Origin");
    QHttpServerResponse resp("text/plain", QByteArray{},
                             QHttpServerResponse::StatusCode::NoContent);
    addCors(resp, origin);
    return resp;
}

bool KeeperHttpBridge::isTrustedOrigin(const QString& origin)
{
    return origin.startsWith("chrome-extension://")
        || origin.startsWith("moz-extension://");
}

void KeeperHttpBridge::addCors(QHttpServerResponse& resp, const QString& origin)
{
    if (!isTrustedOrigin(origin)) return;
    QHttpHeaders hdrs = resp.headers();
    hdrs.append("Access-Control-Allow-Origin",  origin.toUtf8());
    hdrs.append("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    hdrs.append("Access-Control-Allow-Headers", "Content-Type");
    resp.setHeaders(hdrs);
}
