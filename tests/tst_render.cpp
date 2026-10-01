#include <QtTest>
#include <QSignalSpy>
#include "viewport/renderglyphbutton.h"

/* The render control's button (viewport/renderglyphbutton.h): its two glyphs, the breathing of the matrix, the
 * states, clicks and its place in the corner. What a click does to rendering is MainWindow's (mainwindow_render.cpp). */
class TestRenderButton : public QObject {
    Q_OBJECT
private slots:
    void glyphsDifferAndAreSymmetric() {
        using B = RenderGlyphButton;
        int autoCount = 0;
        int pauseCount = 0;
        bool differ = false;
        for (int y = 0; y < B::GRID; y++) {
            for (int x = 0; x < B::GRID; x++) {
                autoCount += B::AUTO_GLYPH[y][x];
                pauseCount += B::PAUSE_GLYPH[y][x];
                differ |= B::AUTO_GLYPH[y][x] != B::PAUSE_GLYPH[y][x];
                QCOMPARE(B::AUTO_GLYPH[y][x], B::AUTO_GLYPH[y][B::GRID - 1 - x]);  // mirror images left/right
                QCOMPARE(B::AUTO_GLYPH[y][x], B::AUTO_GLYPH[B::GRID - 1 - y][x]);  // and top/bottom
                QCOMPARE(B::PAUSE_GLYPH[y][x], B::PAUSE_GLYPH[y][B::GRID - 1 - x]);
            }
        }
        QVERIFY(differ);
        QCOMPARE(pauseCount, 4 * B::GRID);  // two bars, two squares wide
        QCOMPARE(autoCount, 25);            // the ring of the mock-up, with its centre square
    }

    void breathingStaysInRangeAndMovesEachSquare() {
        double minScale = 1.0, maxScale = 0.0;
        for (int step = 0; step < 200; step++) {
            for (int y = 0; y < RenderGlyphButton::GRID; y++) {
                for (int x = 0; x < RenderGlyphButton::GRID; x++) {
                    double scale = 0.0;
                    double opacity = 0.0;
                    RenderGlyphButton::breathing(x, y, step * 17.0, &scale, &opacity);
                    QVERIFY(scale >= 0.58 - 1e-9 && scale <= 0.96 + 1e-9);
                    QVERIFY(opacity >= 125.0 / 255 - 1e-9 && opacity <= 230.0 / 255 + 1e-9);
                    minScale = std::min(minScale, scale);
                    maxScale = std::max(maxScale, scale);
                }
            }
        }
        QVERIFY(maxScale - minScale > 0.3);  // a visible breath
        // individually: at one moment two squares are not the same size
        double a = 0.0, b = 0.0, o = 0.0;
        RenderGlyphButton::breathing(0, 0, 300.0, &a, &o);
        RenderGlyphButton::breathing(6, 6, 300.0, &b, &o);
        QVERIFY(std::abs(a - b) > 0.01);
        // and one square changes with time
        RenderGlyphButton::breathing(3, 3, 0.0, &a, &o);
        RenderGlyphButton::breathing(3, 3, 400.0, &b, &o);
        QVERIFY(std::abs(a - b) > 0.01);
    }

    void statesAndToolTips() {
        QWidget parent;
        QWidget anchor(&parent);
        RenderGlyphButton button(&parent, &anchor);
        QCOMPARE(button.state(), RenderGlyphButton::State::Auto);
        const QString autoTip = button.toolTip();
        button.setState(RenderGlyphButton::State::Paused);
        QCOMPARE(button.state(), RenderGlyphButton::State::Paused);
        QVERIFY(button.toolTip() != autoTip);
        button.setState(RenderGlyphButton::State::Rendering);
        QCOMPARE(button.state(), RenderGlyphButton::State::Rendering);
        button.setState(RenderGlyphButton::State::Auto);  // a quick render: the state is Auto at once
        QCOMPARE(button.state(), RenderGlyphButton::State::Auto);
        QCOMPARE(button.toolTip(), autoTip);
        QCOMPARE(button.focusPolicy(), Qt::NoFocus);  // Space stays with the view
    }

    void activityBreathesThenGivesTheStateBack() {
        QWidget parent;
        QWidget anchor(&parent);
        RenderGlyphButton button(&parent, &anchor);
        for (const auto start : {RenderGlyphButton::State::Auto, RenderGlyphButton::State::Paused}) {
            button.setState(start);
            {
                const RenderGlyphActivity outer(&button);  // an automatic render, or Save
                QCOMPARE(button.state(), RenderGlyphButton::State::Rendering);
                {
                    const RenderGlyphActivity inner(&button);  // nested: leaves it to the outer one
                }
                QCOMPARE(button.state(), RenderGlyphButton::State::Rendering);
            }
            QCOMPARE(button.state(), start);
        }
        const RenderGlyphActivity none(nullptr);  // no button yet: nothing happens
    }

    void clickInsideTheCircleOnly() {
        QWidget parent;
        parent.resize(300, 200);
        QWidget anchor(&parent);
        anchor.setGeometry(0, 0, 300, 200);
        RenderGlyphButton button(&parent, &anchor);
        parent.show();
        QSignalSpy clicked(&button, &RenderGlyphButton::clicked);
        QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier, button.rect().center());
        QCOMPARE(clicked.count(), 1);
        QTest::mouseClick(&button, Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));  // the corner, outside the disc
        QCOMPARE(clicked.count(), 1);
        QTest::mouseClick(&button, Qt::RightButton, Qt::NoModifier, button.rect().center());
        QCOMPARE(clicked.count(), 1);
    }

    void staysInTheLowerRightCorner() {
        QWidget parent;
        parent.resize(400, 300);
        QWidget anchor(&parent);
        anchor.setGeometry(0, 0, 385, 285);  // like a viewport with its scroll bars beside it
        RenderGlyphButton button(&parent, &anchor);
        parent.show();  // a hidden widget gets its resize events only once shown
        const auto check = [&]() {
            const QRect area = anchor.geometry();
            const QRect g = button.geometry();
            QVERIFY(area.contains(g));
            QVERIFY(area.right() - g.right() < 20);   // close to the corner, inside the viewport
            QVERIFY(area.bottom() - g.bottom() < 20);
        };
        check();
        anchor.setGeometry(0, 0, 600, 500);
        check();
        anchor.setGeometry(0, 0, 200, 150);
        check();
    }
};

QObject* newTestRenderButton() { return new TestRenderButton; }
#include "tst_render.moc"
