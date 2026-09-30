#include <QtTest>
#include <cmath>
#include "color/colorspace.h"

/* Tests for color/colorspace.h: HEX <-> RGB <-> CIELAB. Reference LAB values are the D65 figures published by
 * Bruce Lindbloom's calculator (sRGB, D65, 2 degree observer). */

static bool near(const Lab& lab, const double L, const double a, const double b, const double tolerance = 0.01) {
    return std::abs(lab.L - L) < tolerance && std::abs(lab.a - a) < tolerance && std::abs(lab.b - b) < tolerance;
}

static QString show(const Lab& lab) {
    return QString("L %1 a %2 b %3").arg(lab.L, 0, 'f', 4).arg(lab.a, 0, 'f', 4).arg(lab.b, 0, 'f', 4);
}

class TestColour : public QObject {
    Q_OBJECT
private slots:
    void hexToRgb() {
        QRgb c = 0;
        QVERIFY(parseHexColour("#3D3DDD", &c));
        QCOMPARE(c, qRgb(0x3D, 0x3D, 0xDD));
        QVERIFY(parseHexColour("3d3ddd", &c));  // no #, lower case
        QCOMPARE(c, qRgb(0x3D, 0x3D, 0xDD));
        QVERIFY(parseHexColour("  #FF3D3DDD ", &c));  // #AARRGGBB, as the palette list used to show
        QCOMPARE(c, qRgb(0x3D, 0x3D, 0xDD));
        QVERIFY(parseHexColour("#803D3DDD", &c));  // alpha dropped: palettes are opaque
        QCOMPARE(c, qRgb(0x3D, 0x3D, 0xDD));
        QVERIFY(parseHexColour("#F0A", &c));  // #RGB shorthand
        QCOMPARE(c, qRgb(0xFF, 0x00, 0xAA));
    }

    void invalidHexIsRefused() {
        QRgb c = qRgb(1, 2, 3);
        for (const char* bad : {"", "#", "#12", "#12345", "#1234567", "#GG0000", "red", "#123456789", "# 123456"}) {
            QVERIFY2(!parseHexColour(bad, &c), bad);
        }
        QCOMPARE(c, qRgb(1, 2, 3));  // untouched
    }

    void rgbToHex() {
        QCOMPARE(hexColour(qRgb(0x3D, 0x3D, 0xDD)), QString("#3D3DDD"));
        QCOMPARE(hexColour(qRgb(0, 0, 0)), QString("#000000"));
        QCOMPARE(hexColour(qRgba(255, 255, 255, 0)), QString("#FFFFFF"));  // alpha never shown
        QRgb back = 0;
        for (const QRgb c : {qRgb(1, 2, 3), qRgb(250, 128, 7), qRgb(255, 255, 255)}) {
            QVERIFY(parseHexColour(hexColour(c), &back));
            QCOMPARE(back, c);
        }
    }

    void rgbToLab() {
        QVERIFY2(near(::rgbToLab(qRgb(255, 255, 255)), 100.0, 0.0, 0.0), qPrintable(show(::rgbToLab(qRgb(255, 255, 255)))));
        QVERIFY(near(::rgbToLab(qRgb(0, 0, 0)), 0.0, 0.0, 0.0));
        QVERIFY2(near(::rgbToLab(qRgb(255, 0, 0)), 53.2408, 80.0925, 67.2032), qPrintable(show(::rgbToLab(qRgb(255, 0, 0)))));
        QVERIFY2(near(::rgbToLab(qRgb(0, 255, 0)), 87.7347, -86.1827, 83.1793), qPrintable(show(::rgbToLab(qRgb(0, 255, 0)))));
        QVERIFY2(near(::rgbToLab(qRgb(0, 0, 255)), 32.2970, 79.1875, -107.8602), qPrintable(show(::rgbToLab(qRgb(0, 0, 255)))));
        // a mid grey is neutral: a* = b* = 0
        const Lab grey = ::rgbToLab(qRgb(119, 119, 119));
        QVERIFY(std::abs(grey.a) < 1e-4 && std::abs(grey.b) < 1e-4);
        QVERIFY(std::abs(grey.L - 50.0) < 0.5);
    }

    void labToRgb() {
        QCOMPARE(::labToRgb({100.0, 0.0, 0.0}), qRgb(255, 255, 255));
        QCOMPARE(::labToRgb({0.0, 0.0, 0.0}), qRgb(0, 0, 0));
        QCOMPARE(::labToRgb({53.2408, 80.0925, 67.2032}), qRgb(255, 0, 0));
        QCOMPARE(::labToRgb({32.2970, 79.1875, -107.8602}), qRgb(0, 0, 255));
        // very dark colours go through the linear part of both curves
        QCOMPARE(::labToRgb(::rgbToLab(qRgb(3, 2, 1))), qRgb(3, 2, 1));
    }

    void rgbLabRoundTrip() {
        // every third level of each channel: 86^3 colours come back exactly
        for (int r = 0; r < 256; r += 3)
            for (int g = 0; g < 256; g += 3)
                for (int b = 0; b < 256; b += 3) {
                    const QRgb c = qRgb(r, g, b);
                    if (::labToRgb(::rgbToLab(c)) != c) {
                        QFAIL(qPrintable(QString("round trip of %1").arg(hexColour(c))));
                    }
                }
    }

    void hexToLabAndBack() {
        QRgb c = 0;
        QVERIFY(parseHexColour("#3D3DDD", &c));
        const Lab lab = ::rgbToLab(c);
        QVERIFY2(near(lab, 37.0193, 51.1786, -80.6726), qPrintable(show(lab)));
        QCOMPARE(hexColour(::labToRgb(lab)), QString("#3D3DDD"));
        QCOMPARE(hexColour(::labToRgb({53.2408, 80.0925, 67.2032})), QString("#FF0000"));
    }

    void gamut() {
        QVERIFY(inSrgbGamut({50.0, 0.0, 0.0}));
        QVERIFY(inSrgbGamut(::rgbToLab(qRgb(0, 255, 0))));
        QVERIFY(!inSrgbGamut({50.0, 120.0, 0.0}));  // far too saturated for sRGB
        QVERIFY(!inSrgbGamut({95.0, 0.0, -80.0}));  // a very light, very blue colour does not exist in sRGB
        // clamping keeps lightness and hue, and lands on the gamut's edge
        const Lab clamped = clampToSrgbGamut({50.0, 120.0, 40.0});
        QVERIFY(inSrgbGamut(clamped));
        QCOMPARE(clamped.L, 50.0);
        QVERIFY(std::abs(std::atan2(clamped.b, clamped.a) - std::atan2(40.0, 120.0)) < 1e-9);
        QVERIFY(!inSrgbGamut({50.0, clamped.a * 1.01, clamped.b * 1.01}));
        const Lab inside{60.0, 10.0, -10.0};
        QVERIFY(near(clampToSrgbGamut(inside), 60.0, 10.0, -10.0, 1e-12));  // already in: unchanged
        QCOMPARE(clampToSrgbGamut({130.0, 0.0, 0.0}).L, 100.0);
    }
};

QObject* newTestColour() { return new TestColour; }
#include "tst_colour.moc"
