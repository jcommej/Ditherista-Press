#pragma once
#ifndef PIXELBUTTONGLYPH_H
#define PIXELBUTTONGLYPH_H

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QAbstractButton;

/* Draws a pixel glyph (ui_elements/pixelglyphs.h) over an existing button, in place of its icon, and animates it.
 * It covers the button but lets every mouse event through, so the button keeps its clicks, its hover look, its size
 * and everything connected to it; the glyph only follows:
 * - Lock: the padlock of a checkable button (checked = locked). A click animates the shackle; a state set from code
 *   with signals blocked is shown at once.
 * - Cross: the reset cross. A click scatters its pixels and brings them back - the reset itself is not delayed.
 * `glyphSize`: the square the glyph is fitted in, the size the icon had. Nothing repaints while it is still. */
class PixelButtonGlyph final : public QWidget {
public:
    enum class Kind { Lock, Cross };
    PixelButtonGlyph(QAbstractButton* button, Kind kind, int glyphSize);
    static PixelButtonGlyph* attach(QAbstractButton* button, Kind kind, int glyphSize);  // removes the icon too
protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QAbstractButton* button;
    Kind kind;
    int glyphSize;
    QElapsedTimer clock;
    bool animating = false;
    double fromOpen = 0.0;  // padlock: open amount when the animation started
    QTimer frameTimer;
    void start();
    [[nodiscard]] double progress() const;  // 0..1 along the running animation, 1 when still
};

#endif // PIXELBUTTONGLYPH_H
