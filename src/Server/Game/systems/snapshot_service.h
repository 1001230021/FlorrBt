#pragma once

#include "../../../Shared/network_msg.h"
#include <cstddef>
#include <cstdint>
#include <limits>

class CEntity;
class CPlayer;

class CSnapshotService
{
  public:
    static constexpr size_t packet_budget = static_cast<size_t>(std::numeric_limits<std::uint16_t>::max()) - 1024u;
    static constexpr size_t backlog_skip_bytes = packet_budget * 2u;
    static constexpr size_t entity_budget = 900u;

    struct SBuildResult
    {
        ServerMessage message;
        size_t visible_count = 0;
        size_t packed_size = 0;
    };

    bool BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out) const;
    ServerEntitySnap BuildEntitySnap(const CEntity& entity, const CEntity& owner) const;
};
