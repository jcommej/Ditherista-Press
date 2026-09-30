#include "graphicsview.h"
#include "../consts.h"
#include <QFile>
#include <QMimeData>
#include <QFileInfo>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

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
    if (event->button() == Qt::LeftButton) {
        setFocus();  // so that Space works right after
        pressPos = event->pos();
        showOriginal(true);
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphicsView::mouseMoveEvent(QMouseEvent* event) {
    /* moving past the drag distance turns the hold into a drag of the result out of the window */
    if (showingOriginal && (event->buttons() & Qt::LeftButton) &&
        (event->pos() - pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        showOriginal(false);
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GraphicsView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        showOriginal(false);
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GraphicsView::keyPressEvent(QKeyEvent* event) {
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
    zoomLevel = 100;
    emit zoomLevelChangedSignal(zoomLevel);
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
    resetTransform();
    zoomLevel = 100;
    emit zoomLevelChangedSignal(zoomLevel);
    event->accept();
}

void GraphicsView::wheelEvent(QWheelEvent* event) {
    /* zoom in / out when user uses mouse wheel */
    setFocus();
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
        zoomLevel = level;
        float zl = (float) level / 100.0f;
        setTransform(QTransform(zl, 0, 0, 0, zl, 0, 0, 0, 1));
        if (update) {
            emit zoomLevelChangedSignal(zoomLevel);
        }
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