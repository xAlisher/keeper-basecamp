#pragma once

#include <QObject>
#include <QString>
#include "interface.h"

class KeeperInterface : public PluginInterface
{
public:
    virtual ~KeeperInterface() {}

    // Preserve a single Internet Archive item.
    // urlOrId: full URL (archive.org/details/{id}) or bare identifier.
    // Returns {"queued":true,"id":"..."} or {"success":false,"error":"..."}.
    Q_INVOKABLE virtual QString preserveItem(const QString& urlOrId) = 0;

    // Queue up to `limit` items from an IA collection.
    // Returns {"queued":<n>} or {"success":false,"error":"..."}.
    Q_INVOKABLE virtual QString preserveCollection(const QString& name, int limit) = 0;

    // Cancel a pending or in-progress item.
    Q_INVOKABLE virtual QString cancelItem(const QString& identifier) = 0;

    // Current queue state.
    // Returns JSON array: [{id, title, status, files:[{name,cid,status}]}]
    Q_INVOKABLE virtual QString getQueue() = 0;

    // Last 200 completed preservation records.
    Q_INVOKABLE virtual QString getLog() = 0;

    // Configuration: {"maxFilesPerItem":<n>,"skipDerivatives":bool}
    Q_INVOKABLE virtual QString getConfig() = 0;
    Q_INVOKABLE virtual QString setConfig(const QString& json) = 0;

signals:
    void eventResponse(const QString& name, const QVariantList& data);
};

#define KeeperInterface_iid "org.logos.KeeperInterface"
Q_DECLARE_INTERFACE(KeeperInterface, KeeperInterface_iid)
