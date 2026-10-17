// SPDX-FileCopyrightText: 2026 ccontino84
// SPDX-License-Identifier: LGPL-2.1-only

#include "correction.h"

#include "colormatrix.h"

namespace {

typedef double Matrix[3][3];

// Viénot, Brettel & Mollon 1999, "Digital video colourmaps for checking the
// legibility of displays by dichromats", Color Res. Appl. 24(4), 243-252,
// Eq. 4: linear RGB with the ITU-R BT.709 primaries and D65 white, which
// sRGB shares, to the cone fundamentals of Smith & Pokorny
const Matrix VienotRgbToLms = {
    { 17.8824, 43.5161, 4.11935 },
    { 3.45565, 27.1554, 3.86714 },
    { 0.0299566, 0.184309, 1.46709 }
};

// How much a protanomalous or deuteranomalous observer loses, by severity
// 0.0 to 1.0 in steps of 0.1, relative to a dichromat: the largest singular
// value of I − Γ for Machado, Oliveira & Fernandes 2009's matrices Γ (IEEE
// TVCG 15:1291, Table 1). At every severity the loss is almost one
// direction (the second singular value is under 7 % of the first), within
// 14° of the dichromat's, so the same correction fits, scaled to the loss.
const double ProtanLoss[11] = { 0, 0.174, 0.319, 0.444, 0.552, 0.647, 0.732, 0.808, 0.877, 0.941, 1 };
const double DeutanLoss[11] = { 0, 0.204, 0.367, 0.500, 0.610, 0.702, 0.780, 0.847, 0.905, 0.956, 1 };

// How each correction spreads the error of the missing cone, in LMS. The
// error goes into L and M equally: a change of brightness that keeps the
// red-green difference mild observers still see.
struct Variant {
    const char *name;
    // L, M or S
    int cone;
    // The spread at a strength of 1
    double full;
    // Strength is the severity: the spread follows the loss; or null, and
    // the spread is in proportion to the strength
    const double *loss;
};

// Calibrated against Machado observers (lightness difference, ΔL*, between
// digits and backgrounds that a dichromat confuses; the research is outside
// this repository), and checked on the phone by protan and tritan testers:
// - Spreading into the dichromat's two cones instead (the classic
//   daltonization) makes the remaining cone follow the missing one: it
//   cancels the red-green difference mild observers still see, and has
//   determinant 1 − k. Spreading into L and M equally keeps it, adds about
//   twice the lightness difference, never folds, and changes the picture
//   less for normal vision; it clips more (protan 11-22 %, deutan 20-33 %
//   of colours). Testers read better with it.
// - Full strength is for a dichromat, and gives a protanope, a deuteranope
//   and a tritanope (Brettel 1997) the same lightness difference: k 2.0,
//   2.5 and 0.5.
// - For tritan, L and M are both the brightness cones and the dichromat's
//   two cones, so this is the classic spread at half its strength (which
//   never reaches the fold at 1). Machado's tritan model can't give
//   the severity (its loss isn't one direction, and its full severity isn't
//   a tritanope), so the strength is in proportion.
const Variant Variants[] = {
    { "protan", 0, 2.0, ProtanLoss },
    { "deutan", 1, 2.5, DeutanLoss },
    { "tritan", 2, 0.5, nullptr },
};

const char Greyscale[] = "greyscale";

// The table at a strength of 0 to 1, between its steps of 0.1
double interpolate(const double *table, double strength)
{
    const double position = strength * 10;
    const int step = qMin(9, int(position));
    return table[step] + (table[step + 1] - table[step]) * (position - step);
}

const Variant *variant(const QString &name)
{
    const QString correction = name.trimmed().toLower();
    for (const Variant &v : Variants) {
        if (correction == QLatin1String(v.name))
            return &v;
    }
    return nullptr;
}

void multiply(const Matrix a, const Matrix b, Matrix out)
{
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            out[row][column] = 0;
            for (int i = 0; i < 3; ++i)
                out[row][column] += a[row][i] * b[i][column];
        }
    }
}

void invert(const Matrix m, Matrix out)
{
    // Adjugate over determinant
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            const int r1 = (column + 1) % 3, r2 = (column + 2) % 3;
            const int c1 = (row + 1) % 3, c2 = (row + 2) % 3;
            out[row][column] = m[r1][c1] * m[r2][c2] - m[r1][c2] * m[r2][c1];
        }
    }
    const double determinant = m[0][0] * out[0][0] + m[0][1] * out[1][0] + m[0][2] * out[2][0];
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            out[row][column] /= determinant;
    }
}

// RGB to LMS with each cone scaled so that white is (1, 1, 1). The
// simulation is the same at any scale; the correction is not, since it adds
// one cone's error to the others. In Viénot's units S is about 40 times
// smaller than L, and the correction would clip most colours.
void rgbToLms(Matrix out)
{
    for (int row = 0; row < 3; ++row) {
        const double white = VienotRgbToLms[row][0] + VienotRgbToLms[row][1] + VienotRgbToLms[row][2];
        for (int column = 0; column < 3; ++column)
            out[row][column] = VienotRgbToLms[row][column] / white;
    }
}

// A dichromat sees some colours as everyone does: greys, and two colours
// measured with people who have one dichromatic eye (Brettel, Viénot &
// Mollon 1997, JOSA A 14:2647): 475 and 575 nm for protans and deutans, 485
// and 660 nm for tritans. Every other colour looks like the one with the
// same response of the cones they have, along the missing cone's axis.
// Viénot 1999 (step 4) puts these colours on one plane, through black,
// white and the display's blue (and so yellow). Tritan isn't in that paper;
// here it gets the same construction with the display's red (and so cyan)
// for 660 and 485 nm. Its two colours don't share a plane as well, and
// Brettel's two half-planes are not one matrix. In LMS.
void projection(int cone, const Matrix lms, Matrix out)
{
    const double white[3] = { lms[0][0] + lms[0][1] + lms[0][2],
                              lms[1][0] + lms[1][1] + lms[1][2],
                              lms[2][0] + lms[2][1] + lms[2][2] };
    // The display's blue, or red for tritan
    const int primary = cone == 2 ? 0 : 2;
    const double anchor[3] = { lms[0][primary], lms[1][primary], lms[2][primary] };
    const double normal[3] = { white[1] * anchor[2] - white[2] * anchor[1],
                               white[2] * anchor[0] - white[0] * anchor[2],
                               white[0] * anchor[1] - white[1] * anchor[0] };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            out[row][column] = row == column ? 1 : 0;
    }
    // The missing cone's response, from the other two
    for (int column = 0; column < 3; ++column)
        out[cone][column] = column == cone ? 0 : -normal[column] / normal[cone];
}

// lms⁻¹ · m · lms: an LMS transform as one on linear sRGB
QMatrix3x3 toRgb(const Matrix lms, const Matrix m)
{
    Matrix inverse, product, result;
    invert(lms, inverse);
    multiply(m, lms, product);
    multiply(inverse, product, result);
    QMatrix3x3 matrix;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix(row, column) = float(result[row][column]);
    }
    return matrix;
}

}

namespace Correction {

QString normalize(const QString &name)
{
    const Variant *v = variant(name);
    if (v)
        return QLatin1String(v->name);
    if (name.trimmed().toLower() == QLatin1String(Greyscale))
        return QLatin1String(Greyscale);
    return QStringLiteral("none");
}

bool hasStrength(const QString &name)
{
    return variant(name) != nullptr;
}

QMatrix3x3 simulation(const QString &name)
{
    const Variant *v = variant(name);
    if (!v)
        return QMatrix3x3();
    Matrix lms, simulated;
    rgbToLms(lms);
    projection(v->cone, lms, simulated);
    return toRgb(lms, simulated);
}

QMatrix3x3 matrix(const QString &name, qreal strength)
{
    const Variant *v = variant(name);
    if (!v) {
        if (normalize(name) == QLatin1String(Greyscale))
            return ColorMatrix::saturation(0);
        return QMatrix3x3();
    }
    const double s = qBound(0.0, double(strength), 1.0);
    const double k = v->full * (v->loss ? interpolate(v->loss, s) : s);
    if (k == 0)
        return QMatrix3x3();
    const int cone = v->cone;
    Matrix lms, simulated;
    rgbToLms(lms);
    projection(cone, lms, simulated);

    // I + k · E · (I − simulation) in LMS (the structure of Fidaner, Lin &
    // Ozguven 2005, "Analysis of Color Blindness", Stanford Psych 221). The
    // error is only in the missing cone; E adds it to L and M. No source
    // derives E: it is a choice, to be judged by people with the
    // deficiency, and the strength scales it.
    Matrix correction;
    for (int row = 0; row < 3; ++row) {
        const bool receives = row < 2;
        for (int column = 0; column < 3; ++column) {
            const double error = (column == cone ? 1 : 0) - simulated[cone][column];
            correction[row][column] = (row == column ? 1 : 0) + (receives ? k * error : 0);
        }
    }
    return toRgb(lms, correction);
}

}
