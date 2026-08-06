#include "console.h"
#include "commands_registry.h"
#include "logger.h"
#include <algorithm>
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
        } else if (std::isspace(static_cast<unsigned char>(ch)) && !in_quotes)
        {
            if (!current.empty())
            {
                args.push_back(current);
                current.clear();
            }
        } else
        {
            current += ch;
        }
    }

    if (!current.empty()) args.push_back(current);
    return args;
}
} // namespace

void CConsole::RegisterCommand(std::string name, CallBack callback, std::string usage, std::string completion_schema)
{
    auto it = m_cmds.find(name);
    if (it != m_cmds.end())
    {
        it->second = { std::move(callback), std::move(usage), std::move(completion_schema) };
        LOG_WARN("console", "The command " + name + " already exists.");
        return;
    }
    m_cmds.emplace(std::move(name), SCommand{ std::move(callback), std::move(usage), std::move(completion_schema) });
}

void CConsole::ExecuteLine(std::string line)
{
    if (line.empty()) return;

    std::vector<std::string> tokens = SplitString(line);
    if (tokens.empty()) return;

    std::string func_name = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    auto it = m_cmds.find(func_name);
    if (it != m_cmds.end()) it->second.callback(args);
    else LOG_WARN("console", "Unknown command " + func_name + ".");
}

void CConsole::InstallCommands()
{
    if (m_commands_installed) return;
    m_commands_installed = true;

    for (const auto& [name, registration] : GetGlobalCommandRegistry())
    {
        RegisterCommand(name, registration.callback, registration.usage, registration.completion_schema);
    }

    RegisterCommand(
        "help",
        [this](const std::vector<std::string>& args) {
            if (!args.empty())
            {
                auto it = m_cmds.find(args[0]);
                if (it == m_cmds.end())
                {
                    LOG_WARN("console", "Unknown command " + args[0] + ".");
                    return;
                }
                LOG_INFO("console", it->second.usage.empty() ? args[0] : "Usage: " + it->second.usage);
                return;
            }

            std::string text = "Commands:";
            for (const std::string& name : CommandNames())
            {
                text += " ";
                text += name;
            }
            LOG_INFO("console", text);
        },
        "help [command]", "command");
}

std::vector<std::string> CConsole::CommandNames() const
{
    std::vector<std::string> names;
    names.reserve(m_cmds.size());
    for (const auto& [name, command] : m_cmds)
    {
        (void)command;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::optional<std::string_view> CConsole::CommandUsage(std::string_view name) const
{
    auto it = m_cmds.find(std::string(name));
    if (it == m_cmds.end() || it->second.usage.empty()) return std::nullopt;
    return it->second.usage;
}

std::optional<std::string_view> CConsole::CommandCompletionSchema(std::string_view name) const
{
    auto it = m_cmds.find(std::string(name));
    if (it == m_cmds.end() || it->second.completion_schema.empty()) return std::nullopt;
    return it->second.completion_schema;
}
