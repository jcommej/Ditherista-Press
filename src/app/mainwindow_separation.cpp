#include "mainwindow.h"
#include "consts.h"
#include "export/filmwriter.h"
#include "export/psdwriter.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include "ui_elements/signalblocker.h"
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>

/* This file contains:
 * - the Color separation panel of the Mono tab and the rendering of one film per ink, see screening/separation.h
 *
 * Each ink is a coverage plane dithered by the current mono ditherer - with its LPI or dot size - into its own
 * black and white film. The planes come from the colour picture with the Mono tab's Input Image Settings
 * applied, so the sliders the user sees are the ones that act.
 */

void MainWindow::setupSeparationControls() {
    separationGroup = new QGroupBox(tr("Color Separation"), ui->imageSettingsContainer);
    QGridLayout* grid = new QGridLayout(separationGroup);

    separationModeCombo = new QComboBox(separationGroup);
    separationModeCombo->addItem(tr("Composite (grey)"), static_cast<int>(SeparationMode::Composite));
    separationModeCombo->addItem(tr("CMYK - 4 films"), static_cast<int>(SeparationMode::CMYK));
    separationModeCombo->addItem(tr("RGB - 3 films"), static_cast<int>(SeparationMode::RGB));
    separationModeCombo->setToolTip(tr("Composite: one film from the grey image, as in standard Ditherista.\n"
                                       "CMYK / RGB: one black and white film per ink, each dithered with the "
                                       "current algorithm and screen settings."));
    separationViewCombo = new QComboBox(separationGroup);
    separationViewCombo->setToolTip(tr("Preview the simulated print, or one film at a time (black = ink)."));
    separationExportCombo = new QComboBox(separationGroup);
    separationExportCombo->addItem(tr("Separate channels"));
    separationExportCombo->addItem(tr("Simulated composite"));
    separationExportCombo->setToolTip(tr("Save: one 1-bit file per ink (picture_Cyan.tif, ...), or the simulated "
                                         "print as a single colour image.\nPSD always holds every ink as a spot "
                                         "channel in one file."));
    separationPsdCompositeCheck = new QCheckBox(tr("PSD: include simulated print"), separationGroup);
    separationPsdCompositeCheck->setChecked(true);
    separationPsdCompositeCheck->setToolTip(tr("On: the PSD's RGB image is the simulated print. Off: it is plain "
                                               "white and only the spot channels carry the films."));

    grid->addWidget(new QLabel(tr("Mode"), separationGroup), 0, 0);
    grid->addWidget(separationModeCombo, 0, 1);
    grid->addWidget(new QLabel(tr("View"), separationGroup), 1, 0);
    grid->addWidget(separationViewCombo, 1, 1);
    grid->addWidget(new QLabel(tr("Save"), separationGroup), 2, 0);
    grid->addWidget(separationExportCombo, 2, 1);
    grid->addWidget(separationPsdCompositeCheck, 3, 0, 1, 2);
    channelRows = new QWidget(separationGroup);
    grid->addWidget(channelRows, 4, 0, 1, 2);
    grid->setColumnStretch(1, 1);
    separationGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // just above the Screen panel, which sits right above the Input Image Settings
    const int index = ui->verticalLayout->indexOf(ui->imageSettingsStackedWidget) - 1;
    ui->verticalLayout->insertWidget(index, separationGroup, 0);

    connect(separationModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::separationModeChangedSlot);
    connect(separationViewCombo, &QComboBox::currentIndexChanged, this, &MainWindow::separationViewChangedSlot);
    separationModeChangedSlot(0);
}

bool MainWindow::separationActive() const {
    return separationMode != SeparationMode::Composite && lastTabIndex == TAB_INDEX_MONO;
}

void MainWindow::separationModeChangedSlot(int) {
    separationMode = static_cast<SeparationMode>(separationModeCombo->currentData().toInt());
    // view: the simulated print, then each film
    QSignalBlocker blocker(separationViewCombo);
    separationViewCombo->clear();
    separationViewCombo->addItem(tr("Simulated print"));
    for (const InkChannel& channel : channelsFor(separationMode)) {
        separationViewCombo->addItem(tr("%1 film").arg(channel.name));
    }
    const bool separating = separationMode != SeparationMode::Composite;
    rebuildChannelRows();
    separationViewCombo->setEnabled(separating);
    separationExportCombo->setEnabled(separating);
    separationPsdCompositeCheck->setEnabled(separating);
    invalidateSeparation();
    updateSettingsPanelHeight();
    if (!firstLoad) {
        imageHashMono.clearAllDitheredImages();  // back to Composite must show the grey dither again
        ui->treeWidgetMono->clearAllDitherFlags();
        reDither(false);
    }
}

void MainWindow::separationViewChangedSlot(int) {
    if (!firstLoad && separationActive()) {
        showSeparation();  // films are cached: switching the view does not re-dither
    }
}

std::vector<QImage> MainWindow::renderSeparation(const QImage& working, const double dpi, const double upscale) {
    /* one dithered film per ink for `working` (the unadjusted picture at `dpi`): the Mono tab's adjustments are
     * applied to the colour picture, split into coverage planes, and each plane goes through the current mono
     * ditherer. Only one channel's buffers are alive at a time. */
    std::vector<std::vector<float>> planes;
    {
        ImageHashColor colour;
        colour.brightness = imageHashMono.brightness;
        colour.contrast = imageHashMono.contrast;
        colour.gamma = imageHashMono.gamma;
        colour.blacks = imageHashMono.blacks;
        colour.shadows = imageHashMono.shadows;
        colour.midtones = imageHashMono.midtones;
        colour.highlights = imageHashMono.highlights;
        colour.whites = imageHashMono.whites;
        colour.blur = imageHashMono.blur;
        colour.denoise = imageHashMono.denoise;
        colour.pixelsPerMm = dpi / MM_PER_INCH;
        colour.denoiseScale = upscale;
        colour.setSourceImage(&working, true);
        planes = separate(*colour.getSourceQImage(), separationMode);
    }
    const double previousDpi = renderDpi;
    renderDpi = dpi;  // LPI cells and dot size at this resolution
    std::vector<QImage> films;
    const std::vector<ChannelSettings>& settings = currentChannelSettings();
    for (size_t i = 0; i < planes.size(); i++) {
        const std::vector<float>& plane = planes[i];
        if (!settings[i].enabled) {
            films.emplace_back();  // disabled ink: no film, and not rendered at all
            continue;
        }
        renderChannel = static_cast<int>(i);  // its LPI and angle, see applyLpi
        const QImage source = coverageToDitherSource(plane, working);
        ImageHashMono channel;  // neutral adjustments: they were applied on the colour side
        channel.pixelsPerMm = dpi / MM_PER_INCH;
        channel.setSourceImage(&source);
        ditherMonoInto(channel);
        QImage film = *channel.getDitheredImage(current_dither_number);
        cleanExtremes(film, plane);  // clear film where there is no ink at all, solid where it is full
        films.push_back(film);
    }
    renderChannel = -1;
    renderDpi = previousDpi;
    return films;
}

QImage MainWindow::separationView(const std::vector<QImage>& films) const {
    /* what the View selector shows: the simulated print, or one film */
    const int view = separationViewCombo->currentIndex();
    if (view <= 0 || view > static_cast<int>(films.size())) {
        const QImage print = compositeFromFilms(films, separationMode);
        if (!print.isNull()) {
            return print;
        }
    } else if (!films[static_cast<size_t>(view - 1)].isNull()) {
        return films[static_cast<size_t>(view - 1)];
    }
    QImage blank(previewImage.size(), QImage::Format_RGB32);  // every ink disabled, or this one
    blank.fill(Qt::white);
    return blank;
}

void MainWindow::showSeparation() {
    /* displays the separation, rendering the preview films first if the settings changed */
    if (separationFilmsFor != current_dither_number) {
        separationFilms = renderSeparation(previewImage, renderDpi,
                                           static_cast<double>(previewImage.width()) / nativeImage.width());
        separationFilmsFor = current_dither_number;
        ui->treeWidgetMono->setCurrentItemDitherFlag(true);
    }
    const QImage shown = separationView(separationFilms);
    ui->graphicsView->setDitherImageMono(&shown, ui->treeWidgetMono->getCurrentDitherFileName());
    ui->graphicsView->showSourceMono(ui->showOriginalMono->checkState() == Qt::Checked);
}

std::vector<QImage> MainWindow::separationFilmsAtOutput() {
    /* the films at the output DPI: the preview's own when it already runs at that resolution */
    if (renderDpi >= screenGeometry.dpi) {
        if (separationFilmsFor != current_dither_number) {
            showSeparation();
        }
        return separationFilms;
    }
    const QSize film(pixelsFor(printWidthMm, screenGeometry.dpi), pixelsFor(printHeightMm, screenGeometry.dpi));
    const QImage full = nativeImage.scaled(film, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return renderSeparation(full, screenGeometry.dpi, static_cast<double>(film.width()) / nativeImage.width());
}

bool MainWindow::saveSeparation(const QString& fileName, QString* error, int* written) {
    /* Save in separation mode: one 1-bit file per ink next to `fileName` (picture_Cyan.tif, ...), or the
     * simulated print in `fileName` itself */
    const std::vector<QImage> films = separationFilmsAtOutput();
    const QFileInfo info(fileName);
    const QString suffix = info.suffix().toLower();
    const auto write = [&](const QString& path, const QImage& image) {
        if (suffix == "png") return writePng(path, image, screenGeometry.dpi, error);
        if (suffix == "bmp") return writeBmp(path, image, screenGeometry.dpi, error);
        const TiffCompression compression = fileManager.currentSaveFilter() == FileManager::tiffPackBitsFilter()
                                                ? TiffCompression::PackBits : TiffCompression::None;
        return writeTiff(path, image, screenGeometry.dpi, compression, error);
    };
    *written = 0;
    if (suffix == "psd") {  // one document: the print (or white) plus every ink as a spot channel
        const std::vector<InkChannel> channels = channelsFor(separationMode);
        std::vector<PsdSpotChannel> spots;
        for (size_t i = 0; i < films.size(); i++) {
            if (!films[i].isNull()) {
                spots.push_back({channels[i].name, channels[i].ink, films[i]});
            }
        }
        QImage composite = compositeFromFilms(films, separationMode);
        if (!separationPsdCompositeCheck->isChecked()) {
            composite.fill(Qt::white);
        }
        *written = writePsd(fileName, composite, spots, screenGeometry.dpi, error) ? 1 : 0;
        return *written == 1;
    }
    if (separationExportCombo->currentIndex() == 1) {
        *written = write(fileName, compositeFromFilms(films, separationMode)) ? 1 : 0;
        return *written == 1;
    }
    const std::vector<InkChannel> channels = channelsFor(separationMode);
    for (size_t i = 0; i < films.size(); i++) {
        if (films[i].isNull()) {
            continue;  // disabled ink
        }
        const QString path = info.dir().filePath(QString("%1_%2.%3").arg(info.completeBaseName(), channels[i].name, info.suffix()));
        if (!write(path, toFilmImage(films[i]))) {
            return false;
        }
        (*written)++;
    }
    return true;
}

/*******************************
 * PER-INK LPI, ANGLE, ENABLED *
 *******************************/

std::vector<MainWindow::ChannelSettings>& MainWindow::currentChannelSettings() {
    /* settings for the inks of the current mode, created with the usual screen printing angles */
    std::vector<ChannelSettings>& settings = channelSettings[static_cast<int>(separationMode)];
    if (settings.size() != channelsFor(separationMode).size()) {
        // CMYK: the classic 15 / 75 / 0 / 45 degrees, yellow - the least visible - on the moire-prone 0;
        // RGB: three angles 30 degrees apart
        const std::vector<double> angles = separationMode == SeparationMode::CMYK ? std::vector<double>{15, 75, 0, 45}
                                                                                   : std::vector<double>{15, 45, 75};
        settings.assign(angles.size(), ChannelSettings());
        for (size_t i = 0; i < angles.size(); i++) {
            settings[i].angle = angles[i];
        }
    }
    return settings;
}

const MainWindow::ChannelSettings* MainWindow::renderChannelSettings() const {
    const auto it = channelSettings.find(static_cast<int>(separationMode));
    if (renderChannel < 0 || it == channelSettings.end() || renderChannel >= static_cast<int>(it->second.size())) {
        return nullptr;
    }
    return &it->second[static_cast<size_t>(renderChannel)];
}

void MainWindow::rebuildChannelRows() {
    /* one row per ink: enabled, LPI (or the Screen LPI), angle */
    delete channelRows->layout();
    for (QWidget* child : channelRows->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        delete child;
    }
    channelScreenWidgets.clear();
    const std::vector<InkChannel> inks = channelsFor(separationMode);
    channelRows->setVisible(!inks.empty());
    if (inks.empty()) {
        return;
    }
    QGridLayout* grid = new QGridLayout(channelRows);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->addWidget(new QLabel(tr("Ink"), channelRows), 0, 0);
    grid->addWidget(new QLabel(tr("LPI"), channelRows), 0, 1);
    grid->addWidget(new QLabel(tr("Angle"), channelRows), 0, 2);
    std::vector<ChannelSettings>& settings = currentChannelSettings();
    for (size_t i = 0; i < inks.size(); i++) {
        const int row = static_cast<int>(i) + 1;
        QCheckBox* enabled = new QCheckBox(inks[i].name, channelRows);
        enabled->setChecked(settings[i].enabled);
        enabled->setToolTip(tr("Render and save this ink's film"));
        QDoubleSpinBox* lpi = new QDoubleSpinBox(channelRows);
        lpi->setRange(0.0, SCREEN_MAX_LPI);
        lpi->setDecimals(1);
        lpi->setSpecialValueText(tr("screen"));  // 0: same LPI as the Screen panel
        lpi->setKeyboardTracking(false);
        lpi->setValue(settings[i].lpi);
        lpi->setToolTip(tr("This ink's LPI; \"screen\" follows the Screen panel"));
        QDoubleSpinBox* angle = new QDoubleSpinBox(channelRows);
        angle->setRange(0.0, 179.9);
        angle->setDecimals(1);
        angle->setSuffix("\u00B0");
        angle->setKeyboardTracking(false);
        angle->setValue(settings[i].angle);
        angle->setToolTip(tr("Screen angle of this ink. Inks at different angles form a rosette instead of "
                             "printing dot on dot."));
        // the panel is narrow and scrolls vertically only: fields must not demand more than their share
        for (QDoubleSpinBox* field : {lpi, angle}) {
            field->setMinimumWidth(CHANNEL_FIELD_MIN_WIDTH);
            field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        }
        grid->addWidget(enabled, row, 0);
        grid->addWidget(lpi, row, 1);
        grid->addWidget(angle, row, 2);
        channelScreenWidgets.push_back(lpi);
        channelScreenWidgets.push_back(angle);
        const auto changed = [this]() {
            invalidateSeparation();
            if (!firstLoad) reDither(false);
        };
        connect(enabled, &QCheckBox::toggled, this, [this, i, changed](const bool on) {
            currentChannelSettings()[i].enabled = on;
            changed();
        });
        connect(lpi, &QDoubleSpinBox::valueChanged, this, [this, i, changed](const double value) {
            currentChannelSettings()[i].lpi = value;
            changed();
        });
        connect(angle, &QDoubleSpinBox::valueChanged, this, [this, i, changed](const double value) {
            currentChannelSettings()[i].angle = value;
            changed();
        });
    }
    updateChannelRowsEnabled();
}

void MainWindow::updateChannelRowsEnabled() {
    /* LPI and angle shape a matrix screen: they only act with LPI on and an ordered ditherer selected */
    const bool screened = screenGeometry.lpiEnabled && screenUsesLpi();
    for (QWidget* widget : channelScreenWidgets) {
        widget->setEnabled(screened);
    }
}

/*****************************
 * SETTINGS PANEL SCROLLING  *
 *****************************/

void MainWindow::setupSettingsScroll() {
    /* Color Separation, Screen and Input Image Settings share one scroll area below the ditherer list. The list
     * keeps a minimum height; on a small screen the settings scroll rather than squeeze it out. */
    settingsPanel = new QWidget();
    QVBoxLayout* column = new QVBoxLayout(settingsPanel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(ui->verticalLayout->spacing());
    for (QWidget* widget : {static_cast<QWidget*>(separationGroup), static_cast<QWidget*>(screenGroup),
                            static_cast<QWidget*>(ui->imageSettingsStackedWidget)}) {
        ui->verticalLayout->removeWidget(widget);
        column->addWidget(widget);
    }
    settingsScroll = new QScrollArea(ui->imageSettingsContainer);
    settingsScroll->setWidget(settingsPanel);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // wide enough for the content plus the vertical scroll bar, so nothing is cut off on the right
    ui->imageSettingsContainer->setMinimumWidth(settingsPanel->minimumSizeHint().width() +
                                                settingsScroll->verticalScrollBar()->sizeHint().width() +
                                                ui->verticalLayout->contentsMargins().left() +
                                                ui->verticalLayout->contentsMargins().right());
    ui->verticalLayout->addWidget(settingsScroll);
    // the list and the settings share the height; the settings never take more than they need
    ui->verticalLayout->setStretch(ui->verticalLayout->indexOf(ui->tabWidget), 1);
    ui->verticalLayout->setStretch(ui->verticalLayout->indexOf(settingsScroll), 1);
    ui->tabWidget->setMinimumHeight(MIN_DITHERER_LIST_HEIGHT);
    updateSettingsPanelHeight();
}

void MainWindow::updateSettingsPanelHeight() {
    /* caps the scroll area at its content's height, so any spare height goes to the ditherer list */
    if (settingsScroll == nullptr) {
        return;
    }
    settingsPanel->adjustSize();
    settingsScroll->setMaximumHeight(settingsPanel->sizeHint().height() + 2 * settingsScroll->frameWidth());
}
