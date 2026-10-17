// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "filteroutput.h"

FilterOutput::FilterOutput(QObject *parent)
    : QObject(parent)
    , m_state(NotChecked)
    , m_active(false)
{
}

void FilterOutput::setState(State state, const QString &reason)
{
    m_state = state;
    m_reason = reason;
}
