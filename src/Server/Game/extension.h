#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CPlayer;
class IPlayerDirectory;
class IChatDelivery;
class IChatService;
class ISquadDirectory;
class CSnapshotReader;
class CSnapshotWriter;

enum class EExtensionScope { Server, World };

struct SPlayerSessionRef
{
    std::uint32_t id = 0;
    std::uint64_t generation = 0;
    std::string account;
    bool operator==(const SPlayerSessionRef&) const = default;
};

struct SWorldHandle
{
    std::uint32_t id = 0;
    std::uint64_t generation = 0;
    bool operator==(const SWorldHandle&) const = default;
};

struct SWorldExtensionConfig
{
    bool squads = false;
    bool weather_visuals = false;
    bool weather_spawns = false;
    bool operator==(const SWorldExtensionConfig&) const = default;
};

struct SExtensionDescriptor
{
    std::string_view key;
    EExtensionScope scope;
    std::uint32_t state_version;
    std::span<const std::string_view> dependencies{};
};

struct CExtensionContext
{
    IPlayerDirectory& players;
    IChatDelivery& chat_delivery;
    IChatService* chat = nullptr;
    const ISquadDirectory* squads = nullptr;
};

class CExtension
{
  public:
    virtual ~CExtension() = default;
    virtual const SExtensionDescriptor& Descriptor() const = 0;
    virtual bool OnAttach(CExtensionContext&, std::string&) { return true; }
    virtual void OnDetach() noexcept {}
};

class ITickExtension
{
  public:
    virtual ~ITickExtension() = default;
    virtual void Tick(float dt) = 0;
};

// Main-thread publication barrier before read-only consumers (snapshots).
class IMaintainedExtension
{
  public:
    virtual ~IMaintainedExtension() = default;
    virtual void Refresh() = 0;
};

class IPlayerCommandExtension
{
  public:
    virtual ~IPlayerCommandExtension() = default;
    virtual std::span<const std::string_view> Commands() const = 0;
    virtual bool HandleCommand(CPlayer&, const std::vector<std::string>& args) = 0;
};

class IPersistentExtension
{
  public:
    virtual ~IPersistentExtension() = default;
    virtual void CaptureSnapshot(CSnapshotWriter&) const = 0;
    virtual bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string&) = 0;
};
