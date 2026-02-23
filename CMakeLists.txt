cmake_minimum_required(VERSION 3.10)
project(ThreadPoolDemo)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 查找pthread库
find_package(Threads REQUIRED)

# 查找nlohmann/json库
find_package(nlohmann_json REQUIRED)

# 查找spdlog库
find_package(spdlog REQUIRED)

# 创建线程池库
add_library(thread_pool_lib
    src/thread_pool_improved.cpp
    src/thread_pool_config.cpp
    src/logger.cpp
)

target_link_libraries(thread_pool_lib 
    Threads::Threads
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# 设置头文件目录
target_include_directories(thread_pool_lib PUBLIC include)

# # 创建日志测试程序
# add_executable(test_logger test_logger.cpp)
# target_link_libraries(test_logger thread_pool_lib)

# 添加测试子目录
add_subdirectory(test)

add_subdirectory(benchmark)


