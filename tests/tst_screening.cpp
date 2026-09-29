#include <QtTest>
#include <vector>
#include "screening/screengeometry.h"
#include "screening/cellresample.h"
#include "imagehash/imagehashmono.h"
#include "imagehash/imagehashcolor.h"

/* Tests for output DPI / LPI (screengeometry.h, cellresample.h) and their wiring into the image caches. */

static QImage greyImage(const int width, const int height, const int grey) {
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(qRgba(grey, grey, grey, 255));
    return image;
}

static QImage gradientImage(const int width, const int height) {
    QImage image(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int v = (x * 255) / (width - 1);
            image.setPixel(x, y, qRgba(v, (v + y * 7) % 256, 255 - v, 255));
        }
    }
    return image;
}

static std::vector<uint8_t> orderedDither(const DitherImage* image) {
    std::vector<uint8_t> out(static_cast<size_t>(image->width) * image->height);
    OrderedDitherMatrix* matrix = get_bayer8x8_matrix();
    ordered_dither(image, matrix, 0.0, out.data());
    OrderedDitherMatrix_free(matrix);
    return out;
}

class TestScreening : public QObject {
    Q_OBJECT
private slots:
    /* ---- geometry: how LPI becomes a cell size ---- */

    void geometryMatchesSpecExample() {
        // the example from the spec: 300 DPI, 45 LPI -> 6.67 px/cell
        ScreenGeometry g{300.0, 45.0, true};
        QCOMPARE(qRound(g.pixelsPerCell() * 100), 667);
        QCOMPARE(g.cellSize(), 7);                      // snapped to whole pixels
        QCOMPARE(qRound(g.effectiveLpi() * 100), 4286); // 300 / 7 = 42.86 LPI actually produced
        QCOMPARE(qRound(g.cellMm() * 1000), 593);       // 7 px at 300 DPI
    }

    void geometryScalesWithDpi() {
        // same LPI on a finer film: more pixels per cell, same physical dot pitch
        ScreenGeometry g{1200.0, 45.0, true};
        QCOMPARE(g.cellSize(), 27);  // 26.67 -> 27
        QCOMPARE(qRound(g.effectiveLpi() * 100), 4444);
    }

    void exactCellsAreNotRounded() {
        ScreenGeometry g{600.0, 50.0, true};
        QCOMPARE(g.cellSize(), 12);
        QCOMPARE(g.effectiveLpi(), 50.0);  // an exact ratio loses nothing
    }

    void disabledMeansOneDotPerPixel() {
        ScreenGeometry g{300.0, 45.0, false};
        QCOMPARE(g.cellSize(), 1);
        QCOMPARE(g.effectiveLpi(), 300.0);
    }

    void cellNeverBelowOnePixel() {
        ScreenGeometry g{300.0, 300.0, true};  // LPI above half the DPI would round to 0
        QCOMPARE(g.cellSize(), 1);
        g.lpi = 299.0;
        QCOMPARE(g.cellSize(), 1);
    }

    void physicalSize() {
        ScreenGeometry g{300.0, 45.0, false};
        QCOMPARE(g.sizeMm(300), 25.4);  // 300 px at 300 DPI = 1 inch
    }

    /* ---- resampling ---- */

    void coarseSizeCoversPartialCells() {
        QCOMPARE(coarseSize(9, 3), 3);
        QCOMPARE(coarseSize(10, 3), 4);
        QCOMPARE(coarseSize(10, 1), 10);
    }

    void monoDownsampleAveragesBlocks() {
        DitherImage* src = DitherImage_new(5, 3);
        for (int i = 0; i < 15; i++) {
            src->buffer[i] = i / 14.0;
            src->transparency[i] = 255;
        }
        DitherImage* dst = downsampleDitherImage(src, 2);
        QCOMPARE(dst->width, 3);
        QCOMPARE(dst->height, 2);
        // full 2x2 block: pixels 0,1,5,6
        QCOMPARE(dst->buffer[0], (0 + 1 + 5 + 6) / 4.0 / 14.0);
        // right-edge partial block (1 px wide): pixels 4 and 9 only
        QCOMPARE(dst->buffer[2], (4 + 9) / 2.0 / 14.0);
        // bottom-right corner: pixel 14 alone, not diluted by phantom pixels
        QCOMPARE(dst->buffer[5], 1.0);
        QCOMPARE(dst->transparency[5], static_cast<uint8_t>(255));
        DitherImage_free(src);
        DitherImage_free(dst);
    }

    void colorDownsampleAveragesInLinearLight() {
        // half black, half white averages to 50% light, which is sRGB ~188, not 128
        ColorImage* src = ColorImage_new(2, 1);
        ColorImage_set_rgb(src, 0, 0, 0, 0, 255);
        ColorImage_set_rgb(src, 1, 255, 255, 255, 255);
        ColorImage* dst = downsampleColorImage(src, 2);
        QCOMPARE(dst->width, 1);
        QVERIFY(dst->b_srgb[0].r >= 186 && dst->b_srgb[0].r <= 189);
        QCOMPARE(dst->b_srgb[0].a, static_cast<uint8_t>(255));
        ColorImage_free(src);
        ColorImage_free(dst);
    }

    void upsampleReplicatesAndCrops() {
        const uint8_t coarse[] = {1, 2, 3,
                                  4, 5, 6};
        std::vector<uint8_t> out(5 * 3);
        upsampleCells(coarse, 3, 2, out.data(), 5, 3);
        const std::vector<uint8_t> expected = {1, 1, 2, 2, 3,
                                               1, 1, 2, 2, 3,
                                               4, 4, 5, 5, 6};
        QCOMPARE(out, expected);
    }

    /* ---- wiring into the image caches ---- */

    void cellSizeOneIsExactlyTheOriginalPipeline() {
        // LPI off must not even copy the image: the ditherers read the very same buffer as before
        ImageHashMono mono;
        const QImage image = gradientImage(40, 30);
        mono.setSourceImage(&image);
        QVERIFY(!mono.setCellSize(1));  // already 1
        QCOMPARE(mono.getDitherSourceImage(), mono.getSourceImage());

        ImageHashColor color;
        color.setSourceImage(&image);
        QCOMPARE(color.getDitherSourceImage(), color.getSourceImage());
    }

    void returningToOneRestoresTheOriginalResult() {
        ImageHashMono mono;
        const QImage image = gradientImage(40, 30);
        mono.setSourceImage(&image);
        const std::vector<uint8_t> before = orderedDither(mono.getDitherSourceImage());
        QVERIFY(mono.setCellSize(5));
        orderedDither(mono.getDitherSourceImage());
        QVERIFY(mono.setCellSize(1));
        QCOMPARE(orderedDither(mono.getDitherSourceImage()), before);
    }

    void lpiMakesDotsCellSized() {
        // the dither runs on the coarse grid, so every n x n block of the film is one solid dot
        const int n = 4;
        ImageHashMono mono;
        const QImage image = greyImage(64, 48, 128);
        mono.setSourceImage(&image);
        mono.setCellSize(n);
        const DitherImage* coarse = mono.getDitherSourceImage();
        QCOMPARE(coarse->width, 16);
        QCOMPARE(coarse->height, 12);
        const std::vector<uint8_t> dots = orderedDither(coarse);
        mono.setImageFromDither(0, dots.data());
        const QImage* film = mono.getDitheredImage(0);
        QCOMPARE(film->size(), image.size());  // dimensions preserved
        bool hasBlack = false, hasWhite = false;
        for (int y = 0; y < film->height(); y++) {
            for (int x = 0; x < film->width(); x++) {
                const QRgb pixel = film->pixel(x, y);
                QCOMPARE(pixel, film->pixel(x - x % n, y - y % n));  // same as the cell's top-left pixel
                hasBlack |= qRed(pixel) == 0;
                hasWhite |= qRed(pixel) == 255;
            }
        }
        QVERIFY(hasBlack && hasWhite);  // a mid grey really is dithered, not flattened
    }

    void nonMultipleSizesAreCropped() {
        ImageHashColor color;
        const QImage image = gradientImage(23, 17);
        color.setSourceImage(&image);
        color.setCellSize(5);
        const ColorImage* coarse = color.getDitherSourceImage();
        QCOMPARE(coarse->width, 5);
        QCOMPARE(coarse->height, 4);
        std::vector<int> indices(static_cast<size_t>(coarse->width) * coarse->height, 0);
        BytePalette* palette = BytePalette_new(1);
        ByteColor black = {0, 0, 0, 255};
        BytePalette_set(palette, 0, &black);
        color.setImageFromDither(7, palette, indices.data());
        QCOMPARE(color.getDitheredImage(7)->size(), image.size());
        BytePalette_free(palette);
    }

    void adjustmentsInvalidateTheCoarseGrid() {
        ImageHashMono mono;
        const QImage image = greyImage(8, 8, 100);
        mono.setSourceImage(&image);
        mono.setCellSize(2);
        const double before = mono.getDitherSourceImage()->buffer[0];
        mono.brightness = 30;
        mono.adjustSource();
        QVERIFY(mono.getDitherSourceImage()->buffer[0] > before);
    }
};

QTEST_GUILESS_MAIN(TestScreening)
#include "tst_screening.moc"
