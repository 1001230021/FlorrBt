#include "network_rcon.h"
#include "network_module.h"
#include "../Game/player.h"
#include "../server.h"
#include "../../Engine/logger.h"
#include "../../Shared/game_config.h"
#include "../../Shared/utf8.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace
{
std::string ToLowerRcon(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return text;
}

std::string TrimRcon(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return std::string(text);
}

bool ExtractRconPayload(const std::string& message, std::string& payload)
{
    if (message.empty() || message.front() != '/') return false;

    size_t command_begin = 1;
    size_t command_end = command_begin;
    while (command_end < message.size() && !std::isspace(static_cast<unsigned char>(message[command_end])))
        ++command_end;

    if (ToLowerRcon(message.substr(command_begin, command_end - command_begin)) != "rcon") return false;

    payload = TrimRcon(std::string_view(message).substr(command_end));
    return true;
}

std::string LogPriorityName(ELogPriority priority)
{
    switch (priority)
    {
    case ELogPriority::Debug:
        return "DEBUG";
    case ELogPriority::Info:
        return "INFO";
    case ELogPriority::Warning:
        return "WARN";
    case ELogPriority::Error:
        return "ERROR";
    case ELogPriority::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

void SendRconPrivateMessageImpl(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message)
{
    std::string text = message.empty() ? "(empty)" : message;
    size_t offset = 0;
    while (offset < text.size())
    {
        const std::string_view remaining(text.data() + offset, text.size() - offset);
        std::string part(TruncateUtf8(remaining, max_chat_message_size));
        offset += part.size();
        const SChatEntry chat = server.Extensions().Globals().Find<IChatService>()->Submit(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "RCON", part,
                                                            static_cast<int>(player.GetId()));
        if (!chat.message.empty()) server.Extensions().Globals().Find<IChatService>()->SendTo(player, chat);
    }
}
} // namespace

void SendRconPrivateMessage(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message)
{
    SendRconPrivateMessageImpl(server, network, player, message);
}

bool HandleRconCommand(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message)
{
    std::string payload;
    if (!ExtractRconPayload(message, payload)) return false;

    if (!player.IsAuthenticated()) return true;
    network.ValidateRconAuthorization(player);

    if (payload.empty())
    {
        SendRconPrivateMessage(server, network, player,
                                 player.IsRconAuthorized() ? "Usage: /rcon [server command]"
                                                           : "Usage: /rcon [password]");
        return true;
    }

    if (!player.IsRconAuthorized())
    {
        if (game_config::rcon_password.empty())
        {
            SendRconPrivateMessage(server, network, player, "RCON is disabled.");
            return true;
        }

        std::string auth_message;
        if (network.AuthenticateRcon(player, payload, auth_message))
        {
            LOG_INFO("rcon", "Player " + player.GetName() + " authorized from " + player.GetRemoteAddress());
            SendRconPrivateMessage(server, network, player, "RCON authorized.");
        }
        else
        {
            LOG_WARN("rcon", "Failed RCON login for player " + player.GetName() + " from " + player.GetRemoteAddress());
            SendRconPrivateMessage(server, network, player, auth_message);
        }
        return true;
    }

    if (payload == "logout")
    {
        player.SetRconAuthorized(false);
        SendRconPrivateMessage(server, network, player, "RCON logged out; debug sessions revoked.");
        return true;
    }
    if (payload == "dev_debug" || payload == "debug" || payload == "dev")
    {
        if (!network.QueueDebugTicket(player))
            SendRconPrivateMessage(server, network, player, "Unable to open dev debug; check RCON/session limits.");
        return true;
    }

    LOG_INFO("rcon", "Player " + player.GetName() + " executed: " + payload);

    struct CapturedLogs
    {
        std::mutex mutex;
        std::vector<std::string> lines;
    };
    auto captured = std::make_shared<CapturedLogs>();
    size_t sink_id =
        CLogger::AddSink([captured](const std::string& sender, ELogPriority priority, const std::string& msg) {
            std::lock_guard<std::mutex> lock(captured->mutex);
            if (captured->lines.size() < 512)
                captured->lines.push_back("[" + sender + "][" + LogPriorityName(priority) + "] " + msg);
        });

    try
    {
        server.GetConsole().ExecuteLine(payload);
    }
    catch (const std::exception& e)
    {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->lines.push_back(std::string("[rcon][ERROR] ") + e.what());
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured->lines.push_back("[rcon][ERROR] Unknown exception");
    }
    CLogger::RemoveSink(sink_id);

    std::vector<std::string> captured_lines;
    {
        std::lock_guard<std::mutex> lock(captured->mutex);
        captured_lines = captured->lines;
    }

    if (captured_lines.empty())
    {
        SendRconPrivateMessage(server, network, player, "Executed: " + payload);
    }
    else
    {
        const size_t count = std::min(captured_lines.size(), game_config::network_rcon_max_reply_lines);
        for (size_t i = 0; i < count; ++i) SendRconPrivateMessage(server, network, player, captured_lines[i]);
        if (captured_lines.size() > count)
            SendRconPrivateMessage(server, network, player,
                                     "... " + std::to_string(captured_lines.size() - count) + " more lines");
    }
    return true;
}
