#include "report.h"
#include "../Engine/json_value.h"
#include "../Engine/logger.h"
#include "../Shared/game_config.h"
#include "Game/entity.h"
#include "Game/gamecontext.h"
#include "Game/player.h"
#include "Module/network_module.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string_view>
#include <thread>
#include <atomic>

namespace
{
std::string ToLower(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text) result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

std::vector<std::string> SplitCommandLine(const std::string& line)
{
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    for (char ch : line)
    {
        if (ch == '"')
            in_quotes = !in_quotes;
        else if (std::isspace(static_cast<unsigned char>(ch)) && !in_quotes)
        {
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

std::string JoinArgs(const std::vector<std::string>& args, size_t begin)
{
    std::string result;
    for (size_t i = begin; i < args.size(); ++i)
    {
        if (!result.empty()) result += " ";
        result += args[i];
    }
    return result;
}

CPlayer* FindPlayerByName(CGameContext* context, std::string_view name)
{
    if (!context) return nullptr;
    std::string target = ToLower(name);
    for (const auto& player : context->Players())
    {
        if (!player) continue;
        if (ToLower(player->GetName()) == target || ToLower(player->GetAccountName()) == target) return player.get();
    }
    return nullptr;
}

uint32_t NowSeconds()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

const std::vector<std::string>& ReportKeywords()
{
    static const std::vector<std::string> keywords = {
        "fuck",
        "shit",
        "nigger",
        "retard",
        "\xE5\x9E\x83\xE5\x9C\xBE",
        "\xE5\x82\xBB\xE9\x80\xBC",
        "\xE6\x93\x8D\xE4\xBD\xA0",
        "\xE5\xA6\x88\xE7\x9A\x84",
        "\xE6\xAD\xBB\xE5\x85\xA8\xE5\xAE\xB6",
        "sz",
        "lz",
        "wcnm",
        "nmsl",
        "sb",
    };
    return keywords;
}

std::string EscapeJsonString(const std::string& text)
{
    std::string result;
    result.reserve(text.size() + 8);
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20)
            {
                constexpr char hex[] = "0123456789ABCDEF";
                result += "\\u00";
                result += hex[ch >> 4];
                result += hex[ch & 15];
            }
            else
            {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return result;
}

std::string ShellQuote(const std::filesystem::path& path)
{
    std::string text = path.string();
    std::string result = "\"";
    for (char ch : text)
    {
        if (ch == '"') result += "\\\"";
        else result += ch;
    }
    result += "\"";
    return result;
}

std::string BuildReportedTranscript(const std::vector<CServer::SChatEntry>& chats, uint32_t target_player_id)
{
    const uint32_t now = NowSeconds();
    const uint32_t window = static_cast<uint32_t>(std::max(0.f, game_config::report_scan_window_seconds));
    std::string transcript;
    for (const auto& chat : chats)
    {
        if (chat.player_id != target_player_id) continue;
        if (now > chat.system_time && now - chat.system_time > window) continue;
        transcript += "[" + std::to_string(chat.system_time) + "] " + chat.player_name + ": " + chat.message + "\n";
    }
    return transcript;
}

std::optional<std::string> RunCurlJson(const std::string& body)
{
    static std::atomic<uint64_t> request_counter{0};
    std::filesystem::path request_path;
    std::filesystem::path response_path;

    try
    {
        std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
        std::string stamp = std::to_string(NowSeconds()) + "_" + std::to_string(request_counter.fetch_add(1));
        request_path = temp_dir / ("florrbt_report_request_" + stamp + ".json");
        response_path = temp_dir / ("florrbt_report_response_" + stamp + ".json");
        {
            std::ofstream request(request_path, std::ios::binary);
            if (!request) return std::nullopt;
            request << body;
        }

        std::string command = "curl -sS --max-time 6 -X POST " + ShellQuote(game_config::report_deepseek_api_url) +
                              " -H \"Content-Type: application/json\" -H \"Authorization: Bearer " +
                              game_config::report_deepseek_api_key + "\" --data-binary @" + ShellQuote(request_path) +
                              " -o " + ShellQuote(response_path);
        int result = std::system(command.c_str());
        std::filesystem::remove(request_path);
        if (result != 0)
        {
            std::filesystem::remove(response_path);
            return std::nullopt;
        }

        std::ifstream response(response_path, std::ios::binary);
        if (!response)
        {
            std::filesystem::remove(response_path);
            return std::nullopt;
        }
        std::ostringstream stream;
        stream << response.rdbuf();
        std::filesystem::remove(response_path);
        return stream.str();
    }
    catch (const std::exception& e)
    {
        LOG_WARN("report", std::string("AI request failed: ") + e.what());
    }
    catch (...)
    {
        LOG_WARN("report", "AI request failed: unknown exception");
    }

    std::error_code ec;
    if (!request_path.empty()) std::filesystem::remove(request_path, ec);
    if (!response_path.empty()) std::filesystem::remove(response_path, ec);
    return std::nullopt;
}

float ParseDeepSeekMuteSeconds(const std::string& response_text)
{
    std::string error;
    std::optional<CJsonValue> root = CJsonValue::Parse(response_text, &error);
    if (!root) return -1.f;
    const CJsonValue* choices = root->Find("choices");
    if (!choices || choices->AsArray().empty()) return -1.f;
    const CJsonValue* message = choices->AsArray().front().Find("message");
    const CJsonValue* content = message ? message->Find("content") : nullptr;
    if (!content) return -1.f;

    std::optional<CJsonValue> review = CJsonValue::Parse(content->AsString(), &error);
    if (review)
    {
        if (const CJsonValue* seconds = review->Find("mute_seconds"))
            return static_cast<float>(seconds->AsNumber(-1.0));
    }

    const std::string& text = content->AsString();
    size_t pos = text.find_first_of("0123456789");
    if (pos == std::string::npos) return -1.f;
    try
    {
        return std::stof(text.substr(pos));
    }
    catch (...)
    {
        return -1.f;
    }
}

void SendPrivateSystem(CPlayer& player, const std::string& message)
{
    CServer* server = CServer::GetInstance();
    INetworkModule* network = server ? server->GetNetworkModule() : nullptr;
    if (!server || !network) return;

    CServer::SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    entry.system_time = NowSeconds();
    network->SendChatToPlayer(player, entry);
}

struct SAsyncReportResult
{
    uint32_t reporter_id = 0;
    uint32_t target_id = 0;
    std::string reporter_name;
    std::string target_name;
    std::vector<CServer::SChatEntry> chats;
    float mute_seconds = -1.f;
};

std::mutex g_async_report_mutex;
std::queue<SAsyncReportResult> g_async_report_results;

void QueueAsyncResult(SAsyncReportResult result)
{
    std::lock_guard<std::mutex> lock(g_async_report_mutex);
    g_async_report_results.push(std::move(result));
}

std::optional<SAsyncReportResult> PopAsyncResult()
{
    std::lock_guard<std::mutex> lock(g_async_report_mutex);
    if (g_async_report_results.empty()) return std::nullopt;
    SAsyncReportResult result = std::move(g_async_report_results.front());
    g_async_report_results.pop();
    return result;
}

void ApplyReportDecision(CPlayer& reporter, CPlayer& target, float mute_seconds, bool used_ai)
{
    if (used_ai)
    {
        LOG_INFO("report", "AI decision reporter=" + reporter.GetName() + " target=" + target.GetName() +
                               " mute_seconds=" + std::to_string(static_cast<int>(std::round(mute_seconds))));
    }

    if (mute_seconds > 0.f)
    {
        reporter.RegisterValidReport();
        target.MuteFor(mute_seconds);
        SendPrivateSystem(reporter, "Report accepted: " + target.GetName() + " muted for " +
                                        std::to_string(static_cast<int>(std::round(mute_seconds))) +
                                        "s by " + (used_ai ? "AI review." : "keyword review."));
        SendPrivateSystem(target, "You have been muted for " +
                                      std::to_string(static_cast<int>(std::round(mute_seconds))) + "s.");
        return;
    }

    reporter.RegisterInvalidReport();
    if (reporter.IsReportDisabled())
    {
        SendPrivateSystem(reporter, "Report received: no violation found. Report access disabled after too many invalid reports.");
        return;
    }

    if (game_config::report_invalid_disable_count > 0)
    {
        int remaining = std::max(0, game_config::report_invalid_disable_count - reporter.GetInvalidReportCount());
        SendPrivateSystem(reporter, "Report received: no violation found. Invalid reports before disable: " +
                                        std::to_string(remaining) + ".");
    }
    else
    {
        SendPrivateSystem(reporter, "Report received: no violation found.");
    }
}

} // namespace

namespace report
{
float ReviewByKeywords(const std::vector<CServer::SChatEntry>& chats, uint32_t target_player_id)
{
    int matches = 0;
    std::string transcript = ToLower(BuildReportedTranscript(chats, target_player_id));
    for (const std::string& keyword : ReportKeywords())
    {
        size_t pos = 0;
        std::string lower_keyword = ToLower(keyword);
        while ((pos = transcript.find(lower_keyword, pos)) != std::string::npos)
        {
            ++matches;
            pos += lower_keyword.size();
        }
    }

    if (matches <= 0) return 0.f;
    return game_config::report_keyword_base_mute_seconds * std::sqrt(static_cast<float>(matches));
}

float ReviewByDeepSeek(const std::vector<CServer::SChatEntry>& chats, uint32_t target_player_id)
{
    if (game_config::report_deepseek_api_key.empty()) return -1.f;
    std::string transcript = BuildReportedTranscript(chats, target_player_id);
    if (transcript.empty()) return 0.f;

    std::string prompt =
        "You moderate a small multiplayer game chat. Return only JSON like {\"mute_seconds\":0}. "
        "Choose 0 for harmless speech. For harassment, hate, threats, explicit spam, or severe abuse, choose a mute duration in seconds. "
        "Reported chat from the last minute:\n" + transcript;

    std::string body = "{\"model\":\"" + EscapeJsonString(game_config::report_deepseek_model) +
                       "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + EscapeJsonString(prompt) +
                       "\"}],\"temperature\":0}";

    std::optional<std::string> response = RunCurlJson(body);
    if (!response) return -1.f;
    float mute_seconds = ParseDeepSeekMuteSeconds(*response);
    if (mute_seconds < 0.f) return -1.f;
    return std::max(0.f, mute_seconds);
}

void ProcessAsyncResults()
{
    CServer* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    if (!context) return;

    while (std::optional<SAsyncReportResult> result = PopAsyncResult())
    {
        CPlayer* reporter = context->Network().FindPlayerById(result->reporter_id);
        CPlayer* target = context->Network().FindPlayerById(result->target_id);
        if (!reporter || !target) continue;

        float mute_seconds = result->mute_seconds;
        bool used_ai = mute_seconds >= 0.f;
        if (!used_ai) mute_seconds = ReviewByKeywords(result->chats, result->target_id);
        ApplyReportDecision(*reporter, *target, mute_seconds, used_ai);
    }
}

bool HandleServerCommand(CPlayer& reporter, const std::string& command_line)
{
    std::vector<std::string> args = SplitCommandLine(command_line);
    if (args.empty()) return false;

    std::string name = ToLower(args[0]);
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    CServer* server = CServer::GetInstance();
    CGameContext* context = server ? server->GameContext() : nullptr;
    INetworkModule* network = server ? server->GetNetworkModule() : nullptr;
    if (!server || !context || !network) return true;

    if (name == "report")
    {
        if (reporter.IsReportDisabled())
        {
            SendPrivateSystem(reporter, "Report failed: your report access is disabled.");
            return true;
        }

        if (args.size() < 2)
        {
            SendPrivateSystem(reporter, "Usage: /report [name]");
            return true;
        }

        CPlayer* target = FindPlayerByName(context, args[1]);
        if (!target)
        {
            SendPrivateSystem(reporter, "Report failed: player not found.");
            return true;
        }

        std::vector<CServer::SChatEntry> chat_snapshot = server->GetChats();
        if (game_config::report_deepseek_api_key.empty())
        {
            ApplyReportDecision(reporter, *target, ReviewByKeywords(chat_snapshot, target->GetId()), false);
            return true;
        }

        SendPrivateSystem(reporter, "Report received: AI review pending.");
        SAsyncReportResult request;
        request.reporter_id = reporter.GetId();
        request.target_id = target->GetId();
        request.reporter_name = reporter.GetName();
        request.target_name = target->GetName();
        request.chats = std::move(chat_snapshot);
        std::thread([request = std::move(request)]() mutable {
            try
            {
                request.mute_seconds = ReviewByDeepSeek(request.chats, request.target_id);
            }
            catch (const std::exception& e)
            {
                LOG_WARN("report", std::string("AI review failed: ") + e.what());
                request.mute_seconds = -1.f;
            }
            catch (...)
            {
                LOG_WARN("report", "AI review failed: unknown exception");
                request.mute_seconds = -1.f;
            }
            QueueAsyncResult(std::move(request));
        }).detach();
        return true;
    }

    if (name == "say" && args.size() >= 2 && ToLower(args[1]) == "whisper")
    {
        std::vector<std::string> rewritten;
        rewritten.push_back("whisper");
        rewritten.insert(rewritten.end(), args.begin() + 2, args.end());
        args = std::move(rewritten);
        name = "whisper";
    }

    if (name == "whisper" || name == "w")
    {
        if (args.size() < 3)
        {
            SendPrivateSystem(reporter, "Usage: /whisper [name] [message]");
            return true;
        }
        CPlayer* target = FindPlayerByName(context, args[1]);
        if (!target)
        {
            SendPrivateSystem(reporter, "Whisper failed: player not found.");
            return true;
        }

        CServer::SChatEntry entry;
        entry.world = reporter.GetEntity() ? reporter.GetEntity()->GameWorld() : nullptr;
        entry.flag = EChatFlag::Whisper;
        entry.player_id = reporter.GetId();
        entry.target_player_id = static_cast<int>(target->GetId());
        entry.player_name = reporter.GetName();
        entry.message = JoinArgs(args, 2);
        entry.system_time = NowSeconds();
        network->SendChatToPlayer(reporter, entry);
        if (target != &reporter) network->SendChatToPlayer(*target, entry);
        return true;
    }

    return true;
}
}
