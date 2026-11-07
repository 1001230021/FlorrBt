#pragma once
#include <optional>

// 账号迁移命令：
// --migrate-accounts-to-mysql [json_path]：从 JSON 导入 MySQL，再读回逐项校验。
// --migrate-accounts-to-json <json_path>：从 MySQL 导出 JSON，供回退使用。
// 配置来自 data/server.cfg，密码支持 FLORRBT_MYSQL_PASSWORD 覆盖。
// 未指定迁移模式时返回空值，否则返回执行退出码。
std::optional<int> RunAccountMigrationFromArgs(int argc, char** argv);
