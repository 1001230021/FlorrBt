#include "console.h"
#include "commands_registry.h"
#include "logger.h"
#include <cctype>

namespace
{
std::vector<std::string> SplitString(const std::string& str)
{
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;

    for (char ch : str)
    {
        if (ch == '"')
        {
            in_quotes = !in_quotes;
        } else if (std::isspace(static_cast<unsigned char>(ch)) && !in_quotes) {
            if (!current.empty())
            {
                args.push_back(current);
                current.clear();
            }
        } else {
            current += ch;
        }
    }

    if (!current.empty()) args.push_back(current);
    return args;
}
}

void CConsole::RegisterCommand(std::string name, CallBack callback)
{
    auto [it, inserted] = m_cmds.insert({name, callback});
    if (!inserted)
    {
        it->second = std::move(callback);
        LOG_WARN("console", "The command " + name + " already exists.");
    }
}

void CConsole::ExecuteLine(std::string line)
{
    if (line.empty()) return;

    std::vector<std::string> tokens = SplitString(line);
    if (tokens.empty()) return;

    std::string func_name = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = m_cmds.find(func_name);
    if (it != m_cmds.end()) it->second(args);
    else LOG_WARN("console", "Unknown command " + func_name + ".");
}

void CConsole::InstallCommands()
{
    for (const auto& [name, callback] : GetGlobalCommandRegistry())
    {
        RegisterCommand(name, callback);
    }
}
