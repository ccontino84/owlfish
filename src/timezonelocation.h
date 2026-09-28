// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_TIMEZONELOCATION_H
#define OWLFISH_TIMEZONELOCATION_H

#include <QString>

// The location of the system time zone's reference city, from tzdata's
// zone.tab and zone1970.tab. No API provides a time zone's coordinates.
// All paths are under root, so tests can use a directory of their own.
class TimeZoneLocation
{
public:
    // For zone, e.g. "Europe/Berlin"; not valid if tzdata has no coordinates
    // for it, e.g. for "UTC"
    explicit TimeZoneLocation(const QString &zone = QString(),
                              const QString &root = QStringLiteral("/"));

    bool isValid() const { return m_valid; }
    QString zone() const { return m_zone; }
    // The zone's city for display: "Berlin", "New York"
    QString place() const;
    // Degrees, north and east positive
    double latitude() const { return m_latitude; }
    double longitude() const { return m_longitude; }

    // The zone /etc/localtime points to. Follows the links one at a time and
    // stops at the first file in a zoneinfo directory, because tzdata may
    // install zones like Europe/Oslo as links to another zone's file. Empty
    // if /etc/localtime is not a link into zoneinfo.
    static QString systemZone(const QString &root = QStringLiteral("/"));
    // ISO 6709 as in zone.tab: ±DDMM±DDDMM or ±DDMMSS±DDDMMSS
    static bool parseCoordinates(const QString &text, double *latitude, double *longitude);

private:
    bool lookup(const QString &tabPath);

    QString m_zone;
    bool m_valid;
    double m_latitude;
    double m_longitude;
};

#endif
