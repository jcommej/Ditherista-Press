#include "treewidgetdelegate.h"
#include "treewidget.h"
#include "ui_elements/favoritestar.h"
#include "ui_elements/pixelglyphs.h"
#include <QPainter>

// https://stackoverflow.com/questions/7175333/how-to-create-delegate-for-qtreewidget

constexpr int ICON_DIMENSION_PX = 16;
constexpr int STAR_PX = 14;

TreeWidgetDelegate::TreeWidgetDelegate(QObject* parent) : QStyledItemDelegate(parent) {
};

QRect TreeWidgetDelegate::starRect(const QRect& row) {
    const int right = row.x() + row.width() - ICON_DIMENSION_PX - 4 - 5;  // gap to the dithered / not dithered dot
    return {right - STAR_PX, row.y() + (row.height() - STAR_PX) / 2, STAR_PX, STAR_PX};
}

void TreeWidgetDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    /* note: we have to use the 'selected_row' helper variable instead of using
     * option.state & QStyle::State_Selected to determine if a row should be drawn as
     * selected, because in rare cases the delegate gets confused about what is actually selected
     * due to the auto-expanding stacked widget that is jostling things around (the treewidget
     * itself isn't confused; it always knows what really has been clicked - it's just the delegate that's
     * the problem... */
    painter->setRenderHint(QPainter::Antialiasing, true);
    // draw blue background for currently selected row
    const bool selected = index.data(ROLE_NATURAL_ROW).toInt() == selected_row;
    if(selected && option.state & QStyle::State_Enabled) {  // this was using option.state & QStyle::State_Selected
        painter->setPen(Qt::transparent);
        painter->setBrush(option.palette.highlight());
        painter->drawRoundedRect(option.rect, 6, 6);
    }
    // draw row contents; the name stops before the star
    const QRect star = starRect(option.rect);
    QStyleOptionViewItem itemOption(option);
    itemOption.rect.setRight(star.left() - 4);
    if(option.state & QStyle::State_Enabled) {
        itemOption.state &= QStyle::State_Enabled;
    } else {
        itemOption.state = QStyle::State_None;
    }
    QStyledItemDelegate::paint(painter, itemOption, index);
    // dithered / not dithered: a full pixel dot when a result is cached, a hollow ring when not (not clickable)
    int dim = ICON_DIMENSION_PX; // image dimensions
    int ypos = option.rect.y() + static_cast<int>((option.rect.height() - dim) * 0.5f);
    int xpos = option.rect.width() - dim - 4;
    const TreeWidget* tree = qobject_cast<const TreeWidget*>(parent());
    const double ratio = painter->device() != nullptr ? painter->device()->devicePixelRatioF() : 1.0;
    const double fill = tree != nullptr ? tree->doneFill(index.data(ROLE_DITHER_ID).toInt())
                                        : (index.data(Qt::UserRole).toBool() ? 1.0 : 0.0);
    PixelGlyphs::paintStatusDot(painter, QRectF(xpos, ypos, dim, dim), fill, QColor(0x6a, 0x6a, 0x6a),
                                QColor(0xb2, 0xb2, 0xb2), ratio);
    if (tree == nullptr) {
        return;
    }
    // favourite star: faint when empty, a little less under the pointer
    bool from = false;
    bool to = false;
    double t = 1.0;
    tree->starState(index.data(ROLE_DITHER_ID).toInt(), &from, &to, &t);
    const bool enabled = option.state & QStyle::State_Enabled;
    const QColor ink = selected && enabled ? QColor(255, 255, 255) : QColor(205, 205, 205, enabled ? 255 : 110);
    const double emptyOpacity = option.state & QStyle::State_MouseOver ? 0.6 : 0.3;
    FavoriteStar::paint(painter, star, from, to, t, ink, emptyOpacity);
    // a thin line under the last favourite
    const int favourites = tree->shownFavoriteCount();
    if (favourites > 0 && index.row() == favourites - 1 && index.row() + 1 < tree->topLevelItemCount()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(QColor(255, 255, 255, 60), 1));
        const int y = option.rect.bottom();
        painter->drawLine(option.rect.left() + 8, y, option.rect.right() - 8, y);
        painter->restore();
    }
}
