#pragma once

#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>

/**
 * KeeperBridgeHost — the minimal, Qt-string surface the bridge needs from its host.
 *
 * Decouples the bridge from the (now Qt-free public) KeeperImpl: the universal
 * pimpl (KeeperImpl::Impl) implements this interface, exposing the same three
 * QString-returning methods the bridge used to call on KeeperPlugin directly.
 */
class KeeperBridgeHost
{
public:
    virtual ~KeeperBridgeHost() = default;
    virtual QString preserveItem(const QString& urlOrId) = 0;
    virtual QString getQueue() = 0;
    virtual QString getLog()   = 0;
};

/**
 * KeeperHttpBridge — localhost HTTP/1.1 API for the Keeper Chrome extension.
 *
 * Listens on http://127.0.0.1:7355 (default).
 * Implemented with plain QTcpServer/QTcpSocket — no Qt6HttpServer dependency.
 *
 * Routes:
 *   POST /preserve       body: {"url":"..."}  → {"queued":true,"id":"..."}
 *   GET  /queue                               → JSON array of queue items
 *   GET  /status/:id                          → {"status":"...","cid":"..."}
 *   OPTIONS *            CORS preflight       → 204 No Content + CORS headers
 */
class KeeperHttpBridge : public QObject
{
    Q_OBJECT
public:
    explicit KeeperHttpBridge(KeeperBridgeHost* host, QObject* parent = nullptr);
    bool listen(quint16 port = 7355);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void handleRequest(QTcpSocket* socket, const QByteArray& raw);
    void sendResponse(QTcpSocket* socket, int status,
                      const QByteArray& contentType, const QByteArray& body);
    void sendJson(QTcpSocket* socket, int status, const QByteArray& json);

    static QByteArray statusText(int code);

    KeeperBridgeHost* host_;
    QTcpServer        server_;
};
