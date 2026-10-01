#pragma once
#ifndef PIXELGLYPHS_H
#define PIXELGLYPHS_H

#include <QColor>
#include <QRectF>

class QPainter;

/* Small glyphs drawn in square pixels, in the language of the render control: the padlock, the reset cross and the
 * dithered / not dithered dot of the ditherer lists. One "pixel" is a whole number of screen pixels (at least one),
 * so the glyphs stay sharp; each is centred in `box`.
 * `t` / `open` / `fill` run 0..1 along an animation; the callers animate them (QTimer, only while something moves). */
namespace PixelGlyphs {
// side of one glyph pixel for a `columns` x `rows` glyph in `box`, in logical pixels, snapped to the device pixels
double cellSize(const QRectF& box, int columns, int rows, double devicePixelRatio);

/* padlock, 6 x 7 pixels: `open` 0 = locked; towards 1 the shackle rises by one pixel and its left leg leaves the
 * body, the right one staying in it - the open padlock */
void paintLock(QPainter* painter, const QRectF& box, double open, const QColor& ink, double devicePixelRatio);
constexpr double LOCK_MS = 360.0;

/* reset cross, 7 x 7 pixels: along `t` its pixels go out one by one in a scattered order, then come back; t = 0 or 1
 * is the whole cross */
void paintCross(QPainter* painter, const QRectF& box, double t, const QColor& ink, double devicePixelRatio);
bool crossPixelShown(int x, int y, double t);  // as painted
constexpr double CROSS_MS = 300.0;

/* dithered status, a round dot of 8 x 8 pixels: `fill` 0 = a hollow ring (nothing rendered), 1 = a full dot (a
 * result is cached); in between the inside fills from left to right */
void paintStatusDot(QPainter* painter, const QRectF& box, double fill, const QColor& empty, const QColor& full,
                    double devicePixelRatio);
bool statusDotPixel(int x, int y, double fill);  // as painted: is pixel (x, y) drawn at this fill
constexpr double STATUS_MS = 420.0;
}

#endif // PIXELGLYPHS_H
