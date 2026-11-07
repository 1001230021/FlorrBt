#include "extension_runtime.h"
#include "chat_extension.h"
#include "squad_extension.h"
#include "party_extension.h"
#include "player_feature_extensions.h"
#include "weather_extension.h"
#include "weather_snapshot.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../HotReload/snapshot_archive.h"
#include "../../../Engine/logger.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <set>

namespace
{
// JSON 适配与天气模拟分离，使独立模拟测试不依赖持久化库。
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
    RegisterFactory(CInventoryExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) { return std::make_unique<CInventoryExtension>(); }, error);
    RegisterFactory(CTalentExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) { return std::make_unique<CTalentExtension>(); }, error);
    RegisterFactory(CCraftExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) { return std::make_unique<CCraftExtension>(); }, error);
    RegisterFactory(CWeatherExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CPersistentWeatherExtension>();
    }, error);
    RegisterFactory(CChatExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CChatExtension>();
    }, error);
    RegisterFactory(CPartyExtension{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CPartyExtension>();
    }, error);
    RegisterFactory(CSquadExtension({}).Descriptor(), [](SWorldHandle world, const SWorldExtensionConfig& config) -> std::unique_ptr<CExtension> {
        return config.squads ? std::make_unique<CSquadExtension>(world) : nullptr;
    }, error);
}
CExtensionRuntime::~CExtensionRuntime() { ShutDown(); }

struct CExtensionRuntime::CDispatchGuard
{
    CExtensionRuntime& runtime;
    explicit CDispatchGuard(CExtensionRuntime& owner) : runtime(owner) { ++runtime.m_dispatch_depth; }
    ~CDispatchGuard() { if (--runtime.m_dispatch_depth == 0) runtime.ApplyDeferredTopology(); }
};

void CExtensionRuntime::ApplyDeferredTopology() noexcept
{
    if (m_pending_shutdown)
    { m_pending_shutdown = false; m_pending_detaches.clear(); ShutDown(); return; }
    auto pending = std::move(m_pending_detaches);
    m_pending_detaches.clear();
    for (auto world : pending) DetachWorld(world);
}

bool CExtensionRuntime::RegisterFactory(const SExtensionDescriptor& descriptor, TFactory factory, std::string& error)
{
    error.clear();
    if (m_context || descriptor.key.empty() || !descriptor.state_version || !factory ||
        (descriptor.scope != EExtensionScope::Server && descriptor.scope != EExtensionScope::World) ||
        std::any_of(m_factories.begin(), m_factories.end(), [&](const auto& item) { return item.key == descriptor.key; }))
    { error = "Invalid, duplicate or late extension registration"; return false; }
    SFactory entry{std::string(descriptor.key), descriptor.scope, std::move(factory), descriptor.state_version, {}, descriptor.required_state};
    for (auto dependency : descriptor.dependencies)
    {
        if (dependency.empty() || std::find(entry.dependencies.begin(), entry.dependencies.end(), dependency) != entry.dependencies.end())
        { error = "Empty or duplicate extension dependency"; return false; }
        entry.dependencies.emplace_back(dependency);
    }
    m_factories.push_back(std::move(entry));
    return true;
}

bool CExtensionRuntime::MatchesFactory(const SFactory& factory, const CExtension& extension) const
{
    const auto& actual = extension.Descriptor();
    return actual.key == factory.key && actual.scope == factory.scope && actual.state_version == factory.version &&
        actual.required_state == factory.required_state &&
        std::equal(factory.dependencies.begin(), factory.dependencies.end(), actual.dependencies.begin(), actual.dependencies.end());
}

bool CExtensionRuntime::OrderFactories(std::string& error)
{
    std::vector<size_t> order;
    std::vector<unsigned char> visited(m_factories.size(), 0);
    std::function<bool(size_t)> visit = [&](size_t index) {
        if (visited[index] == 2) return true;
        if (visited[index] == 1) { error = "Extension dependency cycle at " + m_factories[index].key; return false; }
        visited[index] = 1;
        const auto& factory = m_factories[index];
        for (const auto& key : factory.dependencies)
        {
            const auto dependency = std::find_if(m_factories.begin(), m_factories.end(), [&](const auto& item) { return item.key == key; });
            if (dependency == m_factories.end()) { error = "Unknown extension dependency: " + key; return false; }
            if (factory.scope == EExtensionScope::Server && dependency->scope == EExtensionScope::World)
            { error = "Global extension cannot depend on a world instance: " + key; return false; }
            if (!visit(static_cast<size_t>(dependency - m_factories.begin()))) return false;
        }
        visited[index] = 2; order.push_back(index); return true;
    };
    for (size_t i = 0; i < m_factories.size(); ++i) if (!visit(i)) return false;
    std::vector<SFactory> sorted; sorted.reserve(order.size());
    for (auto index : order) sorted.push_back(std::move(m_factories[index]));
    m_factories = std::move(sorted);
    return true;
}

bool CExtensionRuntime::Attach(SScope& scope, std::unique_ptr<CExtension> extension, std::string& error)
{
    const auto& descriptor = extension->Descriptor();
    if (descriptor.scope != (scope.world.generation ? EExtensionScope::World : EExtensionScope::Server))
    { error = "Extension scope mismatch"; return false; }
    for (const auto& existing : scope.instances)
        if (existing->Descriptor().key == descriptor.key) { error = "Duplicate extension key"; return false; }
    if (const auto* feature = dynamic_cast<const IClientFeatureExtension*>(extension.get()))
    {
        std::set<std::string_view> keys;
        for (const auto key : feature->FeatureKeys()) {
            if (key.empty() || key.size() > 64 || !keys.insert(key).second) { error = "Invalid feature key"; return false; }
            const auto duplicate = [&](const auto& values) {
                return std::any_of(values.begin(), values.end(), [&](const auto* item) {
                    const auto* other = dynamic_cast<const IClientFeatureExtension*>(item);
                    return other && std::find(other->FeatureKeys().begin(), other->FeatureKeys().end(), key) != other->FeatureKeys().end();
                });
            };
            if (duplicate(scope.view) || duplicate(m_globals.view)) { error = "Duplicate client feature key"; return false; }
        }
    }
    for (auto dependency : descriptor.dependencies)
    {
        const auto has = [&](const SScope& owner) {
            return std::any_of(owner.instances.begin(), owner.instances.end(), [&](const auto& value) {
                return value->Descriptor().key == dependency;
            }) || std::any_of(owner.view.begin(), owner.view.end(), [&](const auto* value) { return value->Descriptor().key == dependency; });
        };
        if (!has(scope) && !has(m_globals)) { error = "Missing extension dependency: " + std::string(dependency); return false; }
    }
    auto* commands = dynamic_cast<IPlayerCommandExtension*>(extension.get());
    if (commands) for (auto name : commands->Commands())
        if (scope.commands.contains(std::string(name)) ||
            (&scope != &m_globals && m_globals.commands.contains(std::string(name))))
        { error = "Duplicate extension command: " + std::string(name); return false; }
    try
    {
        if (!extension->OnAttach(*m_context, error)) { extension->OnDetach(); return false; }
    }
    catch (...) { extension->OnDetach(); throw; }
    if (commands) for (auto name : commands->Commands()) scope.commands.emplace(name, commands);
    scope.instances.push_back(std::move(extension));
    return true;
}
void CExtensionRuntime::Detach(SScope& scope) noexcept
{
    scope.view.clear();
    scope.commands.clear();
    for (auto it = scope.instances.rbegin(); it != scope.instances.rend(); ++it) if (*it) (*it)->OnDetach();
    scope.instances.clear();
}
bool CExtensionRuntime::Init(IPlayerDirectory& players, IChatDelivery& delivery, IPartyDelivery& parties, std::string& error)
{
    error.clear();
    if (m_context) { error = "Extensions already initialized"; return false; }
    if (!OrderFactories(error)) return false;
    CDispatchGuard guard(*this);
    m_context = std::make_unique<CExtensionContext>(CExtensionContext{players, delivery, parties, nullptr, this});
    try
    {
        for (const auto& factory : m_factories)
        {
            if (factory.scope != EExtensionScope::Server) continue;
            auto extension = factory.create({}, {});
            if (!extension || !MatchesFactory(factory, *extension) || !Attach(m_globals, std::move(extension), error))
            { if (error.empty()) error = "Invalid global extension factory"; ShutDown(); return false; }
            m_globals.view.push_back(m_globals.instances.back().get());
            m_context->chat = Globals().Find<IChatService>();
        }
    }
    catch (const std::exception& exception) { error = exception.what(); ShutDown(); return false; }
    catch (...) { error = "Extension initialization failed"; ShutDown(); return false; }
    if (m_pending_shutdown) { error = "Extension initialization was cancelled"; return false; }
    return true;
}
void CExtensionRuntime::ShutDown() noexcept
{
    if (m_dispatch_depth) { m_pending_shutdown = true; return; }
    CDispatchGuard guard(*this);
    for (auto& [id, scope] : m_worlds) Detach(scope);
    m_worlds.clear(); Detach(m_globals); m_context.reset();
}
void CExtensionRuntime::Tick(float dt)
{
    if (m_dispatch_depth) throw std::logic_error("Cannot recursively tick extension runtime");
    CDispatchGuard guard(*this);
    for (const auto& extension : m_globals.instances)
        if (auto* tick = dynamic_cast<ITickExtension*>(extension.get())) tick->Tick(dt);
    for (const auto& [id, scope] : m_worlds)
        for (const auto& extension : scope.instances)
            if (auto* tick = dynamic_cast<ITickExtension*>(extension.get())) tick->Tick(dt);
    RefreshAllWorlds();
}
CExtensionView CExtensionRuntime::AttachWorld(CGameWorld& world, const SWorldExtensionConfig& config)
{
    if (m_dispatch_depth) throw std::logic_error("Extension topology changes require a dispatch boundary");
    CDispatchGuard guard(*this);
    if (!m_context) throw std::logic_error("Attach world before extension runtime initialization");
    CDispatchGuard dispatch(*this);
    const SWorldHandle handle{world.GetId(), world.GetInstanceGeneration()};
    auto found = m_worlds.find(handle.id);
    std::vector<std::unique_ptr<CExtension>> desired;
    for (const auto& factory : m_factories)
        if (factory.scope == EExtensionScope::World)
        {
            auto extension = factory.create(handle, config);
            if (extension && !MatchesFactory(factory, *extension)) throw std::logic_error("World extension factory descriptor mismatch");
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
    struct SDetachStaged { SScope& scope; ~SDetachStaged() { CExtensionRuntime::Detach(scope); } } cleanup{staged};
    std::string error;
    const bool reuse = found != m_worlds.end() && found->second.world == handle;
    std::vector<CExtension*> ordered;
    ordered.reserve(desired.size() + m_globals.view.size());
    // 先验证新拓扑，再暂存新增实例；保留实例的状态和命令处理器。
    for (const auto& extension : desired)
        for (auto dependency : extension->Descriptor().dependencies)
            if (std::none_of(desired.begin(), desired.end(), [&](const auto& item) { return item->Descriptor().key == dependency; }) &&
                std::none_of(m_globals.view.begin(), m_globals.view.end(), [&](const auto* item) { return item->Descriptor().key == dependency; }))
                throw std::runtime_error("Missing world extension dependency");
    if (reuse)
        for (const auto& extension : found->second.instances)
            if (std::any_of(desired.begin(), desired.end(), [&](const auto& item) { return item->Descriptor().key == extension->Descriptor().key; }))
            {
                staged.view.push_back(extension.get());
                if (auto* commands = dynamic_cast<IPlayerCommandExtension*>(extension.get()))
                    for (auto name : commands->Commands()) staged.commands.emplace(name, commands);
            }
    for (auto& candidate : desired)
    {
        const auto kept = std::find_if(staged.view.begin(), staged.view.end(), [&](const auto* item) { return item->Descriptor().key == candidate->Descriptor().key; });
        if (kept != staged.view.end()) ordered.push_back(*kept);
        else
        {
            if (!Attach(staged, std::move(candidate), error)) throw std::runtime_error(error);
            auto* added = staged.instances.back().get(); staged.view.push_back(added); ordered.push_back(added);
        }
    }
    SScope replacement; replacement.world = handle; replacement.config = std::move(staged.config);
    replacement.instances.reserve(ordered.size());
    replacement.view = ordered;
    replacement.view.insert(replacement.view.end(), m_globals.view.begin(), m_globals.view.end());
    replacement.commands = std::move(staged.commands);
    if (found == m_worlds.end()) found = m_worlds.emplace(handle.id, SScope{}).first;
    for (auto* instance : ordered)
    {
        auto take = [&](auto& values) {
            const auto item = std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.get() == instance; });
            if (item == values.end()) return false;
            replacement.instances.push_back(std::move(*item)); return true;
        };
        if (!take(staged.instances)) take(found->second.instances);
    }
    Detach(found->second);
    found->second = std::move(replacement);
    return {this, handle};
}
void CExtensionRuntime::DetachWorld(SWorldHandle world) noexcept
{
    if (m_dispatch_depth)
    {
        if (std::find(m_pending_detaches.begin(), m_pending_detaches.end(), world) == m_pending_detaches.end())
            m_pending_detaches.push_back(world);
        return;
    }
    CDispatchGuard guard(*this);
    const auto found = m_worlds.find(world.id);
    if (found == m_worlds.end() || found->second.world != world) return;
    Detach(found->second); m_worlds.erase(found);
}
void CExtensionRuntime::RefreshWorld(CGameWorld& world)
{
    CDispatchGuard guard(*this);
    const auto found = m_worlds.find(world.GetId());
    if (found == m_worlds.end() || found->second.world.generation != world.GetInstanceGeneration()) return;
    for (const auto& extension : found->second.instances)
        if (auto* maintained = dynamic_cast<IMaintainedExtension*>(extension.get())) maintained->Refresh();
}
void CExtensionRuntime::RefreshAllWorlds()
{
    CDispatchGuard guard(*this);
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

IPartyService* CExtensionRuntime::Parties() const
{
    for (auto* instance : Instances({}))
        if (auto* party = dynamic_cast<IPartyService*>(instance)) return party;
    return nullptr;
}
bool CExtensionRuntime::HandleCommand(CPlayer& player, const std::vector<std::string>& args)
{
    CDispatchGuard guard(*this);
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
bool CExtensionRuntime::PublishPlayerState(CPlayer& player)
{
    CDispatchGuard guard(*this);
    const auto* world = player.GetWorld();
    for (auto* instance : Instances(world ? SWorldHandle{world->GetId(), world->GetInstanceGeneration()} : SWorldHandle{}))
            if (auto* publisher = dynamic_cast<IPlayerStateExtension*>(instance); publisher && !publisher->PublishPlayerState(player))
                return false;
    return true;
}

std::vector<SClientFeature> CExtensionRuntime::ClientFeatures(const CPlayer& player) const
{
    std::vector<SClientFeature> result;
    const auto* world = player.GetWorld();
    for (auto* instance : Instances(world ? SWorldHandle{world->GetId(), world->GetInstanceGeneration()} : SWorldHandle{}))
        if (const auto* feature = dynamic_cast<const IClientFeatureExtension*>(instance)) feature->DescribeFeatures(player, result);
    for (auto& feature : result)
        if (world) for (const auto& rule : world->ExtensionConfig().features)
            if (rule.key == feature.key)
            {
                feature.visible = rule.visible;
                const auto previous = feature.access; feature.access &= rule.access;
                if (feature.access != previous) feature.reason = "world_policy";
            }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
    return result;
}
bool CExtensionRuntime::CanUseFeature(const CPlayer& player, std::string_view key, std::uint8_t access) const
{
    for (const auto& feature : ClientFeatures(player)) if (feature.key == key) return (feature.access & access) == access;
    return false;
}
bool CExtensionRuntime::HasClientFeature(SWorldHandle world, std::string_view key) const
{
    for (const auto* instance : Instances(world))
        if (const auto* feature = dynamic_cast<const IClientFeatureExtension*>(instance))
            if (std::find(feature->FeatureKeys().begin(), feature->FeatureKeys().end(), key) != feature->FeatureKeys().end()) return true;
    return false;
}

void CExtensionRuntime::BeginRestore()
{
    CDispatchGuard guard(*this);
    for (const auto& instance : m_globals.instances)
        if (auto* boundary = dynamic_cast<IRestoreBoundaryExtension*>(instance.get())) boundary->BeginRestore();
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
        entry.Field("required", instance->Descriptor().required_state);
        auto data = MakeJsonObject(); CSnapshotWriter fields(data.get()); persistent->CaptureSnapshot(fields);
        entry.Node("data", data.release()); AppendJson(values.get(), value.release());
    }
    writer.Node(field, values.release());
}

bool CExtensionRuntime::RestoreScope(SScope& scope, const CSnapshotReader& reader, std::string_view field, std::string& error)
{
    const auto* values = reader.Node(field);
    if (!json_is_array(values) || json_array_size(values) > 1024)
    { error = "Invalid persistent extension state list"; return false; }
    std::vector<std::string> keys;
    for (size_t i = 0; i < json_array_size(values); ++i)
    {
        CSnapshotReader saved(json_array_get(values, i)); const auto key = saved.String("key");
        const auto target = std::find_if(scope.instances.begin(), scope.instances.end(), [&](const auto& extension) {
            return extension->Descriptor().key == key;
        });
        if (key.empty() || std::find(keys.begin(), keys.end(), key) != keys.end())
        { error = "Empty or duplicate persistent extension: " + key; return false; }
        keys.push_back(key);
        if (saved.Has("required") && !json_is_boolean(saved.Node("required")))
        { error = "Invalid extension persistence requirement: " + key; return false; }
        const auto version = saved.UInt32("version");
        CSnapshotReader data(saved.Node("data"));
        if (!data.IsValid() || !version)
        { error = "Invalid persistent extension data/version: " + key; return false; }
        if (target == scope.instances.end())
        {
            if (saved.Bool("required", true)) { error = "Unknown required extension: " + key; return false; }
            LOG_WARN("extensions", "Skipped unavailable optional extension state: " + key);
            continue;
        }
        auto* persistent = dynamic_cast<IPersistentExtension*>(target->get());
        if (!persistent) { error = "Extension no longer supports persistence: " + key; return false; }
        if (version > (*target)->Descriptor().state_version)
        {
            if ((*target)->Descriptor().required_state || saved.Bool("required", false) || !persistent->RestoreDefaultState(error))
            { if (error.empty()) error = "Unsupported required extension version: " + key; return false; }
            LOG_WARN("extensions", "Reset optional extension with newer state version: " + key);
            continue;
        }
        if (!persistent->RestoreSnapshot(data, version, error)) return false;
    }
    for (const auto& extension : scope.instances)
    {
        auto* persistent = dynamic_cast<IPersistentExtension*>(extension.get());
        if (!persistent || std::find(keys.begin(), keys.end(), extension->Descriptor().key) != keys.end()) continue;
        if (extension->Descriptor().required_state)
        { error = "Missing required extension state: " + std::string(extension->Descriptor().key); return false; }
        if (!persistent->RestoreDefaultState(error)) return false;
        LOG_WARN("extensions", "Initialized missing optional extension state: " + std::string(extension->Descriptor().key));
    }
    return true;
}

void CExtensionRuntime::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CDispatchGuard guard(const_cast<CExtensionRuntime&>(*this));
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
    CDispatchGuard guard(*this);
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
        SWorldExtensionConfig config{entry.Bool("squads"), entry.Bool("weather_visuals"), entry.Bool("weather_spawns")};
        // 功能策略只由世界快照持久化，扩展作用域不维护第二份权威配置。
        config.features = (*world)->ExtensionConfig().features;
        if ((*world)->ExtensionConfig() != config)
        { error = "World and extension binding configurations disagree"; return false; }
        const auto binding = m_worlds.find(id);
        if (binding == m_worlds.end() || binding->second.world.generation != (*world)->GetInstanceGeneration())
        { error = "Missing restored extension world binding"; return false; }
        auto& scope = binding->second;
        if (!RestoreScope(scope, entry, "instances", error)) return false;
    }
    return true;
}
bool CExtensionRuntime::RestoreLegacySquads(CGameWorld& world, const CSnapshotReader& data, std::string& error)
{
    auto* squads = world.Extensions().Find<CSquadExtension>();
    return !squads || squads->RestoreSnapshot(data, 1, error);
}
