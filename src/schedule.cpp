// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "schedule.h"

namespace {

const int MinutesPerDay = 24 * 60;

}

OwlfishSchedule::OwlfishSchedule(int fromMinutes, int toMinutes, int transitionMinutes)
{
    auto wrap = [](int minutes) { return ((minutes % MinutesPerDay) + MinutesPerDay) % MinutesPerDay; };
    m_from = wrap(fromMinutes);
    m_length = wrap(toMinutes - fromMinutes);
    // Fading in and out both have to fit into the window
    m_transition = qBound(0, transitionMinutes, m_length / 2);
}

qint64 OwlfishSchedule::secondsIntoWindow(const QDateTime &now) const
{
    const qint64 daySeconds = MinutesPerDay * 60;
    const qint64 seconds = now.time().msecsSinceStartOfDay() / 1000;
    return ((seconds - m_from * 60) % daySeconds + daySeconds) % daySeconds;
}

bool OwlfishSchedule::contains(const QDateTime &now) const
{
    return !isEmpty() && secondsIntoWindow(now) < m_length * 60;
}

qreal OwlfishSchedule::strength(const QDateTime &now) const
{
    if (!contains(now))
        return 0;
    if (m_transition == 0)
        return 1;
    const qreal into = secondsIntoWindow(now);
    const qreal left = m_length * 60 - into;
    return qMin<qreal>(1, qMin(into, left) / (m_transition * 60));
}

OwlfishSchedule::Edge OwlfishSchedule::lastEdge(const QDateTime &now) const
{
    Edge last { QDateTime(), false };
    if (isEmpty())
        return last;

    const int to = (m_from + m_length) % MinutesPerDay;
    for (int days = -1; days <= 0; ++days) {
        const QDate date = now.date().addDays(days);
        const Edge candidates[] = {
            { QDateTime(date, QTime(m_from / 60, m_from % 60)), true },
            { QDateTime(date, QTime(to / 60, to % 60)), false },
        };
        for (const Edge &edge : candidates) {
            if (edge.time <= now && (!last.time.isValid() || edge.time > last.time))
                last = edge;
        }
    }
    return last;
}
