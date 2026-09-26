// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef CRASHGUARD_H
#define CRASHGUARD_H

#include <QString>

// Protects the home screen against a plugin that crashes it on startup.
//
// Every start that has not yet been confirmed healthy is counted in a small
// file. After maxUnhealthyStarts such starts in a row the guard trips and the
// plugin stays inactive until the file is removed.
class CrashGuard
{
public:
    explicit CrashGuard(const QString &filePath, int maxUnhealthyStarts = 3);

    // Returns false if the guard has tripped; otherwise records this start
    bool begin();
    // This run has been healthy long enough; reset the count
    void markHealthy();

    int unhealthyStarts() const;
    QString filePath() const { return m_filePath; }

    static QString defaultFilePath();

private:
    void write(int count);

    QString m_filePath;
    int m_maxUnhealthyStarts;
};

#endif
