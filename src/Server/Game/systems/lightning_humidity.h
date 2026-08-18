#pragma once

#include <algorithm>

namespace lightning_humidity
{

inline float ClampHumidity(float humidity)
{
    return std::clamp(humidity, 0.f, 1.f);
}

inline float DamageMultiplier(float humidity)
{
    return 1.f + ClampHumidity(humidity) * 0.2f;
}

inline float RangeMultiplier(float humidity)
{
    return 1.f + ClampHumidity(humidity) * 0.5f;
}

inline int ScaledBounces(int base_bounces, float humidity)
{
    return base_bounces > 0 ? static_cast<int>(base_bounces * RangeMultiplier(humidity)) : 0;
}

} // namespace lightning_humidity
