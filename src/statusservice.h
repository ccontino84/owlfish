// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_STATUSSERVICE_H
#define OWLFISH_STATUSSERVICE_H

#include <QDBusConnection>
#include <QObject>

class OwlfishController;

// Tells the settings page on the session bus whether the plugin runs in the
// compositor. No reply means it is not loaded. From a terminal:
//   dbus-send --session --print-reply --dest=io.github.ccontino84.owlfish
//             /owlfish io.github.ccontino84.owlfish.status
class OwlfishStatusService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.ccontino84.owlfish")

public:
    static const char *const ServiceName;
    static const char *const ObjectPath;

    explicit OwlfishStatusService(OwlfishController *controller);

    bool registerOn(QDBusConnection connection);

public slots:
    // OwlfishController::status()
    QString status() const;
    // Of the plugin running now, which is older than the installed package
    // until the next restart after an upgrade
    QString version() const;
    void resetCrashGuard();

private:
    OwlfishController *m_controller;
};

#endif
