#include "petal.h"
#include "petal_slot.h"
#include "../../gameworld.h"
#include <algorithm>

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
    auto* flower = static_cast<CFlower*>(m_p_owner);
    if (flower)
    {
        m_final_petal_stats = m_base_petal_stats;
        m_final_petal_stats.ActedOn(*flower->GetFinalStats());
    }
    CProjectile::Tick(dt);
}

void CPetal::TakeDamage(float dmg, CEntity*, EDamageType damage_type)
{
    if (damage_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_final_petal_stats.armor);
    if (dmg <= 0.f) return;

    m_health -= dmg;
    if (m_health <= 0.f)
    {
        m_health = 0.f;
        m_is_marked_for_des = true;
    }
}

void CPetalSlot::SpawnCopy(int copy_index, CFlower* flower)
{
    if (!flower || !m_p_proto || !m_p_proto->m_factory) return;
    if (copy_index < 0 || copy_index >= static_cast<int>(m_p_petals.size())) return;
    if (m_p_petals[copy_index] != nullptr || m_banned) return;

    std::unique_ptr<CPetal> petal = m_p_proto->m_factory(flower, m_slot_index, m_stored_rarity);
    if (!petal) return;

    petal->m_copy_index = copy_index;
    CPetal* p_petal = static_cast<CPetal*>(flower->GameWorld()->InsertEntity(std::move(petal)));
    if (!p_petal) return;

    m_p_petals[copy_index] = p_petal;
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
    m_reload_timers.assign(copies, petal_stats.preload);
}

void CPetalSlot::ClearPetal()
{
    for (CPetal* petal : m_p_petals)
    {
        if (petal) petal->m_is_marked_for_des = true;
    }
    m_p_petals.clear();
    m_reload_timers.clear();
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
            float multiplier = std::max(0.001f, flower->GetFinalStats()->detection_multiplier);
            m_reload_timers[i] -= dt / multiplier;
            if (m_reload_timers[i] <= 0.0f)
            {
                m_reload_timers[i] = 0.0f;
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

        petal->Tick(dt);
        m_p_proto->m_p_behavior->OnTick(petal, m_stored_rarity, flower, dt);

        if (petal->m_health <= 0.f || petal->m_is_marked_for_des)
        {
            petal->m_is_marked_for_des = true;
            m_p_proto->m_p_behavior->OnPetalDestroyed(petal, m_stored_rarity, flower);
            m_p_petals[i] = nullptr;
            m_reload_timers[i] = petal->m_final_petal_stats.reload;
        }
    }
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

    SFlowerStats stats = m_p_proto->m_p_behavior->GetStats(m_stored_rarity);
    int count = 0;
    for (auto* petal : m_p_petals)
    {
        if (petal) ++count;
    }

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
