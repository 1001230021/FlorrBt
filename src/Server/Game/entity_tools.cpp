#include "../../Shared/tools.h"
#include "controllers/melee_controller.h"
#include "entities/mob.h"
#include "entities/projectile.h"
#include "gameworld.h"
#include "player.h"
#include <unordered_set>

CPlayer* FindPlayerFromEntity(CEntity* entity, const std::vector<std::unique_ptr<CPlayer>>& players)
{
    std::unordered_set<int> visited_ids;

    while (entity)
    {
        if (entity->m_id >= 0 && !visited_ids.insert(entity->m_id).second) return nullptr;

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
