#pragma once
#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QString>

class QSettings;

/* User preferences (Preferences menu), kept between sessions
 * ----------------------------------------------------------
 * - navigation in the preview: smooth zoom around the pointer, pan with a right-click drag, middle-click joystick,
 *   inertia, pinch to zoom. Each can be turned off; off gives back upstream Ditherista's behaviour.
 * - screen calibration: how many screen pixels (Qt's device-independent pixels, the unit the view works in) make
 *   one inch on this monitor, measured with a ruler; 0 = not calibrated, the system's figure is used.
 * - file names: the name Save proposes, from a template - {name} picture file name, {suffix} the suffix when it
 *   is on, {ext} extension, {dither} algorithm, {dpi} output DPI.
 * - default folders for Open and Save; empty = the system's / the last one used, as before.
 * Stored with QSettings in an INI file: %APPDATA%/ditherista/preferences.ini on Windows.
 */
struct Preferences {
    // navigation
    bool smoothZoom = true;
    bool rightDragPan = true;
    bool middleJoystick = true;
    bool inertia = true;
    bool pinchZoom = true;
    // screen
    double screenPpi = 0.0;
    // file names
    bool autoSuffix = true;
    QString suffix = "_{dither}";
    QString nameTemplate = "{name}{suffix}.{ext}";
    // folders
    QString openFolder;
    QString saveFolder;

    void load(QSettings& settings);
    void save(QSettings& settings) const;
};

struct FileNameFields {
    QString name;    // picture file name without extension; empty = pasted picture
    QString dither;  // algorithm, e.g. "errordiff_floyd-steinberg"
    double dpi = 0.0;
    QString ext;     // without the dot
};

// The file name the template gives, characters Windows refuses in file names replaced by '_'. {suffix} is the
// suffix (its own fields filled in too) when autoSuffix is on, nothing otherwise. A template without {ext} gets
// ".ext" appended, so a film always has its extension.
QString fileNameFromTemplate(const Preferences& preferences, const FileNameFields& fields);

#endif // PREFERENCES_H
