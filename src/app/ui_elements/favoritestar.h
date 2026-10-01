#pragma once
#ifndef FAVORITESTAR_H
#define FAVORITESTAR_H

#include <QColor>
#include <QList>
#include <QRectF>
#include <array>
#include <vector>

class QPainter;

/* Favourite ditherers: the star drawn at the right of each row of the ditherer lists, and the order of the rows.
 * The star is a 7 x 7 matrix of small squares, in the language of the render control's glyph. Favourites are kept
 * as SubDitherType values - the ditherers' stable internal ids, as presets use them - in the order they were added. */
namespace FavoriteStar {
constexpr int GRID = 7;
using Pattern = std::array<std::array<bool, GRID>, GRID>;
extern const Pattern EMPTY;  // the outline of a five-pointed star
extern const Pattern FILLED;

constexpr double ANIMATION_MS = 300.0;

/* paints the star in `rect`. `from` / `to`: favourite before and after the click; `t` 0..1 the animation (1 = at
 * rest). Filling, the inside of the star appears from its centre outwards, the squares swelling a little half way;
 * emptying plays the same backwards. `ink`: colour of a filled star; an empty one is drawn fainter with it,
 * `emptyOpacity`. */
void paint(QPainter* painter, const QRectF& rect, bool from, bool to, double t, const QColor& ink, double emptyOpacity);

// presence (0..1) and scale of one square at `t`, as painted
void square(int x, int y, bool from, bool to, double t, double* presence, double* scale);
}

// `ids` in display order: the favourites first, in the order they were added, then the others in their own order
std::vector<int> favoriteOrder(const std::vector<int>& ids, const QList<int>& favorites);
// adds `id` at the end of the favourites, or removes it; true if it is a favourite now
bool toggleFavorite(QList<int>& favorites, int id);

#endif // FAVORITESTAR_H
