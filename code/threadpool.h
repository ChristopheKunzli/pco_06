#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <stack>
#include <vector>
#include <future>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cassert>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <pcosynchro/pcohoaremonitor.h>
#include <pcosynchro/pcomutex.h>

class Runnable {
public:
    virtual ~Runnable() = default;
    virtual void run() = 0;
    virtual void cancelRun() = 0;
    virtual std::string id() = 0;
};

class ThreadPool : PcoHoareMonitor {
public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout)
        : maxThreadCount(maxThreadCount), maxNbWaiting(maxNbWaiting), idleTimeout(idleTimeout) {}

    ~ThreadPool() {
        monitorIn();
        if (!waiting.empty()) wait(stopCondition);
        monitorOut();

        for (PcoThread *t : threads) {
            t->requestStop();
            monitorIn();
            signal(condition);
            monitorOut();
            t->join();
            delete t;
        }
    }

    /*
     * Start a runnable. If a thread in the pool is avaible, assign the
     * runnable to it. If no thread is available but the pool can grow, create a new
     * pool thread and assign the runnable to it. If no thread is available and the
     * pool is at max capacity and there are less than maxNbWaiting threads waiting,
     * block the caller until a thread becomes available again, and else do not run the runnable.
     * If the runnable has been started, returns true, and else (the last case), return false.
     */
    bool start(std::unique_ptr<Runnable> runnable) {
        monitorIn();

        // TODO if (currentNbThreads == maxThreadCount)

        waiting.push(std::move(runnable));
        bool createNewThread = nbWaiting() < waiting.size() && currentNbThreads() < maxThreadCount;
        bool isStarted = createNewThread || nbWaiting() >= waiting.size();
        if (createNewThread) {
            ++nbThread;
            threads.push_back(new PcoThread(&ThreadPool::execute, this));
        }

        monitorOut();

        return isStarted;
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        return nbThread;
    }

private:

    size_t nbWaiting() {
        mutexNbWorking.lock();
        size_t amount = nbThread - nbWorking;
        mutexNbWorking.unlock();

        return amount;
    }

    void handleTimeout(std::shared_ptr<std::atomic<bool>> requestedStop, std::shared_ptr<std::atomic<bool>> timeout) {
        PcoThread::thisThread()->usleep(1000 * idleTimeout.count());
        if (*timeout) {
            std::cout << "timeout" << std::endl << std::flush;
            *requestedStop = true;
            monitorIn();
            signal(condition);
            monitorOut();
        }

        // TODO
        // delete PcoThread::thisThread();
    }

    void execute() {
        while (true) {
            monitorIn();

            auto timeout = std::make_shared<std::atomic<bool>>(true);
            auto stopRequested = std::make_shared<std::atomic<bool>>(false);

            if (!PcoThread::thisThread()->stopRequested()) {
                new PcoThread(&ThreadPool::handleTimeout, this, stopRequested, timeout);
                if (waiting.empty()) wait(condition);
            }
            if (PcoThread::thisThread()->stopRequested() || *stopRequested) {
                --nbThread;

                monitorOut();

                return;
            }
            *timeout = false;

            std::unique_ptr<Runnable> task = std::move(waiting.front());
            waiting.pop();
            if (waiting.empty()) signal(stopCondition);

            mutexNbWorking.lock();
            ++nbWorking;
            mutexNbWorking.unlock();

            monitorOut();
          
            task->run();

            mutexNbWorking.lock();
            --nbWorking;
            mutexNbWorking.unlock();
        }
    }

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;
    std::vector<PcoThread *>threads{};
    size_t nbWorking = 0;
    size_t nbThread = 0;
    std::queue<std::unique_ptr<Runnable>> waiting{};
    Condition condition;
    Condition stopCondition;
    PcoMutex mutexNbWorking{};
};

#endif // THREADPOOL_H
