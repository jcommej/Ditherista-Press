#include "filters.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>

static void parallelFor(const int count, const std::function<void(int, int)>& body) {
    /* runs body(begin, end) over [0, count) split across the available cores */
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 32);
    if (threads == 1 || count < 64) {
        body(0, count);
        return;
    }
    std::vector<std::thread> pool;
    const int chunk = (count + threads - 1) / threads;
    for (int begin = 0; begin < count; begin += chunk) {
        pool.emplace_back(body, begin, std::min(begin + chunk, count));
    }
    for (std::thread& t : pool) {
        t.join();
    }
}

void gaussianBlur(std::vector<float>& plane, const int width, const int height, const double sigma) {
    if (sigma <= 0.0) {
        return;
    }
    const int radius = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<float> kernel(static_cast<size_t>(2 * radius + 1));
    double sum = 0.0;
    for (int i = -radius; i <= radius; i++) {
        kernel[i + radius] = static_cast<float>(std::exp(-(i * i) / (2.0 * sigma * sigma)));
        sum += kernel[i + radius];
    }
    for (float& k : kernel) {
        k = static_cast<float>(k / sum);
    }
    std::vector<float> tmp(plane.size());
    // horizontal pass: plane -> tmp
    parallelFor(height, [&](const int y0, const int y1) {
        for (int y = y0; y < y1; y++) {
            const float* src = plane.data() + static_cast<size_t>(y) * width;
            float* dst = tmp.data() + static_cast<size_t>(y) * width;
            for (int x = 0; x < width; x++) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; k++) {
                    acc += kernel[k + radius] * src[std::clamp(x + k, 0, width - 1)];
                }
                dst[x] = acc;
            }
        }
    });
    // vertical pass: tmp -> plane
    parallelFor(height, [&](const int y0, const int y1) {
        for (int y = y0; y < y1; y++) {
            float* dst = plane.data() + static_cast<size_t>(y) * width;
            for (int x = 0; x < width; x++) {
                float acc = 0.0f;
                for (int k = -radius; k <= radius; k++) {
                    acc += kernel[k + radius] * tmp[static_cast<size_t>(std::clamp(y + k, 0, height - 1)) * width + x];
                }
                dst[x] = acc;
            }
        }
    });
}

static std::vector<float> boxMean(const std::vector<float>& in, const int width, const int height, const int r) {
    /* mean over the (2r+1)^2 window clipped to the image, via running sums: O(1) per pixel for any r.
     * float storage keeps film-sized planes affordable; the sums themselves are accumulated in double */
    std::vector<float> rows(in.size());
    parallelFor(height, [&](const int y0, const int y1) {
        for (int y = y0; y < y1; y++) {
            const float* src = in.data() + static_cast<size_t>(y) * width;
            float* dst = rows.data() + static_cast<size_t>(y) * width;
            double acc = 0.0;
            for (int x = 0; x <= std::min(r, width - 1); x++) {
                acc += src[x];
            }
            for (int x = 0; x < width; x++) {
                const int lo = std::max(x - r, 0);
                const int hi = std::min(x + r, width - 1);
                dst[x] = static_cast<float>(acc / (hi - lo + 1));
                if (x + r + 1 < width) acc += src[x + r + 1];
                if (x - r >= 0) acc -= src[x - r];
            }
        }
    });
    std::vector<float> out(in.size());
    parallelFor(width, [&](const int x0, const int x1) {
        for (int x = x0; x < x1; x++) {
            double acc = 0.0;
            for (int y = 0; y <= std::min(r, height - 1); y++) {
                acc += rows[static_cast<size_t>(y) * width + x];
            }
            for (int y = 0; y < height; y++) {
                const int lo = std::max(y - r, 0);
                const int hi = std::min(y + r, height - 1);
                out[static_cast<size_t>(y) * width + x] = static_cast<float>(acc / (hi - lo + 1));
                if (y + r + 1 < height) acc += rows[static_cast<size_t>(y + r + 1) * width + x];
                if (y - r >= 0) acc -= rows[static_cast<size_t>(y - r) * width + x];
            }
        }
    });
    return out;
}

void guidedDenoise(std::vector<float>& plane, const int width, const int height, const int strength) {
    if (strength <= 0) {
        return;
    }
    const double s = std::min(strength, DENOISE_MAX) / static_cast<double>(DENOISE_MAX);
    const int radius = 2 + static_cast<int>(std::lround(4.0 * s));  // 2..6 px
    const double noise = 0.12 * s;                                  // noise std-dev treated as flat, in 0..1 units
    const double eps = noise * noise;
    const size_t n = plane.size();

    std::vector<float> meanI = boxMean(plane, width, height, radius);
    std::vector<float> meanII;
    {
        std::vector<float> squares(n);
        for (size_t i = 0; i < n; i++) {
            squares[i] = plane[i] * plane[i];
        }
        meanII = boxMean(squares, width, height, radius);
    }
    // per-window linear model q = a * I + b, computed in place: meanII becomes a, meanI becomes b
    for (size_t i = 0; i < n; i++) {
        const double mean = meanI[i];
        const double variance = std::max(static_cast<double>(meanII[i]) - mean * mean, 0.0);
        const double a = variance / (variance + eps);  // ~1 on edges (keep), ~0 in flat noisy areas (smooth)
        meanII[i] = static_cast<float>(a);
        meanI[i] = static_cast<float>(mean * (1.0 - a));
    }
    const std::vector<float> meanA = boxMean(meanII, width, height, radius);
    const std::vector<float> meanB = boxMean(meanI, width, height, radius);
    for (size_t i = 0; i < n; i++) {
        plane[i] = std::clamp(meanA[i] * plane[i] + meanB[i], 0.0f, 1.0f);
    }
}
