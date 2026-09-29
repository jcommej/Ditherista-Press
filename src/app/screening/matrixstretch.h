#pragma once
#ifndef MATRIXSTRETCH_H
#define MATRIXSTRETCH_H

#include "libdither.h"

/* Stretches an ordered-dither threshold matrix so that one tile spans `cellPx` device pixels (DPI / LPI, see
 * screengeometry.h). cellPx may be fractional.
 *
 * The result is a width x height matrix - the size of the image - so libdither's `x % matrix->width` lookup
 * is the identity and the fractional period is kept exactly instead of being rounded into a repeating tile.
 * Each pixel samples the source matrix at its centre's position within its cell (nearest neighbour: thresholds
 * are ranks, and interpolating them would change the tone curve). Both axes use the same scale, chosen so that
 * the tile's area equals cellPx^2; for square matrices the period is exactly cellPx on both axes.
 *
 * angleDegrees rotates the screen: each pixel centre is rotated into the screen's own axes before the lookup,
 * so the cells line up along that angle - what gives the classic rosette when several inks are printed at
 * different angles instead of dot-on-dot. At 0 the lookup is the unrotated one, bit for bit.
 *
 * Returns a new matrix, caller frees it with OrderedDitherMatrix_free. */
OrderedDitherMatrix* stretchMatrixToCell(const OrderedDitherMatrix* matrix, double cellPx, int width, int height,
                                         double angleDegrees = 0.0);

#endif // MATRIXSTRETCH_H
