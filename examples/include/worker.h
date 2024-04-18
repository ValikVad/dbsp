#pragma once

#include <deque>
#include <functional>
#include <future>
#include <type_traits>

#include "config.h"

#ifdef PREFETCH_ENABLE_MULTI_THREADED
#    include <condition_variable>
#    include <mutex>
#    include <thread>
#endif

class Worker {
public:
    Worker() = default;

public:
    void Start();

    void Stop();
    ~Worker() {
        Stop();
    }

    template <typename Func, typename... Args>
    [[nodiscard]] auto AddTask(bool to_front, Func&& func, Args&&... args) {
        using returnType = std::invoke_result_t<Func, Args...>;
#ifndef PREFETCH_ENABLE_MULTI_THREADED
        std::packaged_task<returnType()> task(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        auto f = task.get_future();
        task();
        return f;
#else
        std::packaged_task<returnType()> task(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        auto f = task.get_future();

        auto l = [task = std::move(task)]() mutable {
            task();
        };
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (to_front == true)
                _tasks.emplace_front(std::move(l));
            else
                _tasks.emplace_back(std::move(l));
            _cv.notify_one();
        }
        return f;
#endif
    }

#ifdef PREFETCH_ENABLE_MULTI_THREADED
private:
    void Do();

private:
    std::mutex _mutex;
    std::mutex _complete_mutex;
    std::atomic<bool> quite = false;
    std::deque<std::packaged_task<void()>> _tasks;
    std::condition_variable _cv;
    std::thread _thread;
#endif
};