#include "graphicsview.h"
#include "../consts.h"
#include <QFile>
#include <QMimeData>
#include <QFileInfo>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QNativeGestureEvent>
#include <QScrollBar>
#include <algorithm>
#include <cmath>

/* A GraphicsView class that supports dragging and dropping */

GraphicsView::GraphicsView(QWidget* parent) : QGraphicsView(parent) {
    /* Constructor */
    setAcceptDrops(true);
    setScene(&scene);
    setStyleSheet("QAbstractScrollArea::corner {\n"
                  "    background: #2a2a2a;\n"
                  "    border: none;\n"
                  "}");
    originalBadge = new QLabel(tr("ORIGINAL"), viewport());
    originalBadge->setStyleSheet("background: rgba(32, 32, 32, 200); color: #e6e6e6; border-radius: 4px;"
                                 "padding: 3px 8px; font-weight: bold;");
    originalBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    originalBadge->move(8, 8);
    originalBadge->adjustSize();
    originalBadge->hide();
    // navigation
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    viewport()->grabGesture(Qt::PinchGesture);
    motionTimer.setInterval(16);  // about 60 frames a second
    connect(&motionTimer, &QTimer::timeout, this, &GraphicsView::motionFrame);
}

/*******************************************
 * NAVIGATION: ZOOM, PAN, JOYSTICK, INERTIA *
 *******************************************/

namespace {
constexpr double JOYSTICK_DEADZONE = 10.0;  // pixels around the click where the view stays still
constexpr double JOYSTICK_GAIN = 4.0;       // speed = gain x (distance - deadzone) ^ power, pixels per second
constexpr double JOYSTICK_POWER = 1.35;
constexpr double JOYSTICK_MAX_SPEED = 6000.0;
constexpr double INERTIA_TAU = 0.3;         // seconds for the speed to fall to 37 %
constexpr double INERTIA_MIN_SPEED = 40.0;  // below this the motion stops
constexpr double WHEEL_ZOOM_PER_NOTCH = 1.2;
}

void GraphicsView::setZoomFactor(double factor, const bool update, const QPointF* anchor) {
    factor = std::clamp(factor, MIN_ZOOM / 100.0, MAX_ZOOM / 100.0);
    const QPointF scenePoint = anchor != nullptr ? mapToScene(anchor->toPoint()) : QPointF();
    zoom = factor;
    zoomLevel = static_cast<int>(std::lround(factor * 100.0));
    setTransform(QTransform::fromScale(factor, factor));
    if (anchor != nullptr) {
        scrollBy(QPointF(mapFromScene(scenePoint)) - *anchor);  // bring the picture point back under the anchor
    }
    if (update) {
        emit zoomLevelChangedSignal(zoomLevel);
    }
}

void GraphicsView::zoomToFit() {
    if (sceneRect().isEmpty()) {
        return;
    }
    fitInView(sceneRect(), Qt::KeepAspectRatio);
    setZoomFactor(transform().m11(), true);
    centerOn(sceneRect().center());
}

void GraphicsView::scrollBy(const QPointF& delta) {
    scrollRemainder += delta;
    const int dx = static_cast<int>(scrollRemainder.x());
    const int dy = static_cast<int>(scrollRemainder.y());
    scrollRemainder -= QPointF(dx, dy);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void GraphicsView::stopMotion() {
    motion = Motion::None;
    motionTimer.stop();
    velocity = QPointF();
    viewport()->unsetCursor();
}

void GraphicsView::motionFrame() {
    const double dt = std::clamp(frameClock.restart() / 1000.0, 0.0, 0.1);
    if (motion == Motion::Joystick) {
        // towards the pointer, faster the further it is from where the middle button went down
        const QPointF offset = joystickPointer - joystickOrigin;
        const double distance = std::hypot(offset.x(), offset.y());
        if (distance <= JOYSTICK_DEADZONE) {
            velocity = QPointF();
        } else {
            const double speed = std::min(JOYSTICK_MAX_SPEED, JOYSTICK_GAIN * std::pow(distance - JOYSTICK_DEADZONE, JOYSTICK_POWER));
            velocity = offset / distance * speed;
        }
    } else if (motion == Motion::Inertia) {
        velocity *= std::exp(-dt / INERTIA_TAU);
        if (std::hypot(velocity.x(), velocity.y()) < INERTIA_MIN_SPEED) {
            stopMotion();
            return;
        }
    } else {
        stopMotion();
        return;
    }
    scrollBy(velocity * dt);
}

bool GraphicsView::viewportEvent(QEvent* event) {
    /* pinch: two fingers on a touch screen (a gesture), or on a touchpad where the system reports it as a native
     * gesture (touchpads that send it as Ctrl + wheel zoom through wheelEvent) */
    if (navigation.pinchZoom && event->type() == QEvent::Gesture) {
        QGestureEvent* gestures = static_cast<QGestureEvent*>(event);
        if (QPinchGesture* pinch = static_cast<QPinchGesture*>(gestures->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                const QPointF at = viewport()->mapFromGlobal(pinch->centerPoint());
                setZoomFactor(zoom * pinch->scaleFactor(), true, &at);
            }
            gestures->accept(pinch);
            return true;
        }
    }
    if (navigation.pinchZoom && event->type() == QEvent::NativeGesture) {
        const QNativeGestureEvent* gesture = static_cast<QNativeGestureEvent*>(event);
        if (gesture->gestureType() == Qt::ZoomNativeGesture) {
            const QPointF at = gesture->position();
            setZoomFactor(zoom * (1.0 + gesture->value()), true, &at);
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void GraphicsView::replaceItem(QGraphicsPixmapItem*& item, QGraphicsPixmapItem* replacement) {
    /* swaps a scene item, deleting the old one: removeItem() alone hands ownership back and leaks it, which
     * upstream did on every re-dither and every adjustment - tens of MB each time on a large preview */
    if (item != nullptr) {
        scene.removeItem(item);
        delete item;
    }
    item = replacement;
    if (item != nullptr) {
        scene.addItem(item);
    }
}

void GraphicsView::setOriginalImage(const QImage& img) {
    /* the untouched picture for hold-to-compare; its pixmap is only built the first time it is shown */
    showOriginal(false);
    replaceItem(orig_pix_item, nullptr);
    originalImage = img;
}

void GraphicsView::showOriginal(const bool show) {
    /* overlays the original on top of whatever is displayed, keeping zoom and scroll position untouched */
    if (show == showingOriginal || (show && originalImage.isNull())) {
        return;
    }
    showingOriginal = show;
    if (show && orig_pix_item == nullptr) {
        QGraphicsPixmapItem* item = new QGraphicsPixmapItem(QPixmap::fromImage(originalImage));
        item->setZValue(1);  // above source and dithered items
        item->setAcceptedMouseButtons(Qt::NoButton);
        replaceItem(orig_pix_item, item);
    }
    if (orig_pix_item != nullptr) {
        orig_pix_item->setVisible(show);
    }
    originalBadge->setVisible(show);
    originalBadge->raise();
}

void GraphicsView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && navigation.rightDragPan) {
        stopMotion();
        panning = true;
        panLast = event->position();
        velocity = QPointF();
        moveClock.start();
        viewport()->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && navigation.middleJoystick) {
        stopMotion();
        setFocus();  // for Esc
        joystickOrigin = joystickPointer = event->position();
        motion = Motion::Joystick;
        frameClock.start();
        motionTimer.start();
        viewport()->setCursor(Qt::SizeAllCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        stopMotion();
        setFocus();  // so that Space works right after
        pressPos = event->pos();
        showOriginal(true);
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphicsView::mouseMoveEvent(QMouseEvent* event) {
    if (panning) {
        const QPointF delta = event->position() - panLast;
        panLast = event->position();
        scrollBy(-delta);  // the picture follows the pointer
        const double dt = moveClock.restart() / 1000.0;
        if (dt > 0.0) {  // smoothed, for the inertia at release
            velocity = velocity * 0.5 + (-delta / dt) * 0.5;
        }
        event->accept();
        return;
    }
    if (motion == Motion::Joystick) {
        joystickPointer = event->position();
        event->accept();
        return;
    }
    /* moving past the drag distance turns the hold into a drag of the result out of the window */
    if (showingOriginal && (event->buttons() & Qt::LeftButton) &&
        (event->pos() - pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        showOriginal(false);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GraphicsView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton && panning) {
        panning = false;
        viewport()->unsetCursor();
        // a flick carries on; a pan that stopped before the release does not
        if (navigation.inertia && moveClock.elapsed() < 80 && std::hypot(velocity.x(), velocity.y()) > INERTIA_MIN_SPEED) {
            motion = Motion::Inertia;
            frameClock.start();
            motionTimer.start();
        } else {
            velocity = QPointF();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && motion == Motion::Joystick) {
        if (navigation.inertia && std::hypot(velocity.x(), velocity.y()) > INERTIA_MIN_SPEED) {
            motion = Motion::Inertia;  // the timer keeps running: the glide slows down
            viewport()->unsetCursor();
        } else {
            stopMotion();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        showOriginal(false);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GraphicsView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && motion != Motion::None) {
        stopMotion();
        event->accept();
        return;
    }
    /* Space, while the preview has focus (click it or scroll over it first), is the keyboard equivalent */
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            showOriginal(true);
        }
        event->accept();
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            showOriginal(false);
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphicsView::showSourceMono(const bool show) const {
    /* shows the original imported mono source image */
    if (show) {
        if (out_pix_item_color != nullptr) out_pix_item_color->setVisible(false);
        if (out_pix_item_mono != nullptr) out_pix_item_mono->setVisible(false);
        src_pix_item_color->setVisible(false);
        src_pix_item_mono->setVisible(true);
    } else {
        if (out_pix_item_color != nullptr) out_pix_item_color->setVisible(false);
        if (out_pix_item_mono != nullptr) out_pix_item_mono->setVisible(true);
        src_pix_item_color->setVisible(false);
        src_pix_item_mono->setVisible(false);
    }
}

void GraphicsView::showSourceColor(const bool show) const {
    /* shows the original imported color source image */
    if (show) {
        if (out_pix_item_color != nullptr) out_pix_item_color->setVisible(false);
        if (out_pix_item_mono != nullptr) out_pix_item_mono->setVisible(false);
        src_pix_item_mono->setVisible(false);
        src_pix_item_color->setVisible(true);
    } else {
        if (out_pix_item_color != nullptr) out_pix_item_color->setVisible(true);
        if (out_pix_item_mono != nullptr) out_pix_item_mono->setVisible(false);
        src_pix_item_mono->setVisible(false);
        src_pix_item_color->setVisible(false);
    }
}

void GraphicsView::setDitherImageMono(const QImage* img, const QString& partialFileName) {
    if(out_pix_item_mono != nullptr) {
        deleteTempFiles();
    }
    GraphicsPixmapItem* item = new GraphicsPixmapItem(QPixmap::fromImage(*img));
    connect(item, SIGNAL(tempFileCreated(QString)), this, SLOT(tempFileCreatedSlot(QString)));
    item->setData(0, partialFileName);
    QGraphicsPixmapItem* previous = out_pix_item_mono;
    replaceItem(previous, item);
    out_pix_item_mono = item;
}

void GraphicsView::setDitherImageColor(const QImage* img, const QString& partialFileName, const QSize& displaySize) {
    if(out_pix_item_color != nullptr) {
        deleteTempFiles();
    }
    GraphicsPixmapItem* item = new GraphicsPixmapItem(QPixmap::fromImage(*img));
    connect(item, SIGNAL(tempFileCreated(QString)), this, SLOT(tempFileCreatedSlot(QString)));
    item->setData(0, partialFileName);
    if (displaySize.isValid() && displaySize != img->size()) {
        // a reduced render (live palette preview) stretched over the scene, pixels kept sharp
        item->setTransformationMode(Qt::FastTransformation);
        item->setTransform(QTransform::fromScale(static_cast<double>(displaySize.width()) / img->width(),
                                                 static_cast<double>(displaySize.height()) / img->height()));
    }
    QGraphicsPixmapItem* previous = out_pix_item_color;
    replaceItem(previous, item);
    out_pix_item_color = item;
}

void GraphicsView::deleteTempFiles() {
    /* remove temporary file used when user drags an image out of the viewport */
    for (int i = 0; i < tempFiles.size(); ++i) {
        QFile::remove(tempFiles[i]);
    }
    tempFiles.clear();
}

void GraphicsView::setSourceImageMono(const QImage* img) {
    /* sets the original source image */
    replaceItem(src_pix_item_mono, new QGraphicsPixmapItem(QPixmap::fromImage(*img)));
}

void GraphicsView::setSourceImageColor(const QImage* img) {
    /* sets the original source image */
    replaceItem(src_pix_item_color, new QGraphicsPixmapItem(QPixmap::fromImage(*img)));
}

void GraphicsView::resetScene(const int width, const int height) {
    /* resets and clears the GraphicsView */
    scene.setSceneRect(0, 0, width, height);
    scene.clear(); // clear scene and remove all QGraphicsItems
    stopMotion();
    setZoomFactor(1.0, true);  // upstream reset the number only; the view kept the previous picture's zoom
    out_pix_item_mono = nullptr;
    out_pix_item_color = nullptr;
    src_pix_item_mono = nullptr;
    src_pix_item_color = nullptr;
    orig_pix_item = nullptr;   // deleted by scene.clear() too
    showingOriginal = false;
    originalBadge->hide();
}

void GraphicsView::dropEvent(QDropEvent* event) {
    /* a file has been dropped onto the graphics view. Trigger import if file is a supported image */
    const QMimeData* mimedata = event->mimeData();
    if(!mimedata->hasUrls())
        return;
    QList<QUrl> url = mimedata->urls();
    for(int i = 0; i < url.count(); i++) {
        if (!url[i].isValid())
            continue;
        if (url[i].scheme() != "file")
            continue;
        QString fileName = url[i].toLocalFile();
        QFileInfo fileInfo(fileName);
        for (int j = 0; j < FILE_FILTERS.count(); j++) {
            if (QString suffix = "*." + fileInfo.suffix().toLower(); FILE_FILTERS[j] == suffix) {
                emit loadImageSignal(fileName);
                resetTransform();
                event->accept();
                return;
            }
        }
    }
}

void GraphicsView::mouseDoubleClickEvent(QMouseEvent* event) {
    /* handle mouse double click -> resets zoom level */
    if (event->button() != Qt::LeftButton) {
        mousePressEvent(event);  // a quick second click of the right or middle button is still navigation
        return;
    }
    stopMotion();
    setZoomFactor(1.0, true);
    event->accept();
}

void GraphicsView::wheelEvent(QWheelEvent* event) {
    /* zoom in / out when user uses mouse wheel */
    setFocus();
    if (navigation.smoothZoom) {
        // continuous, around the point under the pointer; wheel up zooms in
        const double notches = event->angleDelta().y() / 120.0;
        if (notches != 0.0) {
            const QPointF at = event->position();
            setZoomFactor(zoom * std::pow(WHEEL_ZOOM_PER_NOTCH, notches), true, &at);
        }
        event->accept();
        return;
    }
    if (event->angleDelta().y() > 0 && zoomLevel > MIN_ZOOM) {
        zoomLevel -= ZOOM_STEP_WHEEL;
        setZoomLevel(zoomLevel, true);
    } else if (event->angleDelta().y() < 0 && zoomLevel < MAX_ZOOM) {
        zoomLevel += ZOOM_STEP_WHEEL;
        setZoomLevel(zoomLevel, true);
    }
    event->accept();
};

int GraphicsView::getZoomLevel() {
    /* returns the current zoom level */
    return zoomLevel;
}

void GraphicsView::setZoomLevel(int level, bool update) {
    /* sets the zoom level of the view; emits a signal if 'update' is true */
    if (level >= MIN_ZOOM && level <= MAX_ZOOM) {
        setZoomFactor(level / 100.0, update);
    }
}

void GraphicsView::dragEnterEvent(QDragEnterEvent* event) {
    /* accepts drop events into the viewport */
    event->acceptProposedAction();
}

void GraphicsView::dragMoveEvent(QDragMoveEvent* event) {
    /* for dragging images into the viewport */
    event->acceptProposedAction();
}

void GraphicsView::tempFileCreatedSlot(const QString& fileName) {
    tempFiles<<fileName;
}

GraphicsView::~GraphicsView() {
    /* destructor */
    deleteTempFiles();
}