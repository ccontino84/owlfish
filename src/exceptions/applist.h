// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_APPLIST_H
#define OWLFISH_APPLIST_H

#include <QStringList>

// The apps of the launcher, for the settings page's Exceptions: read from
// the launcher entries here, because a LauncherModel in the settings app
// would rewrite the home screen's icon order.
class AppList
{
public:
    explicit AppList(const QStringList &desktopDirs);

    // Four strings per app, sorted by name: the entry's file name, its name
    // (localised), its icon as an image URL (empty if none) and "android" or
    // "". Only entries the launcher shows. Read again only when one of the
    // directories or the locale changed.
    QStringList apps();

    // An icon name from a launcher entry as an image URL: a path as it is,
    // a hicolor app icon if one is installed, a theme icon otherwise
    static QString iconUrl(const QString &icon, const QStringList &hicolorDirs = defaultHicolorDirs());
    static QStringList defaultHicolorDirs();

private:
    QStringList read() const;
    QString signature() const;

    QStringList m_dirs;
    QString m_signature;
    QStringList m_apps;
};

#endif
