// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "crashguard.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

CrashGuard::CrashGuard(const QString &filePath, int maxUnhealthyStarts)
    : m_filePath(filePath)
    , m_maxUnhealthyStarts(maxUnhealthyStarts)
{
}

QString CrashGuard::defaultFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
            + QStringLiteral("/owlfish/unhealthy-starts");
}

int CrashGuard::unhealthyStarts() const
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    return qMax(0, file.readAll().trimmed().toInt());
}

bool CrashGuard::begin()
{
    const int count = unhealthyStarts();
    if (count >= m_maxUnhealthyStarts)
        return false;

    write(count + 1);
    return true;
}

void CrashGuard::markHealthy()
{
    if (QFile::exists(m_filePath) && !QFile::remove(m_filePath))
        qCWarning(lcOwlfish) << "Cannot remove" << m_filePath;
}

void CrashGuard::write(int count)
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcOwlfish) << "Cannot write" << m_filePath;
        return;
    }
    file.write(QByteArray::number(count) + '\n');
    // Must hit the disk before a possible crash later in this start
    if (!file.commit())
        qCWarning(lcOwlfish) << "Cannot commit" << m_filePath;
}
