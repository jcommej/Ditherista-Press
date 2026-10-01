#include <QtTest>
#include <QSignalSpy>
#include "libdither.h"
#include "palette/paletteeditor.h"
#include "screening/separation.h"
#include "screening/inksimulation.h"

/* The order of the palette: the order of the films and of the print passes (overprint, side by side and
 * superposed prints), dragged in the palette editor - and never a change to the dithered picture itself */
class TestPaletteOrder : public QObject {
    Q_OBJECT

    static QImage film(const QString& rows) {  // '#' ink, '.' none; rows separated by '/'
        const QStringList lines = rows.split('/');
        QImage image(static_cast<int>(lines.first().size()), static_cast<int>(lines.size()), QImage::Format_RGB32);
        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                image.setPixel(x, y, lines[y][x] == '#' ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
            }
        }
        return image;
    }
    static bool inked(const QImage& f, int x, int y) { return qRed(f.pixel(x, y)) == 0; }
private slots:
    void overprintExtendsUnderTheFollowingInksOnly() {
        std::vector<QImage> films = {film("#.../..../...."), film(".#../..../...."), film("..#./..../...."), film("...#/..../....")};
        films[2] = QImage();  // disabled ink: extends nothing, gets nothing
        extendUnderFollowing(films, {false, true, false, false});
        QVERIFY(!inked(films[0], 1, 0) && !inked(films[0], 3, 0));  // not overprinting: knockout as before
        QVERIFY(inked(films[1], 1, 0));                              // its own pixel
        QVERIFY(inked(films[1], 3, 0));                              // under the ink after it
        QVERIFY(!inked(films[1], 0, 0));                             // not under the ink before it
        QVERIFY(films[2].isNull());
        QVERIFY(!inked(films[1], 2, 0));                             // nor under a disabled one
    }

    void sideBySideIsTheColourDitherWithoutOverprint() {
        const std::vector<InkChannel> inks = {{"01", qRgb(255, 220, 0)}, {"02", qRgb(200, 30, 30)}};
        const std::vector<QImage> films = {film("#./.."), film(".#/..")};
        const QImage print = sideBySidePrint(films, inks);
        QCOMPARE(print.pixel(0, 0), inks[0].ink | 0xFF000000u);
        QCOMPARE(print.pixel(1, 0), inks[1].ink | 0xFF000000u);
        QCOMPARE(print.pixel(0, 1), qRgb(255, 255, 255));  // paper
    }

    void reflectanceRoundTripIsExact() {
        // an ink alone gives back its own colour: the spectrum recovered from sRGB returns the same sRGB
        for (const QRgb c : {qRgb(255, 0, 0), qRgb(0, 255, 0), qRgb(0, 0, 255), qRgb(255, 255, 0), qRgb(0, 0, 0),
                             qRgb(255, 255, 255), qRgb(22, 22, 22), qRgb(216, 75, 50), qRgb(46, 95, 167), qRgb(229, 184, 58)}) {
            const InkSimulation::Spectrum r = InkSimulation::reflectanceFromSrgb(c);
            for (const double v : r) {
                QVERIFY(v > 0.0 && v < 1.0);  // a physical reflectance
            }
            const QRgb back = InkSimulation::srgbFromReflectance(r);
            QVERIFY2(std::abs(qRed(back) - qRed(c)) <= 1 && std::abs(qGreen(back) - qGreen(c)) <= 1 &&
                     std::abs(qBlue(back) - qBlue(c)) <= 1, qPrintable(QString::number(c, 16) + " -> " + QString::number(back, 16)));
        }
    }

    void superposedIsSubtractiveAndFollowsTheOrder() {
        const InkChannel yellow{"Yellow", qRgb(255, 220, 0)};
        const InkChannel red{"Red", qRgb(200, 30, 30)};
        const auto pixel = [](const std::vector<InkChannel>& inks, const std::vector<double>& opacity) {
            return superposedPrint({film("#"), film("#")}, inks, opacity).pixel(0, 0);
        };
        // one ink alone prints its own colour, whatever its opacity
        for (const double opacity : {0.0, 0.3, 1.0}) {
            const QRgb alone = superposedPrint({film("#")}, {yellow}, {opacity}).pixel(0, 0);
            QVERIFY(std::abs(qRed(alone) - 255) <= 1 && std::abs(qGreen(alone) - 220) <= 1 && qBlue(alone) <= 1);
        }
        QCOMPARE(superposedPrint({film(".")}, {yellow}, {0.3}).pixel(0, 0), qRgb(255, 255, 255));  // paper
        // transparent inks are filters: darker than either, and the order hardly matters
        const QRgb filterYR = pixel({yellow, red}, {0.0, 0.0});
        const QRgb filterRY = pixel({red, yellow}, {0.0, 0.0});
        QVERIFY(qGreen(filterYR) < 30 && qRed(filterYR) < 200);  // subtractive: no light is added
        QVERIFY(std::abs(qRed(filterYR) - qRed(filterRY)) <= 3 && std::abs(qGreen(filterYR) - qGreen(filterRY)) <= 12);
        // covering inks: the last pass shows most - the palette order changes the print
        const QRgb redOnTop = pixel({yellow, red}, {0.8, 0.8});
        const QRgb yellowOnTop = pixel({red, yellow}, {0.8, 0.8});
        QVERIFY(qGreen(yellowOnTop) > 150);   // yellow over red: mostly yellow
        QVERIFY(qGreen(redOnTop) < 60);       // red over yellow: mostly red
    }

    void printLayersStackToThePrint() {
        const std::vector<InkChannel> inks = {{"01", qRgb(255, 220, 0)}, {"02", qRgb(200, 30, 30)}};
        const std::vector<QImage> films = {film("##."), film(".##")};  // overlap in the middle pixel
        const std::vector<double> opacity = {0.3, 0.3};
        const std::vector<QImage> superposed = printLayers(films, inks, opacity, true);
        QCOMPARE(superposed.size(), size_t(2));
        // pass 1: its own colour where it prints, transparent elsewhere
        QCOMPARE(qAlpha(superposed[0].pixel(2, 0)), 0);
        QVERIFY(std::abs(qGreen(superposed[0].pixel(1, 0)) - 220) <= 1);
        // pass 2: where it overlaps, the colour of the superposed print; elsewhere the ink alone
        const QImage print = superposedPrint(films, inks, opacity);
        QCOMPARE(superposed[1].pixel(1, 0), print.pixel(1, 0) | 0xFF000000u);
        QVERIFY(superposed[1].pixel(1, 0) != superposed[0].pixel(1, 0));  // the overlap shows both inks
        QCOMPARE(qAlpha(superposed[1].pixel(0, 0)), 0);
        // the layers stacked (Normal blend: the top opaque pixel) give the superposed print everywhere
        for (int x = 0; x < 3; x++) {
            const QRgb top = qAlpha(superposed[1].pixel(x, 0)) ? superposed[1].pixel(x, 0) : superposed[0].pixel(x, 0);
            QCOMPARE(top | 0xFF000000u, print.pixel(x, 0) | 0xFF000000u);
        }
        // side by side: flat ink colours
        const std::vector<QImage> flat = printLayers(films, inks, opacity, false);
        QCOMPARE(flat[1].pixel(1, 0), inks[1].ink | 0xFF000000u);
        QCOMPARE(flat[0].pixel(1, 0), inks[0].ink | 0xFF000000u);
    }

    void paletteOrderDoesNotChangeTheDither() {
        // the same colours in two orders: the colour ditherer gives the same picture (it looks for the nearest
        // colour among all of them), only the indices differ
        const int w = 48, h = 24;
        ColorImage* image = ColorImage_new(w, h);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                ColorImage_set_rgb(image, static_cast<size_t>(y * w + x), static_cast<uint8_t>(x * 5), static_cast<uint8_t>(y * 10),
                                   static_cast<uint8_t>(255 - x * 3), 255);
            }
        }
        const std::vector<QRgb> colours = {qRgb(20, 20, 20), qRgb(220, 60, 40), qRgb(240, 200, 50), qRgb(40, 90, 170), qRgb(245, 240, 230)};
        const std::vector<size_t> order = {2, 4, 0, 3, 1};  // a reordering
        const auto dither = [&](const std::vector<QRgb>& palette) {
            BytePalette* bytes = BytePalette_new(palette.size());
            for (size_t i = 0; i < palette.size(); i++) {
                const ByteColor c = {static_cast<uint8_t>(qRed(palette[i])), static_cast<uint8_t>(qGreen(palette[i])),
                                     static_cast<uint8_t>(qBlue(palette[i])), 255};
                BytePalette_set(bytes, i, &c);
            }
            CachedPalette* cached = CachedPalette_new();
            CachedPalette_from_BytePalette(cached, bytes);
            FloatColor illuminant;
            illuminant.r = 0.95047; illuminant.g = 1.0; illuminant.b = 1.08883;
            CachedPalette_update_cache(cached, LAB94, &illuminant);
            CachedPalette_set_shift(cached, 1, 1, 1);
            ErrorDiffusionMatrix* matrix = get_floyd_steinberg_matrix();
            std::vector<int> out(static_cast<size_t>(w * h));
            error_diffusion_dither_color(image, matrix, cached, true, out.data());
            std::vector<QRgb> result;
            for (const int index : out) {
                result.push_back(palette[static_cast<size_t>(index)]);
            }
            ErrorDiffusionMatrix_free(matrix);
            CachedPalette_free(cached);
            BytePalette_free(bytes);
            return result;
        };
        std::vector<QRgb> reordered;
        for (const size_t i : order) {
            reordered.push_back(colours[i]);
        }
        QVERIFY(dither(colours) == dither(reordered));
        ColorImage_free(image);
    }

    void dragARowByItsHandle() {
        PaletteEditor editor;
        editor.resize(300, 400);
        editor.setPalette({{qRgb(22, 22, 22), false}, {qRgb(216, 75, 50), false}, {qRgb(229, 184, 58), false},
                           {qRgb(46, 95, 167), false}});
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QList<QWidget*> handles;
        for (QWidget* child : editor.findChildren<QWidget*>()) {
            if (child->toolTip().startsWith("Drag to change the order")) {
                handles.append(child);
            }
        }
        QCOMPARE(handles.size(), 4);
        std::sort(handles.begin(), handles.end(), [](QWidget* a, QWidget* b) {
            return a->mapToGlobal(QPoint()).y() < b->mapToGlobal(QPoint()).y();
        });
        QSignalSpy moved(&editor, &PaletteEditor::moveRequested);
        // the third colour (yellow) dragged above the first
        QWidget* handle = handles[2];
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, QPoint(5, 10));
        const QPoint above = handle->mapFromGlobal(handles[0]->mapToGlobal(QPoint(5, 2)));
        QMouseEvent move(QEvent::MouseMove, above, handle->mapToGlobal(above), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handle, &move);
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, above);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.at(0).at(0).toInt(), 2);
        QCOMPARE(moved.at(0).at(1).toInt(), 0);
        // to the last place
        QTest::mousePress(handles[0], Qt::LeftButton, Qt::NoModifier, QPoint(5, 10));
        const QPoint below = handles[0]->mapFromGlobal(handles[3]->mapToGlobal(QPoint(5, 20)));
        QMouseEvent move2(QEvent::MouseMove, below, handles[0]->mapToGlobal(below), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(handles[0], &move2);
        QTest::mouseRelease(handles[0], Qt::LeftButton, Qt::NoModifier, below);
        QCOMPARE(moved.count(), 2);
        QCOMPARE(moved.at(1).at(0).toInt(), 0);
        QCOMPARE(moved.at(1).at(1).toInt(), 3);
        // a click without moving changes nothing
        QTest::mouseClick(handles[1], Qt::LeftButton, Qt::NoModifier, QPoint(5, 10));
        QCOMPARE(moved.count(), 2);
    }
};

QObject* newTestPaletteOrder() { return new TestPaletteOrder; }
#include "tst_order.moc"
