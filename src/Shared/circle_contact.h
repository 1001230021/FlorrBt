#pragma once
#include "math_types.h"
#include <algorithm>
#include <cmath>

namespace florrbt
{
struct CircleContactWeights
{
    float lhs = 0.f;
    float rhs = 0.f;
};

// Use ratios, never mass^exponent or max(epsilon, sum(1/mass)). The latter
// makes two very heavy bodies stop separating, and the former can overflow.
// Exponent 1 is ordinary mass weighting; 2 makes a 10:1 mass gap act as 100:1.
inline CircleContactWeights ComputeCircleContactWeights(float lhs_mass, float rhs_mass,
                                                        bool lhs_locked = false, bool rhs_locked = false,
                                                        float mass_exponent = 1.f)
{
    lhs_locked = lhs_locked || (std::isinf(lhs_mass) && lhs_mass > 0.f);
    rhs_locked = rhs_locked || (std::isinf(rhs_mass) && rhs_mass > 0.f);
    if (lhs_locked && rhs_locked) return {};
    if (lhs_locked) return { 0.f, 1.f };
    if (rhs_locked) return { 1.f, 0.f };
    if (!(lhs_mass > 0.f) || !std::isfinite(lhs_mass)) lhs_mass = 1.f;
    if (!(rhs_mass > 0.f) || !std::isfinite(rhs_mass)) rhs_mass = 1.f;
    const float exponent = std::isfinite(mass_exponent) ? std::clamp(mass_exponent, 1.f, 4.f) : 1.f;
    const float ratio = std::min(lhs_mass, rhs_mass) / std::max(lhs_mass, rhs_mass);
    const float relative_mass = exponent == 2.f ? ratio * ratio : std::pow(ratio, exponent);
    const float heavy_weight = relative_mass / (1.f + relative_mass);
    const float light_weight = 1.f / (1.f + relative_mass);
    return lhs_mass >= rhs_mass ? CircleContactWeights{ heavy_weight, light_weight }
                                : CircleContactWeights{ light_weight, heavy_weight };
}

// Inelastic, frictionless circle contact. Both normals and position correction
// use inverse-mass weights; a locked body has weight zero. Separating motion
// and tangential motion are never damped, so players can slide along a body.
inline bool ResolveCircleContact(Vector2f& lhs_pos, Vector2f& lhs_velocity, float lhs_radius, float lhs_inv_mass,
                                 Vector2f& rhs_pos, Vector2f& rhs_velocity, float rhs_radius, float rhs_inv_mass,
                                 float separation = 1.f, float velocity_transfer = 1.f)
{
    const Vector2f delta = rhs_pos - lhs_pos;
    const float distance = std::hypot(delta.x, delta.y);
    const float reach = std::max(0.f, lhs_radius) + std::max(0.f, rhs_radius);
    // A tiny skin keeps a just-separated pair eligible for velocity-only
    // pressure sweeps despite float roundoff; it never adds position push.
    const float skin = std::max(0.0001f, reach * 0.00001f);
    if (!std::isfinite(distance) || !std::isfinite(reach) || distance > reach + skin || reach <= 0.f) return false;
    if (!std::isfinite(lhs_inv_mass) || lhs_inv_mass < 0.f) lhs_inv_mass = 0.f;
    if (!std::isfinite(rhs_inv_mass) || rhs_inv_mass < 0.f) rhs_inv_mass = 0.f;
    const float largest = std::max(lhs_inv_mass, rhs_inv_mass);
    if (!(largest > 0.f)) return true;
    lhs_inv_mass /= largest;
    rhs_inv_mass /= largest;
    const float total = lhs_inv_mass + rhs_inv_mass;

    Vector2f normal;
    if (distance > 0.000001f) normal = delta / distance;
    else
    {
        const Vector2f relative = lhs_velocity - rhs_velocity;
        const float speed = std::hypot(relative.x, relative.y);
        normal = speed > 0.000001f ? relative / speed : Vector2f{ 1.f, 0.f };
    }
    const float lhs_weight = lhs_inv_mass / total;
    const float rhs_weight = rhs_inv_mass / total;
    // Never push beyond contact: >100% correction creates visible bouncing.
    const float correction = std::max(0.f, reach - distance) * std::clamp(separation, 0.f, 1.f);
    lhs_pos -= normal * (correction * lhs_weight);
    rhs_pos += normal * (correction * rhs_weight);

    const Vector2f relative = lhs_velocity - rhs_velocity;
    const float closing = relative.x * normal.x + relative.y * normal.y;
    if (closing > 0.f)
    {
        // Change the *relative* normal speed. Equal-speed followers and
        // separating bodies lose nothing, and every lost impulse is received
        // by the other body instead of being discarded once per neighbour.
        const float impulse = closing * std::clamp(velocity_transfer, 0.f, 1.f);
        lhs_velocity -= normal * (impulse * lhs_weight);
        rhs_velocity += normal * (impulse * rhs_weight);
    }
    return true;
}

// Steering is a bounded acceleration, not a reassignment of actual velocity.
// Crowd impulses can therefore carry a slowed mob faster/sideways/backwards
// relative to its requested motion without removing the slowing state.
inline Vector2f StepVelocityTowards(Vector2f velocity, Vector2f desired, float acceleration, float dt)
{
    if (!(dt > 0.f) || !std::isfinite(dt) || !(acceleration > 0.f) || !std::isfinite(acceleration))
        return velocity;
    const Vector2f difference = desired - velocity;
    const float distance = std::hypot(difference.x, difference.y);
    const float step = acceleration * dt;
    if (distance <= step) return desired;
    return velocity + difference * (step / distance);
}
} // namespace florrbt
