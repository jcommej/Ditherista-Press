#include "labpanel.h"
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {

const QColor FRAME(0x25, 0x28, 0x31);
const QColor TEXT(0x8c, 0x92, 0x9d);

void drawMarker(QPainter& painter, const QPointF& at, const QRgb colour) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 4.0));
    painter.drawEllipse(at, 7.0, 7.0);
    painter.setPen(QPen(Qt::white, 2.0));
    painter.drawEllipse(at, 7.0, 7.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromRgb(colour));
    painter.drawEllipse(at, 4.0, 4.0);
}

}  // namespace

/*********************
 * VOLUME            *
 *********************/

LabVolumeView::LabVolumeView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 200);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("sRGB colours in CIELAB (L* vertical). Drag to turn, wheel to zoom."));
    // sample the L*a*b* space on a grid and keep what sRGB can show: the true, irregular gamut volume
    for (double L = 3.0; L <= 97.0; L += 4.0) {
        for (double a = -124.0; a <= 124.0; a += 8.0) {
            for (double b = -124.0; b <= 124.0; b += 8.0) {
                const Lab lab{L, a, b};
                if (inSrgbGamut(lab, 0.0)) {
                    cloud.push_back({lab, labToRgb(lab)});
                }
            }
        }
    }
}

void LabVolumeView::setLab(const Lab& lab) {
    current = lab;
    update();
}

QPointF LabVolumeView::project(const Lab& lab, double* depth) const {
    // L* up; a* and b* horizontal; yaw turns around the L* axis, pitch tilts it towards the viewer
    const double x = lab.a / 128.0, y = (lab.L - 50.0) / 50.0, z = lab.b / 128.0;
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const double xr = x * cy - z * sy;
    const double zr = x * sy + z * cy;
    const double yr = y * cp - zr * sp;
    *depth = y * sp + zr * cp;  // larger = nearer
    const double scale = std::min(width(), height()) * 0.30 * zoom;
    return {width() / 2.0 + xr * scale, height() / 2.0 - yr * scale};
}

void LabVolumeView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRadialGradient gradient(rect().center(), std::max(width(), height()) * 0.7);
    gradient.setColorAt(0.0, QColor(0x17, 0x1a, 0x20));
    gradient.setColorAt(1.0, QColor(0x0c, 0x0d, 0x10));
    painter.setPen(QPen(FRAME, 1.0));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);

    // L* axis, from black to white
    double depth;
    painter.setPen(QPen(QColor(255, 255, 255, 50), 1.0));
    painter.drawLine(project({0.0, 0.0, 0.0}, &depth), project({100.0, 0.0, 0.0}, &depth));

    // the cloud, far points first
    struct Drawn { QPointF at; double depth; QRgb rgb; };
    std::vector<Drawn> points;
    points.reserve(cloud.size());
    for (const Point& p : cloud) {
        const QPointF at = project(p.lab, &depth);
        points.push_back({at, depth, p.rgb});
    }
    std::sort(points.begin(), points.end(), [](const Drawn& a, const Drawn& b) { return a.depth < b.depth; });
    painter.setPen(Qt::NoPen);
    for (const Drawn& p : points) {
        const double nearness = std::clamp((p.depth + 1.4) / 2.8, 0.0, 1.0);
        QColor colour = QColor::fromRgb(p.rgb);
        colour.setAlphaF(0.16 + 0.55 * nearness);
        painter.setBrush(colour);
        const double radius = 1.3 + 1.2 * nearness * zoom;
        painter.drawEllipse(p.at, radius, radius);
    }

    // the plane of the current L*
    QPolygonF plane;
    for (const auto& [a, b] : {std::pair{-128.0, -128.0}, {128.0, -128.0}, {128.0, 128.0}, {-128.0, 128.0}}) {
        plane << project({current.L, a, b}, &depth);
    }
    painter.setBrush(QColor(255, 255, 255, 12));
    painter.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
    painter.drawPolygon(plane);

    drawMarker(painter, project(current, &depth), labToRgb(current));

    painter.setPen(TEXT);
    painter.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignRight | Qt::AlignTop,
                     QString("L* %1 · a* %2 · b* %3").arg(current.L, 0, 'f', 1).arg(current.a, 0, 'f', 1).arg(current.b, 0, 'f', 1));
    painter.drawText(rect().adjusted(10, 8, -10, -8), Qt::AlignLeft | Qt::AlignBottom, tr("drag: turn · wheel: zoom"));
}

void LabVolumeView::mousePressEvent(QMouseEvent* event) {
    lastMouse = event->position().toPoint();
    setCursor(Qt::ClosedHandCursor);
}

void LabVolumeView::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        setCursor(Qt::OpenHandCursor);
        return;
    }
    const QPoint position = event->position().toPoint();
    yaw += (position.x() - lastMouse.x()) * 0.008;
    pitch = std::clamp(pitch + (position.y() - lastMouse.y()) * 0.006, -1.4, 1.4);
    lastMouse = position;
    update();
}

void LabVolumeView::wheelEvent(QWheelEvent* event) {
    zoom = std::clamp(zoom * (event->angleDelta().y() > 0 ? 1.08 : 0.93), 0.65, 1.8);
    update();
    event->accept();
}

/*********************
 * a*b* SLICE        *
 *********************/

LabSliceView::LabSliceView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(160, 160);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("a* (green - red) across, b* (blue - yellow) up, at the L* of the slider. "
                  "Darkened: colours a screen cannot show."));
}

void LabSliceView::setLab(const Lab& lab) {
    current = lab;
    update();
}

void LabSliceView::resizeEvent(QResizeEvent*) {
    planeL = -1.0;  // redraw the plane at the new size
}

void LabSliceView::paintEvent(QPaintEvent*) {
    const int side = std::min(width(), height());
    const QRect square((width() - side) / 2, (height() - side) / 2, side, side);
    if (planeL != current.L || plane.width() != side) {
        plane = QImage(side, side, QImage::Format_RGB32);
        for (int y = 0; y < side; y++) {
            QRgb* row = reinterpret_cast<QRgb*>(plane.scanLine(y));
            const double b = RANGE - (y + 0.5) / side * 2.0 * RANGE;
            for (int x = 0; x < side; x++) {
                const Lab lab{current.L, (x + 0.5) / side * 2.0 * RANGE - RANGE, b};
                const QRgb rgb = labToRgb(lab);
                row[x] = inSrgbGamut(lab) ? rgb : qRgb(qRed(rgb) / 4, qGreen(rgb) / 4, qBlue(rgb) / 4);
            }
        }
        planeL = current.L;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(square), 8.0, 8.0);
    painter.setClipPath(clip);
    painter.drawImage(square.topLeft(), plane);
    painter.setClipping(false);
    painter.setPen(QPen(FRAME, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(square).adjusted(0.5, 0.5, -0.5, -0.5), 8.0, 8.0);
    // neutral axes a* = 0 and b* = 0
    painter.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
    painter.drawLine(QPointF(square.center().x() + 0.5, square.top()), QPointF(square.center().x() + 0.5, square.bottom()));
    painter.drawLine(QPointF(square.left(), square.center().y() + 0.5), QPointF(square.right(), square.center().y() + 0.5));
    painter.setPen(TEXT);
    painter.drawText(square.adjusted(8, 6, -8, -6), Qt::AlignRight | Qt::AlignBottom, tr("a* → red"));
    painter.drawText(square.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, tr("b* ↑ yellow"));
    const QPointF at(square.left() + (current.a + RANGE) / (2.0 * RANGE) * side,
                     square.top() + (RANGE - current.b) / (2.0 * RANGE) * side);
    drawMarker(painter, at, labToRgb(current));
}

void LabSliceView::pick(const QPointF& position) {
    const int side = std::min(width(), height());
    const QPointF origin((width() - side) / 2.0, (height() - side) / 2.0);
    const double a = std::clamp((position.x() - origin.x()) / side * 2.0 * RANGE - RANGE, -RANGE, RANGE);
    const double b = std::clamp(RANGE - (position.y() - origin.y()) / side * 2.0 * RANGE, -RANGE, RANGE);
    emit abPicked(a, b);
}

void LabSliceView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pick(event->position());
    }
}

void LabSliceView::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        pick(event->position());
    }
}

/*********************
 * PANEL             *
 *********************/

LabPanel::LabPanel(QWidget* parent) : QWidget(parent) {
    volume = new LabVolumeView(this);
    slice = new LabSliceView(this);
    lightness = new QSlider(Qt::Vertical, this);
    lightness->setRange(0, 1000);  // L* x 10
    lightness->setToolTip(tr("L*: the lightness of the a*b* plane, 0 black to 100 white"));
    readout = new QLabel(this);
    readout->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    readout->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QGridLayout* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(volume, 0, 0, 1, 2);
    grid->addWidget(slice, 1, 0);
    QVBoxLayout* sliderColumn = new QVBoxLayout();
    QLabel* top = new QLabel("100", this);
    QLabel* bottom = new QLabel("0", this);
    QLabel* name = new QLabel("L*", this);
    for (QLabel* label : {top, bottom, name}) {
        label->setAlignment(Qt::AlignHCenter);
    }
    sliderColumn->addWidget(top);
    sliderColumn->addWidget(lightness, 1, Qt::AlignHCenter);
    sliderColumn->addWidget(bottom);
    sliderColumn->addWidget(name);
    grid->addLayout(sliderColumn, 1, 1);
    grid->addWidget(readout, 2, 0, 1, 2);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);

    connect(slice, &LabSliceView::abPicked, this, [this](const double a, const double b) {
        wantedA = a;
        wantedB = b;
        pickedByUser(clampToSrgbGamut({current.L, a, b}));
    });
    connect(lightness, &QSlider::valueChanged, this, [this](const int value) {
        // keeps aiming at the a*b* last picked: going up and down in L* does not wear the chroma away
        pickedByUser(clampToSrgbGamut({value / 10.0, wantedA, wantedB}));
    });
    show(current);
}

void LabPanel::setLab(const Lab& lab) {
    wantedA = lab.a;
    wantedB = lab.b;
    show(lab);
}

void LabPanel::pickedByUser(const Lab& lab) {
    show(lab);
    emit labPicked(lab);
}

void LabPanel::show(const Lab& lab) {
    current = lab;
    volume->setLab(lab);
    slice->setLab(lab);
    const QSignalBlocker blocker(lightness);
    lightness->setValue(static_cast<int>(std::lround(lab.L * 10.0)));
    const QRgb rgb = labToRgb(lab);
    readout->setText(QString("L* %1   a* %2   b* %3\nR %4   G %5   B %6   %7")
                         .arg(lab.L, 5, 'f', 1).arg(lab.a, 6, 'f', 1).arg(lab.b, 6, 'f', 1)
                         .arg(qRed(rgb), 3).arg(qGreen(rgb), 3).arg(qBlue(rgb), 3).arg(hexColour(rgb)));
}
