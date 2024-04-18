#include "worker.h"

#ifdef PREFETCH_ENABLE_MULTI_THREADED

void Worker::Start() {
    _thread = std::thread(std::bind(&Worker::Do, this));
}

void Worker::Stop() {
    auto last_task = [this]() {
        quite = true;
    };
    std::ignore = AddTask(false, last_task);
    std::lock_guard<std::mutex> lock(_complete_mutex);  // if Stop() is called from two threads
    if (_thread.joinable()) {
        _thread.join();
    }
}

void Worker::Do() {
    while (!quite) {
        std::unique_lock<std::mutex> lock(_mutex);
        _cv.wait(lock, [this]() -> bool {
            return !_tasks.empty() || quite;
        });
        if (!_tasks.empty()) {
            auto task = std::move(_tasks.front());
            _tasks.pop_front();
            lock.unlock();

            task();
        }
    }
}

#else  // PREFETCH_ENABLE_MULTI_THREADED

void Worker::Start() {
    // nothing
}

void Worker::Stop() {
    // nothing
}

#endif
