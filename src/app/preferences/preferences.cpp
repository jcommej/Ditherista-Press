#include "preferences.h"
#include <QObject>
#include <QRegularExpression>
#include <QSettings>
#include <algorithm>

void Preferences::load(QSettings& settings) {
    const Preferences defaults;
    // zoomMode replaced the earlier on/off smoothZoom: off meant upstream's steps around the centre
    const int oldMode = settings.value("navigation/smoothZoom", true).toBool() ? 0 : 2;
    zoomMode = static_cast<ZoomMode>(std::clamp(settings.value("navigation/zoomMode", oldMode).toInt(), 0, 2));
    invertWheel = settings.value("navigation/invertWheel", defaults.invertWheel).toBool();
    wheelOverFields = settings.value("navigation/wheelOverFields", defaults.wheelOverFields).toBool();
    dragPan = settings.value("navigation/dragPan", defaults.dragPan).toBool();
    middleJoystick = settings.value("navigation/middleJoystick", defaults.middleJoystick).toBool();
    inertia = settings.value("navigation/inertia", defaults.inertia).toBool();
    pinchZoom = settings.value("navigation/pinchZoom", defaults.pinchZoom).toBool();
    zoomIncrement = std::clamp(settings.value("navigation/zoomIncrement", defaults.zoomIncrement).toInt(), 1, 100);
    background = static_cast<Background>(std::clamp(settings.value("view/background", static_cast<int>(defaults.background)).toInt(), 0, 2));
    backgroundGrey = std::clamp(settings.value("view/backgroundGrey", defaults.backgroundGrey).toInt(), 0, 255);
    previewQuality = settings.value("view/previewQuality", defaults.previewQuality).toInt();
    if (previewQuality != 50 && previewQuality != 75) {
        previewQuality = 100;
    }
    workingProfile = settings.value("color/workingProfile", defaults.workingProfile).toString();
    embedProfile = settings.value("color/embedProfile", defaults.embedProfile).toBool();
    clipboardContent = static_cast<ClipboardContent>(std::clamp(settings.value("clipboard/content", static_cast<int>(defaults.clipboardContent)).toInt(), 0, 1));
    clipboardFormat = settings.value("clipboard/format", defaults.clipboardFormat).toString();
    if (clipboardFormat != "tif" && clipboardFormat != "psd") {
        clipboardFormat = "png";
    }
    recentFiles = settings.value("recent/files").toStringList().mid(0, MAX_RECENT_FILES);
    favoriteDitherers.clear();
    for (const QString& id : settings.value("favorites/ditherers").toStringList()) {
        bool ok = false;
        const int value = id.toInt(&ok);
        if (ok && !favoriteDitherers.contains(value)) {
            favoriteDitherers.append(value);
        }
    }
    screenPpi = settings.value("screen/ppi", defaults.screenPpi).toDouble();
    autoSuffix = settings.value("files/autoSuffix", defaults.autoSuffix).toBool();
    suffix = settings.value("files/suffix", defaults.suffix).toString();
    nameTemplate = settings.value("files/template", defaults.nameTemplate).toString();
    openFolder = settings.value("folders/open", defaults.openFolder).toString();
    saveFolder = settings.value("folders/save", defaults.saveFolder).toString();
}

void Preferences::save(QSettings& settings) const {
    settings.setValue("navigation/zoomMode", static_cast<int>(zoomMode));
    settings.setValue("navigation/invertWheel", invertWheel);
    settings.setValue("navigation/wheelOverFields", wheelOverFields);
    settings.remove("navigation/smoothZoom");
    settings.remove("navigation/rightDragPan");  // before Drag to Pan
    settings.setValue("navigation/dragPan", dragPan);
    settings.setValue("navigation/middleJoystick", middleJoystick);
    settings.setValue("navigation/inertia", inertia);
    settings.setValue("navigation/pinchZoom", pinchZoom);
    settings.setValue("navigation/zoomIncrement", zoomIncrement);
    settings.setValue("view/background", static_cast<int>(background));
    settings.setValue("view/backgroundGrey", backgroundGrey);
    settings.setValue("view/previewQuality", previewQuality);
    settings.setValue("color/workingProfile", workingProfile);
    settings.setValue("color/embedProfile", embedProfile);
    settings.setValue("clipboard/content", static_cast<int>(clipboardContent));
    settings.setValue("clipboard/format", clipboardFormat);
    settings.setValue("recent/files", recentFiles);
    QStringList favorites;
    for (const int id : favoriteDitherers) {
        favorites << QString::number(id);
    }
    settings.setValue("favorites/ditherers", favorites);
    settings.setValue("screen/ppi", screenPpi);
    settings.setValue("files/autoSuffix", autoSuffix);
    settings.setValue("files/suffix", suffix);
    settings.setValue("files/template", nameTemplate);
    settings.setValue("folders/open", openFolder);
    settings.setValue("folders/save", saveFolder);
    settings.sync();
}

void Preferences::addRecentFile(const QString& path) {
    recentFiles.removeAll(path);
    recentFiles.prepend(path);
    while (recentFiles.size() > MAX_RECENT_FILES) {
        recentFiles.removeLast();
    }
}

const std::vector<WorkingProfile>& workingProfiles() {
    static const std::vector<WorkingProfile> profiles = {
        {"srgb", QObject::tr("sRGB IEC61966-2.1 (default)"), QColorSpace::SRgb},
        {"adobergb", QObject::tr("Adobe RGB (1998)"), QColorSpace::AdobeRgb},
        {"displayp3", QObject::tr("Display P3"), QColorSpace::DisplayP3},
        {"prophoto", QObject::tr("ProPhoto RGB"), QColorSpace::ProPhotoRgb},
        {"rec2020", QObject::tr("Rec. 2020 (BT.2020)"), QColorSpace::Bt2020},
    };
    return profiles;
}

QColorSpace workingColorSpace(const QString& id) {
    for (const WorkingProfile& profile : workingProfiles()) {
        if (profile.id == id) {
            return QColorSpace(profile.space);
        }
    }
    return QColorSpace(QColorSpace::SRgb);
}

QImage toColorSpace(const QImage& image, const QColorSpace& target) {
    QImage result = image.depth() < 24 ? image.convertToFormat(QImage::Format_ARGB32) : image;
    QColorSpace source = image.colorSpace();
    if (!source.isValid()) {
        source = QColorSpace(QColorSpace::SRgb);
    }
    if (source.colorModel() != QColorSpace::ColorModel::Rgb) {
        return result;
    }
    result.setColorSpace(source);
    if (source != target) {
        result.convertToColorSpace(target);
    }
    return result;
}

QString fileNameFromTemplate(const Preferences& preferences, const FileNameFields& fields) {
    const QString name = fields.name.isEmpty() ? QStringLiteral("image") : fields.name;
    const QString dpi = fields.dpi > 0.0 ? QString::number(fields.dpi, 'f', 0) : QString();
    const auto fill = [&](QString text, const QString& suffix) {
        return text.replace("{name}", name).replace("{suffix}", suffix).replace("{dither}", fields.dither)
                   .replace("{dpi}", dpi).replace("{ext}", fields.ext);
    };
    const QString suffix = preferences.autoSuffix ? fill(preferences.suffix, QString()) : QString();
    QString result = fill(preferences.nameTemplate.trimmed().isEmpty() ? QStringLiteral("{name}{suffix}.{ext}")
                                                                       : preferences.nameTemplate, suffix);
    if (!preferences.nameTemplate.contains("{ext}") && !preferences.nameTemplate.trimmed().isEmpty()) {
        result += "." + fields.ext;
    }
    static const QRegularExpression forbidden(R"([\\/:*?"<>|\x00-\x1F])");
    result.replace(forbidden, "_");
    return result.trimmed();
}
