#pragma once
#ifndef FILTERS_H
#define FILTERS_H

#include <vector>

/* Spatial filters applied to the source before dithering (see ImageHash*::adjustSource).
 *
 * They work on one channel at a time: a width x height plane of perceptual (gamma-encoded) values in 0..1.
 * Perceptual rather than linear because noise and softness are judged by eye, and it matches what image editors
 * do; the tonal controls run afterwards on the filtered result.
 *
 * Both are no-ops at 0, returning the plane untouched, so neutral settings change nothing. */

inline constexpr double BLUR_MAX_PX = 20.0;
inline constexpr int DENOISE_MAX = 100;

// Gaussian blur, sigma in pixels. Separable, edges clamped. 0 = no change.
void gaussianBlur(std::vector<float>& plane, int width, int height, double sigma);

/* Edge-preserving denoise, strength 0..DENOISE_MAX. 0 = no change.
 * Self-guided filter (He et al., 2010): in flat areas each pixel moves toward its local mean, while near edges,
 * where local variance is high compared to the noise level eps, pixels are left almost untouched. That removes
 * the grain a dither would otherwise amplify into speckle, without softening outlines the way a blur does. Cost
 * is independent of the radius (box filters on running sums), which matters on film-sized images. */
void guidedDenoise(std::vector<float>& plane, int width, int height, int strength);

#endif // FILTERS_H
