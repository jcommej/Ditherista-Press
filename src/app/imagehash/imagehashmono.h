#pragma once
#ifndef IMAGEHASHMONO_H
#define IMAGEHASHMONO_H

#include "../../../libdither/src/libdither/libdither.h"
#include "imagehash.h"
#include <QObject>
#include <QImage>
#include <QHash>
#include <vector>

// values are from 1 - 100
#define DEFAULT_MONO_BRIGHTNESS_ADJUST 0
#define DEFAULT_MONO_CONTRAST_ADJUST 0
#define DEFAULT_MONO_GAMMA_ADJUST 0

class ImageHashMono final : public ImageHash {
public:
    /* methods */
    void setSourceImage(const QImage* img);
    void setImageFromDither(int i, const uint8_t* out_buf);
    [[nodiscard]] DitherImage* getSourceImage() const;
    [[nodiscard]] DitherImage* getDitherSourceImage();  // what the ditherers read: the source, or its coarse cell grid
    void setCellSize(int n);                             // dot size in film pixels for the next dither (1 = off)
    void adjustSource();

    /* attributes */
    int brightness = DEFAULT_MONO_BRIGHTNESS_ADJUST;
    int contrast = DEFAULT_MONO_CONTRAST_ADJUST;
    int gamma = DEFAULT_MONO_GAMMA_ADJUST;
    int shadows = 0;     // -100..100, see adjust/tonecurve.h
    int midtones = 0;
    int highlights = 0;
    int blur = 0;        // Gaussian sigma in tenths of a pixel
    int denoise = 0;     // 0..100, see adjust/filters.h

private:
    /* attributes */
    DitherImage* sourceImage = nullptr;  // source with adjustments as DitherImage
    DitherImage* origLinear = nullptr;
    DitherImage* coarseImage = nullptr;  // sourceImage averaged down to one pixel per dot (cached)
    int coarseCellSize = 0;              // cell size coarseImage was built for
    int cellSize = 1;                    // 1 = one dot per image pixel
    std::vector<double> filtered;        // origLinear after denoise + blur (cached; empty when both are 0)
    int filteredBlur = 0;                // settings `filtered` was computed with
    int filteredDenoise = 0;
    /* methods */
    void reset();
    const double* filteredSource();
    void clearCoarseImage();
};

#endif  // IMAGEHASHMONO_H
