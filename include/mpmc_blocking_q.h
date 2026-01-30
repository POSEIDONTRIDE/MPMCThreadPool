#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <utility>

#include "circular_queue.h"

template <typename T>
class MpmcBlockingQueue {
    using item_type = T;

public:
    explicit MpmcBlockingQueue(std::size_t max_items)
        : circular_queue_(max_items) {}

    // 入队操作
    void Enqueue(T &&item);                 // 阻塞入队
    void EnqueueNowait(T &&item);           // 非阻塞覆盖入队
    bool EnqueueIfHaveRoom(T &&item);       // 非阻塞条件入队（失败计数）

    // 出队操作
    void Dequeue(T &popped_item);           // 阻塞出队
    bool DequeueFor(T &popped_item, std::chrono::milliseconds wait_duration); // 超时出队

    // 状态查询
    std::size_t Size();
    std::size_t OverrunCounter();
    std::size_t DiscardCounter() const;

private:
    std::mutex queue_mutex_;                    // 队列互斥锁
    std::condition_variable push_cv_;           // 入队条件变量（通知消费者：非空）
    std::condition_variable pop_cv_;            // 出队条件变量（通知生产者：非满）
    CircularQueue<T> circular_queue_;           // 底层循环队列
    std::atomic<std::size_t> discard_counter_{0}; // 丢弃计数器（原子）
};

// -------------------- 实现区 --------------------

template <typename T>
void MpmcBlockingQueue<T>::Enqueue(T &&item) {
    {
        // 获取队列锁，保证原子性
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 等待队列有空间，使用 lambda 表达式避免虚假唤醒
        pop_cv_.wait(lock, [this] {
            return !this->circular_queue_.Full();
        });

        // 队列有空间，安全地插入元素
        circular_queue_.PushBack(std::move(item));

        // lock 在作用域结束时自动释放
    }

    // 通知等待的消费者线程
    push_cv_.notify_one();
}

template <typename T>
void MpmcBlockingQueue<T>::EnqueueNowait(T &&item) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        // 直接插入，如果队列满了会自动覆盖最旧的元素
        circular_queue_.PushBack(std::move(item));
    }

    // 通知消费者有新元素可用
    push_cv_.notify_one();
}

template <typename T>
bool MpmcBlockingQueue<T>::EnqueueIfHaveRoom(T &&item) {
    bool pushed = false;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 检查是否有空间
        if (!circular_queue_.Full()) {
            circular_queue_.PushBack(std::move(item));
            pushed = true;
        }
    }

    if (pushed) {
        // 成功入队，通知消费者
        push_cv_.notify_one();
    } else {
        // 队列满，增加丢弃计数
        ++discard_counter_;
    }

    return pushed;
}

template <typename T>
void MpmcBlockingQueue<T>::Dequeue(T &popped_item) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 等待队列非空
        push_cv_.wait(lock, [this] {
            return !this->circular_queue_.Empty();
        });

        // 获取队首元素
        popped_item = std::move(circular_queue_.Front());
        circular_queue_.PopFront();
    }

    // 通知等待入队的生产者线程
    pop_cv_.notify_one();
}

template <typename T>
bool MpmcBlockingQueue<T>::DequeueFor(T &popped_item,
                                     std::chrono::milliseconds wait_duration) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // 带超时的等待
        if (!push_cv_.wait_for(lock, wait_duration, [this] {
            return !this->circular_queue_.Empty();
        })) {
            // 超时 队列仍为空
            return false;
        }

        // 在超时时间内获取到元素
        popped_item = std::move(circular_queue_.Front());
        circular_queue_.PopFront();
    }

    // 通知等待入队的线程
    pop_cv_.notify_one();
    return true;
}

template <typename T>
std::size_t MpmcBlockingQueue<T>::Size() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return circular_queue_.Size();
}

template <typename T>
std::size_t MpmcBlockingQueue<T>::OverrunCounter() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return circular_queue_.OverrunCounter();
}

template <typename T>
std::size_t MpmcBlockingQueue<T>::DiscardCounter() const {
    return discard_counter_.load(std::memory_order_relaxed);
}
