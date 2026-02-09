#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "thread_pool_config.h"
#include "thread_pool_improved.h"
#include "logger.h"

using namespace thread_pool_improved;

class ThreadPoolConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        // SetUp 会在每个 TEST_F 运行前自动执行
        // 这里做两件事：
        // 1) 初始化日志（只会初始化一次，避免重复初始化）
        // 2) 创建临时目录，用来存放本测试生成的配置文件
        if (!Logger::GetInstance().IsInitialized()) {
            LoggerConfig log_config;
            log_config.name = "test_config";
            log_config.file_path = "../logs/test_config.log";
            log_config.level = LogLevel::DEBUG;  // 用 DEBUG 方便定位配置解析/校验过程
            log_config.enable_console = true;
            log_config.enable_file = true;
            log_config.pattern = "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v";
            Logger::GetInstance().Initialize(log_config);
        }
        
        // 创建测试临时目录（本文件会在 TearDown 中删除）
        test_dir_ = "test_config_temp";
        std::filesystem::create_directories(test_dir_);
    }
    
    void TearDown() override {
        // TearDown 会在每个 TEST_F 结束后自动执行
        // 删除本测试创建的临时目录和文件，避免污染本地环境
        std::filesystem::remove_all(test_dir_);
    }
    
    std::string test_dir_;
    
    // 写一个临时 JSON 配置文件到 test_dir_，便于测试 LoadFromFile
    void CreateTestConfigFile(const std::string& filename, const std::string& content) {
        std::ofstream file(test_dir_ + "/" + filename);
        file << content;
        file.close();
    }
};

// 从完整且合法的 JSON 字符串加载配置：字段应被正确解析并写入 ThreadPoolStruct
TEST_F(ThreadPoolConfigTest, LoadFromString_ValidJson) {
    std::string json_str = R"({
        "core_threads": 4,
        "max_threads": 8,
        "max_queue_size": 1000,
        "keep_alive_time_ms": 60000,
        "queue_full_policy": "BLOCK"
    })";
    
    ThreadPoolStruct config = ThreadPoolConfig::LoadFromString(json_str);
    
    EXPECT_EQ(config.core_threads, 4);
    EXPECT_EQ(config.max_threads, 8);
    EXPECT_EQ(config.max_queue_size, 1000);
    EXPECT_EQ(config.keep_alive_time.count(), 60000);
    EXPECT_EQ(config.queue_full_policy, QueueFullPolicy::BLOCK);
}

// 部分配置：只提供部分字段，其余字段应使用默认值（并保证 max_threads >= core_threads）
TEST_F(ThreadPoolConfigTest, LoadFromString_PartialConfig) {
    std::string json_str = R"({
        "core_threads": 6,
        "queue_full_policy": "DISCARD"
    })";
    
    ThreadPoolStruct config = ThreadPoolConfig::LoadFromString(json_str);
    
    EXPECT_EQ(config.core_threads, 6);
    EXPECT_EQ(config.queue_full_policy, QueueFullPolicy::DISCARD);
    // 未提供的字段应保持默认值；同时 max_threads 至少要等于 core_threads（避免不合理配置）
    EXPECT_GE(config.max_threads, config.core_threads);
    EXPECT_EQ(config.max_queue_size, 1000);
}

// 队列满策略的解析：验证不同策略字符串能正确映射到枚举，并支持大小写兼容（如 "block"）
TEST_F(ThreadPoolConfigTest, LoadFromString_DifferentPolicies) {
    // BLOCK：队列满时阻塞提交者
    std::string json_block = R"({"queue_full_policy": "BLOCK"})";
    ThreadPoolStruct config_block = ThreadPoolConfig::LoadFromString(json_block);
    EXPECT_EQ(config_block.queue_full_policy, QueueFullPolicy::BLOCK);
    
    // OVERWRITE：队列满时覆盖旧任务（保新丢旧）
    std::string json_overwrite = R"({"queue_full_policy": "OVERWRITE"})";
    ThreadPoolStruct config_overwrite = ThreadPoolConfig::LoadFromString(json_overwrite);
    EXPECT_EQ(config_overwrite.queue_full_policy, QueueFullPolicy::OVERWRITE);
    
    // DISCARD：队列满时丢弃新任务
    std::string json_discard = R"({"queue_full_policy": "DISCARD"})";
    ThreadPoolStruct config_discard = ThreadPoolConfig::LoadFromString(json_discard);
    EXPECT_EQ(config_discard.queue_full_policy, QueueFullPolicy::DISCARD);
    
    // 支持小写策略名（兼容配置文件大小写）
    std::string json_lower = R"({"queue_full_policy": "block"})";
    ThreadPoolStruct config_lower = ThreadPoolConfig::LoadFromString(json_lower);
    EXPECT_EQ(config_lower.queue_full_policy, QueueFullPolicy::BLOCK);
}

// 从文件加载配置：写入一个临时 JSON 文件，然后验证 LoadFromFile 能正确解析
TEST_F(ThreadPoolConfigTest, LoadFromFile_ValidFile) {
    std::string config_content = R"({
        "core_threads": 2,
        "max_threads": 6,
        "max_queue_size": 500,
        "keep_alive_time_ms": 30000,
        "queue_full_policy": "OVERWRITE"
    })";
    
    CreateTestConfigFile("valid_config.json", config_content);
    
    ThreadPoolStruct config = ThreadPoolConfig::LoadFromFile(test_dir_ + "/valid_config.json");
    
    EXPECT_EQ(config.core_threads, 2);
    EXPECT_EQ(config.max_threads, 6);
    EXPECT_EQ(config.max_queue_size, 500);
    EXPECT_EQ(config.keep_alive_time.count(), 30000);
    EXPECT_EQ(config.queue_full_policy, QueueFullPolicy::OVERWRITE);
}

// 加载不存在的文件：应该直接报错，避免静默使用默认配置导致隐藏问题
TEST_F(ThreadPoolConfigTest, LoadFromFile_NonExistentFile) {
    EXPECT_THROW(
        ThreadPoolConfig::LoadFromFile("non_existent_file.json"),
        std::runtime_error
    );
}

// JSON 格式本身有错误：解析阶段应抛 invalid_argument
TEST_F(ThreadPoolConfigTest, LoadFromString_InvalidJson) {
    std::string invalid_json = R"({
        "core_threads": 4,
        "max_threads": 8,
        "invalid_syntax"
    })";
    
    EXPECT_THROW(
        ThreadPoolConfig::LoadFromString(invalid_json),
        std::invalid_argument
    );
}

// 配置值不合法：虽然 JSON 语法对，但参数在语义上不合理，应抛 invalid_argument
TEST_F(ThreadPoolConfigTest, LoadFromString_InvalidConfig) {
    // core_threads 不能为 0（线程池最少要有 1 个核心线程）
    std::string json_zero_threads = R"({"core_threads": 0})";
    EXPECT_THROW(
        ThreadPoolConfig::LoadFromString(json_zero_threads),
        std::invalid_argument
    );
    
    // max_threads 不能小于 core_threads（否则扩容/运行逻辑矛盾）
    std::string json_invalid_max = R"({
        "core_threads": 8,
        "max_threads": 4
    })";

    EXPECT_THROW(
        ThreadPoolConfig::LoadFromString(json_invalid_max),
        std::invalid_argument
    );
    
    // keep_alive_time_ms 不能是负数
    std::string json_negative_time = R"({"keep_alive_time_ms": -1000})";
    EXPECT_THROW(
        ThreadPoolConfig::LoadFromString(json_negative_time),
        std::invalid_argument
    );
}

// 队列策略字符串非法：不允许使用未知策略，必须明确报错
TEST_F(ThreadPoolConfigTest, LoadFromString_InvalidPolicy) {
    std::string json_invalid_policy = R"({"queue_full_policy": "INVALID_POLICY"})";
    
    EXPECT_THROW(
        ThreadPoolConfig::LoadFromString(json_invalid_policy),
        std::invalid_argument
    );
}

// 用配置文件创建线程池：验证“配置 -> 线程池实例”的整条链路可用
TEST_F(ThreadPoolConfigTest, CreateThreadPoolFromConfig) {
    std::string config_content = R"({
        "core_threads": 3,
        "max_threads": 6,
        "max_queue_size": 200,
        "keep_alive_time_ms": 45000,
        "queue_full_policy": "DISCARD"
    })";
    
    CreateTestConfigFile("thread_pool_config.json", config_content);
    
    // 通过配置文件创建线程池对象
    auto thread_pool = ThreadPool::CreateFromConfig(test_dir_ + "/thread_pool_config.json");
    
    EXPECT_NE(thread_pool, nullptr);
    EXPECT_EQ(thread_pool->GetQueuePolicy(), QueueFullPolicy::DISCARD);
    
    // 简单跑一下线程池：提交 5 个任务，最后计数应为 5
    std::atomic<int> counter{0};
    
    for (int i = 0; i < 5; ++i) {
        thread_pool->SubmitWithResult([&counter]() {
            counter++;
            return counter.load();
        });
    }
    
    thread_pool->WaitAll();
    EXPECT_EQ(counter.load(), 5);
}

// 预设配置文件（项目自带）：如果文件存在，则加载并做基本合法性检查
TEST_F(ThreadPoolConfigTest, LoadPredefinedConfig) {
    // 只有当该配置文件存在时才测试，避免在不同环境下因为缺文件导致单测失败
    if (std::filesystem::exists("../../config/thread_pool.json")) {
        EXPECT_NO_THROW({
            auto config = ThreadPoolConfig::LoadFromFile("config/thread_pool.json");
            EXPECT_GT(config.core_threads, 0);
            EXPECT_GE(config.max_threads, config.core_threads);
        });
    }
}

// 边界值测试：最小可用值和较大值，确保解析/校验逻辑在边界附近仍然稳定
TEST_F(ThreadPoolConfigTest, LoadFromString_BoundaryValues) {
    // 最小有效配置：尽量小的线程数/队列/保活时间，应该能通过校验
    std::string json_min = R"({
        "core_threads": 1,
        "max_threads": 1,
        "max_queue_size": 1,
        "keep_alive_time_ms": 0
    })";
    
    EXPECT_NO_THROW({
        ThreadPoolStruct config = ThreadPoolConfig::LoadFromString(json_min);
        EXPECT_EQ(config.core_threads, 1);
        EXPECT_EQ(config.max_threads, 1);
        EXPECT_EQ(config.max_queue_size, 1);
        EXPECT_EQ(config.keep_alive_time.count(), 0);
    });
    
    // 较大配置：可能会记录警告（例如资源开销较大），但不应抛异常
    std::string json_large = R"({
        "core_threads": 64,
        "max_threads": 128,
        "max_queue_size": 50000
    })";
    
    EXPECT_NO_THROW({
        ThreadPoolStruct config = ThreadPoolConfig::LoadFromString(json_large);
        EXPECT_EQ(config.core_threads, 64);
        EXPECT_EQ(config.max_threads, 128);
        EXPECT_EQ(config.max_queue_size, 50000);
    });
}
