#include "palettemodel.h"
#include "color/colorspace.h"
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace {

double unit(std::mt19937& generator) {
    /* uniform in [0, 1): from the raw 32-bit output, which the standard fixes exactly, so a seed gives the same
     * palette with any compiler (std::uniform_real_distribution does not promise that) */
    return generator() / 4294967296.0;
}

double spread(std::mt19937& generator, const double amount) {
    return (unit(generator) * 2.0 - 1.0) * amount;
}

}  // namespace

bool PaletteModel::canAdd(const PaletteEntries& palette) {
    return static_cast<int>(palette.size()) < MAX_COLOURS;
}

bool PaletteModel::canRemove(const PaletteEntries& palette, const int index) {
    return index >= 0 && index < static_cast<int>(palette.size()) && !palette[static_cast<size_t>(index)].locked &&
           static_cast<int>(palette.size()) > MIN_COLOURS;
}

bool PaletteModel::add(PaletteEntries& palette, const QRgb colour) {
    if (!canAdd(palette)) {
        return false;
    }
    palette.push_back({colour | 0xFF000000u, false});
    return true;
}

bool PaletteModel::remove(PaletteEntries& palette, const int index) {
    if (!canRemove(palette, index)) {
        return false;
    }
    palette.erase(palette.begin() + index);
    return true;
}

void PaletteModel::randomize(PaletteEntries& palette, const std::uint32_t seed) {
    std::mt19937 generator(seed);
    for (PaletteEntry& entry : palette) {
        // draw for every entry, locked or not, so locking one colour does not change the others' variation
        const double dL = spread(generator, 10.0), da = spread(generator, 25.0), db = spread(generator, 25.0);
        if (entry.locked) {
            continue;
        }
        const Lab lab = rgbToLab(entry.colour);
        entry.colour = labToRgb(clampToSrgbGamut({lab.L + dL, lab.a + da, lab.b + db}));
    }
}

bool PaletteModel::randomizeOne(PaletteEntries& palette, const int index, const std::uint32_t seed) {
    if (index < 0 || index >= static_cast<int>(palette.size()) || palette[static_cast<size_t>(index)].locked) {
        return false;
    }
    std::mt19937 generator(seed);
    const auto byte = [&generator]() { return static_cast<int>(unit(generator) * 256.0); };
    const int r = byte(), g = byte(), b = byte();
    palette[static_cast<size_t>(index)].colour = qRgb(r, g, b);
    return true;
}

QRgb PaletteModel::leastRepresented(const QImage& image, const PaletteEntries& palette) {
    const QRgb grey = qRgb(128, 128, 128);
    if (image.isNull() || palette.empty()) {
        return grey;
    }
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    // at most about 65 000 samples on a regular grid, grouped into 16 x 16 x 16 colour bins
    const int step = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(argb.width()) * argb.height() / 65536.0)));
    struct Bin { double r = 0, g = 0, b = 0; long count = 0; };
    std::vector<Bin> bins(4096);
    for (int y = 0; y < argb.height(); y += step) {
        const QRgb* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); x += step) {
            const QRgb p = row[x];
            if (qAlpha(p) == 0) {
                continue;
            }
            Bin& bin = bins[static_cast<size_t>((qRed(p) >> 4) << 8 | (qGreen(p) >> 4) << 4 | (qBlue(p) >> 4))];
            bin.r += qRed(p); bin.g += qGreen(p); bin.b += qBlue(p);
            bin.count++;
        }
    }
    std::vector<Lab> inks;
    for (const PaletteEntry& entry : palette) {
        inks.push_back(rgbToLab(entry.colour));
    }
    double best = -1.0;
    QRgb result = grey;
    for (const Bin& bin : bins) {
        if (bin.count == 0) {
            continue;
        }
        const QRgb mean = qRgb(static_cast<int>(std::lround(bin.r / bin.count)), static_cast<int>(std::lround(bin.g / bin.count)),
                               static_cast<int>(std::lround(bin.b / bin.count)));
        const Lab lab = rgbToLab(mean);
        double nearest = std::numeric_limits<double>::max();
        for (const Lab& ink : inks) {
            nearest = std::min(nearest, std::hypot(lab.L - ink.L, lab.a - ink.a, lab.b - ink.b));
        }
        const double score = nearest * static_cast<double>(bin.count);
        if (score > best) {
            best = score;
            result = mean;
        }
    }
    return result;
}

QString PaletteModel::toPaintNet(const PaletteEntries& palette, const QString& name) {
    QStringList lines{";paint.net Palette File", QString(";Palette Name: %1").arg(name),
                      QString(";Colors: %1").arg(palette.size())};
    QStringList locked;
    for (size_t i = 0; i < palette.size(); i++) {
        if (palette[i].locked) {
            locked << QString::number(i + 1);
        }
    }
    if (!locked.isEmpty()) {
        lines << QString(";Locked: %1").arg(locked.join(","));
    }
    for (const PaletteEntry& entry : palette) {
        lines << QString("FF%1").arg(entry.colour & 0xFFFFFFu, 6, 16, QLatin1Char('0')).toUpper();
    }
    return lines.join("\n") + "\n";
}

bool PaletteModel::fromPaintNet(const QString& text, PaletteEntries* palette, QString* error, bool* truncated,
                                bool* tooFew) {
    if (tooFew) *tooFew = false;
    static const QRegularExpression colourLine("^([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
    static const QRegularExpression lockedLine("^;\\s*Locked:\\s*([0-9,\\s]*)$", QRegularExpression::CaseInsensitiveOption);
    PaletteEntries entries;
    std::vector<int> locked;
    bool dropped = false;
    for (const QString& raw : text.split('\n')) {
        const QString line = raw.trimmed();
        if (const QRegularExpressionMatch lock = lockedLine.match(line); lock.hasMatch()) {
            for (const QString& n : lock.captured(1).split(',', Qt::SkipEmptyParts)) {
                locked.push_back(n.trimmed().toInt() - 1);
            }
        } else if (colourLine.match(line).hasMatch()) {
            if (static_cast<int>(entries.size()) >= MAX_COLOURS) {
                dropped = true;
                continue;
            }
            QRgb colour;
            parseHexColour(line, &colour);
            entries.push_back({colour, false});
        }
        // anything else - comments, blank lines, a header another program wrote - is skipped, as upstream did
    }
    if (static_cast<int>(entries.size()) < MIN_COLOURS) {
        if (error) *error = QObject::tr("a palette needs at least %1 colours").arg(MIN_COLOURS);
        if (tooFew) *tooFew = true;
        return false;
    }
    for (const int i : locked) {
        if (i >= 0 && i < static_cast<int>(entries.size())) {
            entries[static_cast<size_t>(i)].locked = true;
        }
    }
    if (truncated) *truncated = dropped;
    *palette = entries;
    return true;
}

/********************
 * PALETTE HISTORY  *
 ********************/

void PaletteHistory::record(const PaletteEntries& before) {
    undoSteps.push_back(before);
    redoSteps.clear();
    if (undoSteps.size() > MAX_STEPS) {
        undoSteps.erase(undoSteps.begin());
        floorSize = floorSize > 0 ? floorSize - 1 : 0;
    }
}

bool PaletteHistory::undo(PaletteEntries& current) {
    if (!canUndo()) {
        return false;
    }
    redoSteps.push_back(current);
    current = undoSteps.back();
    undoSteps.pop_back();
    return true;
}

bool PaletteHistory::redo(PaletteEntries& current) {
    if (!canRedo()) {
        return false;
    }
    undoSteps.push_back(current);
    current = redoSteps.back();
    redoSteps.pop_back();
    return true;
}

void PaletteHistory::clear() {
    undoSteps.clear();
    redoSteps.clear();
    floorSize = 0;
}
