#include "blood_sacrifice_ritual.h"
#include "mob.h"
#include "../gameworld.h"
#include "../../server.h"
#include "../../../Engine/logger.h"
#include "../../../Shared/game_config.h"
#include <algorithm>

namespace
{
constexpr float blood_sacrifice_inner_star_radius = 1024.f;
constexpr float blood_sacrifice_outer_star_radius = blood_sacrifice_inner_star_radius * 2.f;
constexpr float blood_sacrifice_fade_duration = 60.f;
constexpr float min_phase_duration = 0.001f;
}

CBloodSacrificeRitual::CBloodSacrificeRitual(CGameWorld* world, sf::Vector2f pos, EMobType mob_type, ERarity rarity,
                                             float timer)
    : CEntity(world, pos.x, pos.y, blood_sacrifice_outer_star_radius), m_mob_type(mob_type), m_rarity(rarity),
      m_draw_duration(std::max(min_phase_duration, timer)), m_fade_duration(blood_sacrifice_fade_duration)
{
    m_health = 1.f;
    m_mass = 0.f;
    m_team = 0;
}

float CBloodSacrificeRitual::EffectProgress() const
{
    if (m_draw_duration <= min_phase_duration)
        return std::clamp(0.5f + m_age / std::max(min_phase_duration, m_fade_duration) * 0.5f, 0.f, 1.f);

    if (m_age < m_draw_duration)
        return std::clamp(m_age / m_draw_duration * 0.5f, 0.f, 0.5f);

    const float fade_progress = (m_age - m_draw_duration) / std::max(min_phase_duration, m_fade_duration);
    return std::clamp(0.5f + fade_progress * 0.5f, 0.f, 1.f);
}

void CBloodSacrificeRitual::Tick(float dt)
{
    if (m_is_marked_for_des) return;

    m_age += std::max(0.f, dt);
    if (!m_spawned && m_age < m_draw_duration) return;

    if (m_spawned)
    {
        if (m_age >= m_draw_duration + m_fade_duration)
            m_is_marked_for_des = true;
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
                    const CMobPrototype* proto = FindMobPrototype(raw_mob->m_mob_type);
                    const std::string mob_name = proto && !proto->m_name.empty()
                                                     ? proto->m_name
                                                     : std::string(GetMobTypeName(raw_mob->m_mob_type));
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
    }
    else
    {
        LOG_INFO("blood_sacrifice", "Ritual failed to spawn " + std::string(GetRarityName(m_rarity)) + " " +
                                       std::string(GetMobTypeName(m_mob_type)) + " at " + std::to_string(m_pos.x) +
                                       "," + std::to_string(m_pos.y));
    }
}
