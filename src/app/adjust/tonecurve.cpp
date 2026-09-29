#include "tonecurve.h"
#include <algorithm>
#include <cmath>

ToneCurve::ToneCurve(const int shadows, const int midtones, const int highlights) {
    identity = shadows == 0 && midtones == 0 && highlights == 0;
    const int amounts[POINTS] = {0, shadows, midtones, highlights, 0};
    for (int i = 0; i < POINTS; i++) {
        ys[i] = xs[i] + MAX_SHIFT * std::clamp(amounts[i], -100, 100) / 100.0;
    }
    // Fritsch-Carlson: secant slopes, averaged at interior points, zeroed at extrema, then limited so each
    // cubic segment stays monotone
    double secants[POINTS - 1];
    for (int i = 0; i < POINTS - 1; i++) {
        secants[i] = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]);
    }
    slopes[0] = secants[0];
    slopes[POINTS - 1] = secants[POINTS - 2];
    for (int i = 1; i < POINTS - 1; i++) {
        slopes[i] = secants[i - 1] * secants[i] <= 0.0 ? 0.0 : (secants[i - 1] + secants[i]) / 2.0;
    }
    for (int i = 0; i < POINTS - 1; i++) {
        if (secants[i] == 0.0) {
            slopes[i] = slopes[i + 1] = 0.0;
            continue;
        }
        const double a = slopes[i] / secants[i];
        const double b = slopes[i + 1] / secants[i];
        const double h = a * a + b * b;
        if (h > 9.0) {
            const double t = 3.0 / std::sqrt(h);
            slopes[i] = t * a * secants[i];
            slopes[i + 1] = t * b * secants[i];
        }
    }
}

double ToneCurve::apply(const double value) const {
    if (identity) {
        return value;
    }
    const double v = std::clamp(value, 0.0, 1.0);
    int i = std::min(static_cast<int>(v / 0.25), POINTS - 2);  // points are evenly spaced
    const double h = xs[i + 1] - xs[i];
    const double t = (v - xs[i]) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    // cubic Hermite basis
    return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * slopes[i]
         + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * slopes[i + 1];
}
