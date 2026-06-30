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
        }
        else
        {
            ++it;
        }
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
