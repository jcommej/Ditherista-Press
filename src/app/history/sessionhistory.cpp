#include "sessionhistory.h"
#include <QJsonDocument>

namespace {
QByteArray pack(const QJsonObject& state) {
    return QJsonDocument(state).toJson(QJsonDocument::Compact);
}
}

void SessionHistory::reset(const QJsonObject& state) {
    states.clear();
    states.push_back(pack(state));
    index = 0;
}

bool SessionHistory::record(const QJsonObject& state) {
    const QByteArray packed = pack(state);
    if (!states.empty() && states[index] == packed) {
        return false;
    }
    if (!states.empty()) {
        states.resize(index + 1);  // undone steps are gone once something new is done
    }
    states.push_back(packed);
    index = states.size() - 1;
    return true;
}

void SessionHistory::replaceCurrent(const QJsonObject& state) {
    if (states.empty()) {
        reset(state);
    } else {
        states[index] = pack(state);
    }
}

QJsonObject SessionHistory::undo() {
    if (canUndo()) {
        index--;
    }
    return current();
}

QJsonObject SessionHistory::redo() {
    if (canRedo()) {
        index++;
    }
    return current();
}

QJsonObject SessionHistory::current() const {
    return states.empty() ? QJsonObject() : QJsonDocument::fromJson(states[index]).object();
}
