#include "chat_extension.h"
#include "extension_runtime.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include "../../../Shared/utf8.h"
#include "../gameworld.h"
#include "../player.h"
#include "../entities/mob.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>

namespace
{
std::uint32_t NowSeconds()
{
    return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
std::string Join(const std::vector<std::string>& args, size_t start)
{
    std::string result;
    for (size_t i = start; i < args.size(); ++i) { if (i != start) result += ' '; result += args[i]; }
    return result;
}
std::string Lower(std::string text)
{
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
}

const SExtensionDescriptor& CChatExtension::Descriptor() const
{
    static const SExtensionDescriptor descriptor{"extension.chat", EExtensionScope::Server, 1};
    return descriptor;
}
bool CChatExtension::OnAttach(CExtensionContext& context, std::string&)
{
    m_players = &context.players; m_delivery = &context.chat_delivery; m_squads = context.squads; return true;
}
void CChatExtension::OnDetach() noexcept { m_players = nullptr; m_delivery = nullptr; m_squads = nullptr; }

SChatEntry CChatExtension::Submit(CGameWorld* world, florrbt::Vector2f pos, EChatFlag flag,
    std::uint32_t player_id, const std::string& name, const std::string& message, int target)
{
    if (message.empty()) return {};
    SChatEntry entry;
    if (world) { entry.world_id = world->GetId(); entry.world_instance_generation = world->GetInstanceGeneration(); }
    entry.pos = pos; entry.flag = flag; entry.player_id = player_id; entry.player_name = name;
    entry.message = std::string(TruncateUtf8(message, max_chat_message_size));
    entry.target_player_id = target; entry.system_time = NowSeconds();
    if (CPlayer* sender = m_players ? m_players->FindPlayerById(player_id) : nullptr)
        entry.sender_session = sender->SessionRef();
    m_history.push_back(entry);
    const size_t limit = std::max<size_t>(1, game_config::server_max_saved_chats);
    if (m_history.size() > limit) m_history.erase(m_history.begin(), m_history.end() - limit);
    return entry;
}

bool CChatExtension::SendTo(CPlayer& player, const SChatEntry& entry)
{
    return m_delivery && player.IsConnected() && player.IsAuthenticated() && m_delivery->DeliverChat(player, entry);
}
void CChatExtension::SendSystem(CPlayer& player, const std::string& message)
{
    SChatEntry entry;
    entry.flag = EChatFlag::Server; entry.target_player_id = static_cast<int>(player.GetId());
    entry.player_name = "Server"; entry.message = message; entry.system_time = NowSeconds();
    SendTo(player, entry);
}

void CChatExtension::Broadcast(const SChatEntry& chat)
{
    if (!m_players || chat.message.empty()) return;
    if (chat.flag == EChatFlag::Squad)
    {
        CPlayer* sender = m_players->FindPlayerById(chat.player_id);
        CEntity* entity = sender ? sender->GetEntity() : nullptr;
        CGameWorld* world = entity ? entity->GameWorld() : nullptr;
        if (!world || world->GetId() != chat.world_id || world->GetInstanceGeneration() != chat.world_instance_generation) return;
        if (const auto* squads = m_squads ? m_squads->FindSquads({world->GetId(), world->GetInstanceGeneration()}) : nullptr)
            for (CPlayer* member : squads->Members(*sender)) if (member) SendTo(*member, chat);
        return;
    }
    for (const auto& player : m_players->GetPlayers())
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        CEntity* entity = player->GetEntity();
        CGameWorld* world = entity ? entity->GameWorld() : nullptr;
        const bool same_world = world && !entity->IsMarkedForDes() && world->GetId() == chat.world_id &&
            world->GetInstanceGeneration() == chat.world_instance_generation;
        if (chat.flag == EChatFlag::Server)
        {
            if (chat.target_player_id >= 0 && static_cast<int>(player->GetId()) != chat.target_player_id) continue;
            if (chat.world_instance_generation && !same_world) continue;
        }
        else if (chat.flag == EChatFlag::Whisper)
        {
            if (static_cast<int>(player->GetId()) != chat.target_player_id && player->GetId() != chat.player_id) continue;
        }
        else if (chat.flag != EChatFlag::Global)
        {
            if (!same_world) continue;
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            const SMobStats* stats = mob ? mob->GetFinalStats() : nullptr;
            const float range = (stats ? stats->horizon : game_config::default_horizon) * 2.f;
            if (DistanceSq(entity->Pos(), chat.pos) > range * range) continue;
        }
        SendTo(*player, chat);
    }
}

void CChatExtension::HandleMessage(CPlayer& player, EChatFlag flag, const std::string& message)
{
    CEntity* entity = player.GetEntity();
    if (!player.IsAuthenticated() || !entity || entity->IsMarkedForDes()) return;
    if (player.IsMuted())
    {
        SendSystem(player, "You are muted for " + std::to_string(static_cast<int>(std::ceil(player.GetMuteTimer()))) + "s.");
        return;
    }
    Broadcast(Submit(entity->GameWorld(), entity->Pos(), flag == EChatFlag::Local ? flag : EChatFlag::Global,
                     player.GetId(), player.GetName(), message));
}

std::span<const std::string_view> CChatExtension::Commands() const
{
    static constexpr std::array<std::string_view, 3> names{"whisper", "w", "say"}; return names;
}
bool CChatExtension::HandleCommand(CPlayer& player, const std::vector<std::string>& args)
{
    if (args.empty() || !m_players) return false;
    if (player.IsMuted())
    {
        SendSystem(player, "You are muted for " + std::to_string(static_cast<int>(std::ceil(player.GetMuteTimer()))) + "s.");
        return true;
    }
    size_t target_index = 1;
    if (args.front() == "say")
    {
        if (args.size() < 2 || Lower(args[1]) != "whisper") return false;
        target_index = 2;
    }
    if (args.size() <= target_index + 1) { SendSystem(player, "Usage: /whisper [name] [message]"); return true; }
    CPlayer* target = nullptr;
    for (const auto& candidate : m_players->GetPlayers())
        if (candidate && (Lower(candidate->GetName()) == Lower(args[target_index]) ||
            Lower(candidate->GetAccountName()) == Lower(args[target_index]))) { target = candidate.get(); break; }
    if (!target) { SendSystem(player, "Whisper failed: player not found."); return true; }
    // Preserve the existing private-message history policy: whispers are not
    // appended to the public moderation transcript.
    SChatEntry entry;
    entry.flag = EChatFlag::Whisper; entry.player_id = player.GetId(); entry.player_name = player.GetName();
    entry.target_player_id = static_cast<int>(target->GetId()); entry.message = Join(args, target_index + 1);
    entry.system_time = NowSeconds();
    SendTo(player, entry); if (target != &player) SendTo(*target, entry);
    return true;
}
