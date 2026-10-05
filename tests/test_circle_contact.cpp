#include "test_framework.h"
#include "Shared/circle_contact.h"
#include <limits>
#include <vector>

using florrbt::Vector2f;

TEST(CircleContactStopsInwardMotionAndPreservesSliding)
{
    Vector2f a{ 0, 0 }, b{ 9, 0 }, av{ 8, 3 }, bv{};
    CHECK(florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 0));
    CHECK_NEAR(a.x, -1, 0.00001);
    CHECK_NEAR(b.x, 9, 0.00001);
    CHECK_NEAR(av.x, 0, 0.00001);
    CHECK_NEAR(av.y, 3, 0.00001);
}

TEST(CircleContactMassWeightsAndSeparatingMotion)
{
    Vector2f a{ 0, 0 }, b{ 9, 0 }, av{ 8, 0 }, bv{};
    florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 0.5f);
    CHECK_NEAR(b.x - a.x, 10, 0.00001);
    CHECK_NEAR(av.x, bv.x, 0.00001);
    CHECK_NEAR(av.x + 2 * bv.x, 8, 0.00001);

    a = { 0, 0 }; b = { 9, 0 }; av = { -4, 3 }; bv = {};
    florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 0);
    CHECK_NEAR(av.x, -4, 0.00001);
    CHECK_NEAR(av.y, 3, 0.00001);
}

TEST(CircleContactNeverOverCorrectsAndHandlesLockedPairs)
{
    Vector2f a{}, b{ 9, 0 }, av{ 2, 0 }, bv{};
    florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 0, 1.25f);
    CHECK_NEAR(b.x - a.x, 10, 0.00001);
    a = {}; b = {}; av = {}; bv = {};
    florrbt::ResolveCircleContact(a, av, 5, 0, b, bv, 5, 0);
    CHECK(std::isfinite(a.x) && std::isfinite(b.x));
    florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 0);
    CHECK_NEAR(std::hypot(b.x - a.x, b.y - a.y), 10, 0.00001);
}

TEST(CircleContactAmplifiesMassGapWithoutOverflow)
{
    const auto ten_to_one = florrbt::ComputeCircleContactWeights(10, 1, false, false, 2);
    CHECK_NEAR(ten_to_one.lhs, 1.f / 101.f, 0.000001);
    CHECK_NEAR(ten_to_one.rhs, 100.f / 101.f, 0.000001);
    const auto linear = florrbt::ComputeCircleContactWeights(10, 1, false, false, 1);
    CHECK_NEAR(linear.lhs, 1.f / 11.f, 0.000001);
    const auto reverse = florrbt::ComputeCircleContactWeights(1, 10, false, false, 2);
    CHECK_NEAR(reverse.lhs, ten_to_one.rhs, 0.000001);
    CHECK_NEAR(reverse.rhs, ten_to_one.lhs, 0.000001);

    const float enormous = std::numeric_limits<float>::max();
    const auto same = florrbt::ComputeCircleContactWeights(enormous, enormous, false, false, 4);
    CHECK_NEAR(same.lhs, 0.5, 0.000001);
    CHECK_NEAR(same.rhs, 0.5, 0.000001);
    const auto extreme = florrbt::ComputeCircleContactWeights(enormous, 1, false, false, 4);
    CHECK_NEAR(extreme.lhs, 0, 0.000001);
    CHECK_NEAR(extreme.rhs, 1, 0.000001);
    const auto fallback = florrbt::ComputeCircleContactWeights(-1, std::numeric_limits<float>::quiet_NaN());
    CHECK_NEAR(fallback.lhs, 0.5, 0.000001);
    CHECK_NEAR(florrbt::ComputeCircleContactWeights(1, 1, true, false, 2).lhs, 0, 0.000001);
    CHECK_NEAR(florrbt::ComputeCircleContactWeights(1, 1, true, true, 2).rhs, 0, 0.000001);
    CHECK_NEAR(florrbt::ComputeCircleContactWeights(std::numeric_limits<float>::infinity(), 1).rhs, 1, 0.000001);
}

TEST(CommonOppositionCannotStopPrimordialMomentum)
{
    // Same type: current mass curve is 2.5^(.6 * rarity level difference).
    const float common_mass = 30.f;
    const float primordial_mass = common_mass * std::pow(2.5f, 9.f * 0.6f);
    const auto weights = florrbt::ComputeCircleContactWeights(primordial_mass, common_mass, false, false, 2);
    Vector2f heavy_pos{}, light_pos{ 100, 0 }, heavy_velocity{ 150, 17 }, light_velocity{ -150, -8 };
    florrbt::ResolveCircleContact(heavy_pos, heavy_velocity, 100, weights.lhs,
        light_pos, light_velocity, 10, weights.rhs, 0.6f);
    CHECK(heavy_velocity.x > 149.98f);
    CHECK(light_velocity.x > 149.98f);
    CHECK_NEAR(heavy_velocity.y, 17, 0.00001);
    CHECK_NEAR(light_velocity.y, -8, 0.00001);
    CHECK(std::abs(heavy_pos.x) < 0.001f);
    CHECK(light_pos.x > 105.99f);

    // Sustained head-on contact must not reintroduce a constant per-tick tax.
    for (int tick = 0; tick < 200; ++tick)
    {
        heavy_pos = {}; light_pos = { 100, 0 };
        heavy_velocity = florrbt::StepVelocityTowards(heavy_velocity, { 150, 0 }, 300, 0.05f);
        light_velocity = florrbt::StepVelocityTowards(light_velocity, { -150, 0 }, 300, 0.05f);
        florrbt::ResolveCircleContact(heavy_pos, heavy_velocity, 100, weights.lhs,
            light_pos, light_velocity, 10, weights.rhs, 0.6f);
    }
    CHECK(heavy_velocity.x > 149.98f);
}

TEST(CircleContactPreservesCoMovingAndSeparatingVelocities)
{
    for (const float transfer : { 0.35f, 1.f })
    {
        Vector2f a{}, b{ 9, 0 }, av{ 70, 12 }, bv{ 70, -8 };
        florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 1, 0.6f, transfer);
        CHECK_NEAR(av.x, 70, 0.00001);
        CHECK_NEAR(bv.x, 70, 0.00001);
        CHECK_NEAR(av.y, 12, 0.00001);
        CHECK_NEAR(bv.y, -8, 0.00001);
        a = {}; b = { 9, 0 }; av = { 20, 12 }; bv = { 80, -8 };
        florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 1, 0.6f, transfer);
        CHECK_NEAR(av.x, 20, 0.00001);
        CHECK_NEAR(bv.x, 80, 0.00001);
    }
}

TEST(CircleContactConservesEffectiveMomentumAndDoesNotCreateEnergy)
{
    for (const float ratio : { 1.f, 2.f, 10.f, 100.f })
    for (const float transfer : { 0.35f, 1.f })
    {
        const auto weights = florrbt::ComputeCircleContactWeights(ratio, 1, false, false, 2);
        const double mass = static_cast<double>(ratio) * ratio;
        Vector2f a{}, b{ 9, 0 }, av{ 90, 7 }, bv{ -30, 3 };
        const double momentum = mass * av.x + bv.x;
        const double energy = mass * (av.x * av.x + av.y * av.y) + bv.x * bv.x + bv.y * bv.y;
        florrbt::ResolveCircleContact(a, av, 5, weights.lhs, b, bv, 5, weights.rhs, 0.6f, transfer);
        CHECK_NEAR(mass * av.x + bv.x, momentum, mass * 0.00002);
        CHECK(mass * (av.x * av.x + av.y * av.y) + bv.x * bv.x + bv.y * bv.y <= energy + 0.00001);
        CHECK_NEAR(av.y, 7, 0.00001);
        CHECK_NEAR(bv.y, 3, 0.00001);
    }
}

TEST(HeavyPairsStillSeparateAndVelocityOnlySweepsDoNotMoveThem)
{
    Vector2f a{}, b{ 9, 0 }, av{ 10, 0 }, bv{};
    florrbt::ResolveCircleContact(a, av, 5, 1e-30f, b, bv, 5, 1e-30f, 0.6f);
    CHECK_NEAR(b.x - a.x, 9.6, 0.00001);
    CHECK_NEAR(av.x, 5, 0.00001);
    CHECK_NEAR(bv.x, 5, 0.00001);
    const auto old_a = a, old_b = b;
    av = { 20, 3 };
    florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 1, 0);
    CHECK(a == old_a && b == old_b);
    CHECK_NEAR(av.x, bv.x, 0.00001);
    b = { 30, 0 }; av = { 20, 3 }; bv = {};
    CHECK(!florrbt::ResolveCircleContact(a, av, 5, 1, b, bv, 5, 1, 0));
    CHECK_NEAR(av.x, 20, 0.00001);
}

namespace
{
float SimulatePackedCrowd(int pushers, bool slowed_front, bool reverse_order)
{
    std::vector<Vector2f> pos(pushers + 1), velocities(pushers + 1);
    for (int i = 0; i <= pushers; ++i) pos[i] = { static_cast<float>(i * 10), 0 };
    for (int tick = 0; tick < 80; ++tick)
    {
        for (int i = 0; i < pushers; ++i)
            velocities[i] = florrbt::StepVelocityTowards(velocities[i], { 100, 0 }, 200, 0.05f);
        velocities.back() = florrbt::StepVelocityTowards(velocities.back(),
            { slowed_front ? -2.f : -100.f, 0 }, slowed_front ? 4.f : 200.f, 0.05f);
        // Packed contacts are maintained by the surrounding crowd. Like the
        // world's extra sweeps this fixture only solves velocities, not damage.
        for (int pass = 0; pass < 3; ++pass)
        for (int j = 0; j < pushers; ++j)
        {
            const int i = (reverse_order != (pass % 2 != 0)) ? pushers - j - 1 : j;
            florrbt::ResolveCircleContact(pos[i], velocities[i], 5, 1,
                pos[i + 1], velocities[i + 1], 5, 1, 0);
        }
        for (const auto& velocity : velocities) CHECK(velocity.x >= -100.001f && velocity.x <= 100.001f);
    }
    return velocities.back().x;
}
}

TEST(CrowdPressureOverpowersOneOpposingOrSlowedMobInEitherOrder)
{
    for (const bool reverse : { false, true })
    {
        CHECK_NEAR(SimulatePackedCrowd(1, false, reverse), 0, 0.00001);
        CHECK(SimulatePackedCrowd(8, false, reverse) > 70.f);
        CHECK(SimulatePackedCrowd(8, true, reverse) > 90.f);
    }
}

TEST(SlowedSteeringCannotEraseOrRotateIncomingVelocity)
{
    for (const int hz : { 20, 60, 120 })
    {
        Vector2f velocity{ 100, 30 };
        for (int tick = 0; tick < hz; ++tick)
            velocity = florrbt::StepVelocityTowards(velocity, { -2, 0 }, 4, 1.f / hz);
        CHECK(velocity.x > 96.f && velocity.y > 28.f);
        CHECK_NEAR(std::hypot(velocity.x - 100, velocity.y - 30), 4, 0.001);
    }
    const Vector2f pushed{ 100, 30 };
    CHECK(florrbt::StepVelocityTowards(pushed, {}, 0, 1) == pushed);
    CHECK(florrbt::StepVelocityTowards(pushed, {}, 100, 0) == pushed);
    CHECK(florrbt::StepVelocityTowards({ 3, 4 }, {}, 100, 1) == Vector2f{});
}
