#include "mainwindow.h"
#include "consts.h"
#include "color/colorspace.h"
#include <QColorDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QRandomGenerator>

/* This file contains:
 * - the palette editor of the Palette tab: editable HEX, lock, delete and randomize per colour, Add Color,
 *   Randomize, Save / Load Palette, and undo / redo of palette changes (Edit menu, Ctrl+Z / Ctrl+Y)
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

    // undo / redo in the Edit menu; a text field being edited keeps Ctrl+Z for itself
    ui->menuEdit->addSeparator();
    undoPaletteAction = ui->menuEdit->addAction(tr("Undo Palette Change"));
    undoPaletteAction->setShortcut(QKeySequence::Undo);
    redoPaletteAction = ui->menuEdit->addAction(tr("Redo Palette Change"));
    redoPaletteAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    connect(undoPaletteAction, &QAction::triggered, this, [this]() {
        PaletteEntries palette = currentPaletteEntries();
        if (!isDithering && paletteHistory.undo(palette)) {
            applyPaletteEntries(palette);
        }
    });
    connect(redoPaletteAction, &QAction::triggered, this, [this]() {
        PaletteEntries palette = currentPaletteEntries();
        if (!isDithering && paletteHistory.redo(palette)) {
            applyPaletteEntries(palette);
        }
    });
    updatePaletteHistoryActions();
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
        // the edit replaces the custom palette made earlier: offer to keep it, as Ditherista always has
        const QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("Custom Palette Exists"),
            tr("Changing the current palette will create a new custom palette.\n"
               "Do you want to save the existing palette?"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
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
    updatePaletteHistoryActions();
}

void MainWindow::updatePaletteHistoryActions() {
    if (undoPaletteAction != nullptr) {
        undoPaletteAction->setEnabled(paletteHistory.canUndo());
        redoPaletteAction->setEnabled(paletteHistory.canRedo());
    }
}

void MainWindow::pickPaletteColour(const int index) {
    const PaletteEntries palette = currentPaletteEntries();
    if (index < 0 || index >= static_cast<int>(palette.size())) {
        return;
    }
    paletteEditor->setEditingRow(index);
    const QColor result = QColorDialog::getColor(QColor::fromRgb(palette[static_cast<size_t>(index)].colour), this);
    paletteEditor->setEditingRow(-1);
    if (result.isValid()) {
        PaletteEntries edited = currentPaletteEntries();
        edited[static_cast<size_t>(index)].colour = result.rgb();
        editPalette(edited);
    }
}
