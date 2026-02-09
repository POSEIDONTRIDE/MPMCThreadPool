#include "thread_pool_config.h"
#include "logger.h"
#include <iostream>

namespace thread_pool_improved {

ThreadPoolStruct ThreadPoolConfig::LoadFromFile(const std::string& config_path) {
    // 从配置文件加载线程池参数（入口函数）
    LOG_INFO("开始从文件加载线程池配置: {}", config_path);
    
    // 1) 检查配置文件是否存在
    if (!std::filesystem::exists(config_path)) {
        LOG_ERROR("配置文件不存在: {}", config_path);
        throw std::runtime_error("配置文件不存在: " + config_path);
    }
    
    // 2) 打开并读取文件内容
    std::ifstream file(config_path);
    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: {}", config_path);
        throw std::runtime_error("无法打开配置文件: " + config_path);
    }
    
    std::string json_content((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    file.close();
    
    LOG_DEBUG("配置文件内容长度: {} 字节", json_content.length());
    
    // 3) 交由字符串解析接口处理
    auto config = LoadFromString(json_content);
    
    // 记录关键配置，便于确认配置是否生效
    LOG_INFO("成功加载线程池配置 - 核心线程: {}, 最大线程: {}, 队列大小: {}", 
             config.core_threads, config.max_threads, config.max_queue_size);
    
    return config;
}

ThreadPoolStruct ThreadPoolConfig::LoadFromString(const std::string& json_str) {
    try {
        // 1) 解析 JSON
        nlohmann::json j = nlohmann::json::parse(json_str);
        
        // 2) 反序列化为 ThreadPoolStruct（由 adl_serializer 支持）
        ThreadPoolStruct config = j.get<ThreadPoolStruct>();
        
        // 3) 校验配置合法性
        ValidateConfig(config);
        
        return config;
    } catch (const nlohmann::json::parse_error& e) {
        // JSON 语法错误
        throw std::invalid_argument("JSON解析错误: " + std::string(e.what()));
    } catch (const nlohmann::json::exception& e) {
        // JSON 结构或字段类型错误
        throw std::invalid_argument("JSON配置错误: " + std::string(e.what()));
    }
}

void ThreadPoolConfig::ValidateConfig(const ThreadPoolStruct& config) {
    // 核心线程数必须大于 0
    if (config.core_threads == 0) {
        throw std::invalid_argument("核心线程数必须大于0");
    }
    
    // 最大线程数不能小于核心线程数
    if (config.max_threads < config.core_threads) {
        throw std::invalid_argument("最大线程数不能小于核心线程数");
    }
    
    // 保活时间不能为负
    if (config.keep_alive_time.count() < 0) {
        throw std::invalid_argument("线程保活时间不能为负数");
    }
    
    // 推荐的最大线程数限制（经验值：CPU 核数 * 4）
    const size_t recommended_max = std::thread::hardware_concurrency() * 4;
    if (config.max_threads > recommended_max) {
        std::cerr << "警告: 最大线程数(" << config.max_threads 
                  << ")超过推荐值(" << recommended_max << ")，可能影响性能" << std::endl;
    }
    
    // 队列过大可能导致内存占用过高
    if (config.max_queue_size > 100000) {
        std::cerr << "警告: 队列大小(" << config.max_queue_size 
                  << ")过大，可能占用过多内存" << std::endl;
    }
}

QueueFullPolicy ThreadPoolConfig::StringToQueueFullPolicy(const std::string& policy_str) {
    // 将字符串策略转换为枚举（支持大小写）
    if (policy_str == "BLOCK" || policy_str == "block") {
        return QueueFullPolicy::BLOCK;
    } else if (policy_str == "OVERWRITE" || policy_str == "overwrite") {
        return QueueFullPolicy::OVERWRITE;
    } else if (policy_str == "DISCARD" || policy_str == "discard") {
        return QueueFullPolicy::DISCARD;
    } else {
        throw std::invalid_argument(
            "无效的队列满处理策略: " + policy_str + 
            " (有效值: BLOCK, OVERWRITE, DISCARD)"
        );
    }
}

std::string ThreadPoolConfig::QueueFullPolicyToString(QueueFullPolicy policy) {
    // 将枚举值转换为字符串（用于 JSON 序列化）
    switch (policy) {
        case QueueFullPolicy::BLOCK:
            return "BLOCK";
        case QueueFullPolicy::OVERWRITE:
            return "OVERWRITE";
        case QueueFullPolicy::DISCARD:
            return "DISCARD";
        default:
            throw std::invalid_argument("未知的队列满处理策略");
    }
}

} // namespace thread_pool_improved
