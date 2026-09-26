// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_COLORTEMPERATURE_H
#define OWLFISH_COLORTEMPERATURE_H

#include <QVector3D>

namespace ColorTemperature {

// No tint: the display's own D65 white
const int Neutral = 6500;
// Blue is fully gone here; lower temperatures only take away green
const int Minimum = 1900;

// Multiplier for gamma-encoded framebuffer values that turns D65 white into
// the white of the given colour temperature, red kept at 1. Clamped to
// Minimum..Neutral.
QVector3D gain(qreal kelvin);

// The temperature a fraction of the way from neutral to the target, even
// in mireds (perceptually closer to uniform than kelvin)
qreal partial(qreal targetKelvin, qreal strength);

}

#endif
