#pragma once
#include "circle_contact.h"
#include "math_types.h"
#include <algorithm>
#include <cmath>

namespace florrbt
{
inline Vector2f StepMoonOrbit(Vector2f center, float radius, float& phase, float angular_speed, float dt)
{
    constexpr float tau = 6.2831853071795864769f;
    if (dt > 0.f && std::isfinite(dt) && std::isfinite(angular_speed))
        phase = std::fmod(phase + std::max(0.f, angular_speed) * dt, tau);
    return center + Vector2f{ std::cos(phase), std::sin(phase) } * std::max(0.f, radius);
}

struct MoonOrbitMotion
{
    Vector2f movement_velocity{};
    Vector2f final_velocity{};
};

// The orbit is a kinematic component, not a tether to the spawn point.
// Keep external impulses separately by subtracting the last orbit velocity.
// Rebase on the actual position so wall/contact corrections never snap back.
inline MoonOrbitMotion StepDriftingMoonOrbit(Vector2f position, Vector2f velocity, Vector2f& center,
                                             Vector2f& orbit_velocity, float& phase, float radius,
                                             float angular_speed, float slowdown_time, float dt)
{
    if (!(dt > 0.f) || !std::isfinite(dt)) return { {}, velocity };
    radius = std::max(0.f, radius);
    center = position - Vector2f{ std::cos(phase), std::sin(phase) } * radius;
    const Vector2f external_velocity = velocity - orbit_velocity;
    const float time = std::max(0.001f, slowdown_time);
    const float decay = std::exp(-dt / time);
    const Vector2f drift = external_velocity * (-std::expm1(-dt / time) * time);
    const Vector2f target = StepMoonOrbit(center, radius, phase, angular_speed, dt);
    orbit_velocity = (target - position) / dt;
    center += drift;
    return { orbit_velocity + drift / dt, orbit_velocity + external_velocity * decay };
}

// Wax-priority resistance modifies *raw-mass-derived* mobility, not a fixed
// 8/92 split. Locked mobs remain immovable. Active Wax uses ordinary mass
// weights without the global mob-contact exponent.
inline CircleContactWeights ComputeWaxContactWeights(float wax_mass, float mob_mass, bool mob_locked,
                                                      float mass_exponent, bool wax_priority,
                                                      float priority_fraction)
{
    auto weights = ComputeCircleContactWeights(wax_mass, mob_mass, false, mob_locked, mass_exponent);
    if (wax_priority && !mob_locked && weights.lhs > 0.f && weights.rhs > 0.f)
    {
        const float fraction = std::clamp(priority_fraction, 0.f, 1.f);
        weights.lhs *= fraction;
        weights.rhs *= 1.f - fraction;
        const float sum = weights.lhs + weights.rhs;
        if (sum > 0.f) { weights.lhs /= sum; weights.rhs /= sum; }
    }
    return weights;
}

inline Vector2f ComputeWaxSpawnVelocity(Vector2f direction, float travel, float damping, float tick_interval)
{
    const float length = std::hypot(direction.x, direction.y);
    if (!(length > 0.f) || !std::isfinite(length)) return {};
    const float speed = std::max(0.f, travel) * (1.f - std::clamp(damping, 0.f, 0.999f)) /
                        std::max(0.001f, tick_interval);
    return direction * (speed / length);
}
}
