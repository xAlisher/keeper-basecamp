#include "keeper_http_bridge.h"
#include "keeper_plugin.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QHostAddress>

// ── Lifecycle ─────────────────────────────────────────────────────────────────

KeeperHttpBridge::KeeperHttpBridge(KeeperPlugin* plugin, QObject* parent)
    : QObject(parent), plugin_(plugin)
{
    connect(&server_, &QTcpServer::newConnection, this, &KeeperHttpBridge::onNewConnection);
}

bool KeeperHttpBridge::listen(quint16 port)
{
    if (!server_.listen(QHostAddress::LocalHost, port)) {
        qWarning() << "KeeperHttpBridge: failed to listen on port" << port << server_.errorString();
        return false;
    }
    qDebug() << "KeeperHttpBridge: listening on http://127.0.0.1:" << server_.serverPort();
    return true;
}

// ── Server callbacks ──────────────────────────────────────────────────────────

void KeeperHttpBridge::onNewConnection()
{
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead,    this,   &KeeperHttpBridge::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void KeeperHttpBridge::onReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    QByteArray data = socket->readAll();
    handleRequest(socket, data);
}

// ── Request dispatcher ────────────────────────────────────────────────────────

void KeeperHttpBridge::handleRequest(QTcpSocket* socket, const QByteArray& raw)
{
    // Parse first line: METHOD /path HTTP/1.x
    int lineEnd = raw.indexOf("\r\n");
    if (lineEnd < 0) lineEnd = raw.indexOf('\n');
    QList<QByteArray> parts = (lineEnd > 0 ? raw.left(lineEnd) : raw).split(' ');
    if (parts.size() < 2) { sendJson(socket, 400, R"({"error":"bad request"})"); return; }

    QByteArray method = parts[0].toUpper();
    QByteArray path   = parts[1];

    // CORS preflight
    if (method == "OPTIONS") {
        sendResponse(socket, 204, "text/plain", "");
        return;
    }

    // POST /preserve
    if (method == "POST" && path == "/preserve") {
        int sep = raw.indexOf("\r\n\r\n");
        QByteArray body = sep >= 0 ? raw.mid(sep + 4) : QByteArray();
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QString url;
        if (doc.isObject()) url = doc.object().value("url").toString();
        if (url.isEmpty()) { sendJson(socket, 400, R"({"error":"missing url"})"); return; }
        sendJson(socket, 200, plugin_->preserveItem(url).toUtf8());
        return;
    }

    // GET /queue
    if (method == "GET" && path == "/queue") {
        sendJson(socket, 200, plugin_->getQueue().toUtf8());
        return;
    }

    // GET /status/<id>
    if (method == "GET" && path.startsWith("/status/")) {
        QString id = QString::fromUtf8(path.mid(8));
        // Check live queue
        for (const auto& v : QJsonDocument::fromJson(plugin_->getQueue().toUtf8()).array()) {
            QJsonObject obj = v.toObject();
            if (obj["id"].toString() == id) {
                QJsonObject resp;
                resp["status"] = obj["status"].toString();
                resp["cid"]    = obj["collectionCid"].toString();
                sendJson(socket, 200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
                return;
            }
        }
        // Check completed log
        for (const auto& v : QJsonDocument::fromJson(plugin_->getLog().toUtf8()).array()) {
            QJsonObject obj = v.toObject();
            if (obj["id"].toString() == id) {
                QJsonObject resp;
                resp["status"] = QStringLiteral("done");
                resp["cid"]    = obj["collectionCid"].toString();
                sendJson(socket, 200, QJsonDocument(resp).toJson(QJsonDocument::Compact));
                return;
            }
        }
        sendJson(socket, 404, R"({"error":"not found"})");
        return;
    }

    sendJson(socket, 404, R"({"error":"not found"})");
}

// ── Response helpers ──────────────────────────────────────────────────────────

QByteArray KeeperHttpBridge::statusText(int code)
{
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        default:  return "Internal Server Error";
    }
}

void KeeperHttpBridge::sendResponse(QTcpSocket* socket, int status,
                                    const QByteArray& contentType, const QByteArray& body)
{
    QByteArray resp;
    resp  = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
    resp += "Access-Control-Allow-Origin: *\r\n";
    resp += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    resp += "Access-Control-Allow-Headers: Content-Type\r\n";
    if (!body.isEmpty()) {
        resp += "Content-Type: " + contentType + "\r\n";
        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    resp += "Connection: close\r\n\r\n";
    resp += body;
    socket->write(resp);
    socket->flush();
    socket->disconnectFromHost();
}

void KeeperHttpBridge::sendJson(QTcpSocket* socket, int status, const QByteArray& json)
{
    sendResponse(socket, status, "application/json", json);
}
