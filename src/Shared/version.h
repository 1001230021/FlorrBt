#pragma once

#include <cstdint>
#include <string_view>

namespace florrbt
{
inline constexpr std::string_view name = "FlorrBt";
// 首包校验协议版本，须与客户端及构建版本同步。
inline constexpr std::uint8_t version_major = 1;
inline constexpr std::uint8_t version_minor = 0;
inline constexpr std::uint8_t version_patch = 9;
inline constexpr std::string_view version = "1.0.9";
inline constexpr std::string_view version_label = "FlorrBt v1.0.9";
}
