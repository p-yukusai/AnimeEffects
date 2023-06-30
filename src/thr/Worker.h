#ifndef THR_WORKER_H
#define THR_WORKER_H

#include "thr/TaskQueue.h"
#include "util/NonCopyable.h"
#include <QMutex>
#include <QReadWriteLock>
#include <QThread>
#include <QWaitCondition>

namespace thr {

class Worker: private util::NonCopyable {
public:
    Worker(TaskQueue& aQueue);
    void start(QThread::Priority aPriority = QThread::InheritPriority);

private:
    class Thread: public QThread {
    public:
        Thread(TaskQueue& aQueue);
        ~Thread();

    private:
        virtual void run();
        bool isExit();

        TaskQueue& mQueue;
        QReadWriteLock mExitLock;
        bool mExit;
    };

    Thread mThread;
};

} // namespace thr

#endif // THR_WORKER_H
