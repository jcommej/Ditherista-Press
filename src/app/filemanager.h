#pragma once
#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>

class FileManager final : public QObject {
    Q_OBJECT
public:
    /* methods */
    FileManager();
    bool getOpenFileName(QString* fileName);
    void setDirectory(const QString &directory);
    [[nodiscard]] bool isDefaultDirectory() const;
    void clearCurrentFileName();
    QString fileSave(bool saveAs, QString suggestedFileName);
    [[nodiscard]] QString currentSaveFilter() const { return currentFilter; }  // film format chosen in the dialog
    // save formats: lossless only, never JPEG (see export/filmwriter.h)
    static QString pngFilter();
    static QString tiffFilter();
    static QString tiffPackBitsFilter();
    static QString bmpFilter();
private:
    /* attributes */
    bool defaultDirectory = true;
    QString fileIoLocation;
    QString currentFileName;
    QString currentFilter;
};

#endif // FILEMANAGER_H
