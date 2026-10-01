#include "abandonedrenders.h"
#include <QFutureWatcher>
#include <cstdlib>
#include <functional>
#include <vector>

namespace AbandonedRenders {

namespace {
std::vector<QFuture<void>>& futures() {
    static std::vector<QFuture<void>> list;
    return list;
}

std::vector<std::function<void()>>& kept() {
    static std::vector<std::function<void()>> list;
    return list;
}

void freeKept() {
    std::vector<std::function<void()>> pending;
    pending.swap(kept());
    for (const std::function<void()>& free : pending) {
        free();
    }
}

template <typename Free>
void keepOrFree(Free free) {
    if (running()) {
        kept().push_back(free);
    } else {
        free();
    }
}
}  // namespace

void adopt(const QFuture<void>& future) {
    if (future.isFinished()) {
        return;
    }
    futures().push_back(future);
    // when it ends: free what was kept, if it was the last one (the watcher lives on the GUI thread)
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>();
    QObject::connect(watcher, &QFutureWatcher<void>::finished, watcher, [watcher]() {
        (void)running();
        watcher->deleteLater();
    });
    watcher->setFuture(future);
}

bool running() {
    std::vector<QFuture<void>>& list = futures();
    std::erase_if(list, [](const QFuture<void>& future) { return future.isFinished(); });
    if (list.empty() && !kept().empty()) {
        freeKept();
    }
    return !list.empty();
}

int keptCount() {
    return static_cast<int>(kept().size());
}

void release(DitherImage* image) {
    if (image != nullptr) {
        keepOrFree([image]() { DitherImage_free(image); });
    }
}

void release(ColorImage* image) {
    if (image != nullptr) {
        keepOrFree([image]() { ColorImage_free(image); });
    }
}

void releaseBuffer(void* buffer) {
    if (buffer != nullptr) {
        keepOrFree([buffer]() { std::free(buffer); });
    }
}

}  // namespace AbandonedRenders
