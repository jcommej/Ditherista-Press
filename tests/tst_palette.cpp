#include <QtTest>
#include <QColorDialog>
#include "palette/palettemodel.h"
#include "palette/colourpickerdialog.h"
#include "palette/labpanel.h"
#include "color/colorspace.h"
#include "palette/palettethemes.h"

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
        bool tooFew = false;
        QVERIFY(!PaletteModel::fromPaintNet(";one colour\nFF000000\n", &p, &error, nullptr, &tooFew));
        QVERIFY(!error.isEmpty());
        QVERIFY(tooFew);
        QCOMPARE(p.size(), size_t(2));  // untouched on failure
        // other lines are skipped, as the upstream reader did
        QVERIFY(PaletteModel::fromPaintNet("GIMP Palette\nFF000000\nFFFFFF\nnot a colour\n", &p, &error, nullptr, &tooFew));
        QVERIFY(!tooFew);
        QCOMPARE(p.size(), size_t(2));
        QString many;
        for (int i = 0; i < 300; i++) many += "FF102030\n";
        bool truncated = false;
        QVERIFY(PaletteModel::fromPaintNet(many, &p, &error, &truncated));
        QVERIFY(truncated);
        QCOMPARE(static_cast<int>(p.size()), PaletteModel::MAX_COLOURS);
    }

    void everyBuiltInPaletteReads() {
        // the palettes shipped in resources/palettes now go through this reader
        const QDir dir(QFileInfo(QString(__FILE__)).dir().filePath("../src/app/resources/palettes"));
        const QStringList files = dir.entryList({"*.pal"}, QDir::Files);
        QVERIFY(files.size() > 40);
        for (const QString& name : files) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
            const QString text = QString::fromUtf8(file.readAll());
            PaletteEntries p;
            QString error;
            QVERIFY2(PaletteModel::fromPaintNet(text, &p, &error), qPrintable(name + ": " + error));
            // as many colours as the file's colour lines
            const qsizetype lines = text.split('\n').filter(QRegularExpression("^\\s*[0-9a-fA-F]{6}([0-9a-fA-F]{2})?\\s*$")).size();
            QCOMPARE(static_cast<qsizetype>(p.size()), std::min<qsizetype>(lines, PaletteModel::MAX_COLOURS));
        }
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

    void pickerSessionUndoesOnlyItsOwnSteps() {
        PaletteHistory history;
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        history.record(p);  // a change made before the picker opened
        p[0].colour = qRgb(1, 1, 1);
        const PaletteEntries opened = p;
        history.beginSession();  // picker opens on #010101, then tries 44, 55, 66
        for (const int v : {0x44, 0x55, 0x66}) {
            history.record(p);
            p[0].colour = qRgb(v, v, v);
        }
        QVERIFY(history.undo(p));
        QCOMPARE(p[0].colour, qRgb(0x55, 0x55, 0x55));  // Ctrl+Z: the colour tried before
        int steps = 1;
        while (history.undo(p)) steps++;
        QCOMPARE(steps, 3);  // back to where the picker opened, no further
        QVERIFY(p == opened);
        QVERIFY(history.redo(p));
        QCOMPARE(p[0].colour, qRgb(0x44, 0x44, 0x44));
    }

    void pickerOkFoldsTheSessionIntoOneStep() {
        PaletteHistory history;
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        const PaletteEntries opened = p;
        history.beginSession();
        for (const int v : {0x44, 0x55, 0x66}) {
            history.record(p);
            p[0].colour = qRgb(v, v, v);
        }
        history.commitSession();
        QVERIFY(history.undo(p));  // one step back: the palette before the picker
        QVERIFY(p == opened);
        QVERIFY(!history.undo(p));
        QVERIFY(history.redo(p));
        QCOMPARE(p[0].colour, qRgb(0x66, 0x66, 0x66));
    }

    void pickerCancelPutsBackTheOpeningPalette() {
        PaletteHistory history;
        PaletteEntries p = entries({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        history.record(p);
        p[1].colour = qRgb(9, 9, 9);  // before the picker: stays undoable
        const PaletteEntries opened = p;
        history.beginSession();
        history.record(p);
        p[0].colour = qRgb(0x44, 0x44, 0x44);
        QVERIFY(history.cancelSession(p));
        QVERIFY(p == opened);
        QVERIFY(!history.canRedo());
        QVERIFY(history.undo(p));
        QCOMPARE(p[1].colour, qRgb(255, 255, 255));
        // nothing tried: nothing to put back
        history.beginSession();
        QVERIFY(!history.cancelSession(p));
    }
};

/* The colour picker: its two sides show one colour, whichever side changes it. */
class TestColourPicker : public QObject {
    Q_OBJECT
private slots:
    void settingAColourMovesBothSides() {
        ColourPickerDialog dialog;
        QSignalSpy changed(&dialog, &ColourPickerDialog::colourChanged);
        dialog.setColour(qRgb(0x3D, 0x3D, 0xDD));
        QCOMPARE(dialog.colour(), qRgb(0x3D, 0x3D, 0xDD));
        QCOMPARE(dialog.findChild<QColorDialog*>()->currentColor().rgb(), qRgb(0x3D, 0x3D, 0xDD));
        const Lab lab = dialog.findChild<LabPanel*>()->lab();
        const Lab expected = rgbToLab(qRgb(0x3D, 0x3D, 0xDD));
        QVERIFY(std::abs(lab.L - expected.L) < 1e-9 && std::abs(lab.a - expected.a) < 1e-9 && std::abs(lab.b - expected.b) < 1e-9);
        QCOMPARE(changed.count(), 0);  // set from outside: not a user change
    }

    void rgbSideMovesTheLabSide() {
        ColourPickerDialog dialog;
        dialog.setColour(qRgb(0, 0, 0));
        QSignalSpy changed(&dialog, &ColourPickerDialog::colourChanged);
        dialog.findChild<QColorDialog*>()->setCurrentColor(QColor(255, 0, 0));  // as the user would, on the RGB side
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).value<QRgb>(), qRgb(255, 0, 0));
        const Lab lab = dialog.findChild<LabPanel*>()->lab();
        QVERIFY(std::abs(lab.L - 53.2408) < 0.01 && std::abs(lab.a - 80.0925) < 0.01 && std::abs(lab.b - 67.2032) < 0.01);
    }

    void labSideMovesTheRgbSide() {
        ColourPickerDialog dialog;
        dialog.setColour(qRgb(0, 0, 0));
        QSignalSpy changed(&dialog, &ColourPickerDialog::colourChanged);
        LabPanel* panel = dialog.findChild<LabPanel*>();
        emit panel->labPicked(Lab{32.2970, 79.1875, -107.8602});  // as a drag on the a*b* plane would
        QCOMPARE(changed.count(), 1);
        QCOMPARE(dialog.colour(), qRgb(0, 0, 255));
        QCOMPARE(dialog.findChild<QColorDialog*>()->currentColor().rgb(), qRgb(0, 0, 255));
        QCOMPARE(hexColour(dialog.colour()), QString("#0000FF"));
    }

    void labSliceClampsToTheScreenGamut() {
        // the plane reports any a*b*; the panel keeps the colour inside sRGB, same hue
        LabPanel panel;
        panel.setLab({50.0, 0.0, 0.0});
        QSignalSpy picked(&panel, &LabPanel::labPicked);
        emit panel.findChild<LabSliceView*>()->abPicked(128.0, 0.0);
        QCOMPARE(picked.count(), 1);
        const Lab lab = panel.lab();
        QVERIFY(inSrgbGamut(lab));
        QCOMPARE(lab.L, 50.0);
        QVERIFY(lab.a > 50.0 && std::abs(lab.b) < 1e-9);
    }
};

/* Palette themes: settings of the existing reduction, from few colours to many (palette/palettethemes.h) */
class TestPaletteThemes : public QObject {
    Q_OBJECT
private slots:
    void themesGoFromFewToManyColours() {
        QCOMPARE(PALETTE_THEMES.size(), size_t(5));
        const int expected[] = {3, 4, 7, 11, 32};
        for (size_t i = 0; i < PALETTE_THEMES.size(); i++) {
            const PaletteTheme& theme = PALETTE_THEMES[i];
            QCOMPARE(theme.colours, expected[i]);
            QVERIFY(theme.colours >= 2 && theme.colours <= 256);  // the reduction's limits
            QVERIFY(theme.reduction >= 0 && theme.reduction <= 2);  // Median Cut, Wu, KD-Tree
            QVERIFY(!theme.keepBlackWhite || theme.colours > 2);  // black and white leave room for colours
        }
    }

    void fieldsShowTheMatchingTheme() {
        for (size_t i = 0; i < PALETTE_THEMES.size(); i++) {
            const PaletteTheme& t = PALETTE_THEMES[i];
            QCOMPARE(matchingPaletteTheme(t.colours, t.reduction, t.keepBlackWhite, false, false, false), static_cast<int>(i));
        }
        const PaletteTheme& filtre = PALETTE_THEMES[2];
        QCOMPARE(matchingPaletteTheme(filtre.colours + 1, filtre.reduction, filtre.keepBlackWhite, false, false, false), -1);
        QCOMPARE(matchingPaletteTheme(filtre.colours, filtre.reduction, filtre.keepBlackWhite, false, true, false), -1);  // + RGB
        QCOMPARE(matchingPaletteTheme(16, 0, false, false, false, false), -1);  // upstream default: Custom
    }
};

QObject* newTestPalette() { return new TestPalette; }
QObject* newTestPaletteThemes() { return new TestPaletteThemes; }
QObject* newTestColourPicker() { return new TestColourPicker; }
#include "tst_palette.moc"
