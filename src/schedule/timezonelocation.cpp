// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "timezonelocation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

// /etc/localtime -> /var/lib/timed/localtime -> /usr/share/zoneinfo/<zone>
// on Sailfish OS; more would be a loop
const int MaxLinks = 8;
const char *const ZoneInfoDir = "/zoneinfo/";

}

TimeZoneLocation::TimeZoneLocation(const QString &zone, const QString &root)
    : m_zone(zone)
    , m_valid(false)
    , m_latitude(0)
    , m_longitude(0)
{
    if (zone.isEmpty())
        return;
    // zone.tab has one line per country, e.g. Europe/Oslo; zone1970.tab
    // merges countries with the same clocks since 1970, and has no Oslo
    const QDir zoneInfo(QDir(root).filePath(QStringLiteral("usr/share/zoneinfo")));
    if (!lookup(zoneInfo.filePath(QStringLiteral("zone.tab"))))
        lookup(zoneInfo.filePath(QStringLiteral("zone1970.tab")));
}

QString TimeZoneLocation::place() const
{
    QString name = m_zone.mid(m_zone.lastIndexOf(QLatin1Char('/')) + 1);
    return name.replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString TimeZoneLocation::systemZone(const QString &root)
{
    QString path = QDir(root).filePath(QStringLiteral("etc/localtime"));
    for (int i = 0; i < MaxLinks; ++i) {
        // One level only, unlike QFileInfo::canonicalFilePath()
        const QString target = QFileInfo(path).symLinkTarget();
        if (target.isEmpty())
            break;
        const int index = target.lastIndexOf(QLatin1String(ZoneInfoDir));
        if (index >= 0) {
            QString zone = target.mid(index + int(qstrlen(ZoneInfoDir)));
            // Variants of the same zones in some distributions
            for (const char *variant : { "posix/", "right/" }) {
                if (zone.startsWith(QLatin1String(variant)))
                    zone = zone.mid(int(qstrlen(variant)));
            }
            return zone;
        }
        path = target;
    }
    return QString();
}

bool TimeZoneLocation::parseCoordinates(const QString &text, double *latitude, double *longitude)
{
    static const QRegularExpression pattern(QStringLiteral(
            "^([+-])(\\d{2})(\\d{2})(\\d{2})?([+-])(\\d{3})(\\d{2})(\\d{2})?$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return false;

    auto angle = [&match](int first) {
        const double value = match.captured(first + 1).toInt()
                + match.captured(first + 2).toInt() / 60.0
                + match.captured(first + 3).toInt() / 3600.0;
        return match.captured(first) == QLatin1String("-") ? -value : value;
    };
    const double lat = angle(1);
    const double lon = angle(5);
    if (qAbs(lat) > 90 || qAbs(lon) > 180)
        return false;
    *latitude = lat;
    *longitude = lon;
    return true;
}

bool TimeZoneLocation::lookup(const QString &tabPath)
{
    QFile file(tabPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    // Tab separated: country code(s), coordinates, zone, optional comment
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() >= 3 && columns.at(2) == m_zone)
            return m_valid = parseCoordinates(columns.at(1), &m_latitude, &m_longitude);
    }
    return false;
}
