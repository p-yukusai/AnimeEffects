#ifndef THR_PARALLELER_H
#define THR_PARALLELER_H

#include "thr/Task.h"
#include "thr/TaskQueue.h"
#include "thr/Worker.h"
#include "util/NonCopyable.h"
#include <list>
#include <memory>

namespace thr {

class Paralleler: private util::NonCopyable {
public:
    Paralleler(int aWorkerCount);

    void start(QThread::Priority aPriority = QThread::InheritPriority);

    void push(Task& aTask);
    void cancel(Task& aTask);
    void wakeAll();

private:
    TaskQueue mQueue;
    std::list<std::unique_ptr<Worker>> mWorkers;
    int mWorkerCount;
};

} // namespace thr

#endif // THR_PARALLELER_H
