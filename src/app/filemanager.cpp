#include "filemanager.h"
#include "consts.h"
#include <QFileDialog>
#include <QStandardPaths>

FileManager::FileManager() {
    /* Constructor */
    if(QStringList dir = QStandardPaths::standardLocations(QStandardPaths::HomeLocation); dir.count() > 0) {
        fileIoLocation = dir[0];
    } else {
        qDebug()<<"WARNING: could not determine default load/save location";
    }
}

void FileManager::setDirectory(const QString& directory) {
    fileIoLocation = directory;
}

bool FileManager::getOpenFileName(QString* fileName, const QString& defaultFolder) {
    const QString filter = tr("Images") + " (" + FILE_FILTERS.join(" ") + ")";
    const QString start = !defaultFolder.isEmpty() && QDir(defaultFolder).exists() ? defaultFolder : fileIoLocation;
    *fileName = QFileDialog::getOpenFileName((QWidget*)parent(),
                                                    tr("Open Image File"), start,
                                                    filter);
    if(fileName->isEmpty() || fileName->isNull() || !QFile::exists(*fileName)) {
        return false; // cancelled or file does not exist
    }
    fileIoLocation = QFileInfo(*fileName).absolutePath(); // remember file location
    defaultDirectory = false;
    return(true);
}

QString FileManager::currentExtension() const {
    return currentFilter == pngFilter() || currentFilter.isEmpty() ? "png"
           : currentFilter == bmpFilter() ? "bmp" : currentPsdLayout() >= 0 ? "psd" : "tif";
}

QString FileManager::fileSave(const bool saveAs, QString suggestedFileName, const QString& defaultFolder) {
    /* Displays a file "Save-as" dialog when user chooses "Save As" and returns the file-name.
     * Displays a file "Save-as" dialog when the user chooses "Save" for the first time, then returns
     * the file name.
     * Returns an empty file-name if cancelled */
    if(!currentFileName.isEmpty() && !saveAs) {
        return currentFileName;
    }
    if (currentFileName.isEmpty()) {
        static const QStringList extensions{"png", "tif", "tiff", "bmp", "psd"};
        if (!extensions.contains(QFileInfo(suggestedFileName).suffix().toLower())) {
            suggestedFileName += "." + currentExtension();
        }
        const QString folder = !defaultFolder.isEmpty() && QDir(defaultFolder).exists() ? defaultFolder : fileIoLocation;
        suggestedFileName = folder + QDir::separator() + suggestedFileName;
    } else {
        suggestedFileName = currentFileName;
    }
    const QString filters = QStringList({pngFilter(), tiffFilter(), tiffPackBitsFilter(), bmpFilter(), psdFilter(),
                                         psdSpotChannelsFilter(), psdLayersAndSpotChannelsFilter()}).join(";;");
    QString selected = currentFilter.isEmpty() ? pngFilter() : currentFilter;
    const QString fileName = QFileDialog::getSaveFileName((QWidget*)parent(),
                                                    tr("Save Film"), suggestedFileName,
                                                    filters, &selected);
    if(!fileName.isEmpty()) {
        currentFileName = fileName;
        currentFilter = selected;
        fileIoLocation = QFileInfo(fileName).absolutePath(); // remember file location
        defaultDirectory = false;
    }
    return fileName;
}

QString FileManager::pngFilter() {
    return tr("PNG (*.png)");
}

QString FileManager::psdFilter() {
    return tr("Photoshop PSD, inks as layers (*.psd)");
}

QString FileManager::psdSpotChannelsFilter() {
    return tr("Photoshop PSD, inks as spot channels (*.psd)");
}

QString FileManager::psdLayersAndSpotChannelsFilter() {
    return tr("Photoshop PSD, inks as layers and spot channels (*.psd)");
}

int FileManager::currentPsdLayout() const {
    // PsdInkLayout: SpotChannels 0, Layers 1, LayersAndSpotChannels 2
    return currentFilter == psdFilter() ? 1 : currentFilter == psdSpotChannelsFilter() ? 0
           : currentFilter == psdLayersAndSpotChannelsFilter() ? 2 : -1;
}

QString FileManager::bmpFilter() {
    return tr("BMP (*.bmp)");
}

QString FileManager::tiffFilter() {
    return tr("TIFF, uncompressed (*.tif *.tiff)");
}

QString FileManager::tiffPackBitsFilter() {
    return tr("TIFF, lossless PackBits compression (*.tif *.tiff)");
}

bool FileManager::isDefaultDirectory() const {
    return defaultDirectory;
}

void FileManager::clearCurrentFileName() {
    currentFileName = "";
}