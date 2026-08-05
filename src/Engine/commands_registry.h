#pragma once
#include "../Shared/shared.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

using ConsoleCommandCallback = std::function<void(const std::vector<std::string>&)>;

struct SConsoleCommandRegistration
{
    ConsoleCommandCallback callback;
    std::string usage;
    std::string completion_schema;
};

inline auto& GetGlobalCommandRegistry()
{
    static std::unordered_map<std::string, SConsoleCommandRegistration> g_registry;
    return g_registry;
}

#define REGISTER_CONSOLE_COMMAND(name, ...)                                                                            \
    static struct CmdReg_##name                                                                                        \
    {                                                                                                                  \
        CmdReg_##name()                                                                                                \
        {                                                                                                              \
            GetGlobalCommandRegistry()[#name] = { [](const std::vector<std::string>& args) __VA_ARGS__, {}, {} };      \
        }                                                                                                              \
    } CmdReg_##name##_instance;

#define REGISTER_CONSOLE_COMMAND_USAGE(name, usage, completion_schema, ...)                                            \
    static struct CmdReg_##name                                                                                        \
    {                                                                                                                  \
        CmdReg_##name()                                                                                                \
        {                                                                                                              \
            GetGlobalCommandRegistry()[#name] = { [](const std::vector<std::string>& args) __VA_ARGS__,                \
                                                  usage,                                                               \
                                                  completion_schema };                                                 \
        }                                                                                                              \
    } CmdReg_##name##_instance;
