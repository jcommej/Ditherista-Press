#include "cellresample.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

int coarseSize(const int pixels, const int n) {
    return (pixels + n - 1) / n;
}

DitherImage* downsampleDitherImage(const DitherImage* src, const int n) {
    const int cw = coarseSize(src->width, n);
    const int ch = coarseSize(src->height, n);
    DitherImage* dst = DitherImage_new(cw, ch);
    for (int cy = 0; cy < ch; cy++) {
        const int y1 = std::min((cy + 1) * n, src->height);
        for (int cx = 0; cx < cw; cx++) {
            const int x1 = std::min((cx + 1) * n, src->width);
            double value = 0.0;
            double alpha = 0.0;
            int count = 0;
            for (int y = cy * n; y < y1; y++) {
                for (int x = cx * n; x < x1; x++) {
                    const size_t i = static_cast<size_t>(y) * src->width + x;
                    value += src->buffer[i];  // DitherImage buffers are already linear
                    alpha += src->transparency[i];
                    count++;
                }
            }
            const size_t j = static_cast<size_t>(cy) * cw + cx;
            dst->buffer[j] = value / count;
            dst->transparency[j] = static_cast<uint8_t>(std::lround(alpha / count));
        }
    }
    return dst;
}

ColorImage* downsampleColorImage(const ColorImage* src, const int n) {
    // ColorImage keeps sRGB bytes; decode through a table rather than calling pow() per sample
    static const std::array<double, 256> toLinear = [] {
        std::array<double, 256> t{};
        for (int i = 0; i < 256; i++) {
            t[i] = gamma_decode(i / 255.0);
        }
        return t;
    }();
    const auto toByte = [](const double linear) {
        return static_cast<uint8_t>(std::lround(gamma_encode(linear) * 255.0));
    };
    const int cw = coarseSize(src->width, n);
    const int ch = coarseSize(src->height, n);
    ColorImage* dst = ColorImage_new(cw, ch);
    for (int cy = 0; cy < ch; cy++) {
        const int y1 = std::min((cy + 1) * n, src->height);
        for (int cx = 0; cx < cw; cx++) {
            const int x1 = std::min((cx + 1) * n, src->width);
            double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
            int count = 0;
            for (int y = cy * n; y < y1; y++) {
                for (int x = cx * n; x < x1; x++) {
                    const ByteColor& c = src->b_srgb[static_cast<size_t>(y) * src->width + x];
                    r += toLinear[c.r];
                    g += toLinear[c.g];
                    b += toLinear[c.b];
                    a += c.a;
                    count++;
                }
            }
            ColorImage_set_rgb(dst, static_cast<size_t>(cy) * cw + cx, toByte(r / count), toByte(g / count),
                               toByte(b / count), static_cast<uint8_t>(std::lround(a / count)));
        }
    }
    return dst;
}

template <typename T>
static void upsample(const T* coarse, const int coarseWidth, const int n, T* out, const int width, const int height) {
    for (int y = 0; y < height; y++) {
        const T* row = coarse + static_cast<size_t>(y / n) * coarseWidth;
        T* dst = out + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; x++) {
            dst[x] = row[x / n];
        }
    }
}

void upsampleCells(const uint8_t* coarse, const int coarseWidth, const int n, uint8_t* out, const int width, const int height) {
    upsample(coarse, coarseWidth, n, out, width, height);
}

void upsampleCells(const int* coarse, const int coarseWidth, const int n, int* out, const int width, const int height) {
    upsample(coarse, coarseWidth, n, out, width, height);
}
