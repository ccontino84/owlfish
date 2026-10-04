// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_PQDISPLAY_H
#define OWLFISH_PQDISPLAY_H

#include <QGenericMatrix>
#include <QScopedPointer>
#include <QString>

// The colour matrix of the display hardware on MediaTek devices, through the
// vendor's picture quality service (vendor.mediatek.hardware.pq_aidl) on
// /dev/binder. libgbinder is loaded at run time, so devices without it (or
// without the service) simply don't have this. The service applies the
// matrix in linear light. Its fixed point follows the display controller's
// colour correction (CCORR) block: 2048 = 1.0 with 13-bit coefficients (the
// Jolla Phone), 1024 with 12-bit ones. It keeps the matrix until it is
// changed or the device restarts: whoever sets one must set the identity
// again.
class PqDisplay
{
public:
    // The service's calls. The real one goes through libgbinder; tests use
    // a fake.
    class Backend
    {
    public:
        virtual ~Backend() {}
        // Looks the service up; false with *error if that fails
        virtual bool connect(QString *error) = 0;
        // 0 if the call fails
        virtual int interfaceVersion() = 0;
        // In the service's fixed point; false with *error if the call fails
        virtual bool setColorMatrix3x3(const int values[9], QString *error) = 0;
        virtual bool setRgbGain(const int values[3], QString *error) = 0;
    };

    // The CCORR block as the device tree describes it; 0 or -1 where a
    // property is missing
    struct Ccorr
    {
        bool found = false;
        int bits = 0;
        int engines = 0;
        int linear = -1;
    };

    static const int MinimumVersion = 7;
    static const char *const HwReleasePath;
    static const char *const DeviceTreePath;

    // Takes ownership of backend; nullptr is the libgbinder one
    explicit PqDisplay(Backend *backend = nullptr);
    ~PqDisplay();

    // ID= in /etc/hw-release, empty if there is none
    static QString deviceId(const QString &hwReleasePath = QLatin1String(HwReleasePath));
    // Devices where the service's scale (2048 = 1.0) has been checked
    static bool isVerified(const QString &deviceId);
    // Found through the device tree's "ccorr0" alias, on a MediaTek node.
    // A few small file reads, no other side effect.
    static Ccorr ccorr(const QString &deviceTreePath = QLatin1String(DeviceTreePath));
    // The layout checked on the Jolla Phone, where 2048 = 1.0: 13 bits, one
    // engine, linear
    static bool isKnown(const Ccorr &ccorr);
    // "13 bits, 1 engine, linear", or "none"
    static QString describe(const Ccorr &ccorr);

    // Connects and checks the interface version; false with error() if
    // that fails. Only tries once.
    bool open();
    bool isOpen() const { return m_open; }
    int interfaceVersion() const { return m_version; }
    // Why open() or the last call failed
    QString error() const { return m_error; }

    // The whole transform, in linear light. Skipped if the same as the last
    // one sent.
    bool setMatrix(const QMatrix3x3 &matrix);
    // Identity, and the service's separate RGB gain back to 1
    bool reset();

    int calls() const { return m_calls; }
    double meanCallMs() const { return m_calls ? m_totalCallMs / m_calls : 0; }
    double maxCallMs() const { return m_maxCallMs; }

    // Rounded to the service's fixed point
    static void toFixed(const QMatrix3x3 &matrix, int out[9]);

private:
    void countCall(double ms);

    QScopedPointer<Backend> m_backend;
    bool m_tried;
    bool m_open;
    int m_version;
    QString m_error;
    bool m_sent;
    int m_last[9];
    int m_calls;
    double m_totalCallMs;
    double m_maxCallMs;
};

#endif
