#pragma once
#ifndef INKSIMULATION_H
#define INKSIMULATION_H

#include <QRgb>
#include <array>
#include <vector>

/* Superposed print of palette inks: a simulation of subtractive colour - inks printed one over the other on paper.
 *
 * The model, as colour science does it for printed inks (all of it on 36 bands of 10 nm, 380..730 nm):
 * 1. Reflectance curves. A palette colour is read as "this ink printed alone on white paper"; its reflectance
 *    curve is the smoothest one between 0 and 1 that gives exactly that sRGB colour under D65: S. A. Burns' LHTSS
 *    method (least hyperbolic tangent slope squared, "Generating Reflectance Curves from sRGB Triplets",
 *    arXiv:1710.05732), solved by Newton's method. The paper is white, read the same way.
 * 2. Ink layers. Each pass is a Kubelka-Munk layer over what is under it (paper, or the passes before): it absorbs
 *    and scatters light, wavelength by wavelength. Its opacity sets the scattering (S X): at 0 the ink is a pure
 *    filter, light goes through it to the paper and back, and the order of the passes does not matter; towards 1
 *    it hides what is under it, and the last pass shows most. Its absorption is solved so that the ink alone on the
 *    paper gives back its palette colour: the simulation only models overlaps.
 * 3. Back to sRGB: CIE 1931 2-degree colour matching functions and D65 (CIE tables), XYZ to linear sRGB.
 * It is a model of ideal inks, not a measured proof: real inks, paper, mesh and ink film thickness differ. */
namespace InkSimulation {
constexpr int BANDS = 36;
using Spectrum = std::array<double, BANDS>;

Spectrum reflectanceFromSrgb(QRgb colour);  // LHTSS, 0 < reflectance < 1
QRgb srgbFromReflectance(const Spectrum& reflectance);

struct Ink {
    Spectrum infinite{};  // reflectance of a layer thick enough to hide anything (Kubelka-Munk R infinity)
    double scattering = 0.0;  // S X of one pass
};
// `printed`: the colour of the ink alone on `paper`; `opacity` 0 (transparent) .. 1 (covering)
Ink inkFromPrint(QRgb printed, double opacity, const Spectrum& paper);
// reflectance of one pass of `ink` over `below`
Spectrum overlay(const Ink& ink, const Spectrum& below);
// the colour of the passes `layers` (indices into `inks`, first pass first) on `paper`
QRgb print(const std::vector<Ink>& inks, const std::vector<int>& layers, const Spectrum& paper);
}

#endif // INKSIMULATION_H
