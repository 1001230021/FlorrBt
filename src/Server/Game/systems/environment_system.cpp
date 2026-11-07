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
    // 高速移动即使在两次采样之间穿入又离开，也须清除旧花瓣运行态并启动一次完整重载。
    if (player_controlled && movement_start && !inside && !mob.IsInShortcut() &&
        world.SweptCircleBlockedByWall(*movement_start, mob.Pos(), radius, EWallQuery::Shortcut))
        mob.SetShortcutContact(true);
    mob.SetShortcutContact(inside);
}
