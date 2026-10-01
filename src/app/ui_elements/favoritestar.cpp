#include "favoritestar.h"
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace FavoriteStar {

const Pattern EMPTY = {{
    {0, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 1, 0, 0},
    {1, 1, 0, 0, 0, 1, 1},
    {0, 1, 0, 0, 0, 1, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {1, 1, 0, 0, 0, 1, 1},
}};

const Pattern FILLED = {{
    {0, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {1, 1, 1, 1, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {1, 1, 0, 0, 0, 1, 1},
}};

namespace {
constexpr double REST_SCALE = 0.72;  // a square fills 72 % of its cell at rest
constexpr double POP = 0.38;         // and swells by this much half way through the animation
constexpr double SPREAD = 0.45;      // the last inner square starts this late (fraction of the animation)

double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double filledness(const bool from, const bool to, const double t) {
    /* 0 = empty star, 1 = filled, along the animation */
    if (from == to) {
        return to ? 1.0 : 0.0;
    }
    return to ? t : 1.0 - t;
}
}  // namespace

void square(const int x, const int y, const bool from, const bool to, const double t, double* presence, double* scale) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    *scale = REST_SCALE + (from != to ? std::sin(clamped * std::numbers::pi) * POP : 0.0);
    if (!FILLED[y][x]) {
        *presence = 0.0;
    } else if (EMPTY[y][x]) {
        *presence = 1.0;  // the outline is there in both states
    } else {
        // inside: appears from the centre outwards when filling, and the other way round when emptying
        const double distance = std::hypot(x - GRID / 2, y - GRID / 2) / 2.5;
        const double delay = std::min(distance, 1.0) * SPREAD;
        *presence = ease((filledness(from, to, clamped) - delay) / (1.0 - SPREAD));
    }
}

void paint(QPainter* painter, const QRectF& rect, const bool from, const bool to, const double t, const QColor& ink,
           const double emptyOpacity) {
    const double cell = std::min(rect.width(), rect.height()) / GRID;
    const QPointF origin(rect.center().x() - cell * GRID / 2.0, rect.center().y() - cell * GRID / 2.0);
    const double opacity = emptyOpacity + (1.0 - emptyOpacity) * ease(filledness(from, to, std::clamp(t, 0.0, 1.0)));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    for (int y = 0; y < GRID; y++) {
        for (int x = 0; x < GRID; x++) {
            double presence = 0.0;
            double scale = 0.0;
            square(x, y, from, to, t, &presence, &scale);
            if (presence < 0.04) {
                continue;
            }
            const double size = cell * scale * (0.4 + 0.6 * presence);  // grows in as it appears
            const QPointF c(origin.x() + (x + 0.5) * cell, origin.y() + (y + 0.5) * cell);
            QColor colour = ink;
            colour.setAlphaF(static_cast<float>(ink.alphaF() * opacity * presence));
            painter->setBrush(colour);
            painter->drawRoundedRect(QRectF(c.x() - size / 2.0, c.y() - size / 2.0, size, size), size * 0.14, size * 0.14);
        }
    }
    painter->restore();
}

}  // namespace FavoriteStar

std::vector<int> favoriteOrder(const std::vector<int>& ids, const QList<int>& favorites) {
    std::vector<int> order;
    order.reserve(ids.size());
    for (const int favorite : favorites) {
        if (std::find(ids.begin(), ids.end(), favorite) != ids.end() &&
            std::find(order.begin(), order.end(), favorite) == order.end()) {
            order.push_back(favorite);
        }
    }
    for (const int id : ids) {
        if (!favorites.contains(id)) {
            order.push_back(id);
        }
    }
    return order;
}

bool toggleFavorite(QList<int>& favorites, const int id) {
    if (favorites.removeAll(id) > 0) {
        return false;
    }
    favorites.append(id);
    return true;
}
