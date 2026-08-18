#pragma once
#include "opencontroller.h"

class CGameWorld;

// Abstract base for pvp-styled world controllers. Virtually inherits
// COpenController so a diamond child can combine it with another
// COpenController-derived controller (e.g. the mutable-terrain controller)
// and keep a single open-controller subobject.
//
// The base only owns the pvp lifecycle contract:
//   - PvpStartCondition  decides when the match begins,
//   - PvpEndCondition    decides when the match ends,
//   - OnPvpStart/OnPvpEnd fire exactly once at the transitions.
// Everything else a concrete pvp controller needs must be expressed through
// these (no extra pure virtuals: derived controllers implement their own
// hooks where the three above cannot cover them).
class CPvpControllerBase : virtual public COpenController
{
  public:
    ~CPvpControllerBase() override = default;

    // Match lifecycle conditions. Implementations may read players, squads,
    // entities or map state from `world`.
    virtual bool PvpStartCondition(CGameWorld& world) const = 0;
    virtual bool PvpEndCondition(CGameWorld& world) const = 0;

    // Transition callbacks, invoked once per state change (start/end).
    virtual void OnPvpStart(CGameWorld& world) { (void)world; }
    virtual void OnPvpEnd(CGameWorld& world) = 0;

    bool IsPvpActive() const { return m_pvp_active; }

  protected:
    // Drives the pvp lifecycle from a controller tick: evaluates the start
    // and end conditions and fires the transition callbacks exactly once.
    // Derived controllers call this from their OnTick.
    void TickPvpState(CGameWorld& world)
    {
        if (!m_pvp_active)
        {
            if (!PvpStartCondition(world)) return;
            m_pvp_active = true;
            OnPvpStart(world);
            return;
        }
        if (!PvpEndCondition(world)) return;
        m_pvp_active = false;
        OnPvpEnd(world);
    }

    bool m_pvp_active = false;
};
