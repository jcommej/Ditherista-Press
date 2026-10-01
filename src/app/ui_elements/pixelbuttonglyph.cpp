#include "pixelbuttonglyph.h"
#include "pixelglyphs.h"
#include <QAbstractButton>
#include <QEvent>
#include <QPainter>
#include <algorithm>

namespace {
const QColor ENABLED_INK(0xe6, 0xe6, 0xe6);   // the colours of the icons they replace
const QColor DISABLED_INK(0x5a, 0x5a, 0x5a);
}

PixelButtonGlyph::PixelButtonGlyph(QAbstractButton* button, const Kind kind, const int glyphSize)
    : QWidget(button), button(button), kind(kind), glyphSize(glyphSize) {
    setAttribute(Qt::WA_TransparentForMouseEvents);  // clicks, hover and tool tips stay the button's
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(button->rect());
    button->installEventFilter(this);
    frameTimer.setInterval(16);
    connect(&frameTimer, &QTimer::timeout, this, [this]() {
        if (progress() >= 1.0) {
            animating = false;
            frameTimer.stop();
        }
        update();
    });
    if (kind == Kind::Lock) {
        connect(button, &QAbstractButton::toggled, this, [this](bool) {  // not emitted when set with signals blocked
            // the open amount the shackle was heading for before this toggle: 1 if it was unlocked
            const double previous = this->button->isChecked() ? 1.0 : 0.0;
            fromOpen = animating ? fromOpen + (previous - fromOpen) * progress() : previous;  // reversed mid-way
            start();
        });
    } else {
        connect(button, &QAbstractButton::clicked, this, [this]() { start(); });
    }
    show();
}

PixelButtonGlyph* PixelButtonGlyph::attach(QAbstractButton* button, const Kind kind, const int glyphSize) {
    button->setIcon(QIcon());
    return new PixelButtonGlyph(button, kind, glyphSize);
}

void PixelButtonGlyph::start() {
    clock.start();
    animating = true;
    frameTimer.start();
    update();
}

double PixelButtonGlyph::progress() const {
    if (!animating) {
        return 1.0;
    }
    const double duration = kind == Kind::Lock ? PixelGlyphs::LOCK_MS : PixelGlyphs::CROSS_MS;
    return std::min(1.0, static_cast<double>(clock.elapsed()) / duration);
}

bool PixelButtonGlyph::eventFilter(QObject* watched, QEvent* event) {
    if (watched == button && event->type() == QEvent::Resize) {
        setGeometry(button->rect());
    } else if (watched == button && event->type() == QEvent::EnabledChange) {
        update();
    }
    return QWidget::eventFilter(watched, event);
}

void PixelButtonGlyph::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QColor ink = button->isEnabled() ? ENABLED_INK : DISABLED_INK;
    const QRectF box(rect().center().x() + 0.5 - glyphSize / 2.0, rect().center().y() + 0.5 - glyphSize / 2.0,
                     glyphSize, glyphSize);
    const double t = progress();
    if (kind == Kind::Lock) {
        const double target = button->isChecked() ? 0.0 : 1.0;  // open amount: 0 locked
        const double open = animating ? fromOpen + (target - fromOpen) * t : target;
        PixelGlyphs::paintLock(&painter, box, open, ink, devicePixelRatioF());
    } else {
        PixelGlyphs::paintCross(&painter, box, animating ? t : 1.0, ink, devicePixelRatioF());
    }
}
