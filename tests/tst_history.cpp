#include <QtTest>
#include <QtConcurrent>
#include <atomic>
#include "history/abandonedrenders.h"
#include "history/sessionhistory.h"

/* Undo history of the session (history/sessionhistory.h) and the renders left running after a stop
 * (history/abandonedrenders.h) */
class TestHistory : public QObject {
    Q_OBJECT

    static QJsonObject state(int value) { return QJsonObject{{"contrast", value}, {"ditherer", 80}}; }
private slots:
    void recordsOnlyChanges() {
        SessionHistory history;
        history.reset(state(0));
        QVERIFY(!history.canUndo());
        QVERIFY(!history.record(state(0)));  // nothing changed: no step
        QVERIFY(history.record(state(10)));
        QVERIFY(history.record(state(20)));
        QCOMPARE(history.size(), size_t(3));
        QVERIFY(history.canUndo());
    }

    void undoRedoAndNoLimit() {
        SessionHistory history;
        history.reset(state(0));
        for (int i = 1; i <= 5000; i++) {  // no limit on the number of steps
            history.record(state(i));
        }
        for (int i = 4999; i >= 0; i--) {
            QCOMPARE(history.undo(), state(i));
        }
        QVERIFY(!history.canUndo());
        QCOMPARE(history.undo(), state(0));  // nothing before the first: stays
        QCOMPARE(history.redo(), state(1));
        QCOMPARE(history.redo(), state(2));
        QCOMPARE(history.current(), state(2));
    }

    void newChangeDropsTheUndoneSteps() {
        SessionHistory history;
        history.reset(state(0));
        history.record(state(1));
        history.record(state(2));
        history.undo();
        history.undo();
        QVERIFY(history.canRedo());
        QVERIFY(history.record(state(9)));  // a new branch
        QVERIFY(!history.canRedo());
        QCOMPARE(history.undo(), state(0));
        QCOMPARE(history.redo(), state(9));
    }

    void replaceCurrentAndReset() {
        SessionHistory history;
        history.reset(state(0));
        history.record(state(1));
        history.replaceCurrent(state(2));  // the same step, settled
        QCOMPARE(history.current(), state(2));
        QCOMPARE(history.undo(), state(0));
        history.reset(state(5));  // a new picture
        QVERIFY(!history.canUndo() && !history.canRedo());
        QCOMPARE(history.current(), state(5));
    }

    void abandonedRenderKeepsItsImagesAlive() {
        std::atomic<bool> release(false);
        QFuture<void> slow = QtConcurrent::run([&release]() {
            while (!release.load()) {
                QThread::msleep(5);
            }
        });
        AbandonedRenders::adopt(slow);
        QVERIFY(AbandonedRenders::running());
        AbandonedRenders::release(DitherImage_new(4, 4));   // what the ditherer reads: kept
        AbandonedRenders::releaseBuffer(calloc(16, 1));     // what it writes into: kept
        QCOMPARE(AbandonedRenders::keptCount(), 2);
        release.store(true);
        slow.waitForFinished();
        QTRY_COMPARE_WITH_TIMEOUT(AbandonedRenders::keptCount(), 0, 2000);  // freed once it has ended
        QVERIFY(!AbandonedRenders::running());
        AbandonedRenders::release(DitherImage_new(4, 4));   // none running: freed at once
        QCOMPARE(AbandonedRenders::keptCount(), 0);
    }
};

QObject* newTestHistory() { return new TestHistory; }
#include "tst_history.moc"
