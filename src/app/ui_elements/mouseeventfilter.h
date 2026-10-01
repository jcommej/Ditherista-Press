#pragma once
#ifndef MOUSEEVENTFILTER_H
#define MOUSEEVENTFILTER_H

/* an event-filter to ignore mouse events while the program is busy */

#include <QObject>
#include <QKeyEvent>

class MouseEventFilter final : public QObject {
    Q_OBJECT
signals:
    void keyEventSignal(QKeyEvent* event);
private:
    bool uiDisabled = false;
protected:
    /* methods */
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool wheelOverFields = true;
    bool redirectWheel(QObject* obj, QEvent* event);
public:
    void enableUi() { uiDisabled = false; };
    void disableUi() { uiDisabled = true; };
    // Preferences: false = the wheel over a number box, slider or drop-down list no longer changes its value
    // (a frequent accident while scrolling the settings); it scrolls the panel around it instead
    void setWheelOverFields(const bool on) { wheelOverFields = on; }
};

#endif  // MOUSEEVENTFILTER_H
