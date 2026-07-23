#include "drop.h"
#include "petals/petal.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../server.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
float InitialDropMergeTimer(sf::Vector2f pos, int owner_id)
{
    const float interval = std::max(game_config::server_fixed_dt, game_config::default_drop_merge_interval);
    if (interval <= 0.f) return 0.f;

    const float owner = static_cast<float>(owner_id + 1);
    float seed = std::fabs(pos.x) * 0.6180339f + std::fabs(pos.y) * 0.4142135f + std::fabs(owner) * 0.2718281f;
    float phase = std::fmod(seed, 1.f);
    if (phase < 0.f) phase += 1.f;
    return interval * std::clamp(phase, 0.05f, 0.95f);
}
}

CDrop::CDrop() : CDrop(nullptr, {0.f, 0.f}, PetalType::None, ERarity::Null, drop_owner_all, 0.f, 0) {}

CDrop::CDrop(CGameWorld* world, sf::Vector2f pos, PetalType type, ERarity rarity, int owner_id, float lifetime,
             uint16_t stack_num)
    : CEntity(world, pos.x, pos.y, game_config::default_drop_radius), m_type(type), m_rarity(rarity),
      m_owner_id(owner_id), m_stack_num(stack_num), m_merge_timer(InitialDropMergeTimer(pos, owner_id)),
      m_timer(std::max(0.f, lifetime))
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

    m_merge_timer -= dt;
    if (m_merge_timer <= 0.f)
    {
        m_merge_timer = std::max(game_config::server_fixed_dt, game_config::default_drop_merge_interval);
        MergeNearbyDrops();
    }
}

bool CDrop::CanBePickedUpBy(uint32_t player_id) const
{
    if (m_is_marked_for_des || m_unable_picked_timer > 0.f) return false;
    if (m_type == PetalType::None || m_rarity == ERarity::Null || m_stack_num == 0) return false;
    return m_owner_id == drop_owner_all || m_owner_id == static_cast<int>(player_id);
}

bool CDrop::PickUpTo(CPlayer& player)
{
    if (!CanBePickedUpBy(player.GetId())) return false;

    if (!player.ObtainPetalCard(static_cast<uint8_t>(m_type), static_cast<uint8_t>(m_rarity), m_stack_num)) return false;
    if (CServer::MeetsPetalReportRarity(m_rarity, game_config::min_drop_report_rarity))
    {
        if (const CPetalPrototype* proto = FindPetalPrototype(m_type))
        {
            if (CServer* server = CServer::GetInstance())
                server->BroadcastPetalReport("found", m_rarity, proto->m_name, player.GetName());
        }
    }
    m_is_marked_for_des = true;
    return true;
}

void CDrop::MergeNearbyDrops()
{
    CGameWorld* world = GameWorld();
    if (!world || game_config::default_drop_stack_range <= 0.f) return;

    world->GetSpatialGrid().ForEachInRange(m_pos, game_config::default_drop_stack_range, [this](CEntity* entity)
    {
        auto* other = dynamic_cast<CDrop*>(entity);
        if (!other || other == this || other->m_is_marked_for_des) return;
        if (other->m_type != m_type || other->m_rarity != m_rarity) return;
        if (other->m_owner_id != m_owner_id) return;
        if (other->m_id <= m_id) return;

        uint16_t max_stack = std::numeric_limits<uint16_t>::max();
        if (m_stack_num == max_stack) return;

        uint16_t free_stack = static_cast<uint16_t>(max_stack - m_stack_num);
        uint16_t moved_stack = std::min(free_stack, other->m_stack_num);
        m_stack_num = static_cast<uint16_t>(m_stack_num + moved_stack);
        other->m_stack_num = static_cast<uint16_t>(other->m_stack_num - moved_stack);
        if (other->m_stack_num == 0) other->m_is_marked_for_des = true;
    });
}
