#pragma once

#include <memory>
#include <string>

#include <logos_module_context.h>
#include <logos_result.h>

/**
 * @brief keeper — universal (LogosModuleContext) form of the Keeper module.
 *
 * v0.2 migration of the legacy Qt KeeperPlugin (QObject/PluginInterface). The
 * public API is Qt-free (StdLogosResult / std::string); the battle-tested Qt
 * internals — the archive.org fetch/download state machine (QNetworkAccessManager),
 * the localhost KeeperHttpBridge (QTcpServer on 127.0.0.1:7355), QJson persistence
 * (QFile/QDir/QStandardPaths), QTimer sequencing and QRegularExpression parsing —
 * are preserved verbatim behind a pimpl in the .cpp. Universal core modules may use
 * Qt internally (Qt6::Core+Network link via logos_module()).
 *
 * The Impl ctor does the old initLogos() work: resolve the persistence path
 * (QStandardPaths::AppDataLocation + "/keeper"), loadQueue(), and start the bridge.
 * Keeper is fully self-contained (no getClient/callModule): stash upload + beacon
 * inscription are still driven by the UI polling getPendingUpload()/getInscriptionQueue().
 */
class KeeperImpl : public LogosModuleContext
{
public:
    KeeperImpl();
    ~KeeperImpl();

    // ── Preservation queue ──────────────────────────────────────────────────────
    StdLogosResult preserveItem(const std::string& urlOrId);
    StdLogosResult preserveCollection(const std::string& name, int limit);
    StdLogosResult cancelItem(const std::string& identifier);
    StdLogosResult getQueue();
    StdLogosResult getLog();

    // ── Config ──────────────────────────────────────────────────────────────────
    StdLogosResult getConfig();
    StdLogosResult setConfig(const std::string& json);
    StdLogosResult clearLog();
    StdLogosResult clearQueue();

    // ── Bridge + upload/inscription handoff (UI-polled) ─────────────────────────
    StdLogosResult getBridgeStatus();
    StdLogosResult getInscriptionQueue();
    StdLogosResult markInscribed(const std::string& cid);
    StdLogosResult getPendingUpload();
    StdLogosResult onUploadResult(const std::string& identifier, const std::string& fileName, const std::string& cid);

    std::string name() const { return "keeper"; }
    std::string version() const { return "0.2.0"; }

private:
    struct Impl;                     // Qt state (QNAM state machine, QTcpServer bridge, QJson persistence)
    std::unique_ptr<Impl> d;
};
