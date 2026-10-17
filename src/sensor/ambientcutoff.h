// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef AMBIENTCUTOFF_H
#define AMBIENTCUTOFF_H

#include <QObject>
#include <QTimer>

// Decides from ambient light readings whether it is too bright for extra dimming.
//
// Bright: above the threshold. Dark again: below ResumeRatio of it, so a
// reading hovering around the threshold does not toggle the filter. A change
// must also persist for a while (shadows, a hand over the sensor) before it
// counts, except for the first reading after reset(), which applies at once.
//
// The sensor may send nothing until the light changes, so the level it
// already has can be given as an initial reading. It applies at once, and so
// does the first real reading after it, as the initial one may be stale.
//
// A sensor at the top of its scale may see much more light than that, so a
// reading at its maximum counts as bright, whatever the threshold.
class AmbientCutoff : public QObject
{
    Q_OBJECT

public:
    // Keep in sync with the settings page text
    static constexpr double ResumeRatio = 0.75;

    explicit AmbientCutoff(QObject *parent = nullptr);

    // A changed threshold applies to the last reading at once
    void setThreshold(int lux);
    int threshold() const { return m_threshold; }
    void setDelays(int toBrightMs, int toDarkMs);
    // The most the sensor can report, in lux; 0 if not known
    void setMaximum(qreal lux);

    bool isBright() const { return m_bright; }

    void addReading(qreal lux);
    // Only until the first reading after reset(); ignored afterwards
    void addInitialReading(qreal lux);
    // Forget the state, e.g. when the sensor is stopped
    void reset();

signals:
    void brightChanged(bool bright);

private:
    void setBright(bool bright);
    bool isBrightReading(qreal lux) const;
    bool isSaturated(qreal lux) const;
    // The last reading against a changed threshold or maximum, at once and
    // without hysteresis
    void reevaluate();

    enum Priming {
        // No reading since reset()
        Unprimed,
        // Only an initial reading
        Provisional,
        Primed
    };

    int m_threshold;
    int m_toBrightMs;
    int m_toDarkMs;
    qreal m_maximum;
    bool m_bright;
    Priming m_priming;
    qreal m_lastLux;
    bool m_pendingBright;
    QTimer m_timer;
};

#endif
