cmake_minimum_required(VERSION 3.16)
project(ThreadPoolTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Google Test
find_package(GTest REQUIRED)

# 包含头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 创建测试可执行文件
add_executable(circular_queue_test 
    unit/circular_q_test.cpp
)

add_executable(thread_pool_test
    unit/thread_pool_test.cpp
)

add_executable(thread_pool_config_test
    unit/thread_pool_config_test.cpp
)

# 链接必要的库
target_link_libraries(circular_queue_test 
    GTest::gtest
    GTest::gtest_main
    pthread
)

target_link_libraries(thread_pool_test
    GTest::gtest
    GTest::gtest_main
    pthread
    thread_pool_lib
)

target_link_libraries(thread_pool_config_test
    GTest::gtest
    GTest::gtest_main
    pthread
    thread_pool_lib
)

# 启用测试
enable_testing()
add_test(NAME CircularQueueTests COMMAND circular_queue_test)
add_test(NAME ThreadPoolTests COMMAND thread_pool_test)
add_test(NAME ThreadPoolConfigTests COMMAND thread_pool_config_test)

# 添加编译选项
target_compile_options(circular_queue_test PRIVATE 
    -Wall -Wextra -Wpedantic
)

target_compile_options(thread_pool_test PRIVATE 
    -Wall -Wextra -Wpedantic
)

target_compile_options(thread_pool_config_test PRIVATE 
    -Wall -Wextra -Wpedantic
)