#include "test_framework.h"
#include "Shared/circle_contact.h"
#include "Shared/drop_stack.h"
#include "Shared/game_config.h"
#include "Shared/mob_type.h"
#include "Shared/petal_motion.h"
#include "Shared/spawn_dominance.h"
#include <array>
#include <string>
#include <vector>

namespace
{
struct Zone
{
    float difficulty;
    std::vector<florrbt::Vector2f> vertices;
    std::string mobs = "Ant";
    std::string spawn_profile;
};
Zone MakeZone(float level, float x, float y, float w, float h)
{
    return { level * 10.f, { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } } };
}
struct RolledDrop { EPetalType type; ERarity rarity; };
}

TEST(MoonOrbitHasFixedRightHandCenterAndIndependentAngularSpeed)
{
    constexpr float pi = 3.14159265358979323846f;
    for (const float radius : { 18.f, 100.f, 250.f })
    for (const int hz : { 20, 60, 120 })
    {
        const florrbt::Vector2f spawn{ 100, 200 }, center = spawn + florrbt::Vector2f{ radius, 0 };
        float phase = pi;
        auto pos = florrbt::StepMoonOrbit(center, radius, phase, 0.2f, 0);
        CHECK_NEAR(pos.x, spawn.x, 0.001); CHECK_NEAR(pos.y, spawn.y, 0.001);
        for (int i = 0; i < hz * 10; ++i)
        {
            pos = florrbt::StepMoonOrbit(center, radius, phase, 0.2f, 1.f / hz);
            CHECK_NEAR(std::hypot(pos.x - center.x, pos.y - center.y), radius, 0.001);
        }
        CHECK_NEAR(phase, pi + 2.f, 0.0003);
        // The saved center/phase continue at the same point after restore.
        float restored_phase = phase;
        const auto restored = florrbt::StepMoonOrbit(center, radius, restored_phase, 0.2f, 0.05f);
        const auto uninterrupted = florrbt::StepMoonOrbit(center, radius, phase, 0.2f, 0.05f);
        CHECK(restored == uninterrupted);
    }
}

TEST(MoonKeepsBubbleMomentumAtDifferentTickRates)
{
    constexpr float pi = 3.14159265358979323846f;
    for (const int hz : { 20, 60, 120 })
    {
        florrbt::Vector2f pos{ 100, 200 }, center{ 240, 200 }, orbital{}, velocity{};
        float phase = pi;
        const auto step = [&] {
            const auto motion = florrbt::StepDriftingMoonOrbit(pos, velocity, center, orbital,
                phase, 140, .2f, 1.f, 1.f / hz);
            pos += motion.movement_velocity / static_cast<float>(hz);
            velocity = motion.final_velocity;
        };
        for (int i = 0; i < hz; ++i) step();
        CHECK_NEAR(center.x, 240, .03); CHECK_NEAR(center.y, 200, .03);
        // Bubble changes total velocity between petal ticks, in any slot order.
        const florrbt::Vector2f before = center, impulse{ 500, -120 };
        velocity += impulse;
        for (int i = 0; i < hz * 2; ++i) step();
        CHECK_NEAR(center.x - before.x, impulse.x * (1.f - std::exp(-2.f)), .1);
        CHECK_NEAR(center.y - before.y, impulse.y * (1.f - std::exp(-2.f)), .1);
        CHECK_NEAR(velocity.x - orbital.x, impulse.x * std::exp(-2.f), .02);
        CHECK_NEAR(velocity.y - orbital.y, impulse.y * std::exp(-2.f), .02);
        CHECK_NEAR(std::hypot(pos.x - center.x, pos.y - center.y), 140, .01);
    }
}

TEST(MoonDefaultsBrakeQuicklyAndBubbleLeavesPlayerBoostUnchanged)
{
    CHECK_NEAR(game_config::default_moon_slowdown_time, .1f, .00001);
    CHECK_NEAR(game_config::default_bubble_moon_boost_multiplier, .625f, .00001);
    CHECK_NEAR(game_config::default_bubble_boost_speed, game_config::WorldUnits(720), .00001);
    const float impulse = game_config::default_bubble_boost_speed * game_config::default_bubble_moon_boost_multiplier;
    for (const int hz : { 20, 60, 120 })
    {
        florrbt::Vector2f pos{ 0, 0 }, center{ 100, 0 }, orbital{}, velocity{ impulse, 0 };
        float phase = 3.14159265358979323846f;
        for (int tick = 0; tick < hz * 3 / 10; ++tick)
        {
            const auto step = florrbt::StepDriftingMoonOrbit(pos, velocity, center, orbital, phase, 100,
                game_config::default_moon_orbit_speed, game_config::default_moon_slowdown_time, 1.f / hz);
            pos += step.movement_velocity / static_cast<float>(hz);
            velocity = step.final_velocity;
        }
        const auto external = velocity - orbital;
        CHECK(std::hypot(external.x, external.y) < impulse * .051f);
        CHECK_NEAR(external.x, impulse * std::exp(-3.f), .02);
        CHECK_NEAR(center.x - 100, impulse * .1f * (1.f - std::exp(-3.f)), .02);
        CHECK(std::hypot(orbital.x, orbital.y) > 19.f); // slow orbit is not braked
    }
}

TEST(MoonContactCorrectionAndRestoreDoNotReturnToSpawn)
{
    florrbt::Vector2f pos{ 5, 7 }, center{ 105, 7 }, orbital{}, velocity{};
    float phase = 3.14159265358979323846f;
    auto motion = florrbt::StepDriftingMoonOrbit(pos, velocity, center, orbital, phase, 100, .2f, 1, .05f);
    pos += motion.movement_velocity * .05f;
    velocity = motion.final_velocity;
    pos += florrbt::Vector2f{ 50, -20 }; // wall/contact/teleport correction
    const auto saved_pos = pos, saved_vel = velocity, saved_center = center, saved_orbit = orbital;
    const auto saved_phase = phase;
    motion = florrbt::StepDriftingMoonOrbit(pos, velocity, center, orbital, phase, 100, .2f, 1, .05f);
    CHECK_NEAR(center.x, 155, .001); CHECK_NEAR(center.y, -13, .001);
    auto restored_center = saved_center, restored_orbit = saved_orbit;
    float restored_phase = saved_phase;
    const auto restored = florrbt::StepDriftingMoonOrbit(saved_pos, saved_vel, restored_center, restored_orbit,
        restored_phase, 100, .2f, 1, .05f);
    CHECK(motion.movement_velocity == restored.movement_velocity);
    CHECK(motion.final_velocity == restored.final_velocity);
    CHECK(center == restored_center && orbital == restored_orbit);
    const auto paused = florrbt::StepDriftingMoonOrbit(pos, velocity, center, orbital, phase, 100, .2f, 1, 0);
    CHECK(paused.final_velocity == velocity);
}

TEST(WaxSlidesTowardsSlotAndRetainsEqualMassPassiveResistance)
{
    const auto velocity = florrbt::ComputeWaxSpawnVelocity({ 0, -200 }, 48, 0.9f, 0.05f);
    CHECK_NEAR(velocity.x, 0, 0.00001); CHECK(velocity.y < 0);
    float position = 0, speed = velocity.y;
    for (int i = 0; i < 250; ++i) { position += speed * 0.05f; speed *= 0.9f; }
    CHECK_NEAR(position, -48, 0.001);
    CHECK(florrbt::ComputeWaxSpawnVelocity({}, 48, 0.9f, 0.05f) == florrbt::Vector2f{});
    florrbt::Vector2f wax{}, mob{ 18, 0 }, wax_vel{}, mob_vel{ -100, 5 };
    const auto weights = florrbt::ComputeWaxContactWeights(1, 1, false, 2, true, .08f);
    florrbt::ResolveCircleContact(wax, wax_vel, 10, weights.lhs, mob, mob_vel, 10, weights.rhs);
    CHECK_NEAR(wax_vel.x, -8, 0.0001);
    CHECK_NEAR(mob_vel.x, -8, 0.0001);
    CHECK_NEAR(mob_vel.y, 5, 0.0001);
    CHECK_NEAR(wax.x, -0.16, 0.0001);
}

TEST(WaxNormalMobContactUsesBothMassesAndMobGrowthCurve)
{
    for (const bool passive : { false, true })
    {
        const auto light_wax = florrbt::ComputeWaxContactWeights(.1f, 10, false, 2, passive, .08f);
        const auto heavy_wax = florrbt::ComputeWaxContactWeights(10, .1f, false, 2, passive, .08f);
        CHECK(light_wax.lhs > .998f);
        CHECK(heavy_wax.lhs < .002f);
        CHECK_NEAR(light_wax.lhs + light_wax.rhs, 1, .00001);
        const auto fixed = florrbt::ComputeWaxContactWeights(100, 1, true, 2, passive, .08f);
        CHECK(fixed.lhs == 1 && fixed.rhs == 0);
        const auto base = florrbt::ComputeWaxContactWeights(game_config::default_wax_mass,
            game_config::mob_soldier_ant_mass, false, 2, passive, .08f);
        for (int level = 1; level <= 10; ++level)
        {
            const float scale = game_config::GetMobMassScaleForLevel(level);
            const auto same_tier = florrbt::ComputeWaxContactWeights(game_config::default_wax_mass * scale,
                game_config::mob_soldier_ant_mass * scale, false, 2, passive, .08f);
            CHECK_NEAR(same_tier.lhs, base.lhs, .00001);
        }
    }
}

TEST(WaxActiveContactCannotDisplaceAnImmovableMob)
{
    florrbt::Vector2f wax{}, mob{ 18, 0 }, wax_velocity{ 100, 9 }, mob_velocity{};
    const auto weights = florrbt::ComputeWaxContactWeights(1, 1, true, 2, false, .08f);
    florrbt::ResolveCircleContact(wax, wax_velocity, 10, weights.lhs, mob, mob_velocity, 10, weights.rhs);
    CHECK_NEAR(mob.x, 18, .00001); CHECK_NEAR(mob_velocity.x, 0, .00001);
    CHECK_NEAR(wax_velocity.x, 0, .00001); CHECK_NEAR(wax_velocity.y, 9, .00001);
}

TEST(LeafcutterSoldierMultipliersMatchRequestedBaseBalance)
{
    CHECK_NEAR(game_config::mob_leafcutter_soldier_health_multiplier, 1.25f, .00001);
    CHECK_NEAR(game_config::mob_leafcutter_soldier_mass_multiplier, 1.25f, .00001);
    CHECK_NEAR(game_config::mob_leafcutter_soldier_radius_multiplier, 1.15f, .00001);
    CHECK_NEAR(game_config::mob_leafcutter_soldier_damage_multiplier, 1.5f, .00001);
}

TEST(CrabAndJellyfishAreAppendedNativeMobsWithFixedBehaviorTuning)
{
    CHECK(static_cast<int>(EMobType::Crab) == 41);
    CHECK(static_cast<int>(EMobType::Jellyfish) == 42);
    CHECK(GetMobTypeName(EMobType::Crab) == "Crab");
    CHECK(MatchMobTypeAlias("crab", EMobType::Crab));
    CHECK(game_config::mob_crab_dash_speed > game_config::mob_crab_max_velocity);
    CHECK(game_config::mob_crab_dash_distance > game_config::mob_crab_radius);
    CHECK(game_config::mob_crab_orbit_distance >= game_config::default_third_eye_reach_primordial);
    CHECK(MatchMobTypeAlias("jellyfish", EMobType::Jellyfish));
    CHECK(game_config::mob_jellyfish_max_velocity < game_config::mob_crab_max_velocity);
    CHECK_NEAR(game_config::mob_jellyfish_lightning_interval, 1.f, 0.00001);
}

TEST(UniqueMobRewardsStackAtFinalizationWithoutChangingCounts)
{
    std::vector<RolledDrop> rolls = { { EPetalType::Basic, ERarity::Ultra }, { EPetalType::Rose, ERarity::Mythic },
        { EPetalType::Basic, ERarity::Ultra }, { EPetalType::Basic, ERarity::Mythic }, { EPetalType::Basic, ERarity::Ultra } };
    const auto unique = florrbt::FinalizeDropStacks(rolls, true);
    CHECK(unique.size() == 3);
    CHECK(unique[0].count == 3 && unique[1].count == 1 && unique[2].count == 1);
    const auto normal = florrbt::FinalizeDropStacks(rolls, false);
    CHECK(normal.size() == rolls.size());
    for (const auto& entry : normal) CHECK(entry.count == 1);
    rolls.assign(140000, { EPetalType::Basic, ERarity::Ultra });
    const auto split = florrbt::FinalizeDropStacks(rolls, true);
    CHECK(split.size() == 3);
    CHECK(split[0].count == 65535 && split[1].count == 65535 && split[2].count == 8930);
    CHECK(florrbt::FinalizeDropStacks(std::vector<RolledDrop>{}, true).empty());
}

TEST(SpawnProtectionUsesFloorAndAFullTwoLevelGap)
{
    CHECK(florrbt::ComputeSpawnRarityExcess(8, 60) == 2);
    CHECK(florrbt::ComputeSpawnRarityExcess(7, 60) == 1);
    CHECK(florrbt::ComputeSpawnRarityExcess(8, 69.99f) == 2);
    CHECK(florrbt::ComputeSpawnRarityExcess(8, 70) == 1);
    CHECK(florrbt::ComputeSpawnRarityExcess(1, 0) == 0);
    CHECK(florrbt::ComputeSpawnRarityExcess(8, 600, 100) == 2);
}

TEST(SpawnDominanceNearlyClosesNaturalSpawnsAtTwoLevelGap)
{
    // A mob exactly two levels above its habitat contributes pressure 1 at
    // the centre of its influence.  Keep the default strong enough that a
    // manual Primordial in a U-zone leaves only a small natural-spawn chance.
    CHECK_NEAR(game_config::open_dominant_mob_range, game_config::WorldUnits(2048), 0.00001);
    CHECK_NEAR(game_config::open_dominant_mob_rate_strength, 12.f, 0.00001);
    CHECK_NEAR(game_config::open_dominant_mob_min_rate, 0.02f, 0.00001);
    CHECK_NEAR(game_config::open_dominant_mob_level_penalty, 1.5f, 0.00001);

    const auto influence = florrbt::MakeSpawnInfluence({}, 0, 2, game_config::open_dominant_mob_range);
    const float centre_rate = florrbt::SpawnRateUnderPressure(influence.strength,
        game_config::open_dominant_mob_rate_strength, game_config::open_dominant_mob_min_rate);
    CHECK_NEAR(influence.strength, 1.f, 0.00001);
    CHECK_NEAR(centre_rate, 1.f / 13.f, 0.00001);
    CHECK(centre_rate < 0.1f);
    CHECK(centre_rate >= game_config::open_dominant_mob_min_rate);
    CHECK_NEAR(florrbt::SpawnLevelPenalty(influence.strength,
        game_config::open_dominant_mob_level_penalty), 1.5f, 0.00001);
}

TEST(SpawnHabitatSelectsContainingOrNearestPolygonNotCentroid)
{
    std::vector<Zone> zones{ MakeZone(6, 0, 0, 1000, 20), MakeZone(8, 1050, 40, 20, 20) };
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 500, 10 }), 60, 0.00001);
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 990, 25 }), 60, 0.00001);
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 1050, 50 }), 80, 0.00001);
    zones.push_back(MakeZone(7, 0, 0, 100, 20));
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 50, 10 }), 70, 0.00001);
    zones[2].mobs.clear();
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 50, 10 }), 60, 0.00001);
    zones[2].spawn_profile = "profile";
    CHECK_NEAR(*florrbt::FindSpawnHabitatDifficulty(zones, { 50, 10 }), 70, 0.00001);
    zones.clear(); CHECK(!florrbt::FindSpawnHabitatDifficulty(zones, {}));
    const std::vector<florrbt::Vector2f> concave{ {0,0}, {100,0}, {100,20}, {20,20}, {20,100}, {0,100} };
    CHECK_NEAR(florrbt::DistanceSquaredToSpawnPolygon({ 10, 80 }, concave), 0, 0.00001);
    CHECK(florrbt::DistanceSquaredToSpawnPolygon({ 80, 80 }, concave) > 0);
}

TEST(SpawnPressureFallsWithDistanceScalesWithGapAndDoesNotStackWithoutBound)
{
    std::vector<florrbt::SpawnInfluence> influences{ florrbt::MakeSpawnInfluence({}, 0, 2, 1000) };
    CHECK_NEAR(florrbt::MeasureSpawnPressure({}, influences), 1, 0.00001);
    CHECK_NEAR(florrbt::MeasureSpawnPressure({ 500, 0 }, influences), 0.5, 0.00001);
    CHECK_NEAR(florrbt::MeasureSpawnPressure({ 1000, 0 }, influences), 0, 0.00001);
    influences.push_back(influences[0]);
    CHECK_NEAR(florrbt::MeasureSpawnPressure({}, influences), 1, 0.00001);
    CHECK(florrbt::MakeSpawnInfluence({}, 0, 4, 1000).radius > influences[0].radius);
    CHECK(florrbt::MakeSpawnInfluence({}, 0, 4, 1000).strength > influences[0].strength);
    CHECK(florrbt::MakeSpawnInfluence({}, 0, 1, 1000).strength == 0);
    CHECK_NEAR(florrbt::SpawnRateUnderPressure(0, .75f, .15f), 1, 0.00001);
    CHECK_NEAR(florrbt::SpawnRateUnderPressure(1, .75f, .15f), 1.f / 1.75f, 0.00001);
    CHECK_NEAR(florrbt::SpawnRateUnderPressure(100, .75f, .15f), .15f, 0.00001);
    CHECK_NEAR(florrbt::SpawnLevelPenalty(1, .75f), .75f, 0.00001);
    CHECK_NEAR(florrbt::SpawnLevelPenalty(100, .75f), 3, 0.00001);
}

TEST(SpawnPressureLowersQualityAtEveryQuantileIncludingRemoteRareRolls)
{
    const auto level = [](ERarity rarity) {
        return rarity == ERarity::Unique ? 9.f : rarity == ERarity::Primordial ? 10.f : GetRaritySortRank(rarity);
    };
    int normal_mythic_or_above = 0, pressured_mythic_or_above = 0;
    for (const float difficulty : { 20.f, 60.f, 69.9f, 80.f, 90.f, 100.f })
    for (int i = 0; i < 10000; ++i)
    {
        const float roll = (static_cast<float>(i) + 0.5f) / 10000.f;
        const auto original = florrbt::SampleSpawnRarity(difficulty, 10, .36f, roll, level);
        const auto pressured = florrbt::SampleSpawnRarity(difficulty - 7.5f, 10, .36f, roll, level);
        CHECK(GetRaritySortRank(pressured) <= GetRaritySortRank(original));
        if (difficulty == 60.f)
        {
            normal_mythic_or_above += level(original) >= 6;
            pressured_mythic_or_above += level(pressured) >= 6;
        }
    }
    CHECK(normal_mythic_or_above > 9000);
    CHECK(pressured_mythic_or_above < 2000);
}
