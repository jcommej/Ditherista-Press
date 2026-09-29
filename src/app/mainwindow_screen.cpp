#include "mainwindow.h"
#include "ui_elements/signalblocker.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLabel>

/* This file contains:
 * - the Screen settings panel: output DPI and LPI (dot size), see screening/screengeometry.h
 */

static const QList<int> DPI_PRESETS = {300, 600, 1200};

void MainWindow::setupScreenControls() {
    /* builds the Screen panel and inserts it above the Input Image Settings */
    QGroupBox* group = new QGroupBox(tr("Screen"), ui->imageSettingsContainer);
    group->setToolTip(tr("Film resolution and screen frequency. LPI sets the size of the dither dots."));
    QGridLayout* grid = new QGridLayout(group);

    dpiCombo = new QComboBox(group);
    dpiCombo->setEditable(true);
    dpiCombo->setInsertPolicy(QComboBox::NoInsert);  // typed values are used, not added to the presets
    for (const int dpi : DPI_PRESETS) {
        dpiCombo->addItem(QString::number(dpi));
    }
    dpiCombo->setValidator(new QIntValidator(static_cast<int>(SCREEN_MIN_DPI), static_cast<int>(SCREEN_MAX_DPI), dpiCombo));
    dpiCombo->setCurrentText(QString::number(static_cast<int>(SCREEN_DEFAULT_DPI)));
    dpiCombo->setToolTip(tr("Output DPI: resolution of the film. Presets or type a value."));

    lpiCheck = new QCheckBox(tr("LPI"), group);
    lpiCheck->setToolTip(tr("When off, every image pixel is one dot, as in standard Ditherista."));
    lpiSpin = new QDoubleSpinBox(group);
    lpiSpin->setRange(SCREEN_MIN_LPI, SCREEN_MAX_LPI);
    lpiSpin->setDecimals(1);
    lpiSpin->setSingleStep(1.0);
    lpiSpin->setKeyboardTracking(false);  // re-dither once the value is committed, not on every keystroke
    lpiSpin->setValue(SCREEN_DEFAULT_LPI);
    lpiSpin->setEnabled(false);
    lpiSpin->setToolTip(tr("Lines per inch. Cells are snapped to whole film pixels; the LPI actually produced "
                           "is shown below."));

    screenInfoLabel = new QLabel(group);
    screenInfoLabel->setWordWrap(true);

    grid->addWidget(new QLabel(tr("Output DPI"), group), 0, 0);
    grid->addWidget(dpiCombo, 0, 1);
    grid->addWidget(lpiCheck, 1, 0);
    grid->addWidget(lpiSpin, 1, 1);
    grid->addWidget(screenInfoLabel, 2, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    const int index = ui->verticalLayout->indexOf(ui->imageSettingsStackedWidget);
    ui->verticalLayout->insertWidget(index, group, 0);

    connect(dpiCombo, &QComboBox::currentTextChanged, this, &MainWindow::screenSettingsChangedSlot);
    connect(lpiCheck, &QCheckBox::toggled, this, &MainWindow::screenSettingsChangedSlot);
    connect(lpiSpin, &QDoubleSpinBox::valueChanged, this, &MainWindow::screenSettingsChangedSlot);
    updateScreenInfo();
}

void MainWindow::screenSettingsChangedSlot() {
    /* user changed DPI or LPI: resize the dither cells and re-dither */
    bool ok = false;
    const int dpi = dpiCombo->currentText().toInt(&ok);
    if (ok && dpi >= SCREEN_MIN_DPI && dpi <= SCREEN_MAX_DPI) {  // ignore half-typed values like "1" on the way to "1200"
        screenGeometry.dpi = dpi;
    }
    screenGeometry.enabled = lpiCheck->isChecked();
    screenGeometry.lpi = lpiSpin->value();
    lpiSpin->setEnabled(screenGeometry.enabled);
    updateScreenInfo();

    const int n = screenGeometry.cellSize();
    const bool monoChanged = imageHashMono.setCellSize(n);
    const bool colorChanged = imageHashColor.setCellSize(n);
    if (monoChanged) {
        ui->treeWidgetMono->clearAllDitherFlags();
    }
    if (colorChanged) {
        ui->treeWidgetColor->clearAllDitherFlags();
    }
    if ((monoChanged || colorChanged) && !firstLoad) {
        reDither(false);
    }
}

void MainWindow::updateScreenInfo() {
    /* shows the computed screen geometry, e.g. "300 DPI · 6.67 px/cell → 7 px · 42.86 LPI · 0.593 mm" */
    const ScreenGeometry& g = screenGeometry;
    QString text;
    if (g.enabled) {
        text = tr("%1 px/cell → %2 px · %3 LPI · %4 mm/cell")
                   .arg(g.pixelsPerCell(), 0, 'f', 2)
                   .arg(g.cellSize())
                   .arg(g.effectiveLpi(), 0, 'f', 2)
                   .arg(g.cellMm(), 0, 'f', 3);
    } else {
        text = tr("1 dot per image pixel (LPI off)");
    }
    if (!firstLoad) {
        const QImage* source = imageHashMono.getSourceQImage();
        text += "\n" + tr("Film: %1 × %2 mm at %3 DPI")
                           .arg(g.sizeMm(source->width()), 0, 'f', 1)
                           .arg(g.sizeMm(source->height()), 0, 'f', 1)
                           .arg(g.dpi, 0, 'f', 0);
    }
    screenInfoLabel->setText(text);
}
