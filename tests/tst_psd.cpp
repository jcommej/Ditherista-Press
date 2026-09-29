#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <random>
#include "export/psdwriter.h"

/* Tests for the PSD writer (export/psdwriter.h). The file is decoded here with a minimal reader written from the
 * specification. Setting DITHERISTA_TEST_OUT keeps a copy of each document in that directory, for checking with
 * an independent reader (psd-tools, Photoshop). */

namespace {

struct Psd {
    int channels = 0, width = 0, height = 0, depth = 0, mode = 0;
    double dpi = 0;
    QStringList names;
    std::vector<QRgb> inks;
    std::vector<int> kinds;
    std::vector<QByteArray> planes;  // decoded channel data, width * height each
};

quint32 be32(const QByteArray& d, const qsizetype at) {
    return quint32(quint8(d[at])) << 24 | quint32(quint8(d[at + 1])) << 16 | quint32(quint8(d[at + 2])) << 8 | quint8(d[at + 3]);
}
quint16 be16(const QByteArray& d, const qsizetype at) { return quint16(quint8(d[at]) << 8 | quint8(d[at + 1])); }

Psd readPsd(const QString& path) {
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QByteArray d = f.readAll();
    Psd p;
    if (d.left(4) != "8BPS" || be16(d, 4) != 1) return p;
    p.channels = be16(d, 12);
    p.height = int(be32(d, 14));
    p.width = int(be32(d, 18));
    p.depth = be16(d, 22);
    p.mode = be16(d, 24);
    qsizetype at = 26;
    at += 4 + be32(d, at);                         // colour mode data
    const qsizetype resourcesEnd = at + 4 + be32(d, at);
    at += 4;
    while (at < resourcesEnd) {
        const quint16 id = be16(d, at + 4);
        at += 6;
        const int nameLength = quint8(d[at]);
        at += (nameLength + 2) & ~1;                // Pascal name padded to even
        const quint32 size = be32(d, at);
        const qsizetype data = at + 4;
        if (id == 1005) p.dpi = be32(d, data) / 65536.0;
        if (id == 1045) {
            qsizetype s = data;
            while (s < data + size) {
                const quint32 n = be32(d, s);
                QString name;
                for (quint32 i = 0; i < n; i++) { const quint16 ch = be16(d, s + 4 + 2 * i); if (ch) name += QChar(ch); }
                p.names << name;
                s += 4 + 2 * n;
            }
        }
        if (id == 1077) {
            for (qsizetype s = data + 4; s + 13 <= data + size; s += 13) {
                p.inks.push_back(qRgb(be16(d, s + 2) / 257, be16(d, s + 4) / 257, be16(d, s + 6) / 257));
                p.kinds.push_back(quint8(d[s + 12]));
            }
        }
        at = data + ((size + 1) & ~1u);
    }
    at = resourcesEnd;
    at += 4 + be32(d, at);                          // layer and mask info
    const quint16 compression = be16(d, at);
    at += 2;
    if (compression != 1) return p;
    std::vector<quint16> counts;
    for (int i = 0; i < p.channels * p.height; i++) { counts.push_back(be16(d, at)); at += 2; }
    for (int c = 0; c < p.channels; c++) {
        QByteArray plane;
        for (int y = 0; y < p.height; y++) {
            const qsizetype end = at + counts[size_t(c * p.height + y)];
            while (at < end) {
                const int n = qint8(d[at++]);
                if (n >= 0) { plane.append(d.mid(at, n + 1)); at += n + 1; }
                else if (n != -128) { plane.append(QByteArray(1 - n, d[at++])); }
            }
        }
        p.planes.push_back(plane);
    }
    return p;
}

QImage film(const int w, const int h, const unsigned seed) {
    QImage image(w, h, QImage::Format_ARGB32);
    std::mt19937 rng(seed);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            image.setPixel(x, y, (y < h / 2 ? (x / 9 + seed) % 2 : rng() & 1) ? 0xffffffffu : 0xff000000u);
    return image;
}

QByteArray greyBytes(const QImage& image) {
    const QImage g = image.convertToFormat(QImage::Format_Grayscale8);
    QByteArray out;
    for (int y = 0; y < g.height(); y++) out.append(reinterpret_cast<const char*>(g.constScanLine(y)), g.width());
    return out;
}

void keepCopy(const QString& path, const QString& name) {
    const QString dir = qEnvironmentVariable("DITHERISTA_TEST_OUT");
    if (!dir.isEmpty()) { QFile::remove(dir + "/" + name); QFile::copy(path, dir + "/" + name); }
}

}  // namespace

class TestPsd : public QObject {
    Q_OBJECT
private slots:
    void cmykSpotChannelsWithComposite() {
        QTemporaryDir dir;
        const QString path = dir.filePath("separation.psd");
        const int w = 157, h = 83;  // odd sizes
        QImage print(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) print.setPixel(x, y, qRgb(x, y * 3, 255 - x));
        const std::vector<PsdSpotChannel> spots = {
            {"Cyan", qRgb(0, 255, 255), film(w, h, 1)}, {"Magenta", qRgb(255, 0, 255), film(w, h, 2)},
            {"Yellow", qRgb(255, 255, 0), film(w, h, 3)}, {"Black", qRgb(0, 0, 0), film(w, h, 4)}};
        QString error;
        QVERIFY2(writePsd(path, print, spots, 300.0, &error), qPrintable(error));
        keepCopy(path, "separation.psd");

        const Psd p = readPsd(path);
        QCOMPARE(p.width, w);
        QCOMPARE(p.height, h);
        QCOMPARE(p.depth, 8);
        QCOMPARE(p.mode, 3);                 // RGB
        QCOMPARE(p.channels, 3 + 4);         // composite + one spot channel per ink
        QCOMPARE(p.dpi, 300.0);              // exact
        QCOMPARE(p.names, QStringList({"Cyan", "Magenta", "Yellow", "Black"}));
        QCOMPARE(p.kinds, std::vector<int>({2, 2, 2, 2}));  // spot, not alpha
        QCOMPARE(p.inks[1], qRgb(255, 0, 255));
        // composite and every film, pixel for pixel
        QByteArray red;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) red.append(char(x));
        QCOMPARE(p.planes[0], red);
        for (size_t i = 0; i < spots.size(); i++) {
            QCOMPARE(p.planes[3 + i], greyBytes(spots[i].film));
        }
    }

    void blackAndWhiteFilmIsAGreyscaleDocument() {
        QTemporaryDir dir;
        const QString path = dir.filePath("film.psd");
        const QImage f = film(64, 40, 5);
        QString error;
        QVERIFY(writePsd(path, f, {}, 1200.0, &error));
        keepCopy(path, "film.psd");
        const Psd p = readPsd(path);
        QCOMPARE(p.mode, 1);  // Grayscale
        QCOMPARE(p.channels, 1);
        QCOMPARE(p.dpi, 1200.0);
        QCOMPARE(p.planes[0], greyBytes(f));
    }

    void tooLargeIsRefusedNotCorrupted() {
        QTemporaryDir dir;
        QString error;
        QVERIFY(!writePsd(dir.filePath("big.psd"), QImage(PSD_MAX_SIDE + 1, 1, QImage::Format_RGB32), {}, 300.0, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(dir.filePath("big.psd")));
    }
};

QObject* newTestPsd() { return new TestPsd; }
#include "tst_psd.moc"
