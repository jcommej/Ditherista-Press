#pragma once
#ifndef LABPANEL_H
#define LABPANEL_H

#include "color/colorspace.h"
#include <QImage>
#include <QWidget>
#include <vector>

class QLabel;
class QSlider;

/* CIELAB colour selection, next to the usual RGB / HSV picker (see colourpickerdialog.h)
 * -------------------------------------------------------------------------------------
 * - LabVolumeView: the colours sRGB can show, as a cloud of points at their true L*a*b* positions - an irregular
 *   volume, not a sphere. L* is the vertical axis (black at the bottom, white at the top), a* runs green -> red,
 *   b* blue -> yellow. The plane of the current L* and the selected colour are drawn in it. Drag to turn it,
 *   wheel to zoom. It only shows; it does not pick.
 * - LabSliceView: the a* x b* plane at the current L*, where colours sRGB cannot show are darkened. Clicking or
 *   dragging picks a* and b*; a point outside sRGB is brought back to its edge (same hue, less chroma).
 * - LabPanel: both, the vertical L* slider (0 at the bottom, 100 at the top) and a readout of L* a* b* and R G B.
 * The panel holds one colour in L*a*b*; setLab shows a colour without signalling, labPicked reports the user's.
 */

class LabVolumeView final : public QWidget {
    Q_OBJECT
public:
    explicit LabVolumeView(QWidget* parent = nullptr);
    void setLab(const Lab& lab);
    [[nodiscard]] QSize sizeHint() const override { return {300, 280}; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    struct Point { Lab lab; QRgb rgb; };
    std::vector<Point> cloud;  // in-gamut samples, computed once
    Lab current;
    double yaw = -0.65, pitch = 0.5, zoom = 1.0;
    QPoint lastMouse;
    [[nodiscard]] QPointF project(const Lab& lab, double* depth) const;
};

class LabSliceView final : public QWidget {
    Q_OBJECT
public:
    explicit LabSliceView(QWidget* parent = nullptr);
    void setLab(const Lab& lab);
    [[nodiscard]] QSize sizeHint() const override { return {240, 240}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override { return width; }
    static constexpr double RANGE = 128.0;  // a* and b* shown from -RANGE to +RANGE
signals:
    void abPicked(double a, double b);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    Lab current;
    QImage plane;           // the a*b* plane at planeL, at the widget's size
    double planeL = -1.0;
    void pick(const QPointF& position);
};

class LabPanel final : public QWidget {
    Q_OBJECT
public:
    explicit LabPanel(QWidget* parent = nullptr);
    void setLab(const Lab& lab);  // shows it; no signal
    [[nodiscard]] Lab lab() const { return current; }
signals:
    void labPicked(const Lab& lab);  // the user moved the slice pointer or the L* slider
private:
    LabVolumeView* volume;
    LabSliceView* slice;
    QSlider* lightness;
    QLabel* readout;
    Lab current;
    double wantedA = 0.0, wantedB = 0.0;  // a*b* last picked: moving L* keeps aiming at them
    void show(const Lab& lab);
    void pickedByUser(const Lab& lab);
};

#endif // LABPANEL_H
