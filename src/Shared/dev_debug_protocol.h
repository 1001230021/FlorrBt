#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A separate, explicitly versioned connection branch. Never interpret these
// packets as player input/auth; the first byte selects this branch pre-player.
namespace dev_debug
{
inline constexpr std::uint8_t magic = 0xd0;
inline constexpr std::uint8_t version = 1;
inline constexpr std::size_t header_size = 6;
inline constexpr std::size_t hello_limit = 512;
inline constexpr std::size_t request_limit = 32768;
inline constexpr std::size_t response_limit = 2 * 1024 * 1024;
inline constexpr std::size_t backlog_limit = 4 * 1024 * 1024;
inline constexpr std::size_t entity_limit = 1024;
inline constexpr std::size_t selection_limit = 128;
inline constexpr std::size_t session_limit = 8;
inline constexpr std::size_t ticket_size = 64;

enum class FrameStatus { Incomplete, Ready, Invalid };
inline FrameStatus ReadFrameSize(const std::uint8_t* data, std::size_t size, std::size_t limit,
                                std::size_t& payload_size)
{
    payload_size = 0;
    if (!size) return FrameStatus::Incomplete;
    if (data[0] != magic || (size > 1 && data[1] != version)) return FrameStatus::Invalid;
    if (size < header_size) return FrameStatus::Incomplete;
    for (unsigned i = 0; i < 4; ++i) payload_size |= std::size_t(data[2 + i]) << (i * 8);
    if (!payload_size || payload_size > limit) return FrameStatus::Invalid;
    return size < header_size + payload_size ? FrameStatus::Incomplete : FrameStatus::Ready;
}

inline std::vector<std::uint8_t> PackFrame(std::string_view json)
{
    if (json.empty() || json.size() > response_limit) return {};
    std::vector<std::uint8_t> bytes(header_size + json.size());
    bytes[0] = magic; bytes[1] = version;
    for (unsigned i = 0; i < 4; ++i) bytes[2 + i] = static_cast<std::uint8_t>(json.size() >> (i * 8));
    std::copy(json.begin(), json.end(), bytes.begin() + header_size);
    return bytes;
}

inline bool IsTicket(std::string_view value)
{
    return value.size() == ticket_size && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

// Work is bounded even for an authenticated admin; the debug UI must not
// accidentally flood the simulation while dragging or repeating a command.
struct RateLimit
{
    double tokens = 32.0;
    double time = 0.0;
    bool Take(double now)
    {
        now = std::max(now, time);
        tokens = std::min(32.0, tokens + std::max(0.0, now - time) * 20.0);
        time = now;
        if (tokens < 1.0) return false;
        tokens -= 1.0;
        return true;
    }
};
} // namespace dev_debug
