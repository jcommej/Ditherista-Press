#include "mainwindow.h"
#include "export/filmwriter.h"
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
                                         "print as a single colour image."));

    grid->addWidget(new QLabel(tr("Mode"), separationGroup), 0, 0);
    grid->addWidget(separationModeCombo, 0, 1);
    grid->addWidget(new QLabel(tr("View"), separationGroup), 1, 0);
    grid->addWidget(separationViewCombo, 1, 1);
    grid->addWidget(new QLabel(tr("Save"), separationGroup), 2, 0);
    grid->addWidget(separationExportCombo, 2, 1);
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
    separationViewCombo->setEnabled(separating);
    separationExportCombo->setEnabled(separating);
    invalidateSeparation();
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
    for (const std::vector<float>& plane : planes) {
        const QImage source = coverageToDitherSource(plane, working);
        ImageHashMono channel;  // neutral adjustments: they were applied on the colour side
        channel.pixelsPerMm = dpi / MM_PER_INCH;
        channel.setSourceImage(&source);
        ditherMonoInto(channel);
        QImage film = *channel.getDitheredImage(current_dither_number);
        cleanExtremes(film, plane);  // clear film where there is no ink at all, solid where it is full
        films.push_back(film);
    }
    renderDpi = previousDpi;
    return films;
}

QImage MainWindow::separationView(const std::vector<QImage>& films) const {
    /* what the View selector shows: the simulated print, or one film */
    const int view = separationViewCombo->currentIndex();
    if (view <= 0 || view > static_cast<int>(films.size())) {
        return compositeFromFilms(films, separationMode);
    }
    return films[static_cast<size_t>(view - 1)];
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
    if (separationExportCombo->currentIndex() == 1) {
        *written = write(fileName, compositeFromFilms(films, separationMode)) ? 1 : 0;
        return *written == 1;
    }
    const std::vector<InkChannel> channels = channelsFor(separationMode);
    for (size_t i = 0; i < films.size(); i++) {
        const QString path = info.dir().filePath(QString("%1_%2.%3").arg(info.completeBaseName(), channels[i].name, info.suffix()));
        if (!write(path, toFilmImage(films[i]))) {
            return false;
        }
        (*written)++;
    }
    return true;
}
