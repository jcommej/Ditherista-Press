#include "renderglyphbutton.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr int SHADOW = 4;              // room around the circle for its shadow
constexpr double GLYPH_SPAN = 0.68;    // the matrix covers this much of the circle's diameter
constexpr double IDLE_SCALE = 0.70;    // a square fills 70 % of its cell when nothing moves
constexpr double IDLE_OPACITY = 215.0 / 255.0;
constexpr double REST_OPACITY = 0.78;  // the whole button, pointer away: discreet over the picture
constexpr double PRESSED_SCALE = 0.93;

double ease(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
}

// a ring: rendering follows every change
const RenderGlyphButton::Glyph RenderGlyphButton::AUTO_GLYPH = {{
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {1, 1, 0, 0, 0, 1, 1},
    {1, 0, 0, 1, 0, 0, 1},
    {1, 1, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0},
}};

// two bars: paused
const RenderGlyphButton::Glyph RenderGlyphButton::PAUSE_GLYPH = {{
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
}};

RenderGlyphButton::RenderGlyphButton(QWidget* parent, QWidget* anchor) : QWidget(parent), anchor(anchor) {
    setFixedSize(DIAMETER + 2 * SHADOW, DIAMETER + 2 * SHADOW);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);  // Space stays with the view (show the original)
    setCursor(Qt::PointingHandCursor);
    clock.start();
    frameTimer.setInterval(16);  // about 60 frames a second, only while something moves
    connect(&frameTimer, &QTimer::timeout, this, &RenderGlyphButton::frame);
    anchor->installEventFilter(this);
    updateToolTip();
    reposition();
}

void RenderGlyphButton::breathing(const int x, const int y, const double elapsedMs, double* scale, double* opacity) {
    /* the whole matrix breathes in and out once per BREATH_MS; each square is shifted in phase by its place, so the
     * squares swell one after the other like a dither screen getting darker and lighter */
    const double p = std::fmod(std::max(elapsedMs, 0.0), BREATH_MS) / BREATH_MS;
    const double breath = 0.5 - 0.5 * std::cos(p * 2.0 * std::numbers::pi);  // 0 -> 1 -> 0
    const double local = 0.5 + 0.5 * std::sin(breath * 2.0 * std::numbers::pi + (x + y * 0.73) * 0.42);
    *scale = 0.58 + local * 0.38;
    *opacity = (125.0 + local * 105.0) / 255.0;
}

void RenderGlyphButton::setState(const State state) {
    current = state;
    if (state == State::Rendering) {
        shown = State::Rendering;
        renderStart = now();
        glideTo(&PAUSE_GLYPH);  // the bars breathe while the render runs
    } else if (state == State::Auto && shown == State::Rendering && now() - renderStart < MIN_RENDER_MS) {
        // a quick render: frame() turns the glyph back into the ring once the breath has been seen
    } else {
        shown = state;
        glideTo(state == State::Paused ? &PAUSE_GLYPH : &AUTO_GLYPH);
    }
    updateToolTip();
    frameTimer.start();
    repaint();  // now: the render that follows may hold the event loop for a moment
}

void RenderGlyphButton::glideTo(const Glyph* target) {
    if (target == toGlyph) {
        return;
    }
    fromGlyph = toGlyph;
    toGlyph = target;
    transitionStart = now();
}

void RenderGlyphButton::frame() {
    const double t = now();
    if (shown == State::Rendering && current != State::Rendering && t - renderStart >= MIN_RENDER_MS) {
        shown = current;  // the render is over: back to the ring (or the bars)
        glideTo(current == State::Paused ? &PAUSE_GLYPH : &AUTO_GLYPH);
    }
    update();
    if (shown != State::Rendering && t - transitionStart > TRANSITION_MS) {
        frameTimer.stop();  // still: no more frames until the next change
    }
}

void RenderGlyphButton::updateToolTip() {
    QString tip;
    switch (current) {
        case State::Auto: tip = tr("Pause automatic rendering"); break;
        case State::Paused: tip = tr("Render with the current settings and resume automatic rendering"); break;
        case State::Rendering: tip = tr("Rendering..."); break;
    }
    setToolTip(tip);
    setAccessibleName(tip);
}

void RenderGlyphButton::reposition() {
    /* lower right corner of the anchor (the viewport, so the scroll bars stay clear), in the parent's coordinates */
    const QRect area = anchor->geometry();
    move(area.right() + 1 - width() - MARGIN + SHADOW, area.bottom() + 1 - height() - MARGIN + SHADOW);
    raise();
}

bool RenderGlyphButton::eventFilter(QObject* watched, QEvent* event) {
    if (watched == anchor && (event->type() == QEvent::Resize || event->type() == QEvent::Move ||
                              event->type() == QEvent::Show)) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void RenderGlyphButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        pressed = true;
        update();
    }
    event->accept();  // never reaches the view: no pan, no export drag from here
}

void RenderGlyphButton::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && pressed) {
        pressed = false;
        update();
        const QPointF centre(width() / 2.0, height() / 2.0);
        if (QLineF(centre, event->position()).length() <= DIAMETER / 2.0) {
            emit clicked();
        }
    }
    event->accept();
}

void RenderGlyphButton::enterEvent(QEnterEvent* event) {
    hovered = true;
    update();
    QWidget::enterEvent(event);
}

void RenderGlyphButton::leaveEvent(QEvent* event) {
    hovered = false;
    pressed = false;
    update();
    QWidget::leaveEvent(event);
}

void RenderGlyphButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double t = now();
    const QPointF centre(width() / 2.0, height() / 2.0);
    painter.translate(centre);
    if (pressed) {
        painter.scale(PRESSED_SCALE, PRESSED_SCALE);
    }
    painter.setOpacity(hovered || pressed || shown == State::Rendering ? 1.0 : REST_OPACITY);

    // soft shadow, then the disc
    const double radius = DIAMETER / 2.0;
    painter.setPen(Qt::NoPen);
    for (int i = SHADOW; i > 0; i--) {
        painter.setBrush(QColor(0, 0, 0, 22));
        painter.drawEllipse(QPointF(0, 1.5), radius + i * 0.75, radius + i * 0.75);
    }
    painter.setBrush(hovered ? QColor(0x33, 0x33, 0x33) : QColor(0x29, 0x29, 0x29));
    painter.setPen(QPen(hovered ? QColor(0x66, 0x66, 0x66) : QColor(0x50, 0x50, 0x50), 1.0));
    painter.drawEllipse(QPointF(0, 0), radius - 0.5, radius - 0.5);
    painter.setPen(QPen(QColor(255, 255, 255, 6), 1.0));  // faint inner rim
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(0, 0), radius - 1.5, radius - 1.5);

    // the matrix
    const double cell = DIAMETER * GLYPH_SPAN / GRID;
    const double start = -cell * GRID / 2.0;
    const double glide = ease((t - transitionStart) / TRANSITION_MS);
    const bool breathingNow = shown == State::Rendering;
    const double breathIn = breathingNow ? ease((t - renderStart) / TRANSITION_MS) : 0.0;  // no jump at the start
    painter.setPen(Qt::NoPen);
    for (int y = 0; y < GRID; y++) {
        for (int x = 0; x < GRID; x++) {
            const double from = (*fromGlyph)[y][x] ? 1.0 : 0.0;
            const double to = (*toGlyph)[y][x] ? 1.0 : 0.0;
            const double presence = from + (to - from) * glide;  // squares shrink away or grow in
            if (presence < 0.04) {
                continue;
            }
            double scale = IDLE_SCALE;
            double opacity = IDLE_OPACITY;
            if (breathingNow) {
                double breathScale = 0.0;
                double breathOpacity = 0.0;
                breathing(x, y, t - renderStart, &breathScale, &breathOpacity);
                scale += (breathScale - IDLE_SCALE) * breathIn;
                opacity += (breathOpacity - IDLE_OPACITY) * breathIn;
            }
            const double size = cell * scale * presence;
            const QPointF c(start + (x + 0.5) * cell, start + (y + 0.5) * cell);
            painter.setBrush(QColor(235, 235, 235, static_cast<int>(std::lround(255.0 * opacity * presence))));
            painter.drawRoundedRect(QRectF(c.x() - size / 2.0, c.y() - size / 2.0, size, size), size * 0.14, size * 0.14);
        }
    }
}
