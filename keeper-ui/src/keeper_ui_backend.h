#pragma once

#include "rep_keeper_ui_source.h"     // generated from src/keeper_ui.rep (repc)
#include "logos_ui_plugin_context.h"  // modules() + onContextReady() + isContextReady()

#include <QString>

class QTimer;

/**
 * @brief keeper_ui backend — bridges QML to the universal keeper + logos_beacon
 *        modules, and drives the legacy stash upload async.
 *
 * QML (logos.module("keeper_ui") + logos.watch) → this backend. keeper and
 * logos_beacon are Qt-free universal modules → forwarded SYNCHRONOUSLY via
 * modules().keeper.* / modules().logos_beacon.* (proven safe — beacon_ui does the
 * same). stash is a LEGACY module → a sync modules().stash.* call would deadlock
 * the single-threaded ui-host loop, so the upload path is fire-and-forget + an
 * internal QTimer poll, with the CID/error surfaced through the lastUpload PROP.
 */
class KeeperUiBackend : public KeeperUiSimpleSource,
                        public LogosUiPluginContext
{
public:
    // ── keeper (universal → SYNC forward) ─────────────────────────────────────
    QString getBridgeStatus() override;
    QString getQueue() override;
    QString getPendingUpload() override;
    QString getLog() override;
    QString preserveItem(QString urlOrId) override;
    QString getConfig() override;
    QString setConfig(QString json) override;
    QString refreshStashStatus() override;
    QString getKeeperChannel() override;
    QString clearQueue() override;
    QString cancelItem(QString id) override;
    QString clearLog() override;

    // ── logos_beacon (universal → SYNC forward) ───────────────────────────────
    QString getNodeInfo() override;
    QString getBeaconConfig() override;
    QString getInscriptionLog() override;

    // ── stash (legacy → ASYNC fire-and-forget + internal poll) ────────────────
    QString startUpload(QString path, QString id, QString file) override;

    QString ping() override;

protected:
    void onContextReady() override;

private:
    // stash upload state machine (single in-flight)
    void ensurePollTimer();
    void pollStashResult();                              // one poll tick → getLatestLogosResultAsync
    void finishUpload(const QString& cid, const QString& error);

    QTimer* m_uploadPoll     = nullptr;
    bool    m_uploadInFlight = false;
    int     m_uploadAttempts = 0;
    QString m_uploadId;      // keeper queue item id
    QString m_uploadFile;    // logical file name (for onUploadResult)
    QString m_uploadBase;    // basename of the uploaded path (CID-match key)
};
