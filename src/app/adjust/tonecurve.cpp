#include "tonecurve.h"
#include <algorithm>
#include <cmath>

static double fraction(const int amount) {
    return std::clamp(amount, -100, 100) / 100.0;
}

ToneCurve::ToneCurve(const int blacks, const int shadows, const int midtones, const int highlights, const int whites) {
    identity = blacks == 0 && shadows == 0 && midtones == 0 && highlights == 0 && whites == 0;

    // ends: a negative Blacks moves the black point right (crush), a positive one moves it up (lift);
    // a positive Whites moves the white point left (clip), a negative one moves it down (dull)
    const double b = fraction(blacks) * ENDPOINT_RANGE;
    const double w = fraction(whites) * ENDPOINT_RANGE;
    xs[0] = b < 0.0 ? -b : 0.0;
    ys[0] = b > 0.0 ? b : 0.0;
    xs[POINTS - 1] = w > 0.0 ? 1.0 - w : 1.0;
    ys[POINTS - 1] = w < 0.0 ? 1.0 + w : 1.0;

    // zone points
    const int zones[3] = {shadows, midtones, highlights};
    for (int i = 1; i <= 3; i++) {
        xs[i] = 0.25 * i;
        ys[i] = xs[i] + fraction(zones[i - 1]) * ZONE_SHIFT;
    }
    // keep outputs strictly increasing so tones can never invert, whatever the combination
    constexpr double GAP = 0.01;
    for (int i = 1; i < POINTS - 1; i++) {
        ys[i] = std::max(ys[i], ys[i - 1] + GAP);
    }
    for (int i = POINTS - 2; i > 0; i--) {
        ys[i] = std::min(ys[i], ys[i + 1] - GAP);
    }

    // Fritsch-Carlson: secant slopes, averaged at interior points, then limited so each cubic segment stays
    // monotone (all secants are positive here, since xs and ys both increase strictly)
    double secants[POINTS - 1];
    for (int i = 0; i < POINTS - 1; i++) {
        secants[i] = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i]);
    }
    slopes[0] = secants[0];
    slopes[POINTS - 1] = secants[POINTS - 2];
    for (int i = 1; i < POINTS - 1; i++) {
        slopes[i] = (secants[i - 1] + secants[i]) / 2.0;
    }
    for (int i = 0; i < POINTS - 1; i++) {
        const double a = slopes[i] / secants[i];
        const double c = slopes[i + 1] / secants[i];
        const double h = a * a + c * c;
        if (h > 9.0) {
            const double t = 3.0 / std::sqrt(h);
            slopes[i] = t * a * secants[i];
            slopes[i + 1] = t * c * secants[i];
        }
    }
}

double ToneCurve::apply(const double value) const {
    if (identity) {
        return value;
    }
    // flat beyond the ends: crushed blacks stay black, clipped whites stay white
    if (value <= xs[0]) {
        return ys[0];
    }
    if (value >= xs[POINTS - 1]) {
        return ys[POINTS - 1];
    }
    int i = 0;
    while (i < POINTS - 2 && value > xs[i + 1]) {
        i++;
    }
    const double h = xs[i + 1] - xs[i];
    const double t = (value - xs[i]) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    // cubic Hermite basis
    return (2 * t3 - 3 * t2 + 1) * ys[i] + (t3 - 2 * t2 + t) * h * slopes[i]
         + (-2 * t3 + 3 * t2) * ys[i + 1] + (t3 - t2) * h * slopes[i + 1];
}
