#include <QtTest>
#include "palette/palettemodel.h"
#include "color/colorspace.h"

/* Tests for palette/palettemodel.h: limits, locks, randomizing, the colour to add, files and undo. */

static PaletteEntries entries(std::initializer_list<QRgb> colours) {
    PaletteEntries palette;
    for (const QRgb c : colours) palette.push_back({c, false});
    return palette;
}

class TestPalette : public QObject {
    Q_OBJECT
private slots:
    void addAndRemoveKeepTwoToTwoHundredFiftySix() {
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        QVERIFY(!PaletteModel::canRemove(p, 0));  // two colours: the minimum
        QVERIFY(!PaletteModel::remove(p, 0));
        QCOMPARE(p.size(), size_t(2));
        QVERIFY(PaletteModel::add(p, qRgba(255, 0, 0, 0)));
        QCOMPARE(p.back().colour, qRgb(255, 0, 0));  // stored opaque
        QVERIFY(PaletteModel::remove(p, 0));
        QCOMPARE(p.front().colour, qRgb(255, 255, 255));
        QVERIFY(!PaletteModel::remove(p, 5));   // out of range
        QVERIFY(!PaletteModel::remove(p, -1));
        while (PaletteModel::canAdd(p)) PaletteModel::add(p, qRgb(1, 2, 3));
        QCOMPARE(static_cast<int>(p.size()), PaletteModel::MAX_COLOURS);
        QVERIFY(!PaletteModel::add(p, qRgb(1, 2, 3)));
    }

    void lockedColoursAreNotRemovedOrRandomized() {
        PaletteEntries p = entries({qRgb(10, 20, 30), qRgb(200, 100, 50), qRgb(90, 90, 200)});
        p[1].locked = true;
        QVERIFY(!PaletteModel::remove(p, 1));
        PaletteModel::randomize(p, 7);
        QCOMPARE(p[1].colour, qRgb(200, 100, 50));
        QVERIFY(!PaletteModel::randomizeOne(p, 1, 7));
        QCOMPARE(p[1].colour, qRgb(200, 100, 50));
        QVERIFY(PaletteModel::randomizeOne(p, 0, 7));
    }

    void randomizeIsDeterministicAndStaysClose() {
        const PaletteEntries start = entries({qRgb(10, 20, 30), qRgb(200, 100, 50), qRgb(90, 90, 200), qRgb(240, 240, 10)});
        PaletteEntries a = start, b = start, c = start;
        PaletteModel::randomize(a, 1234);
        PaletteModel::randomize(b, 1234);
        PaletteModel::randomize(c, 1235);
        QVERIFY(a == b);   // same seed, same result
        QVERIFY(!(a == c));  // another seed, another variation
        QVERIFY(!(a == start));
        for (size_t i = 0; i < start.size(); i++) {  // a variation, not a new palette: lightness moves by 10 at most
            // (a* and b* move by 25 at most too, before a colour beyond sRGB is brought back into it)
            QVERIFY(std::abs(rgbToLab(a[i].colour).L - rgbToLab(start[i].colour).L) <= 10.5);
        }
        // locking a colour does not change how the others vary
        PaletteEntries locked = start;
        locked[0].locked = true;
        PaletteModel::randomize(locked, 1234);
        for (size_t i = 1; i < start.size(); i++) QCOMPARE(locked[i].colour, a[i].colour);
    }

    void leastRepresentedIsTheMissingInk() {
        // mostly black and white, plus a patch of orange the palette lacks
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        for (int y = 0; y < 50; y++) for (int x = 0; x < 100; x++) image.setPixel(x, y, qRgb(0, 0, 0));
        for (int y = 80; y < 100; y++) for (int x = 0; x < 30; x++) image.setPixel(x, y, qRgb(250, 120, 20));
        const PaletteEntries palette = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        QCOMPARE(PaletteModel::leastRepresented(image, palette), qRgb(250, 120, 20));
        QCOMPARE(PaletteModel::leastRepresented(QImage(), palette), qRgb(128, 128, 128));
    }

    void paintNetRoundTripKeepsLocks() {
        PaletteEntries p = entries({qRgb(0x3D, 0x3D, 0xDD), qRgb(0, 0, 0), qRgb(255, 255, 255)});
        p[0].locked = p[2].locked = true;
        const QString text = PaletteModel::toPaintNet(p, "test");
        QVERIFY(text.startsWith(";paint.net Palette File"));
        QVERIFY(text.contains(";Locked: 1,3"));
        QVERIFY(text.contains("FF3D3DDD"));
        PaletteEntries back;
        QString error;
        QVERIFY2(PaletteModel::fromPaintNet(text, &back, &error), qPrintable(error));
        QVERIFY(back == p);
    }

    void paintNetReadsOtherFilesAndRefusesBadOnes() {
        PaletteEntries p;
        QString error;
        // Lospec style: RRGGBB, CRLF, no header
        QVERIFY(PaletteModel::fromPaintNet("ff0000\r\n00ff00\r\n\r\n", &p, &error));
        QCOMPARE(p.size(), size_t(2));
        QCOMPARE(p[1].colour, qRgb(0, 255, 0));
        QVERIFY(!p[0].locked);
        QVERIFY(!PaletteModel::fromPaintNet(";one colour\nFF000000\n", &p, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!PaletteModel::fromPaintNet("FF000000\nFFFFFF\nnot a colour\n", &p, &error));
        QCOMPARE(p.size(), size_t(2));  // untouched on failure
        QString many;
        for (int i = 0; i < 300; i++) many += "FF102030\n";
        bool truncated = false;
        QVERIFY(PaletteModel::fromPaintNet(many, &p, &error, &truncated));
        QVERIFY(truncated);
        QCOMPARE(static_cast<int>(p.size()), PaletteModel::MAX_COLOURS);
    }

    void historyUndoesAndRedoes() {
        PaletteHistory history;
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        const PaletteEntries first = p;
        history.record(p);
        PaletteModel::add(p, qRgb(255, 0, 0));
        const PaletteEntries second = p;
        history.record(p);
        p[0].colour = qRgb(9, 9, 9);
        const PaletteEntries third = p;
        QVERIFY(history.undo(p));
        QVERIFY(p == second);
        QVERIFY(history.undo(p));
        QVERIFY(p == first);
        QVERIFY(!history.undo(p));
        QVERIFY(history.redo(p));
        QVERIFY(history.redo(p));
        QVERIFY(p == third);
        QVERIFY(!history.redo(p));
        // a new change drops the redo steps
        history.undo(p);
        history.record(p);
        QVERIFY(!history.canRedo());
    }

    void historyFloorLimitsAColourPickerSession() {
        PaletteHistory history;
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        history.record(p);  // a change made before the picker opened
        p[0].colour = qRgb(1, 1, 1);
        history.setFloor();  // picker opens on #010101
        const PaletteEntries opened = p;
        for (const int v : {0x44, 0x55, 0x66}) {
            history.record(p);
            p[0].colour = qRgb(v, v, v);
        }
        int steps = 0;
        while (history.undo(p)) steps++;
        QCOMPARE(steps, 3);  // back to where the picker opened, no further
        QVERIFY(p == opened);
        history.clearFloor();
        QVERIFY(history.undo(p));  // closed: the palette history goes on
        QCOMPARE(p[0].colour, qRgb(0, 0, 0));
    }
};

QObject* newTestPalette() { return new TestPalette; }
#include "tst_palette.moc"
