#pragma once

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstddef>   // size_t
#include <utility>   // std::move, std::forward

#include "circular_queue.h"

namespace thread_pool_improved {

template <typename T>
class MpmcBlockingQueue {
    using item_type = T;
    
public:
    explicit MpmcBlockingQueue(size_t max_items)
        : circular_queue_(max_items) {}

    // 尝试将元素加入队列，如果队列中没有剩余空间（已满），则阻塞（当前操作 / 线程）
    void Enqueue(T &&item) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            pop_cv_.wait(lock, [this] { return !this->circular_queue_.Full(); });
            circular_queue_.PushBack(std::move(item));
        }
        push_cv_.notify_one();
    }

    // 立即将元素加入队列。若队列中无剩余空间（已满），则覆盖队列中最旧的消息
    void EnqueueNowait(T &&item) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            circular_queue_.PushBack(std::move(item));
        }
        push_cv_.notify_one();
    }

    // 仅当队列有剩余空间时才将元素加入队列，否则丢弃该元素
    bool EnqueueIfHaveRoom(T &&item) {
        bool pushed = false;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (!circular_queue_.Full()) {
                circular_queue_.PushBack(std::move(item));
                pushed = true;
            }
        }

        if (pushed) {
            push_cv_.notify_one();
        } else {
            ++discard_counter_;
        }
        return pushed;
    }

    // 带超时的出队操作
    // 返回值：如果成功出队则返回 true，否则返回 false
    bool DequeueFor(T &popped_item, std::chrono::milliseconds wait_duration) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (!push_cv_.wait_for(lock, wait_duration, [this] { return !this->circular_queue_.Empty(); })) {
                return false;
            }
            popped_item = std::move(circular_queue_.Front());
            circular_queue_.PopFront();
        }
        pop_cv_.notify_one();
        return true;
    }

    // 阻塞式出队操作，无超时限制
    void Dequeue(T &popped_item) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            push_cv_.wait(lock, [this] { return !this->circular_queue_.Empty(); });
            popped_item = std::move(circular_queue_.Front());
            circular_queue_.PopFront();
        }
        pop_cv_.notify_one();
    }

    // 获取溢出计数器值
    size_t OverrunCounter() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return circular_queue_.OverrunCounter();
    }

    // 获取丢弃计数器值
    size_t DiscardCounter() const { 
        return discard_counter_.load(std::memory_order_relaxed); 
    }

    // 获取当前队列大小
    size_t Size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return circular_queue_.Size();
    }

    // 重置溢出计数器
    void ResetOverrunCounter() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        circular_queue_.ResetOverrunCounter();
    }

    // 重置丢弃计数器
    void ResetDiscardCounter() { 
        discard_counter_.store(0, std::memory_order_relaxed); 
    }
    
private:
    mutable std::mutex queue_mutex_;            // 队列互斥锁
    std::condition_variable push_cv_;           // 入队条件变量
    std::condition_variable pop_cv_;            // 出队条件变量
    mutable CircularQueue<T> circular_queue_;   // 底层循环队列
    std::atomic<size_t> discard_counter_{0};    // 丢弃计数器
};

} // namespace thread_pool_improved