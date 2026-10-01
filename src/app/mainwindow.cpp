#include "mainwindow.h"
#include "modernredux/style.h"
#include "treewidget.h"
#include "consts.h"
#include "ui_elements/svg.h"
#include "ui_elements/signalblocker.h"
#include "export/filmwriter.h"
#include "export/psdwriter.h"

#include <QClipboard>
#include <QMimeData>
#include <QtConcurrent>
#include <QImageWriter>
#include <QImageReader>
#include <QSvgRenderer>
#include <QMainWindow>
#include <QNetworkReply>
#include <QApplication>
#include <qevent.h>
#include <QPainter>
#include <QMenuBar>
#include <QIntValidator>
#include <QFileDialog>

void MainWindow::refreshUiColorDitherStatus(bool resetLab, bool updateSwatches) {
    /* refreshes all relevant UI elements when the color palette changes */
    imageHashColor.clearAllDitheredImages();
    ui->treeWidgetColor->clearAllDitherFlags();
    if (updateSwatches) {
        updatePaletteColorSwatches(cachedPalette->target_palette);
    }
    if (resetLab) {
        resetLabHCVspinBoxes();
    }
    if (separationMode == SeparationMode::Palette) {
        refreshSeparationInks();  // one film per colour of the new palette
    }
}

/*************************************************
 * CONSTRUCTOR / DESTRUCTOR / GUI INITIALIZATION *
 *************************************************/

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    /* Constructor */
    uiSetup();  // Qt GUI setup
    setupScreenControls();  // output DPI / LPI panel
    setupSeparationControls();  // CMYK / RGB films, above the Screen panel
    setupToneControls();    // shadows / midtones / highlights / blur / denoise
    setupPresetControls();  // Load / Save / Delete, at the top of the settings
    setupPaletteEditor();   // editable colour list of the Palette tab, and palette undo
    setupPreferences();     // Preferences menu, 1:1 and Fit buttons
    setupSettingsScroll();  // after every panel exists: they move into one scroll area
    // the two panels above take ~250 px from the ditherer list: open taller than the minimum when the screen allows
    resize(width(), std::min(DEFAULT_WINDOW_HEIGHT, screen()->availableGeometry().height() - 40));
    // Ditherista component setup
    fileManager.setParent(this);
    helpWindow = new HelpWindow(this);
    batchDitherDialog = new BatchDitherDialog(this);
    notification = new NotificationLabel(ui->graphicsView);
    updateCheck = new UpdateCheck(this);
    // populate widgets and set default values
    setDithererDefaults();           // default values for various ditherers
    addPredefinedPalettes();         // adds built in palettes (in resources/palettes) to the palette color combo
    populateColorComparisonCombo();  // e.g. if we use LAB94, sRGB, etc. comparison
    lastBuiltInPalette = QString(":/resources/palettes/%1.pal").arg(DEFAULT_PALETTE);
    populateDithererSelection();     // add color and mono ditherers to the list
    setApplicationDefaults();
    expandColorComparisonArea(false);
    connectSignals();
}

MainWindow::~MainWindow() {
    /* Destructor */
    CachedPalette_free(cachedPalette);
    BytePalette_free(customPalette);
    delete ui;
}

/****************************************************
 * GUI & EVENT SLOTS                                *
 * Major UI events: color/mono/palette tab changed, *
 *                  user picks a ditherer, etc.     *
 ****************************************************/

void MainWindow::editMenuAboutToShowSlot() {
    /* disable edit->paste menu item if clipboard doesn't contain something we can load */
    ui->actionPaste->setEnabled(false);
    const QClipboard* clipboard = QGuiApplication::clipboard();
    if(const QMimeData* md = clipboard->mimeData(); md->hasUrls()) {
        ui->actionPaste->setEnabled(true);
    } else if(!clipboard->image(QClipboard::Clipboard).isNull()) {
        ui->actionPaste->setEnabled(true);
    }
}

void MainWindow::tabWidgetChangedSlot(int index) {
    /* user changes to another tab: Mono, Color, Palette */
    // TODO we may not need the lastTabIndex anymore (is lastTabIndex redundant?)
    if (index < TAB_INDEX_PALETTE) {
        lastTabIndex = index; // holds dither tab was last active (color or mono)
        separationGroup->setVisible(index == TAB_INDEX_COLOR);  // separation starts from the colour picture
        updateSettingsPanelHeight();
        if (index == TAB_INDEX_MONO) {      // trigger a re-dither for the active ditherer in the tab we're switching to
            ui->imageSettingsStackedWidget->setCurrentIndex(0);
            ui->graphicsView->showSourceMono(ui->showOriginalMono->checkState() == Qt::Checked);
            activeTreeWidget = ui->treeWidgetMono;
        } else {
            ui->imageSettingsStackedWidget->setCurrentIndex(1);
            ui->graphicsView->showSourceColor(ui->showOriginalColor->checkState() == Qt::Checked);
            activeTreeWidget = ui->treeWidgetColor;
        }
        treeWidgetItemChangedSlot(activeTreeWidget->currentItem());
    } else {
        ui->ditherSettings->setCurrentIndex(0); // hide ditherer settings in palette tab
    }
}

void MainWindow::treeWidgetItemChangedSlot(QTreeWidgetItem* item) { // ignore clang-tidy, don't add const here!
    /* User clicked on a ditherer in the Mono ditherer list. This function updates the UI to reflect the current
     * displayed GUI settings for the selected ditherer, and then dithers the image accordingly.
     * NOTE: There is seemingly duplicate code here - but we allow this for the possibility that color ditherers
     *       settings may diverge from mono ditherer settings in the future.
     * */
    // TODO investigate: do we need current_dither_number/current_sub_dither_type outside this method?
    setMouseBusy(true);
    current_dither_type = static_cast<DitherType>(item->data(ITEM_DATA_DTYPE, Qt::UserRole).toInt());
    current_sub_dither_type = static_cast<SubDitherType>(item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt());
    current_dither_number = current_sub_dither_type;
    int index = ditherPage[current_dither_type]; // index of stackedwidget page with the ditherer's settings
    switch(current_dither_type) {
        case ALL: {  // Allebach mono
            bool randomize = activeTreeWidget->getValue(current_sub_dither_type, setting_randomize).toBool();
            whileBlocking(ui->ALL_randomize)->setChecked(randomize);
        } break;
        case DBS: {  // DBS mono
            int dbsIndex = activeTreeWidget->getValue(current_sub_dither_type, setting_formula).toInt();
            whileBlocking(ui->DBS_formula)->setCurrentIndex(dbsIndex);
        } break;
        case DOT: {  // Dot diffusion mono
            ui->Generic_dither_name->setText(item->text(0));
            ui->Generic_groupBox->setTitle(tr("Dot Diffusion Settings"));
        } break;
        case ERR_C: {  // Error diffusion color
            bool serpentine = activeTreeWidget->getValue(current_sub_dither_type, setting_serpentine).toBool();
            whileBlocking(ui->ERR_C_serpentine)->setChecked(serpentine);
        } break;
        case ERR: {  // Error diffusion mono
            bool serpentine = activeTreeWidget->getValue(current_sub_dither_type, setting_serpentine).toBool();
            double jitter = activeTreeWidget->getValue(current_sub_dither_type, setting_jitter).toDouble();
            whileBlocking(ui->ERR_serpentine)->setChecked(serpentine);
            whileBlocking(ui->ERR_jitter)->setValue(jitter);
        }break;
        case GRD: {  // Grid dithering mono
            bool altAlgorithm = activeTreeWidget->getValue(current_sub_dither_type, setting_alt_algorithm).toBool();
            int width = activeTreeWidget->getValue(current_sub_dither_type, setting_width).toInt();
            int height = activeTreeWidget->getValue(current_sub_dither_type, setting_height).toInt();
            int minPixels = activeTreeWidget->getValue(current_sub_dither_type, setting_min_pixels).toInt();
            whileBlocking(ui->GRD_altAlgorithm)->setChecked(altAlgorithm);
            whileBlocking(ui->GRD_width)->setValue(width);
            whileBlocking(ui->GRD_height)->setValue(height);
            whileBlocking(ui->GRD_minPixels)->setValue(minPixels);
        } break;
        case LIP: {  // Dot Lippens dithering mono
            ui->Generic_dither_name->setText(item->text(0));
            ui->Generic_groupBox->setTitle(tr("Dot Lippens Settings"));
        } break;
        case ORD_C: {  // Ordered dithering color
            switch (current_sub_dither_type) {
                case ORD_VA2_C:
                case ORD_VA4_C: {
                    index = ditherPage[current_sub_dither_type];
                    whileBlocking(ui->ORD_VAR_C_step)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_step).toInt());
                } break;
                case ORD_IGR_C: {
                    index = ditherPage[current_sub_dither_type];
                    whileBlocking(ui->ORD_IGR_C_step)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_step).toInt());
                    whileBlocking(ui->ORD_IGR_C_a)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_a).toDouble());
                    whileBlocking(ui->ORD_IGR_C_b)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_b).toDouble());
                    whileBlocking(ui->ORD_IGR_C_c)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_c).toDouble());
                } break;
                default: {
                    ui->Generic_dither_name->setText(item->text(0));
                    ui->Generic_groupBox->setTitle(tr("Ordered Dither Settings"));
                } break;
            }
        } break;
        case ORD: {  // Ordered dithering mono
            switch (current_sub_dither_type) {
                case ORD_VA2:
                case ORD_VA4: {
                    index = ditherPage[current_sub_dither_type];
                    whileBlocking(ui->ORD_VAR_jitter)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_jitter).toDouble());
                    whileBlocking(ui->ORD_VAR_step)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_step).toInt());
                } break;
                case ORD_IGR: {
                    index = ditherPage[current_sub_dither_type];
                    whileBlocking(ui->ORD_IGR_jitter)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_jitter).toDouble());
                    whileBlocking(ui->ORD_IGR_step)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_step).toInt());
                    whileBlocking(ui->ORD_IGR_a)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_a).toDouble());
                    whileBlocking(ui->ORD_IGR_b)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_b).toDouble());
                    whileBlocking(ui->ORD_IGR_c)->setValue(activeTreeWidget->getValue(current_sub_dither_type, setting_c).toDouble());
                } break;
                default: {
                    double jitter = activeTreeWidget->getValue(current_sub_dither_type, setting_jitter).toDouble();
                    whileBlocking(ui->ORD_jitter)->setValue(jitter);
                } break;
            }
        } break;
        case PAT: {  // Pattern dithering mono
            ui->Generic_dither_name->setText(item->text(0));
            ui->Generic_groupBox->setTitle(tr("Pattern Dither Settings"));
        } break;
        case RIM: {  // Riemersma dithering mono
            bool altAlgorithm = !activeTreeWidget->getValue(current_sub_dither_type, setting_use_riemersma).toBool();
            whileBlocking(ui->RIM_modRiemersma)->setChecked(altAlgorithm);
        } break;
        case THR: {  // Thresholding mono
            bool autoThreshold = activeTreeWidget->getValue(current_sub_dither_type, setting_auto_threshold).toBool();
            double threshold = activeTreeWidget->getValue(current_sub_dither_type, setting_threshold).toDouble();
            double jitter = activeTreeWidget->getValue(current_sub_dither_type, setting_jitter).toDouble();
            whileBlocking(ui->THR_autoThreshold)->setChecked(autoThreshold);
            whileBlocking(ui->THR_threshold)->setValue(threshold);
            whileBlocking(ui->THR_jitter)->setValue(jitter);
        } break;
        case VAR: {  // Variable error diffusion dithering mono
            bool serpentine = activeTreeWidget->getValue(current_sub_dither_type, setting_serpentine).toBool();
            whileBlocking(ui->VAR_serpentine)->setChecked(serpentine);
        } break;
        default: break;
    }
    ui->ditherSettings->setCurrentIndex(index); // display parameters for chosen ditherer
    activeTreeWidget->scrollToItem(activeTreeWidget->currentItem());
    updateScreenControls();  // LPI or dot size, depending on the algorithm
    setMouseBusy(false);
    reDither(false);
}

/****************************
 * TRIGGER DITHERING        *
 ****************************/

void MainWindow::reDither(const bool force) {
    /* dithers the loaded image with the currently selected ditherer. if the image already exists in the imagehash
	 * then the image is retrieved from the hash. The force parameter forces a re-dither, even if the dithered image
	 * exists in the hash. */
    if(isDithering || applyingPreset)  // a preset renders once, after setting everything
        return;
    setMouseBusy(true);
    if(force) {
        if (current_dither_number < COLOR_DITHER_START) {
            imageHashMono.clearDitheredImage(current_dither_number);
        } else {
            imageHashColor.clearDitheredImage(current_dither_number);
        }
    }
    if (current_dither_number >= COLOR_DITHER_START && separationActive()) {  // one film per ink
        if (force) {
            invalidateSeparation();
        }
        showSeparation();
    } else if (current_dither_number < COLOR_DITHER_START) { // MONO DITHERING
        if(!imageHashMono.hasDitheredImage(current_dither_number)) {  // if dithered image isn't cached, then (re)compute it
            ditherMonoInto(imageHashMono);
            ui->treeWidgetMono->setCurrentItemDitherFlag(true);
        }
        setDitherImageMono(); // also applies custom light/dark colors
        ui->graphicsView->showSourceMono(ui->showOriginalMono->checkState() == Qt::Checked); // is show original checked?
    } else {  // COLOR DITHERING
        if(!imageHashColor.hasDitheredImage(current_dither_number)) {  // if dithered image isn't cached, then (re)compute it
            ditherColorInto(imageHashColor);
            ui->treeWidgetColor->setCurrentItemDitherFlag(true);
        }
        ui->graphicsView->setDitherImageColor(imageHashColor.getDitheredImage(current_dither_number), ui->treeWidgetColor->getCurrentDitherFileName());
        ui->graphicsView->showSourceColor(ui->showOriginalColor->checkState() == Qt::Checked); // is show original checked?
    }
    setMouseBusy(false);
}

void MainWindow::ditherMonoInto(ImageHashMono& hash) {
    /* runs the current mono ditherer on `hash` - the on-screen preview, or a full-resolution film for export -
     * and stores the result in it. The ditherers read their source through monoTarget. */
    monoTarget = &hash;
    hash.setCellSize(screenDotPixels());
    const DitherImage* ditherSource = hash.getDitherSourceImage();  // coarse grid when dot size is on
    uint8_t *out_buf = static_cast<uint8_t *>(calloc(static_cast<size_t>(ditherSource->width) * ditherSource->height, sizeof(uint8_t)));
    switch (current_dither_type) {
        case ALL: ALL_dither(out_buf); break;
        case GRD: GRD_dither(out_buf); break;
        case DBS: DBS_dither(out_buf); break;
        case THR: THR_dither(out_buf); break;
        case DOT: DOT_dither(out_buf, current_sub_dither_type); break;
        case ERR: ERR_dither(out_buf, current_sub_dither_type); break;
        case LIP: LIP_dither(out_buf, current_sub_dither_type); break;
        case ORD: ORD_dither(out_buf, current_sub_dither_type); break;
        case PAT: PAT_dither(out_buf, current_sub_dither_type); break;
        case RIM: RIM_dither(out_buf, current_sub_dither_type); break;
        case VAR: VAR_dither(out_buf, current_sub_dither_type); break;
        default: break;
    }
    hash.setImageFromDither(current_dither_number, out_buf);
    free(out_buf);
    monoTarget = &imageHashMono;
}

void MainWindow::ditherColorInto(ImageHashColor& hash) {
    /* colour counterpart of ditherMonoInto */
    colorTarget = &hash;
    hash.setCellSize(screenDotPixels());
    const ColorImage* ditherSource = hash.getDitherSourceImage();  // coarse grid when dot size is on
    int* out_buf = static_cast<int*>(calloc(static_cast<size_t>(ditherSource->width) * ditherSource->height, sizeof(int)));
    switch (current_dither_type) {
        case ERR_C: ERR_C_dither(out_buf, current_sub_dither_type); break;
        case ORD_C: ORD_C_dither(out_buf, current_sub_dither_type); break;
        default: break;
    }
    hash.setImageFromDither(current_dither_number, cachedPalette->target_palette, out_buf);
    free(out_buf);
    colorTarget = &imageHashColor;
}

/****************************
 * IMAGE LOADING & SAVING   *
 ****************************/

QImage MainWindow::renderFilm() {
    /* the dithered image at the output DPI. When the preview already runs at that DPI it is the film; otherwise
     * (large films, see screengeometry.h) the current settings are rendered again at full resolution, into
     * caches that live only for this call so their memory is returned as soon as the file is written */
    const bool mono = lastTabIndex == TAB_INDEX_MONO;
    if (separationActive()) {  // the simulated print or the film shown in the View selector
        return separationView(separationFilmsAtOutput());
    }
    if (renderDpi >= screenGeometry.dpi) {
        return mono ? *imageHashMono.getDitheredImage(current_dither_number)
                    : *imageHashColor.getDitheredImage(current_dither_number);
    }
    const QSize film(pixelsFor(printWidthMm, screenGeometry.dpi), pixelsFor(printHeightMm, screenGeometry.dpi));
    QImage full = nativeImage.scaled(film, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const double previewDpi = renderDpi;
    renderDpi = screenGeometry.dpi;  // LPI cells, dot size and blur are converted at the film's resolution
    const double pixelsPerMm = screenGeometry.dpi / MM_PER_INCH;
    const double upscale = static_cast<double>(film.width()) / nativeImage.width();
    QImage result;
    if (mono) {
        ImageHashMono hash;
        hash.copyAdjustmentsFrom(imageHashMono);
        hash.pixelsPerMm = pixelsPerMm;
        hash.denoiseScale = upscale;
        hash.setSourceImage(&full, true);
        full = QImage();  // the cache holds its own copy
        ditherMonoInto(hash);
        result = *hash.getDitheredImage(current_dither_number);
    } else {
        ImageHashColor hash;
        hash.copyAdjustmentsFrom(imageHashColor);
        hash.pixelsPerMm = pixelsPerMm;
        hash.denoiseScale = upscale;
        hash.setSourceImage(&full, true);
        full = QImage();
        ditherColorInto(hash);  // same palette as the preview
        result = *hash.getDitheredImage(current_dither_number);
    }
    renderDpi = previewDpi;
    return result;
}

void MainWindow::saveFile(const QString &fileName) {
    /* saves the film at the output DPI, losslessly, with the DPI in the file (see export/filmwriter.h) */
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix != "png" && suffix != "tif" && suffix != "tiff" && suffix != "bmp" && suffix != "psd") {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
            tr("save as .png, .tif, .bmp or .psd"), 3000);
        return;
    }
    setMouseBusy(true);
    if (renderDpi < screenGeometry.dpi) {
        notification->showText(tr("rendering the film at %1 DPI...").arg(screenGeometry.dpi, 0, 'f', 0), 60000);
        QApplication::processEvents();
    }
    if (separationActive()) {
        QString error;
        int written = 0;
        const bool ok = saveSeparation(fileName, &error, &written);
        setMouseBusy(false);
        if (ok) {
            notification->showText(tr("%1 file(s) saved at %2 DPI").arg(written).arg(screenGeometry.dpi, 0, 'f', 0), 3000);
        } else {
            notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                tr("failed to save file") + "\n" + error, 3000);
        }
        return;
    }
    // 1-bit when the result is pure black and white; a colour film carries the working profile
    const QImage film = withProfile(toFilmImage(renderFilm()));
    QString error;
    bool ok;
    if (suffix == "png") {
        ok = writePng(fileName, film, screenGeometry.dpi, &error);
    } else if (suffix == "bmp") {
        ok = writeBmp(fileName, film, screenGeometry.dpi, &error);
    } else if (suffix == "psd") {
        ok = writePsd(fileName, film, {}, screenGeometry.dpi, &error);  // Grayscale film, or RGB for colour
    } else {
        const TiffCompression compression = fileManager.currentSaveFilter() == FileManager::tiffPackBitsFilter()
                                                ? TiffCompression::PackBits : TiffCompression::None;
        ok = writeTiff(fileName, film, screenGeometry.dpi, compression, &error);
    }
    setMouseBusy(false);
    if (ok) {
        notification->showText(tr("film saved: %1 × %2 px, %3 DPI, %4\n%5")
                                   .arg(film.width()).arg(film.height()).arg(screenGeometry.dpi, 0, 'f', 0)
                                   .arg(film.format() == QImage::Format_Mono ? tr("1-bit") : tr("colour"))
                                   .arg(fileName), 3000);
    } else {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
            tr("failed to save file") + "\n" + error, 3000);
    }
}

void MainWindow::loadImageFromFileSlot(const QString &fileName) {
    /* loads the given image and dithers it */
    setMouseBusy(true);
    QImageReader reader;
    reader.setFileName(fileName);
    if(reader.canRead()) {
        const QImage image = reader.read();
        if (reader.error() == 0) {
            currentDirectory = QFileInfo(fileName).absolutePath();
            if (fileManager.isDefaultDirectory()) {
                // if there's no previous dir to remember, we'll use the source image's directory
                fileManager.setDirectory(currentDirectory);
            }
            fileManager.clearCurrentFileName();
            sourceFileName = QFileInfo(fileName).completeBaseName();  // {name} in the Filename Settings
            preferences.addRecentFile(QFileInfo(fileName).absoluteFilePath());  // File > Open Recent
            savePreferences();
            loadImage(&image);
            setMouseBusy(false);
            return;
        }
    }
    setMouseBusy(false);
    notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
        tr("failed to load image"), 2000);
}

void MainWindow::loadImage(const QImage* image) {
    /* called after an image has been loaded / copy-pasted / dragged into the tool */
    if(image->width() * image->height() > IMAGE_MAX_SIZE) {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
            tr("image resolution is bigger than 4k"), 2000);
        return;
    }
    // physical size from the file's resolution, then the picture at the output DPI (see mainwindow_screen.cpp)
    const QImage working = adoptNativeImage(image);
    // reset UI
    notification->cancel();
    ui->graphicsView->resetScene(working.width(), working.height());
    ui->resolutionLabel->setText(QString("%1 \u00D7 %2").arg(working.width()).arg(working.height()));

    // set mono image
    imageHashMono.setSourceImage(&working);
    ui->graphicsView->setOriginalImage(working);  // hold-to-compare shows this, untouched
    previewImage = working;
    invalidateSeparation();
    ui->treeWidgetMono->clearAllDitherFlags();
    ui->showOriginalMono->setCheckState(Qt::Unchecked);
    ui->graphicsView->setSourceImageMono(imageHashMono.getSourceQImage());
    resetContrastButtonMonoClickedSlot();
    resetBrightnessButtonMonoClickedSlot();
    resetGammaButtonMonoClickedSlot();

    // set color image
    imageHashColor.setSourceImage(&working);
    generateCachedPalette(false, true, true);
    ui->showOriginalColor->setCheckState(Qt::Unchecked);
    ui->graphicsView->setSourceImageColor(imageHashColor.getSourceQImage());
    resetContrastButtonColorClickedSlot();
    resetBrightnessButtonColorClickedSlot();
    resetGammaButtonColorClickedSlot();
    resetSaturationButtonColorClickedSlot();
    resetToneControls();  // the image caches reset the values themselves in setSourceImage

    if(firstLoad) { // on first load, we're in mono dithering mode
        firstLoad = false;
        enableGui(true);
    }
    updateScreenControls();  // film size depends on the image dimensions
    treeWidgetItemChangedSlot(activeTreeWidget->currentItem());
}
