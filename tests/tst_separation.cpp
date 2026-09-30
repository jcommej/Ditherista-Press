#include <QtTest>
#include <vector>
#include "screening/separation.h"
#include "imagehash/imagehashmono.h"

/* Tests for colour separation (screening/separation.h): the conversion, the hand-off to the mono ditherers, and
 * the simulated composite. */

static QImage solid(const QRgb colour, const int w = 16, const int h = 16) {
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(colour);
    return image;
}

static std::vector<float> coverageOf(const QImage& image, const SeparationMode mode, const size_t channel) {
    return separate(image, mode)[channel];
}

static double inkFraction(const QImage& film) {
    const QImage argb = film.convertToFormat(QImage::Format_ARGB32);
    long ink = 0;
    for (int y = 0; y < argb.height(); y++)
        for (int x = 0; x < argb.width(); x++)
            ink += qRed(argb.pixel(x, y)) == 0;
    return static_cast<double>(ink) / (static_cast<double>(argb.width()) * argb.height());
}

static QImage ditherChannel(const std::vector<float>& coverage, const QImage& alphaFrom) {
    // what the app does per channel: coverage -> grey source -> mono cache -> ordered dither -> film
    const QImage source = coverageToDitherSource(coverage, alphaFrom);
    ImageHashMono hash;
    hash.setSourceImage(&source);
    const DitherImage* image = hash.getDitherSourceImage();
    std::vector<uint8_t> out(static_cast<size_t>(image->width) * image->height);
    OrderedDitherMatrix* matrix = get_bayer8x8_matrix();
    ordered_dither(image, matrix, 0.0, out.data());
    OrderedDitherMatrix_free(matrix);
    hash.setImageFromDither(0, out.data());
    QImage film = *hash.getDitheredImage(0);
    cleanExtremes(film, coverage);
    return film;
}

class TestSeparation : public QObject {
    Q_OBJECT
private slots:
    void cmykOfPrimaries() {
        const auto at = [](const QRgb c, const size_t ch) { return coverageOf(solid(c), SeparationMode::CMYK, ch)[0]; };
        // white paper: no ink anywhere
        for (size_t ch = 0; ch < 4; ch++) QCOMPARE(at(qRgb(255, 255, 255), ch), 0.0f);
        // black: all on the black film, none coloured
        QCOMPARE(at(qRgb(0, 0, 0), 3), 1.0f);
        for (size_t ch = 0; ch < 3; ch++) QCOMPARE(at(qRgb(0, 0, 0), ch), 0.0f);
        // red = magenta + yellow
        QCOMPARE(at(qRgb(255, 0, 0), 0), 0.0f);
        QCOMPARE(at(qRgb(255, 0, 0), 1), 1.0f);
        QCOMPARE(at(qRgb(255, 0, 0), 2), 1.0f);
        QCOMPARE(at(qRgb(255, 0, 0), 3), 0.0f);
        // cyan = cyan only
        QCOMPARE(at(qRgb(0, 255, 255), 0), 1.0f);
        QCOMPARE(at(qRgb(0, 255, 255), 1), 0.0f);
        // a neutral grey goes entirely to black (maximum black generation)
        QVERIFY(std::abs(at(qRgb(128, 128, 128), 3) - (1.0f - 128 / 255.0f)) < 1e-6f);
        for (size_t ch = 0; ch < 3; ch++) QVERIFY(std::abs(at(qRgb(128, 128, 128), ch)) < 1e-6f);
    }

    void rgbCoverageIsTheChannelValue() {
        const auto planes = separate(solid(qRgb(255, 64, 0)), SeparationMode::RGB);
        QCOMPARE(planes.size(), size_t(3));
        QCOMPARE(planes[0][0], 1.0f);
        QVERIFY(std::abs(planes[1][0] - 64 / 255.0f) < 1e-6f);
        QCOMPARE(planes[2][0], 0.0f);
    }

    void compositeModeHasNoChannels() {
        QVERIFY(channelsFor(SeparationMode::Composite).empty());
        QVERIFY(separate(solid(qRgb(1, 2, 3)), SeparationMode::Composite).empty());
    }

    void ditherSourceDecodesToOneMinusCoverage() {
        for (const float c : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const QImage alpha = solid(qRgb(0, 0, 0), 4, 4);
            const QImage source = coverageToDitherSource(std::vector<float>(16, c), alpha);
            ImageHashMono hash;
            hash.setSourceImage(&source);
            QVERIFY2(std::abs(hash.getSourceImage()->buffer[0] - (1.0 - c)) < 0.01, qPrintable(QString::number(c)));
        }
    }

    void ditheredFilmsCarryTheRightInk() {
        // a 50% cyan tint: about half the cyan film is ink, the other films stay clear
        const QImage tint = solid(qRgb(128, 255, 255), 64, 64);
        const auto planes = separate(tint, SeparationMode::CMYK);
        const double cyan = inkFraction(ditherChannel(planes[0], tint));
        QVERIFY2(std::abs(cyan - (1.0 - 128 / 255.0)) < 0.05, qPrintable(QString::number(cyan)));
        for (size_t ch = 1; ch < 4; ch++) {
            QCOMPARE(inkFraction(ditherChannel(planes[ch], tint)), 0.0);
        }
    }

    void clearAndSolidAreasAreClean() {
        // without cleanExtremes, Bayer 8x8 leaves 1 dark pixel in 64 on a 0% channel
        const QImage white = solid(qRgb(255, 255, 255), 64, 64);
        QCOMPARE(inkFraction(ditherChannel(std::vector<float>(64 * 64, 0.0f), white)), 0.0);
        QCOMPARE(inkFraction(ditherChannel(std::vector<float>(64 * 64, 1.0f), white)), 1.0);
    }

    void compositeMixesInksLikeThePress() {
        const QImage ink = solid(qRgb(0, 0, 0), 2, 1);
        const QImage none = solid(qRgb(255, 255, 255), 2, 1);
        // CMYK: cyan + magenta print blue, nothing prints white, black prints black
        QCOMPARE(compositeFromFilms({ink, ink, none, none}, SeparationMode::CMYK).pixel(0, 0), qRgb(0, 0, 255));
        QCOMPARE(compositeFromFilms({none, none, none, none}, SeparationMode::CMYK).pixel(0, 0), qRgb(255, 255, 255));
        QCOMPARE(compositeFromFilms({none, none, none, ink}, SeparationMode::CMYK).pixel(0, 0), qRgb(0, 0, 0));
        // RGB on a dark garment: red + green light up as yellow, nothing stays black
        QCOMPARE(compositeFromFilms({ink, ink, none}, SeparationMode::RGB).pixel(0, 0), qRgb(255, 255, 0));
        QCOMPARE(compositeFromFilms({none, none, none}, SeparationMode::RGB).pixel(0, 0), qRgb(0, 0, 0));
    }

    void paletteInksAreNamedForFiles() {
        const auto inks = paletteInks({qRgb(0xE0, 0x3C, 0x28), qRgba(0, 0, 255, 128)});
        QCOMPARE(inks.size(), size_t(2));
        QCOMPARE(inks[0].name, QString("01_E03C28"));
        QCOMPARE(inks[1].name, QString("02_0000FF"));
        QCOMPARE(inks[1].ink, qRgb(0, 0, 255));  // the ink is opaque whatever the palette's alpha
        QVERIFY(channelsFor(SeparationMode::Palette).empty());
    }

    void paletteSplitGivesEachPixelToOneFilm() {
        // a 4 x 1 dither: red, white, blue, transparent
        const std::vector<QRgb> palette{qRgb(255, 0, 0), qRgb(255, 255, 255), qRgb(0, 0, 255)};
        QImage dithered(4, 1, QImage::Format_ARGB32);
        dithered.setPixel(0, 0, qRgb(255, 0, 0));
        dithered.setPixel(1, 0, qRgb(255, 255, 255));
        dithered.setPixel(2, 0, qRgb(0, 0, 255));
        dithered.setPixel(3, 0, qRgba(0, 0, 0, 0));
        const auto films = splitByPalette(dithered, palette, {true, true, true});
        QCOMPARE(films.size(), size_t(3));
        const auto ink = [&](const size_t film, const int x) { return qRed(films[film].pixel(x, 0)) == 0; };
        QVERIFY(ink(0, 0) && !ink(0, 1) && !ink(0, 2) && !ink(0, 3));
        QVERIFY(!ink(1, 0) && ink(1, 1) && !ink(1, 2) && !ink(1, 3));
        QVERIFY(!ink(2, 0) && !ink(2, 1) && ink(2, 2) && !ink(2, 3));
        // the simulated print of the films is the dither again (transparent prints as paper)
        const QImage print = compositeFromFilms(films, paletteInks(palette), false);
        for (int x = 0; x < 3; x++) QCOMPARE(print.pixel(x, 0), dithered.pixel(x, 0));
        QCOMPARE(print.pixel(3, 0), qRgb(255, 255, 255));
    }

    void paletteSplitSkipsDisabledAndRepeatedColours() {
        // white left out, and a palette listing red twice: red prints once, on the first red film
        const std::vector<QRgb> palette{qRgb(255, 0, 0), qRgb(255, 255, 255), qRgb(255, 0, 0)};
        const auto films = splitByPalette(solid(qRgb(255, 0, 0), 2, 2), palette, {true, false, true});
        QCOMPARE(inkFraction(films[0]), 1.0);
        QVERIFY(films[1].isNull());
        QCOMPARE(inkFraction(films[2]), 0.0);
        // missing entries in `wanted` count as disabled
        QVERIFY(splitByPalette(solid(qRgb(255, 0, 0), 2, 2), palette, {true})[2].isNull());
    }
};

QObject* newTestSeparation() { return new TestSeparation; }
#include "tst_separation.moc"
