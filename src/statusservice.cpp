// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "statusservice.h"
#include "controller.h"
#include "logging.h"

#include <QDBusError>

const char *const OwlfishStatusService::ServiceName = "io.github.ccontino84.owlfish";
const char *const OwlfishStatusService::ObjectPath = "/owlfish";

OwlfishStatusService::OwlfishStatusService(OwlfishController *controller)
    : QObject(controller)
    , m_controller(controller)
{
}

bool OwlfishStatusService::registerOn(QDBusConnection connection)
{
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
