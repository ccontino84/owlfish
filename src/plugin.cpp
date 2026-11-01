// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "controller.h"
#include "logging.h"
#include "statusservice.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGenericPlugin>
#include <QPointer>
#include <QTimer>

// Loaded by the lipstickaddons loader (key lipstickaddon-owlfish, see
// lipstick-addons.md) through QT_QPA_GENERIC_PLUGINS=lipstickaddons
// (00-owlfish.conf, written by config/update-env when the package is
// installed), or directly by its own key, owlfish.
//
// An optional specification tunes where the filter attaches, for testing:
//   owlfish:class=<QQuickWindow subclass>:process=<executable name or *>
// Defaults: class=LipstickCompositor, process=lipstick
class OwlfishPlugin : public QGenericPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QGenericPluginFactoryInterface_iid FILE "owlfish.json")

public:
    QObject *create(const QString &key, const QString &specification) override
    {
        if (key.compare(QLatin1String("owlfish"), Qt::CaseInsensitive) != 0
                && key.compare(QLatin1String("lipstickaddon-owlfish"), Qt::CaseInsensitive) != 0)
            return nullptr;

        QByteArray windowClass("LipstickCompositor");
        QString process(QStringLiteral("lipstick"));
        for (const QString &option : specification.split(QLatin1Char(':'))) {
            if (option.isEmpty())
                continue;
            const int separator = option.indexOf(QLatin1Char('='));
            const QString name = option.left(separator);
            const QString value = separator >= 0 ? option.mid(separator + 1) : QString();
            if (name == QLatin1String("class"))
                windowClass = value.toLatin1();
            else if (name == QLatin1String("process"))
                process = value;
            else
                qCWarning(lcOwlfish) << "Unknown option" << option;
        }

        // Processes started by the compositor may inherit its environment;
        // stay out of everything but the compositor itself
        const QString executable = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
        if (process != QLatin1String("*") && executable != process) {
            qCDebug(lcOwlfish) << "Not activating in" << executable;
            return new QObject;
        }

        // Once per process: the same plugin may be named by the environment
        // and found by the loader. An empty object, not none, which makes Qt
        // log "No such plugin". Again once the first is gone (tests).
        if (m_controller) {
            qCDebug(lcOwlfish) << "Already running";
            return new QObject;
        }

        OwlfishController *controller = new OwlfishController(windowClass);
        m_controller = controller;
        // Also when the crash guard has tripped, so that the settings page
        // can say so. Created during QGuiApplication's setup; register once
        // the event loop runs.
        OwlfishStatusService *service = new OwlfishStatusService(controller);
        QTimer::singleShot(0, service, [service]() {
            service->registerOn(QDBusConnection::sessionBus());
        });
        return controller;
    }

private:
    // One plugin instance per process: Qt keeps it as long as the library
    QPointer<OwlfishController> m_controller;
};

#include "plugin.moc"
