/*
使用 vector 实现循环队列
*/
#pragma once

#include <vector>
#include <stdexcept>
#include <cassert>

namespace thread_pool_improved {
template <typename T>
class CircularQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    // 默认构造函数
    CircularQueue() = default;

    // 带容量的构造函数
    explicit CircularQueue(size_type capacity) 
        : capacity_(capacity > 0 ? capacity + 1 : 1)  // +1是为了区分满和空
        , items_(capacity_) {}

    // 拷贝构造和赋值
    CircularQueue(const CircularQueue&) = default;
    CircularQueue& operator=(const CircularQueue&) = default;

    // 移动构造
    CircularQueue(CircularQueue&& other) noexcept 
        : capacity_(other.capacity_)
        , head_(other.head_)
        , tail_(other.tail_)
        , overrun_counter_(other.overrun_counter_)
        , items_(std::move(other.items_)) {
        // 重置other到有效状态
        other.capacity_ = 1;
        other.head_ = other.tail_ = 0;
        other.overrun_counter_ = 0;
    }

    // 移动赋值
    CircularQueue& operator=(CircularQueue&& other) noexcept {
        if (this != &other) {
            capacity_ = other.capacity_;
            head_ = other.head_;
            tail_ = other.tail_;
            overrun_counter_ = other.overrun_counter_;
            items_ = std::move(other.items_);
            
            other.capacity_ = 1;
            other.head_ = other.tail_ = 0;
            other.overrun_counter_ = 0;
        }
        return *this;
    }

    // 插入元素（移动版本）
    void PushBack(T&& item) {
        if (capacity_ <= 1) return;  // 容量为0时无法插入
        
        items_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;
        
        // 检查是否溢出
        if (tail_ == head_) {
            head_ = (head_ + 1) % capacity_;
            ++overrun_counter_;
        }
    }

    // 插入元素（拷贝版本）
    void PushBack(const T& item) {
        if (capacity_ <= 1) return;
        
        items_[tail_] = item;
        tail_ = (tail_ + 1) % capacity_;
        
        if (tail_ == head_) {
            head_ = (head_ + 1) % capacity_;
            ++overrun_counter_;
        }
    }

    // 就地构造元素
    template<typename... Args>
    void EmplaceBack(Args&&... args) {
        if (capacity_ <= 1) return;
        
        items_[tail_] = T(std::forward<Args>(args)...);
        tail_ = (tail_ + 1) % capacity_;
        
        if (tail_ == head_) {
            head_ = (head_ + 1) % capacity_;
            ++overrun_counter_;
        }
    }

    // 访问首元素
    const_reference Front() const {
        if (Empty()) {
            throw std::runtime_error("CircularQueue is empty");
        }
        return items_[head_];
    }

    reference Front() {
        if (Empty()) {
            throw std::runtime_error("CircularQueue is empty");
        }
        return items_[head_];
    }

    // 弹出首元素
    void PopFront() {
        if (Empty()) {
            throw std::runtime_error("Cannot pop from empty CircularQueue");
        }
        head_ = (head_ + 1) % capacity_;
    }

    // 检查是否为空
    bool Empty() const noexcept {
        return tail_ == head_;
    }

    // 检查是否已满
    bool Full() const noexcept {
        return capacity_ > 1 && ((tail_ + 1) % capacity_) == head_;
    }

    // 获取当前大小
    size_type Size() const noexcept {
        if (tail_ >= head_) {
            return tail_ - head_;
        } else {
            return capacity_ - (head_ - tail_);
        }
    }

    // 获取最大容量
    size_type Capacity() const noexcept {
        return capacity_ > 1 ? capacity_ - 1 : 0;
    }

    // 按索引访问元素
    const_reference At(size_type index) const {
        if (index >= Size()) {
            throw std::out_of_range("Index out of range");
        }
        return items_[(head_ + index) % capacity_];
    }

    reference At(size_type index) {
        if (index >= Size()) {
            throw std::out_of_range("Index out of range");
        }
        return items_[(head_ + index) % capacity_];
    }

    // 重载[]操作符（不做边界检查，为了性能）
    const_reference operator[](size_type index) const noexcept {
        assert(index < Size());
        return items_[(head_ + index) % capacity_];
    }

    reference operator[](size_type index) noexcept {
        assert(index < Size());
        return items_[(head_ + index) % capacity_];
    }

    // 清空队列
    void Clear() noexcept {
        head_ = tail_ = 0;
        overrun_counter_ = 0;
    }

    // 获取溢出计数
    size_type OverrunCounter() const noexcept {
        return overrun_counter_;
    }

    // 重置溢出计数
    void ResetOverrunCounter() noexcept {
        overrun_counter_ = 0;
    }

private:
    size_type capacity_ = 1;        // 实际容量+1
    size_type head_ = 0;            // 头指针
    size_type tail_ = 0;            // 尾指针  
    size_type overrun_counter_ = 0; // 溢出计数
    std::vector<T> items_;          // 存储容器
};
}


