#include "report.h"
#include "server.h"
#include "../Engine/json_value.h"
#include "../Engine/logger.h"
#include "../Shared/game_config.h"
#include "Game/entity.h"
#include "Game/gamecontext.h"
#include "Game/extensions/extension_services.h"
#include "Game/gamecontrollers/world_controller_factory.h"
#include "Game/gameworld.h"
#include "Game/player.h"
#include "Module/network_module.h"
#include "Module/world_module.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <deque>
#include <unordered_map>

namespace
{
std::string ToLower(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

std::vector<std::string> SplitCommandLine(const std::string& line)
{
    std::vector<std::string> args;
    std::string current;
    bool in_quotes = false;
    for (char ch : line)
    {
        if (ch == '"') in_quotes = !in_quotes;
        else if (std::isspace(static_cast<unsigned char>(ch)) && !in_quotes)
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
            } else
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

std::string BuildReportedTranscript(const std::vector<SChatEntry>& chats, const SPlayerSessionRef& target)
{
    const uint32_t now = NowSeconds();
    const uint32_t window = static_cast<uint32_t>(std::max(0.f, game_config::report_scan_window_seconds));
    std::string transcript;
    for (const auto& chat : chats)
    {
        if (chat.sender_session != target) continue;
        if (now > chat.system_time && now - chat.system_time > window) continue;
        transcript += "[" + std::to_string(chat.system_time) + "] " + chat.player_name + ": " + chat.message + "\n";
        while (transcript.size() > 65536) transcript.erase(0, transcript.find('\n') + 1);
    }
    return transcript;
}

std::optional<std::string> RunCurlJson(const std::string& body, const std::string& url, const std::string& api_key)
{
    static std::atomic<uint64_t> request_counter{ 0 };
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

        std::string command = "curl -sS --max-time 6 -X POST " + ShellQuote(url) +
                              " -H \"Content-Type: application/json\" -H \"Authorization: Bearer " +
                              api_key + "\" --data-binary @" + ShellQuote(request_path) +
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
    } catch (const std::exception& e)
    {
        LOG_WARN("report", std::string("AI request failed: ") + e.what());
    } catch (...)
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
        {
            const double parsed = seconds->AsNumber(-1.0);
            if (std::isfinite(parsed) && parsed >= 0.0 && parsed <= 86400.0 * 30.0)
                return static_cast<float>(parsed);
            return -1.f;
        }
    }

    const std::string& text = content->AsString();
    size_t pos = text.find_first_of("0123456789");
    if (pos == std::string::npos) return -1.f;
    try
    {
        const std::string numeric = text.substr(pos);
        std::size_t consumed = 0;
        const float parsed = std::stof(numeric, &consumed);
        if (consumed != numeric.size()) return -1.f;
        return std::isfinite(parsed) && parsed >= 0.f && parsed <= 86400.f * 30.f ? parsed : -1.f;
    } catch (...)
    {
        return -1.f;
    }
}

void SendPrivateSystem(CPlayer& player, const std::string& message)
{
    CServer* server = CServer::GetInstance();
    INetworkModule* network = server ? server->GetNetworkModule() : nullptr;
    if (!server || !network) return;

    SChatEntry entry;
    entry.flag = EChatFlag::Server;
    entry.player_id = 0;
    entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server";
    entry.message = message;
    entry.system_time = NowSeconds();
    server->Extensions().Globals().Find<IChatService>()->SendTo(player, entry);
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
                                        std::to_string(static_cast<int>(std::round(mute_seconds))) + "s by " +
                                        (used_ai ? "AI review." : "keyword review."));
        SendPrivateSystem(target, "You have been muted for " +
                                      std::to_string(static_cast<int>(std::round(mute_seconds))) + "s.");
        return;
    }

    reporter.RegisterInvalidReport();
    if (reporter.IsReportDisabled())
    {
        SendPrivateSystem(
            reporter, "Report received: no violation found. Report access disabled after too many invalid reports.");
        return;
    }

    if (game_config::report_invalid_disable_count > 0)
    {
        int remaining = std::max(0, game_config::report_invalid_disable_count - reporter.GetInvalidReportCount());
        SendPrivateSystem(reporter, "Report received: no violation found. Invalid reports before disable: " +
                                        std::to_string(remaining) + ".");
    } else
    {
        SendPrivateSystem(reporter, "Report received: no violation found.");
    }
}

} // namespace

namespace report
{
float ReviewByKeywords(const std::string& source)
{
    int matches = 0;
    std::string transcript = ToLower(source);
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

float ReviewByDeepSeek(const std::string& transcript, const std::string& url, const std::string& api_key,
                      const std::string& model)
{
    if (api_key.empty()) return -1.f;
    if (transcript.empty()) return 0.f;

    std::string prompt = "You moderate a small multiplayer game chat. Return only JSON like {\"mute_seconds\":0}. "
                         "Choose 0 for harmless speech. For harassment, hate, threats, explicit spam, or severe abuse, "
                         "choose a mute duration in seconds. "
                         "Reported chat from the last minute:\n" +
                         transcript;

    std::string body = "{\"model\":\"" + EscapeJsonString(model) +
                       "\",\"messages\":[{\"role\":\"user\",\"content\":\"" + EscapeJsonString(prompt) +
                       "\"}],\"temperature\":0}";

    std::optional<std::string> response = RunCurlJson(body, url, api_key);
    if (!response) return -1.f;
    float mute_seconds = ParseDeepSeekMuteSeconds(*response);
    if (mute_seconds < 0.f) return -1.f;
    return std::max(0.f, mute_seconds);
}

} // namespace report

struct CReportReviewService::CImplementation
{
    struct SJob
    {
        SPlayerSessionRef reporter, target;
        std::string transcript, url, api_key, model;
        float fallback = 0.f, result = -1.f;
        std::uint64_t epoch = 0;
    };
    explicit CImplementation(TReview value) : review(std::move(value)) {}
    TReview review;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    std::deque<SJob> jobs, results;
    std::unordered_map<std::string, std::uint64_t> pending;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> cooldowns;
    std::uint64_t epoch = 1;
    bool stopping = false;

    void Run()
    {
        std::unique_lock lock(mutex);
        while (true)
        {
            wake.wait(lock, [&] { return stopping || !jobs.empty(); });
            if (stopping) return;
            SJob job = std::move(jobs.front()); jobs.pop_front();
            lock.unlock();
            try
            {
                job.result = review ? review(job.transcript) :
                    report::ReviewByDeepSeek(job.transcript, job.url, job.api_key, job.model);
                if (!std::isfinite(job.result) || job.result > 86400.f * 30.f) job.result = -1.f;
            }
            catch (...) { job.result = -1.f; }
            lock.lock();
            if (!stopping && job.epoch == epoch) results.push_back(std::move(job));
        }
    }
};

CReportReviewService::CReportReviewService(TReview review) : m_impl(std::make_unique<CImplementation>(std::move(review))) {}
CReportReviewService::~CReportReviewService() { ShutDown(); }

bool CReportReviewService::Submit(CPlayer& reporter, CPlayer& target, const std::vector<SChatEntry>& chats, std::string& error)
{
    error.clear();
    if (!reporter.IsAuthenticated() || !target.IsAuthenticated() || reporter.IsReportDisabled())
    { error = "Report unavailable for this session"; return false; }
    auto& impl = *m_impl;
    std::unique_lock lock(impl.mutex);
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(impl.cooldowns, [&](const auto& value) { return now - value.second >= std::chrono::seconds(5); });
    if (impl.stopping || impl.pending.size() >= 32 || impl.cooldowns.size() >= 4096 ||
        impl.pending.contains(reporter.GetAccountName()) || impl.cooldowns.contains(reporter.GetAccountName()))
    { error = "Report queue busy or rate limited; retry later"; return false; }
    CImplementation::SJob job;
    job.reporter = reporter.SessionRef(); job.target = target.SessionRef(); job.epoch = impl.epoch;
    job.transcript = BuildReportedTranscript(chats, job.target);
    job.fallback = report::ReviewByKeywords(job.transcript);
    // Capture every mutable setting on the game thread.
    job.url = game_config::report_deepseek_api_url;
    job.api_key = game_config::report_deepseek_api_key;
    job.model = game_config::report_deepseek_model;
    impl.cooldowns[reporter.GetAccountName()] = now;
    if (job.api_key.empty() && !impl.review)
    {
        lock.unlock();
        ApplyReportDecision(reporter, target, job.fallback, false);
        return true;
    }
    impl.pending[job.reporter.account] = job.epoch;
    impl.jobs.push_back(std::move(job));
    try
    {
        if (!impl.worker.joinable()) impl.worker = std::thread([&impl] { impl.Run(); });
    }
    catch (...)
    {
        impl.jobs.pop_back(); impl.pending.erase(reporter.GetAccountName());
        error = "Unable to start report worker"; return false;
    }
    lock.unlock(); impl.wake.notify_one();
    SendPrivateSystem(reporter, "Report received: AI review pending.");
    return true;
}

void CReportReviewService::Process(CServer& server)
{
    auto* network = server.GetNetworkModule();
    if (!network) return;
    std::deque<CImplementation::SJob> results;
    {
        std::lock_guard lock(m_impl->mutex);
        results.swap(m_impl->results);
        for (const auto& job : results) m_impl->pending.erase(job.reporter.account);
    }
    for (const auto& job : results)
    {
        auto* reporter = network->FindPlayerById(job.reporter.id);
        auto* target = network->FindPlayerById(job.target.id);
        if (!reporter || !target || !reporter->MatchesSession(job.reporter) || !target->MatchesSession(job.target)) continue;
        ApplyReportDecision(*reporter, *target, job.result >= 0.f ? job.result : job.fallback, job.result >= 0.f);
    }
}

void CReportReviewService::CancelPending()
{
    std::lock_guard lock(m_impl->mutex);
    ++m_impl->epoch;
    m_impl->jobs.clear(); m_impl->results.clear(); m_impl->pending.clear(); m_impl->cooldowns.clear();
}

void CReportReviewService::ShutDown()
{
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->stopping = true;
        ++m_impl->epoch; m_impl->jobs.clear(); m_impl->results.clear(); m_impl->pending.clear();
    }
    m_impl->wake.notify_all();
    if (m_impl->worker.joinable()) m_impl->worker.join();
}

std::size_t CReportReviewService::PendingCount() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->pending.size();
}

namespace report
{
void ProcessAsyncResults()
{
    if (auto* server = CServer::GetInstance()) server->Reports().Process(*server);
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

    CEntity* reporter_entity = reporter.GetEntity();
    CGameWorld* reporter_world = reporter_entity ? reporter_entity->GameWorld() : nullptr;
    args[0] = name;
    if ((name == "invite" || name == "join") && server->Extensions().HandleCommand(reporter, args)) return true;

    if (name == "create" || name == "create_room" || name == "createroom")
    {
        if (args.size() < 2)
        {
            SendPrivateSystem(reporter, "Usage: /create [controller name]");
            return true;
        }

        const std::string controller_name = ToLower(JoinArgs(args, 1));
        const std::string_view controller_key = map_declarations::ControllerKeyForName(controller_name);
        if (controller_key.empty())
        {
            SendPrivateSystem(reporter, "unknown controller: " + controller_name);
            return true;
        }
        if (controller_key == world_controller_keys::waiting)
        {
            SendPrivateSystem(reporter, "cannot create a room with the waiting controller");
            return true;
        }

        IWorldModule* world_module = server ? server->GetWorldModule() : nullptr;
        if (!world_module)
        {
            SendPrivateSystem(reporter, "room creation unavailable");
            return true;
        }

        const int room_code = world_module->NextRoomCode();
        if (room_code < 0)
        {
            SendPrivateSystem(reporter, "no room codes available");
            return true;
        }

        CGameWorld* waiting_world =
            world_module->CreateRoomWorld(game_config::room_default_map_path, controller_key, room_code);
        if (!waiting_world)
        {
            SendPrivateSystem(reporter, "failed to create room");
            return true;
        }

        // The creator joins their own room.
        CEntity* reporter_entity = reporter.GetEntity();
        const int departing_entity_id = reporter_entity ? reporter_entity->Id() : -1;
        bool transferred = false;
        if (reporter_entity && reporter_entity->GameWorld() != waiting_world)
            transferred = static_cast<bool>(reporter_entity->GameWorld()->TransferPlayerEntityToWorld(reporter, *waiting_world));
        LOG_INFO("room", "create: code=" + std::to_string(room_code) + " controller=" + controller_name +
                             " player=" + reporter.GetName() + " entity=" +
                             (departing_entity_id >= 0 ? std::to_string(departing_entity_id) : "none") +
                             " transferred=" + (transferred ? "yes" : "no"));
        if (!transferred)
            SendPrivateSystem(reporter, "room created: code " + std::to_string(room_code) + " (transfer failed, use /join-room)");
        else
            SendPrivateSystem(reporter, "room created: code " + std::to_string(room_code) + " controller " +
                                            controller_name);
        return true;
    }

    if (name == "join-room" || name == "joinroom" || name == "join_room")
    {
        if (args.size() < 2)
        {
            SendPrivateSystem(reporter, "Usage: /join-room [code]");
            return true;
        }

        int room_code = 0;
        const auto [end, error] = std::from_chars(args[1].data(), args[1].data() + args[1].size(), room_code);
        if (error != std::errc{} || end != args[1].data() + args[1].size())
        {
            SendPrivateSystem(reporter, "invalid room code");
            return true;
        }

        IWorldModule* world_module = server ? server->GetWorldModule() : nullptr;
        CGameWorld* waiting_world = world_module ? world_module->FindRoomWorld(room_code) : nullptr;
        if (!waiting_world)
        {
            SendPrivateSystem(reporter, "room not found: " + args[1]);
            return true;
        }

        CEntity* reporter_entity = reporter.GetEntity();
        if (!reporter_entity || reporter_entity->GameWorld() == waiting_world)
        {
            SendPrivateSystem(reporter, "already in room " + args[1]);
            return true;
        }

        CGameWorld* source_world = reporter_entity->GameWorld();
        const bool transferred_entity = source_world &&
            static_cast<bool>(source_world->TransferPlayerEntityToWorld(reporter, *waiting_world));
        LOG_INFO("room", "join: code=" + std::to_string(room_code) + " player=" + reporter.GetName() +
                             " source=" + (source_world ? std::to_string(source_world->GetId()) : "none") +
                             " target=" + std::to_string(waiting_world->GetId()) +
                             " result=" + (transferred_entity ? "ok" : "fail"));
        if (!transferred_entity)
        {
            SendPrivateSystem(reporter, "failed to join room " + args[1]);
            return true;
        }

        SendPrivateSystem(reporter, "joined room " + std::to_string(room_code));
        return true;
    }

    if (reporter_world && reporter_world->GetController() &&
        reporter_world->GetController()->HandlePlayerCommand(*reporter_world, reporter, args))
    {
        return true;
    }

    if (name == "start")
    {
        SendPrivateSystem(reporter, "you are not in a waiting room");
        return true;
    }

    if (server->Extensions().HandleCommand(reporter, args)) return true;

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

        const auto& chats = server->Extensions().Globals().Find<IChatService>()->History();
        std::string error;
        if (!server->Reports().Submit(reporter, *target, chats, error)) SendPrivateSystem(reporter, error);
        return true;
    }

    return true;
}
} // namespace report
