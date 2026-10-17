// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "mediatekoutput.h"
#include "colormatrix.h"
#include "logging.h"

MediaTekOutput::MediaTekOutput(PqDisplay::Backend *backend, const QString &hwReleasePath,
                               const QString &deviceTreePath, QObject *parent)
    : FilterOutput(parent)
    , m_pq(new PqDisplay(backend))
    , m_hwReleasePath(hwReleasePath)
    , m_deviceTreePath(deviceTreePath)
    , m_ccorrRead(false)
{
}

MediaTekOutput::~MediaTekOutput()
{
    deactivate();
}

bool MediaTekOutput::applies()
{
    if (!m_ccorrRead) {
        m_ccorr = PqDisplay::ccorr(m_deviceTreePath);
        m_ccorrRead = true;
    }
    return isVerified() || PqDisplay::isKnown(m_ccorr);
}

bool MediaTekOutput::isVerified() const
{
    return PqDisplay::isVerified(PqDisplay::deviceId(m_hwReleasePath));
}

void MediaTekOutput::check()
{
    if (state() != NotChecked)
        return;
    if (!m_pq->open()) {
        setState(Unavailable, QStringLiteral("display hardware not available: %1").arg(m_pq->error()));
        return;
    }
    setState(Ready, isVerified()
             ? QStringLiteral("device %1 verified").arg(PqDisplay::deviceId(m_hwReleasePath))
             : QStringLiteral("display hardware detected"));
}

bool MediaTekOutput::activate()
{
    if (isActive())
        return true;
    // Also clears whatever an earlier run left behind
    if (!m_pq->reset()) {
        setState(Unavailable, QStringLiteral("display hardware failed: %1").arg(m_pq->error()));
        return false;
    }
    setActive(true);
    return true;
}

void MediaTekOutput::deactivate()
{
    if (!isActive())
        return;
    setActive(false);
    m_pq->reset();
}

void MediaTekOutput::apply(const QVector3D &gain, const QMatrix3x3 &matrix)
{
    if (!isActive())
        return;
    if (!m_pq->setMatrix(ColorMatrix::withGain(matrix, gain)))
        failedCall();
}

void MediaTekOutput::failedCall()
{
    setState(Unavailable, QStringLiteral("display hardware failed: %1").arg(m_pq->error()));
    emit failed();
}

QStringList MediaTekOutput::diagnostics() const
{
    QStringList lines;
    if (m_ccorrRead)
        lines << QStringLiteral("display colour correction %1").arg(PqDisplay::describe(m_ccorr));
    if (m_pq->isOpen()) {
        lines << QStringLiteral("display hardware version %1, %2 calls, mean %3 ms, max %4 ms")
                 .arg(m_pq->interfaceVersion()).arg(m_pq->calls())
                 .arg(m_pq->meanCallMs(), 0, 'f', 2).arg(m_pq->maxCallMs(), 0, 'f', 2);
        if (!m_pq->error().isEmpty())
            lines << QStringLiteral("display hardware error: %1").arg(m_pq->error());
    } else {
        lines << QStringLiteral("display hardware %1")
                 .arg(m_pq->error().isEmpty() ? QStringLiteral("not checked") : m_pq->error());
    }
    return lines;
}
