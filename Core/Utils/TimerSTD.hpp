#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

class TimerStd
{
public:
    explicit TimerStd(std::function<void()> task) : task_(std::move(task)), running_(false)
    {
    }

    ~TimerStd()
    {
        stop();
    }

    void start_once(std::chrono::milliseconds delay)
    {
        start(delay, delay, false);
    }

    void start_interval(std::chrono::milliseconds interval)
    {
        start(interval, interval, true);
    }

    void stop()
    {

        {
            std::lock_guard<std::mutex> lock(mtx_);
            running_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    void start(std::chrono::milliseconds delay, std::chrono::milliseconds interval, bool repeat)
    {
        stop();

        running_ = true;
        worker_ = std::thread([=]() {
            std::unique_lock<std::mutex> lock(mtx_);
            auto wake_time = std::chrono::steady_clock::now() + delay;

            while (running_) {
                if (cv_.wait_until(lock, wake_time) == std::cv_status::timeout) {
                    if (!running_) {
                        break;
                    }
                    task_();

                    if (!repeat) {
                        running_ = false;
                        break;
                    }

                    wake_time += interval;
                }
            }
        });
    }

private:
    std::function<void()> task_;
    std::atomic<bool> running_;
    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
};