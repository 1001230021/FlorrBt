// Privileged observer service, separate from gameplay player connections.
#include "dev_debug_service.h"
#include "../../Engine/logger.h"
#include "../../Shared/dev_debug_protocol.h"
#include "../../Shared/tools.h"
#include "../../Shared/utf8.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/mob.h"
#include "../Game/entities/drop.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/systems/snapshot_service.h"
#include "../Module/network_module.h"
#include "../Persistence/account_store.h"
#include "../server.h"
#include <jansson.h>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>

namespace
{
struct JsonDelete { void operator()(json_t* value) const { if (value) json_decref(value); } };
using Json = std::unique_ptr<json_t, JsonDelete>;
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
json_t* Num(double value) { return json_real(std::isfinite(value) ? value : 0.0); }
json_t* Str(const std::string& value)
{
    if (auto* text = json_stringn(value.data(), value.size())) return text;
    return json_string("<invalid UTF-8>");
}
void Set(json_t* object, const char* key, json_t* value) { json_object_set_new(object, key, value ? value : json_null()); }
std::string ReadDebugText(json_t* object, const char* key, std::size_t limit = 256)
{
    json_t* value = json_object_get(object, key);
    if (!json_is_string(value) || json_string_length(value) > limit) throw std::runtime_error(std::string("Invalid ") + key);
    return { json_string_value(value), json_string_length(value) };
}
std::int64_t Int(json_t* object, const char* key, std::int64_t low, std::int64_t high)
{
    json_t* value = json_object_get(object, key);
    if (!json_is_integer(value)) throw std::runtime_error(std::string("Invalid ") + key);
    const auto number = json_integer_value(value);
    if (number < low || number > high) throw std::runtime_error(std::string("Out of range: ") + key);
    return number;
}
float Float(json_t* object, const char* key, double low, double high)
{
    json_t* value = json_object_get(object, key);
    const double number = json_number_value(value);
    if (!json_is_number(value) || !std::isfinite(number) || number < low || number > high)
        throw std::runtime_error(std::string("Invalid ") + key);
    return static_cast<float>(number);
}
std::uint64_t Generation(json_t* object, const char* key = "generation")
{
    const auto text = ReadDebugText(object, key, 20);
    std::uint64_t number = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (error != std::errc{} || end != text.data() + text.size() || !number) throw std::runtime_error("Invalid generation");
    return number;
}
json_t* Position(florrbt::Vector2f p)
{
    auto* out = json_object(); Set(out, "x", Num(p.x)); Set(out, "y", Num(p.y)); return out;
}
void ApplyDebugKill(CEntity& entity)
{
    double pool = std::max(0.0, static_cast<double>(entity.Health()));
    if (const auto* flower = dynamic_cast<const CFlower*>(&entity)) pool += std::max(0.0, static_cast<double>(flower->GetShield()));
    // Do not inject FLT_MAX into damage sharing/feedback arithmetic. Normal
    // invulnerability and death/drop handling still apply to this damage.
    const float damage = static_cast<float>(std::min(static_cast<double>(std::numeric_limits<float>::max()) / 8.0,
                                                    std::max(1.0, pool * 2.0 + 1.0)));
    entity.TakeDamage(damage, nullptr, EDamageType::Poison);
}
void HealDebugEntity(CEntity& entity)
{
    float maximum = std::max(1.f, entity.Health());
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        if (const auto* stats = mob->GetFinalStats()) maximum = stats->max_health;
    }
    else if (const auto* petal = dynamic_cast<const CPetal*>(&entity)) maximum = petal->m_final_petal_stats.health;
    if (std::isfinite(maximum) && maximum > 0.f) entity.Health() = maximum;
}
json_t* Items(const std::vector<SInventoryItem>& items)
{
    auto* out = json_array();
    for (const auto& item : items)
    {
        auto* entry = json_object();
        Set(entry, "type", json_integer(item.petal_type)); Set(entry, "rarity", json_integer(item.rarity));
        Set(entry, "count", json_integer(item.count)); json_array_append_new(out, entry);
    }
    return out;
}
json_t* Stats(const SMobStats* stats)
{
    auto* out = json_object();
    if (!stats) return out;
    Set(out, "maxHealth", Num(stats->max_health)); Set(out, "damage", Num(stats->damage));
    Set(out, "armor", Num(stats->armor)); Set(out, "radius", Num(stats->radius)); Set(out, "mass", Num(stats->mass));
    Set(out, "horizon", Num(stats->horizon)); Set(out, "maxVelocity", Num(stats->max_velocity));
    Set(out, "acceleration", Num(stats->acceleration)); Set(out, "turnSpeed", Num(stats->turn_speed));
    Set(out, "absorbRange", Num(stats->max_absorb_range)); Set(out, "extraHits", json_integer(stats->extra_hit_num));
    return out;
}
json_t* EntityJson(const CEntity& entity, const CSnapshotService& snapshots, bool detailed = false)
{
    const auto snap = snapshots.BuildEntitySnap(entity, entity);
    auto* out = json_object();
    Set(out, "entityId", json_integer(entity.Id())); Set(out, "generation", Str(std::to_string(entity.Generation())));
    Set(out, "entityType", json_integer(snap.entity_type)); Set(out, "family", json_integer(static_cast<int>(entity.GetEntityType())));
    Set(out, "pos", Position(entity.Pos())); Set(out, "radius", Num(entity.Radius()));
    Set(out, "angle", Num(entity.FacingAngle())); Set(out, "hpPercent", Num(snap.hp_percent));
    Set(out, "shieldPercent", Num(snap.shield_percent)); Set(out, "rarity", json_integer(snap.rarity));
    Set(out, "flags", json_integer(snap.flags & ~static_cast<std::uint16_t>(ServerEntityFlag::Owner)));
    Set(out, "name", Str(snap.name)); Set(out, "team", json_integer(entity.Team()));
    Set(out, "health", Num(entity.Health())); Set(out, "visible", json_boolean(entity.IsVisible()));
    auto* states = json_array();
    for (const auto& state : snap.states)
    {
        auto* entry = json_object(); Set(entry, "type", json_integer(static_cast<int>(state.type)));
        Set(entry, "rarity", json_integer(state.rarity)); Set(entry, "value", json_integer(state.value));
        json_array_append_new(states, entry);
    }
    Set(out, "states", states);
    if (const auto* flower = dynamic_cast<const CFlower*>(&entity))
    {
        auto* slots = json_array();
        for (const auto& slot : CSnapshotService::BuildPrimarySlots(*flower))
        {
            auto* entry = json_object(); Set(entry, "petalType", json_integer(slot.petal_type));
            Set(entry, "rarity", json_integer(slot.rarity));
            auto* copies = json_array();
            for (const auto& copy : slot.copies)
            {
                auto* c = json_object(); Set(c, "state", json_integer(static_cast<int>(copy.state)));
                Set(c, "loading", json_boolean(copy.state == SOwnerPetalSlot::ECopyState::Loading));
                Set(c, "progress", Num(copy.progress / 255.0)); json_array_append_new(copies, c);
            }
            Set(entry, "copies", copies); json_array_append_new(slots, entry);
        }
        Set(out, "primarySlots", slots);
    }
    if (detailed)
    {
        Set(out, "cppType", Str(typeid(entity).name())); Set(out, "mass", Num(entity.Mass()));
        Set(out, "dead", json_boolean(entity.IsDead())); Set(out, "canCollide", json_boolean(entity.CanCollide()));
        Set(out, "tickMode", json_integer(static_cast<int>(entity.TickMode())));
        if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
        {
            Set(out, "baseStats", Stats(mob->GetBaseStats())); Set(out, "stats", Stats(mob->GetFinalStats()));
            Set(out, "velocity", Position(mob->m_vel)); Set(out, "naturalSpawn", json_boolean(mob->IsNaturallySpawned()));
            Set(out, "inactiveSeconds", Num(mob->InactiveSeconds()));
        }
        if (const auto* projectile = dynamic_cast<const CProjectile*>(&entity))
            if (const auto* owner = projectile->GetOwner())
            {
                Set(out, "ownerId", json_integer(owner->Id()));
                Set(out, "ownerGeneration", Str(std::to_string(owner->Generation())));
            }
        if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
        {
            Set(out, "petalType", json_integer(static_cast<int>(petal->GetPetalType())));
            Set(out, "slotIndex", json_integer(petal->m_slot_index)); Set(out, "copyIndex", json_integer(petal->m_copy_index));
            Set(out, "petalDamage", Num(petal->m_final_petal_stats.damage));
            Set(out, "petalMaxHealth", Num(petal->m_final_petal_stats.health));
        }
        if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
        {
            Set(out, "petalType", json_integer(static_cast<int>(drop->GetType())));
            Set(out, "count", json_integer(drop->GetStackNum())); Set(out, "lifetime", Num(drop->GetLifetime()));
            Set(out, "pickupOwner", json_integer(drop->GetOwnerId()));
        }
    }
    return out;
}
} // namespace

bool EditDebugInventory(CServer& server, CPlayer& player, const std::string& operation,
                        std::uint8_t type, std::uint8_t rarity, std::uint32_t count, std::string& result)
{
    if (!player.IsAuthenticated() || !FindPetalPrototype(static_cast<EPetalType>(type)) ||
        !IsKnownRarity(static_cast<ERarity>(rarity)) || count > max_inventory_item_count ||
        (operation != "add" && operation != "remove" && operation != "set"))
    { result = "Invalid inventory operation/type/rarity/count"; return false; }
    const std::string account = player.GetAccountName();
    int level = 0; std::int64_t exp = 0;
    if (account.empty() || !CAccountDataStore::GetProgress(account, level, exp))
    { result = "Account is unavailable"; return false; }
    CAccountDataStore::CSaveBatch batch;
    std::uint32_t current = 0;
    for (const auto& item : CAccountDataStore::GetInventory(account))
        if (item.petal_type == type && item.rarity == rarity) current = item.count;
    const std::uint64_t target = operation == "set" ? count : operation == "add"
        ? static_cast<std::uint64_t>(current) + count : current >= count ? current - count : UINT64_MAX;
    if (target > max_inventory_item_count) { result = "Insufficient items or inventory count overflow"; return false; }
    if (target < current && !CAccountDataStore::TakeItem(account, type, rarity, current - static_cast<std::uint32_t>(target)))
    { result = "Inventory changed; retry"; return false; }
    if (target > current) CAccountDataStore::AddItem(account, type, rarity, static_cast<std::uint32_t>(target - current));
    if (auto* network = server.GetNetworkModule()) network->QueueInventoryUpdate(player);
    result = "Backpack count " + std::to_string(current) + " -> " + std::to_string(target);
    return true;
}

struct CDevDebugService::Impl
{
    struct Grant
    {
        std::uint32_t player = 0;
        std::string account, remote, password;
        double expires = 0;
    };
    struct LogLine { std::uint64_t sequence; std::string text; };
    struct LogHub { std::mutex mutex; std::deque<LogLine> lines; std::uint64_t sequence = 0; };
    struct Session
    {
        sf::TcpSocket socket;
        std::string remote;
        std::vector<std::uint8_t> receive, send;
        std::size_t send_offset = 0;
        bool authorized = false, closed = false;
        Grant grant;
        double deadline = Now() + 5, last_snapshot = 0, last_status = 0, last_command = 0;
        dev_debug::RateLimit rate;
        std::uint32_t world = 0;
        std::uint64_t world_generation = 0, last_log = 0;
        florrbt::Vector2f center{};
        float radius = 1000;
        std::size_t terrain_layer = 0, terrain_row = 0, terrain_col = 0;
        bool terrain_pending = false;
    };

    CServer& server;
    CSnapshotService snapshots;
    std::unordered_map<std::string, Grant> tickets;
    std::vector<std::unique_ptr<Session>> sessions;
    std::shared_ptr<LogHub> logs = std::make_shared<LogHub>();
    std::size_t sink = 0;

    explicit Impl(CServer& value) : server(value)
    {
        sink = CLogger::AddSink([hub = logs](const std::string& sender, ELogPriority priority, const std::string& text) {
            std::string line = CLogger::FormatLine(sender, priority, std::string(TruncateUtf8(text, 4096)));
            std::lock_guard<std::mutex> lock(hub->mutex);
            hub->lines.push_back({ ++hub->sequence, std::move(line) });
            while (hub->lines.size() > 512) hub->lines.pop_front();
        });
    }
    ~Impl() { CLogger::RemoveSink(sink); }

    bool Valid(const Grant& grant) const
    {
        const auto* network = server.GetNetworkModule();
        const auto* player = network ? network->FindPlayerById(grant.player) : nullptr;
        return player && player->IsConnected() && player->IsAuthenticated() && player->IsRconAuthorized() &&
            player->GetAccountName() == grant.account && player->GetRemoteAddress() == grant.remote &&
            !game_config::rcon_password.empty() && grant.password == game_config::rcon_password;
    }
    bool Queue(Session& session, json_t* object)
    {
        Json owned(object);
        char* encoded = json_dumps(object, JSON_COMPACT | JSON_ENSURE_ASCII);
        if (!encoded) { session.closed = true; return false; }
        auto frame = dev_debug::PackFrame(encoded);
        std::free(encoded);
        if (frame.empty() || session.send.size() - session.send_offset + frame.size() > dev_debug::backlog_limit)
        { session.closed = true; return false; }
        if (session.send_offset)
        {
            session.send.erase(session.send.begin(), session.send.begin() + session.send_offset);
            session.send_offset = 0;
        }
        session.send.insert(session.send.end(), frame.begin(), frame.end());
        return true;
    }
    void Reply(Session& session, json_t* request, bool ok, const std::string& message, json_t* data = nullptr)
    {
        auto* out = json_object(); Set(out, "kind", Str("result")); Set(out, "ok", json_boolean(ok));
        json_t* id = json_object_get(request, "request");
        Set(out, "request", json_integer(json_is_integer(id) ? json_integer_value(id) : 0));
        Set(out, "message", Str(message)); if (data) Set(out, "data", data);
        Queue(session, out);
    }
    bool Flush(Session& session)
    {
        if (session.send_offset >= session.send.size()) { session.send.clear(); session.send_offset = 0; return true; }
        std::size_t sent = 0;
        const auto status = session.socket.send(session.send.data() + session.send_offset,
                                               session.send.size() - session.send_offset, sent);
        session.send_offset += sent;
        return status != sf::Socket::Status::Disconnected && status != sf::Socket::Status::Error;
    }
    CGameWorld* World(const Session& session) const
    {
        auto* world = server.FindWorldById(session.world);
        return world && world->GetInstanceGeneration() == session.world_generation ? world : nullptr;
    }
    CGameWorld& RequestWorld(Session& session, json_t* request)
    {
        const auto id = static_cast<std::uint32_t>(Int(request, "world", 0, UINT32_MAX));
        auto* world = server.FindWorldById(id);
        if (!world || world->GetInstanceGeneration() != Generation(request, "worldGeneration"))
            throw std::runtime_error("World was removed/replaced; refresh the world list");
        if (id != session.world || world->GetInstanceGeneration() != session.world_generation)
            throw std::runtime_error("Switch to the target world first");
        return *world;
    }
    florrbt::Vector2f RequestPosition(json_t* request, const CGameWorld& world)
    {
        florrbt::Vector2f pos{ Float(request, "x", 0, 10000000), Float(request, "y", 0, 10000000) };
        const auto* map = world.GetMap();
        if (map && (pos.x > static_cast<double>(map->width) * map->tile_width ||
                    pos.y > static_cast<double>(map->height) * map->tile_height))
            throw std::runtime_error("Position is outside the map");
        return pos;
    }
    CPlayer& RequestPlayer(json_t* request)
    {
        auto* network = server.GetNetworkModule();
        auto* player = network ? network->FindPlayerById(static_cast<std::uint32_t>(Int(request, "player", 1, UINT32_MAX))) : nullptr;
        if (!player || !player->IsAuthenticated() || player->GetAccountName() != ReadDebugText(request, "account", 256))
            throw std::runtime_error("Player session changed; refresh the player list");
        return *player;
    }

    void SendCatalog(Session& session);
    void SendStatus(Session& session);
    json_t* PlayerJson(const CPlayer& player, bool detailed);
    void SendSnapshot(Session& session);
    void SendTerrain(Session& session);
    void SendLogs(Session& session);
    void Handle(Session& session, json_t* request);
    void EntityAction(Session& session, json_t* request);
    void PlayerAction(Session& session, json_t* request);
};

void CDevDebugService::Impl::SendCatalog(Session& session)
{
    auto* out = json_object(); Set(out, "kind", Str("catalog"));
    auto* types = json_array();
    for (const auto& [type, proto] : GetMobRegistry())
    {
        if (!proto || type == EMobType::None || type == EMobType::PlayerFlower) continue;
        auto* entry = json_object(); Set(entry, "kind", Str("mob")); Set(entry, "id", json_integer(static_cast<int>(type)));
        Set(entry, "name", Str(proto->m_name));
        Set(entry, "visualType", json_integer(static_cast<int>(proto->m_visual_proxy_type == EMobType::None ? type : proto->m_visual_proxy_type)));
        json_array_append_new(types, entry);
    }
    for (const auto& [type, proto] : GetPetalRegistry())
    {
        if (!proto || type == EPetalType::None) continue;
        auto* entry = json_object(); Set(entry, "kind", Str("petal")); Set(entry, "id", json_integer(static_cast<int>(type)));
        Set(entry, "visualType", json_integer(static_cast<int>(type))); Set(entry, "name", Str(proto->m_name));
        json_array_append_new(types, entry);
    }
    Set(out, "types", types);
    auto* rarities = json_array();
    for (int r = 1; r < static_cast<int>(rarity_names.size()); ++r)
    {
        auto* entry = json_object(); Set(entry, "id", json_integer(r)); Set(entry, "name", Str(std::string(rarity_names[r])));
        json_array_append_new(rarities, entry);
    }
    Set(out, "rarities", rarities);
    auto* commands = json_array();
    for (const auto& name : server.GetConsole().CommandNames())
    {
        auto* entry = json_object(); Set(entry, "name", Str(name));
        Set(entry, "usage", Str(std::string(server.GetConsole().CommandUsage(name).value_or(name))));
        json_array_append_new(commands, entry);
    }
    Set(out, "commands", commands); Set(out, "selectionLimit", json_integer(dev_debug::selection_limit));
    Queue(session, out);
}

json_t* CDevDebugService::Impl::PlayerJson(const CPlayer& player, bool detailed)
{
    auto* out = json_object(); Set(out, "id", json_integer(player.GetId()));
    Set(out, "name", Str(player.GetName())); Set(out, "account", Str(player.GetAccountName()));
    Set(out, "connected", json_boolean(player.IsConnected())); Set(out, "ip", Str(player.GetRemoteAddress()));
    Set(out, "rcon", json_boolean(player.IsRconAuthorized())); Set(out, "muteSeconds", Num(player.GetMuteTimer()));
    Set(out, "timeoutSeconds", Num(player.GetTimeoutLeft())); Set(out, "talentPoints", json_integer(player.GetTalentPoints()));
    int level = 1; std::int64_t exp = 0;
    CAccountDataStore::GetProgress(player.GetAccountName(), level, exp);
    Set(out, "level", json_integer(level)); Set(out, "exp", Str(std::to_string(exp)));
    if (const auto* entity = player.GetEntity())
    {
        Set(out, "entityId", json_integer(entity->Id())); Set(out, "generation", Str(std::to_string(entity->Generation())));
        Set(out, "pos", Position(entity->Pos())); Set(out, "health", Num(entity->Health())); Set(out, "team", json_integer(entity->Team()));
        if (const auto* world = entity->GameWorld())
        {
            Set(out, "world", json_integer(world->GetId())); Set(out, "worldGeneration", Str(std::to_string(world->GetInstanceGeneration())));
            Set(out, "map", Str(world->GetMapName()));
            Set(out, "mapPath", Str(world->GetMapPath()));
        }
        if (detailed) Set(out, "entity", EntityJson(*entity, snapshots, true));
    }
    if (detailed)
    {
        Set(out, "inventory", Items(CAccountDataStore::GetInventory(player.GetAccountName())));
        Set(out, "slots", Items(CAccountDataStore::GetSlots(player.GetAccountName())));
        Set(out, "secondarySlots", Items(CAccountDataStore::GetSecondarySlots(player.GetAccountName())));
        Set(out, "secondChanceCooldown", Num(player.GetSecondChanceCooldown()));
        Set(out, "reportDisabled", json_boolean(player.IsReportDisabled()));
        auto* talents = json_array();
        for (const auto& talent : CAccountDataStore::GetTalents(player.GetAccountName()))
        {
            auto* entry = json_object(); Set(entry, "id", json_integer(static_cast<int>(talent.id)));
            Set(entry, "rarity", json_integer(static_cast<int>(talent.rarity))); Set(entry, "rank", json_integer(talent.rank));
            json_array_append_new(talents, entry);
        }
        Set(out, "talents", talents);
        auto* codex = json_array();
        for (const auto& [key, count] : player.GetCodex())
        {
            auto* entry = json_object(); Set(entry, "type", json_integer(key >> 8));
            Set(entry, "rarity", json_integer(key & 255)); Set(entry, "count", json_integer(count));
            json_array_append_new(codex, entry);
        }
        Set(out, "codex", codex);
        auto* checkpoints = json_array();
        for (const auto& checkpoint : player.m_cp_stack)
        {
            auto* entry = json_object(); Set(entry, "id", json_integer(checkpoint.checkpoint_id));
            Set(entry, "level", json_integer(checkpoint.level)); Set(entry, "count", json_integer(checkpoint.count));
            json_array_append_new(checkpoints, entry);
        }
        Set(out, "checkpoints", checkpoints);
    }
    return out;
}

void CDevDebugService::Impl::SendStatus(Session& session)
{
    auto* out = json_object(); Set(out, "kind", Str("status"));
    auto* worlds = json_array();
    for (const auto* world : server.GetWorlds())
    {
        if (!world) continue;
        auto* entry = json_object(); Set(entry, "id", json_integer(world->GetId()));
        Set(entry, "generation", Str(std::to_string(world->GetInstanceGeneration())));
        Set(entry, "map", Str(world->GetMapName()));
        Set(entry, "mapPath", Str(world->GetMapPath()));
        Set(entry, "controller", Str(world->GetController() ? std::string(world->GetController()->SnapshotKey()) : ""));
        Set(entry, "entities", json_integer(world->GetEntityCount())); Set(entry, "players", json_integer(world->GetPlayerCount()));
        if (const auto* map = world->GetMap())
        {
            Set(entry, "width", Num(static_cast<double>(map->width) * map->tile_width));
            Set(entry, "height", Num(static_cast<double>(map->height) * map->tile_height));
        }
        json_array_append_new(worlds, entry);
    }
    Set(out, "worlds", worlds);
    auto* players = json_array();
    if (const auto* network = server.GetNetworkModule())
        for (const auto& player : network->GetPlayers())
            if (player && player->IsAuthenticated()) json_array_append_new(players, PlayerJson(*player, false));
    Set(out, "players", players);
    Set(out, "tick", json_integer(server.GetTick()));
    Set(out, "tps", Num(server.GetPerformanceTelemetry().actual_tps));
    Set(out, "tickMs", Num(server.GetPerformanceTelemetry().last_tick_ms));
    Set(out, "world", json_integer(session.world)); Set(out, "worldGeneration", Str(std::to_string(session.world_generation)));
    Queue(session, out);
}

void CDevDebugService::Impl::SendSnapshot(Session& session)
{
    auto* world = World(session);
    if (!world) return;
    struct Candidate { CEntity* entity; float distance; };
    std::vector<Candidate> candidates;
    const auto collect = [&](CEntity* entity) {
        if (!entity || entity->IsMarkedForDes()) return;
        const float distance = DistanceSq(entity->Pos(), session.center);
        const float reach = session.radius + std::max(0.f, entity->Radius());
        if (distance <= reach * reach) candidates.push_back({ entity, distance });
    };
    // Include hidden/passive objects that need not inhabit the gameplay
    // spatial index. Work is bounded by actual population, not camera area:
    // zooming out must never walk millions of empty spatial cells.
    world->ForEachEntity(collect);
    const auto closer = [](const Candidate& a, const Candidate& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.entity->Id() < b.entity->Id();
    };
    const auto total = candidates.size();
    if (candidates.size() > dev_debug::entity_limit)
    {
        std::nth_element(candidates.begin(), candidates.begin() + dev_debug::entity_limit, candidates.end(), closer);
        candidates.resize(dev_debug::entity_limit);
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.entity->Radius() != b.entity->Radius() ? a.entity->Radius() > b.entity->Radius() : a.entity->Id() < b.entity->Id();
    });
    auto* out = json_object(); Set(out, "kind", Str("snapshot"));
    Set(out, "world", json_integer(world->GetId())); Set(out, "worldGeneration", Str(std::to_string(world->GetInstanceGeneration())));
    Set(out, "map", Str(world->GetMapName())); Set(out, "tick", json_integer(server.GetTick()));
    Set(out, "mapPath", Str(world->GetMapPath()));
    Set(out, "total", json_integer(total)); Set(out, "truncated", json_boolean(total > candidates.size()));
    auto* entities = json_array();
    for (const auto& candidate : candidates) json_array_append_new(entities, EntityJson(*candidate.entity, snapshots));
    Set(out, "entities", entities); Queue(session, out);
}

void CDevDebugService::Impl::SendTerrain(Session& session)
{
    const auto* world = World(session); const auto* map = world ? world->GetMap() : nullptr;
    if (!session.terrain_pending || !map) return;
    while (session.terrain_layer < map->tile_layers.size())
    {
        const auto& layer = map->tile_layers[session.terrain_layer];
        if (layer.width <= 0 || layer.height <= 0 || session.terrain_row >= static_cast<std::size_t>(layer.height))
        { ++session.terrain_layer; session.terrain_row = session.terrain_col = 0; continue; }
        auto* out = json_object(); Set(out, "kind", Str("terrain"));
        Set(out, "world", json_integer(world->GetId())); Set(out, "worldGeneration", Str(std::to_string(world->GetInstanceGeneration())));
        Set(out, "layer", json_integer(session.terrain_layer)); Set(out, "row", json_integer(session.terrain_row));
        Set(out, "col", json_integer(session.terrain_col));
        auto* gids = json_array();
        const std::size_t count = std::min(std::size_t{256}, static_cast<std::size_t>(layer.width) - session.terrain_col);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t index = session.terrain_row * layer.width + session.terrain_col + i;
            json_array_append_new(gids, json_integer(index < layer.tiles.size() ? static_cast<std::uint32_t>(layer.tiles[index]) : 0));
        }
        Set(out, "gids", gids); Queue(session, out);
        session.terrain_col += count;
        if (session.terrain_col >= static_cast<std::size_t>(layer.width)) { session.terrain_col = 0; ++session.terrain_row; }
        return;
    }
    session.terrain_pending = false;
}

void CDevDebugService::Impl::SendLogs(Session& session)
{
    auto* entries = json_array();
    {
        std::lock_guard<std::mutex> lock(logs->mutex);
        for (const auto& line : logs->lines)
        {
            if (line.sequence <= session.last_log) continue;
            json_array_append_new(entries, Str(line.text)); session.last_log = line.sequence;
            if (json_array_size(entries) >= 64) break;
        }
    }
    if (!json_array_size(entries)) { json_decref(entries); return; }
    auto* out = json_object(); Set(out, "kind", Str("logs")); Set(out, "lines", entries); Queue(session, out);
}

void CDevDebugService::Impl::EntityAction(Session& session, json_t* request)
{
    CGameWorld& world = RequestWorld(session, request);
    const auto action = ReadDebugText(request, "action", 32);
    if (action != "despawn" && action != "kill" && action != "heal" && action != "team" && action != "teleport")
        throw std::runtime_error("Unknown entity action");
    json_t* refs = json_object_get(request, "targets");
    if (!json_is_array(refs) || !json_array_size(refs) || json_array_size(refs) > dev_debug::selection_limit)
        throw std::runtime_error("Select 1..128 entities");
    std::vector<CEntity*> targets; std::unordered_set<int> seen; std::string audit_targets;
    for (std::size_t i = 0; i < json_array_size(refs); ++i)
    {
        auto* ref = json_array_get(refs, i);
        const auto id = static_cast<int>(Int(ref, "id", 0, INT32_MAX));
        auto* entity = world.GetEntity(id, Generation(ref));
        if (!entity || entity->IsMarkedForDes() || !seen.insert(id).second)
            throw std::runtime_error("Stale/duplicate entity selection; nothing was changed");
        targets.push_back(entity);
        if (!audit_targets.empty()) audit_targets += ',';
        audit_targets += std::to_string(id) + "@" + std::to_string(entity->Generation());
    }
    const int team = action == "team" ? static_cast<int>(Int(request, "team", 0, 255)) : 0;
    florrbt::Vector2f shift{};
    if (action == "teleport")
    {
        auto destination = RequestPosition(request, world);
        florrbt::Vector2f center{};
        for (const auto* entity : targets) center += entity->Pos() / static_cast<float>(targets.size());
        shift = destination - center;
        for (const auto* entity : targets)
        {
            auto next = entity->Pos() + shift;
            const auto* map = world.GetMap();
            if (!std::isfinite(next.x) || !std::isfinite(next.y) || next.x < 0 || next.y < 0 ||
                (map && (next.x > static_cast<double>(map->width) * map->tile_width ||
                         next.y > static_cast<double>(map->height) * map->tile_height)))
                throw std::runtime_error("Group would move outside the map; nothing was changed");
        }
    }
    // All references and parameters are validated before the first mutation.
    std::size_t applied = 0;
    for (auto* entity : targets)
    {
        if (entity->IsMarkedForDes()) continue;
        if (action == "despawn") entity->MarkForDestroy(EEntityRemovalReason::Despawned);
        else if (action == "kill") ApplyDebugKill(*entity);
        else if (action == "team") entity->Team() = team;
        else if (action == "heal") HealDebugEntity(*entity);
        else if (action == "teleport")
        {
            entity->Pos() += shift; entity->PrevPos() = entity->Pos();
            if (auto* mob = dynamic_cast<CMobBase*>(entity)) mob->m_vel = { 0.f, 0.f };
        }
        ++applied;
    }
    LOG_INFO("dev", session.grant.account + " entity " + action + " world=" + std::to_string(world.GetId()) +
                    " targets=" + std::to_string(applied) + " [" + audit_targets + "]");
    Reply(session, request, true, "Processed " + std::to_string(applied) + " entities: " + action);
    session.last_snapshot = 0;
}

void CDevDebugService::Impl::PlayerAction(Session& session, json_t* request)
{
    CPlayer& player = RequestPlayer(request);
    const auto action = ReadDebugText(request, "action", 32);
    const auto id = player.GetId(); const std::string account = player.GetAccountName();
    std::string result = action, audit;
    if (action == "inventory")
    {
        const auto type = static_cast<std::uint8_t>(Int(request, "type", 1, 255));
        const auto rarity = static_cast<std::uint8_t>(Int(request, "rarity", 1, 255));
        const auto count = static_cast<std::uint32_t>(Int(request, "count", 0, max_inventory_item_count));
        const auto mode = ReadDebugText(request, "mode", 16);
        if (!EditDebugInventory(server, player, mode, type, rarity, count, result))
            throw std::runtime_error(result);
        audit = " " + mode + " type=" + std::to_string(type) + " rarity=" + std::to_string(rarity) +
                " count=" + std::to_string(count) + " " + result;
    }
    else if (action == "mute") player.MuteFor(Float(request, "seconds", 1, 86400 * 30));
    else if (action == "unmute") player.Unmute();
    else if (action == "kick")
    {
        server.GetNetworkModule()->KickPlayer(id, "Removed by dev debug administrator");
    }
    else if (action == "teleport")
    {
        CGameWorld& world = RequestWorld(session, request);
        const auto position = RequestPosition(request, world);
        auto* entity = player.GetEntity(); auto* source = entity ? entity->GameWorld() : nullptr;
        if (!source || !source->TransferPlayerEntityToWorld(player, world, position))
            throw std::runtime_error("Player teleport failed");
    }
    else if (action == "kill" || action == "heal")
    {
        auto* entity = player.GetEntity();
        if (!entity) throw std::runtime_error("Player has no entity");
        if (action == "kill") ApplyDebugKill(*entity);
        else HealDebugEntity(*entity);
    }
    else throw std::runtime_error("Unknown player action");
    // A kick/teleport may invalidate the CPlayer/entity; use copied identity.
    LOG_INFO("dev", session.grant.account + " player " + action + " player=" + std::to_string(id) + " account=" + account + audit);
    Reply(session, request, true, result); session.last_status = session.last_snapshot = 0;
}

void CDevDebugService::Impl::Handle(Session& session, json_t* request)
{
    const auto op = ReadDebugText(request, "op", 32);
    if (!session.authorized)
    {
        if (op != "hello") { session.closed = true; return; }
        const auto token = ReadDebugText(request, "ticket", dev_debug::ticket_size);
        auto it = tickets.find(token);
        if (!dev_debug::IsTicket(token) || it == tickets.end() || it->second.expires < Now() ||
            it->second.remote != session.remote || !Valid(it->second))
        { session.closed = true; return; }
        session.grant = it->second; tickets.erase(it); session.authorized = true;
        const auto* player = server.GetNetworkModule()->FindPlayerById(session.grant.player);
        const auto* entity = player ? player->GetEntity() : nullptr;
        const auto* world = entity ? entity->GameWorld() : server.GetMainWorld();
        if (world)
        {
            session.world = world->GetId(); session.world_generation = world->GetInstanceGeneration();
            session.center = entity ? entity->Pos() : florrbt::Vector2f{}; session.terrain_pending = true;
        }
        auto* hello = json_object(); Set(hello, "kind", Str("hello")); Set(hello, "version", json_integer(dev_debug::version));
        Set(hello, "operator", Str(session.grant.account)); Set(hello, "center", Position(session.center));
        Set(hello, "world", json_integer(session.world)); Set(hello, "worldGeneration", Str(std::to_string(session.world_generation)));
        Queue(session, hello); SendCatalog(session); SendStatus(session);
        LOG_INFO("dev", "Observer authorized for " + session.grant.account);
        return;
    }
    if (!Valid(session.grant)) { session.closed = true; return; }
    if (op == "view")
    {
        const auto id = static_cast<std::uint32_t>(Int(request, "world", 0, UINT32_MAX));
        auto* world = server.FindWorldById(id);
        if (!world || world->GetInstanceGeneration() != Generation(request, "worldGeneration"))
            throw std::runtime_error("World changed; refresh the world list");
        const florrbt::Vector2f center{ Float(request, "x", -10000000, 10000000), Float(request, "y", -10000000, 10000000) };
        const float radius = Float(request, "radius", 32, 65536);
        if (session.world != id || session.world_generation != world->GetInstanceGeneration())
        {
            session.world = id; session.world_generation = world->GetInstanceGeneration();
            session.terrain_layer = session.terrain_row = session.terrain_col = 0; session.terrain_pending = true;
        }
        session.center = center; session.radius = radius;
    }
    else if (op == "inspect")
    {
        auto& world = RequestWorld(session, request);
        auto* entity = world.GetEntity(static_cast<int>(Int(request, "id", 0, INT32_MAX)), Generation(request));
        if (!entity || entity->IsMarkedForDes()) throw std::runtime_error("Entity no longer exists");
        Reply(session, request, true, "Entity", EntityJson(*entity, snapshots, true));
    }
    else if (op == "player") Reply(session, request, true, "Player", PlayerJson(RequestPlayer(request), true));
    else if (op == "entity_action") EntityAction(session, request);
    else if (op == "player_action") PlayerAction(session, request);
    else if (op == "spawn")
    {
        auto& world = RequestWorld(session, request);
        const auto kind = ReadDebugText(request, "kind", 16);
        const auto type = static_cast<int>(Int(request, "type", 1, 255));
        const auto rarity = static_cast<ERarity>(Int(request, "rarity", 1, 255));
        if (!IsKnownRarity(rarity)) throw std::runtime_error("Unknown rarity");
        const auto count = static_cast<int>(Int(request, "count", 1, 64));
        const auto position = RequestPosition(request, world);
        if (kind == "mob")
        {
            if (static_cast<EMobType>(type) == EMobType::PlayerFlower || !FindMobPrototype(static_cast<EMobType>(type)))
                throw std::runtime_error("Unknown/unspawnable mob type");
        }
        else if (kind != "petal" || !FindPetalPrototype(static_cast<EPetalType>(type)))
            throw std::runtime_error("Unknown petal type");
        for (int i = 0; i < count; ++i)
        {
            if (kind == "mob")
            {
                auto mob = CreateMob(static_cast<EMobType>(type), &world, position, rarity, false, false);
                if (!mob || !world.InsertEntity(std::move(mob))) throw std::runtime_error("Mob factory rejected spawn");
            }
            else
                world.InsertEntity(std::make_unique<CDrop>(&world, position, static_cast<EPetalType>(type), rarity));
        }
        LOG_INFO("dev", session.grant.account + " spawn " + kind + "=" + std::to_string(type) + " rarity=" +
                       std::to_string(static_cast<int>(rarity)) + " count=" + std::to_string(count) + " world=" + std::to_string(world.GetId()));
        Reply(session, request, true, "Spawned " + std::to_string(count)); session.last_snapshot = 0;
    }
    else if (op == "console")
    {
        if (Now() - session.last_command < 0.25) throw std::runtime_error("Console command rate limit");
        const auto command = ReadDebugText(request, "line", 2048);
        if (command.empty() || command.find('\0') != std::string::npos) throw std::runtime_error("Empty/invalid command");
        session.last_command = Now();
        // Audit command names, not potential passwords in their arguments.
        LOG_INFO("dev", session.grant.account + " console " + command.substr(0, command.find_first_of(" \t\r\n")));
        server.GetConsole().ExecuteLine(command);
        Reply(session, request, true, "Command executed; output is in the console stream");
    }
    else if (op == "refresh")
    {
        SendCatalog(session); SendStatus(session);
        session.terrain_layer = session.terrain_row = session.terrain_col = 0;
        session.terrain_pending = true; session.last_snapshot = 0;
        Reply(session, request, true, "World view refreshed");
    }
    else if (op == "ping") Reply(session, request, true, "pong");
    else if (op == "close") session.closed = true;
    else throw std::runtime_error("Unknown debug operation");
}

CDevDebugService::CDevDebugService(CServer& server) : m_impl(std::make_unique<Impl>(server)) {}
CDevDebugService::~CDevDebugService() = default;

std::string CDevDebugService::IssueTicket(const CPlayer& authorizer)
{
    if (!authorizer.IsAuthenticated() || !authorizer.IsConnected() || !authorizer.IsRconAuthorized() ||
        game_config::rcon_password.empty()) return {};
    const double now = Now();
    for (auto it = m_impl->tickets.begin(); it != m_impl->tickets.end();)
        if (it->second.expires < now || it->second.player == authorizer.GetId()) it = m_impl->tickets.erase(it);
        else ++it;
    if (m_impl->tickets.size() >= 32) return {};
    std::array<std::uint8_t, 32> random{};
    if (!PlatformRandomBytes(random.data(), random.size())) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string ticket; ticket.reserve(dev_debug::ticket_size);
    for (auto byte : random) { ticket.push_back(hex[byte >> 4]); ticket.push_back(hex[byte & 15]); }
    m_impl->tickets.emplace(ticket, Impl::Grant{ authorizer.GetId(), authorizer.GetAccountName(), authorizer.GetRemoteAddress(),
                                               game_config::rcon_password, now + 30 });
    return ticket;
}

void CDevDebugService::Accept(sf::TcpSocket&& socket, std::string remote, std::vector<std::uint8_t> received)
{
    if (m_impl->sessions.size() >= dev_debug::session_limit || game_config::rcon_password.empty())
    { socket.disconnect(); return; }
    auto session = std::make_unique<Impl::Session>();
    session->socket = std::move(socket); session->socket.setBlocking(false);
    session->remote = std::move(remote); session->receive = std::move(received);
    // Do not reserve an observer slot for an idle/unauthorized peer. The
    // pre-connection gate delivered a complete, small hello frame already.
    std::size_t size = 0;
    if (dev_debug::ReadFrameSize(session->receive.data(), session->receive.size(), dev_debug::hello_limit, size) !=
        dev_debug::FrameStatus::Ready) return;
    json_error_t error{};
    Json hello(json_loadb(reinterpret_cast<const char*>(session->receive.data() + dev_debug::header_size),
                         size, JSON_REJECT_DUPLICATES, &error));
    if (!hello || !json_is_object(hello.get())) return;
    session->receive.erase(session->receive.begin(), session->receive.begin() + dev_debug::header_size + size);
    try { m_impl->Handle(*session, hello.get()); } catch (...) { return; }
    if (session->authorized && !session->closed) m_impl->sessions.push_back(std::move(session));
}

void CDevDebugService::Tick(float)
{
    const double now = Now();
    for (auto it = m_impl->tickets.begin(); it != m_impl->tickets.end();)
        if (it->second.expires < now || !m_impl->Valid(it->second)) it = m_impl->tickets.erase(it);
        else ++it;
    std::array<std::uint8_t, 8192> buffer{};
    for (auto& ptr : m_impl->sessions)
    {
        auto& session = *ptr;
        if (session.closed || (!session.authorized && now > session.deadline) ||
            (session.authorized && !m_impl->Valid(session.grant)) || !m_impl->Flush(session))
        { session.closed = true; continue; }
        std::size_t received = 0;
        const auto status = session.socket.receive(buffer.data(), buffer.size(), received);
        if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error)
        { session.closed = true; continue; }
        if (received) session.receive.insert(session.receive.end(), buffer.begin(), buffer.begin() + received);
        if (session.receive.size() > dev_debug::request_limit * 2) { session.closed = true; continue; }
        for (int work = 0; work < 16 && !session.closed; ++work)
        {
            std::size_t length = 0;
            const auto frame = dev_debug::ReadFrameSize(session.receive.data(), session.receive.size(), dev_debug::request_limit, length);
            if (frame == dev_debug::FrameStatus::Incomplete) break;
            if (frame == dev_debug::FrameStatus::Invalid || !session.rate.Take(now)) { session.closed = true; break; }
            json_error_t error{};
            Json request(json_loadb(reinterpret_cast<const char*>(session.receive.data() + dev_debug::header_size),
                                   length, JSON_REJECT_DUPLICATES, &error));
            session.receive.erase(session.receive.begin(), session.receive.begin() + dev_debug::header_size + length);
            if (!request || !json_is_object(request.get())) { session.closed = true; break; }
            try { m_impl->Handle(session, request.get()); }
            catch (const std::exception& e)
            {
                if (!session.authorized) session.closed = true;
                else m_impl->Reply(session, request.get(), false, e.what());
            }
        }
        if (!session.authorized || session.closed) continue;
        // A command may kick its own authorizer or rotate the RCON password.
        // Do not emit another snapshot/log after that authority is revoked.
        if (!m_impl->Valid(session.grant)) { session.closed = true; continue; }
        // Do not build more snapshots while a slow connection is backed up.
        if (session.send.size() - session.send_offset < 512 * 1024)
        {
            if (now - session.last_snapshot >= 0.1) { m_impl->SendSnapshot(session); session.last_snapshot = now; }
            if (now - session.last_status >= 1) { m_impl->SendStatus(session); session.last_status = now; }
            m_impl->SendTerrain(session); m_impl->SendLogs(session);
        }
        if (!m_impl->Flush(session)) session.closed = true;
    }
    std::erase_if(m_impl->sessions, [](const auto& session) { return session->closed; });
}

void CDevDebugService::Reset()
{
    m_impl->sessions.clear(); m_impl->tickets.clear();
}

void CDevDebugService::BroadcastTerrain(CGameWorld& world, std::uint8_t layer, std::uint16_t row,
                                      std::uint16_t col, std::uint32_t gid)
{
    for (auto& session : m_impl->sessions)
    {
        if (!session->authorized || !m_impl->Valid(session->grant) || m_impl->World(*session) != &world) continue;
        auto* out = json_object(); Set(out, "kind", Str("terrain")); Set(out, "world", json_integer(world.GetId()));
        Set(out, "worldGeneration", Str(std::to_string(world.GetInstanceGeneration())));
        Set(out, "layer", json_integer(layer)); Set(out, "row", json_integer(row)); Set(out, "col", json_integer(col));
        auto* gids = json_array(); json_array_append_new(gids, json_integer(gid)); Set(out, "gids", gids);
        m_impl->Queue(*session, out);
    }
}

void CDevDebugService::BroadcastWallRemoval(CGameWorld& world, float min_x, float min_y, float max_x, float max_y)
{
    for (auto& session : m_impl->sessions)
    {
        if (!session->authorized || !m_impl->Valid(session->grant) || m_impl->World(*session) != &world) continue;
        auto* out = json_object(); Set(out, "kind", Str("walls")); Set(out, "world", json_integer(world.GetId()));
        Set(out, "worldGeneration", Str(std::to_string(world.GetInstanceGeneration())));
        Set(out, "minX", Num(min_x)); Set(out, "minY", Num(min_y)); Set(out, "maxX", Num(max_x)); Set(out, "maxY", Num(max_y));
        m_impl->Queue(*session, out);
    }
}
