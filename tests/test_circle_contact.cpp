#include "test_framework.h"
#include "Shared/circle_contact.h"

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
