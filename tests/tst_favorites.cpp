#include <QtTest>
#include <functional>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QToolButton>
#include "treewidget.h"
#include "treewidgetdelegate.h"
#include "ui_elements/favoritestar.h"
#include "ui_elements/pixelbuttonglyph.h"
#include "ui_elements/pixelglyphs.h"

/* Favourite ditherers: the star's drawing and animation, and the list - a click on the star toggles the favourite
 * without selecting the ditherer; a copy of each favourite goes on top in the order added, the ditherer staying at
 * its own place too. */
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
    void toggleKeepsTheOrderOfAdding() {
        QList<int> favourites = {13, 10, 12};
        QVERIFY(!toggleFavorite(favourites, 10));  // removed, the others keep their order
        QCOMPARE(favourites, QList<int>({13, 12}));
        QVERIFY(toggleFavorite(favourites, 11));   // added last, not sorted
        QCOMPARE(favourites, QList<int>({13, 12, 11}));
    }

    void starIsTheDiamondOfTheMockUp() {
        int empty = 0;
        for (int y = 0; y < FavoriteStar::GRID; y++) {
            for (int x = 0; x < FavoriteStar::GRID; x++) {
                QVERIFY(!FavoriteStar::EMPTY[y][x] || FavoriteStar::FILLED[y][x]);  // the outline is part of it
                QCOMPARE(FavoriteStar::FILLED[y][x], std::abs(x - 3) + std::abs(y - 3) <= 3);  // a diamond
                QCOMPARE(FavoriteStar::EMPTY[y][x], std::abs(x - 3) + std::abs(y - 3) == 3);
                empty += FavoriteStar::EMPTY[y][x];
            }
        }
        QCOMPARE(empty, 12);
    }

    void animationFillsFromTheCentreAndEndsFull() {
        double presence = 0.0;
        double scale = 0.0;
        for (int y = 0; y < FavoriteStar::GRID; y++) {
            for (int x = 0; x < FavoriteStar::GRID; x++) {
                FavoriteStar::square(x, y, false, true, 1.0, &presence, &scale);
                QCOMPARE(presence > 0.99, FavoriteStar::FILLED[y][x]);
                QVERIFY(std::abs(scale - 0.72) < 1e-9);
            }
        }
        double centre = 0.0;
        double side = 0.0;
        FavoriteStar::square(3, 3, false, true, 0.4, &centre, &scale);
        FavoriteStar::square(2, 2, false, true, 0.4, &side, &scale);  // inside the diamond, off-centre
        QVERIFY(centre > side);
        QVERIFY(scale > 0.72);  // squares swell during the animation
        FavoriteStar::square(3, 3, true, false, 1.0, &presence, &scale);
        QCOMPARE(presence, 0.0);
        FavoriteStar::square(3, 0, true, false, 1.0, &presence, &scale);
        QCOMPARE(presence, 1.0);
    }

    void favouriteIsCopiedOnTopAndStaysInPlace() {
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
        QCOMPARE(pressed.count(), 0);  // the ditherer is not selected
        QCOMPARE(changed.count(), 1);
        QCOMPARE(tree.favorites(), QList<int>({5}));
        QCOMPARE(tree.topLevelItemCount(), 20);  // the copy comes once the star has finished
        QTRY_COMPARE_WITH_TIMEOUT(tree.topLevelItemCount(), 21, 2000);
        QCOMPARE(shownIds(tree).mid(0, 8), QList<int>({5, 0, 1, 2, 3, 4, 5, 6}));  // on top, and at its place
        QVERIFY(TreeWidget::isFavoriteCopy(tree.topLevelItem(0)));
        QVERIFY(!TreeWidget::isFavoriteCopy(tree.topLevelItem(6)));
        QCOMPARE(tree.shownFavoriteCount(), 1);
        QCOMPARE(tree.currentItem(), selected);

        // a second favourite (the selected one) goes below the first
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, starOf(tree, 1));  // ditherer 0
        QTRY_COMPARE_WITH_TIMEOUT(tree.topLevelItemCount(), 22, 2000);
        QCOMPARE(shownIds(tree).mid(0, 4), QList<int>({5, 0, 0, 1}));
        QCOMPARE(tree.currentItem(), selected);  // still the ditherer at its place

        // clicking the copy selects the same ditherer
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualItemRect(tree.topLevelItem(1)).topLeft() + QPoint(30, 5));
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(tree.currentItem()->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt(), 0);
        QVERIFY(TreeWidget::isFavoriteCopy(tree.currentItem()));

        // removing the selected copy's favourite: the selection goes to the ditherer at its place
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier, starOf(tree, 1));
        QTRY_COMPARE_WITH_TIMEOUT(tree.topLevelItemCount(), 21, 2000);
        QCOMPARE(shownIds(tree).mid(0, 3), QList<int>({5, 0, 1}));
        QCOMPARE(tree.favorites(), QList<int>({5}));
        QCOMPARE(tree.currentItem(), tree.originalItem(0));
        QCOMPARE(pressed.count(), 1);
    }

    void ditheredFlagIsSharedWithTheCopyAndAnimated() {
        TreeWidget tree(nullptr);
        tree.setColumnCount(1);
        fill(tree, 6);
        tree.setFavorites({3});
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        QCOMPARE(tree.doneFill(3), 0.0);
        tree.setDitherFlag(3, true);
        QCOMPARE(tree.topLevelItem(0)->data(ITEM_DATA_DONE, Qt::UserRole).toBool(), true);  // the copy
        QCOMPARE(tree.originalItem(3)->data(ITEM_DATA_DONE, Qt::UserRole).toBool(), true);
        QVERIFY(tree.doneFill(3) < 0.5);  // it fills over a moment
        QTRY_COMPARE_WITH_TIMEOUT(tree.doneFill(3), 1.0, 2000);
        tree.clearAllDitherFlags();       // results gone stale: back to hollow
        QTRY_COMPARE_WITH_TIMEOUT(tree.doneFill(3), 0.0, 2000);
        QCOMPARE(tree.topLevelItem(0)->data(ITEM_DATA_DONE, Qt::UserRole).toBool(), false);
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
        QCOMPARE(shownIds(tree), QList<int>({6, 2, 0, 1, 2, 3, 4, 5, 6, 7}));
        QCOMPARE(tree.shownFavoriteCount(), 2);
        tree.setItemActive(0);  // the first ditherer as built, at its own place
        QCOMPARE(tree.currentItem(), tree.originalItem(0));
        tree.setFavorites({});
        QCOMPARE(shownIds(tree), QList<int>({0, 1, 2, 3, 4, 5, 6, 7}));
    }

    void rowsOnScreenDoNotJump() {
        TreeWidget tree(nullptr);
        tree.setColumnCount(1);
        tree.resize(260, 120);
        fill(tree, 60);
        tree.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tree));
        tree.verticalScrollBar()->setValue(tree.verticalScrollBar()->maximum() / 2);
        QVERIFY(tree.verticalScrollBar()->value() > 0);
        QTreeWidgetItem* under = tree.itemAt(QPoint(20, 30));
        const int top = tree.visualItemRect(under).top();
        QTest::mouseClick(tree.viewport(), Qt::LeftButton, Qt::NoModifier,
                          TreeWidgetDelegate::starRect(tree.visualItemRect(under)).center());
        QTRY_COMPARE_WITH_TIMEOUT(tree.shownFavoriteCount(), 1, 2000);
        QCOMPARE(tree.visualItemRect(under).top(), top);  // the copy went on top, out of view: nothing moved here
    }
};

/* The pixel glyphs (ui_elements/pixelglyphs.h) and the overlay that puts them on buttons */
class TestPixelGlyphs : public QObject {
    Q_OBJECT

    static QImage render(const std::function<void(QPainter*)>& paint) {
        QImage image(40, 40, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        paint(&painter);
        painter.end();
        return image;
    }
    static int lit(const QImage& image) {
        int count = 0;
        for (int y = 0; y < image.height(); y++) for (int x = 0; x < image.width(); x++) count += qAlpha(image.pixel(x, y)) > 0;
        return count;
    }
private slots:
    void pixelsAreWholeScreenPixels() {
        QCOMPARE(PixelGlyphs::cellSize(QRectF(0, 0, 16, 16), 8, 8, 1.0), 2.0);
        QCOMPARE(PixelGlyphs::cellSize(QRectF(0, 0, 16, 16), 7, 7, 1.0), 2.0);
        QCOMPARE(PixelGlyphs::cellSize(QRectF(0, 0, 16, 16), 7, 7, 1.5), 2.0);   // 3 device pixels
        QCOMPARE(PixelGlyphs::cellSize(QRectF(0, 0, 4, 4), 7, 7, 1.0), 1.0);     // never below one pixel
    }

    void statusDotHollowThenFullFromLeftToRight() {
        int hollow = 0, full = 0;
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                hollow += PixelGlyphs::statusDotPixel(x, y, 0.0);
                full += PixelGlyphs::statusDotPixel(x, y, 1.0);
            }
        }
        QVERIFY(!PixelGlyphs::statusDotPixel(3, 3, 0.0));  // hollow inside
        QVERIFY(full > hollow && full == 52);
        QVERIFY(PixelGlyphs::statusDotPixel(2, 3, 0.4));    // half way: the left side is filled
        QVERIFY(!PixelGlyphs::statusDotPixel(5, 3, 0.4));   // not the right side yet
        const QImage empty = render([](QPainter* p) {
            PixelGlyphs::paintStatusDot(p, QRectF(0, 0, 16, 16), 0.0, Qt::gray, Qt::white, 1.0); });
        const QImage dot = render([](QPainter* p) {
            PixelGlyphs::paintStatusDot(p, QRectF(0, 0, 16, 16), 1.0, Qt::gray, Qt::white, 1.0); });
        QCOMPARE(lit(empty), hollow * 4);  // 2 x 2 screen pixels each, crisp
        QCOMPARE(lit(dot), full * 4);
    }

    void crossScattersAndComesBack() {
        int whole = 0, mid = 0;
        for (int y = 0; y < 7; y++) {
            for (int x = 0; x < 7; x++) {
                whole += PixelGlyphs::crossPixelShown(x, y, 1.0);
                mid += PixelGlyphs::crossPixelShown(x, y, 0.45);
                QCOMPARE(PixelGlyphs::crossPixelShown(x, y, 0.0), PixelGlyphs::crossPixelShown(x, y, 1.0));
            }
        }
        QCOMPARE(mid, 0);       // all out half way
        QVERIFY(whole > 20);
        int partly = 0;
        for (int y = 0; y < 7; y++) for (int x = 0; x < 7; x++) partly += PixelGlyphs::crossPixelShown(x, y, 0.2);
        QVERIFY(partly > 0 && partly < whole);  // one by one, not all at once
    }

    void lockOpensWithItsLeftLegOut() {
        const QImage closed = render([](QPainter* p) { PixelGlyphs::paintLock(p, QRectF(13, 13, 14, 14), 0.0, Qt::white, 1.0); });
        const QImage open = render([](QPainter* p) { PixelGlyphs::paintLock(p, QRectF(13, 13, 14, 14), 1.0, Qt::white, 1.0); });
        QVERIFY(closed != open);
        // the glyph is 6 x 7 pixels of 2 x 2 at (14, 13); the left leg is pixel column 1, row 2 just above the body
        QVERIFY(qAlpha(closed.pixel(14 + 2 + 1, 13 + 4 + 1)) > 0);
        QVERIFY(qAlpha(open.pixel(14 + 2 + 1, 13 + 4 + 1)) == 0);   // open: a gap there
        QVERIFY(qAlpha(open.pixel(14 + 8 + 1, 13 + 4 + 1)) > 0);    // the right leg stays in the body
        QVERIFY(qAlpha(open.pixel(14 + 4, 13 - 2 + 1)) > 0);        // the arch has risen one pixel
    }

    void overlayLeavesTheButtonAlone() {
        QWidget window;
        QPushButton reset(&window);
        reset.setIcon(QIcon(QPixmap(8, 8)));
        reset.setFixedSize(20, 20);
        PixelButtonGlyph* cross = PixelButtonGlyph::attach(&reset, PixelButtonGlyph::Kind::Cross, 16);
        QVERIFY(reset.icon().isNull());  // replaced by the glyph
        QToolButton lock(&window);
        lock.setCheckable(true);
        lock.move(30, 0);
        lock.resize(22, 22);
        PixelButtonGlyph::attach(&lock, PixelButtonGlyph::Kind::Lock, 14);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(cross->geometry(), reset.rect());
        QSignalSpy clicked(&reset, &QPushButton::clicked);
        QTest::mouseClick(&reset, Qt::LeftButton);  // the click reaches the button at once
        QCOMPARE(clicked.count(), 1);
        QSignalSpy toggled(&lock, &QToolButton::toggled);
        QTest::mouseClick(&lock, Qt::LeftButton);
        QCOMPARE(toggled.count(), 1);
        QVERIFY(lock.isChecked());  // the locking itself is the button's, unchanged
        QTest::mouseClick(&lock, Qt::LeftButton);
        QVERIFY(!lock.isChecked());
    }
};

QObject* newTestFavorites() { return new TestFavorites; }
QObject* newTestPixelGlyphs() { return new TestPixelGlyphs; }
#include "tst_favorites.moc"
