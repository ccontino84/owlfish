// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_QUALCOMMCLIENT_H
#define OWLFISH_QUALCOMMCLIENT_H

#include <QDBusConnection>
#include <QGenericMatrix>
#include <QObject>
#include <QScopedPointer>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <functional>

// The colour matrix of the display hardware on Qualcomm devices, through
// Owlfish's helper (qualcomm-helper/helper.h, in the owlfish-qualcomm
// package) on the system bus. Lipstick can't call the hardware itself: the
// composer only accepts a few system users. Used where the helper is
// installed and its check of the hardware passes; nothing is called
// elsewhere.
//
// Unlike PqDisplay, nothing waits: the check and the calls are answered
// later. The helper turns the matrix off when lipstick goes away and sends
// it again when the display turns on. The display only shows a new matrix
// with the next frame, so applied() asks for one.
class QualcommClient : public QObject
{
    Q_OBJECT

public:
    // The helper's calls. The real one goes over D-Bus; tests use a fake.
    class Backend
    {
    public:
        virtual ~Backend() {}
        // The helper is installed (its D-Bus activation file exists)
        virtual bool isInstalled() = 0;
        // done(ok, what was found or why it can't be used), later
        virtual void probe(const std::function<void(bool, const QString &)> &done) = 0;
        // Rows are the output R, G, B; answered by applied or failed
        virtual void setMatrix(const QVector<double> &values) = 0;
        virtual void reset() = 0;

        // Set by QualcommClient
        std::function<void()> applied;
        std::function<void(const QString &)> failed;
    };

    enum State { NotChecked, Checking, Ready, Unavailable };

    static const char *const ServiceName;
    static const char *const ObjectPath;
    static const char *const Interface;
    static const char *const ActivationFile;

    // Takes ownership of backend; nullptr is the D-Bus one on the system bus
    explicit QualcommClient(Backend *backend = nullptr, QObject *parent = nullptr);
    // The D-Bus one, on another bus for tests
    static Backend *createDBusBackend(const QDBusConnection &connection);
    ~QualcommClient() override;

    // Checked once
    bool isInstalled();
    State state() const { return m_state; }
    // Starts the check; checked() follows
    void check();
    // What the helper found, or why it can't be used
    QString info() const { return m_info; }

    // Skipped if the same as the last one sent; false unless Ready
    bool setMatrix(const QMatrix3x3 &matrix);
    // Turns the matrix off; also after a failed call
    void reset();
    int calls() const { return m_calls; }

signals:
    void checked();
    // The hardware has the matrix: draw a frame
    void applied();
    // A call failed; state() is Unavailable from now on
    void failed();

private:
    void probed(bool ok, const QString &info);
    void callFailed(const QString &error);

    QScopedPointer<Backend> m_backend;
    int m_installed;
    State m_state;
    bool m_wasReady;
    QString m_info;
    bool m_sent;
    QVector<double> m_last;
    int m_calls;
};

#endif
