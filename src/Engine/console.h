#pragma once
#include "logger.h"
#include <functional>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

class CConsole
{
  public:
    using CallBack = std::function<void(const std::vector<std::string>&)>;

    CConsole() = default;
    ~CConsole() = default;

    void RegisterCommand(std::string name, CallBack callback);
    void ExecuteLine(std::string line);
    void InstallCommands();
    std::vector<std::string> CommandNames() const;

  private:
    std::unordered_map<std::string, CallBack> m_cmds;
    bool m_commands_installed = false;
};
