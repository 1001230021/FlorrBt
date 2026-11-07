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

// 按质量比计算权重，避免质量幂溢出或倒数和被 epsilon 截断后重物无法分离。
// 指数 1 为普通质量权重，指数 2 使 10:1 的质量比产生 100:1 的权重差。
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

// 无弹性、无摩擦圆接触；按逆质量修正法向速度和位置，锁定物体权重为零。
// 不衰减分离或切向运动，使玩家能够沿物体滑动。
inline bool ResolveCircleContact(Vector2f& lhs_pos, Vector2f& lhs_velocity, float lhs_radius, float lhs_inv_mass,
                                 Vector2f& rhs_pos, Vector2f& rhs_velocity, float rhs_radius, float rhs_inv_mass,
                                 float separation = 1.f, float velocity_transfer = 1.f)
{
    const Vector2f delta = rhs_pos - lhs_pos;
    const float distance = std::hypot(delta.x, delta.y);
    const float reach = std::max(0.f, lhs_radius) + std::max(0.f, rhs_radius);
    // 容差仅让刚分离的接触对继续参与速度压力求解，不额外推开位置。
    const float skin = std::max(0.0001f, reach * 0.00001f);
    if (!std::isfinite(distance) || !std::isfinite(reach) || distance > reach + skin || reach <= 0.f) return false;
    if (!std::isfinite(lhs_inv_mass) || lhs_inv_mass < 0.f) lhs_inv_mass = 0.f;
    if (!std::isfinite(rhs_inv_mass) || rhs_inv_mass < 0.f) rhs_inv_mass = 0.f;
    if (!std::isfinite(lhs_velocity.x) || !std::isfinite(lhs_velocity.y)) lhs_velocity = {};
    if (!std::isfinite(rhs_velocity.x) || !std::isfinite(rhs_velocity.y)) rhs_velocity = {};
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
    // 位置修正不能超过接触深度，否则会出现可见弹跳。
    const float correction = std::max(0.f, reach - distance) * std::clamp(separation, 0.f, 1.f);
    lhs_pos -= normal * (correction * lhs_weight);
    rhs_pos += normal * (correction * rhs_weight);

    const Vector2f relative = lhs_velocity - rhs_velocity;
    const float closing = relative.x * normal.x + relative.y * normal.y;
    if (closing > 0.f)
    {
        // 仅修正相对法向速度，并将冲量传给另一物体；同速跟随和分离运动不损失速度。
        const float impulse = closing * std::clamp(velocity_transfer, 0.f, 1.f);
        lhs_velocity -= normal * (impulse * lhs_weight);
        rhs_velocity += normal * (impulse * rhs_weight);
    }
    return true;
}

// 转向使用有界加速度，因此减速状态保留时，外部推挤仍可使生物侧移、后退或超出自身行走速度。
inline Vector2f StepVelocityTowards(Vector2f velocity, Vector2f desired, float acceleration, float dt)
{
    const bool finite_velocity = std::isfinite(velocity.x) && std::isfinite(velocity.y);
    if (!finite_velocity) velocity = {};
    if (!(dt > 0.f) || !std::isfinite(dt) || !(acceleration > 0.f) || !std::isfinite(acceleration))
        return velocity;
    const bool finite_desired = std::isfinite(desired.x) && std::isfinite(desired.y);
    if (!finite_desired) return velocity;
    const Vector2f difference = desired - velocity;
    const float distance = std::hypot(difference.x, difference.y);
    const float step = acceleration * dt;
    if (!std::isfinite(distance) || !std::isfinite(step)) return desired;
    if (distance <= step) return desired;
    const Vector2f result = velocity + difference * (step / distance);
    return std::isfinite(result.x) && std::isfinite(result.y) ? result : desired;
}
}
