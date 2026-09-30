#pragma once
#ifndef COLORSPACE_H
#define COLORSPACE_H

#include <QColor>
#include <QString>

/* sRGB <-> CIELAB, and the HEX notation of palette colours
 * -------------------------------------------------------
 * Assumptions, stated once for the whole app:
 * - sRGB as in IEC 61966-2-1: the piecewise transfer function (linear below 0.04045, exponent 2.4 above) and the
 *   sRGB primaries, whose RGB -> XYZ matrix is the one for the D65 white point.
 * - CIELAB relative to the D65 reference white Xn = 0.95047, Yn = 1, Zn = 1.08883 (2 degree observer) - the
 *   white of sRGB itself, so white is L* 100, a* = b* = 0 with no chromatic adaptation.
 * - The exact CIE constants: epsilon = 216 / 24389, kappa = 24389 / 27 (not the rounded 0.008856 / 7.787 of older
 *   texts, which leave a small discontinuity at the junction).
 * libdither has an RGB -> LAB conversion for its colour comparisons, but internally, with the rounded constants
 * and no inverse; the palette editor uses this one. They agree to about 0.01 in L*a*b*.
 *
 * L* runs from 0 (black) to 100 (white); a* from green (negative) to red, b* from blue (negative) to yellow. The
 * colours sRGB can show form an irregular volume inside that space, not a sphere: labToSrgb reports components
 * outside 0..1 for colours beyond it (see inSrgbGamut, clampToSrgbGamut).
 */

struct Lab {
    double L = 0.0;
    double a = 0.0;
    double b = 0.0;
};

// sRGB components 0..1 -> CIELAB
Lab srgbToLab(double r, double g, double b);
Lab rgbToLab(QRgb rgb);
// CIELAB -> sRGB components, not clamped: below 0 or above 1 means outside the sRGB gamut
void labToSrgb(const Lab& lab, double* r, double* g, double* b);
// nearest 8-bit colour, components clamped to 0..255
QRgb labToRgb(const Lab& lab);
// true if the colour exists in sRGB, give or take `tolerance` on the 0..1 components
bool inSrgbGamut(const Lab& lab, double tolerance = 0.5 / 255.0);
// same lightness and hue, chroma reduced until the colour fits in sRGB (L* clamped to 0..100 first)
Lab clampToSrgbGamut(const Lab& lab);

// "#RRGGBB", "RRGGBB", "#RGB", "RGB", "#AARRGGBB" or "AARRGGBB" (the notations Ditherista shows and reads in
// palette files), case-insensitive, spaces around ignored. Alpha is read but dropped: palettes are opaque.
// Returns false - and leaves *rgb alone - for anything else.
bool parseHexColour(const QString& text, QRgb* rgb);
// "#RRGGBB", upper case
QString hexColour(QRgb rgb);

#endif // COLORSPACE_H
