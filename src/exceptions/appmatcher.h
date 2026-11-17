// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_APPMATCHER_H
#define OWLFISH_APPMATCHER_H

#include <QSet>
#include <QString>
#include <QStringList>

// Tells whether the app behind a window is one of the user's exceptions.
// An exception is a launcher entry (its file name, "jolla-camera.desktop");
// both sides are turned into the names an app can be recognised by, which
// are then compared.
namespace AppMatcher {

// Where launcher entries are looked up, in this order: the user's own
// (Android apps live there) and the system's
QStringList defaultDesktopDirs();

// The path of the launcher entry with this file name in the first of dirs
// that has it; empty if none has
QString findDesktopFile(const QString &id, const QStringList &dirs);

// The names the app of a launcher entry is recognised by: for an Android app
// its package (X-apkd-packageName) only; otherwise the program its Exec runs
// and the entry's own name, both without path or extension. Empty if the
// file can't be read.
QStringList desktopKeys(const QString &path);

// The program an Exec line runs, without its path: wrappers (invoker,
// sailjail, …), their options, field codes and environment assignments are
// skipped. For an interpreter (sailfish-qml, python3, …) the script's name,
// without its extension. Empty if there is none.
QString execProgram(const QString &exec);

// The names the app behind a window is recognised by: the window's surface
// class name (an Android app's package; some apps' own name), and from
// procRoot/<pid>: the program (exe, unless it is a launcher booster), the
// first argument and comm, all without path. For an interpreter also its
// script's name.
QStringList windowKeys(qint64 pid, const QString &className,
                       const QString &procRoot = QStringLiteral("/proc"));

// Whether one of the window's names is one of the keys. comm is cut to 15
// characters by the kernel, so a 15-character name also matches a longer key
// that starts with it.
bool matches(const QStringList &windowKeys, const QSet<QString> &keys);

}

#endif
