#include "paletteeditor.h"
#include "ui_elements/pixelbuttonglyph.h"
#include "ui_elements/pixelglyphs.h"
#include <QCoreApplication>
#include <QMouseEvent>
#include <functional>
#include <QVariantAnimation>
#include "color/colorspace.h"
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int ICON_SIZE = 14;
const char* INVALID_STYLE = "QLineEdit { border: 1px solid #ec6a5e; }";

QIcon icon(const QString& normal, const QString& disabled) {
    QIcon result;
    result.addFile(normal, QSize(), QIcon::Normal);
    result.addFile(disabled, QSize(), QIcon::Disabled);
    return result;
}

QIcon swatchIcon(const QRgb colour, const qreal ratio) {
    QPixmap pixmap(QSize(18, 18) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0x80, 0x80, 0x80), 1.0));
    painter.setBrush(QColor::fromRgb(colour));
    painter.drawRoundedRect(QRectF(0.5, 0.5, 17.0, 17.0), 3.0, 3.0);
    return QIcon(pixmap);
}

}  // namespace

/* the drag handle at the head of a palette row: a discreet pixel glyph, and the row's position for the first
 * three; pressing it starts moving the row (PaletteEditor follows the pointer) */
class PaletteDragHandle final : public QWidget {
public:
    std::function<void()> pressed;
    std::function<void(const QPoint&)> moved;
    std::function<void()> released;
    explicit PaletteDragHandle(QWidget* parent) : QWidget(parent) {
        setFixedSize(22, 22);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QCoreApplication::translate("PaletteEditor", "Drag to change the order of the colours - the order of the films and of the print passes"));
    }
    void setPosition(const int index) {
        position = index;
        update();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QColor ink(0x8a, 0x8a, 0x8a);  // discreet
        PixelGlyphs::paintHandle(&painter, QRectF(1, 6, 10, 10), ink, devicePixelRatioF());
        if (position >= 0 && position < 3) {
            QFont font = painter.font();
            font.setPointSizeF(font.pointSizeF() * 0.8);
            painter.setFont(font);
            painter.setPen(ink);
            painter.drawText(QRect(12, 0, 10, height()), Qt::AlignCenter, QString::number(position + 1));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && pressed) {
            setCursor(Qt::ClosedHandCursor);
            pressed();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if ((event->buttons() & Qt::LeftButton) && moved) {
            moved(event->globalPosition().toPoint());
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && released) {
            setCursor(Qt::OpenHandCursor);
            released();
        }
    }
private:
    int position = -1;
};

PaletteEditor::PaletteEditor(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget* list = new QWidget(scroll);
    rowsLayout = new QVBoxLayout(list);
    rowsLayout->setContentsMargins(0, 0, 0, 0);
    rowsLayout->setSpacing(2);
    rowsLayout->addStretch(1);
    scroll->setWidget(list);
    dropLine = new QWidget(list);
    dropLine->setStyleSheet("background: rgb(58, 130, 220);");
    dropLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    dropLine->hide();
    column->addWidget(scroll, 1);

    QGridLayout* buttons = new QGridLayout();
    addButton = new QPushButton(tr("+ Add Color"), this);
    addButton->setToolTip(tr("Add the colour of the picture the palette renders worst"));
    QPushButton* randomize = new QPushButton(tr("Randomize"), this);
    randomize->setToolTip(tr("A new variation of every unlocked colour (Ctrl+Z to go back)"));
    QPushButton* save = new QPushButton(tr("Save Palette"), this);
    save->setToolTip(tr("Save as a Paint.NET palette (.txt / .pal), locks included"));
    QPushButton* load = new QPushButton(tr("Load Palette"), this);
    load->setToolTip(tr("Load a Paint.NET palette (.txt / .pal) to edit it"));
    buttons->addWidget(addButton, 0, 0);
    buttons->addWidget(randomize, 0, 1);
    buttons->addWidget(save, 1, 0);
    buttons->addWidget(load, 1, 1);
    column->addLayout(buttons);
    connect(addButton, &QPushButton::clicked, this, &PaletteEditor::addRequested);
    connect(randomize, &QPushButton::clicked, this, &PaletteEditor::randomizeRequested);
    connect(save, &QPushButton::clicked, this, &PaletteEditor::saveRequested);
    connect(load, &QPushButton::clicked, this, &PaletteEditor::loadRequested);
}

PaletteEditor::Row PaletteEditor::makeRow(const int index) {
    Row row{};
    row.widget = new QWidget(rowsLayout->parentWidget());
    row.widget->setObjectName("paletteRow");
    row.widget->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* line = new QHBoxLayout(row.widget);
    line->setContentsMargins(2, 1, 2, 1);
    line->setSpacing(4);
    const auto tool = [&row](const QString& tip) {
        QToolButton* button = new QToolButton(row.widget);
        button->setAutoRaise(true);
        button->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
        button->setToolTip(tip);
        return button;
    };
    row.handle = new PaletteDragHandle(row.widget);
    row.handle->pressed = [this, index]() {
        dragFrom = dragTo = index;
    };
    row.handle->moved = [this](const QPoint& globalPos) { dragMoved(globalPos); };
    row.handle->released = [this]() { dragEnded(); };
    row.swatch = tool(tr("Edit this colour in the colour picker"));
    row.swatch->setIconSize(QSize(18, 18));
    row.hex = new QLineEdit(row.widget);
    row.hex->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    row.hex->setMaxLength(12);
    row.hex->setToolTip(tr("#RRGGBB, #RGB or #AARRGGBB - Enter to apply"));
    row.lock = tool(tr("Lock: the colour cannot be deleted or randomized"));
    row.lock->setCheckable(true);
    PixelButtonGlyph::attach(row.lock, PixelButtonGlyph::Kind::Lock, ICON_SIZE);  // pixel padlock, animated
    row.remove = tool(tr("Delete this colour"));
    row.remove->setIcon(icon(":/resources/trash.svg", ":/resources/trash_disabled.svg"));
    row.shuffle = tool(tr("Replace this colour with a random one"));
    row.shuffle->setIcon(icon(":/resources/shuffle.svg", ":/resources/shuffle_disabled.svg"));
    line->addWidget(row.handle);
    line->addWidget(row.swatch);
    line->addWidget(row.hex, 1);
    line->addWidget(row.lock);
    line->addWidget(row.remove);
    line->addWidget(row.shuffle);

    const size_t i = static_cast<size_t>(index);
    connect(row.swatch, &QToolButton::clicked, this, [this, index]() { emit pickRequested(index); });
    connect(row.hex, &QLineEdit::textEdited, this, [this, i]() { hexEdited(i); });
    connect(row.hex, &QLineEdit::editingFinished, this, [this, i]() { hexFinished(i); });
    connect(row.lock, &QToolButton::toggled, this, [this, index](const bool on) { emit lockToggled(index, on); });
    connect(row.remove, &QToolButton::clicked, this, [this, index]() { emit removeRequested(index); });
    connect(row.shuffle, &QToolButton::clicked, this, [this, index]() { emit randomizeOneRequested(index); });
    return row;
}

void PaletteEditor::setPalette(const PaletteEntries& palette) {
    entries = palette;
    while (rows.size() < entries.size()) {
        rows.push_back(makeRow(static_cast<int>(rows.size())));
        rowsLayout->insertWidget(static_cast<int>(rows.size()) - 1, rows.back().widget);  // before the stretch
    }
    while (rows.size() > entries.size()) {
        rows.back().widget->hide();
        rows.back().widget->deleteLater();  // may be the row whose delete button was just clicked
        rows.pop_back();
    }
    for (size_t i = 0; i < rows.size(); i++) {
        updateRow(i);
    }
    addButton->setEnabled(PaletteModel::canAdd(entries));
    if (editingRow >= static_cast<int>(rows.size())) {
        editingRow = -1;
    }
    if (flashRow >= 0 && flashRow < static_cast<int>(rows.size())) {
        flash(flashRow);  // the colour that was just moved, at its new place
    }
    flashRow = -1;
}

void PaletteEditor::dragMoved(const QPoint& globalPos) {
    /* the row goes where the pointer is among the other rows; a line shows the place */
    if (dragFrom < 0 || dragFrom >= static_cast<int>(rows.size())) {
        return;
    }
    QWidget* list = rowsLayout->parentWidget();
    const int y = list->mapFromGlobal(globalPos).y();
    int to = 0;
    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
        if (i != dragFrom && rows[static_cast<size_t>(i)].widget->geometry().center().y() < y) {
            to++;
        }
    }
    dragTo = to;
    // the line: above the row that will follow the moved one, or below the last
    std::vector<QWidget*> others;
    for (int i = 0; i < static_cast<int>(rows.size()); i++) {
        if (i != dragFrom) {
            others.push_back(rows[static_cast<size_t>(i)].widget);
        }
    }
    if (others.empty() || to == dragFrom) {
        dropLine->hide();
        return;
    }
    const int lineY = to < static_cast<int>(others.size()) ? others[static_cast<size_t>(to)]->geometry().top() - 2
                                                         : others.back()->geometry().bottom() + 1;
    dropLine->setGeometry(2, std::max(0, lineY), list->width() - 4, 2);
    dropLine->show();
    dropLine->raise();
}

void PaletteEditor::dragEnded() {
    dropLine->hide();
    const int from = dragFrom;
    const int to = dragTo;
    dragFrom = dragTo = -1;
    if (from >= 0 && to >= 0 && from != to && from < static_cast<int>(entries.size()) && to < static_cast<int>(entries.size())) {
        flashRow = to;
        emit moveRequested(from, to);  // MainWindow reorders the palette and calls setPalette
        flashRow = -1;
    }
}

void PaletteEditor::flash(const int index) {
    /* a short highlight fading out on the row that moved */
    QWidget* row = rows[static_cast<size_t>(index)].widget;
    QVariantAnimation* fade = new QVariantAnimation(row);
    fade->setDuration(300);
    fade->setStartValue(0.45);
    fade->setEndValue(0.0);
    connect(fade, &QVariantAnimation::valueChanged, row, [this, row, index](const QVariant& value) {
        if (index != editingRow) {
            row->setStyleSheet(QString("#paletteRow { background: rgba(58, 130, 220, %1); border-radius: 4px; }")
                                   .arg(value.toDouble(), 0, 'f', 3));
        }
    });
    connect(fade, &QVariantAnimation::finished, row, [this, index]() {
        if (index < static_cast<int>(rows.size())) {
            updateRow(static_cast<size_t>(index));
        }
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void PaletteEditor::updateRow(const size_t index) {
    const Row& row = rows[index];
    const PaletteEntry& entry = entries[index];
    row.handle->setPosition(static_cast<int>(index));
    row.swatch->setIcon(swatchIcon(entry.colour, devicePixelRatioF()));
    if (!row.hex->hasFocus() || !row.hex->isModified()) {  // never overwrite what the user is typing
        row.hex->setText(hexColour(entry.colour));
        row.hex->setStyleSheet(QString());
    }
    const QSignalBlocker blocker(row.lock);
    row.lock->setChecked(entry.locked);
    row.remove->setEnabled(PaletteModel::canRemove(entries, static_cast<int>(index)));
    row.remove->setToolTip(entry.locked ? tr("Locked: unlock the colour to delete it")
                           : static_cast<int>(entries.size()) <= PaletteModel::MIN_COLOURS
                               ? tr("A palette needs at least %1 colours").arg(PaletteModel::MIN_COLOURS)
                               : tr("Delete this colour"));
    row.shuffle->setEnabled(!entry.locked);
    row.widget->setStyleSheet(static_cast<int>(index) == editingRow
                                  ? "#paletteRow { background: rgba(58, 130, 220, 0.35); border-radius: 4px; }"
                                  : QString());
}

void PaletteEditor::setEditingRow(const int index) {
    const int previous = editingRow;
    editingRow = index;
    for (const int i : {previous, index}) {
        if (i >= 0 && i < static_cast<int>(rows.size())) {
            updateRow(static_cast<size_t>(i));
        }
    }
}

void PaletteEditor::hexEdited(const size_t index) {
    QRgb colour;
    rows[index].hex->setStyleSheet(parseHexColour(rows[index].hex->text(), &colour) ? QString() : INVALID_STYLE);
}

void PaletteEditor::hexFinished(const size_t index) {
    if (index >= rows.size() || index >= entries.size()) {
        return;
    }
    QLineEdit* hex = rows[index].hex;
    if (!hex->isModified()) {
        return;  // focus left without typing
    }
    hex->setModified(false);
    QRgb colour;
    if (!parseHexColour(hex->text(), &colour)) {
        // not a colour: back to the current one, the red frame fading out as the only notice
        hex->setText(hexColour(entries[index].colour));
        QTimer::singleShot(700, hex, [hex]() { hex->setStyleSheet(QString()); });
        return;
    }
    hex->setText(hexColour(colour));
    hex->setStyleSheet(QString());
    if (colour != entries[index].colour) {
        emit colourEdited(static_cast<int>(index), colour);
    }
}
