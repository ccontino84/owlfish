// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "alscalibration.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

double alsValueMultiplier(const QString &mceConfigDir)
{
    double multiplier = 1.0;

    const QRegularExpression pattern(QStringLiteral("^[0-9][0-9].*\\.ini$"));
    QDir dir(mceConfigDir);
    for (const QString &name : dir.entryList(QDir::Files, QDir::Name)) {
        if (!pattern.match(name).hasMatch())
            continue;

        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString group;
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
                continue;
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                group = line.mid(1, line.length() - 2).trimmed();
                continue;
            }
            const int separator = line.indexOf(QLatin1Char('='));
            if (group != QLatin1String("Sensors") || separator < 0
                    || line.left(separator).trimmed() != QLatin1String("AlsValueMultiplier"))
                continue;

            bool ok = false;
            const double value = line.mid(separator + 1).trimmed().toDouble(&ok);
            if (ok && value > 0)
                multiplier = value;
            else
                qCWarning(lcOwlfish) << "Ignoring invalid AlsValueMultiplier in" << file.fileName();
        }
    }

    return multiplier;
}
