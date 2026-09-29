#pragma once
#ifndef FILMWRITER_H
#define FILMWRITER_H

#include <QImage>
#include <QString>

/* Lossless film export: PNG, TIFF and BMP, with the output DPI written into the file.
 *
 * TIFF is written here rather than through Qt's plugin, for two reasons that matter on film:
 *   - resolution is stored as an exact rational per inch (300/1, ResolutionUnit = inch). Qt stores dots per metre,
 *     which reads back as 299.9994 DPI;
 *   - a black and white film is written as a true 1-bit image (8x smaller than 8-bit grey, and what RIPs and
 *     imagesetters expect), with or without PackBits compression. PackBits is lossless and understood by every
 *     TIFF reader. Nothing in this path ever uses JPEG.
 * PNG has no per-inch field: its pHYs chunk only stores whole dots per metre, so 300 DPI is written as 11811
 * px/m, which every editor displays as 300.
 *
 * Classic TIFF offsets are 32-bit, so a TIFF over 4 GB is refused rather than written corrupt; a 1-bit film would
 * need to exceed 34 gigapixels to get there. */

enum class TiffCompression { None, PackBits };

// Reduces a dithered result to the smallest exact format: Format_Mono (1-bit) when every pixel is opaque pure
// black or white, RGB32 when every pixel is opaque, ARGB32 otherwise. Never changes a pixel value.
QImage toFilmImage(const QImage& dithered);

// Writes Format_Mono (1-bit), Grayscale8, RGB32 (8-bit RGB) or ARGB32 (8-bit RGBA) images. Other formats are
// converted to ARGB32 first. Returns false and sets `error` on failure; the file is written atomically.
bool writeTiff(const QString& path, const QImage& image, double dpi, TiffCompression compression, QString* error);
bool writePng(const QString& path, const QImage& image, double dpi, QString* error);
// BMP is uncompressed, so lossless too; like PNG it stores whole dots per metre. 1-bit films stay 1-bit.
bool writeBmp(const QString& path, const QImage& image, double dpi, QString* error);

#endif // FILMWRITER_H
