#pragma once
#ifndef PRESETSTORE_H
#define PRESETSTORE_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

/* Presets on disk: one readable JSON file per preset, in a folder (by default the app's data folder, e.g.
 * %APPDATA%/ditherista/presets). Files can be copied between machines or shared. What a preset holds is decided
 * by MainWindow (mainwindow_presets.cpp); this class only names, lists, reads and writes them.
 *
 * A preset's name is what the user typed; its file name is that name with the characters Windows forbids in
 * file names replaced, and the real name is kept inside the file. */
class PresetStore {
public:
    explicit PresetStore(QString directory);
    static QString defaultDirectory();

    [[nodiscard]] QStringList names() const;  // sorted, case-insensitively
    [[nodiscard]] bool exists(const QString& name) const;
    bool save(const QString& name, const QJsonObject& preset, QString* error) const;  // atomic, overwrites
    [[nodiscard]] QJsonObject load(const QString& name, QString* error) const;         // empty on error
    bool remove(const QString& name) const;
    [[nodiscard]] QString directory() const { return dir; }
    [[nodiscard]] QString pathFor(const QString& name) const;

private:
    QString dir;
};

#endif // PRESETSTORE_H
