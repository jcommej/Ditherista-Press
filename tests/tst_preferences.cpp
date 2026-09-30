#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QWheelEvent>
#include "preferences/preferences.h"
#include "viewport/graphicsview.h"

/* Tests for preferences/preferences.h (file names, storage) and the preview's navigation (viewport/graphicsview.h) */

static FileNameFields fields(const QString& name, const QString& ext = "png") {
    FileNameFields f;
    f.name = name;
    f.dither = "errordiff_floyd-steinberg";
    f.dpi = 300.0;
    f.ext = ext;
    return f;
}

class TestPreferences : public QObject {
    Q_OBJECT
private slots:
    void defaultTemplateNamesAfterThePicture() {
        const Preferences p;
        QCOMPARE(fileNameFromTemplate(p, fields("horse")), QString("horse_errordiff_floyd-steinberg.png"));
        QCOMPARE(fileNameFromTemplate(p, fields("horse", "tif")), QString("horse_errordiff_floyd-steinberg.tif"));
        QCOMPARE(fileNameFromTemplate(p, fields("")), QString("image_errordiff_floyd-steinberg.png"));  // pasted
    }

    void suffixCanBeTurnedOff() {
        Preferences p;
        p.suffix = "-Dither_Boy";
        QCOMPARE(fileNameFromTemplate(p, fields("example")), QString("example-Dither_Boy.png"));
        p.autoSuffix = false;
        QCOMPARE(fileNameFromTemplate(p, fields("example")), QString("example.png"));
    }

    void templateFields() {
        Preferences p;
        p.nameTemplate = "{dpi}dpi_{name}_{dither}.{ext}";
        QCOMPARE(fileNameFromTemplate(p, fields("horse", "psd")), QString("300dpi_horse_errordiff_floyd-steinberg.psd"));
        p.nameTemplate = "{name}";  // no {ext}: added, a film always has its extension
        QCOMPARE(fileNameFromTemplate(p, fields("horse")), QString("horse.png"));
        p.nameTemplate = "   ";     // empty: the default template
        QCOMPARE(fileNameFromTemplate(p, fields("horse")), QString("horse_errordiff_floyd-steinberg.png"));
    }

    void forbiddenCharactersAreReplaced() {
        Preferences p;
        p.suffix = "_a/b:c*?";
        QCOMPARE(fileNameFromTemplate(p, fields("x")), QString("x_a_b_c__.png"));
    }

    void preferencesRoundTrip() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("preferences.ini");
        Preferences written;
        written.smoothZoom = false;
        written.inertia = false;
        written.screenPpi = 108.5;
        written.autoSuffix = false;
        written.suffix = "-film";
        written.nameTemplate = "{name}{suffix}_{dpi}.{ext}";
        written.openFolder = "C:/pictures";
        written.saveFolder = "D:/films";
        {
            QSettings settings(path, QSettings::IniFormat);
            written.save(settings);
        }
        Preferences read;
        QSettings settings(path, QSettings::IniFormat);
        read.load(settings);
        QCOMPARE(read.smoothZoom, false);
        QCOMPARE(read.dragPan, true);
        QCOMPARE(read.inertia, false);
        QCOMPARE(read.screenPpi, 108.5);
        QCOMPARE(read.autoSuffix, false);
        QCOMPARE(read.suffix, QString("-film"));
        QCOMPARE(read.nameTemplate, QString("{name}{suffix}_{dpi}.{ext}"));
        QCOMPARE(read.openFolder, QString("C:/pictures"));
        QCOMPARE(read.saveFolder, QString("D:/films"));
    }

    void newSettingsRoundTrip() {
        QTemporaryDir dir;
        const QString path = dir.filePath("preferences.ini");
        Preferences written;
        written.zoomIncrement = 25;
        written.background = Preferences::Background::GraphPaperBlack;
        written.backgroundGrey = 40;
        written.previewQuality = 50;
        written.workingProfile = "adobergb";
        written.embedProfile = false;
        written.clipboardContent = Preferences::ClipboardContent::SeparateFiles;
        written.clipboardFormat = "tif";
        written.recentFiles = {"a.png", "b.png"};
        {
            QSettings settings(path, QSettings::IniFormat);
            written.save(settings);
        }
        Preferences read;
        QSettings settings(path, QSettings::IniFormat);
        read.load(settings);
        QCOMPARE(read.zoomIncrement, 25);
        QCOMPARE(read.background, Preferences::Background::GraphPaperBlack);
        QCOMPARE(read.backgroundGrey, 40);
        QCOMPARE(read.previewQuality, 50);
        QCOMPARE(read.workingProfile, QString("adobergb"));
        QCOMPARE(read.embedProfile, false);
        QCOMPARE(read.clipboardContent, Preferences::ClipboardContent::SeparateFiles);
        QCOMPARE(read.clipboardFormat, QString("tif"));
        QCOMPARE(read.recentFiles, QStringList({"a.png", "b.png"}));
    }

    void badValuesFallBackToDefaults() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("bad.ini"), QSettings::IniFormat);
        settings.setValue("view/previewQuality", 33);
        settings.setValue("clipboard/format", "jpg");  // never JPEG
        settings.setValue("navigation/zoomIncrement", 500);
        Preferences p;
        p.load(settings);
        QCOMPARE(p.previewQuality, 100);
        QCOMPARE(p.clipboardFormat, QString("png"));
        QCOMPARE(p.zoomIncrement, 100);
    }

    void recentFilesKeepTheLastFive() {
        Preferences p;
        for (const char* f : {"1", "2", "3", "4", "5", "6"}) p.addRecentFile(f);
        QCOMPARE(p.recentFiles, QStringList({"6", "5", "4", "3", "2"}));
        p.addRecentFile("4");  // opened again: moves to the top, no duplicate
        QCOMPARE(p.recentFiles, QStringList({"4", "6", "5", "3", "2"}));
    }

    void workingProfilesAreKnown() {
        QCOMPARE(workingColorSpace("srgb"), QColorSpace(QColorSpace::SRgb));
        QCOMPARE(workingColorSpace("adobergb"), QColorSpace(QColorSpace::AdobeRgb));
        QCOMPARE(workingColorSpace("displayp3"), QColorSpace(QColorSpace::DisplayP3));
        QCOMPARE(workingColorSpace("nonsense"), QColorSpace(QColorSpace::SRgb));
        QVERIFY(::workingProfiles().size() >= 5);
    }

    void srgbPicturesAreLeftUntouched() {
        // the default: an untagged picture and an sRGB one keep every pixel, bit for bit
        QImage picture(4, 1, QImage::Format_ARGB32);
        for (int x = 0; x < 4; x++) picture.setPixel(x, 0, qRgb(250, 30 * x, 7 + x));
        const QImage untagged = toColorSpace(picture, QColorSpace(QColorSpace::SRgb));
        for (int x = 0; x < 4; x++) QCOMPARE(untagged.pixel(x, 0), picture.pixel(x, 0));
        QCOMPARE(untagged.colorSpace(), QColorSpace(QColorSpace::SRgb));
        picture.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const QImage tagged = toColorSpace(picture, QColorSpace(QColorSpace::SRgb));
        for (int x = 0; x < 4; x++) QCOMPARE(tagged.pixel(x, 0), picture.pixel(x, 0));
    }

    void picturesAreConvertedToTheWorkingSpace() {
        // sRGB red is inside Adobe RGB: less red is needed there to show the same colour
        QImage red(1, 1, QImage::Format_ARGB32);
        red.fill(qRgb(255, 0, 0));
        const QImage adobe = toColorSpace(red, QColorSpace(QColorSpace::AdobeRgb));
        QCOMPARE(adobe.colorSpace(), QColorSpace(QColorSpace::AdobeRgb));
        const QRgb p = adobe.pixel(0, 0);
        QVERIFY2(qRed(p) > 200 && qRed(p) < 235 && qGreen(p) < 10 && qBlue(p) < 10,
                 qPrintable(QString("%1 %2 %3").arg(qRed(p)).arg(qGreen(p)).arg(qBlue(p))));
        // and back: an Adobe RGB picture opened with sRGB as the working space
        const QImage back = toColorSpace(adobe, QColorSpace(QColorSpace::SRgb));
        QVERIFY(std::abs(qRed(back.pixel(0, 0)) - 255) <= 1);
    }

    void missingFileGivesTheDefaults() {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("none.ini"), QSettings::IniFormat);
        Preferences p;
        p.smoothZoom = false;
        p.load(settings);
        QCOMPARE(p.smoothZoom, true);
        QCOMPARE(p.screenPpi, 0.0);
        QCOMPARE(p.nameTemplate, QString("{name}{suffix}.{ext}"));
    }
};

class TestNavigation : public QObject {
    Q_OBJECT
private slots:
    void zoomKeepsThePointUnderTheAnchor() {
        GraphicsView view;
        view.resize(400, 300);
        view.resetScene(2000, 1500);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.setZoomFactor(1.0, false);
        const QPointF anchor(300.0, 100.0);
        const QPointF before = view.mapToScene(anchor.toPoint());
        view.setZoomFactor(2.5, false, &anchor);
        const QPointF after = view.mapToScene(anchor.toPoint());
        QVERIFY2(std::abs(after.x() - before.x()) < 1.0 && std::abs(after.y() - before.y()) < 1.0,
                 qPrintable(QString("%1,%2 -> %3,%4").arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y())));
        QCOMPARE(view.getZoomLevel(), 250);
    }

    void smoothWheelZoomsInAroundThePointer() {
        GraphicsView view;
        view.resize(400, 300);
        view.resetScene(2000, 1500);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QPointF at(120.0, 80.0);
        const QPointF before = view.mapToScene(at.toPoint());
        QWheelEvent up(at, view.viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &up);
        QVERIFY(std::abs(view.zoomFactor() - 1.1) < 1e-9);  // wheel up zooms in by one notch: +10 % by default
        const QPointF after = view.mapToScene(at.toPoint());
        QVERIFY(std::abs(after.x() - before.x()) < 1.0 && std::abs(after.y() - before.y()) < 1.0);
    }

    void stepWheelWhenSmoothZoomIsOff() {
        GraphicsView view;
        GraphicsView::Navigation navigation;
        navigation.smoothZoom = false;
        view.setNavigation(navigation);
        view.resetScene(200, 200);
        QWheelEvent up(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &up);
        QCOMPARE(view.getZoomLevel(), 90);  // upstream: wheel up zooms out by 10 %
    }

    void zoomIsLimited() {
        GraphicsView view;
        view.setZoomFactor(1000.0, false);
        QCOMPARE(view.getZoomLevel(), 1600);
        view.setZoomFactor(0.0001, false);
        QCOMPARE(view.getZoomLevel(), 2);
    }

    void fitShowsTheWholePicture() {
        GraphicsView view;
        view.resize(400, 300);
        view.resetScene(4000, 1000);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.zoomToFit();
        QVERIFY(view.zoomFactor() < 0.11 && view.zoomFactor() > 0.08);  // 4000 px wide in under 400
        QTest::qWait(50);  // the scroll bars go away once the event loop has run, as in the app
        const QRectF shown = view.mapToScene(view.viewport()->rect()).boundingRect();
        QVERIFY2(shown.contains(QRectF(0, 0, 4000, 1000).adjusted(1, 1, -1, -1)),
                 qPrintable(QString("zoom %1, shown %2,%3 %4x%5, viewport %6x%7").arg(view.zoomFactor())
                                .arg(shown.x()).arg(shown.y()).arg(shown.width()).arg(shown.height())
                                .arg(view.viewport()->width()).arg(view.viewport()->height())));
    }
};

QObject* newTestPreferences() { return new TestPreferences; }
QObject* newTestNavigation() { return new TestNavigation; }
#include "tst_preferences.moc"
