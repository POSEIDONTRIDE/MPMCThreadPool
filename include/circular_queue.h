#pragma once

#include <cstddef>
#include <vector>
#include <utility>
#include <stdexcept>
#include <cassert>

// 模板类定义
template <typename T>
class CircularQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using reference = T&;
    using const_reference = const T&;

    // 构造与析构
    CircularQueue() = default;
    explicit CircularQueue(size_type capacity);

    // 核心操作
    void PushBack(T&& item);
    void PushBack(const T& item);

    template<typename... Args>
    void EmplaceBack(Args&&... args);

    void PopFront();
    reference Front();
    const_reference Front() const;

    // 状态查询
    bool Empty() const noexcept;
    bool Full() const noexcept;
    size_type Size() const noexcept;
    size_type Capacity() const noexcept;

    // 随机访问
    reference At(size_type index);
    const_reference At(size_type index) const;
    reference operator[](size_type index) noexcept;
    const_reference operator[](size_type index) const noexcept;

    // 统计/清理
    void Clear() noexcept;
    size_type OverrunCounter() const noexcept;
    void ResetOverrunCounter() noexcept;

private:
    size_type capacity_ = 1;          // 实际容量+1（用于区分满和空）
    size_type head_ = 0;              // 头指针
    size_type tail_ = 0;              // 尾指针
    size_type overrun_counter_ = 0;   // 溢出计数
    std::vector<T> items_;            // 存储容器
};

// 容量策略
template <typename T>
CircularQueue<T>::CircularQueue(size_type capacity)
    : capacity_(capacity > 0 ? capacity + 1 : 1)  // +1是为了区分满和空
    , items_(capacity_) {}

// 队列为空：头尾指针相等
template <typename T>
bool CircularQueue<T>::Empty() const noexcept {
    return tail_ == head_;
}

// 队列已满：尾指针的下一个位置是头指针
template <typename T>
bool CircularQueue<T>::Full() const noexcept {
    return capacity_ > 1 && ((tail_ + 1) % capacity_) == head_;
}

// 移动语义插入
template <typename T>
void CircularQueue<T>::PushBack(T&& item) {
    if (capacity_ <= 1) return;  // 边界检查

    items_[tail_] = std::move(item);
    tail_ = (tail_ + 1) % capacity_;

    if (tail_ == head_) {
        head_ = (head_ + 1) % capacity_;
        ++overrun_counter_;
    }
}

// 拷贝插入
template <typename T>
void CircularQueue<T>::PushBack(const T& item) {
    if (capacity_ <= 1) return;

    // 如果这里抛异常，tail_/head_/overrun_counter_ 都不会被更新
    items_[tail_] = item;

    tail_ = (tail_ + 1) % capacity_;
    if (tail_ == head_) {
        head_ = (head_ + 1) % capacity_;
        ++overrun_counter_;
    }
}

// 就地构造优化
template <typename T>
template <typename... Args>
void CircularQueue<T>::EmplaceBack(Args&&... args) {
    if (capacity_ <= 1) return;

    items_[tail_] = T(std::forward<Args>(args)...);
    tail_ = (tail_ + 1) % capacity_;

    if (tail_ == head_) {
        head_ = (head_ + 1) % capacity_;
        ++overrun_counter_;
    }
}

// 队首删除操作
template <typename T>
void CircularQueue<T>::PopFront() {
    if (Empty()) {
        throw std::runtime_error("Cannot pop from empty CircularQueue");
    }
    head_ = (head_ + 1) % capacity_;
}

// 队首访问操作
template <typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::Front() const {
    if (Empty()) {
        throw std::runtime_error("CircularQueue is empty");
    }
    return items_[head_];
}

template <typename T>
typename CircularQueue<T>::reference CircularQueue<T>::Front() {
    if (Empty()) {
        throw std::runtime_error("CircularQueue is empty");
    }
    return items_[head_];
}

// 随机访问支持
template <typename T>
typename CircularQueue<T>::reference CircularQueue<T>::At(size_type index) {
    if (index >= Size()) {
        throw std::out_of_range("Index out of range");
    }
    return items_[(head_ + index) % capacity_];
}

template <typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::At(size_type index) const {
    if (index >= Size()) {
        throw std::out_of_range("Index out of range");
    }
    return items_[(head_ + index) % capacity_];
}

template <typename T>
typename CircularQueue<T>::reference CircularQueue<T>::operator[](size_type index) noexcept {
    assert(index < Size());
    return items_[(head_ + index) % capacity_];
}

template <typename T>
typename CircularQueue<T>::const_reference CircularQueue<T>::operator[](size_type index) const noexcept {
    assert(index < Size());
    return items_[(head_ + index) % capacity_];
}

// 大小计算
template <typename T>
typename CircularQueue<T>::size_type CircularQueue<T>::Size() const noexcept {
    if (tail_ >= head_) {
        return tail_ - head_;
    } else {
        return capacity_ - (head_ - tail_);
    }
}

// 容量查询（对外容量，不含“+1”的哨兵槽）
template <typename T>
typename CircularQueue<T>::size_type CircularQueue<T>::Capacity() const noexcept {
    return capacity_ > 1 ? capacity_ - 1 : 0;
}

// 资源清理（逻辑清空，不释放底层 vector 内存）
template <typename T>
void CircularQueue<T>::Clear() noexcept {
    head_ = tail_ = 0;
    overrun_counter_ = 0;
}

// 获取溢出次数
template <typename T>
typename CircularQueue<T>::size_type CircularQueue<T>::OverrunCounter() const noexcept {
    return overrun_counter_;
}

// 重置溢出计数
template <typename T>
void CircularQueue<T>::ResetOverrunCounter() noexcept {
    overrun_counter_ = 0;
}


