#include "mouseeventfilter.h"
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QDebug>
#include <QScrollBar>
#include <QWheelEvent>

bool MouseEventFilter::redirectWheel(QObject* obj, QEvent* event) {
    /* a wheel event over a value field: handed to the scroll area around it rather than changing the value.
     * Scroll bars and open drop-down lists keep their wheel. */
    QWidget* widget = qobject_cast<QWidget*>(obj);
    QWidget* field = nullptr;
    for (QWidget* w = widget; w != nullptr && field == nullptr; w = w->parentWidget()) {
        if (qobject_cast<QScrollBar*>(w) || qobject_cast<QAbstractItemView*>(w) || w->isWindow()) {
            return false;
        }
        if (qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QComboBox*>(w) || qobject_cast<QAbstractSlider*>(w)) {
            field = w;
        }
    }
    if (field == nullptr) {
        return false;
    }
    for (QWidget* w = field->parentWidget(); w != nullptr; w = w->parentWidget()) {
        if (QAbstractScrollArea* area = qobject_cast<QAbstractScrollArea*>(w)) {
            QApplication::sendEvent(area->verticalScrollBar(), event);  // the panel scrolls as if over its bar
            return true;
        }
    }
    return true;  // nothing to scroll: the wheel does nothing
}

bool MouseEventFilter::eventFilter(QObject* obj, QEvent* event) {
    if (!wheelOverFields && event->type() == QEvent::Wheel && redirectWheel(obj, event)) {
        return true;
    }
    /* https://stackoverflow.com/questions/6580226/qt-change-cursor-to-hourglass-and-disable-cursor */
    if (uiDisabled) { // UI is disabled -> ignore all events
        switch (event->type()) {
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            case QEvent::Gesture:
            case QEvent::DragEnter:
            case QEvent::DragMove:
            case QEvent::DragLeave:
            case QEvent::Drop:
                return true;
            default:
                break;
        }
    } else { // UI is enabled -> catch cursor up/down key presses
        switch (event->type()) {
            case QEvent::KeyPress: {
                QKeyEvent *keyEvent = (QKeyEvent *) event;
                if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up ||
                        keyEvent->key() == Qt::Key_Plus || keyEvent->key() == Qt::Key_Minus) {
                    emit keyEventSignal(keyEvent);
                    return true;
                }
            }
            default:
                break;
        }
    }
    return QObject::eventFilter(obj, event);
}