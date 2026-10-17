// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "helper.h"

#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QFile>
#include <QTimer>
#include <QtDebug>

#include <cmath>
#include <cstring>

namespace {

const double Identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
// Sailfish OS users start here; lipstick runs as the device owner
const uint FirstUserUid = 100000;

// From its command line; true if that can't be read, so the user check
// alone decides
bool isLipstick(uint pid)
{
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!pid || !file.open(QIODevice::ReadOnly))
        return true;
    const QByteArray program = file.read(4096).split('\0').value(0);
    return program.mid(program.lastIndexOf('/') + 1) == "lipstick";
}

}

const char *const QualcommHelper::ServiceName = "io.github.ccontino84.owlfish.Qualcomm";
const char *const QualcommHelper::ObjectPath = "/qualcomm";

QualcommHelper::QualcommHelper(const Qdcm::Session::Run &run, QObject *parent)
    : QObject(parent)
    , m_session(run)
    , m_hasPending(false)
    , m_applyQueued(false)
    , m_displayOff(false)
    , m_allowedUid(0)
    , m_quitAfterFailedProbe(false)
{
    std::memcpy(m_current, Identity, sizeof(m_current));
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &QualcommHelper::ownerGone);
}

QualcommHelper::~QualcommHelper()
{
    QString error;
    if (!Qdcm::isIdentity(m_current) && !m_session.apply(Identity, &error))
        qWarning() << "Cannot turn the colour matrix off:" << error;
    m_session.close();
}

bool QualcommHelper::registerOn(QDBusConnection connection)
{
    m_watcher.setConnection(connection);
    if (!connection.registerObject(QLatin1String(ObjectPath), this,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qWarning() << "Cannot register" << ObjectPath;
        return false;
    }
    if (!connection.registerService(QLatin1String(ServiceName))) {
        qWarning() << "Cannot register" << ServiceName << connection.lastError().message();
        return false;
    }
    connection.connect(QStringLiteral("com.nokia.mce"), QStringLiteral("/com/nokia/mce/signal"),
                       QStringLiteral("com.nokia.mce.signal"), QStringLiteral("display_status_ind"),
                       this, SLOT(displayStatusChanged(QString)));
    return true;
}

bool QualcommHelper::authorized()
{
    if (!calledFromDBus())
        return true;
    const QString sender = message().service();
    QHash<QString, bool>::const_iterator known = m_callers.constFind(sender);
    bool allowed;
    if (known != m_callers.constEnd()) {
        allowed = known.value();
    } else {
        QDBusConnectionInterface *bus = connection().interface();
        const QDBusReply<uint> uid = bus->serviceUid(sender);
        const QDBusReply<uint> pid = bus->servicePid(sender);
        allowed = uid.isValid() && (uid.value() == 0 || uid.value() == m_allowedUid
                                    || (uid.value() >= FirstUserUid && isLipstick(pid.isValid() ? pid.value() : 0)));
        m_callers.insert(sender, allowed);
        if (!allowed)
            qWarning() << "Refused" << sender << "uid" << uid.value() << "pid" << pid.value();
    }
    if (!allowed)
        sendErrorReply(QDBusError::AccessDenied, QStringLiteral("Only the home screen may set the display colours"));
    return allowed;
}

bool QualcommHelper::Probe(QString &info)
{
    if (!authorized())
        return false;
    QString error;
    const bool ok = m_session.probe(&error);
    // Off either way
    std::memcpy(m_current, Identity, sizeof(m_current));
    m_hasPending = false;
    info = ok ? QStringLiteral("QDCM PCC, read back checked") : error;
    qInfo() << "Probe:" << qPrintable(info);
    // After the reply has gone out
    if (!ok && m_quitAfterFailedProbe)
        QTimer::singleShot(1000, qApp, &QCoreApplication::quit);
    return ok;
}

void QualcommHelper::SetMatrix(const QList<double> &values)
{
    if (!authorized())
        return;
    if (values.size() != 9) {
        emit Failed(QStringLiteral("%1 values, needs 9").arg(values.size()));
        return;
    }
    // The latest caller owns the matrix: turned off when it goes away
    const QString sender = calledFromDBus() ? message().service() : QString();
    if (!sender.isEmpty() && m_watcher.watchedServices() != QStringList(sender))
        m_watcher.setWatchedServices(QStringList(sender));
    double matrix[9];
    for (int i = 0; i < 9; ++i)
        matrix[i] = values.at(i);
    request(matrix);
}

void QualcommHelper::Reset()
{
    if (!authorized())
        return;
    request(Identity);
}

void QualcommHelper::request(const double matrix[9])
{
    std::memcpy(m_pending, matrix, sizeof(m_pending));
    m_hasPending = true;
    // Calls that arrive while one is applied are handled after it, so only
    // the latest of them is applied: a slow call means fewer steps, never
    // a backlog
    if (!m_applyQueued) {
        m_applyQueued = true;
        QTimer::singleShot(0, this, &QualcommHelper::applyPending);
    }
}

void QualcommHelper::applyPending()
{
    m_applyQueued = false;
    if (!m_hasPending)
        return;
    m_hasPending = false;
    double matrix[9];
    std::memcpy(matrix, m_pending, sizeof(matrix));

    QString error;
    if (!Qdcm::isAcceptable(matrix, &error)) {
        qWarning() << "Matrix refused:" << error;
        emit Failed(error);
        return;
    }
    if (!m_session.apply(matrix, &error)) {
        qWarning() << "Cannot set the colour matrix:" << error;
        // Unknown now: the next request is sent whatever it is
        for (double &value : m_current)
            value = std::nan("");
        emit Failed(error);
        return;
    }
    std::memcpy(m_current, matrix, sizeof(m_current));
    emit Applied();
}

void QualcommHelper::displayStatusChanged(const QString &status)
{
    // "on", "dimmed" or "off"
    const bool off = status == QLatin1String("off");
    const bool turnedOn = m_displayOff && !off;
    m_displayOff = off;
    // The hardware lost it while the display was off
    if (turnedOn && !m_hasPending && !Qdcm::isIdentity(m_current) && !std::isnan(m_current[0])) {
        double matrix[9];
        std::memcpy(matrix, m_current, sizeof(matrix));
        request(matrix);
    }
}

void QualcommHelper::ownerGone(const QString &service)
{
    qInfo() << service << "went away; turning the colour matrix off";
    m_watcher.setWatchedServices(QStringList());
    m_callers.remove(service);
    request(Identity);
}
