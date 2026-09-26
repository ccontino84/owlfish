// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "ambientcutoff.h"

namespace {

const int DefaultThresholdLux = 1000;
// Getting readable fast matters more than getting dim fast
const int DefaultToBrightMs = 1500;
const int DefaultToDarkMs = 5000;

}

AmbientCutoff::AmbientCutoff(QObject *parent)
    : QObject(parent)
    , m_threshold(DefaultThresholdLux)
    , m_toBrightMs(DefaultToBrightMs)
    , m_toDarkMs(DefaultToDarkMs)
    , m_bright(false)
    , m_primed(false)
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
    if (m_primed) {
        m_timer.stop();
        setBright(m_lastLux > m_threshold);
    }
}

void AmbientCutoff::setDelays(int toBrightMs, int toDarkMs)
{
    m_toBrightMs = toBrightMs;
    m_toDarkMs = toDarkMs;
}

void AmbientCutoff::addReading(qreal lux)
{
    m_lastLux = lux;
    const bool bright = m_bright ? lux >= m_threshold * ResumeRatio : lux > m_threshold;

    if (!m_primed) {
        m_primed = true;
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

void AmbientCutoff::reset()
{
    m_timer.stop();
    m_primed = false;
    setBright(false);
}

void AmbientCutoff::setBright(bool bright)
{
    if (m_bright == bright)
        return;
    m_bright = bright;
    emit brightChanged(bright);
}
