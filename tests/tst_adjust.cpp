#include <QtTest>
#include <cmath>
#include <random>
#include <vector>
#include "adjust/tonecurve.h"
#include "adjust/filters.h"
#include "imagehash/imagehashmono.h"
#include "imagehash/imagehashcolor.h"

/* Tests for Shadows / Midtones / Highlights, Blur and Denoise (adjust/) and their wiring into the image caches. */

static QImage photoLikeImage(const int width, const int height) {
    // gradients plus deterministic grain, so every adjustment has something to act on
    QImage image(width, height, QImage::Format_ARGB32);
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> grain(-12, 12);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int v = x * 255 / (width - 1);
            const auto c = [&](const int base) { return std::clamp(base + grain(rng), 0, 255); };
            image.setPixel(x, y, qRgba(c(v), c((v + y * 5) % 256), c(255 - v), 255));
        }
    }
    return image;
}

static double stddev(const std::vector<float>& plane, const int width, const int x0, const int x1, const int y0, const int y1) {
    double sum = 0.0, sq = 0.0;
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const double v = plane[static_cast<size_t>(y) * width + x];
            sum += v;
            sq += v * v;
            n++;
        }
    }
    const double mean = sum / n;
    return std::sqrt(std::max(sq / n - mean * mean, 0.0));
}

class TestAdjust : public QObject {
    Q_OBJECT
private slots:
    /* ---- tone curve ---- */

    void neutralCurveIsIdentity() {
        const ToneCurve curve(0, 0, 0);
        QVERIFY(curve.isIdentity());
        for (int i = 0; i <= 100; i++) {
            QCOMPARE(curve.apply(i / 100.0), i / 100.0);
        }
    }

    void blackAndWhiteStayFixed() {
        for (const int amount : {-100, 100}) {
            for (const ToneCurve& curve : {ToneCurve(amount, 0, 0), ToneCurve(0, amount, 0), ToneCurve(0, 0, amount),
                                           ToneCurve(amount, amount, amount)}) {
                QCOMPARE(curve.apply(0.0), 0.0);
                QCOMPARE(curve.apply(1.0), 1.0);
            }
        }
    }

    void eachSliderActsMostlyOnItsZone() {
        const auto shift = [](const ToneCurve& c, const double v) { return c.apply(v) - v; };
        const ToneCurve shadows(100, 0, 0), midtones(0, 100, 0), highlights(0, 0, 100);
        // full effect at the zone centre
        QVERIFY(std::abs(shift(shadows, 0.25) - ToneCurve::MAX_SHIFT) < 1e-9);
        QVERIFY(std::abs(shift(midtones, 0.5) - ToneCurve::MAX_SHIFT) < 1e-9);
        QVERIFY(std::abs(shift(highlights, 0.75) - ToneCurve::MAX_SHIFT) < 1e-9);
        // and much less on the far side of the range: this is not a brightness control
        QVERIFY(shift(shadows, 0.2) > 3 * std::abs(shift(shadows, 0.8)));
        QVERIFY(shift(highlights, 0.8) > 3 * std::abs(shift(highlights, 0.2)));
        QVERIFY(shift(midtones, 0.5) > 2 * shift(midtones, 0.15));
        QVERIFY(shift(midtones, 0.5) > 2 * shift(midtones, 0.85));
    }

    void negativeDarkens() {
        const ToneCurve curve(-100, -100, -100);
        for (const double v : {0.1, 0.25, 0.5, 0.75, 0.9}) {
            QVERIFY(curve.apply(v) < v);
        }
    }

    void neverInvertsTones() {
        // every combination, including opposed extremes, stays strictly increasing
        const int amounts[] = {-100, -50, 0, 50, 100};
        for (const int s : amounts) {
            for (const int m : amounts) {
                for (const int h : amounts) {
                    const ToneCurve curve(s, m, h);
                    double previous = curve.apply(0.0);
                    for (int i = 1; i <= 1000; i++) {
                        const double value = curve.apply(i / 1000.0);
                        QVERIFY2(value > previous, qPrintable(QString("s=%1 m=%2 h=%3 at %4").arg(s).arg(m).arg(h).arg(i)));
                        previous = value;
                    }
                }
            }
        }
    }

    /* ---- blur and denoise ---- */

    void zeroFiltersLeaveThePlaneUntouched() {
        std::vector<float> plane(50 * 40);
        for (size_t i = 0; i < plane.size(); i++) {
            plane[i] = static_cast<float>((i * 7919) % 1000) / 1000.0f;
        }
        const std::vector<float> before = plane;
        gaussianBlur(plane, 50, 40, 0.0);
        guidedDenoise(plane, 50, 40, 0);
        QCOMPARE(plane, before);
    }

    void blurKeepsFlatAreasAndSmoothsNoise() {
        const int w = 120, h = 80;
        std::vector<float> flat(w * h, 0.4f);
        gaussianBlur(flat, w, h, 3.0);
        for (const float v : flat) {
            QVERIFY(std::abs(v - 0.4f) < 1e-5f);
        }
        std::mt19937 rng(1);
        std::normal_distribution<float> noise(0.5f, 0.1f);
        std::vector<float> noisy(w * h);
        for (float& v : noisy) v = noise(rng);
        const double before = stddev(noisy, w, 0, w, 0, h);
        gaussianBlur(noisy, w, h, 2.0);
        QVERIFY(stddev(noisy, w, 10, w - 10, 10, h - 10) < before / 3);
    }

    void denoiseSmoothsGrainButKeepsEdges() {
        // dark left half, light right half, both grainy
        const int w = 120, h = 80, edge = 60;
        std::mt19937 rng(3);
        std::normal_distribution<float> grain(0.0f, 0.04f);
        std::vector<float> plane(w * h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                plane[y * w + x] = (x < edge ? 0.2f : 0.8f) + grain(rng);
            }
        }
        const double grainBefore = stddev(plane, w, 5, edge - 10, 5, h - 5);
        guidedDenoise(plane, w, h, 60);
        const double grainAfter = stddev(plane, w, 5, edge - 10, 5, h - 5);
        QVERIFY2(grainAfter < grainBefore / 2, qPrintable(QString("%1 -> %2").arg(grainBefore).arg(grainAfter)));
        // the pixels right next to the edge keep their side's tone instead of being blurred towards 0.5
        double left = 0.0, right = 0.0;
        for (int y = 0; y < h; y++) {
            left += plane[y * w + edge - 1];
            right += plane[y * w + edge];
        }
        QVERIFY2(left / h < 0.3 && right / h > 0.7, qPrintable(QString("%1 / %2").arg(left / h).arg(right / h)));
    }

    /* ---- wiring: neutral settings are exactly the original pipeline ---- */

    void monoNeutralIsBitIdentical() {
        ImageHashMono mono;
        const QImage image = photoLikeImage(90, 60);
        mono.setSourceImage(&image);
        const DitherImage* source = mono.getSourceImage();
        const std::vector<double> original(source->buffer, source->buffer + 90 * 60);
        mono.shadows = 40;
        mono.midtones = -30;
        mono.highlights = 20;
        mono.blur = 25;
        mono.denoise = 50;
        mono.adjustSource();
        QVERIFY(std::vector<double>(source->buffer, source->buffer + 90 * 60) != original);
        mono.shadows = mono.midtones = mono.highlights = mono.blur = mono.denoise = 0;
        mono.adjustSource();
        QCOMPARE(std::vector<double>(source->buffer, source->buffer + 90 * 60), original);
    }

    void colorNeutralIsBitIdentical() {
        ImageHashColor color;
        const QImage image = photoLikeImage(90, 60);
        color.setSourceImage(&image);
        const QImage original = color.getSourceQImage()->copy();
        color.shadows = 40;
        color.blur = 25;
        color.denoise = 50;
        color.adjustSource();
        QVERIFY(*color.getSourceQImage() != original);
        color.shadows = color.blur = color.denoise = 0;
        color.adjustSource();
        QCOMPARE(*color.getSourceQImage(), original);
    }

    void newImageStartsNeutral() {
        ImageHashMono mono;
        const QImage image = photoLikeImage(40, 30);
        mono.setSourceImage(&image);
        mono.blur = 30;
        mono.shadows = 50;
        mono.adjustSource();
        mono.setSourceImage(&image);
        QCOMPARE(mono.blur, 0);
        QCOMPARE(mono.shadows, 0);
    }

    void shadowsLightenTheDitherSource() {
        ImageHashMono mono;
        const QImage dark(10, 10, QImage::Format_ARGB32);
        QImage image = dark;
        image.fill(qRgba(64, 64, 64, 255));  // a shadow tone
        mono.setSourceImage(&image);
        const double before = mono.getSourceImage()->buffer[0];
        mono.shadows = 100;
        mono.adjustSource();
        QVERIFY(mono.getSourceImage()->buffer[0] > before);
    }
};

QObject* newTestAdjust() { return new TestAdjust; }
#include "tst_adjust.moc"
