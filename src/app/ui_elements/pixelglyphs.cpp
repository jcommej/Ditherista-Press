#include "pixelglyphs.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace PixelGlyphs {

namespace {
double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double snap(const double value, const double devicePixelRatio) {
    return std::round(value * devicePixelRatio) / devicePixelRatio;
}

struct Grid {  // where pixel (0, 0) of a glyph goes, and the size of one pixel
    double x;
    double y;
    double cell;
};

Grid place(const QRectF& box, const int columns, const int rows, const double devicePixelRatio) {
    const double cell = cellSize(box, columns, rows, devicePixelRatio);
    return {snap(box.center().x() - cell * columns / 2.0, devicePixelRatio),
            snap(box.center().y() - cell * rows / 2.0, devicePixelRatio), cell};
}

void pixels(QPainter* painter, const Grid& grid, const double x, const double y, const double width,
            const double height, const QColor& ink) {
    painter->fillRect(QRectF(grid.x + x * grid.cell, grid.y + y * grid.cell, width * grid.cell, height * grid.cell), ink);
}

constexpr int CROSS[7][7] = {
    {1, 1, 0, 0, 0, 1, 1},
    {1, 1, 1, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 1, 1, 1, 0},
    {1, 1, 1, 0, 1, 1, 1},
    {1, 1, 0, 0, 0, 1, 1},
};

constexpr int DOT[8][8] = {
    {0, 0, 1, 1, 1, 1, 0, 0},
    {0, 1, 1, 1, 1, 1, 1, 0},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0, 0},
};

bool inDot(const int x, const int y) {
    return x >= 0 && x < 8 && y >= 0 && y < 8 && DOT[y][x];
}

bool dotRing(const int x, const int y) {
    return inDot(x, y) && (!inDot(x - 1, y) || !inDot(x + 1, y) || !inDot(x, y - 1) || !inDot(x, y + 1));
}

QColor mix(const QColor& a, const QColor& b, const double t) {
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
                            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}
}  // namespace

double cellSize(const QRectF& box, const int columns, const int rows, const double devicePixelRatio) {
    const double fit = std::min(box.width() / columns, box.height() / rows) * devicePixelRatio;
    return std::max(1.0, std::floor(fit)) / devicePixelRatio;
}

void paintLock(QPainter* painter, const QRectF& box, const double open, const QColor& ink, const double devicePixelRatio) {
    const Grid grid = place(box, 6, 7, devicePixelRatio);
    const double o = ease(open);
    const double lift = snap(o * grid.cell, devicePixelRatio) / grid.cell;  // in glyph pixels, on the screen grid
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    // body, with a keyhole
    pixels(painter, grid, 0, 3, 6, 2, ink);
    pixels(painter, grid, 0, 5, 2, 1, ink);
    pixels(painter, grid, 4, 5, 2, 1, ink);
    pixels(painter, grid, 0, 6, 6, 1, ink);
    // shackle: the arch rises, the right leg stays in the body, the left one comes out of it
    pixels(painter, grid, 1, -lift, 4, 1, ink);
    pixels(painter, grid, 4, 1 - lift, 1, 2 + lift, ink);
    const double leftLeg = std::round((2.0 - o) * grid.cell * devicePixelRatio) / devicePixelRatio / grid.cell;
    pixels(painter, grid, 1, 1 - lift, 1, leftLeg, ink);
    painter->restore();
}

bool crossPixelShown(const int x, const int y, const double t) {
    if (x < 0 || x >= 7 || y < 0 || y >= 7 || !CROSS[y][x]) {
        return false;
    }
    // out in a scattered order during the first 45 %, back in the reverse order after
    const double threshold = t <= 0.0 || t >= 1.0 ? 0.0 : t < 0.45 ? ease(t / 0.45) : 1.0 - ease((t - 0.45) / 0.55);
    return ((x * 7 + y * 13) % 17) / 17.0 >= threshold;
}

void paintCross(QPainter* painter, const QRectF& box, const double t, const QColor& ink, const double devicePixelRatio) {
    const Grid grid = place(box, 7, 7, devicePixelRatio);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 7; x++) {
            if (crossPixelShown(x, y, t)) {
                pixels(painter, grid, x, y, 1, 1, ink);
            }
        }
    }
    painter->restore();
}

bool statusDotPixel(const int x, const int y, const double fill) {
    if (!inDot(x, y)) {
        return false;
    }
    return dotRing(x, y) || (x + 0.5) / 8.0 <= fill;  // the inside fills from left to right
}

void paintStatusDot(QPainter* painter, const QRectF& box, const double fill, const QColor& empty, const QColor& full,
                    const double devicePixelRatio) {
    const Grid grid = place(box, 8, 8, devicePixelRatio);
    const double f = std::clamp(fill, 0.0, 1.0);
    const QColor ring = mix(empty, full, ease(f));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            if (statusDotPixel(x, y, f)) {
                pixels(painter, grid, x, y, 1, 1, dotRing(x, y) ? ring : full);
            }
        }
    }
    painter->restore();
}

void paintHandle(QPainter* painter, const QRectF& box, const QColor& ink, const double devicePixelRatio) {
    const Grid grid = place(box, 5, 5, devicePixelRatio);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    for (const int y : {0, 2, 4}) {
        pixels(painter, grid, 0, y, 5, 1, ink);
    }
    painter->restore();
}

}  // namespace PixelGlyphs
