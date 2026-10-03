// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colormatrix.h"

#include <cmath>

namespace {

// Rec. 709 / sRGB luminance of linear red, green and blue
const float Luminance[3] = { 0.2126f, 0.7152f, 0.0722f };

}

namespace ColorMatrix {

QMatrix3x3 identity()
{
    return QMatrix3x3();
}

QMatrix3x3 saturation(qreal factor)
{
    // identity · s + luminance · (1 − s), one row per output channel
    // double: qreal is float on 32-bit ARM
    const float s = float(qMax(0.0, double(factor)));
    QMatrix3x3 matrix;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix(row, column) = (row == column ? s : 0) + (1 - s) * Luminance[column];
    }
    return matrix;
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
