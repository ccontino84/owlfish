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

    bool isBright() const { return m_bright; }

    void addReading(qreal lux);
    // Forget the state, e.g. when the sensor is stopped
    void reset();

signals:
    void brightChanged(bool bright);

private:
    void setBright(bool bright);

    int m_threshold;
    int m_toBrightMs;
    int m_toDarkMs;
    bool m_bright;
    bool m_primed;
    qreal m_lastLux;
    bool m_pendingBright;
    QTimer m_timer;
};

#endif
