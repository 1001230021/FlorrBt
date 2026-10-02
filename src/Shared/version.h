#pragma once

#include <cstdint>
#include <string_view>

namespace florrbt
{
inline constexpr std::string_view name = "FlorrBt";
// Wire version triple sent by the client in its first packet; the server
// rejects clients that do not match. Keep in sync with the version string and
// with web/src/version.js.
inline constexpr std::uint8_t version_major = 1;
inline constexpr std::uint8_t version_minor = 0;
inline constexpr std::uint8_t version_patch = 3;
inline constexpr std::string_view version = "1.0.3";
inline constexpr std::string_view version_label = "FlorrBt v1.0.3";
} // namespace florrbt
