#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <stack>
#include <vector>
#include <chrono>
#include <cassert>
#include <pcosynchro/pcologger.h>
#include <pcosynchro/pcothread.h>
#include <pcosynchro/pcohoaremonitor.h>
//#include <pcosynchro/pcoconditionvariable.h>

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
        // TODO : End smoothly
        for (PcoThread *t : threads) {
            //t->requestStop();
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

        semaphore.acquire();

        waiting.push(std::move(runnable));
        if (nbWaiting() == 0 && currentNbThreads() < maxThreadCount) {
            //allocate new thread
            threads.push_back(new PcoThread(&ThreadPool::execute, this));
        }

        monitorOut();

        //TODO
        return false;
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

    void execute() {
        monitorIn();

        if (waiting.empty()) wait(condition);

        ++nbWorking;

        std::unique_ptr<Runnable> &task = waiting.front();
        waiting.pop();

        task->run();

        --nbWorking;

        if (waiting.empty()) {
            semaphore.release();
        } else {
            signal(condition);
        }

        monitorOut();
    }

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;

    std::vector<PcoThread *>threads{};
    size_t nbWorking = 0;
    std::queue<std::unique_ptr<Runnable>> waiting{};
    Condition condition;
    PcoSemaphore semaphore;
};

#endif // THREADPOOL_H
