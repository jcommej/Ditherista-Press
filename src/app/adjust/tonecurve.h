#pragma once
#ifndef TONECURVE_H
#define TONECURVE_H

/* Shadows / Midtones / Highlights
 * -------------------------------
 * A smooth, monotone transfer curve on perceptual (gamma-encoded) values in 0..1. Three control points sit at
 * the centre of each tonal zone - 0.25 (shadows), 0.5 (midtones), 0.75 (highlights) - and each slider moves its
 * point up or down; black (0) and white (1) stay fixed. Between the points the curve is a monotone cubic
 * (Fritsch-Carlson), so every slider acts mostly on its own zone and fades out smoothly across the neighbours,
 * with no visible break.
 *
 * A slider at +/-100 moves its point by MAX_SHIFT. MAX_SHIFT is chosen below half the gap between points
 * (0.25 / 2), so no combination of sliders can make the points cross: the curve is always increasing, and tones
 * can never be inverted.
 *
 * All three at 0 is the identity, and callers skip the curve entirely in that case so the result is bit for bit
 * what it was before these controls existed.
 */
class ToneCurve {
public:
    static constexpr double MAX_SHIFT = 0.12;

    // each amount in -100..100
    ToneCurve(int shadows, int midtones, int highlights);
    [[nodiscard]] bool isIdentity() const { return identity; }
    [[nodiscard]] double apply(double value) const;  // value in 0..1, perceptual

private:
    static constexpr int POINTS = 5;
    double xs[POINTS] = {0.0, 0.25, 0.5, 0.75, 1.0};
    double ys[POINTS]{};
    double slopes[POINTS]{};
    bool identity = true;
};

#endif // TONECURVE_H
