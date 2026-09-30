#include "mainwindow.h"
#include "treewidget.h"
#include "export/psdwriter.h"
#include "ui_elements/signalblocker.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QTreeWidgetItemIterator>

/* This file contains:
 * - presets: capturing every setting that shapes a film into JSON, applying one back, and the Presets bar.
 *   Storage itself is presets/presetstore.h.
 *
 * A preset holds the tab (Mono or Color), the ditherer and its own settings, output DPI, LPI and dot size, the
 * Input Image Settings of that tab, the colour separation (mode, save choice, each ink's LPI, angle and
 * on/off), the mono ink colours and the colour palette. The print size is left out on purpose: it belongs to
 * the picture, not to the way it is screened.
 */

static const char* PRESET_FORMAT = "ditherista-screenprinting-preset";
static constexpr int PRESET_VERSION = 1;
static const char* ADJUSTMENTS[] = {"brightness", "contrast", "gamma", "blacks", "shadows", "midtones",
                                    "highlights", "whites", "blur", "denoise"};

/****************
 * PRESETS BAR  *
 ****************/

void MainWindow::setupPresetControls() {
    presetStore = std::make_unique<PresetStore>(PresetStore::defaultDirectory());
    presetGroup = new QGroupBox(tr("Presets"), ui->imageSettingsContainer);
    presetGroup->setToolTip(tr("Saved in %1").arg(presetStore->directory()));
    QHBoxLayout* row = new QHBoxLayout(presetGroup);
    presetCombo = new QComboBox(presetGroup);
    presetCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // long names must not widen the panel
    QPushButton* load = new QPushButton(tr("Load"), presetGroup);
    QPushButton* save = new QPushButton(tr("Save…"), presetGroup);
    QPushButton* remove = new QPushButton(tr("Delete"), presetGroup);
    load->setToolTip(tr("Apply the selected preset"));
    save->setToolTip(tr("Save the current settings as a preset"));
    remove->setToolTip(tr("Delete the selected preset"));
    row->addWidget(presetCombo, 1);
    row->addWidget(load);
    row->addWidget(save);
    row->addWidget(remove);
    presetGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    connect(load, &QPushButton::clicked, this, [this]() {
        if (!presetCombo->currentText().isEmpty()) {
            loadPresetNamed(presetCombo->currentText());
        }
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Preset"), tr("Preset name:"), QLineEdit::Normal,
                                                   presetCombo->currentText(), &ok).trimmed();
        if (!ok || name.isEmpty()) {
            return;
        }
        if (presetStore->exists(name) &&
            QMessageBox::question(this, tr("Save Preset"), tr("Replace the preset \"%1\"?").arg(name)) != QMessageBox::Yes) {
            return;
        }
        savePresetNamed(name);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        const QString name = presetCombo->currentText();
        if (name.isEmpty() ||
            QMessageBox::question(this, tr("Delete Preset"), tr("Delete the preset \"%1\"?").arg(name)) != QMessageBox::Yes) {
            return;
        }
        presetStore->remove(name);
        refreshPresetList(QString());
    });
    refreshPresetList(QString());
}

void MainWindow::refreshPresetList(const QString& select) {
    QSignalBlocker blocker(presetCombo);
    presetCombo->clear();
    presetCombo->addItems(presetStore->names());
    if (!select.isEmpty()) {
        presetCombo->setCurrentText(select);
    }
}

void MainWindow::savePresetNamed(const QString& name) {
    QString error;
    if (presetStore->save(name, capturePreset(), &error)) {
        refreshPresetList(name);
        notification->showText(tr("preset saved: %1").arg(name), 2000);
    } else {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" + tr("preset not saved") + "\n" + error, 3000);
    }
}

void MainWindow::loadPresetNamed(const QString& name) {
    QString error;
    const QJsonObject preset = presetStore->load(name, &error);
    if (preset.value("format").toString() != PRESET_FORMAT) {
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" + tr("not a preset: %1").arg(name) +
                               (error.isEmpty() ? QString() : "\n" + error), 3000);
        return;
    }
    applyPreset(preset);
    notification->showText(tr("preset loaded: %1").arg(name), 2000);
}

/****************
 * CAPTURE      *
 ****************/

QJsonObject MainWindow::capturePreset() const {
    const bool mono = lastTabIndex == TAB_INDEX_MONO;
    QJsonObject preset{{"format", PRESET_FORMAT}, {"version", PRESET_VERSION}, {"tab", mono ? "mono" : "color"}};

    // ditherer and its own settings (jitter, serpentine, step...)
    QJsonObject settings;
    for (int key = setting_serpentine; key <= setting_use_riemersma; key++) {
        const QVariant value = activeTreeWidget->getValue(current_sub_dither_type, static_cast<SettingKey>(key));
        if (value.isValid()) {
            settings.insert(QString::number(key), QJsonValue::fromVariant(value));
        }
    }
    preset.insert("ditherer", QJsonObject{
        {"type", static_cast<int>(current_dither_type)},
        {"subtype", static_cast<int>(current_sub_dither_type)},
        {"name", activeTreeWidget->currentItem() ? activeTreeWidget->currentItem()->text(0) : QString()},
        {"settings", settings}});

    preset.insert("screen", QJsonObject{
        {"dpi", screenGeometry.dpi}, {"lpi", screenGeometry.lpi}, {"lpiEnabled", screenGeometry.lpiEnabled},
        {"dotMm", screenGeometry.dotMm}, {"dotEnabled", screenGeometry.dotEnabled}});

    // the Input Image Settings of the current tab
    QJsonObject adjust;
    const int values[] = {
        mono ? imageHashMono.brightness : imageHashColor.brightness, mono ? imageHashMono.contrast : imageHashColor.contrast,
        mono ? imageHashMono.gamma : imageHashColor.gamma, mono ? imageHashMono.blacks : imageHashColor.blacks,
        mono ? imageHashMono.shadows : imageHashColor.shadows, mono ? imageHashMono.midtones : imageHashColor.midtones,
        mono ? imageHashMono.highlights : imageHashColor.highlights, mono ? imageHashMono.whites : imageHashColor.whites,
        mono ? imageHashMono.blur : imageHashColor.blur, mono ? imageHashMono.denoise : imageHashColor.denoise};
    for (size_t i = 0; i < std::size(ADJUSTMENTS); i++) {
        adjust.insert(ADJUSTMENTS[i], values[i]);
    }
    if (!mono) {
        adjust.insert("saturation", imageHashColor.saturation);
    }
    preset.insert("adjust", adjust);

    // separation, with every mode's inks so switching modes after loading keeps them too
    QJsonObject inks;
    for (const auto& [mode, channels] : channelSettings) {
        QJsonArray list;
        for (const ChannelSettings& c : channels) {
            list.append(QJsonObject{{"enabled", c.enabled}, {"lpi", c.lpi}, {"angle", c.angle}});
        }
        inks.insert(QString::number(mode), list);
    }
    preset.insert("separation", QJsonObject{
        {"mode", static_cast<int>(separationMode)}, {"save", separationExportCombo->currentIndex()},
        {"psdComposite", separationPsdCompositeCheck->isChecked()},
        {"psdLayout", separationPsdLayoutCombo->currentData().toInt()}, {"inks", inks}});

    preset.insert("monoColors", QJsonObject{
        {"dark", ui->monoColorOneButton->getColor().name(QColor::HexArgb)},
        {"light", ui->monoColorTwoButton->getColor().name(QColor::HexArgb)}});

    // palette: its source and settings, plus the colours themselves in case the source is gone on load
    QJsonArray colours;
    QJsonArray locked;  // indices of the locked colours (palette editor)
    const PaletteEntries entries = currentPaletteEntries();
    for (size_t i = 0; i < entries.size(); i++) {
        colours.append(QColor::fromRgb(entries[i].colour).name(QColor::HexArgb));
        if (entries[i].locked) {
            locked.append(static_cast<int>(i));
        }
    }
    preset.insert("palette", QJsonObject{
        {"source", ui->paletteSourceCombo->currentIndex()},
        {"builtIn", ui->predefinedPaletteCombo->currentData().toString()},
        {"file", lastLoadedPalette},
        {"colors", ui->paletteColorsEdit->text().toInt()},
        {"reduction", ui->colorReductionCombo->currentIndex()},
        {"unique", ui->palGenUniqueColorsCheck->isChecked()}, {"bw", ui->palGenBWCheck->isChecked()},
        {"rgb", ui->palGenRGBCheck->isChecked()}, {"cmy", ui->palGenCMYCheck->isChecked()},
        {"comparison", ui->colorComparisonCombo->currentIndex()},
        {"entries", colours}, {"locked", locked}});
    return preset;
}

/****************
 * APPLY        *
 ****************/

void MainWindow::applyPreset(const QJsonObject& preset) {
    /* sets every control, then renders once: reDither is held while settings change one by one */
    applyingPreset = true;
    bool mono = preset.value("tab").toString() != "color";
    const QJsonObject ditherer = preset.value("ditherer").toObject();
    int subtype = ditherer.value("subtype").toInt(current_sub_dither_type);
    const QJsonObject separation = preset.value("separation").toObject();
    // separation used to live in the Mono tab: such a preset opens in the Color tab, with the colour version of
    // its algorithm when there is one (error diffusion and ordered matrices)
    if (mono && separation.value("mode").toInt(0) != static_cast<int>(SeparationMode::Composite)) {
        for (QTreeWidgetItemIterator it(ui->treeWidgetColor); *it; ++it) {
            if ((*it)->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() == subtype + COLOR_DITHER_START) {
                mono = false;
                subtype += COLOR_DITHER_START;
                break;
            }
        }
    }
    const int tab = mono ? TAB_INDEX_MONO : TAB_INDEX_COLOR;
    if (ui->tabWidget->currentIndex() != tab) {
        ui->tabWidget->setCurrentIndex(tab);
    }

    // mono ink colours
    const QJsonObject monoColors = preset.value("monoColors").toObject();
    if (monoColors.contains("dark")) monoColorOneChangedSlot(QColor(monoColors.value("dark").toString()));
    if (monoColors.contains("light")) monoColorTwoChangedSlot(QColor(monoColors.value("light").toString()));

    // screen
    const QJsonObject screen = preset.value("screen").toObject();
    whileBlocking(lpiCheck)->setChecked(screen.value("lpiEnabled").toBool(screenGeometry.lpiEnabled));
    whileBlocking(lpiSpin)->setValue(screen.value("lpi").toDouble(screenGeometry.lpi));
    whileBlocking(dotCheck)->setChecked(screen.value("dotEnabled").toBool(screenGeometry.dotEnabled));
    whileBlocking(dotSpin)->setValue(screen.value("dotMm").toDouble(screenGeometry.dotMm));
    screenSettingsChangedSlot();
    const double dpi = screen.value("dpi").toDouble(screenGeometry.dpi);
    if (dpi != screenGeometry.dpi && dpi >= SCREEN_MIN_DPI && dpi <= SCREEN_MAX_DPI) {
        whileBlocking(dpiCombo)->setCurrentText(QString::number(static_cast<int>(dpi)));
        if (firstLoad) {
            screenGeometry.dpi = dpi;
        } else if (!applyOutputSize(dpi, printWidthMm, printHeightMm)) {  // keeps the print size
            whileBlocking(dpiCombo)->setCurrentText(QString::number(static_cast<int>(screenGeometry.dpi)));
        }
    }

    // separation and inks
    const QJsonObject inks = separation.value("inks").toObject();
    for (const QString& mode : inks.keys()) {
        std::vector<ChannelSettings> channels;
        for (const QJsonValue& value : inks.value(mode).toArray()) {
            const QJsonObject c = value.toObject();
            channels.push_back({c.value("enabled").toBool(true), c.value("lpi").toDouble(0.0), c.value("angle").toDouble(0.0)});
        }
        channelSettings[mode.toInt()] = channels;
    }
    whileBlocking(separationExportCombo)->setCurrentIndex(separation.value("save").toInt(0));
    whileBlocking(separationPsdCompositeCheck)->setChecked(separation.value("psdComposite").toBool(true));
    const int layout = separationPsdLayoutCombo->findData(separation.value("psdLayout").toInt(static_cast<int>(PsdInkLayout::Layers)));
    if (layout >= 0) {
        whileBlocking(separationPsdLayoutCombo)->setCurrentIndex(layout);
    }
    const int modeIndex = separationModeCombo->findData(separation.value("mode").toInt(0));
    if (modeIndex >= 0 && modeIndex != separationModeCombo->currentIndex()) {
        separationModeCombo->setCurrentIndex(modeIndex);  // rebuilds the ink rows from channelSettings
    } else {
        rebuildChannelRows();
    }

    // palette (colour tab)
    if (!mono) {
        applyPresetPalette(preset.value("palette").toObject());
    }

    // Input Image Settings of that tab
    const QJsonObject adjust = preset.value("adjust").toObject();
    const auto get = [&adjust](const char* key) { return adjust.value(key).toInt(0); };
    if (mono) {
        imageHashMono.brightness = get("brightness"); imageHashMono.contrast = get("contrast"); imageHashMono.gamma = get("gamma");
        imageHashMono.blacks = get("blacks"); imageHashMono.shadows = get("shadows"); imageHashMono.midtones = get("midtones");
        imageHashMono.highlights = get("highlights"); imageHashMono.whites = get("whites");
        imageHashMono.blur = get("blur"); imageHashMono.denoise = get("denoise");
        whileBlocking(ui->brightnessSliderMono)->setValue(imageHashMono.brightness);
        whileBlocking(ui->brightnessEditMono)->setValue(imageHashMono.brightness / 100.0);
        whileBlocking(ui->contrastSliderMono)->setValue(imageHashMono.contrast);
        whileBlocking(ui->contrastEditMono)->setValue(imageHashMono.contrast / 100.0);
        whileBlocking(ui->gammaSliderMono)->setValue(imageHashMono.gamma);
        whileBlocking(ui->gammaEditMono)->setValue(imageHashMono.gamma / 100.0);
    } else {
        imageHashColor.brightness = get("brightness"); imageHashColor.contrast = get("contrast"); imageHashColor.gamma = get("gamma");
        imageHashColor.saturation = get("saturation");
        imageHashColor.blacks = get("blacks"); imageHashColor.shadows = get("shadows"); imageHashColor.midtones = get("midtones");
        imageHashColor.highlights = get("highlights"); imageHashColor.whites = get("whites");
        imageHashColor.blur = get("blur"); imageHashColor.denoise = get("denoise");
        whileBlocking(ui->brightnessSliderColor)->setValue(imageHashColor.brightness);
        whileBlocking(ui->brightnessEditColor)->setValue(imageHashColor.brightness / 100.0);
        whileBlocking(ui->contrastSliderColor)->setValue(imageHashColor.contrast);
        whileBlocking(ui->contrastEditColor)->setValue(imageHashColor.contrast / 100.0);
        whileBlocking(ui->gammaSliderColor)->setValue(imageHashColor.gamma);
        whileBlocking(ui->gammaEditColor)->setValue(imageHashColor.gamma / 100.0);
        whileBlocking(ui->saturationSliderColor)->setValue(imageHashColor.saturation);
        whileBlocking(ui->saturationEditColor)->setValue(imageHashColor.saturation / 100.0);
    }
    // the rows added in mainwindow_tone.cpp: 7 for the mono page, then 7 for the colour page, in this order
    const char* toneKeys[] = {"blacks", "shadows", "midtones", "highlights", "whites", "blur", "denoise"};
    const size_t offset = mono ? 0 : std::size(toneKeys);
    for (size_t i = 0; i < std::size(toneKeys) && offset + i < adjustControls.size(); i++) {
        const int value = get(toneKeys[i]);
        const double scale = std::string(toneKeys[i]) == "blur" ? 100.0 : 1.0;
        whileBlocking(adjustControls[offset + i].slider)->setValue(value);
        whileBlocking(adjustControls[offset + i].spin)->setValue(value / scale);
    }
    if (!firstLoad) {
        if (mono) adjustImageMono(); else adjustImageColor();
    }

    // ditherer last: selecting it loads its settings into the panel
    const QJsonObject settings = ditherer.value("settings").toObject();
    for (const QString& key : settings.keys()) {
        activeTreeWidget->setValue(static_cast<SubDitherType>(subtype), static_cast<SettingKey>(key.toInt()),
                                   settings.value(key).toVariant());
    }
    for (QTreeWidgetItemIterator it(activeTreeWidget); *it; ++it) {
        if ((*it)->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() == subtype) {
            activeTreeWidget->setCurrentItem(*it);
            activeTreeWidget->treeWidgetItemChangedSlot(*it, 0);  // as if clicked: loads its settings, then re-dither
            break;
        }
    }

    // one render with everything in place
    applyingPreset = false;
    imageHashMono.clearAllDitheredImages();
    imageHashColor.clearAllDitheredImages();
    ui->treeWidgetMono->clearAllDitherFlags();
    ui->treeWidgetColor->clearAllDitherFlags();
    invalidateSeparation();
    updateScreenControls();
    updateSettingsPanelHeight();
    if (!firstLoad) {
        reDither(false);
    }
}

void MainWindow::applyPresetPalette(const QJsonObject& palette) {
    /* the palette's source and settings; its saved colours stand in when the source is no longer there */
    if (palette.isEmpty()) {
        return;
    }
    whileBlocking(ui->paletteColorsEdit)->setText(QString::number(palette.value("colors").toInt(16)));
    const int reduction = palette.value("reduction").toInt(ui->colorReductionCombo->currentIndex());
    whileBlocking(ui->colorReductionCombo)->setCurrentIndex(reduction);
    colorReductionMode = static_cast<enum QuantizationMethod>(reduction + 1);  // as in colorReductionComboChangedSlot
    whileBlocking(ui->palGenUniqueColorsCheck)->setChecked(palette.value("unique").toBool());
    whileBlocking(ui->palGenBWCheck)->setChecked(palette.value("bw").toBool());
    whileBlocking(ui->palGenRGBCheck)->setChecked(palette.value("rgb").toBool());
    whileBlocking(ui->palGenCMYCheck)->setChecked(palette.value("cmy").toBool());
    const int builtIn = ui->predefinedPaletteCombo->findData(palette.value("builtIn").toString());
    if (builtIn >= 0) {
        whileBlocking(ui->predefinedPaletteCombo)->setCurrentIndex(builtIn);
        lastBuiltInPalette = QString(":/resources/palettes/%1.pal").arg(palette.value("builtIn").toString());
    }
    int source = palette.value("source").toInt(PALETTE_BUILT_IN);
    if (source == PALETTE_FROM_FILE) {
        const QString file = palette.value("file").toString();
        int errorCode = 0;
        BytePalette* loaded = QFile::exists(file) ? loadPaintNetPalette(file, &errorCode) : nullptr;
        if (loaded != nullptr) {
            BytePalette_free(loadFromFilePalette);
            loadFromFilePalette = loaded;
            lastLoadedPalette = file;
            ui->palettePathEdit->setText(file);
        } else {
            source = PALETTE_CUSTOM;  // the file moved: fall back to the colours saved with the preset
        }
    }
    if (source == PALETTE_CUSTOM) {
        const QJsonArray entries = palette.value("entries").toArray();
        if (!entries.isEmpty()) {
            BytePalette* colours = BytePalette_new(static_cast<size_t>(entries.size()));
            for (qsizetype i = 0; i < entries.size(); i++) {
                const QColor c(entries.at(i).toString());
                const ByteColor bc = {static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()),
                                      static_cast<uint8_t>(c.blue()), static_cast<uint8_t>(c.alpha())};
                BytePalette_set(colours, static_cast<size_t>(i), &bc);
            }
            BytePalette_free(customPalette);
            customPalette = colours;
            customLocks.assign(static_cast<size_t>(entries.size()), false);
            for (const QJsonValue& index : palette.value("locked").toArray()) {
                if (index.toInt(-1) >= 0 && index.toInt() < entries.size()) {
                    customLocks[static_cast<size_t>(index.toInt())] = true;
                }
            }
            if (ui->paletteSourceCombo->count() == PALETTE_CUSTOM) {  // no "custom" entry yet
                ui->paletteSourceCombo->addItem(tr("custom"));
            }
        } else {
            source = PALETTE_BUILT_IN;
        }
    }
    whileBlocking(ui->paletteSourceCombo)->setCurrentIndex(source);
    whileBlocking(ui->paletteSourceWidget)->setCurrentIndex(source);
    const int comparison = palette.value("comparison").toInt(ui->colorComparisonCombo->currentIndex());
    whileBlocking(ui->colorComparisonCombo)->setCurrentIndex(comparison);
    colorComparisonComboChangedSlot(comparison);  // sets the comparison mode and rebuilds the palette cache
}
