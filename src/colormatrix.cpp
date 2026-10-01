// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colormatrix.h"

#include <cmath>

namespace ColorMatrix {

QMatrix3x3 identity()
{
    return QMatrix3x3();
}

qreal linearGain(qreal encodedGain)
{
    // The sRGB decoding curve: ColorTemperature encodes its linear gains
    // with it, and dimming by an encoded factor d looks like d^2.2 in
    // linear light (checked by eye on the Jolla Phone)
    const double c = qBound(0.0, double(encodedGain), 1.0);
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

QMatrix3x3 withGain(const QMatrix3x3 &matrix, const QVector3D &encodedGain)
{
    QMatrix3x3 result;
    for (int row = 0; row < 3; ++row) {
        const float gain = float(linearGain(encodedGain[row]));
        for (int column = 0; column < 3; ++column)
            result(row, column) = gain * matrix(row, column);
    }
    return result;
}

}
