#pragma once
#include "logger.h"
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CConsole
{
  public:
    using CallBack = std::function<void(const std::vector<std::string>&)>;

    CConsole() = default;
    ~CConsole() = default;

    void RegisterCommand(std::string name, CallBack callback, std::string usage = {},
                         std::string completion_schema = {});
    void ExecuteLine(std::string line);
    void InstallCommands();
    std::vector<std::string> CommandNames() const;
    std::optional<std::string_view> CommandUsage(std::string_view name) const;
    std::optional<std::string_view> CommandCompletionSchema(std::string_view name) const;

  private:
    struct SCommand
    {
        CallBack callback;
        std::string usage;
        std::string completion_schema;
    };

    std::unordered_map<std::string, SCommand> m_cmds;
    bool m_commands_installed = false;
};
