#!/bin/bash

# 线程池压测工具构建脚本

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}    线程池压测工具构建脚本${NC}"
echo -e "${GREEN}========================================${NC}"

# 检查是否在正确的目录
if [ ! -f "CMakeLists.txt" ]; then
    echo -e "${RED}错误: 请在benchmark目录下运行此脚本${NC}"
    exit 1
fi

# 创建构建目录
echo -e "${YELLOW}创建构建目录...${NC}"
mkdir -p build
cd build

# 运行CMake配置
echo -e "${YELLOW}运行CMake配置...${NC}"
if ! cmake .. -DCMAKE_TOOLCHAIN_FILE=/root/.vcpkg/scripts/buildsystems/vcpkg.cmake; then
    echo -e "${RED}CMake配置失败${NC}"
    exit 1
fi



# 编译项目
echo -e "${YELLOW}编译项目...${NC}"
if ! make -j$(nproc); then
    echo -e "${RED}编译失败${NC}"
    exit 1
fi

# 检查可执行文件是否生成
if [ -f "bin/thread_pool_benchmark" ]; then
    echo -e "${GREEN}构建成功！${NC}"
    echo -e "${GREEN}可执行文件位置: build/bin/thread_pool_benchmark${NC}"
    echo ""
    echo -e "${YELLOW}使用示例:${NC}"
    echo "  cd build/bin"
    echo "  ./thread_pool_benchmark --help"
    echo "  ./thread_pool_benchmark quick"
    echo "  ./thread_pool_benchmark stress_test --output my_report.txt"
    echo ""
else
    echo -e "${RED}构建失败：可执行文件未生成${NC}"
    exit 1
fi

echo -e "${GREEN}构建完成！${NC}"