// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "pqdisplay.h"
#include "logging.h"

#include <QElapsedTimer>
#include <QFile>

#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace {

const char *const Library = "libgbinder.so.1";
const char *const Device = "/dev/binder";
const char *const Service = "vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL/default";
const char *const Interface = "vendor.mediatek.hardware.pq_aidl.IPictureQuality_AIDL";

// Transaction codes of IPictureQuality_AIDL version 7; later versions only
// add calls
const int GetInterfaceVersion = 16777215;
const int SetColorMatrix3x3 = 51;
const int SetRgbGain = 62;

const int One = 2048;

// hw-release IDs of the devices where 2048 = 1.0 was checked by eye
const char *const VerifiedDevices[] = {
    "jp2601", // Jolla Phone (2026)
};

// libgbinder keeps these on the caller's stack; its own are smaller
struct Writer { const void *d[16]; };
struct Reader { const void *d[16]; };

template <typename Function>
bool resolve(void *library, Function *function, const char *name, const char **missing)
{
    *function = reinterpret_cast<Function>(dlsym(library, name));
    if (!*function && !*missing)
        *missing = name;
    return *function != nullptr;
}

class GbinderBackend : public PqDisplay::Backend
{
public:
    GbinderBackend();
    ~GbinderBackend() override;

    bool connect(QString *error) override;
    int interfaceVersion() override;
    bool setColorMatrix3x3(const int values[9], QString *error) override;
    bool setRgbGain(const int values[3], QString *error) override;

private:
    bool transact(int code, const int *values, int count, bool matrix, QString *error);

    // The few libgbinder functions used (include/gbinder_*.h, 1.1)
    struct Api
    {
        void *(*servicemanagerNew)(const char *dev);
        void (*servicemanagerUnref)(void *sm);
        void *(*getServiceSync)(void *sm, const char *name, int *status);
        void *(*clientNew)(void *object, const char *iface);
        void (*clientUnref)(void *client);
        void *(*clientNewRequest)(void *client);
        void *(*clientTransactSyncReply)(void *client, unsigned int code, void *request, int *status);
        void (*localRequestInitWriter)(void *request, Writer *writer);
        void (*localRequestUnref)(void *request);
        void (*writerAppendInt32)(Writer *writer, unsigned int value);
        void (*remoteReplyInitReader)(void *reply, Reader *reader);
        void (*remoteReplyUnref)(void *reply);
        int (*readerReadInt32)(Reader *reader, int *value);
    };

    Api m_api;
    void *m_library;
    void *m_serviceManager;
    void *m_client;
};

GbinderBackend::GbinderBackend()
    : m_library(nullptr)
    , m_serviceManager(nullptr)
    , m_client(nullptr)
{
    std::memset(&m_api, 0, sizeof(m_api));
}

GbinderBackend::~GbinderBackend()
{
    if (m_client)
        m_api.clientUnref(m_client);
    if (m_serviceManager)
        m_api.servicemanagerUnref(m_serviceManager);
    // The library stays loaded: it may have threads and GLib sources
}

bool GbinderBackend::connect(QString *error)
{
    m_library = dlopen(Library, RTLD_NOW | RTLD_LOCAL);
    if (!m_library) {
        *error = QStringLiteral("no %1").arg(QLatin1String(Library));
        return false;
    }

    const char *missing = nullptr;
    resolve(m_library, &m_api.servicemanagerNew, "gbinder_servicemanager_new", &missing);
    resolve(m_library, &m_api.servicemanagerUnref, "gbinder_servicemanager_unref", &missing);
    resolve(m_library, &m_api.getServiceSync, "gbinder_servicemanager_get_service_sync", &missing);
    resolve(m_library, &m_api.clientNew, "gbinder_client_new", &missing);
    resolve(m_library, &m_api.clientUnref, "gbinder_client_unref", &missing);
    resolve(m_library, &m_api.clientNewRequest, "gbinder_client_new_request", &missing);
    resolve(m_library, &m_api.clientTransactSyncReply, "gbinder_client_transact_sync_reply", &missing);
    resolve(m_library, &m_api.localRequestInitWriter, "gbinder_local_request_init_writer", &missing);
    resolve(m_library, &m_api.localRequestUnref, "gbinder_local_request_unref", &missing);
    resolve(m_library, &m_api.writerAppendInt32, "gbinder_writer_append_int32", &missing);
    resolve(m_library, &m_api.remoteReplyInitReader, "gbinder_remote_reply_init_reader", &missing);
    resolve(m_library, &m_api.remoteReplyUnref, "gbinder_remote_reply_unref", &missing);
    resolve(m_library, &m_api.readerReadInt32, "gbinder_reader_read_int32", &missing);
    if (missing) {
        *error = QStringLiteral("%1 missing").arg(QLatin1String(missing));
        return false;
    }

    m_serviceManager = m_api.servicemanagerNew(Device);
    if (!m_serviceManager) {
        *error = QStringLiteral("no service manager on %1").arg(QLatin1String(Device));
        return false;
    }
    int status = 0;
    // Owned by the service manager until the next idle; the client keeps
    // its own reference
    void *object = m_api.getServiceSync(m_serviceManager, Service, &status);
    if (!object) {
        *error = QStringLiteral("no picture quality service (%1)").arg(status);
        return false;
    }
    m_client = m_api.clientNew(object, Interface);
    if (!m_client) {
        *error = QStringLiteral("cannot create a client");
        return false;
    }
    return true;
}

int GbinderBackend::interfaceVersion()
{
    if (!m_client)
        return 0;
    // The reply is the status, then the version
    int status = 0;
    void *request = m_api.clientNewRequest(m_client);
    void *reply = m_api.clientTransactSyncReply(m_client, GetInterfaceVersion, request, &status);
    m_api.localRequestUnref(request);
    int exception = -1;
    int version = 0;
    if (reply) {
        Reader reader;
        m_api.remoteReplyInitReader(reply, &reader);
        m_api.readerReadInt32(&reader, &exception);
        m_api.readerReadInt32(&reader, &version);
        m_api.remoteReplyUnref(reply);
    }
    return exception == 0 ? version : 0;
}

bool GbinderBackend::setColorMatrix3x3(const int values[9], QString *error)
{
    return transact(SetColorMatrix3x3, values, 9, true, error);
}

bool GbinderBackend::setRgbGain(const int values[3], QString *error)
{
    return transact(SetRgbGain, values, 3, false, error);
}

bool GbinderBackend::transact(int code, const int *values, int count, bool matrix, QString *error)
{
    if (!m_client) {
        *error = QStringLiteral("not connected");
        return false;
    }

    void *request = m_api.clientNewRequest(m_client);
    Writer writer;
    m_api.localRequestInitWriter(request, &writer);
    if (matrix) {
        // int[3][3]: a length before each fixed-size array, as AIDL does
        m_api.writerAppendInt32(&writer, 3);
        for (int row = 0; row < 3; ++row) {
            m_api.writerAppendInt32(&writer, 3);
            for (int column = 0; column < 3; ++column)
                m_api.writerAppendInt32(&writer, unsigned(values[row * 3 + column]));
        }
    } else {
        for (int i = 0; i < count; ++i)
            m_api.writerAppendInt32(&writer, unsigned(values[i]));
    }
    // The service's transition in frames: at once
    m_api.writerAppendInt32(&writer, 0);

    int status = 0;
    void *reply = m_api.clientTransactSyncReply(m_client, unsigned(code), request, &status);
    m_api.localRequestUnref(request);
    // The status, then the service's Result: 0 is OK
    int exception = -1;
    int result = -1;
    if (reply) {
        Reader reader;
        m_api.remoteReplyInitReader(reply, &reader);
        m_api.readerReadInt32(&reader, &exception);
        m_api.readerReadInt32(&reader, &result);
        m_api.remoteReplyUnref(reply);
    }

    if (exception != 0 || result != 0) {
        *error = QStringLiteral("call %1 failed: status %2, exception %3, result %4")
                .arg(code).arg(status).arg(exception).arg(result);
        return false;
    }
    return true;
}

}

const char *const PqDisplay::HwReleasePath = "/etc/hw-release";

PqDisplay::PqDisplay(Backend *backend)
    : m_backend(backend ? backend : new GbinderBackend)
    , m_tried(false)
    , m_open(false)
    , m_version(0)
    , m_sent(false)
    , m_calls(0)
    , m_totalCallMs(0)
    , m_maxCallMs(0)
{
}

PqDisplay::~PqDisplay()
{
}

QString PqDisplay::deviceId(const QString &hwReleasePath)
{
    QFile file(hwReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith("ID="))
            continue;
        QByteArray value = trimmed.mid(3);
        if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\''))
                && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        return QString::fromUtf8(value);
    }
    return QString();
}

bool PqDisplay::isVerified(const QString &deviceId)
{
    for (const char *verified : VerifiedDevices) {
        if (deviceId == QLatin1String(verified))
            return true;
    }
    return false;
}

bool PqDisplay::open()
{
    if (m_tried)
        return m_open;
    m_tried = true;

    if (!m_backend->connect(&m_error))
        return false;
    m_version = m_backend->interfaceVersion();
    if (m_version < MinimumVersion) {
        m_error = QStringLiteral("picture quality service version %1, needs %2")
                .arg(m_version).arg(int(MinimumVersion));
        return false;
    }
    m_open = true;
    return true;
}

void PqDisplay::toFixed(const QMatrix3x3 &matrix, int out[9])
{
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            out[row * 3 + column] = int(std::lround(matrix(row, column) * One));
    }
}

bool PqDisplay::setMatrix(const QMatrix3x3 &matrix)
{
    if (!m_open)
        return false;
    int values[9];
    toFixed(matrix, values);
    if (m_sent && std::memcmp(values, m_last, sizeof(values)) == 0)
        return true;

    QElapsedTimer timer;
    timer.start();
    const bool ok = m_backend->setColorMatrix3x3(values, &m_error);
    countCall(timer.nsecsElapsed() / 1e6);
    if (!ok) {
        qCWarning(lcOwlfish) << "Display hardware:" << m_error;
        m_sent = false;
        return false;
    }
    std::memcpy(m_last, values, sizeof(values));
    m_sent = true;
    return true;
}

bool PqDisplay::reset()
{
    if (!m_open)
        return false;
    const int gain[3] = { One, One, One };
    QElapsedTimer timer;
    timer.start();
    const bool gainReset = m_backend->setRgbGain(gain, &m_error);
    countCall(timer.nsecsElapsed() / 1e6);
    if (!gainReset)
        qCWarning(lcOwlfish) << "Display hardware:" << m_error;
    // Also when the gain failed: the matrix is what Owlfish changes
    m_sent = false;
    return setMatrix(QMatrix3x3()) && gainReset;
}

void PqDisplay::countCall(double ms)
{
    ++m_calls;
    m_totalCallMs += ms;
    m_maxCallMs = qMax(m_maxCallMs, ms);
}
