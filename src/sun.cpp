// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "sun.h"

#include <cmath>

// NOAA's solar calculator equations (after Meeus, "Astronomical Algorithms"),
// as documented at https://gml.noaa.gov/grad/solcalc/calcdetails.html
namespace {

const double Pi = 3.14159265358979323846;
// Sun's centre below the horizon at sunrise and sunset, in degrees
const double HorizonDepression = 0.833;
const int MinutesPerDay = 24 * 60;

double radians(double degrees)
{
    return degrees * Pi / 180;
}

double degrees(double radians)
{
    return radians * 180 / Pi;
}

struct SolarPosition {
    // Degrees
    double declination;
    // Minutes; apparent minus mean solar time
    double equationOfTime;
};

SolarPosition solarPosition(double julianDay)
{
    // Julian centuries since J2000.0
    const double t = (julianDay - 2451545) / 36525;

    const double meanLongitude = std::fmod(280.46646 + t * (36000.76983 + t * 0.0003032), 360);
    const double meanAnomaly = 357.52911 + t * (35999.05029 - 0.0001537 * t);
    const double eccentricity = 0.016708634 - t * (0.000042037 + 0.0000001267 * t);
    const double m = radians(meanAnomaly);
    const double centre = std::sin(m) * (1.914602 - t * (0.004817 + 0.000014 * t))
            + std::sin(2 * m) * (0.019993 - 0.000101 * t)
            + std::sin(3 * m) * 0.000289;
    const double omega = radians(125.04 - 1934.136 * t);
    const double apparentLongitude = meanLongitude + centre - 0.00569 - 0.00478 * std::sin(omega);
    const double meanObliquity = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
    const double obliquity = radians(meanObliquity + 0.00256 * std::cos(omega));

    SolarPosition position;
    position.declination = degrees(std::asin(std::sin(obliquity) * std::sin(radians(apparentLongitude))));

    const double y = std::pow(std::tan(obliquity / 2), 2);
    const double l = radians(meanLongitude);
    position.equationOfTime = 4 * degrees(y * std::sin(2 * l)
                                          - 2 * eccentricity * std::sin(m)
                                          + 4 * eccentricity * y * std::sin(m) * std::cos(2 * l)
                                          - 0.5 * y * y * std::sin(4 * l)
                                          - 1.25 * eccentricity * eccentricity * std::sin(2 * m));
    return position;
}

// Cosine of the hour angle at sunrise and sunset; outside -1..1 the sun
// does not reach the horizon
double cosHourAngle(double latitude, double declination)
{
    const double phi = radians(latitude);
    const double delta = radians(declination);
    return (std::cos(radians(90 + HorizonDepression)) - std::sin(phi) * std::sin(delta))
            / (std::cos(phi) * std::cos(delta));
}

}

SunTimes SunTimes::compute(const QDate &date, double latitude, double longitude)
{
    // Minutes are counted from 00:00 UTC on date
    const double midnightJulianDay = date.toJulianDay() - 0.5;
    const QDateTime midnight(date, QTime(0, 0), Qt::UTC);
    auto julianDay = [midnightJulianDay](double minutes) { return midnightJulianDay + minutes / MinutesPerDay; };

    SunTimes times;

    const double meanNoon = 720 - 4 * longitude;
    const SolarPosition noon = solarPosition(julianDay(meanNoon));
    const double noonCos = cosHourAngle(latitude, noon.declination);
    if (noonCos > 1) {
        times.m_state = PolarNight;
        return times;
    }
    if (noonCos < -1) {
        times.m_state = PolarDay;
        return times;
    }

    // Each event at the solar position of its own time: start from solar
    // noon's and refine
    auto event = [&](int sign) {
        double minutes = meanNoon - noon.equationOfTime + sign * 4 * degrees(std::acos(noonCos));
        for (int i = 0; i < 3; ++i) {
            const SolarPosition position = solarPosition(julianDay(minutes));
            const double c = cosHourAngle(latitude, position.declination);
            // On the last days before midnight sun or polar night the sun
            // only just reaches the horizon; keep the estimate from noon
            if (c < -1 || c > 1)
                break;
            minutes = meanNoon - position.equationOfTime + sign * 4 * degrees(std::acos(c));
        }
        return midnight.addSecs(qint64(std::floor(minutes * 60 + 0.5)));
    };
    times.m_sunrise = event(-1);
    times.m_sunset = event(1);
    return times;
}

int SunTimes::minuteOfDay(const QDateTime &time, const QTimeZone &zone)
{
    const QDateTime local = zone.isValid() ? time.toTimeZone(zone) : time.toLocalTime();
    // Rounded to the nearest minute
    return (local.time().msecsSinceStartOfDay() / 1000 + 30) / 60 % MinutesPerDay;
}
