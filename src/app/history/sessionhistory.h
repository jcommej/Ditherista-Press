#pragma once
#ifndef SESSIONHISTORY_H
#define SESSIONHISTORY_H

#include <QByteArray>
#include <QJsonObject>
#include <vector>

/* Undo / redo of every setting (Edit > Undo, Ctrl+Z): a list of states of the whole session, one per change, with the
 * current one in it. A state is a JSON object (MainWindow::captureSession); they are kept compact, without a limit
 * on the number of steps - a few kilobytes each. Recording a state while some were undone drops the undone ones,
 * as in any editor. One history per picture: opening a picture resets it. */
class SessionHistory {
public:
    void reset(const QJsonObject& state);           // a single state, nothing to undo
    bool record(const QJsonObject& state);          // after a change; false if it equals the current state
    void replaceCurrent(const QJsonObject& state);  // the same step, as the application settled it
    [[nodiscard]] bool canUndo() const { return index > 0; }
    [[nodiscard]] bool canRedo() const { return index + 1 < states.size(); }
    QJsonObject undo();  // the previous state, now current; check canUndo first
    QJsonObject redo();
    [[nodiscard]] QJsonObject current() const;
    [[nodiscard]] bool isEmpty() const { return states.empty(); }
    [[nodiscard]] size_t size() const { return states.size(); }
private:
    std::vector<QByteArray> states;  // compact JSON
    size_t index = 0;
};

#endif // SESSIONHISTORY_H
