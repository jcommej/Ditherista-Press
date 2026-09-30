#include "colourpickerdialog.h"
#include "labpanel.h"
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QShortcut>
#include <QVBoxLayout>

ColourPickerDialog::ColourPickerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Palette Colour"));
    setModal(false);
    heading = new QLabel(this);

    // Qt's colour picker as a plain widget inside this window: all its controls, without its own buttons
    picker = new QColorDialog(this);
    picker->setWindowFlags(Qt::Widget);
    picker->setOptions(QColorDialog::DontUseNativeDialog | QColorDialog::NoButtons);
    labPanel = new LabPanel(this);
    labPanel->setMinimumWidth(300);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QLabel* hint = new QLabel(tr("Ctrl+Z / Ctrl+Y: previous / next colour tried"), this);
    hint->setEnabled(false);
    QHBoxLayout* sides = new QHBoxLayout();
    sides->addWidget(picker, 0, Qt::AlignTop);
    sides->addWidget(labPanel, 1);
    QHBoxLayout* bottom = new QHBoxLayout();
    bottom->addWidget(hint, 1);
    bottom->addWidget(buttons);
    QVBoxLayout* column = new QVBoxLayout(this);
    column->addWidget(heading);
    column->addLayout(sides, 1);
    column->addLayout(bottom);

    connect(picker, &QColorDialog::currentColorChanged, this, &ColourPickerDialog::fromPicker);
    connect(labPanel, &LabPanel::labPicked, this, &ColourPickerDialog::fromLab);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QShortcut* undo = new QShortcut(QKeySequence::Undo, this);
    QShortcut* redo = new QShortcut(QKeySequence::Redo, this);
    QShortcut* redoAlt = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this);
    connect(undo, &QShortcut::activated, this, &ColourPickerDialog::undoRequested);
    connect(redo, &QShortcut::activated, this, &ColourPickerDialog::redoRequested);
    connect(redoAlt, &QShortcut::activated, this, &ColourPickerDialog::redoRequested);
}

void ColourPickerDialog::setTitle(const QString& what) {
    heading->setText(what);
}

void ColourPickerDialog::setColour(const QRgb rgb) {
    updating = true;
    lab = rgbToLab(rgb);
    picker->setCurrentColor(QColor::fromRgb(rgb));
    labPanel->setLab(lab);
    updating = false;
}

void ColourPickerDialog::fromPicker(const QColor& colour) {
    if (updating) {
        return;
    }
    const QRgb rgb = colour.rgb() | 0xFF000000u;
    if (rgb == labToRgb(lab)) {
        return;  // e.g. the picker repeating the colour the LAB side just gave it
    }
    lab = rgbToLab(rgb);
    updating = true;
    labPanel->setLab(lab);
    updating = false;
    emit colourChanged(rgb);
}

void ColourPickerDialog::fromLab(const Lab& picked) {
    if (updating) {
        return;
    }
    const QRgb before = labToRgb(lab);
    lab = picked;  // kept exact: dragging in L*a*b* does not snap to 8-bit steps
    const QRgb rgb = labToRgb(lab);
    updating = true;
    picker->setCurrentColor(QColor::fromRgb(rgb));
    updating = false;
    if (rgb != before) {
        emit colourChanged(rgb);
    }
}
