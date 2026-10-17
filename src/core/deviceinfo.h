// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_DEVICEINFO_H
#define OWLFISH_DEVICEINFO_H

#include <QString>

namespace DeviceInfo {

extern const char *const HwReleasePath;

// ID= in /etc/hw-release, e.g. "jp2601"; empty if there is none
QString id(const QString &hwReleasePath = QLatin1String(HwReleasePath));

}

#endif
