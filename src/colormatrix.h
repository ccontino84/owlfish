// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_COLORMATRIX_H
#define OWLFISH_COLORMATRIX_H

#include <QGenericMatrix>
#include <QVector3D>

// 3×3 matrices that act on linear-light sRGB (column vectors: out = M · in)
namespace ColorMatrix {

QMatrix3x3 identity();
// Scales the saturation around the grey of the same luminance (Rec. 709):
// 1 is no change, 0 is grey. Every row sums to 1, so white stays white.
QMatrix3x3 saturation(qreal factor);

bool isIdentity(const QMatrix3x3 &matrix);

// A gain that multiplies sRGB-encoded values, as the same gain on linear
// light: what the display hardware needs for Owlfish's gains
qreal linearGain(qreal encodedGain);
// diag(linear gains) · matrix: the whole transform in linear light
QMatrix3x3 withGain(const QMatrix3x3 &matrix, const QVector3D &encodedGain);

}

#endif
