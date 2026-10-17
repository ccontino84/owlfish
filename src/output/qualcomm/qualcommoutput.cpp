// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "qualcommoutput.h"

QualcommOutput::QualcommOutput(QualcommClient::Backend *backend, QObject *parent)
    : FilterOutput(parent)
    , m_display(new QualcommClient(backend))
{
    connect(m_display.data(), &QualcommClient::checked, this, &QualcommOutput::displayChecked);
    connect(m_display.data(), &QualcommClient::applied, this, [this]() {
        if (isActive())
            emit applied();
    });
    connect(m_display.data(), &QualcommClient::failed, this, &QualcommOutput::displayFailed);
}

QualcommOutput::~QualcommOutput()
{
    deactivate();
}

bool QualcommOutput::applies()
{
    return m_display->isInstalled();
}

void QualcommOutput::check()
{
    if (state() != NotChecked)
        return;
    setState(Checking, QStringLiteral("checking the Qualcomm helper"));
    m_display->check();
}

void QualcommOutput::displayChecked()
{
    if (m_display->state() == QualcommClient::Ready)
        setState(Ready, QStringLiteral("Qualcomm helper ready"));
    else
        setState(Unavailable, QStringLiteral("Qualcomm helper not usable: %1").arg(m_display->info()));
    emit checked();
}

void QualcommOutput::displayFailed()
{
    setState(Unavailable, QStringLiteral("display hardware failed: %1").arg(m_display->info()));
    if (isActive())
        emit failed();
}

bool QualcommOutput::activate()
{
    if (state() != Ready)
        return false;
    // The helper's check left the hardware neutral
    setActive(true);
    return true;
}

void QualcommOutput::deactivate()
{
    if (!isActive())
        return;
    setActive(false);
    // The helper also does it when lipstick goes away
    m_display->reset();
}

void QualcommOutput::apply(const QVector3D &gain, const QMatrix3x3 &matrix)
{
    if (isActive())
        m_display->setMatrix(transform(matrix, gain));
}

QStringList QualcommOutput::diagnostics() const
{
    if (!m_display->isInstalled())
        return QStringList();
    QString state;
    switch (m_display->state()) {
    case QualcommClient::NotChecked: state = QStringLiteral("not checked"); break;
    case QualcommClient::Checking: state = QStringLiteral("checking"); break;
    case QualcommClient::Ready: state = QStringLiteral("ready, ") + m_display->info(); break;
    case QualcommClient::Unavailable: state = QStringLiteral("not usable, ") + m_display->info(); break;
    }
    return QStringList(QStringLiteral("qualcomm helper %1, %2 calls").arg(state).arg(m_display->calls()));
}

QMatrix3x3 QualcommOutput::transform(const QMatrix3x3 &matrix, const QVector3D &gain)
{
    QMatrix3x3 result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            result(row, column) = gain[row] * matrix(row, column);
    }
    return result;
}
