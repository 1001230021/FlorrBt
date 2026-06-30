#pragma once
#include "console.h"
#include "logger.h"

namespace
{
std::vector<std::string> SplitString(const std::string& str)
{
    std::vector<std::string> args;
    std::string current;
    bool inQuotes = false;

    for (char ch : str)
    {
        if (ch == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (std::isspace(ch) && !inQuotes)
        {
            if (!current.empty())
            {
                args.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += ch;
        }
    }
    if (!current.empty())
    {
        args.push_back(current);
    }
    return args;
}
} // namespace

void CConsole::RegisterCommand(std::string name, CallBack cb)
{
    auto [it, inserted] = m_Cmds.insert({ name, cb });
    if (!inserted)
    {
        it->second = std::move(cb);
        LOG_WARN("console", "The command " + name + " already exists.");
    }
}

void CConsole::ExecuteLine(std::string line)
{
    if (line.empty())
        return;

    std::vector<std::string> vs = SplitString(line);
    if (vs.empty())
        return;

    std::string func_name = vs[0];
    std::vector<std::string> args(vs.begin() + 1, vs.end());

    auto it = m_Cmds.find(func_name);
    if (it != m_Cmds.end())
        it->second(args);
    else
        LOG_WARN("console", "Unknown command " + func_name + ".");
}
