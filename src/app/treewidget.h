#pragma once
#ifndef TREEWIDGET_H
#define TREEWIDGET_H

#include "enums.h"
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QTreeWidget>
#include <QMenu>

constexpr int ITEM_DATA_DONE = 0;
constexpr int ITEM_DATA_COUNT = 1;
constexpr int ITEM_DATA_DTYPE = 2;
constexpr int ITEM_DATA_DSUBTYPE = 3;
// the same, on column 0 where the delegate can read them: place in the list as built, and SubDitherType
constexpr int ROLE_NATURAL_ROW = Qt::UserRole + 1;
constexpr int ROLE_DITHER_ID = Qt::UserRole + 2;

class TreeWidget final : public QTreeWidget {
    Q_OBJECT
signals:
    void itemChangedSignal(QTreeWidgetItem* item);
    void batchDitherSignal();
    void favoritesChanged();  // a star was clicked
public:
    /* favourites: the star at the right of each row. Favourites go to the top of the list, in the order they were
     * added, above a thin line; the others keep their own order. Clicking the star never selects the ditherer. */
    void setFavorites(const QList<int>& ids);  // SubDitherType values; those of the other list are ignored
    [[nodiscard]] QList<int> favorites() const { return favoriteIds; }
    [[nodiscard]] bool isFavorite(int id) const { return favoriteIds.contains(id); }
    void starState(int id, bool* from, bool* to, double* t) const;  // for the delegate: t = 1 at rest
    [[nodiscard]] int shownFavoriteCount() const { return favoritesOnTop; }  // rows above the separator
    void toggleFavoriteItem(QTreeWidgetItem* item);
    /* methods */
    explicit TreeWidget(QWidget*);
    void handleKeyPressEvent(QKeyEvent* event);
    void mousePressEvent(QMouseEvent* event) override;
    void clearAllDitherFlags() const;
    void setItemActive(int index);
    void changeDitherer(QTreeWidgetItem* item);
    QTreeWidgetItem* addTreeItem(DitherType dt, SubDitherType num, const QString &title);
    QString getCurrentDitherFileName() const;
    void setCurrentItemDitherFlag(bool value) const;
    void setValue(SubDitherType key1, SettingKey key2, const QVariant& value);
    QVariant getValue(SubDitherType key1, SettingKey key2);
protected:
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool viewportEvent(QEvent* event) override;  // the star's tool tip
private:
    /* attributes */
    QList<int> favoriteIds;
    int favoritesOnTop = 0;
    struct StarAnimation {
        bool from = false;
        bool to = false;
        QElapsedTimer clock;
    };
    QHash<int, StarAnimation> starAnimations;
    QTimer starTimer;      // repaints while a star moves
    QTimer reorderTimer;   // the rows move once the star has finished
    bool starPressed = false;
    [[nodiscard]] bool onStar(const QPoint& pos, QTreeWidgetItem** item = nullptr) const;
    void applyFavoriteOrder();
    QMenu* menu;
    QHash<SubDitherType, QHash<SettingKey, QVariant>> settings;
    int currentDitherNumber{};
    DitherType currentDitherType;
    SubDitherType currentSubDitherType;
    int item_count = 0;
    /* methods */
    void showContextMenuSlot(const QPoint& pos) const;
public slots:
    void treeWidgetItemChangedSlot(QTreeWidgetItem* item, int);
private slots:
//    void batchDitherRequestedSlot(); // TODO temporary disabled
};

#endif  // TREEWIDGET_H
