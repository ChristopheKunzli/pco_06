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

class RunnableWrapper {
    private:
    std::unique_ptr<Runnable> runnable;
    public:
    RunnableWrapper(std::unique_ptr<Runnable> runnable) : runnable(std::move(runnable)) {}
    RunnableWrapper(RunnableWrapper& other) : runnable(std::move(other.runnable)) {}
    std::unique_ptr<Runnable> release() {
        return std::move(runnable);
    }
};

class ThreadPool : PcoHoareMonitor {
public:
    ThreadPool(int maxThreadCount, int maxNbWaiting, std::chrono::milliseconds idleTimeout)
        : maxThreadCount(maxThreadCount), maxNbWaiting(maxNbWaiting), idleTimeout(idleTimeout) {}

    ~ThreadPool() {
        monitorIn();
        if (!waiting.empty()) wait(stopCondition);
        monitorOut();

        for (auto t : threads) {
            monitorIn();
            t.thread->requestStop();
            signal(*t.condition);
            monitorOut();

            t.thread->join();
            
            delete t.thread;
            delete t.condition;
            delete t.isWaiting;
        }

        for (PcoThread *t : timeoutThreads) {
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

        if (waiting.size() == maxNbWaiting) {
            runnable->cancelRun();
            monitorOut();
            return false;
        }

        if (nbWaiting() <= waiting.size() && currentNbThreads() < maxThreadCount) {
            ++nbThread;
            Condition *threadCondition = new Condition();
            std::atomic<bool> *isWaiting = new std::atomic<bool>(false);
            threads.push_back({
                new PcoThread(&ThreadPool::execute, this, threadCondition, isWaiting, std::make_shared<RunnableWrapper>(std::move(runnable))),
                threadCondition,
                isWaiting,
            });
        } else {
            std::shared_ptr<Condition> runnableCondition = std::make_shared<Condition>();
            std::shared_ptr<std::atomic<bool>> runnableIsProcessed = std::make_shared<std::atomic<bool>>(false);

            waiting.push({
                std::move(runnable),
                runnableIsProcessed,
                runnableCondition,
            });

            for (auto t : threads) {
                if (*t.isWaiting) {
                    *t.isWaiting = false;
                    signal(*t.condition);
                    break;
                }
            }
    
            if (!*runnableIsProcessed) wait(*runnableCondition);
        }

        monitorOut();

        return true;
    }

    /* Returns the number of currently running threads. They do not need to be executing a task,
     * just to be alive.
     */
    size_t currentNbThreads() {
        return nbThread;
    }

private:

    size_t nbWaiting() {
        int amount = 0;
        for (struct Thread t : threads)
            if (*t.isWaiting) ++amount;

        return amount;
    }

    void handleTimeout(std::shared_ptr<std::atomic<bool>> canTimeout, std::shared_ptr<std::atomic<bool>> stopRequested, Condition *condition, std::atomic<bool> *isWaiting) {
        PcoThread::thisThread()->usleep(1000 * idleTimeout.count());
        if (*canTimeout) {
            monitorIn();
            *stopRequested = true;
            *isWaiting = false;
            signal(*condition);
            monitorOut();

            return;
        }
    }

    void execute(Condition *condition, std::atomic<bool> *isWaiting, std::shared_ptr<RunnableWrapper> task) {
        std::unique_ptr<Runnable> runnable = std::move(task->release());
        runnable->run();
        
        while (true) {
            monitorIn();

            auto canTimeout = std::make_shared<std::atomic<bool>>(true);
            auto stopRequested = std::make_shared<std::atomic<bool>>(false);

            if (waiting.empty() && !PcoThread::thisThread()->stopRequested()) {
                *isWaiting = true;
                timeoutThreads.push_back(new PcoThread(&ThreadPool::handleTimeout, this, canTimeout, stopRequested, condition, isWaiting));
                wait(*condition);
                *canTimeout = false;
            }

            if (PcoThread::thisThread()->stopRequested() || *stopRequested) {
                --nbThread;

                monitorOut();

                return;
            }

            runnable = std::move(waiting.front().runnable);
            *waiting.front().isProcessed = true;
            signal(*waiting.front().condition);
            waiting.pop();

            if (waiting.empty()) signal(stopCondition);

            monitorOut();
          
            runnable->run();
        }
    }

    size_t maxThreadCount;
    size_t maxNbWaiting;
    std::chrono::milliseconds idleTimeout;
    size_t nbThread = 0;
    struct Thread {
        PcoThread *thread;
        Condition *condition;
        std::atomic<bool> *isWaiting;
    };
    std::vector<struct Thread>threads{};
    std::vector<PcoThread *> timeoutThreads{};
    struct Task {
        std::unique_ptr<Runnable> runnable;
        std::shared_ptr<std::atomic<bool>> isProcessed;
        std::shared_ptr<Condition> condition;
    };
    std::queue<struct Task> waiting{};
    Condition stopCondition{};
};

#endif // THREADPOOL_H
