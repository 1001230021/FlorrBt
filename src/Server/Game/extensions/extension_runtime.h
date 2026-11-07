#pragma once
#include "../extension.h"
#include "extension_services.h"
#include <memory>
#include <functional>
#include <span>
#include <unordered_map>

class CGameWorld;
class IWeatherDiurnalView;

class CExtensionRuntime;

// 非拥有的作用域绑定；世界代数变化后，旧视图返回空结果。
class CExtensionView
{
  public:
    template<class T> T* Find() const
    {
        for (CExtension* extension : Instances())
            if (auto* capability = dynamic_cast<T*>(extension)) return capability;
        return nullptr;
    }
    CExtensionView() = default;
  private:
    friend class CExtensionRuntime;
    CExtensionView(CExtensionRuntime* runtime, SWorldHandle world) : m_runtime(runtime), m_world(world) {}
    std::span<CExtension* const> Instances() const;
    CExtensionRuntime* m_runtime = nullptr;
    SWorldHandle m_world;
};

class CExtensionRuntime : public ISquadDirectory
{
  public:
    CExtensionRuntime();
    ~CExtensionRuntime();
    using TFactory = std::function<std::unique_ptr<CExtension>(SWorldHandle, const SWorldExtensionConfig&)>;
    // 仅初始化前注册；世界工厂返回空表示当前配置禁用该扩展。
    bool RegisterFactory(const SExtensionDescriptor&, TFactory, std::string& error);
    bool Init(IPlayerDirectory&, IChatDelivery&, IPartyDelivery&, std::string& error);
    void ShutDown() noexcept;
    void Tick(float dt);
    CExtensionView Globals() { return {this, {}}; }
    CExtensionView AttachWorld(CGameWorld&, const SWorldExtensionConfig&);
    void DetachWorld(SWorldHandle) noexcept;
    void RefreshWorld(CGameWorld&);
    void RefreshAllWorlds();
    // 命令在所属作用域注册，由运行时统一分派。
    bool HandleCommand(CPlayer&, const std::vector<std::string>& args);
    bool PublishPlayerState(CPlayer&);
    std::vector<SClientFeature> ClientFeatures(const CPlayer&) const;
    bool CanUseFeature(const CPlayer&, std::string_view key, std::uint8_t access) const;
    bool HasClientFeature(SWorldHandle, std::string_view key) const;
    void BeginRestore();
    const ISquadView* FindSquads(SWorldHandle) const override;
    IPartyService* Parties() const override;
    void CaptureSnapshot(CSnapshotWriter&) const;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, const std::vector<CGameWorld*>&,
                         std::string& error);
    bool RestoreLegacySquads(CGameWorld&, const CSnapshotReader&, std::string& error);
  private:
    friend class CExtensionView;
    struct SScope
    {
        SWorldHandle world;
        SWorldExtensionConfig config;
        std::vector<std::unique_ptr<CExtension>> instances;
        std::vector<CExtension*> view;
        std::unordered_map<std::string, IPlayerCommandExtension*> commands;
    };
    bool Attach(SScope&, std::unique_ptr<CExtension>, std::string& error);
    static void Detach(SScope&) noexcept;
    std::span<CExtension* const> Instances(SWorldHandle) const;
    static void CaptureScope(const SScope&, CSnapshotWriter&, std::string_view field);
    static bool RestoreScope(SScope&, const CSnapshotReader&, std::string_view field, std::string& error);
    struct SFactory
    {
        std::string key; EExtensionScope scope; TFactory create;
        std::uint32_t version;
        std::vector<std::string> dependencies;
        bool required_state;
    };
    bool OrderFactories(std::string& error);
    bool MatchesFactory(const SFactory&, const CExtension&) const;
    struct CDispatchGuard;
    void ApplyDeferredTopology() noexcept;
    unsigned m_dispatch_depth = 0;
    std::vector<SWorldHandle> m_pending_detaches;
    bool m_pending_shutdown = false;
    std::vector<SFactory> m_factories;
    SScope m_globals;
    std::unordered_map<std::uint32_t, SScope> m_worlds;
    std::unique_ptr<CExtensionContext> m_context;
};
