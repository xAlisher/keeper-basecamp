#pragma once

#include <QObject>
#include <QString>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QTimer>

#include "keeper_interface.h"
#include "logos_api.h"
#include "logos_api_client.h"
#include "logos_object.h"

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

class KeeperPlugin : public QObject, public KeeperInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KeeperInterface_iid FILE "metadata.json")
    Q_INTERFACES(KeeperInterface PluginInterface)

public:
    KeeperPlugin();
    ~KeeperPlugin() override = default;

    QString name()    const override { return "keeper"; }
    QString version() const override { return "0.1.0"; }
    Q_INVOKABLE void initLogos(LogosAPI* api);

    Q_INVOKABLE QString preserveItem(const QString& urlOrId) override;
    Q_INVOKABLE QString preserveCollection(const QString& name, int limit) override;
    Q_INVOKABLE QString cancelItem(const QString& identifier) override;
    Q_INVOKABLE QString getQueue() override;
    Q_INVOKABLE QString getLog() override;
    Q_INVOKABLE QString getConfig() override;
    Q_INVOKABLE QString setConfig(const QString& json) override;
    Q_INVOKABLE QString clearLog();
    Q_INVOKABLE QString clearQueue();
    Q_INVOKABLE QString getLogosMsgStatus() const;
    Q_INVOKABLE QString getInscriptionQueue() const;
    Q_INVOKABLE QString markInscribed(const QString& cid);

    // Logos Messaging pairing
    Q_INVOKABLE QString addPairedExtension(const QString& hexPubkey);
    Q_INVOKABLE QString removePairedExtension(const QString& hexPubkey);
    Q_INVOKABLE QString getPairedExtensions();

signals:
    void eventResponse(const QString& name, const QVariantList& data);

private:
    // IA helpers
    QString parseIdentifier(const QString& urlOrId);
    void    fetchMetadata(const QString& identifier);
    void    processNextFile();
    void    downloadFile(const QString& identifier, const KeeperFile& file);
    void    uploadToStash(const QString& identifier, const QString& localPath, const QString& fileName);
    void    pollForFileCid(const QString& identifier, const QString& fileName,
                           const QString& tmpPath, int attempts);
    void    finishItem(const QString& identifier, const QString& collectionCid);
    void    inscribeToBeacon(const QString& identifier, const QString& cid);
    void    pollForTxHash(const QString& identifier, const QString& cid, int attempts);
    void    advanceQueue();

    // Logos Messaging (delivery_module)
    void    initDeliveryModule();
    void    onWakuMessageReceived(const QVariantList& data);
    void    onWakuConnectionChanged(const QVariantList& data);
    void    publishStatus(const QString& identifier, const QString& status,
                          const QString& cid = {}, double progress = -1.0,
                          const QString& error = {});

    // Persistence
    void loadQueue();
    void saveQueue();
    void saveInscriptionQueue();
    void savePairedExtensions();
    void appendLog(const KeeperItem& item);
    QString persistPath(const QString& filename);

    // State
    void emitEvent(const QString& name, const QVariantList& data);
    QString itemsToJson(const QList<KeeperItem>& items);

    LogosAPIClient*  stashClient_    = nullptr;
    LogosAPIClient*  beaconClient_   = nullptr;
    LogosAPIClient*  deliveryClient_ = nullptr;
    LogosObject*     deliveryObject_ = nullptr;

    QList<KeeperItem>  queue_;
    QList<QJsonObject> log_;
    QList<QJsonObject> inscriptionQueue_;
    bool               busy_ = false;

    QNetworkAccessManager* nam_ = nullptr;

    // Logos Messaging state
    bool    lmReady_  = false;
    QString lmStatus_;
    QStringList m_pairedPubkeys;

    // Config
    int  maxFilesPerItem_ = 20;
    bool skipDerivatives_ = true;

    QString m_persistencePath;
};
