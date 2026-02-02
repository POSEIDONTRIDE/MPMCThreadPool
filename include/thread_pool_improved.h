#pragma once
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <functional>
#include <future>
#include "mpmc_blocking_q.h"

namespace thread_pool_improved {

// 任务基类 - 支持多态的任务执行
class TaskBase {
public:
    virtual ~TaskBase() = default;
    virtual void Execute() = 0; // 纯虚函数，子类必须实现
    virtual bool IsExecutionSuccessful() const { return true; } // 默认认为执行成功，子类可以重写
};

// 带返回值的任务模板
template<typename T>
class FutureTask : public TaskBase {
public:
    explicit FutureTask(std::function<T()> func) : func_(std::move(func)) {}
    
    void Execute() override {
        try {
            if constexpr (std::is_void_v<T>) {
                func_();
                promise_.set_value();
                execution_success_ = true;
            } else {
                promise_.set_value(func_());
                execution_success_ = true;
            }
        } catch (...) {
            // 将异常传递给future
            promise_.set_exception(std::current_exception());
            execution_success_ = false;
        }
    }
    
    // 获取future对象
    std::future<T> GetFuture() {
        return promise_.get_future();
    }
    
    // 检查执行是否成功
    bool IsExecutionSuccessful() const {
        return execution_success_;
    }

private:
    std::function<T()> func_; // 存储要执行的函数
    std::promise<T> promise_; // 用于设置返回值的promise
    bool execution_success_ = false; // 执行是否成功的标志
};

// 队列满时的处理策略
enum class QueueFullPolicy {
    BLOCK,      // 阻塞等待（默认）
    OVERWRITE,  // 覆盖最旧的任务
    DISCARD     // 丢弃新任务
};

// 线程池相关配置
struct ThreadPoolStruct {
    size_t core_threads = std::thread::hardware_concurrency();      ///< 核心线程数
    size_t max_threads = std::thread::hardware_concurrency() * 2;   ///< 最大线程数
    size_t max_queue_size = 1000;                                   ///< 最大队列大小，0表示无限制
    std::chrono::milliseconds keep_alive_time{60000};               ///< 非核心线程空闲超时时间(60秒)
    QueueFullPolicy queue_full_policy = QueueFullPolicy::BLOCK;     ///< 队列满时的处理策略
    
    // 动态线程管理配置
    bool enable_dynamic_threads = true;                             ///< 是否启用动态线程管理
    size_t thread_creation_threshold = 2;                           ///< 触发创建新线程的待处理任务阈值
    std::chrono::milliseconds thread_idle_timeout{30000};           ///< 线程空闲超时时间(30秒)
    std::chrono::milliseconds load_check_interval{5000};            ///< 负载检查间隔(5秒)
    double scale_up_threshold = 0.8;                                ///< 扩容阈值（活跃线程比例）
    double scale_down_threshold = 0.3;                              ///< 缩容阈值（活跃线程比例）
    std::chrono::milliseconds min_idle_time_for_removal{10000};     ///< 线程移除前的最小空闲时间(10秒)
    size_t max_consecutive_idle_checks = 3;                         ///< 线程移除前的最大连续空闲检查次数
};

// 线程池状态
enum class ThreadPoolState {
    RUNNING,          // 正常运行状态
    PAUSED,           // 暂停状态（不接受新任务，已有任务暂停执行）
    SHUTTING_DOWN,    // 优雅关闭中（不接受新任务，等待现有任务完成）
    FORCE_STOPPING,   // 强制停止中（不接受新任务，尽快停止）
    STOPPED           // 已停止
};

// 关闭选项
enum class ShutdownOption {
    GRACEFUL,         // 优雅关闭：等待所有任务完成
    FORCE,           // 强制关闭：立即停止，不等待任务完成
    TIMEOUT          // 超时关闭：等待指定时间后强制停止
};


class ThreadPool {
public:
    
    struct Stats {
        size_t tasks_completed = 0;    ///< 已完成任务数
        size_t tasks_failed = 0;       ///< 失败任务数
        double avg_task_time_ms = 0;   ///< 平均任务执行时间（毫秒）
        size_t active_threads = 0;     ///< 当前活跃线程数
        size_t peak_threads = 0;       ///< 峰值线程数
        size_t threads_created = 0;    ///< 总创建线程数
        size_t threads_destroyed = 0;  ///< 总销毁线程数
        double load_factor = 0.0;      ///< 当前负载因子
        
        // 队列使用率相关统计
        size_t current_queue_size = 0; ///< 当前队列中的任务数
        size_t max_queue_size = 0;     ///< 队列最大容量
        double queue_usage_rate = 0.0; ///< 当前队列使用率 (0.0-1.0)
        size_t peak_queue_size = 0;    ///< 队列峰值大小
        double peak_queue_usage_rate = 0.0; ///< 队列峰值使用率
        size_t tasks_discarded = 0;    ///< 被丢弃的任务数
        size_t tasks_overwritten = 0;  ///< 被覆盖的任务数（溢出计数）
    };

// 构造函数：指定线程数和最大队列大小
    ThreadPool(size_t thread_count = std::thread::hardware_concurrency(),
               size_t max_queue_size = 1000,
               QueueFullPolicy queue_policy = QueueFullPolicy::BLOCK);
    
    // 使用配置结构体的构造函数
    explicit ThreadPool(const ThreadPoolStruct& config);
    
    // 从JSON配置文件创建线程池
    static std::unique_ptr<ThreadPool> CreateFromConfig(const std::string& config_path);
    
    using queue_type = MpmcBlockingQueue<std::unique_ptr<TaskBase>>;
    
    ~ThreadPool();
    
    // 提交任务（接受基类指针）
    // 返回值：true表示任务成功提交，false表示任务被丢弃（仅在DISCARD策略下）
    bool Submit(std::unique_ptr<TaskBase> task);
    
    // 提交带返回值的函数（返回future）
    template<typename F, typename... Args>
    auto SubmitWithResult(F&& func, Args&&... args) 
        -> std::future<typename std::invoke_result_t<F, Args...>>;
    
    // 等待所有任务完成
    void WaitAll();
    
    // 获取当前队列中的任务数量
    size_t QueueSize();
    
    // 获取当前活跃的线程数
    size_t ActiveThreads() const;
    
    // 停止线程池（立即停止）
    void Stop();
    
    // 优雅关闭线程池
    void Shutdown(ShutdownOption option = ShutdownOption::GRACEFUL, 
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    
    // 暂停线程池（暂停任务执行但保持线程活跃）
    void Pause();
    
    // 恢复线程池执行
    void Resume();
    
    // 检查线程池是否暂停
    bool IsPaused() const { return state_.load() == ThreadPoolState::PAUSED; }
    
    // 获取线程池当前状态
    ThreadPoolState GetState() const { return state_.load(); }
    
    // 等待所有正在执行的任务完成（用于优雅关闭）
    bool WaitForRunningTasks(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
    
    // 检查线程池是否已停止
    bool IsStopped() const { return stop_.load(); }
    
    // 获取队列满时的处理策略
    QueueFullPolicy GetQueuePolicy() const { return queue_policy_; }
    
    // 设置队列满时的处理策略
    void SetQueuePolicy(QueueFullPolicy policy) { queue_policy_ = policy; }
    
    // 获取丢弃的任务数量
    size_t GetDiscardCounter() const { return task_queue_.DiscardCounter(); }
    
    // 获取线程池统计信息
    Stats GetStats() const;
    
    // 动态线程管理相关方法
    void TriggerLoadCheck();           // 手动触发负载检查
    size_t GetCurrentThreadCount() const { return current_threads_.load(); }
    size_t GetCoreThreadCount() const { return config_.core_threads; }
    size_t GetMaxThreadCount() const { return config_.max_threads; }
    
    // 禁止拷贝
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 工作线程的主循环函数
    void WorkerLoop();
    
    // 动态线程管理私有方法
    void LoadBalancingLoop();                   // 负载均衡主循环
    bool TryCreateNewThread();                  // 尝试创建新线程
    bool TryRemoveIdleThread();                 // 尝试移除空闲线程
    void UpdateThreadActivity(size_t thread_index); // 更新线程活跃度
    double CalculateLoadFactor() const;         // 计算负载因子
    void CleanupFinishedThreads();              // 清理已结束的线程
    
    // 状态控制私有方法
    bool CanAcceptNewTasks() const;             // 检查是否可以接受新任务
    void SetState(ThreadPoolState new_state);  // 设置线程池状态
    bool WaitForStateChange(ThreadPoolState from_state, std::chrono::milliseconds timeout); // 等待状态改变
    void ForceStop();                           // 强制停止（不等待任务完成）
    
    // 同步原语
    mutable std::mutex queue_mutex_;           // 队列互斥锁
    std::condition_variable queue_condition_;  // 队列条件变量
    std::condition_variable wait_condition_;   // 等待条件变量
    mutable std::mutex thread_management_mutex_; // 线程管理互斥锁
    std::condition_variable pause_condition_;  // 暂停条件变量
    mutable std::mutex state_mutex_;           // 状态互斥锁
    
    // 线程管理
    std::vector<std::thread> workers_;         // 工作线程容器
    std::atomic<bool> stop_{false};            // 停止标志
    std::atomic<size_t> active_threads_{0};    // 活跃线程计数
    std::atomic<size_t> pending_tasks_{0};     // 待处理任务计数
    std::atomic<size_t> running_tasks_{0};     // 正在执行的任务计数
    
    // 配置参数
    size_t max_queue_size_;                    // 最大队列大小
    queue_type task_queue_;                    // 任务队列
    QueueFullPolicy queue_policy_;             // 队列满时的处理策略

    ThreadPoolStruct config_;  
    std::atomic<size_t> current_threads_{0};   ///< 当前线程数（原子操作）
    
    // 动态线程管理相关
    std::vector<std::chrono::steady_clock::time_point> thread_last_active_; ///< 线程最后活跃时间
    std::vector<std::unique_ptr<std::atomic<size_t>>> thread_idle_count_;  ///< 线程空闲检查次数计数
    std::vector<std::unique_ptr<std::atomic<bool>>> thread_should_exit_;   ///< 线程退出标志
    std::thread load_balancer_thread_;                    ///< 负载均衡线程
    std::atomic<bool> load_balancer_stop_{false};        ///< 负载均衡线程停止标志

    // 线程池状态
    std::atomic<ThreadPoolState> state_{ThreadPoolState::RUNNING};

    // 统计信息
    mutable std::mutex stats_mutex_;           // 统计信息互斥锁
    Stats stats_;



};

// 模板函数实现
template<typename F, typename... Args>
auto ThreadPool::SubmitWithResult(F&& func, Args&&... args) 
    -> std::future<typename std::invoke_result_t<F, Args...>> {
    
    using ReturnType = typename std::invoke_result_t<F, Args...>;
    
    // 绑定函数和参数
    auto bound_func = std::bind(std::forward<F>(func), std::forward<Args>(args)...);
    
    // 创建任务并获取future
    auto task = std::make_unique<FutureTask<ReturnType>>(std::move(bound_func));
    auto future = task->GetFuture();
    
    // 提交任务，检查是否成功
    bool submitted = Submit(std::move(task));
    
    // 如果任务被丢弃，设置future为异常状态
    if (!submitted) {
        // 创建一个新的promise来设置异常
        std::promise<ReturnType> promise;
        promise.set_exception(std::make_exception_ptr(std::future_error(std::future_errc::broken_promise)));
        return promise.get_future();
    }
    
    return future;
}
}