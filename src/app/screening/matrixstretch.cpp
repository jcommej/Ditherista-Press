#include "matrixstretch.h"
#include "matrices.h"  // libdither's layout of OrderedDitherMatrix (opaque in libdither.h)
#include <cmath>
#include <vector>

static int sampleIndex(const int pixel, const double pixelsPerElement, const int elements) {
    // which matrix element the centre of `pixel` falls in, wrapping every `elements` elements
    const int i = static_cast<int>(std::floor((pixel + 0.5) / pixelsPerElement)) % elements;
    return i < 0 ? i + elements : i;
}

static int wrapIndex(const double position, const double pixelsPerElement, const int elements) {
    const long i = static_cast<long>(std::floor(position / pixelsPerElement)) % elements;
    return static_cast<int>(i < 0 ? i + elements : i);
}

OrderedDitherMatrix* stretchMatrixToCell(const OrderedDitherMatrix* matrix, const double cellPx, const int width,
                                         const int height, const double angleDegrees) {
    const double pixelsPerElement = cellPx / std::sqrt(static_cast<double>(matrix->width) * matrix->height);
    const double angle = std::fmod(angleDegrees, 360.0);
    if (angle != 0.0) {
        // rotated screen: (u, v) are the pixel centre's coordinates along the screen's axes
        const double radians = angle * 3.14159265358979323846 / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        std::vector<int> buffer(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; y++) {
            int* row = buffer.data() + static_cast<size_t>(y) * width;
            const double py = y + 0.5;
            for (int x = 0; x < width; x++) {
                const double px = x + 0.5;
                const int ix = wrapIndex(px * c + py * s, pixelsPerElement, matrix->width);
                const int iy = wrapIndex(-px * s + py * c, pixelsPerElement, matrix->height);
                row[x] = matrix->buffer[static_cast<size_t>(iy) * matrix->width + ix];
            }
        }
        return OrderedDitherMatrix_new(width, height, matrix->divisor, buffer.data());
    }
    std::vector<int> columns(static_cast<size_t>(width));
    for (int x = 0; x < width; x++) {
        columns[x] = sampleIndex(x, pixelsPerElement, matrix->width);
    }
    std::vector<int> buffer(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; y++) {
        const int* sourceRow = matrix->buffer + static_cast<size_t>(sampleIndex(y, pixelsPerElement, matrix->height)) * matrix->width;
        int* row = buffer.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; x++) {
            row[x] = sourceRow[columns[x]];
        }
    }
    return OrderedDitherMatrix_new(width, height, matrix->divisor, buffer.data());
}
