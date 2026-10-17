// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_QUALCOMM_HELPER_H
#define OWLFISH_QUALCOMM_HELPER_H

#include "qdcm.h"

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QList>
#include <QObject>

// The system service that sets the display hardware's colour matrix for
// Owlfish on Qualcomm devices such as the Xperias (Qdcm). Runs as graphics, the one thing
// lipstick can't do itself. Accepts calls from lipstick (and root) only.
//
// The hardware forgets the matrix when the display turns off, while the
// composer still reports it as set, so it is sent again whenever the display
// turns on. When lipstick goes away, the matrix is turned off.
class QualcommHelper : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.ccontino84.owlfish.Qualcomm")

public:
    static const char *const ServiceName;
    static const char *const ObjectPath;

    explicit QualcommHelper(const Qdcm::Session::Run &run, QObject *parent = nullptr);
    ~QualcommHelper() override;

    bool registerOn(QDBusConnection connection);
    // For tests: callers with this user ID are accepted too
    void allowUid(uint uid) { m_allowedUid = uid; }
    // Quits the application soon after a failed check: nothing for it to do
    void setQuitAfterFailedProbe(bool quit) { m_quitAfterFailedProbe = quit; }

public slots:
    // Checks the hardware (Qdcm::Session::probe()); leaves the PCC off.
    // info: what was found, or why it can't be used.
    bool Probe(QString &info);
    // 9 values, rows are the output R, G, B; the identity turns the PCC off.
    // Applied later; the latest call wins. Applied() or Failed() follow.
    Q_NOREPLY void SetMatrix(const QList<double> &values);
    Q_NOREPLY void Reset();

signals:
    // The hardware has the matrix: lipstick should draw a frame, which
    // makes the display show it
    void Applied();
    void Failed(const QString &error);

private slots:
    void displayStatusChanged(const QString &status);
    void ownerGone(const QString &service);
    void applyPending();

private:
    bool authorized();
    void request(const double matrix[9]);

    Qdcm::Session m_session;
    QDBusServiceWatcher m_watcher;
    // Unique names already checked
    QHash<QString, bool> m_callers;
    double m_pending[9];
    bool m_hasPending;
    bool m_applyQueued;
    // What the hardware has, as far as known
    double m_current[9];
    bool m_displayOff;
    uint m_allowedUid;
    bool m_quitAfterFailedProbe;
};

#endif
