#pragma once

#include "snapshot_archive.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

class CServer;

enum class ESnapshotRestorePhase
{
    CreateObjects,
    RestoreState,
    ResolveReferences,
    RebuildDerivedData,
    Validate,
};

struct SSnapshotSectionCodec
{
    using capture_callback = std::function<CJsonOwner(CServer&, std::string&)>;
    using restore_callback =
        std::function<bool(CServer&, const json_t*, std::uint32_t, ESnapshotRestorePhase, std::string&)>;
    using migration_callback = std::function<bool(std::uint32_t, json_t*, std::string&)>;

    std::string key;
    std::uint32_t version = 1;
    bool required = true;
    capture_callback capture;
    restore_callback restore;
    migration_callback migrate;
};

bool RegisterSnapshotSection(SSnapshotSectionCodec codec, std::string* error = nullptr);

class CHotReloadSnapshotService
{
  public:
    static constexpr std::uint32_t container_version = 1;

    static bool Capture(CServer& server, const std::filesystem::path& path, std::string& error);
    static bool Restore(CServer& server, const std::filesystem::path& path, std::string& error);
};
