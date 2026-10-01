#include "mainwindow.h"
#include "treewidget.h"
#include "ui_elements/signalblocker.h"
#include <QEvent>
#include <QToolButton>

/* Undo / redo of every setting: Edit > Undo (Ctrl+Z), Redo (Ctrl+Y, Ctrl+Shift+Z), without a limit.
 *
 * A state of the session (captureSession) holds what presets hold - output DPI, LPI, dot size, separation, mono
 * colours, palette - plus the print size and its padlock, both tabs' ditherer and Input Image Settings, every
 * ditherer's own settings and the current tab. A state is recorded shortly after each change (one slider release,
 * one validated value, one clicked ditherer...), when it differs from the last one: renders and user input
 * schedule it. Undo restores the previous state, applying only what differs: going back to a ditherer just
 * selects it again, and shows its cached result if it has one.
 * Palette edits are steps of it too; the colour picker keeps its own Ctrl+Z inside its window, and its session
 * becomes one step when it closes. One history per picture: opening a picture starts a new one. */

namespace {
constexpr int CAPTURE_DELAY_MS = 120;  // a burst of changes (a preset, a palette edit) is one step

/* user input anywhere in the application: a change may follow, a state is recorded if one did */
class HistoryTrigger final : public QObject {
public:
    HistoryTrigger(QObject* parent, std::function<void()> trigger) : QObject(parent), trigger(std::move(trigger)) {}
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonRelease || event->type() == QEvent::KeyRelease) {
            trigger();
        }
        return QObject::eventFilter(watched, event);
    }
private:
    std::function<void()> trigger;
};

int currentDitherer(const TreeWidget* tree) {
    const QTreeWidgetItem* item = tree->currentItem();
    return item != nullptr ? item->data(ITEM_DATA_DSUBTYPE, Qt::UserRole).toInt() : -1;
}
}  // namespace

void MainWindow::setupHistory() {
    captureTimer.setSingleShot(true);
    captureTimer.setInterval(CAPTURE_DELAY_MS);
    connect(&captureTimer, &QTimer::timeout, this, [this]() {
        if (firstLoad || restoringHistory || pickerIndex >= 0) {
            return;  // the colour picker records one step when it closes (endPickerSession)
        }
        if (isDithering || applyingPreset) {
            captureTimer.start();  // after the change is complete
            return;
        }
        if (history.record(captureSession())) {
            updateHistoryActions();
        }
    });
    qApp->installEventFilter(new HistoryTrigger(this, [this]() { scheduleHistoryCapture(); }));

    // first in the Edit menu; a text field being edited keeps Ctrl+Z for itself
    QAction* first = ui->menuEdit->actions().isEmpty() ? nullptr : ui->menuEdit->actions().first();
    undoAction = new QAction(tr("Undo"), ui->menuEdit);
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction = new QAction(tr("Redo"), ui->menuEdit);
    redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    ui->menuEdit->insertAction(first, undoAction);
    ui->menuEdit->insertAction(first, redoAction);
    if (first != nullptr) {
        ui->menuEdit->insertSeparator(first);
    }
    connect(undoAction, &QAction::triggered, this, [this]() { undoSession(false); });
    connect(redoAction, &QAction::triggered, this, [this]() { undoSession(true); });
    updateHistoryActions();
}

void MainWindow::scheduleHistoryCapture() {
    if (!firstLoad && !restoringHistory) {
        captureTimer.start();
    }
}

void MainWindow::resetHistory() {
    /* a new picture: its own history, starting from what is set now */
    captureTimer.stop();
    history.reset(captureSession());
    updateHistoryActions();
}

void MainWindow::updateHistoryActions() {
    if (undoAction != nullptr) {
        undoAction->setEnabled(!firstLoad && history.canUndo());
        redoAction->setEnabled(!firstLoad && history.canRedo());
    }
}

QJsonObject MainWindow::captureSession() const {
    QJsonObject common = capturePreset();  // screen, separation, mono colours, palette
    common.remove("tab");                  // these three are kept below for both tabs
    common.remove("ditherer");
    common.remove("adjust");
    return {
        {"common", common},
        {"tab", lastTabIndex},
        {"monoDitherer", currentDitherer(ui->treeWidgetMono)},
        {"colorDitherer", currentDitherer(ui->treeWidgetColor)},
        {"monoSettings", ui->treeWidgetMono->settingsJson()},
        {"colorSettings", ui->treeWidgetColor->settingsJson()},
        {"monoAdjust", captureAdjustments(true)},
        {"colorAdjust", captureAdjustments(false)},
        {"print", QJsonObject{{"width", printWidthMm}, {"height", printHeightMm}, {"locked", aspectLockButton->isChecked()}}},
    };
}

void MainWindow::undoSession(const bool redo) {
    if (firstLoad || isDithering) {
        return;
    }
    if (captureTimer.isActive()) {  // a change not recorded yet becomes a step first
        captureTimer.stop();
        if (pickerIndex < 0 && !applyingPreset && history.record(captureSession())) {
            updateHistoryActions();
        }
    }
    if (redo ? !history.canRedo() : !history.canUndo()) {
        return;
    }
    restoreSession(redo ? history.redo() : history.undo());
}

void MainWindow::selectDitherer(TreeWidget* tree, const int id, const bool load) {
    /* the ditherer `id` at its own place in `tree`, as if clicked when `load` (its settings shown, rendered) */
    QTreeWidgetItem* item = tree->originalItem(id);
    if (item == nullptr) {
        return;
    }
    {
        const QSignalBlocker blocker(tree);
        tree->setCurrentItem(item);
        item->setSelected(true);
    }
    tree->changeDitherer(item);
    if (load) {
        treeWidgetItemChangedSlot(item);
    }
    tree->viewport()->update();
}

void MainWindow::restoreSession(const QJsonObject& target) {
    /* applies `target`, changing only what differs from now; one render at the end (held while paused) */
    if (target.isEmpty() || firstLoad || isDithering) {
        return;
    }
    const QJsonObject now = captureSession();
    if (now == target) {
        return;
    }
    restoringHistory = true;
    const auto differs = [&](const char* key) { return now.value(key) != target.value(key); };
    const int tab = target.value("tab").toInt(lastTabIndex);
    const bool targetMono = tab == TAB_INDEX_MONO;
    TreeWidget* activeTree = targetMono ? ui->treeWidgetMono : ui->treeWidgetColor;
    TreeWidget* otherTree = targetMono ? ui->treeWidgetColor : ui->treeWidgetMono;
    const int activeId = target.value(targetMono ? "monoDitherer" : "colorDitherer").toInt(-1);
    const int otherId = target.value(targetMono ? "colorDitherer" : "monoDitherer").toInt(-1);

    // every ditherer's own settings; the cached results of those that change are stale
    for (const bool mono : {true, false}) {
        const char* key = mono ? "monoSettings" : "colorSettings";
        if (!differs(key)) {
            continue;
        }
        const QJsonObject before = now.value(key).toObject();
        const QJsonObject after = target.value(key).toObject();
        TreeWidget* tree = mono ? ui->treeWidgetMono : ui->treeWidgetColor;
        tree->setSettingsJson(after);
        QStringList subtypes = before.keys() + after.keys();
        subtypes.removeDuplicates();
        for (const QString& subtype : subtypes) {
            if (before.value(subtype) != after.value(subtype)) {
                if (mono) {
                    imageHashMono.clearDitheredImage(subtype.toInt());
                } else {
                    imageHashColor.clearDitheredImage(subtype.toInt());
                    invalidateSeparation();
                }
                tree->setDitherFlag(subtype.toInt(), false);
            }
        }
    }

    applyingPreset = true;  // every re-dither below is held: one render at the end
    if (otherId >= 0 && currentDitherer(otherTree) != otherId) {
        selectDitherer(otherTree, otherId, false);
    }
    const bool onlySelection = !differs("common") && !differs("monoAdjust") && !differs("colorAdjust") && !differs("print");
    if (onlySelection) {
        // another ditherer, another tab, or its own settings: the other results stay cached
        if (tab != lastTabIndex) {
            ui->tabWidget->setCurrentIndex(tab);
        }
        selectDitherer(activeTree, activeId, true);  // shows its settings
        applyingPreset = false;
        reDither(false);
    } else {
        if (differs("print")) {
            const QJsonObject print = target.value("print").toObject();
            whileBlocking(aspectLockButton)->setChecked(print.value("locked").toBool(true));
            const double width = print.value("width").toDouble(printWidthMm);
            const double height = print.value("height").toDouble(printHeightMm);
            if (requestOutputSize(screenGeometry.dpi, width, height)) {
                whileBlocking(printWidthSpin)->setValue(width);
                whileBlocking(printHeightSpin)->setValue(height);
            }
        }
        const char* otherAdjust = targetMono ? "colorAdjust" : "monoAdjust";
        if (differs(otherAdjust)) {
            applyAdjustments(target.value(otherAdjust).toObject(), !targetMono);
        }
        const QJsonObject common = target.value("common").toObject();
        if (targetMono && now.value("common").toObject().value("palette") != common.value("palette")) {
            applyPresetPalette(common.value("palette").toObject());  // applyPreset sets it in the Color tab only
        }
        // the rest as a preset of the target tab: it sets everything, then renders once
        QJsonObject preset = common;
        preset.insert("tab", targetMono ? "mono" : "color");
        preset.insert("ditherer", QJsonObject{
            {"subtype", activeId},
            {"settings", target.value(targetMono ? "monoSettings" : "colorSettings").toObject()
                             .value(QString::number(activeId)).toObject()}});
        preset.insert("adjust", target.value(targetMono ? "monoAdjust" : "colorAdjust").toObject());
        applyPreset(preset);  // ends the hold
    }
    applyingPreset = false;
    restoringHistory = false;
    captureTimer.stop();
    history.replaceCurrent(captureSession());  // as the application settled it
    updateHistoryActions();
}
