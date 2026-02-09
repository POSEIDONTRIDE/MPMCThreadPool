#!/bin/bash

# Thread Pool 项目部署脚本
# 用于将本地代码同步到云服务器

# 配置信息
SERVER_IP="124.221.19.77"
SERVER_USER="root"
SERVER_PATH="/usr/team_project/thread_pool"
LOCAL_PATH="/Users/chef/Documents/team_project/thread_pool"

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Thread Pool 项目部署脚本 ===${NC}"
echo -e "${YELLOW}目标服务器: ${SERVER_USER}@${SERVER_IP}${NC}"
echo -e "${YELLOW}目标路径: ${SERVER_PATH}${NC}"
echo ""

# 检查本地路径是否存在
if [ ! -d "$LOCAL_PATH" ]; then
    echo -e "${RED}错误: 本地路径不存在: $LOCAL_PATH${NC}"
    exit 1
fi

# 开始同步
echo -e "${GREEN}开始同步文件...${NC}"

# 使用 rsync 同步文件
# -a: 归档模式，保持文件属性
# -v: 详细输出
# -z: 压缩传输
# --progress: 显示进度
# --delete: 删除目标目录中源目录没有的文件
# --exclude: 排除不需要的文件和目录
# --checksum: 使用校验和而不是时间戳来判断文件是否需要更新
rsync -avz --progress --delete --checksum \
    --exclude '.git/' \
    --exclude '.gitignore' \
    --exclude 'build/' \
    --exclude '*.o' \
    --exclude '*.so' \
    --exclude '*.a' \
    --exclude '*.exe' \
    --exclude '.DS_Store' \
    --exclude '*.tmp' \
    --exclude '*.log' \
    --exclude 'deploy.sh' \
    --exclude '.vscode/' \
    "$LOCAL_PATH/" "${SERVER_USER}@${SERVER_IP}:${SERVER_PATH}/"

# 检查同步结果
if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}✅ 文件同步成功完成！${NC}"

    # 验证远程文件
    echo -e "${YELLOW}正在验证远程文件...${NC}"
    ssh "${SERVER_USER}@${SERVER_IP}" "ls -la ${SERVER_PATH}/ | head -10"

    echo ""
    echo -e "${YELLOW}您可以登录服务器查看文件：${NC}"
    echo -e "${YELLOW}ssh ${SERVER_USER}@${SERVER_IP}${NC}"
    echo -e "${YELLOW}cd ${SERVER_PATH}${NC}"
else
    echo ""
    echo -e "${RED}❌ 文件同步失败！${NC}"
    echo -e "${RED}请检查网络连接和服务器配置${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}部署脚本执行完毕${NC}"