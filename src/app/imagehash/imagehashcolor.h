#pragma once
#ifndef IMAGEHASHCOLOR_H
#define IMAGEHASHCOLOR_H

#include "../../../libdither/src/libdither/libdither.h"
#include "imagehash.h"
#include <QObject>
#include <QImage>
#include <QHash>

// values are from 1 - 100
#define DEFAULT_COLOR_BRIGHTNESS_ADJUST 0
#define DEFAULT_COLOR_CONTRAST_ADJUST 0
#define DEFAULT_COLOR_GAMMA_ADJUST 0
#define DEFAULT_COLOR_SATURATION_ADJUST 0

class ImageHashColor final : public ImageHash {
public:
    /* methods */
    void setSourceImage(const QImage* img);
    void setImageFromDither(int i, const BytePalette* pal, const int* out_buf);
    [[nodiscard]] ColorImage* getSourceImage() const;
    void adjustSource();
    ColorImage* getSourceImage();
    [[nodiscard]] ColorImage* getDitherSourceImage();  // what the ditherers read: the source, or its coarse cell grid
    void setCellSize(int n);                            // dot size in film pixels for the next dither (1 = off)

    /* attributes */
    int brightness = DEFAULT_COLOR_BRIGHTNESS_ADJUST;
    int contrast = DEFAULT_COLOR_CONTRAST_ADJUST;
    int gamma = DEFAULT_COLOR_GAMMA_ADJUST;
    int saturation = DEFAULT_COLOR_SATURATION_ADJUST;
    int shadows = 0;     // -100..100, see adjust/tonecurve.h
    int midtones = 0;
    int highlights = 0;
    int blur = 0;        // Gaussian sigma in tenths of a pixel
    int denoise = 0;     // 0..100, see adjust/filters.h
private:
    /* attributes */
    ColorImage* sourceImage = nullptr;  // source with adjustments as DitherImage
    ColorImage* coarseImage = nullptr;  // sourceImage averaged down to one pixel per dot (cached)
    int coarseCellSize = 0;             // cell size coarseImage was built for
    int cellSize = 1;                   // 1 = one dot per image pixel
    QImage filteredQImage;              // origQImage after denoise + blur (cached; null when both are 0)
    int filteredBlur = 0;               // settings filteredQImage was computed with
    int filteredDenoise = 0;
    /* methods */
    void reset();
    const QImage& filteredSource();
    void clearCoarseImage();
};

#endif  // IMAGEHASHCOLOR_H
