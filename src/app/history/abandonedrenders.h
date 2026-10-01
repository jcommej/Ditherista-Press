#pragma once
#ifndef ABANDONEDRENDERS_H
#define ABANDONEDRENDERS_H

#include <QFuture>
#include "libdither.h"

/* Renders stopped with the render control. libdither cannot be interrupted, so a stopped ditherer keeps running in
 * its thread until it ends, then its result is thrown away; the application goes on at once. Meanwhile the images
 * and buffers it may still read or write must stay alive: while any abandoned render runs, the release functions
 * below keep what they are given and free it all once the last one has ended. With none running they free at once.
 * GUI thread only. */
namespace AbandonedRenders {
void adopt(const QFuture<void>& future);  // a ditherer left running
[[nodiscard]] bool running();             // one still is (frees what was kept once none is)
[[nodiscard]] int keptCount();            // images and buffers waiting (tests)
void release(DitherImage* image);         // DitherImage_free, deferred while needed
void release(ColorImage* image);
void releaseBuffer(void* buffer);         // free(), deferred while needed
}

#endif // ABANDONEDRENDERS_H
