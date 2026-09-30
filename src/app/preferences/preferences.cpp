#include "preferences.h"
#include <QRegularExpression>
#include <QSettings>

void Preferences::load(QSettings& settings) {
    const Preferences defaults;
    smoothZoom = settings.value("navigation/smoothZoom", defaults.smoothZoom).toBool();
    rightDragPan = settings.value("navigation/rightDragPan", defaults.rightDragPan).toBool();
    middleJoystick = settings.value("navigation/middleJoystick", defaults.middleJoystick).toBool();
    inertia = settings.value("navigation/inertia", defaults.inertia).toBool();
    pinchZoom = settings.value("navigation/pinchZoom", defaults.pinchZoom).toBool();
    screenPpi = settings.value("screen/ppi", defaults.screenPpi).toDouble();
    autoSuffix = settings.value("files/autoSuffix", defaults.autoSuffix).toBool();
    suffix = settings.value("files/suffix", defaults.suffix).toString();
    nameTemplate = settings.value("files/template", defaults.nameTemplate).toString();
    openFolder = settings.value("folders/open", defaults.openFolder).toString();
    saveFolder = settings.value("folders/save", defaults.saveFolder).toString();
}

void Preferences::save(QSettings& settings) const {
    settings.setValue("navigation/smoothZoom", smoothZoom);
    settings.setValue("navigation/rightDragPan", rightDragPan);
    settings.setValue("navigation/middleJoystick", middleJoystick);
    settings.setValue("navigation/inertia", inertia);
    settings.setValue("navigation/pinchZoom", pinchZoom);
    settings.setValue("screen/ppi", screenPpi);
    settings.setValue("files/autoSuffix", autoSuffix);
    settings.setValue("files/suffix", suffix);
    settings.setValue("files/template", nameTemplate);
    settings.setValue("folders/open", openFolder);
    settings.setValue("folders/save", saveFolder);
    settings.sync();
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
