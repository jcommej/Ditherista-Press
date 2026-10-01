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
    if(fileManager.getOpenFileName(&fileName, preferences.openFolder)) {
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
    QString fileName = suggestedFileName();  // Preferences > Filename Settings
    if(!fileName.isEmpty()) {
        fileName = fileManager.fileSave(saveAs, fileName, preferences.saveFolder);
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
     * Pasted as an image it carries no resolution: the pixel count is right, and the DPI is set in the editor
     * (in Photoshop: Image Size, Resample off). Pasted as a file (Explorer...), the DPI is in the file. */
    if (isActiveWindow()) {
        copyToClipboard();  // pixels and files, as File > Copy to Clipboard (mainwindow_preferences.cpp)
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
                sourceFileName.clear();  // a pasted picture has no file name: {name} becomes "image"
                loadImage(&image);
            }
        }
    }
}
