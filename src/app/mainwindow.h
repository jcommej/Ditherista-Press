#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ui_mainwindow.h"
#include "libdither.h"
#include "about/aboutwindow.h"
#include "help/helpwindow.h"
#include "enums.h"
#include "imagehash/imagehashmono.h"
#include "imagehash/imagehashcolor.h"
#include "ui_elements/notificationlabel.h"
#include "filemanager.h"
#include "ui_elements/mouseeventfilter.h"
#include "updatecheck.h"
#include "batch/batchditherdialog.h"
#include "screening/screengeometry.h"
#include "screening/separation.h"
#include "presets/presetstore.h"
#include "palette/palettemodel.h"
#include "palette/paletteeditor.h"
#include "palette/colourpickerdialog.h"
#include "preferences/preferences.h"
#include "preferences/preferencesdialogs.h"
#include "history/sessionhistory.h"
#include <QTimer>
#include <QJsonObject>
#include <memory>
#include <QTreeWidgetItem>
#include <functional>
#include <map>
#include <QScrollArea>
#include <vector>
#include <QFuture>
#include <QMainWindow>
#include <QtNetwork/QNetworkAccessManager>

// main tabWidget indexes
#define TAB_INDEX_MONO 0
#define TAB_INDEX_COLOR 1
#define TAB_INDEX_PALETTE 2

// palette source combobox index
#define PALETTE_BUILT_IN 0
#define PALETTE_FROM_FILE 1
#define PALETTE_REDUCED 2
#define PALETTE_CUSTOM 3

// paint.NET palette loading error codes
#define OK_PALETTE_LOAD 0
#define ERROR_PALETTE_LOAD_IO 1
#define ERROR_PALETTE_LOAD_PARSE 2
#define ERROR_PALETTE_LOAD_LOW_COLORS 3
#define ERROR_PALETTE_LOAD_MAX_COLORS 4

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// dither type mapping to stacked-widget (ui->ditherSettings) index pages (dithertype : index_page)
const QHash<int, int> ditherPage = {
        {DOT, 1}, {LIP, 1},  {PAT, 1}, {ORD_C, 1}, // generic dither settings page = 1
        {DBS, 2}, {ERR, 3}, {ORD, 4}, {RIM, 5}, {THR, 6}, {VAR, 7}, {ALL, 8},
        {ORD_IGR, 9}, {ORD_VA2, 10}, {ORD_VA4, 10}, {GRD, 11},
        {ERR_C, 12}, {ORD_VA2_C, 13}, {ORD_VA4_C, 13}, {ORD_IGR_C, 14}};

class RenderGlyphButton;

struct RenderCancelled {};  // thrown by runDitherThread when the render control stops a render

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    /* methods */
    explicit MainWindow(QWidget* parent = nullptr);
    void resizeEvent(QResizeEvent* event) override;
//    void keyPressEvent(QKeyEvent* event) override;
    ~MainWindow() override;
private:
    /* attributes */
    // GUI windows and program components
    Ui::MainWindow* ui{};                   // Qt UI for main window
    AboutWindow* aboutWindow;               // about window
    HelpWindow* helpWindow;                 // help window
    UpdateCheck* updateCheck;               // update check dialog
    FileManager fileManager;                // file open/save dialog
    BatchDitherDialog* batchDitherDialog;   // batch dither dialog
    NotificationLabel* notification;        // notification area inside the viewport
    MouseEventFilter eventFilter;           // filters out undesired mouse events
    QNetworkAccessManager* networkAccessManager{};  // manager http update checks
    // program state
    bool isDithering = false;               // true while dithering in progress
    bool firstLoad = true;                  // true if no image has been loaded yet
    int lastTabIndex = TAB_INDEX_MONO;      // last active index (mono or color tab; palette tab is ignored)
    QString currentDirectory;               // directory from where the current input image has been loaded from
    QString lastLoadedPalette;              // file name of the last palette loaded from external file
    QString lastBuiltInPalette;             // resource name of the last loaded built-in palette
    TreeWidget* activeTreeWidget = nullptr; // either colorTreeWidget or monoTreeWidget (based on active tab)
    // dithering
    QFuture<void> fthread;                 // thread for dithering
    ImageHashMono imageHashMono;           // caching for already dithered images Mono
    ImageHashColor imageHashColor;         // caching for already dithered images Color
    // what the ditherer functions read from: the preview caches above, or a full-resolution film during export
    ImageHashMono* monoTarget = &imageHashMono;
    ImageHashColor* colorTarget = &imageHashColor;
    double renderDpi = SCREEN_DEFAULT_DPI; // resolution of the image being dithered: preview DPI, or output DPI on export
    void ditherMonoInto(ImageHashMono& hash);
    void ditherColorInto(ImageHashColor& hash);
    int current_dither_number = 0;         // number of current ditherer (lower numbers=mono, higher numbers=color)
    DitherType current_dither_type = ALL;  // current dither type:     e.g. error diffusion
    SubDitherType current_sub_dither_type = ALL_ALL; // current sub-dither type: e.g. floyd steinberg error diffusion
    // color palette related
    enum QuantizationMethod colorReductionMode;
    enum ColorComparisonMode colorComparisonMode;
    BytePalette* reducedPalette = nullptr;    // reduced palette sRGB
    CachedPalette* cachedPalette = nullptr;   // reduced palette in a particular color format (e.g. LAB94, HSV, etc.)
    BytePalette* loadFromFilePalette = nullptr; // caches the last palette that has been loaded externally from file
    FloatColor srcIlluminant;                 // currently chosen illuminant for LAB color


    QString lastSavedPalette;

    BytePalette* customPalette = nullptr;

    // screen (output DPI / LPI / dot size), see mainwindow_screen.cpp
    ScreenGeometry screenGeometry;
    QComboBox* dpiCombo = nullptr;
    QCheckBox* lpiCheck = nullptr;
    QDoubleSpinBox* lpiSpin = nullptr;
    QCheckBox* dotCheck = nullptr;
    QDoubleSpinBox* dotSpin = nullptr;
    QLabel* screenInfoLabel = nullptr;
    QDoubleSpinBox* printWidthSpin = nullptr;
    QDoubleSpinBox* printHeightSpin = nullptr;
    QToolButton* aspectLockButton = nullptr;  // keeps print width and height in proportion
    QImage nativeImage;          // picture as loaded, before any resampling to the output DPI
    double printWidthMm = 0.0;   // physical print size: the reference the whole pipeline keeps
    double printHeightMm = 0.0;
    QImage adoptNativeImage(const QImage* image);  // new picture: print size and DPI from the file
    void applyFilterScale(const QSize& working, double dpi);
    bool applyOutputSize(double dpi, double widthMm, double heightMm);
    bool outputSizeFits(double dpi, double widthMm, double heightMm);  // false (with a notice) above EXPORT_MAX_PIXELS
    void setupScreenControls();
    void updateScreenControls();
    [[nodiscard]] bool screenUsesLpi() const;   // current algorithm has a screen cell (ordered matrix)
    [[nodiscard]] int screenDotPixels() const;  // coarse grid for the current algorithm (1 = full resolution)
    OrderedDitherMatrix* applyLpi(OrderedDitherMatrix* matrix, int width, int height) const;

    // colour separation (Color tab), see mainwindow_separation.cpp
    SeparationMode separationMode = SeparationMode::Composite;
    QGroupBox* separationGroup = nullptr;
    QComboBox* separationModeCombo = nullptr;
    QComboBox* separationViewCombo = nullptr;
    QComboBox* separationExportCombo = nullptr;
    QCheckBox* separationPsdCompositeCheck = nullptr;
    QComboBox* separationPsdLayoutCombo = nullptr;  // inks as layers, spot channels, or both
    struct ChannelSettings {
        bool enabled = true;
        double lpi = 0.0;    // 0 = follow the Screen panel's LPI
        double angle = 0.0;  // screen angle in degrees
    };
    std::map<int, std::vector<ChannelSettings>> channelSettings;  // per separation mode, kept across mode changes
    int renderChannel = -1;          // ink being rendered, for its LPI and angle; -1 = composite
    QWidget* channelRows = nullptr;  // Ink / LPI / Angle table, rebuilt when the mode changes
    std::vector<QWidget*> channelScreenWidgets;  // LPI and angle fields: only meaningful with an LPI screen
    [[nodiscard]] std::vector<QRgb> paletteColours() const;           // the colour ditherers' current palette
    [[nodiscard]] std::vector<InkChannel> separationInks() const;     // inks of the current mode
    void refreshSeparationInks();  // View selector and ink rows, after a mode or palette change
    std::vector<ChannelSettings>& currentChannelSettings();
    [[nodiscard]] const ChannelSettings* renderChannelSettings() const;
    void rebuildChannelRows();
    void updateChannelRowsEnabled();
    // palette editor (colour list, add / delete / lock / randomize, undo), see mainwindow_palette_editor.cpp
    PaletteEditor* paletteEditor = nullptr;
    PaletteHistory paletteHistory;
    std::vector<bool> customLocks;  // locks of customPalette's colours; ignored if the sizes differ
    void setupPaletteEditor();
    [[nodiscard]] PaletteEntries currentPaletteEntries() const;  // the palette the colour ditherers use
    // one undoable step: records the current palette, runs `beforeApply` (e.g. to realign the separation's
    // inks), applies; nothing happens if the user cancels replacing an earlier custom palette
    void editPalette(const PaletteEntries& edited, const std::function<void()>& beforeApply = {});
    void applyPaletteEntries(const PaletteEntries& entries);  // becomes the custom palette, then re-dither
    void undoPalette(bool redo);  // one step back or forward, from the Edit menu or the colour picker
    // colour picker session on one palette colour: live low-resolution preview while the colour moves, the full
    // preview and one undo step once it rests (see mainwindow_palette_editor.cpp)
    ColourPickerDialog* colourPicker = nullptr;
    int pickerIndex = -1;          // palette colour being picked; -1 = no session
    QRgb pickerPending = 0;        // latest colour from the picker
    QTimer* liveTimer = nullptr;   // coalesces picker changes into live previews
    QTimer* settleTimer = nullptr; // fires once the colour has rested
    std::unique_ptr<ImageHashColor> liveSource;  // reduced, adjusted picture for the live preview, kept per session
    qint64 liveSourceKey = 0;                    // the adjusted preview it was made from (QImage::cacheKey)
    void pickPaletteColour(int index);
    void renderLivePalettePreview();
    void settlePickerColour();
    void endPickerSession(bool keep);
    // Preferences menu: navigation, screen calibration, file names, default folders; mainwindow_preferences.cpp
    Preferences preferences;
    QString sourceFileName;       // picture file name without extension, for {name}; empty for a pasted picture
    PreferencesDialog* preferencesDialog = nullptr;
    // Preferences menu entries also set from the Preferences window
    std::vector<QAction*> zoomModeActions;
    QAction* invertWheelAction = nullptr;
    QAction* wheelOverFieldsAction = nullptr;
    QImage loadedImage;                   // the picture as read, with its own colour profile
    void setupPreferences();
    void setupFileMenu();                 // Open Recent, Paste Image, Copy to Clipboard
    void savePreferences();
    void applyNavigation();               // navigation, zoom increment and background of the view
    void preferencesChanged(PreferencesDialog::Change what);
    [[nodiscard]] double screenPpi() const;  // calibrated, or what the system reports
    void zoomToRealSize();                   // 1:1: the film at its size on paper
    void showPreferences(PreferencesDialog::Section section);
    [[nodiscard]] QString suggestedFileName() const;  // from the Filename Settings
    // preview resolution for a film: the preview cap, times the Preview Quality
    [[nodiscard]] double previewDpi(double dpi, double widthMm, double heightMm) const;
    // colour management: pictures in the working profile; colour exports carry it when asked
    [[nodiscard]] QImage toWorkingSpace(const QImage& image) const;
    [[nodiscard]] QImage withProfile(QImage film) const;
    void copyToClipboard();  // Copy to Clipboard: the film as pixels, and as files, see Preferences > Clipboard
    // render control: pause automatic rendering, then render once, see mainwindow_render.cpp
    RenderGlyphButton* renderButton = nullptr;
    bool renderPaused = false;      // reDither only marks renderDirty
    bool renderDirty = false;       // something changed while paused: the result on screen is stale
    bool renderFlushing = false;    // the held work is being done, before the one render
    bool sourceDirtyMono = false;   // adjustments changed while paused: adjustSource still to run
    bool sourceDirtyColor = false;
    bool outputSizeDirty = false;   // DPI, print size or preview quality changed while paused: resample still to run
    void setupFavoriteDitherers();  // favourite stars of both ditherer lists, kept in the preferences
    void setupRenderControl();
    void renderButtonClickedSlot();
    void renderPending();           // the held work, then one render
    void renderBeforeExport();      // save / copy while paused: the file matches the settings shown
    bool requestOutputSize(double dpi, double widthMm, double heightMm);  // applyOutputSize, or held while paused
    // stopping a render with the render control or Esc: runDitherThread throws RenderCancelled back to reDither
    bool renderStoppable = false;     // inside reDither: a stop is possible
    bool renderStopRequested = false;
    bool renderFromFlush = false;     // the render the paused control asked for
    bool stoppedFlush = false;        // ...was the one stopped: its settings stay
    void requestRenderStop();
    void renderStopped();             // the change is undone, the control pauses
    // undo / redo of every setting, see mainwindow_history.cpp
    SessionHistory history;
    QTimer captureTimer;              // records a state shortly after a change
    QAction* undoAction = nullptr;
    QAction* redoAction = nullptr;
    bool restoringHistory = false;
    void setupHistory();
    void scheduleHistoryCapture();
    void resetHistory();              // a new picture: a new history
    void updateHistoryActions();
    [[nodiscard]] QJsonObject captureSession() const;
    void restoreSession(const QJsonObject& target);
    void undoSession(bool redo);
    void selectDitherer(TreeWidget* tree, int id, bool load);
    // presets, see mainwindow_presets.cpp
    std::unique_ptr<PresetStore> presetStore;
    QGroupBox* presetGroup = nullptr;
    QComboBox* presetCombo = nullptr;
    bool applyingPreset = false;  // holds reDither while a preset sets controls one by one
    void setupPresetControls();
    void refreshPresetList(const QString& select);
    [[nodiscard]] QJsonObject capturePreset() const;
    void applyPreset(const QJsonObject& preset);
    void applyPresetPalette(const QJsonObject& palette);
    [[nodiscard]] QJsonObject captureAdjustments(bool mono) const;  // Input Image Settings of one tab
    void applyAdjustments(const QJsonObject& adjust, bool mono);
    // settings panels in a scroll area, so the ditherer list keeps its room on small screens
    QGroupBox* screenGroup = nullptr;
    QScrollArea* settingsScroll = nullptr;
    QWidget* settingsPanel = nullptr;
    void setupSettingsScroll();
    void updateSettingsPanelHeight();
    std::vector<QImage> separationFilms;  // preview films, one per ink
    int separationFilmsFor = -1;          // dither number they were rendered with; -1 = stale
    QImage previewImage;                  // the picture at the preview's resolution, before any adjustment
    void setupSeparationControls();
    [[nodiscard]] bool separationActive() const;  // Color tab with CMYK, RGB or Palette selected
    void invalidateSeparation() { separationFilmsFor = -1; }
    // `preview`: `working` is previewImage, whose colour dither the Palette mode takes from imageHashColor
    std::vector<QImage> renderSeparation(const QImage& working, double dpi, double upscale, bool preview);
    [[nodiscard]] QImage separationView(const std::vector<QImage>& films) const;
    void showSeparation();
    std::vector<QImage> separationFilmsAtOutput();
    bool saveSeparation(const QString& fileName, QString* error, int* written);

    // shadows / midtones / highlights / blur / denoise rows, see mainwindow_tone.cpp
    struct AdjustControl {
        QSlider* slider;
        QDoubleSpinBox* spin;
    };
    std::vector<AdjustControl> adjustControls;
    void setupToneControls();
    void resetToneControls();
    void addAdjustRow(QGridLayout* grid, int row, const QString& label, const QString& toolTip, int minimum,
                      int maximum, double scale, int decimals,
                      const std::function<void(int)>& apply);

    /* methods */
    void setDitherImageMono();      // sets ditherimage in graphicsview and applies user chosen light/dark colors
    // UI related and helpers
    void setMouseBusy(bool isBusy);
    void enableGui(bool enable);
    QPixmap* loadSvg(const QString& fileName); // load SVG as QPixmap from (resource) file
    void setResetIcon(QPushButton* button);  // size and pixel cross of a reset button
    void uiSetup();                       // ui initialization (main)
    void setDithererDefaults();           // ui initialization
    void populateDithererSelection();     // ui initialization
    void addPredefinedPalettes();         // ui initialization
    void populateColorComparisonCombo();  // ui initialization
    void connectSignals();                // ui initialization
    void setApplicationDefaults();        // initialization - set default application values
    void expandColorComparisonArea(bool expand);
    // dithering (general)
    void runDitherThread();
    void reDither(bool force);
    // dithering mono
    void ALL_dither(uint8_t* out_buf);
    void GRD_dither(uint8_t* out_buf);
    void DBS_dither(uint8_t* out_buf);
    void THR_dither(uint8_t* out_buf);
    void DOT_dither(uint8_t* out_buf, SubDitherType n);
    void ERR_dither(uint8_t* out_buf, SubDitherType n);
    void LIP_dither(uint8_t* out_buf, SubDitherType n);
    void ORD_dither(uint8_t* out_buf, SubDitherType n);
    void PAT_dither(uint8_t* out_buf, SubDitherType n);
    void RIM_dither(uint8_t* out_buf, SubDitherType n);
    void VAR_dither(uint8_t* out_buf, SubDitherType n);
    // dithering color
    void ERR_C_dither(int* out_buf, const SubDitherType n);
    void ORD_C_dither(int* out_buf, const SubDitherType n);
    ErrorDiffusionMatrix* colorErrorMatrix(SubDitherType n);
    OrderedDitherMatrix* colorOrderedMatrix(SubDitherType n);
    void ditherInkPlane(ImageHashMono& plane);  // one CMYK / RGB ink, with the current color ditherer's matrix
    // batch dithering
    BatchDitherResult ditherSingleImage(const QString& inFileName, const QString& outFileName);
    // palette and color handling
    BytePalette* loadPaintNetPalette(QString fileName, int* errorCode);
    void savePaintNetPalette(const PaletteEntries& palette);  // asks for the file name
    bool loadPalette(QString fileName);
    void updatePaletteColorSwatches(BytePalette* palette);
    void generateCachedPalette(bool dither, bool resetLab, bool updateSwatches);
    void resetLabHCVspinBoxes();
    void updatePaletteFromImage();
    void updateCachedPalette(BytePalette* pal);
    // file I/O
    void saveFile(const QString &fileName);
    QImage renderFilm();  // dithered image at the output DPI, re-rendered at full resolution if the preview is lighter
    void loadImage(const QImage* image);
    void fileSaveSlotImpl(bool saveAs);
    // image adjustments
    void adjustImageMono();
    void adjustImageColor();


    void refreshUiColorDitherStatus(bool resetLab, bool updateSwatches);
    void labHCVChanged();
    bool loadPaintNetPaletteWrapper(QString fileName);

private slots:
    // image adjustment mono
    void resetContrastButtonMonoClickedSlot();
    void resetBrightnessButtonMonoClickedSlot();
    void resetGammaButtonMonoClickedSlot();
    void contrastSliderMonoValueChangedSlot(int value);
    void brightnessSliderMonoValueChangedSlot(int value);
    void gammaSliderMonoValueChangedSlot(int value);
    void contrastEditMonoEditingFinishedSlot(double value = 0.0);
    void brightnessEditMonoEditingFinishedSlot(double value = 0.0);
    void gammaEditMonoEditingFinishedSlot(double value = 0.0);
    void showOriginalMonoButtonClickedSlot(int checked);
    // image adjustment color
    void resetContrastButtonColorClickedSlot();
    void resetBrightnessButtonColorClickedSlot();
    void resetGammaButtonColorClickedSlot();
    void resetSaturationButtonColorClickedSlot();
    void contrastSliderColorValueChangedSlot(int);
    void brightnessSliderColorValueChangedSlot(int);
    void gammaSliderColorValueChangedSlot(int);
    void saturationSliderColorValueChangedSlot(int);
    void contrastEditColorEditingFinishedSlot(double value = 0.0);
    void brightnessEditColorEditingFinishedSlot(double value = 0.0);
    void gammaEditColorEditingFinishedSlot(double value = 0.0);
    void saturationEditColorEditingFinishedSlot(double value = 0.0);
    void showOriginalColorButtonClickedSlot(int checked);
    // UI helpers
    void paletteSourceWidgetIndexChangedSlot(int index);
    void imageSettingsStackedWidgetIndexChangedSlot(int index);
    // file I/O
    void loadImageFromFileSlot(const QString &fileName);
    void fileSaveSlot();
    void fileSaveAsSlot();
    void fileOpenSlot();
    // dither settings mono
    void DBS_setFormulaSlot(int formula);
    void RIM_modRiemersmaToggledSlot(bool value);
    void THR_thresholdValueChangedSlot(double threshold);
    void THR_toggleAutoThresholdSlot(bool autoThreshold);
    void ORD_stepValueChangedSlot(int step);
    void ORD_IGR_aValueChangedSlot(double a);
    void ORD_IGR_bValueChangedSlot(double b);
    void ORD_IGR_cValueChangedSlot(double c);
    void ALL_randomizeToggleSlot(bool value);
    void GRD_altAlgorithmToggleSlot(bool value);
    void GRD_widthValueChangedSlot(int value) ;
    void GRD_heightValueChangedSlot(int value);
    void GRD_minPixelsValueChangedSlot(int value);
    void serpentineToggledSlot(bool serpentine);
    void jitterValueChangedSlot(double jitter);
    // dither settings color
    void serpentineColorToggledSlot(bool serpentine);
    void ORD_stepColorValueChangedSlot(int step);
    void ORD_IGR_aColorValueChangedSlot(double a);
    void ORD_IGR_bColorValueChangedSlot(double b);
    void ORD_IGR_cColorValueChangedSlot(double c);
    // dithering
    void forceReDitherSlot() { reDither(true); };
    void screenSettingsChangedSlot();
    void separationModeChangedSlot(int);
    void savePresetNamed(const QString& name);
    void loadPresetNamed(const QString& name);
    void separationViewChangedSlot(int);
    void outputDpiEditedSlot();
    void printWidthEditedSlot(double widthMm);
    void printHeightEditedSlot(double heightMm);
    // palette mono
    void monoColorOneChangedSlot(QColor color);
    void monoColorTwoChangedSlot(QColor color);
    void resetMonoColorsClickedSlot();
    // palette color
    void paletteSourceComboChangedSlot(int index);
    void predefinedPaletteComboChangedSlot(int index);
    void colorComparisonComboChangedSlot(int index);
    void paletteBrowseButtonClickedSlot();
    void palettePathEditEditingFinishedSlot();
    void paletteColorsEditEditingFinishedSlot();
    void colorReductionComboChangedSlot(int index);
    void paletteIncludeExtremeColorsSlot(int); // include light/dark, CMY, RGB color
    // palette LAB colors
    void spinBoxValueChangedSlot(double value);
    void spinBoxHueChangedSlot(double value);
    void spinBoxChromaChangedSlot(double value);
    void srcIlluminantComboChangedSlot(int index);
    // misc UI
    void treeWidgetItemChangedSlot(QTreeWidgetItem* item);
    void tabWidgetChangedSlot(int index);
    void copySlot();
    void pasteSlot();
    void editMenuAboutToShowSlot();
    void keyEventSlot(QKeyEvent* event);
    // batch dithering
//    void batchDitherRequestedSlot(); // TODO temporary disabled
    // online update check
    void networkRequestFinishedSlot(QNetworkReply* reply) const;
    void updateCheckSlot();

    void savePaletteButtonClickedSlot();
    void resetHueWeightButtonClickedSlot();
    void resetChromaWeightButtonClickedSlot();
    void resetValueWeightButtonClickedSlot();

    void zoomLevelChangedSlot(int zoomLevel);
    void zoomLevelComboCurrentIndexChangedSlot(int index);
    void zoomLevelEditingFinishedSlot();
};

#endif // MAINWINDOW_H
