#pragma once
#include "account_store.h"
#include <cstdint>
#include <string>
#include <vector>

// MySQL 账号后端支持 libmysql 或 MariaDB Connector/C。
// 配置来自 game_config::mysql_*，密码支持环境变量覆盖。
// 启动线程加载，保存线程独占连接并按上次成功快照计算差异。
// 未编译数据库支持时显式报错，不静默丢弃数据。
namespace mysql_account_backend
{
bool Available();

// 启动/迁移时连接并初始化缺失表及差异基线。
bool Initialize(std::string* error);

bool LoadAll(std::vector<SPlayerAccount>& accounts, std::uint64_t& generation, std::string* error,
             bool* unique_ledger_initialized = nullptr);

// 在单个事务中发布完整快照，跳过沙盒临时账号。
// 失败后保留重试所需的脏状态；提交回应丢失时按不确定提交处理。
bool PublishSnapshot(const std::vector<SPlayerAccount>& accounts, std::uint64_t generation, std::string* error,
                     bool unique_ledger_initialized = true);
bool PublishSnapshot(const SAccountSnapshot& accounts, std::uint64_t generation, std::string* error,
                     bool unique_ledger_initialized = true);

void ResetPublishState();
}
