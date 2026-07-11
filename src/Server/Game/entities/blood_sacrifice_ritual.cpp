#include "blood_sacrifice_ritual.h"
#include "mob.h"
#include "../gameworld.h"
#include "../../../Engine/logger.h"
#include <algorithm>

CBloodSacrificeRitual::CBloodSacrificeRitual(CGameWorld* world, sf::Vector2f pos, EMobType mob_type, ERarity rarity,
                                             float timer)
    : CEntity(world, pos.x, pos.y, 0.f), m_mob_type(mob_type), m_rarity(rarity), m_timer(std::max(0.f, timer))
{
    m_health = 1.f;
    m_mass = 0.f;
    m_team = 0;
}

void CBloodSacrificeRitual::Tick(float dt)
{
    if (m_is_marked_for_des) return;

    m_timer -= dt;
    if (m_timer > 0.f) return;

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

    m_is_marked_for_des = true;
}
