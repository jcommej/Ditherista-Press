#pragma once
#ifndef SCREENGEOMETRY_H
#define SCREENGEOMETRY_H

#include <cmath>

/* How LPI becomes a dither cell
 * -----------------------------
 * A screen of L lines per inch, printed on film at D dots per inch, has cells of D / L device pixels per side:
 * 300 DPI at 45 LPI is 6.67 px per cell, i.e. 25.4 / 45 = 0.564 mm.
 *
 * libdither's algorithms place one dot per image pixel. To make dots bigger, the image is averaged down by an
 * integer factor N, dithered at that reduced size, and every resulting dot is replicated as an N x N block of
 * film pixels (see cellresample.h). The dither pattern is computed on the coarse grid, so the dots really are
 * N times larger - this is not a resize of the finished image.
 *
 * N must be a whole number. Using the fractional D / L would make neighbouring dots alternate between
 * floor(D / L) and ceil(D / L) pixels (6, 7, 7, 6, 7, ...), a visible beat pattern on film. N is therefore
 * round(D / L), and the LPI actually produced, D / N, is reported next to the requested one so the difference
 * is never hidden.
 *
 * With screening disabled N is 1, which bypasses resampling entirely and reproduces the original behaviour
 * bit for bit.
 */

inline constexpr double SCREEN_DEFAULT_DPI = 300.0;
inline constexpr double SCREEN_DEFAULT_LPI = 45.0;
inline constexpr double SCREEN_MIN_DPI = 72.0;
inline constexpr double SCREEN_MAX_DPI = 4800.0;
inline constexpr double SCREEN_MIN_LPI = 1.0;
inline constexpr double SCREEN_MAX_LPI = 300.0;
inline constexpr double MM_PER_INCH = 25.4;

struct ScreenGeometry {
    double dpi = SCREEN_DEFAULT_DPI;  // output (film) resolution
    double lpi = SCREEN_DEFAULT_LPI;  // requested screen frequency
    bool enabled = false;             // false: one dot per image pixel, as in upstream Ditherista

    [[nodiscard]] double pixelsPerCell() const {
        /* exact, unrounded cell size requested by the user */
        return dpi / lpi;
    }

    [[nodiscard]] int cellSize() const {
        /* cell size actually used, in whole film pixels */
        if (!enabled) {
            return 1;
        }
        const long n = std::lround(pixelsPerCell());
        return n < 1 ? 1 : static_cast<int>(n);
    }

    [[nodiscard]] double effectiveLpi() const {
        /* screen frequency actually produced once the cell is snapped to whole pixels */
        return dpi / cellSize();
    }

    [[nodiscard]] double cellMm() const {
        /* physical size of one produced cell */
        return MM_PER_INCH * cellSize() / dpi;
    }

    [[nodiscard]] double sizeMm(const int pixels) const {
        /* physical size of an image dimension printed at the output DPI */
        return MM_PER_INCH * pixels / dpi;
    }
};

#endif // SCREENGEOMETRY_H
