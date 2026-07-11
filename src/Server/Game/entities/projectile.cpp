#include "projectile.h"
#include "../gameworld.h"

CEntity* CProjectile::GetOwner() const
{
    CGameWorld* world = const_cast<CProjectile*>(this)->GameWorld();
    if (!world || m_owner_id < 0) return nullptr;
    return world->GetEntity(m_owner_id);
}
