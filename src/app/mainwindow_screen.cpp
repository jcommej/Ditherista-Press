#include "mainwindow.h"
#include "screening/matrixstretch.h"
#include "consts.h"
#include "ui_elements/signalblocker.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>

/* This file contains:
 * - the Screen settings panel: output DPI, LPI and dot size, see screening/screengeometry.h
 */

static const QList<int> DPI_PRESETS = {300, 600, 1200};

void MainWindow::setupScreenControls() {
    /* builds the Screen panel and inserts it above the Input Image Settings */
    QGroupBox* group = new QGroupBox(tr("Screen"), ui->imageSettingsContainer);
    QGridLayout* grid = new QGridLayout(group);

    dpiCombo = new QComboBox(group);
    dpiCombo->setEditable(true);
    dpiCombo->setInsertPolicy(QComboBox::NoInsert);  // typed values are used, not added to the presets
    for (const int dpi : DPI_PRESETS) {
        dpiCombo->addItem(QString::number(dpi));
    }
    dpiCombo->setValidator(new QIntValidator(static_cast<int>(SCREEN_MIN_DPI), static_cast<int>(SCREEN_MAX_DPI), dpiCombo));
    dpiCombo->setCurrentText(QString::number(static_cast<int>(SCREEN_DEFAULT_DPI)));
    dpiCombo->setToolTip(tr("Output DPI: resolution of the film. The picture is resampled to it; its print size, "
                            "cells and dots keep their physical size."));

    // print size, the physical reference; the padlock keeps width and height in proportion
    printWidthSpin = new QDoubleSpinBox(group);
    printHeightSpin = new QDoubleSpinBox(group);
    for (QDoubleSpinBox* spin : {printWidthSpin, printHeightSpin}) {
        spin->setRange(1.0, 5000.0);
        spin->setDecimals(1);
        spin->setSuffix(" mm");
        spin->setKeyboardTracking(false);
        spin->setEnabled(false);  // until an image is loaded
        spin->setToolTip(tr("Print size on film. Starts from the file's own resolution; editing it resamples the "
                            "picture."));
    }
    QWidget* sizeRow = new QWidget(group);
    QHBoxLayout* sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(2);
    aspectLockButton = new QToolButton(sizeRow);
    aspectLockButton->setCheckable(true);
    aspectLockButton->setChecked(true);
    aspectLockButton->setAutoRaise(true);
    aspectLockButton->setIconSize(QSize(14, 14));
    QIcon lockIcon;
    lockIcon.addFile(":/resources/lock_open.svg", QSize(), QIcon::Normal, QIcon::Off);
    lockIcon.addFile(":/resources/lock_closed.svg", QSize(), QIcon::Normal, QIcon::On);
    aspectLockButton->setIcon(lockIcon);
    aspectLockButton->setToolTip(tr("Keep proportions: locked, width and height change together."));
    sizeLayout->addWidget(printWidthSpin, 1);
    sizeLayout->addWidget(aspectLockButton);
    sizeLayout->addWidget(printHeightSpin, 1);

    lpiCheck = new QCheckBox(tr("LPI"), group);
    lpiCheck->setToolTip(tr("Screen frequency for ordered dithers: one matrix tile per cell of 25.4 / LPI mm."));
    lpiSpin = new QDoubleSpinBox(group);
    lpiSpin->setRange(SCREEN_MIN_LPI, SCREEN_MAX_LPI);
    lpiSpin->setDecimals(1);
    lpiSpin->setSingleStep(1.0);
    lpiSpin->setKeyboardTracking(false);  // re-dither once the value is committed, not on every keystroke
    lpiSpin->setValue(SCREEN_DEFAULT_LPI);

    dotCheck = new QCheckBox(tr("Dot size"), group);
    dotCheck->setToolTip(tr("Size of one dot on film, for algorithms without a screen cell (error diffusion, "
                            "DBS, Riemersma...). Snapped to whole device pixels."));
    dotSpin = new QDoubleSpinBox(group);
    dotSpin->setRange(SCREEN_MIN_DOT_MM, SCREEN_MAX_DOT_MM);
    dotSpin->setDecimals(3);
    dotSpin->setSingleStep(0.05);
    dotSpin->setSuffix(" mm");
    dotSpin->setKeyboardTracking(false);
    dotSpin->setValue(SCREEN_DEFAULT_DOT_MM);

    screenInfoLabel = new QLabel(group);  // short lines, no wrapping: its height must be known to the layout

    grid->addWidget(new QLabel(tr("Print size"), group), 0, 0);
    grid->addWidget(sizeRow, 0, 1);
    grid->addWidget(new QLabel(tr("Output DPI"), group), 1, 0);
    grid->addWidget(dpiCombo, 1, 1);
    grid->addWidget(lpiCheck, 2, 0);
    grid->addWidget(lpiSpin, 2, 1);
    grid->addWidget(dotCheck, 3, 0);
    grid->addWidget(dotSpin, 3, 1);
    grid->addWidget(screenInfoLabel, 4, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);  // the ditherer list gives way, not this
    const int index = ui->verticalLayout->indexOf(ui->imageSettingsStackedWidget);
    ui->verticalLayout->insertWidget(index, group, 0);

    // DPI and size resample the picture, so they apply once the value is committed - not on every keystroke,
    // where typing "1200" would resample at 120 first
    connect(dpiCombo, &QComboBox::activated, this, &MainWindow::outputDpiEditedSlot);
    connect(dpiCombo->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::outputDpiEditedSlot);
    connect(printWidthSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::printWidthEditedSlot);
    connect(printHeightSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::printHeightEditedSlot);
    connect(lpiCheck, &QCheckBox::toggled, this, &MainWindow::screenSettingsChangedSlot);
    connect(lpiSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::screenSettingsChangedSlot);
    connect(dotCheck, &QCheckBox::toggled, this, &MainWindow::screenSettingsChangedSlot);
    connect(dotSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::screenSettingsChangedSlot);
    updateScreenControls();
}

bool MainWindow::screenUsesLpi() const {
    /* true if the current algorithm has a screen cell: an ordered dither built on a regular threshold matrix.
     * Blue noise and interleaved gradient noise are ordered dithers too, but aperiodic by design. */
    if (current_dither_type != ORD && current_dither_type != ORD_C) {
        return false;
    }
    switch (current_sub_dither_type) {
        case ORD_BLU: case ORD_IGR: case ORD_BLU_C: case ORD_IGR_C: return false;
        default: return true;
    }
}

int MainWindow::screenDotPixels() const {
    /* coarse grid for the current algorithm at the resolution being rendered (preview or film): matrix
     * algorithms always dither at full resolution */
    ScreenGeometry g = screenGeometry;
    g.dpi = renderDpi;
    return screenUsesLpi() ? 1 : g.dotPixels();
}

OrderedDitherMatrix* MainWindow::applyLpi(OrderedDitherMatrix* matrix, const int width, const int height) const {
    /* stretches the matrix to the LPI cell when LPI is on; takes ownership of `matrix` */
    if (matrix == nullptr || !screenGeometry.lpiEnabled || !screenUsesLpi()) {
        return matrix;
    }
    // cell in pixels of the image being rendered: fewer in a reduced preview, same size on film
    OrderedDitherMatrix* stretched = stretchMatrixToCell(matrix, renderDpi / screenGeometry.lpi, width, height);
    OrderedDitherMatrix_free(matrix);
    return stretched;
}

/******************************************************
 * PHYSICAL SIZE AND OUTPUT DPI (see screengeometry.h) *
 ******************************************************/

QImage MainWindow::adoptNativeImage(const QImage* image) {
    /* a new picture: takes its print size from the file's resolution and starts the Output DPI at that same
     * resolution, so the returned working image is the picture itself, unresampled */
    nativeImage = image->convertToFormat(QImage::Format_ARGB32);
    const auto dpiOf = [](const int dotsPerMeter) { return dotsPerMeter > 0 ? dotsPerMeter * 0.0254 : SCREEN_DEFAULT_DPI; };
    const double fileDpiX = dpiOf(image->dotsPerMeterX());
    const double fileDpiY = image->dotsPerMeterY() > 0 ? dpiOf(image->dotsPerMeterY()) : fileDpiX;
    // files store dots per metre, so 300 DPI reads back as 299.9994: snap to the whole DPI it was saved as
    const double dpi = std::clamp(std::round(fileDpiX), SCREEN_MIN_DPI, SCREEN_MAX_DPI);
    const bool squarePixels = std::abs(fileDpiX - fileDpiY) < 0.5;
    printWidthMm = image->width() * MM_PER_INCH / (std::abs(dpi - fileDpiX) < 0.5 ? dpi : fileDpiX);
    printHeightMm = image->height() * MM_PER_INCH / (squarePixels && std::abs(dpi - fileDpiY) < 0.5 ? dpi : fileDpiY);
    screenGeometry.dpi = dpi;
    whileBlocking(dpiCombo)->setCurrentText(QString::number(static_cast<int>(dpi)));
    whileBlocking(printWidthSpin)->setValue(printWidthMm);
    whileBlocking(printHeightSpin)->setValue(printHeightMm);
    printWidthSpin->setEnabled(true);
    printHeightSpin->setEnabled(true);

    renderDpi = previewDpiFor(dpi, printWidthMm, printHeightMm);
    const QSize size(pixelsFor(printWidthMm, renderDpi), pixelsFor(printHeightMm, renderDpi));
    applyFilterScale(size, renderDpi);
    if (size == nativeImage.size()) {
        return nativeImage;
    }
    // a clamped or non-square file resolution: resample once to square pixels
    return nativeImage.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void MainWindow::applyFilterScale(const QSize& working, const double dpi) {
    /* blur is set in mm and denoise in source pixels; the preview caches need their resolution to convert */
    const double pixelsPerMm = dpi / MM_PER_INCH;
    const double upscale = static_cast<double>(working.width()) / nativeImage.width();
    imageHashMono.pixelsPerMm = imageHashColor.pixelsPerMm = pixelsPerMm;
    imageHashMono.denoiseScale = imageHashColor.denoiseScale = upscale;
}

bool MainWindow::applyOutputSize(const double dpi, const double widthMm, const double heightMm) {
    /* sets the film to widthMm x heightMm at dpi and resamples the preview for it, keeping every adjustment.
     * Returns false, changing nothing, if the film would exceed EXPORT_MAX_PIXELS */
    if (isDithering) {
        // runDitherThread keeps processing events while a ditherer runs; resampling now would free the image it
        // is writing into. User input is blocked during that time, but nothing else guarantees it.
        return false;
    }
    const QSize film(pixelsFor(widthMm, dpi), pixelsFor(heightMm, dpi));
    const long long pixels = static_cast<long long>(film.width()) * film.height();
    if (pixels > EXPORT_MAX_PIXELS) {
        notification->showText("<font color=#ec6a5e>" + tr("TOO LARGE") + "</font>\n" +
            tr("%1 × %2 px (%3 MP) at %4 DPI, max %5 MP.\nReduce the print size or the DPI.")
                .arg(film.width()).arg(film.height()).arg(pixels / 1e6, 0, 'f', 0).arg(dpi, 0, 'f', 0)
                .arg(EXPORT_MAX_PIXELS / 1'000'000), 4000);
        return false;
    }
    const double previousPreviewDpi = renderDpi;
    screenGeometry.dpi = dpi;
    printWidthMm = widthMm;
    printHeightMm = heightMm;
    renderDpi = previewDpiFor(dpi, widthMm, heightMm);  // the film itself is only rendered on export
    const QSize size(pixelsFor(widthMm, renderDpi), pixelsFor(heightMm, renderDpi));

    setMouseBusy(true);
    const QImage working = size == nativeImage.size() ? nativeImage
                                                      : nativeImage.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    applyFilterScale(size, renderDpi);
    // keep the picture the same size on screen: zoom scales inversely with the pixel count
    const int zoom = std::clamp(static_cast<int>(std::lround(ui->graphicsView->getZoomLevel() * previousPreviewDpi / renderDpi)),
                                MIN_ZOOM, MAX_ZOOM);
    ui->graphicsView->resetScene(size.width(), size.height());
    ui->resolutionLabel->setText(QString("%1 × %2").arg(size.width()).arg(size.height()));
    imageHashMono.setSourceImage(&working, true);   // true: same picture, adjustments are kept
    ui->graphicsView->setOriginalImage(working);
    previewImage = working;
    invalidateSeparation();
    imageHashColor.setSourceImage(&working, true);
    generateCachedPalette(false, false, true);
    ui->graphicsView->setSourceImageMono(imageHashMono.getSourceQImage());
    ui->graphicsView->setSourceImageColor(imageHashColor.getSourceQImage());
    ui->treeWidgetMono->clearAllDitherFlags();
    ui->treeWidgetColor->clearAllDitherFlags();
    setMouseBusy(false);
    ui->graphicsView->setZoomLevel(zoom, true);  // after setMouseBusy: the zoom field ignores updates while busy
    updateScreenControls();
    treeWidgetItemChangedSlot(activeTreeWidget->currentItem());  // re-dither at the new resolution
    return true;
}

void MainWindow::outputDpiEditedSlot() {
    /* Output DPI committed: resample, keeping the print size */
    bool ok = false;
    const int dpi = dpiCombo->currentText().toInt(&ok);
    if (!ok || dpi < SCREEN_MIN_DPI || dpi > SCREEN_MAX_DPI || dpi == screenGeometry.dpi) {
        whileBlocking(dpiCombo)->setCurrentText(QString::number(static_cast<int>(screenGeometry.dpi)));
        return;
    }
    if (firstLoad) {  // nothing to resample yet; the next image sets its own DPI anyway
        screenGeometry.dpi = dpi;
        updateScreenControls();
    } else if (!applyOutputSize(dpi, printWidthMm, printHeightMm)) {
        whileBlocking(dpiCombo)->setCurrentText(QString::number(static_cast<int>(screenGeometry.dpi)));
    }
}

void MainWindow::printWidthEditedSlot(const double widthMm) {
    // locked: keep the current proportions (which may differ from the file's if they were unlocked before)
    const double heightMm = aspectLockButton->isChecked() ? widthMm * printHeightMm / printWidthMm : printHeightMm;
    if (applyOutputSize(screenGeometry.dpi, widthMm, heightMm)) {
        whileBlocking(printHeightSpin)->setValue(heightMm);
    } else {
        whileBlocking(printWidthSpin)->setValue(printWidthMm);
    }
}

void MainWindow::printHeightEditedSlot(const double heightMm) {
    const double widthMm = aspectLockButton->isChecked() ? heightMm * printWidthMm / printHeightMm : printWidthMm;
    if (applyOutputSize(screenGeometry.dpi, widthMm, heightMm)) {
        whileBlocking(printWidthSpin)->setValue(widthMm);
    } else {
        whileBlocking(printHeightSpin)->setValue(printHeightMm);
    }
}

/**************************
 * LPI AND DOT SIZE       *
 **************************/

void MainWindow::screenSettingsChangedSlot() {
    /* user changed LPI or dot size: every cached result may be stale */
    screenGeometry.lpiEnabled = lpiCheck->isChecked();
    screenGeometry.lpi = lpiSpin->value();
    screenGeometry.dotEnabled = dotCheck->isChecked();
    screenGeometry.dotMm = dotSpin->value();
    updateScreenControls();
    invalidateSeparation();
    if (!firstLoad) {
        imageHashMono.clearAllDitheredImages();
        imageHashColor.clearAllDitheredImages();
        ui->treeWidgetMono->clearAllDitherFlags();
        ui->treeWidgetColor->clearAllDitherFlags();
        reDither(false);
    }
}

void MainWindow::updateScreenControls() {
    /* enables the LPI or dot size row depending on the current algorithm, and shows the computed geometry */
    const ScreenGeometry& g = screenGeometry;
    const bool lpiApplies = screenUsesLpi();
    lpiCheck->setEnabled(lpiApplies);
    lpiSpin->setEnabled(lpiApplies && g.lpiEnabled);
    dotCheck->setEnabled(!lpiApplies);
    dotSpin->setEnabled(!lpiApplies && g.dotEnabled);

    QStringList lines;
    if (lpiApplies && g.lpiEnabled) {
        lines << tr("%1 LPI · %2 mm / cell").arg(g.lpi, 0, 'f', 1).arg(g.cellMm(), 0, 'f', 3);
        lines << tr("%1 DPI · %2 px / cell").arg(g.dpi, 0, 'f', 0).arg(g.pixelsPerCell(), 0, 'f', 2);
    } else if (!lpiApplies && g.dotEnabled) {
        lines << tr("Dot %1 mm · %2 px at %3 DPI").arg(g.actualDotMm(), 0, 'f', 3).arg(g.dotPixels()).arg(g.dpi, 0, 'f', 0);
    } else {
        lines << tr("1 dot per image pixel");
    }
    if (!firstLoad) {
        lines << tr("Film: %1 × %2 px").arg(pixelsFor(printWidthMm, g.dpi)).arg(pixelsFor(printHeightMm, g.dpi));
        if (renderDpi < g.dpi) {  // large film: the preview is lighter, the export renders at full DPI
            lines << tr("Preview at %1 DPI, film rendered on save").arg(renderDpi, 0, 'f', 0);
        }
    }
    screenInfoLabel->setText(lines.join("\n"));
}
