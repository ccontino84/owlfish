// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "qualcommclient.h"
#include "logging.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QPointer>

#include <cmath>

namespace {

// Starting the helper and its check of the hardware take a few calls of
// the vendor tool
const int ProbeTimeoutMs = 20000;

class DBusBackend : public QObject, public QualcommClient::Backend
{
    Q_OBJECT

public:
    explicit DBusBackend(const QDBusConnection &connection) : m_bus(connection) {}

    bool isInstalled() override
    {
        return QFile::exists(QLatin1String(QualcommClient::ActivationFile));
    }

    void probe(const std::function<void(bool, const QString &)> &done) override
    {
        // Only where the helper is installed
        m_bus.connect(
                QLatin1String(QualcommClient::ServiceName), QLatin1String(QualcommClient::ObjectPath),
                QLatin1String(QualcommClient::Interface), QStringLiteral("Applied"),
                this, SLOT(onApplied()));
        m_bus.connect(
                QLatin1String(QualcommClient::ServiceName), QLatin1String(QualcommClient::ObjectPath),
                QLatin1String(QualcommClient::Interface), QStringLiteral("Failed"),
                this, SLOT(onFailed(QString)));
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                m_bus.asyncCall(call(QStringLiteral("Probe")), ProbeTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [done](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            QDBusPendingReply<bool, QString> reply = *watcher;
            if (reply.isError())
                done(false, reply.error().message());
            else
                done(reply.argumentAt<0>(), reply.argumentAt<1>());
        });
    }

    void setMatrix(const QVector<double> &values) override
    {
        // An array of doubles, without registering a type inside lipstick
        QDBusArgument array;
        array.beginArray(qMetaTypeId<double>());
        for (double value : values)
            array << value;
        array.endArray();
        QDBusMessage message = call(QStringLiteral("SetMatrix"));
        message << QVariant::fromValue(array);
        send(message);
    }

    void reset() override
    {
        send(call(QStringLiteral("Reset")));
    }

private slots:
    void onApplied()
    {
        if (applied)
            applied();
    }

    void onFailed(const QString &error)
    {
        if (failed)
            failed(error);
    }

private:
    QDBusConnection m_bus;

    static QDBusMessage call(const QString &method)
    {
        return QDBusMessage::createMethodCall(QLatin1String(QualcommClient::ServiceName),
                                              QLatin1String(QualcommClient::ObjectPath),
                                              QLatin1String(QualcommClient::Interface), method);
    }

    void send(const QDBusMessage &message)
    {
        // Answered by the Applied and Failed signals
        if (!m_bus.send(message))
            qCWarning(lcOwlfish) << "Qualcomm helper:" << m_bus.lastError().message();
    }
};

}

const char *const QualcommClient::ServiceName = "io.github.ccontino84.owlfish.Qualcomm";
const char *const QualcommClient::ObjectPath = "/qualcomm";
const char *const QualcommClient::Interface = "io.github.ccontino84.owlfish.Qualcomm";
const char *const QualcommClient::ActivationFile =
        "/usr/share/dbus-1/system-services/io.github.ccontino84.owlfish.Qualcomm.service";

QualcommClient::QualcommClient(Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend ? backend : new DBusBackend(QDBusConnection::systemBus()))
    , m_installed(-1)
    , m_state(NotChecked)
    , m_wasReady(false)
    , m_sent(false)
    , m_calls(0)
{
    QPointer<QualcommClient> self(this);
    m_backend->applied = [self]() {
        if (self && self->m_state == Ready)
            emit self->applied();
    };
    m_backend->failed = [self](const QString &error) {
        if (self)
            self->callFailed(error);
    };
}

QualcommClient::~QualcommClient()
{
}

QualcommClient::Backend *QualcommClient::createDBusBackend(const QDBusConnection &connection)
{
    return new DBusBackend(connection);
}

bool QualcommClient::isInstalled()
{
    if (m_installed < 0)
        m_installed = m_backend->isInstalled() ? 1 : 0;
    return m_installed == 1;
}

void QualcommClient::check()
{
    if (m_state != NotChecked)
        return;
    m_state = Checking;
    QPointer<QualcommClient> self(this);
    m_backend->probe([self](bool ok, const QString &info) {
        if (self)
            self->probed(ok, info);
    });
}

void QualcommClient::probed(bool ok, const QString &info)
{
    if (m_state != Checking)
        return;
    m_state = ok ? Ready : Unavailable;
    m_wasReady = ok;
    m_info = info.isEmpty() ? QStringLiteral("no answer") : info;
    qCInfo(lcOwlfish) << "Qualcomm helper:" << (ok ? "ready," : "not usable,") << qPrintable(m_info);
    emit checked();
}

void QualcommClient::callFailed(const QString &error)
{
    // Also signals meant for an earlier run, which don't matter before the
    // check
    if (m_state != Ready)
        return;
    m_state = Unavailable;
    m_info = error;
    m_sent = false;
    qCWarning(lcOwlfish) << "Qualcomm helper:" << error;
    emit failed();
}

bool QualcommClient::setMatrix(const QMatrix3x3 &matrix)
{
    if (m_state != Ready)
        return false;
    QVector<double> values;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            // Finer than the eye can tell, coarse enough to skip repeats
            values << std::round(double(matrix(row, column)) * 1e5) / 1e5;
    }
    if (m_sent && values == m_last)
        return true;
    m_backend->setMatrix(values);
    ++m_calls;
    m_last = values;
    m_sent = true;
    return true;
}

void QualcommClient::reset()
{
    // Also after a failed call: whatever can still be reset
    if (!m_wasReady)
        return;
    m_backend->reset();
    ++m_calls;
    m_sent = false;
}

#include "qualcommclient.moc"
