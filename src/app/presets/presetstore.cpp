#include "presetstore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

static const char* NAME_KEY = "name";
static const char* SUFFIX = ".json";

PresetStore::PresetStore(QString directory) : dir(std::move(directory)) {}

QString PresetStore::defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets";
}

QString PresetStore::pathFor(const QString& name) const {
    // characters Windows refuses in file names, and leading/trailing dots or spaces
    QString file = name;
    file.replace(QRegularExpression(R"([<>:"/\\|?*\x00-\x1f])"), "_");
    file = file.trimmed();
    while (file.endsWith('.')) file.chop(1);
    if (file.isEmpty()) file = "_";
    return QDir(dir).filePath(file + SUFFIX);
}

QStringList PresetStore::names() const {
    QStringList result;
    const QFileInfoList files = QDir(dir).entryInfoList({QString("*") + SUFFIX}, QDir::Files);
    for (const QFileInfo& info : files) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        const QString name = object.value(NAME_KEY).toString();
        result << (name.isEmpty() ? info.completeBaseName() : name);  // hand-made files without a name still show
    }
    std::sort(result.begin(), result.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool PresetStore::exists(const QString& name) const {
    return QFile::exists(pathFor(name));
}

bool PresetStore::save(const QString& name, const QJsonObject& preset, QString* error) const {
    if (!QDir().mkpath(dir)) {
        *error = QObject::tr("cannot create %1").arg(dir);
        return false;
    }
    QJsonObject object = preset;
    object.insert(NAME_KEY, name);
    QSaveFile file(pathFor(name));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

QJsonObject PresetStore::load(const QString& name, QString* error) const {
    QFile file(pathFor(name));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QObject::tr("not a valid preset: %1").arg(parse.errorString());
        return {};
    }
    return document.object();
}

bool PresetStore::remove(const QString& name) const {
    return QFile::remove(pathFor(name));
}
