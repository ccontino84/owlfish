// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "ambientcutoff.h"

namespace {

const int DefaultThresholdLux = 1000;
// Getting readable fast matters more than getting dim fast
const int DefaultToBrightMs = 1500;
const int DefaultToDarkMs = 5000;
// Readings this close to the sensor's maximum count as at it
const qreal SaturatedRatio = 0.999;

}

AmbientCutoff::AmbientCutoff(QObject *parent)
    : QObject(parent)
    , m_threshold(DefaultThresholdLux)
    , m_toBrightMs(DefaultToBrightMs)
    , m_toDarkMs(DefaultToDarkMs)
    , m_maximum(0)
    , m_bright(false)
    , m_priming(Unprimed)
    , m_lastLux(0)
    , m_pendingBright(false)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() { setBright(m_pendingBright); });
}

void AmbientCutoff::setThreshold(int lux)
{
    lux = qMax(1, lux);
    if (m_threshold == lux)
        return;
    m_threshold = lux;

    // The user moved the threshold: compare the current light with it right
    // away, without waiting for the light to change and without hysteresis
    reevaluate();
}

void AmbientCutoff::reevaluate()
{
    if (m_priming != Unprimed) {
        m_timer.stop();
        setBright(isSaturated(m_lastLux) || m_lastLux > m_threshold);
    }
}

void AmbientCutoff::setDelays(int toBrightMs, int toDarkMs)
{
    m_toBrightMs = toBrightMs;
    m_toDarkMs = toDarkMs;
}

void AmbientCutoff::setMaximum(qreal lux)
{
    lux = qMax(qreal(0), lux);
    if (m_maximum == lux)
        return;
    m_maximum = lux;
    reevaluate();
}

bool AmbientCutoff::isSaturated(qreal lux) const
{
    return m_maximum > 0 && lux >= m_maximum * SaturatedRatio;
}

bool AmbientCutoff::isBrightReading(qreal lux) const
{
    if (isSaturated(lux))
        return true;
    return m_bright ? lux >= m_threshold * ResumeRatio : lux > m_threshold;
}

void AmbientCutoff::addReading(qreal lux)
{
    m_lastLux = lux;
    const bool bright = isBrightReading(lux);

    if (m_priming != Primed) {
        m_priming = Primed;
        m_timer.stop();
        setBright(bright);
        return;
    }

    if (bright == m_bright) {
        m_timer.stop();
        return;
    }

    if (!m_timer.isActive() || m_pendingBright != bright) {
        m_pendingBright = bright;
        m_timer.start(bright ? m_toBrightMs : m_toDarkMs);
    }
}

void AmbientCutoff::addInitialReading(qreal lux)
{
    if (m_priming != Unprimed)
        return;
    m_priming = Provisional;
    m_lastLux = lux;
    setBright(isBrightReading(lux));
}

void AmbientCutoff::reset()
{
    m_timer.stop();
    m_priming = Unprimed;
    setBright(false);
}

void AmbientCutoff::setBright(bool bright)
{
    if (m_bright == bright)
        return;
    m_bright = bright;
    emit brightChanged(bright);
}
