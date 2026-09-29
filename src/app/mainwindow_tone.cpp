#include "mainwindow.h"
#include "consts.h"
#include "adjust/filters.h"
#include "ui_elements/signalblocker.h"
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>

/* This file contains:
 * - the Blacks / Shadows / Midtones / Highlights / Whites / Blur / Denoise rows of the Input Image Settings, for both the mono and
 *   the color page. The processing lives in adjust/ and imagehash/; this only builds and wires the controls.
 */

void MainWindow::addAdjustRow(QGridLayout* grid, const int row, const QString& label, const QString& toolTip,
                              const int minimum, const int maximum, const double scale, const int decimals,
                              const std::function<void(int)>& apply) {
    /* one row in the style of the existing ones: label, slider, numeric field, reset button.
     * The slider holds the stored integer; the field shows it divided by `scale`. */
    QWidget* parent = grid->parentWidget();
    QLabel* text = new QLabel(label, parent);
    QSlider* slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(minimum, maximum);
    slider->setTracking(false);  // like the existing sliders: apply on release, not on every step
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setToolTip(toolTip);
    QDoubleSpinBox* spin = new QDoubleSpinBox(parent);
    spin->setRange(minimum / scale, maximum / scale);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals == 0 ? 1.0 : 0.1);
    spin->setKeyboardTracking(false);
    spin->setMinimumSize(ui->brightnessEditMono->minimumSize());  // same column width as the existing rows
    spin->setMaximumSize(ui->brightnessEditMono->maximumSize());
    spin->setToolTip(toolTip);
    QPushButton* reset = new QPushButton(parent);
    setResetIcon(reset, &adjustResetIcon);
    reset->setToolTip(tr("Reset"));

    grid->addWidget(text, row, 0);
    grid->addWidget(slider, row, 1);
    grid->addWidget(spin, row, 2);
    grid->addWidget(reset, row, 3);

    connect(slider, &QSlider::valueChanged, this, [=](const int value) {
        whileBlocking(spin)->setValue(value / scale);
        apply(value);
    });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [=](const double value) {
        const int stored = static_cast<int>(std::lround(value * scale));
        whileBlocking(slider)->setValue(stored);
        apply(stored);
    });
    connect(reset, &QPushButton::clicked, this, [=]() {
        slider->setValue(0);  // every new control is neutral at 0
    });
    adjustControls.push_back({slider, spin});
}

void MainWindow::setupToneControls() {
    /* inserts the new rows above the "show original image" checkbox of each page */
    adjustResetIcon = QIcon();
    QPixmap* enabled = loadSvg(":/resources/times.svg");
    adjustResetIcon.addPixmap(*enabled, QIcon::Normal);
    delete enabled;
    QPixmap* disabled = loadSvg(":/resources/times_disabled.svg");
    adjustResetIcon.addPixmap(*disabled, QIcon::Disabled);
    delete disabled;

    const QString blacksTip = tr("- : crush the darkest tones to solid black (100% ink).\n"
                                 "+ : lift pure black so no area is solid.");
    const QString whitesTip = tr("+ : clip the lightest tones to paper white (no dot).\n"
                                 "- : dull pure white so every area keeps some dots.");
    const QString shadowsTip = tr("Lighten (+) or darken (-) mainly the dark tones. Black stays black.");
    const QString midtonesTip = tr("Lighten (+) or darken (-) mainly the middle tones.");
    const QString highlightsTip = tr("Lighten (+) or darken (-) mainly the light tones. White stays white.");
    const QString blurTip = tr("Gaussian blur before dithering, radius in mm on film: the same softness at any DPI.");
    const QString denoiseTip = tr("Edge-preserving noise reduction before dithering: smooths grain in flat areas "
                                  "without softening outlines.");
    const int blurMax = static_cast<int>(BLUR_MAX_MM * 100);

    const auto monoSet = [this](int& field, const int value) {
        if (field != value) { field = value; adjustImageMono(); }
    };
    const auto colorSet = [this](int& field, const int value) {
        if (field != value) { field = value; adjustImageColor(); }
    };

    // mono page
    {
        QGridLayout* grid = ui->gridLayout_17;
        int row = grid->rowCount() - 1;  // the "show original" checkbox row, moved to the end below
        grid->removeWidget(ui->showOriginalMono);
        addAdjustRow(grid, row++, tr("Blacks"), blacksTip, -100, 100, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.blacks, v); });
        addAdjustRow(grid, row++, tr("Shadows"), shadowsTip, -100, 100, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.shadows, v); });
        addAdjustRow(grid, row++, tr("Midtones"), midtonesTip, -100, 100, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.midtones, v); });
        addAdjustRow(grid, row++, tr("Highlights"), highlightsTip, -100, 100, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.highlights, v); });
        addAdjustRow(grid, row++, tr("Whites"), whitesTip, -100, 100, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.whites, v); });
        addAdjustRow(grid, row++, tr("Blur (mm)"), blurTip, 0, blurMax, 100.0, 2,
                     [=, this](int v) { monoSet(imageHashMono.blur, v); });
        addAdjustRow(grid, row++, tr("Denoise"), denoiseTip, 0, DENOISE_MAX, 1.0, 0,
                     [=, this](int v) { monoSet(imageHashMono.denoise, v); });
        grid->addWidget(ui->showOriginalMono, row, 0, 1, 2);
    }
    // color page
    {
        QGridLayout* grid = ui->gridLayout_19;
        int row = grid->rowCount() - 1;
        grid->removeWidget(ui->showOriginalColor);
        addAdjustRow(grid, row++, tr("Blacks"), blacksTip, -100, 100, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.blacks, v); });
        addAdjustRow(grid, row++, tr("Shadows"), shadowsTip, -100, 100, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.shadows, v); });
        addAdjustRow(grid, row++, tr("Midtones"), midtonesTip, -100, 100, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.midtones, v); });
        addAdjustRow(grid, row++, tr("Highlights"), highlightsTip, -100, 100, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.highlights, v); });
        addAdjustRow(grid, row++, tr("Whites"), whitesTip, -100, 100, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.whites, v); });
        addAdjustRow(grid, row++, tr("Blur (mm)"), blurTip, 0, blurMax, 100.0, 2,
                     [=, this](int v) { colorSet(imageHashColor.blur, v); });
        addAdjustRow(grid, row++, tr("Denoise"), denoiseTip, 0, DENOISE_MAX, 1.0, 0,
                     [=, this](int v) { colorSet(imageHashColor.denoise, v); });
        grid->addWidget(ui->showOriginalColor, row, 0, 1, 2);
    }
}

void MainWindow::resetToneControls() {
    /* a new image starts neutral: the image caches reset their values, this resets what the controls show */
    for (const AdjustControl& control : adjustControls) {
        whileBlocking(control.slider)->setValue(0);
        whileBlocking(control.spin)->setValue(0.0);
    }
}
