#pragma once

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QObject>

class KeeperPlugin;

/**
 * KeeperHttpBridge — localhost HTTP API for the Basecamp Keeper Chrome extension.
 *
 * Listens on http://127.0.0.1:7355 (default port).
 *
 * Routes:
 *   POST /preserve       body: {"url":"..."}  → {"queued":true,"id":"..."}
 *   GET  /queue                               → JSON array of queue items
 *   GET  /status/:id                          → {"status":"...","cid":"..."}
 *   OPTIONS *            CORS preflight       → 204 No Content + CORS headers
 *
 * All responses include Access-Control-Allow-Origin: * so the content script
 * can call the API from the archive.org page context.
 *
 * Threading: QHttpServer invokes synchronous route handlers on the thread that
 * owns it (main thread). KeeperPlugin lives on the same thread → direct calls
 * are safe; no extra locking needed.
 */
class KeeperHttpBridge : public QObject
{
    Q_OBJECT
public:
    explicit KeeperHttpBridge(KeeperPlugin* plugin, QObject* parent = nullptr);

    // Start listening. Returns true on success.
    bool listen(quint16 port = 7355);

private:
    QHttpServerResponse handlePreserve(const QHttpServerRequest& req);
    QHttpServerResponse handleQueue();
    QHttpServerResponse handleStatus(const QString& identifier);

    static QHttpServerResponse jsonOk(const QByteArray& body);
    static QHttpServerResponse jsonErr(const QByteArray& body,
                                       QHttpServerResponse::StatusCode code
                                           = QHttpServerResponse::StatusCode::BadRequest);
    static QHttpServerResponse corsPreflight();
    static void addCors(QHttpServerResponse& resp);

    KeeperPlugin* plugin_;
    QHttpServer   server_;
};
