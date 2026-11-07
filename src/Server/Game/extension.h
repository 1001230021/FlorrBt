#pragma once
#include "../../Shared/client_features.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CPlayer;
class IPlayerDirectory;
class IChatDelivery;
class IPartyDelivery;
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
    std::vector<SFeatureRule> features;
    bool operator==(const SWorldExtensionConfig&) const = default;
};

struct SExtensionDescriptor
{
    std::string_view key;
    EExtensionScope scope;
    std::uint32_t state_version;
    std::span<const std::string_view> dependencies{};
    // 旧描述符默认要求恢复状态，避免缺项时静默丢失数据。
    bool required_state = true;
};

struct CExtensionContext
{
    IPlayerDirectory& players;
    IChatDelivery& chat_delivery;
    IPartyDelivery& party_delivery;
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

// 在快照等只读消费者运行前，于主线程发布派生状态。
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
    // 可选状态须定义缺项时的重置，不能沿用上一场景的数据。
    virtual bool RestoreDefaultState(std::string& error)
    { error = "Extension has no default-state migration"; return false; }
};

class IRestoreBoundaryExtension
{
  public:
    virtual ~IRestoreBoundaryExtension() = default;
    virtual void BeginRestore() = 0;
};

class IPlayerStateExtension
{
  public:
    virtual ~IPlayerStateExtension() = default;
    virtual bool PublishPlayerState(CPlayer&) = 0;
};

class IClientFeatureExtension
{
  public:
    virtual ~IClientFeatureExtension() = default;
    virtual std::span<const std::string_view> FeatureKeys() const = 0;
    virtual void DescribeFeatures(const CPlayer&, std::vector<SClientFeature>&) const = 0;
};
