// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_SCHEDULE_H
#define OWLFISH_SCHEDULE_H

#include <QDateTime>

// Fixed daily times, in minutes after local midnight. The window may cross
// midnight; from == to means no window. The filter fades in over the
// transition starting at "from" and fades out over it ending at "to".
class OwlfishSchedule
{
public:
    struct Edge {
        QDateTime time;
        bool on;
    };

    OwlfishSchedule(int fromMinutes, int toMinutes, int transitionMinutes);

    bool isEmpty() const { return m_length == 0; }
    bool contains(const QDateTime &now) const;
    // 0 to 1 inside the window, following the transitions; 0 outside
    qreal strength(const QDateTime &now) const;
    // The most recent start or end at or before now; invalid time if empty
    Edge lastEdge(const QDateTime &now) const;

private:
    qint64 secondsIntoWindow(const QDateTime &now) const;

    int m_from;
    int m_length;
    int m_transition;
};

#endif
