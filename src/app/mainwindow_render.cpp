#include "mainwindow.h"
#include "viewport/renderglyphbutton.h"

/* Render control: the round button in the preview's lower right corner.
 *
 *   Auto --click--> Paused --(settings change, any number of times)--> click --> one render --> Auto
 *
 * While paused every change goes on as usual - controls, caches, palette - except the work that makes a new result:
 * - reDither() only marks the result stale (renderDirty); a forced re-dither still drops the stale cache entry
 * - adjustImageMono/Color() hold adjustSource (tone curve, blur, denoise): sourceDirtyMono/Color
 * - Output DPI, print size and preview quality hold the resample: outputSizeDirty, the new size already checked
 * - the colour picker's live preview is off
 * The last result stays on screen. The click does the held work, then renders once through reDither().
 * Saving or copying while changes wait renders them first, so a file always matches the settings shown. */

void MainWindow::setupRenderControl() {
    renderButton = new RenderGlyphButton(ui->graphicsView, ui->graphicsView->viewport());
    connect(renderButton, &RenderGlyphButton::clicked, this, &MainWindow::renderButtonClickedSlot);
    renderButton->show();
}

void MainWindow::renderButtonClickedSlot() {
    if (isDithering || renderFlushing || renderButton->state() == RenderGlyphButton::State::Rendering) {
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
    reDither(false);  // the caches of changed settings were dropped when they changed: this renders what is stale
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
