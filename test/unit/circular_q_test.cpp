/*
CircularQueue 单元测试
使用 Google Test 框架验证循环队列的各项功能是否符合预期，
包括构造、入队出队、满队列覆盖、索引访问、异常处理等行为。
*/

#include "circular_queue.h"
#include <gtest/gtest.h>
#include <string>
#include <stdexcept>

using namespace thread_pool_improved;

// 测试基类：为每个 TEST_F 提供统一的初始化和清理入口
// SetUp 和 TearDown 会在【每一个测试用例】前后自动执行
class CircularQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        // SetUp 会在每个 TEST_F 执行前被自动调用
        // 通常用于初始化测试环境或公共数据
        // 当前测试不需要额外初始化，因此这里留空
    }
    
    void TearDown() override {
        // TearDown 会在每个 TEST_F 执行结束后被自动调用
        // 通常用于释放资源或清理状态
        // 当前测试没有需要清理的内容
    }
};

// 测试构造函数的基本行为
TEST_F(CircularQueueTest, Constructors) {
    // 默认构造：队列应为空，容量为 0
    CircularQueue<int> q1;
    EXPECT_TRUE(q1.Empty());
    EXPECT_EQ(q1.Size(), 0);
    EXPECT_EQ(q1.Capacity(), 0);
    
    // 指定容量构造：初始应为空，容量应等于传入值
    CircularQueue<int> q2(5);
    EXPECT_TRUE(q2.Empty());
    EXPECT_EQ(q2.Size(), 0);
    EXPECT_EQ(q2.Capacity(), 5);
    EXPECT_FALSE(q2.Full());
    
    // 容量为 0 的边界情况：队列始终为空
    CircularQueue<int> q3(0);
    EXPECT_TRUE(q3.Empty());
    EXPECT_EQ(q3.Capacity(), 0);
}

// 测试基本的入队和出队操作
TEST_F(CircularQueueTest, BasicOperations) {
    CircularQueue<int> q(3);
    
    // 入队后：队列不为空，Size 增加，Front 指向最早插入的元素
    q.PushBack(10);
    EXPECT_FALSE(q.Empty());
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front(), 10);
    
    // 填满队列
    q.PushBack(20);
    q.PushBack(30);
    EXPECT_EQ(q.Size(), 3);
    EXPECT_TRUE(q.Full());
    
    // 出队：移除队首元素，Front 前移
    q.PopFront();
    EXPECT_EQ(q.Size(), 2);
    EXPECT_EQ(q.Front(), 20);
    EXPECT_FALSE(q.Full());
    
    q.PopFront();
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front(), 30);
    
    // 出队到空
    q.PopFront();
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0);
}

// 测试满队列情况下的覆盖（Overrun）行为
TEST_F(CircularQueueTest, OverrunBehavior) {
    CircularQueue<int> q(3);
    
    // 填满队列
    q.PushBack(1);
    q.PushBack(2);
    q.PushBack(3);
    EXPECT_TRUE(q.Full());
    EXPECT_EQ(q.OverrunCounter(), 0);
    
    // 继续入队：应覆盖最早的元素
    q.PushBack(4);  // 覆盖 1
    EXPECT_EQ(q.OverrunCounter(), 1);
    EXPECT_EQ(q.Front(), 2);
    EXPECT_EQ(q.Size(), 3);
    
    q.PushBack(5);  // 覆盖 2
    EXPECT_EQ(q.OverrunCounter(), 2);
    EXPECT_EQ(q.Front(), 3);
    
    // 重置溢出计数器
    q.ResetOverrunCounter();
    EXPECT_EQ(q.OverrunCounter(), 0);
}

// 测试异常情况和非法访问
TEST_F(CircularQueueTest, ExceptionHandling) {
    CircularQueue<int> q(2);
    
    // 空队列访问：应抛出运行时异常
    EXPECT_THROW(q.Front(), std::runtime_error);
    EXPECT_THROW(q.PopFront(), std::runtime_error);
    
    q.PushBack(1);
    
    // At 方法是带边界检查的访问，越界应抛出异常
    EXPECT_THROW(q.At(1), std::out_of_range);
    
    // 合法访问不应抛异常，且返回值正确
    EXPECT_NO_THROW({
        int val = q.Front();
        int val2 = q.At(0);
        EXPECT_EQ(val, 1);
        EXPECT_EQ(val2, 1);
    });
}

// 测试索引访问功能
TEST_F(CircularQueueTest, IndexAccess) {
    CircularQueue<int> q(5);
    
    // 插入 3 个元素
    for (int i = 0; i < 3; ++i) {
        q.PushBack(i * 10);
    }
    
    // 使用 At 访问（逻辑顺序）
    EXPECT_EQ(q.At(0), 0);
    EXPECT_EQ(q.At(1), 10);
    EXPECT_EQ(q.At(2), 20);
    
    // 使用 operator[] 访问
    EXPECT_EQ(q[0], 0);
    EXPECT_EQ(q[1], 10);
    EXPECT_EQ(q[2], 20);
    
    // 测试可写访问
    q.At(1) = 15;
    EXPECT_EQ(q.At(1), 15);
    
    q[2] = 25;
    EXPECT_EQ(q[2], 25);
}

// 测试 Clear 功能
TEST_F(CircularQueueTest, ClearFunction) {
    CircularQueue<int> q(3);
    
    q.PushBack(1);
    q.PushBack(2);
    q.PushBack(3);
    q.PushBack(4);  // 触发覆盖
    
    EXPECT_EQ(q.Size(), 3);
    EXPECT_EQ(q.OverrunCounter(), 1);
    
    // 清空队列
    q.Clear();
    
    EXPECT_TRUE(q.Empty());
    EXPECT_EQ(q.Size(), 0);
    EXPECT_EQ(q.OverrunCounter(), 0);
    EXPECT_FALSE(q.Full());
}

// 测试移动语义相关行为
TEST_F(CircularQueueTest, MoveSemantics) {
    CircularQueue<std::string> q(3);
    
    // 移动插入：原字符串应处于被移动后的状态
    std::string str1 = "hello";
    q.PushBack(std::move(str1));
    EXPECT_TRUE(str1.empty());
    
    // 移动构造
    CircularQueue<std::string> q2(std::move(q));
    EXPECT_EQ(q2.Size(), 1);
    EXPECT_EQ(q2.Front(), "hello");
    EXPECT_TRUE(q.Empty());
    
    // 移动赋值
    CircularQueue<std::string> q3;
    q3 = std::move(q2);
    EXPECT_EQ(q3.Size(), 1);
    EXPECT_EQ(q3.Front(), "hello");
    EXPECT_TRUE(q2.Empty());
}

// 测试 EmplaceBack 原地构造功能
TEST_F(CircularQueueTest, EmplaceBack) {
    CircularQueue<std::string> q(3);
    
    // 使用不同的构造参数原地构造 string
    q.EmplaceBack("hello", 5);
    EXPECT_EQ(q.Size(), 1);
    EXPECT_EQ(q.Front(), "hello");
    
    q.EmplaceBack(3, 'x');
    EXPECT_EQ(q.Size(), 2);
    EXPECT_EQ(q.At(1), "xxx");
}

// 测试边界情况
TEST_F(CircularQueueTest, EdgeCases) {
    // 容量为 1 的队列
    CircularQueue<int> q1(1);
    q1.PushBack(10);
    EXPECT_TRUE(q1.Full());
    EXPECT_EQ(q1.Size(), 1);
    
    q1.PushBack(20);  // 覆盖 10
    EXPECT_EQ(q1.Front(), 20);
    EXPECT_EQ(q1.OverrunCounter(), 1);
    
    // 容量为 0 的队列
    CircularQueue<int> q2(0);
    q2.PushBack(10);  // 应被忽略
    EXPECT_TRUE(q2.Empty());
    EXPECT_EQ(q2.Size(), 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
