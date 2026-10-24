#include "extension_runtime.h"
#include "chat_extension.h"
#include "squad_extension.h"
#include "weather_extension.h"
#include "weather_snapshot.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../HotReload/snapshot_archive.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{
// Serialization adapter keeps the dependency-free weather simulation usable
// by the standalone tests; the runtime still dispatches persistence uniformly.
class CPersistentWeatherExtension final : public CWeatherExtension, public IPersistentExtension
{
  public:
    void CaptureSnapshot(CSnapshotWriter& writer) const override
    {
        const auto state = CaptureWeatherSnapshot(*this);
        if (!state || json_object_update(writer.Object(), state.get()) != 0)
            throw std::runtime_error("Cannot capture weather extension");
    }
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    { return RestoreWeatherSnapshot(*this, reader.Object(), version, error); }
};
}

CExtensionRuntime::CExtensionRuntime()
{
    std::string error;
    RegisterFactory(CWeatherExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CPersistentWeatherExtension>();
    }, error);
    RegisterFactory(CChatExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CChatExtension>();
    }, error);
    RegisterFactory(CSquadExtension({}).Descriptor(), [](SWorldHandle world, const SWorldExtensionConfig& config) -> std::unique_ptr<CExtension> {
        return config.squads ? std::make_unique<CSquadExtension>(world) : nullptr;
    }, error);
}
CExtensionRuntime::~CExtensionRuntime() { ShutDown(); }

bool CExtensionRuntime::RegisterFactory(const SExtensionDescriptor& descriptor, TFactory factory, std::string& error)
{
    error.clear();
    if (m_context || descriptor.key.empty() || !descriptor.state_version || !factory ||
        (descriptor.scope != EExtensionScope::Server && descriptor.scope != EExtensionScope::World) ||
        std::any_of(m_factories.begin(), m_factories.end(), [&](const auto& item) { return item.key == descriptor.key; }))
    { error = "Invalid, duplicate or late extension registration"; return false; }
    m_factories.push_back({std::string(descriptor.key), descriptor.scope, std::move(factory)});
    return true;
}

bool CExtensionRuntime::Attach(SScope& scope, std::unique_ptr<CExtension> extension, std::string& error)
{
    const auto& descriptor = extension->Descriptor();
    if (descriptor.scope != (scope.world.generation ? EExtensionScope::World : EExtensionScope::Server))
    { error = "Extension scope mismatch"; return false; }
    for (const auto& existing : scope.instances)
        if (existing->Descriptor().key == descriptor.key) { error = "Duplicate extension key"; return false; }
    for (auto dependency : descriptor.dependencies)
    {
        const auto has = [&](const SScope& owner) {
            return std::any_of(owner.instances.begin(), owner.instances.end(), [&](const auto& value) {
                return value->Descriptor().key == dependency;
            });
        };
        if (!has(scope) && !has(m_globals)) { error = "Missing extension dependency: " + std::string(dependency); return false; }
    }
    auto* commands = dynamic_cast<IPlayerCommandExtension*>(extension.get());
    if (commands) for (auto name : commands->Commands())
        if (scope.commands.contains(std::string(name)) ||
            (&scope != &m_globals && m_globals.commands.contains(std::string(name))))
        { error = "Duplicate extension command: " + std::string(name); return false; }
    if (!extension->OnAttach(*m_context, error)) { extension->OnDetach(); return false; }
    if (commands) for (auto name : commands->Commands()) scope.commands.emplace(name, commands);
    scope.instances.push_back(std::move(extension));
    return true;
}
void CExtensionRuntime::Detach(SScope& scope) noexcept
{
    scope.view.clear();
    scope.commands.clear();
    for (auto it = scope.instances.rbegin(); it != scope.instances.rend(); ++it) (*it)->OnDetach();
    scope.instances.clear();
}
bool CExtensionRuntime::Init(IPlayerDirectory& players, IChatDelivery& delivery, std::string& error)
{
    error.clear();
    if (m_context) { error = "Extensions already initialized"; return false; }
    m_context = std::make_unique<CExtensionContext>(CExtensionContext{players, delivery, nullptr, this});
    for (const auto& factory : m_factories)
    {
        if (factory.scope != EExtensionScope::Server) continue;
        auto extension = factory.create({}, {});
        if (!extension || extension->Descriptor().key != factory.key || !Attach(m_globals, std::move(extension), error))
        { if (error.empty()) error = "Invalid global extension factory"; ShutDown(); return false; }
        m_globals.view.push_back(m_globals.instances.back().get());
        m_context->chat = Globals().Find<IChatService>();
    }
    return true;
}
void CExtensionRuntime::ShutDown() noexcept
{
    for (auto& [id, scope] : m_worlds) Detach(scope);
    m_worlds.clear(); Detach(m_globals); m_context.reset();
}
void CExtensionRuntime::Tick(float dt)
{
    for (const auto& extension : m_globals.instances)
        if (auto* tick = dynamic_cast<ITickExtension*>(extension.get())) tick->Tick(dt);
    for (const auto& [id, scope] : m_worlds)
        for (const auto& extension : scope.instances)
            if (auto* tick = dynamic_cast<ITickExtension*>(extension.get())) tick->Tick(dt);
    RefreshAllWorlds();
}
CExtensionView CExtensionRuntime::AttachWorld(CGameWorld& world, const SWorldExtensionConfig& config)
{
    if (!m_context) throw std::logic_error("Attach world before extension runtime initialization");
    const SWorldHandle handle{world.GetId(), world.GetInstanceGeneration()};
    auto found = m_worlds.find(handle.id);
    std::vector<std::unique_ptr<CExtension>> desired;
    for (const auto& factory : m_factories)
        if (factory.scope == EExtensionScope::World)
        {
            auto extension = factory.create(handle, config);
            if (extension && extension->Descriptor().key != factory.key) throw std::logic_error("World extension factory key mismatch");
            if (extension) desired.push_back(std::move(extension));
        }
    const bool same_instances = found != m_worlds.end() && found->second.instances.size() == desired.size() &&
        std::equal(desired.begin(), desired.end(), found->second.instances.begin(), [](const auto& a, const auto& b) {
            return a->Descriptor().key == b->Descriptor().key;
        });
    if (found != m_worlds.end() && found->second.world == handle && same_instances)
    {
        found->second.config = config;
        return {this, handle};
    }
    SScope staged; staged.world = handle; staged.config = config;
    std::string error;
    for (auto& extension : desired)
    {
        if (!Attach(staged, std::move(extension), error)) { Detach(staged); throw std::runtime_error(error); }
        staged.view.push_back(staged.instances.back().get());
    }
    staged.view.insert(staged.view.end(), m_globals.view.begin(), m_globals.view.end());
    if (found != m_worlds.end()) Detach(found->second);
    m_worlds.insert_or_assign(handle.id, std::move(staged));
    return {this, handle};
}
void CExtensionRuntime::DetachWorld(SWorldHandle world) noexcept
{
    const auto found = m_worlds.find(world.id);
    if (found == m_worlds.end() || found->second.world != world) return;
    Detach(found->second); m_worlds.erase(found);
}
void CExtensionRuntime::RefreshWorld(CGameWorld& world)
{
    const auto found = m_worlds.find(world.GetId());
    if (found == m_worlds.end() || found->second.world.generation != world.GetInstanceGeneration()) return;
    for (const auto& extension : found->second.instances)
        if (auto* maintained = dynamic_cast<IMaintainedExtension*>(extension.get())) maintained->Refresh();
}
void CExtensionRuntime::RefreshAllWorlds()
{
    for (const auto& instance : m_globals.instances)
        if (auto* maintained = dynamic_cast<IMaintainedExtension*>(instance.get())) maintained->Refresh();
    for (auto& [id, scope] : m_worlds)
        for (const auto& instance : scope.instances)
            if (auto* maintained = dynamic_cast<IMaintainedExtension*>(instance.get())) maintained->Refresh();
}
std::span<CExtension* const> CExtensionRuntime::Instances(SWorldHandle world) const
{
    if (world.generation)
    {
        const auto found = m_worlds.find(world.id);
        if (found == m_worlds.end() || found->second.world != world) return {};
        return found->second.view;
    }
    return m_globals.view;
}
std::span<CExtension* const> CExtensionView::Instances() const
{
    return m_runtime ? m_runtime->Instances(m_world) : std::span<CExtension* const>{};
}
const ISquadView* CExtensionRuntime::FindSquads(SWorldHandle world) const
{
    if (!world.generation) return nullptr;
    for (const auto* instance : Instances(world))
        if (const auto* squads = dynamic_cast<const ISquadView*>(instance)) return squads;
    return nullptr;
}
bool CExtensionRuntime::HandleCommand(CPlayer& player, const std::vector<std::string>& args)
{
    if (args.empty() || !player.IsAuthenticated()) return false;
    if (const auto found = m_globals.commands.find(args[0]); found != m_globals.commands.end())
        return found->second->HandleCommand(player, args);
    CEntity* entity = player.GetEntity(); CGameWorld* world = entity ? entity->GameWorld() : nullptr;
    if (world)
    {
        const auto found = m_worlds.find(world->GetId());
        if (found != m_worlds.end() && found->second.world.generation == world->GetInstanceGeneration())
            if (const auto command = found->second.commands.find(args[0]); command != found->second.commands.end())
                return command->second->HandleCommand(player, args);
    }
    static constexpr std::array<std::string_view, 5> squad_names{"invite", "join", "leave", "sq", "squad"};
    if (std::find(squad_names.begin(), squad_names.end(), args[0]) == squad_names.end()) return false;
    if (auto* chat = Globals().Find<IChatService>()) chat->SendSystem(player, "squad is unavailable in this world");
    return true;
}
void CExtensionRuntime::SyncSquadMetadata(CPlayer& player)
{
    CEntity* entity = player.GetEntity(); CGameWorld* world = entity ? entity->GameWorld() : nullptr;
    const auto* squads = world ? world->Extensions().Find<ISquadView>() : nullptr;
    if (auto* chat = Globals().Find<IChatService>())
        chat->SendSystem(player, CSquadExtension::Metadata(squads ? squads->Members(player) : std::vector<CPlayer*>{}));
}
void CExtensionRuntime::CaptureScope(const SScope& scope, CSnapshotWriter& writer, std::string_view field)
{
    auto values = MakeJsonArray();
    for (const auto& instance : scope.instances)
    {
        const auto* persistent = dynamic_cast<const IPersistentExtension*>(instance.get());
        if (!persistent) continue;
        auto value = MakeJsonObject(); CSnapshotWriter entry(value.get());
        entry.Field("key", instance->Descriptor().key); entry.Field("version", instance->Descriptor().state_version);
        auto data = MakeJsonObject(); CSnapshotWriter fields(data.get()); persistent->CaptureSnapshot(fields);
        entry.Node("data", data.release()); AppendJson(values.get(), value.release());
    }
    writer.Node(field, values.release());
}

bool CExtensionRuntime::RestoreScope(SScope& scope, const CSnapshotReader& reader, std::string_view field, std::string& error)
{
    const auto* values = reader.Node(field);
    const size_t expected = std::count_if(scope.instances.begin(), scope.instances.end(), [](const auto& extension) {
        return dynamic_cast<const IPersistentExtension*>(extension.get()) != nullptr;
    });
    if (!json_is_array(values) || json_array_size(values) != expected)
    { error = "Missing or extra persistent extension state"; return false; }
    std::vector<std::string> keys;
    for (size_t i = 0; i < json_array_size(values); ++i)
    {
        CSnapshotReader saved(json_array_get(values, i)); const auto key = saved.String("key");
        const auto target = std::find_if(scope.instances.begin(), scope.instances.end(), [&](const auto& extension) {
            return extension->Descriptor().key == key;
        });
        if (target == scope.instances.end() || std::find(keys.begin(), keys.end(), key) != keys.end())
        { error = "Unknown or duplicate persistent extension: " + key; return false; }
        keys.push_back(key);
        const auto version = saved.UInt32("version");
        auto* persistent = dynamic_cast<IPersistentExtension*>(target->get());
        CSnapshotReader data(saved.Node("data"));
        if (!persistent || !data.IsValid() || !version || version > (*target)->Descriptor().state_version)
        { error = "Invalid persistent extension data/version: " + key; return false; }
        if (!persistent->RestoreSnapshot(data, version, error)) return false;
    }
    return true;
}

void CExtensionRuntime::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CaptureScope(m_globals, writer, "globals");
    auto worlds = MakeJsonArray();
    std::vector<std::uint32_t> ids;
    for (const auto& [id, scope] : m_worlds) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
    {
        const auto& scope = m_worlds.at(id);
        auto value = MakeJsonObject(); CSnapshotWriter entry(value.get());
        entry.Field("world_id", id); entry.UInt64("world_generation", scope.world.generation);
        entry.Field("squads", scope.config.squads); entry.Field("weather_visuals", scope.config.weather_visuals);
        entry.Field("weather_spawns", scope.config.weather_spawns);
        CaptureScope(scope, entry, "instances"); AppendJson(worlds.get(), value.release());
    }
    writer.Node("worlds", worlds.release());
}
bool CExtensionRuntime::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version,
    const std::vector<CGameWorld*>& worlds, std::string& error)
{
    if (!RestoreScope(m_globals, reader, "globals", error)) return false;
    const json_t* entries = reader.Node("worlds");
    if (version != 1 || !json_is_array(entries) || json_array_size(entries) != worlds.size())
    { error = "Invalid extension scope topology"; return false; }
    std::vector<std::uint32_t> seen;
    for (size_t i = 0; i < json_array_size(entries); ++i)
    {
        CSnapshotReader entry(json_array_get(entries, i));
        const auto id = entry.UInt32("world_id");
        const auto world = std::find_if(worlds.begin(), worlds.end(), [&](auto* candidate) { return candidate && candidate->GetId() == id; });
        if (world == worlds.end() || (*world)->GetInstanceGeneration() != entry.UInt64("world_generation") ||
            std::find(seen.begin(), seen.end(), id) != seen.end()) { error = "Invalid extension world identity"; return false; }
        seen.push_back(id);
        for (const char* field : {"squads", "weather_visuals", "weather_spawns"})
            if (!json_is_boolean(entry.Node(field))) { error = "Invalid extension scope configuration"; return false; }
        const SWorldExtensionConfig config{entry.Bool("squads"), entry.Bool("weather_visuals"), entry.Bool("weather_spawns")};
        if ((*world)->ExtensionConfig() != config)
        { error = "World and extension binding configurations disagree"; return false; }
        (*world)->ConfigureExtensions(config);
        auto& scope = m_worlds.at(id);
        if (!RestoreScope(scope, entry, "instances", error)) return false;
    }
    return true;
}
bool CExtensionRuntime::RestoreLegacySquads(CGameWorld& world, const CSnapshotReader& data, std::string& error)
{
    auto* squads = world.Extensions().Find<CSquadExtension>();
    return !squads || squads->RestoreSnapshot(data, 1, error);
}
