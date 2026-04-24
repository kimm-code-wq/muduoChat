#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>

class ThreadPool {
public:
    ThreadPool(std::size_t threads);
    ~ThreadPool();

    // 提交任务到线程池
    void submit(std::function<void()> task);

private:
    // 工作线程
    std::vector<std::thread> _workers;
    // 任务队列
    std::queue<std::function<void()>> _tasks;

    // 同步原语
    std::mutex _queue_mutex;
    std::condition_variable _condition;
    bool _stop;
};


// 构造函数，创建指定数量的线程
inline ThreadPool::ThreadPool(std::size_t threads) : _stop(false) {
    for (std::size_t i = 0; i < threads; ++i) {
        _workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {   
                    std::unique_lock<std::mutex> lock(this->_queue_mutex);
                    this->_condition.wait(lock, [this] { return this->_stop || !this->_tasks.empty(); });
                    
                    if (this->_stop && this->_tasks.empty()) {
                        return;
                    }

                    task = std::move(this->_tasks.front());
                    this->_tasks.pop();
                }

                task();
            }
        });
    }
}

// 析构函数，停止所有线程
inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(_queue_mutex);
        _stop = true;
    }
    
    _condition.notify_all();
    
    for (std::thread &worker : _workers) {
        worker.join();
    }
}

// 提交任务到线程池
inline void ThreadPool::submit(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(_queue_mutex);
        
        // 停止后不再接受新任务
        if (_stop) {
            throw std::runtime_error("submit on stopped ThreadPool");
        }

        _tasks.emplace(std::move(task));
    }

    _condition.notify_one();
}

#endif