#include "treewidget.h"
#include "treewidgetdelegate.h"
#include "ui_elements/favoritestar.h"
#include "ui_elements/pixelglyphs.h"
#include <QHelpEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QToolTip>
#include <algorithm>

/*
 * TreeWidget manages all info about the currently selected ditherer.
 * It provides the global number of the currently selected ditherer,
 * the selected ditherer's short identifier (e.g. ERR) and it long identifier (e.g. ERR_FFS)
 * and the suggested default file name part that includes the ditherer's name and type.
 */

TreeWidget::TreeWidget(QWidget* parent) : QTreeWidget(parent) {
    /* Constructor */
    TreeWidgetDelegate* delegate = new TreeWidgetDelegate(this);
    setItemDelegate(delegate);
    connect(this, SIGNAL(itemPressed(QTreeWidgetItem*, int)), this, SLOT(treeWidgetItemChangedSlot(QTreeWidgetItem*, int)));
    // favourite stars and dithered dots: a few frames while one changes, nothing while all are still
    viewport()->setAttribute(Qt::WA_Hover);  // the star under the pointer shows a little more
    glyphTimer.setInterval(16);
    connect(&glyphTimer, &QTimer::timeout, this, [this]() {
        for (auto it = starAnimations.begin(); it != starAnimations.end();) {
            it = it.value().clock.elapsed() > FavoriteStar::ANIMATION_MS ? starAnimations.erase(it) : std::next(it);
        }
        for (auto it = doneAnimations.begin(); it != doneAnimations.end();) {
            it = it.value().clock.elapsed() > PixelGlyphs::STATUS_MS ? doneAnimations.erase(it) : std::next(it);
        }
        if (starAnimations.isEmpty() && doneAnimations.isEmpty()) {
            glyphTimer.stop();
        }
        viewport()->update();
    });
    reorderTimer.setSingleShot(true);
    reorderTimer.setInterval(static_cast<int>(FavoriteStar::ANIMATION_MS) + 30);
    connect(&reorderTimer, &QTimer::timeout, this, &TreeWidget::rebuildFavoriteRows);

    // TODO temporary disabled (batch dithering)
//    // context menu setup
//    menu = new QMenu(this);
//    QAction* action1 = new QAction(tr("Batch dither..."), this);
//    connect(action1, SIGNAL(triggered()), this, SLOT(batchDitherRequestedSlot()));
//    menu->addAction(action1);
}

void TreeWidget::mousePressEvent(QMouseEvent* event) {
    /* Determines the clicking behavior:
       left click to select items
       right click on currently selected item brings up a context menu
    */
    if (event->button() == Qt::RightButton) {
        // TODO temporary disabled (batch dithering)
        // showContextMenuSlot(event->pos());
    } else if (event->button() == Qt::LeftButton) {
        QTreeWidgetItem* item = nullptr;
        starPressed = onStar(event->pos(), &item);
        if (starPressed) {
            toggleFavoriteItem(item);  // not passed on: the ditherer is not selected
        } else {
            QTreeWidget::mousePressEvent(event);
        }
    }
    event->accept();
}

void TreeWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (starPressed) {
        starPressed = false;
        event->accept();
        return;
    }
    QTreeWidget::mouseReleaseEvent(event);
}

void TreeWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    /* a quick second click on a star is a second toggle, as two separate clicks would be */
    QTreeWidgetItem* item = nullptr;
    if (event->button() == Qt::LeftButton && onStar(event->pos(), &item)) {
        starPressed = true;
        toggleFavoriteItem(item);
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

bool TreeWidget::viewportEvent(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const QHelpEvent* help = static_cast<QHelpEvent*>(event);
        QTreeWidgetItem* item = nullptr;
        if (onStar(help->pos(), &item)) {
            const bool favourite = isFavorite(item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt());
            QToolTip::showText(help->globalPos(), favourite ? tr("Remove from favourites") : tr("Add to favourites"),
                               viewport(), TreeWidgetDelegate::starRect(visualItemRect(item)));
            return true;
        }
    }
    return QTreeWidget::viewportEvent(event);
}

bool TreeWidget::onStar(const QPoint& pos, QTreeWidgetItem** item) const {
    /* `pos` in the viewport; a little margin around the star makes it easier to hit */
    QTreeWidgetItem* under = itemAt(pos);
    if (under == nullptr || !TreeWidgetDelegate::starRect(visualItemRect(under)).adjusted(-3, -3, 3, 3).contains(pos)) {
        return false;
    }
    if (item != nullptr) {
        *item = under;
    }
    return true;
}

void TreeWidget::toggleFavoriteItem(QTreeWidgetItem* item) {
    const int id = item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt();
    const bool favourite = toggleFavorite(favoriteIds, id);
    StarAnimation& animation = starAnimations[id];
    animation.from = !favourite;
    animation.to = favourite;
    animation.clock.start();
    glyphTimer.start();
    reorderTimer.start();  // again if another star was clicked meanwhile: the rows move once, at the end
    emit favoritesChanged();
}

void TreeWidget::starState(const int id, bool* from, bool* to, double* t) const {
    const auto it = starAnimations.constFind(id);
    if (it == starAnimations.constEnd()) {
        *from = *to = isFavorite(id);
        *t = 1.0;
        return;
    }
    *from = it.value().from;
    *to = it.value().to;
    *t = std::min(1.0, static_cast<double>(it.value().clock.elapsed()) / FavoriteStar::ANIMATION_MS);
}

void TreeWidget::setFavorites(const QList<int>& ids) {
    favoriteIds.clear();
    for (const int id : ids) {
        if (originalItem(id) != nullptr && !favoriteIds.contains(id)) {
            favoriteIds.append(id);
        }
    }
    rebuildFavoriteRows();
}

QTreeWidgetItem* TreeWidget::originalItem(const int id) const {
    for (int i = 0; i < topLevelItemCount(); i++) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (!isFavoriteCopy(item) && item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() == id) {
            return item;
        }
    }
    return nullptr;
}

bool TreeWidget::isFavoriteCopy(const QTreeWidgetItem* item) {
    return item != nullptr && item->data(0, ROLE_FAVORITE_COPY).toBool();
}

void TreeWidget::rebuildFavoriteRows() {
    /* a copy of each favourite on top, in the order they were added; every ditherer also stays at its own place
     * below, so the list as built is always there. The selected ditherer stays selected, and the rows on screen stay
     * where they are (unless the list is scrolled to the top: then the favourites push it down, in view). */
    QTreeWidgetItem* current = currentItem();
    const int currentId = current != nullptr ? current->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() : -1;
    const bool currentWasCopy = isFavoriteCopy(current);
    // the first ditherer at its own place on screen: it should not move
    QTreeWidgetItem* anchor = nullptr;
    for (int i = 0; i < topLevelItemCount() && anchor == nullptr; i++) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (!isFavoriteCopy(item) && visualItemRect(item).bottom() >= 0) {
            anchor = item;
        }
    }
    const int anchorTop = anchor != nullptr ? visualItemRect(anchor).top() : 0;
    const bool atTop = verticalScrollBar()->value() == verticalScrollBar()->minimum();

    const QSignalBlocker blocker(this);  // the same ditherer stays selected: nothing to load or render
    for (int i = topLevelItemCount() - 1; i >= 0; i--) {
        if (isFavoriteCopy(topLevelItem(i))) {
            delete takeTopLevelItem(i);
        }
    }
    favoritesOnTop = 0;
    for (const int id : favoriteIds) {
        if (const QTreeWidgetItem* original = originalItem(id)) {
            QTreeWidgetItem* copy = original->clone();
            copy->setData(0, ROLE_FAVORITE_COPY, true);
            insertTopLevelItem(favoritesOnTop++, copy);
        }
    }
    // selection: the copy the user had selected if it is still a favourite, else the ditherer at its own place
    QTreeWidgetItem* select = current;
    if (currentWasCopy) {
        select = originalItem(currentId);
        for (int i = 0; i < favoritesOnTop; i++) {
            if (topLevelItem(i)->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() == currentId) {
                select = topLevelItem(i);
            }
        }
    }
    if (select != nullptr) {
        setCurrentItem(select);
        select->setSelected(true);
    }
    if (anchor != nullptr && !atTop) {
        const int moved = visualItemRect(anchor).top() - anchorTop;
        const int step = verticalScrollMode() == QAbstractItemView::ScrollPerItem
                             ? (moved + (moved >= 0 ? 1 : -1) * visualItemRect(anchor).height() / 2) / std::max(1, visualItemRect(anchor).height())
                             : moved;
        verticalScrollBar()->setValue(verticalScrollBar()->value() + step);
    }
    viewport()->update();
}

void TreeWidget::showContextMenuSlot(const QPoint& pos) const {
    /* User right clicked on a tileset -> show context menu */
    if (QTreeWidgetItem* rightClickItem = itemAt(pos); rightClickItem == currentItem()) {  // only trigger context menu at currently selected item
        menu->popup(mapToGlobal(pos));
    }
}

// TODO temporary disabled (batch dithering)
//void TreeWidget::batchDitherRequestedSlot() {
//    /* Gets the currently selected ditherer and initiates a batch render */
//    if (currentItem() != nullptr) {
//        emit batchDitherSignal();
//    }
//}

QString TreeWidget::getCurrentDitherFileName() const {
    /* Returns a human readable name of the currently selected ditherer */
    QString fileName = ditherTypeName[currentDitherType];
    if(!subDitherTypeName[currentSubDitherType].isEmpty()) {
        fileName += QString("_") + subDitherTypeName[currentSubDitherType];
    }
    fileName += "_dither";
    return fileName;
}

void TreeWidget::treeWidgetItemChangedSlot(QTreeWidgetItem* item, int) {
    /* User selected a different ditherer from the tree */
    changeDitherer(item);
    emit itemChangedSignal(item);
}

void TreeWidget::setItemActive(const int index) {
    /* Sets the specified ditherer as active and selected ditherer; `index`: its place in the list as built */
    QTreeWidgetItem* item = topLevelItem(index);
    for (int i = 0; i < topLevelItemCount(); i++) {  // the ditherer at its own place, below the favourites' copies
        if (!isFavoriteCopy(topLevelItem(i)) && topLevelItem(i)->data(ITEM_DATA_COUNT, Qt::UserRole).toInt() == index) {
            item = topLevelItem(i);
        }
    }
    item->setSelected(true);
    changeDitherer(item);
    setCurrentItem(item);
}

void TreeWidget::changeDitherer(QTreeWidgetItem* item) {
    /* shared functionality called from manually setting the active ditherer(setItemActive())
     * or via Slot(treeWidgetItemChangedSlot()) */
    TreeWidgetDelegate* id = static_cast<TreeWidgetDelegate*>(itemDelegate());
    id->selected_row = item->data(ITEM_DATA_COUNT, Qt::UserRole).toInt();
    currentDitherType = static_cast<DitherType>(item->data(ITEM_DATA_DTYPE, Qt::UserRole).toInt());
    currentSubDitherType = static_cast<SubDitherType>(item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt());
    currentDitherNumber = item->data(ITEM_DATA_COUNT, Qt::UserRole).toInt();
}

QTreeWidgetItem* TreeWidget::addTreeItem(const DitherType dt, const SubDitherType num, const QString& title) {
    /* Adds ditherers to the TreeWidget GUI item */
    QIcon icon;
    switch(dt) {
        case ERR_C:
        case ERR: icon = QIcon(":/resources/ico_error.svg"); break;
        case GRD_C:
        case GRD: icon = QIcon(":/resources/ico_grid.svg"); break;
        case DOT_C:
        case DOT: icon = QIcon(":/resources/ico_dot.svg"); break;
        case ORD_C:
        case ORD: icon = QIcon(":/resources/ico_ordered.svg"); break;
        case LIP_C:
        case LIP: icon = QIcon(":/resources/ico_dotlippens.svg"); break;
        case DBS_C:
        case DBS: icon = QIcon(":/resources/ico_dbs.svg"); break;
        case ALL_C:
        case ALL: icon = QIcon(":/resources/ico_allebach.svg"); break;
        case THR_C:
        case THR: icon = QIcon(":/resources/ico_threshold.svg"); break;
        case PAT_C:
        case PAT: icon = QIcon(":/resources/ico_pattern.svg"); break;
        case RIM_C:
        case RIM: icon = QIcon(":/resources/ico_riemersma.svg"); break;
        case VAR_C:
        case VAR: icon = QIcon(":/resources/ico_variable.svg"); break;
    }
    QTreeWidgetItem* item = new QTreeWidgetItem();
    item->setText(0, title);
    item->setIcon(0, icon);
    item->setData(ITEM_DATA_DONE, Qt::UserRole, false);
    item->setData(ITEM_DATA_COUNT, Qt::UserRole, item_count);  // overall number of the ditherer
    item->setData(ITEM_DATA_DTYPE, Qt::UserRole, dt);          // dither type: e.g. error diffusion
    item->setData(ITEM_DATA_DSUBTYPE, Qt::UserRole, num);      // sub-type: e.g. floyd-steinberg
    item->setData(0, ROLE_NATURAL_ROW, item_count);            // the same two, readable by the delegate
    item->setData(0, ROLE_DITHER_ID, num);
    addTopLevelItem(item);
    item_count++;
    return item;
}

void TreeWidget::clearAllDitherFlags() {
    /* Marks all dither slots as not having dithered anything yet. i.e. re-visiting the slot after the dither
       flag has been cleared will trigger a re-dither.
     */
    for(int i = 0; i < topLevelItemCount(); i++) {
        setDitherFlag(topLevelItem(i)->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt(), false);
    }
}

void TreeWidget::setDitherFlag(const int id, const bool done) {
    /* the dithered / not dithered dot of a ditherer, on its row and on its favourite copy; a change animates it */
    bool changed = false;
    for (int i = 0; i < topLevelItemCount(); i++) {
        QTreeWidgetItem* item = topLevelItem(i);
        if (item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() == id &&
            item->data(ITEM_DATA_DONE, Qt::UserRole).toBool() != done) {
            item->setData(ITEM_DATA_DONE, Qt::UserRole, done);
            changed = true;
        }
    }
    if (changed && isVisible()) {
        StatusAnimation& animation = doneAnimations[id];
        const double fill = animation.clock.isValid() && animation.clock.elapsed() < PixelGlyphs::STATUS_MS
                                ? doneFill(id) : (done ? 0.0 : 1.0);  // a reversal starts from where it was
        animation.from = fill;
        animation.to = done ? 1.0 : 0.0;
        animation.clock.start();
        glyphTimer.start();
    }
}

double TreeWidget::doneFill(const int id) const {
    /* 0 = hollow, 1 = full, along the animation */
    const auto it = doneAnimations.constFind(id);
    if (it != doneAnimations.constEnd()) {
        const double t = std::min(1.0, static_cast<double>(it.value().clock.elapsed()) / PixelGlyphs::STATUS_MS);
        const double eased = t * t * (3.0 - 2.0 * t);
        return it.value().from + (it.value().to - it.value().from) * eased;
    }
    const QTreeWidgetItem* item = originalItem(id);
    return item != nullptr && item->data(ITEM_DATA_DONE, Qt::UserRole).toBool() ? 1.0 : 0.0;
}

void TreeWidget::handleKeyPressEvent(QKeyEvent* event) {
    /* Qt's key press event. Allows the user to use cursor keys to go to the next/previous ditherer in the list */
    if(event->key() == Qt::Key_Down){
        if(QTreeWidgetItem* item = itemBelow(currentItem()); item != nullptr) {
            setCurrentItem(item);
            item->setSelected(true);
            emit itemPressed(item, 0);
        }
    } else if (event->key() == Qt::Key_Up) {
        if(QTreeWidgetItem* item = itemAbove(currentItem()); item != nullptr) {
            setCurrentItem(item);
            item->setSelected(true);
            emit itemPressed(item, 0);
        }
    } else {
        QTreeView::keyPressEvent(event);
    }
    event->accept();
}

void TreeWidget::setCurrentItemDitherFlag(const bool value) {
    setDitherFlag(currentItem()->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt(), value);
}

void TreeWidget::setValue(const SubDitherType key1, const SettingKey key2, const QVariant& value) {
    settings[key1][key2] = value;
}

QVariant TreeWidget::getValue(const SubDitherType key1, const SettingKey key2) {
    return settings[key1][key2];
}

QJsonObject TreeWidget::settingsJson() const {
    QJsonObject json;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        QJsonObject values;
        for (auto value = it.value().constBegin(); value != it.value().constEnd(); ++value) {
            if (value.value().isValid()) {
                values.insert(QString::number(value.key()), QJsonValue::fromVariant(value.value()));
            }
        }
        if (!values.isEmpty()) {
            json.insert(QString::number(it.key()), values);
        }
    }
    return json;
}

void TreeWidget::setSettingsJson(const QJsonObject& json) {
    for (const QString& subtype : json.keys()) {
        const QJsonObject values = json.value(subtype).toObject();
        for (const QString& key : values.keys()) {
            settings[static_cast<SubDitherType>(subtype.toInt())][static_cast<SettingKey>(key.toInt())] =
                values.value(key).toVariant();
        }
    }
}
