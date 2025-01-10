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
        : maxThreadCount(maxThreadCount), maxNbWaiting(maxNbWaiting), idleTimeout(idleTimeout) {
        threads.reserve(maxThreadCount);
    }

    ~ThreadPool() {
        monitorIn();
        if (!waiting.empty()) wait(stopCondition);
        monitorOut();

        // TODO : End smoothly
        for (PcoThread *t : threads) {
            t->requestStop();
            signal(condition);
            t->join();
            delete t;
        }

        // TODO: delete runnables
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
            //allocate new thread
            threads.push_back(new PcoThread(&ThreadPool::execute, this));
        }

        monitorOut();

        //TODO
        return isStarted;
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        return threads.size();
    }

private:

    size_t nbWaiting() {
        return threads.size() - nbWorking;
    }

    void handleTimeout(PcoThread *parentThread, std::atomic<bool> *timeout) {
        PcoThread::thisThread()->usleep(1000 * idleTimeout.count());
        if (*timeout) {
            monitorIn();
            std::cout << "timeout" << std::endl << std::flush;
            signal(condition);
            parentThread->requestStop();
            monitorOut();
        }

        delete timeout;

        // TODO
        // delete PcoThread::thisThread();
    }

    void execute() {
        while (true) {
            monitorIn();

            std::atomic<bool> *timeout = new std::atomic<bool>(true);
            PcoThread *thisThread = PcoThread::thisThread();

            new PcoThread(&ThreadPool::handleTimeout, this, thisThread, timeout);

            if (waiting.empty() && !PcoThread::thisThread()->stopRequested()) wait(condition);
            if (PcoThread::thisThread()->stopRequested()) {
                auto iterator = std::find(threads.begin(), threads.end(), PcoThread::thisThread());
                threads.erase(iterator, next(iterator));

                monitorOut();

                // TODO
                // delete PcoThread::thisThread();
                return;
            }
            *timeout = false;

            ++nbWorking;

            std::unique_ptr<Runnable> task = std::move(waiting.front());
            waiting.pop();

            // unlock monitor before running the task
            monitorOut();
          
            task->run();

            monitorIn();
            
            --nbWorking;
            if (waiting.empty()) signal(stopCondition);

            monitorOut();
        }
    }

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;
    std::vector<PcoThread *>threads{};
    size_t nbWorking = 0;
    std::queue<std::unique_ptr<Runnable>> waiting{};
    Condition condition;
    Condition stopCondition;
};

#endif // THREADPOOL_H
