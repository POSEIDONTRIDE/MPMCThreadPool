#include "thread_pool_benchmark.h"
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
    // 从配置文件加载配置
    std::string config_path = "/usr/team_project/thread_pool/config/thread_pool.json";
    if (argc > 1 && std::string(argv[1]) == "--config") {
        if (argc > 2) {
            config_path = argv[2];
        }
    }
    
    benchmark::BenchmarkConfig config = benchmark::BenchmarkConfig::LoadFromFile(config_path);
    
    // 解析命令行参数（可覆盖配置文件）
    int arg_idx = 1;
    if (argc > arg_idx && std::string(argv[arg_idx]) == "--config") {
        arg_idx += 2; // 跳过 --config 和配置文件路径
    }
    
    if (argc > arg_idx) {
        config.core_threads = std::stoul(argv[arg_idx]);
        arg_idx++;
    }
    if (argc > arg_idx) {
        config.duration_seconds = std::stoul(argv[arg_idx]);
        arg_idx++;
    }
    if (argc > arg_idx) {
        std::string mode = argv[arg_idx];
        if (mode == "tasks") {
            config.use_duration_mode = false;
            arg_idx++;
            if (argc > arg_idx) {
                config.total_tasks = std::stoul(argv[arg_idx]);
            }
        }
    }
    
    if (config.enable_console_output) {
        std::cout << "=== 线程池吞吐量压测工具 ===" << std::endl;
        std::cout << "使用方法: " << argv[0] << " [--config 配置文件路径] [核心线程数] [测试时间秒] [模式(time|tasks)] [任务数(仅tasks模式)]" << std::endl;
        std::cout << "当前配置:" << std::endl;
        std::cout << "  核心线程数: " << config.core_threads << std::endl;
        std::cout << "  最大线程数: " << config.max_threads << std::endl;
        std::cout << "  队列大小: " << config.max_queue_size << std::endl;
        std::cout << "  日志记录: " << (config.enable_logging ? "启用" : "禁用") << std::endl;
        std::cout << "  控制台输出: " << (config.enable_console_output ? "启用" : "禁用") << std::endl;
        std::cout << "  实时监控: " << (config.enable_real_time_monitoring ? "启用" : "禁用") << std::endl;
    }
    
    if (config.enable_console_output) {
        if (config.use_duration_mode) {
            std::cout << "  测试模式: 按时间测试" << std::endl;
            std::cout << "  测试时间: " << config.duration_seconds << " 秒" << std::endl;
            std::cout << "  预热时间: " << config.warmup_seconds << " 秒" << std::endl;
        } else {
            std::cout << "  测试模式: 按任务数测试" << std::endl;
            std::cout << "  任务总数: " << config.total_tasks << std::endl;
        }
        
        std::cout << std::endl;
    }
    
    try {
        // 创建压测对象
        benchmark::ThreadPoolBenchmark benchmark_test(config);
        
        // 运行压测
        auto result = benchmark_test.RunBenchmark();
        
        // 打印结果
        benchmark_test.PrintResult(result);
        
        // 运行多组对比测试 - 寻找最大吞吐量配置
        if (config.use_duration_mode && argc <= 2) {
            if (config.enable_console_output) {
                std::cout << "\n=== 线程池性能调优测试 ===" << std::endl;
                std::cout << "目标: 找到最大吞吐量的线程池配置" << std::endl;
            }
            
            // 更全面的线程数测试范围
            std::vector<size_t> thread_counts;
            size_t max_threads = std::thread::hardware_concurrency() * 3; // 扩大测试范围
            for (size_t i = 1; i <= max_threads; i *= 2) {
                thread_counts.push_back(i);
            }
            // 添加一些中间值
            if (std::thread::hardware_concurrency() >= 4) {
                thread_counts.push_back(std::thread::hardware_concurrency());
                thread_counts.push_back(std::thread::hardware_concurrency() + 2);
            }
            
            std::vector<benchmark::BenchmarkResult> results;
            double max_throughput = 0.0;
            size_t best_core_threads = 1;
            size_t best_max_threads = 2;
            
            for (size_t thread_count : thread_counts) {
                if (config.enable_console_output) {
                    std::cout << "\n--- 测试配置: 核心线程=" << thread_count 
                             << ", 最大线程=" << thread_count * 2 << " ---" << std::endl;
                }
                
                benchmark::BenchmarkConfig test_config = config;
                test_config.core_threads = thread_count;
                test_config.max_threads = thread_count * 2;
                test_config.duration_seconds = 20; // 增加测试时间获得更准确结果
                test_config.warmup_seconds = 5;    // 充分预热
                test_config.enable_real_time_monitoring = false; // 关闭监控减少干扰
                test_config.enable_console_output = false;       // 测试期间不输出详细信息
                
                benchmark::ThreadPoolBenchmark test(test_config);
                auto test_result = test.RunBenchmark();
                results.push_back(test_result);
                
                if (config.enable_console_output) {
                    std::cout << "吞吐量: " << std::fixed << std::setprecision(2) 
                             << test_result.throughput_per_second << " 任务/秒" << std::endl;
                }
                
                // 记录最佳配置
                if (test_result.throughput_per_second > max_throughput) {
                    max_throughput = test_result.throughput_per_second;
                    best_core_threads = thread_count;
                    best_max_threads = thread_count * 2;
                }
            }
            
            // 输出最终结果
            if (config.enable_console_output) {
                std::cout << "\n" << std::string(50, '=') << std::endl;
                std::cout << "🎯 线程池最大吞吐量测试结果" << std::endl;
                std::cout << std::string(50, '=') << std::endl;
                std::cout << "最大吞吐量: " << std::fixed << std::setprecision(2) 
                         << max_throughput << " 任务/秒" << std::endl;
                std::cout << "最佳配置: 核心线程=" << best_core_threads 
                         << ", 最大线程=" << best_max_threads << std::endl;
                std::cout << "建议: 在配置文件中设置 core_threads=" << best_core_threads 
                         << ", max_threads=" << best_max_threads << std::endl;
                std::cout << std::string(50, '=') << std::endl;
            }
            
            // 记录到日志文件
            if (config.enable_logging) {
                // 这里需要临时初始化日志系统
                benchmark::ThreadPoolBenchmark temp_benchmark(config);
                // 日志记录会在temp_benchmark构造时初始化
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "压测过程中发生错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
