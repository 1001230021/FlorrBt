#include "petal.h"
#include "petal_slot.h"
#include "../../gameworld.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
std::optional<sf::Vector2f> CalculateSpawnGlobal(CPetal* petal, CFlower* flower)
{
    if (!petal || !flower || !flower->GetFinalStats()) return std::nullopt;

    int total_copies = flower->m_total_copies;
    int start_index = flower->GetStartCopyIndex(petal->m_slot_index);
    if (start_index < 0 || total_copies <= 0) return std::nullopt;

    sf::Vector2f orbit_center = flower->m_pos;
    if (petal->m_type != EPetalType::Moon)
    {
        if (CPetal* moon = flower->GetMoonPetal())
            orbit_center = moon->m_pos;
    }

    float base_radius = petal->m_type == EPetalType::Moon ? flower->GetFinalStats()->radius :
                        (flower->GetMoonPetal() && flower->GetMoonPetal() != petal ? flower->GetMoonPetal()->m_radius :
                         flower->GetFinalStats()->radius);
    float reach = game_config::default_petal_neutral_reach + flower->GetFinalStats()->reach;
    if (flower->m_attacking) reach += game_config::default_petal_attack_offset;
    else if (flower->m_defending) reach += game_config::default_petal_defend_offset;

    float orbit_distance = base_radius + game_config::default_petal_orbit_radius + reach;
    float angle = flower->GetPetalRotationAngle() +
                  (2.0f * game_config::pi * (start_index + petal->m_copy_index)) /
                      static_cast<float>(total_copies);
    return orbit_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit_distance;
}

bool IsLiveSlotPetal(const CPetal* petal, const CFlower* flower)
{
    if (!petal || !flower) return false;

    CGameWorld* world = const_cast<CFlower*>(flower)->GameWorld();
    if (!world || petal->m_id < 0) return false;
    return world->GetEntity(petal->m_id) == petal && !petal->m_is_marked_for_des;
}
}

CPetal::CPetal(float r, CFlower* owner, int slot, SPetalStats petal_stats)
    : CProjectile(owner->m_pos.x, owner->m_pos.y, r, owner), m_base_petal_stats(petal_stats),
      m_final_petal_stats(petal_stats), m_max_slot_num(petal_stats.copy), m_slot_index(slot)
{
    m_skip_world_tick = true;
    m_mass = petal_stats.mass;
    m_health = petal_stats.health;
}

void CPetal::Tick(float dt)
{
    auto* flower = dynamic_cast<CFlower*>(GetOwner());
    if (flower)
    {
        m_final_petal_stats = m_base_petal_stats;
        m_final_petal_stats.ActedOn(*flower->GetFinalStats());
    } else {
        m_is_marked_for_des = true;
        return;
    }
    CProjectile::Tick(dt);
    m_lifetime += dt;
}

void CPetal::TakeDamage(float dmg, CEntity*, EDamageType damage_type)
{
    if (damage_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_final_petal_stats.armor);
    if (dmg <= 0.f) return;

    m_health -= dmg;
    if (m_health <= 0.f)
    {
        m_health = 0.f;
    }
}

void CBeetleEggPetal::TakeDamage(float dmg, CEntity* attacker, EDamageType damage_type)
{
    CEntity* summon = GameWorld() ? GameWorld()->GetEntity(m_summon_id) : nullptr;
    if (m_has_spawned_summon && summon && !summon->m_is_marked_for_des)
    {
        summon->TakeDamage(dmg, attacker, damage_type);
        m_health = std::max(1.f, summon->m_health);
        return;
    }

    CPetal::TakeDamage(dmg, attacker, damage_type);
}

void CMissilePetal::Tick(float dt)
{
    CPetal::Tick(dt);
    if (!m_fired) return;

    m_fired_lifetime += dt;
    float lifetime = game_config::default_missile_lifetime;
    if (m_type == EPetalType::Carrot)
        lifetime *= game_config::default_carrot_lifetime_multiplier;
    if (m_fired_lifetime >= lifetime || m_health <= 0.f)
        m_is_marked_for_des = true;
}

void CPetalSlot::SpawnCopy(int copy_index, CFlower* flower)
{
    if (!flower || !m_p_proto || !m_p_proto->m_factory) return;
    if (copy_index < 0 || copy_index >= static_cast<int>(m_p_petals.size())) return;
    if (m_p_petals[copy_index] != nullptr || m_banned) return;

    std::unique_ptr<CPetal> petal = m_p_proto->m_factory(flower, m_slot_index, m_stored_rarity);
    if (!petal) return;

    if (m_p_proto->m_type != EPetalType::Moon)
    {
        if (CPetal* moon = flower->GetMoonPetal()) petal->m_pos = moon->m_pos;
    }

    petal->m_copy_index = copy_index;
    if (std::optional<sf::Vector2f> global = CalculateSpawnGlobal(petal.get(), flower))
    {
        sf::Vector2f center = (m_p_proto->m_type == EPetalType::Moon) ? flower->m_pos :
                              (flower->GetMoonPetal() ? flower->GetMoonPetal()->m_pos : flower->m_pos);
        petal->m_pos = (center + *global) * 0.5f;
        petal->m_spawn_flight_boost = true;
    }

    CPetal* p_petal = static_cast<CPetal*>(flower->GameWorld()->InsertEntity(std::move(petal)));
    if (!p_petal) return;

    m_p_petals[copy_index] = p_petal;
    m_bonus_active[copy_index] = true;
    m_p_proto->m_p_behavior->OnPetalSpawned(p_petal, m_stored_rarity, flower);
}

void CPetalSlot::SetPetal(const CPetalPrototype* proto, int slot_index, ERarity rarity)
{
    ClearPetal();

    m_p_proto = proto;
    m_stored_rarity = rarity;
    m_slot_index = slot_index;

    if (!m_p_proto || !m_p_proto->m_p_behavior) return;

    SPetalStats petal_stats = m_p_proto->m_p_behavior->GetPetalStats(rarity);
    int copies = std::max(0, petal_stats.copy);

    m_p_petals.assign(copies, nullptr);
    m_bonus_active.assign(copies, false);
    m_reload_timers.assign(copies, std::max(0.f, petal_stats.preload));
    m_reload_ignore_multiplier.assign(copies, false);
}

void CPetalSlot::ClearPetal()
{
    for (CPetal* petal : m_p_petals)
    {
        if (!petal) continue;
        if (m_p_proto && m_p_proto->m_p_behavior)
        {
            auto* flower = dynamic_cast<CFlower*>(petal->GetOwner());
            m_p_proto->m_p_behavior->OnPetalCleared(petal, m_stored_rarity, flower);
        }
        petal->m_is_marked_for_des = true;
    }
    m_p_petals.clear();
    m_bonus_active.clear();
    m_reload_timers.clear();
    m_reload_ignore_multiplier.clear();
}

void CPetalSlot::KillCopy(int copy_index)
{
    if (copy_index < 0 || copy_index >= static_cast<int>(m_reload_timers.size())) return;

    CPetal* petal = m_p_petals[copy_index];
    if (petal) petal->m_health = 0.f;
}

void CPetalSlot::Tick(float dt, CFlower* flower)
{
    if (m_banned || !m_p_proto || !m_p_proto->m_p_behavior || !flower) return;

    for (size_t i = 0; i < m_reload_timers.size(); ++i)
    {
        if (m_reload_timers[i] > 0.0f)
        {
            float multiplier = std::max(game_config::default_petal_reload_multiplier_min,
                                        flower->GetFinalStats()->petal_reload_multiplier);
            m_reload_timers[i] -= (i < m_reload_ignore_multiplier.size() && m_reload_ignore_multiplier[i]) ? dt : dt / multiplier;
            if (m_reload_timers[i] <= 0.0f)
            {
                m_reload_timers[i] = 0.0f;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = false;
                SpawnCopy(static_cast<int>(i), flower);
            }
        } else if (m_p_petals[i] == nullptr) {
            SpawnCopy(static_cast<int>(i), flower);
        }
    }

    for (size_t i = 0; i < m_p_petals.size(); ++i)
    {
        CPetal* petal = m_p_petals[i];
        if (!petal) continue;
        if (petal->m_hidden)
        {
            m_p_proto->m_p_behavior->OnTick(petal, m_stored_rarity, flower, dt);
            if (petal->m_health <= 0.f || petal->m_is_marked_for_des)
            {
                float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
                petal->m_is_marked_for_des = true;
                m_p_petals[i] = nullptr;
                m_reload_timers[i] = reload;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
            }
            continue;
        }

        if (!IsLiveSlotPetal(petal, flower))
        {
            auto* egg = dynamic_cast<CBeetleEggPetal*>(petal);
            if (egg && egg->m_has_spawned_summon)
            {
                m_p_proto->m_p_behavior->OnTick(petal, m_stored_rarity, flower, dt);
                if (petal->m_health <= 0.f)
                {
                    m_p_petals[i] = nullptr;
                    m_reload_timers[i] = petal->m_final_petal_stats.reload;
                    if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = false;
                }
            } else {
                m_p_petals[i] = nullptr;
            }
            continue;
        }

        petal->Tick(dt);
        if (IsLiveSlotPetal(petal, flower)) m_p_proto->m_p_behavior->OnTick(petal, m_stored_rarity, flower, dt);

        if (petal->m_detach_from_slot)
        {
            float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
            petal->m_detach_from_slot = false;
            m_p_petals[i] = nullptr;
            m_reload_timers[i] = reload;
            if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
            continue;
        }

        if (petal->m_health <= 0.f || petal->m_is_marked_for_des)
        {
            float reload = petal->m_reload_override >= 0.f ? petal->m_reload_override : petal->m_final_petal_stats.reload;
            m_p_proto->m_p_behavior->OnPetalDestroyed(petal, m_stored_rarity, flower);
            if (petal->m_reload_override >= 0.f) reload = petal->m_reload_override;
            bool should_reload = m_p_proto->m_p_behavior->ShouldReloadAfterPetalDestroyed(petal);
            if (should_reload)
            {
                petal->m_is_marked_for_des = true;
                m_p_petals[i] = nullptr;
                m_reload_timers[i] = reload;
                if (i < m_reload_ignore_multiplier.size()) m_reload_ignore_multiplier[i] = petal->m_reload_ignore_multiplier;
            } else {
                petal->m_hidden = true;
                petal->m_vel = {0.f, 0.f};
                petal->m_is_marked_for_des = false;
                petal->m_health = std::max(1.f, petal->m_health);
            }
        }
    }
}

int CPetalSlot::GetBonusCopyCount() const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return 0;

    SPetalStats petal_stats = m_p_proto->m_p_behavior->GetPetalStats(m_stored_rarity);
    if (petal_stats.copy <= 0) return 1;

    if (KeepsBonusDuringPreload(m_p_proto->m_p_behavior->GetBonusMode()))
        return static_cast<int>(m_bonus_active.size());

    if (LosesBonusDuringReload(m_p_proto->m_p_behavior->GetBonusMode()))
    {
        int count = 0;
        for (const CPetal* petal : m_p_petals)
        {
            if (petal && !petal->m_is_marked_for_des && petal->m_health > 0.f) ++count;
        }
        return count;
    }

    int count = 0;
    for (uint8_t active : m_bonus_active)
    {
        if (active != 0) ++count;
    }
    return count;
}

int CPetalSlot::GetCurrentCopyCount() const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return 0;
    if (m_p_proto->m_p_behavior->IsOpen()) return static_cast<int>(m_p_petals.size());

    int base_copy = static_cast<int>(m_p_petals.size());
    return (base_copy > 0) ? 1 : 0;
}

void CPetalSlot::ApplyStatsTo(SFlowerStats& target) const
{
    if (!m_p_proto || !m_p_proto->m_p_behavior) return;
    if (!m_available || m_banned) return;

    SFlowerStats stats = m_p_proto->m_p_behavior->GetStats(m_stored_rarity);
    int count = GetBonusCopyCount();
    for (int i = 0; i < count; ++i)
    {
        target.ActedOn(stats);
    }
}

const CPetalPrototype* FindPetalPrototype(EPetalType type)
{
    auto it = g_petal_registry.find(type);
    if (it == g_petal_registry.end()) return nullptr;
    return it->second.get();
}
