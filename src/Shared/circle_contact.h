#pragma once
#include "math_types.h"
#include <algorithm>
#include <cmath>

namespace florrbt
{
// Inelastic, frictionless circle contact. Both normals and position correction
// use inverse-mass weights; a locked body has weight zero. Separating motion
// and tangential motion are never damped, so players can slide along a body.
inline bool ResolveCircleContact(Vector2f& lhs_pos, Vector2f& lhs_velocity, float lhs_radius, float lhs_inv_mass,
                                 Vector2f& rhs_pos, Vector2f& rhs_velocity, float rhs_radius, float rhs_inv_mass,
                                 float separation = 1.f)
{
    const Vector2f delta = rhs_pos - lhs_pos;
    const float distance = std::hypot(delta.x, delta.y);
    const float reach = std::max(0.f, lhs_radius) + std::max(0.f, rhs_radius);
    if (distance > reach || reach <= 0.f) return false;
    const float total = lhs_inv_mass + rhs_inv_mass;
    if (!(total > 0.f) || !std::isfinite(total)) return true;

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
    const float correction = (reach - distance) * std::clamp(separation, 0.f, 1.f);
    lhs_pos -= normal * (correction * lhs_weight);
    rhs_pos += normal * (correction * rhs_weight);

    const Vector2f relative = lhs_velocity - rhs_velocity;
    const float closing = relative.x * normal.x + relative.y * normal.y;
    if (closing > 0.f)
    {
        lhs_velocity -= normal * (closing * lhs_weight);
        rhs_velocity += normal * (closing * rhs_weight);
    }
    return true;
}
} // namespace florrbt
