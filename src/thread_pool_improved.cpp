#include "thread_pool_improved.h"
#include <stdexcept>
#include <future>
#include "mpmc_blocking_q.h"
#include "thread_pool_config.h"
#include "logger.h"

namespace thread_pool_improved {    

// ============================ 构造：简易模式 ============================
// 这个构造函数用于“固定线程数线程池”：动态扩缩容默认关闭
ThreadPool::ThreadPool(size_t thread_count, size_t max_queue_size, QueueFullPolicy queue_policy)
    : max_queue_size_(max_queue_size), task_queue_(max_queue_size), queue_policy_(queue_policy) {
    LOG_INFO("创建线程池 - 线程数: {}, 最大队列大小: {}, 队列策略: {}", 
             thread_count, max_queue_size, static_cast<int>(queue_policy));
    
    // 验证参数：线程数必须 > 0
    if (thread_count == 0) {
        LOG_ERROR("线程数不能为0");
        throw std::invalid_argument("Thread count must be greater than 0");
    }
    
    // 初始化默认配置（兼容模式，关闭动态线程管理）
    config_.core_threads = thread_count;
    config_.max_threads = thread_count;
    config_.max_queue_size = max_queue_size;
    config_.queue_full_policy = queue_policy;
    config_.enable_dynamic_threads = false; // 简单构造函数默认关闭动态管理
    config_.thread_idle_timeout = std::chrono::milliseconds(30000);
    config_.load_check_interval = std::chrono::milliseconds(5000);
    config_.scale_up_threshold = 0.8;
    config_.scale_down_threshold = 0.3;
    config_.min_idle_time_for_removal = std::chrono::milliseconds(10000);
    config_.max_consecutive_idle_checks = 3;
    
    // 初始化动态线程管理相关数据结构（即使不启用，也要初始化以避免段错误）
    thread_last_active_.resize(thread_count);
    thread_idle_count_.reserve(thread_count);
    thread_should_exit_.reserve(thread_count);
    
    for (size_t i = 0; i < thread_count; ++i) {
        thread_idle_count_.emplace_back(std::make_unique<std::atomic<size_t>>(0));
        thread_should_exit_.emplace_back(std::make_unique<std::atomic<bool>>(false));
    }
    
    // 创建工作线程：每个线程进入 WorkerLoop()
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this);
        thread_last_active_[i] = std::chrono::steady_clock::now();
        LOG_DEBUG("创建工作线程 {}/{}", i + 1, thread_count);
    }
    
    current_threads_ = thread_count;
    stats_.peak_threads = thread_count;
    stats_.threads_created = thread_count;
    
    LOG_INFO("线程池创建完成，共 {} 个工作线程", thread_count);
}

// ============================ 构造：配置模式 ============================
// 支持动态扩缩容：core_threads 先启动，后续按负载创建/回收线程
ThreadPool::ThreadPool(const ThreadPoolStruct& config)
    : max_queue_size_(config.max_queue_size), 
      task_queue_(config.max_queue_size), 
      queue_policy_(config.queue_full_policy),
      config_(config) {
    LOG_INFO("使用配置创建线程池 - 核心线程: {}, 最大线程: {}, 队列大小: {}, 保活时间: {}ms", 
             config.core_threads, config.max_threads, config.max_queue_size, config.keep_alive_time.count());
    
    // 验证参数：核心线程必须 > 0
    if (config.core_threads == 0) {
        LOG_ERROR("核心线程数不能为0");
        throw std::invalid_argument("Core thread count must be greater than 0");
    }
    
    // 初始化动态线程管理相关数据结构（按 max_threads 规模准备）
    thread_last_active_.resize(config.max_threads);
    thread_idle_count_.reserve(config.max_threads);
    thread_should_exit_.reserve(config.max_threads);
    
    // 对于atomic类型，使用unique_ptr包装
    for (size_t i = 0; i < config.max_threads; ++i) {
        thread_idle_count_.emplace_back(std::make_unique<std::atomic<size_t>>(0));
        thread_should_exit_.emplace_back(std::make_unique<std::atomic<bool>>(false));
    }
    
    // 创建核心工作线程
    for (size_t i = 0; i < config.core_threads; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this);
        thread_last_active_[i] = std::chrono::steady_clock::now();
        LOG_DEBUG("创建核心工作线程 {}/{}", i + 1, config.core_threads);
    }
    
    current_threads_ = config.core_threads;
    stats_.peak_threads = config.core_threads;
    stats_.threads_created = config.core_threads;
    
    // 启动负载均衡线程（如果启用动态线程管理）
    if (config.enable_dynamic_threads) {
        load_balancer_thread_ = std::thread(&ThreadPool::LoadBalancingLoop, this);
        LOG_INFO("启动负载均衡线程，动态线程管理已启用");
    }
    
    LOG_INFO("线程池创建完成，核心线程数: {}", config.core_threads);
}

ThreadPool::~ThreadPool() {
    LOG_DEBUG("销毁线程池");
    Stop();
}

std::unique_ptr<ThreadPool> ThreadPool::CreateFromConfig(const std::string& config_path) {
    LOG_INFO("从配置文件创建线程池: {}", config_path);
    ThreadPoolStruct config = ThreadPoolConfig::LoadFromFile(config_path);
    auto pool = std::make_unique<ThreadPool>(config);
    LOG_INFO("成功从配置文件创建线程池");
    return pool;
}

// ============================ 停止线程池（优雅停止） ============================
// Stop() 语义：不再接受新任务，唤醒所有线程，等待线程退出并 join
void ThreadPool::Stop() {
    // 防止重复停止
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) {
        LOG_DEBUG("线程池已经停止或正在停止中");
        return;
    }
    
    LOG_INFO("开始停止线程池，当前活跃线程: {}, 待处理任务: {}", 
             active_threads_.load(), pending_tasks_.load());
    
    // 设置状态为优雅关闭，让正在执行的任务完成
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        ThreadPoolState current_state = state_.load();
        if (current_state == ThreadPoolState::PAUSED) {
            // 如果处于暂停状态，先恢复执行
            SetState(ThreadPoolState::RUNNING);
            pause_condition_.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 给暂停的线程一点时间恢复
        }
        SetState(ThreadPoolState::SHUTTING_DOWN);
    }
    
    // 停止负载均衡线程
    if (config_.enable_dynamic_threads) {
        load_balancer_stop_ = true;
    }
    
    // 唤醒所有等待的线程，让它们处理完剩余任务
    queue_condition_.notify_all();
    wait_condition_.notify_all();
    pause_condition_.notify_all();
    
    // 等待负载均衡线程结束
    if (config_.enable_dynamic_threads && load_balancer_thread_.joinable()) {
        load_balancer_thread_.join();
        LOG_DEBUG("负载均衡线程已停止");
    }
    
    // 等待所有工作线程结束
    size_t joined_count = 0;
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
            joined_count++;
        }
    }
    
    // 设置最终状态为已停止
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        SetState(ThreadPoolState::STOPPED);
    }
    
    LOG_INFO("线程池已停止，共等待 {} 个线程结束", joined_count);
}

// ============================ 提交任务 ============================
// Submit() 语义：仅在 RUNNING 状态可提交；队列满由 queue_policy_ 决定行为
bool ThreadPool::Submit(std::unique_ptr<TaskBase> task) {
    if (!CanAcceptNewTasks()) {
        ThreadPoolState current_state = state_.load();
        std::string state_str;
        switch (current_state) {
            case ThreadPoolState::PAUSED:
                state_str = "暂停";
                break;
            case ThreadPoolState::SHUTTING_DOWN:
                state_str = "关闭中";
                break;
            case ThreadPoolState::FORCE_STOPPING:
                state_str = "强制停止中";
                break;
            case ThreadPoolState::STOPPED:
                state_str = "已停止";
                break;
            default:
                state_str = "未知";
        }
        LOG_ERROR("尝试向状态为 {} 的线程池提交任务", state_str);
        throw std::runtime_error("Cannot submit task to thread pool in current state: " + state_str);
    }
    
    bool submitted = false;
    LOG_PERF_START(task_submit);
    
    // 根据配置的策略选择不同的入队方式
    switch (queue_policy_) {
        case QueueFullPolicy::BLOCK:
            LOG_TRACE("使用阻塞策略提交任务");
            task_queue_.Enqueue(std::move(task));
            submitted = true;
            break;
        case QueueFullPolicy::OVERWRITE:
            LOG_TRACE("使用覆盖策略提交任务");
            task_queue_.EnqueueNowait(std::move(task));
            submitted = true;
            break;
        case QueueFullPolicy::DISCARD:
            LOG_TRACE("使用丢弃策略提交任务");
            submitted = task_queue_.EnqueueIfHaveRoom(std::move(task));
            if (!submitted) {
                LOG_WARN("任务被丢弃，队列已满");
            }
            break;
    }
    
    if (submitted) {
        pending_tasks_++;
        queue_condition_.notify_one();
        LOG_DEBUG("任务提交成功，待处理任务数: {}", pending_tasks_.load());
        
        // 动态扩容：任务积压 + 负载因子均满足时尝试创建新线程
        if (config_.enable_dynamic_threads && 
            pending_tasks_.load() >= config_.thread_creation_threshold &&
            current_threads_.load() < config_.max_threads) {
            
            double load_factor = CalculateLoadFactor();
            if (load_factor > config_.scale_up_threshold) {
                std::lock_guard<std::mutex> lock(thread_management_mutex_);
                TryCreateNewThread();
            }
        }
    }
    
    LOG_PERF_END(task_submit);
    return submitted;
}

void ThreadPool::WaitAll() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    wait_condition_.wait(lock, [this] {
        return pending_tasks_ == 0;
    });
}

size_t ThreadPool::QueueSize() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.Size();
}

size_t ThreadPool::ActiveThreads() const {
    // 返回当前存活的线程数，而不是正在执行任务的线程数
    // 这样能更好地反映线程池的工作状态
    return active_threads_.load();
}

// ============================ 工作线程主循环 ============================
// WorkerLoop() 语义：
// - 等待队列任务或停止信号
// - 取到任务后释放锁执行 Execute()
// - 根据线程池状态处理暂停/关闭/强制停止等行为
void ThreadPool::WorkerLoop() {
    LOG_FUNC_ENTER();
    std::thread::id thread_id = std::this_thread::get_id();
    
    // 查找当前线程的索引（用于动态线程管理相关数组）
    size_t thread_index = SIZE_MAX;
    {
        std::lock_guard<std::mutex> lock(thread_management_mutex_);
        for (size_t i = 0; i < workers_.size(); ++i) {
            if (workers_[i].get_id() == thread_id) {
                thread_index = i;
                break;
            }
        }
    }
    
    LOG_DEBUG("工作线程启动，线程ID: {}, 索引: {}", reinterpret_cast<uint64_t>(&thread_id), thread_index);
    
    while (true) {
        std::unique_ptr<TaskBase> task;
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 检查线程退出标志（动态缩容会设置该标志）
            if (thread_index != SIZE_MAX && thread_should_exit_[thread_index]->load()) {
                LOG_DEBUG("工作线程 {} 收到退出信号", thread_index);
                return;
            }
            
            // 检查是否处于暂停状态：暂停期间不处理队列任务
            ThreadPoolState current_pause_state = state_.load();
            if (current_pause_state == ThreadPoolState::PAUSED) {
                LOG_TRACE("线程池处于暂停状态，线程等待恢复...");
                
                // 释放队列锁，避免死锁
                lock.unlock();
                
                // 等待恢复或停止信号，使用更短的检查间隔
                while (state_.load() == ThreadPoolState::PAUSED && !stop_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                
                ThreadPoolState resumed_state = state_.load();
                LOG_TRACE("线程池已恢复或收到停止信号，当前状态: {}", static_cast<int>(resumed_state));
                
                // 如果是因为停止信号而唤醒，直接返回
                if (stop_.load()) {
                    LOG_DEBUG("工作线程因停止信号从暂停状态唤醒");
                    return;
                }
                
                // 重新获取锁
                lock.lock();
                
                // 检查从暂停状态转换后的状态
                if (resumed_state == ThreadPoolState::SHUTTING_DOWN) {
                    LOG_TRACE("从暂停状态直接进入关闭状态，不再处理队列中的任务");
                    // 从暂停直接进入关闭状态，不应该继续处理队列任务
                    // 只等待正在执行的任务完成，这是暂停语义的体现
                } else if (resumed_state == ThreadPoolState::RUNNING) {
                    LOG_TRACE("从暂停状态恢复到运行状态，继续处理任务");
                    // 只有真正恢复到运行状态才继续处理队列任务
                    if (task_queue_.Size() > 0) {
                        LOG_TRACE("恢复后发现队列有任务，直接处理");
                        continue; // 跳过等待，直接进入下一轮循环获取任务
                    }
                }
            }
            
            // 等待任务或停止信号
            LOG_TRACE("工作线程等待任务...");
            
            if (config_.enable_dynamic_threads && !stop_) {
                // 动态线程管理模式：使用超时等待（便于空闲线程退出判断）
                queue_condition_.wait_for(lock, config_.thread_idle_timeout, [this] {
                    return stop_ || task_queue_.Size() > 0;
                });
            } else {
                // 静态模式或停止模式：无限等待直到有任务或 stop
                queue_condition_.wait(lock, [this] {
                    return stop_ || task_queue_.Size() > 0;
                });
            }
            
            // 检查关闭状态
            ThreadPoolState current_state = state_.load();
            if (stop_ || current_state == ThreadPoolState::FORCE_STOPPING) {
                if (task_queue_.Size() == 0 || current_state == ThreadPoolState::FORCE_STOPPING) {
                    LOG_DEBUG("工作线程收到停止信号，线程ID: {}", reinterpret_cast<uint64_t>(&thread_id));
                    return; // 停止线程
                } else {
                    LOG_TRACE("工作线程收到停止信号，但队列还有任务，继续处理");
                }
            }
            
            // 在关闭过程中，如果有任务仍要处理（优雅关闭模式）
            if (current_state == ThreadPoolState::SHUTTING_DOWN && task_queue_.Size() == 0) {
                LOG_DEBUG("优雅关闭中，队列为空，工作线程退出");
                return;
            }
            
            // 检查是否有任务
            if (task_queue_.Size() > 0) {
                task_queue_.Dequeue(task);
                has_task = true;
                active_threads_++;
                LOG_TRACE("工作线程获取任务，当前活跃线程: {}", active_threads_.load());
                
                // 更新线程活跃度
                if (thread_index != SIZE_MAX) {
                    UpdateThreadActivity(thread_index);
                }
            } else {
                // 没有任务：动态模式下累计空闲次数，满足条件时非核心线程退出
                if (thread_index != SIZE_MAX && config_.enable_dynamic_threads) {
                    thread_idle_count_[thread_index]->fetch_add(1);
                    LOG_TRACE("线程 {} 空闲等待，空闲次数: {}", thread_index, thread_idle_count_[thread_index]->load());
                    
                    // 非核心线程检查是否应该退出
                    if (thread_index >= config_.core_threads) {
                        auto now = std::chrono::steady_clock::now();
                        auto idle_duration = now - thread_last_active_[thread_index];
                        
                        if (idle_duration >= config_.thread_idle_timeout &&
                            thread_idle_count_[thread_index]->load() >= config_.max_consecutive_idle_checks) {
                            LOG_INFO("非核心线程 {} 空闲超时，准备退出", thread_index);
                            thread_should_exit_[thread_index]->store(true);
                            return;
                        }
                    }
                }
            }
        }
        
        // 执行任务：此时不持有队列锁，避免阻塞其他线程取任务
        if (has_task) {
            LOG_PERF_START(task_execution);
            running_tasks_++; // 增加正在执行的任务计数
            
            // 在强制停止状态下，跳过任务执行
            ThreadPoolState current_exec_state = state_.load();
            if (current_exec_state == ThreadPoolState::FORCE_STOPPING) {
                LOG_DEBUG("强制停止中，跳过任务执行");
                running_tasks_--;
                pending_tasks_--;
                wait_condition_.notify_all();
                return;
            }
            
            try {
                auto start_time = std::chrono::steady_clock::now();
                task->Execute();
                auto end_time = std::chrono::steady_clock::now();
                
                // 更新统计信息
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                    double task_time_ms = duration.count() / 1000.0;
                    
                    // 检查任务执行是否成功
                    if (task->IsExecutionSuccessful()) {
                        stats_.tasks_completed++;
                        stats_.avg_task_time_ms = (stats_.avg_task_time_ms * (stats_.tasks_completed - 1) + task_time_ms) / stats_.tasks_completed;
                        LOG_TRACE("任务执行成功");
                    } else {
                        stats_.tasks_failed++;
                        LOG_TRACE("任务执行失败（内部异常）");
                    }
                }
            } catch (const std::exception& e) {
                LOG_ERROR("任务执行异常: {}", e.what());
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.tasks_failed++;
                }
            } catch (...) {
                LOG_ERROR("任务执行发生未知异常");
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.tasks_failed++;
                }
            }
            LOG_PERF_END(task_execution);
            
            // 任务执行完成后更新计数
            active_threads_--;
            pending_tasks_--;
            running_tasks_--; // 减少正在执行的任务计数
            LOG_TRACE("任务完成，剩余待处理任务: {}, 活跃线程: {}, 正在执行任务: {}", 
                     pending_tasks_.load(), active_threads_.load(), running_tasks_.load());
            
            // 通知等待的线程
            wait_condition_.notify_all();
        }
    }
    
    LOG_FUNC_EXIT();
}

// 获取线程池统计信息
ThreadPool::Stats ThreadPool::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats stats = stats_;
    stats.active_threads = active_threads_.load();
    stats.load_factor = CalculateLoadFactor();
    
    // 更新队列使用率相关统计
    stats.current_queue_size = task_queue_.Size();
    stats.max_queue_size = max_queue_size_;
    
    // 计算当前队列使用率
    if (max_queue_size_ > 0) {
        stats.queue_usage_rate = static_cast<double>(stats.current_queue_size) / max_queue_size_;
    } else {
        stats.queue_usage_rate = 0.0; // 无限制队列
    }
    
    // 更新峰值队列大小
    if (stats.current_queue_size > stats_.peak_queue_size) {
        const_cast<ThreadPool*>(this)->stats_.peak_queue_size = stats.current_queue_size;
        if (max_queue_size_ > 0) {
            const_cast<ThreadPool*>(this)->stats_.peak_queue_usage_rate = 
                static_cast<double>(stats.current_queue_size) / max_queue_size_;
        }
    }
    stats.peak_queue_size = stats_.peak_queue_size;
    stats.peak_queue_usage_rate = stats_.peak_queue_usage_rate;
    
    // 获取任务丢弃和覆盖统计
    stats.tasks_discarded = task_queue_.DiscardCounter();
    stats.tasks_overwritten = task_queue_.OverrunCounter();
    
    return stats;
}

// 手动触发负载检查
void ThreadPool::TriggerLoadCheck() {
    if (!config_.enable_dynamic_threads) {
        LOG_WARN("动态线程管理未启用，无法触发负载检查");
        return;
    }
    
    std::lock_guard<std::mutex> lock(thread_management_mutex_);
    
    double load_factor = CalculateLoadFactor();
    size_t current_count = current_threads_.load();
    
    LOG_DEBUG("手动负载检查 - 当前线程数: {}, 负载因子: {:.2f}, 活跃线程: {}, 待处理任务: {}", 
              current_count, load_factor, active_threads_.load(), pending_tasks_.load());
    
    // 检查是否需要扩容
    size_t pending = pending_tasks_.load();
    
    // 多种扩容策略：
    // 1. 传统的负载因子策略
    // 2. 任务积压策略 
    // 3. 任务密度策略
    bool should_expand = false;
    
    if (current_count < config_.max_threads) {
        // 策略1: 负载因子过高
        if (load_factor > config_.scale_up_threshold && pending >= config_.thread_creation_threshold) {
            should_expand = true;
            LOG_DEBUG("负载因子过高触发扩容: {:.2f} > {:.2f}", load_factor, config_.scale_up_threshold);
        }
        // 策略2: 任务积压过多
        else if (pending >= config_.thread_creation_threshold * 2) {
            should_expand = true;
            LOG_DEBUG("任务积压过多触发扩容: {} >= {}", pending, config_.thread_creation_threshold * 2);
        }
        // 策略3: 任务密度过高(每个线程平均超过2个待处理任务)
        else if (current_count > 0 && static_cast<double>(pending) / current_count > 2.0) {
            should_expand = true;
            LOG_DEBUG("任务密度过高触发扩容: {:.1f} 任务/线程", static_cast<double>(pending) / current_count);
        }
        
        if (should_expand) {
            TryCreateNewThread();
        }
    }
    // 检查是否需要缩容
    else if (load_factor < config_.scale_down_threshold && 
             current_count > config_.core_threads) {
        TryRemoveIdleThread();
    }
}

// 负载均衡主循环
void ThreadPool::LoadBalancingLoop() {
    LOG_FUNC_ENTER();
    LOG_INFO("负载均衡线程启动，检查间隔: {}ms", config_.load_check_interval.count());
    
    while (!load_balancer_stop_.load()) {
        try {
            std::this_thread::sleep_for(config_.load_check_interval);
            
            if (load_balancer_stop_.load()) {
                break;
            }
            
            TriggerLoadCheck();
            CleanupFinishedThreads();
            
        } catch (const std::exception& e) {
            LOG_ERROR("负载均衡线程异常: {}", e.what());
        } catch (...) {
            LOG_ERROR("负载均衡线程发生未知异常");
        }
    }
    
    LOG_INFO("负载均衡线程退出");
    LOG_FUNC_EXIT();
}

// 尝试创建新线程
bool ThreadPool::TryCreateNewThread() {
    size_t current_count = current_threads_.load();
    
    if (current_count >= config_.max_threads) {
        LOG_DEBUG("已达到最大线程数限制: {}", config_.max_threads);
        return false;
    }
    
    try {
        // 查找可用的线程索引
        size_t thread_index = current_count;
        
        // 创建新线程
        if (thread_index < workers_.size()) {
            // 如果workers_容器中有位置，直接替换
            if (workers_[thread_index].joinable()) {
                workers_[thread_index].join();
            }
            workers_[thread_index] = std::thread(&ThreadPool::WorkerLoop, this);
        } else {
            // 否则添加新线程
            workers_.emplace_back(&ThreadPool::WorkerLoop, this);
        }
        
        thread_last_active_[thread_index] = std::chrono::steady_clock::now();
        thread_idle_count_[thread_index]->store(0);
        thread_should_exit_[thread_index]->store(false);
        
        current_threads_++;
        
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.threads_created++;
            if (current_threads_.load() > stats_.peak_threads) {
                stats_.peak_threads = current_threads_.load();
            }
        }
        
        LOG_INFO("创建新线程成功，当前线程数: {}/{}", current_threads_.load(), config_.max_threads);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("创建新线程失败: {}", e.what());
        return false;
    }
}

// 尝试移除空闲线程
bool ThreadPool::TryRemoveIdleThread() {
    size_t current_count = current_threads_.load();
    
    if (current_count <= config_.core_threads) {
        LOG_TRACE("已是核心线程数，不能继续移除");
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    
    // 查找可以移除的空闲线程
    for (size_t i = config_.core_threads; i < current_count; ++i) {
        auto idle_duration = now - thread_last_active_[i];
        
        if (idle_duration >= config_.min_idle_time_for_removal &&
            thread_idle_count_[i]->load() >= config_.max_consecutive_idle_checks) {
            
            // 标记线程退出
            thread_should_exit_[i]->store(true);
            queue_condition_.notify_all();
            
            LOG_INFO("标记线程 {} 退出，空闲时间: {}ms, 空闲检查次数: {}", 
                     i, 
                     std::chrono::duration_cast<std::chrono::milliseconds>(idle_duration).count(),
                     thread_idle_count_[i]->load());
            
            return true;
        }
    }
    
    return false;
}

// 更新线程活跃度
void ThreadPool::UpdateThreadActivity(size_t thread_index) {
    if (thread_index < thread_last_active_.size()) {
        thread_last_active_[thread_index] = std::chrono::steady_clock::now();
        thread_idle_count_[thread_index]->store(0);
    }
}

// 计算负载因子
double ThreadPool::CalculateLoadFactor() const {
    size_t current_count = current_threads_.load();
    if (current_count == 0) return 0.0;
    
    size_t active_count = active_threads_.load();
    return static_cast<double>(active_count) / static_cast<double>(current_count);
}

// 清理已结束的线程
void ThreadPool::CleanupFinishedThreads() {
    std::lock_guard<std::mutex> lock(thread_management_mutex_);
    
    for (size_t i = 0; i < workers_.size(); ++i) {
        if (thread_should_exit_[i]->load() && workers_[i].joinable()) {
            // 检查线程是否已经结束
            if (workers_[i].get_id() == std::thread::id{}) {
                continue; // 线程已经结束
            }
            
            // 等待线程结束（非阻塞检查）
            try {
                workers_[i].join();
                current_threads_--;
                
                {
                    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                    stats_.threads_destroyed++;
                }
                
                LOG_INFO("成功回收线程 {}, 当前线程数: {}", i, current_threads_.load());
            } catch (const std::exception& e) {
                LOG_ERROR("回收线程 {} 时发生异常: {}", i, e.what());
            }
        }
    }
}

// 暂停线程池
void ThreadPool::Pause() {
    LOG_INFO("暂停线程池执行");
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    ThreadPoolState current_state = state_.load();
    if (current_state == ThreadPoolState::RUNNING) {
        SetState(ThreadPoolState::PAUSED);
        LOG_INFO("线程池已暂停");
    } else {
        LOG_WARN("线程池当前状态为 {}，无法暂停", static_cast<int>(current_state));
    }
}

// 恢复线程池执行
void ThreadPool::Resume() {
    LOG_INFO("恢复线程池执行");
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    ThreadPoolState current_state = state_.load();
    if (current_state == ThreadPoolState::PAUSED) {
        SetState(ThreadPoolState::RUNNING);
        pause_condition_.notify_all(); // 唤醒所有等待的线程
        LOG_INFO("线程池已恢复");
    } else {
        LOG_WARN("线程池当前状态为 {}，无法恢复", static_cast<int>(current_state));
    }
}

// 优雅关闭线程池
void ThreadPool::Shutdown(ShutdownOption option, std::chrono::milliseconds timeout) {
    LOG_INFO("开始关闭线程池，选项: {}, 超时: {}ms", 
             static_cast<int>(option), timeout.count());
    
    bool should_proceed = false;
    ThreadPoolState original_state;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        original_state = state_.load();
        
        if (original_state == ThreadPoolState::STOPPED || 
            original_state == ThreadPoolState::SHUTTING_DOWN ||
            original_state == ThreadPoolState::FORCE_STOPPING) {
            LOG_INFO("线程池已经在关闭或已关闭");
            return;
        }
        
        should_proceed = true;
        
        switch (option) {
            case ShutdownOption::GRACEFUL:
                SetState(ThreadPoolState::SHUTTING_DOWN);
                break;
            case ShutdownOption::FORCE:
                SetState(ThreadPoolState::FORCE_STOPPING);
                break;
            case ShutdownOption::TIMEOUT:
                SetState(ThreadPoolState::SHUTTING_DOWN);
                break;
        }
        
        // 如果处于暂停状态，根据关闭选项决定行为
        if (original_state == ThreadPoolState::PAUSED) {
            LOG_INFO("从暂停状态开始关闭，选项: {}，当前待处理任务: {}", 
                     static_cast<int>(option), pending_tasks_.load());
                     
            if (option == ShutdownOption::FORCE) {
                // 强制关闭：直接跳过暂停状态，立即停止
                SetState(ThreadPoolState::FORCE_STOPPING);
                LOG_INFO("强制关闭模式：跳过暂停恢复");
            } else {
                // 优雅关闭或超时关闭：只等待正在执行的任务完成
                // 不恢复队列任务的处理，这是暂停语义的正确体现
                SetState(ThreadPoolState::SHUTTING_DOWN);
                LOG_INFO("优雅关闭模式：等待正在执行的任务完成，队列任务将被跳过");
            }
            
            // 通知所有等待的线程
            pause_condition_.notify_all();
            queue_condition_.notify_all();
        }
    }
    
    if (!should_proceed) {
        return;
    }
    
    if (option == ShutdownOption::FORCE) {
        // 强制停止：立即设置停止标志，不等待正在执行的任务
        ForceStop();
        return;
    }
    
    if (option == ShutdownOption::TIMEOUT) {
        // 超时关闭：等待指定时间后强制停止        
        if (!WaitForRunningTasks(timeout)) {
            LOG_WARN("等待任务完成超时，转为强制停止");
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                SetState(ThreadPoolState::FORCE_STOPPING);
            }
            ForceStop();
            return;
        }
    } else if (option == ShutdownOption::GRACEFUL) {
        // 优雅关闭：等待所有任务完成
        // 使用一个合理的最大等待时间，避免无限等待
        auto max_wait_time = std::chrono::minutes(10); // 最多等待10分钟
        if (!WaitForRunningTasks(max_wait_time)) {
            LOG_WARN("优雅关闭等待超时，转为强制停止");
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                SetState(ThreadPoolState::FORCE_STOPPING);
            }
            ForceStop();
            return;
        }
    }
    
    // 设置最终停止状态
    Stop();
    
    LOG_INFO("线程池关闭完成");
}

// 等待正在执行的任务完成
bool ThreadPool::WaitForRunningTasks(std::chrono::milliseconds timeout) {
    LOG_INFO("等待正在执行的任务完成，超时时间: {}ms", timeout.count());
    
    auto start_time = std::chrono::steady_clock::now();
    
    while (true) {
        size_t running = running_tasks_.load();
        size_t pending = pending_tasks_.load();
        
        if (running == 0 && pending == 0) {
            LOG_INFO("所有任务已完成");
            return true;
        }
        
        // 检查超时
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            LOG_WARN("等待任务完成超时，当前还有 {} 个正在执行的任务，{} 个待处理任务", 
                     running, pending);
            return false;
        }
        
        // 使用短暂睡眠等待，避免复杂的条件变量操作
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // 检查是否所有任务都完成了
        if (running_tasks_.load() == 0 && pending_tasks_.load() == 0) {
            LOG_INFO("所有任务已完成");
            return true;
        }
    }
}

// 检查是否可以接受新任务
bool ThreadPool::CanAcceptNewTasks() const {
    ThreadPoolState current_state = state_.load();
    return current_state == ThreadPoolState::RUNNING;
}

// 设置线程池状态
void ThreadPool::SetState(ThreadPoolState new_state) {
    ThreadPoolState old_state = state_.load();
    state_.store(new_state);
    
    LOG_DEBUG("线程池状态从 {} 转换为 {}", 
              static_cast<int>(old_state), static_cast<int>(new_state));
}

// 等待状态改变
bool ThreadPool::WaitForStateChange(ThreadPoolState from_state, std::chrono::milliseconds timeout) {
    auto start_time = std::chrono::steady_clock::now();
    
    while (state_.load() == from_state) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        if (elapsed >= timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return true;
}

// 强制停止（不等待任务完成）
void ThreadPool::ForceStop() {
    // 防止重复停止
    bool expected = false;
    if (!stop_.compare_exchange_strong(expected, true)) {
        LOG_DEBUG("线程池已经停止或正在停止中");
        return;
    }
    
    LOG_INFO("开始强制停止线程池，当前活跃线程: {}, 待处理任务: {}", 
             active_threads_.load(), pending_tasks_.load());
    
    // 设置状态为强制停止中
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        SetState(ThreadPoolState::FORCE_STOPPING);
    }
    
    // 停止负载均衡线程
    if (config_.enable_dynamic_threads) {
        load_balancer_stop_ = true;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // 设置所有线程退出标志
        for (auto& should_exit : thread_should_exit_) {
            should_exit->store(true);
        }
    }
    queue_condition_.notify_all();
    wait_condition_.notify_all();
    pause_condition_.notify_all();  // 唤醒暂停的线程
    
    // 等待负载均衡线程结束
    if (config_.enable_dynamic_threads && load_balancer_thread_.joinable()) {
        load_balancer_thread_.join();
        LOG_DEBUG("负载均衡线程已停止");
    }
    
    // 等待所有工作线程结束，使用简化的强制停止逻辑
    size_t joined_count = 0;
    const auto max_wait_per_thread = std::chrono::milliseconds(50); // 每个线程最多等待50ms
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            try {
                // 使用分离模式，不等待线程完全结束
                // 在强制停止模式下，线程应该能够快速响应停止信号
                worker.detach();
                joined_count++;
                LOG_DEBUG("工作线程已分离");
            } catch (const std::exception& e) {
                LOG_ERROR("分离工作线程时发生异常: {}", e.what());
            }
        }
    }
    
    // 设置最终状态为已停止
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        SetState(ThreadPoolState::STOPPED);
    }
    
    LOG_INFO("强制停止完成，成功分离 {} 个工作线程", joined_count);
}

}
