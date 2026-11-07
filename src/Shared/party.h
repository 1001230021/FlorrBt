#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct SPartyMember
{
    std::uint32_t player_id = 0;
    std::uint64_t session_generation = 0;
    std::string name;
    bool connected = false;
    std::uint32_t world_id = 0;
    std::uint64_t world_generation = 0;
    bool has_entity = false;
    std::uint32_t entity_id = 0;
    std::uint64_t entity_generation = 0;
    bool operator==(const SPartyMember&) const = default;
};

struct SPartyState
{
    static constexpr std::size_t max_members = 64;
    std::uint32_t leader_id = 0;
    std::uint64_t leader_generation = 0;
    std::vector<SPartyMember> members;
    bool operator==(const SPartyState&) const = default;
};

inline bool IsValidPartyState(const SPartyState& state)
{
    if (state.members.size() > SPartyState::max_members) return false;
    if (state.members.empty()) return !state.leader_id && !state.leader_generation;
    bool leader = false;
    for (size_t i = 0; i < state.members.size(); ++i)
    {
        const auto& member = state.members[i];
        if (!member.player_id || !member.session_generation || member.session_generation == UINT64_MAX ||
            member.name.size() > UINT8_MAX || (!member.world_generation && member.world_id) ||
            (member.has_entity && (!member.world_generation || !member.entity_generation)) ||
            (!member.has_entity && (member.entity_id || member.entity_generation))) return false;
        for (size_t j = 0; j < i; ++j) if (state.members[j].player_id == member.player_id) return false;
        leader = leader || (member.player_id == state.leader_id && member.session_generation == state.leader_generation);
    }
    return leader;
}
