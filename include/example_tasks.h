#pragma once
#include "thread_pool_improved.h"
#include <iostream>
#include <string>
#include <chrono>
#include <random>
using namespace thread_pool_improved;


// 目前没用上，大家可以自己写一个 demo 测试一下
class CalculationTask : public TaskBase {
public:
    CalculationTask(int start, int end, const std::string& name)
        : start_(start), end_(end), name_(name) {}
    
    void Execute() override {
        std::cout << "开始执行计算任务: " << name_ << std::endl;
        
        long long sum = 0;
        for (int i = start_; i <= end_; ++i) {
            sum += i;
        }
        
        // 模拟计算耗时
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::cout << "任务 " << name_ << " 完成，计算结果: " << sum << std::endl;
    }

private:
    int start_, end_;
    std::string name_;
};

class FileProcessTask : public TaskBase {
public:
    FileProcessTask(const std::string& filename, const std::string& operation)
        : filename_(filename), operation_(operation) {}
    
    void Execute() override {
        std::cout << "开始处理文件: " << filename_ << "，操作: " << operation_ << std::endl;
        
        // 模拟文件处理时间
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(50, 200);
        std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
        
        std::cout << "文件 " << filename_ << " 的 " << operation_ << " 操作完成" << std::endl;
    }

private:
    std::string filename_;
    std::string operation_;
};

class NetworkTask : public TaskBase {
public:
    NetworkTask(const std::string& url, int timeout_ms)
        : url_(url), timeout_ms_(timeout_ms) {}
    
    void Execute() override {
        std::cout << "开始网络请求: " << url_ << std::endl;
        
        // 模拟网络请求
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms_));
        
        // 模拟成功/失败
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10);
        
        if (dis(gen) <= 8) {  // 80% 成功率
            std::cout << "网络请求成功: " << url_ << std::endl;
        } else {
            std::cout << "网络请求失败: " << url_ << std::endl;
        }
    }

private:
    std::string url_;
    int timeout_ms_;
};

class StatefulTask : public TaskBase {
public:
    StatefulTask(int task_id, std::shared_ptr<std::atomic<int>> counter)
        : task_id_(task_id), global_counter_(counter) {}
    
    void Execute() override {
        std::cout << "任务 " << task_id_ << " 开始执行" << std::endl;
        
        // 模拟工作
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // 原子操作更新全局计数器
        int current_count = global_counter_->fetch_add(1) + 1;
        
        std::cout << "任务 " << task_id_ << " 完成，全局计数: " << current_count << std::endl;
    }

private:
    int task_id_;
    std::shared_ptr<std::atomic<int>> global_counter_;
};
