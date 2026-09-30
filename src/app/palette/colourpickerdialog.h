#pragma once
#ifndef COLOURPICKERDIALOG_H
#define COLOURPICKERDIALOG_H

#include "color/colorspace.h"
#include <QDialog>

class QColorDialog;
class QLabel;
class LabPanel;

/* The palette colour picker: Qt's colour picker, as Ditherista has always used (hue / saturation field, value,
 * RGB, HSV, HTML and the screen colour picker), with the CIELAB panel beside it (labpanel.h).
 * ----------------------------------------------------------------------------------------------------------
 * - One colour: `lab` holds it, and each side is only a way to change it. A change on either side moves the
 *   other side's pointers and fields; values are converted with color/colorspace.h (sRGB, D65).
 * - Not modal, and live: colourChanged is sent on every change, so the picture can follow while searching.
 * - Ctrl+Z / Ctrl+Y ask for the previous / next colour of the session (undoRequested / redoRequested): the steps
 *   live in the palette history, so the picker and the palette never keep two different histories
 *   (mainwindow_palette_editor.cpp). A text field being edited keeps Ctrl+Z for itself.
 * - OK keeps the colour, Cancel (or Esc, or closing) puts back the one the picker opened with.
 */
class ColourPickerDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ColourPickerDialog(QWidget* parent = nullptr);
    void setColour(QRgb rgb);           // shows it everywhere; no colourChanged
    [[nodiscard]] QRgb colour() const { return labToRgb(lab); }
    void setTitle(const QString& what);  // e.g. "colour 3 of 16"
signals:
    void colourChanged(QRgb rgb);  // the user changed it, on either side
    void undoRequested();
    void redoRequested();
private:
    QColorDialog* picker;
    LabPanel* labPanel;
    QLabel* heading;
    Lab lab;
    bool updating = false;  // set while one side is being moved to follow the other
    void fromPicker(const QColor& colour);
    void fromLab(const Lab& picked);
};

#endif // COLOURPICKERDIALOG_H
