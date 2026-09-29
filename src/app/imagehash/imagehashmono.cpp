#include "imagehashmono.h"
#include "../screening/cellresample.h"
#include "../adjust/filters.h"
#include "../adjust/tonecurve.h"
#include <vector>

void ImageHashMono::setSourceImage(const QImage* inputImage, const bool keepAdjustments) {
    /* sets the source images */
    ImageHash::setSourceImage(inputImage);
    DitherImage_free(sourceImage);  // previous picture or previous resolution (upstream leaked these)
    DitherImage_free(origLinear);
    clearCoarseImage();
    sourceImage = DitherImage_new(origQImage.width(), origQImage.height());
    origLinear = DitherImage_new(origQImage.width(), origQImage.height());
    for(int y = 0; y < sourceImage->height; y++) {
        for(int x = 0; x < sourceImage->width; x++) {
            const QRgb pixel = origQImage.pixel(QPoint(x, y));
            DitherImage_set_pixel_rgba(sourceImage, x, y, qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel), true);
            DitherImage_set_pixel_rgba(origLinear, x, y, qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel), true);
        }
    }
    if (!keepAdjustments) {
        brightness = DEFAULT_MONO_BRIGHTNESS_ADJUST;
        contrast = DEFAULT_MONO_CONTRAST_ADJUST;
        gamma = DEFAULT_MONO_GAMMA_ADJUST;
        blacks = shadows = midtones = highlights = whites = 0;
        blur = denoise = 0;
    }
    filtered.clear();  // belongs to the previous image
    filteredBlur = filteredDenoise = 0;
    adjustSource();
}

const double* ImageHashMono::filteredSource() {
    /* origLinear after denoise and blur. Neutral filters return origLinear itself, untouched. The result is cached
     * so that moving a tonal slider does not re-run the filters. */
    if (blur == 0 && denoise == 0) {
        filtered.clear();
        filteredBlur = filteredDenoise = 0;
        return origLinear->buffer;
    }
    if (filtered.empty() || filteredBlur != blur || filteredDenoise != denoise) {
        const int w = origLinear->width;
        const int h = origLinear->height;
        const size_t n = static_cast<size_t>(w) * h;
        std::vector<float> plane(n);
        for (size_t i = 0; i < n; i++) {
            plane[i] = static_cast<float>(gamma_encode(origLinear->buffer[i]));  // filters work perceptually
        }
        guidedDenoise(plane, w, h, denoise, denoiseScale);
        gaussianBlur(plane, w, h, blur / 100.0 * pixelsPerMm);
        filtered.resize(n);
        for (size_t i = 0; i < n; i++) {
            filtered[i] = gamma_decode(plane[i]);
        }
        filteredBlur = blur;
        filteredDenoise = denoise;
    }
    return filtered.data();
}

void ImageHashMono::adjustSource() {
    /* applies image adjustments: denoise and blur, then gamma, contrast, brightness, then the tonal zones */
    double dBrightness = (double)brightness / 100.0;
    double dContrast = (double)(contrast / 100.0) + 1.0;
    double dGamma = 1.0 / ((double)(gamma / 100.0) + 1.0);
    const double* base = filteredSource();
    const ToneCurve curve(blacks, shadows, midtones, highlights, whites);
    for (int y = 0; y < sourceImage->height; y++) {
        for (int x = 0; x < sourceImage->width; x++) {
            const size_t i = y * sourceImage->width + x;
            double c = base[i];
            c = pow(c, dGamma);               // apply gamma
            c = (c - 0.5) * dContrast + 0.5;  // apply contrast
            c = c + dBrightness;              // apply brightness
            c = c < 0.0 ? 0.0 : (c > 1.0 ? 1.0 : c); // clamp to 0.0 - 1.0 range
            if (!curve.isIdentity()) {        // tonal zones act on perceived tone, not linear light
                c = gamma_decode(curve.apply(gamma_encode(c)));
            }
            sourceImage->buffer[i] = c;
            int p = static_cast<int>(gamma_encode(c) * 255.0);
            sourceQImage.setPixel(x, y, qRgba(p, p, p, sourceImage->transparency[i]));
        }
    }
    clearCoarseImage();  // derived from sourceImage, rebuilt on next use
}

void ImageHashMono::clearCoarseImage() {
    DitherImage_free(coarseImage);
    coarseImage = nullptr;
}

void ImageHashMono::copyAdjustmentsFrom(const ImageHashMono& other) {
    brightness = other.brightness;
    contrast = other.contrast;
    gamma = other.gamma;
    blacks = other.blacks;
    shadows = other.shadows;
    midtones = other.midtones;
    highlights = other.highlights;
    whites = other.whites;
    blur = other.blur;
    denoise = other.denoise;
}

void ImageHashMono::setCellSize(const int n) {
    /* grid for the next dither only: cached results keep the grid they were dithered with, so switching between
     * algorithms that use different grids does not invalidate them */
    cellSize = n;
}

DitherImage* ImageHashMono::getDitherSourceImage() {
    if (cellSize == 1) {
        return sourceImage;
    }
    if (coarseImage != nullptr && coarseCellSize != cellSize) {
        clearCoarseImage();
    }
    if (coarseImage == nullptr) {
        coarseImage = downsampleDitherImage(sourceImage, cellSize);
        coarseCellSize = cellSize;
    }
    return coarseImage;
}

void ImageHashMono::reset() {
    /* clears all dithered images and source / original images */
    ImageHash::reset();
    if(sourceImage != nullptr) {
        DitherImage_free(sourceImage);
        DitherImage_free(origLinear);
    }
    clearCoarseImage();
}

DitherImage* ImageHashMono::getSourceImage() const {
    /* returns the source image (with adjustments) */
    return sourceImage;
}

void ImageHashMono::setImageFromDither(int i, const uint8_t* dither_buf) {
    /* sets the dithered image from the ditherer's output buffer, which is getDitherSourceImage()-sized */
    if(outImage.contains(i)) {
        clearDitheredImage(i);
    }
    const uint8_t* out_buf = dither_buf;
    std::vector<uint8_t> film;
    if (cellSize > 1) {  // grow each coarse dot back into a cellSize x cellSize block
        film.resize(static_cast<size_t>(sourceImage->width) * sourceImage->height);
        upsampleCells(dither_buf, getDitherSourceImage()->width, cellSize, film.data(), sourceImage->width, sourceImage->height);
        out_buf = film.data();
    }
    outImage[i] = new QImage(sourceImage->width, sourceImage->height, QImage::Format_ARGB32);
    outImage[i]->fill(qRgb(0, 0, 0));
    for (int y = 0; y < sourceImage->height; y++) {
        for (int x = 0; x < sourceImage->width; x++) {
            size_t addr = (size_t)(y * sourceImage->width + x);
            if (out_buf[addr] == 255) {
                outImage[i]->setPixel(x, y, qRgba(255, 255, 255, 255));
            } else if (out_buf[addr] == 128) {
                outImage[i]->setPixel(x, y, qRgba(0, 0, 0, 0));
            }
        }
    }
}