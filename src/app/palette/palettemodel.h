#pragma once
#ifndef PALETTEMODEL_H
#define PALETTEMODEL_H

#include <QImage>
#include <QString>
#include <cstdint>
#include <vector>

/* Editable palette: its colours, each with a lock, and the rules that keep it usable by the colour ditherers
 * -----------------------------------------------------------------------------------------------------------
 * - 2 to 256 colours (the limits of Ditherista's palette files): removing below 2 or adding beyond 256 is refused.
 * - A lock guards a colour against removal and randomizing; it can still be edited and used.
 * - Randomizing is deterministic for a given seed: the same palette and seed always give the same result.
 * - Files are Paint.NET palettes (what Save Palette has always written, and Lospec uses); locks go in a comment
 *   line, ";Locked: 1,4" (1-based), which other programs skip like any comment.
 */

struct PaletteEntry {
    QRgb colour = 0xFF000000u;  // opaque
    bool locked = false;
    bool operator==(const PaletteEntry& other) const { return colour == other.colour && locked == other.locked; }
};

using PaletteEntries = std::vector<PaletteEntry>;

namespace PaletteModel {
    constexpr int MIN_COLOURS = 2;
    constexpr int MAX_COLOURS = 256;

    bool canAdd(const PaletteEntries& palette);
    bool canRemove(const PaletteEntries& palette, int index);  // in range, unlocked, and more than 2 colours left
    bool add(PaletteEntries& palette, QRgb colour);            // at the end
    bool remove(PaletteEntries& palette, int index);

    // every unlocked colour moved at random around itself in CIELAB (about +-10 L*, +-25 a* and b*), kept in sRGB
    void randomize(PaletteEntries& palette, std::uint32_t seed);
    // one colour replaced by a new random one, anywhere in sRGB; refused if locked or out of range
    bool randomizeOne(PaletteEntries& palette, int index, std::uint32_t seed);

    // the colour of `image` the palette renders worst: pixels grouped by similar colour, and the group whose
    // pixels are, all together, furthest from their nearest palette colour (CIELAB distance x pixel count) gives
    // its average colour. Mid grey for an empty image or palette.
    QRgb leastRepresented(const QImage& image, const PaletteEntries& palette);

    // Paint.NET palette file with the locks as a comment
    QString toPaintNet(const PaletteEntries& palette, const QString& name);
    // reads what toPaintNet writes, and any Paint.NET palette: RRGGBB or AARRGGBB lines (alpha ignored); other
    // lines are skipped, as Ditherista always has. Fails with a message below 2 colours (*tooFew set); beyond 256
    // the rest is dropped and *truncated set.
    bool fromPaintNet(const QString& text, PaletteEntries* palette, QString* error, bool* truncated = nullptr,
                      bool* tooFew = nullptr);
}

/* Undo / redo for the palette editor: whole-palette snapshots, which are small (256 colours at most). Every
 * change records the palette as it was before.
 * A colour picker session records one step per colour tried, and undoes no further back than the palette it
 * opened on. When it ends, OK folds its steps into one (Ctrl+Z afterwards goes back to the palette before the
 * picker), Cancel returns that palette and drops the steps. */
class PaletteHistory {
public:
    static constexpr size_t MAX_STEPS = 200;
    void record(const PaletteEntries& before);  // before a change; clears the redo steps
    [[nodiscard]] bool canUndo() const { return undoSteps.size() > floorSize; }
    [[nodiscard]] bool canRedo() const { return !redoSteps.empty(); }
    bool undo(PaletteEntries& current);  // current becomes the previous state, and can be redone
    bool redo(PaletteEntries& current);
    void beginSession() { floorSize = undoSteps.size(); }
    void commitSession();
    bool cancelSession(PaletteEntries& current);  // false if the palette is already the one the session began on
    void clear();
private:
    std::vector<PaletteEntries> undoSteps;
    std::vector<PaletteEntries> redoSteps;
    size_t floorSize = 0;
};

#endif // PALETTEMODEL_H
