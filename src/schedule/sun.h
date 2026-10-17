// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_SUN_H
#define OWLFISH_SUN_H

#include <QDateTime>
#include <QTimeZone>

// Sunset and sunrise from NOAA's solar equations (declination and equation of
// time), with the sun's centre 0.833° below the horizon: the standard
// definition, which includes refraction and the sun's radius. NOAA gives
// the accuracy as about 1 min between ±72° latitude and 10 min beyond.
class SunTimes
{
public:
    enum State {
        Normal,
        // The sun does not set: midnight sun
        PolarDay,
        // The sun does not rise: polar night
        PolarNight
    };

    // The day around local solar noon of date, at latitude (north positive)
    // and longitude (east positive), in degrees
    static SunTimes compute(const QDate &date, double latitude, double longitude);

    State state() const { return m_state; }
    // In UTC; invalid unless Normal. The sunrise is this morning's, the
    // sunset this evening's; either may fall on another calendar date.
    QDateTime sunrise() const { return m_sunrise; }
    QDateTime sunset() const { return m_sunset; }

    // Minutes after local midnight in zone; an invalid zone means the
    // process's local time
    static int minuteOfDay(const QDateTime &time, const QTimeZone &zone = QTimeZone());

private:
    SunTimes() : m_state(Normal) {}

    State m_state;
    QDateTime m_sunrise;
    QDateTime m_sunset;
};

#endif
