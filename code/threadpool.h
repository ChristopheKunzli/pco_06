#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <stack>
#include <vector>
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

// Utilisé pour transmettre un unique_ptr en argument de la méthode d'un thread car std::move() ne semble pas fonctionner
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
        // Wait for all tasks to be processed
        if (!waiting.empty()) wait(stopCondition);
        monitorOut();

        for (auto t : threads) {
            monitorIn();
            // request stop
            t.thread->requestStop();
            // signal on the condition if the thread is blocked on it
            signal(*t.condition);
            monitorOut();

            // wait for the thread to end
            t.thread->join();
            
            // delete allocated ressources
            delete t.thread;
            delete t.condition;
            delete t.isWaiting;
        }

        for (PcoThread *t : timeoutThreads) {
            // wait for the timeout thread to end
            t->join();
            
            // delete the timeout thread
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

        // Check if the task can be processed
        if (waiting.size() == maxNbWaiting) {
            runnable->cancelRun();
            monitorOut();
            return false;
        }

        if (nbWaiting() <= waiting.size() && currentNbThreads() < maxThreadCount) {
            // Create a new thread if required
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

            // Otherwise push a task to the queue
            waiting.push({
                std::move(runnable),
                runnableIsProcessed,
                runnableCondition,
            });

            // Find and signal a waiting thread a task has arrived
            for (auto t : threads) {
                if (*t.isWaiting) {
                    *t.isWaiting = false;
                    signal(*t.condition);
                    break;
                }
            }
    
            // Wait for the task to be processed
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
        // Sleep for the timeout
        PcoThread::thisThread()->usleep(1000 * idleTimeout.count());
        
        // Check if the parent thread found a task to run
        if (*canTimeout) {
            monitorIn();
            // Ask for stop
            *stopRequested = true;
            *isWaiting = false;

            // Unlock parent thread blocked in wait
            signal(*condition);
            monitorOut();

            return;
        }
    }

    void execute(Condition *condition, std::atomic<bool> *isWaiting, std::shared_ptr<RunnableWrapper> task) {
        // Execute the task given as parameter
        std::unique_ptr<Runnable> runnable = std::move(task->release());
        runnable->run();
        
        // Find new tasks to run
        while (true) {
            monitorIn();

            auto canTimeout = std::make_shared<std::atomic<bool>>(true);
            auto stopRequested = std::make_shared<std::atomic<bool>>(false);

            if (waiting.empty() && !PcoThread::thisThread()->stopRequested()) {
                // If the task queue is empty wait for a new task to arrive
                *isWaiting = true;
                timeoutThreads.push_back(new PcoThread(&ThreadPool::handleTimeout, this, canTimeout, stopRequested, condition, isWaiting));
                wait(*condition);
                *canTimeout = false;
            }

            // If a stop is required either by destructor or timeout, end thread
            if (PcoThread::thisThread()->stopRequested() || *stopRequested) {
                --nbThread;

                monitorOut();

                return;
            }

            // Take a task on the queue
            runnable = std::move(waiting.front().runnable);
            
            // Signal start method the task is processed
            *waiting.front().isProcessed = true;
            signal(*waiting.front().condition);
            
            waiting.pop();

            // Signal destructor if required no task is left
            if (waiting.empty()) signal(stopCondition);

            monitorOut();
          
            // Execute task
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
