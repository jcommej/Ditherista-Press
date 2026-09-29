#include "matrixstretch.h"
#include "matrices.h"  // libdither's layout of OrderedDitherMatrix (opaque in libdither.h)
#include <cmath>
#include <vector>

static int sampleIndex(const int pixel, const double pixelsPerElement, const int elements) {
    // which matrix element the centre of `pixel` falls in, wrapping every `elements` elements
    const int i = static_cast<int>(std::floor((pixel + 0.5) / pixelsPerElement)) % elements;
    return i < 0 ? i + elements : i;
}

OrderedDitherMatrix* stretchMatrixToCell(const OrderedDitherMatrix* matrix, const double cellPx, const int width, const int height) {
    const double pixelsPerElement = cellPx / std::sqrt(static_cast<double>(matrix->width) * matrix->height);
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
