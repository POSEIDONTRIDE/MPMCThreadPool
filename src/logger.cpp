#include "logger.h"
#include <iostream>
#include <filesystem>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace thread_pool_improved {

Logger& Logger::GetInstance() {
    // Meyers Singleton：首次调用时初始化，线程安全（C++11 起保证）
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    // 防止遗漏关闭（尤其是文件 sink 需要 flush）
    Shutdown();
}

bool Logger::Initialize(const LoggerConfig& config) {
    // 防止重复初始化（日志系统通常只初始化一次）
    if (initialized_) {
        std::cerr << "警告: 日志系统已经初始化，忽略重复初始化" << std::endl;
        return true;
    }

    config_ = config;
    
    try {
        // sinks = 日志输出目标集合（控制台 / 文件等）
        std::vector<spdlog::sink_ptr> sinks;
        
        // 1) 控制台输出（带颜色）
        if (config_.enable_console) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(ToSpdlogLevel(config_.level));
            console_sink->set_pattern(config_.pattern);
            sinks.push_back(console_sink);
        }
        
        // 2) 文件输出（滚动文件：按大小切分，保留 N 个）
        if (config_.enable_file) {
            // 创建日志目录，避免文件 sink 初始化失败
            if (!CreateLogDirectory(config_.file_path)) {
                std::cerr << "错误: 无法创建日志目录: " << config_.file_path << std::endl;
                return false;
            }
            
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config_.file_path, 
                config_.max_file_size, 
                config_.max_files
            );
            file_sink->set_level(ToSpdlogLevel(config_.level));
            file_sink->set_pattern(config_.pattern);
            sinks.push_back(file_sink);
        }
        
        // 至少启用一种输出方式，否则日志器没有意义
        if (sinks.empty()) {
            std::cerr << "错误: 至少需要启用一个输出方式（控制台或文件）" << std::endl;
            return false;
        }
        
        // 创建 spdlog logger，并绑定多个 sinks
        logger_ = std::make_shared<spdlog::logger>(config_.name, sinks.begin(), sinks.end());
        
        // 设置全局日志级别（控制哪些日志会被输出）
        logger_->set_level(ToSpdlogLevel(config_.level));
        
        // auto_flush 为 true 时：达到指定级别后自动 flush
        if (config_.auto_flush) {
            logger_->flush_on(spdlog::level::info);
        }
        
        // 注册到 spdlog：后续可通过名称获取
        spdlog::register_logger(logger_);
        
        // 设置为默认 logger：spdlog::info(...) 也会走它
        spdlog::set_default_logger(logger_);
        
        initialized_ = true;
        
        // 输出一条启动日志，方便确认配置生效
        LOG_INFO("日志系统初始化成功 - 名称: {}, 级别: {}, 控制台: {}, 文件: {}", 
                config_.name, 
                static_cast<int>(config_.level),
                config_.enable_console ? "启用" : "禁用",
                config_.enable_file ? config_.file_path : "禁用");
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: 初始化日志系统失败: " << e.what() << std::endl;
        return false;
    }
}

void Logger::SetLevel(LogLevel level) {
    // 只有初始化完成后才允许调整级别
    if (!initialized_ || !logger_) {
        std::cerr << "警告: 日志系统未初始化，无法设置日志级别" << std::endl;
        return;
    }
    
    config_.level = level;
    logger_->set_level(ToSpdlogLevel(level));
    
    // logger 的 sinks 也需要同步更新（避免不同输出目标级别不一致）
    for (auto& sink : logger_->sinks()) {
        sink->set_level(ToSpdlogLevel(level));
    }
    
    LOG_INFO("日志级别已更新为: {}", static_cast<int>(level));
}

LogLevel Logger::GetLevel() const {
    return config_.level;
}

void Logger::Flush() {
    // 主动 flush：用于关键日志/程序退出前
    if (logger_) {
        logger_->flush();
    }
}

void Logger::Shutdown() {
    // 未初始化就无需关闭
    if (!initialized_) {
        return;
    }
    
    try {
        if (logger_) {
            // 关闭前输出一条日志，帮助定位退出流程
            LOG_INFO("日志系统正在关闭...");
            logger_->flush();

            // 从 spdlog 注册表移除，避免重复名称冲突
            spdlog::drop(config_.name);

            // 释放 logger
            logger_.reset();
        }
        
        // 关闭 spdlog（释放线程/资源等）
        spdlog::shutdown();
        
        initialized_ = false;
        
    } catch (const std::exception& e) {
        std::cerr << "警告: 关闭日志系统时发生错误: " << e.what() << std::endl;
    }
}

spdlog::level::level_enum Logger::ToSpdlogLevel(LogLevel level) {
    // 自定义 LogLevel -> spdlog level 映射
    switch (level) {
        case LogLevel::TRACE:    return spdlog::level::trace;
        case LogLevel::DEBUG:    return spdlog::level::debug;
        case LogLevel::INFO:     return spdlog::level::info;
        case LogLevel::WARN:     return spdlog::level::warn;
        case LogLevel::ERROR:    return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        case LogLevel::OFF:      return spdlog::level::off;
        default:                 return spdlog::level::info;
    }
}

bool Logger::CreateLogDirectory(const std::string& file_path) {
    try {
        // 从日志文件路径中提取目录部分并创建（若不存在）
        std::filesystem::path path(file_path);
        std::filesystem::path dir = path.parent_path();
        
        if (!dir.empty() && !std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "错误: 创建日志目录失败: " << e.what() << std::endl;
        return false;
    }
}

} // namespace thread_pool_improved
