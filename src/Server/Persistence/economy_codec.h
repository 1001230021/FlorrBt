#pragma once
#include "../../Shared/economy.h"
#include <iosfwd>
#include <string>

class CJsonValue;

// 账号 JSON、MySQL 载荷与隔离进度快照共用编解码。
bool ReadEconomyState(const CJsonValue* value, SAccountEconomyState& state, std::string* error);
void WriteEconomyState(std::ostream& output, const SAccountEconomyState& state);
std::string EconomyStateJson(const SAccountEconomyState& state);
