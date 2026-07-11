#include "drop.h"
#include "../../../Engine/account_data.h"
#include "../gameworld.h"
#include <algorithm>
#include <limits>
#include <vector>

CDrop::CDrop() : CDrop(nullptr, {0.f, 0.f}, PetalType::None, ERarity::Null, drop_owner_all, 0.f, 0) {}

CDrop::CDrop(CGameWorld* world, sf::Vector2f pos, PetalType type, ERarity rarity, int owner_id, float lifetime,
             uint16_t stack_num)
    : CEntity(world, pos.x, pos.y, game_config::default_drop_radius), m_type(type), m_rarity(rarity),
      m_owner_id(owner_id), m_stack_num(stack_num), m_timer(std::max(0.f, lifetime))
{
    m_health = 1.f;
    m_mass = game_config::default_drop_mass;
}

void CDrop::Tick(float dt)
{
    if (m_is_marked_for_des) return;

    if (m_type == PetalType::None || m_rarity == ERarity::Null || m_stack_num == 0)
    {
        m_is_marked_for_des = true;
        return;
    }

    if (m_unable_picked_timer > 0.f) m_unable_picked_timer = std::max(0.f, m_unable_picked_timer - dt);

    if (m_timer <= 0.f)
    {
        m_is_marked_for_des = true;
        return;
    }

    m_timer -= dt;
    if (m_timer <= 0.f)
    {
        m_is_marked_for_des = true;
        return;
    }

    MergeNearbyDrops();
}

bool CDrop::CanBePickedUpBy(uint32_t player_id) const
{
    if (m_is_marked_for_des || m_unable_picked_timer > 0.f) return false;
    if (m_type == PetalType::None || m_rarity == ERarity::Null || m_stack_num == 0) return false;
    return m_owner_id == drop_owner_all || m_owner_id == static_cast<int>(player_id);
}

bool CDrop::PickUpTo(const std::string& account_name, uint32_t player_id)
{
    if (account_name.empty() || !CanBePickedUpBy(player_id)) return false;

    CAccountDataStore::AddItem(account_name, static_cast<uint8_t>(m_type), static_cast<uint8_t>(m_rarity), m_stack_num);
    m_is_marked_for_des = true;
    return true;
}

void CDrop::MergeNearbyDrops()
{
    CGameWorld* world = GameWorld();
    if (!world || game_config::default_drop_stack_range <= 0.f) return;

    std::vector<CEntity*> candidates = world->GetSpatialGrid().QueryRange(
        m_pos, game_config::default_drop_stack_range,
        [this](const CEntity* entity) -> bool
        {
            if (!entity || entity == this || entity->m_is_marked_for_des) return false;

            auto* other = dynamic_cast<const CDrop*>(entity);
            if (!other) return false;
            if (other->m_type != m_type || other->m_rarity != m_rarity) return false;
            if (other->m_owner_id != m_owner_id) return false;
            return other->m_id > m_id;
        });

    for (CEntity* entity : candidates)
    {
        auto* other = dynamic_cast<CDrop*>(entity);
        if (!other || other->m_is_marked_for_des) continue;

        uint16_t max_stack = std::numeric_limits<uint16_t>::max();
        if (m_stack_num == max_stack) return;

        uint16_t free_stack = static_cast<uint16_t>(max_stack - m_stack_num);
        uint16_t moved_stack = std::min(free_stack, other->m_stack_num);
        m_stack_num = static_cast<uint16_t>(m_stack_num + moved_stack);
        other->m_stack_num = static_cast<uint16_t>(other->m_stack_num - moved_stack);
        if (other->m_stack_num == 0) other->m_is_marked_for_des = true;
    }
}
