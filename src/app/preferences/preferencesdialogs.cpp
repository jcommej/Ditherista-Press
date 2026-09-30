#include "preferencesdialogs.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// the look of the mock-ups: dark cards, bold titles over a rule, grey hints
const char* DIALOG_STYLE = R"(
QDialog { background: #141518; }
QLabel#sectionTitle { color: #e6e8ec; font-size: 15px; font-weight: 600; }
QFrame#sectionRule { background: #2a2d34; max-height: 1px; min-height: 1px; border: none; }
QLabel#fieldLabel { color: #c9ccd2; font-weight: 600; }
QLabel#hint { color: #6f757f; }
QLineEdit { background: #111316; color: #e4e6eb; border: 1px solid #2a2e37; border-radius: 6px; padding: 7px 9px; }
QLineEdit:focus { border-color: #555c69; }
QFrame#previewBox { background: #0f1114; border: 1px solid #2a2e37; border-radius: 6px; }
QLabel#previewChip { background: #1c2530; color: #9fc3e6; border-radius: 4px; padding: 3px 8px; }
QPushButton#browse { background: #1f242c; color: #9fc3e6; border: 1px solid #2a2e37; border-radius: 6px;
                     padding: 7px 12px; font-weight: 600; }
QPushButton#browse:hover { background: #262c36; }
)";

QFont monospace() {
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

}  // namespace

/*********************
 * TOGGLE SWITCH     *
 *********************/

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF track = QRectF(rect()).adjusted(1, 3, -1, -3);
    painter.setPen(QPen(QColor(0x3a, 0x3f, 0x48), 1.0));
    painter.setBrush(isChecked() ? QColor(0x3b, 0x4a, 0x5e) : QColor(0x22, 0x25, 0x2b));
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const double knob = track.height() - 6.0;
    const double x = isChecked() ? track.right() - 3.0 - knob : track.left() + 3.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(isChecked() ? QColor(0x9f, 0xc3, 0xe6) : QColor(0x6f, 0x75, 0x7f));
    painter.drawEllipse(QRectF(x, track.top() + 3.0, knob, knob));
}

/*****************************************
 * FILENAME SETTINGS AND DEFAULT FOLDERS *
 *****************************************/

QWidget* FilesDialog::sectionHeader(const QString& icon, const QString& title) {
    QWidget* header = new QWidget(this);
    QVBoxLayout* column = new QVBoxLayout(header);
    column->setContentsMargins(0, 8, 0, 4);
    QHBoxLayout* line = new QHBoxLayout();
    QLabel* picture = new QLabel(header);
    picture->setPixmap(QIcon(icon).pixmap(16, 16));
    QLabel* text = new QLabel(title, header);
    text->setObjectName("sectionTitle");
    line->addWidget(picture);
    line->addWidget(text, 1);
    QFrame* rule = new QFrame(header);
    rule->setObjectName("sectionRule");
    column->addLayout(line);
    column->addWidget(rule);
    return header;
}

QLabel* FilesDialog::hint(const QString& text) {
    QLabel* label = new QLabel(text, this);
    label->setObjectName("hint");
    label->setWordWrap(true);
    return label;
}

QWidget* FilesDialog::folderRow(QLineEdit* field, const QString& title) {
    QWidget* row = new QWidget(this);
    QHBoxLayout* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    field->setFont(monospace());
    field->setPlaceholderText(tr("System default (leave empty for OS default)"));
    field->setClearButtonEnabled(true);
    QPushButton* browse = new QPushButton(QIcon(":/resources/folder.svg"), tr("Browse"), row);
    browse->setObjectName("browse");
    line->addWidget(field, 1);
    line->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, field, title]() {
        const QString folder = QFileDialog::getExistingDirectory(this, title, field->text());
        if (!folder.isEmpty()) {
            field->setText(QDir::toNativeSeparators(folder));
            emit field->editingFinished();
        }
    });
    return row;
}

FilesDialog::FilesDialog(Preferences* preferences, const FileNameFields& example, QWidget* parent)
    : QDialog(parent), preferences(preferences), example(example) {
    setWindowTitle(tr("Files and Folders"));
    setStyleSheet(DIALOG_STYLE);
    setMinimumWidth(620);
    QVBoxLayout* column = new QVBoxLayout(this);
    column->setContentsMargins(18, 12, 18, 16);
    column->setSpacing(6);
    const auto label = [this](const QString& text) {
        QLabel* l = new QLabel(text, this);
        l->setObjectName("fieldLabel");
        return l;
    };

    // Filename Settings
    column->addWidget(sectionHeader(":/resources/file.svg", tr("Filename Settings")));
    QHBoxLayout* suffixLine = new QHBoxLayout();
    autoSuffix = new ToggleSwitch(this);
    autoSuffix->setChecked(preferences->autoSuffix);
    suffixLine->addWidget(label(tr("Auto-Add Suffix")), 1);
    suffixLine->addWidget(autoSuffix);
    column->addLayout(suffixLine);
    column->addWidget(label(tr("Filename Suffix")));
    suffix = new QLineEdit(preferences->suffix, this);
    column->addWidget(suffix);
    column->addWidget(hint(tr("Text appended to the file name (e.g. \"image_errordiff_floyd-steinberg.png\"). "
                              "It may use {dither} and {dpi}.")));
    column->addWidget(label(tr("Filename Template")));
    nameTemplate = new QLineEdit(preferences->nameTemplate, this);
    column->addWidget(nameTemplate);
    column->addWidget(hint(tr("Pattern: {name} = picture file name, {suffix} = suffix, {ext} = extension, "
                              "{dither} = algorithm, {dpi} = output DPI. Separated inks add _Cyan, _Magenta... "
                              "to the name.")));
    QFrame* previewBox = new QFrame(this);
    previewBox->setObjectName("previewBox");
    QHBoxLayout* previewLine = new QHBoxLayout(previewBox);
    previewLine->setContentsMargins(12, 10, 12, 10);
    QLabel* previewLabel = label(tr("Preview:"));
    preview = new QLabel(previewBox);
    preview->setObjectName("previewChip");
    preview->setFont(monospace());
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLine->addWidget(previewLabel);
    previewLine->addWidget(preview);
    previewLine->addStretch(1);
    column->addWidget(previewBox);

    // Default Folders
    folderSection = sectionHeader(":/resources/folder.svg", tr("Default Folders"));
    column->addSpacing(10);
    column->addWidget(folderSection);
    column->addWidget(label(tr("Default Open Folder")));
    openFolder = new QLineEdit(QDir::toNativeSeparators(preferences->openFolder), this);
    column->addWidget(folderRow(openFolder, tr("Default Open Folder")));
    column->addWidget(hint(tr("Where Open starts. Leave empty to start in the folder used last. "
                              "Your setting is saved automatically.")));
    column->addWidget(label(tr("Default Save Folder")));
    saveFolder = new QLineEdit(QDir::toNativeSeparators(preferences->saveFolder), this);
    column->addWidget(folderRow(saveFolder, tr("Default Save Folder")));
    column->addWidget(hint(tr("Where Save and Save As start for a new film. Leave empty to save next to the "
                              "picture or in the folder used last. Your setting is saved automatically.")));
    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    column->addSpacing(8);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    // every change is kept at once
    connect(autoSuffix, &QAbstractButton::toggled, this, [this](const bool on) {
        this->preferences->autoSuffix = on;
        suffix->setEnabled(on);
        updatePreview();
        emit changed();
    });
    connect(suffix, &QLineEdit::textChanged, this, [this](const QString& text) {
        this->preferences->suffix = text;
        updatePreview();
        emit changed();
    });
    connect(nameTemplate, &QLineEdit::textChanged, this, [this](const QString& text) {
        this->preferences->nameTemplate = text;
        updatePreview();
        emit changed();
    });
    connect(openFolder, &QLineEdit::editingFinished, this, [this]() {
        this->preferences->openFolder = QDir::fromNativeSeparators(openFolder->text().trimmed());
        emit changed();
    });
    connect(saveFolder, &QLineEdit::editingFinished, this, [this]() {
        this->preferences->saveFolder = QDir::fromNativeSeparators(saveFolder->text().trimmed());
        emit changed();
    });
    suffix->setEnabled(preferences->autoSuffix);
    updatePreview();
}

void FilesDialog::updatePreview() {
    preview->setText(fileNameFromTemplate(*preferences, example));
}

void FilesDialog::focusSection(const Section section) {
    (section == Section::Folders ? openFolder : suffix)->setFocus();
}

/*********************
 * CALIBRATION       *
 *********************/

class CalibrationDialog::Bar final : public QWidget {
public:
    explicit Bar(QWidget* parent) : QWidget(parent) {
        setMinimumHeight(70);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::SizeHorCursor);
    }
    int pixels = 400;
    std::function<void()> moved;
    void setPixels(const int value) {
        pixels = std::clamp(value, 50, std::max(50, width() - 2 * MARGIN));
        update();
        if (moved) moved();
    }
protected:
    static constexpr int MARGIN = 12;
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.fillRect(rect(), QColor(0x10, 0x12, 0x16));
        const int top = 18, height = 24;
        painter.fillRect(QRect(MARGIN, top, pixels, height), QColor(0x9f, 0xc3, 0xe6));
        painter.setPen(QColor(0xe6, 0xe8, 0xec));
        // the two ends, where the ruler's marks go
        painter.drawLine(MARGIN, top - 10, MARGIN, top + height + 10);
        painter.drawLine(MARGIN + pixels, top - 10, MARGIN + pixels, top + height + 10);
        painter.setPen(QColor(0x6f, 0x75, 0x7f));
        painter.drawText(QRect(MARGIN, top + height + 8, pixels, 20), Qt::AlignCenter,
                         QObject::tr("%1 px - drag the right end, or use the arrow keys").arg(pixels));
    }
    void mousePressEvent(QMouseEvent* event) override { setPixels(static_cast<int>(event->position().x()) - MARGIN); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) setPixels(static_cast<int>(event->position().x()) - MARGIN);
    }
    void keyPressEvent(QKeyEvent* event) override {
        const int step = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
        if (event->key() == Qt::Key_Right) setPixels(pixels + step);
        else if (event->key() == Qt::Key_Left) setPixels(pixels - step);
        else QWidget::keyPressEvent(event);
    }
};

CalibrationDialog::CalibrationDialog(const double currentPpi, const double systemPpi, QWidget* parent)
    : QDialog(parent), systemPpi(systemPpi) {
    setWindowTitle(tr("Calibrate Screen"));
    setStyleSheet(DIALOG_STYLE);
    setMinimumWidth(760);
    QVBoxLayout* column = new QVBoxLayout(this);
    column->setContentsMargins(18, 12, 18, 16);
    QLabel* title = new QLabel(tr("Screen Calibration"), this);
    title->setObjectName("sectionTitle");
    column->addWidget(title);
    QLabel* how = new QLabel(tr("Hold a ruler against the screen and stretch the bar until it measures exactly the "
                                "length below. Zoom 1:1 then shows films at their size on paper."), this);
    how->setObjectName("hint");
    how->setWordWrap(true);
    column->addWidget(how);
    QHBoxLayout* lengthLine = new QHBoxLayout();
    QLabel* lengthLabel = new QLabel(tr("The bar measures"), this);
    lengthLabel->setObjectName("fieldLabel");
    length = new QDoubleSpinBox(this);
    length->setRange(20.0, 400.0);
    length->setDecimals(1);
    length->setSuffix(" mm");
    length->setValue(100.0);
    lengthLine->addWidget(lengthLabel);
    lengthLine->addWidget(length);
    lengthLine->addStretch(1);
    column->addLayout(lengthLine);
    bar = new Bar(this);
    column->addWidget(bar);
    result = new QLabel(this);
    result->setObjectName("fieldLabel");
    column->addWidget(result);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    QPushButton* useSystem = buttons->addButton(tr("Use System Value"), QDialogButtonBox::ResetRole);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(useSystem, &QPushButton::clicked, this, [this]() {
        reset = true;
        accept();
    });
    bar->moved = [this]() { updateResult(); };
    connect(length, &QDoubleSpinBox::valueChanged, this, [this]() { updateResult(); });
    // start with the bar at 100 mm as currently believed
    const double start = currentPpi > 0.0 ? currentPpi : systemPpi;
    bar->pixels = static_cast<int>(std::lround(100.0 / 25.4 * start));
    updateResult();
}

double CalibrationDialog::ppi() const {
    return bar->pixels / (length->value() / 25.4);
}

void CalibrationDialog::updateResult() {
    result->setText(tr("%1 pixels per inch on this screen (system: %2)").arg(ppi(), 0, 'f', 1).arg(systemPpi, 0, 'f', 1));
}
