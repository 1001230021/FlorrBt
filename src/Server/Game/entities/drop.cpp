#include "drop.h"
#include "../../Persistence/unique_petal_registry.h"
#include "../../server.h"
#include "../gameworld.h"
#include "../gamecontext.h"
#include "../player.h"
#include "petals/petal.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
float InitialDropMergeTimer(florrbt::Vector2f pos, int owner_id)
{
    const float interval = std::max(game_config::server_fixed_dt, game_config::default_drop_merge_interval);
    if (interval <= 0.f) return 0.f;

    const float owner = static_cast<float>(owner_id + 1);
    float seed = std::fabs(pos.x) * game_config::default_drop_merge_phase_x_coefficient +
                 std::fabs(pos.y) * game_config::default_drop_merge_phase_y_coefficient +
                 std::fabs(owner) * game_config::default_drop_merge_phase_owner_coefficient;
    float phase = std::fmod(seed, 1.f);
    if (phase < 0.f) phase += 1.f;
    return interval *
           std::clamp(phase, game_config::default_drop_merge_phase_min, game_config::default_drop_merge_phase_max);
}
} // namespace

CDrop::CDrop() : CDrop(nullptr, { 0.f, 0.f }, PetalType::None, ERarity::Null, drop_owner_all, 0.f, 0) {}

CDrop::CDrop(CGameWorld* world, florrbt::Vector2f pos, PetalType type, ERarity rarity, int owner_id, float lifetime,
             uint16_t stack_num)
    : CEntity(world, pos.x, pos.y, game_config::default_drop_radius,
              MakeDropEntityType()),
      m_type(type), m_rarity(rarity),
      m_owner_id(owner_id), m_stack_num(stack_num), m_merge_timer(InitialDropMergeTimer(pos, owner_id)),
      m_timer(std::max(0.f, lifetime))
{
    Health() = 1.f;
    Mass() = game_config::default_drop_mass;
    if (m_owner_id != drop_owner_all && world && world->GameContext())
        for (const auto& player : world->GameContext()->Players())
            if (player && player->IsAuthenticated() && player->GetId() == static_cast<uint32_t>(m_owner_id))
            {
                m_owner_account = player->GetAccountName();
                m_owner_progress_account = player->GetProgressAccountName();
                break;
            }
}

void CDrop::BeginInertiaDrop(florrbt::Vector2f center_start, florrbt::Vector2f center_velocity, float delay)
{
    m_inertia_active = true;
    m_inertia_duration = std::max(0.001f, delay);
    m_inertia_elapsed = 0.f;
    m_inertia_target = Pos();
    m_inertia_center_start = center_start;
    m_inertia_center_velocity = center_velocity;
    // Un-pickable the whole window.
    m_unable_picked_timer = std::max(m_unable_picked_timer, m_inertia_duration);
}

void CDrop::Tick(float dt)
{
    if (IsMarkedForDes()) return;

    if (m_type == PetalType::None || m_rarity == ERarity::Null || m_stack_num == 0)
    {
        MarkForDestroy(EEntityRemovalReason::Despawned);
        return;
    }

    if (m_unable_picked_timer > 0.f) m_unable_picked_timer = std::max(0.f, m_unable_picked_timer - dt);

    // Inertia gliding: the center drifts with the inherited velocity while it
    // decays linearly to zero; the drop lerps from that moving center to its
    // final spot over the same window.
    if (m_inertia_active)
    {
        m_inertia_elapsed = std::min(m_inertia_duration, m_inertia_elapsed + dt);
        const float tau = std::max(0.001f, m_inertia_duration);
        const float t = m_inertia_elapsed / tau; // 0..1
        const float drift_t = m_inertia_elapsed;
        // ∫v0(1 - s/τ)ds = v0(t - t²/(2τ))
        const float drift_x = m_inertia_center_velocity.x * (drift_t - drift_t * drift_t / (2.f * tau));
        const float drift_y = m_inertia_center_velocity.y * (drift_t - drift_t * drift_t / (2.f * tau));
        const float center_x = m_inertia_center_start.x + drift_x;
        const float center_y = m_inertia_center_start.y + drift_y;
        const float ease = t * t * (3.f - 2.f * t); // smoothstep
        Pos() = { center_x + (m_inertia_target.x - center_x) * ease,
                  center_y + (m_inertia_target.y - center_y) * ease };
        PrevPos() = Pos();
        if (t >= 1.f)
        {
            Pos() = m_inertia_target;
            PrevPos() = m_inertia_target;
            m_inertia_active = false;
        }
    }

    if (m_timer <= 0.f)
    {
        MarkForDestroy(EEntityRemovalReason::Expired);
        return;
    }

    m_timer -= dt;
    if (m_timer <= 0.f)
    {
        MarkForDestroy(EEntityRemovalReason::Expired);
        return;
    }

    m_merge_timer -= dt;
    if (m_merge_timer <= 0.f)
    {
        m_merge_timer = std::max(game_config::server_fixed_dt, game_config::default_drop_merge_interval);
        MergeNearbyDrops();
    }
}

bool CDrop::IsOwnedBy(const CPlayer& player) const
{
    return m_owner_id == drop_owner_all || (player.IsAuthenticated() && !m_owner_account.empty() &&
        m_owner_account == player.GetAccountName() && (m_owner_progress_account.empty() ?
            !player.HasIsolatedProgress() : m_owner_progress_account == player.GetProgressAccountName()));
}

bool CDrop::CanBePickedUpBy(const CPlayer& player) const
{
    if (!player.IsAuthenticated() || player.GetAccountName().empty() ||
        IsMarkedForDes() || m_unable_picked_timer > 0.f || m_timer <= 0.f) return false;
    if (m_type == PetalType::None || m_rarity == ERarity::Null || m_stack_num == 0) return false;
    return IsOwnedBy(player);
}

bool CDrop::PickUpTo(CPlayer& player)
{
    if (!CanBePickedUpBy(player)) return false;

    if (m_rarity == ERarity::Unique && !player.HasIsolatedProgress())
    {
        CServer* server = CServer::GetInstance();
        IUniquePetalRegistry* registry = server ? server->GetUniquePetalRegistry() : nullptr;
        if (!registry || !registry->GetUnique(m_type, 0, player.GetAccountName())) return false;

        player.ObtainPetalCard(static_cast<uint8_t>(m_type), static_cast<uint8_t>(m_rarity), 1, false);
        MarkForDestroy(EEntityRemovalReason::Consumed);
        return true;
    }

    if (!player.ObtainPetalCard(static_cast<uint8_t>(m_type), static_cast<uint8_t>(m_rarity), m_stack_num))
        return false;
    if (CServer::MeetsPetalReportRarity(m_rarity, game_config::min_drop_report_rarity))
    {
        if (const CPetalPrototype* proto = FindPetalPrototype(m_type))
        {
            if (CServer* server = CServer::GetInstance())
                server->BroadcastPetalReport("found", m_rarity, proto->m_name, player.GetName());
        }
    }
    MarkForDestroy(EEntityRemovalReason::Consumed);
    return true;
}

void CDrop::MergeNearbyDrops()
{
    CGameWorld* world = GameWorld();
    if (!world || IsMarkedForDes() || m_timer <= 0.f || m_inertia_active || m_unable_picked_timer > 0.f ||
        game_config::default_drop_stack_range <= 0.f) return;

    world->GetSpatialGrid().ForEachInRange(Pos(), game_config::default_drop_stack_range, [this](CEntity* entity) {
        auto* other = dynamic_cast<CDrop*>(entity);
        if (!other || other == this || other->IsMarkedForDes()) return;
        if (other->m_type != m_type || other->m_rarity != m_rarity) return;
        if (other->m_timer <= 0.f || other->m_inertia_active || other->m_unable_picked_timer > 0.f) return;
        if ((m_owner_id == drop_owner_all) != (other->m_owner_id == drop_owner_all)) return;
        if (m_owner_id != drop_owner_all && (m_owner_account.empty() || m_owner_account != other->m_owner_account)) return;
        if (m_owner_progress_account != other->m_owner_progress_account) return;
        if (other->Id() <= Id()) return;

        uint16_t max_stack = std::numeric_limits<uint16_t>::max();
        if (m_stack_num == max_stack) return;

        uint16_t free_stack = static_cast<uint16_t>(max_stack - m_stack_num);
        uint16_t moved_stack = std::min(free_stack, other->m_stack_num);
        if (moved_stack == 0) return;
        m_timer = std::max(m_timer, other->m_timer);
        m_stack_num = static_cast<uint16_t>(m_stack_num + moved_stack);
        other->m_stack_num = static_cast<uint16_t>(other->m_stack_num - moved_stack);
        if (other->m_stack_num == 0) other->MarkForDestroy(EEntityRemovalReason::Consumed);
    });
}
