// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "statusservice.h"
#include "controller.h"
#include "logging.h"
#include <owlfish_version.h>

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusServiceWatcher>

const char *const OwlfishStatusService::ServiceName = "io.github.ccontino84.owlfish";
const char *const OwlfishStatusService::ObjectPath = "/owlfish";

OwlfishStatusService::OwlfishStatusService(OwlfishController *controller)
    : QObject(controller)
    , m_controller(controller)
    , m_previewWatcher(new QDBusServiceWatcher(this))
{
    m_previewWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_previewWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
        qCInfo(lcOwlfish) << "Preview caller" << service << "left the bus";
        m_previewWatcher->setWatchedServices(QStringList());
        m_controller->setPreview(QString());
    });
}

bool OwlfishStatusService::registerOn(QDBusConnection connection)
{
    m_previewWatcher->setConnection(connection);
    if (!connection.registerObject(QLatin1String(ObjectPath), this, QDBusConnection::ExportAllSlots)) {
        qCWarning(lcOwlfish) << "Cannot register" << ObjectPath << "on D-Bus";
        return false;
    }
    if (!connection.registerService(QLatin1String(ServiceName))) {
        qCWarning(lcOwlfish) << "Cannot register" << ServiceName << "on D-Bus:"
                              << connection.lastError().message();
        connection.unregisterObject(QLatin1String(ObjectPath));
        return false;
    }
    return true;
}

QString OwlfishStatusService::status() const
{
    return m_controller->status();
}

QString OwlfishStatusService::version() const
{
    return QStringLiteral(OWLFISH_VERSION);
}

void OwlfishStatusService::resetCrashGuard()
{
    qCInfo(lcOwlfish) << "Crash guard reset from D-Bus";
    m_controller->resetCrashGuard();
}

void OwlfishStatusService::reload()
{
    m_controller->reload();
}

QString OwlfishStatusService::renderer() const
{
    return m_controller->renderer();
}

QString OwlfishStatusService::diagnostics() const
{
    return m_controller->diagnostics();
}

bool OwlfishStatusService::preview(const QString &part)
{
    const bool on = m_controller->setPreview(part);
    // The settings page may crash or be closed with a finger still down
    QStringList watched;
    if (on && calledFromDBus())
        watched << message().service();
    if (watched != m_previewWatcher->watchedServices())
        m_previewWatcher->setWatchedServices(watched);
    return on;
}
