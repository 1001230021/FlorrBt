#include "console_module.h"
#include "../../Engine/commands_registry.h"
#include <iostream>

#ifdef _WIN32
#include <conio.h>
#endif

bool IConsoleModule::Init()
{
    for (const auto& [name, callback] : GetGlobalCommandRegistry())
    {
        m_console.RegisterCommand(name, callback);
    }
    return true;
}

void IConsoleModule::Tick(float)
{
    std::string input;
    while (TryReadLine(input))
    {
        if (!input.empty()) m_console.ExecuteLine(input);
    }
}

void IConsoleModule::ShutDown() {}

bool IConsoleModule::TryReadLine(std::string& line)
{
#ifdef _WIN32
    if (!_kbhit()) return false;

    while (_kbhit())
    {
        int ch = _getch();
        if (ch == '\r' || ch == '\n')
        {
            std::cout << std::endl;
            line = std::move(m_pending_line);
            m_pending_line.clear();
            return true;
        }
        if (ch == '\b')
        {
            if (!m_pending_line.empty()) m_pending_line.pop_back();
            continue;
        }
        if (ch >= 32 && ch <= 126)
        {
            m_pending_line.push_back(static_cast<char>(ch));
            std::cout << static_cast<char>(ch) << std::flush;
        }
    }
    return false;
#else
    if (std::cin.rdbuf()->in_avail() <= 0) return false;
    return static_cast<bool>(std::getline(std::cin, line));
#endif
}