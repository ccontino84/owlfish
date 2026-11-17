// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_FRONTAPP_H
#define OWLFISH_FRONTAPP_H

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QStringList>

// Follows the window lipstick shows in front (its topmostWindowId) and tells
// whether its app is one of the exceptions, the launcher entries the user
// chose on the settings page. Home, the lock screen and lipstick's own
// windows are never an exception.
class FrontApp : public QObject
{
    Q_OBJECT

public:
    // compositor: lipstick's LipstickCompositor window. Anything without a
    // topmostWindowId (the tests' view) never has an app in front.
    explicit FrontApp(QObject *compositor, QObject *parent = nullptr);

    // Launcher entry file names; looked up in desktopDirs
    void setExceptions(const QStringList &desktopIds);
    // For tests: where launcher entries are looked up (AppMatcher::defaultDesktopDirs())
    void setDesktopDirs(const QStringList &dirs) { m_desktopDirs = dirs; }
    // How many of the exceptions were found
    int exceptionCount() const { return m_exceptionCount; }

    // The app in front is one of the exceptions
    bool isException() const { return m_exception; }
    // The names the app in front is known by (AppMatcher::windowKeys())
    QStringList frontKeys() const { return m_frontKeys; }
    // For tests, and where the window can't be read: the app in front by its
    // names
    void setFrontKeys(const QStringList &keys);

signals:
    void exceptionChanged(bool exception);

private slots:
    void update();

private:
    void evaluate();

    QPointer<QObject> m_compositor;
    QStringList m_desktopDirs;
    QStringList m_exceptions;
    QSet<QString> m_keys;
    int m_exceptionCount;
    QStringList m_frontKeys;
    bool m_exception;
};

#endif
