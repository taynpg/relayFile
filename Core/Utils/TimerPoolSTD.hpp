#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

class TimerPoolStd
{
public:
    using TimerId = std::uint64_t;

    explicit TimerPoolStd(std::size_t worker_threads = 1) : stopped_(false)
    {
        for (std::size_t i = 0; i < worker_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~TimerPoolStd()
    {
        stop();
    }

    TimerId start_once(std::chrono::milliseconds delay, std::function<void()> task)
    {
        return schedule(delay, delay, false, std::move(task));
    }

    TimerId start_interval(std::chrono::milliseconds interval, std::function<void()> task)
    {
        return schedule(interval, interval, true, std::move(task));
    }

    void stop(TimerId id)
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = versions_.find(id);
        if (it != versions_.end()) {
            ++it->second;
        }
        cv_.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

private:
    struct TimerTask {

        TimerId id{};
        std::uint64_t version{};
        std::chrono::steady_clock::time_point expire;
        std::chrono::milliseconds interval{};
        bool repeat{false};
        std::function<void()> task;

        bool operator>(const TimerTask& rhs) const noexcept
        {
            return expire > rhs.expire;
        }
    };

    TimerId schedule(std::chrono::milliseconds delay, std::chrono::milliseconds interval, bool repeat, std::function<void()> task)
    {
        std::lock_guard<std::mutex> lock(mtx_);

        const TimerId id = next_id_++;
        const std::uint64_t version = 0;
        versions_[id] = version;

        heap_.emplace(TimerTask{id, version, std::chrono::steady_clock::now() + delay, interval, repeat, std::move(task)});
        cv_.notify_one();

        return id;
    }

    void worker_loop()
    {
        std::unique_lock<std::mutex> lock(mtx_);

        while (!stopped_) {

            if (heap_.empty()) {
                cv_.wait(lock);
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            const TimerTask& top = heap_.top();

            if (top.expire > now) {
                cv_.wait_until(lock, top.expire);
                continue;
            }

            TimerTask task = top;
            heap_.pop();

            auto it = versions_.find(task.id);
            if (it == versions_.end() || it->second != task.version) {
                continue;
            }

            if (task.repeat) {
                task.expire = now + task.interval;
                heap_.push(task);
            } else {
                versions_.erase(it);
            }

            lock.unlock();

            try {
                task.task();
            } catch (...) {
            }

            lock.lock();
        }
    }

private:
    std::atomic<bool> stopped_{false};
    std::atomic<TimerId> next_id_{0};

    std::unordered_map<TimerId, std::uint64_t> versions_;
    std::vector<std::thread> workers_;
    std::priority_queue<TimerTask, std::vector<TimerTask>, std::greater<TimerTask>> heap_;

    std::mutex mtx_;
    std::condition_variable cv_;
};