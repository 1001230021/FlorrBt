#pragma once

#include "../../../Shared/network_msg.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

class CEntity;
class CFlower;
class CPlayer;

class CSnapshotService
{
  public:
    static constexpr size_t packet_budget = static_cast<size_t>(std::numeric_limits<std::uint16_t>::max()) - 1024u;
    static constexpr size_t backlog_skip_bytes = packet_budget * 2u;
    static constexpr size_t entity_budget = 2048u;
    static constexpr size_t snapshot_header_size = server_snapshot_header_size;

    struct SBuildResult
    {
        ServerMessage message;
        size_t visible_count = 0;
        size_t packed_size = 0;
    };

    // Reuse serialized entities only during one immutable snapshot pass.
    // Keep this local to a worker job; do not retain it across world mutation
    // or share it between threads. Standalone builds use their own batch.
    class CBuildBatch
    {
      public:
        CBuildBatch();
        ~CBuildBatch();
        CBuildBatch(const CBuildBatch&) = delete;
        CBuildBatch& operator=(const CBuildBatch&) = delete;

      private:
        friend class CSnapshotService;
        struct SImpl;
        std::unique_ptr<SImpl> m_impl;
    };

    bool BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out) const;
    bool BuildSnapshot(CPlayer& player, std::uint32_t snapshot_id, SBuildResult& out, CBuildBatch& batch) const;
    static std::vector<SOwnerPetalSlot> BuildPrimarySlots(const CFlower& flower);
    ServerEntitySnap BuildEntitySnap(const CEntity& entity, const CEntity& owner) const;
};
