#pragma once
#ifndef PREFERENCESDIALOGS_H
#define PREFERENCESDIALOGS_H

#include "preferences.h"
#include <QAbstractButton>
#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;

/* The windows of the Preferences menu (see preferences.h). Changes apply at once and are reported with
 * `changed`; MainWindow saves them. */

// on / off switch, as in the Filename Settings mock-up
class ToggleSwitch final : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override { return {40, 22}; }
protected:
    void paintEvent(QPaintEvent* event) override;
};

/* Filename Settings and Default Folders */
class FilesDialog final : public QDialog {
    Q_OBJECT
public:
    FilesDialog(Preferences* preferences, const FileNameFields& example, QWidget* parent = nullptr);
    enum class Section { FileNames, Folders };
    void focusSection(Section section);
signals:
    void changed();
private:
    Preferences* preferences;
    FileNameFields example;
    ToggleSwitch* autoSuffix;
    QLineEdit* suffix;
    QLineEdit* nameTemplate;
    QLabel* preview;
    QLineEdit* openFolder;
    QLineEdit* saveFolder;
    QWidget* folderSection;
    void updatePreview();
    QWidget* sectionHeader(const QString& icon, const QString& title);
    QLabel* hint(const QString& text);
    QWidget* folderRow(QLineEdit* field, const QString& title);
};

/* Screen calibration: a bar to stretch until it measures a given length on the screen, with a ruler */
class CalibrationDialog final : public QDialog {
    Q_OBJECT
public:
    // currentPpi: the calibrated figure, or 0; systemPpi: what the system reports, used until calibrated
    CalibrationDialog(double currentPpi, double systemPpi, QWidget* parent = nullptr);
    [[nodiscard]] double ppi() const;  // screen pixels per inch from the bar and the length it measures
    [[nodiscard]] bool resetRequested() const { return reset; }
private:
    class Bar;
    Bar* bar;
    QDoubleSpinBox* length;  // mm the bar measures on the screen
    QLabel* result;
    double systemPpi;
    bool reset = false;
    void updateResult();
};

#endif // PREFERENCESDIALOGS_H
