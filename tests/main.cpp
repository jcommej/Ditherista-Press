#include <QApplication>
#include <QtTest>
#include <memory>

/* Runs every test class. Each writes its report to results_<Class>.txt, since QtTest's stdout is not always
 * visible from PowerShell hosts (see run_tests.ps1). Exit code = number of classes with failures. */

QObject* newTestScreening();
QObject* newTestAdjust();
QObject* newTestExport();
QObject* newTestPreview();
QObject* newTestSeparation();
QObject* newTestPsd();
QObject* newTestPresets();
QObject* newTestColour();
QObject* newTestPalette();
QObject* newTestColourPicker();
QObject* newTestPreferences();
QObject* newTestNavigation();
QObject* newTestWheelOverFields();
QObject* newTestRenderButton();

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");  // widget tests run without showing any window
    QApplication app(argc, argv);
    int failed = 0;
    for (QObject* (*make)() : {newTestScreening, newTestAdjust, newTestExport, newTestPreview, newTestSeparation, newTestPsd, newTestPresets,
                                  newTestColour, newTestPalette, newTestColourPicker,
                                  newTestPreferences, newTestNavigation, newTestWheelOverFields, newTestRenderButton}) {
        std::unique_ptr<QObject> test(make());
        const QString output = QString("results_%1.txt,txt").arg(test->metaObject()->className());
        const QStringList args = {app.arguments().first(), "-o", output};
        failed += QTest::qExec(test.get(), args) != 0 ? 1 : 0;
    }
    return failed;
}
