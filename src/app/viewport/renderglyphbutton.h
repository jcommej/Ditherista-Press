#pragma once
#ifndef RENDERGLYPHBUTTON_H
#define RENDERGLYPHBUTTON_H

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>
#include <array>

/* The render control in the preview's lower right corner: a discreet round button holding a 7 x 7 matrix of small
 * squares, like a dither cell.
 * - Auto: a ring - results are rendered as soon as a setting changes (upstream behaviour)
 * - Paused: two bars - settings change freely, the last result stays on screen
 * - Rendering: the pause glyph breathes, every square growing and shrinking a little out of step, while the one
 *   render runs; then the glyph turns back into the ring
 * The button only shows the state and reports clicks; MainWindow decides what a click does (mainwindow_render.cpp).
 * It stays anchored to the lower right corner of `anchor` (the view's viewport), whatever its size. */
class RenderGlyphButton final : public QWidget {
    Q_OBJECT
signals:
    void clicked();
public:
    enum class State { Auto, Paused, Rendering };
    static constexpr int GRID = 7;
    using Glyph = std::array<std::array<bool, GRID>, GRID>;
    static const Glyph AUTO_GLYPH;
    static const Glyph PAUSE_GLYPH;

    explicit RenderGlyphButton(QWidget* parent, QWidget* anchor);
    [[nodiscard]] State state() const { return current; }
    void setState(State state);  // the glyph glides to the new state; Rendering -> Auto after one breath at least
    [[nodiscard]] QSize sizeHint() const override { return {DIAMETER, DIAMETER}; }

    // one square of the matrix at `elapsedMs` of a render: scale of the cell (0.58..0.96) and opacity (0..1)
    static void breathing(int x, int y, double elapsedMs, double* scale, double* opacity);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    static constexpr int DIAMETER = 46;            // device-independent pixels
    static constexpr int MARGIN = 14;              // from the viewport's corner
    static constexpr double TRANSITION_MS = 230.0;
    static constexpr double BREATH_MS = 1450.0;    // one breath of the matrix while rendering
    static constexpr double MIN_RENDER_MS = 550.0; // a quick render still shows a visible breath
    QWidget* anchor;
    State current = State::Auto;
    State shown = State::Auto;                     // lags behind `current` while a short render finishes its breath
    const Glyph* fromGlyph = &AUTO_GLYPH;
    const Glyph* toGlyph = &AUTO_GLYPH;
    double transitionStart = -TRANSITION_MS;       // ms on `clock`
    double renderStart = 0.0;
    bool hovered = false;
    bool pressed = false;
    QElapsedTimer clock;
    QTimer frameTimer;                             // repaints while something moves, stops when still
    void frame();
    void glideTo(const Glyph* target);
    void reposition();
    void updateToolTip();
    [[nodiscard]] double now() const { return static_cast<double>(clock.elapsed()); }
};

#endif // RENDERGLYPHBUTTON_H
