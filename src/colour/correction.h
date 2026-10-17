// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#ifndef OWLFISH_CORRECTION_H
#define OWLFISH_CORRECTION_H

#include <QGenericMatrix>
#include <QString>

// Colour correction, a 3×3 matrix in linear sRGB:
//   none           no change
//   protan         red-weak vision (L cones)
//   deutan         green-weak vision (M cones)
//   tritan         blue-weak vision (S cones)
//   greyscale      no colour at all, keeping each colour's brightness
// For the colour-vision deficiencies ("daltonization"), the difference a
// dichromat can't see becomes a difference of brightness, so colours they
// confuse differ in one they see. Colours they already see as everyone does
// stay as they are: greys, and blue and yellow (protan, deutan) or red and
// cyan (tritan). Full strength is for a dichromat; for protan and deutan
// the strength is the severity, from mild (anomalous) to full.
namespace Correction {

// One of the names above; anything else is "none"
QString normalize(const QString &name);
// Whether the strength applies; greyscale is always full
bool hasStrength(const QString &name);
// How a dichromat sees each colour (Viénot et al. 1999; tritan by the same
// construction); the identity for "none" and greyscale
QMatrix3x3 simulation(const QString &name);
// The correction: strength 0 is no change, 1 the full correction
QMatrix3x3 matrix(const QString &name, qreal strength);

}

#endif
