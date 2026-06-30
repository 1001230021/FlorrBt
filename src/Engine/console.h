#pragma once
#include "logger.h"
#include <functional>
#include <iostream>
#include <map>
#include <string>

class CConsole
{
  private:
    using CallBack = std::function<void(const std::vector<std::string>&)>;
    std::unordered_map<std::string, CallBack> m_Cmds;

  public:
    void RegisterCommand(std::string name, CallBack cb);
    void ExecuteLine(std::string line);
    void InstallCommands();

    CConsole() = default;
    ~CConsole() = default;
};
