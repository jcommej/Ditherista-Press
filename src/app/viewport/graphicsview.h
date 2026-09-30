#pragma once
#ifndef GRAPHICSVIEW_H
#define GRAPHICSVIEW_H

#include "graphicspixmapitem.h"
#include <QGraphicsView>
#include <QDragEnterEvent>
#include <QLabel>

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

    ~GraphicsView() override;
private:
    /* attributes */
    int zoomLevel = 100; // in percent
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
