#pragma once
#ifndef SEPARATION_H
#define SEPARATION_H

#include <QImage>
#include <QString>
#include <vector>

/* Colour separation: one black-and-white film per ink
 * ---------------------------------------------------
 * The adjusted colour picture is split into ink coverage planes, 0 (no ink) to 1 (solid), and each plane is
 * dithered on its own with the chosen mono ditherer, so every film is a true 1-bit positive: black where the
 * ink goes.
 *
 * CMYK uses the classic conversion with maximum black generation, on sRGB values:
 *   K = 1 - max(R, G, B),  C = (1 - R - K) / (1 - K),  M and Y likewise.
 * Every grey goes entirely to the black film and neutral areas carry no coloured ink - robust on press, and a
 * common choice on light garments. A black-generation (GCR) control can refine it later.
 * RGB gives one film per primary, with coverage equal to the channel's value: the usual separation for
 * printing red, green and blue inks on a dark garment.
 *
 * The coverage planes are handed to the mono ditherers through an ordinary grey image (coverageToDitherSource):
 * a pixel whose linear lightness is 1 - c gets a fraction c of ink from any ditherer.
 */

enum class SeparationMode { Composite, RGB, CMYK };

struct InkChannel {
    QString name;  // used in file names: picture_Cyan.tif
    QRgb ink;      // colour for the simulated composite
};

std::vector<InkChannel> channelsFor(SeparationMode mode);

// coverage planes (0..1, width x height each) for every channel of `mode`, from an sRGB image
std::vector<std::vector<float>> separate(const QImage& srgb, SeparationMode mode);

// grey image the mono pipeline decodes to lightness 1 - coverage; alpha is taken from `alphaFrom`
QImage coverageToDitherSource(const std::vector<float>& coverage, const QImage& alphaFrom);

// Forces untouched and solid areas: 0 coverage becomes clear film, 1 becomes solid ink. Ordered ditherers put
// a dark dot on pure white where the smallest threshold lands exactly on 0.5 (1 pixel in 64 with Bayer 8x8); on
// a film those specks would print everywhere the ink should be absent. `film` is the dithered black/white
// result at the coverage plane's size.
void cleanExtremes(QImage& film, const std::vector<float>& coverage);

// simulated print of the dithered films (Format_Mono or black/white): inks multiplied on white for CMYK, added on
// black for RGB, so overlaps look like the press would show them
QImage compositeFromFilms(const std::vector<QImage>& films, SeparationMode mode);

#endif // SEPARATION_H
