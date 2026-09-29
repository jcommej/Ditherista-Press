#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include "presets/presetstore.h"

/* Tests for preset storage (presets/presetstore.h). What a preset contains, and applying it, is checked in the
 * running app. */

class TestPresets : public QObject {
    Q_OBJECT
private slots:
    void savedPresetReadsBackIdentical() {
        QTemporaryDir dir;
        const PresetStore store(dir.filePath("presets"));  // created on first save
        QJsonObject preset{{"screen", QJsonObject{{"dpi", 600}, {"lpi", 45.5}, {"lpiEnabled", true}}},
                           {"separation", QJsonObject{{"angles", QJsonArray{15, 75, 0, 45}}}}};
        QString error;
        QVERIFY2(store.save("T-shirt 45 LPI", preset, &error), qPrintable(error));
        QVERIFY(store.exists("T-shirt 45 LPI"));
        const QJsonObject back = store.load("T-shirt 45 LPI", &error);
        QCOMPARE(back.value("screen").toObject(), preset.value("screen").toObject());
        QCOMPARE(back.value("separation").toObject().value("angles").toArray(), (QJsonArray{15, 75, 0, 45}));
        QCOMPARE(back.value("name").toString(), QString("T-shirt 45 LPI"));
    }

    void namesWithForbiddenCharactersKeepTheirName() {
        QTemporaryDir dir;
        const PresetStore store(dir.path());
        QString error;
        const QString name = "Film 1/2: \"quadri\" <600>?";
        QVERIFY(store.save(name, {}, &error));
        QVERIFY(QFile::exists(store.pathFor(name)));
        QVERIFY(!QFileInfo(store.pathFor(name)).fileName().contains('/'));
        QCOMPARE(store.names(), QStringList({name}));  // the real name, read from inside the file
    }

    void listIsSortedAndOverwriteReplaces() {
        QTemporaryDir dir;
        const PresetStore store(dir.path());
        QString error;
        store.save("zeta", {{"v", 1}}, &error);
        store.save("Alpha", {{"v", 1}}, &error);
        store.save("beta", {{"v", 1}}, &error);
        store.save("beta", {{"v", 2}}, &error);  // same name: replaced, not duplicated
        QCOMPARE(store.names(), QStringList({"Alpha", "beta", "zeta"}));
        QCOMPARE(store.load("beta", &error).value("v").toInt(), 2);
    }

    void removeAndBrokenFiles() {
        QTemporaryDir dir;
        const PresetStore store(dir.path());
        QString error;
        store.save("gone", {}, &error);
        QVERIFY(store.remove("gone"));
        QVERIFY(store.names().isEmpty());
        QFile broken(dir.filePath("broken.json"));
        broken.open(QIODevice::WriteOnly);
        broken.write("{ not json");
        broken.close();
        QVERIFY(store.load("broken", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QObject* newTestPresets() { return new TestPresets; }
#include "tst_presets.moc"
