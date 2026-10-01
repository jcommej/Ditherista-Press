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
 *
 * Palette separation works the other way round: the colour ditherer maps every pixel to exactly one palette
 * colour, and each colour becomes a film (splitByPalette). Inks never overlap, as in spot colour printing with
 * an indexed palette.
 */

enum class SeparationMode { Composite, RGB, CMYK, Palette };  // stored in presets: append only

struct InkChannel {
    QString name;  // used in file names: picture_Cyan.tif
    QRgb ink;      // colour for the simulated composite
};

// inks of RGB and CMYK; empty for Composite and Palette, whose inks come from the palette (paletteInks)
std::vector<InkChannel> channelsFor(SeparationMode mode);

// one ink per palette entry, named by its position and colour for the file names: 01_E03C28
std::vector<InkChannel> paletteInks(const std::vector<QRgb>& palette);

// coverage planes (0..1, width x height each) for every channel of `mode`, from an sRGB image
std::vector<std::vector<float>> separate(const QImage& srgb, SeparationMode mode);

// grey image the mono pipeline decodes to lightness 1 - coverage; alpha is taken from `alphaFrom`
QImage coverageToDitherSource(const std::vector<float>& coverage, const QImage& alphaFrom);

// Forces untouched and solid areas: 0 coverage becomes clear film, 1 becomes solid ink. Ordered ditherers put
// a dark dot on pure white where the smallest threshold lands exactly on 0.5 (1 pixel in 64 with Bayer 8x8); on
// a film those specks would print everywhere the ink should be absent. `film` is the dithered black/white
// result at the coverage plane's size.
void cleanExtremes(QImage& film, const std::vector<float>& coverage);

// simulated print of the dithered films (Format_Mono or black/white; null for an ink left out): inks multiplied on white for CMYK, added on
// black for RGB, so overlaps look like the press would show them
QImage compositeFromFilms(const std::vector<QImage>& films, SeparationMode mode);
// same with any inks; `additive`: light on black (RGB) rather than ink multiplied on white
QImage compositeFromFilms(const std::vector<QImage>& films, const std::vector<InkChannel>& inks, bool additive);

// films of a colour dither whose pixels are palette colours: film i is black where the pixel is palette[i] (the
// first entry of that colour, if the palette repeats one), white elsewhere and where the pixel is transparent.
// `wanted[i]` false (or missing) leaves film i null, as for a disabled ink.
std::vector<QImage> splitByPalette(const QImage& dithered, const std::vector<QRgb>& palette,
                                   const std::vector<bool>& wanted);

/* Palette films in print order (the order of the palette: film 0 is the first pass).
 * Overprint: an ink whose `overprint` is set also prints under every enabled ink that follows it - no knockout
 * there - so its film covers their areas too. Null films (disabled inks) are left alone and extend nothing. */
void extendUnderFollowing(std::vector<QImage>& films, const std::vector<bool>& overprint);

/* The print, side by side: every pixel takes the colour of the last ink printed there, nothing mixes; on white
 * where no ink prints. Without overprint it is the colour dither itself. */
QImage sideBySidePrint(const std::vector<QImage>& films, const std::vector<InkChannel>& inks);

/* The print, superposed: the passes in order on white paper, simulated as subtractive colour - spectral
 * Kubelka-Munk layers, see inksimulation.h. `opacity[i]` 0 (transparent ink, a filter) .. 1 (covering ink); the
 * colour of a pixel is worked out once per combination of inks. An ink alone gives its own colour. */
QImage superposedPrint(const std::vector<QImage>& films, const std::vector<InkChannel>& inks,
                       const std::vector<double>& opacity);

#endif // SEPARATION_H
