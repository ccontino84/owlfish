// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "deviceinfo.h"

#include <QFile>

namespace DeviceInfo {

const char *const HwReleasePath = "/etc/hw-release";

QString id(const QString &hwReleasePath)
{
    QFile file(hwReleasePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    for (const QByteArray &line : file.readAll().split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith("ID="))
            continue;
        QByteArray value = trimmed.mid(3);
        if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\''))
                && value.endsWith(value.at(0)))
            value = value.mid(1, value.size() - 2);
        return QString::fromUtf8(value);
    }
    return QString();
}

}
