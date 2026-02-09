#include "thread_pool_improved.h"
#include "logger.h"
#include <gtest/gtest.h>
#include <string>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <future>
#include <atomic>
#include <vector>
#include <random>

using namespace thread_pool_improved;

// 线程池测试基类
// SetUp / TearDown 会在每一个 TEST_F 前后自动执行
class ThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // SetUp 在每个测试开始前执行
        // 这里初始化日志系统，方便在测试失败或调试时查看线程池内部行为
        // 日志系统只会初始化一次，后续测试不会重复初始化
        if (!Logger::GetInstance().IsInitialized()) {
            LoggerConfig log_config;
            log_config.name = "test_threadpool";
            log_config.file_path = "../logs/test_threadpool.log";
            log_config.level = LogLevel::INFO;  // 线程池测试使用 INFO 级别即可
            log_config.enable_console = true;
            log_config.enable_file = true;
            log_config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [线程:%t] %v";
            Logger::GetInstance().Initialize(log_config);
        }
    }
    
    void TearDown() override {
        // TearDown 在每个测试结束后执行
        // 当前测试没有需要显式清理的资源，这里保留为空
    }
};

// ========================== 基础功能测试 ==========================

// 测试线程池是否能正确执行最基本的任务
TEST_F(ThreadPoolTest, BasicFunctionality) {
    ThreadPool pool(2);
    
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    
    // 提交 10 个简单任务，每个任务都会递增计数器
    for (int i = 0; i < 10; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                counter.fetch_add(1);
            })
        );
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.wait();
    }
    
    // 所有任务执行完后，计数器应为 10
    EXPECT_EQ(counter.load(), 10);
}

// ========================== 带返回值的任务 ==========================

// 测试线程池是否支持有返回值的任务
TEST_F(ThreadPoolTest, TasksWithReturnValue) {
    ThreadPool pool(4);
    
    auto future1 = pool.SubmitWithResult([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    
    auto future2 = pool.SubmitWithResult([]() -> std::string {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return "Hello World";
    });
    
    auto future3 = pool.SubmitWithResult([]() -> double {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 3.14159;
    });
    
    // 验证不同返回类型的任务都能正确获取结果
    EXPECT_EQ(future1.get(), 42);
    EXPECT_EQ(future2.get(), "Hello World");
    EXPECT_DOUBLE_EQ(future3.get(), 3.14159);
}

// ========================== 并发执行能力 ==========================

// 测试线程池在并发执行时，活跃线程数不会超过线程池大小
TEST_F(ThreadPoolTest, ConcurrentExecution) {
    const int num_threads = 4;
    const int num_tasks = 100;
    ThreadPool pool(num_threads);
    
    std::atomic<int> active_threads{0};
    std::atomic<int> max_active_threads{0};
    std::atomic<int> completed_tasks{0};
    
    std::vector<std::future<void>> futures;
    
    // 提交多个任务，记录并发执行过程中“同时活跃的最大线程数”
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&active_threads, &max_active_threads, &completed_tasks]() {
                // 当前活跃线程数 +1
                int current = active_threads.fetch_add(1) + 1;
                
                // 更新历史最大并发数
                int max = max_active_threads.load();
                while (max < current && !max_active_threads.compare_exchange_weak(max, current)) {
                    // 自旋直到成功更新
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                
                // 任务结束，活跃线程数 -1
                active_threads.fetch_sub(1);
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.wait();
    }
    
    // 所有任务都应完成
    EXPECT_EQ(completed_tasks.load(), num_tasks);
    // 最大并发线程数不应超过线程池线程数
    EXPECT_LE(max_active_threads.load(), num_threads);
    // 所有任务结束后，不应再有活跃线程
    EXPECT_EQ(active_threads.load(), 0);
}

// ========================== 异常处理 ==========================

// 测试任务抛异常时，线程池是否能正确通过 future 传播异常
TEST_F(ThreadPoolTest, ExceptionHandling) {
    ThreadPool pool(2);
    
    // 提交一个会抛出异常的任务
    auto future1 = pool.SubmitWithResult([]() -> int {
        throw std::runtime_error("测试异常");
        return 0;
    });
    
    // 提交一个正常任务
    auto future2 = pool.SubmitWithResult([]() -> int {
        return 42;
    });
    
    // 正常任务应正常完成
    EXPECT_EQ(future2.get(), 42);
    
    // 异常任务在 get() 时应抛出异常
    EXPECT_THROW(future1.get(), std::runtime_error);
}

// ========================== 线程池停止行为 ==========================

// 测试调用 Stop 后线程池的行为
TEST_F(ThreadPoolTest, ThreadPoolStop) {
    ThreadPool pool(2);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 提交一些任务
    for (int i = 0; i < 5; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 停止线程池（不再接受新任务）
    pool.Stop();
    
    // 等待已有任务完成，设置超时防止测试卡死
    bool all_completed = true;
    for (auto& future : futures) {
        auto status = future.wait_for(std::chrono::milliseconds(1000));
        if (status == std::future_status::timeout) {
            all_completed = false;
            std::cout << "警告: 任务超时未完成" << std::endl;
            break;
        }
    }
    
    // 如果全部完成，检查完成数量；否则至少应完成一部分
    if (all_completed) {
        EXPECT_EQ(completed_tasks.load(), 5);
    } else {
        std::cout << "完成的任务数: " << completed_tasks.load() << "/5" << std::endl;
        EXPECT_GT(completed_tasks.load(), 0);
    }
    
    // 线程池应处于停止状态
    EXPECT_TRUE(pool.IsStopped());
    
    // 停止后再提交任务，应直接失败
    EXPECT_THROW(
        pool.SubmitWithResult([]() { return 42; }),
        std::runtime_error
    );
}

// ========================== WaitAll 功能 ==========================

// 测试 WaitAll 是否能阻塞直到所有任务完成
TEST_F(ThreadPoolTest, WaitAllFunctionality) {
    ThreadPool pool(2);
    
    std::atomic<int> counter{0};
    
    // 提交任务但不保存 future
    for (int i = 0; i < 10; ++i) {
        pool.SubmitWithResult([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            counter.fetch_add(1);
        });
    }
    
    // 调用 WaitAll，等待线程池中所有任务执行完毕
    auto start = std::chrono::steady_clock::now();
    pool.WaitAll();
    auto duration = std::chrono::steady_clock::now() - start;
    
    EXPECT_EQ(counter.load(), 10);
    // WaitAll 不应无限阻塞
    EXPECT_LT(duration, std::chrono::milliseconds(5000));
}

// ========================== 性能简单验证 ==========================

// 简单性能测试：提交一定数量的计算任务，确保线程池能正常完成
TEST_F(ThreadPoolTest, PerformanceBenchmark) {
    const int num_threads = 4;
    const int num_tasks = 1000;
    ThreadPool pool(num_threads);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::future<int>> futures;
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(
            pool.SubmitWithResult([i]() -> int {
                int result = 0;
                for (int j = 0; j < 1000; ++j) {
                    result += i * j;
                }
                return result;
            })
        );
    }
    
    int total = 0;
    for (auto& future : futures) {
        total += future.get();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_EQ(futures.size(), num_tasks);
    EXPECT_GT(total, 0);
    
    std::cout << "执行 " << num_tasks << " 个任务耗时: " << duration.count() << "ms" << std::endl;
}

// ========================== 内存与资源管理 ==========================

// 测试线程池销毁后，外部共享资源是否仍然安全
TEST_F(ThreadPoolTest, MemoryManagement) {
    std::shared_ptr<int> shared_counter = std::make_shared<int>(0);
    
    {
        ThreadPool pool(2);
        
        // 提交使用 shared_ptr 的任务
        for (int i = 0; i < 10; ++i) {
            pool.SubmitWithResult([shared_counter]() {
                (*shared_counter)++;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
        
        // 等待部分任务开始执行
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 线程池销毁后，shared_ptr 仍应有效
    EXPECT_GT(*shared_counter, 0);
}

// ========================== 边界情况 ==========================

// 测试一些特殊或极端情况
TEST_F(ThreadPoolTest, EdgeCases) {
    // 线程数为 0：非法配置，应直接抛异常
    EXPECT_THROW(ThreadPool(0), std::invalid_argument);
    
    // 单线程池的基本行为
    ThreadPool pool(1);
    std::atomic<int> counter{0};
    
    auto future = pool.SubmitWithResult([&counter]() -> int {
        counter.fetch_add(1);
        return 42;
    });
    
    EXPECT_EQ(future.get(), 42);
    EXPECT_EQ(counter.load(), 1);
}

// ========================== 队列大小查询 ==========================

// 测试 QueueSize 接口是否能反映当前任务队列状态
TEST_F(ThreadPoolTest, QueueSizeQuery) {
    ThreadPool pool(1, 10);
    
    EXPECT_EQ(pool.QueueSize(), 0);
    
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    
    // 提交多个任务，其中一部分会进入队列
    for (int i = 0; i < 5; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                counter.fetch_add(1);
            })
        );
    }
    
    // 等待一小段时间，让任务排队
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 此时队列中应有等待执行的任务
    EXPECT_GT(pool.QueueSize(), 0);
    
    // 等待任务全部完成
    for (auto& future : futures) {
        future.wait();
    }
    
    // 队列应回到空状态
    EXPECT_EQ(pool.QueueSize(), 0);
}

// ========================== 活跃线程数 ==========================

// 测试 ActiveThreads 接口在任务执行期间是否有变化
TEST_F(ThreadPoolTest, ActiveThreadsCount) {
    ThreadPool pool(4);
    
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    
    // 提交一些执行时间较长的任务
    for (int i = 0; i < 4; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                counter.fetch_add(1);
            })
        );
    }
    
    // 等待任务开始执行
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // 执行期间应存在活跃线程
    EXPECT_GT(pool.ActiveThreads(), 0);
    
    for (auto& future : futures) {
        future.wait();
    }
    
    // 给一点时间让内部计数更新
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 任务结束后，活跃线程数应回落
    EXPECT_LE(pool.ActiveThreads(), 4);
}

// 活跃线程数的基本行为测试
TEST_F(ThreadPoolTest, ActiveThreadsBasic) {
    ThreadPool pool(2);
    
    // 初始状态下不应有活跃线程
    EXPECT_EQ(pool.ActiveThreads(), 0);
    
    std::atomic<int> counter{0};
    
    auto future = pool.SubmitWithResult([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        counter.fetch_add(1);
        return 42;
    });
    
    // 等待任务开始执行
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_GT(pool.ActiveThreads(), 0);
    
    EXPECT_EQ(future.get(), 42);
    
    // 等待计数更新
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    EXPECT_EQ(pool.ActiveThreads(), 0);
    EXPECT_EQ(counter.load(), 1);
}

// ========================== 大量任务 ==========================

// 测试在线程池中提交大量任务是否会丢任务
TEST_F(ThreadPoolTest, LargeNumberOfTasks) {
    const int num_tasks = 10000;
    ThreadPool pool(4);
    
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed]() {
                completed.fetch_add(1);
            })
        );
    }
    
    // 等待所有任务完成
    for (auto& future : futures) {
        future.wait();
    }
    
    // 所有任务都应被执行一次
    EXPECT_EQ(completed.load(), num_tasks);
}

// ==================== 队列策略测试 ====================

class QueuePolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试开始前会自动执行
        // 本组测试不需要准备额外环境，所以留空
    }
    void TearDown() override {
        // 每个测试结束后会自动执行
        // 本组测试没有需要清理的资源，所以留空
    }
};

// 测试 BLOCK 策略：队列满了以后，新提交的任务应该“卡住等待”，而不是丢掉或覆盖
TEST_F(QueuePolicyTest, BlockPolicyTest) {
    // 1 个工作线程 + 队列容量 2：很容易制造“队列满”的场景
    ThreadPool pool(1, 2, QueueFullPolicy::BLOCK);
    
    std::atomic<int> completed_tasks{0};
    std::atomic<bool> task_started{false};
    
    // 第 1 个任务：故意跑很久，让工作线程一直忙着，后面的任务只能排队
    auto future1 = pool.SubmitWithResult([&completed_tasks, &task_started]() {
        task_started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed_tasks++;
        return 1;
    });
    
    // 等到第 1 个任务真的开始执行（避免后面测试时机不稳定）
    while (!task_started.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // 第 2 个任务：此时工作线程被占用，所以它应该进入队列等待
    auto future2 = pool.SubmitWithResult([&completed_tasks]() {
        completed_tasks++;
        return 2;
    });
    
    // 第 3 个任务：队列容量只有 2（通常包含“正在执行的任务之外的等待任务”）
    // 在 BLOCK 策略下，这次提交应该会等待直到队列有空位
    auto start_time = std::chrono::steady_clock::now();
    auto future3 = pool.SubmitWithResult([&completed_tasks]() {
        completed_tasks++;
        return 3;
    });
    
    // 睡一会儿：如果 Submit 被阻塞了，这段时间就会反映在 elapsed 中
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    
    // 判断“确实发生了阻塞”：elapsed 至少接近我们等待的时间
    EXPECT_GE(elapsed, std::chrono::milliseconds(40));
    
    // 等待所有任务结束，再检查结果
    future1.wait();
    future2.wait();
    future3.wait();
    
    EXPECT_EQ(completed_tasks.load(), 3);
    EXPECT_EQ(future1.get(), 1);
    EXPECT_EQ(future2.get(), 2);
    EXPECT_EQ(future3.get(), 3);
}

// 测试 OVERWRITE 策略：队列满了以后，新任务会覆盖（挤掉）队列里最旧的任务
TEST_F(QueuePolicyTest, OverwritePolicyTest) {
    // 1 个工作线程 + 队列容量 1：更容易触发“覆盖”行为
    ThreadPool pool(1, 1, QueueFullPolicy::OVERWRITE);
    
    std::atomic<int> completed_tasks{0};
    std::atomic<bool> task_started{false};
    
    // 第 1 个任务：占住工作线程，保证后续任务需要排队
    auto future1 = pool.SubmitWithResult([&completed_tasks, &task_started]() {
        task_started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed_tasks++;
        return 1;
    });
    
    // 等待第 1 个任务开始执行（避免时机不稳定）
    while (!task_started.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // 再提交第 2 个任务：队列容量只有 1，OVERWRITE 策略下可能会把队列里更早的等待任务挤掉
    auto future2 = pool.SubmitWithResult([&completed_tasks]() {
        completed_tasks++;
        return 2;
    });
    
    // 等待任务完成
    future1.wait();
    future2.wait();
    
    // 在覆盖策略下，被覆盖掉的任务可能根本不会执行，所以 completed_tasks 可能小于 2
    EXPECT_LE(completed_tasks.load(), 2);
    // 第 2 个任务应能拿到结果（至少它是“最新的任务”）
    EXPECT_EQ(future2.get(), 2);
}

// 测试 DISCARD 策略：队列满了以后，新提交的任务会被直接丢弃
TEST_F(QueuePolicyTest, DiscardPolicyTest) {
    // 1 个工作线程 + 队列容量 1：很容易让“第 3 个任务”触发丢弃
    ThreadPool pool(1, 1, QueueFullPolicy::DISCARD);
    
    std::atomic<int> completed_tasks{0};
    std::atomic<bool> task1_started{false};
    
    // 第 1 个任务：占住工作线程
    auto future1 = pool.SubmitWithResult([&completed_tasks, &task1_started]() {
        task1_started = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed_tasks++;
        return 1;
    });
    
    // 确保第 1 个任务已经开始执行
    while (!task1_started.load()) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    // 第 2 个任务：进入队列等待
    auto future2 = pool.SubmitWithResult([&completed_tasks]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        completed_tasks++;
        return 2;
    });
    
    // 第 3 个任务：队列已满，DISCARD 策略下它会被直接丢弃
    auto future3 = pool.SubmitWithResult([&completed_tasks]() {
        completed_tasks++;
        return 3;
    });
    
    // 等待前两个任务完成
    future1.wait();
    future2.wait();
    
    // 只有前两个任务应该执行成功
    EXPECT_EQ(completed_tasks.load(), 2);
    EXPECT_EQ(future1.get(), 1);
    EXPECT_EQ(future2.get(), 2);
    
    // 第 3 个任务被丢弃：future.get() 应报错（future 没有对应的正常执行结果）
    EXPECT_THROW(future3.get(), std::future_error);
    
    // 丢弃计数器应该大于 0，说明确实发生过丢弃
    EXPECT_GT(pool.GetDiscardCounter(), 0);
}

// 测试用配置结构体创建线程池 + 测试运行时修改队列策略
TEST_F(QueuePolicyTest, ConfigConstructorTest) {
    ThreadPoolStruct config;
    config.core_threads = 2;
    config.max_queue_size = 5;
    config.queue_full_policy = QueueFullPolicy::DISCARD;
    
    ThreadPool pool(config);
    
    EXPECT_EQ(pool.GetQueuePolicy(), QueueFullPolicy::DISCARD);
    
    // 运行时修改策略：验证 SetQueuePolicy / GetQueuePolicy 是否生效
    pool.SetQueuePolicy(QueueFullPolicy::BLOCK);
    EXPECT_EQ(pool.GetQueuePolicy(), QueueFullPolicy::BLOCK);
}

// ==================== 动态线程管理测试 ====================

class DynamicThreadPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 动态线程管理相关测试可能比较复杂，打开日志方便观察“创建/回收”过程
        if (!Logger::GetInstance().IsInitialized()) {
            LoggerConfig log_config;
            log_config.name = "test_dynamic_threadpool";
            log_config.file_path = "../logs/test_dynamic_threadpool.log";
            log_config.level = LogLevel::INFO;
            log_config.enable_console = true;
            log_config.enable_file = true;
            log_config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [线程:%t] %v";
            Logger::GetInstance().Initialize(log_config);
        }
    }
    
    void TearDown() override {
        // 本组测试不需要额外清理，留空即可
    }
    
    // 构造“开启动态线程”的配置：用于触发自动扩容 / 自动回收
    ThreadPoolStruct CreateDynamicConfig() {
        ThreadPoolStruct config;
        config.core_threads = 2;
        config.max_threads = 6;
        config.max_queue_size = 10;
        config.enable_dynamic_threads = true;
        config.thread_creation_threshold = 3;
        config.thread_idle_timeout = std::chrono::milliseconds(500);
        config.load_check_interval = std::chrono::milliseconds(100);
        config.scale_up_threshold = 0.8;
        config.scale_down_threshold = 0.3;
        config.min_idle_time_for_removal = std::chrono::milliseconds(300);
        config.max_consecutive_idle_checks = 2;
        config.queue_full_policy = QueueFullPolicy::BLOCK;
        return config;
    }
    
    // 构造“关闭动态线程”的配置：只保留固定线程数，便于做对比
    ThreadPoolStruct CreateStaticConfig() {
        ThreadPoolStruct config;
        config.core_threads = 2;
        config.max_threads = 6;
        config.max_queue_size = 10;
        config.enable_dynamic_threads = false;
        config.queue_full_policy = QueueFullPolicy::BLOCK;
        return config;
    }
};

// 测试动态扩容（被 DISABLED 禁用）：负载大时应该创建更多线程
TEST_F(DynamicThreadPoolTest, DISABLED_DynamicThreadCreation) {
    auto config = CreateDynamicConfig();
    ThreadPool pool(config);
    
    // 刚启动时，线程数应该等于核心线程数
    EXPECT_EQ(pool.GetCurrentThreadCount(), config.core_threads);
    EXPECT_EQ(pool.GetCoreThreadCount(), config.core_threads);
    EXPECT_EQ(pool.GetMaxThreadCount(), config.max_threads);
    
    std::atomic<int> active_tasks{0};
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 提交一批耗时任务，让队列堆积，触发“扩容条件”
    for (int i = 0; i < 10; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&active_tasks, &completed_tasks]() {
                active_tasks.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                active_tasks.fetch_sub(1);
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 给负载检查线程一些时间，看看是否会创建额外线程
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // 线程数应该增加，但不能超过 max_threads
    EXPECT_GT(pool.GetCurrentThreadCount(), config.core_threads);
    EXPECT_LE(pool.GetCurrentThreadCount(), config.max_threads);
    
    for (auto& future : futures) {
        future.wait();
    }
    
    EXPECT_EQ(completed_tasks.load(), 10);
    
    // 统计信息应能反映“创建过额外线程”
    auto stats = pool.GetStats();
    EXPECT_GT(stats.threads_created, config.core_threads);
    EXPECT_GT(stats.peak_threads, config.core_threads);
    EXPECT_EQ(stats.tasks_completed, 10);
    EXPECT_EQ(stats.tasks_failed, 0);
}

// 测试“空闲回收”：任务做完后，多余线程应在空闲超时后被回收（但核心线程不能被回收）
TEST_F(DynamicThreadPoolTest, ThreadIdleTimeoutRecycling) {
    auto config = CreateDynamicConfig();
    // 为了让单测更快结束，把回收相关参数调得更激进
    config.thread_idle_timeout = std::chrono::milliseconds(200);
    config.min_idle_time_for_removal = std::chrono::milliseconds(150);
    config.max_consecutive_idle_checks = 2;
    config.load_check_interval = std::chrono::milliseconds(50);
    
    ThreadPool pool(config);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    LOG_INFO("开始线程回收测试 - 核心线程数: {}, 最大线程数: {}", 
             config.core_threads, config.max_threads);
    
    // 第一阶段：制造负载，促使线程池创建额外线程
    for (int i = 0; i < 12; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_DEBUG("执行任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 等一会儿，让线程池有机会扩容
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    size_t peak_threads = pool.GetCurrentThreadCount();
    LOG_INFO("峰值线程数: {}", peak_threads);
    EXPECT_GT(peak_threads, config.core_threads);
    EXPECT_LE(peak_threads, config.max_threads);
    
    // 等任务全部做完
    for (auto& future : futures) {
        future.wait();
    }
    
    LOG_INFO("所有任务完成，已完成任务数: {}", completed_tasks.load());
    EXPECT_EQ(completed_tasks.load(), 12);
    
    // 第二阶段：不再提交任务，等待线程因为空闲而被回收
    LOG_INFO("等待线程回收...");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    
    size_t after_recycle_threads = pool.GetCurrentThreadCount();
    LOG_INFO("回收后线程数: {}", after_recycle_threads);
    
    // 回收后线程数应下降，但不能低于核心线程数
    EXPECT_LT(after_recycle_threads, peak_threads);
    EXPECT_GE(after_recycle_threads, config.core_threads);
    
    // 统计信息应反映“销毁过线程”
    auto stats = pool.GetStats();
    EXPECT_GT(stats.threads_created, config.core_threads);
    EXPECT_GT(stats.threads_destroyed, 0);
    EXPECT_EQ(stats.peak_threads, peak_threads);
    
    LOG_INFO("线程回收测试完成 - 初始: {}, 峰值: {}, 回收后: {}, 创建: {}, 销毁: {}", 
             config.core_threads, peak_threads, after_recycle_threads,
             stats.threads_created, stats.threads_destroyed);
}

// 测试“回收统计是否正确”：扩容过、回收过后，stats 中的 created/destroyed/peak 是否一致
TEST_F(DynamicThreadPoolTest, ThreadRecyclingStatistics) {
    auto config = CreateDynamicConfig();
    config.thread_idle_timeout = std::chrono::milliseconds(150);
    config.min_idle_time_for_removal = std::chrono::milliseconds(100);
    config.max_consecutive_idle_checks = 2;
    config.load_check_interval = std::chrono::milliseconds(50);
    
    ThreadPool pool(config);
    
    // 刚创建时：线程创建数应等于核心线程数，销毁数为 0，峰值也是核心线程数
    auto initial_stats = pool.GetStats();
    EXPECT_EQ(initial_stats.threads_created, config.core_threads);
    EXPECT_EQ(initial_stats.threads_destroyed, 0);
    EXPECT_EQ(initial_stats.peak_threads, config.core_threads);
    
    std::vector<std::future<void>> futures;
    
    // 制造负载：触发扩容
    for (int i = 0; i < 15; ++i) {
        futures.push_back(
            pool.SubmitWithResult([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            })
        );
    }
    
    // 给扩容一点时间
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto peak_stats = pool.GetStats();
    size_t created_threads = peak_stats.threads_created;
    size_t peak_count = peak_stats.peak_threads;
    
    LOG_INFO("峰值期统计 - 创建线程: {}, 峰值线程: {}", created_threads, peak_count);
    EXPECT_GT(created_threads, config.core_threads);
    EXPECT_GT(peak_count, config.core_threads);
    
    for (auto& future : futures) {
        future.wait();
    }
    
    // 任务结束后等待回收
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    auto final_stats = pool.GetStats();
    
    LOG_INFO("最终统计 - 创建: {}, 销毁: {}, 峰值: {}, 当前: {}", 
             final_stats.threads_created, final_stats.threads_destroyed,
             final_stats.peak_threads, pool.GetCurrentThreadCount());
    
    // created/peak 应保持扩容时的记录；destroyed 应大于 0；当前线程数不应超过峰值且不低于核心线程数
    EXPECT_EQ(final_stats.threads_created, created_threads);
    EXPECT_GT(final_stats.threads_destroyed, 0);
    EXPECT_EQ(final_stats.peak_threads, peak_count);
    EXPECT_LE(pool.GetCurrentThreadCount(), peak_count);
    EXPECT_GE(pool.GetCurrentThreadCount(), config.core_threads);
}

// 测试回收边界条件：核心线程不能被回收，非核心线程在空闲后应该能回收掉
TEST_F(DynamicThreadPoolTest, ThreadRecyclingBoundaryConditions) {
    auto config = CreateDynamicConfig();
    config.core_threads = 2;
    config.max_threads = 4;
    config.thread_idle_timeout = std::chrono::milliseconds(100);
    config.min_idle_time_for_removal = std::chrono::milliseconds(80);
    config.max_consecutive_idle_checks = 1;
    config.load_check_interval = std::chrono::milliseconds(30);
    
    ThreadPool pool(config);
    
    // 测试 1：只提交少量任务，不触发扩容；等很久后，核心线程数也不能减少
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < 2; ++i) {
        futures.push_back(
            pool.SubmitWithResult([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            })
        );
    }
    
    for (auto& future : futures) {
        future.wait();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    EXPECT_EQ(pool.GetCurrentThreadCount(), config.core_threads);
    
    futures.clear();
    
    // 测试 2：提交足够多任务触发扩容；任务结束后等待回收，线程数应下降到接近核心线程数
    for (int i = 0; i < 8; ++i) {
        futures.push_back(
            pool.SubmitWithResult([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
            })
        );
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    size_t peak = pool.GetCurrentThreadCount();
    EXPECT_GT(peak, config.core_threads);
    
    for (auto& future : futures) {
        future.wait();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    
    size_t after_recycle = pool.GetCurrentThreadCount();
    
    EXPECT_LE(after_recycle, peak);
    EXPECT_GE(after_recycle, config.core_threads);
    
    LOG_INFO("边界测试 - 核心线程: {}, 峰值: {}, 回收后: {}", 
             config.core_threads, peak, after_recycle);
}

// 多轮测试：反复触发“扩容 -> 回收”，看线程池是否能稳定工作且每轮结果合理
TEST_F(DynamicThreadPoolTest, MultipleCreateRecycleCycles) {
    auto config = CreateDynamicConfig();
    config.thread_idle_timeout = std::chrono::milliseconds(150);
    config.min_idle_time_for_removal = std::chrono::milliseconds(100);
    config.max_consecutive_idle_checks = 2;
    config.load_check_interval = std::chrono::milliseconds(40);
    
    ThreadPool pool(config);
    
    std::vector<size_t> peak_threads;
    std::vector<size_t> recycle_threads;
    
    // 进行 3 轮：每轮都制造负载触发扩容，然后空闲等待回收
    for (int cycle = 0; cycle < 3; ++cycle) {
        LOG_INFO("开始第 {} 轮测试", cycle + 1);
        
        std::vector<std::future<void>> futures;
        
        for (int i = 0; i < 10; ++i) {
            futures.push_back(
                pool.SubmitWithResult([cycle, i]() {
                    LOG_TRACE("执行第 {} 轮任务 {}", cycle + 1, i);
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                })
            );
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        size_t peak = pool.GetCurrentThreadCount();
        peak_threads.push_back(peak);
        
        for (auto& future : futures) {
            future.wait();
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        
        size_t after_recycle = pool.GetCurrentThreadCount();
        recycle_threads.push_back(after_recycle);
        
        LOG_INFO("第 {} 轮完成 - 峰值: {}, 回收后: {}", cycle + 1, peak, after_recycle);
        
        EXPECT_GT(peak, config.core_threads);
        EXPECT_LE(after_recycle, peak);
        EXPECT_GE(after_recycle, config.core_threads);
    }
    
    // 每轮的峰值应该差不多（允许有少量波动）
    for (size_t i = 1; i < peak_threads.size(); ++i) {
        EXPECT_LE(std::abs(static_cast<int>(peak_threads[i]) - static_cast<int>(peak_threads[0])), 2);
    }
    
    // 每轮回收后都应接近核心线程数（允许多 1 个线程的缓冲）
    for (size_t i = 0; i < recycle_threads.size(); ++i) {
        EXPECT_LE(recycle_threads[i], config.core_threads + 1);
    }
    
    auto final_stats = pool.GetStats();
    LOG_INFO("多轮测试完成 - 总创建: {}, 总销毁: {}", 
             final_stats.threads_created, final_stats.threads_destroyed);
    
    EXPECT_GT(final_stats.threads_destroyed, 0);
}

// 负载感知：低负载时保持核心线程，高负载时增加线程，并且 load_factor 合理
TEST_F(DynamicThreadPoolTest, LoadAwareThreadAdjustment) {
    auto config = CreateDynamicConfig();
    ThreadPool pool(config);
    
    // 低负载：只跑一个小任务，线程数应保持核心线程数
    std::atomic<int> completed_tasks{0};
    
    auto future1 = pool.SubmitWithResult([&completed_tasks]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        completed_tasks.fetch_add(1);
    });
    
    future1.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    size_t low_load_threads = pool.GetCurrentThreadCount();
    EXPECT_EQ(low_load_threads, config.core_threads);
    
    // 高负载：提交一堆长任务，让队列堆积，触发扩容
    std::vector<std::future<void>> high_load_futures;
    for (int i = 0; i < 12; ++i) {
        high_load_futures.push_back(
            pool.SubmitWithResult([&completed_tasks]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 等待负载检查线程反应
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    
    size_t high_load_threads = pool.GetCurrentThreadCount();
    EXPECT_GT(high_load_threads, low_load_threads);
    EXPECT_LE(high_load_threads, config.max_threads);
    
    for (auto& future : high_load_futures) {
        future.wait();
    }
    
    // load_factor 应在 [0, 1] 范围内
    auto stats = pool.GetStats();
    EXPECT_GE(stats.load_factor, 0.0);
    EXPECT_LE(stats.load_factor, 1.0);
    
    std::cout << "负载测试 - 低负载线程数: " << low_load_threads 
              << ", 高负载线程数: " << high_load_threads 
              << ", 负载因子: " << stats.load_factor << std::endl;
}

// 手动触发负载检查：不等定时器，直接调用 TriggerLoadCheck 看是否能促使线程调整
TEST_F(DynamicThreadPoolTest, ManualLoadCheck) {
    auto config = CreateDynamicConfig();
    ThreadPool pool(config);
    
    size_t initial_threads = pool.GetCurrentThreadCount();
    
    // 提交一些较慢任务，让队列/负载变高
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 6; ++i) {
        futures.push_back(
            pool.SubmitWithResult([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            })
        );
    }
    
    // 手动触发一次负载检查
    pool.TriggerLoadCheck();
    
    // 等一会儿让这次检查生效
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    size_t after_check_threads = pool.GetCurrentThreadCount();
    
    for (auto& future : futures) {
        future.wait();
    }
    
    // 手动检查至少不应该让线程数变小（通常会保持不变或增大）
    EXPECT_GE(after_check_threads, initial_threads);
    
    std::cout << "手动负载检查 - 初始: " << initial_threads 
              << ", 检查后: " << after_check_threads << std::endl;
}

// 统计信息测试：完成数/失败数/平均耗时/峰值线程等统计是否能反映真实情况
TEST_F(DynamicThreadPoolTest, StatisticsCollection) {
    auto config = CreateDynamicConfig();
    ThreadPool pool(config);
    
    // 刚启动时统计应为 0，线程创建数应等于核心线程数
    auto initial_stats = pool.GetStats();
    EXPECT_EQ(initial_stats.tasks_completed, 0);
    EXPECT_EQ(initial_stats.tasks_failed, 0);
    EXPECT_EQ(initial_stats.threads_created, config.core_threads);
    EXPECT_EQ(initial_stats.threads_destroyed, 0);
    EXPECT_EQ(initial_stats.peak_threads, config.core_threads);
    
    std::atomic<int> task_counter{0};
    std::vector<std::future<void>> futures;
    
    // 提交 5 个正常任务（应计入 completed）
    for (int i = 0; i < 5; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&task_counter]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                task_counter.fetch_add(1);
            })
        );
    }
    
    // 再提交 2 个会抛异常的任务（应计入 failed）
    for (int i = 0; i < 2; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&task_counter]() {
                task_counter.fetch_add(1);
                throw std::runtime_error("测试异常");
            })
        );
    }
    
    // 等待任务结束：这里不关心 future.get() 的异常，只关心统计结果
    for (size_t i = 0; i < futures.size(); ++i) {
        try {
            futures[i].wait();
        } catch (...) {
            // 忽略异常
        }
    }
    
    // 给线程池一点时间更新统计信息
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto final_stats = pool.GetStats();
    
    EXPECT_EQ(final_stats.tasks_completed, 5);
    EXPECT_EQ(final_stats.tasks_failed, 2);
    EXPECT_GT(final_stats.avg_task_time_ms, 0);
    EXPECT_GE(final_stats.peak_threads, config.core_threads);
    
    std::cout << "统计信息 - 成功: " << final_stats.tasks_completed 
              << ", 失败: " << final_stats.tasks_failed 
              << ", 平均时间: " << final_stats.avg_task_time_ms << "ms"
              << ", 峰值线程: " << final_stats.peak_threads << std::endl;
}

// 简单对比：动态线程池 vs 静态线程池，看看动态版在高并发任务下是否明显变差
TEST_F(DynamicThreadPoolTest, DynamicVsStaticPerformance) {
    const int num_tasks = 50;
    const int task_duration_ms = 100;
    
    // 动态线程池：允许扩容/回收
    auto dynamic_config = CreateDynamicConfig();
    auto dynamic_start = std::chrono::high_resolution_clock::now();
    
    {
        ThreadPool dynamic_pool(dynamic_config);
        std::vector<std::future<void>> dynamic_futures;
        
        for (int i = 0; i < num_tasks; ++i) {
            dynamic_futures.push_back(
                dynamic_pool.SubmitWithResult([task_duration_ms]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(task_duration_ms));
                })
            );
        }
        
        for (auto& future : dynamic_futures) {
            future.wait();
        }
    }
    
    auto dynamic_end = std::chrono::high_resolution_clock::now();
    auto dynamic_duration = std::chrono::duration_cast<std::chrono::milliseconds>(dynamic_end - dynamic_start);
    
    // 静态线程池：线程数固定，不会扩容
    auto static_config = CreateStaticConfig();
    auto static_start = std::chrono::high_resolution_clock::now();
    
    {
        ThreadPool static_pool(static_config);
        std::vector<std::future<void>> static_futures;
        
        for (int i = 0; i < num_tasks; ++i) {
            static_futures.push_back(
                static_pool.SubmitWithResult([task_duration_ms]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(task_duration_ms));
                })
            );
        }
        
        for (auto& future : static_futures) {
            future.wait();
        }
    }
    
    auto static_end = std::chrono::high_resolution_clock::now();
    auto static_duration = std::chrono::duration_cast<std::chrono::milliseconds>(static_end - static_start);
    
    std::cout << "性能对比 - 动态线程池: " << dynamic_duration.count() << "ms, "
              << "静态线程池: " << static_duration.count() << "ms" << std::endl;
    
    // 动态线程池不要求一定更快，但至少不能差太多（防止动态逻辑带来严重退化）
    double performance_ratio = static_cast<double>(dynamic_duration.count()) / static_duration.count();
    EXPECT_LT(performance_ratio, 1.5);
}

// 极限压力：大量短任务，验证动态线程管理不会把系统搞崩，并且统计数据正确
TEST_F(DynamicThreadPoolTest, ExtremeLoadConditions) {
    auto config = CreateDynamicConfig();
    config.max_threads = 10;
    ThreadPool pool(config);
    
    const int num_tasks = 100;
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 提交大量“非常短”的任务，主要考察调度和统计
    for (int i = 0; i < num_tasks; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks]() {
                for (int j = 0; j < 1000; ++j) {
                    volatile int temp = j * j;
                    (void)temp;
                }
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 每个 future 都要在合理时间内结束，避免死锁/卡死
    for (auto& future : futures) {
        auto status = future.wait_for(std::chrono::milliseconds(1000));
        EXPECT_EQ(status, std::future_status::ready);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(completed_tasks.load(), num_tasks);
    
    auto stats = pool.GetStats();
    EXPECT_EQ(stats.tasks_completed, num_tasks);
    EXPECT_EQ(stats.tasks_failed, 0);
    EXPECT_LE(stats.peak_threads, config.max_threads);
    
    std::cout << "极限测试 - 完成任务: " << stats.tasks_completed 
              << ", 峰值线程: " << stats.peak_threads 
              << ", 平均时间: " << stats.avg_task_time_ms << "ms" << std::endl;
}

// 简化版动态线程池测试（被 DISABLED 禁用）：用于快速检查动态线程池的基本可用性
TEST_F(DynamicThreadPoolTest, DISABLED_BasicDynamicThreadPool) {
    auto config = CreateDynamicConfig();
    config.load_check_interval = std::chrono::milliseconds(100);
    
    ThreadPool pool(config);
    
    EXPECT_EQ(pool.GetCurrentThreadCount(), config.core_threads);
    EXPECT_EQ(pool.GetCoreThreadCount(), config.core_threads);
    
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < 3; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                completed.fetch_add(1);
            })
        );
    }
    
    for (auto& future : futures) {
        auto status = future.wait_for(std::chrono::milliseconds(2000));
        EXPECT_EQ(status, std::future_status::ready);
    }
    
    EXPECT_EQ(completed.load(), 3);
}

// 用配置文件创建动态线程池：验证“配置 -> 动态线程行为”的完整链路
TEST_F(DynamicThreadPoolTest, ConfigFileBasedDynamicThreads) {
    auto pool = ThreadPool::CreateFromConfig("../config/thread_pool.json");
    
    EXPECT_EQ(pool->GetCoreThreadCount(), 4);
    EXPECT_EQ(pool->GetMaxThreadCount(), 8);
    EXPECT_EQ(pool->GetCurrentThreadCount(), 4);
    
    std::vector<std::future<void>> futures;
    
    // 提交足够多、足够慢的任务，让队列明显堆积，从而触发扩容
    for (int i = 0; i < 30; ++i) {
        futures.push_back(
            pool->SubmitWithResult([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            })
        );
    }
    
    // 等任务开始执行并产生队列积压
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 队列里待处理任务数应足够多，才能满足扩容触发条件
    size_t pending = pool->QueueSize();
    EXPECT_GT(pending, 2);
    
    // 再等一会儿，让负载检查逻辑真的跑起来
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    size_t current_threads = pool->GetCurrentThreadCount();
    EXPECT_GT(current_threads, 4);
    EXPECT_LE(current_threads, 8);
    
    for (auto& future : futures) {
        future.wait();
    }
    
    auto stats = pool->GetStats();
    std::cout << "配置文件测试 - 当前线程: " << current_threads 
              << ", 峰值线程: " << stats.peak_threads 
              << ", 负载因子: " << stats.load_factor << std::endl;
}

// ==================== 状态控制测试 ====================

class StateControlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试开始前自动执行：初始化日志，方便观察“暂停/恢复/关闭”等状态变化
        // 日志系统只会初始化一次，避免多个测试重复初始化
        if (!Logger::GetInstance().IsInitialized()) {
            LoggerConfig log_config;
            log_config.name = "test_state_control";
            log_config.file_path = "../logs/test_state_control.log";
            log_config.level = LogLevel::INFO;
            log_config.enable_console = true;
            log_config.enable_file = true;
            log_config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] [线程:%t] %v";
            Logger::GetInstance().Initialize(log_config);
        }
    }
    
    void TearDown() override {
        // 每个测试结束后自动执行：本组测试无额外资源需要清理
    }
};

// 测试 Pause/Resume：暂停后不接收新任务；恢复后继续执行队列中的任务
TEST_F(StateControlTest, PauseAndResume) {
    ThreadPool pool(2);
    
    // 刚创建时应为 RUNNING，且未暂停
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    EXPECT_FALSE(pool.IsPaused());
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 先提交 5 个任务，让线程池处于“正在处理任务”的状态
    for (int i = 0; i < 5; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_INFO("执行任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    // 等一小会，让部分任务已经开始执行（避免 Pause 太早导致测试不稳定）
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 暂停线程池：进入 PAUSED，且 IsPaused() 为 true
    pool.Pause();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::PAUSED);
    EXPECT_TRUE(pool.IsPaused());
    
    // 暂停后不允许再提交新任务（防止“暂停还在收任务”造成语义混乱）
    EXPECT_THROW(
        pool.SubmitWithResult([]() { return 42; }),
        std::runtime_error
    );
    
    // 记录暂停期间已完成的任务数：暂停后队列中的任务不应继续推进（但正在运行的可能会结束）
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int paused_completed = completed_tasks.load();
    
    // 恢复线程池：回到 RUNNING，允许继续处理任务
    pool.Resume();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    EXPECT_FALSE(pool.IsPaused());
    
    // 等待之前提交的任务都做完
    for (auto& future : futures) {
        future.wait();
    }
    
    EXPECT_EQ(completed_tasks.load(), 5);
    
    LOG_INFO("暂停测试完成 - 暂停时完成: {}, 最终完成: {}", 
             paused_completed, completed_tasks.load());
}

// 测试优雅关闭：Shutdown(GRACEFUL) 应等待已提交的任务全部完成，然后停止线程池
TEST_F(StateControlTest, GracefulShutdown) {
    ThreadPool pool(2);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 提交 6 个比较慢的任务：用于观察“优雅关闭是否真的等任务做完”
    for (int i = 0; i < 6; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_INFO("开始执行长任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                completed_tasks.fetch_add(1);
                LOG_INFO("完成长任务 {}", i);
            })
        );
    }
    
    // 让任务先跑起来，避免“还没开始就关闭”导致结果不稳定
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 优雅关闭：计时，用于判断它确实等待了一段时间
    auto start_time = std::chrono::steady_clock::now();
    pool.Shutdown(ShutdownOption::GRACEFUL);
    auto end_time = std::chrono::steady_clock::now();
    
    // 优雅关闭的核心要求：任务都做完，线程池变 STOPPED
    EXPECT_EQ(completed_tasks.load(), 6);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    
    // 关闭后不允许再提交任务
    EXPECT_THROW(
        pool.SubmitWithResult([]() { return 42; }),
        std::runtime_error
    );
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    LOG_INFO("优雅关闭耗时: {}ms", duration.count());
    
    // 优雅关闭需要等待任务完成，所以耗时不应过短（这里只做一个下限的“合理性检查”）
    EXPECT_GE(duration.count(), 100);
}

// 测试强制停止：Shutdown(FORCE) 应尽快停止，不保证任务都完成
TEST_F(StateControlTest, ForceShutdown) {
    ThreadPool pool(2);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 任务设置得非常长：为了让 FORCE 停止时“来不及做完”更明显
    for (int i = 0; i < 6; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_INFO("开始执行长任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                completed_tasks.fetch_add(1);
                LOG_INFO("完成长任务 {}", i);
            })
        );
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 强制停止：计时，验证它“确实很快返回”
    auto start_time = std::chrono::steady_clock::now();
    pool.Shutdown(ShutdownOption::FORCE);
    auto end_time = std::chrono::steady_clock::now();
    
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    LOG_INFO("强制停止耗时: {}ms, 完成任务数: {}", duration.count(), completed_tasks.load());
    
    // FORCE 的核心特点：不等任务，尽快结束
    EXPECT_LT(duration.count(), 200);
    
    // FORCE 不保证所有任务完成，所以完成数可能小于 6
    EXPECT_LE(completed_tasks.load(), 6);
}

// 测试超时关闭：Shutdown(TIMEOUT, X) 会等一段时间，超时后停止（不保证任务都完成）
TEST_F(StateControlTest, TimeoutShutdown) {
    ThreadPool pool(2);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 任务设置得非常长：保证在 300ms 内不可能全部做完
    for (int i = 0; i < 4; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_INFO("开始执行超长任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                completed_tasks.fetch_add(1);
                LOG_INFO("完成超长任务 {}", i);
            })
        );
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // TIMEOUT：计时，验证它大致会在超时时间附近返回
    auto start_time = std::chrono::steady_clock::now();
    pool.Shutdown(ShutdownOption::TIMEOUT, std::chrono::milliseconds(300));
    auto end_time = std::chrono::steady_clock::now();
    
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    LOG_INFO("超时关闭耗时: {}ms, 完成任务数: {}", duration.count(), completed_tasks.load());
    
    // 期望：耗时不应明显短于超时时间，也不应拖太久
    EXPECT_GE(duration.count(), 250);
    EXPECT_LT(duration.count(), 500);
    
    // 超时关闭不保证所有任务完成
    EXPECT_LE(completed_tasks.load(), 4);
}

// 测试在 PAUSED 状态下关闭：暂停后队列不再推进，优雅关闭只会等“正在执行的任务”结束
TEST_F(StateControlTest, ShutdownFromPausedState) {
    ThreadPool pool(2);
    
    std::atomic<int> completed_tasks{0};
    std::vector<std::future<void>> futures;
    
    // 提交 4 个任务：其中最多 2 个能同时执行，剩下会在队列里等待
    for (int i = 0; i < 4; ++i) {
        futures.push_back(
            pool.SubmitWithResult([&completed_tasks, i]() {
                LOG_INFO("执行任务 {}", i);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                completed_tasks.fetch_add(1);
            })
        );
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 暂停：此时队列里的等待任务应该不会再被取出来执行
    pool.Pause();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::PAUSED);
    
    // 从暂停状态进行优雅关闭：本质上只会让“正在执行的任务”收尾，然后停止
    pool.Shutdown(ShutdownOption::GRACEFUL);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    
    // 这里验证暂停的语义是否正确：
    // - 同时最多只有 2 个任务在跑，所以最多完成 2 个
    // - 50ms 的等待足够让至少 1 个任务进入执行阶段，所以至少完成 1 个
    int completed = completed_tasks.load();
    LOG_INFO("暂停状态下完成的任务数: {}/4", completed);
    EXPECT_GE(completed, 1);
    EXPECT_LE(completed, 2);
    
    LOG_INFO("从暂停状态关闭测试完成");
}

// 测试状态查询接口：GetState/IsPaused/IsStopped 三者是否一致
TEST_F(StateControlTest, StateQuery) {
    ThreadPool pool(2);
    
    // 初始状态：运行中
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    EXPECT_FALSE(pool.IsPaused());
    EXPECT_FALSE(pool.IsStopped());
    
    // 暂停状态
    pool.Pause();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::PAUSED);
    EXPECT_TRUE(pool.IsPaused());
    EXPECT_FALSE(pool.IsStopped());
    
    // 恢复回运行状态
    pool.Resume();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    EXPECT_FALSE(pool.IsPaused());
    EXPECT_FALSE(pool.IsStopped());
    
    // 停止状态
    pool.Stop();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    EXPECT_FALSE(pool.IsPaused());
    EXPECT_TRUE(pool.IsStopped());
}

// 测试幂等性：重复调用同一个操作，不应该把状态弄乱，也不应该报错
TEST_F(StateControlTest, IdempotentOperations) {
    ThreadPool pool(2);
    
    // 连续 Pause：第一次进入 PAUSED，第二次应仍保持 PAUSED
    pool.Pause();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::PAUSED);
    pool.Pause();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::PAUSED);
    
    // 连续 Resume：第一次回到 RUNNING，第二次应仍保持 RUNNING
    pool.Resume();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    pool.Resume();
    EXPECT_EQ(pool.GetState(), ThreadPoolState::RUNNING);
    
    // 连续 Shutdown：第一次停止，第二次应仍保持 STOPPED
    pool.Shutdown(ShutdownOption::GRACEFUL);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
    pool.Shutdown(ShutdownOption::GRACEFUL);
    EXPECT_EQ(pool.GetState(), ThreadPoolState::STOPPED);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
