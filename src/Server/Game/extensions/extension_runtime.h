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

// Non-owning scope binding. A generation change makes stale world views empty.
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
    // Register before Init. A null world factory result means disabled for
    // this configuration. Live unload/re-entrant registration is not allowed.
    bool RegisterFactory(const SExtensionDescriptor&, TFactory, std::string& error);
    bool Init(IPlayerDirectory&, IChatDelivery&, std::string& error);
    void ShutDown() noexcept;
    void Tick(float dt);
    CExtensionView Globals() { return {this, {}}; }
    CExtensionView AttachWorld(CGameWorld&, const SWorldExtensionConfig&);
    void DetachWorld(SWorldHandle) noexcept;
    void RefreshWorld(CGameWorld&);
    void RefreshAllWorlds();
    // Command names are registered once per scope. Dispatch never guesses
    // by casting a world controller or leaks a squad message to global chat.
    bool HandleCommand(CPlayer&, const std::vector<std::string>& args);
    void SyncSquadMetadata(CPlayer&);
    const ISquadView* FindSquads(SWorldHandle) const override;
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
    struct SFactory { std::string key; EExtensionScope scope; TFactory create; };
    std::vector<SFactory> m_factories;
    SScope m_globals;
    std::unordered_map<std::uint32_t, SScope> m_worlds;
    std::unique_ptr<CExtensionContext> m_context;
};
