#include "preferencesdialogs.h"
#include <QComboBox>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// the look of the mock-ups: dark cards, bold titles over a rule, grey hints
const char* DIALOG_STYLE = R"(
QDialog, QScrollArea, QWidget#page { background: #141518; }
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

// a combo box listing (text, value) pairs, set to `current`, calling `picked` with the value chosen
template <typename T>
QComboBox* choice(QWidget* parent, const std::vector<std::pair<QString, T>>& items, const T& current,
                  const std::function<void(const T&)>& picked) {
    QComboBox* combo = new QComboBox(parent);
    for (size_t i = 0; i < items.size(); i++) {
        combo->addItem(items[i].first);
        if (items[i].second == current) {
            combo->setCurrentIndex(static_cast<int>(i));
        }
    }
    QObject::connect(combo, &QComboBox::currentIndexChanged, parent, [items, picked](const int index) {
        if (index >= 0) picked(items[static_cast<size_t>(index)].second);
    });
    return combo;
}

// the grey slider: a white-to-black groove, and a mark under the default grey
class GreySlider final : public QSlider {
public:
    GreySlider(const int markAt, QWidget* parent) : QSlider(Qt::Horizontal, parent), mark(markAt) {
        setRange(0, 255);
        setMinimumHeight(30);
        setStyleSheet("QSlider::groove:horizontal { height: 8px; border-radius: 4px; border: 1px solid #3a3f48;"
                      "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ffffff, stop:1 #000000); }"
                      "QSlider::sub-page:horizontal, QSlider::add-page:horizontal { background: transparent; }"
                      "QSlider::handle:horizontal { background: #9fc3e6; width: 14px; margin: -5px 0; border-radius: 7px; }");
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QSlider::paintEvent(event);
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
        const int x = groove.left() + handle.width() / 2 +
                      QStyle::sliderPositionFromValue(minimum(), maximum(), mark, groove.width() - handle.width());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x9f, 0xc3, 0xe6));
        const double y = groove.center().y() + 9.0;  // a small triangle under the groove
        painter.drawPolygon(QPolygonF({QPointF(x, y), QPointF(x - 4.0, y + 6.0), QPointF(x + 4.0, y + 6.0)}));
    }
private:
    int mark;
};

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

/*********************
 * PREFERENCES       *
 *********************/

QWidget* PreferencesDialog::sectionHeader(const Section section, const QString& icon, const QString& title) {
    QWidget* header = new QWidget(scroll->widget());
    QVBoxLayout* column = new QVBoxLayout(header);
    column->setContentsMargins(0, 14, 0, 4);
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
    sections.emplace_back(section, header);
    return header;
}

QLabel* PreferencesDialog::hint(const QString& text) {
    QLabel* l = new QLabel(text, scroll->widget());
    l->setObjectName("hint");
    l->setWordWrap(true);
    return l;
}

QLabel* PreferencesDialog::label(const QString& text) {
    QLabel* l = new QLabel(text, scroll->widget());
    l->setObjectName("fieldLabel");
    return l;
}

QWidget* PreferencesDialog::folderRow(QLineEdit* field, const QString& title) {
    QWidget* row = new QWidget(scroll->widget());
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

PreferencesDialog::PreferencesDialog(Preferences* preferences, const FileNameFields& example, QWidget* parent)
    : QDialog(parent), preferences(preferences), example(example) {
    setWindowTitle(tr("Preferences"));
    setStyleSheet(DIALOG_STYLE);
    resize(660, 720);
    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 12);
    scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QWidget* page = new QWidget();
    page->setObjectName("page");
    scroll->setWidget(page);
    outer->addWidget(scroll, 1);
    QVBoxLayout* column = new QVBoxLayout(page);
    column->setContentsMargins(18, 4, 18, 12);
    column->setSpacing(6);
    const auto changedNow = [this](const Change what) { emit changed(what); };

    // Color Management
    column->addWidget(sectionHeader(Section::ColorManagement, ":/resources/pref_color.svg", tr("Color Management")));
    column->addWidget(label(tr("Working Color Profile")));
    std::vector<std::pair<QString, QString>> profiles;
    for (const WorkingProfile& p : workingProfiles()) {
        profiles.emplace_back(p.name, p.id);
    }
    column->addWidget(choice<QString>(this, profiles, preferences->workingProfile, [this, changedNow](const QString& id) {
        this->preferences->workingProfile = id;
        changedNow(Change::ColorProfile);
    }));
    column->addWidget(hint(tr("Pictures are converted from their own profile (sRGB when they have none) into this "
                              "space before any adjustment or dithering; palette colours and HEX values are "
                              "values in this space. sRGB suits most work and every screen; Adobe RGB, Display P3, "
                              "ProPhoto and Rec. 2020 keep more saturated colours for the separations. The preview "
                              "shows the values as they are, without converting them for the screen.")));
    QHBoxLayout* embedLine = new QHBoxLayout();
    ToggleSwitch* embed = new ToggleSwitch(page);
    embed->setChecked(preferences->embedProfile);
    embedLine->addWidget(label(tr("Embed the profile in colour exports")), 1);
    embedLine->addWidget(embed);
    column->addLayout(embedLine);
    column->addWidget(hint(tr("Colour PNG, TIFF and PSD files carry the working profile, so other programs read "
                              "their colours right. Black and white films have no colour and no profile.")));
    connect(embed, &QAbstractButton::toggled, this, [this, changedNow](const bool on) {
        this->preferences->embedProfile = on;
        changedNow(Change::Other);
    });
    column->addWidget(label(tr("When an Edit Replaces the Custom Palette")));
    column->addWidget(choice<Preferences::CustomPaletteReplace>(
        this, {{tr("Ask whether to save it first (default)"), Preferences::CustomPaletteReplace::Ask},
               {tr("Always save it to a file first"), Preferences::CustomPaletteReplace::Save},
               {tr("Replace it without asking"), Preferences::CustomPaletteReplace::Replace}},
        preferences->customPaletteReplace, [this, changedNow](const Preferences::CustomPaletteReplace choice) {
            this->preferences->customPaletteReplace = choice;
            changedNow(Change::Other);
        }));
    column->addWidget(hint(tr("Editing a built-in, file or reduced palette makes it the custom palette, in place of "
                              "the one made before. \"Don't ask again\" in that question sets this choice.")));

    // Preview Quality
    column->addWidget(sectionHeader(Section::PreviewQuality, ":/resources/pref_preview.svg", tr("Preview Quality")));
    column->addWidget(choice<int>(this, {{tr("Original (100%) - best quality (default)"), 100},
                                         {tr("Medium (75%) - about 2x faster"), 75},
                                         {tr("Low (50%) - about 4x faster"), 50}},
                                  preferences->previewQuality, [this, changedNow](const int quality) {
        this->preferences->previewQuality = quality;
        changedNow(Change::PreviewQuality);
    }));
    column->addWidget(hint(tr("How it works: lower settings render the preview from a smaller copy of the picture, "
                              "shown at the same size on screen. This makes the preview much faster for large "
                              "films.\nLow (50%): about 4x faster, for quick edits and large pictures (4K and "
                              "more). Medium (75%): about 2x faster, balanced. Original (100%): full resolution "
                              "preview, slowest but most accurate.\nNote: saved films, copies and PSDs are always "
                              "rendered at full quality, whatever this setting.")));

    // Zoom
    column->addWidget(sectionHeader(Section::Zoom, ":/resources/pref_zoom.svg", tr("Zoom and Mouse Wheel")));
    column->addWidget(label(tr("Zoom Mode")));
    column->addWidget(choice<Preferences::ZoomMode>(
        this, {{tr("Smooth, around the pointer (default)"), Preferences::ZoomMode::SmoothPointer},
               {tr("Stepped, around the pointer"), Preferences::ZoomMode::SteppedPointer},
               {tr("Stepped, around the centre (upstream Ditherista)"), Preferences::ZoomMode::SteppedCentre}},
        preferences->zoomMode, [this, changedNow](const Preferences::ZoomMode mode) {
            this->preferences->zoomMode = mode;
            changedNow(Change::View);
        }));
    column->addWidget(hint(tr("Smooth glides to each new zoom; stepped jumps there. Around the pointer, the point "
                              "under the mouse stays put and the wheel up zooms in; upstream zooms around the "
                              "centre, the wheel up zooming out.")));
    const auto toggle = [this, page, column, changedNow](const QString& text, bool Preferences::* field) {
        QHBoxLayout* line = new QHBoxLayout();
        ToggleSwitch* button = new ToggleSwitch(page);
        button->setChecked(this->preferences->*field);
        line->addWidget(label(text), 1);
        line->addWidget(button);
        column->addLayout(line);
        connect(button, &QAbstractButton::toggled, this, [this, field, changedNow](const bool on) {
            this->preferences->*field = on;
            changedNow(Change::View);
        });
    };
    toggle(tr("Invert Wheel Zoom"), &Preferences::invertWheel);
    column->addWidget(hint(tr("The wheel zooms the other way round, in every zoom mode.")));
    toggle(tr("Wheel Changes Values Over Fields"), &Preferences::wheelOverFields);
    column->addWidget(hint(tr("On: the wheel over a number, slider or list changes its value, as usual. Off: it "
                              "scrolls the settings instead, so a value never changes by accident while "
                              "scrolling; click and type, or drag, to change it.")));
    column->addWidget(label(tr("Zoom Increment (%)")));
    QHBoxLayout* incrementLine = new QHBoxLayout();
    QSlider* increment = new QSlider(Qt::Horizontal, page);
    increment->setRange(1, 50);
    increment->setValue(preferences->zoomIncrement);
    QLabel* incrementValue = label(QString("%1 %").arg(preferences->zoomIncrement));
    incrementValue->setMinimumWidth(48);
    incrementLine->addWidget(increment, 1);
    incrementLine->addWidget(incrementValue);
    column->addLayout(incrementLine);
    column->addWidget(hint(tr("How much one notch of the wheel zooms: that many points in stepped mode (10 % is "
                              "upstream's step), that many percent of the current zoom in smooth mode.")));
    connect(increment, &QSlider::valueChanged, this, [this, incrementValue, changedNow](const int value) {
        this->preferences->zoomIncrement = value;
        incrementValue->setText(QString("%1 %").arg(value));
        changedNow(Change::View);
    });

    // Background
    column->addWidget(sectionHeader(Section::Background, ":/resources/pref_grid.svg", tr("Background")));
    column->addWidget(choice<Preferences::Background>(
        this, {{tr("Solid colour"), Preferences::Background::Solid},
               {tr("Graph paper, white (1 mm, thick line every 1 cm)"), Preferences::Background::GraphPaperWhite},
               {tr("Graph paper, black (1 mm, thick line every 1 cm)"), Preferences::Background::GraphPaperBlack}},
        preferences->background, [this, changedNow](const Preferences::Background mode) {
            this->preferences->background = mode;
            changedNow(Change::View);
        }));
    QHBoxLayout* greyLine = new QHBoxLayout();
    QSlider* grey = new GreySlider(Preferences().backgroundGrey, page);  // marked at the default grey
    grey->setValue(preferences->backgroundGrey);
    grey->setToolTip(tr("White on the left, black on the right; the mark is the default grey"));
    QPushButton* greyReset = new QPushButton(tr("Default"), page);
    greyReset->setObjectName("browse");
    greyLine->addWidget(label(tr("White")));
    greyLine->addWidget(grey, 1);
    greyLine->addWidget(label(tr("Black")));
    greyLine->addWidget(greyReset);
    column->addLayout(greyLine);
    column->addWidget(hint(tr("The colour around the picture, for the solid background: the mark is the "
                              "default grey. The graph paper follows the film's real size: one thin line per "
                              "millimetre, a thick one per centimetre, from the picture's corner.")));
    connect(grey, &QSlider::valueChanged, this, [this, changedNow](const int value) {
        this->preferences->backgroundGrey = value;
        changedNow(Change::View);
    });
    connect(greyReset, &QPushButton::clicked, this, [grey]() { grey->setValue(Preferences().backgroundGrey); });

    // Clipboard
    column->addWidget(sectionHeader(Section::Clipboard, ":/resources/pref_clipboard.svg", tr("Clipboard")));
    column->addWidget(label(tr("Copy to Clipboard puts")));
    column->addWidget(choice<Preferences::ClipboardContent>(
        this, {{tr("The film as shown - composite, lossless (default)"), Preferences::ClipboardContent::Composite},
               {tr("Ask which channel, when separating (one ink, the print, or every ink)"), Preferences::ClipboardContent::AskChannel}},
        preferences->clipboardContent, [this, changedNow](const Preferences::ClipboardContent content) {
            this->preferences->clipboardContent = content;
            changedNow(Change::Other);
        }));
    column->addWidget(label(tr("File Format of the Copies")));
    column->addWidget(choice<QString>(this, {{tr("PNG"), "png"}, {tr("TIFF (PackBits, lossless)"), "tif"}, {tr("PSD"), "psd"}},
                                      preferences->clipboardFormat, [this, changedNow](const QString& format) {
        this->preferences->clipboardFormat = format;
        changedNow(Change::Other);
    }));
    column->addWidget(hint(tr("The copy is rendered at the output DPI, like Save. It goes on the clipboard twice: "
                              "as pixels, for programs that paste an image (the resolution is then set in the "
                              "program), and as files with the DPI and profile inside, for programs that paste "
                              "files (Explorer, a folder, some layout programs). When separating, \"ask\" opens a "
                              "small window to pick the simulated print, one ink's film, or every ink as files.")));

    // Filename Settings
    column->addWidget(sectionHeader(Section::FileNames, ":/resources/file.svg", tr("Filename Settings")));
    QHBoxLayout* suffixLine = new QHBoxLayout();
    ToggleSwitch* autoSuffix = new ToggleSwitch(page);
    autoSuffix->setChecked(preferences->autoSuffix);
    suffixLine->addWidget(label(tr("Auto-Add Suffix")), 1);
    suffixLine->addWidget(autoSuffix);
    column->addLayout(suffixLine);
    column->addWidget(label(tr("Filename Suffix")));
    QLineEdit* suffix = new QLineEdit(preferences->suffix, page);
    column->addWidget(suffix);
    column->addWidget(hint(tr("Text appended to the file name (e.g. \"image_errordiff_floyd-steinberg.png\"). "
                              "It may use {dither} and {dpi}.")));
    column->addWidget(label(tr("Filename Template")));
    QLineEdit* nameTemplate = new QLineEdit(preferences->nameTemplate, page);
    column->addWidget(nameTemplate);
    column->addWidget(hint(tr("Pattern: {name} = picture file name, {suffix} = suffix, {ext} = extension, "
                              "{dither} = algorithm, {dpi} = output DPI. Separated inks add _Cyan, _Magenta... "
                              "to the name.")));
    QFrame* previewBox = new QFrame(page);
    previewBox->setObjectName("previewBox");
    QHBoxLayout* previewLine = new QHBoxLayout(previewBox);
    previewLine->setContentsMargins(12, 10, 12, 10);
    preview = new QLabel(previewBox);
    preview->setObjectName("previewChip");
    preview->setFont(monospace());
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLine->addWidget(label(tr("Preview:")));
    previewLine->addWidget(preview);
    previewLine->addStretch(1);
    column->addWidget(previewBox);
    connect(autoSuffix, &QAbstractButton::toggled, this, [this, suffix, changedNow](const bool on) {
        this->preferences->autoSuffix = on;
        suffix->setEnabled(on);
        updatePreview();
        changedNow(Change::Other);
    });
    connect(suffix, &QLineEdit::textChanged, this, [this, changedNow](const QString& text) {
        this->preferences->suffix = text;
        updatePreview();
        changedNow(Change::Other);
    });
    connect(nameTemplate, &QLineEdit::textChanged, this, [this, changedNow](const QString& text) {
        this->preferences->nameTemplate = text;
        updatePreview();
        changedNow(Change::Other);
    });
    suffix->setEnabled(preferences->autoSuffix);

    // Default Folders
    column->addWidget(sectionHeader(Section::Folders, ":/resources/folder.svg", tr("Default Folders")));
    column->addWidget(label(tr("Default Open Folder")));
    QLineEdit* openFolder = new QLineEdit(QDir::toNativeSeparators(preferences->openFolder), page);
    column->addWidget(folderRow(openFolder, tr("Default Open Folder")));
    column->addWidget(hint(tr("Where Open starts. Leave empty to start in the folder used last. "
                              "Your setting is saved automatically.")));
    column->addWidget(label(tr("Default Save Folder")));
    QLineEdit* saveFolder = new QLineEdit(QDir::toNativeSeparators(preferences->saveFolder), page);
    column->addWidget(folderRow(saveFolder, tr("Default Save Folder")));
    column->addWidget(hint(tr("Where Save and Save As start for a new film. Leave empty to save next to the "
                              "picture or in the folder used last. Your setting is saved automatically.")));
    connect(openFolder, &QLineEdit::editingFinished, this, [this, openFolder, changedNow]() {
        this->preferences->openFolder = QDir::fromNativeSeparators(openFolder->text().trimmed());
        changedNow(Change::Other);
    });
    connect(saveFolder, &QLineEdit::editingFinished, this, [this, saveFolder, changedNow]() {
        this->preferences->saveFolder = QDir::fromNativeSeparators(saveFolder->text().trimmed());
        changedNow(Change::Other);
    });
    column->addStretch(1);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QHBoxLayout* bottom = new QHBoxLayout();
    bottom->setContentsMargins(18, 0, 18, 0);
    bottom->addWidget(buttons);
    outer->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    updatePreview();
}

void PreferencesDialog::updatePreview() {
    preview->setText(fileNameFromTemplate(*preferences, example));
}

void PreferencesDialog::showSection(const Section section) {
    for (const auto& [which, header] : sections) {
        if (which == section) {
            // the section's title at the top of the page
            scroll->verticalScrollBar()->setValue(header->y());
        }
    }
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
