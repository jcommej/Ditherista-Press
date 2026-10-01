#include "mainwindow.h"
#include "consts.h"
#include "ui_elements/signalblocker.h"
#include "palette/palettethemes.h"
#include <QComboBox>
#include <QLabel>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMessageBox>

/* This file contains:
 * - color palette related methods
 */

/************************************
 * LAB COLOR PARAMETER HANDLING     *
 ************************************/

void MainWindow::srcIlluminantComboChangedSlot(int index) {
    /* user changed the LAB illuminant combo */
    switch (index) {
        case 1: FloatColor_from_FloatColor(&srcIlluminant, &D93_XYZ); break;
        case 2: FloatColor_from_FloatColor(&srcIlluminant, &D75_XYZ); break;
        case 3: FloatColor_from_FloatColor(&srcIlluminant, &D65_XYZ); break;
        case 4: FloatColor_from_FloatColor(&srcIlluminant, &D55_XYZ); break;
        case 5: FloatColor_from_FloatColor(&srcIlluminant, &D50_XYZ); break;
        case 6: FloatColor_from_FloatColor(&srcIlluminant, &A_XYZ); break;
        case 7: FloatColor_from_FloatColor(&srcIlluminant, &B_XYZ); break;
        case 8: FloatColor_from_FloatColor(&srcIlluminant, &C_XYZ); break;
        case 9: FloatColor_from_FloatColor(&srcIlluminant, &E_XYZ); break;
        case 10: FloatColor_from_FloatColor(&srcIlluminant, &F1_XYZ); break;
        case 11: FloatColor_from_FloatColor(&srcIlluminant, &F2_XYZ); break;
        case 12: FloatColor_from_FloatColor(&srcIlluminant, &F3_XYZ); break;
        case 13: FloatColor_from_FloatColor(&srcIlluminant, &F7_XYZ); break;
        case 14: FloatColor_from_FloatColor(&srcIlluminant, &F11_XYZ); break;
    }
    generateCachedPalette(true, false, false);
}

void MainWindow::resetLabHCVspinBoxes() {
    /* resets LAB weights to their default values */
    whileBlocking(ui->spinBoxValue)->setValue(cachedPalette->lab_weights.v);
    whileBlocking(ui->spinBoxChroma)->setValue(cachedPalette->lab_weights.c);
    whileBlocking(ui->spinBoxHue)->setValue(cachedPalette->lab_weights.h);
}

void MainWindow::labHCVChanged() {
    /* refresh/re-dither the current image after user changed LAB weight settings */
    CachedPalette_free_cache(cachedPalette); // no need the re-gen entire palette if LAB weights change
    imageHashColor.clearAllDitheredImages();
    ui->treeWidgetColor->clearAllDitherFlags();
    reDither(true);
}

void MainWindow::spinBoxValueChangedSlot(double value) {
    /* user changed LAB color Value weight spin-box */
    cachedPalette->lab_weights.v = value;
    labHCVChanged();
}

void MainWindow::resetValueWeightButtonClickedSlot() {
    /* user pressed the LAB color Value reset button */
    whileBlocking(ui->spinBoxValue)->setValue(LAB_W_VALUE);
    spinBoxValueChangedSlot(LAB_W_VALUE);
}

void MainWindow::spinBoxHueChangedSlot(double hue) {
    /* user changed LAB color Hue weight spin-box */
    cachedPalette->lab_weights.h = hue;
    labHCVChanged();
}

void MainWindow::resetHueWeightButtonClickedSlot() {
    /* user pressed the LAB color Hue reset button */
    whileBlocking(ui->spinBoxHue)->setValue(LAB_W_HUE);
    spinBoxHueChangedSlot(LAB_W_HUE);
}

void MainWindow::spinBoxChromaChangedSlot(double chroma) {
    /* user changed LAB color Chroma weight spin-box */
    cachedPalette->lab_weights.c = chroma;
    labHCVChanged();
}

void MainWindow::resetChromaWeightButtonClickedSlot() {
    /* user pressed the LAB color Chroma reset button */
    whileBlocking(ui->spinBoxChroma)->setValue(LAB_W_CHROMA);
    spinBoxChromaChangedSlot(LAB_W_CHROMA);
}

/************************************
 * MONO-PALETTE / MONO DITHERING    *
 ************************************/

void MainWindow::monoColorOneChangedSlot(QColor color) {
    /* user changed one of the mono colors */
    ui->monoColorOneLabel->setColor(color);
    ui->monoColorOneButton->setColor(color);
    ui->resetMonoColors->setEnabled(color != DEFAULT_DARK_MONO_COLOR || ui->monoColorTwoButton->getColor() != DEFAULT_LIGHT_MONO_COLOR);
    setDitherImageMono();
}

void MainWindow::monoColorTwoChangedSlot(QColor color) {
    /* user changed one of the mono colors */
    ui->monoColorTwoLabel->setColor(color);
    ui->monoColorTwoButton->setColor(color);
    ui->resetMonoColors->setEnabled(ui->monoColorOneButton->getColor() != DEFAULT_DARK_MONO_COLOR || color != DEFAULT_LIGHT_MONO_COLOR);
    setDitherImageMono();
}

void MainWindow::resetMonoColorsClickedSlot() {
    /* use clicked the mono palette color's reset button */
    ui->monoColorOneLabel->setColor(DEFAULT_DARK_MONO_COLOR);
    ui->monoColorOneButton->setColor(DEFAULT_DARK_MONO_COLOR);
    ui->monoColorTwoLabel->setColor(DEFAULT_LIGHT_MONO_COLOR);
    ui->monoColorTwoButton->setColor(DEFAULT_LIGHT_MONO_COLOR);
    ui->resetMonoColors->setEnabled(false);
    setDitherImageMono();
}

/************************************
 * PAINT.NET PALETTE FILE I/O       *
 ************************************/

void MainWindow::savePaletteButtonClickedSlot() {
    /* saves the palette to a Paint.NET file */
    savePaintNetPalette(currentPaletteEntries());
}

void MainWindow::savePaintNetPalette(const PaletteEntries& palette) {
    /* asks for a file name and saves `palette` as a Paint.NET palette, locks in a comment (palette/palettemodel.h) */
    const QString filter = tr("Palettes") + " (" + PALETTE_FILE_FILTERS.join(" ") + ")";
    const QString fileName = QFileDialog::getSaveFileName(this, tr("Save Palette"), lastSavedPalette, filter);
    if (fileName.isEmpty()) { // user cancelled
        notification->showText(tr("WARNING: palette not saved"), 3000);
        return;
    }
    QFile file(fileName);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(PaletteModel::toPaintNet(palette, QString("Ditherista palette (%1)").arg(palette.size())).toUtf8());
        file.close();
    } else {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                               tr("palette file write error"), 3000);
        return;
    }
    notification->showText(tr("palette saved"), 2000);
    lastSavedPalette = fileName;
}

BytePalette* MainWindow::loadPaintNetPalette(QString fileName, int* errorCode) {
    /* loads a Paint.NET compatible palette file, such as found on lospec.com.
     * Transparency is currently not supported.
     * Color entries can be 32 or 24 bit (i.e. including alpha or without)
     * Comments starting with ; are supported */
    *errorCode = OK_PALETTE_LOAD;
    QFile file = QFile(fileName);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug()<<"ERROR: loading Paint .NET palette: "<<file.errorString();
        *errorCode = ERROR_PALETTE_LOAD_IO;
        return nullptr;
    }
    // one reader for every palette file: the one the palette editor uses (palette/palettemodel.h)
    PaletteEntries palette;
    QString error;
    bool truncated = false;
    bool tooFew = false;
    if (!PaletteModel::fromPaintNet(QString::fromUtf8(file.readAll()), &palette, &error, &truncated, &tooFew)) {
        qDebug()<<"ERROR: parsing palette: "<<error;
        *errorCode = tooFew ? ERROR_PALETTE_LOAD_LOW_COLORS : ERROR_PALETTE_LOAD_PARSE;
        return nullptr;
    }
    if (truncated) {
        qDebug()<<"WARNING: palette has more than 256 colors (palette truncated)";
        *errorCode = ERROR_PALETTE_LOAD_MAX_COLORS;
    }
    BytePalette* pal = BytePalette_new(palette.size());
    for (size_t i = 0; i < palette.size(); i++) {
        const ByteColor color = {static_cast<uint8_t>(qRed(palette[i].colour)), static_cast<uint8_t>(qGreen(palette[i].colour)),
                                 static_cast<uint8_t>(qBlue(palette[i].colour)), 255};  // transparency not supported
        BytePalette_set(pal, i, &color);
    }
    return pal;
}

/************************************
 * COLOR-PALETTE LOADING FROM FILE  *
 ************************************/

void MainWindow::paletteBrowseButtonClickedSlot() {
    /* user pressed "Browse" button to load a custom palette */
    QString fileIoLocation = lastLoadedPalette;
    const QString filter = tr("Palettes") + " (" + PALETTE_FILTERS.join(" ") + ")";
    QString fileName = QFileDialog::getOpenFileName((QWidget *) parent(),
                                                    tr("Open Palette File"), fileIoLocation,
                                                    filter);
    if (fileName.isEmpty() || fileName.isNull() || !QFile::exists(fileName)) {
        return; // cancelled or file does not exist
    }
    if (loadPaintNetPaletteWrapper(fileName)) {
        whileBlocking(ui->palettePathEdit)->setText(fileName);
    }
}

void MainWindow::palettePathEditEditingFinishedSlot() {
    /* user entered a path in the palette path text-edit field */
    QString newPalette = ui->palettePathEdit->text();
    if (lastLoadedPalette != newPalette) {
        if (!loadPaintNetPaletteWrapper(newPalette)) {
            whileBlocking(ui->palettePathEdit)->setText(lastLoadedPalette); // restore old filename
        }
    }
}

bool MainWindow::loadPaintNetPaletteWrapper(QString fileName) {
    /* wrapper, with error handling for loading a Paint.NET palette */
    int errorCode;
    if (loadFromFilePalette != nullptr) {
        BytePalette_free(loadFromFilePalette);
    }
    loadFromFilePalette = loadPaintNetPalette(fileName, &errorCode);
    if (errorCode == OK_PALETTE_LOAD || errorCode == ERROR_PALETTE_LOAD_MAX_COLORS) {
        generateCachedPalette(true, false, true);
        if (errorCode == ERROR_PALETTE_LOAD_MAX_COLORS) {
            notification->showText(tr("WARNING\nmore than 256 colors in palette"), 3000);
        }
        lastLoadedPalette = fileName;
    } else {
        switch(errorCode) {
            case ERROR_PALETTE_LOAD_IO:
                notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                                       tr("palette file read error"), 3000);
                break;
            case ERROR_PALETTE_LOAD_PARSE:
                notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                                       tr("palette file parsing error"), 3000);
                break;
            case ERROR_PALETTE_LOAD_LOW_COLORS:
                notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                                       tr("palette has too few colors"), 3000);
                break;
            default: // we should never get this error
                notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" +
                                       tr("palette loading error"), 3000);
                break;
        }
        return false;
    }
    return true;
}

/*************************************************
 * COLOR-COMPARISON MODE                         *
 *************************************************/

void MainWindow::colorComparisonComboChangedSlot(int index) {
    /* user changed the combobox for setting the color comparison mode */
    switch(index) {
        case 3: colorComparisonMode = LUMINANCE; break;
        case 5: colorComparisonMode = SRGB; break;
        case 7: colorComparisonMode = LINEAR; break;
        case 4: colorComparisonMode = HSV; break;
        case 0: colorComparisonMode = LAB76; break;
        case 1: colorComparisonMode = LAB94; break;
        case 2: colorComparisonMode = LAB2000; break;
        case 6: colorComparisonMode = SRGB_CCIR; break;
        case 8: colorComparisonMode = LINEAR_CCIR; break;
        case 9: colorComparisonMode = TETRAPAL; break;
    }
    bool expand = colorComparisonMode == LAB76 || colorComparisonMode == LAB94 || colorComparisonMode == LAB2000;
    expandColorComparisonArea(expand);

    // TODO instead of disabling (=temporary solution), we should rework the .ui file instead and not display the spinners at all
    if (expand) {
        ui->spinBoxHue->setEnabled(colorComparisonMode != LAB76);
        ui->spinBoxChroma->setEnabled(colorComparisonMode != LAB76);
        ui->spinBoxValue->setEnabled(colorComparisonMode != LAB76);
        ui->resetHueWeightButton->setEnabled(colorComparisonMode != LAB76);
        ui->resetChromaWeightButton->setEnabled(colorComparisonMode != LAB76);
        ui->resetValueWeightButton->setEnabled(colorComparisonMode != LAB76);
    }

    generateCachedPalette(true, false, true);
}

/*******************************************************
 * COLOR-PALETTE SELECTION / CACHED-PALETTE GENERATION *
 *******************************************************/

void MainWindow::paletteSourceComboChangedSlot(int index) {
    /* user changed the combobox for setting the palette source (built-in, from file, custom) */
    ui->paletteSourceWidget->setCurrentIndex(index);
    generateCachedPalette(true, false, true);
}

void MainWindow::updatePaletteFromImage() {
    /* creates a reduced palette from the currently loaded image */
    ColorImage* image = imageHashColor.getSourceImage();
    if (image == nullptr) {
        qDebug() << "WARNING: image from imageHash at generateCachedPalette() is null!";
    } else {
        bool ok = false;
        int colors = QVariant(ui->paletteColorsEdit->text()).toInt(&ok);
        if (ok) {
            CachedPalette_free(cachedPalette);
            cachedPalette = CachedPalette_new();
            fthread = QtConcurrent::run(CachedPalette_from_image, cachedPalette, image, colors,
                                        colorReductionMode,
                                        ui->palGenUniqueColorsCheck->isChecked(),
                                        ui->palGenBWCheck->isChecked(),
                                        ui->palGenRGBCheck->isChecked(),
                                        ui->palGenCMYCheck->isChecked());
            runDitherThread();
            CachedPalette_update_cache(cachedPalette, colorComparisonMode, &srcIlluminant);
            CachedPalette_set_shift(cachedPalette, DEFAULT_BIT_SHIFT.r, DEFAULT_BIT_SHIFT.g, DEFAULT_BIT_SHIFT.b);
        }
    }
}

void MainWindow::updateCachedPalette(BytePalette* pal) {
    /* loads a palette from file or .qrc resource */
    CachedPalette_free(cachedPalette);
    cachedPalette = CachedPalette_new();
    BytePalette_free(reducedPalette);
    reducedPalette = BytePalette_copy(pal);
    CachedPalette_from_BytePalette(cachedPalette, reducedPalette);
    CachedPalette_update_cache(cachedPalette, colorComparisonMode, &srcIlluminant);
    CachedPalette_set_shift(cachedPalette, DEFAULT_BIT_SHIFT.r, DEFAULT_BIT_SHIFT.g, DEFAULT_BIT_SHIFT.b);
}

void MainWindow::generateCachedPalette(bool dither, bool resetLab, bool updateSwatches) {
    /* generates a cache for speeding up palette lookups during dithering */
    setMouseBusy(true);
    // use file based palette
    if (ui->paletteSourceWidget->currentIndex() == PALETTE_FROM_FILE) {
        if (loadFromFilePalette != nullptr) {
            updateCachedPalette(loadFromFilePalette);
        } else { // no file based palette loaded -> do nothing
            setMouseBusy(false);
            return;
        }
    // use built-in palette from resource file
    } else if (ui->paletteSourceWidget->currentIndex() == PALETTE_BUILT_IN) {
        int errorCode = 0;
        BytePalette* pal = loadPaintNetPalette(lastBuiltInPalette, &errorCode);
        if (pal != nullptr) {
            updateCachedPalette(pal);
            BytePalette_free(pal);
        } else {
            qDebug()<<"WARNING: failed to load palette from ressource:"<<lastBuiltInPalette<<errorCode;
        }
    // quantify colors form image
    } else if (ui->paletteSourceWidget->currentIndex() == PALETTE_CUSTOM) {
        updateCachedPalette(customPalette);
    } else {
        updatePaletteFromImage();
    }
    updatePaletteColorSwatches(cachedPalette->target_palette);
    refreshUiColorDitherStatus(resetLab, updateSwatches);
    if (!resetLab) {
        cachedPalette->lab_weights.v = ui->spinBoxValue->value();
        cachedPalette->lab_weights.c = ui->spinBoxChroma->value();
        cachedPalette->lab_weights.h = ui->spinBoxHue->value();
    }
    setMouseBusy(false);
    if (dither) {
        reDither(true);
    }
}

/*************************************************
 * PRE-DEFINED (BUILT-IN) COLOR-PALETTE          *
 *************************************************/

void MainWindow::predefinedPaletteComboChangedSlot(int index) {
    /* user chooses a different pre-defined palette */
    QString palName = ui->predefinedPaletteCombo->itemData(index).toString();
    lastBuiltInPalette = QString(":/resources/palettes/%1.pal").arg(palName);
    generateCachedPalette(true, false, true);
}

/*************************************************
 * COLOR-PALETTE REDUCTION / QUANTIFICATION      *
 *************************************************/

void MainWindow::paletteIncludeExtremeColorsSlot(int) {
    /* triggered for custom palettes when user checks one of the checkboxes, e.g. to include RGB or CMY colors */
    generateCachedPalette(true, false, true);
}

void MainWindow::colorReductionComboChangedSlot(int index) {
    /* triggered for custom palettes when user changes the color reduction method */
    // the combo lists Median Cut, Wu, KD-Tree in the enum's order (upstream added 1, so each ran the next one)
    colorReductionMode = static_cast<enum QuantizationMethod>(index);
    generateCachedPalette(true, false, true);
}

/*************************************************
 * COLOR-PALETTE COLOR SWATCHES                  *
 *************************************************/

void MainWindow::paletteColorsEditEditingFinishedSlot() {
    /* triggered for custom palattes when user edits the input field for the number of colors */
    generateCachedPalette(true, false, true);
}

void MainWindow::updatePaletteColorSwatches(BytePalette* palette) {
    /* shows the palette in the palette editor (see mainwindow_palette_editor.cpp) */
    if (paletteEditor != nullptr && palette == cachedPalette->target_palette) {
        paletteEditor->setPalette(currentPaletteEntries());  // with the custom palette's locks
    }
}

/*************************************************
 * PALETTE THEMES (REDUCED PALETTE)              *
 *************************************************/

void MainWindow::setupPaletteThemes() {
    /* a Theme row on top of the "reduced" palette page: Ristretto .. Grand Cru fill the existing fields (see
     * palette/palettethemes.h); the row shows Custom when the fields match no theme */
    paletteThemeCombo = new QComboBox(ui->paletteColorsEdit->parentWidget());
    paletteThemeCombo->addItem(tr("Custom"));
    for (const PaletteTheme& theme : PALETTE_THEMES) {
        paletteThemeCombo->addItem(QString("%1 - %2").arg(QString::fromUtf8(theme.name)).arg(theme.colours));
        paletteThemeCombo->setItemData(paletteThemeCombo->count() - 1, tr(theme.toolTip), Qt::ToolTipRole);
    }
    paletteThemeCombo->setToolTip(tr("Ready-made settings for reducing the picture's own colours, from 3 to 32"));
    QLabel* label = new QLabel(tr("Theme"), ui->paletteColorsEdit->parentWidget());
    // the existing rows move down one row
    QGridLayout* grid = ui->gridLayout_34;
    struct Cell { QLayoutItem* item; int row, column, rowSpan, columnSpan; };
    std::vector<Cell> cells;
    while (grid->count() > 0) {
        Cell cell{nullptr, 0, 0, 1, 1};
        grid->getItemPosition(0, &cell.row, &cell.column, &cell.rowSpan, &cell.columnSpan);
        cell.item = grid->takeAt(0);
        cells.push_back(cell);
    }
    int columns = 1;
    for (const Cell& cell : cells) {
        grid->addItem(cell.item, cell.row + 1, cell.column, cell.rowSpan, cell.columnSpan);
        columns = std::max(columns, cell.column + cell.columnSpan);
    }
    grid->addWidget(label, 0, 0);
    grid->addWidget(paletteThemeCombo, 0, 1, 1, columns - 1);

    connect(paletteThemeCombo, &QComboBox::activated, this, [this](const int index) {
        if (index <= 0) {
            return;  // Custom: the fields stay as they are
        }
        const PaletteTheme& theme = PALETTE_THEMES[static_cast<size_t>(index - 1)];
        whileBlocking(ui->paletteColorsEdit)->setText(QString::number(theme.colours));
        whileBlocking(ui->colorReductionCombo)->setCurrentIndex(theme.reduction);
        colorReductionMode = static_cast<enum QuantizationMethod>(theme.reduction);
        whileBlocking(ui->palGenBWCheck)->setChecked(theme.keepBlackWhite);
        whileBlocking(ui->palGenUniqueColorsCheck)->setChecked(false);
        whileBlocking(ui->palGenRGBCheck)->setChecked(false);
        whileBlocking(ui->palGenCMYCheck)->setChecked(false);
        generateCachedPalette(true, false, true);  // one new palette, one render
    });
    // any field changed by hand (or by a preset, an undo): the theme it now matches, or Custom
    const auto follow = [this]() { updatePaletteThemeCombo(); };
    connect(ui->paletteColorsEdit, &QLineEdit::textChanged, this, follow);
    connect(ui->colorReductionCombo, &QComboBox::currentIndexChanged, this, follow);
    for (QCheckBox* check : {ui->palGenBWCheck, ui->palGenUniqueColorsCheck, ui->palGenRGBCheck, ui->palGenCMYCheck}) {
        connect(check, &QCheckBox::toggled, this, follow);
    }
    updatePaletteThemeCombo();
}

void MainWindow::updatePaletteThemeCombo() {
    if (paletteThemeCombo == nullptr) {
        return;
    }
    const int theme = matchingPaletteTheme(ui->paletteColorsEdit->text().toInt(), ui->colorReductionCombo->currentIndex(),
                                           ui->palGenBWCheck->isChecked(), ui->palGenUniqueColorsCheck->isChecked(),
                                           ui->palGenRGBCheck->isChecked(), ui->palGenCMYCheck->isChecked());
    whileBlocking(paletteThemeCombo)->setCurrentIndex(theme + 1);  // 0 = Custom
}
