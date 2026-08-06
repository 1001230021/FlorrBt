#include "blood_sacrifice_ritual.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include "../../server.h"
#include "../gameworld.h"
#include "mob.h"
#include <algorithm>

namespace
{
float BloodSacrificeRarityProgress(ERarity rarity)
{
    const float min_rank = static_cast<float>(GetRarityValueRank(ERarity::Common));
    const float max_rank = static_cast<float>(GetRarityValueRank(ERarity::Primordial));
    const float rank = std::clamp(static_cast<float>(GetRarityValueRank(rarity)), min_rank, max_rank);
    return max_rank > min_rank ? (rank - min_rank) / (max_rank - min_rank) : 0.f;
}

float BloodSacrificeRadius(ERarity rarity)
{
    const float progress = BloodSacrificeRarityProgress(rarity);
    const float scale = game_config::blood_sacrifice_radius_scale_min +
                        (game_config::blood_sacrifice_radius_scale_max -
                         game_config::blood_sacrifice_radius_scale_min) *
                            progress;
    return game_config::blood_sacrifice_inner_star_radius *
           game_config::blood_sacrifice_outer_star_radius_multiplier * scale;
}

float BloodSacrificeFadeDuration(ERarity rarity)
{
    const float progress = BloodSacrificeRarityProgress(rarity);
    const float duration = game_config::blood_sacrifice_fade_duration_min +
                           (game_config::blood_sacrifice_fade_duration_max -
                            game_config::blood_sacrifice_fade_duration_min) *
                               progress;
    return std::max(game_config::blood_sacrifice_min_phase_duration, duration);
}
} // namespace

CBloodSacrificeRitual::CBloodSacrificeRitual(CGameWorld* world, sf::Vector2f pos, EMobType mob_type, ERarity rarity,
                                             float timer)
    : CEntity(world, pos.x, pos.y, BloodSacrificeRadius(rarity),
              MakeEntityType(EEntityType::Effect, server_blood_sacrifice_entity_type)),
      m_mob_type(mob_type), m_rarity(rarity),
      m_draw_duration(std::max(game_config::blood_sacrifice_min_phase_duration, timer)),
      m_fade_duration(BloodSacrificeFadeDuration(rarity))
{
    m_health = 1.f;
    m_mass = 0.f;
    m_team = 0;
}

float CBloodSacrificeRitual::EffectProgress() const
{
    const float spawn_progress = std::clamp(game_config::blood_sacrifice_spawn_progress, 0.f, 1.f);
    if (m_draw_duration <= game_config::blood_sacrifice_min_phase_duration)
        return std::clamp(spawn_progress +
                              m_age / std::max(game_config::blood_sacrifice_min_phase_duration, m_fade_duration) *
                                  (1.f - spawn_progress),
                          0.f, 1.f);

    if (m_age < m_draw_duration) return std::clamp(m_age / m_draw_duration * spawn_progress, 0.f, spawn_progress);

    const float fade_progress =
        (m_age - m_draw_duration) / std::max(game_config::blood_sacrifice_min_phase_duration, m_fade_duration);
    return std::clamp(spawn_progress + fade_progress * (1.f - spawn_progress), 0.f, 1.f);
}

void CBloodSacrificeRitual::Tick(float dt)
{
    if (m_is_marked_for_des) return;

    m_age += std::max(0.f, dt);
    if (!m_spawned && m_age < m_draw_duration) return;

    if (m_spawned)
    {
        if (m_age >= m_draw_duration + m_fade_duration) MarkForDestroy(EEntityRemovalReason::Expired);
        return;
    }

    m_spawned = true;

    bool spawned = false;
    int spawned_id = -1;
    CGameWorld* world = GameWorld();
    if (world && m_mob_type != EMobType::None && m_rarity != ERarity::Null)
    {
        auto mob = CreateMob(m_mob_type, world, m_pos, m_rarity);
        if (mob)
        {
            CMobBase* raw_mob = dynamic_cast<CMobBase*>(world->InsertEntity(std::move(mob)));
            if (raw_mob)
            {
                spawned = true;
                spawned_id = raw_mob->m_id;
                if (CServer::MeetsPetalReportRarity(raw_mob->GetRarity(), game_config::min_mob_spawn_report_rarity))
                {
                    const CMobPrototype* proto = FindMobPrototype(raw_mob->GetMobType());
                    const std::string mob_name = proto && !proto->m_name.empty()
                                                     ? proto->m_name
                                                     : std::string(GetMobTypeName(raw_mob->GetMobType()));
                    if (CServer* server = CServer::GetInstance())
                        server->BroadcastMobReport("has been summoned", raw_mob->GetRarity(), mob_name);
                }
            }
        }
    }

    if (spawned)
    {
        LOG_INFO("blood_sacrifice", "Ritual spawned " + std::string(GetRarityName(m_rarity)) + " " +
                                        std::string(GetMobTypeName(m_mob_type)) + " id " + std::to_string(spawned_id) +
                                        " at " + std::to_string(m_pos.x) + "," + std::to_string(m_pos.y));
    } else
    {
        LOG_INFO("blood_sacrifice", "Ritual failed to spawn " + std::string(GetRarityName(m_rarity)) + " " +
                                        std::string(GetMobTypeName(m_mob_type)) + " at " + std::to_string(m_pos.x) +
                                        "," + std::to_string(m_pos.y));
    }
}
