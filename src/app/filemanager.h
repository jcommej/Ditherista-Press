#pragma once
#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>

class FileManager final : public QObject {
    Q_OBJECT
public:
    /* methods */
    FileManager();
    // defaultFolder (Preferences): where the dialog starts when set; otherwise the folder used last
    bool getOpenFileName(QString* fileName, const QString& defaultFolder = QString());
    void setDirectory(const QString &directory);
    [[nodiscard]] bool isDefaultDirectory() const;
    void clearCurrentFileName();
    // suggestedFileName: a name with its extension (see currentExtension), or without, which gets one
    QString fileSave(bool saveAs, QString suggestedFileName, const QString& defaultFolder = QString());
    [[nodiscard]] QString currentSaveFilter() const { return currentFilter; }  // film format chosen in the dialog
    [[nodiscard]] QString currentExtension() const;  // of that format, without the dot: png until one is chosen
    // save formats: lossless only, never JPEG (see export/filmwriter.h)
    static QString pngFilter();
    static QString tiffFilter();
    static QString tiffPackBitsFilter();
    static QString bmpFilter();
    static QString psdFilter();                // inks as layers (the default layout)
    static QString psdSpotChannelsFilter();
    static QString psdLayersAndSpotChannelsFilter();
    // PsdInkLayout of the PSD format chosen in the dialog (export/psdwriter.h), -1 when not a PSD
    [[nodiscard]] int currentPsdLayout() const;
private:
    /* attributes */
    bool defaultDirectory = true;
    QString fileIoLocation;
    QString currentFileName;
    QString currentFilter;
};

#endif // FILEMANAGER_H
