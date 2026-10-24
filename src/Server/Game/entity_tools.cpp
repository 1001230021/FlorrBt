#include "../../Shared/tools.h"
#include "controllers/melee_controller.h"
#include "entities/mob.h"
#include "entities/projectile.h"
#include "gameworld.h"
#include "player.h"
#include <unordered_set>

CPlayer* FindPlayerFromEntity(CEntity* entity, const std::vector<std::unique_ptr<CPlayer>>& players)
{
    std::unordered_set<SEntityHandle, SEntityHandleHash> visited_refs;

    while (entity)
    {
        const SEntityHandle ref = entity->GetHandle();
        if (ref.IsValid() && !visited_refs.insert(ref).second) return nullptr;

        for (const auto& player : players)
        {
            if (player && player->GetEntity() == entity) return player.get();
        }

        if (auto* projectile = dynamic_cast<CProjectile*>(entity))
        {
            entity = projectile->GetOwner();
            continue;
        }

        if (auto* mob = dynamic_cast<CMobBase*>(entity))
        {
            if (auto* body = dynamic_cast<IMultiSegmentBodyMob*>(mob))
            {
                entity = ResolveMultiSegmentRoot(mob->GameWorld(), body->RootHandle());
                continue;
            }
            auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
            CGameWorld* world = mob->GameWorld();
            if (controller && world)
            {
                entity = controller->GetOwner(world);
                continue;
            }
        }

        return nullptr;
    }

    return nullptr;
}

CEntity* FindRootOwnerEntity(CEntity* entity)
{
    std::unordered_set<SEntityHandle, SEntityHandleHash> visited_refs;
    CEntity* current = entity;

    while (current)
    {
        const SEntityHandle ref = current->GetHandle();
        if (ref.IsValid() && !visited_refs.insert(ref).second) return current;

        if (auto* projectile = dynamic_cast<CProjectile*>(current))
        {
            CEntity* owner = projectile->GetOwner();
            if (!owner) return current;
            current = owner;
            continue;
        }

        if (auto* mob = dynamic_cast<CMobBase*>(current))
        {
            if (auto* body = dynamic_cast<IMultiSegmentBodyMob*>(mob))
            {
                CEntity* root = ResolveMultiSegmentRoot(mob->GameWorld(), body->RootHandle());
                if (!root) return current;
                current = root;
                continue;
            }
            auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
            CGameWorld* world = mob->GameWorld();
            if (controller && world)
            {
                CEntity* owner = controller->GetOwner(world);
                if (!owner) return current;
                current = owner;
                continue;
            }
        }

        return current;
    }

    return nullptr;
}

const CEntity* FindRootOwnerEntity(const CEntity* entity) { return FindRootOwnerEntity(const_cast<CEntity*>(entity)); }

bool ShareRootOwner(const CEntity* lhs, const CEntity* rhs)
{
    const CEntity* lhs_root = FindRootOwnerEntity(lhs);
    const CEntity* rhs_root = FindRootOwnerEntity(rhs);
    return lhs_root && rhs_root && lhs_root == rhs_root;
}
