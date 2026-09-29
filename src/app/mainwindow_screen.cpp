#include "mainwindow.h"
#include "screening/matrixstretch.h"
#include <QCheckBox>
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
    dpiCombo->setToolTip(tr("Output DPI: resolution the pattern is drawn at. Does not change the physical "
                            "size of cells or dots."));

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

    screenInfoLabel = new QLabel(group);
    screenInfoLabel->setWordWrap(true);

    grid->addWidget(new QLabel(tr("Output DPI"), group), 0, 0);
    grid->addWidget(dpiCombo, 0, 1);
    grid->addWidget(lpiCheck, 1, 0);
    grid->addWidget(lpiSpin, 1, 1);
    grid->addWidget(dotCheck, 2, 0);
    grid->addWidget(dotSpin, 2, 1);
    grid->addWidget(screenInfoLabel, 3, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    const int index = ui->verticalLayout->indexOf(ui->imageSettingsStackedWidget);
    ui->verticalLayout->insertWidget(index, group, 0);

    connect(dpiCombo, &QComboBox::currentTextChanged, this, &MainWindow::screenSettingsChangedSlot);
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
    /* coarse grid for the current algorithm: matrix algorithms always dither at full resolution */
    return screenUsesLpi() ? 1 : screenGeometry.dotPixels();
}

OrderedDitherMatrix* MainWindow::applyLpi(OrderedDitherMatrix* matrix, const int width, const int height) const {
    /* stretches the matrix to the LPI cell when LPI is on; takes ownership of `matrix` */
    if (matrix == nullptr || !screenGeometry.lpiEnabled || !screenUsesLpi()) {
        return matrix;
    }
    OrderedDitherMatrix* stretched = stretchMatrixToCell(matrix, screenGeometry.pixelsPerCell(), width, height);
    OrderedDitherMatrix_free(matrix);
    return stretched;
}

void MainWindow::screenSettingsChangedSlot() {
    /* user changed DPI, LPI or dot size: every cached result may be stale */
    bool ok = false;
    const int dpi = dpiCombo->currentText().toInt(&ok);
    if (ok && dpi >= SCREEN_MIN_DPI && dpi <= SCREEN_MAX_DPI) {  // ignore half-typed values like "1" on the way to "1200"
        screenGeometry.dpi = dpi;
    }
    screenGeometry.lpiEnabled = lpiCheck->isChecked();
    screenGeometry.lpi = lpiSpin->value();
    screenGeometry.dotEnabled = dotCheck->isChecked();
    screenGeometry.dotMm = dotSpin->value();
    updateScreenControls();
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
        const QImage* source = imageHashMono.getSourceQImage();
        lines << tr("Film: %1 × %2 mm").arg(g.sizeMm(source->width()), 0, 'f', 1).arg(g.sizeMm(source->height()), 0, 'f', 1);
    }
    screenInfoLabel->setText(lines.join("\n"));
}
