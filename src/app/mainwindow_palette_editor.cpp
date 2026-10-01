#include "mainwindow.h"
#include <QCheckBox>
#include "consts.h"
#include "color/colorspace.h"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QScrollArea>

/* This file contains:
 * - the palette editor of the Palette tab: editable HEX, lock, delete and randomize per colour, Add Color,
 *   Randomize, Save / Load Palette; palette changes are steps of Edit > Undo (mainwindow_history.cpp), the colour
 *   picker walks its own session with Ctrl+Z
 *
 * The palette is still the one the colour ditherers already use (cachedPalette->target_palette). Any change turns
 * it into the custom palette, as editing a colour always has, and re-dithers. Every change goes through
 * editPalette, which records the palette as it was: one history for all palette edits.
 */

void MainWindow::setupPaletteEditor() {
    QWidget* group = ui->colorListWidget->parentWidget();
    paletteEditor = new PaletteEditor(group);
    paletteEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    paletteEditor->setMinimumHeight(220);  // about six colours and the buttons
    // takes the place of the upstream colour list, which stays in the .ui but is no longer shown; its layout is
    // nested inside the group's, so look for the one that holds it
    for (QBoxLayout* box : group->findChildren<QBoxLayout*>()) {
        if (const int index = box->indexOf(ui->colorListWidget); index >= 0) {
            box->insertWidget(index, paletteEditor, 1);
            break;
        }
    }
    ui->colorListWidget->hide();
    ui->savePaletteButton->hide();  // the editor's Save Palette does the same, for any palette source

    // the Palette tab scrolls: in a short window its settings, the colours and the buttons below them stay
    // reachable instead of being cut off
    if (QGridLayout* tabLayout = qobject_cast<QGridLayout*>(ui->tabPalette->layout())) {
        QLayoutItem* content = tabLayout->itemAtPosition(0, 0);
        if (QLayout* column = content != nullptr ? content->layout() : nullptr) {
            tabLayout->removeItem(column);
            column->setParent(nullptr);
            QWidget* panel = new QWidget();
            panel->setLayout(column);  // the groups inside move to the panel
            QScrollArea* scroll = new QScrollArea(ui->tabPalette);
            scroll->setWidget(panel);
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            tabLayout->addWidget(scroll, 0, 0);
        }
    }

    connect(paletteEditor, &PaletteEditor::colourEdited, this, [this](const int index, const QRgb colour) {
        PaletteEntries palette = currentPaletteEntries();
        palette[static_cast<size_t>(index)].colour = colour;
        editPalette(palette);
    });
    connect(paletteEditor, &PaletteEditor::lockToggled, this, [this](const int index, const bool locked) {
        PaletteEntries palette = currentPaletteEntries();
        palette[static_cast<size_t>(index)].locked = locked;
        editPalette(palette);
    });
    connect(paletteEditor, &PaletteEditor::removeRequested, this, [this](const int index) {
        PaletteEntries palette = currentPaletteEntries();
        if (!PaletteModel::remove(palette, index)) {
            return;  // locked, or the last two colours: the button was disabled anyway
        }
        editPalette(palette, [this, index, size = palette.size()]() {
            // the Palette separation's inks follow their colour
            std::vector<ChannelSettings>& inks = channelSettings[static_cast<int>(SeparationMode::Palette)];
            if (inks.size() == size + 1) {
                inks.erase(inks.begin() + index);
            }
        });
    });
    connect(paletteEditor, &PaletteEditor::randomizeOneRequested, this, [this](const int index) {
        PaletteEntries palette = currentPaletteEntries();
        if (PaletteModel::randomizeOne(palette, index, QRandomGenerator::global()->generate())) {
            editPalette(palette);
        }
    });
    connect(paletteEditor, &PaletteEditor::addRequested, this, [this]() {
        PaletteEntries palette = currentPaletteEntries();
        // the colour of the (adjusted) picture that the palette renders worst: the ink that is missing
        const QRgb colour = PaletteModel::leastRepresented(firstLoad ? QImage() : *imageHashColor.getSourceQImage(), palette);
        if (!PaletteModel::add(palette, colour)) {
            return;
        }
        editPalette(palette, [this, size = palette.size()]() {
            std::vector<ChannelSettings>& inks = channelSettings[static_cast<int>(SeparationMode::Palette)];
            if (inks.size() == size - 1) {
                inks.push_back(ChannelSettings());
            }
        });
        notification->showText(tr("colour added: %1").arg(hexColour(colour)), 2000);
    });
    connect(paletteEditor, &PaletteEditor::randomizeRequested, this, [this]() {
        PaletteEntries palette = currentPaletteEntries();
        const std::uint32_t seed = QRandomGenerator::global()->generate();
        PaletteModel::randomize(palette, seed);
        editPalette(palette);
        notification->showText(tr("palette randomized (seed %1)\nCtrl+Z to go back").arg(seed), 2000);
    });
    connect(paletteEditor, &PaletteEditor::pickRequested, this, &MainWindow::pickPaletteColour);
    connect(paletteEditor, &PaletteEditor::moveRequested, this, &MainWindow::movePaletteColour);
    connect(paletteEditor, &PaletteEditor::saveRequested, this, [this]() { savePaintNetPalette(currentPaletteEntries()); });
    connect(paletteEditor, &PaletteEditor::loadRequested, this, [this]() {
        const QString filter = tr("Palettes") + " (" + PALETTE_FILTERS.join(" ") + ")";
        const QString fileName = QFileDialog::getOpenFileName(this, tr("Load Palette"), lastSavedPalette, filter);
        if (fileName.isEmpty()) {
            return;
        }
        QFile file(fileName);
        PaletteEntries palette;
        QString error;
        bool truncated = false;
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            error = file.errorString();
        } else if (PaletteModel::fromPaintNet(QString::fromUtf8(file.readAll()), &palette, &error, &truncated)) {
            editPalette(palette);
            notification->showText(tr("palette loaded: %1 colours%2").arg(palette.size())
                                       .arg(truncated ? tr("\n(more than 256 in the file: the rest was left out)") : QString()), 3000);
            lastSavedPalette = fileName;
            return;
        }
        notification->showText("<font color=#ec6a5e>" + tr("ERROR") + "</font>\n" + tr("palette not loaded") + "\n" + error, 3000);
    });

    // palette edits are steps of Edit > Undo, the history of every setting (mainwindow_history.cpp); paletteHistory
    // still serves the colour picker's own Ctrl+Z within a session
}

void MainWindow::movePaletteColour(const int from, const int to) {
    /* a colour dragged to another place: the order of the palette is the order of the films (names, PSD, files)
     * and of the print passes in the superposed print. The colours do not change, and since the ditherers look
     * for the nearest colour among all of them, neither does the dithered picture: no new render, only the films
     * follow. Each ink's settings (on/off, overprint, LPI, angle) move with their colour. */
    PaletteEntries palette = currentPaletteEntries();
    const int size = static_cast<int>(palette.size());
    if (isDithering || pickerIndex >= 0 || from < 0 || to < 0 || from >= size || to >= size || from == to) {
        return;
    }
    const PaletteEntry moved = palette[static_cast<size_t>(from)];
    palette.erase(palette.begin() + from);
    palette.insert(palette.begin() + to, moved);
    const auto carryInks = [this, from, to, size]() {
        std::vector<ChannelSettings>& inks = channelSettings[static_cast<int>(SeparationMode::Palette)];
        if (static_cast<int>(inks.size()) == size) {
            const ChannelSettings ink = inks[static_cast<size_t>(from)];
            inks.erase(inks.begin() + from);
            inks.insert(inks.begin() + to, ink);
        }
    };
    if (ui->paletteSourceWidget->currentIndex() != PALETTE_CUSTOM) {
        editPalette(palette, carryInks);  // it becomes the custom palette, the usual way
        return;
    }
    paletteHistory.record(currentPaletteEntries());
    carryInks();
    BytePalette* colours = BytePalette_new(palette.size());
    customLocks.clear();
    for (size_t i = 0; i < palette.size(); i++) {
        const ByteColor c = {static_cast<uint8_t>(qRed(palette[i].colour)), static_cast<uint8_t>(qGreen(palette[i].colour)),
                             static_cast<uint8_t>(qBlue(palette[i].colour)), 255};
        BytePalette_set(colours, i, &c);
        customLocks.push_back(palette[i].locked);
    }
    BytePalette_free(customPalette);
    customPalette = colours;
    const FloatColor weights = cachedPalette->lab_weights;
    updateCachedPalette(customPalette);  // the lookup cache follows the new indices; the dithered pictures stay
    cachedPalette->lab_weights = weights;
    updatePaletteColorSwatches(cachedPalette->target_palette);
    if (separationMode == SeparationMode::Palette) {
        refreshSeparationInks();  // films renamed and reordered
    } else {
        invalidateSeparation();
    }
    if (!firstLoad) {
        reDither(false);
    }
}

PaletteEntries MainWindow::currentPaletteEntries() const {
    PaletteEntries palette;
    if (cachedPalette == nullptr || cachedPalette->target_palette == nullptr) {
        return palette;
    }
    const size_t size = cachedPalette->target_palette->size;
    const bool locks = ui->paletteSourceWidget->currentIndex() == PALETTE_CUSTOM && customLocks.size() == size;
    for (size_t i = 0; i < size; i++) {
        const ByteColor* c = BytePalette_get(cachedPalette->target_palette, i);
        palette.push_back({qRgb(c->r, c->g, c->b), locks && customLocks[i]});
    }
    return palette;
}

void MainWindow::editPalette(const PaletteEntries& edited, const std::function<void()>& beforeApply) {
    if (isDithering) {
        return;  // a change while the image is being dithered would pull the palette from under the ditherer
    }
    const PaletteEntries before = currentPaletteEntries();
    if (edited == before) {
        return;
    }
    if (ui->paletteSourceWidget->currentIndex() != PALETTE_CUSTOM && customPalette != nullptr) {
        // the edit replaces the custom palette made earlier: offer to keep it, as Ditherista always has, unless
        // "Don't ask again" (Preferences > Color Management) settled the answer
        QMessageBox::StandardButton reply = QMessageBox::No;
        if (preferences.customPaletteReplace == Preferences::CustomPaletteReplace::Save) {
            reply = QMessageBox::Yes;
        } else if (preferences.customPaletteReplace == Preferences::CustomPaletteReplace::Ask) {
            QMessageBox box(QMessageBox::Question, tr("Custom Palette Exists"),
                            tr("Changing the current palette will create a new custom palette.\n"
                               "Do you want to save the existing palette?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this);
            QCheckBox* dontAsk = new QCheckBox(tr("Don't ask again (Preferences > Color Management)"), &box);
            box.setCheckBox(dontAsk);
            reply = static_cast<QMessageBox::StandardButton>(box.exec());
            if (dontAsk->isChecked() && reply != QMessageBox::Cancel) {
                preferences.customPaletteReplace = reply == QMessageBox::Yes ? Preferences::CustomPaletteReplace::Save
                                                                             : Preferences::CustomPaletteReplace::Replace;
                savePreferences();
            }
        }
        if (reply == QMessageBox::Cancel) {
            paletteEditor->setPalette(before);  // puts back what the list shows, e.g. a lock just clicked
            return;
        }
        if (reply == QMessageBox::Yes) {
            PaletteEntries custom;
            for (size_t i = 0; i < customPalette->size; i++) {
                const ByteColor* c = BytePalette_get(customPalette, i);
                custom.push_back({qRgb(c->r, c->g, c->b), i < customLocks.size() && customLocks[i]});
            }
            savePaintNetPalette(custom);
        }
    }
    paletteHistory.record(before);
    if (beforeApply) {
        beforeApply();
    }
    applyPaletteEntries(edited);
}

void MainWindow::applyPaletteEntries(const PaletteEntries& entries) {
    /* `entries` become the custom palette and the colour ditherers use it; the list shows it again through
     * generateCachedPalette -> updatePaletteColorSwatches */
    const bool wasCustom = ui->paletteSourceWidget->currentIndex() == PALETTE_CUSTOM;
    BytePalette* colours = BytePalette_new(entries.size());
    customLocks.clear();
    for (size_t i = 0; i < entries.size(); i++) {
        const ByteColor c = {static_cast<uint8_t>(qRed(entries[i].colour)), static_cast<uint8_t>(qGreen(entries[i].colour)),
                             static_cast<uint8_t>(qBlue(entries[i].colour)), 255};
        BytePalette_set(colours, i, &c);
        customLocks.push_back(entries[i].locked);
    }
    BytePalette_free(customPalette);
    customPalette = colours;
    if (ui->paletteSourceCombo->count() == PALETTE_CUSTOM) {  // no "custom" entry yet
        ui->paletteSourceCombo->addItem(tr("custom"));
    }
    if (wasCustom) {
        generateCachedPalette(true, false, true);
    } else {
        ui->paletteSourceCombo->setCurrentIndex(PALETTE_CUSTOM);  // shows the custom page and re-dithers
    }
}

void MainWindow::undoPalette(const bool redo) {
    if (settleTimer != nullptr && settleTimer->isActive()) {  // a picked colour not yet recorded: record it first
        settleTimer->stop();
        settlePickerColour();
    }
    PaletteEntries palette = currentPaletteEntries();
    if (isDithering || !(redo ? paletteHistory.redo(palette) : paletteHistory.undo(palette))) {
        return;
    }
    applyPaletteEntries(palette);
    if (pickerIndex >= 0 && pickerIndex < static_cast<int>(palette.size())) {
        pickerPending = palette[static_cast<size_t>(pickerIndex)].colour;
        colourPicker->setColour(pickerPending);  // the picker shows the colour it went back to
    }
}

/*************************
 * COLOUR PICKER SESSION *
 *************************/

namespace {
constexpr double LIVE_PREVIEW_PIXELS = 300000.0;  // live preview resolution while a colour moves (~0.1 s a frame)
constexpr int LIVE_DELAY_MS = 30;                  // coalesces picker events
constexpr int SETTLE_DELAY_MS = 350;               // the colour has rested: full preview and one undo step
constexpr int SETTLE_DELAY_LARGE_MS = 900;         // same, for a preview that takes seconds to render in full
}

void MainWindow::pickPaletteColour(const int index) {
    const PaletteEntries palette = currentPaletteEntries();
    if (index < 0 || index >= static_cast<int>(palette.size())) {
        return;
    }
    if (colourPicker == nullptr) {
        colourPicker = new ColourPickerDialog(this);
        liveTimer = new QTimer(this);
        liveTimer->setSingleShot(true);
        liveTimer->setInterval(LIVE_DELAY_MS);
        settleTimer = new QTimer(this);
        settleTimer->setSingleShot(true);
        settleTimer->setInterval(SETTLE_DELAY_MS);
        connect(liveTimer, &QTimer::timeout, this, &MainWindow::renderLivePalettePreview);
        connect(settleTimer, &QTimer::timeout, this, &MainWindow::settlePickerColour);
        connect(colourPicker, &ColourPickerDialog::colourChanged, this, [this](const QRgb colour) {
            pickerPending = colour;
            liveTimer->start();
            settleTimer->start();  // restarted by every change: fires once the colour rests
        });
        connect(colourPicker, &ColourPickerDialog::undoRequested, this, [this]() { undoPalette(false); });
        connect(colourPicker, &ColourPickerDialog::redoRequested, this, [this]() { undoPalette(true); });
        connect(colourPicker, &QDialog::finished, this, [this](const int result) {
            endPickerSession(result == QDialog::Accepted);
        });
    }
    if (pickerIndex >= 0) {
        endPickerSession(true);  // another colour clicked while picking: keep the first one's
    }
    // palette changes only show on the colour dither: make it the one on screen
    if (!firstLoad && lastTabIndex != TAB_INDEX_COLOR) {
        const int tab = ui->tabWidget->currentIndex();
        ui->tabWidget->setCurrentIndex(TAB_INDEX_COLOR);
        ui->tabWidget->setCurrentIndex(tab);
    }
    pickerIndex = index;
    pickerPending = palette[static_cast<size_t>(index)].colour;
    // a large preview takes seconds to render in full: wait longer before it, so a pause in the search does not
    // block the live preview behind a full render
    const double pixels = static_cast<double>(previewImage.width()) * previewImage.height();
    settleTimer->setInterval(pixels > 4.0 * LIVE_PREVIEW_PIXELS ? SETTLE_DELAY_LARGE_MS : SETTLE_DELAY_MS);
    paletteHistory.beginSession();
    paletteEditor->setEditingRow(index);
    colourPicker->setColour(pickerPending);
    colourPicker->setTitle(tr("Colour %1 of %2").arg(index + 1).arg(palette.size()));
    colourPicker->show();
    colourPicker->raise();
    colourPicker->activateWindow();
}

void MainWindow::renderLivePalettePreview() {
    /* the colour dither with the colour being picked, on a reduced copy of the preview: fast enough to follow the
     * pointer. The full preview comes once the colour rests (settlePickerColour). */
    if (pickerIndex < 0 || firstLoad || lastTabIndex != TAB_INDEX_COLOR || current_dither_number < COLOR_DITHER_START ||
        renderPaused) {  // render control paused: the last result stays on screen
        return;
    }
    if (isDithering) {
        liveTimer->start();  // try again when the running dither is done
        return;
    }
    PaletteEntries palette = currentPaletteEntries();
    if (pickerIndex >= static_cast<int>(palette.size())) {
        return;
    }
    palette[static_cast<size_t>(pickerIndex)].colour = pickerPending;
    // the palette as the ditherers want it, set up like generateCachedPalette does
    BytePalette* colours = BytePalette_new(palette.size());
    for (size_t i = 0; i < palette.size(); i++) {
        const ByteColor c = {static_cast<uint8_t>(qRed(palette[i].colour)), static_cast<uint8_t>(qGreen(palette[i].colour)),
                             static_cast<uint8_t>(qBlue(palette[i].colour)), 255};
        BytePalette_set(colours, i, &c);
    }
    CachedPalette* live = CachedPalette_new();
    CachedPalette_from_BytePalette(live, colours);
    BytePalette_free(colours);
    CachedPalette_update_cache(live, colorComparisonMode, &srcIlluminant);
    CachedPalette_set_shift(live, DEFAULT_BIT_SHIFT.r, DEFAULT_BIT_SHIFT.g, DEFAULT_BIT_SHIFT.b);
    live->lab_weights = cachedPalette->lab_weights;

    // the reduced, adjusted picture: made once per session, as long as the picture and its adjustments stay
    const qint64 key = imageHashColor.getSourceQImage()->cacheKey();
    const double scale = std::min(1.0, std::sqrt(LIVE_PREVIEW_PIXELS / (static_cast<double>(previewImage.width()) * previewImage.height())));
    if (liveSource == nullptr || liveSourceKey != key) {
        const QSize size(std::max(1, static_cast<int>(previewImage.width() * scale)), std::max(1, static_cast<int>(previewImage.height() * scale)));
        const QImage small = scale < 1.0 ? previewImage.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : previewImage;
        liveSource = std::make_unique<ImageHashColor>();
        liveSource->copyAdjustmentsFrom(imageHashColor);
        liveSource->pixelsPerMm = renderDpi * small.width() / previewImage.width() / MM_PER_INCH;
        liveSource->denoiseScale = static_cast<double>(small.width()) / nativeImage.width();
        liveSource->setSourceImage(&small, true);
        liveSourceKey = key;
    }
    const QImage* small = liveSource->getSourceQImage();
    const double previewDpi = renderDpi;
    renderDpi = previewDpi * small->width() / previewImage.width();  // LPI cell and dot size keep their size on paper
    CachedPalette* kept = cachedPalette;
    cachedPalette = live;
    ditherColorInto(*liveSource);
    cachedPalette = kept;
    renderDpi = previewDpi;
    CachedPalette_free(live);
    if (pickerIndex < 0 || liveSource == nullptr || !liveSource->hasDitheredImage(current_dither_number)) {
        return;  // the session ended while this rendered
    }
    // shown stretched over the scene by the view: no full-size copy to make
    ui->graphicsView->setDitherImageColor(liveSource->getDitheredImage(current_dither_number),
                                          ui->treeWidgetColor->getCurrentDitherFileName(), previewImage.size());
    ui->graphicsView->showSourceColor(false);
}

void MainWindow::settlePickerColour() {
    /* the picked colour has rested: it goes into the palette - one undo step - and the preview is rendered in full */
    if (pickerIndex < 0) {
        return;
    }
    if (isDithering) {
        settleTimer->start();
        return;
    }
    PaletteEntries palette = currentPaletteEntries();
    if (pickerIndex >= static_cast<int>(palette.size())) {
        colourPicker->reject();  // the colour is gone (e.g. undo of the Add that made it)
        return;
    }
    if (palette[static_cast<size_t>(pickerIndex)].colour == pickerPending) {
        if (!firstLoad) reDither(false);  // back where it was: replace the live preview by the full one
        return;
    }
    palette[static_cast<size_t>(pickerIndex)].colour = pickerPending;
    editPalette(palette);
}

void MainWindow::endPickerSession(const bool keep) {
    if (pickerIndex < 0) {
        return;
    }
    if (isDithering) {
        // closed while a preview renders (with a stand-in palette): finish once it is done
        QTimer::singleShot(50, this, [this, keep]() { endPickerSession(keep); });
        return;
    }
    liveTimer->stop();
    if (keep) {
        if (settleTimer->isActive()) {
            settleTimer->stop();
            settlePickerColour();
        }
        paletteHistory.commitSession();  // the colours tried become one step
    } else {
        settleTimer->stop();
        PaletteEntries palette = currentPaletteEntries();
        if (paletteHistory.cancelSession(palette)) {
            applyPaletteEntries(palette);  // the palette the picker opened on
        } else if (!firstLoad) {
            reDither(false);  // a live preview may still be on screen
        }
    }
    pickerIndex = -1;
    liveSource.reset();  // the reduced picture is only kept while picking
    paletteEditor->setEditingRow(-1);
    scheduleHistoryCapture();  // the session is one step of Edit > Undo
}
