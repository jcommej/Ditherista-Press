#include "mainwindow.h"
#include "export/filmwriter.h"
#include "export/psdwriter.h"
#include <QActionGroup>
#include <QClipboard>
#include <QInputDialog>
#include <QDir>
#include <QMimeData>
#include <QScreen>
#include <QSettings>
#include <QToolButton>
#include <QUrl>

/* This file contains:
 * - the Preferences menu (between Edit and Help): navigation in the preview, screen calibration, and the
 *   Preferences window (colour management, preview quality, zoom, background, clipboard, file names, folders)
 * - the 1:1 and Fit buttons next to the zoom level
 * - File > Open Recent, Paste Image and Copy to Clipboard
 * - colour management: the working profile pictures are converted to, and embedded in colour exports
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
        return action;
    };
    menu->setToolTipsVisible(true);
    menu->addSection(tr("Navigation"))->setEnabled(false);
    // zoom mode: three choices, one checked
    QMenu* zoomModes = menu->addMenu(tr("Zoom Mode"));
    QActionGroup* zoomGroup = new QActionGroup(zoomModes);
    for (const auto& [text, mode] : std::vector<std::pair<QString, Preferences::ZoomMode>>{
             {tr("Smooth, Around the Pointer"), Preferences::ZoomMode::SmoothPointer},
             {tr("Stepped, Around the Pointer"), Preferences::ZoomMode::SteppedPointer},
             {tr("Stepped, Around the Centre (upstream Ditherista)"), Preferences::ZoomMode::SteppedCentre}}) {
        QAction* action = zoomModes->addAction(text);
        action->setCheckable(true);
        action->setActionGroup(zoomGroup);
        action->setData(static_cast<int>(mode));
        zoomModeActions.push_back(action);
        const Preferences::ZoomMode chosen = mode;
        connect(action, &QAction::triggered, this, [this, chosen]() {
            preferences.zoomMode = chosen;
            applyNavigation();
            savePreferences();
        });
    }
    invertWheelAction = option(tr("Invert Wheel Zoom"), tr("The wheel zooms the other way round, in every zoom mode."),
                               &Preferences::invertWheel);
    wheelOverFieldsAction = option(tr("Wheel Changes Values Over Fields"),
                                   tr("On: the wheel over a number, slider or list changes its value, as usual.\n"
                                      "Off: it scrolls the settings instead, so a value never changes by accident "
                                      "while scrolling."), &Preferences::wheelOverFields);
    option(tr("Drag to Pan"),
           tr("Drag with the left or the right button to move the picture.\nCtrl + drag exports the film as a file, "
              "Space shows the original.\nOff: hold the left button for the original, drag to export, as in "
              "upstream Ditherista."), &Preferences::dragPan);
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

    // the Preferences window, opened at the section asked for
    menu->addSection(tr("Settings"))->setEnabled(false);
    using Section = PreferencesDialog::Section;
    for (const auto& [text, section] : std::vector<std::pair<QString, Section>>{
             {tr("Color Management..."), Section::ColorManagement}, {tr("Preview Quality..."), Section::PreviewQuality},
             {tr("Zoom and Mouse Wheel..."), Section::Zoom}, {tr("Background..."), Section::Background},
             {tr("Clipboard..."), Section::Clipboard}, {tr("Filename Settings..."), Section::FileNames},
             {tr("Default Folders..."), Section::Folders}}) {
        const Section which = section;
        connect(menu->addAction(text), &QAction::triggered, this, [this, which]() { showPreferences(which); });
    }

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
    setupFileMenu();
}

void MainWindow::savePreferences() {
    QSettings settings = preferenceFile();
    preferences.save(settings);
}

void MainWindow::applyNavigation() {
    GraphicsView::Navigation navigation;
    navigation.zoomMode = static_cast<GraphicsView::ZoomMode>(preferences.zoomMode);
    navigation.invertWheel = preferences.invertWheel;
    navigation.dragPan = preferences.dragPan;
    navigation.middleJoystick = preferences.middleJoystick;
    navigation.inertia = preferences.inertia;
    navigation.pinchZoom = preferences.pinchZoom;
    navigation.zoomIncrement = preferences.zoomIncrement;
    ui->graphicsView->setNavigation(navigation);
    ui->graphicsView->setBackground(static_cast<GraphicsView::Background>(preferences.background), preferences.backgroundGrey);
    eventFilter.setWheelOverFields(preferences.wheelOverFields);
    // the menu follows changes made in the Preferences window
    for (QAction* action : zoomModeActions) {
        const QSignalBlocker blocker(action);
        action->setChecked(action->data().toInt() == static_cast<int>(preferences.zoomMode));
    }
    for (const auto& [action, value] : {std::pair{invertWheelAction, preferences.invertWheel},
                                        std::pair{wheelOverFieldsAction, preferences.wheelOverFields}}) {
        if (action != nullptr) {
            const QSignalBlocker blocker(action);
            action->setChecked(value);
        }
    }
}

void MainWindow::preferencesChanged(const PreferencesDialog::Change what) {
    savePreferences();
    switch (what) {
        case PreferencesDialog::Change::View:
            applyNavigation();
            break;
        case PreferencesDialog::Change::PreviewQuality:
            if (!firstLoad) {
                requestOutputSize(screenGeometry.dpi, printWidthMm, printHeightMm);  // same film, new preview
            }
            break;
        case PreferencesDialog::Change::ColorProfile:
            if (!firstLoad) {
                // the picture as read, converted again, then the preview and every result made anew
                nativeImage = toWorkingSpace(loadedImage).convertToFormat(QImage::Format_ARGB32);
                requestOutputSize(screenGeometry.dpi, printWidthMm, printHeightMm);  // resamples, re-dithers
            }
            break;
        case PreferencesDialog::Change::Other:
            break;
    }
}

double MainWindow::screenPpi() const {
    return preferences.screenPpi > 0.0 ? preferences.screenPpi : screen()->physicalDotsPerInch();
}

double MainWindow::previewDpi(const double dpi, const double widthMm, const double heightMm) const {
    return previewDpiFor(dpi, widthMm, heightMm) * preferences.previewQuality / 100.0;
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

void MainWindow::showPreferences(const PreferencesDialog::Section section) {
    if (preferencesDialog == nullptr) {
        FileNameFields example;
        example.name = sourceFileName.isEmpty() ? QStringLiteral("example") : sourceFileName;
        example.dither = "errordiff_floyd-steinberg";
        example.dpi = screenGeometry.dpi;
        example.ext = "png";
        preferencesDialog = new PreferencesDialog(&preferences, example, this);
        connect(preferencesDialog, &PreferencesDialog::changed, this, &MainWindow::preferencesChanged);
        connect(preferencesDialog, &QDialog::finished, this, [this]() {
            preferencesDialog->deleteLater();  // made again next time, with the current picture as the example
            preferencesDialog = nullptr;
        });
    }
    preferencesDialog->show();
    preferencesDialog->raise();
    preferencesDialog->activateWindow();
    preferencesDialog->showSection(section);
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

/*************************
 * COLOUR MANAGEMENT     *
 *************************/

QImage MainWindow::toWorkingSpace(const QImage& image) const {
    /* the picture's values in the working profile (see toColorSpace) */
    return toColorSpace(image, workingColorSpace(preferences.workingProfile));
}

QImage MainWindow::withProfile(QImage film) const {
    /* the working profile on a colour film, for the writers to embed; a black and white film has none */
    if (preferences.embedProfile && film.format() != QImage::Format_Mono && film.format() != QImage::Format_Grayscale8) {
        film.setColorSpace(workingColorSpace(preferences.workingProfile));
    } else {
        film.setColorSpace(QColorSpace());
    }
    return film;
}

/*************************
 * FILE MENU             *
 *************************/

void MainWindow::setupFileMenu() {
    // Open Recent, right after Open: the last files opened, most recent first
    QMenu* recent = new QMenu(tr("Open Recent"), ui->menuFile);
    const QList<QAction*> actions = ui->menuFile->actions();
    const qsizetype openIndex = actions.indexOf(ui->actionOpen);
    QAction* after = openIndex >= 0 && openIndex + 1 < actions.size() ? actions[openIndex + 1] : nullptr;
    ui->menuFile->insertMenu(after, recent);
    connect(recent, &QMenu::aboutToShow, this, [this, recent]() {
        recent->clear();
        for (const QString& path : preferences.recentFiles) {
            QAction* item = recent->addAction(QDir::toNativeSeparators(path));
            item->setEnabled(QFile::exists(path));  // moved or deleted: shown, greyed
            connect(item, &QAction::triggered, this, [this, path]() { loadImageFromFileSlot(path); });
        }
        if (preferences.recentFiles.isEmpty()) {
            recent->addAction(tr("(none)"))->setEnabled(false);
        } else {
            recent->addSeparator();
            connect(recent->addAction(tr("Clear Recent")), &QAction::triggered, this, [this]() {
                preferences.recentFiles.clear();
                savePreferences();
            });
        }
    });

    // Paste Image and Copy to Clipboard, after the save entries and before Quit
    QAction* paste = new QAction(tr("Paste Image"), ui->menuFile);
    paste->setToolTip(tr("Open the picture on the clipboard, e.g. a layer copied in Photoshop (Ctrl+V)"));
    QAction* copy = new QAction(tr("Copy to Clipboard"), ui->menuFile);
    copy->setToolTip(tr("The film at the output DPI, without saving it first (Ctrl+C). See Preferences > Clipboard"));
    const QList<QAction*> entries = ui->menuFile->actions();
    const qsizetype saveAsIndex = entries.indexOf(ui->actionSaveAs);
    QAction* beforeQuit = saveAsIndex >= 0 && saveAsIndex + 1 < entries.size() ? entries[saveAsIndex + 1] : nullptr;
    ui->menuFile->insertSeparator(beforeQuit);
    ui->menuFile->insertAction(beforeQuit, paste);
    ui->menuFile->insertAction(beforeQuit, copy);
    ui->menuFile->setToolTipsVisible(true);
    connect(paste, &QAction::triggered, this, &MainWindow::pasteSlot);
    connect(copy, &QAction::triggered, this, &MainWindow::copyToClipboard);
    connect(ui->menuFile, &QMenu::aboutToShow, this, [this, copy, paste]() {
        copy->setEnabled(!firstLoad);
        const QMimeData* clipboard = QGuiApplication::clipboard()->mimeData();
        paste->setEnabled(clipboard != nullptr && (clipboard->hasImage() || clipboard->hasUrls()));
    });
}

void MainWindow::copyToClipboard() {
    /* the film as Save would write it, onto the clipboard twice: pixels for programs that paste an image, and
     * files (with DPI and profile) for programs that paste files. The files live in a temp folder, replaced by
     * the next copy. */
    if (firstLoad || isDithering) {
        return;
    }
    renderBeforeExport();  // render control paused with changes waiting: render them first
    // separating, with Preferences > Clipboard on "ask": which channel - the print, one ink, or every ink
    enum { Print = -1, Every = -2 };
    int channel = Print;
    if (separationActive() && preferences.clipboardContent == Preferences::ClipboardContent::AskChannel) {
        const std::vector<InkChannel> inks = separationInks();
        const std::vector<ChannelSettings>& settings = currentChannelSettings();
        QStringList items{tr("Simulated print (composite)")};
        std::vector<int> indices{Print};
        for (size_t i = 0; i < inks.size() && i < settings.size(); i++) {
            if (settings[i].enabled) {
                items << tr("%1 film").arg(inks[i].name);
                indices.push_back(static_cast<int>(i));
            }
        }
        items << tr("Every ink, as files (one per ink, or one PSD)");
        indices.push_back(Every);
        bool chosen = false;
        const QString item = QInputDialog::getItem(this, tr("Copy to Clipboard"), tr("Channel to copy:"), items, 0,
                                                   false, &chosen);
        if (!chosen) {
            return;
        }
        channel = indices[static_cast<size_t>(items.indexOf(item))];
    }
    setMouseBusy(true);
    if (renderDpi < screenGeometry.dpi) {
        notification->showText(tr("rendering the film at %1 DPI...").arg(screenGeometry.dpi, 0, 'f', 0), 60000);
        QApplication::processEvents();
    }
    QDir folder(QDir::temp().filePath("ditherista-clipboard"));
    folder.removeRecursively();
    QDir().mkpath(folder.path());
    const QString format = preferences.clipboardFormat;
    const QString base = QFileInfo(suggestedFileName()).completeBaseName();
    const auto write = [&](const QString& path, const QImage& image, QString* error) {
        if (format == "tif") return writeTiff(path, image, screenGeometry.dpi, TiffCompression::PackBits, error);
        if (format == "psd") return writePsd(path, image, {}, screenGeometry.dpi, error);
        return writePng(path, image, screenGeometry.dpi, error);
    };
    QList<QUrl> files;
    QImage pixels;
    QString error;
    bool ok = true;
    if (separationActive()) {
        const std::vector<QImage> films = separationFilmsAtOutput();
        const std::vector<InkChannel> inks = separationInks();
        if (channel >= 0 && static_cast<size_t>(channel) < films.size() && !films[static_cast<size_t>(channel)].isNull()) {
            // one ink: its black and white film, as pixels and as a file
            pixels = toFilmImage(films[static_cast<size_t>(channel)]);
            const QString path = folder.filePath(QString("%1_%2.%3").arg(base, inks[static_cast<size_t>(channel)].name, format));
            ok = write(path, pixels, &error);
            files << QUrl::fromLocalFile(path);
        } else if (channel == Print && preferences.clipboardContent == Preferences::ClipboardContent::Composite) {
            pixels = withProfile(toFilmImage(separationView(films)));  // what the View selector shows
        } else {
            pixels = withProfile(toFilmImage(compositeFromFilms(films, inks, separationMode == SeparationMode::RGB)));
        }
        if (channel == Every) {
            if (format == "psd") {  // one document holding every ink, as Save writes it
                const QString path = folder.filePath(base + ".psd");
                std::vector<PsdSpotChannel> spots;
                for (size_t i = 0; i < films.size() && i < inks.size(); i++) {
                    if (!films[i].isNull()) spots.push_back({inks[i].name, inks[i].ink, films[i]});
                }
                QImage composite = withProfile(compositeFromFilms(films, inks, separationMode == SeparationMode::RGB));
                ok = writePsd(path, composite, spots, screenGeometry.dpi, &error,
                              static_cast<PsdInkLayout>(separationPsdLayoutCombo->currentData().toInt()),
                              separationMode == SeparationMode::RGB);
                files << QUrl::fromLocalFile(path);
            } else {
                for (size_t i = 0; ok && i < films.size() && i < inks.size(); i++) {
                    if (films[i].isNull()) continue;  // ink left out
                    const QString path = folder.filePath(QString("%1_%2.%3").arg(base, inks[i].name, format));
                    ok = write(path, toFilmImage(films[i]), &error);
                    files << QUrl::fromLocalFile(path);
                }
            }
        }
    } else {
        pixels = withProfile(toFilmImage(renderFilm()));
    }
    if (ok && files.isEmpty()) {  // the composite as one file
        const QString path = folder.filePath(base + "." + format);
        ok = write(path, pixels, &error);
        files << QUrl::fromLocalFile(path);
    }
    if (!ok) {
        setMouseBusy(false);
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" + tr("copy failed") + "\n" + error, 3000);
        return;
    }
    QMimeData* data = new QMimeData();
    data->setImageData(pixels.format() == QImage::Format_Mono ? pixels.convertToFormat(QImage::Format_RGB32) : pixels);
    data->setUrls(files);
    QGuiApplication::clipboard()->setMimeData(data, QClipboard::Clipboard);
    setMouseBusy(false);
    notification->showText(tr("copied: %1 × %2 px at %3 DPI\n%4 file(s) for programs that paste files")
                               .arg(pixels.width()).arg(pixels.height()).arg(screenGeometry.dpi, 0, 'f', 0).arg(files.size()), 3000);
}
