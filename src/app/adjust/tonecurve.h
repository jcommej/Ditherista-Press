#pragma once
#ifndef TONECURVE_H
#define TONECURVE_H

/* Blacks / Shadows / Midtones / Highlights / Whites
 * -------------------------------------------------
 * A smooth, monotone transfer curve on perceptual (gamma-encoded) values in 0..1, built from five points.
 *
 * Shadows, Midtones and Highlights move a control point at the centre of their zone - 0.25, 0.5, 0.75 - up (+)
 * or down (-) by up to ZONE_SHIFT. Between points the curve is a monotone cubic (Fritsch-Carlson), so each
 * slider acts mostly on its own zone and fades out smoothly across its neighbours, with no visible break.
 *
 * Blacks and Whites move the ends of the curve, which the zone sliders never touch:
 *   Blacks  -  crushes: every input below up to ENDPOINT_RANGE becomes solid black (100% ink on film)
 *           +  lifts:   pure black is raised by up to ENDPOINT_RANGE, so there is no solid area left
 *   Whites  +  clips:   every input above 1 - ENDPOINT_RANGE becomes paper white (no dot at all)
 *           -  dulls:   pure white is lowered by up to ENDPOINT_RANGE, so every area keeps some dots
 * In screen printing that sets where the ink fills in and where the smallest printable dot ends.
 *
 * The ends stay outside the zone points (ENDPOINT_RANGE < 0.25), and the points' outputs are forced into
 * strictly increasing order, so no combination of sliders can invert tones: the curve only ever rises or,
 * where blacks are crushed or whites clipped, stays flat.
 *
 * All five at 0 is the identity, and callers skip the curve entirely in that case so the result is bit for bit
 * what it was before these controls existed.
 */
class ToneCurve {
public:
    static constexpr double ZONE_SHIFT = 0.20;
    static constexpr double ENDPOINT_RANGE = 0.20;

    // each amount in -100..100
    ToneCurve(int blacks, int shadows, int midtones, int highlights, int whites);
    [[nodiscard]] bool isIdentity() const { return identity; }
    [[nodiscard]] double apply(double value) const;  // value in 0..1, perceptual

private:
    static constexpr int POINTS = 5;
    double xs[POINTS]{};
    double ys[POINTS]{};
    double slopes[POINTS]{};
    bool identity = true;
};

#endif // TONECURVE_H
