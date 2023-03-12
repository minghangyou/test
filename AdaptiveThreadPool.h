#ifndef ADAPTIVE_THREADPOOL_H
#define ADAPTIVE_THREADPOOL_H
#include <thread>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <queue>
#include <functional>
#include <atomic>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <future>
#include "../log/log.h"
class AdaptiveThreadPool {
public:
    AdaptiveThreadPool(int minSize, int maxSize, int idleTime)
        : minSize_(minSize), maxSize_(maxSize), idleTime_(idleTime),
        taskQueue_(), mutex_(), notEmpty_(), notFull_(),
        threads_size_(minSize),taskCount_(0), running_(false), idleCount_(0)
    {
        running_ = true;
        for (int i = 0; i < minSize_; ++i) {
            startThread();
        }
        auto timer = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(idleTime_));
                adjustPoolSize();
            }
        });

        timer.detach();
    }

    ~AdaptiveThreadPool() {
        running_ = false;
        notEmpty_.notify_all();
    }

    template<class F>
    auto submit(F&& task) 
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            taskQueue_.emplace(std::forward<F>(task));
            ++taskCount_;
        }
        notEmpty_.notify_one();
    }

private:

    inline void startThread() {
        std::thread([this]()
        {
            std::unique_lock<std::mutex> locker(mutex_);
            while(true){
                if(taskCount_>0){
                    auto task = std::move(taskQueue_.front());
                    taskQueue_.pop();
                    --taskCount_;
                    locker.unlock();
                    if (task) {
                        task();
                    }
                    else {
                        break;
                    }
                    locker.lock();
                }
                else if(!running_) break;
                else notEmpty_.wait(locker);
            }}).detach();
    }
    void threadFunc() {
        std::unique_lock<std::mutex> locker(mutex_);
        while(true){
            if(taskCount_>0){
                auto task = std::move(taskQueue_.front());
                taskQueue_.pop();
                --taskCount_;
                locker.unlock();
                if (task) {
                    task();
                }
                else {
                    break;
                }
                locker.lock();
            }
            else if(!running_) break;
            else notEmpty_.wait(locker);
        }
    }

    void adjustPoolSize() {
        std::unique_lock<std::mutex> lock(mutex_);
        double taskEfficiency = static_cast<double>(taskCount_) / static_cast<double>(threads_size_);
        if (taskEfficiency >2.0 && threads_size_ < maxSize_) {
            int newThreads = std::min(static_cast<int>(std::ceil(threads_size_ * (taskEfficiency - 1.0))), static_cast<int>(maxSize_ - threads_size_));
            for (int i = 0; i < newThreads; ++i) {
                startThread();
                ++threads_size_;
            }
        }
        else if (taskEfficiency < 0.5 && threads_size_ > minSize_) {
            int threadsToStop = std::min(static_cast<int>(std::ceil(threads_size_ * (1.0 - taskEfficiency) / 2)), threads_size_ - minSize_);// 每次将空闲进程减半
            while (threadsToStop > 0) {
                taskQueue_.emplace(nullptr);
                --threadsToStop;
                --threads_size_;
            }
        }
    }

    
    int maxSize_;
    int minSize_;
    int idleTime_;
    std::queue<std::function<void()>> taskQueue_;
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::atomic<int> threads_size_;
    std::atomic<int> taskCount_;
    std::atomic<bool> running_;
    int idleCount_;

};




#endif
