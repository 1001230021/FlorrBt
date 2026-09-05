#pragma once

#include <type_traits>

// Small, dependency-free math value types used by the simulation and wire
// protocol.  Keeping these types in Shared makes the game model independent
// from a rendering/windowing library; platform adapters can convert to a
// library-specific vector only at the actual I/O boundary.
namespace florrbt
{
template <typename T> struct Vector2
{
    static_assert(std::is_arithmetic_v<T>, "Vector2 requires an arithmetic scalar type");

    T x{};
    T y{};

    constexpr Vector2() noexcept = default;
    constexpr Vector2(T x_value, T y_value) noexcept : x(x_value), y(y_value) {}

    template <typename U, std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
    constexpr Vector2(const Vector2<U>& other) noexcept
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y))
    {
    }

    constexpr Vector2& operator+=(const Vector2& other) noexcept
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    constexpr Vector2& operator-=(const Vector2& other) noexcept
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    constexpr Vector2& operator*=(T scalar) noexcept
    {
        x *= scalar;
        y *= scalar;
        return *this;
    }

    constexpr Vector2& operator/=(T scalar) noexcept
    {
        x /= scalar;
        y /= scalar;
        return *this;
    }

    constexpr Vector2 operator+() const noexcept { return *this; }
    constexpr Vector2 operator-() const noexcept { return { -x, -y }; }

    constexpr bool operator==(const Vector2&) const noexcept = default;
};

template <typename T> constexpr Vector2<T> operator+(Vector2<T> lhs, const Vector2<T>& rhs) noexcept
{
    lhs += rhs;
    return lhs;
}

template <typename T> constexpr Vector2<T> operator-(Vector2<T> lhs, const Vector2<T>& rhs) noexcept
{
    lhs -= rhs;
    return lhs;
}

template <typename T> constexpr Vector2<T> operator*(Vector2<T> value, T scalar) noexcept
{
    value *= scalar;
    return value;
}

template <typename T> constexpr Vector2<T> operator*(T scalar, Vector2<T> value) noexcept
{
    value *= scalar;
    return value;
}

template <typename T> constexpr Vector2<T> operator/(Vector2<T> value, T scalar) noexcept
{
    value /= scalar;
    return value;
}

using Vector2f = Vector2<float>;
using Vector2i = Vector2<int>;
using Vector2u = Vector2<unsigned int>;
} // namespace florrbt
