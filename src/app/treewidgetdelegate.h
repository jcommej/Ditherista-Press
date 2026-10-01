#pragma once
#ifndef TREEWIDGETDELEGATE_H
#define TREEWIDGETDELEGATE_H

#include <QStyledItemDelegate>

class TreeWidgetDelegate final : public QStyledItemDelegate {
public:
    /* attributes */
    int selected_row = 0; // place in the list as built (ROLE_NATURAL_ROW) of the selected item - rows move with favourites
    /* methods */
    explicit TreeWidgetDelegate(QObject* parent);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    static QRect starRect(const QRect& row);  // the favourite star, left of the dithered / not dithered dot
private:
    /* attributes */
    QPixmap ready;
    QPixmap notReady;
};

#endif  // TREEWIDGETDELEGATE_H
