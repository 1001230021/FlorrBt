#pragma once
#include "../Shared/shared.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

inline auto& GetGlobalCommandRegistry()
{
    static std::unordered_map<std::string, std::function<void(const std::vector<std::string>&)>> g_registry;
    return g_registry;
}

#define REGISTER_CONSOLE_COMMAND(name, body)                                                                           \
    static struct CmdReg_##name                                                                                        \
    {                                                                                                                  \
        CmdReg_##name()                                                                                                \
        {                                                                                                              \
            GetGlobalCommandRegistry()[#name] = [](const std::vector<std::string>& args) body;                         \
        }                                                                                                              \
    } CmdReg_##name##_instance;