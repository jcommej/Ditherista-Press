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
class PaletteDragHandle;

/* The colour list of the Palette tab: one row per colour,
 *     [handle + order] [swatch] [#HEX] [lock] [delete] [randomize this colour]
 * and below them  [+ Add Color] [Randomize] [Save Palette] [Load Palette].
 * It only shows the palette and reports what the user asks for; MainWindow owns the palette, applies the change
 * (see mainwindow_palette_editor.cpp) and calls setPalette with the result. Rows are updated in place, so the
 * field or button that sent a signal is never destroyed while its signal is being handled.
 * The order of the rows is the order of the palette - of the films and of the print passes: drag a row by its
 * handle to move its colour; every row shows its position.
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
    void moveRequested(int from, int to);  // a row dragged by its handle: the colour goes from `from` to `to`
    void addRequested();
    void randomizeRequested();
    void saveRequested();
    void loadRequested();
private:
    struct Row {
        QWidget* widget;
        PaletteDragHandle* handle;
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
    // dragging a row by its handle
    int dragFrom = -1;
    int dragTo = -1;
    int flashRow = -1;           // the row that just moved, highlighted for a moment
    QWidget* dropLine = nullptr;  // where the row will go
    void dragMoved(const QPoint& globalPos);
    void dragEnded();
    void flash(int index);
    Row makeRow(int index);
    void updateRow(size_t index);
    void hexEdited(size_t index);    // while typing: red frame when the text is no colour
    void hexFinished(size_t index);  // Enter or focus out: apply, or put the colour back
};

#endif // PALETTEEDITOR_H
