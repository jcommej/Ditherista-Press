#include <QtTest>
#include <QApplication>
#include <QGraphicsScene>
#include <QScrollBar>
#include "viewport/graphicsview.h"

/* Tests for hold-to-compare in the preview (viewport/graphicsview.cpp). Runs on Qt's offscreen platform: no
 * window appears and no real input is involved. */

static QImage filled(const QRgb colour) {
    QImage image(60, 40, QImage::Format_ARGB32);
    image.fill(colour);
    return image;
}

class TestPreview : public QObject {
    Q_OBJECT
private:
    GraphicsView* view = nullptr;
    QImage source, dithered, original;

    void press(const QPoint& at) { QTest::mousePress(view->viewport(), Qt::LeftButton, {}, at); }
    void release(const QPoint& at) { QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, at); }
    void moveHeld(const QPoint& at) {
        // QTest::mouseMove sends no button state, and a drag needs the button held
        QMouseEvent move(QEvent::MouseMove, at, view->viewport()->mapToGlobal(at), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(view->viewport(), &move);
    }
    [[nodiscard]] QGraphicsItem* topItemAt(const QPointF& scenePos) const {
        const auto items = static_cast<QGraphicsView*>(view)->scene()->items(scenePos);  // topmost first
        for (QGraphicsItem* item : items) {
            if (item->isVisible()) return item;
        }
        return nullptr;
    }

private slots:
    void init() {
        view = new GraphicsView();
        // these tests are about upstream's left button (hold = original, drag = export): Drag to Pan off
        GraphicsView::Navigation upstream;
        upstream.dragPan = false;
        view->setNavigation(upstream);
        view->resize(300, 300);
        view->show();
        source = filled(qRgb(128, 128, 128));
        dithered = filled(qRgb(0, 0, 0));
        original = filled(qRgb(200, 50, 50));
        view->resetScene(60, 40);
        view->setSourceImageMono(&source);
        view->setSourceImageColor(&source);
        view->setDitherImageMono(&dithered, "test");
        view->showSourceMono(false);
        view->setOriginalImage(original);
    }

    void cleanup() {
        delete view;
        view = nullptr;
    }

    void holdingTheButtonShowsTheOriginal() {
        QVERIFY(!view->isShowingOriginal());
        press(QPoint(20, 20));
        QVERIFY(view->isShowingOriginal());
        // the original really is what is on top
        const auto* top = dynamic_cast<const QGraphicsPixmapItem*>(topItemAt(QPointF(5, 5)));
        QVERIFY(top != nullptr);
        QCOMPARE(top->pixmap().toImage().pixel(5, 5), original.pixel(5, 5));
        release(QPoint(20, 20));
        QVERIFY(!view->isShowingOriginal());
        const auto* after = dynamic_cast<const QGraphicsPixmapItem*>(topItemAt(QPointF(5, 5)));
        QCOMPARE(after->pixmap().toImage().pixel(5, 5), dithered.pixel(5, 5));
    }

    void zoomAndScrollAreKept() {
        view->setZoomLevel(300, true);
        const QTransform transform = view->transform();
        const QPointF centre = view->mapToScene(view->viewport()->rect().center());
        press(QPoint(30, 30));
        QCOMPARE(view->transform(), transform);
        release(QPoint(30, 30));
        QCOMPARE(view->transform(), transform);
        QCOMPARE(view->getZoomLevel(), 300);
        QCOMPARE(view->mapToScene(view->viewport()->rect().center()), centre);
    }

    void spaceWorksToo() {
        QTest::keyPress(view, Qt::Key_Space);
        QVERIFY(view->isShowingOriginal());
        QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, QString(), true);  // auto-repeat
        QApplication::sendEvent(view, &repeat);
        QVERIFY(view->isShowingOriginal());  // key auto-repeat must not flicker it off
        QTest::keyRelease(view, Qt::Key_Space);
        QVERIFY(!view->isShowingOriginal());
    }

    void movingFarTurnsTheHoldIntoADrag() {
        press(QPoint(20, 20));
        moveHeld(QPoint(21, 21));  // hand tremor: still comparing
        QVERIFY(view->isShowingOriginal());
        moveHeld(QPoint(20 + 4 * QApplication::startDragDistance(), 20));
        QVERIFY(!view->isShowingOriginal());  // result is back, ready to be dragged out
        release(QPoint(20 + 4 * QApplication::startDragDistance(), 20));
    }

    void nothingHappensWithoutAnImage() {
        view->setOriginalImage(QImage());
        press(QPoint(20, 20));
        QVERIFY(!view->isShowingOriginal());
        release(QPoint(20, 20));
    }

    void replacingImagesDoesNotPileUpInTheScene() {
        // upstream added a new item on every adjustment and never deleted replaced results
        press(QPoint(5, 5));
        release(QPoint(5, 5));  // builds the original's item once
        const qsizetype items = static_cast<QGraphicsView*>(view)->scene()->items().size();
        for (int i = 0; i < 20; i++) {
            view->setSourceImageMono(&source);
            view->setSourceImageColor(&source);
            view->setDitherImageMono(&dithered, "test");
            view->setDitherImageColor(&dithered, "test");
        }
        QCOMPARE(static_cast<QGraphicsView*>(view)->scene()->items().size(), items + 1);  // + the colour result, set for the first time here
    }

    void dragToPanMovesThePictureInstead() {
        // the default since Preferences: the left button pans, Space alone shows the original
        view->setNavigation(GraphicsView::Navigation());
        view->setZoomLevel(800, true);  // 480 x 320 in a 300 x 300 view: room to scroll
        QTest::qWait(20);
        const int before = view->horizontalScrollBar()->value();
        press(QPoint(150, 150));
        QVERIFY(!view->isShowingOriginal());
        moveHeld(QPoint(110, 150));
        release(QPoint(110, 150));
        QCOMPARE(view->horizontalScrollBar()->value(), before + 40);  // the picture follows the pointer
        QVERIFY(!view->isShowingOriginal());
        QTest::keyPress(view, Qt::Key_Space);
        QVERIFY(view->isShowingOriginal());
        QTest::keyRelease(view, Qt::Key_Space);
        // Ctrl + left is left to the film's drag out of the window: no pan
        const int at = view->horizontalScrollBar()->value();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::ControlModifier, QPoint(150, 150));
        QMouseEvent move(QEvent::MouseMove, QPointF(140, 150), view->viewport()->mapToGlobal(QPointF(140, 150)),
                         Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
        QApplication::sendEvent(view->viewport(), &move);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::ControlModifier, QPoint(140, 150));
        QCOMPARE(view->horizontalScrollBar()->value(), at);
        QVERIFY(!view->isShowingOriginal());
    }

    void newImageReplacesTheOriginal() {
        press(QPoint(5, 5));
        release(QPoint(5, 5));
        const QImage other = filled(qRgb(10, 200, 10));
        view->setOriginalImage(other);
        press(QPoint(5, 5));
        const auto* top = dynamic_cast<const QGraphicsPixmapItem*>(topItemAt(QPointF(5, 5)));
        QCOMPARE(top->pixmap().toImage().pixel(5, 5), other.pixel(5, 5));
        release(QPoint(5, 5));
    }
};

QObject* newTestPreview() { return new TestPreview; }
#include "tst_preview.moc"
