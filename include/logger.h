#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace thread_pool_improved {

/**
 * @brief 日志级别（对 spdlog 的简单封装）
 */
enum class LogLevel {
    TRACE = 0,
    DEBUG = 1, 
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5,
    OFF = 6
};

/**
 * @brief 日志系统配置
 *
 * 用于控制日志级别、输出方式以及文件滚动策略，
 * 一般在程序启动阶段初始化。
 */
struct LoggerConfig {
    std::string name = "thread_pool";                     
    std::string file_path = "../logs/thread_pool.log";     
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v";
    LogLevel level = LogLevel::INFO;
    bool enable_console = true;
    bool enable_file = true;
    size_t max_file_size = 10 * 1024 * 1024;
    size_t max_files = 5;
    bool auto_flush = true;
};

/**
 * @brief 日志管理器（单例）
 *
 * 为线程池项目提供统一的日志接口，
 * 底层基于 spdlog 实现。
 */
class Logger {
public:
    /**
     * @brief 获取日志单例实例
     */
    static Logger& GetInstance();

    /**
     * @brief 初始化日志系统（程序启动时调用一次）
     */
    bool Initialize(const LoggerConfig& config = LoggerConfig{});

    /**
     * @brief 设置日志级别（支持运行时调整）
     */
    void SetLevel(LogLevel level);

    /**
     * @brief 获取当前日志级别
     */
    LogLevel GetLevel() const;

    /**
     * @brief 刷新日志缓冲区
     */
    void Flush();

    /**
     * @brief 关闭日志系统并释放资源
     */
    void Shutdown();

    // 以下接口为不同级别日志的轻量封装
    template<typename... Args>
    void Trace(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::trace)) {
            logger_->trace(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Debug(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::debug)) {
            logger_->debug(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Info(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::info)) {
            logger_->info(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Warn(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::warn)) {
            logger_->warn(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Error(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::err)) {
            logger_->error(fmt, std::forward<Args>(args)...);
        }
    }

    template<typename... Args>
    void Critical(const std::string& fmt, Args&&... args) {
        if (logger_ && logger_->should_log(spdlog::level::critical)) {
            logger_->critical(fmt, std::forward<Args>(args)...);
        }
    }

    /**
     * @brief 判断日志系统是否已初始化
     */
    bool IsInitialized() const { return initialized_; }

private:
    Logger() = default;
    ~Logger();

    // 禁止拷贝，保证单例语义
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * @brief 自定义日志级别转换为 spdlog 日志级别
     */
    static spdlog::level::level_enum ToSpdlogLevel(LogLevel level);

    /**
     * @brief 创建日志文件目录（如不存在）
     */
    bool CreateLogDirectory(const std::string& file_path);

private:
    std::shared_ptr<spdlog::logger> logger_;   // spdlog 日志器
    LoggerConfig config_;                      // 当前日志配置
    bool initialized_ = false;                 // 是否已初始化
};

} // namespace thread_pool_improved

// ==================== 日志快捷宏 ====================
// 简化日志调用，避免在业务代码中反复获取单例

#define LOG_TRACE(fmt, ...)     thread_pool_improved::Logger::GetInstance().Trace(fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)     thread_pool_improved::Logger::GetInstance().Debug(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)      thread_pool_improved::Logger::GetInstance().Info(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)      thread_pool_improved::Logger::GetInstance().Warn(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)     thread_pool_improved::Logger::GetInstance().Error(fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...)  thread_pool_improved::Logger::GetInstance().Critical(fmt, ##__VA_ARGS__)

// 条件日志宏：condition 为 true 时才输出
#define LOG_IF(condition, level, fmt, ...) \
    do { \
        if (condition) { \
            LOG_##level(fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// DEBUG 模式下记录函数进入/退出
#ifdef DEBUG
    #define LOG_FUNC_ENTER()    LOG_TRACE("进入函数: {}", __FUNCTION__)
    #define LOG_FUNC_EXIT()     LOG_TRACE("退出函数: {}", __FUNCTION__)
#else
    #define LOG_FUNC_ENTER()    do {} while(0)
    #define LOG_FUNC_EXIT()     do {} while(0)
#endif

// 性能测量宏（微秒级）
#define LOG_PERF_START(name) \
    auto __perf_start_##name = std::chrono::high_resolution_clock::now()

#define LOG_PERF_END(name) \
    do { \
        auto __perf_end = std::chrono::high_resolution_clock::now(); \
        auto __perf_duration = std::chrono::duration_cast<std::chrono::microseconds>(__perf_end - __perf_start_##name); \
        LOG_DEBUG("性能测量 [{}]: {} 微秒", #name, __perf_duration.count()); \
    } while(0)
