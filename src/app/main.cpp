#include "mainwindow.h"
#include <QTranslator>
#include <QApplication>

#undef main

// int main(int argc, char* argv[]) { // TODO mingw
int main(int argc, char** argv) {    
#if defined(__APPLE__) && defined(__MACH__)
    QApplication::setDesktopSettingsAware(false);
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
    QApplication a(argc, argv);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    a.setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif
    QTranslator translator;

    bool translatorLoaded = false;
    // load a translator for the current system locale
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& locale : uiLanguages) {
        const QString baseName = "application_" + QLocale(locale).name();
        if (translator.load(":/" + baseName)) {
            a.installTranslator(&translator);
            translatorLoaded = true;
            break;
        }
    }
    // if no translator found for system locale, load default translator (en_US)
    if(!translatorLoaded) {
        if(translator.load(":/application_en_US"))
            a.installTranslator(&translator);
        else
            qDebug()<<"WARNING: could not load default translator";
    }

    MainWindow w;
    w.show();
    // an image passed on the command line ("Open with..." in Explorer) is loaded once the event loop runs
    const QStringList args = QApplication::arguments();
    if (args.size() > 1) {
        QMetaObject::invokeMethod(&w, "loadImageFromFileSlot", Qt::QueuedConnection, Q_ARG(QString, args.at(1)));
    }
    return a.exec();
}
