// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_SETTINGS_H
#define OWLFISH_SETTINGS_H

#include <QObject>

#ifdef HAVE_MLITE
class MDConfItem;
#endif

// User settings under dconf /apps/owlfish/:
//   enabled              bool, default false: master switch
//   temperature          kelvin, 1900 (no blue left) to 6500 (no tint),
//                        default 4500
//   dim                  extra dimming in percent, 0 to MaximumDim, default 0
//   dim_cutoff           bool, default true: no extra dimming in bright light
//   dim_cutoff_lux       ambient light level that counts as bright, 100 to
//                        50000, default 1000
//   schedule             bool, default false: warm colour only between
//                        schedule_from and schedule_to, neutral otherwise
//   schedule_from        start, minutes after midnight, default 21:00
//   schedule_to          end, default 07:00
//   schedule_transition  minutes to fade in after the start and out before
//                        the end, default 60
// Written by the plugin:
//   als_multiplier       the ALS calibration factor, so the settings page can
//                        show readings in the same units
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
    int scheduleFrom() const;
    int scheduleTo() const;
    int scheduleTransition() const;

    void publishAlsMultiplier(double multiplier);

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
    MDConfItem *m_scheduleFrom;
    MDConfItem *m_scheduleTo;
    MDConfItem *m_scheduleTransition;
    MDConfItem *m_alsMultiplier;
#else
    bool m_enabled;
    int m_temperature;
    int m_dim;
    bool m_cutoffEnabled;
    int m_cutoffLux;
    bool m_scheduled;
    int m_scheduleFrom;
    int m_scheduleTo;
    int m_scheduleTransition;
#endif
};

#endif
