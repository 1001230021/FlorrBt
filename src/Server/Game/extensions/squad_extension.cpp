#include "squad_extension.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace
{
std::string Lower(std::string text)
{
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string Join(const std::vector<std::string>& args, size_t start)
{
    std::string result;
    for (size_t i = start; i < args.size(); ++i) { if (i != start) result += ' '; result += args[i]; }
    return result;
}
}

const SExtensionDescriptor& CSquadExtension::Descriptor() const
{
    static constexpr std::array<std::string_view, 1> dependencies{"extension.chat"};
    static const SExtensionDescriptor descriptor{"extension.squad", EExtensionScope::World, 1, dependencies};
    return descriptor;
}
bool CSquadExtension::OnAttach(CExtensionContext& context, std::string& error)
{
    if (!context.chat) { error = "Squad extension requires chat"; return false; }
    m_players = &context.players; m_chat = context.chat; return true;
}
void CSquadExtension::OnDetach() noexcept { m_players = nullptr; m_chat = nullptr; }
bool CSquadExtension::InWorld(const CPlayer& player) const
{
    if (!player.IsConnected() || !player.IsAuthenticated()) return false;
    const CEntity* entity = player.GetEntity();
    const CGameWorld* world = entity ? entity->GameWorld() : nullptr;
    return world && world->GetId() == m_world.id && world->GetInstanceGeneration() == m_world.generation;
}
CPlayer* CSquadExtension::Resolve(std::uint32_t id) const
{
    CPlayer* player = m_players ? m_players->FindPlayerById(id) : nullptr;
    const auto generation = m_generations.find(id);
    return player && InWorld(*player) && (generation == m_generations.end() ||
        generation->second == player->SessionGeneration()) ? player : nullptr;
}
void CSquadExtension::Ensure(const CPlayer& player)
{
    m_memberships.try_emplace(player.GetId(), player.GetId());
    m_generations.insert_or_assign(player.GetId(), player.SessionGeneration());
}
std::uint32_t CSquadExtension::Root(std::uint32_t id) const
{
    std::uint32_t current = id;
    for (size_t hops = 0; hops <= m_memberships.size(); ++hops)
    {
        const auto found = m_memberships.find(current);
        if (found == m_memberships.end() || found->second == current) return current;
        current = found->second;
    }
    return id;
}
std::vector<std::uint32_t> CSquadExtension::MemberIds(std::uint32_t root) const
{
    std::vector<std::uint32_t> result;
    for (const auto& [id, parent] : m_memberships)
        if (Root(id) == root && Resolve(id)) result.push_back(id);
    std::sort(result.begin(), result.end()); return result;
}
void CSquadExtension::Refresh()
{
    if (!m_players) return;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> active;
    for (const auto& player : m_players->GetPlayers())
        if (player && InWorld(*player)) active.emplace_back(player->GetId(), player->SessionGeneration());
    if (m_has_active_cache && active == m_last_active) return;
    m_last_active = std::move(active);
    m_has_active_cache = true;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> components;
    std::vector<CPlayer*> newcomers;
    for (const auto& player : m_players->GetPlayers())
    {
        if (!player || !InWorld(*player)) continue;
        const auto generation = m_generations.find(player->GetId());
        const bool reused = generation != m_generations.end() && generation->second != player->SessionGeneration();
        if (reused || !m_memberships.contains(player->GetId())) newcomers.push_back(player.get());
        else components[Root(player->GetId())].push_back(player->GetId());
    }
    m_memberships.clear(); m_generations.clear();
    for (const auto& [old_root, ids] : components)
    {
        const std::uint32_t root = std::find(ids.begin(), ids.end(), old_root) != ids.end() ?
            old_root : *std::min_element(ids.begin(), ids.end());
        for (std::uint32_t id : ids)
        {
            m_memberships[id] = root;
            m_generations[id] = m_players->FindPlayerById(id)->SessionGeneration();
        }
    }
    // A recycled leader ID must not become the leader of its predecessor's
    // surviving squad merely because both used the same numeric root key.
    for (CPlayer* player : newcomers) Ensure(*player);
    std::erase_if(m_invites, [&](const auto& entry) {
        CPlayer* invitee = Resolve(entry.first); CPlayer* inviter = Resolve(entry.second.inviter);
        return !invitee || !inviter || invitee->SessionGeneration() != entry.second.invitee_generation ||
            inviter->SessionGeneration() != entry.second.inviter_generation;
    });
}
std::uint32_t CSquadExtension::RootId(const CPlayer& player) const
{
    if (!InWorld(player) || Resolve(player.GetId()) != &player) return player.GetId();
    const auto ids = MemberIds(Root(player.GetId()));
    const auto root = Root(player.GetId());
    return ids.empty() ? player.GetId() : (std::find(ids.begin(), ids.end(), root) != ids.end() ? root : ids.front());
}
std::vector<CPlayer*> CSquadExtension::Members(const CPlayer& player) const
{
    if (!InWorld(player)) return {};
    if (Resolve(player.GetId()) != &player || !m_memberships.contains(player.GetId()))
        return {const_cast<CPlayer*>(&player)};
    std::vector<CPlayer*> result;
    for (auto id : MemberIds(Root(player.GetId()))) if (CPlayer* member = Resolve(id)) result.push_back(member);
    return result;
}
bool CSquadExtension::Invite(CPlayer& inviter, CPlayer& invitee)
{
    Refresh();
    if (&inviter == &invitee || !InWorld(inviter) || !InWorld(invitee)) return false;
    Ensure(inviter); Ensure(invitee);
    m_invites[invitee.GetId()] = {inviter.GetId(), inviter.SessionGeneration(), invitee.SessionGeneration()};
    return true;
}
CPlayer* CSquadExtension::ConsumeInvite(CPlayer& invitee)
{
    const auto found = m_invites.find(invitee.GetId());
    if (found == m_invites.end()) return nullptr;
    const SInvite invite = found->second; m_invites.erase(found);
    CPlayer* inviter = Resolve(invite.inviter);
    if (!inviter || invitee.SessionGeneration() != invite.invitee_generation ||
        inviter->SessionGeneration() != invite.inviter_generation || !InWorld(invitee) ||
        inviter->GetEntity()->IsMarkedForDes()) return nullptr;
    return inviter;
}
bool CSquadExtension::Accept(CPlayer& invitee, CPlayer& inviter)
{
    Refresh();
    if (&inviter == &invitee || !InWorld(inviter) || !InWorld(invitee)) return false;
    Ensure(inviter); Ensure(invitee);
    const auto target = Root(inviter.GetId()), source = Root(invitee.GetId());
    if (target == source) return false;
    const auto target_ids = MemberIds(target), source_ids = MemberIds(source);
    if (target_ids.size() + source_ids.size() > std::max<size_t>(1, game_config::open_controller_max_squad_size)) return false;
    for (auto id : source_ids) m_memberships[id] = target;
    return true;
}
bool CSquadExtension::Leave(CPlayer& player)
{
    Refresh(); if (!InWorld(player)) return false;
    Ensure(player);
    auto members = MemberIds(Root(player.GetId()));
    if (members.size() <= 1) return false;
    const auto root = Root(player.GetId());
    std::erase(members, player.GetId());
    const auto successor = player.GetId() == root ? members.front() : root;
    for (auto id : members) m_memberships[id] = successor;
    m_memberships[player.GetId()] = player.GetId(); return true;
}
std::string CSquadExtension::Metadata(const std::vector<CPlayer*>& players)
{
    std::string result = "<unseen>(";
    for (const auto* player : players)
    {
        if (!player) continue;
        if (result.size() > 9) result.push_back('\x1f');
        result += player->GetName();
    }
    return result + ')';
}
std::span<const std::string_view> CSquadExtension::Commands() const
{
    static constexpr std::array<std::string_view, 5> names{"invite", "join", "leave", "sq", "squad"}; return names;
}
bool CSquadExtension::HandleCommand(CPlayer& player, const std::vector<std::string>& args)
{
    if (!m_chat || args.empty()) return false;
    const auto reply = [&](CPlayer& to, const std::string& message) { m_chat->SendSystem(to, message); };
    const auto notify = [&](CPlayer& to, const std::string& message) {
        reply(to, message); reply(to, Metadata(Members(to)));
    };
    const auto& name = args[0];
    if (name == "invite" || name == "join")
    {
        if (args.size() < 2) { reply(player, "Usage: /invite [name|accept|refuse]"); return true; }
        const std::string action = Lower(args[1]);
        if (action == "accept" || action == "refuse")
        {
            CPlayer* inviter = ConsumeInvite(player);
            if (!inviter) { reply(player, "<sq-invite-none>()"); return true; }
            if (action == "refuse")
            {
                reply(player, "<sq-invite-refused-you>(" + inviter->GetName() + ")");
                reply(*inviter, "<sq-invite-refused>(" + player.GetName() + ")"); return true;
            }
            if (!Accept(player, *inviter))
            {
                reply(player, "<sq-invite-failed>(" + inviter->GetName() + ")");
                reply(*inviter, "<sq-invite-failed-them>(" + player.GetName() + ")"); return true;
            }
            for (CPlayer* member : Members(player))
                notify(*member, member == &player ? "<sq-joined-you>(" + inviter->GetName() + ")" :
                    "<sq-joined>(" + player.GetName() + ")");
            return true;
        }
        CPlayer* target = nullptr;
        const auto target_name = Lower(Join(args, 1));
        for (const auto& candidate : m_players->GetPlayers())
            if (candidate && (Lower(candidate->GetName()) == target_name || Lower(candidate->GetAccountName()) == target_name))
            { target = candidate.get(); break; }
        if (!target || !Invite(player, *target)) { reply(player, "<sq-invite-invalid>()"); return true; }
        reply(player, "<sq-invite-sent>(" + target->GetName() + ")");
        reply(*target, "<sq-invite>(" + player.GetName() + ")"); return true;
    }
    if (name == "leave")
    {
        auto old = Members(player);
        if (!Leave(player)) { reply(player, "failed to leave squad"); return true; }
        for (CPlayer* member : old) if (member != &player) notify(*member, player.GetName() + " has left the squad");
        notify(player, "you have left the squad"); return true;
    }
    if (name != "sq" && name != "squad") return false;
    if (args.size() < 2) { reply(player, "Usage: /sq [message]"); return true; }
    if (player.IsMuted())
    { reply(player, "You are muted for " + std::to_string(static_cast<int>(std::ceil(player.GetMuteTimer()))) + "s."); return true; }
    if (Members(player).size() <= 1) { reply(player, "you are not in a squad"); return true; }
    CEntity* entity = player.GetEntity(); if (!entity) return true;
    m_chat->Broadcast(m_chat->Submit(entity->GameWorld(), entity->Pos(), EChatFlag::Squad, player.GetId(),
                                     player.GetName(), Join(args, 1)));
    return true;
}
void CSquadExtension::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CJsonOwner members = MakeJsonArray();
    std::vector<std::uint32_t> ids;
    for (const auto& [id, parent] : m_memberships) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
    {
        auto value = MakeJsonObject(); CSnapshotWriter entry(value.get());
        entry.Field("player_id", id); entry.Field("parent_id", Root(id));
        const auto gen = m_generations.find(id);
        if (gen != m_generations.end()) entry.UInt64("session_generation", gen->second);
        AppendJson(members.get(), value.release());
    }
    writer.Node("squads", members.release());
}
bool CSquadExtension::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version != 1 || !reader.IsValid()) { error = "Invalid or unsupported squad extension state"; return false; }
    const json_t* entries = reader.Node("squads");
    if (!json_is_array(entries) || json_array_size(entries) > 4096) { error = "Invalid squad member list"; return false; }
    std::unordered_map<std::uint32_t, std::uint32_t> memberships;
    std::unordered_map<std::uint32_t, std::uint64_t> generations;
    for (size_t i = 0; i < json_array_size(entries); ++i)
    {
        CSnapshotReader entry(json_array_get(entries, i));
        const auto id = entry.UInt32("player_id"), parent = entry.UInt32("parent_id");
        if (!entry.IsValid() || !id || !parent || !memberships.emplace(id, parent).second)
        { error = "Invalid duplicate squad member"; return false; }
        const auto generation = entry.UInt64("session_generation");
        if (entry.Has("session_generation") && (!generation || generation == UINT64_MAX))
        { error = "Invalid squad player generation"; return false; }
        if (generation) generations[id] = generation;
    }
    for (const auto& [id, parent] : memberships)
    {
        std::uint32_t current = id;
        size_t hops = 0;
        for (; hops <= memberships.size(); ++hops)
        {
            const auto found = memberships.find(current);
            if (found == memberships.end()) { error = "Squad references a missing member"; return false; }
            if (found->second == current) break;
            current = found->second;
        }
        if (hops > memberships.size()) { error = "Squad membership contains a cycle"; return false; }
    }
    m_memberships = std::move(memberships); m_generations = std::move(generations); m_invites.clear();
    m_last_active.clear(); m_has_active_cache = false;
    // Valid legacy parent forests are normalized only during restore.
    for (auto& [id, parent] : m_memberships) parent = Root(id);
    return true;
}
