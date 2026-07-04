#include "mob.h"
#include "../../../Shared/game_config.h"

CMobBase::~CMobBase() = default;

template <typename TStats> CMob<TStats>::~CMob() = default;

template class CMob<SMobStats>;
template class CMob<SFlowerStats>;

void CMobBase::AddState(std::unique_ptr<CState> state)
{
    if (state) m_states.push_back(std::move(state));
}

void CMobBase::TickStates(float dt)
{
    for (auto it = m_states.begin(); it != m_states.end();)
    {
        (*it)->Tick(dt);
        if ((*it)->m_timer != endless && (*it)->m_timer <= 0.0f)
        {
            it = m_states.erase(it);
        } else {
            ++it;
        }
    }
}

void CMobBase::MoveTowards(const sf::Vector2f& target_pos, float dt)
{
    const SMobStats* stats = GetFinalStats();
    if (!stats) return;

    sf::Vector2f delta = target_pos - m_pos;
    float len = Length(delta);
    if (len <= m_radius)
    {
        m_vel *= game_config::mob_stop_damping;
        if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = {0.f, 0.f};
        return;
    }

    sf::Vector2f desired_vel = delta / len * stats->max_velocity;
    sf::Vector2f diff = desired_vel - m_vel;
    float diff_len = Length(diff);
    float max_accel = stats->acceleration * dt;

    if (diff_len <= max_accel)
    {
        m_vel = desired_vel;
    } else {
        m_vel += diff / diff_len * max_accel;
    }
}

bool CMobBase::RemoveState(CState* state)
{
    if (!state) return false;

    for (auto it = m_states.begin(); it != m_states.end(); ++it)
    {
        if (it->get() == state)
        {
            m_states.erase(it);
            return true;
        }
    }
    return false;
}

const CMobPrototype* FindMobPrototype(EMobType type)
{
    auto it = g_mob_registry.find(type);
    if (it == g_mob_registry.end()) return nullptr;
    return it->second.get();
}

std::unique_ptr<CMobBase> CreateMob(EMobType type, CGameWorld* world, sf::Vector2f pos, ERarity rarity)
{
    const CMobPrototype* prototype = FindMobPrototype(type);
    if (!prototype || !prototype->m_factory) return nullptr;
    return prototype->m_factory(world, pos, rarity);
}

void RegisterMobs()
{
    RegisterBeetle();
    RegisterNormalLadybug();
    RegisterNormalFlower();
    RegisterPlayerFlower();
    RegisterSummonedBeetle();
}
