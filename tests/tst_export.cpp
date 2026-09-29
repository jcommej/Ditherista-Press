#include <QtTest>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <random>
#include "export/filmwriter.h"

/* Tests for the lossless film writers (export/filmwriter.h). Files are read back with Qt's own readers - an
 * independent implementation - and the TIFF tags are also parsed directly to check the stored resolution. */

struct TiffTags {
    QMap<int, quint32> value;   // first value of each tag (offset for out-of-line values)
    QMap<int, quint32> count;
    QByteArray data;
    [[nodiscard]] quint32 u32(const quint32 at) const {
        return quint8(data[at]) | quint8(data[at + 1]) << 8 | quint8(data[at + 2]) << 16 | quint32(quint8(data[at + 3])) << 24;
    }
    [[nodiscard]] quint16 u16(const quint32 at) const { return quint8(data[at]) | quint8(data[at + 1]) << 8; }
};

static TiffTags readTags(const QString& path) {
    TiffTags t;
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    t.data = f.readAll();
    const quint32 ifd = t.u32(4);
    const int n = t.u16(ifd);
    for (int i = 0; i < n; i++) {
        const quint32 e = ifd + 2 + 12 * i;
        const int tag = t.u16(e);
        const int type = t.u16(e + 2);
        t.count[tag] = t.u32(e + 4);
        t.value[tag] = (type == 3 && t.count[tag] == 1) ? t.u16(e + 8) : t.u32(e + 8);
    }
    return t;
}

static QImage film(const int width, const int height, const unsigned seed) {
    // black and white with long runs and random noise, so PackBits sees both repeats and literals
    QImage image(width, height, QImage::Format_ARGB32);
    std::mt19937 rng(seed);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const bool white = y < height / 3 ? (x / 17) % 2 : (rng() & 1);
            image.setPixel(x, y, white ? 0xffffffffu : 0xff000000u);
        }
    }
    return image;
}

static QImage readBack(const QString& path) {
    QImageReader reader(path);
    const QImage image = reader.read();
    if (image.isNull()) qWarning() << reader.errorString();
    return image.convertToFormat(QImage::Format_ARGB32);
}

class TestExport : public QObject {
    Q_OBJECT
private slots:
    void blackAndWhiteBecomesOneBit() {
        const QImage source = film(37, 11, 1);
        const QImage f = toFilmImage(source);
        QCOMPARE(f.format(), QImage::Format_Mono);
        QCOMPARE(f.convertToFormat(QImage::Format_ARGB32), source);  // no pixel changed
    }

    void greyOrTransparentStayFullColour() {
        QImage grey = film(8, 8, 2);
        grey.setPixel(3, 3, qRgb(128, 128, 128));
        QCOMPARE(toFilmImage(grey).format(), QImage::Format_RGB32);
        QImage transparent = film(8, 8, 3);
        transparent.setPixel(2, 2, qRgba(0, 0, 0, 0));
        const QImage f = toFilmImage(transparent);
        QCOMPARE(f.format(), QImage::Format_ARGB32);
        QCOMPARE(f, transparent);
    }

    void tiffOneBitRoundTrips_data() {
        QTest::addColumn<int>("compression");
        QTest::addColumn<int>("width");
        QTest::newRow("uncompressed") << int(TiffCompression::None) << 301;  // width not a multiple of 8
        QTest::newRow("packbits") << int(TiffCompression::PackBits) << 301;
        QTest::newRow("packbits wide") << int(TiffCompression::PackBits) << 2000;
    }
    void tiffOneBitRoundTrips() {
        QFETCH(int, compression);
        QFETCH(int, width);
        QTemporaryDir dir;
        const QString path = dir.filePath("film.tif");
        const QImage source = film(width, 97, 4);
        QString error;
        QVERIFY2(writeTiff(path, toFilmImage(source), 300.0, TiffCompression(compression), &error), qPrintable(error));
        QCOMPARE(readBack(path), source);  // pixel for pixel through an independent reader
        const TiffTags tags = readTags(path);
        QCOMPARE(tags.value[256], quint32(width));
        QCOMPARE(tags.value[257], 97u);
        QCOMPARE(tags.value[258], 1u);                      // true 1-bit
        QCOMPARE(tags.value[259], compression == int(TiffCompression::PackBits) ? 32773u : 1u);
        QCOMPARE(tags.value[296], 2u);                      // per inch
        QCOMPARE(tags.u32(tags.value[282]), 300u);          // X resolution = 300 / 1 exactly
        QCOMPARE(tags.u32(tags.value[282] + 4), 1u);
        QCOMPARE(tags.u32(tags.value[283]), 300u);
    }

    void packBitsIsSmallerOnFilm() {
        QTemporaryDir dir;
        QImage flat(1200, 800, QImage::Format_ARGB32);
        flat.fill(0xffffffffu);
        for (int y = 300; y < 500; y++) for (int x = 200; x < 900; x++) flat.setPixel(x, y, 0xff000000u);
        QString error;
        writeTiff(dir.filePath("a.tif"), toFilmImage(flat), 600.0, TiffCompression::None, &error);
        writeTiff(dir.filePath("b.tif"), toFilmImage(flat), 600.0, TiffCompression::PackBits, &error);
        QVERIFY(QFileInfo(dir.filePath("b.tif")).size() < QFileInfo(dir.filePath("a.tif")).size() / 5);
        QCOMPARE(readBack(dir.filePath("b.tif")), flat);
    }

    void tiffColourRoundTrips() {
        QTemporaryDir dir;
        QImage rgb(50, 30, QImage::Format_RGB32);
        QImage rgba(50, 30, QImage::Format_ARGB32);
        for (int y = 0; y < 30; y++) {
            for (int x = 0; x < 50; x++) {
                rgb.setPixel(x, y, qRgb(x * 5, y * 8, (x + y) * 3));
                rgba.setPixel(x, y, qRgba(x * 5, y * 8, 200, (x * y) % 256));
            }
        }
        QString error;
        for (const auto c : {TiffCompression::None, TiffCompression::PackBits}) {
            QVERIFY(writeTiff(dir.filePath("rgb.tif"), rgb, 1200.0, c, &error));
            QCOMPARE(readBack(dir.filePath("rgb.tif")), rgb.convertToFormat(QImage::Format_ARGB32));
            QCOMPARE(readTags(dir.filePath("rgb.tif")).value[277], 3u);
            QVERIFY(writeTiff(dir.filePath("rgba.tif"), rgba, 1200.0, c, &error));
            // Qt reads RGBA TIFFs premultiplied, which drops the colour of fully transparent pixels: compare in
            // that representation, and check the stored bytes themselves below
            const QImage back = readBack(dir.filePath("rgba.tif"));
            const QImage a = back.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            const QImage b = rgba.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            int worst = 0;
            for (int y = 0; y < 30; y++) for (int x = 0; x < 50; x++) {
                const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
                worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                  std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
            }
            QVERIFY2(worst <= 1, qPrintable(QString("max channel difference %1").arg(worst)));  // reader's rounding
            if (c == TiffCompression::None) {
                const TiffTags tags = readTags(dir.filePath("rgba.tif"));
                const quint32 at = tags.value[273];  // first strip: straight (unassociated) R, G, B, A bytes
                QCOMPARE(tags.value[338], 2u);
                QCOMPARE(tags.data.mid(at, 4), QByteArray("\x00\x00\xc8\x00", 4));  // pixel (0,0), alpha 0 kept as is
            }
            QCOMPARE(readTags(dir.filePath("rgba.tif")).value[277], 4u);
        }
    }

    void fractionalDpiStaysExact() {
        QTemporaryDir dir;
        QString error;
        QVERIFY(writeTiff(dir.filePath("f.tif"), toFilmImage(film(16, 16, 5)), 299.5, TiffCompression::None, &error));
        const TiffTags tags = readTags(dir.filePath("f.tif"));
        QCOMPARE(tags.u32(tags.value[282]), 599u);
        QCOMPARE(tags.u32(tags.value[282] + 4), 2u);
    }

    void pngKeepsPixelsAndDpi() {
        QTemporaryDir dir;
        const QString path = dir.filePath("film.png");
        const QImage source = film(123, 45, 6);
        QString error;
        QVERIFY2(writePng(path, toFilmImage(source), 300.0, &error), qPrintable(error));
        QImageReader reader(path);
        const QImage back = reader.read();
        QCOMPARE(back.convertToFormat(QImage::Format_ARGB32), source);
        QCOMPARE(back.dotsPerMeterX(), 11811);  // PNG's only unit: 300 DPI to the nearest dot per metre
        QCOMPARE(qRound(back.dotsPerMeterX() * 0.0254), 300);
    }
};

QObject* newTestExport() { return new TestExport; }
#include "tst_export.moc"
