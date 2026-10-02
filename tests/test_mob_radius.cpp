#include "test_framework.h"
#include "Shared/game_config.h"
#include <array>
#include <cmath>

TEST(MobRadiusKeepsLowTiersAndMatchesReferenceAnchors)
{
    const std::array<float, 4> low_tiers = { 1.f, 1.15f, 1.495f, 2.16775f };
    for (int level = 1; level <= 4; ++level)
        CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(level), low_tiers[level - 1], 0.00001);
    CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(8), 11.5f, 0.0001);
    // Eternal and Unique both map to level 9; do not change combat/value ranks.
    CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(9), 18.f, 0.0001);
    CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(10), 25.f, 0.0001);
    CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(-10), 1.f, 0.0001);
    CHECK_NEAR(game_config::GetMobRadiusScaleForLevel(100), 25.f, 0.0001);

    const float mecha_to_player = game_config::default_flower_radius / game_config::mob_player_flower_radius;
    CHECK_NEAR(mecha_to_player * game_config::GetMobRadiusScaleForLevel(8), 12.77778f, 0.001);
    CHECK_NEAR(mecha_to_player * game_config::GetMobRadiusScaleForLevel(9), 20.f, 0.001);
    CHECK_NEAR(mecha_to_player * game_config::GetMobRadiusScaleForLevel(10), 27.77778f, 0.001);
}

TEST(MobRadiusGrowthIsMonotoneAndDoesNotExplode)
{
    float previous = game_config::GetMobRadiusScaleForLevel(1);
    const float middle_ratio = game_config::GetMobRadiusScaleForLevel(5) /
                              game_config::GetMobRadiusScaleForLevel(4);
    for (int level = 2; level <= 10; ++level)
    {
        const float current = game_config::GetMobRadiusScaleForLevel(level);
        CHECK(std::isfinite(current));
        CHECK(current > previous);
        CHECK(current / previous < 1.6f);
        if (level >= 5 && level <= 8) CHECK_NEAR(current / previous, middle_ratio, 0.0001);
        previous = current;
    }
    CHECK(game_config::GetMobRadiusScaleForLevel(10) / game_config::GetMobRadiusScaleForLevel(9) <
          game_config::GetMobRadiusScaleForLevel(9) / game_config::GetMobRadiusScaleForLevel(8));
}

TEST(MobRadiusConfigCannotMakeLaterTiersSmaller)
{
    const float saved_super = game_config::mob_radius_scale_super;
    const float saved_eternal = game_config::mob_radius_scale_eternal;
    const float saved_primordial = game_config::mob_radius_scale_primordial;
    game_config::mob_radius_scale_super = -1.f;
    game_config::mob_radius_scale_eternal = 1.f;
    game_config::mob_radius_scale_primordial = 0.f;
    for (int level = 2; level <= 10; ++level)
        CHECK(game_config::GetMobRadiusScaleForLevel(level) >= game_config::GetMobRadiusScaleForLevel(level - 1));
    game_config::mob_radius_scale_super = saved_super;
    game_config::mob_radius_scale_eternal = saved_eternal;
    game_config::mob_radius_scale_primordial = saved_primordial;
}
