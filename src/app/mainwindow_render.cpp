#include "mainwindow.h"
#include "viewport/renderglyphbutton.h"
#include <QKeyEvent>
#include <functional>

/* Render control: the round button in the preview's lower right corner.
 *
 *   Auto --click--> Paused --(settings change, any number of times)--> click --> one render --> Auto
 *   during any render: click (or Esc) --> stopped, the change undone, Paused (renderStopped)
 *
 * While paused every change goes on as usual - controls, caches, palette - except the work that makes a new result:
 * - reDither() only marks the result stale (renderDirty); a forced re-dither still drops the stale cache entry
 * - adjustImageMono/Color() hold adjustSource (tone curve, blur, denoise): sourceDirtyMono/Color
 * - Output DPI, print size and preview quality hold the resample: outputSizeDirty, the new size already checked
 * - the colour picker's live preview is off
 * The last result stays on screen. The click does the held work, then renders once through reDither().
 * Saving or copying while changes wait renders them first, so a file always matches the settings shown. */

namespace {
/* While a ditherer runs, runDitherThread keeps the GUI painting with processEvents. User input is held back then -
 * nothing may change the picture under the ditherer - except the render control and Esc, which stop the render.
 * The window cannot be closed meanwhile either. */
class RenderInputGate final : public QObject {
public:
    RenderInputGate(QObject* parent, std::function<bool()> active, QWidget* allowed, std::function<void()> stop)
        : QObject(parent), active(std::move(active)), allowed(allowed), stop(std::move(stop)) {}
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!active()) {
            return false;
        }
        switch (event->type()) {
            case QEvent::KeyPress:
                if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                    stop();
                }
                return true;
            case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick:
            case QEvent::MouseMove: case QEvent::Enter: case QEvent::Leave: case QEvent::HoverMove:
                return watched != allowed;  // the render control's own events go through
            case QEvent::KeyRelease: case QEvent::ShortcutOverride: case QEvent::Shortcut: case QEvent::Wheel:
            case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd: case QEvent::ContextMenu:
            case QEvent::NonClientAreaMouseButtonPress: case QEvent::NonClientAreaMouseButtonDblClick:
            case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop: case QEvent::NativeGesture:
            case QEvent::Gesture: case QEvent::TabletPress:
                return true;
            case QEvent::Close:
                event->ignore();
                return true;
            default:
                return false;
        }
    }
private:
    std::function<bool()> active;
    QWidget* allowed;
    std::function<void()> stop;
};
}  // namespace

void MainWindow::setupRenderControl() {
    renderButton = new RenderGlyphButton(ui->graphicsView, ui->graphicsView->viewport());
    connect(renderButton, &RenderGlyphButton::clicked, this, &MainWindow::renderButtonClickedSlot);
    renderButton->show();
    qApp->installEventFilter(new RenderInputGate(this, [this]() { return isDithering; }, renderButton,
                                                 [this]() { requestRenderStop(); }));
}

void MainWindow::requestRenderStop() {
    /* the click or Esc during a render: runDitherThread lets the ditherer go and unwinds back to reDither */
    if (isDithering && renderStoppable) {
        renderStopRequested = true;
    }
}

void MainWindow::renderStopped() {
    /* after a stopped render, once the call stack has unwound: the change that started it is undone (as Ctrl+Z
     * would - the history had not recorded it yet) and the control pauses; the previous result is still shown.
     * A render asked from the paused control keeps its settings: it waits there for the next click. */
    renderPaused = true;
    renderButton->setState(RenderGlyphButton::State::Paused);
    if (!stoppedFlush) {
        restoreSession(history.current());
    }
    renderDirty = true;
    stoppedFlush = false;
    notification->showText(tr("render stopped"), 1500);
}

void MainWindow::renderButtonClickedSlot() {
    if (isDithering) {
        requestRenderStop();  // the click stops the render
        return;
    }
    if (renderFlushing || renderButton->state() == RenderGlyphButton::State::Rendering) {
        return;  // one render at a time
    }
    if (!renderPaused) {
        renderPaused = true;
        renderButton->setState(RenderGlyphButton::State::Paused);
        return;
    }
    renderButton->setState(RenderGlyphButton::State::Rendering);
    renderPending();
    renderPaused = false;  // back to automatic rendering, everything shown being up to date
    renderButton->setState(RenderGlyphButton::State::Auto);
}

void MainWindow::renderPending() {
    /* the work held while paused, then one render with every current setting. renderPaused stays on during the
     * preparation, so the re-dithers that applyOutputSize and adjustImage* ask for are held and only the last
     * reDither runs */
    if (firstLoad || isDithering) {
        return;
    }
    renderFlushing = true;
    if (outputSizeDirty) {
        // resamples, and adjusts both pictures again with the current adjustments: adjustSource is done too
        if (applyOutputSize(screenGeometry.dpi, printWidthMm, printHeightMm)) {
            sourceDirtyMono = sourceDirtyColor = false;
        }
        outputSizeDirty = false;
    }
    if (sourceDirtyMono) {
        sourceDirtyMono = false;
        adjustImageMono();
    }
    if (sourceDirtyColor) {
        sourceDirtyColor = false;
        adjustImageColor();
    }
    renderFlushing = false;
    const bool paused = renderPaused;
    renderPaused = false;
    renderFromFlush = true;  // a stop keeps these settings (renderStopped)
    reDither(false);  // the caches of changed settings were dropped when they changed: this renders what is stale
    renderFromFlush = false;
    renderPaused = paused;
    renderDirty = false;
}

void MainWindow::renderBeforeExport() {
    /* a file or a copy is made from the current settings: render what waits first, staying paused */
    if (!renderPaused || !renderDirty || firstLoad || isDithering) {
        return;
    }
    renderButton->setState(RenderGlyphButton::State::Rendering);
    renderPending();
    renderButton->setState(RenderGlyphButton::State::Paused);
}

bool MainWindow::requestOutputSize(const double dpi, const double widthMm, const double heightMm) {
    /* applyOutputSize, or while paused: the new film size is checked and kept, the resample waits for the render */
    if (!renderPaused || renderFlushing) {
        return applyOutputSize(dpi, widthMm, heightMm);
    }
    if (!outputSizeFits(dpi, widthMm, heightMm)) {
        return false;
    }
    screenGeometry.dpi = dpi;
    printWidthMm = widthMm;
    printHeightMm = heightMm;
    outputSizeDirty = true;
    updateScreenControls();
    renderDirty = true;
    return true;
}
