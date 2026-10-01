#pragma once
#ifndef PSDWRITER_H
#define PSDWRITER_H

#include <QImage>
#include <QString>
#include <vector>

/* Photoshop PSD export for films
 * ------------------------------
 * A separation can be written two ways, or both at once:
 *   - layers: one layer per ink, the ink's colour where the film has ink and transparent elsewhere, over a
 *     background layer. CMYK inks multiply over white "Paper"; RGB inks use Screen over a black "Garment", as
 *     light inks on a dark shirt. Toggling layers shows any combination of inks; locking a layer's
 *     transparency and filling it black gives that ink's film.
 *   - spot channels: one named, coloured spot channel per ink holding its film (black = ink), which Photoshop
 *     can print as a film directly.
 * The document's own image (what viewers without layer support show) is always the simulated print, or plain
 * white for spot channels alone when asked.
 * Without separation, a black and white film becomes a Grayscale document and a colour dither an RGB one.
 *
 * Written from the Adobe file format specification: 8 bits per channel, PackBits (RLE) compression, which is
 * lossless, compressed row by row so a large film never needs extra full-size planes, and the resolution
 * resource set to the output DPI in pixels per inch. No PSD library was worth a dependency - the maintained
 * ones read PSDs rather than write them. Documents over 30000 px on a side need the PSB variant and are refused.
 */

struct PsdSpotChannel {
    QString name;
    QRgb ink;      // display colour
    QImage film;   // black = ink, white = clear; same size as the composite
};

enum class PsdInkLayout { SpotChannels, Layers, LayersAndSpotChannels };

inline constexpr int PSD_MAX_SIDE = 30000;

// `composite`: the document's image. Grayscale when black/white or grey with no inks, RGB otherwise; an RGB
// composite with a colour space (QImage::colorSpace) carries it as the document's ICC profile.
// `additive`: RGB separation (Screen over black) rather than CMYK (Multiply over white); only used for layers.
bool writePsd(const QString& path, const QImage& composite, const std::vector<PsdSpotChannel>& inks, double dpi,
              QString* error, PsdInkLayout layout = PsdInkLayout::SpotChannels, bool additive = false);

#endif // PSDWRITER_H
