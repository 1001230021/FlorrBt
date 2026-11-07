#include "party_extension.h"
#include "../../../Shared/game_config.h"
#include "../../HotReload/snapshot_archive.h"
#include "../gameworld.h"
#include "../player.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include "../../../Shared/utf8.h"

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

const SExtensionDescriptor& CPartyExtension::Descriptor() const
{
    static constexpr std::array<std::string_view, 1> dependencies{"extension.chat"};
    static const SExtensionDescriptor descriptor{"extension.party", EExtensionScope::Server, 1, dependencies, false};
    return descriptor;
}
bool CPartyExtension::OnAttach(CExtensionContext& context, std::string& error)
{
    if (!context.chat) { error = "Squad extension requires chat"; return false; }
    m_players = &context.players; m_chat = context.chat;
    m_delivery = &context.party_delivery;
    return true;
}
void CPartyExtension::OnDetach() noexcept { m_players = nullptr; m_chat = nullptr; m_delivery = nullptr; }
bool CPartyExtension::InWorld(const CPlayer& player) const
{
    return m_players && player.IsAuthenticated() && m_players->FindPlayerById(player.GetId()) == &player;
}
CPlayer* CPartyExtension::Resolve(std::uint32_t id) const
{
    CPlayer* player = m_players ? m_players->FindPlayerById(id) : nullptr;
    const auto generation = m_generations.find(id);
    const auto account = m_accounts.find(id);
    if (player && account != m_accounts.end() && account->second != player->GetAccountName()) return nullptr;
    return player && InWorld(*player) && (generation == m_generations.end() ||
        generation->second == player->SessionGeneration()) ? player : nullptr;
}
void CPartyExtension::Ensure(const CPlayer& player)
{
    m_memberships.try_emplace(player.GetId(), player.GetId());
    m_generations.insert_or_assign(player.GetId(), player.SessionGeneration());
    m_accounts.insert_or_assign(player.GetId(), player.GetAccountName());
}
std::uint32_t CPartyExtension::Root(std::uint32_t id) const
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
void CPartyExtension::RebuildGroups()
{
    m_groups.clear();
    for (const auto& [id, parent] : m_memberships) m_groups[Root(id)].push_back(id);
    for (auto& [root, ids] : m_groups) std::sort(ids.begin(), ids.end());
}
std::vector<std::uint32_t> CPartyExtension::MemberIds(std::uint32_t root) const
{
    std::vector<std::uint32_t> result;
    const auto group = m_groups.find(root);
    if (group != m_groups.end())
        for (auto id : group->second) if (Resolve(id)) result.push_back(id);
    return result;
}
void CPartyExtension::RefreshMembership()
{
    if (!m_players) return;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> active;
    for (const auto& player : m_players->GetPlayers())
        if (player && InWorld(*player)) active.emplace_back(player->GetId(), player->SessionGeneration());
    const bool same_accounts = std::all_of(active.begin(), active.end(), [&](const auto& entry) {
        const auto account = m_accounts.find(entry.first);
        return account == m_accounts.end() || account->second == m_players->FindPlayerById(entry.first)->GetAccountName();
    });
    if (m_has_active_cache && active == m_last_active && same_accounts) return;
    m_last_active = std::move(active);
    m_has_active_cache = true;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> components;
    std::vector<CPlayer*> newcomers;
    for (const auto& player : m_players->GetPlayers())
    {
        if (!player || !InWorld(*player)) continue;
        const auto generation = m_generations.find(player->GetId());
        const auto account = m_accounts.find(player->GetId());
        const bool reused = (generation != m_generations.end() && generation->second != player->SessionGeneration()) ||
            (account != m_accounts.end() && account->second != player->GetAccountName());
        if (reused || !m_memberships.contains(player->GetId())) newcomers.push_back(player.get());
        else components[Root(player->GetId())].push_back(player->GetId());
    }
    m_memberships.clear(); m_generations.clear(); m_accounts.clear();
    for (const auto& [old_root, ids] : components)
    {
        const std::uint32_t root = std::find(ids.begin(), ids.end(), old_root) != ids.end() ?
            old_root : *std::min_element(ids.begin(), ids.end());
        for (std::uint32_t id : ids)
        {
            m_memberships[id] = root;
            m_generations[id] = m_players->FindPlayerById(id)->SessionGeneration();
            m_accounts[id] = m_players->FindPlayerById(id)->GetAccountName();
        }
    }
    // ID 复用的新会话不得继承前任队长仍存续的队伍。
    for (CPlayer* player : newcomers) Ensure(*player);
    std::erase_if(m_invites, [&](const auto& entry) {
        CPlayer* invitee = Resolve(entry.first); CPlayer* inviter = Resolve(entry.second.inviter);
        return !invitee || !inviter || invitee->SessionGeneration() != entry.second.invitee_generation ||
            inviter->SessionGeneration() != entry.second.inviter_generation;
    });
    RebuildGroups();
}
std::uint32_t CPartyExtension::RootId(const CPlayer& player) const
{
    if (!InWorld(player) || Resolve(player.GetId()) != &player) return player.GetId();
    const auto ids = MemberIds(Root(player.GetId()));
    const auto root = Root(player.GetId());
    return ids.empty() ? player.GetId() : (std::find(ids.begin(), ids.end(), root) != ids.end() ? root : ids.front());
}
std::vector<CPlayer*> CPartyExtension::Members(const CPlayer& player) const
{
    if (!InWorld(player)) return {};
    if (Resolve(player.GetId()) != &player || !m_memberships.contains(player.GetId()))
        return {m_players->FindPlayerById(player.GetId())};
    std::vector<CPlayer*> result;
    for (auto id : MemberIds(Root(player.GetId()))) if (CPlayer* member = Resolve(id)) result.push_back(member);
    return result;
}
bool CPartyExtension::Invite(CPlayer& inviter, CPlayer& invitee)
{
    RefreshMembership();
    if (&inviter == &invitee || !InWorld(inviter) || !InWorld(invitee)) return false;
    Ensure(inviter); Ensure(invitee);
    m_invites[invitee.GetId()] = {inviter.GetId(), inviter.SessionGeneration(), invitee.SessionGeneration()};
    return true;
}
CPlayer* CPartyExtension::ConsumeInvite(CPlayer& invitee)
{
    const auto found = m_invites.find(invitee.GetId());
    if (found == m_invites.end()) return nullptr;
    const SInvite invite = found->second; m_invites.erase(found);
    CPlayer* inviter = Resolve(invite.inviter);
    if (!inviter || Resolve(invitee.GetId()) != &invitee || invitee.SessionGeneration() != invite.invitee_generation ||
        inviter->SessionGeneration() != invite.inviter_generation || !InWorld(invitee)) return nullptr;
    return inviter;
}
bool CPartyExtension::Accept(CPlayer& invitee, CPlayer& inviter)
{
    RefreshMembership();
    if (&inviter == &invitee || !InWorld(inviter) || !InWorld(invitee)) return false;
    Ensure(inviter); Ensure(invitee);
    const auto target = Root(inviter.GetId()), source = Root(invitee.GetId());
    if (target == source) return false;
    const auto target_ids = MemberIds(target), source_ids = MemberIds(source);
    if (target_ids.size() + source_ids.size() > std::clamp<size_t>(game_config::open_controller_max_squad_size, 1, SPartyState::max_members)) return false;
    for (auto id : source_ids) m_memberships[id] = target;
    RebuildGroups(); return true;
}
bool CPartyExtension::Leave(CPlayer& player)
{
    RefreshMembership(); if (!InWorld(player)) return false;
    Ensure(player);
    auto members = MemberIds(Root(player.GetId()));
    if (members.size() <= 1) return false;
    const auto root = Root(player.GetId());
    std::erase(members, player.GetId());
    const auto successor = player.GetId() == root ? members.front() : root;
    for (auto id : members) m_memberships[id] = successor;
    m_memberships[player.GetId()] = player.GetId(); RebuildGroups(); return true;
}
std::span<const std::string_view> CPartyExtension::Commands() const
{
    static constexpr std::array<std::string_view, 5> names{"invite", "join", "leave", "sq", "squad"}; return names;
}
bool CPartyExtension::HandleCommand(CPlayer& player, const std::vector<std::string>& args)
{
    if (!m_chat || args.empty()) return false;
    const auto reply = [&](CPlayer& to, const std::string& message) { m_chat->SendSystem(to, message); };
    const auto notify = [&](CPlayer& to, const std::string& message) {
        reply(to, message);
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
            if (candidate && InWorld(*candidate) && (Lower(candidate->GetName()) == target_name || Lower(candidate->GetAccountName()) == target_name))
            {
                if (target) { reply(player, "<sq-invite-invalid>()"); return true; }
                target = candidate.get();
            }
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
    CEntity* entity = player.GetEntity();
    m_chat->Broadcast(m_chat->Submit(entity ? entity->GameWorld() : player.GetWorld(), entity ? entity->Pos() : florrbt::Vector2f{}, EChatFlag::Squad, player.GetId(),
                                     player.GetName(), Join(args, 1)));
    return true;
}
void CPartyExtension::CaptureSnapshot(CSnapshotWriter& writer) const
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
bool CPartyExtension::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
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
    std::unordered_map<std::uint32_t, size_t> sizes;
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
        if (++sizes[current] > SPartyState::max_members) { error = "队伍成员超出协议上限"; return false; }
    }
    m_memberships = std::move(memberships); m_generations = std::move(generations); m_invites.clear();
    m_last_active.clear(); m_has_active_cache = false;
    // 旧快照允许多级父节点，恢复时统一压平。
    for (auto& [id, parent] : m_memberships) parent = Root(id);
    m_accounts.clear(); m_accept_legacy = false; m_published.clear(); RebuildGroups();
    return true;
}

void CPartyExtension::BeginRestore()
{
    m_memberships.clear(); m_generations.clear(); m_accounts.clear(); m_groups.clear();
    m_published.clear(); m_invites.clear(); m_last_active.clear();
    m_has_active_cache = false; m_accept_legacy = true;
}

bool CPartyExtension::RestoreDefaultState(std::string&)
{
    BeginRestore(); return true;
}

bool CPartyExtension::ReadLegacy(const CSnapshotReader& reader, std::vector<SLegacyPartyMember>& output, std::string& error)
{
    CPartyExtension checked;
    if (!checked.RestoreSnapshot(reader, 1, error)) return false;
    output.clear(); output.reserve(checked.m_memberships.size());
    for (const auto& [id, parent] : checked.m_memberships)
    {
        const auto generation = checked.m_generations.find(id);
        output.push_back({id, parent, generation == checked.m_generations.end() ? 0 : generation->second});
    }
    return true;
}

void CPartyExtension::ImportLegacy(SWorldHandle world, const std::vector<SLegacyPartyMember>& members)
{
    if (!m_accept_legacy || !m_players) return;
    RefreshMembership();
    std::unordered_map<std::uint32_t, std::vector<CPlayer*>> groups;
    for (const auto& member : members)
    {
        auto* player = m_players->FindPlayerById(member.id);
        const auto* location = player ? player->GetWorld() : nullptr;
        if (!player || !InWorld(*player) || !location ||
            SWorldHandle{location->GetId(), location->GetInstanceGeneration()} != world ||
            (member.generation && member.generation != player->SessionGeneration())) continue;
        groups[member.parent].push_back(player);
    }
    for (auto& [old_root, group] : groups)
    {
        auto leader = std::find_if(group.begin(), group.end(), [old_root](const auto* p) { return p->GetId() == old_root; });
        if (leader == group.end()) leader = std::min_element(group.begin(), group.end(), [](auto* a, auto* b) { return a->GetId() < b->GetId(); });
        for (auto* member : group) { Ensure(*member); m_memberships[member->GetId()] = (*leader)->GetId(); }
    }
    RebuildGroups();
}

bool CPartyExtension::Publish(CPlayer& player, bool force)
{
    if (!m_delivery || !player.IsConnected() || !InWorld(player)) return false;
    SPartyState state;
    state.leader_id = RootId(player);
    auto* viewer_world = player.GetWorld();
    for (auto* candidate : Members(player))
    {
        SPartyMember member;
        member.player_id = candidate->GetId(); member.session_generation = candidate->SessionGeneration();
        member.name = std::string(TruncateUtf8(candidate->GetName(), UINT8_MAX));
        member.connected = candidate->IsConnected();
        auto* world = candidate->GetWorld();
        if (world) { member.world_id = world->GetId(); member.world_generation = world->GetInstanceGeneration(); }
        auto* entity = candidate->GetEntity();
        const auto* rules = viewer_world ? viewer_world->GetController() : nullptr;
        if (entity && world == viewer_world && !entity->IsMarkedForDes() && !world->IsRecovering() &&
            (candidate == &player || (world->ExtensionConfig().squads &&
                (!rules || rules->SnapshotEntityVisibleToPlayer(*world, player, *entity)))))
        {
            member.has_entity = true; member.entity_id = static_cast<uint32_t>(entity->Id());
            member.entity_generation = entity->Generation();
        }
        if (member.player_id == state.leader_id) state.leader_generation = member.session_generation;
        state.members.push_back(std::move(member));
    }
    const auto old = m_published.find(player.GetId());
    if (!force && old != m_published.end() && old->second == state) return true;
    if (!m_delivery->DeliverParty(player, state)) return false;
    m_published.insert_or_assign(player.GetId(), std::move(state)); return true;
}

bool CPartyExtension::PublishPlayerState(CPlayer& player)
{
    RefreshMembership();
    return Publish(player, true);
}

void CPartyExtension::Refresh()
{
    RefreshMembership();
    if (!m_players) return;
    std::erase_if(m_published, [&](const auto& saved) { return !Resolve(saved.first); });
    for (const auto& player : m_players->GetPlayers())
        if (player) Publish(*player, false);
}
