#pragma once
#ifndef PSDWRITER_H
#define PSDWRITER_H

#include <QImage>
#include <QString>
#include <vector>

/* Photoshop PSD export for films
 * ------------------------------
 * Separations are written the way screen printers keep them in Photoshop: an RGB document whose composite is
 * the simulated print (or plain white), plus one spot channel per ink - named, with its ink colour - holding
 * that ink's film (black = ink, as Photoshop shows spot channels). Each spot channel can be printed as a film
 * directly. Without separation, a black and white film becomes a Grayscale document and a colour dither an RGB
 * one.
 *
 * Written from the Adobe file format specification: flat document (no layers), 8 bits per channel, PackBits
 * (RLE) compression, which is lossless, and the resolution resource set to the output DPI in pixels per inch.
 * No PSD library was worth a dependency - the maintained ones read PSDs rather than write them - and the subset
 * needed here is small. Documents over 30000 px on a side need the PSB variant and are refused.
 */

struct PsdSpotChannel {
    QString name;
    QRgb ink;      // display colour of the spot channel
    QImage film;   // black = ink, white = clear; same size as the composite
};

inline constexpr int PSD_MAX_SIDE = 30000;

// `composite`: Grayscale document when it is black/white or grey and there are no spot channels, RGB otherwise
bool writePsd(const QString& path, const QImage& composite, const std::vector<PsdSpotChannel>& spots, double dpi,
              QString* error);

#endif // PSDWRITER_H
