#pragma once
#ifndef GRAPHICSVIEW_H
#define GRAPHICSVIEW_H

#include "graphicspixmapitem.h"
#include <QGraphicsView>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>

class GraphicsView final : public QGraphicsView {
    Q_OBJECT
signals:
    void loadImageSignal(QString fileName);
    void zoomLevelChangedSignal(int zoomLevel);
public:
    /* methods */
    explicit GraphicsView(QWidget* parent = nullptr);
    void dropEvent(QDropEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // hold the left button or Space on the preview: the untouched original replaces the result until release
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void setOriginalImage(const QImage& img);  // the picture before any processing, at the preview's resolution
    [[nodiscard]] bool isShowingOriginal() const { return showingOriginal; }
    void resetScene(int width, int height);

    void showSourceMono(bool show) const;
    void showSourceColor(const bool show) const;

    void setDitherImageMono(const QImage* img, const QString& partialFileName);
    // displaySize: the size to show it at, when `img` is a reduced render (invalid = its own size)
    void setDitherImageColor(const QImage* img, const QString& partialFileName, const QSize& displaySize = QSize());

    void setSourceImageMono(const QImage* img);
    void setSourceImageColor(const QImage* img);

    int getZoomLevel();

    void setZoomLevel(int level, bool update);

    /* Navigation (Preferences menu). Each can be turned off, giving back upstream's behaviour:
     * - smoothZoom: the wheel zooms continuously around the point under the pointer (upstream: steps of 10 %
     *   around the centre, wheel up zooming out)
     * - dragPan: drag with the left or the right button to move the picture; Ctrl + left drag exports the film
     *   as a file, Space shows the original (upstream: hold the left button for the original, drag to export)
     * - middleJoystick: click the middle button and move away from that point: the view glides that way, faster
     *   the further the pointer is; release (or Esc) to stop
     * - inertia: after a pan or a joystick glide the view carries on and slows down
     * - pinchZoom: pinch on a touch screen or a touchpad */
    struct Navigation {
        bool smoothZoom = true;
        bool dragPan = true;
        bool middleJoystick = true;
        bool inertia = true;
        bool pinchZoom = true;
        int zoomIncrement = 10;  // % per wheel notch: points added (stepped) or factor 1 + % (smooth)
    };
    /* Background behind the picture: a grey (0 white .. 255 black), or graph paper at the film's scale - a thin
     * line every millimetre, a thick one every centimetre - on white or black, from the picture's corner */
    enum class Background { Solid, GraphPaperWhite, GraphPaperBlack };
    void setBackground(Background mode, int grey);
    void setPixelsPerMm(double pixelsPerMm);  // of the preview: the scale of the graph paper
    void setNavigation(const Navigation& settings) {
        navigation = settings;
        restCursor();
    }
    // zoom factor (1 = 100 %), limited to MIN_ZOOM..MAX_ZOOM percent; `anchor`, a point of the viewport, stays
    // over the same point of the picture
    void setZoomFactor(double factor, bool update, const QPointF* anchor = nullptr);
    [[nodiscard]] double zoomFactor() const { return zoom; }
    void zoomToFit();  // the whole picture in the view

    ~GraphicsView() override;
protected:
    bool viewportEvent(QEvent* event) override;  // pinch gestures
    void drawBackground(QPainter* painter, const QRectF& rect) override;  // graph paper
private:
    /* attributes */
    int zoomLevel = 100; // in percent, rounded from zoom
    double zoom = 1.0;
    Navigation navigation;
    Background background = Background::Solid;
    double pixelsPerMm = 0.0;  // 0 = unknown (no picture): no graph paper lines
    // right-drag pan, middle-button joystick, inertia: the view moves by scrolling
    enum class Motion { None, Joystick, Inertia };
    bool panning = false;
    Qt::MouseButton panningButton = Qt::NoButton;
    QPointF panLast;
    QPointF velocity;           // scroll speed in viewport pixels per second
    QElapsedTimer moveClock;    // time since the last pan move
    Motion motion = Motion::None;
    QPointF joystickOrigin;
    QPointF joystickPointer;
    QTimer motionTimer;         // one frame of joystick or inertia motion
    QElapsedTimer frameClock;
    QPointF scrollRemainder;    // sub-pixel scrolling carried over to the next frame
    void scrollBy(const QPointF& delta);
    void motionFrame();
    void stopMotion();
    void restCursor() { viewport()->setCursor(navigation.dragPan ? Qt::OpenHandCursor : Qt::ArrowCursor); }
    QGraphicsScene scene;
    GraphicsPixmapItem* out_pix_item_mono = nullptr;   // dithered mono image
    GraphicsPixmapItem* out_pix_item_color = nullptr;  // dithered color image
    QGraphicsPixmapItem* src_pix_item_mono = nullptr;  // source mono image
    QGraphicsPixmapItem* src_pix_item_color = nullptr; // source color image
    QGraphicsPixmapItem* orig_pix_item = nullptr;      // untouched original, built on first use
    QImage originalImage;                              // implicitly shared: no copy until first shown
    bool showingOriginal = false;
    QPoint pressPos;                                   // where the left button went down
    QLabel* originalBadge = nullptr;                   // "ORIGINAL" marker while it is shown
    QStringList tempFiles;
    /* methods */
    void deleteTempFiles();
    void showOriginal(bool show);
    void replaceItem(QGraphicsPixmapItem*& item, QGraphicsPixmapItem* replacement);
private slots:
    void tempFileCreatedSlot(const QString& fileName);
};

#endif // GRAPHICSVIEW_H
