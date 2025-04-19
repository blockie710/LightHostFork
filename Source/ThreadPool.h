//
// ThreadPool.h
// Nova Host
//
// Created for NovaHost April 19, 2025
//

#pragma once

#include <JuceHeader.h>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

/**
 * A thread pool implementation for parallel task processing
 * Manages a set of worker threads that execute tasks from a shared queue
 */
class ThreadPool
{
public:
    /**
     * Creates a thread pool with the specified number of worker threads
     * @param numThreads Number of worker threads to create (defaults to hardware concurrency)
     */
    ThreadPool(size_t numThreads = 0) 
        : running(true)
    {
        // Use hardware concurrency if not specified or if specified as 0
        size_t actualThreads = numThreads > 0 ? numThreads : std::thread::hardware_concurrency();
        // Ensure at least one thread even on platforms where hardware_concurrency() returns 0
        actualThreads = actualThreads > 0 ? actualThreads : 2;
        
        // Start the worker threads
        for (size_t i = 0; i < actualThreads; ++i)
        {
            workers.emplace_back([this] {
                // Thread worker function
                while (true)
                {
                    std::function<void()> task;
                    
                    // Wait for and get a task from the queue
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        
                        // Wait until there's a task or we're shutting down
                        taskAvailable.wait(lock, [this] {
                            return !running || !tasks.empty();
                        });
                        
                        // Exit if we're shutting down and there are no more tasks
                        if (!running && tasks.empty())
                            return;
                            
                        // Get the next task
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    
                    // Execute the task
                    try
                    {
                        task();
                    }
                    catch (const std::exception& e)
                    {
                        // Log the exception - using JUCE logging
                        juce::Logger::writeToLog("ThreadPool exception: " + juce::String(e.what()));
                    }
                    catch (...)
                    {
                        juce::Logger::writeToLog("ThreadPool: Unknown exception in worker thread");
                    }
                }
            });
        }
    }
    
    /**
     * Destructor ensures all threads are properly joined
     */
    ~ThreadPool()
    {
        // Signal all threads to stop
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            running = false;
        }
        
        // Wake up all threads so they can check the running flag
        taskAvailable.notify_all();
        
        // Join all worker threads
        for (auto& worker : workers)
        {
            if (worker.joinable())
                worker.join();
        }
    }
    
    /**
     * Adds a new task to the thread pool
     * @param func The function to execute
     * @param args The arguments to pass to the function
     * @return A future for the function's result
     */
    template<class F, class... Args>
    auto addJob(F&& func, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;
        
        // Create a packaged task with the function and arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(func), std::forward<Args>(args)...)
        );
        
        // Get the future result before we move the task
        std::future<return_type> result = task->get_future();
        
        // Add the task to the queue
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            
            // Don't allow adding tasks after stopping the pool
            if (!running)
                throw std::runtime_error("Cannot add job to stopped ThreadPool");
                
            // Wrap the packaged task in a void function for the queue
            tasks.emplace([task]() {
                (*task)();
            });
        }
        
        // Notify one thread that a task is available
        taskAvailable.notify_one();
        
        return result;
    }
    
    /**
     * Returns the number of worker threads in the pool
     */
    size_t getThreadCount() const
    {
        return workers.size();
    }
    
    /**
     * Returns the number of worker threads in the pool
     */
    size_t getNumThreads() const
    {
        return workers.size();
    }
    
    /**
     * Wait for all current tasks to complete
     * Note: This doesn't prevent new tasks from being added while waiting
     */
    void waitForAllJobs()
    {
        // Create a special synchronization task
        std::mutex waitMutex;
        std::condition_variable waitCondition;
        std::atomic<size_t> jobsRemaining{getThreadCount()};
        
        // Add a sync task for each thread
        for (size_t i = 0; i < getThreadCount(); ++i)
        {
            addJob([&waitMutex, &waitCondition, &jobsRemaining]() {
                // Decrement the counter when the task runs
                if (--jobsRemaining == 0)
                {
                    // Last task to complete notifies the waiting thread
                    std::unique_lock<std::mutex> lock(waitMutex);
                    waitCondition.notify_one();
                }
            });
        }
        
        // Wait for all tasks to complete
        std::unique_lock<std::mutex> lock(waitMutex);
        waitCondition.wait(lock, [&jobsRemaining]() {
            return jobsRemaining == 0;
        });
    }
    
private:
    // Worker threads
    std::vector<std::thread> workers;
    
    // Task queue
    std::queue<std::function<void()>> tasks;
    
    // Synchronization
    std::mutex queueMutex;
    std::condition_variable taskAvailable;
    bool running;
    
    // Prevent copying
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
};