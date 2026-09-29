#include "mainwindow.h"
#include "modernredux/style.h"
#include "consts.h"
#include <QClipboard>
#include <QMimeData>

void MainWindow::runDitherThread() {
    /* executes a ditherer in a thread */
    isDithering = true;
    while(!fthread.isFinished()) {
        QThread::msleep(THREAD_SLEEP_DELAY_MS);
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); // keep GUI thread alive while dithering
    }
    fthread.waitForFinished();
    isDithering = false;
}

/**************************************
 * MENU ACTIONS SLOTS - FILE I/O      *
 **************************************/

void MainWindow::fileOpenSlot() {
    /* shows a file open dialog and loads an image file from disk */
    QString fileName;
    if(fileManager.getOpenFileName(&fileName)) {
        loadImageFromFileSlot(fileName);
    }
}

void MainWindow::fileSaveAsSlot() {
    /* save current image under new name */
    fileSaveSlotImpl(true);
}

void MainWindow::fileSaveSlot() {
    /* save current image */
    fileSaveSlotImpl(false);
}

void MainWindow::fileSaveSlotImpl(const bool saveAs) {
    QString fileName;
    fileName = activeTreeWidget->getCurrentDitherFileName();
    if(!fileName.isEmpty()) {
        fileName = fileManager.fileSave(saveAs, fileName);
        if (!fileName.isEmpty()) {
            saveFile(fileName);
        }
    }
}

/**************************************
 * MENU ACTIONS SLOTS - COPY & PASTE  *
 **************************************/

void MainWindow::copySlot() {
    /* copies the film - at the output DPI, not the possibly lighter preview - for a quick paste into an editor.
     * The clipboard carries pixels only, no resolution: the pasted image has the right pixel count, and its DPI
     * is set in the editor (in Photoshop: Image Size, Resample off) */
    if (isActiveWindow()) {
        setMouseBusy(true);
        const QImage film = renderFilm().convertToFormat(QImage::Format_ARGB32);
        QGuiApplication::clipboard()->setImage(film, QClipboard::Clipboard);
        setMouseBusy(false);
        notification->showText(tr("film copied: %1 × %2 px\nset the resolution to %3 DPI after pasting")
                                   .arg(film.width()).arg(film.height()).arg(screenGeometry.dpi, 0, 'f', 0), 3000);
    }
}

void MainWindow::pasteSlot() {
    /* user pasted an image or a file name from the clipboard*/
    if (isActiveWindow()) {
        const QClipboard *clipboard = QGuiApplication::clipboard();
        if (const QMimeData *md = clipboard->mimeData(); md->hasUrls()) { // pasted content is a file name or path
            if (md->urls()[0].isLocalFile()) {
                loadImageFromFileSlot(md->urls()[0].toLocalFile());
            }
        } else { // pasted content is a supported image
            const QImage image = clipboard->image(QClipboard::Clipboard);
            if (!image.isNull()) {
                loadImage(&image);
            }
        }
    }
}
