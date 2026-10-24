#include "environment_system.h"
#include "../gameworld.h"
#include "../entities/mob.h"

void CEnvironmentSystem::Synchronize(CGameWorld& world, CMobBase& mob,
                                    std::optional<florrbt::Vector2f> movement_start) const
{
    if (mob.GameWorld() != &world) return;
    const bool player_controlled = mob.IsPlayerControlled();
    if (!player_controlled && mob.IsInShortcut() && !mob.IsDead() && !mob.IsMarkedForDes())
    {
        if (const auto safe = world.NearestUnblockedPosition(mob.Pos(), mob.WallCollisionRadius()))
        {
            mob.Pos() = mob.PrevPos() = *safe;
            mob.m_vel = {};
        }
    }
    const float radius = mob.EnvironmentProbeRadius();
    const bool inside = player_controlled &&
        world.CircleBlockedByWall(mob.Pos(), radius, EWallQuery::Shortcut);
    // A fast movement can enter and leave between samples. It must still clear
    // the old petal runtime and start one full reload, not keep loaded petals.
    if (player_controlled && movement_start && !inside && !mob.IsInShortcut() &&
        world.SweptCircleBlockedByWall(*movement_start, mob.Pos(), radius, EWallQuery::Shortcut))
        mob.SetShortcutContact(true);
    mob.SetShortcutContact(inside);
}
