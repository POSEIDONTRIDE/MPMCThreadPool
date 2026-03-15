# MPMCThreadPool

基于 C++11 实现的高性能线程池框架，支持动态线程管理、多种队列满策略与完整生命周期控制。

## 性能数据

| 测试环境 | 峰值吞吐 | 平均延迟 | 队列峰值使用率 |
|---------|---------|---------|------------|
| 16 线程 | ~55 万任务/秒 | ~1.8 微秒 | 100% |

> 优化前吞吐约 4 万任务/秒，定位缓存行伪共享问题后采用分片计数方案，提升约 13×。

## 核心特性

- **动态线程管理**：根据队列负载因子（0.8 扩容 / 0.3 缩容）自动伸缩线程数，滞后阈值避免频繁抖动
- **MPMC 阻塞队列**：基于循环数组实现，两个独立 condition_variable 避免混淆唤醒
- **三种队列满策略**：BLOCK（关键任务）/ OVERWRITE（实时数据流）/ DISCARD（可选任务）
- **完整生命周期**：支持启动 / 暂停 / 恢复 / 优雅关闭 / 强制停止五种状态
- **工程化实践**：JSON 配置管理、spdlog 日志、GTest 单元测试、CMake 构建

## 项目结构
```
├── include/        # 头文件
├── src/            # 源文件
├── test/           # 单元测试
├── benchmark/      # 压测工具
├── config/         # JSON 配置文件
├── scripts/        # 构建脚本
└── CMakeLists.txt
```

## 快速开始
```bash
# 构建
cmake -B build
cmake --build build

# 运行单元测试
./build/test/thread_pool_test
./build/test/circular_queue_test
./build/test/thread_pool_config_test

# 运行压测
./build/bin/thread_pool_benchmark
```

## 技术栈

C++11/14 · std::thread · condition_variable · atomic · Future/Promise · nlohmann/json · spdlog · GTest · CMake
