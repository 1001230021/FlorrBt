#include "account_link_service.h"
#include "oauth_provider.h"
#include "../Game/player.h"
#include "../Game/game_services.h"
#include "../Persistence/account_store.h"
#include "../HotReload/snapshot_archive.h"
#include "../../Engine/json_value.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace
{
using Clock = std::chrono::steady_clock;
std::string Env(const char* key) { const char* value = std::getenv(key); return value ? value : ""; }
std::string String(const CJsonValue& value, const char* key) { const auto* item = value.Find(key); return item && item->IsString() ? item->AsString() : ""; }
std::string Json(const json_t* value) { char* raw = json_dumps(value, JSON_COMPACT); if (!raw) return {}; std::string result(raw); std::free(raw); return result; }
}
struct CAccountLinkService::SImpl
{
    struct STask {
        SPlayerSessionRef session; std::string account_id, provider, action, state, code, password_hash;
        std::uint32_t request = 0; SAccountAuthWork proof; Clock::time_point expires;
    };
    struct SResult { STask task; SAccountAuthResult proof; SOAuthIdentityResult identity; };
    struct SReply { SPlayerSessionRef session; std::uint64_t fence; std::uint32_t request; std::string error; };
    std::map<std::string, COAuthProvider> providers;
    std::unordered_map<std::string, STask> attempts;
    std::unordered_map<std::string, Clock::time_point> cooldowns;
    std::map<std::string, SPlayerSessionRef> busy;
    std::deque<SReply> replies;
    std::deque<STask> jobs; std::deque<SResult> results;
    std::mutex mutex; std::condition_variable condition; std::jthread worker;
    SImpl(std::vector<SOAuthProviderConfig> configs, COAuthProvider::TTransport transport) {
        if (configs.empty()) configs = {
            {"discord", Env("FLORRBT_DISCORD_CLIENT_ID"), Env("FLORRBT_DISCORD_CLIENT_SECRET"), Env("FLORRBT_DISCORD_REDIRECT_URI")},
            {"qq", Env("FLORRBT_QQ_APP_ID"), Env("FLORRBT_QQ_APP_KEY"), Env("FLORRBT_QQ_REDIRECT_URI")}};
        for (auto& config : configs) providers.emplace(config.provider, COAuthProvider(config, transport));
        for (const std::string name : {"discord", "qq"}) if (!providers.contains(name)) providers.emplace(name, COAuthProvider({name, {}, {}, {}}, transport));
    }
    void Start() {
        if (worker.joinable()) return;
        worker = std::jthread([this](std::stop_token stop) {
            std::unique_lock lock(mutex);
            while (!stop.stop_requested()) {
                condition.wait(lock, [&] { return stop.stop_requested() || !jobs.empty(); });
                if (stop.stop_requested()) break;
                SResult result; result.task = std::move(jobs.front()); jobs.pop_front(); lock.unlock();
                try {
                    if (result.task.action == "complete") result.identity = providers.at(result.task.provider).Exchange(result.task.code, stop);
                    else result.proof = CAccountDataStore::ExecuteAuthentication(std::move(result.task.proof));
                } catch (...) { result.identity.error = "verification_unavailable"; }
                result.task.code.clear(); lock.lock(); results.push_back(std::move(result));
            }
        });
    }
};
CAccountLinkService::CAccountLinkService(std::vector<SOAuthProviderConfig> providers, COAuthProvider::TTransport transport)
    : m_impl(std::make_unique<SImpl>(std::move(providers), std::move(transport))) {}
CAccountLinkService::~CAccountLinkService() { ShutDown(); }
void CAccountLinkService::ShutDown()
{
    auto& s = *m_impl; s.worker.request_stop(); s.condition.notify_all(); if (s.worker.joinable()) s.worker.join();
    std::lock_guard lock(s.mutex); s.jobs.clear(); s.results.clear(); s.attempts.clear(); s.busy.clear(); s.replies.clear(); s.cooldowns.clear();
}
std::string CAccountLinkService::State(const CPlayer& player) const
{
    auto root = MakeJsonObject(); CSnapshotWriter writer(root.get()); writer.Field("schema", 1); writer.Field("kind", "state");
    writer.Field("account", player.GetAccountName()); writer.UInt64("session", player.SessionGeneration());
    writer.Field("emailPolicy", static_cast<int>(game_config::GetEmailPolicy()));
    std::string id; std::vector<SLinkedIdentity> links; CAccountDataStore::GetIdentityState(player.GetAccountName(), id, links);
    auto entries = MakeJsonArray();
    SAccountEmailState email; CAccountDataStore::GetEmailState(player.GetAccountName(), email);
    for (const std::string name : {"discord", "qq", "email"}) {
        auto value = MakeJsonObject(); CSnapshotWriter item(value.get()); item.Field("provider", name);
        const auto linked = std::find_if(links.begin(), links.end(), [&](const auto& identity) { return identity.provider == name; });
        const bool bound = name == "email" ? email.verified : linked != links.end();
        item.Field("configured", name == "email" ? (game_config::IsEmailBindingEnabled() && !game_config::email_username.empty() &&
            !game_config::email_password.empty() && !game_config::email_smtp_host.empty()) : m_impl->providers.at(name).Config().Enabled());
        item.Field("bound", bound); item.Field("label", name == "email" ? email.email : linked != links.end() ? linked->label : "");
        item.Field("canUnlink", bound && !(name == "email" && game_config::IsEmailBindingRequired()));
        AppendJson(entries.get(), value.release());
    }
    writer.Node("providers", entries.release()); return Json(root.get());
}
void CAccountLinkService::Request(CPlayer& player, const CJsonValue& value, const TDelivery& deliver)
{
    if (!player.IsAuthenticated() || !value.IsObject()) return;
    const auto* sequence = value.Find("request");
    if (!sequence || !sequence->IsNumber() || sequence->AsNumber() < 1 || sequence->AsNumber() > UINT32_MAX ||
        std::floor(sequence->AsNumber()) != sequence->AsNumber()) return;
    const auto request = static_cast<std::uint32_t>(sequence->AsNumber());
    const auto respond = [&](std::string_view error) {
        auto root = MakeJsonObject(); CSnapshotWriter writer(root.get()); writer.Field("schema", 1); writer.Field("kind", "result");
        writer.Field("request", request); writer.Field("error", error); deliver(player, Json(root.get())); deliver(player, State(player));
    };
    auto& s = *m_impl; const auto action = String(value, "action"), provider = String(value, "provider");
    if (action == "list") { deliver(player, State(player)); return; }
    if (action != "begin" && action != "complete" && action != "unlink") { respond("invalid_request"); return; }
    std::string account_id; std::vector<SLinkedIdentity> links;
    if (!CAccountDataStore::GetIdentityState(player.GetAccountName(), account_id, links)) { respond("account_changed"); return; }
    if (s.busy.contains(account_id) || s.busy.size() >= 32 || s.replies.size() >= 128 ||
        std::any_of(s.replies.begin(), s.replies.end(), [&](const auto& reply) { return reply.session.account == player.GetAccountName(); })) { respond("busy"); return; }
    if (provider != "email" && !s.providers.contains(provider)) { respond("invalid_provider"); return; }
    if (action != "unlink" && (provider == "email" || !s.providers.at(provider).Config().Enabled())) { respond("not_configured"); return; }
    SImpl::STask task; task.session = player.SessionRef(); task.account_id = account_id; task.provider = provider;
    task.action = action; task.request = request; task.expires = Clock::now() + std::chrono::minutes(5);
    if (action == "complete") {
        const auto state = String(value, "state"); const auto found = s.attempts.find(state);
        if (found == s.attempts.end() || found->second.expires < Clock::now() || found->second.provider != provider ||
            !player.MatchesSession(found->second.session) || found->second.account_id != account_id) { respond("authorization_expired"); return; }
        task = std::move(found->second); s.attempts.erase(found); task.action = action; task.request = request; task.code = String(value, "code");
        if (task.code.empty() || task.code.size() > 4096) { respond("authorization_failed"); return; }
    } else {
        const auto next = s.cooldowns.find(account_id);
        if (next != s.cooldowns.end() && next->second > Clock::now()) { respond("try_later"); return; }
        s.cooldowns[account_id] = Clock::now() + std::chrono::seconds(3);
        const auto password = String(value, "password");
        if (password.empty() || password.size() > 64 || !CAccountDataStore::PrepareAuthentication(player.GetAccountName(), password, "",
            player.GetRemoteAddress(), false, task.proof)) { respond("password_required"); return; }
        std::erase_if(s.attempts, [&](const auto& entry) { return entry.second.account_id == account_id || entry.second.expires < Clock::now(); });
        if (s.attempts.size() >= 128 || !GenerateOpaqueId(task.state)) { respond("busy"); return; }
    }
    s.Start(); s.busy[account_id] = player.SessionRef();
    { std::lock_guard lock(s.mutex); s.jobs.push_back(std::move(task)); } s.condition.notify_one();
}
void CAccountLinkService::Tick(IPlayerDirectory& players, const TDelivery& deliver)
{
    auto& s = *m_impl; std::deque<SImpl::SResult> results;
    { std::lock_guard lock(s.mutex); results.swap(s.results); }
    for (auto& result : results) {
        auto& task = result.task; const auto busy = s.busy.find(task.account_id);
        if (busy == s.busy.end() || busy->second != task.session) continue;
        s.busy.erase(busy);
        auto* player = players.FindPlayerById(task.session.id);
        if (!player || !player->IsConnected() || !player->MatchesSession(task.session) || task.expires < Clock::now()) continue;
        std::string error;
        if (task.action != "complete") {
            if (!result.proof.success || !result.proof.password_verified || !CAccountDataStore::CommitLogin(result.proof, &error)) {
                error = "wrong_password"; s.cooldowns[task.account_id] = Clock::now() + std::chrono::seconds(30);
            }
            else task.password_hash = result.proof.observed_password_hash;
        }
        if (error.empty() && !CAccountDataStore::IdentityProofCurrent(task.session.account, task.account_id, task.password_hash)) error = "account_changed";
        if (error.empty() && task.action == "begin") {
            const auto url = s.providers.at(task.provider).AuthorizationUrl(task.state);
            auto data = MakeJsonObject(); CSnapshotWriter writer(data.get()); writer.Field("schema", 1); writer.Field("kind", "authorize");
            writer.Field("request", task.request); writer.Field("provider", task.provider); writer.Field("state", task.state); writer.Field("url", url);
            task.proof = {}; s.attempts.insert_or_assign(task.state, task);
            if (!deliver(*player, Json(data.get()))) s.attempts.erase(task.state);
            continue;
        }
        if (error.empty() && task.action == "complete") {
            if (!result.identity.success) error = result.identity.error.empty() ? "authorization_failed" : result.identity.error;
            else if (!CAccountDataStore::LinkIdentity(task.session.account, task.account_id, task.password_hash, result.identity.identity, &error))
                if (error.empty()) error = "binding_failed";
        }
        if (error.empty() && task.action == "unlink" && !CAccountDataStore::UnlinkIdentity(task.session.account, task.account_id, task.password_hash, task.provider, &error))
            if (error.empty()) error = "binding_failed";
        s.replies.push_back({task.session, error.empty() ? CAccountDataStore::CurrentSaveFence() : 0, task.request, error});
    }
    for (auto it = s.replies.begin(); it != s.replies.end();) {
        auto* player = players.FindPlayerById(it->session.id);
        if (!player || !player->MatchesSession(it->session)) { it = s.replies.erase(it); continue; }
        if (!player->IsConnected() || (it->fence && !CAccountDataStore::IsSaveCommitted(it->fence))) { ++it; continue; }
        auto root = MakeJsonObject(); CSnapshotWriter writer(root.get()); writer.Field("schema", 1); writer.Field("kind", "result");
        writer.Field("request", it->request); writer.Field("error", it->error);
        if (!deliver(*player, Json(root.get())) || !deliver(*player, State(*player))) { ++it; continue; }
        it = s.replies.erase(it);
    }
    std::erase_if(s.attempts, [&](const auto& entry) { return entry.second.expires < Clock::now(); });
    std::erase_if(s.cooldowns, [&](const auto& entry) { return entry.second < Clock::now(); });
}
void CAccountLinkService::CancelSession(SPlayerSessionRef session)
{
    auto& s = *m_impl;
    std::erase_if(s.attempts, [&](const auto& entry) { return entry.second.session == session; });
    std::erase_if(s.replies, [&](const auto& entry) { return entry.session == session; });
    std::erase_if(s.busy, [&](const auto& entry) { return entry.second == session; });
    std::lock_guard lock(s.mutex);
    std::erase_if(s.jobs, [&](const auto& entry) { return entry.session == session; });
    std::erase_if(s.results, [&](const auto& entry) { return entry.task.session == session; });
    // 已执行的值任务可以结束；没有对应 busy 会话时，结果不能提交。
}
