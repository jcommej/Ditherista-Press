#include <QtTest>
#include <vector>
#include "screening/screengeometry.h"
#include "screening/cellresample.h"
#include "screening/matrixstretch.h"
#include "matrices.h"  // to read stretched matrix values
#include <cstdlib>
#include <algorithm>
#include <cmath>
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

static std::vector<int> cellStarts(const double cellPx, const int width) {
    // x positions where a new matrix tile begins, read off a matrix whose values are their own column index
    std::vector<int> columns(8 * 8);
    for (int i = 0; i < 64; i++) {
        columns[i] = i % 8;
    }
    OrderedDitherMatrix* m = OrderedDitherMatrix_new(8, 8, 64.0, columns.data());
    OrderedDitherMatrix* s = stretchMatrixToCell(m, cellPx, width, 1);
    std::vector<int> starts;
    for (int x = 0; x < width; x++) {
        if (x == 0 || s->buffer[x] < s->buffer[x - 1]) {
            starts.push_back(x);
        }
    }
    OrderedDitherMatrix_free(m);
    OrderedDitherMatrix_free(s);
    return starts;
}

class TestScreening : public QObject {
    Q_OBJECT
private slots:
    /* ---- geometry: LPI is physical, DPI only sets how finely it is drawn ---- */

    void cellSizeIsPhysicalAndIndependentOfDpi() {
        // 45 LPI = 0.564 mm per cell, at any DPI
        for (const double dpi : {300.0, 600.0, 1200.0}) {
            ScreenGeometry g;
            g.dpi = dpi;
            g.lpi = 45.0;
            QCOMPARE(qRound(g.cellMm() * 1000), 564);
        }
    }

    void pixelsPerCellFollowDpi() {
        // same 0.564 mm cell drawn with more pixels as the DPI rises, never rounded
        ScreenGeometry g;
        g.lpi = 45.0;
        g.dpi = 300.0;
        QCOMPARE(qRound(g.pixelsPerCell() * 100), 667);
        g.dpi = 600.0;
        QCOMPARE(qRound(g.pixelsPerCell() * 100), 1333);
        g.dpi = 1200.0;
        QCOMPARE(qRound(g.pixelsPerCell() * 100), 2667);
    }

    void lowerLpiMeansBiggerCells() {
        ScreenGeometry g;
        g.lpi = 30.0;
        const double coarse = g.cellMm();
        g.lpi = 60.0;
        QVERIFY(coarse > g.cellMm());
        QCOMPARE(coarse, 2 * g.cellMm());
    }

    void dotSizeSnapsToWholePixels() {
        ScreenGeometry g;
        g.dotEnabled = true;
        g.dotMm = 0.25;
        g.dpi = 300.0;
        QCOMPARE(g.dotPixels(), 3);  // 2.95 px
        QCOMPARE(qRound(g.actualDotMm() * 1000), 254);
        g.dpi = 1200.0;
        QCOMPARE(g.dotPixels(), 12); // 11.8 px: same physical dot, finer rounding
        QCOMPARE(qRound(g.actualDotMm() * 1000), 254);
    }

    void dotSizeOffOrTinyMeansOneDotPerPixel() {
        ScreenGeometry g;
        QCOMPARE(g.dotPixels(), 1);  // disabled by default
        g.dotEnabled = true;
        g.dotMm = 0.01;              // below one pixel
        QCOMPARE(g.dotPixels(), 1);
    }

    void physicalSize() {
        ScreenGeometry g;
        g.dpi = 300.0;
        QCOMPARE(g.sizeMm(300), 25.4);  // 300 px at 300 DPI = 1 inch
    }

    void printSizeIsKeptAcrossDpi() {
        // a 200 x 150 mm print: the pixel count follows the DPI, the size on film does not
        for (const double dpi : {300.0, 600.0, 1200.0}) {
            ScreenGeometry g;
            g.dpi = dpi;
            const int w = pixelsFor(200.0, dpi);
            const int h = pixelsFor(150.0, dpi);
            QVERIFY(std::abs(g.sizeMm(w) - 200.0) <= MM_PER_INCH / dpi);  // within one pixel
            QVERIFY(std::abs(g.sizeMm(h) - 150.0) <= MM_PER_INCH / dpi);
        }
        QCOMPARE(pixelsFor(200.0, 600.0), 2 * pixelsFor(200.0, 300.0) + (pixelsFor(200.0, 600.0) % 2));
        QCOMPARE(pixelsFor(25.4, 300.0), 300);  // one inch
        QCOMPARE(pixelsFor(0.0001, 300.0), 1);  // never zero
    }

    void previewStaysLightForLargeFilms() {
        // a film within budget is previewed at full DPI: preview and film are the same image
        QCOMPARE(previewDpiFor(300.0, 254.0, 169.3), 300.0);
        // 1 x 1 m at 300 DPI (140 MP) and 254 x 169 mm at 1200 DPI (96 MP) get a reduced preview under budget
        for (const auto& [dpi, w, h] : {std::tuple{300.0, 1000.0, 1000.0}, std::tuple{1200.0, 254.0, 169.3}}) {
            const double preview = previewDpiFor(dpi, w, h);
            QVERIFY(preview < dpi);
            QVERIFY(static_cast<double>(pixelsFor(w, preview)) * pixelsFor(h, preview) <= PREVIEW_MAX_PIXELS);
            QVERIFY(static_cast<double>(pixelsFor(w, preview)) * pixelsFor(h, preview) > 0.95 * PREVIEW_MAX_PIXELS);
        }
        // and the film itself fits the export limit that the UI enforces
        QVERIFY(static_cast<long long>(pixelsFor(1000.0, 300.0)) * pixelsFor(1000.0, 300.0) <= EXPORT_MAX_PIXELS);
    }

    /* ---- matrix stretching: the pattern repeats every DPI / LPI pixels ---- */

    void periodMatchesLpiAtEveryDpi() {
        // 4 inches of film at 45 LPI is 180 cells, whatever the DPI
        for (const double dpi : {300.0, 600.0, 1200.0}) {
            ScreenGeometry g;
            g.dpi = dpi;
            g.lpi = 45.0;
            const int width = static_cast<int>(4 * dpi);
            const std::vector<int> starts = cellStarts(g.pixelsPerCell(), width);
            QCOMPARE(static_cast<int>(starts.size()), 180);
            // no drift: cell k starts within one pixel of its exact position k * DPI / LPI
            for (size_t k = 0; k < starts.size(); k++) {
                QVERIFY2(std::abs(starts[k] - k * g.pixelsPerCell()) <= 1.0,
                         qPrintable(QString("cell %1 at %2 DPI starts at %3").arg(k).arg(dpi).arg(starts[k])));
            }
        }
    }

    void wholeCellStretchIsTheOriginalTiling() {
        // an 8 px cell for an 8x8 matrix is the upstream behaviour: every pixel keeps its threshold
        std::vector<int> values(64);
        for (int i = 0; i < 64; i++) {
            values[i] = (i * 37) % 64;
        }
        OrderedDitherMatrix* m = OrderedDitherMatrix_new(8, 8, 64.0, values.data());
        OrderedDitherMatrix* s = stretchMatrixToCell(m, 8.0, 20, 13);
        for (int y = 0; y < 13; y++) {
            for (int x = 0; x < 20; x++) {
                QCOMPARE(s->buffer[y * 20 + x], values[(y % 8) * 8 + x % 8]);
            }
        }
        OrderedDitherMatrix_free(m);
        OrderedDitherMatrix_free(s);
    }

    static double fractionEqualAfterShift(const OrderedDitherMatrix* s, const int dx, const int dy) {
        long same = 0, total = 0;
        for (int y = 0; y + dy < s->height; y++) {
            for (int x = std::max(0, -dx); x + dx < s->width && x < s->width; x++) {
                same += s->buffer[y * s->width + x] == s->buffer[(y + dy) * s->width + x + dx];
                total++;
            }
        }
        return static_cast<double>(same) / total;
    }

    void rotatedScreenRepeatsAlongItsAngle() {
        // 36.87 degrees (a 3-4-5 triangle): one 10 px cell along the screen is exactly (8, 6) pixels
        std::vector<int> values(64);
        for (int i = 0; i < 64; i++) values[i] = (i * 37) % 64;
        OrderedDitherMatrix* m = OrderedDitherMatrix_new(8, 8, 64.0, values.data());
        const double angle = std::atan2(3.0, 4.0) * 180.0 / 3.14159265358979323846;
        OrderedDitherMatrix* s = stretchMatrixToCell(m, 10.0, 200, 200, angle);
        QVERIFY(fractionEqualAfterShift(s, 8, 6) > 0.99);   // one cell along the rotated axis
        QVERIFY(fractionEqualAfterShift(s, -6, 8) > 0.99);  // and along the perpendicular one
        QVERIFY(fractionEqualAfterShift(s, 10, 0) < 0.8);   // but not along the unrotated one
        OrderedDitherMatrix_free(s);
        OrderedDitherMatrix_free(m);
    }

    void zeroAndFullTurnAreTheUnrotatedScreen() {
        std::vector<int> values(64);
        for (int i = 0; i < 64; i++) values[i] = (i * 37) % 64;
        OrderedDitherMatrix* m = OrderedDitherMatrix_new(8, 8, 64.0, values.data());
        OrderedDitherMatrix* plain = stretchMatrixToCell(m, 6.67, 90, 70);
        for (const double angle : {0.0, 360.0}) {
            OrderedDitherMatrix* s = stretchMatrixToCell(m, 6.67, 90, 70, angle);
            QVERIFY(std::equal(s->buffer, s->buffer + 90 * 70, plain->buffer));
            OrderedDitherMatrix_free(s);
        }
        OrderedDitherMatrix_free(plain);
        OrderedDitherMatrix_free(m);
    }

    void stretchedDitherKeepsTheTone() {
        // a mid grey through a stretched clustered-dot matrix still comes out about half ink
        DitherImage* grey = DitherImage_new(400, 400);
        for (int i = 0; i < 400 * 400; i++) {
            grey->buffer[i] = 0.5;
            grey->transparency[i] = 255;
        }
        OrderedDitherMatrix* m = get_bayer_clustered_dot_1_matrix();
        OrderedDitherMatrix* s = stretchMatrixToCell(m, 300.0 / 45.0, 400, 400);
        std::vector<uint8_t> out(400 * 400);
        ordered_dither(grey, s, 0.0, out.data());
        int white = 0;
        for (const uint8_t v : out) {
            white += v == 0xff;
        }
        const double coverage = white / (400.0 * 400.0);
        QVERIFY2(coverage > 0.4 && coverage < 0.6, qPrintable(QString::number(coverage)));
        OrderedDitherMatrix_free(m);
        OrderedDitherMatrix_free(s);
        DitherImage_free(grey);
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
        // dot size off must not even copy the image: the ditherers read the very same buffer as before
        ImageHashMono mono;
        const QImage image = gradientImage(40, 30);
        mono.setSourceImage(&image);
        mono.setCellSize(1);
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
        mono.setCellSize(5);
        orderedDither(mono.getDitherSourceImage());
        mono.setCellSize(1);
        QCOMPARE(orderedDither(mono.getDitherSourceImage()), before);
    }

    void coarseGridFollowsTheCellSize() {
        // switching algorithms switches grids; the cached coarse image must never be reused at the wrong size
        ImageHashMono mono;
        const QImage image = greyImage(60, 60, 128);
        mono.setSourceImage(&image);
        mono.setCellSize(3);
        QCOMPARE(mono.getDitherSourceImage()->width, 20);
        mono.setCellSize(4);
        QCOMPARE(mono.getDitherSourceImage()->width, 15);
        mono.setCellSize(1);
        QCOMPARE(mono.getDitherSourceImage()->width, 60);
    }

    void dotSizeMakesDotsBlockSized() {
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

QObject* newTestScreening() { return new TestScreening; }
#include "tst_screening.moc"
