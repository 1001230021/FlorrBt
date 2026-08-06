#include "open_reward_service.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/drop_rate.h"
#include "../../../Shared/game_config.h"
#include "../controllers/melee_controller.h"
#include "../entities/drop.h"
#include "../entities/mob.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "opencontroller.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
struct lootable_player
{
    CPlayer* player = nullptr;
    float damage = 0.f;
};

bool IsActivePlayer(const CGameContext* context, const CPlayer* player)
{
    if (!context || !player) return false;
    for (const auto& existing : context->Players())
        if (existing.get() == player) return true;
    return false;
}

bool DefeatRewardsSuppressed(CGameWorld& world, const CMobBase& mob)
{
    if (mob.HasTag(EEntityTag::NoDefeatRewards)) return true;

    const auto* controller = dynamic_cast<const CSummonedMeleeController*>(mob.GetController());
    if (!controller) return false;

    CGameContext* context = world.GameContext();
    CEntity* owner = controller->GetOwner(&world);
    return context && owner && context->FindPlayerFromEntity(owner) != nullptr;
}

std::string MobName(const CMobBase& mob)
{
    const CMobPrototype* proto = FindMobPrototype(mob.GetMobType());
    return proto && !proto->m_name.empty() ? proto->m_name : std::string(GetMobTypeName(mob.GetMobType()));
}

float TotalDamageRecorded(const CMobBase& mob)
{
    float total = 0.f;
    for (const CDamageData& damage_data : mob.GetDamageData())
        if (damage_data.m_total_dmg > 0.f) total += damage_data.m_total_dmg;
    return total;
}

std::string DamageSummary(const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    std::vector<const CDamageData*> records;
    records.reserve(mob.GetDamageData().size());
    for (const CDamageData& damage_data : mob.GetDamageData())
        if (damage_data.m_total_dmg > 0.f) records.push_back(&damage_data);

    std::sort(records.begin(), records.end(),
              [](const CDamageData* lhs, const CDamageData* rhs) { return lhs->m_total_dmg > rhs->m_total_dmg; });

    std::ostringstream output;
    for (size_t i = 0; i < records.size() && i < 5; ++i)
    {
        if (i > 0) output << ", ";
        const CDamageData& record = *records[i];
        const bool active = IsActivePlayer(context, record.m_player);
        output << (active ? record.m_player->GetName() : "<stale>") << "#"
               << (active ? record.m_player->GetId() : 0) << "=" << record.m_total_dmg;
    }
    if (records.size() > 5) output << ", ...";
    std::string result = output.str();
    return result.empty() ? "<none>" : result;
}

std::string LootableSummary(const std::vector<lootable_player>& players)
{
    std::ostringstream output;
    for (size_t i = 0; i < players.size(); ++i)
    {
        if (i > 0) output << ", ";
        const lootable_player& entry = players[i];
        output << (entry.player ? entry.player->GetName() : "<null>") << "#"
               << (entry.player ? entry.player->GetId() : 0) << "=" << entry.damage;
    }
    std::string result = output.str();
    return result.empty() ? "<none>" : result;
}

std::string DropSummary(const std::vector<SDropRate>& drops)
{
    std::ostringstream output;
    for (size_t i = 0; i < drops.size(); ++i)
    {
        if (i > 0) output << ", ";
        output << GetRarityName(drops[i].rarity) << " " << GetPetalTypeName(drops[i].type);
    }
    std::string result = output.str();
    return result.empty() ? "<none>" : result;
}

void LogSuperLootIssue(const CMobBase& mob, const std::string& reason)
{
    const SMobStats* stats = mob.GetFinalStats();
    const float max_health = stats ? stats->max_health : 0.f;
    LOG_WARN("loot", reason + " mob=" + std::string(GetRarityName(mob.GetRarity())) + " " +
                         std::string(GetMobTypeName(mob.GetMobType())) + " id=" + std::to_string(mob.m_id) +
                         " max_health=" + std::to_string(max_health) + " recorded_damage=" +
                         std::to_string(TotalDamageRecorded(mob)) + " damage_sources=" + DamageSummary(mob));
}

std::vector<CPlayer*> FindDefeatCreditPlayers(COpenController& controller, CGameWorld& world, const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    std::unordered_map<CPlayer*, float> damage_by_player;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!IsActivePlayer(context, damage_data.m_player) || damage_data.m_total_dmg <= 0.f) continue;
        damage_by_player[damage_data.m_player] += damage_data.m_total_dmg;
    }
    if (damage_by_player.empty()) return {};

    auto damage_for = [&damage_by_player](CPlayer* player) {
        const auto it = damage_by_player.find(player);
        return it == damage_by_player.end() ? 0.f : it->second;
    };
    auto damage_order = [&damage_for](CPlayer* lhs, CPlayer* rhs) {
        const float lhs_damage = damage_for(lhs);
        const float rhs_damage = damage_for(rhs);
        return lhs_damage != rhs_damage ? lhs_damage > rhs_damage : lhs->GetId() < rhs->GetId();
    };

    std::vector<CPlayer*> contributors;
    contributors.reserve(damage_by_player.size());
    for (const auto& [player, damage] : damage_by_player)
    {
        (void)damage;
        contributors.push_back(player);
    }
    std::sort(contributors.begin(), contributors.end(), damage_order);

    CPlayer* top_player = contributors.front();
    std::vector<CPlayer*> squad = controller.GetSquadPlayerList(world, *top_player);
    if (squad.size() <= 1) return { top_player };
    if (std::find(squad.begin(), squad.end(), top_player) == squad.end()) squad.push_back(top_player);
    std::sort(squad.begin(), squad.end(), damage_order);
    return squad;
}

std::string JoinPlayerNames(const std::vector<CPlayer*>& players)
{
    std::string result;
    for (CPlayer* player : players)
    {
        if (!player) continue;
        if (!result.empty()) result += ", ";
        result += player->GetName();
    }
    if (const size_t separator = result.rfind(", "); separator != std::string::npos)
        result.replace(separator, 2, " and ");
    return result;
}

void ReportMobDefeat(COpenController& controller, CGameWorld& world, const CMobBase& mob)
{
    CGameContext* context = world.GameContext();
    if (!context || !context->Events().ShouldReportRarity(mob.GetRarity(),
                                                          game_config::min_mob_spawn_report_rarity))
        return;

    std::string action = "has been defeated";
    const std::string credited_players = JoinPlayerNames(FindDefeatCreditPlayers(controller, world, mob));
    if (!credited_players.empty()) action += " by " + credited_players;
    context->Events().ReportMob(action, mob.GetRarity(), MobName(mob));
}

std::vector<lootable_player> FindLootablePlayers(COpenController& controller, CGameWorld& world, const CMobBase& mob)
{
    const CGameContext* context = const_cast<CMobBase&>(mob).GameContext();
    const bool above_ultra = IsAboveRarity(mob.GetRarity(), ERarity::Ultra);
    const size_t max_lootable =
        above_ultra ? game_config::max_lootable_player_above_ultra : game_config::max_lootable_players;
    const float min_damage_rate = above_ultra ? game_config::open_loot_min_damage_rate_above_ultra
                                              : game_config::open_loot_min_damage_rate_normal;

    float total_damage = 0.f;
    std::unordered_map<CPlayer*, float> damage_by_player;
    std::vector<CPlayer*> contributors;
    for (const CDamageData& damage_data : mob.GetDamageData())
    {
        if (!IsActivePlayer(context, damage_data.m_player) || damage_data.m_total_dmg <= 0.f) continue;
        total_damage += damage_data.m_total_dmg;
        if (!damage_by_player.contains(damage_data.m_player)) contributors.push_back(damage_data.m_player);
        damage_by_player[damage_data.m_player] += damage_data.m_total_dmg;
    }

    for (CPlayer* contributor : contributors)
    {
        for (CPlayer* member : controller.GetSquadPlayerList(world, *contributor))
            if (IsActivePlayer(context, member)) damage_by_player.try_emplace(member, 0.f);
    }

    const SMobStats* stats = mob.GetFinalStats();
    const float max_health = stats ? stats->max_health : 0.f;
    const float min_damage = std::min(total_damage, max_health) * min_damage_rate;

    std::vector<lootable_player> candidates;
    candidates.reserve(damage_by_player.size());
    for (const auto& [player, damage] : damage_by_player)
        candidates.push_back({ player, damage });

    std::unordered_map<std::uint32_t, std::vector<lootable_player*>> squad_damage;
    for (lootable_player& candidate : candidates)
        if (candidate.player) squad_damage[controller.GetSquadRootId(world, *candidate.player)].push_back(&candidate);

    const float adjustment_range = std::max(0.f, game_config::open_squad_loot_damage_range);
    for (auto& [root_id, members] : squad_damage)
    {
        (void)root_id;
        if (members.size() < 2) continue;

        float minimum = members.front()->damage;
        float maximum = minimum;
        float total = 0.f;
        for (const lootable_player* member : members)
        {
            minimum = std::min(minimum, member->damage);
            maximum = std::max(maximum, member->damage);
            total += member->damage;
        }

        const float average = total / static_cast<float>(members.size());
        const float span = maximum - minimum;
        for (lootable_player* member : members)
        {
            if (span <= std::numeric_limits<float>::epsilon()) member->damage = average;
            else
            {
                const float progress = (member->damage - minimum) / span;
                member->damage = average - adjustment_range + progress * adjustment_range * 2.f;
            }
        }
    }

    std::erase_if(candidates, [min_damage](const lootable_player& candidate) { return candidate.damage < min_damage; });
    std::sort(candidates.begin(), candidates.end(), [](const lootable_player& lhs, const lootable_player& rhs) {
        if (lhs.damage != rhs.damage) return lhs.damage > rhs.damage;
        return (lhs.player ? lhs.player->GetId() : 0) < (rhs.player ? rhs.player->GetId() : 0);
    });
    if (candidates.size() > max_lootable) candidates.resize(max_lootable);
    return candidates;
}

sf::Vector2f DropSpreadOffset(size_t index, size_t count)
{
    if (count <= 1) return { 0.f, 0.f };
    const float angle = 2.f * game_config::pi * static_cast<float>(index) / static_cast<float>(count);
    const float radius = std::max(0.f, game_config::open_drop_spread_radius);
    return { std::cos(angle) * radius, std::sin(angle) * radius };
}
} // namespace

void COpenRewardService::OnMobSpawned(CGameWorld& world, CMobBase& mob)
{
    if (DefeatRewardsSuppressed(world, mob)) return;
    CGameContext* context = world.GameContext();
    if (!context || !context->Events().ShouldReportRarity(mob.GetRarity(),
                                                          game_config::min_mob_spawn_report_rarity))
        return;
    context->Events().ReportMobSpawn(world, "has spawned", mob.GetRarity(), MobName(mob));
}

void COpenRewardService::OnMobDefeated(COpenController& controller, CGameWorld& world, CMobBase& mob)
{
    if (mob.GetMobType() == EMobType::SummonedBeetle || mob.GetMobType() == EMobType::SummonedSoldierAnt ||
        DefeatRewardsSuppressed(world, mob))
        return;

    ReportMobDefeat(controller, world, mob);

    std::vector<lootable_player> lootable_players = FindLootablePlayers(controller, world, mob);
    const bool log_loot_issues = IsAtLeastRarity(mob.GetRarity(), ERarity::Super);
    if (lootable_players.empty())
    {
        if (log_loot_issues) LogSuperLootIssue(mob, "No lootable players");
        return;
    }

    const std::vector<SDropRate>& rates = QueryDropRates(mob.GetMobType(), mob.GetRarity());
    if (rates.empty())
    {
        if (log_loot_issues) LogSuperLootIssue(mob, "No drop table");
        return;
    }

    for (const lootable_player& lootable : lootable_players)
    {
        if (!lootable.player)
        {
            if (log_loot_issues) LogSuperLootIssue(mob, "Skipped lootable null player");
            continue;
        }

        std::vector<SDropRate> drops = RollDrops(mob.GetMobType(), mob.GetRarity());
        if (drops.empty())
        {
            if (log_loot_issues)
                LOG_WARN("loot", "Rolled zero drops mob=" + std::string(GetRarityName(mob.GetRarity())) + " " +
                                     std::string(GetMobTypeName(mob.GetMobType())) + " id=" +
                                     std::to_string(mob.m_id) + " player=" + lootable.player->GetName() + "#" +
                                     std::to_string(lootable.player->GetId()) + " lootables=" +
                                     LootableSummary(lootable_players) + " damage_sources=" + DamageSummary(mob));
            continue;
        }

        for (size_t i = 0; i < drops.size(); ++i)
        {
            const SDropRate& drop = drops[i];
            auto entity = std::make_unique<CDrop>(&world, mob.m_pos + DropSpreadOffset(i, drops.size()), drop.type,
                                                  drop.rarity, lootable.player->GetId());
            world.InsertEntity(std::move(entity));
        }
        if (log_loot_issues && mob.GetMobType() == EMobType::AntHole)
            LOG_INFO("loot", "Generated AntHole drops mob=" + std::string(GetRarityName(mob.GetRarity())) +
                                 " id=" + std::to_string(mob.m_id) + " player=" + lootable.player->GetName() + "#" +
                                 std::to_string(lootable.player->GetId()) + " drops=" + DropSummary(drops));
    }
}
