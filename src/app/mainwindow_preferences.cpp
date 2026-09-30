#include "mainwindow.h"
#include <QScreen>
#include <QSettings>
#include <QToolButton>

/* This file contains:
 * - the Preferences menu (between Edit and Help): navigation in the preview, screen calibration, Filename
 *   Settings and Default Folders (see preferences/preferences.h)
 * - the 1:1 and Fit buttons next to the zoom level
 */

namespace {
QSettings preferenceFile() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "ditherista", "preferences");
}
}  // namespace

void MainWindow::setupPreferences() {
    {
        QSettings settings = preferenceFile();
        preferences.load(settings);
    }
    QMenu* menu = new QMenu(tr("Preferences"), ui->menubar);
    ui->menubar->insertMenu(ui->menuHelp->menuAction(), menu);

    // navigation: each one on or off, applied at once
    const auto option = [this, menu](const QString& text, const QString& tip, bool Preferences::* field) {
        QAction* action = menu->addAction(text);
        action->setCheckable(true);
        action->setChecked(preferences.*field);
        action->setToolTip(tip);
        connect(action, &QAction::toggled, this, [this, field](const bool on) {
            preferences.*field = on;
            applyNavigation();
            savePreferences();
        });
    };
    menu->setToolTipsVisible(true);
    QAction* navigationTitle = menu->addSection(tr("Navigation"));
    navigationTitle->setEnabled(false);
    option(tr("Smooth Zoom Around the Pointer"),
           tr("The wheel zooms continuously around the point under the pointer.\n"
              "Off: steps of 10 % around the centre, as in upstream Ditherista."), &Preferences::smoothZoom);
    option(tr("Pan with Right-Click Drag"),
           tr("Drag with the right button to move the picture. The left button keeps showing the original "
              "(hold) and exporting (drag)."), &Preferences::rightDragPan);
    option(tr("Middle-Click Joystick"),
           tr("Hold the middle button and move away from where you clicked: the view glides that way, faster "
              "the further you go. Release or Esc to stop."), &Preferences::middleJoystick);
    option(tr("Inertia"), tr("After a pan or a glide, the view carries on and slows down."), &Preferences::inertia);
    option(tr("Pinch to Zoom"), tr("Two fingers on a touch screen or a touchpad."), &Preferences::pinchZoom);

    menu->addSection(tr("Screen"))->setEnabled(false);
    QAction* calibrate = menu->addAction(tr("Calibrate Screen..."));
    calibrate->setToolTip(tr("Measure the screen with a ruler, so that Zoom 1:1 shows films at their size on paper"));
    connect(calibrate, &QAction::triggered, this, [this]() {
        CalibrationDialog dialog(preferences.screenPpi, screen()->physicalDotsPerInch(), this);
        if (dialog.exec() == QDialog::Accepted) {
            preferences.screenPpi = dialog.resetRequested() ? 0.0 : dialog.ppi();
            savePreferences();
            notification->showText(dialog.resetRequested() ? tr("screen: system value")
                                                            : tr("screen calibrated: %1 px per inch").arg(preferences.screenPpi, 0, 'f', 1), 2000);
        }
    });

    menu->addSection(tr("Files"))->setEnabled(false);
    connect(menu->addAction(tr("Filename Settings...")), &QAction::triggered, this,
            [this]() { showFilesDialog(FilesDialog::Section::FileNames); });
    connect(menu->addAction(tr("Default Folders...")), &QAction::triggered, this,
            [this]() { showFilesDialog(FilesDialog::Section::Folders); });

    // 1:1 and Fit, right after the zoom level
    const auto zoomButton = [this](const QString& text, const QString& tip) {
        QToolButton* button = new QToolButton(ui->statusBarWidget);
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        return button;
    };
    QToolButton* realSize = zoomButton(tr("1:1"), tr("Zoom 1:1: the film at its size on paper (Preferences > "
                                                     "Calibrate Screen for an exact size)"));
    QToolButton* fit = zoomButton(tr("Fit"), tr("The whole picture in the view"));
    if (QHBoxLayout* bar = ui->statusBarWidget->findChild<QHBoxLayout*>("horizontalLayout_9")) {
        const int index = bar->indexOf(ui->zoomLevelCombo) + 1;
        bar->insertWidget(index, fit);
        bar->insertWidget(index, realSize);
    }
    connect(realSize, &QToolButton::clicked, this, &MainWindow::zoomToRealSize);
    connect(fit, &QToolButton::clicked, this, [this]() {
        ui->graphicsView->zoomToFit();
        ui->graphicsView->setFocus();
    });
    applyNavigation();
}

void MainWindow::savePreferences() {
    QSettings settings = preferenceFile();
    preferences.save(settings);
}

void MainWindow::applyNavigation() {
    GraphicsView::Navigation navigation;
    navigation.smoothZoom = preferences.smoothZoom;
    navigation.rightDragPan = preferences.rightDragPan;
    navigation.middleJoystick = preferences.middleJoystick;
    navigation.inertia = preferences.inertia;
    navigation.pinchZoom = preferences.pinchZoom;
    ui->graphicsView->setNavigation(navigation);
}

double MainWindow::screenPpi() const {
    return preferences.screenPpi > 0.0 ? preferences.screenPpi : screen()->physicalDotsPerInch();
}

void MainWindow::zoomToRealSize() {
    /* one inch of film on one inch of screen: the preview holds renderDpi pixels per inch of film, the screen
     * shows screenPpi view pixels per inch */
    if (firstLoad || renderDpi <= 0.0) {
        return;
    }
    ui->graphicsView->setZoomFactor(screenPpi() / renderDpi, true);
    ui->graphicsView->setFocus();
    if (preferences.screenPpi <= 0.0) {
        notification->showText(tr("1:1 with the system's screen size\nPreferences > Calibrate Screen for an exact size"), 2500);
    }
}

void MainWindow::showFilesDialog(const FilesDialog::Section section) {
    if (filesDialog == nullptr) {
        FileNameFields example;
        example.name = sourceFileName.isEmpty() ? QStringLiteral("example") : sourceFileName;
        example.dither = "errordiff_floyd-steinberg";
        example.dpi = screenGeometry.dpi;
        example.ext = "png";
        filesDialog = new FilesDialog(&preferences, example, this);
        connect(filesDialog, &FilesDialog::changed, this, &MainWindow::savePreferences);
        connect(filesDialog, &QDialog::finished, this, [this]() {
            filesDialog->deleteLater();  // made again next time, with the current picture as the example
            filesDialog = nullptr;
        });
    }
    filesDialog->show();
    filesDialog->raise();
    filesDialog->activateWindow();
    filesDialog->focusSection(section);
}

QString MainWindow::suggestedFileName() const {
    FileNameFields fields;
    fields.name = sourceFileName;
    fields.dither = activeTreeWidget->getCurrentDitherFileName();
    if (fields.dither.endsWith("_dither")) {
        fields.dither.chop(7);  // "errordiff_floyd-steinberg_dither" -> "errordiff_floyd-steinberg"
    }
    fields.dpi = screenGeometry.dpi;
    fields.ext = fileManager.currentExtension();
    return fileNameFromTemplate(preferences, fields);
}
