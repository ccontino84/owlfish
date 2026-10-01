// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef ALSCALIBRATION_H
#define ALSCALIBRATION_H

#include <QString>

class QDBusMessage;

// Some devices' sensor stacks report raw light sensor values instead of lux.
// mce corrects them with [Sensors] AlsValueMultiplier from its ini files; use
// the same factor so readings and thresholds mean what mce means by lux.
//
// Reads <dir>/[0-9][0-9]*.ini in name order, later files overriding earlier
// ones, as mce does. Returns 1.0 if the key is not set or not valid.
double alsValueMultiplier(const QString &mceConfigDir = QStringLiteral("/etc/mce"));

// sensorfw's light sensor sends nothing to a new session until the value
// changes. It tells the value it already has (raw, like the readings) in
// reply to local.ALSSensor.lux on com.nokia.SensorService
// /SensorManager/alssensor: a (timestamp, value) structure. False if the
// reply is an error or has another shape.
bool alsLuxFromReply(const QDBusMessage &reply, quint32 *lux);
// The largest value the light sensor can report (raw, like the readings),
// from the reply to local.ALSSensor.getAvailableDataRanges: an array of
// (min, max, resolution). False if the reply is an error, has another shape
// or no range.
bool alsMaximumFromReply(const QDBusMessage &reply, double *maximum);

#endif
