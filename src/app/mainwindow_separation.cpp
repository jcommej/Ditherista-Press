#include "mainwindow.h"
#include "consts.h"
#include <QScopeGuard>
#include <QSpinBox>
#include <cmath>
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
 * - the Color separation panel of the Color tab and the rendering of one film per ink, see screening/separation.h
 *
 * CMYK / RGB: each ink is a coverage plane dithered in black and white with the matrix of the current colour
 * ditherer - with its LPI or dot size - into its own film (ditherInkPlane).
 * Palette: the colour dither itself, one film per palette colour.
 * Both start from the picture with the Color tab's Input Image Settings applied.
 */

void MainWindow::setupSeparationControls() {
    separationGroup = new QGroupBox(tr("Color Separation"), ui->imageSettingsContainer);
    QGridLayout* grid = new QGridLayout(separationGroup);

    separationModeCombo = new QComboBox(separationGroup);
    separationModeCombo->addItem(tr("Composite (colour)"), static_cast<int>(SeparationMode::Composite));
    separationModeCombo->addItem(tr("CMYK - 4 films"), static_cast<int>(SeparationMode::CMYK));
    separationModeCombo->addItem(tr("RGB - 3 films"), static_cast<int>(SeparationMode::RGB));
    separationModeCombo->addItem(tr("Palette - 1 film per colour"), static_cast<int>(SeparationMode::Palette));
    separationModeCombo->setToolTip(tr("Composite: the colour dither, as in standard Ditherista.\n"
                                       "CMYK / RGB: one black and white film per ink, each dithered with the "
                                       "current algorithm's matrix and screen settings.\n"
                                       "Palette: the colour dither with the current palette, one film per "
                                       "palette colour; inks never overlap."));
    separationViewCombo = new QComboBox(separationGroup);
    separationViewCombo->setToolTip(tr("Preview the simulated print, or one film at a time (black = ink)."));
    separationExportCombo = new QComboBox(separationGroup);
    separationExportCombo->addItem(tr("Separate channels"));
    separationExportCombo->addItem(tr("Simulated composite"));
    separationExportCombo->addItem(tr("Separate channels + simulated print"));
    separationExportCombo->setToolTip(tr("Save: one 1-bit file per ink (picture_Cyan.tif, picture_01_E03C28.tif...), "
                                         "the simulated print as a single colour image, or both (the print as "
                                         "picture_print). The print is the one View shows: side by side or "
                                         "superposed.\nPSD holds every ink in one file, as layers, spot channels or "
                                         "both: pick it in Save As, or below."));
    separationPsdLayoutCombo = new QComboBox(separationGroup);
    separationPsdLayoutCombo->addItem(tr("Layers"), static_cast<int>(PsdInkLayout::Layers));
    separationPsdLayoutCombo->addItem(tr("Spot channels"), static_cast<int>(PsdInkLayout::SpotChannels));
    separationPsdLayoutCombo->addItem(tr("Layers + spot channels"), static_cast<int>(PsdInkLayout::LayersAndSpotChannels));
    separationPsdLayoutCombo->setToolTip(tr("How a PSD holds the inks.\n"
                                            "Layers: one layer per ink in its colour over a Paper layer (Multiply), "
                                            "or over a black Garment for RGB (Screen).\n"
                                            "Spot channels: one spot channel per ink, black = ink, printable as films."));
    separationPsdCompositeCheck = new QCheckBox(tr("PSD: include simulated print"), separationGroup);
    separationPsdCompositeCheck->setChecked(true);
    separationPsdCompositeCheck->setToolTip(tr("Spot channels only. On: the PSD's RGB image is the simulated "
                                               "print. Off: it is plain white and only the spot channels carry the "
                                               "films. With layers, the image is always the simulated print."));

    grid->addWidget(new QLabel(tr("Mode"), separationGroup), 0, 0);
    grid->addWidget(separationModeCombo, 0, 1);
    grid->addWidget(new QLabel(tr("View"), separationGroup), 1, 0);
    grid->addWidget(separationViewCombo, 1, 1);
    grid->addWidget(new QLabel(tr("Save"), separationGroup), 2, 0);
    grid->addWidget(separationExportCombo, 2, 1);
    grid->addWidget(new QLabel(tr("PSD"), separationGroup), 3, 0);
    grid->addWidget(separationPsdLayoutCombo, 3, 1);
    grid->addWidget(separationPsdCompositeCheck, 4, 0, 1, 2);
    channelRows = new QWidget(separationGroup);
    grid->addWidget(channelRows, 5, 0, 1, 2);
    grid->setColumnStretch(1, 1);
    separationGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    // just above the Screen panel, which sits right above the Input Image Settings
    const int index = ui->verticalLayout->indexOf(ui->imageSettingsStackedWidget) - 1;
    ui->verticalLayout->insertWidget(index, separationGroup, 0);
    separationGroup->setVisible(lastTabIndex == TAB_INDEX_COLOR);  // Color tab only; the app opens on Mono

    connect(separationModeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::separationModeChangedSlot);
    connect(separationViewCombo, &QComboBox::currentIndexChanged, this, &MainWindow::separationViewChangedSlot);
    separationModeChangedSlot(0);
}

bool MainWindow::separationActive() const {
    return separationMode != SeparationMode::Composite && lastTabIndex == TAB_INDEX_COLOR;
}

std::vector<QRgb> MainWindow::paletteColours() const {
    std::vector<QRgb> colours;
    if (cachedPalette != nullptr && cachedPalette->target_palette != nullptr) {
        for (size_t i = 0; i < cachedPalette->target_palette->size; i++) {
            const ByteColor* c = BytePalette_get(cachedPalette->target_palette, i);
            colours.push_back(qRgb(c->r, c->g, c->b));
        }
    }
    return colours;
}

std::vector<InkChannel> MainWindow::separationInks() const {
    return separationMode == SeparationMode::Palette ? paletteInks(paletteColours()) : channelsFor(separationMode);
}

void MainWindow::refreshSeparationInks() {
    /* view: the simulated print, then each film; and one row per ink */
    const int view = separationViewCombo->currentIndex();
    QSignalBlocker blocker(separationViewCombo);
    separationViewCombo->clear();
    if (separationMode == SeparationMode::Palette) {
        separationViewCombo->addItem(tr("Print - side by side"));
        separationViewCombo->setItemData(0, tr("Every colour as dithered, next to each other, nothing mixed"), Qt::ToolTipRole);
        separationViewCombo->addItem(tr("Print - superposed"));
        separationViewCombo->setItemData(1, tr("The passes in palette order, inks over paper (subtractive): where inks "
                                               "overprint they mix. An approximation, not a colour proof"),
                                         Qt::ToolTipRole);
    } else {
        separationViewCombo->addItem(tr("Simulated print"));
    }
    for (const InkChannel& channel : separationInks()) {
        separationViewCombo->addItem(tr("%1 film").arg(channel.name));
    }
    separationViewCombo->setCurrentIndex(view < separationViewCombo->count() ? std::max(view, 0) : 0);
    rebuildChannelRows();
    invalidateSeparation();
    updateSettingsPanelHeight();
}

void MainWindow::separationModeChangedSlot(int) {
    separationMode = static_cast<SeparationMode>(separationModeCombo->currentData().toInt());
    whileBlocking(separationViewCombo)->setCurrentIndex(0);  // other inks: start on the simulated print
    refreshSeparationInks();
    const bool separating = separationMode != SeparationMode::Composite;
    separationViewCombo->setEnabled(separating);
    separationExportCombo->setEnabled(separating);
    separationPsdCompositeCheck->setEnabled(separating);
    separationPsdLayoutCombo->setEnabled(separating);
    if (!firstLoad) {
        ui->treeWidgetColor->clearAllDitherFlags();  // the flags followed the films, not the colour dithers
        reDither(false);
    }
}

void MainWindow::separationViewChangedSlot(const int view) {
    if (separationMode == SeparationMode::Palette && view >= 0 && view < separationFilmOffset()) {
        printSuperposed = view == 1;  // the print Save and Copy write
    }
    if (!firstLoad && separationActive()) {
        if (renderPaused) {
            renderDirty = true;  // films may be stale: shown once the render control resumes
        } else {
            showSeparation();  // films are cached: switching the view does not re-dither
        }
    }
}

std::vector<QImage> MainWindow::renderSeparation(const QImage& working, const double dpi, const double upscale,
                                                 const bool preview) {
    /* one dithered film per ink for `working` (the unadjusted picture at `dpi`), with the Color tab's adjustments.
     * CMYK / RGB: the adjusted picture is split into coverage planes and each one is dithered on its own; only
     * one channel's buffers are alive at a time. Palette: the colour dither is split by palette colour. */
    const std::vector<ChannelSettings>& settings = currentChannelSettings();
    const double previousDpi = renderDpi;
    renderDpi = dpi;  // LPI cells and dot size at this resolution
    const auto restore = qScopeGuard([this, previousDpi]() {  // also when a stopped render unwinds through here
        renderDpi = previousDpi;
        renderChannel = -1;
    });
    if (separationMode == SeparationMode::Palette) {
        std::vector<bool> wanted;
        std::vector<bool> overprint;  // in palette order: the order of the passes
        for (const ChannelSettings& ink : settings) {
            wanted.push_back(ink.enabled);
            overprint.push_back(ink.overprint);
        }
        std::vector<QImage> films;
        if (preview) {  // the Composite view's own dither, cached
            if (!imageHashColor.hasDitheredImage(current_dither_number)) {
                ditherColorInto(imageHashColor);
            }
            films = splitByPalette(*imageHashColor.getDitheredImage(current_dither_number), paletteColours(), wanted);
            extendUnderFollowing(films, overprint);
        } else {
            ImageHashColor film;
            film.copyAdjustmentsFrom(imageHashColor);
            film.pixelsPerMm = dpi / MM_PER_INCH;
            film.denoiseScale = upscale;
            film.setSourceImage(&working, true);
            ditherColorInto(film);  // same palette as the preview
            films = splitByPalette(*film.getDitheredImage(current_dither_number), paletteColours(), wanted);
            extendUnderFollowing(films, overprint);
        }
        renderDpi = previousDpi;
        return films;
    }
    std::vector<std::vector<float>> planes;
    {
        ImageHashColor colour;
        colour.copyAdjustmentsFrom(imageHashColor);
        colour.pixelsPerMm = dpi / MM_PER_INCH;
        colour.denoiseScale = upscale;
        colour.setSourceImage(&working, true);
        planes = separate(*colour.getSourceQImage(), separationMode);
    }
    std::vector<QImage> films;
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
        ditherInkPlane(channel);
        QImage film = *channel.getDitheredImage(current_dither_number);
        cleanExtremes(film, plane);  // clear film where there is no ink at all, solid where it is full
        films.push_back(film);
    }
    renderChannel = -1;
    renderDpi = previousDpi;
    return films;
}

int MainWindow::separationFilmOffset() const {
    return separationMode == SeparationMode::Palette ? 2 : 1;  // the two print views, or the simulated print
}

QImage MainWindow::separationPrint(const std::vector<QImage>& films) const {
    /* the simulated print: palette inks side by side or superposed (the last chosen view), CMYK multiplied on
     * white, RGB added on black */
    if (separationMode == SeparationMode::Palette) {
        if (!printSuperposed) {
            return sideBySidePrint(films, separationInks());
        }
        std::vector<double> opacity;
        if (const auto inks = channelSettings.find(static_cast<int>(SeparationMode::Palette)); inks != channelSettings.end()) {
            for (const ChannelSettings& ink : inks->second) {
                opacity.push_back(ink.opacity);
            }
        }
        return superposedPrint(films, separationInks(), opacity);
    }
    return compositeFromFilms(films, separationInks(), separationMode == SeparationMode::RGB);
}

QImage MainWindow::separationView(const std::vector<QImage>& films) const {
    /* what the View selector shows: the simulated print, or one film */
    const int view = separationViewCombo->currentIndex();
    const int offset = separationFilmOffset();
    if (view < offset || view - offset >= static_cast<int>(films.size())) {
        const QImage print = separationPrint(films);
        if (!print.isNull()) {
            return print;
        }
    } else if (!films[static_cast<size_t>(view - offset)].isNull()) {
        return films[static_cast<size_t>(view - offset)];
    }
    QImage blank(previewImage.size(), QImage::Format_RGB32);  // every ink disabled, or this one
    blank.fill(Qt::white);
    return blank;
}

void MainWindow::showSeparation() {
    /* displays the separation, rendering the preview films first if the settings changed */
    if (separationFilmsFor != current_dither_number) {
        separationFilms = renderSeparation(previewImage, renderDpi,
                                           static_cast<double>(previewImage.width()) / nativeImage.width(), true);
        separationFilmsFor = current_dither_number;
        ui->treeWidgetColor->setCurrentItemDitherFlag(true);
    }
    const QImage shown = separationView(separationFilms);
    ui->graphicsView->setDitherImageColor(&shown, ui->treeWidgetColor->getCurrentDitherFileName());
    ui->graphicsView->showSourceColor(ui->showOriginalColor->checkState() == Qt::Checked);
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
    return renderSeparation(full, screenGeometry.dpi, static_cast<double>(film.width()) / nativeImage.width(), false);
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
    const std::vector<InkChannel> channels = separationInks();
    const bool additive = separationMode == SeparationMode::RGB;
    if (suffix == "psd") {  // one document: the print (or white) plus every ink as a spot channel
        std::vector<PsdSpotChannel> spots;
        for (size_t i = 0; i < films.size(); i++) {
            if (!films[i].isNull()) {
                spots.push_back({channels[i].name, channels[i].ink, films[i]});
            }
        }
        // the layout picked in the Save As dialog, else the one of the Separation panel
        const int chosen = fileManager.currentPsdLayout();
        if (chosen >= 0) {
            const int index = separationPsdLayoutCombo->findData(chosen);
            if (index >= 0) {
                whileBlocking(separationPsdLayoutCombo)->setCurrentIndex(index);  // the panel shows what was saved
            }
        }
        const PsdInkLayout layout = static_cast<PsdInkLayout>(separationPsdLayoutCombo->currentData().toInt());
        QImage composite = withProfile(separationPrint(films));  // spots stay profile-less
        if (layout == PsdInkLayout::SpotChannels && !separationPsdCompositeCheck->isChecked()) {
            composite.fill(Qt::white);  // with layers, the image must match what the layers show
        }
        *written = writePsd(fileName, composite, spots, screenGeometry.dpi, error, layout, additive) ? 1 : 0;
        return *written == 1;
    }
    if (separationExportCombo->currentIndex() == 1) {
        *written = write(fileName, withProfile(separationPrint(films))) ? 1 : 0;
        return *written == 1;
    }
    if (separationExportCombo->currentIndex() == 2) {  // the films, and the print as shown in View next to them
        const QString print = info.dir().filePath(QString("%1_print.%2").arg(info.completeBaseName(), info.suffix()));
        if (!write(print, withProfile(separationPrint(films)))) {
            return false;
        }
        (*written)++;
    }
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
    if (separationMode == SeparationMode::Palette) {
        // a palette of another size: every colour prints, except white, taken for the paper
        const std::vector<QRgb> palette = paletteColours();
        if (settings.size() != palette.size()) {
            settings.assign(palette.size(), ChannelSettings());
            for (size_t i = 0; i < palette.size(); i++) {
                settings[i].enabled = palette[i] != qRgb(255, 255, 255);
            }
        }
        return settings;
    }
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
    /* one row per ink: enabled, LPI (or the Screen LPI), angle. Palette inks come from a single colour dither,
     * which has one screen for all: only the enabled box, with the ink's colour. */
    delete channelRows->layout();
    for (QWidget* child : channelRows->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        delete child;
    }
    channelScreenWidgets.clear();
    const std::vector<InkChannel> inks = separationInks();
    channelRows->setVisible(!inks.empty());
    if (inks.empty()) {
        return;
    }
    const bool palette = separationMode == SeparationMode::Palette;
    QGridLayout* grid = new QGridLayout(channelRows);
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->addWidget(new QLabel(tr("Ink"), channelRows), 0, 0);
    if (palette) {
        QLabel* header = new QLabel(tr("Overprint"), channelRows);
        header->setToolTip(tr("The ink also prints under the inks that come after it in the palette, without knockout: "
                              "they overprint it, and the superposed print shows them mixing"));
        grid->addWidget(header, 0, 1, Qt::AlignRight);
        QLabel* opacityHeader = new QLabel(tr("Opacity"), channelRows);
        opacityHeader->setToolTip(tr("For the superposed print: how much the ink hides what is under it, from a "
                                     "transparent ink (a filter) to a covering one"));
        grid->addWidget(opacityHeader, 0, 2, Qt::AlignRight);
    } else {
        grid->addWidget(new QLabel(tr("LPI"), channelRows), 0, 1);
        grid->addWidget(new QLabel(tr("Angle"), channelRows), 0, 2);
    }
    std::vector<ChannelSettings>& settings = currentChannelSettings();
    const auto changed = [this]() {
        invalidateSeparation();
        if (!firstLoad) reDither(false);
    };
    for (size_t i = 0; i < inks.size(); i++) {
        const int row = static_cast<int>(i) + 1;
        QCheckBox* enabled = new QCheckBox(inks[i].name, channelRows);
        enabled->setChecked(settings[i].enabled);
        enabled->setToolTip(tr("Render and save this ink's film"));
        connect(enabled, &QCheckBox::toggled, this, [this, i, changed](const bool on) {
            currentChannelSettings()[i].enabled = on;
            changed();
        });
        if (palette) {
            QPixmap swatch(12, 12);
            swatch.fill(QColor::fromRgb(inks[i].ink));
            enabled->setIcon(QIcon(swatch));
            grid->addWidget(enabled, row, 0);
            QCheckBox* overprint = new QCheckBox(channelRows);
            overprint->setChecked(settings[i].overprint);
            overprint->setToolTip(tr("%1 also prints under the inks after it (no knockout)").arg(inks[i].name));
            grid->addWidget(overprint, row, 1, Qt::AlignRight);
            connect(overprint, &QCheckBox::toggled, this, [this, i, changed](const bool on) {
                currentChannelSettings()[i].overprint = on;
                changed();
            });
            QSpinBox* opacity = new QSpinBox(channelRows);
            opacity->setRange(0, 100);
            opacity->setSuffix(" %");
            opacity->setKeyboardTracking(false);
            opacity->setValue(static_cast<int>(std::lround(settings[i].opacity * 100.0)));
            opacity->setToolTip(tr("%1 in the superposed print: 0 % a transparent ink that tints what is under it, "
                                   "100 % a covering ink that hides it").arg(inks[i].name));
            opacity->setMinimumWidth(CHANNEL_FIELD_MIN_WIDTH);
            opacity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            grid->addWidget(opacity, row, 2);
            connect(opacity, &QSpinBox::valueChanged, this, [this, i](const int value) {
                currentChannelSettings()[i].opacity = value / 100.0;
                if (!firstLoad && separationActive()) {
                    showSeparation();  // the films stay; only the print is simulated again
                }
                scheduleHistoryCapture();
            });
            continue;
        }
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
    for (QWidget* widget : {static_cast<QWidget*>(presetGroup), static_cast<QWidget*>(separationGroup),
                            static_cast<QWidget*>(screenGroup),
                            static_cast<QWidget*>(ui->imageSettingsStackedWidget)}) {
        ui->verticalLayout->removeWidget(widget);  // presetGroup was never in it: a no-op there
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
