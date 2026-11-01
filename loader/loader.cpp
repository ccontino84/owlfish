// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

// The lipstickaddons loader, contract version 1 (lipstick-addons.md).
//
// Every add-on package ships an identical copy of this file under its own
// library name; they share the key "lipstickaddons", so Qt creates only the
// first. QT_QPA_GENERIC_PLUGINS=lipstickaddons then starts every installed
// generic plugin with a key "lipstickaddon-<name>", in name order, with the
// same specification. Plugins without that key, such as Qt's own input
// plugins, are never touched. Keep this file unchanged: a change of
// behaviour needs a new key.

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QGenericPlugin>
#include <QGenericPluginFactory>
#include <QLoggingCategory>
#include <QStringList>

namespace {

Q_LOGGING_CATEGORY(lcAddons, "lipstickaddons", QtInfoMsg)

const char *const Prefix = "lipstickaddon-";

}

class LipstickAddonsPlugin : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "lipstickaddons.json")

public:
    QObject *create(const QString &key, const QString &specification) override
    {
        if (key.compare(QLatin1String("lipstickaddons"), Qt::CaseInsensitive) != 0)
            return nullptr;
        // An empty object rather than none: none makes Qt log "No such plugin"
        QObject *holder = new QObject;

        // Only in the compositor (processes it starts may inherit its
        // environment); "process=<name or *>" in the specification, for tests
        QString process(QStringLiteral("lipstick"));
        for (const QString &option : specification.split(QLatin1Char(':'))) {
            if (option.startsWith(QLatin1String("process=")))
                process = option.mid(8);
        }
        const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        if (process != QLatin1String("*") && executable != process)
            return holder;
        if (m_loaded)
            return holder;
        m_loaded = true;

        QStringList keys;
        for (const QString &candidate : QGenericPluginFactory::keys()) {
            if (candidate.startsWith(QLatin1String(Prefix), Qt::CaseInsensitive))
                keys << candidate.toLower();
        }
        keys.removeDuplicates();
        keys.sort();
        for (const QString &addon : keys) {
            // Owned by the holder, which Qt deletes with the application
            if (QObject *object = QGenericPluginFactory::create(addon, specification)) {
                object->setParent(holder);
                qCInfo(lcAddons) << "Started" << addon;
            } else {
                qCWarning(lcAddons) << "Could not start" << addon;
            }
        }
        return holder;
    }

private:
    // One plugin instance per process
    bool m_loaded = false;
};

#include "loader.moc"
