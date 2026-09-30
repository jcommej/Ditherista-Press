#pragma once
#ifndef PALETTEEDITOR_H
#define PALETTEEDITOR_H

#include "palette/palettemodel.h"
#include <QWidget>
#include <vector>

class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

/* The colour list of the Palette tab: one row per colour,
 *     [swatch] [#HEX] [lock] [delete] [randomize this colour]
 * and below them  [+ Add Color] [Randomize] [Save Palette] [Load Palette].
 * It only shows the palette and reports what the user asks for; MainWindow owns the palette, applies the change
 * (see mainwindow_palette_editor.cpp) and calls setPalette with the result. Rows are updated in place, so the
 * field or button that sent a signal is never destroyed while its signal is being handled.
 */
class PaletteEditor final : public QWidget {
    Q_OBJECT
public:
    explicit PaletteEditor(QWidget* parent = nullptr);
    void setPalette(const PaletteEntries& palette);
    [[nodiscard]] const PaletteEntries& palette() const { return entries; }
    void setEditingRow(int index);  // highlights the colour open in the colour picker; -1 = none
signals:
    void colourEdited(int index, QRgb colour);  // a valid HEX typed in a row
    void lockToggled(int index, bool locked);
    void removeRequested(int index);
    void randomizeOneRequested(int index);
    void pickRequested(int index);  // swatch clicked: open the colour picker on this colour
    void addRequested();
    void randomizeRequested();
    void saveRequested();
    void loadRequested();
private:
    struct Row {
        QWidget* widget;
        QToolButton* swatch;
        QLineEdit* hex;
        QToolButton* lock;
        QToolButton* remove;
        QToolButton* shuffle;
    };
    PaletteEntries entries;
    std::vector<Row> rows;
    QVBoxLayout* rowsLayout;
    QPushButton* addButton;
    int editingRow = -1;
    Row makeRow(int index);
    void updateRow(size_t index);
    void hexEdited(size_t index);    // while typing: red frame when the text is no colour
    void hexFinished(size_t index);  // Enter or focus out: apply, or put the colour back
};

#endif // PALETTEEDITOR_H
