// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_SETTINGS_H
#define OWLFISH_SETTINGS_H

#include <QObject>
#include <QVariant>

#ifdef HAVE_MLITE
#include <QHash>

class MDConfItem;
#else
#include <QVariantMap>
#endif

// User settings under dconf /apps/owlfish/:
//   enabled              bool, default false: master switch
//   temperature          kelvin, 1900 (no blue left) to 6500 (no tint),
//                        default 4500
//   dim                  extra dimming in percent, 0 to MaximumDim, default 0
//   dim_cutoff           bool, default true: no extra dimming in bright light
//   dim_cutoff_lux       ambient light level that counts as bright, 100 to
//                        50000, default 1000
//   schedule             bool, default false: warm colour only at night,
//                        neutral otherwise
//   schedule_sun         bool, default false: the night is from sunset to
//                        sunrise instead of schedule_from to schedule_to
//   schedule_from        start, minutes after midnight, default 21:00
//   schedule_to          end, default 07:00
//   schedule_transition  minutes to fade in after the start and out before
//                        the end, default 60
//   location_manual      bool, default false: sunset and sunrise at latitude
//                        and longitude instead of the time zone's city
//   latitude, longitude  degrees, north and east positive
// Written by the plugin, for the settings page:
//   als_multiplier       the ALS calibration factor, so the settings page can
//                        show readings in the same units
//   sun_place            the time zone's city, empty if the time zone has
//                        no location
//   auto_latitude, auto_longitude
//                        the city's location; unset if there is none
//   sun_state            "normal", "polar_day", "polar_night" or
//                        "no_location", for today; always set
//   sun_set, sun_rise    minutes after midnight; unset unless normal
//   sun_latitude         the latitude sun_state is for
class OwlfishSettings : public QObject
{
    Q_OBJECT

public:
    // Never dim completely: the user must still be able to see the screen to
    // turn the filter off again
    static const int MaximumDim = 75;
    static const int DefaultTemperature = 4500;
    static const int DefaultCutoffLux = 1000;
    // Nothing darker counts as bright; brighter is direct sun
    static const int MinimumCutoffLux = 100;
    static const int MaximumCutoffLux = 50000;
    static const int DefaultFrom = 21 * 60;
    static const int DefaultTo = 7 * 60;
    static const int DefaultTransition = 60;
    static const int MaximumTransition = 120;

    explicit OwlfishSettings(QObject *parent = nullptr);

    bool enabled() const;
    int temperature() const;
    int dim() const;
    bool cutoffEnabled() const;
    int cutoffLux() const;
    bool scheduled() const;
    bool scheduleSun() const;
    int scheduleFrom() const;
    int scheduleTo() const;
    int scheduleTransition() const;
    bool locationManual() const;
    // NaN if not set or out of range
    double latitude() const;
    double longitude() const;

    void publishAlsMultiplier(double multiplier);
    // Writes one of the keys the plugin writes, if it changed. An invalid
    // value unsets it.
    void publish(const QString &key, const QVariant &value);
#ifndef HAVE_MLITE
    // What was published, for tests
    QVariant published(const QString &key) const { return m_published.value(key); }
#endif

signals:
    void changed();

private:
#ifdef HAVE_MLITE
    MDConfItem *m_enabled;
    MDConfItem *m_temperature;
    MDConfItem *m_dim;
    MDConfItem *m_cutoffEnabled;
    MDConfItem *m_cutoffLux;
    MDConfItem *m_scheduled;
    MDConfItem *m_scheduleSun;
    MDConfItem *m_scheduleFrom;
    MDConfItem *m_scheduleTo;
    MDConfItem *m_scheduleTransition;
    MDConfItem *m_locationManual;
    MDConfItem *m_latitude;
    MDConfItem *m_longitude;
    QHash<QString, MDConfItem *> m_publishedItems;
#else
    bool m_enabled;
    int m_temperature;
    int m_dim;
    bool m_cutoffEnabled;
    int m_cutoffLux;
    bool m_scheduled;
    bool m_scheduleSun;
    int m_scheduleFrom;
    int m_scheduleTo;
    int m_scheduleTransition;
    bool m_locationManual;
    double m_latitude;
    double m_longitude;
    QVariantMap m_published;
#endif
};

#endif
