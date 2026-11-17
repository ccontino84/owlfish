// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "applist.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QMap>
#include <QSet>

#include <algorithm>

#ifdef HAVE_MLITE
#include <mdesktopentry.h>
#endif

namespace {

struct App {
    QString id;
    QString name;
    QString icon;
    bool android;
};

// What the launcher shows of one entry; false if it shows nothing
bool readEntry(const QString &path, App *app)
{
#ifdef HAVE_MLITE
    // As lipstick's LauncherItem: localised names, also through Jolla's
    // translation catalogues
    MDesktopEntry entry(path);
    if (!entry.isValid() || entry.type() != QLatin1String("Application") || entry.hidden()
            || entry.noDisplay() || entry.notShowIn().contains(QStringLiteral("X-MeeGo")))
        return false;
    app->name = entry.name();
    app->icon = entry.icon();
    app->android = !entry.value(QStringLiteral("Desktop Entry"), QStringLiteral("X-apkd-packageName")).isEmpty();
    return !app->name.isEmpty();
#else
    // Host builds: the plain keys
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QMap<QString, QString> values;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (inEntry && equals > 0)
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    const auto isTrue = [&values](const char *key) {
        return values.value(QLatin1String(key)) == QLatin1String("true");
    };
    if (values.value(QStringLiteral("Type")) != QLatin1String("Application") || isTrue("NoDisplay")
            || isTrue("Hidden") || values.value(QStringLiteral("NotShowIn")).contains(QLatin1String("X-MeeGo")))
        return false;
    app->name = values.value(QStringLiteral("Name"));
    app->icon = values.value(QStringLiteral("Icon"));
    app->android = !values.value(QStringLiteral("X-apkd-packageName")).isEmpty();
    return !app->name.isEmpty();
#endif
}

}

AppList::AppList(const QStringList &desktopDirs)
    : m_dirs(desktopDirs)
{
}

QStringList AppList::defaultHicolorDirs()
{
    // The sizes Sailfish OS apps install, largest first
    return QStringList()
            << QStringLiteral("/usr/share/icons/hicolor/172x172/apps")
            << QStringLiteral("/usr/share/icons/hicolor/128x128/apps")
            << QStringLiteral("/usr/share/icons/hicolor/108x108/apps")
            << QStringLiteral("/usr/share/icons/hicolor/86x86/apps");
}

QString AppList::iconUrl(const QString &icon, const QStringList &hicolorDirs)
{
    if (icon.isEmpty())
        return QString();
    if (icon.startsWith(QLatin1Char('/')))
        return QStringLiteral("file://") + icon;
    if (icon.contains(QLatin1Char(':')))
        return icon;
    for (const QString &dir : hicolorDirs) {
        const QString path = dir + QLatin1Char('/') + icon + QStringLiteral(".png");
        if (QFileInfo(path).isFile())
            return QStringLiteral("file://") + path;
    }
    return QStringLiteral("image://theme/") + icon;
}

QString AppList::signature() const
{
    QStringList parts;
    parts << QLocale::system().name();
    for (const QString &dir : m_dirs) {
        const QFileInfo info(dir);
        parts << dir + QLatin1Char('@') + QString::number(info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0);
    }
    return parts.join(QLatin1Char('\n'));
}

QStringList AppList::apps()
{
    const QString current = signature();
    if (current != m_signature) {
        m_apps = read();
        m_signature = current;
    }
    return m_apps;
}

QStringList AppList::read() const
{
    QList<App> apps;
    QSet<QString> seen;
    // The first directory's entry hides a later one's of the same name
    for (const QString &dir : m_dirs) {
        const QStringList files = QDir(dir).entryList(QStringList() << QStringLiteral("*.desktop"),
                                                      QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &id : files) {
            if (seen.contains(id))
                continue;
            seen.insert(id);
            App app;
            app.id = id;
            if (readEntry(dir + QLatin1Char('/') + id, &app))
                apps << app;
        }
    }
    std::sort(apps.begin(), apps.end(), [](const App &a, const App &b) {
        const int order = QString::localeAwareCompare(a.name, b.name);
        return order != 0 ? order < 0 : a.id < b.id;
    });

    QStringList list;
    for (const App &app : apps)
        list << app.id << app.name << iconUrl(app.icon) << (app.android ? QStringLiteral("android") : QString());
    return list;
}
