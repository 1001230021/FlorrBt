#include "mob.h"
#include "../state.h"

CMobBase::~CMobBase() = default;

template <typename TStats> CMob<TStats>::~CMob() = default;

template class CMob<SMobStats>;
template class CMob<SFlowerStats>;

void CMobBase::AddState(std::unique_ptr<CState> state)
{
    if (state)
        m_States.push_back(std::move(state));
}

void CMobBase::TickStates(float dt)
{
    for (auto it = m_States.begin(); it != m_States.end();)
    {
        (*it)->Tick(dt);
        if ((*it)->m_Timer <= 0.0f)
        {
            it = m_States.erase(it);
        } else
            ++it;
    }
}

void CMobBase::MoveTowards(const sf::Vector2f& targetPos, float dt)
{
    sf::Vector2f delta = targetPos - m_Pos;
    float len = Length(delta);
    if (len <= m_Radius)
    {
        m_Vel *= 0.9f;
        if (LengthSq(m_Vel) <= 1e-5) m_Vel *= 0.f;
        return;
    }
    float dx = delta.x / len;
    float dy = delta.y / len;
    sf::Vector2f desired_vel = { dx * GetFinalStats()->max_velocity, dy * GetFinalStats()->max_velocity };
    sf::Vector2f diff = desired_vel - m_Vel;
    float diff_len = sqrt(diff.x * diff.x + diff.y * diff.y);
    if (diff_len <= GetFinalStats()->acceleration * dt)
    {
        m_Vel = desired_vel;
    } else {
        sf::Vector2f accelDir = diff / diff_len;
        m_Vel += accelDir * GetFinalStats()->acceleration * dt;
    }
}

bool CMobBase::RemoveState(CState* state)
{
    if (!state)
        return false;
    for (auto it = m_States.begin(); it != m_States.end(); ++it)
    {
        if (it->get() == state)
        {
            m_States.erase(it);
            return true;
        }
    }
    return false;
}