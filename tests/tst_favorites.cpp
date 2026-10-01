#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include "treewidget.h"
#include "treewidgetdelegate.h"
#include "ui_elements/favoritestar.h"

/* Favourite ditherers: the order of the list, the star's drawing and animation, and the list itself - a click on
 * the star toggles the favourite without selecting the ditherer, favourites move to the top in the order added. */
class TestFavorites : public QObject {
    Q_OBJECT

    static void fill(TreeWidget& tree, int count) {
        for (int i = 0; i < count; i++) {
            tree.addTreeItem(ERR, static_cast<SubDitherType>(i), QString("Ditherer %1").arg(i));
        }
    }
    static QList<int> shownIds(const TreeWidget& tree) {
        QList<int> ids;
        for (int i = 0; i < tree.topLevelItemCount(); i++) {
            ids << tree.topLevelItem(i)->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt();
        }
        return ids;
    }
    static QPoint starOf(TreeWidget& tree, int row) {
        return TreeWidgetDelegate::starRect(tree.visualItemRect(tree.topLevelItem(row))).center();
    }
private slots:
    void orderKeepsTheOrderOfAdding() {
        const std::vector<int> ids = {10, 11, 12, 13, 14};
        QCOMPARE(favoriteOrder(ids, {}), ids);  // no favourite: the list as it is
        QCOMPARE(favoriteOrder(ids, {13, 10}), std::vector<int>({13, 10, 11, 12, 14}));  // not sorted
        QCOMPARE(favoriteOrder(ids, {99, 12}), std::vector<int>({12, 10, 11, 13, 14}));  // unknown ids ignored
        QList<int> favourites = {13, 10, 12};
        QVERIFY(!toggleFavorite(favourites, 10));  // removed: back to its place, the others keep their order
        QCOMPARE(favoriteOrder(ids, favourites), std::vector<int>({13, 12, 10, 11, 14}));
        QVERIFY(toggleFavorite(favourites, 11));   // added last
        QCOMPARE(favourites, QList<int>({13, 12, 11}));
    }

    void starPatterns() {
        int empty = 0;
        int filled = 0;
        for (int y = 0; y < FavoriteStar::GRID; y++) {
            for (int x = 0; x < FavoriteStar::GRID; x++) {
                QVERIFY(!FavoriteStar::EMPTY[y][x] || FavoriteStar::FILLED[y][x]);  // the outline is part of the star
                QCOMPARE(FavoriteStar::FILLED[y][x], FavoriteStar::FILLED[y][FavoriteStar::GRID - 1 - x]);  // symmetric
                empty += FavoriteStar::EMPTY[y][x];
                filled += FavoriteStar::FILLED[y][x];
            }
        }
        QVERIFY(filled > empty);
        QVERIFY(!FavoriteStar::EMPTY[3][3] && FavoriteStar::FILLED[3][3]);  // hollow, then full
    }

    void animationFillsFromTheCentreAndEndsFull() {
        double presence = 0.0;
        double scale = 0.0;
        // filling: at the end every square of the filled star is there, at rest size
        for (int y = 0; y < FavoriteStar::GRID; y++) {
            for (int x = 0; x < FavoriteStar::GRID; x++) {
                FavoriteStar::square(x, y, false, true, 1.0, &presence, &scale);
                QCOMPARE(presence > 0.99, FavoriteStar::FILLED[y][x]);
                QVERIFY(std::abs(scale - 0.72) < 1e-9);
            }
        }
        // progressive: half way, the centre is further than an inner square off-centre
        double centre = 0.0;
        double side = 0.0;
        FavoriteStar::square(3, 3, false, true, 0.4, &centre, &scale);
        FavoriteStar::square(2, 2, false, true, 0.4, &side, &scale);  // inside the star, off-centre
        QVERIFY(centre > side);
        QVERIFY(scale > 0.72);  // squares swell during the animation
        // emptying is the same backwards, and ends as the outline
        FavoriteStar::square(3, 3, true, false, 1.0, &presence, &scale);
        QCOMPARE(presence, 0.0);
        FavoriteStar::square(3, 0, true, false, 1.0, &presence, &scale);
        QCOMPARE(presence, 1.0);
        // it paints something, in both states
        for (const bool favourite : {false, true}) {
            QImage image(28, 28, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            FavoriteStar::paint(&painter, QRectF(0, 0, 28, 28), favourite, favourite, 1.0, Qt::white, 0.3);
            painter.end();
            int lit = 0;
            for (int y = 0; y < 28; y++) for (int x = 0; x < 28; x++) lit += qAlpha(image.pixel(x, y)) > 0;
            QVERIFY(lit > 20);
        }
    }

    void starClickTogglesWithoutSelecting() {
        TreeWidget tree(nullptr);
        tree.setColumnCount(1);
        tree.resize(260, 300);
        fill(tree, 20);
        tree.setItemActive(0);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        QSignalSpy pressed(&tree, &QTreeWidget::itemPressed);
        QSignalSpy changed(&tree, &TreeWidget::favoritesChanged);
        QTreeWidgetItem* selected = tree.currentItem();

        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, starOf(tree, 5));
        QCOMPARE(pressed.count(), 0);           // the ditherer is not selected
        QCOMPARE(changed.count(), 1);
        QCOMPARE(tree.favorites(), QList<int>({5}));
        QCOMPARE(tree.currentItem(), selected);
        QCOMPARE(shownIds(tree).first(), 0);    // the row moves once the star has finished
        QTRY_COMPARE_WITH_TIMEOUT(shownIds(tree).first(), 5, 2000);
        QCOMPARE(tree.shownFavoriteCount(), 1);
        QCOMPARE(tree.currentItem(), selected);  // still the same ditherer selected

        // a second favourite goes below the first; the selected ditherer can be one
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, starOf(tree, 1));  // ditherer 0
        QTRY_COMPARE_WITH_TIMEOUT(shownIds(tree).mid(0, 3), QList<int>({5, 0, 1}), 2000);
        QCOMPARE(tree.currentItem(), selected);
        QCOMPARE(tree.currentItem()->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt(), 0);

        // removing the first: back to its place, the other favourite keeps its own
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, starOf(tree, 0));
        QTRY_COMPARE_WITH_TIMEOUT(shownIds(tree).mid(0, 7), QList<int>({0, 1, 2, 3, 4, 5, 6}), 2000);
        QCOMPARE(tree.favorites(), QList<int>({0}));
        QCOMPARE(pressed.count(), 0);

        // a click on the name still selects
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualItemRect(tree.topLevelItem(3)).topLeft() + QPoint(30, 5));
        QCOMPARE(pressed.count(), 1);
    }

    void restoredFavoritesAndNoneLooksAsBefore() {
        TreeWidget tree(nullptr);
        tree.setColumnCount(1);
        fill(tree, 8);
        tree.setFavorites({});
        QCOMPARE(shownIds(tree), QList<int>({0, 1, 2, 3, 4, 5, 6, 7}));
        QCOMPARE(tree.shownFavoriteCount(), 0);  // no separator
        tree.setFavorites({6, 1003, 2, 6});      // an id of the colour list and a duplicate are ignored
        QCOMPARE(tree.favorites(), QList<int>({6, 2}));
        QCOMPARE(shownIds(tree), QList<int>({6, 2, 0, 1, 3, 4, 5, 7}));
        QCOMPARE(tree.shownFavoriteCount(), 2);
        tree.setItemActive(0);  // the first ditherer as built, wherever it is shown
        QCOMPARE(tree.currentItem()->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt(), 0);
    }

    void scrollDoesNotJump() {
        TreeWidget tree(nullptr);
        tree.setColumnCount(1);
        tree.resize(260, 120);
        fill(tree, 60);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        tree.verticalScrollBar()->setValue(tree.verticalScrollBar()->maximum() / 2);
        const int scroll = tree.verticalScrollBar()->value();
        QVERIFY(scroll > 0);
        QTreeWidgetItem* under = tree.itemAt(QPoint(20, 10));
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                          TreeWidgetDelegate::starRect(tree.visualItemRect(under)).center());
        QTRY_COMPARE_WITH_TIMEOUT(tree.shownFavoriteCount(), 1, 2000);
        QCOMPARE(tree.verticalScrollBar()->value(), scroll);
    }
};

QObject* newTestFavorites() { return new TestFavorites; }
#include "tst_favorites.moc"
