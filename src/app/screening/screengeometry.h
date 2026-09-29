#pragma once
#ifndef SCREENGEOMETRY_H
#define SCREENGEOMETRY_H

#include <cmath>

/* Output DPI, LPI and dot size
 * ----------------------------
 * Output DPI is the resolution of the film: how many device pixels per inch the pattern is drawn with.
 * LPI is the physical frequency of the screen, independent of the DPI: one cell is 25.4 / LPI mm
 * (45 LPI = 0.564 mm). Drawn at D DPI, that cell spans D / LPI pixels - 6.67 px at 300 DPI, 13.33 px at 600,
 * 26.67 px at 1200. The cell keeps its physical size at any DPI; a higher DPI only draws it more finely.
 *
 * LPI applies to algorithms that have a cell, i.e. ordered dithers built on a threshold matrix. One tile of the
 * matrix is stretched over one cell (see matrixstretch.h), so the pattern repeats every D / LPI pixels. The
 * cell size stays fractional: each pixel reads the threshold at its exact position inside its cell. A 6.67 px
 * cell necessarily covers 6 or 7 whole pixels, but every cell starts within one pixel of k * D / LPI, so the
 * error never accumulates and the frequency on film is exactly the requested LPI.
 * Note that some matrices contain several dots per tile (the 45 degree "magic" ones, for instance); they produce
 * that many dots per cell.
 *
 * Error diffusion, DBS, Riemersma and the other matrix-free algorithms have no cell, so LPI means nothing for
 * them. They get a dot size instead: the image is averaged down so that one dither pixel covers dotMm on film,
 * then each dot is replicated back as a block (see cellresample.h). That block must be a whole number of device
 * pixels, so the size actually produced is reported next to the requested one.
 *
 * With both options off, every image pixel is one device pixel, as in upstream Ditherista.
 */

inline constexpr double SCREEN_DEFAULT_DPI = 300.0;
inline constexpr double SCREEN_DEFAULT_LPI = 45.0;
inline constexpr double SCREEN_DEFAULT_DOT_MM = 0.25;
inline constexpr double SCREEN_MIN_DPI = 72.0;
inline constexpr double SCREEN_MAX_DPI = 4800.0;
inline constexpr double SCREEN_MIN_LPI = 1.0;
inline constexpr double SCREEN_MAX_LPI = 300.0;
inline constexpr double SCREEN_MIN_DOT_MM = 0.01;
inline constexpr double SCREEN_MAX_DOT_MM = 10.0;
inline constexpr double MM_PER_INCH = 25.4;

/* Physical size is the reference
 * ------------------------------
 * A picture's print size comes from its file resolution (pixels / file DPI) and can be edited. The working image
 * is the picture resampled to printSize x Output DPI, so changing the DPI changes the pixel count, never the size
 * on film. On load the Output DPI starts at the file's own DPI, which means no resampling at all: an untouched
 * image goes through the pipeline pixel for pixel, as in upstream Ditherista.
 *
 * The working image is capped: every stage keeps full-resolution buffers (double-precision grey, the dithered
 * result, the on-screen copy), around 75 bytes per pixel in total. */
inline constexpr long long WORKING_MAX_PIXELS = 80'000'000;

inline int pixelsFor(const double mm, const double dpi) {
    /* pixel count of `mm` at `dpi` */
    const long n = std::lround(mm * dpi / MM_PER_INCH);
    return n < 1 ? 1 : static_cast<int>(n);
}

struct ScreenGeometry {
    double dpi = SCREEN_DEFAULT_DPI;        // output (film) resolution
    double lpi = SCREEN_DEFAULT_LPI;        // screen frequency, for matrix-based algorithms
    bool lpiEnabled = false;
    double dotMm = SCREEN_DEFAULT_DOT_MM;   // dot size, for matrix-free algorithms
    bool dotEnabled = false;

    [[nodiscard]] double cellMm() const {
        /* physical size of one screen cell - depends on LPI only */
        return MM_PER_INCH / lpi;
    }

    [[nodiscard]] double pixelsPerCell() const {
        /* the same cell in device pixels at the output DPI - fractional, never rounded */
        return dpi / lpi;
    }

    [[nodiscard]] int dotPixels() const {
        /* dot size in whole device pixels (1 = off) */
        if (!dotEnabled) {
            return 1;
        }
        const long n = std::lround(dotMm * dpi / MM_PER_INCH);
        return n < 1 ? 1 : static_cast<int>(n);
    }

    [[nodiscard]] double actualDotMm() const {
        /* dot size actually produced once snapped to whole pixels */
        return MM_PER_INCH * dotPixels() / dpi;
    }

    [[nodiscard]] double sizeMm(const int pixels) const {
        /* physical size of an image dimension printed at the output DPI */
        return MM_PER_INCH * pixels / dpi;
    }
};

#endif // SCREENGEOMETRY_H
