#pragma once
#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QColorSpace>
#include <QImage>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

class QSettings;

// working colour profiles offered: id stored in the preferences, name shown, Qt's colour space
struct WorkingProfile {
    QString id;
    QString name;
    QColorSpace::NamedColorSpace space;
};
const std::vector<WorkingProfile>& workingProfiles();
QColorSpace workingColorSpace(const QString& id);  // sRGB for an unknown id
// `image`'s values in `target`, tagged with it. A picture without a profile is taken as sRGB, as every program
// does; one whose profile is not RGB (grey...) is only tagged, not converted. An untagged or sRGB picture and an
// sRGB target leave every pixel as it is.
QImage toColorSpace(const QImage& image, const QColorSpace& target);

/* User preferences (Preferences menu), kept between sessions
 * ----------------------------------------------------------
 * - navigation in the preview: smooth zoom around the pointer, pan with a right-click drag, middle-click joystick,
 *   inertia, pinch to zoom. Each can be turned off; off gives back upstream Ditherista's behaviour.
 * - screen calibration: how many screen pixels (Qt's device-independent pixels, the unit the view works in) make
 *   one inch on this monitor, measured with a ruler; 0 = not calibrated, the system's figure is used.
 * - file names: the name Save proposes, from a template - {name} picture file name, {suffix} the suffix when it
 *   is on, {ext} extension, {dither} algorithm, {dpi} output DPI.
 * - default folders for Open and Save; empty = the system's / the last one used, as before.
 * - zoom increment, view background, preview quality, working colour profile, clipboard content, recent files.
 * Stored with QSettings in an INI file: %APPDATA%/ditherista/preferences.ini on Windows.
 */
struct Preferences {
    // navigation
    // wheel zoom: smooth (animated) around the pointer, by steps around the pointer, or by steps around the
    // centre as upstream did (where the wheel up zooms out)
    enum class ZoomMode { SmoothPointer, SteppedPointer, SteppedCentre };
    ZoomMode zoomMode = ZoomMode::SmoothPointer;
    bool invertWheel = false;      // the wheel zooms the other way round
    bool wheelOverFields = true;   // false: the wheel over a number, slider or list field scrolls the panel instead
    bool dragPan = true;
    bool middleJoystick = true;
    bool inertia = true;
    bool pinchZoom = true;
    int zoomIncrement = 10;  // % per wheel notch: added (stepped zoom) or multiplied (smooth zoom)
    // view background: a grey from white (0) to black (255), or graph paper at the film's scale
    enum class Background { Solid, GraphPaperWhite, GraphPaperBlack };
    Background background = Background::Solid;
    int backgroundGrey = 128;  // mid grey, the default the slider marks
    // preview quality: the preview is rendered at this % of its resolution (100 = best); export is always full
    int previewQuality = 100;
    // colour management: the working space pictures are converted to, embedded in colour exports
    QString workingProfile = "srgb";  // see workingProfiles()
    bool embedProfile = true;
    // an edit of a built-in (or file, or reduced) palette replaces the custom palette made earlier: ask whether to
    // save that one to a file first (upstream), or always save it, or replace it without asking
    enum class CustomPaletteReplace { Ask, Save, Replace };
    CustomPaletteReplace customPaletteReplace = CustomPaletteReplace::Ask;
    // clipboard: Copy to Clipboard puts the film as an image, plus files for programs that paste files
    // when separating: copy the simulated print, or ask which channel (one ink, or every ink as files)
    enum class ClipboardContent { Composite, AskChannel };
    ClipboardContent clipboardContent = ClipboardContent::Composite;
    QString clipboardFormat = "png";  // file format of the copied files: png, tif or psd
    // File > Open Recent
    QStringList recentFiles;
    static constexpr int MAX_RECENT_FILES = 5;
    void addRecentFile(const QString& path);
    // favourite ditherers, mono and colour, as SubDitherType values (stable ids), in the order they were added
    QList<int> favoriteDitherers;
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
