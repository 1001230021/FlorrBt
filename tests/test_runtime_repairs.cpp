#include "test_framework.h"
#include "Server/server.h"
#include "Server/Module/world_module.h"
#include "Server/Module/network_module.h"
#include "Server/Game/gamecontext.h"
#include "Server/Game/controller_factory.h"
#include "Server/Game/talent.h"
#include "Server/Game/gamecontrollers/world_controller_factory.h"
#include "Server/Game/gamecontrollers/pvp_controller.h"
#include "Server/Game/gamecontrollers/sandbox_controller.h"
#include "Server/Game/gamecontrollers/ant_kingdom_controller.h"
#include "Server/Game/gamecontrollers/deathmatch_controller.h"
#include "Server/Game/chain.h"
#include "Server/Game/entities/eou_mob.h"
#include "Server/Game/entities/petals/petals_behavior.h"
#include "Server/Game/entities/projectile.h"
#include "Server/Game/entities/drop.h"
#include "Server/Game/extensions/weather_extension.h"
#include "Server/Game/extensions/squad_extension.h"
#include "Server/Game/extensions/chat_extension.h"
#include "Server/HotReload/hot_reload_snapshot.h"
#include "Server/HotReload/mob_stats_snapshot.h"
#include "Server/Persistence/account_store.h"
#include "Server/report.h"
#include <chrono>
#include <filesystem>
#include <future>
#include <thread>

namespace
{
struct STestSession
{
    std::filesystem::path directory;
    STestSession()
    {
        directory = std::filesystem::path("artifacts") /
            ("runtime-repairs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        game_config::account_backend = "json";
        game_config::garden_world_instances = game_config::ant_hel_world_instances =
            game_config::open_world_instances_per_map = 1;
        std::string error;
        CHECK(RegisterPetals(error));
        CHECK(RegisterMobs(error));
        CHECK(CAccountDataStore::Load(directory / "accounts.json", &error));
        CHECK(CAccountDataStore::CommitRegistration("runtime-player", "offline-fixture-hash", "", "127.0.0.1", &error));
    }
    ~STestSession() { CAccountDataStore::ShutDown(); }
};
STestSession& Session() { static STestSession session; return session; }

CMobBase* Spawn(CGameWorld& world, EMobType type, ERarity rarity = ERarity::Common,
               florrbt::Vector2f pos = {1000.f, 1000.f})
{
    auto mob = CreateMob(type, &world, pos, rarity);
    CHECK(mob != nullptr);
    return dynamic_cast<CMobBase*>(world.InsertEntity(std::move(mob)));
}

json_t* SavedWorlds(json_t* root)
{
    return json_object_get(json_object_get(json_object_get(root, "sections"), "worlds"), "data");
}
}

// Uses production modules and codecs, but deliberately never calls Init:
// no listening ports, auth workers, GUI, startup commands or real accounts.
class CServerTestAccess
{
  public:
    CServer server;
    CServerTestAccess()
    {
        Session();
        server.m_modules.clear();
        auto worlds = std::make_unique<IWorldModule>();
        server.m_p_world_module = worlds.get();
        auto network = std::make_unique<INetworkModule>(server);
        server.m_p_network_module = network.get();
        std::string extension_error;
        CHECK(server.Extensions().Init(*network, *network, extension_error));
        server.m_p_game_context = std::make_unique<CGameContext>(*network,
            server, server, server.Extensions());
        for (const auto& world : worlds->GetWorlds())
        {
            world->SetController(nullptr);
            world->ClearEntitiesForRestore();
            world->SetGameContext(server.m_p_game_context.get());
        }
        server.m_modules.push_back(std::move(worlds));
        server.m_modules.push_back(std::move(network));
    }
    ~CServerTestAccess()
    {
        server.GetNetworkModule()->ClearPlayersForRestore();
        for (CGameWorld* world : server.GetWorlds()) world->SetGameContext(nullptr);
    }
    CGameWorld& World() { return *server.GetMainWorld(); }
    static void SetShortcutTestTerrain(CGameWorld& world)
    {
        world.m_map->walls.clear();
        for (int index = 0; index < 2; ++index)
        {
            FlorrBtMap::Wall wall;
            wall.id = index;
            wall.category = index == 0 ? EWallQuery::Shortcut : EWallQuery::Solid;
            const float x = index == 0 ? 2000.f : 2400.f;
            wall.vertices = {{x, 900}, {x + 100, 900}, {x + 100, 1100}, {x, 1100}};
            FinalizeWallBounds(wall);
            world.m_map->walls.push_back(std::move(wall));
        }
        world.BuildWallGrid();
    }
    static void ResolveTestWalls(CGameWorld& world, CEntity& entity)
    {
        CEntity* raw = &entity;
        world.ResolveWallCollisions(std::span<CEntity* const>(&raw, 1));
    }
    void CheckAtomicSceneSync()
    {
        class CScene final : public IGameController
        {
          public:
            void OnTick(CGameWorld&, float) override {}
            std::string_view SnapshotKey() const override { return "test.scene"; }
            bool WritePlayerScene(const CGameWorld&, const CPlayer&, IPlayerSceneWriter& writer) const override
            { return writer.TerrainTile(1, 2, 3, 4) && writer.RemoveWall({0, 0}, {5, 5}); }
        };
        World().SetController(std::make_unique<CScene>());
        auto* player = Player(World());
        auto& network = *server.GetNetworkModule();
        ServerMessage noise; noise.type = ServerMessage::Type::Environment;
        CHECK(network.QueueMessage(*player, noise));
        const auto before = player->m_send_buffer;
        const auto saved_limit = game_config::network_max_send_buffer_size;
        game_config::network_max_send_buffer_size = 1;
        CHECK(!network.QueueWelcome(*player));
        CHECK(player->m_send_buffer == before);
        CHECK(network.m_pending_scene_syncs.contains(player->GetId()));
        network.NotifyPlayerRulesChanged(*player);
        CHECK(network.m_pending_scene_syncs.at(player->GetId()).full_scene);
        CHECK(!network.SendTerrainDelta(*player, 0, 0, 0, 8));
        player->m_send_buffer.clear(); player->m_send_offset = 0;
        CHECK(network.QueueWelcome(*player));
        CHECK(!network.m_pending_scene_syncs.contains(player->GetId()));
        game_config::network_max_send_buffer_size = saved_limit;
        std::vector<ServerMessage::Type> messages;
        for (size_t offset = 0; offset + 2 < player->m_send_buffer.size();)
        {
            const size_t length = player->m_send_buffer[offset] | (size_t(player->m_send_buffer[offset + 1]) << 8);
            CHECK(length > 0 && offset + length + 2 <= player->m_send_buffer.size());
            if (!length || offset + length + 2 > player->m_send_buffer.size()) break;
            messages.push_back(static_cast<ServerMessage::Type>(player->m_send_buffer[offset + 2]));
            offset += length + 2;
        }
        CHECK(!messages.empty() && messages.front() == ServerMessage::Type::Welcome);
        CHECK(std::find(messages.begin(), messages.end(), ServerMessage::Type::TerrainDelta) != messages.end());
        CHECK(std::find(messages.begin(), messages.end(), ServerMessage::Type::TerrainWallRemove) != messages.end());
        player->m_send_buffer.clear(); player->m_send_offset = 0;
        network.NotifyPlayerRulesChanged(*player);
        CHECK(!network.m_pending_scene_syncs.contains(player->GetId()));
        CHECK(!player->m_send_buffer.empty());
        for (size_t offset = 0; offset + 2 < player->m_send_buffer.size();)
        {
            const size_t length = player->m_send_buffer[offset] | (size_t(player->m_send_buffer[offset + 1]) << 8);
            CHECK(length > 0 && offset + length + 2 <= player->m_send_buffer.size());
            if (!length || offset + length + 2 > player->m_send_buffer.size()) break;
            const auto type = static_cast<ServerMessage::Type>(player->m_send_buffer[offset + 2]);
            CHECK(type != ServerMessage::Type::Welcome && type != ServerMessage::Type::TerrainDelta &&
                  type != ServerMessage::Type::TerrainWallRemove);
            offset += length + 2;
        }
    }
    static void FailTransferAfter(CGameWorld& world, int inserts) { world.m_transfer_fault_after_insert = inserts; }
    void CheckSparsePlayerIds()
    {
        auto& network = *server.GetNetworkModule();
        const auto high_id = static_cast<std::uint32_t>(std::numeric_limits<int>::max() - 1);
        auto player = std::make_unique<CPlayer>(sf::TcpSocket{}, high_id, "sparse-id");
        player->Authenticate("sparse-id");
        CHECK(network.InsertRestoredPlayer(std::move(player)));
        CHECK(!network.InsertRestoredPlayer(std::make_unique<CPlayer>(sf::TcpSocket{}, high_id + 1, "overflow-id")));
        CHECK(!network.InsertRestoredPlayer(std::make_unique<CPlayer>(sf::TcpSocket{}, UINT32_MAX, "unsigned-id")));
        network.FinalizePlayerRestore();
        CHECK(network.GetNewPlayerId() == 1);
        CHECK(network.GetNewPlayerId() == 2);
        network.FreePlayerId(1);
        CHECK(network.GetNewPlayerId() == 1);
        CHECK(network.GetNewPlayerId() == 3);
        network.m_next_player_id = std::numeric_limits<int>::max();
        CHECK(network.GetNewPlayerId() == -1);
    }
    void CheckSnapshotProtocol()
    {
        CPlayer* player = Player(World());
        auto& network = *server.GetNetworkModule();
        ServerMessage full;
        full.type = ServerMessage::Type::Snapshot;
        full.snapshot_id = UINT32_MAX - 1;
        full.owner_entity_id = 1;
        ServerEntitySnap owner, other;
        owner.entity_id = 1; owner.generation = 1;
        other.entity_id = 65537; other.generation = 2;
        full.entities = {owner, other};
        size_t actual = 0;
        auto history = network.PrepareSnapshotForSend(*player, full, ServerMessage::GetPackedSize(full), actual);
        CHECK(full.base_snapshot_id == full_snapshot_base_id);
        CHECK(network.QueueMessage(*player, full, actual));
        network.RecordSentSnapshot(*player, std::move(history));
        ClientSnapshotAck ack;
        ack.snapshot_id = *full.snapshot_id;
        network.HandleSnapshotAck(*player, ack);

        ServerMessage next = full;
        next.snapshot_id = 0;
        next.entities = {owner};
        history = network.PrepareSnapshotForSend(*player, next, ServerMessage::GetPackedSize(next), actual);
        CHECK(next.base_snapshot_id == UINT32_MAX - 1);
        CHECK(next.entities.empty());
        CHECK(next.removed_entity_ids == std::vector<net_entity_id>{65537});
        CHECK(network.QueueMessage(*player, next, actual));
        network.RecordSentSnapshot(*player, std::move(history));
        const auto first_packet_size = size_t(player->m_send_buffer[0]) + (size_t(player->m_send_buffer[1]) << 8) + 2;
        player->m_send_offset = 1;
        network.DropQueuedSnapshots(*player);
        CHECK(player->m_send_buffer.size() == first_packet_size);
        CHECK(player->m_send_offset == 1);
        ack.snapshot_id = 0; network.HandleSnapshotAck(*player, ack);
        ack.snapshot_id = UINT32_MAX - 1; network.HandleSnapshotAck(*player, ack);
        CHECK(network.m_client_snapshot_states.at(player->GetId()).acked_snapshot_id == 0);
        ack.snapshot_id = 100; network.HandleSnapshotAck(*player, ack);
        next = full; next.snapshot_id = 1;
        history = network.PrepareSnapshotForSend(*player, next, ServerMessage::GetPackedSize(next), actual);
        CHECK(next.base_snapshot_id == full_snapshot_base_id);
        network.RecordSentSnapshot(*player, history);
        ack.snapshot_id = 1; network.HandleSnapshotAck(*player, ack);
        for (std::uint32_t id = 2; id < 100; ++id)
        {
            history.snapshot_id = id;
            network.RecordSentSnapshot(*player, history);
        }
        CHECK(!network.m_client_snapshot_states.at(player->GetId()).acked_snapshot_id);
        ack.snapshot_id = full_snapshot_base_id; network.HandleSnapshotAck(*player, ack);
        CHECK(!network.m_client_snapshot_states.contains(player->GetId()));
    }
    CPlayer* Player(CGameWorld& world, std::uint32_t id = 1)
    {
        auto player = std::make_unique<CPlayer>(sf::TcpSocket{}, id, "runtime-player");
        player->Authenticate("runtime-player");
        player->SetOwnedEntity(Spawn(world, EMobType::PlayerFlower));
        CPlayer* raw = player.get();
        CHECK(server.GetNetworkModule()->InsertRestoredPlayer(std::move(player)));
        return raw;
    }
};

TEST(ShortcutQuarterTileGeometryAndCoverAreClassifiedAtLoad)
{
    const auto map = LoadMapFromTmj("data/maps/garden.tmj");
    CHECK(map != nullptr); if (!map) return;
    size_t expected = 0, actual = 0;
    for (const auto& layer : map->tile_layers)
        if (layer.role == EMapLayerRole::Shortcut)
            expected += std::count_if(layer.tiles.begin(), layer.tiles.end(), [](int gid) { return gid != 0; });
    for (const auto& wall : map->walls)
    {
        if (wall.category != EWallQuery::Shortcut) continue;
        ++actual;
        float twice_area = 0.f;
        for (size_t i = 0; i < wall.vertices.size(); ++i)
        {
            const auto a = wall.vertices[i], b = wall.vertices[(i + 1) % wall.vertices.size()];
            CHECK(std::fmod(a.x, 256.f) == 0.f && std::fmod(a.y, 256.f) == 0.f);
            twice_area += a.x * b.y - a.y * b.x;
        }
        const float quarter_count = std::abs(twice_area) / (2.f * 256.f * 256.f);
        CHECK(quarter_count >= 1.f && quarter_count <= 3.f);
    }
    CHECK(expected > 0 && actual == expected);
}

TEST(ShortcutOnlyIgnoresItsOwnWallsForPlayerControlledMobs)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 301);
    world.ClearEntitiesForRestore();
    CServerTestAccess::SetShortcutTestTerrain(world);
    auto* player_mob = Spawn(world, EMobType::Bee);
    auto* ai_mob = Spawn(world, EMobType::Bee);
    player_mob->SetController(std::make_unique<CPlayerController>());
    CHECK(world.CircleBlockedByWall({2050, 1000}, 10));
    CHECK(!world.CircleBlockedByWall({2050, 1000}, 10, player_mob->WallCollisionMask()));
    CHECK(world.CircleBlockedByWall({2450, 1000}, 10, player_mob->WallCollisionMask()));
    for (auto* mob : {player_mob, ai_mob})
    {
        mob->PrevPos() = {1900, 1000}; mob->Pos() = {2050, 1000};
        CServerTestAccess::ResolveTestWalls(world, *mob);
    }
    CHECK(player_mob->Pos().x == 2050.f);
    CHECK(ai_mob->Pos().x < 2000.f);
    player_mob->SynchronizeEnvironment();
    CHECK(player_mob->IsInShortcut());
    player_mob->PrevPos() = {2300, 1000}; player_mob->Pos() = {2450, 1000};
    CServerTestAccess::ResolveTestWalls(world, *player_mob);
    CHECK(player_mob->Pos().x < 2400.f);
}

TEST(ShortcutSuspendsStatContributionsAndPetalsButPreservesStatesAndTalents)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* player = fixture.Player(world);
    auto* flower = dynamic_cast<CPlayerFlower*>(player->GetEntity());
    CHECK(flower != nullptr); if (!flower) return;
    flower->RebuildFinalStats();
    const auto baseline = *flower->GetFinalStats();
    player->RestoreTalentPoints(1000);
    CHECK(player->AddTalent(ETalentId::FlowerHealth, ERarity::Common));
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    flower->LoadPetalSlot(1, FindPetalPrototype(ToPetalType(EModPetalType::MagicLeaf)), ERarity::Common);
    auto& slot = flower->GetSlots()[0];
    slot.SpawnCopy(0, flower); flower->RebuildFinalStats();
    CHECK(slot.m_p_petals[0] != nullptr);
    CHECK(flower->GetFinalStats()->health_regen > baseline.health_regen);
    CHECK(flower->GetFinalStats()->max_health > baseline.max_health);
    CHECK(flower->GetMaxMana() > baseline.max_mana);
    flower->Mana() = flower->GetMaxMana();
    const auto talent_count = player->GetTalents().size(), slot_count = flower->GetSlots().size();
    flower->Pos() = {2050, 1000}; flower->SynchronizeEnvironment();
    CHECK(flower->IsInShortcut() && flower->ArePetalsSuppressed());
    CHECK(slot.m_p_petals[0] == nullptr);
    CHECK(flower->GetFinalStats()->health_regen == baseline.health_regen);
    CHECK(flower->GetFinalStats()->max_health == baseline.max_health);
    CHECK(flower->GetMaxMana() == baseline.max_mana && flower->Mana() <= baseline.max_mana);
    CHECK(player->GetTalents().size() == talent_count && flower->GetSlots().size() == slot_count);
    const auto timers = slot.m_reload_timers;
    const float mana = flower->Mana();
    slot.Tick(10.f, flower); slot.SpawnCopy(0, flower);
    CHECK(slot.m_reload_timers == timers && slot.m_p_petals[0] == nullptr && flower->Mana() == mana);
    CHECK(flower->HasState<CInvincibleState>()); // Spawn protection is a state, not a stat modifier.
    flower->RemoveState(flower->FindFirstState<CInvincibleState>());
    flower->AddState(std::make_unique<CPoisonState>(flower, 1.f, 5.f, ERarity::Common, nullptr));
    flower->AddState(std::make_unique<CPulseState>(flower, 1.f, ERarity::Common));
    const float health = flower->Health(); flower->TickStates(0.2f);
    CHECK(std::abs(flower->Health() - (health - 1.f)) < 0.001f);
    CHECK(std::abs(flower->FindFirstState<CPoisonState>()->m_timer - 0.8f) < 0.001f);
    CHECK(GetPulseMovementMultiplier(flower) > 1.f);
    CSnapshotService service;
    CHECK((service.BuildEntitySnap(*flower, *flower).flags & static_cast<uint16_t>(ServerEntityFlag::Shortcut)) != 0);
    flower->Pos() = {2200, 1000}; flower->SynchronizeEnvironment();
    CHECK(!flower->IsInShortcut() && !flower->ArePetalsSuppressed());
    CHECK(slot.m_reload_timers[0] == slot.m_reload_durations[0] && slot.m_reload_timers[0] > 0.f);
    flower->RebuildFinalStats();
    CHECK(flower->GetFinalStats()->health_regen == baseline.health_regen); // Petal is still reloading.
    CHECK(flower->GetFinalStats()->max_health > baseline.max_health); // Talent resumes immediately.
    CHECK(flower->GetMaxMana() > baseline.max_mana && flower->Mana() == mana); // No free refill.
    slot.Tick(0.1f, flower); const auto remaining = slot.m_reload_timers;
    flower->SynchronizeEnvironment();
    CHECK(slot.m_reload_timers == remaining);
    slot.Tick(slot.m_reload_durations[0] + 0.1f, flower); flower->RebuildFinalStats();
    CHECK(flower->GetFinalStats()->health_regen > baseline.health_regen);
    CHECK((service.BuildEntitySnap(*flower, *flower).flags & static_cast<uint16_t>(ServerEntityFlag::Shortcut)) == 0);
}

TEST(ShortcutDiggingOverlapAndSweptCrossingReloadOnlyAfterLastRestriction)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* flower = dynamic_cast<CFlower*>(fixture.Player(world)->GetEntity());
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    auto& slot = flower->GetSlots()[0]; slot.SpawnCopy(0, flower);
    flower->Pos() = {2050, 1000}; flower->SynchronizeEnvironment();
    flower->AddState(std::make_unique<CDiggingState>(flower, 0.2f, ERarity::Common));
    flower->RefreshPetalSuppression();
    const auto timers = slot.m_reload_timers;
    flower->TickStates(0.3f);
    CHECK(!flower->HasState<CDiggingState>() && flower->ArePetalsSuppressed());
    CHECK(slot.m_reload_timers == timers);
    flower->Pos() = {2200, 1000}; flower->SynchronizeEnvironment();
    CHECK(!flower->ArePetalsSuppressed() && slot.m_reload_timers[0] > 0.f);
    slot.SpawnCopy(0, flower);
    CHECK(slot.m_p_petals[0] != nullptr);
    flower->SynchronizeEnvironment(florrbt::Vector2f{1900, 1000});
    CHECK(!flower->IsInShortcut() && slot.m_p_petals[0] == nullptr);
    CHECK(slot.m_reload_timers[0] == slot.m_reload_durations[0]);
    flower->Pos() = {2050, 1000}; flower->SynchronizeEnvironment();
    flower->AddState(std::make_unique<CDiggingState>(flower, 0.2f, ERarity::Common));
    flower->RefreshPetalSuppression();
    flower->Pos() = {2200, 1000}; flower->SynchronizeEnvironment();
    CHECK(flower->ArePetalsSuppressed());
    flower->TickStates(0.3f);
    CHECK(!flower->ArePetalsSuppressed() && slot.m_reload_timers[0] > 0.f);
}

TEST(ShortcutKeepsPlayerControlledMobSkillsAvailable)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* hornet = Spawn(world, EMobType::Hornet, ERarity::Common, {2050, 1000});
    hornet->SetController(std::make_unique<CPlayerController>());
    hornet->Tick(0.f);
    CHECK(hornet->IsInShortcut());
    auto* skills = dynamic_cast<ISkillCasterMob*>(hornet);
    CHECK(skills != nullptr); if (!skills) return;
    CHECK(skills->TryCastSkill(2, nullptr));
    CHECK(skills->IsSkillBusy());
}

TEST(ShortcutFootprintDoesNotOscillateAndControlLossEvacuatesTheBody)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* flower = dynamic_cast<CPlayerFlower*>(fixture.Player(world)->GetEntity());
    flower->Radius() = 80.f; flower->Pos() = {1950, 1000}; flower->SynchronizeEnvironment();
    CHECK(flower->IsInShortcut());
    flower->Radius() = 18.f; flower->SynchronizeEnvironment();
    CHECK(flower->IsInShortcut() && flower->ShortcutProbeRadius() == 80.f);
    flower->Pos() = {1890, 1000}; flower->SynchronizeEnvironment();
    CHECK(!flower->IsInShortcut() && flower->ShortcutProbeRadius() == 0.f);
    flower->Pos() = {2050, 1000}; flower->SynchronizeEnvironment();
    flower->SetController(nullptr);
    CHECK(!flower->IsInShortcut());
    CHECK(!world.CircleBlockedByWall(flower->Pos(), flower->WallCollisionRadius()));
}

TEST(ShortcutBlockedMovementDoesNotTriggerAndLateDisplacementsDoNotDoubleReload)
{
    class CMotionController final : public IController
    {
      public:
        void OnTick(CMobBase*, float) override {}
        bool IsPlayerControlled() const override { return true; }
        std::string_view SnapshotKey() const override { return "test.motion"; }
    };
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* flower = dynamic_cast<CFlower*>(fixture.Player(world)->GetEntity());
    flower->SetController(std::make_unique<CMotionController>());
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    auto& slot = flower->GetSlots()[0]; slot.SpawnCopy(0, flower);
    flower->Pos() = {2550, 1000}; flower->m_vel = {-1000, 0};
    world.Tick(0.5f);
    CHECK(flower->Pos().x > 2500.f && !flower->IsInShortcut());
    CHECK(slot.m_p_petals[0] != nullptr && !flower->HasPendingPetalReload());
    flower->Pos() = {1900, 1000}; flower->SynchronizeEnvironment();
    flower->Pos() = {2200, 1000}; flower->SynchronizeEnvironmentAfterDisplacement();
    CHECK(!flower->IsInShortcut() && slot.m_p_petals[0] == nullptr && slot.m_reload_timers[0] > 0.f);
    slot.Tick(0.1f, flower); const auto timers = slot.m_reload_timers;
    flower->SynchronizeEnvironmentAfterDisplacement();
    CHECK(slot.m_reload_timers == timers);
}

TEST(ShortcutTransferRollbackKeepsRestrictionsAndCommitResumesOnce)
{
    CServerTestAccess fixture;
    auto& source = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(source);
    auto* target = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(target != nullptr); if (!target) return;
    auto* player = fixture.Player(source);
    auto* flower = dynamic_cast<CFlower*>(player->GetEntity());
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    flower->Pos() = {2050, 1000}; flower->SynchronizeEnvironment();
    const auto timers = flower->GetSlots()[0].m_reload_timers;
    CServerTestAccess::FailTransferAfter(source, 1);
    CHECK(!source.TransferPlayerEntityToWorld(*player, *target, florrbt::Vector2f{1000, 1000}));
    CHECK(flower->GameWorld() == &source && flower->IsInShortcut());
    CHECK(flower->GetSlots()[0].m_reload_timers == timers);
    CServerTestAccess::FailTransferAfter(source, -1);
    CHECK(source.TransferPlayerEntityToWorld(*player, *target, florrbt::Vector2f{1000, 1000}));
    CHECK(flower->GameWorld() == target && !flower->IsInShortcut() && !flower->ArePetalsSuppressed());
    const auto& slot = flower->GetSlots()[0];
    CHECK(slot.m_reload_timers[0] > 0.f && slot.m_reload_timers[0] == slot.m_reload_durations[0]);
}

TEST(ShortcutSnapshotRestoresSuspensionWithoutReloadingOrLosingPoison)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    auto* flower = dynamic_cast<CFlower*>(player->GetEntity());
    const auto& walls = world.GetMap()->walls;
    const auto found = std::find_if(walls.begin(), walls.end(), [&](const auto& wall) {
        return wall.category == EWallQuery::Shortcut &&
            world.CircleBlockedByWall({wall.center.x, wall.center.y}, 0.f, EWallQuery::Shortcut);
    });
    CHECK(found != walls.end()); if (found == walls.end()) return;
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    flower->Pos() = {found->center.x, found->center.y}; flower->SynchronizeEnvironment();
    flower->AddState(std::make_unique<CPoisonState>(flower, 8.f, 1.f, ERarity::Common, nullptr));
    const auto reference = flower->GetHandle();
    const auto timers = flower->GetSlots()[0].m_reload_timers;
    const auto path = Session().directory / "shortcut.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    flower = dynamic_cast<CFlower*>(world.ResolveEntity(reference));
    CHECK(flower != nullptr); if (!flower) return;
    CHECK(flower->IsInShortcut() && flower->ArePetalsSuppressed() && flower->HasPendingPetalReload());
    CHECK(flower->GetSlots()[0].m_reload_timers == timers);
    CHECK(flower->FindFirstState<CPoisonState>() && flower->FindFirstState<CPoisonState>()->m_timer == 8.f);
    flower->Tick(0.1f);
    CHECK(flower->GetSlots()[0].m_reload_timers == timers);
}

TEST(SandboxTransferIsolatesProgressAndRestoresStableIdentity)
{
    CServerTestAccess fixture;
    auto& source = fixture.World();
    auto* sandbox = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::sandbox);
    CHECK(sandbox != nullptr); if (!sandbox) return;
    auto* player = fixture.Player(source);
    const auto identity = player->GetAccountName();
    const auto inventory = CAccountDataStore::GetInventory(identity);
    const auto slots = CAccountDataStore::GetSlots(identity);
    CHECK(source.TransferPlayerEntityToWorld(*player, *sandbox, florrbt::Vector2f{1000, 1000}));
    CHECK(player->GetAccountName() == identity);
    CHECK(player->HasIsolatedProgress());
    const auto scope = player->GetProgressAccountName();
    auto* controller = sandbox->GetController();
    CHECK(controller->HandlePlayerCommand(*sandbox, *player, {"add", "Basic", "Rare", "4"}));
    CHECK(CAccountDataStore::GetInventory(identity) == inventory);
    CHECK(CAccountDataStore::HasItem(scope, static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Rare), 4));
    CPlayerLifecycleService lifecycle;
    CHECK(lifecycle.SpawnPlayer(*player, *sandbox, EPlayerSpawnReason::Respawn) != nullptr);
    CHECK(player->GetProgressAccountName() == scope);
    CHECK(CAccountDataStore::HasItem(scope, static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Rare), 4));
    CHECK(sandbox->TransferPlayerEntityToWorld(*player, source, florrbt::Vector2f{1000, 1000}));
    CHECK(!player->HasIsolatedProgress());
    CHECK(player->GetAccountName() == identity);
    CHECK(CAccountDataStore::GetInventory(identity) == inventory);
    CHECK(CAccountDataStore::GetSlots(identity) == slots);
    SPlayerAccount removed; CHECK(!CAccountDataStore::CaptureSandboxAccount(scope, removed));
}

TEST(SandboxPermissionsTargetsAndMalformedCommandsStayScoped)
{
    CServerTestAccess fixture;
    CControllerParams params; params.Set("enable_permission", "true");
    auto* sandbox = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::sandbox, params);
    CHECK(sandbox != nullptr); if (!sandbox) return;
    auto* owner = fixture.Player(fixture.World(), 1);
    auto* member = fixture.Player(fixture.World(), 2);
    auto* outside = fixture.Player(fixture.World(), 3);
    std::string error;
    CHECK(CAccountDataStore::CommitRegistration("boundary-member", "offline-fixture-hash", "", "127.0.0.1", &error));
    CHECK(CAccountDataStore::CommitRegistration("boundary-outside", "offline-fixture-hash", "", "127.0.0.1", &error));
    member->Authenticate("boundary-member"); outside->Authenticate("boundary-outside");
    CHECK(fixture.World().TransferPlayerEntityToWorld(*owner, *sandbox, florrbt::Vector2f{1000, 1000}));
    CHECK(fixture.World().TransferPlayerEntityToWorld(*member, *sandbox, florrbt::Vector2f{1100, 1000}));
    auto* controller = sandbox->GetController();
    const auto before = CAccountDataStore::GetInventory(member->GetProgressAccountName());
    controller->HandlePlayerCommand(*sandbox, *member, {"perm", "boundary-member"});
    controller->HandlePlayerCommand(*sandbox, *member, {"add", "Basic", "Rare", "2"});
    CHECK(CAccountDataStore::GetInventory(member->GetProgressAccountName()) == before);
    controller->HandlePlayerCommand(*sandbox, *owner, {"perm", "boundary-member"});
    controller->HandlePlayerCommand(*sandbox, *member, {"add", "Basic", "Rare", "2"});
    CHECK(CAccountDataStore::GetInventory(member->GetProgressAccountName()) != before);
    const auto position = outside->GetEntity()->Pos();
    controller->HandlePlayerCommand(*sandbox, *owner, {"kill", "boundary-outside"});
    controller->HandlePlayerCommand(*sandbox, *owner, {"tele", "boundary-outside", "0,0"});
    CHECK(!outside->GetEntity()->IsMarkedForDes());
    CHECK(outside->GetEntity()->Pos() == position);
    controller->HandlePlayerCommand(*sandbox, *owner, {"tele"});
    controller->HandlePlayerCommand(*sandbox, *owner, {"add"});
    controller->HandlePlayerCommand(*sandbox, *owner, {"add", "boundary-member", "Basic", "Rare", "3"});
    CHECK(CAccountDataStore::HasItem(member->GetProgressAccountName(), static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Rare), 5));
    controller->HandlePlayerCommand(*sandbox, *owner, {"add", "Basic", "Rare", "1", "junk"});
    controller->HandlePlayerCommand(*sandbox, *owner, {"tele", "1,2", "junk"});
}

TEST(SandboxSnapshotKeepsTemporaryProgressAndPermissionSessions)
{
    const auto path = Session().directory / "sandbox-boundaries.json";
    std::string error; std::uint32_t world_id = 0;
    SEntityHandle victim_ref, drop_ref;
    {
        CServerTestAccess fixture;
        CControllerParams params; params.Set("enable_permission", "true");
        auto* sandbox = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::sandbox, params);
        CHECK(sandbox != nullptr); if (!sandbox) return; world_id = sandbox->GetId();
        auto* player = fixture.Player(fixture.World());
        CHECK(fixture.World().TransferPlayerEntityToWorld(*player, *sandbox, florrbt::Vector2f{1000, 1000}));
        sandbox->GetController()->HandlePlayerCommand(*sandbox, *player, {"add", "Basic", "Rare", "6"});
        auto* victim = Spawn(*sandbox, EMobType::Rock);
        victim_ref = victim->GetHandle();
        victim->ApplyDamageDirect(std::min(0.01f, victim->Health() * 0.5f), player->GetEntity(), EDamageType::Normal);
        auto* drop = sandbox->InsertEntity(std::make_unique<CDrop>(sandbox, player->GetEntity()->Pos(),
            EPetalType::Basic, ERarity::Rare, static_cast<int>(player->GetId())));
        drop_ref = drop->GetHandle();
        CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    }
    CServerTestAccess restored;
    CHECK(CHotReloadSnapshotService::Restore(restored.server, path, error));
    if (!error.empty()) std::printf("sandbox restore: %s\n", error.c_str());
    auto* player = restored.server.GetNetworkModule()->FindPlayerById(1);
    auto* world = restored.server.FindWorldById(world_id);
    CHECK(player && world); if (!player || !world) return;
    CHECK(player->GetAccountName() == "runtime-player");
    CHECK(player->HasIsolatedProgress());
    auto* victim = dynamic_cast<CMobBase*>(world->ResolveEntity(victim_ref));
    auto* drop = dynamic_cast<CDrop*>(world->ResolveEntity(drop_ref));
    CHECK(victim && drop); if (!victim || !drop) return;
    CHECK(victim->GetDamageData().size() == 1);
    if (victim->GetDamageData().empty()) return;
    CHECK(victim->GetDamageData().front().ResolvePlayer(world->GameContext()) == player);
    CHECK(drop->IsOwnedBy(*player));
    CHECK(CAccountDataStore::HasItem(player->GetProgressAccountName(), static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Rare), 6));
    player->AttachSocket(sf::TcpSocket{});
    world->GetController()->HandlePlayerCommand(*world, *player, {"add", "Basic", "Rare", "1"});
    CHECK(CAccountDataStore::HasItem(player->GetProgressAccountName(), static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Rare), 7));
    world->SetController(std::make_unique<COpenController>());
    CHECK(!player->HasIsolatedProgress());
    CHECK(player->GetAccountName() == "runtime-player");
    CHECK(victim->GetDamageData().front().ResolvePlayer(world->GameContext()) == nullptr);
    CHECK(!drop->IsOwnedBy(*player));
}

TEST(DelayedRewardsAndDropsCannotCrossSandboxProgressScopes)
{
    CServerTestAccess fixture;
    auto& source = fixture.World(); auto* player = fixture.Player(source);
    auto* normal = Spawn(source, EMobType::Rock);
    normal->ApplyDamageDirect(std::min(0.01f, normal->Health() * 0.5f), player->GetEntity(), EDamageType::Normal);
    CHECK(normal->GetDamageData().size() == 1); if (normal->GetDamageData().empty()) return;
    auto* sandbox = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::sandbox);
    CHECK(sandbox); if (!sandbox) return;
    CHECK(source.TransferPlayerEntityToWorld(*player, *sandbox, florrbt::Vector2f{1000, 1000}));
    CHECK(normal->GetDamageData().front().ResolvePlayer(source.GameContext()) == nullptr);
    auto* temporary = Spawn(*sandbox, EMobType::Rock);
    temporary->ApplyDamageDirect(std::min(0.01f, temporary->Health() * 0.5f), player->GetEntity(), EDamageType::Normal);
    CHECK(temporary->GetDamageData().size() == 1); if (temporary->GetDamageData().empty()) return;
    auto* drop = static_cast<CDrop*>(sandbox->InsertEntity(std::make_unique<CDrop>(sandbox, player->GetEntity()->Pos(),
        EPetalType::Basic, ERarity::Rare, static_cast<int>(player->GetId()))));
    CHECK(drop->IsOwnedBy(*player));
    CHECK(sandbox->TransferPlayerEntityToWorld(*player, source, florrbt::Vector2f{1000, 1000}));
    CHECK(normal->GetDamageData().front().ResolvePlayer(source.GameContext()) == player);
    CHECK(temporary->GetDamageData().front().ResolvePlayer(sandbox->GameContext()) == nullptr);
    CHECK(!drop->IsOwnedBy(*player));
}

TEST(SceneInitializationIsAtomicAndRetriesBeforePublishingSnapshots)
{
    CServerTestAccess fixture;
    fixture.CheckAtomicSceneSync();
}

TEST(ReportWorkerFiltersHistoryAndDiscardsRecycledSessions)
{
    CServerTestAccess fixture;
    auto* reporter = fixture.Player(fixture.World(), 1);
    auto* target = fixture.Player(fixture.World(), 2);
    target->Authenticate("report-target");
    std::promise<void> started, release;
    auto proceed = release.get_future().share();
    std::string observed;
    CReportReviewService service([&](const std::string& transcript) {
        observed = transcript; started.set_value(); proceed.wait(); return 60.f;
    });
    SChatEntry current, previous;
    current.player_id = previous.player_id = target->GetId();
    current.system_time = previous.system_time = static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    current.sender_session = target->SessionRef(); current.message = "current-session";
    previous.sender_session = target->SessionRef(); --previous.sender_session.generation;
    previous.message = "old-session-must-not-be-reviewed";
    std::string error;
    CHECK(service.Submit(*reporter, *target, {previous, current}, error));
    CHECK(!service.Submit(*reporter, *target, {current}, error));
    CHECK(started.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(fixture.server.GetNetworkModule()->KickPlayer(2, "offline session reuse test"));
    auto* replacement = fixture.Player(fixture.World(), 2);
    replacement->Authenticate("report-target");
    release.set_value();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (service.PendingCount() && std::chrono::steady_clock::now() < deadline)
    { service.Process(fixture.server); std::this_thread::yield(); }
    CHECK(service.PendingCount() == 0);
    CHECK(!replacement->IsMuted());
    CHECK(observed.find("current-session") != std::string::npos);
    CHECK(observed.find("old-session") == std::string::npos);
}

TEST(ReportWorkerHasBoundedQueueCancellationAndJoinedShutdown)
{
    CServerTestAccess fixture;
    auto* target = fixture.Player(fixture.World());
    std::promise<void> release;
    auto proceed = release.get_future().share();
    CReportReviewService service([&](const std::string&) { proceed.wait(); return 60.f; });
    std::vector<std::unique_ptr<CPlayer>> reporters;
    std::string error;
    for (int i = 0; i < 33; ++i)
    {
        auto player = std::make_unique<CPlayer>(sf::TcpSocket{}, 100 + i, "queued");
        player->Authenticate("queued-" + std::to_string(i));
        CHECK(service.Submit(*player, *target, {}, error) == (i < 32));
        reporters.push_back(std::move(player));
    }
    CHECK(service.PendingCount() == 32);
    service.CancelPending();
    CHECK(service.PendingCount() == 0);
    release.set_value();
    service.ShutDown();
    service.Process(fixture.server);
    CHECK(!target->IsMuted());
    CHECK(!service.Submit(*reporters.front(), *target, {}, error));
}

TEST(EouBoardBorrowIsRevokedBeforeControllerDestruction)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    world.SetController(std::make_unique<CAntKingdomController>());
    auto* controller = dynamic_cast<CAntKingdomController*>(world.GetController());
    auto* entity = Spawn(world, ToMobType(EModMobType::EouWorker));
    auto* worker = dynamic_cast<IEouMob*>(entity);
    CHECK(worker && controller); if (!worker || !controller) return;
    worker->SetBoard(&controller->WallBoard());
    CHECK(worker->Board() != nullptr);
    world.SetController(std::make_unique<COpenController>());
    CHECK(worker->Board() == nullptr);
    entity->Tick(0.05f);
}

TEST(TerrainEditsAndNativeWallMessagesStayInTheirWorld)
{
    CServerTestAccess fixture;
    auto& first = *fixture.server.GetWorlds()[1];
    first.SetController(std::make_unique<CMutableTerrainController>());
    auto* second = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::mutable_terrain);
    CHECK(second); if (!second) return;
    auto* observer = fixture.Player(first, 1);
    auto* editor = fixture.Player(*second, 2);
    const auto has_terrain_message = [](const CPlayer& player) {
        for (size_t offset = 0; offset + 2 < player.m_send_buffer.size();)
        {
            const size_t length = player.m_send_buffer[offset] | (size_t(player.m_send_buffer[offset + 1]) << 8);
            if (!length || offset + length + 2 > player.m_send_buffer.size()) break;
            const auto type = static_cast<ServerMessage::Type>(player.m_send_buffer[offset + 2]);
            if (type == ServerMessage::Type::TerrainDelta || type == ServerMessage::Type::TerrainWallRemove) return true;
            offset += length + 2;
        }
        return false;
    };
    auto* flower = dynamic_cast<CPlayerFlower*>(editor->GetEntity());
    CHECK(flower); if (!flower) return;
    CHECK(flower->EquipPetal(0, FindPetalPrototype(ToPetalType(EModPetalType::Eraser)), ERarity::Common));
    flower->m_defending = true;
    const auto wall_count = first.GetMap()->walls.size();
    bool edited = false;
    for (int row = 1; row < second->GetMap()->height - 1 && !edited; ++row)
        for (int col = 1; col < second->GetMap()->width - 1 && !edited; ++col)
        {
            flower->Pos() = {float(col * second->GetMap()->tile_width), float(row * second->GetMap()->tile_height)};
            second->GetController()->OnTick(*second, 1.f);
            auto data = MakeJsonObject(); CSnapshotWriter writer(data.get()); second->GetController()->CaptureSnapshot(writer);
            edited = json_array_size(json_object_get(data.get(), "terrain_dirty")) > 0 ||
                json_array_size(json_object_get(data.get(), "terrain_wall_removes")) > 0;
        }
    CHECK(edited);
    CHECK(!has_terrain_message(*observer));
    first.GetController()->OnTick(first, 1.f);
    auto data = MakeJsonObject(); CSnapshotWriter writer(data.get()); first.GetController()->CaptureSnapshot(writer);
    CHECK(json_array_size(json_object_get(data.get(), "terrain_dirty")) == 0);
    CHECK(first.GetMap()->walls.size() == wall_count);

    CWall board; board.Reload(*second);
    const auto client_before = second->GetMap()->walls;
    bool removed = false;
    for (const auto& wall : client_before)
    {
        if (wall.max_x - wall.min_x <= 2 || wall.max_y - wall.min_y <= 2) continue;
        const int row = static_cast<int>(wall.center.y / board.TileSize());
        const int col = static_cast<int>(wall.center.x / board.TileSize());
        if (board.CommitRemove(row, col)) { removed = true; break; }
    }
    CHECK(removed);
    CHECK(!has_terrain_message(*observer));
    struct CRecorder : IPlayerSceneWriter
    {
        std::vector<std::pair<florrbt::Vector2f, florrbt::Vector2f>> rectangles;
        bool TerrainTile(std::uint8_t, std::uint16_t, std::uint16_t, std::uint32_t) override { return true; }
        bool RemoveWall(florrbt::Vector2f min, florrbt::Vector2f max) override { rectangles.emplace_back(min, max); return true; }
    } recorded;
    CHECK(board.WritePlayerScene(recorded));
    auto client_after = client_before;
    for (const auto& [min, max] : recorded.rectangles)
        std::erase_if(client_after, [&](const auto& wall) {
            return wall.max_x >= min.x && wall.min_x <= max.x && wall.max_y >= min.y && wall.min_y <= max.y;
        });
    CHECK(client_after.size() == second->GetMap()->walls.size());
    board.DetachWorld(*second);
}

TEST(SnapshotRestoreRebuildsPristineMapBeforeReplayingTerrain)
{
    CServerTestAccess fixture;
    auto& world = *fixture.server.GetWorlds()[1];
    world.SetController(std::make_unique<CMutableTerrainController>());
    const auto original_walls = world.GetMap()->walls.size();
    const auto path = Session().directory / "pristine-terrain.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    world.RemoveWallsInRect({-100000, -100000}, {100000, 100000});
    CHECK(world.GetMap()->walls.empty());
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    if (!error.empty()) std::printf("terrain restore: %s\n", error.c_str());
    CHECK(world.GetMap()->walls.size() == original_walls);
    CHECK(world.GetController()->SnapshotKey() == world_controller_keys::mutable_terrain);
}

TEST(DeathmatchAssignsAndRestoresTeamsAcrossEntryRespawnAndControllerChange)
{
    CServerTestAccess fixture;
    auto* player = fixture.Player(fixture.World());
    fixture.World().SetController(std::make_unique<CDeathmatchController>(CControllerParams{}));
    CHECK(player->GetEntity()->Team() == 0);
    CPlayerLifecycleService lifecycle;
    CHECK(lifecycle.SpawnPlayer(*player, fixture.World(), EPlayerSpawnReason::Respawn) != nullptr);
    CHECK(player->GetEntity()->Team() == 0);
    fixture.World().SetController(std::make_unique<COpenController>());
    CHECK(player->GetEntity()->Team() == game_config::mob_player_flower_team);
}

TEST(WorldSimulationDefersTransfersAndTicksMovedEntitiesOncePerFrame)
{
    CServerTestAccess fixture;
    auto& first = fixture.World();
    auto& second = *fixture.server.GetWorlds()[1];
    class CCounter final : public CMob<SMobStats>
    {
      public:
        int ticks = 0;
        explicit CCounter(CGameWorld& world) : CMob(&world, {1000, 1000}, 1.f, EMobType::Bee, ERarity::Common, SMobStats{}) {}
        void Tick(float) override { ++ticks; }
        bool CanCollide() const override { return false; }
        bool CollidesWithWalls() const override { return false; }
    };
    class CMove final : public IGameController
    {
      public:
        CPlayer& player; CGameWorld& destination; bool requested = false;
        CMove(CPlayer& p, CGameWorld& d) : player(p), destination(d) {}
        std::string_view SnapshotKey() const override { return "test.move_on_tick"; }
        void OnTick(CGameWorld& world, float) override
        {
            if (requested) return;
            requested = true;
            const auto result = world.TransferPlayerEntityToWorld(player, destination, florrbt::Vector2f{1000, 1000});
            CHECK(result.deferred && !result.committed);
            CHECK(player.GetEntity()->GameWorld() == &world);
        }
    };
    auto* player = fixture.Player(first);
    player->GetEntity()->MarkForDestroy(); first.FlushEntityRemovals();
    auto* counter = static_cast<CCounter*>(first.InsertEntity(std::make_unique<CCounter>(first)));
    player->SetOwnedEntity(counter);
    second.Tick(0.05f); // Deliberately different world-local tick counters.
    first.SetController(std::make_unique<CMove>(*player, second));
    fixture.server.GetWorldModule()->Tick(0.05f);
    CHECK(counter->ticks == 1 && counter->GameWorld() == &second);
    CHECK(counter->ActiveTickMarker() == 0);
    second.SetController(std::make_unique<CMove>(*player, first));
    fixture.server.GetWorldModule()->Tick(0.05f);
    CHECK(counter->ticks == 2 && counter->GameWorld() == &first);
    fixture.server.GetWorldModule()->Tick(0.05f);
    CHECK(counter->ticks == 3);
}

TEST(ControllerFactoryKeysAreUniqueAndRoundTrip)
{
    Session();
    std::string error;
    for (const auto& factory : SnapshotControllerFactories())
    {
        auto controller = CreateSnapshotController(factory.key);
        CHECK(controller && controller->SnapshotKey() == factory.key);
        if (!controller) continue;
        CJsonOwner data = MakeJsonObject();
        CSnapshotWriter writer(data.get());
        controller->CaptureSnapshot(writer);
        CHECK(controller->RestoreSnapshot(CSnapshotReader(data.get()), controller->SnapshotVersion(), error));
    }
    for (const auto& [type, prototype] : GetMobRegistry())
        if (prototype->m_controller_factory)
            for (ERarity rarity : {ERarity::Common, ERarity::Super, ERarity::Eternal, ERarity::Primordial})
            {
                auto controller = prototype->m_controller_factory(rarity);
                CHECK(!controller || CreateSnapshotController(controller->SnapshotKey()) != nullptr);
            }
}

TEST(CleanupDrainsOwnerDependenciesAndBrokenChains)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    CMobBase* first = Spawn(world, EMobType::Bee);
    CMobBase* second = Spawn(world, EMobType::Bee, ERarity::Common, {1100.f, 1000.f});
    auto* chain = world.InsertEntity(std::make_unique<CChainEntity>(&world, *first, *second, 100.f, 100.f, -1.f, nullptr));
    const auto chain_ref = chain->GetHandle();
    auto* shot = world.InsertEntity(std::make_unique<CMissile>(&world, first->Pos(), 2.f,
        florrbt::Vector2f{1, 0}, 0.f, 1.f, 1.f, 10.f, first));
    const auto shot_ref = shot->GetHandle();
    auto* child = world.InsertEntity(std::make_unique<CMissile>(&world, first->Pos(), 2.f,
        florrbt::Vector2f{1, 0}, 0.f, 1.f, 1.f, 10.f, shot));
    const auto child_ref = child->GetHandle();
    first->MarkForDestroy(EEntityRemovalReason::Defeated);
    world.FlushEntityRemovals();
    CHECK(world.ResolveEntity(chain_ref) == nullptr);
    CHECK(world.ResolveEntity(shot_ref) == nullptr);
    CHECK(world.ResolveEntity(child_ref) == nullptr);
    CHECK(world.GetEntityCount() == 1);
}

TEST(SnapshotAckWrapResetEvictionAndPartialQueueAreConsistent)
{
    CServerTestAccess fixture;
    fixture.CheckSnapshotProtocol();
}

TEST(DynamicRoomsRestoreIntoANewServerWithStableIdentities)
{
    const auto path = Session().directory / "rooms.json";
    std::uint32_t active_id = 0, waiting_id = 0;
    std::uint64_t generation = 0;
    SEntityHandle mob_ref;
    std::string error;
    {
        CServerTestAccess original;
        auto* module = original.server.GetWorldModule();
        auto* waiting = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 4321);
        CHECK(waiting != nullptr);
        if (!waiting) return;
        waiting_id = waiting->GetId();
        auto* active = module->CreateRoomGameWorld(4321);
        CHECK(active != nullptr);
        if (!active) return;
        active_id = active->GetId(); generation = active->GetInstanceGeneration();
        mob_ref = Spawn(*active, EMobType::Bee)->GetHandle();
        CHECK(CHotReloadSnapshotService::Capture(original.server, path, error));
    }
    CServerTestAccess restored;
    CGameWorld* main_before = restored.server.GetMainWorld();
    CHECK(CHotReloadSnapshotService::Restore(restored.server, path, error));
    if (!error.empty()) std::printf("restore: %s\n", error.c_str());
    CHECK(restored.server.GetMainWorld() == main_before);
    auto* room = restored.server.GetWorldModule()->FindRoomWorld(4321);
    CHECK(room && room->GetId() == active_id);
    if (!room) return;
    CHECK(room->GetInstanceGeneration() == generation);
    CHECK(room->ResolveEntity(mob_ref) != nullptr);
    CHECK(restored.server.FindWorldById(waiting_id) != nullptr);
}

TEST(SnapshotPreservesIntrinsicStatsRandomStreamAndAllocatorHighWater)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* summon = Spawn(world, EMobType::SummonedBeetle);
    SMobStats stats = *summon->GetBaseStats();
    stats.max_health *= 3.f; stats.damage *= 7.f;
    summon->RestoreBaseStats(stats);
    summon->Health() = 123.f;
    const auto summon_ref = summon->GetHandle();
    auto* rock = Spawn(world, EMobType::Rock);
    const auto rock_ref = rock->GetHandle();
    const float rock_max = rock->GetBaseStats()->max_health;
    auto* retired = Spawn(world, EMobType::Bee);
    const auto retired_generation = retired->Generation();
    retired->MarkForDestroy(); world.FlushEntityRemovals();
    auto expected_rng = GetRng();
    const auto path = Session().directory / "stats.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    CHECK(GetRng()() == expected_rng());
    summon = dynamic_cast<CMobBase*>(world.ResolveEntity(summon_ref));
    rock = dynamic_cast<CMobBase*>(world.ResolveEntity(rock_ref));
    CHECK(summon && rock);
    if (!summon || !rock) return;
    CHECK_NEAR(summon->GetBaseStats()->damage, stats.damage, 0.001);
    CHECK_NEAR(summon->GetBaseStats()->max_health, stats.max_health, 0.001);
    CHECK_NEAR(summon->Health(), 123.f, 0.001);
    CHECK_NEAR(rock->GetBaseStats()->max_health, rock_max, 0.001);
    CHECK(Spawn(world, EMobType::Bee)->Generation() > retired_generation);
}

TEST(LegacyLoadedMissilesBindAfterDependentObjectsExist)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* dandelion = Spawn(world, EMobType::Dandelion);
    auto* hornet = Spawn(world, EMobType::Hornet);
    dandelion->Tick(0.f); hornet->Tick(0.f);
    const auto dandelion_ref = dandelion->GetHandle(), hornet_ref = hornet->GetHandle();
    const auto path = Session().directory / "legacy-ammo.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error);
    CHECK(root != nullptr); if (!root) return;
    json_t* worlds = SavedWorlds(root.get());
    for (size_t w = 0; w < json_array_size(worlds); ++w)
    {
        json_t* entities = json_object_get(json_array_get(worlds, w), "entities");
        for (size_t e = 0; e < json_array_size(entities); ++e)
        {
            json_t* data = json_object_get(json_array_get(entities, e), "data");
            json_t* runtime = json_object_get(data, "runtime");
            if (!json_is_object(runtime)) continue;
            json_object_del(runtime, "loaded_missile_world_id");
            json_object_del(runtime, "loaded_missile_world_instance_generation");
            json_t* missiles = json_object_get(runtime, "loaded_missiles");
            for (size_t i = 0; i < json_array_size(missiles); ++i)
            {
                json_object_del(json_array_get(missiles, i), "world_id");
                json_object_del(json_array_get(missiles, i), "world_instance_generation");
            }
        }
    }
    CHECK(WriteJsonAtomically(path, root.get(), error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    dandelion = dynamic_cast<CMobBase*>(world.ResolveEntity(dandelion_ref));
    hornet = dynamic_cast<CMobBase*>(world.ResolveEntity(hornet_ref));
    CHECK(dandelion && hornet); if (!dandelion || !hornet) return;
    CHECK(dynamic_cast<IAttackableMob*>(dandelion)->TryAttack(nullptr));
    const auto count = world.GetEntityCount();
    hornet->Tick(0.f);
    CHECK(world.GetEntityCount() == count);
}

TEST(RestoredDisconnectedPlayersHaveNoHeldInput)
{
    CServerTestAccess fixture;
    auto* player = fixture.Player(fixture.World());
    auto* mob = dynamic_cast<CMobBase*>(player->GetEntity());
    auto* controller = dynamic_cast<CPlayerController*>(mob->GetController());
    ClientOperate input;
    input.type = ClientOperate::Type::Input; input.move_x = 100; input.is_attacking = true;
    controller->PushOperate(input); mob->Tick(0.05f);
    const auto path = Session().directory / "input.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    player = fixture.server.GetNetworkModule()->FindPlayerById(1);
    CHECK(player && !player->IsConnected()); if (!player) return;
    mob = dynamic_cast<CMobBase*>(player->GetEntity());
    CHECK(mob != nullptr); if (!mob) return;
    const auto position = mob->Pos();
    mob->Tick(0.05f);
    CHECK(mob->Pos() == position);
    CHECK(!dynamic_cast<IAttackableMob*>(mob)->IsAttacking());
}

TEST(DestroyingWorldMigratesPlayersAndQueuesFallbackRespawn)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    auto* room = module->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(room != nullptr); if (!room) return;
    auto* player = fixture.Player(*room);
    CHECK(module->DestroyWorldById(room->GetId()));
    CHECK(player->GetEntity() && player->GetEntity()->GameWorld() == &fixture.World());
    room = module->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(room != nullptr); if (!room) return;
    auto* removed = fixture.Player(*room, 2);
    removed->GetEntity()->MarkForDestroy();
    CHECK(module->DestroyWorldById(room->GetId()));
    CHECK(!removed->GetEntity() && removed->NeedsRespawn());
    CPlayerLifecycleService{}.RespawnDeadControlledEntities(fixture.server.GetNetworkModule()->GetPlayers(),
        fixture.World(), *fixture.server.GetNetworkModule());
    CHECK(removed->GetEntity() && !removed->NeedsRespawn());
}

TEST(PlayerHornetExecutesDashAndReleasesCarriedMobOnDeath)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* hornet = Spawn(world, EMobType::Hornet, ERarity::Eternal);
    hornet->SetController(std::make_unique<CPlayerController>());
    auto* skills = dynamic_cast<ISkillCasterMob*>(hornet);
    CHECK(skills->TryCastSkill(2, nullptr));
    const auto hornet_ref = hornet->GetHandle();
    std::string error;
    const auto path = Session().directory / "active-hornet-skill.json";
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    hornet = dynamic_cast<CMobBase*>(world.ResolveEntity(hornet_ref));
    CHECK(hornet != nullptr); if (!hornet) return;
    hornet->Tick(game_config::mob_hornet_skill_windup_time + 0.01f);
    const auto position = hornet->Pos();
    hornet->Tick(0.1f);
    CHECK(DistanceSq(hornet->Pos(), position) > 0.f);

    auto* carrier = Spawn(world, EMobType::Hornet, ERarity::Eternal, {5000.f, 5000.f});
    carrier->SetController(std::make_unique<CPlayerController>());
    auto* carried = Spawn(world, EMobType::Hornet, ERarity::Ultra, {5100.f, 5000.f});
    auto* enemy = Spawn(world, EMobType::Bee, ERarity::Common, {5400.f, 5000.f});
    enemy->Team() = 123;
    CHECK(dynamic_cast<ISkillCasterMob*>(carrier)->TryCastSkill(3, enemy));
    CHECK(carried->SkipWorldTick());
    carrier->MarkForDestroy(); world.FlushEntityRemovals();
    CHECK(!carried->SkipWorldTick());
    CHECK(!world.IsCarriedEntity(*carried));
}

TEST(LightningFindsLargeTargetByItsEdge)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* source = Spawn(world, EMobType::Bee);
    auto* target = Spawn(world, EMobType::QueenAnt, ERarity::Primordial, {2400.f, 1000.f});
    source->Team() = 123; target->Team() = 456;
    const float health = target->Health();
    world.ChainLightning(*source, health * 0.01f, 1, source);
    CHECK(target->Health() < health);
}

namespace
{
class STestBoard : public CTileBoard
{
  public:
    int Width() const override { return 20; }
    int Height() const override { return 20; }
    float TileSize() const override { return 100.f; }
    bool TileSolid(int, int col) const override { return col == 7; }
    bool HasTile(int row, int col) const override { return TileSolid(row, col); }
    bool CommitRemove(int, int, int) override { return false; }
    bool CommitPlace(int, int, std::uint32_t) override { return false; }
    void CaptureSnapshot(CSnapshotWriter&) const override {}
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string&) override { return true; }
    void Reload(CGameWorld&) override {}
};
class STestForager : public CEOUMob<SMobStats>
{
  public:
    using CEOUMob<SMobStats>::CEOUMob;
    float DepositFood(float) override { return 0.f; }
    void Scan() { EouFoodTick(100.f, 1.f); }
};
}

TEST(FoodSearchAdvancesPastFourUnreachableCandidates)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    STestBoard board;
    SMobStats stats; stats.horizon = 2000.f;
    STestForager worker(&world, {550.f, 550.f}, 10.f, ToMobType(EModMobType::EouWorker), ERarity::Common, stats);
    worker.SetBoard(&board);
    for (int i = 0; i < 5; ++i)
    {
        auto* food = dynamic_cast<CEouFoodMob*>(Spawn(world, ToMobType(EModMobType::EouFood), ERarity::Common,
              i == 4 ? florrbt::Vector2f{50.f, 550.f} : florrbt::Vector2f{850.f, 500.f + i * 30.f}));
        CHECK(food != nullptr);
        if (food) food->SetFoodTotal(100.f);
    }
    worker.Scan(); CHECK(!worker.HasTarget());
    worker.Scan(); CHECK(worker.HasTarget());
    worker.SetTarget({850.f, 550.f});
    CHECK(worker.HasTarget()); // failed replacement preserves the previous path
}

TEST(SemanticRestoreFailureRollsBackWorldTopologyAndLiveState)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* mob = Spawn(world, EMobType::Bee);
    const auto ref = mob->GetHandle();
    auto* module = fixture.server.GetWorldModule();
    auto* room = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 5432);
    CHECK(room != nullptr); if (!room) return;
    const auto room_id = room->GetId();
    const auto path = Session().directory / "bad-semantic.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error);
    CHECK(root != nullptr); if (!root) return;
    json_t* first_world = json_array_get(SavedWorlds(root.get()), 0);
    json_t* entity = json_array_get(json_object_get(first_world, "entities"), 0);
    json_object_set_new(json_object_get(entity, "data"), "mob_type", json_integer(999));
    CHECK(WriteJsonAtomically(path, root.get(), error));
    CHECK(module->DestroyWorldById(room_id));
    mob->Health() = 77.f;
    CHECK(!CHotReloadSnapshotService::Restore(fixture.server, path, error));
    CHECK(!error.empty());
    CHECK(module->FindRoomWorld(5432) == nullptr);
    CHECK(module->FindWorldById(room_id) == nullptr);
    CHECK(fixture.server.GetMainWorld() == &world);
    mob = dynamic_cast<CMobBase*>(world.ResolveEntity(ref));
    CHECK(mob != nullptr);
    if (mob) CHECK_NEAR(mob->Health(), 77.f, 0.001);
}

TEST(PlayerHornetSummonProducesActualMobs)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    world.RemoveWallsInRect({-1.e8f, -1.e8f}, {1.e8f, 1.e8f});
    auto* caster = Spawn(world, EMobType::Hornet, ERarity::Eternal);
    caster->SetController(std::make_unique<CPlayerController>());
    Spawn(world, EMobType::PlayerFlower, ERarity::Common, {1100.f, 1000.f});
    const auto minimum = game_config::mob_hornet_skill1_min_nearby_players;
    game_config::mob_hornet_skill1_min_nearby_players = 0;
    CHECK(dynamic_cast<ISkillCasterMob*>(caster)->TryCastSkill(1, nullptr));
    game_config::mob_hornet_skill1_min_nearby_players = minimum;
    caster->Tick(game_config::mob_hornet_skill_windup_time + 0.01f);
    int hornets = 0;
    world.ForEachEntity([&](CEntity* entity) {
        if (auto* mob = dynamic_cast<CMobBase*>(entity); mob && mob->GetMobType() == EMobType::Hornet) ++hornets;
    });
    CHECK(hornets > 1);
}

TEST(LegacyHornetControllerActionsMigrateIntoMobRuntime)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* mob = Spawn(world, EMobType::Hornet, ERarity::Eternal);
    const auto ref = mob->GetHandle();
    CHECK(mob->GetController()->SnapshotKey() == "controller.hornet_special");
    CHECK(dynamic_cast<ISkillCasterMob*>(mob)->TryCastSkill(2, nullptr));
    const auto path = Session().directory / "legacy-hornet-actions.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error);
    CHECK(root != nullptr); if (!root) return;
    json_t* first_world = json_array_get(SavedWorlds(root.get()), 0);
    json_t* data = json_object_get(json_array_get(json_object_get(first_world, "entities"), 0), "data");
    json_t* runtime = json_object_get(data, "runtime");
    json_t* controller = json_object_get(json_object_get(data, "controller"), "data");
    CHECK(json_object_update(controller, json_object_get(runtime, "special_actions")) == 0);
    json_object_del(runtime, "special_actions");
    json_object_set_new(data, "runtime_version", json_integer(1));
    CHECK(WriteJsonAtomically(path, root.get(), error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    mob = dynamic_cast<CMobBase*>(world.ResolveEntity(ref));
    CHECK(mob != nullptr); if (!mob) return;
    CHECK(dynamic_cast<ISkillCasterMob*>(mob)->IsSkillBusy());
    mob->Tick(game_config::mob_hornet_skill_windup_time + 0.01f);
    const auto position = mob->Pos();
    mob->Tick(0.1f);
    CHECK(DistanceSq(position, mob->Pos()) > 0.f);
}

TEST(RuntimeSnapshotsRejectUnregisteredSpawnEnums)
{
    Session();
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter writer(data.get());
    writer.Field("summon_type", 63); // gap between native and mod mobs
    CFireflyController controller;
    std::string error;
    CHECK(!controller.RestoreSnapshot(CSnapshotReader(data.get()), 1, error));
    CGameWorld world("data/maps/test.tmj");
    auto hole = CreateMob(EMobType::AntHole, &world, {}, ERarity::Common);
    CHECK(hole != nullptr); if (!hole) return;
    CJsonOwner queue = MakeJsonArray(), entry = MakeJsonObject();
    CSnapshotWriter item(entry.get());
    item.Field("type", 999); item.Field("rarity", 1);
    AppendJson(queue.get(), entry.release());
    writer.Node("spawn_queue", queue.release());
    CHECK(!hole->RestoreRuntimeSnapshot(CSnapshotReader(data.get()), 1, error));
}

TEST(TopologyRejectsMainWorldOwnershipAndSharedRoomWorldsBeforeMutation)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    CEntity* sentinel = Spawn(fixture.World(), EMobType::Bee);
    const SEntityHandle sentinel_ref = sentinel->GetHandle();
    const auto original = module->CaptureTopology();
    CHECK(original.worlds.size() >= 3);
    if (original.worlds.size() < 3) return;
    std::string error;

    auto bad = original;
    bad.rooms.push_back({6100, original.main_world, IWorldModule::SWorldRef::invalid_id, 0.f});
    CHECK(!module->RestoreTopology(bad, error));
    CHECK(fixture.World().ResolveEntity(sentinel_ref) == sentinel);
    bad = original;
    bad.rooms.push_back({6100, original.worlds[1].id, original.main_world, 0.f});
    CHECK(!module->ValidateTopology(bad, error));
    bad = original;
    bad.rooms.push_back({6100, original.worlds[1].id, original.worlds[2].id, 0.f});
    bad.rooms.push_back({6101, original.worlds[2].id, IWorldModule::SWorldRef::invalid_id, 0.f});
    CHECK(!module->ValidateTopology(bad, error));
    bad = original;
    bad.rooms.push_back({6100, original.worlds[1].id, original.worlds[1].id, 0.f});
    CHECK(module->ValidateTopology(bad, error)); // an unstarted room legitimately uses one world
}

TEST(EmptyWaitingWorldRetiresDuringAnOccupiedGame)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    auto* waiting = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 6102);
    CHECK(waiting != nullptr); if (!waiting) return;
    const auto waiting_id = waiting->GetId();
    auto* game = module->CreateRoomGameWorld(6102);
    CHECK(game != nullptr); if (!game) return;
    CPlayer* player = fixture.Player(*game);
    CEntity* controlled = player->GetEntity();
    module->Tick(0.f); // exercise lifecycle only, without game/controller simulation
    CHECK(module->FindWorldById(waiting_id) == nullptr);
    CHECK(module->FindRoomWorld(6102) == game);
    CHECK(player->GetEntity() == controlled);

    waiting = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 6103);
    CHECK(waiting != nullptr); if (!waiting) return;
    const auto occupied_waiting_id = waiting->GetId();
    game = module->CreateRoomGameWorld(6103);
    CHECK(game != nullptr); if (!game) return;
    fixture.Player(*waiting, 2);
    module->Tick(0.f);
    CHECK(module->FindWorldById(occupied_waiting_id) == waiting);
}

TEST(DeletingGameWorldAlsoClosesItsWaitingWorldAndMigratesPlayers)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    auto* waiting = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 6104);
    CHECK(waiting != nullptr); if (!waiting) return;
    const auto waiting_id = waiting->GetId();
    auto* game = module->CreateRoomGameWorld(6104);
    CHECK(game != nullptr); if (!game) return;
    const auto game_id = game->GetId();
    CPlayer* waiting_player = fixture.Player(*waiting);
    CHECK(module->DestroyWorldById(game_id));
    CHECK(module->FindWorldById(game_id) == nullptr);
    CHECK(module->FindWorldById(waiting_id) == nullptr);
    CHECK(module->FindRoomWorld(6104) == nullptr);
    CHECK(waiting_player->GetEntity() && waiting_player->GetEntity()->GameWorld() == &fixture.World());
    CHECK(module->CaptureTopology().rooms.empty());
    CHECK(module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::sandbox, 6104) != nullptr);
}

TEST(CompoundWorldTransferMovesTheWholeAssemblyFromRootOrBody)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 100), target("data/maps/test.tmj", 101);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "compound-transfer");
    player.Authenticate("compound-transfer");
    auto* root = Spawn(source, EMobType::FireflyLarva);
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root);
    CHECK(assembly && !assembly->GetSegmentReferences().empty());
    if (!assembly || assembly->GetSegmentReferences().empty()) return;
    const auto references = assembly->GetSegmentReferences();
    const auto root_ref = root->GetHandle();
    player.SetOwnedEntity(root);
    const auto source_count = source.GetEntityCount();
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(player.GetEntity() == root && source.ResolveEntity(root_ref) == nullptr);
    CHECK(source.GetEntityCount() == 0 && target.GetEntityCount() == source_count);
    for (const auto& reference : references) CHECK(source.ResolveEntity(reference.entity_ref) == nullptr);
    for (const auto& reference : assembly->GetSegmentReferences())
    {
        auto* body = dynamic_cast<IMultiSegmentBodyMob*>(target.ResolveEntity(reference.entity_ref));
        CHECK(body && body->RootHandle().RefersTo(root->GetHandle()));
    }
    CEntity* body = target.ResolveEntity(assembly->GetSegmentReferences().front().entity_ref);
    player.SetOwnedEntity(body);
    CHECK(target.TransferPlayerEntityToWorld(player, source, florrbt::Vector2f{800.f, 800.f}));
    CHECK(player.GetEntity() == body && root->GameWorld() == &source);
    CHECK(source.GetEntityCount() == source_count && target.GetEntityCount() == 0);
    for (const auto& reference : assembly->GetSegmentReferences())
        CHECK(source.ResolveEntity(reference.entity_ref) != nullptr);
}

TEST(WorldTransferCancelsSpatialSkillsAndQueuedInputWithoutResettingCooldowns)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 102), target("data/maps/test.tmj", 103);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "skill-transfer");
    player.Authenticate("skill-transfer");
    auto* hornet = Spawn(source, EMobType::Hornet, ERarity::Eternal);
    hornet->SetController(std::make_unique<CPlayerController>());
    player.SetOwnedEntity(hornet);
    auto* skills = dynamic_cast<ISkillCasterMob*>(hornet);
    CHECK(skills->TryCastSkill(2, nullptr));
    hornet->Tick(game_config::mob_hornet_skill_windup_time + 0.01f);
    ClientOperate input; input.type = ClientOperate::Type::Input; input.move_x = 100;
    dynamic_cast<CPlayerController*>(hornet->GetController())->PushOperate(input);
    const florrbt::Vector2f arrival{8000.f, 8000.f};
    CHECK(source.TransferPlayerEntityToWorld(player, target, arrival));
    CHECK(player.GetEntity() == hornet);
    CHECK(!hornet->IsMovementControlledBySkill() && !skills->IsSkillBusy());
    hornet->Tick(0.05f);
    CHECK(hornet->Pos() == arrival);

    auto* crab = Spawn(source, EMobType::Crab);
    crab->SetController(std::make_unique<CPlayerController>());
    player.SetOwnedEntity(crab);
    auto* crab_skills = dynamic_cast<ISkillCasterMob*>(crab);
    CHECK(crab_skills->TryCastSkill(0, nullptr));
    CHECK(source.TransferPlayerEntityToWorld(player, target, arrival));
    CHECK(player.GetEntity() == crab);
    CHECK(!crab_skills->IsSkillBusy());
    CHECK(!crab_skills->CanCastSkill(0)); // transfer must not bypass the charged cooldown
    crab->Tick(0.05f);
    CHECK(crab->Pos() == arrival);
}

namespace
{
class SDestructiveLeaveController : public IGameController
{
  public:
    explicit SDestructiveLeaveController(int* calls = nullptr, bool remove_now = true)
        : m_calls(calls), m_remove_now(remove_now) {}
    void OnTick(CGameWorld&, float) override {}
    std::string_view SnapshotKey() const override { return "test.destructive_leave"; }
    void OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing) override
    {
        if (m_calls) ++*m_calls;
        if (CEntity* entity = player.GetEntity())
        {
            CGameWorld* owner_world = entity->GameWorld();
            entity->MarkForDestroy();
            if (m_remove_now) owner_world->FlushEntityRemovals();
        }
    }
  private:
    int* m_calls;
    bool m_remove_now;
};
class SRejectArrivalController : public IGameController
{
  public:
    void OnTick(CGameWorld&, float) override {}
    std::string_view SnapshotKey() const override { return "test.reject_arrival"; }
    bool ValidatePlayerTransition(const SPlayerTransitionView&, EPlayerTransitionSide side, std::string& error) const override
    {
        if (side != EPlayerTransitionSide::Arrival) return true;
        error = "Destination refused during preflight"; return false;
    }
};
}

TEST(TransferPreflightFailureHasNoDepartureEffectsOrReservedIds)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 104), target("data/maps/test.tmj", 105);
    int leave_calls = 0;
    source.SetController(std::make_unique<SDestructiveLeaveController>(&leave_calls));
    target.SetController(std::make_unique<SRejectArrivalController>());
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "leave-hook");
    player.Authenticate("leave-hook");
    auto* original = Spawn(source, EMobType::Bee); player.SetOwnedEntity(original);
    const auto result = source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f});
    CHECK(!result && !result.error.empty());
    CHECK(player.GetEntity() == original && !original->IsMarkedForDes() && target.GetEntityCount() == 0);
    CHECK(leave_calls == 0);
    CHECK(Spawn(target, EMobType::Bee)->Id() == 0);
}

TEST(IntrinsicArmorRoundTripPreservesSignedDamageSemantics)
{
    Session();
    SMobStats stats; stats.max_health = 10.f; stats.armor = -3.f;
    CJsonOwner data = CaptureMobBaseStats(stats);
    const SMobStats restored = ReadMobBaseStats(CSnapshotReader(data.get()), SMobStats{});
    CHECK_NEAR(restored.armor, -3.f, 0.0001);
    CGameWorld world("data/maps/test.tmj", 106);
    world.SetController(nullptr);
    CMob<SMobStats> mob(&world, {500.f, 500.f}, 10.f, EMobType::Bee, ERarity::Common, restored);
    mob.TakeDamage(1.f, nullptr, EDamageType::Normal);
    CHECK_NEAR(mob.Health(), 6.f, 0.0001);
}

TEST(WorldDeletionDoesNotRepeatLeaveHooksOrUseTheirDeletedEntity)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    std::uint32_t player_id = 1;
    for (bool remove_now : {false, true})
    {
        auto* world = module->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
        CHECK(world != nullptr); if (!world) return;
        int leave_calls = 0;
        world->SetController(std::make_unique<SDestructiveLeaveController>(&leave_calls, remove_now));
        CPlayer* player = fixture.Player(*world, player_id++);
        const auto world_id = world->GetId();
        CHECK(module->DestroyWorldById(world_id));
        CHECK(leave_calls == 1);
        fixture.World().FlushEntityRemovals();
        CHECK(!player->GetEntity() && (player->NeedsRespawn() || player->HasOwnedEntity()));
    }
}

TEST(LeavingPvpRestoresThePlayerFlowerCooperativeTeam)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 107), target("data/maps/test.tmj", 108);
    source.SetController(CreateWorldController(world_controller_keys::pvp));
    target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "pvp-transfer");
    player.Authenticate("pvp-transfer");
    auto* flower = Spawn(source, EMobType::PlayerFlower);
    player.SetOwnedEntity(flower);
    source.GetController()->OnPlayerArrivalCommitted(source, player);
    CHECK(flower->Team() != game_config::mob_player_flower_team);
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(player.GetEntity() == flower);
    CHECK(flower->Team() == game_config::mob_player_flower_team);
}

TEST(DandelionTransferPreservesRemainingAmmoAndDoesNotRefillSpentAmmo)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 109), target("data/maps/test.tmj", 110);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "dandelion-transfer"); player.Authenticate("dandelion-transfer");
    auto* dandelion = Spawn(source, EMobType::Dandelion); player.SetOwnedEntity(dandelion);
    dandelion->Tick(0.f);
    std::vector<CDandelionMissile*> missiles;
    source.ForEachEntity([&](CEntity* entity) {
        if (auto* missile = dynamic_cast<CDandelionMissile*>(entity); missile && missile->GetOwner() == dandelion)
            missiles.push_back(missile);
    });
    CHECK(missiles.size() > 1); if (missiles.size() <= 1) return;
    auto* fired = missiles.front();
    CHECK(fired->Fire({1.f, 0.f}, 100.f, 1.f));
    const auto owner_ref = dandelion->GetHandle();
    const float health = missiles.back()->Health();
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(player.GetEntity() == dandelion && source.ResolveEntity(owner_ref) == nullptr);
    CHECK(fired->IsMarkedForDes()); // fired projectiles retain their old cleanup policy
    for (size_t i = 1; i < missiles.size(); ++i)
        CHECK(missiles[i]->GameWorld() == &target && missiles[i]->GetOwner() == dandelion && missiles[i]->IsAttachedToOwner());
    CHECK_NEAR(missiles.back()->Health(), health, 0.0001);
    source.FlushEntityRemovals(); CHECK(source.GetEntityCount() == 0);
    CHECK(target.GetEntityCount() == missiles.size());
    dandelion->Tick(0.f); CHECK(target.GetEntityCount() == missiles.size());
    CHECK(dynamic_cast<IAttackableMob*>(dandelion)->TryAttack(nullptr));
    for (size_t i = 0; i <= missiles.size(); ++i) dandelion->Tick(1.f);
    CHECK(target.TransferPlayerEntityToWorld(player, source, florrbt::Vector2f{800.f, 800.f}));
    target.FlushEntityRemovals(); CHECK(target.GetEntityCount() == 0);
    dandelion->Tick(0.f); CHECK(source.GetEntityCount() == 1);
}
TEST(DandelionTransferredBeforeAmmoSpawnInitializesOnlyInDestination)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 111), target("data/maps/test.tmj", 112);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "dandelion-unloaded-transfer");
    player.Authenticate("dandelion-unloaded-transfer");
    auto* dandelion = Spawn(source, EMobType::Dandelion);
    player.SetOwnedEntity(dandelion);

    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    dandelion->Tick(0.f);
    CHECK(source.GetEntityCount() == 0 && target.GetEntityCount() > 1);
    target.ForEachEntity([&](CEntity* entity) {
        if (auto* missile = dynamic_cast<CDandelionMissile*>(entity))
            CHECK(missile->GetOwner() == dandelion && missile->IsAttachedToOwner());
    });

}

TEST(PossessionRejectsOtherPlayersAndKeepsSameWorldMembership)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* network = fixture.server.GetNetworkModule();
    auto* first = fixture.Player(world, 1);
    auto* second = fixture.Player(world, 2);
    CEntity* original = first->GetEntity();
    auto* occupied = dynamic_cast<CMobBase*>(second->GetEntity());
    IController* occupied_controller = occupied->GetController();
    CHECK(!network->AssignPlayerEntity(first->GetId(), world, occupied->Id()));
    CHECK(first->GetEntity() == original && second->GetEntity() == occupied);
    CHECK(occupied->GetController() == occupied_controller);
    // Keep the remainder independent if an old implementation stole control.
    first->SetOwnedEntity(original);
    auto* removed = Spawn(world, EMobType::Bee);
    removed->MarkForDestroy();
    CHECK(!network->AssignPlayerEntity(first->GetId(), world, removed->Id()));
    first->SetOwnedEntity(original);

    world.SetController(CreateWorldController(world_controller_keys::pvp));
    auto* pvp = dynamic_cast<CAntHelPvpController*>(world.GetController());
    pvp->OnPlayerArrivalCommitted(world, *first);
    const int team = pvp->PlayerTeam(first->GetId());
    first->m_cp_stack.push_back({123, 1, 2});
    auto* target = Spawn(world, EMobType::Bee);
    CHECK(network->AssignPlayerEntity(first->GetId(), world, target->Id()));
    CHECK(first->GetEntity() == target);
    CHECK(pvp->PlayerTeam(first->GetId()) == team && target->Team() == team);
    CHECK(first->m_cp_stack.size() == 1);
    IController* controller = target->GetController();
    CHECK(network->AssignPlayerEntity(first->GetId(), world, target->Id()));
    CHECK(target->GetController() == controller);
}

TEST(StandaloneSnapshotBuildDoesNotRetainPreviousWorldState)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 113);
    world.SetController(nullptr); world.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "snapshot-cache");
    player.Authenticate("snapshot-cache");
    auto* owner = Spawn(world, EMobType::Bee);
    player.SetOwnedEntity(owner);
    CSnapshotService service;
    CSnapshotService::SBuildResult first, second;
    CHECK(service.BuildSnapshot(player, 42, first));
    owner->Pos().x += 100.f;
    CHECK(service.BuildSnapshot(player, 42, second));
    CHECK(second.message.entities.front().pos == owner->Pos());
    CHECK(second.message.entities.front().pos != first.message.entities.front().pos);
}

TEST(SnapshotMandatoryOwnerIsUniqueAndRemovedOwnersAreRejected)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 114);
    world.SetController(nullptr); world.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "snapshot-titan");
    player.Authenticate("snapshot-titan");
    auto* owner = Spawn(world, EMobType::Titan);
    player.SetOwnedEntity(owner);
    CSnapshotService service;
    CSnapshotService::SBuildResult result;
    CHECK(service.BuildSnapshot(player, 43, result));
    CHECK(result.message.entities.size() == 1);
    CHECK(result.visible_count == 1);
    owner->MarkForDestroy();
    CHECK(!service.BuildSnapshot(player, 44, result));
}

namespace
{
class SMutatingMembershipController : public IGameController
{
  public:
    std::function<void(CGameWorld&, CPlayer&)> on_enter;
    std::function<void(CGameWorld&, CPlayer&)> on_leave;
    std::function<bool(const SPlayerTransitionView&, EPlayerTransitionSide, std::string&)> preflight;
    void OnTick(CGameWorld&, float) override {}
    std::string_view SnapshotKey() const override { return "test.mutating_membership"; }
    bool ValidatePlayerTransition(const SPlayerTransitionView& view, EPlayerTransitionSide side, std::string& error) const override
    { return !preflight || preflight(view, side, error); }
    void OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player) override
    {
        if (on_enter) on_enter(world, player);
    }
    void OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* departing) override
    {
        if (on_leave) on_leave(world, player);
    }
};
}

TEST(SpawnArrivalRemovalDoesNotLeaveEnvironmentSynchronizationWithDanglingActor)
{
    class CRemoveSpawn final : public IGameController
    {
      public:
        void OnTick(CGameWorld&, float) override {}
        std::string_view SnapshotKey() const override { return "test.remove_spawn"; }
        void OnPlayerEntityReady(CGameWorld& world, CPlayer&, CPlayerFlower& flower, EPlayerSpawnReason) override
        {
            flower.MarkForDestroy();
            world.FlushEntityRemovals();
        }
    };
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    world.SetController(std::make_unique<CRemoveSpawn>());
    CPlayerLifecycleService lifecycle;
    CHECK(lifecycle.SpawnPlayer(*player, world, EPlayerSpawnReason::Respawn) == nullptr);
    CHECK(player->GetEntity() == nullptr);
}

TEST(PossessionPreflightIsSideEffectFreeAndCommitClearsOnlyCrossWorldCheckpoints)
{
    CServerTestAccess fixture;
    auto& source = fixture.World();
    auto* target = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(target != nullptr); if (!target) return;
    target->SetController(nullptr); target->ClearEntitiesForRestore();
    auto* player = fixture.Player(source);
    CEntity* original = player->GetEntity();
    auto* network = fixture.server.GetNetworkModule();
    for (bool remove_now : {false, true})
    {
        auto* candidate = Spawn(*target, EMobType::Bee);
        const auto candidate_ref = candidate->GetHandle();
        IController* old_controller = candidate->GetController();
        auto hook = std::make_unique<SMutatingMembershipController>();
        hook->preflight = [remove_now](const SPlayerTransitionView&, EPlayerTransitionSide, std::string& error) {
            if (remove_now) throw std::runtime_error("preflight failure");
            error = "preflight rejected"; return false;
        };
        hook->on_leave = [&, candidate_ref, remove_now](CGameWorld&, CPlayer&) {
            target->ResolveEntity(candidate_ref)->MarkForDestroy();
            if (remove_now) target->FlushEntityRemovals();
        };
        // Installing a probe is not itself the transition under test. Live
        // controller replacement now correctly dispatches departure/arrival.
        source.SetController(std::move(hook), false);
        CHECK(!network->AssignPlayerEntity(player->GetId(), *target, candidate_ref.id));
        CHECK(player->GetEntity() == original);
        CHECK(candidate->GetController() == old_controller && !candidate->IsMarkedForDes());
        candidate->MarkForDestroy();
        target->FlushEntityRemovals();
        CHECK(target->ResolveEntity(candidate_ref) == nullptr);
    }
    source.SetController(nullptr, false);
    player->m_cp_stack.push_back({123, 1, 2});
    auto enter_hook = std::make_unique<SMutatingMembershipController>();
    enter_hook->on_enter = [](CGameWorld& world, CPlayer& entering) {
        entering.GetEntity()->MarkForDestroy();
        world.FlushEntityRemovals();
    };
    target->SetController(std::move(enter_hook));
    auto* candidate = Spawn(*target, EMobType::Bee);
    CHECK(network->AssignPlayerEntity(player->GetId(), *target, candidate->Id()));
    CHECK(player->GetEntity() == nullptr && player->HasOwnedEntity());
    CHECK(player->m_cp_stack.empty());
    target->SetController(nullptr);
    player->SetOwnedEntity(original);
    player->m_cp_stack.push_back({123, 1, 2});
    candidate = Spawn(*target, EMobType::Bee);
    CHECK(network->AssignPlayerEntity(player->GetId(), *target, candidate->Id()));
    CHECK(player->GetEntity() == candidate && player->m_cp_stack.empty());
    auto death_hook = std::make_unique<SMutatingMembershipController>();
    death_hook->on_enter = [](CGameWorld&, CPlayer& entering) {
        dynamic_cast<CPlayerFlower*>(entering.GetEntity())->EnterDeathState();
    };
    target->SetController(std::move(death_hook), false);
    auto* dying_flower = Spawn(*target, EMobType::PlayerFlower);
    CHECK(network->AssignPlayerEntity(player->GetId(), *target, dying_flower->Id()));
    CHECK(player->GetEntity() == dying_flower && dying_flower->IsDead());
}

TEST(CommittedArrivalMayRemoveTheEntityWithoutReturningADanglingPointer)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 115), target("data/maps/test.tmj", 116);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "enter-hook-transfer");
    player.Authenticate("enter-hook-transfer");
    auto controller = std::make_unique<SMutatingMembershipController>();
    controller->on_enter = [](CGameWorld& world, CPlayer& entering) {
        entering.GetEntity()->MarkForDestroy();
        world.FlushEntityRemovals();
    };
    target.SetController(std::move(controller));
    player.SetOwnedEntity(Spawn(source, EMobType::Bee));
    const auto committed = source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f});
    CHECK(committed && !committed.entity.IsValid());
    CHECK(player.GetEntity() == nullptr && player.HasOwnedEntity());
    CHECK(source.GetEntityCount() == 0 && target.GetEntityCount() == 0);
    auto death_hook = std::make_unique<SMutatingMembershipController>();
    death_hook->on_enter = [](CGameWorld&, CPlayer& entering) {
        dynamic_cast<CPlayerFlower*>(entering.GetEntity())->EnterDeathState();
    };
    target.SetController(std::move(death_hook));
    auto* dying_flower = Spawn(source, EMobType::PlayerFlower);
    player.SetOwnedEntity(dying_flower);
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(player.GetEntity() == dying_flower && dying_flower->IsDead());
}

TEST(DroppingPlayerDoesNotReuseEntityDeletedByLeaveHook)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    const auto entity_ref = player->GetEntity()->GetHandle();
    int calls = 0;
    world.SetController(std::make_unique<SDestructiveLeaveController>(&calls, true));
    CHECK(fixture.server.GetNetworkModule()->KickPlayer(player->GetId(), "offline regression"));
    CHECK(calls == 1 && world.ResolveEntity(entity_ref) == nullptr);
    CHECK(fixture.server.GetNetworkModule()->GetPlayers().empty());
}

TEST(SnapshotBatchesShareAnImmutablePassAndReleaseCachedPointers)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 117);
    world.SetController(nullptr); world.ClearEntitiesForRestore();
    CPlayer first(sf::TcpSocket{}, 1, "snapshot-batch-first"), second(sf::TcpSocket{}, 2, "snapshot-batch-second");
    first.Authenticate("snapshot-batch-first"); second.Authenticate("snapshot-batch-second");
    auto* first_mob = Spawn(world, EMobType::Bee);
    auto* second_mob = Spawn(world, EMobType::Bee);
    first.SetOwnedEntity(first_mob); second.SetOwnedEntity(second_mob);
    CSnapshotService service;
    CSnapshotService::SBuildResult first_result, second_result;
    {
        CSnapshotService::CBuildBatch batch;
        CHECK(service.BuildSnapshot(first, 0, first_result, batch));
        CHECK(service.BuildSnapshot(second, 0, second_result, batch));
        CHECK(first_result.message.entities.size() == 2 && second_result.message.entities.size() == 2);
        for (const auto& entity : second_result.message.entities)
            CHECK(((entity.flags & static_cast<std::uint16_t>(ServerEntityFlag::Owner)) != 0) ==
                  (entity.entity_id == static_cast<net_entity_id>(second_mob->Id())));
    }
    first_mob->Pos().x += 10.f;
    CSnapshotService::CBuildBatch next_batch;
    CHECK(service.BuildSnapshot(first, 0, first_result, next_batch));
    CHECK(first_result.message.entities.front().pos == first_mob->Pos());
}

TEST(SparseRestoredPlayerIdsDoNotAllocateEveryMissingId)
{
    CServerTestAccess fixture;
    fixture.CheckSparsePlayerIds();
}

TEST(SnapshotPreflightRejectsDuplicateControlAndUnallocatablePlayerIds)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* first = fixture.Player(world, 1);
    auto* second = fixture.Player(world, 2);
    CEntity* first_owner = first->GetEntity();
    CEntity* second_owner = second->GetEntity();
    const auto path = Session().directory / "duplicate-control.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto original = LoadJsonFile(path, error);
    CHECK(original != nullptr); if (!original) return;
    for (bool duplicate_control : {true, false})
    {
        CJsonOwner data(json_deep_copy(original.get()));
        json_t* players = json_object_get(json_object_get(json_object_get(data.get(), "sections"), "players"), "data");
        CHECK(json_array_size(players) == 2); if (json_array_size(players) != 2) return;
        json_t* changed = json_array_get(players, 1);
        if (duplicate_control)
            for (const char* key : {"entity_id", "entity_generation", "world_id", "world_instance_generation"})
                json_object_set(changed, key, json_object_get(json_array_get(players, 0), key));
        else
            json_object_set_new(changed, "player_id", json_integer(std::numeric_limits<int>::max()));
        CHECK(WriteJsonAtomically(path, data.get(), error));
        CHECK(!CHotReloadSnapshotService::Restore(fixture.server, path, error));
        CHECK(!error.empty());
        auto* current_first = fixture.server.GetNetworkModule()->FindPlayerById(1);
        auto* current_second = fixture.server.GetNetworkModule()->FindPlayerById(2);
        CHECK(current_first == first && current_second == second);
        CHECK(current_first && current_first->GetEntity() == first_owner);
        CHECK(current_second && current_second->GetEntity() == second_owner);
    }
}

TEST(SnapshotsKeepDeadPlayerFlowersVisibleUntilRemoval)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 118);
    world.SetController(nullptr); world.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "dead-owner-snapshot");
    player.Authenticate("dead-owner-snapshot");
    auto* owner = dynamic_cast<CPlayerFlower*>(Spawn(world, EMobType::PlayerFlower));
    auto* other = dynamic_cast<CPlayerFlower*>(Spawn(world, EMobType::PlayerFlower));
    player.SetOwnedEntity(owner);
    owner->EnterDeathState(); other->EnterDeathState();
    CHECK(owner->IsDead() && other->IsDead() && !owner->IsMarkedForDes());
    CSnapshotService service;
    CSnapshotService::SBuildResult result;
    CHECK(service.BuildSnapshot(player, 45, result));
    CHECK(result.message.entities.size() == 2);
    for (const auto& entity : result.message.entities)
        CHECK((entity.flags & static_cast<std::uint16_t>(ServerEntityFlag::Dead)) != 0);
}

namespace
{
struct SExtensionTestHost : IPlayerDirectory, IChatDelivery
{
    std::vector<std::unique_ptr<CPlayer>> players;
    std::vector<std::pair<std::uint32_t, SChatEntry>> delivered;
    CExtensionRuntime runtime;
    explicit SExtensionTestHost(std::function<void(CExtensionRuntime&)> configure = {})
    {
        Session(); std::string error;
        if (configure) configure(runtime);
        CHECK(runtime.Init(*this, *this, error));
    }
    const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const override { return players; }
    CPlayer* FindPlayerById(std::uint32_t id) const override
    {
        for (const auto& player : players) if (player->GetId() == id) return player.get();
        return nullptr;
    }
    bool DeliverChat(CPlayer& player, const SChatEntry& entry) override
    { delivered.emplace_back(player.GetId(), entry); return true; }
    CPlayer& Player(CGameWorld& world, std::uint32_t id, const std::string& name)
    {
        auto player = std::make_unique<CPlayer>(sf::TcpSocket{}, id, name);
        player->Authenticate(name); player->SetOwnedEntity(Spawn(world, EMobType::Bee));
        players.push_back(std::move(player)); return *players.back();
    }
    std::vector<std::uint32_t> Recipients() const
    {
        std::vector<std::uint32_t> ids;
        for (const auto& [id, entry] : delivered) ids.push_back(id);
        std::sort(ids.begin(), ids.end()); return ids;
    }
};
}

TEST(ExtensionsShareGlobalWeatherButIsolateWorldSquadsAndStaleViews)
{
    SExtensionTestHost host;
    CGameWorld first("data/maps/test.tmj", 201), second("data/maps/test.tmj", 202);
    first.SetController(nullptr); second.SetController(nullptr);
    auto a = host.runtime.AttachWorld(first, {true, false, false});
    auto b = host.runtime.AttachWorld(second, {true, false, false});
    CHECK(a.Find<IWeatherDiurnalView>() == b.Find<IWeatherDiurnalView>());
    CHECK(a.Find<ISquadView>() != b.Find<ISquadView>());
    auto* weather = host.runtime.Globals().Find<CWeatherExtension>();
    const auto before = weather->GetWeatherDiurnalSnapshot();
    host.runtime.Tick(0.05f);
    CHECK_NEAR(weather->GetWeatherDiurnalSnapshot().simulation_seconds - before.simulation_seconds, 0.05, 0.00001);
    auto& one = host.Player(first, 1, "one"); auto& two = host.Player(first, 2, "two");
    auto& outsider = host.Player(second, 3, "outsider");
    auto* commands = a.Find<ISquadCommands>();
    CHECK(!commands->Invite(one, outsider));
    CHECK(commands->Invite(one, two)); CHECK(commands->ConsumeInvite(two) == &one); CHECK(commands->Accept(two, one));
    CHECK(a.Find<ISquadView>()->Members(one).size() == 2);
    auto updated = host.runtime.AttachWorld(first, {true, true, true});
    CHECK(updated.Find<ISquadView>() == a.Find<ISquadView>());
    CHECK(updated.Find<ISquadView>()->Members(one).size() == 2);
    host.runtime.DetachWorld({first.GetId(), first.GetInstanceGeneration()});
    CHECK(a.Find<ISquadView>() == nullptr && a.Find<IChatService>() == nullptr);
    CHECK(b.Find<ISquadView>() != nullptr);
    host.runtime.ShutDown(); CHECK(b.Find<IChatService>() == nullptr);
}

TEST(RegisteredExtensionCapabilitiesTickMaintainAndPersistInBothScopes)
{
    class CCounter final : public CExtension, public ITickExtension, public IMaintainedExtension, public IPersistentExtension
    {
      public:
        explicit CCounter(bool world) : local(world) {}
        bool local; int ticks = 0, maintenance = 0;
        const SExtensionDescriptor& Descriptor() const override
        {
            static const SExtensionDescriptor global{"test.counter.global", EExtensionScope::Server, 1};
            static const SExtensionDescriptor world{"test.counter.world", EExtensionScope::World, 1};
            return local ? world : global;
        }
        void Tick(float) override { ++ticks; }
        void Refresh() override { ++maintenance; }
        void CaptureSnapshot(CSnapshotWriter& writer) const override { writer.Field("ticks", ticks); }
        bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string&) override
        { ticks = reader.Int("ticks"); return version == 1; }
    };
    SExtensionTestHost host([](CExtensionRuntime& runtime) {
        std::string error;
        CHECK(runtime.RegisterFactory(CCounter(false).Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
            return std::make_unique<CCounter>(false);
        }, error));
        CHECK(runtime.RegisterFactory(CCounter(true).Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
            return std::make_unique<CCounter>(true);
        }, error));
    });
    CGameWorld world("data/maps/test.tmj", 302); world.SetController(nullptr); world.ConfigureExtensions({});
    auto view = host.runtime.AttachWorld(world, {});
    auto* global = host.runtime.Globals().Find<CCounter>(); auto* local = view.Find<CCounter>();
    CHECK(global && local && global != local); if (!global || !local) return;
    host.runtime.Tick(0.05f);
    CHECK(global->ticks == 1 && local->ticks == 1);
    CHECK(global->maintenance == 1 && local->maintenance == 1);
    auto state = MakeJsonObject(); CSnapshotWriter writer(state.get()); host.runtime.CaptureSnapshot(writer);
    CHECK(json_array_size(json_object_get(state.get(), "globals")) == 2);
    host.runtime.Tick(0.05f);
    std::string error;
    CHECK(host.runtime.RestoreSnapshot(CSnapshotReader(state.get()), 1, {&world}, error));
    CHECK(global->ticks == 1 && local->ticks == 1);
    CHECK(!host.runtime.RegisterFactory(CCounter(true).Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CCounter>(true);
    }, error));
}

TEST(WhisperCommandsHonorMuteAndChatHistoryCapturesSessionIdentity)
{
    SExtensionTestHost host;
    CGameWorld world("data/maps/test.tmj", 301);
    world.SetController(nullptr);
    auto& sender = host.Player(world, 1, "whisper-sender");
    auto& target = host.Player(world, 2, "whisper-target");
    sender.MuteFor(60.f);
    CHECK(host.runtime.HandleCommand(sender, {"whisper", "whisper-target", "must-not-deliver"}));
    for (const auto& [id, entry] : host.delivered)
    { CHECK(id != target.GetId()); CHECK(entry.flag != EChatFlag::Whisper); }
    sender.Unmute(); host.delivered.clear();
    auto* chat = host.runtime.Globals().Find<IChatService>();
    const auto entry = chat->Submit(&world, {}, EChatFlag::Global, sender.GetId(), sender.GetName(), "identity");
    CHECK(entry.sender_session == sender.SessionRef());
}

TEST(SquadQueriesAreReadOnlyAndMembershipRejectsRecycledSessions)
{
    SExtensionTestHost host;
    CGameWorld world("data/maps/test.tmj", 203); world.SetController(nullptr);
    auto view = host.runtime.AttachWorld(world, {true, false, false});
    auto& first = host.Player(world, 1, "first"); auto& second = host.Player(world, 2, "second");
    auto* squads = view.Find<CSquadExtension>();
    CHECK(squads->Invite(first, second)); CHECK(squads->ConsumeInvite(second) == &first); CHECK(squads->Accept(second, first));
    auto before = MakeJsonObject(); CSnapshotWriter before_writer(before.get()); squads->CaptureSnapshot(before_writer);
    for (int i = 0; i < 10; ++i) { CHECK(squads->Members(first).size() == 2); CHECK(squads->RootId(second) == 1); }
    auto after = MakeJsonObject(); CSnapshotWriter after_writer(after.get()); squads->CaptureSnapshot(after_writer);
    CHECK(json_equal(before.get(), after.get()));
    CHECK(squads->Invite(first, second));
    host.players.erase(host.players.begin() + 1);
    auto& replacement = host.Player(world, 2, "replacement");
    CHECK(squads->ConsumeInvite(replacement) == nullptr);
    CHECK(squads->Members(first).size() == 1);
    CHECK(squads->Members(replacement).size() == 1);
    squads->Refresh();
    CHECK(squads->Members(first).size() == 1 && squads->RootId(replacement) == 2);
    CHECK(squads->Invite(first, replacement)); CHECK(squads->ConsumeInvite(replacement) == &first);
    CHECK(squads->Accept(replacement, first));
    host.players.erase(host.players.begin());
    auto& new_leader_id = host.Player(world, 1, "new-leader-id");
    squads->Refresh();
    CHECK(squads->Members(new_leader_id).size() == 1);
    CHECK(squads->Members(replacement).size() == 1 && squads->RootId(replacement) == 2);
}

TEST(ChatExtensionPreservesChannelRoutingAndNeverLeaksUnavailableSquadChat)
{
    SExtensionTestHost host;
    CGameWorld world("data/maps/test.tmj", 204), other("data/maps/test.tmj", 205);
    world.SetController(nullptr); other.SetController(nullptr);
    auto scope = host.runtime.AttachWorld(world, {true, false, false});
    host.runtime.AttachWorld(other, {false, false, false});
    auto& first = host.Player(world, 1, "First"); auto& second = host.Player(world, 2, "Second");
    auto& remote = host.Player(other, 3, "Remote");
    auto* chat = host.runtime.Globals().Find<IChatService>();
    chat->HandleMessage(first, EChatFlag::Global, "hello");
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2, 3})); host.delivered.clear();
    chat->HandleMessage(first, EChatFlag::Local, "nearby");
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(first, {"whisper", "remote", "private"}));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 3})); host.delivered.clear();
    CHECK(chat->History().size() == 2); // private messages retain old history policy
    CHECK(host.runtime.HandleCommand(first, {"invite", "second"})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(second, {"invite", "accept"})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(first, {"sq", "group-only"}));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(remote, {"sq", "do-not-broadcast"}));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{3}));
    CHECK(host.delivered.front().second.message == "squad is unavailable in this world"); host.delivered.clear();
    first.MuteFor(10.f); chat->HandleMessage(first, EChatFlag::Global, "muted");
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1})); host.delivered.clear();
    chat->Broadcast(chat->Submit(&world, {}, EChatFlag::Server, 0, "Server", "world-only"));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    CHECK(scope.Find<ISquadCommands>()->Leave(second));
    CHECK(scope.Find<ISquadView>()->Members(first).size() == 1);
}

TEST(ExtensionControllerReplacementDoesNotDestroySquads)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* first = fixture.Player(world, 1); auto* second = fixture.Player(world, 2);
    auto* commands = world.Extensions().Find<ISquadCommands>();
    CHECK(commands != nullptr); if (!commands) return;
    CHECK(commands->Invite(*first, *second)); CHECK(commands->ConsumeInvite(*second) == first); CHECK(commands->Accept(*second, *first));
    const auto* view = world.Extensions().Find<ISquadView>();
    world.SetController(CreateWorldController(world_controller_keys::pvp));
    CHECK(view == world.Extensions().Find<ISquadView>() && view->Members(*first).size() == 2);
    world.SetController(nullptr);
    CHECK(view->Members(*first).size() == 2);
}

TEST(ExtensionSnapshotRoundTripAndLegacyControllerWeatherMigration)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    world.SetController(CreateWorldController(world_controller_keys::open));
    auto* first = fixture.Player(world, 1); auto* second = fixture.Player(world, 2);
    auto* squads = world.Extensions().Find<ISquadCommands>();
    CHECK(squads->Invite(*first, *second)); CHECK(squads->ConsumeInvite(*second) == first); CHECK(squads->Accept(*second, *first));
    std::string error;
    auto* weather = fixture.server.Extensions().Globals().Find<CWeatherExtension>();
    CHECK(weather->ForceWeather(EWeatherType::Rain, 0.7f, error));
    const auto path = Session().directory / "extensions.json";
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error); CHECK(root != nullptr); if (!root) return;
    json_t* sections = json_object_get(root.get(), "sections");
    CHECK(json_object_get(sections, "weather_diurnal") == nullptr);
    CHECK(json_object_get(sections, "extensions") != nullptr);
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    auto reconnect = [&]() {
        first = fixture.server.GetNetworkModule()->FindPlayerById(1);
        second = fixture.server.GetNetworkModule()->FindPlayerById(2);
        CHECK(first && second); if (!first || !second) return;
        first->AttachSocket(sf::TcpSocket{}); second->AttachSocket(sf::TcpSocket{});
    };
    reconnect();
    CHECK(world.Extensions().Find<ISquadView>()->Members(*first).size() == 2);
    CHECK(weather->IsWeatherForced());

    // Convert the captured v2 topology to the old v1 controller-owned layout.
    json_object_set_new(root.get(), "container_version", json_integer(1));
    json_object_set_new(json_object_get(sections, "players"), "version", json_integer(1));
    json_t* extensions = json_object_get(json_object_get(sections, "extensions"), "data");
    json_t* weather_record = json_array_get(json_object_get(extensions, "globals"), 0);
    json_object_set(sections, "weather_diurnal", weather_record);
    const json_t* scopes = json_object_get(extensions, "worlds");
    json_t* worlds = SavedWorlds(root.get());
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        json_t* saved_world = json_array_get(worlds, i);
        json_object_del(saved_world, "extension_config");
        CSnapshotReader world_reader(saved_world);
        json_t* controller = json_object_get(saved_world, "controller");
        if (!json_is_object(controller)) continue;
        if (world_reader.UInt32("world_id") == world.GetId())
            json_object_set_new(controller, "type", json_string("world_controller.weather_diurnal_open"));
        for (size_t j = 0; j < json_array_size(scopes); ++j)
        {
            CSnapshotReader scope(json_array_get(scopes, j));
            if (scope.UInt32("world_id") != world_reader.UInt32("world_id")) continue;
            const json_t* instances = scope.Node("instances");
            if (!json_array_size(instances)) continue;
            const json_t* data = json_object_get(json_array_get(instances, 0), "data");
            json_object_set(json_object_get(controller, "data"), "squads", json_object_get(data, "squads"));
        }
    }
    json_object_del(sections, "extensions");
    const auto legacy_path = Session().directory / "legacy-extensions.json";
    CHECK(WriteJsonAtomically(legacy_path, root.get(), error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, legacy_path, error));
    reconnect();
    CHECK(world.GetController()->SnapshotKey() == world_controller_keys::open);
    CHECK(world.ExtensionConfig().weather_spawns && world.ExtensionConfig().weather_visuals);
    CHECK(world.Extensions().Find<ISquadView>()->Members(*first).size() == 2);
    CHECK(weather->IsWeatherForced());
}

TEST(ExtensionSnapshotFailureRollsBackWorldBindingsAndState)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    const auto owner = player->GetEntity()->GetHandle();
    const auto config = world.ExtensionConfig();
    const auto path = Session().directory / "invalid-extensions.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error); CHECK(root != nullptr); if (!root) return;
    json_t* data = json_object_get(json_object_get(json_object_get(root.get(), "sections"), "extensions"), "data");
    json_t* instance = json_array_get(json_object_get(json_array_get(json_object_get(data, "worlds"), 0), "instances"), 0);
    CHECK(instance != nullptr); if (!instance) return;
    json_object_set_new(instance, "key", json_string("extension.does_not_exist"));
    CHECK(WriteJsonAtomically(path, root.get(), error));
    CHECK(!CHotReloadSnapshotService::Restore(fixture.server, path, error));
    CHECK(!error.empty());
    CHECK(world.ResolveEntity(owner) != nullptr && world.ExtensionConfig() == config);
    CHECK(world.Extensions().Find<ISquadView>() != nullptr);
}

TEST(SquadSnapshotValidationIsAtomicAndRejectsMissingOrCyclicState)
{
    SExtensionTestHost host;
    CGameWorld world("data/maps/test.tmj", 206); world.SetController(nullptr);
    auto view = host.runtime.AttachWorld(world, {true, false, false});
    auto& one = host.Player(world, 1, "one"); auto& two = host.Player(world, 2, "two");
    auto* squads = view.Find<CSquadExtension>();
    CHECK(squads->Invite(one, two)); CHECK(squads->ConsumeInvite(two) == &one); CHECK(squads->Accept(two, one));
    auto saved = MakeJsonObject(); CSnapshotWriter writer(saved.get()); squads->CaptureSnapshot(writer);
    std::string error;
    auto invalid = MakeJsonObject();
    CHECK(!squads->RestoreSnapshot(CSnapshotReader(invalid.get()), 1, error));
    CHECK(!squads->RestoreSnapshot(CSnapshotReader(nullptr), 1, error));
    for (int mode = 0; mode < 3; ++mode)
    {
        invalid.reset(json_deep_copy(saved.get()));
        json_t* members = json_object_get(invalid.get(), "squads");
        json_t* first = json_array_get(members, 0);
        if (mode == 0) json_object_set_new(first, "parent_id", json_integer(2));
        if (mode == 1) json_object_set_new(first, "parent_id", json_integer(999));
        if (mode == 2) json_object_set_new(first, "session_generation", json_integer(0));
        CHECK(!squads->RestoreSnapshot(CSnapshotReader(invalid.get()), 1, error));
        CHECK(!error.empty());
        CHECK(squads->Members(one).size() == 2 && squads->RootId(two) == 1);
    }
}

TEST(CompoundTransferRollbackRestoresObjectsReferencesIndexesAndAllocators)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 301), target("data/maps/test.tmj", 302);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    auto* root = Spawn(source, EMobType::FireflyLarva);
    CPlayer player(sf::TcpSocket{}, 1, "rollback-compound"); player.Authenticate("rollback-compound");
    player.SetOwnedEntity(root); player.m_cp_stack.push_back({123, 1, 2});
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root);
    const auto refs = assembly->GetSegmentReferences();
    CHECK(!refs.empty()); if (refs.empty()) return;
    const auto root_ref = root->GetHandle();
    const auto original_pos = root->Pos();
    const auto source_count = source.GetEntityCount(), target_generation = target.NextEntityGeneration();
    for (int fail_after : {1, 2})
    {
        CServerTestAccess::FailTransferAfter(source, fail_after);
        const auto result = source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f});
        CHECK(!result && !result.error.empty());
        CHECK(player.GetEntity() == root && source.ResolveEntity(root_ref) == root && root->Pos() == original_pos);
        CHECK(source.GetEntityCount() == source_count && target.GetEntityCount() == 0);
        CHECK(target.NextEntityGeneration() == target_generation && player.m_cp_stack.size() == 1);
        CHECK(source.FindClosestEntity(root->Pos(), 1.f, [&](const CEntity* entity) { return entity == root; }) == root);
        for (const auto& ref : refs)
        {
            const auto* body = dynamic_cast<IMultiSegmentBodyMob*>(source.ResolveEntity(ref.entity_ref));
            CHECK(body && body->RootHandle().RefersTo(root_ref));
        }
    }
    CHECK(Spawn(target, EMobType::Bee)->Id() == 0);
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(player.GetEntity() == root && source.GetEntityCount() == 0 && target.GetEntityCount() == source_count + 1);
}

TEST(TransferFailurePreservesPvpMembershipLoadoutAndPacketQueues)
{
    CServerTestAccess fixture;
    auto& source = fixture.World();
    source.SetController(CreateWorldController(world_controller_keys::pvp));
    auto* player = fixture.Player(source);
    source.GetController()->OnPlayerArrivalCommitted(source, *player);
    CEntity* entity = player->GetEntity(); const auto ref = entity->GetHandle(); const int team = entity->Team();
    auto before = MakeJsonObject(); CSnapshotWriter before_writer(before.get()); source.GetController()->CaptureSnapshot(before_writer);
    auto* target = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(target != nullptr); if (!target) return;
    target->SetController(nullptr); target->ClearEntitiesForRestore();
    const auto queue_size = player->m_send_buffer.size();
    CServerTestAccess::FailTransferAfter(source, 1);
    CHECK(!source.TransferPlayerEntityToWorld(*player, *target, florrbt::Vector2f{500.f, 500.f}));
    auto after = MakeJsonObject(); CSnapshotWriter after_writer(after.get()); source.GetController()->CaptureSnapshot(after_writer);
    CHECK(json_equal(before.get(), after.get()));
    CHECK(player->GetEntity() == entity && source.ResolveEntity(ref) == entity && entity->Team() == team);
    CHECK(player->m_send_buffer.size() == queue_size && target->GetEntityCount() == 0);
}

TEST(CompoundTransferRejectsASecondControllerWithoutSplittingTheAssembly)
{
    CServerTestAccess fixture;
    auto& source = fixture.World();
    auto* first = fixture.Player(source, 1); auto* second = fixture.Player(source, 2);
    auto* root = Spawn(source, EMobType::FireflyLarva);
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root);
    CEntity* body = source.ResolveEntity(assembly->GetSegmentReferences().front().entity_ref);
    first->SetOwnedEntity(root); second->SetOwnedEntity(body);
    auto* target = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(target != nullptr); if (!target) return;
    target->SetController(nullptr); target->ClearEntitiesForRestore();
    const auto count = source.GetEntityCount();
    CHECK(!source.TransferPlayerEntityToWorld(*first, *target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(first->GetEntity() == root && second->GetEntity() == body && source.GetEntityCount() == count);
    CHECK(target->GetEntityCount() == 0);
}

TEST(HornetAttachedAmmoAndStateOwnersSurviveWorldTransfer)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 303), target("data/maps/test.tmj", 304);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "hornet-ammo"); player.Authenticate("hornet-ammo");
    auto* hornet = Spawn(source, EMobType::Hornet); player.SetOwnedEntity(hornet); hornet->Tick(0.f);
    CMissile* ammo = nullptr;
    source.ForEachEntity([&](CEntity* entity) { if (auto* missile = dynamic_cast<CMissile*>(entity); missile && missile->GetOwner() == hornet) ammo = missile; });
    CHECK(ammo && ammo->IsAttachedToOwner()); if (!ammo) return;
    hornet->AddState(std::make_unique<CInvincibleState>(hornet, 5.f, ERarity::Common));
    const auto old_ammo = ammo->GetHandle(); const float health = ammo->Health();
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(ammo->GetOwner() == hornet && ammo->GameWorld() == &target && ammo->IsAttachedToOwner());
    CHECK(source.ResolveEntity(old_ammo) == nullptr && ammo->Health() == health);
    CHECK(hornet->GetStates().front()->ResolveOwner() == hornet);
    const auto count = target.GetEntityCount(); hornet->Tick(0.f); CHECK(target.GetEntityCount() == count);
    CHECK(dynamic_cast<IHornetMob*>(hornet)->TryCastMissileInDirection({1.f, 0.f}));
}

TEST(MainWorldSelectionDoesNotLeaveNetworkOrContextBoundToDeletedWorld)
{
    CServerTestAccess fixture;
    auto* module = fixture.server.GetWorldModule();
    auto* player = fixture.Player(fixture.World());
    const auto old_id = fixture.World().GetId();
    auto* replacement = module->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(replacement != nullptr); if (!replacement) return;
    CHECK(module->SetMainWorld(replacement->GetId()));
    CHECK(fixture.server.GetMainWorld() == replacement && fixture.server.GameContext()->MainWorld() == replacement);
    CHECK(module->DestroyWorldById(old_id));
    CHECK(player->GetEntity() && player->GetEntity()->GameWorld() == replacement);
    CHECK(fixture.server.GetNetworkModule()->GameContext() == fixture.server.GameContext());
    player->m_send_buffer.clear(); player->m_send_offset = 0;
    CHECK(fixture.server.GetNetworkModule()->QueueWelcome(*player));
    const auto size = size_t(player->m_send_buffer[0]) | (size_t(player->m_send_buffer[1]) << 8);
    const auto message = ServerMessage::parse(player->m_send_buffer.data() + 2, size);
    CHECK(message.type == ServerMessage::Type::Welcome && message.map_name == replacement->GetMapPath());
    auto* room = module->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::open, 7001);
    CHECK(room && !module->SetMainWorld(room->GetId()));
    CHECK(!module->SetMainWorld(UINT32_MAX) && fixture.server.GetMainWorld() == replacement);
}

TEST(HotReloadCanReplaceMainWorldMapAndIdentityWithoutPinnedReferences)
{
    CServerTestAccess fixture;
    const auto all = fixture.server.GetWorlds();
    CHECK(all.size() >= 2); if (all.size() < 2) return;
    CGameWorld* old_selected = all.back(); const auto selected_id = old_selected->GetId();
    fixture.Player(*old_selected);
    const auto path = Session().directory / "replacement-main.json";
    std::string error; CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    auto root = LoadJsonFile(path, error); CHECK(root != nullptr); if (!root) return;
    json_t* sections = json_object_get(root.get(), "sections");
    json_t* topology = json_object_get(json_object_get(sections, "world_topology"), "data");
    json_object_set_new(topology, "main_world", json_integer(selected_id));
    json_t* worlds = json_object_get(topology, "worlds");
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        json_t* entry = json_array_get(worlds, i);
        if (CSnapshotReader(entry).UInt32("id") == selected_id)
            json_object_set_new(entry, "map_path", json_string("data/maps/test.tmj"));
    }
    worlds = SavedWorlds(root.get());
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        json_t* entry = json_array_get(worlds, i);
        if (CSnapshotReader(entry).UInt32("world_id") == selected_id)
            json_object_set_new(entry, "map_path", json_string("data/maps/test.tmj"));
    }
    CHECK(WriteJsonAtomically(path, root.get(), error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    CGameWorld* main = fixture.server.GetMainWorld();
    CHECK(main && main != old_selected && main->GetId() == selected_id && main->GetMapPath() == "data/maps/test.tmj");
    CHECK(fixture.server.GameContext()->MainWorld() == main);
    auto* player = fixture.server.GetNetworkModule()->FindPlayerById(1);
    CHECK(player && player->GetEntity() && player->GetEntity()->GameWorld() == main);
    player->AttachSocket(sf::TcpSocket{});
    CHECK(fixture.server.GetNetworkModule()->QueueWelcome(*player));
}

TEST(SameWorldCompoundTeleportRollsBackEveryPositionOnStagingFailure)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 305);
    world.SetController(nullptr); world.ClearEntitiesForRestore();
    auto* root = Spawn(world, EMobType::FireflyLarva);
    CPlayer player(sf::TcpSocket{}, 1, "teleport-rollback"); player.Authenticate("teleport-rollback");
    player.SetOwnedEntity(root);
    const auto generation = world.NextEntityGeneration();
    std::vector<std::pair<CEntity*, florrbt::Vector2f>> positions;
    world.ForEachEntity([&](CEntity* entity) { positions.emplace_back(entity, entity->Pos()); });
    CServerTestAccess::FailTransferAfter(world, 2);
    CHECK(!world.TransferPlayerEntityToWorld(player, world, florrbt::Vector2f{500.f, 500.f}));
    for (const auto& [entity, pos] : positions) CHECK(entity->Pos() == pos);
    CHECK(player.GetEntity() == root && world.NextEntityGeneration() == generation);
    const auto before = root->Pos();
    CHECK(world.TransferPlayerEntityToWorld(player, world, florrbt::Vector2f{500.f, 500.f}));
    const auto delta = root->Pos() - before;
    for (const auto& [entity, pos] : positions) CHECK(entity->Pos() == pos + delta);
}

TEST(CommittedNotificationsCannotReenterOrDestroyTransitionParticipants)
{
    CServerTestAccess fixture;
    auto& source = fixture.World(); auto* player = fixture.Player(source);
    CEntity* original = player->GetEntity();
    auto* module = fixture.server.GetWorldModule();
    auto* target = module->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(target != nullptr); if (!target) return;
    auto* other = Spawn(*target, EMobType::Bee);
    int notices = 0;
    auto controller = std::make_unique<SMutatingMembershipController>();
    controller->on_enter = [&](CGameWorld& world, CPlayer& entering) {
        ++notices;
        CHECK(!world.TransferPlayerEntityToWorld(entering, source, florrbt::Vector2f{1000.f, 1000.f}));
        CHECK(!module->DestroyWorldById(world.GetId()));
        CHECK(!module->SetMainWorld(world.GetId()));
        CHECK(!fixture.server.GetNetworkModule()->KickPlayer(entering.GetId(), "nested"));
        entering.SetOwnedEntity(other);
        CHECK(entering.GetEntity() == original);
        world.SetController(nullptr);
        CHECK(world.GetController() != nullptr);
    };
    target->SetController(std::move(controller));
    CHECK(source.TransferPlayerEntityToWorld(*player, *target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(notices == 1 && player->GetEntity() == original && original->GameWorld() == target);
    CHECK(!player->IsTransitionActive() && !source.IsTransitionActive() && !target->IsTransitionActive());
}

TEST(LiveControllerReplacementGuardsControlAndSessionLifetime)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* original = player->GetEntity(); auto* other = Spawn(world, EMobType::Bee);
    auto controller = std::make_unique<SMutatingMembershipController>();
    int notices = 0;
    controller->on_enter = [&](CGameWorld& current, CPlayer& entering) {
        ++notices;
        CHECK(current.IsTransitionActive() && entering.IsTransitionActive());
        CHECK(!fixture.server.GetNetworkModule()->KickPlayer(entering.GetId(), "rebind callback"));
        entering.SetOwnedEntity(other);
        CHECK(entering.GetEntity() == original);
        bool refused = false;
        try { fixture.server.GetNetworkModule()->ClearPlayersForRestore(); }
        catch (const std::logic_error&) { refused = true; }
        CHECK(refused);
    };
    world.SetController(std::move(controller));
    CHECK(notices == 1 && player->GetEntity() == original);
    CHECK(!world.IsTransitionActive() && !player->IsTransitionActive());
    CHECK(player->m_send_buffer.size() > 2);
    CHECK(player->m_send_buffer[2] != static_cast<std::uint8_t>(ServerMessage::Type::Welcome));
}

TEST(TransferredRelicClearsSourceZoneAndRebindsBannedSlotState)
{
    Session();
    CGameWorld source("data/maps/test.tmj", 306), target("data/maps/test.tmj", 307);
    source.SetController(nullptr); target.SetController(nullptr);
    source.ClearEntitiesForRestore(); target.ClearEntitiesForRestore();
    CPlayer player(sf::TcpSocket{}, 1, "relic-transfer"); player.Authenticate("relic-transfer");
    auto* flower = dynamic_cast<CPlayerFlower*>(Spawn(source, EMobType::PlayerFlower)); player.SetOwnedEntity(flower);
    SPetalStats stats; stats.health = 10.f; stats.radius = 2.f;
    auto petal = std::make_unique<CRelicPetal>(2.f, flower, 0, stats, EPetalType::Relic);
    auto* relic = dynamic_cast<CRelicPetal*>(source.InsertEntity(std::move(petal)));
    auto zone = std::make_unique<CStateZone>(&source, flower->Pos(), 20.f,
        CStateZone::MakeStateFactory<CPsionicConnectionState>(1.f, ERarity::Common));
    CEntity* zone_ptr = source.InsertEntity(std::move(zone)); const auto zone_ref = zone_ptr->GetHandle();
    relic->m_state_zone_ref = zone_ref;
    auto ban = std::make_unique<CBanSlotState>(flower, 10.f, 0, ERarity::Common);
    auto* ban_ptr = ban.get(); flower->AddState(std::move(ban));
    CHECK(source.TransferPlayerEntityToWorld(player, target, florrbt::Vector2f{500.f, 500.f}));
    CHECK(zone_ptr->IsMarkedForDes());
    CHECK(ban_ptr->ResolveOwner() == flower);
    flower->RemoveState(ban_ptr);
    CHECK(!flower->GetSlots()[0].m_banned);
    source.FlushEntityRemovals(); target.FlushEntityRemovals();
    CHECK(source.ResolveEntity(zone_ref) == nullptr && source.GetEntityCount() == 0);
    CHECK(player.GetEntity() == flower);
}
