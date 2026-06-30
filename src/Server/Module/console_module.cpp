#include "console_module.h"
#include "../../Engine/commands_registry.h"

bool IConsoleModule::Init()
{
    for (const auto& [name, cb] : GetGlobalCommandRegistry())
        m_Console.RegisterCommand(name, cb);
    return true;
}

void IConsoleModule::Tick(float dt)
{
    std::string input;
    std::getline(std::cin, input);
    if (!input.empty())
        m_Console.ExecuteLine(input);
}

void IConsoleModule::ShutDown() {}