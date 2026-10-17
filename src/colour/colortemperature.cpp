// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "colortemperature.h"

#include <cmath>

namespace {

// Chromaticity of a black body, Kim et al. 2002 cubic spline fit of the
// Planckian locus (valid 1667-25000 K)
void planckianLocus(double kelvin, double *x, double *y)
{
    const double t = 1000.0 / kelvin;
    if (kelvin <= 4000)
        *x = ((-0.2661239 * t - 0.2343589) * t + 0.8776956) * t + 0.179910;
    else
        *x = ((-3.0258469 * t + 2.1070379) * t + 0.2226347) * t + 0.240390;

    const double u = *x;
    if (kelvin <= 2222)
        *y = ((-1.1063814 * u - 1.34811020) * u + 2.18555832) * u - 0.20219683;
    else if (kelvin <= 4000)
        *y = ((-0.9549476 * u - 1.37418593) * u + 2.09137015) * u - 0.16748867;
    else
        *y = ((3.0817580 * u - 5.87338670) * u + 3.75112997) * u - 0.37001483;
}

// CIE daylight locus (valid 4000-25000 K); D65 lies on it
void daylightLocus(double kelvin, double *x, double *y)
{
    const double t = 1000.0 / kelvin;
    *x = ((-4.6070 * t + 2.9678) * t + 0.09911) * t + 0.244063;
    *y = (-3.000 * *x + 2.870) * *x - 0.275;
}

double srgbEncode(double linear)
{
    if (linear <= 0.0031308)
        return 12.92 * linear;
    return 1.055 * std::pow(linear, 1 / 2.4) - 0.055;
}

}

namespace ColorTemperature {

QVector3D gain(qreal requested)
{
    // double throughout: qreal is float on 32-bit ARM
    double kelvin = requested;
    if (!std::isfinite(kelvin) || kelvin >= Neutral - 0.5)
        return QVector3D(1, 1, 1);
    kelvin = qMax<double>(kelvin, Minimum);

    // Black body below 5000 K, daylight near 6500 K so that the neutral end
    // is D65, blended in between. Same construction as the redshift table.
    double x, y;
    planckianLocus(kelvin, &x, &y);
    if (kelvin > 5000) {
        double dx, dy;
        daylightLocus(kelvin, &dx, &dy);
        const double w = (kelvin - 5000) / 1500;
        x += (dx - x) * w;
        y += (dy - y) * w;
    }

    // xyY with Y = 1 to linear sRGB (D65 white is 1, 1, 1)
    const double X = x / y;
    const double Z = (1 - x - y) / y;
    double rgb[3] = {
         3.2404542 * X - 1.5371385 - 0.4985314 * Z,
        -0.9692660 * X + 1.8760108 + 0.0415560 * Z,
         0.0556434 * X - 0.2040259 + 1.0572252 * Z,
    };

    // Keep the brightest channel (red) at full, and encode: the filter
    // multiplies gamma-encoded values, and encode(a) * encode(b) is close to
    // encode(a * b)
    const double peak = qMax(rgb[0], qMax(rgb[1], rgb[2]));
    for (double &c : rgb)
        c = srgbEncode(qBound<double>(0, c / peak, 1));
    return QVector3D(rgb[0], rgb[1], rgb[2]);
}

qreal partial(qreal targetKelvin, qreal strength)
{
    if (strength <= 0)
        return Neutral;
    targetKelvin = qBound<qreal>(Minimum, targetKelvin, Neutral);
    if (strength >= 1)
        return targetKelvin;
    const qreal neutral = 1e6 / Neutral;
    const qreal target = 1e6 / targetKelvin;
    return 1e6 / (neutral + (target - neutral) * strength);
}

}
