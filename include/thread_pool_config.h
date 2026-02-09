#pragma once
#include <string>
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "thread_pool_improved.h"

namespace thread_pool_improved {

/**
 * @brief 线程池 JSON 配置加载器
 *
 * 负责从 JSON 文件或字符串中读取配置，
 * 并转换为 ThreadPoolStruct 结构体。
 */
class ThreadPoolConfig {
public:
    /**
     * @brief 从 JSON 文件加载线程池配置
     *
     * @param config_path JSON 配置文件路径
     * @return ThreadPoolStruct 解析后的配置
     *
     * @throws std::runtime_error    文件不存在或读取失败
     * @throws std::invalid_argument JSON 格式错误或配置值非法
     */
    static ThreadPoolStruct LoadFromFile(const std::string& config_path);
    
    /**
     * @brief 从 JSON 字符串加载线程池配置
     *
     * @param json_str JSON 字符串
     * @return ThreadPoolStruct 解析后的配置
     *
     * @throws std::invalid_argument JSON 格式错误或配置值非法
     */
    static ThreadPoolStruct LoadFromString(const std::string& json_str);
    
    /**
     * @brief 将字符串转换为队列满策略枚举
     */
    static QueueFullPolicy StringToQueueFullPolicy(const std::string& policy_str);
    
    /**
     * @brief 将队列满策略枚举转换为字符串
     */
    static std::string QueueFullPolicyToString(QueueFullPolicy policy);

private:
    /**
     * @brief 校验线程池配置的合法性
     *
     * 用于检查线程数量、队列大小等参数是否合理。
     */
    static void ValidateConfig(const ThreadPoolStruct& config);
};

} // namespace thread_pool_improved


// ================= JSON 序列化 / 反序列化支持 =================
//
// 通过 nlohmann::json 的 ADL 机制，
// 实现配置结构体与 JSON 之间的自动转换。

namespace nlohmann {

    /**
     * @brief QueueFullPolicy 的 JSON 序列化支持
     */
    template<>
    struct adl_serializer<thread_pool_improved::QueueFullPolicy> {
        static void to_json(json& j, const thread_pool_improved::QueueFullPolicy& policy) {
            j = thread_pool_improved::ThreadPoolConfig::QueueFullPolicyToString(policy);
        }
        
        static void from_json(const json& j, thread_pool_improved::QueueFullPolicy& policy) {
            policy = thread_pool_improved::ThreadPoolConfig::StringToQueueFullPolicy(
                j.get<std::string>()
            );
        }
    };
    
    /**
     * @brief ThreadPoolStruct 的 JSON 序列化支持
     */
    template<>
    struct adl_serializer<thread_pool_improved::ThreadPoolStruct> {
        static void to_json(json& j, const thread_pool_improved::ThreadPoolStruct& config) {
            j = json{
                {"core_threads", config.core_threads},
                {"max_threads", config.max_threads},
                {"max_queue_size", config.max_queue_size},
                {"keep_alive_time_ms", config.keep_alive_time.count()},
                {"queue_full_policy", config.queue_full_policy}
            };
        }
        
        static void from_json(const json& j, thread_pool_improved::ThreadPoolStruct& config) {
            // 基础线程池配置
            if (j.contains("core_threads")) {
                config.core_threads = j.at("core_threads").get<size_t>();
            }
            if (j.contains("max_threads")) {
                config.max_threads = j.at("max_threads").get<size_t>();
            }
            if (j.contains("max_queue_size")) {
                config.max_queue_size = j.at("max_queue_size").get<size_t>();
            }
            if (j.contains("keep_alive_time_ms")) {
                config.keep_alive_time =
                    std::chrono::milliseconds(j.at("keep_alive_time_ms").get<int64_t>());
            }
            if (j.contains("queue_full_policy")) {
                config.queue_full_policy =
                    j.at("queue_full_policy").get<thread_pool_improved::QueueFullPolicy>();
            }
            
            // 动态线程管理相关配置
            if (j.contains("enable_dynamic_threads")) {
                config.enable_dynamic_threads = j.at("enable_dynamic_threads").get<bool>();
            }
            if (j.contains("thread_creation_threshold")) {
                config.thread_creation_threshold = j.at("thread_creation_threshold").get<size_t>();
            }
            if (j.contains("thread_idle_timeout_ms")) {
                config.thread_idle_timeout =
                    std::chrono::milliseconds(j.at("thread_idle_timeout_ms").get<int64_t>());
            }
            if (j.contains("load_check_interval_ms")) {
                config.load_check_interval =
                    std::chrono::milliseconds(j.at("load_check_interval_ms").get<int64_t>());
            }
            if (j.contains("scale_up_threshold")) {
                config.scale_up_threshold = j.at("scale_up_threshold").get<double>();
            }
            if (j.contains("scale_down_threshold")) {
                config.scale_down_threshold = j.at("scale_down_threshold").get<double>();
            }
            if (j.contains("min_idle_time_for_removal_ms")) {
                config.min_idle_time_for_removal =
                    std::chrono::milliseconds(j.at("min_idle_time_for_removal_ms").get<int64_t>());
            }
            if (j.contains("max_consecutive_idle_checks")) {
                config.max_consecutive_idle_checks =
                    j.at("max_consecutive_idle_checks").get<size_t>();
            }
            
            // 若未显式设置 max_threads，确保其不小于 core_threads
            if (!j.contains("max_threads") && config.max_threads < config.core_threads) {
                config.max_threads = config.core_threads;
            }
        }
    };
}
