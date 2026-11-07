#include "test_framework.h"
#include <SFML/Network/SocketSelector.hpp>
#include "Server/server.h"
#include "Server/Module/world_module.h"
#include "Server/Module/network_module.h"
#include "Server/Module/server_gui_module.h"
#include "Server/Game/gamecontext.h"
#include "Server/Game/controller_factory.h"
#include "Server/Game/talent.h"
#include "Server/Game/gamecontrollers/world_controller_factory.h"
#include "Server/Game/gamecontrollers/pvp_controller.h"
#include "Server/Game/gamecontrollers/opencontroller.h"
#include "Server/Game/gamecontrollers/mutable_terrain_controller.h"
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
#include "Server/Game/extensions/party_extension.h"
#include "Server/Game/extensions/chat_extension.h"
#include "Server/HotReload/hot_reload_snapshot.h"
#include "Server/HotReload/mob_stats_snapshot.h"
#include "Server/Persistence/account_store.h"
#include "Server/Persistence/server_data_store.h"
#include "Server/report.h"
#include "Server/Auth/oauth_provider.h"
#include "Server/Auth/account_link_service.h"
#include "Server/Auth/email_verification_service.h"
#include "Engine/json_value.h"
#include <chrono>
#include <filesystem>
#include <future>
#include <thread>
#include <regex>

namespace
{
struct SCraftInput { uint8_t petal_type = 0, rarity = 0; uint32_t count = 0; };
struct SSlotInput { uint8_t slot_index = 0, petal_type = 0, rarity = 0; };
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
        CHECK(server.Extensions().Init(*network, *network, *network, extension_error));
        server.m_p_game_context = std::make_unique<CGameContext>(*network, *network, *network, *network,
            server, server, server, server.Extensions());
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
    static void CheckFailureExitCodes()
    {
        struct CModule final : IModule
        {
            bool initialize = true, throws_on_tick = false, ticked = false;
            bool Init() override { return initialize; }
            void Tick(float) override { ticked = true; if (throws_on_tick) throw std::runtime_error("offline tick failure"); }
            void ShutDown() override {}
        };
        struct CFailedStore final : IServerDataStore
        {
            bool Init(std::string&) override { return true; }
            bool ShutDown(std::string& error) override { error = "offline persistence failure"; return false; }
            IUniquePetalRegistry& UniquePetals() override { throw std::logic_error("unused fixture"); }
            const IUniquePetalRegistry& UniquePetals() const override { throw std::logic_error("unused fixture"); }
        };
        {
            CServer value; value.m_modules.clear();
            auto module = std::make_unique<CModule>(); module->initialize = false; auto* observer = module.get();
            value.m_modules.push_back(std::move(module));
            CHECK(!value.InitializeModules()); CHECK(value.GetExitCode() != 0 && !value.IsRunning());
            value.Run(); CHECK(!observer->ticked);
            value.ShutDown(); CHECK(value.GetExitCode() != 0);
        }
        {
            CServer value; value.m_modules.clear();
            auto module = std::make_unique<CModule>(); module->throws_on_tick = true;
            value.m_modules.push_back(std::move(module));
            CHECK(value.InitializeModules()); value.Run();
            CHECK(value.GetExitCode() != 0 && !value.IsRunning());
        }
        {
            CServer value; value.m_modules.clear();
            const auto fixed_dt = game_config::server_fixed_dt;
            game_config::server_fixed_dt = 0; value.Run(); game_config::server_fixed_dt = fixed_dt;
            CHECK(value.GetExitCode() != 0);
        }
        {
            CServer value; value.m_modules.clear();
            value.m_p_data_store = std::make_unique<CFailedStore>();
            value.m_exit_code = CServer::hot_reload_exit_code;
            value.ShutDown(); CHECK(value.GetExitCode() == 1);
        }
        {
            CServer value; value.m_modules.clear();
            value.RequestStop(); value.Run(); value.ShutDown();
            CHECK(value.GetExitCode() == 0);
            value.Fail("fixture", "restore rejected", CServer::restore_failed_exit_code);
            value.Fail("fixture", "later cleanup failure");
            CHECK(value.GetExitCode() == CServer::restore_failed_exit_code);
        }
    }
    void TickModules(float dt) { server.TickModules(dt); }
    void CheckAccountMenuDoesNotSpawnUntilStart()
    {
        auto& network = *server.GetNetworkModule();
        auto pending = std::make_unique<CPlayer>(sf::TcpSocket{}, 1, "menu-fixture");
        auto* player = pending.get(); CHECK(network.InsertRestoredPlayer(std::move(pending)));
        ClientAuthRequest enter; enter.mode = ClientAuthRequest::Mode::EnterGame; enter.name = "runtime-player";
        CHECK(network.HandleAuthRequest(*player, enter) == INetworkModule::EPlayerBufferResult::Continue);
        CHECK(!player->IsAuthenticated() && !player->GetEntity());
        player->m_send_buffer.clear(); player->m_send_offset = 0;
        CHECK(network.ApplyAuthentication(*player, enter.name, true, EAuthResultCode::Authenticated, {}, {}, nullptr, true) ==
              INetworkModule::EPlayerBufferResult::Continue);
        CHECK(player->IsAuthenticated() && !player->GetEntity() && !player->HasOwnedEntity() && !player->NeedsRespawn());
        CPlayerLifecycleService{}.RespawnDeadControlledEntities(network.GetPlayers(), World(), network);
        CHECK(!player->GetEntity());
        const auto lobby = QueuedMessages(*player);
        CHECK(!lobby.empty() && lobby[0].type == ServerMessage::Type::AuthResult);
        CHECK(std::any_of(lobby.begin(), lobby.end(), [](const auto& message) { return message.type == ServerMessage::Type::AccountLinks; }));
        const auto inventory = std::find_if(lobby.begin(), lobby.end(), [](const auto& message) { return message.type == ServerMessage::Type::Inventory; });
        CHECK(inventory != lobby.end());
        if (inventory != lobby.end())
        {
            CHECK(inventory->inventory_scope == enter.name);
            CHECK(inventory->inventory_actor_scope.empty());
            CHECK(inventory->inventory == CAccountDataStore::GetInventory(enter.name));
        }
        CHECK(std::none_of(lobby.begin(), lobby.end(), [](const auto& message) { return message.type == ServerMessage::Type::OwnerState; }));
        CHECK(std::none_of(lobby.begin(), lobby.end(), [](const auto& message) { return message.type == ServerMessage::Type::Welcome || message.type == ServerMessage::Type::Snapshot; }));
        player->m_send_buffer.clear(); player->m_send_offset = 0;
        // 离线 socket 可能在发送时断开；实际场景包由浏览器联网测试验证。
        ClientOperate revive; revive.type = ClientOperate::Type::Chores; revive.agree = true;
        uint8_t packet[3]{};
        const auto packet_size = ClientOperate::pack(revive, packet);
        player->m_receive_buffer.assign(packet, packet + packet_size);
        CHECK(network.ProcessPlayerBuffer(*player) == INetworkModule::EPlayerBufferResult::Continue);
        CHECK(!player->GetEntity() && !player->NeedsRespawn());
        network.HandleAuthRequest(*player, enter);
        auto* body = player->GetEntity(); CHECK(body && body->GameWorld() == &World());
        CHECK(!player->GetUseNewPlayerSpawn());
        CHECK(network.HandleAuthRequest(*player, enter) == INetworkModule::EPlayerBufferResult::Continue);
        CHECK(player->GetEntity() == body);
    }
    static void ClearTestWalls(CGameWorld& world) { world.m_map->walls.clear(); world.BuildWallGrid(); }
    void CheckEmailBindingBeforeEnteringWorld()
    {
        auto& network = *server.GetNetworkModule();
        const auto policy = game_config::email_enabled;
        const auto user = game_config::email_username, password = game_config::email_password;
        game_config::email_enabled = 1; game_config::email_username = "sender@example.test"; game_config::email_password = "offline-fixture";
        struct CRestore { int policy; std::string user, password; ~CRestore() {
            game_config::email_enabled = policy; game_config::email_username = user; game_config::email_password = password;
        } } restore{policy, user, password};
        std::string error;
        CHECK(CAccountDataStore::CommitRegistration("email-menu-fixture", "offline-fixture-hash", "", "127.0.0.1", &error));
        auto pending = std::make_unique<CPlayer>(sf::TcpSocket{}, 1, "email-menu-fixture");
        pending->Authenticate("email-menu-fixture"); pending->SetRemoteAddress("127.0.0.1");
        auto* player = pending.get(); CHECK(network.InsertRestoredPlayer(std::move(pending)));
        std::promise<std::string> sent; auto message = sent.get_future();
        network.m_email_verification_service = std::make_unique<CEmailVerificationService>([&](const auto& mail, std::stop_token) {
            sent.set_value(mail.message); return remote_transport::SResult{};
        });
        CHECK(network.m_email_verification_service->Start(error));
        ClientAuthRequest request; request.mode = ClientAuthRequest::Mode::RequestBindingCode;
        request.name = player->GetAccountName(); request.email = "receiver@example.test";
        CHECK(network.HandleAuthRequest(*player, request) == INetworkModule::EPlayerBufferResult::Continue);
        const bool ready = message.wait_for(std::chrono::seconds(5)) == std::future_status::ready; CHECK(ready);
        if (!ready) { network.m_email_verification_service->ShutDown(); return; }
        const auto mail = message.get(); std::smatch match;
        CHECK(std::regex_search(mail, match, std::regex("Your FlorrBt verification code is ([0-9]+)")));
        if (match.size() != 2) { network.m_email_verification_service->ShutDown(); return; }
        for (int i = 0; i < 1000; ++i) {
            network.ProcessEmailDeliveryResults();
            if (network.m_email_delivery_requests.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        request.mode = ClientAuthRequest::Mode::ConfirmBinding; request.code = match[1].str();
        CHECK(network.HandleAuthRequest(*player, request) == INetworkModule::EPlayerBufferResult::Continue);
        SAccountEmailState state; CHECK(CAccountDataStore::GetEmailState(player->GetAccountName(), state));
        CHECK(state.verified && state.email == request.email && !player->GetEntity() && !player->GetWorld());
        CHECK(CAccountDataStore::Flush()); network.m_email_verification_service->ShutDown();
    }
    static void SendCraft(CServerTestAccess& fixture, CPlayer& player, const SCraftInput& request)
    {
        auto packet = EconomyRequest(fixture, player, EEconomyOperation::Craft);
        packet.petal_type = request.petal_type; packet.rarity = request.rarity; packet.count = request.count;
        SendEconomy(fixture, player, packet);
    }
    static void SendForge(CServerTestAccess& fixture, CPlayer& player, const SCraftInput& request)
    {
        auto packet = EconomyRequest(fixture, player, EEconomyOperation::Forge);
        packet.petal_type = request.petal_type; packet.rarity = request.rarity; packet.count = request.count;
        SendEconomy(fixture, player, packet);
    }
    static void SendSecondary(CServerTestAccess& fixture, CPlayer& player, const SSlotInput& request)
    {
        auto packet = EconomyRequest(fixture, player, EEconomyOperation::Loadout);
        packet.primary = CAccountDataStore::GetSlots(player.GetInventoryAccountName());
        packet.secondary = CAccountDataStore::GetSecondarySlots(player.GetInventoryAccountName());
        if (auto* flower = dynamic_cast<CFlower*>(player.GetEntity())) {
            packet.primary.resize(flower->GetSlots().size()); packet.secondary.resize(flower->GetSlots().size());
        }
        if (request.slot_index >= packet.secondary.size()) packet.secondary.resize(request.slot_index + 1);
        packet.secondary[request.slot_index] = {request.petal_type, request.rarity, request.petal_type ? 1u : 0u};
        SendEconomy(fixture, player, packet);
    }
    static ClientEconomyRequest EconomyRequest(CServerTestAccess& fixture, CPlayer& player, EEconomyOperation operation)
    {
        ClientEconomyRequest request; request.operation = operation;
        request.context.inventory_scope = player.GetInventoryAccountName();
        request.context.actor_scope = fixture.server.GetNetworkModule()->InventoryActorScope(player);
        CAccountDataStore::GetEconomyCounters(request.context.inventory_scope, request.context.expected_revision, request.context.request_id);
        ++request.context.request_id; return request;
    }
    static void SendEconomy(CServerTestAccess& fixture, CPlayer& player, const ClientEconomyRequest& request, bool flush = true)
    {
        fixture.server.GetNetworkModule()->HandleEconomyRequest(player, request);
        if (flush) CHECK(CAccountDataStore::Flush());
        fixture.server.GetNetworkModule()->ProcessEconomyReplies();
    }
    static void SendRespawn(CServerTestAccess& fixture, CPlayer& player)
    {
        ClientOperate op; op.type = ClientOperate::Type::Chores; op.agree = true;
        std::array<uint8_t, 3> bytes{};
        const auto size = ClientOperate::pack(op, bytes.data());
        player.m_receive_buffer.assign(bytes.begin(), bytes.begin() + size);
        fixture.server.GetNetworkModule()->ProcessPlayerBuffer(player);
    }
    static std::vector<ServerMessage> QueuedMessages(const CPlayer& player)
    {
        std::vector<ServerMessage> messages;
        for (size_t offset = player.m_send_offset; offset + 2 <= player.m_send_buffer.size();)
        {
            const size_t length = player.m_send_buffer[offset] | (size_t(player.m_send_buffer[offset + 1]) << 8);
            CHECK(length > 0 && offset + 2 + length <= player.m_send_buffer.size());
            if (!length || offset + 2 + length > player.m_send_buffer.size()) break;
            messages.push_back(ServerMessage::parse(player.m_send_buffer.data() + offset + 2, length));
            offset += length + 2;
        }
        return messages;
    }
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

TEST(AccountMenuAuthenticationWaitsForExplicitStart)
{
    CServerTestAccess fixture;
    fixture.CheckAccountMenuDoesNotSpawnUntilStart();
}

TEST(AccountLinkEmailVerificationWorksInAccountMenuWithoutSpawning)
{
    CServerTestAccess fixture; fixture.CheckEmailBindingBeforeEnteringWorld();
}

TEST(ClientFeaturePoliciesGateOperationsWithoutChangingScenesOrInventory)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto& runtime = fixture.server.Extensions(); const auto initial = player->GetEntity()->GetHandle();
    auto config = world.ExtensionConfig(); config.features = {{"inventory", false, feature_access::all}, {"craft", true, 0}, {"talent", true, 0}};
    world.ConfigureExtensions(config);
    const auto entries = runtime.ClientFeatures(*player);
    CHECK(entries.size() == 3);
    CHECK(!entries[0].visible && (entries[0].access & feature_access::edit));
    const auto inventory = CAccountDataStore::GetInventory(player->GetInventoryAccountName());
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Craft);
    request.petal_type = static_cast<uint8_t>(EPetalType::Basic); request.rarity = 1; request.count = 5;
    CServerTestAccess::SendEconomy(fixture, *player, request); SEconomyReceipt receipt;
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Forbidden && CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == inventory);
    CHECK(!runtime.CanUseFeature(*player, "talent", feature_access::edit));
    CHECK(player->GetEntity()->GetHandle() == initial);
    const auto messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::none_of(messages.begin(), messages.end(), [](const auto& message) { return message.type == ServerMessage::Type::Welcome; }));
    config.features = {{"inventory", true, feature_access::view}}; world.ConfigureExtensions(config);
    CHECK(runtime.CanUseFeature(*player, "inventory", feature_access::view));
    CHECK(!runtime.CanUseFeature(*player, "inventory", feature_access::edit));
    auto* flower = dynamic_cast<CPlayerFlower*>(player->GetEntity()); CHECK(flower); if (!flower) return;
    CHECK(player->TryApplyLoadout({}, {}) == EEconomyOutcome::Forbidden);
    config.features.clear(); world.ConfigureExtensions(config); flower->EnterDeathState();
    CHECK(runtime.CanUseFeature(*player, "inventory", feature_access::edit));
    std::string error; const auto path = Session().directory / "feature-policy.json";
    config.features = {{"craft", false, 0}}; world.ConfigureExtensions(config);
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    CHECK(world.ExtensionConfig().features == config.features);
}

TEST(AccountLinkProvidersVerifyOpaqueIdentityUsingOnlyServerSideExchange)
{
    std::vector<remote_transport::SHttpRequest> calls;
    COAuthProvider disabled({"discord", "", "", ""}); CHECK(!disabled.Config().Enabled() && disabled.AuthorizationUrl("nonce").empty());
    COAuthProvider discord({"discord", "fixture-app", "fixture-secret", "https://game.example/oauth/callback/discord"},
        [&](const auto& request, std::stop_token) {
            calls.push_back(request); remote_transport::SResult result; result.status = 200;
            result.body = calls.size() == 1 ? R"({"access_token":"fixture-token"})" : R"({"id":"123456789","username":"fixture-user"})";
            return result;
        });
    const auto url = discord.AuthorizationUrl(std::string(32, 'a'));
    CHECK(url.find("fixture-secret") == std::string::npos && url.find("scope=identify") != std::string::npos);
    const auto result = discord.Exchange("a&b");
    CHECK(result.success && result.identity.subject == "123456789" && result.identity.application == "fixture-app");
    CHECK(calls.size() == 2 && calls[0].post && calls[0].body.find("code=a%26b") != std::string::npos);
    CHECK(calls[1].bearer_token == "fixture-token" && result.identity.label == "fixture-user");
    calls.clear();
    COAuthProvider qq({"qq", "10001", "fixture-key", "https://game.example/oauth/callback/qq"},
        [&](const auto& request, std::stop_token) {
            calls.push_back(request); remote_transport::SResult result; result.status = 200;
            result.body = calls.size() == 1 ? "access_token=fixture-token&expires_in=3600" :
                "callback({\"client_id\":\"10001\",\"openid\":\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\"});";
            return result;
        });
    CHECK(qq.Exchange("code").identity.subject == std::string(32, 'a'));
    COAuthProvider mismatch({"qq", "10002", "fixture-key", "https://game.example/oauth/callback/qq"},
        [&](const auto& request, std::stop_token) {
            remote_transport::SResult result; result.status = 200;
            result.body = request.url.find("/token?") != std::string::npos ? R"({"access_token":"fixture"})" :
                R"({"client_id":"other-app","openid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"; return result;
        });
    CHECK(!mismatch.Exchange("code").success);
}

TEST(AccountLinkServiceBindsOnlyVerifiedAttemptsAndPersistsBeforeSuccess)
{
    CServerTestAccess fixture; auto* player = fixture.Player(fixture.World()); player->SetRemoteAddress("127.0.0.1");
    SAccountAuthWork work; std::string error;
    CHECK(CAccountDataStore::PrepareAuthentication("link-fixture", "fixture-password", "", "127.0.0.1", true, work, &error));
    const auto registration = CAccountDataStore::ExecuteAuthentication(std::move(work));
    CHECK(registration.success && CAccountDataStore::CommitRegistration(registration, &error)); player->Authenticate("link-fixture");
    std::atomic<int> requests = 0;
    CAccountLinkService service({{"discord", "fixture-app", "fixture-secret", "https://game.example/oauth/callback/discord"}},
        [&](const auto& request, std::stop_token) {
            ++requests; remote_transport::SResult response; response.status = 200;
            response.body = request.post ? R"({"access_token":"fixture-token"})" : R"({"id":"987654321","username":"linked-fixture"})";
            return response;
        });
    std::vector<std::string> replies; auto deliver = [&](CPlayer&, const std::string& payload) { replies.push_back(payload); return true; };
    auto send = [&](const std::string& text) { auto request = CJsonValue::Parse(text); CHECK(request); if (request) service.Request(*player, *request, deliver); };
    auto wait = [&](const std::string& kind) {
        for (int i = 0; i < 2000; ++i) {
            service.Tick(*fixture.server.GetNetworkModule(), deliver);
            for (const auto& text : replies) { auto value = CJsonValue::Parse(text); if (value && value->Find("kind") && value->Find("kind")->AsString() == kind) return text; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(false); return std::string{};
    };
    send(R"({"request":1,"action":"begin","provider":"discord","password":"fixture-password"})");
    const auto authorized = CJsonValue::Parse(wait("authorize")); CHECK(authorized); if (!authorized) return;
    const auto state = authorized->Find("state")->AsString(); CHECK(state.size() == 32);
    replies.clear();
    send(R"({"request":2,"action":"complete","provider":"discord","state":"00000000000000000000000000000000","code":"fake"})");
    CHECK(requests == 0 && !replies.empty() && replies[0].find("authorization_expired") != std::string::npos);
    replies.clear();
    send("{\"request\":3,\"action\":\"complete\",\"provider\":\"discord\",\"state\":\"" + state + "\",\"code\":\"valid-code\"}");
    const auto terminal = CJsonValue::Parse(wait("result")); CHECK(terminal && terminal->Find("error")->AsString().empty());
    CHECK(requests == 2 && CAccountDataStore::IsSaveCommitted(CAccountDataStore::CurrentSaveFence()));
    std::string id; std::vector<SLinkedIdentity> links;
    CHECK(CAccountDataStore::GetIdentityState("link-fixture", id, links) && links.size() == 1 && links[0].subject == "987654321");
    replies.clear();
    send("{\"request\":4,\"action\":\"complete\",\"provider\":\"discord\",\"state\":\"" + state + "\",\"code\":\"valid-code\"}");
    CHECK(requests == 2 && !replies.empty() && replies[0].find("authorization_expired") != std::string::npos);
    service.ShutDown();
}

TEST(AccountLinkServiceRejectsUnpreparedProvidersAndUnsolicitedCallbacks)
{
    CServerTestAccess fixture; auto* player = fixture.Player(fixture.World()); std::vector<std::string> replies;
    const auto before = CAccountDataStore::SnapshotAccounts();
    auto deliver = [&](CPlayer&, const std::string& payload) { replies.push_back(payload); return true; };
    auto request = CJsonValue::Parse(R"({"request":1,"action":"complete","provider":"discord","state":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","code":"fake"})");
    CHECK(request); if (!request) return;
    fixture.server.AccountLinks().Request(*player, *request, deliver);
    CHECK(replies.size() == 2 && (replies[0].find("not_configured") != std::string::npos || replies[0].find("authorization_expired") != std::string::npos));
    CHECK(CAccountDataStore::SnapshotAccounts() == before);
}

TEST(ArchitectureCraftRejectionRepliesWithoutConsumingOrInventingReturns)
{
    CServerTestAccess fixture;
    auto* player = fixture.Player(fixture.World());
    const auto before = CAccountDataStore::GetInventory(player->GetProgressAccountName());
    CServerTestAccess::SendCraft(fixture, *player, {static_cast<uint8_t>(EPetalType::Basic),
        static_cast<uint8_t>(ERarity::Common), max_inventory_item_count + 5});
    auto messages = CServerTestAccess::QueuedMessages(*player);
    const auto result = std::find_if(messages.begin(), messages.end(), [](const auto& message) {
        return message.type == ServerMessage::Type::EconomyResult;
    });
    CHECK(result != messages.end());
    if (result != messages.end())
    {
        CHECK(result->craft_success == false && result->craft_consumed == 0);
        CHECK(result->craft_petal_type == static_cast<uint8_t>(EPetalType::Basic));
        CHECK(result->craft_items.empty());
    }
    CHECK(CAccountDataStore::GetInventory(player->GetProgressAccountName()) == before);
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
        return message.type == ServerMessage::Type::Inventory;
    }));

    fixture.World().SetController(CreateWorldController(world_controller_keys::sandbox));
    CHECK(player->HasIsolatedProgress());
    player->m_send_buffer.clear(); player->m_send_offset = 0;
    CServerTestAccess::SendForge(fixture, *player, {static_cast<uint8_t>(EPetalType::Basic),
        static_cast<uint8_t>(ERarity::Super), 5});
    messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& message) {
        return message.type == ServerMessage::Type::EconomyResult && message.craft_success == false &&
               message.craft_consumed == 0 && message.craft_items.empty();
    }));
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == before);
}

TEST(ServerFailureExitCodesDistinguishStartupTickAndShutdownFromNormalStop)
{
    CServerTestAccess::CheckFailureExitCodes();
}

TEST(ServerGuiDisabledStartsAndStopsWithoutADisplay)
{
    const bool was_enabled = game_config::gui_console_enabled;
    game_config::gui_console_enabled = false;
    {
        CConsole console;
        IServerGuiModule gui(console);
        CHECK(gui.Init());
        gui.Tick(0.05f);
        gui.ShutDown();
        gui.ShutDown();
    }
    game_config::gui_console_enabled = was_enabled;
}

TEST(SandboxReentryUsesFreshInventoryIdentityAndReloadPreservesIt)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    world.SetController(CreateWorldController(world_controller_keys::sandbox));
    const auto old_scope = player->GetInventoryAccountName();
    const auto type = static_cast<uint8_t>(EPetalType::Basic), rarity = static_cast<uint8_t>(ERarity::Common);
    CAccountDataStore::AddItem(old_scope, type, rarity, 5);
    auto delayed = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Craft);
    delayed.petal_type = type; delayed.rarity = rarity; delayed.count = 5;
    world.SetController(CreateWorldController(world_controller_keys::open));
    world.SetController(CreateWorldController(world_controller_keys::sandbox));
    const auto next_scope = player->GetInventoryAccountName();
    CHECK(next_scope != old_scope);
    const auto before = CAccountDataStore::GetInventory(next_scope);
    CServerTestAccess::SendEconomy(fixture, *player, delayed);
    CHECK(CAccountDataStore::GetInventory(next_scope) == before);
    SEconomyReceipt receipt; CHECK(!CAccountDataStore::FindEconomyReceipt(next_scope, delayed.context.request_id, receipt));
    std::string error;
    const auto path = Session().directory / "sandbox-instance-identity.json";
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    player = fixture.server.GetNetworkModule()->FindPlayerById(1);
    CHECK(player && player->GetInventoryAccountName() == next_scope);
    if (player) CHECK(CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == before);
}

TEST(PvpRolesRejectOverlappingTeamsOverflowAndReusedSessions)
{
    CControllerParams params;
    for (const auto* value : {"1", "255", "2147483647"})
    {
        params.Set("start_team", value);
        CHECK(!CreateWorldController(world_controller_keys::pvp, params));
    }
    params.Set("start_team", "2"); params.Set("spectator_team", "3");
    CHECK(!CreateWorldController(world_controller_keys::pvp, params));
    params.Set("spectator_team", "200");
    CServerTestAccess fixture; auto& world = fixture.World();
    world.SetController(CreateWorldController(world_controller_keys::pvp, params));
    auto* controller = dynamic_cast<CAntHelPvpController*>(world.GetController());
    CHECK(controller); if (!controller) return;
    auto* first = fixture.Player(world, 1); auto* second = fixture.Player(world, 2); auto* spectator = fixture.Player(world, 3);
    for (auto* player : {first, second, spectator}) controller->OnPlayerArrivalCommitted(world, *player);
    CHECK(controller->HasMatchTeam(*first) && controller->HasMatchTeam(*second));
    CHECK(!controller->HasMatchTeam(*spectator) && controller->PlayerTeam(3) == 200);
    CPlayer reused(sf::TcpSocket{}, 1, "runtime-player"); reused.Authenticate("runtime-player");
    CHECK(!controller->HasMatchTeam(reused));
    auto saved = MakeJsonObject(); CSnapshotWriter writer(saved.get()); controller->CaptureSnapshot(writer);
    CAntHelPvpController restored(params); std::string error;
    CHECK(restored.RestoreSnapshot(CSnapshotReader(saved.get()), controller->SnapshotVersion(), error));
    CHECK(restored.HasMatchTeam(*first) && !restored.HasMatchTeam(*spectator) && !restored.HasMatchTeam(reused));
    json_object_set_new(saved.get(), "player_role_3", json_integer(1));
    CHECK(!restored.RestoreSnapshot(CSnapshotReader(saved.get()), controller->SnapshotVersion(), error));
}

TEST(AntennaeTalentUsesCurrentDefinitionWithoutReplacingStaticIdentity)
{
    auto& talents = AntennaeTalents();
    auto* identity = &talents.back();
    const float old_value = game_config::default_antennae_horizon_ultra;
    game_config::default_antennae_horizon_ultra = 23.f;
    CAntennaeBehavior behavior;
    auto stats = EmptyFlowerStats(); STalentContext context; context.flower_stats = &stats;
    talents.back().Apply(ETalentEvent::RebuildFlowerStats, context);
    CHECK_NEAR(stats.horizon, behavior.GetStats(ERarity::Ultra).horizon, 0.001f);
    CHECK(&AntennaeTalents().back() == identity);
    stats = EmptyFlowerStats(); talents.front().Apply(ETalentEvent::RebuildFlowerStats, context);
    CHECK_NEAR(stats.horizon, game_config::default_horizon * (1.f + 22.f / 3.f), 0.001f);
    game_config::default_antennae_horizon_ultra = old_value;
}

TEST(WorldTerrainHistorySurvivesControllerReplacementAndSnapshotRestore)
{
    CServerTestAccess fixture;
    auto* world = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::mutable_terrain);
    CHECK(world && world->GetMap() && !world->GetMap()->walls.empty()); if (!world || !world->GetMap()) return;
    const auto id = world->GetId();
    const auto wall = world->GetMap()->walls.front();
    const auto before = world->GetMap()->walls.size();
    world->Terrain().EraseTerrain({wall.center.x, wall.center.y}, 1.f);
    const auto after = world->GetMap()->walls.size();
    CHECK(after < before);
    const auto revision = world->Terrain().Revision();
    struct CWriter final : IPlayerSceneWriter
    {
        size_t tiles = 0, walls = 0;
        bool TerrainTile(std::uint8_t, std::uint16_t, std::uint16_t, std::uint32_t) override { ++tiles; return true; }
        bool RemoveWall(florrbt::Vector2f, florrbt::Vector2f) override { ++walls; return true; }
    } first, second, restored;
    CHECK(world->Terrain().WritePlayerScene(first)); CHECK(first.walls > 0);
    world->SetController(CreateWorldController(world_controller_keys::open));
    CHECK(world->Terrain().Revision() == revision && world->GetMap()->walls.size() == after);
    CHECK(world->Terrain().WritePlayerScene(second));
    CHECK(second.tiles == first.tiles && second.walls == first.walls);
    const auto path = Session().directory / "world-terrain.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    world = fixture.server.FindWorldById(id);
    CHECK(world); if (!world) return;
    CHECK(world->Terrain().WritePlayerScene(restored));
    CHECK(world->GetMap()->walls.size() == after && world->Terrain().Revision() == revision);
    CHECK(restored.tiles == first.tiles && restored.walls == first.walls);
    auto invalid = MakeJsonObject(); CSnapshotWriter writer(invalid.get()); world->Terrain().CaptureSnapshot(writer);
    json_object_set_new(invalid.get(), "terrain_dirty", json_pack("[[i,i]]", -1, 0));
    CHECK(!world->Terrain().RestoreSnapshot(CSnapshotReader(invalid.get()), 1, error));
    CHECK(world->GetMap()->walls.size() == after && world->Terrain().Revision() == revision);

    auto legacy = LoadJsonFile(path, error); CHECK(legacy); if (!legacy) return;
    auto* section = json_object_get(json_object_get(legacy.get(), "sections"), "worlds");
    CHECK(json_integer_value(json_object_get(section, "version")) == 2);
    auto* worlds = json_object_get(section, "data");
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        auto* saved = json_array_get(worlds, i);
        if (CSnapshotReader(saved).UInt32("world_id") != id) continue;
        auto* controller = json_object_get(saved, "controller");
        auto* controller_data = json_object_get(controller, "data");
        auto* terrain = json_object_get(saved, "terrain");
        CHECK(json_object_update(controller_data, terrain) == 0);
        json_object_set_new(controller, "type", json_string("world_controller.mutable_terrain"));
        json_object_set_new(controller, "version", json_integer(1));
        json_object_del(saved, "terrain");
    }
    json_object_set_new(section, "version", json_integer(1));
    const auto legacy_path = Session().directory / "legacy-controller-terrain.json";
    CHECK(WriteJsonAtomically(legacy_path, legacy.get(), error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, legacy_path, error));
    world = fixture.server.FindWorldById(id); CHECK(world);
    if (world) CHECK(world->GetMap()->walls.size() == after);
}

TEST(ModePreparationFailurePreservesControllerBodiesAndProgressScopes)
{
    struct CPrevious final : COpenController
    {
        int departures = 0;
        void OnPlayerDepartureCommitted(CGameWorld&, CPlayer&, CEntity*) override { ++departures; }
    };
    struct CRejected final : COpenController
    {
        bool PrepareActivation(CWorldPreparation&, std::string& error) const override
        { error = "测试：准备失败"; return false; }
    };
    CServerTestAccess fixture; auto& world = fixture.World();
    auto previous = std::make_unique<CPrevious>(); auto* original = previous.get();
    CHECK(world.SetController(std::move(previous)));
    auto* player = fixture.Player(world, 1); const auto body = player->GetEntity()->GetHandle();
    const auto scope = player->GetInventoryAccountName();
    const auto rejected = world.SetController(std::make_unique<CRejected>());
    CHECK(!rejected && !rejected.recovering && world.GetController() == original && original->departures == 0);
    CHECK(player->GetEntity()->GetHandle() == body && player->GetInventoryAccountName() == scope);
    auto* missing = fixture.Player(world, 2); missing->Authenticate("missing-progress-record");
    const auto accounts_before = CAccountDataStore::SnapshotAccounts().size();
    const auto bad_scope = world.SetController(CreateWorldController(world_controller_keys::sandbox));
    CHECK(!bad_scope && !world.IsRecovering() && world.GetController() == original && original->departures == 0);
    CHECK(CAccountDataStore::SnapshotAccounts().size() == accounts_before);
    CHECK(!player->HasIsolatedProgress());
    auto* destination = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::sandbox);
    CHECK(destination); if (!destination) return;
    const auto missing_body = missing->GetEntity()->GetHandle();
    CHECK(!world.TransferPlayerEntityToWorld(*missing, *destination, florrbt::Vector2f{1024, 1024}));
    CHECK(missing->GetWorld() == &world && missing->GetEntity()->GetHandle() == missing_body);
    CHECK(original->departures == 0);
    auto* npc = Spawn(*destination, EMobType::MechaFlower);
    CHECK(!fixture.server.GetNetworkModule()->AssignPlayerEntity(missing->GetId(), *destination, npc->Id()));
    CHECK(!npc->IsPlayerControlled() && missing->GetEntity()->GetHandle() == missing_body);
}

TEST(ModeCommitExceptionFreezesRoomAndBlocksSnapshotsAndEconomy)
{
    struct CBroken final : COpenController
    {
        void OnPlayerArrivalCommitted(CGameWorld&, CPlayer&) override { throw std::runtime_error("测试：进入回调失败"); }
    };
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    const auto result = world.SetController(std::make_unique<CBroken>());
    CHECK(!result && result.committed && result.recovering && world.IsRecovering());
    const auto pos = player->GetEntity()->Pos();
    auto* mob = dynamic_cast<CMobBase*>(player->GetEntity()); mob->m_vel = {100, 0};
    world.Tick(1.f); CHECK(player->GetEntity()->Pos() == pos);
    CHECK(!fixture.server.GetNetworkModule()->QueueOwnerStateUpdate(*player));
    const auto inventory = CAccountDataStore::GetInventory(player->GetInventoryAccountName());
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Craft);
    request.petal_type = static_cast<uint8_t>(EPetalType::Basic); request.rarity = 1; request.count = 5;
    CServerTestAccess::SendEconomy(fixture, *player, request);
    SEconomyReceipt receipt;
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Forbidden && CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == inventory);
    std::string error;
    CHECK(!CHotReloadSnapshotService::Capture(fixture.server, Session().directory / "recovery-incomplete.json", error));
    CHECK(world.SetController(CreateWorldController(world_controller_keys::open)));
    CHECK(!world.IsRecovering());
}

TEST(CommittedTransferFailureReportsRecoveryWithoutPretendingRollback)
{
    struct CBroken final : COpenController
    {
        void OnPlayerArrivalCommitted(CGameWorld&, CPlayer&) override { throw std::runtime_error("测试：迁移后回调失败"); }
    };
    CServerTestAccess fixture; auto& source = fixture.World(); auto* player = fixture.Player(source);
    auto* destination = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(destination); if (!destination) return;
    CHECK(destination->SetController(std::make_unique<CBroken>()));
    const auto result = source.TransferPlayerEntityToWorld(*player, *destination, florrbt::Vector2f{1024, 1024});
    CHECK(result.committed && result.recovering);
    CHECK(player->GetWorld() == destination && source.IsRecovering() && destination->IsRecovering());
}

TEST(EconomyLoadoutIsAtomicAndNpcRequestsCannotCrossControlScopes)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    const auto formal = CAccountDataStore::GetInventory(player->GetAccountName());
    auto* npc = dynamic_cast<CFlower*>(Spawn(world, EMobType::MechaFlower));
    CHECK(npc); if (!npc) return;
    npc->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Basic), ERarity::Common);
    npc->LoadPetalSlot(1, FindPetalPrototype(EPetalType::Rose), ERarity::Common);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, npc->Id()));
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    const auto original = CAccountDataStore::GetSlots(player->GetInventoryAccountName());
    request.primary = original; request.secondary.resize(original.size());
    npc->GetSlots()[0].SpawnCopy(0, npc);
    auto* petal = npc->GetSlots()[0].m_p_petals[0]; CHECK(petal);
    std::swap(request.primary[0], request.primary[1]);
    request.primary.back() = {255, 1, 1};
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::GetSlots(player->GetInventoryAccountName()) == original);
    CHECK(npc->GetSlots()[0].m_p_petals[0] == petal && petal && !petal->IsMarkedForDes());

    request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    request.primary = original; request.secondary.resize(original.size());
    std::swap(request.primary[0], request.primary[1]);
    npc->GetSlots()[2].SetBanSource(777, true);
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::GetSlots(player->GetInventoryAccountName()) == request.primary);
    CHECK(npc->GetSlots()[0].m_p_proto->m_type == EPetalType::Rose);
    CHECK(npc->GetSlots()[2].IsBanned());
    CHECK(petal->IsMarkedForDes());
    npc->GetSlots()[0].SpawnCopy(0, npc);
    auto* replacement = npc->GetSlots()[0].m_p_petals[0];
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(npc->GetSlots()[0].m_p_petals[0] == replacement);

    auto* second = Spawn(world, EMobType::MechaFlower);
    const auto old_scope = player->GetInventoryAccountName();
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, second->Id()));
    const auto next = CAccountDataStore::GetSlots(player->GetInventoryAccountName());
    CHECK(player->GetInventoryAccountName() != old_scope);
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::GetSlots(player->GetInventoryAccountName()) == next);
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == formal);
}

TEST(DeathLoadoutPersistsWithoutSpawningAndSameWorldRespawnKeepsScene)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    std::string error;
    CHECK(CAccountDataStore::CommitRegistration("death-loadout", "offline-fixture-hash", "", "127.0.0.1", &error));
    player->Authenticate("death-loadout"); player->RestoreSavedLoadout();
    auto* corpse = dynamic_cast<CPlayerFlower*>(player->GetEntity()); CHECK(corpse); if (!corpse) return;
    const auto type = static_cast<uint8_t>(EPetalType::Rose);
    CAccountDataStore::AddItem(player->GetInventoryAccountName(), type, 1, 3);
    const auto live_actor = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout).context.actor_scope;
    corpse->EnterDeathState();
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    CHECK(!request.context.actor_scope.empty() && request.context.actor_scope == live_actor);
    request.primary = CAccountDataStore::GetSlots(player->GetInventoryAccountName());
    request.primary.resize(corpse->GetSlots().size()); request.secondary.resize(request.primary.size());
    request.primary[0] = {type, 1, 1}; request.secondary[0] = {type, 1, 1};
    CServerTestAccess::SendEconomy(fixture, *player, request);
    SEconomyReceipt receipt;
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Completed);
    CHECK(corpse->GetSlots()[0].m_p_proto->m_type == EPetalType::Rose);
    const auto inventory = CAccountDataStore::GetInventory(player->GetInventoryAccountName());
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == inventory);
    for (int i = 0; i < 100; ++i) corpse->Tick(0.05f);
    CHECK(corpse->m_is_dead && corpse->Health() == 0.f);
    for (const auto& slot : corpse->GetSlots())
        for (const auto* petal : slot.m_p_petals) CHECK(!petal);
    CHECK(corpse->ReviveFromYggdrasil(0.5f));
    CHECK(corpse->GetSlots()[0].m_p_proto->m_type == EPetalType::Rose);
    corpse->EnterDeathState();

    player->m_send_buffer.clear(); player->m_send_offset = 0;
    CHECK(CPlayerLifecycleService{}.RespawnPlayer(*player, world, *fixture.server.GetNetworkModule()));
    auto* revived = dynamic_cast<CPlayerFlower*>(player->GetEntity());
    CHECK(revived && revived != corpse && !revived->IsDead()); if (!revived) return;
    CHECK(revived->GetSlots()[0].m_p_proto->m_type == EPetalType::Rose);
    CHECK(CAccountDataStore::GetSecondarySlots(player->GetInventoryAccountName())[0] == request.secondary[0]);
    const auto messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::OwnerState; }));
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::Inventory; }));
    CHECK(std::none_of(messages.begin(), messages.end(), [](const auto& msg) {
        return msg.type == ServerMessage::Type::Welcome || msg.type == ServerMessage::Type::TerrainDelta ||
            msg.type == ServerMessage::Type::TerrainWallRemove;
    }));
    const auto saved = CAccountDataStore::GetSlots(player->GetInventoryAccountName());
    auto stale = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    CHECK(stale.context.actor_scope != live_actor);
    stale.context.actor_scope = live_actor; stale.primary = request.primary; stale.secondary = request.secondary;
    std::swap(stale.primary[0], stale.primary[1]);
    CServerTestAccess::SendEconomy(fixture, *player, stale);
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), stale.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Forbidden);
    CHECK(CAccountDataStore::GetSlots(player->GetInventoryAccountName()) == saved);
}

TEST(DeathLoadoutDoesNotActivateEquipmentAurasBeforeRevival)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    std::string error;
    CHECK(CAccountDataStore::CommitRegistration("death-aura", "offline-fixture-hash", "", "127.0.0.1", &error));
    player->Authenticate("death-aura"); player->RestoreSavedLoadout();
    auto* corpse = dynamic_cast<CPlayerFlower*>(player->GetEntity()); CHECK(corpse); if (!corpse) return;
    corpse->EnterDeathState();
    const auto max_health = corpse->GetFinalStats()->max_health;
    const auto radius = corpse->Radius();
    const auto type = static_cast<uint8_t>(EPetalType::Corruption), rarity = static_cast<uint8_t>(ERarity::Primordial);
    CAccountDataStore::AddItem(player->GetInventoryAccountName(), type, rarity, 1);
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    request.primary = CAccountDataStore::GetSlots(player->GetInventoryAccountName());
    request.primary.resize(corpse->GetSlots().size()); request.secondary.resize(request.primary.size());
    request.primary[0] = {type, rarity, 1};
    CServerTestAccess::SendEconomy(fixture, *player, request);
    SEconomyReceipt receipt;
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Completed);
    corpse->RebuildFinalStats();
    for (int i = 0; i < 20; ++i) corpse->Tick(0.05f);
    CHECK(corpse->IsDead() && !corpse->HasState<CCorruptionState>());
    CHECK(corpse->Health() == 0.f && corpse->GetFinalStats()->max_health == max_health && corpse->Radius() == radius);
    CHECK(corpse->ReviveFromYggdrasil(0.5f));
    corpse->Tick(0.05f);
    CHECK(corpse->HasState<CCorruptionState>());
}

TEST(DeathLoadoutStillHonorsControllerLocksAndForbiddenForge)
{
    struct CLocked final : IGameController
    {
        void OnTick(CGameWorld&, float) override {}
        std::string_view SnapshotKey() const override { return "test.death_loadout_locked"; }
        bool LocksPetalLoadout() const override { return true; }
    };
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* corpse = dynamic_cast<CPlayerFlower*>(player->GetEntity()); CHECK(corpse); if (!corpse) return;
    corpse->EnterDeathState();
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Forge);
    request.petal_type = static_cast<uint8_t>(EPetalType::Basic); request.rarity = static_cast<uint8_t>(ERarity::Super); request.count = 5;
    CServerTestAccess::SendEconomy(fixture, *player, request);
    SEconomyReceipt receipt;
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Forbidden);
    CHECK(world.SetController(std::make_unique<CLocked>()));
    request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Loadout);
    request.primary.resize(corpse->GetSlots().size()); request.secondary.resize(request.primary.size());
    const auto inventory = CAccountDataStore::GetInventory(player->GetInventoryAccountName());
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::FindEconomyReceipt(player->GetInventoryAccountName(), request.context.request_id, receipt));
    CHECK(receipt.outcome == EEconomyOutcome::Forbidden);
    CHECK(CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == inventory);
}

TEST(EconomyRepliesWaitForDurableReceiptAndReplayDoesNotConsumeAgain)
{
    CServerTestAccess fixture;
    auto* player = fixture.Player(fixture.World());
    const auto type = static_cast<uint8_t>(EPetalType::Basic), rarity = static_cast<uint8_t>(ERarity::Common);
    CAccountDataStore::AddItem(player->GetInventoryAccountName(), type, rarity, 10);
    CHECK(CAccountDataStore::Flush());
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Craft);
    request.petal_type = type; request.rarity = rarity; request.count = 5;
    {
        CAccountDataStore::CSaveBatch hold;
        CServerTestAccess::SendEconomy(fixture, *player, request, false);
        const auto messages = CServerTestAccess::QueuedMessages(*player);
        CHECK(std::none_of(messages.begin(), messages.end(), [](const auto& msg) {
            return msg.type == ServerMessage::Type::EconomyResult;
        }));
    }
    const auto after = CAccountDataStore::GetInventory(player->GetInventoryAccountName());
    CHECK(CAccountDataStore::Flush());
    CServerTestAccess::SendEconomy(fixture, *player, request);
    const auto messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::any_of(messages.begin(), messages.end(), [&](const auto& msg) {
        return msg.type == ServerMessage::Type::EconomyResult && msg.economy_request_id == request.context.request_id &&
               msg.economy_recorded && msg.economy_outcome == EEconomyOutcome::Completed;
    }));
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CHECK(CAccountDataStore::GetInventory(player->GetInventoryAccountName()) == after);
}

TEST(OrderedTickConsumesInputBeforeWorldAndPublishesCommittedPositions)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::ClearTestWalls(world);
    // An isolated loopback pair exercises the actual receive/send stages;
    // no game server listener, auth service or real account database starts.
    sf::TcpListener listener;
    CHECK(listener.listen(0, sf::IpAddress::LocalHost) == sf::Socket::Status::Done);
    sf::TcpSocket client, accepted;
    CHECK(client.connect(sf::IpAddress::LocalHost, listener.getLocalPort(), sf::seconds(2.f)) == sf::Socket::Status::Done);
    CHECK(listener.accept(accepted) == sf::Socket::Status::Done);
    accepted.setBlocking(false);
    auto player = std::make_unique<CPlayer>(std::move(accepted), 1, "runtime-player");
    player->Authenticate("runtime-player");
    player->SetVersionVerified(true); // This fixture starts after authentication.
    auto* flower = dynamic_cast<CPlayerFlower*>(Spawn(world, EMobType::PlayerFlower));
    CHECK(flower != nullptr); if (!flower) return;
    player->SetOwnedEntity(flower); CPlayer* raw = player.get();
    CHECK(fixture.server.GetNetworkModule()->InsertRestoredPlayer(std::move(player)));
    // The legacy wire axes are intentionally transposed by both clients and
    // the controller. wire move_y drives world X.
    ClientOperate input; input.type = ClientOperate::Type::Input; input.move_x = 0; input.move_y = 127;
    ClientOperate attack; attack.type = ClientOperate::Type::Chores; attack.is_attacking = true;
    std::array<std::uint8_t, 8> request{};
    size_t length = ClientOperate::pack(input, request.data());
    length += ClientOperate::pack(attack, request.data() + length);
    CHECK(client.send(request.data(), length) == sf::Socket::Status::Done);
    client.setBlocking(false);
    sf::SocketSelector ready; ready.add(raw->GetSocket()); CHECK(ready.wait(sf::seconds(2.f)));
    struct SIntervalRestore { float old = game_config::network_snapshot_interval;
        ~SIntervalRestore() { game_config::network_snapshot_interval = old; } } restore;
    game_config::network_snapshot_interval = 0.f;
    const float before = flower->Pos().x;
    const auto flower_ref = flower->GetHandle();
    fixture.TickModules(0.05f);
    CHECK(world.ResolveEntity(flower_ref) == flower); if (world.ResolveEntity(flower_ref) != flower) return;
    CHECK(flower->IsAttacking() && flower->Pos().x > before);
    sf::SocketSelector output; output.add(client); CHECK(output.wait(sf::seconds(2.f)));
    std::vector<std::uint8_t> received;
    std::array<std::uint8_t, 65536> buffer{};
    size_t count = 0;
    while (client.receive(buffer.data(), buffer.size(), count) == sf::Socket::Status::Done)
        received.insert(received.end(), buffer.begin(), buffer.begin() + count);
    bool found = false;
    for (size_t offset = 0; offset + 2 <= received.size();)
    {
        const size_t size = received[offset] | (size_t(received[offset + 1]) << 8);
        CHECK(size > 0 && offset + 2 + size <= received.size());
        if (!size || offset + 2 + size > received.size()) break;
        const auto message = ServerMessage::parse(received.data() + offset + 2, size);
        if (message.type == ServerMessage::Type::Snapshot)
            for (const auto& entity : message.entities)
                if (entity.entity_id == static_cast<net_entity_id>(flower->Id()))
                { found = true; CHECK_NEAR(entity.pos.x, flower->Pos().x, 0.001f); }
        offset += size + 2;
    }
    CHECK(found);
}

TEST(OrderedTickDefersNewWorldParticipationUntilNextFrame)
{
    class CCounter final : public IGameController
    {
      public:
        explicit CCounter(int& count) : counter(count) {}
        int& counter;
        void OnTick(CGameWorld&, float) override { ++counter; }
        std::string_view SnapshotKey() const override { return "test.new_world_counter"; }
    };
    class CCreator final : public IGameController
    {
      public:
        IWorldModule& worlds; CGameWorld*& created; int& count;
        CCreator(IWorldModule& w, CGameWorld*& c, int& n) : worlds(w), created(c), count(n) {}
        void OnTick(CGameWorld&, float) override
        {
            if (created) return;
            created = worlds.CreateWorld("data/maps/test.tmj", world_controller_keys::open);
            CHECK(created != nullptr); if (!created) return;
            created->SetController(std::make_unique<CCounter>(count));
        }
        std::string_view SnapshotKey() const override { return "test.world_creator"; }
    };
    CServerTestAccess fixture;
    auto* worlds = fixture.server.GetWorldModule();
    int count = 0; CGameWorld* created = nullptr;
    fixture.World().SetController(std::make_unique<CCreator>(*worlds, created, count));
    worlds->Tick(0.05f); CHECK(created && count == 0);
    worlds->Tick(0.05f); CHECK(count == 1);
}

TEST(OrderedTickPoisonUsesOnlyItsRemainingLifetime)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 311); world.SetController(nullptr); world.ClearEntitiesForRestore();
    auto* mob = Spawn(world, EMobType::Bee); mob->Health() = 1000.f;
    auto poison = std::make_unique<CPoisonState>(mob, 0.01f, 100.f, ERarity::Common, nullptr);
    auto* first = poison.get(); mob->AddState(std::move(poison));
    first->Tick(0.05f);
    CHECK_NEAR(mob->Health(), 999.f, 0.001f); CHECK_NEAR(first->m_timer, 0.f, 0.00001f);
    mob->RemoveState(first);
    poison = std::make_unique<CPoisonState>(mob, endless, 100.f, ERarity::Common, nullptr);
    auto* permanent = poison.get(); mob->AddState(std::move(poison));
    permanent->Tick(0.05f);
    CHECK_NEAR(mob->Health(), 994.f, 0.001f); CHECK(permanent->m_timer == endless);
}

TEST(OrderedTickContactAttacksAreCapturedBeforeEitherDamageCallback)
{
    class CCounterHit final : public CMob<SMobStats>
    {
      public:
        explicit CCounterHit(CGameWorld& world) : CMob(&world, {2000, 2000}, 30.f, EMobType::Bee, ERarity::Common, SMobStats{})
        { m_base_stats.damage = m_final_stats.damage = 100.f; Health() = 1.f; TickMode() = EEntityTickMode::Always; }
        void OnTickActions(float) override {}
        void OnTakeDamage(float damage, CEntity* source, EDamageType type) override
        {
            m_base_stats.damage = m_final_stats.damage = 0.f;
            CMob<SMobStats>::OnTakeDamage(damage, source, type);
        }
    };
    Session();
    CGameWorld world("data/maps/test.tmj", 312); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    auto* first = world.InsertEntity(std::make_unique<CCounterHit>(world));
    auto* second = world.InsertEntity(std::make_unique<CCounterHit>(world));
    const auto first_ref = first->GetHandle(), second_ref = second->GetHandle();
    world.Tick(0.05f);
    CHECK(world.ResolveEntity(first_ref) == nullptr && world.ResolveEntity(second_ref) == nullptr);
}

TEST(SourcedSlotBansDoNotReleaseOtherStatesOrModePolicy)
{
    CServerTestAccess fixture;
    auto* flower = dynamic_cast<CFlower*>(fixture.Player(fixture.World())->GetEntity());
    CHECK(flower != nullptr); if (!flower) return;
    { CBanSlotState unattached(flower, 1.f, 0, ERarity::Common); CHECK(!flower->GetSlots()[0].IsBanned()); }
    auto a = std::make_unique<CBanSlotState>(flower, 1.f, 0, ERarity::Common); auto* first = a.get();
    auto b = std::make_unique<CBanSlotState>(flower, 3.f, 0, ERarity::Common); auto* second = b.get();
    flower->AddState(std::move(a)); flower->AddState(std::move(b));
    CHECK(flower->GetSlots()[0].IsBanned());
    CHECK(flower->RemoveState(first)); CHECK(flower->GetSlots()[0].IsBanned());
    flower->SetBanned(true, 0);
    CHECK(flower->RemoveState(second)); CHECK(flower->GetSlots()[0].IsBanned());
    flower->SetBanned(false, 0); CHECK(!flower->GetSlots()[0].IsBanned());
}

TEST(SourcedCorruptionRestoresCurrentBaseAfterLastSource)
{
    CServerTestAccess fixture;
    auto* bee = Spawn(fixture.World(), EMobType::Bee); bee->SetTeam(2);
    auto a = std::make_unique<CCorruptionState>(bee, 1.f, ERarity::Common); auto* first = a.get();
    auto b = std::make_unique<CCorruptionState>(bee, 3.f, ERarity::Common); auto* second = b.get();
    bee->AddState(std::move(a)); bee->AddState(std::move(b)); CHECK(bee->Team() == 0);
    bee->RemoveState(first); CHECK(bee->Team() == 0);
    bee->SetTeam(7); CHECK(bee->Team() == 0 && bee->BaseTeam() == 7);
    bee->RemoveState(second); CHECK(bee->Team() == 7);
}

TEST(SourcedDiggingOnlyReloadsAfterLastSuppressionEnds)
{
    CServerTestAccess fixture;
    auto* flower = dynamic_cast<CFlower*>(fixture.Player(fixture.World())->GetEntity());
    CHECK(flower != nullptr); if (!flower) return;
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Basic), ERarity::Common);
    auto a = std::make_unique<CDiggingState>(flower, 1.f, ERarity::Common); auto* first = a.get();
    auto b = std::make_unique<CDiggingState>(flower, 3.f, ERarity::Common); auto* second = b.get();
    flower->AddState(std::move(a)); flower->AddState(std::move(b)); flower->RefreshPetalSuppression();
    const auto suspended_timers = flower->GetSlots()[0].m_reload_timers;
    flower->RemoveState(first);
    CHECK(flower->ArePetalsSuppressed() && flower->HasPendingPetalReload());
    CHECK(flower->GetSlots()[0].m_reload_timers == suspended_timers);
    flower->RemoveState(second);
    CHECK(!flower->ArePetalsSuppressed() && !flower->HasPendingPetalReload());
    CHECK(flower->GetSlots()[0].m_reload_timers == flower->GetSlots()[0].m_reload_durations);
}

TEST(SourcedEffectsRestoreAuthoredPolicySeparatelyFromRuntimeStates)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* flower = dynamic_cast<CFlower*>(fixture.Player(world)->GetEntity());
    CHECK(flower != nullptr); if (!flower) return;
    const auto ref = flower->GetHandle();
    flower->SetTeam(2);
    flower->AddState(std::make_unique<CCorruptionState>(flower, 3.f, ERarity::Common));
    flower->AddState(std::make_unique<CNullificationState>(flower, 3.f, ERarity::Common));
    flower->SetTeam(7);
    flower->AddState(std::make_unique<CBanSlotState>(flower, 3.f, 0, ERarity::Common));
    flower->SetBanned(true, 1);
    flower->AddState(std::make_unique<CBanSlotState>(flower, 3.f, 1, ERarity::Common));
    const auto path = Session().directory / "sourced-effects.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    flower = dynamic_cast<CFlower*>(world.ResolveEntity(ref));
    CHECK(flower != nullptr); if (!flower) return;
    CHECK(flower->Team() == 0 && flower->BaseTeam() == 7);
    CHECK(flower->FindFirstState<CCorruptionState>() && !flower->FindFirstState<CCorruptionState>()->IsRuleManaged());
    CHECK(flower->FindFirstState<CNullificationState>() && !flower->FindFirstState<CNullificationState>()->IsRuleManaged());
    for (auto* state : flower->FindStates<CBanSlotState>()) flower->RemoveState(state);
    CHECK(!flower->GetSlots()[0].IsBanned() && flower->GetSlots()[1].IsBanned());
    for (auto* state : flower->FindStates<CCorruptionState>()) flower->RemoveState(state);
    CHECK(flower->Team() == 7);
}

TEST(ArchitectureBodylessSessionsKeepRoomRulesAndManualRespawnDestination)
{
    CServerTestAccess fixture;
    auto* worlds = fixture.server.GetWorldModule();
    auto* room = worlds->CreateRoomWorld("data/maps/test.tmj", world_controller_keys::open, 7801);
    CHECK(room != nullptr); if (!room) return;
    auto* player = fixture.Player(*room);
    player->GetEntity()->MarkForDestroy(); room->FlushEntityRemovals();
    CHECK(!player->GetEntity() && player->GetWorld() == room);
    CHECK(room->Players().size() == 1 && room->GetPlayerCount() == 1);
    const auto room_id = room->GetId();
    worlds->Tick(game_config::room_idle_destroy_seconds + 1.f);
    CHECK(worlds->FindWorldById(room_id) == room);
    if (worlds->FindWorldById(room_id) != room) return;
    room->SetController(CreateWorldController(world_controller_keys::waiting));
    const auto secondary_before = CAccountDataStore::GetSecondarySlots(player->GetProgressAccountName());
    CAccountDataStore::AddItem(player->GetProgressAccountName(), static_cast<uint8_t>(EPetalType::Basic),
        static_cast<uint8_t>(ERarity::Common), 1);
    SSlotInput equip;
    equip.slot_index = 0; equip.petal_type = static_cast<uint8_t>(EPetalType::Basic);
    equip.rarity = static_cast<uint8_t>(ERarity::Common);
    CServerTestAccess::SendSecondary(fixture, *player, equip);
    CHECK(CAccountDataStore::GetSecondarySlots(player->GetProgressAccountName()) == secondary_before);
    CAccountDataStore::TakeItem(player->GetProgressAccountName(), equip.petal_type, equip.rarity, 1);
    room->SetController(CreateWorldController(world_controller_keys::sandbox));
    CHECK(player->HasIsolatedProgress());
    CServerTestAccess::SendRespawn(fixture, *player);
    CHECK(player->GetEntity() && player->GetEntity()->GameWorld() == room);
    CHECK(player->HasIsolatedProgress());
}

TEST(ArchitectureSameWorldTeleportOnlyAppliesDestinationEnvironment)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); CServerTestAccess::SetShortcutTestTerrain(world);
    auto* player = fixture.Player(world);
    auto* flower = dynamic_cast<CFlower*>(player->GetEntity());
    flower->Pos() = {1900, 1000}; flower->SynchronizeEnvironment();
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    auto& slot = flower->GetSlots()[0]; slot.SpawnCopy(0, flower);
    auto* original_petal = slot.m_p_petals[0]; CHECK(original_petal != nullptr);
    CHECK(world.TransferPlayerEntityToWorld(*player, world, florrbt::Vector2f{2200, 1000}));
    flower->SynchronizeEnvironmentAfterDisplacement();
    CHECK(!flower->IsInShortcut() && slot.m_p_petals[0] == original_petal);
    CHECK(!original_petal->IsMarkedForDes());
    CHECK(world.TransferPlayerEntityToWorld(*player, world, florrbt::Vector2f{2050, 1000}));
    CHECK(flower->IsInShortcut() && flower->ArePetalsSuppressed() && slot.m_p_petals[0] == nullptr);
    CHECK(world.TransferPlayerEntityToWorld(*player, world, florrbt::Vector2f{2200, 1000}));
    CHECK(!flower->IsInShortcut() && slot.m_reload_timers[0] > 0.f);
    const auto timers = slot.m_reload_timers;
    flower->SynchronizeEnvironmentAfterDisplacement(); CHECK(slot.m_reload_timers == timers);
}

TEST(ArchitectureEouMatchAnnouncementsStayInTheirWorld)
{
    CServerTestAccess fixture;
    auto* outside = fixture.Player(fixture.World(), 1);
    auto* room = fixture.server.GetWorldModule()->CreateWorld("data/maps/eou_small.tmj", world_controller_keys::ant_kingdom);
    CHECK(room != nullptr); if (!room) return;
    auto* inside = fixture.Player(*room, 2);
    auto* controller = dynamic_cast<CAntKingdomController*>(room->GetController());
    CHECK(controller != nullptr); if (!controller) return;
    controller->OnPlayerArrivalCommitted(*room, *inside);
    auto* team = controller->FindTeam(controller->PlayerTeam(inside->GetId()));
    CHECK(team != nullptr); if (!team) return;
    const auto queen_ref = team->Queen();
    auto* queen = room->GetEntity(queen_ref.id, queen_ref.generation);
    CHECK(queen != nullptr); if (!queen) return;
    outside->m_send_buffer.clear(); outside->m_send_offset = 0;
    inside->m_send_buffer.clear(); inside->m_send_offset = 0;
    queen->Health() = 0.f; queen->MarkForDestroy(EEntityRemovalReason::Defeated);
    room->FlushEntityRemovals();
    const auto has_announcement = [](const auto& messages) {
        return std::any_of(messages.begin(), messages.end(), [](const auto& message) {
            return message.type == ServerMessage::Type::Chat && message.chat.message.find("has fallen") != std::string::npos;
        });
    };
    CHECK(has_announcement(CServerTestAccess::QueuedMessages(*inside)));
    CHECK(!has_announcement(CServerTestAccess::QueuedMessages(*outside)));
}

TEST(ArchitectureSquadSurvivesRetainedReconnectAndAvatarReplacement)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); world.ConfigureExtensions({true, false, false});
    auto* first = fixture.Player(world, 1);
    auto* second = fixture.Player(world, 2);
    auto* squads = fixture.server.Extensions().Globals().Find<CPartyExtension>();
    CHECK(squads != nullptr); if (!squads) return;
    CHECK(squads->Invite(*first, *second)); CHECK(squads->Accept(*second, *first));
    CHECK(squads->Members(*first).size() == 2);
    second->DetachSocket(); fixture.server.Extensions().RefreshAllWorlds();
    CHECK(squads->Members(*first).size() == 2);
    second->GetEntity()->MarkForDestroy(); world.FlushEntityRemovals();
    fixture.server.Extensions().RefreshAllWorlds();
    CHECK(!second->GetEntity() && squads->Members(*first).size() == 2);
    second->AttachSocket(sf::TcpSocket{});
    CPlayerLifecycleService lifecycle;
    CHECK(lifecycle.SpawnPlayer(*second, world, EPlayerSpawnReason::Respawn));
    fixture.server.Extensions().RefreshAllWorlds();
    CHECK(squads->Members(*first).size() == 2);
    CHECK(fixture.server.GetNetworkModule()->KickPlayer(second->GetId(), "offline fixture cleanup"));
    fixture.server.Extensions().RefreshAllWorlds();
    CHECK(squads->Members(*first).size() == 1);
}

TEST(ArchitectureInternalProgressNamesCannotRegisterOrAuthenticate)
{
    Session();
    SAccountAuthWork work;
    std::string error;
    CHECK(!CAccountDataStore::PrepareAuthentication("@sandbox:public", "test-password", "", "127.0.0.1", true, work, &error));
    CHECK(!CAccountDataStore::CommitRegistration("@sandbox:public", "offline-fixture-hash", "", "127.0.0.1", &error));
    const std::string temporary = "@sandbox:architecture-auth-boundary";
    CHECK(CAccountDataStore::SandboxCloneAccount(temporary, "runtime-player"));
    // Even a matching trusted IP must never authenticate a progress-store key.
    CHECK(!CAccountDataStore::PrepareAuthentication(temporary, "", "", "127.0.0.1", false, work, &error));
    CHECK(work.account_name.empty());
    CAccountDataStore::RemoveSandboxAccount(temporary);
}

TEST(ArchitectureTilesetCollectionsAndMixedSourcesLoadWithoutMissingWalls)
{
    for (const auto* name : {"garden", "garden_walltest", "ant_hel", "eou_small", "test", "waiting_room"})
    {
        const auto map = LoadMapFromTmj(std::string("data/maps/") + name + ".tmj");
        CHECK(map && map->width > 0 && map->height > 0);
    }
    std::string error;
    auto root = LoadJsonFile("data/maps/test.tmj", error);
    CHECK(root != nullptr); if (!root) return;
    auto tilesets = MakeJsonArray();
    CJsonOwner embedded(json_loads(R"({"firstgid":1,"columns":0,"name":"inline","tilecount":1,
        "tilewidth":256,"tileheight":256,"tiles":[{"id":0,"image":"unused.svg",
        "imagewidth":256,"imageheight":256,"objectgroup":{"draworder":"index","name":"",
        "opacity":1,"type":"objectgroup","visible":true,"objects":[{"id":1,"name":"","type":"",
        "visible":true,"x":0,"y":0,"width":256,"height":256,"rotation":0}]}}]})", 0, nullptr));
    CHECK(embedded != nullptr); if (!embedded) return;
    CHECK(AppendJson(tilesets.get(), embedded.release()));
    const auto external = std::filesystem::absolute("data/tiles/tileset.tsj").generic_string();
    CHECK(AppendJson(tilesets.get(), json_pack("{s:i,s:s}", "firstgid", 145, "source", external.c_str())));
    json_object_set_new(root.get(), "tilesets", tilesets.release());
    json_t* layers = json_object_get(root.get(), "layers");
    bool first = true;
    for (size_t i = 0; i < json_array_size(layers); ++i)
    {
        json_t* layer = json_array_get(layers, i);
        if (CSnapshotReader(layer).String("type") != "tilelayer") continue;
        const size_t count = json_array_size(json_object_get(layer, "data"));
        auto data = MakeJsonArray();
        for (size_t j = 0; j < count; ++j)
            CHECK(AppendJson(data.get(), json_integer(first && j == 0 ? 1 : first && j == 1 ? 169 : 0)));
        json_object_set_new(layer, "data", data.release()); first = false;
    }
    const auto path = Session().directory / "mixed-tilesets.tmj";
    CHECK(WriteJsonAtomically(path, root.get(), error));
    const auto map = LoadMapFromTmj(path.string());
    CHECK(map != nullptr); if (!map) return;
    CHECK(map->walls.size() == 2); // one inline + one external, each emitted exactly once
}

TEST(ArchitectureAccountSaveRetriesWithoutAnotherGameplayMutation)
{
    const auto directory = Session().directory;
    std::string error;
    CHECK(CAccountDataStore::Flush(&error));
    const auto path = directory / "accounts.json", blocked_temp = directory / "accounts.json.tmp";
    std::filesystem::create_directory(blocked_temp); // block only the fixture's temporary output
    const int original_points = CAccountDataStore::GetTalentPoints("runtime-player");
    int points = original_points + 7;
    CAccountDataStore::SetTalentPoints("runtime-player", points);
    CHECK(!CAccountDataStore::Flush(&error));
    CHECK(!error.empty());
    points += 5;
    CAccountDataStore::SetTalentPoints("runtime-player", points);
    CHECK(!CAccountDataStore::Flush(&error)); // a newer generation must supersede the failed one
    const auto generation = CAccountDataStore::CurrentSaveFence();
    std::filesystem::remove(blocked_temp);
    // Poll the read-only durable fence rather than repeatedly opening the
    // destination file. Windows readers can deny atomic replacement while
    // JSON parsing holds that file open, perturbing the retry being tested.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!CAccountDataStore::IsSaveCommitted(generation) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(CAccountDataStore::IsSaveCommitted(generation));
    bool persisted = false;
    const auto json = CJsonValue::LoadFromFile(path);
    if (json)
        if (const auto* accounts = json->Find("accounts"); accounts && accounts->IsArray())
            for (const auto& account : accounts->AsArray())
                if (const auto* name = account.Find("name"); name && name->AsString() == "runtime-player")
                    if (const auto* value = account.Find("talent_points")) persisted = value->AsInt() == points;
    CHECK(persisted); // no Save/Flush/new mutation after repairing the filesystem
    CAccountDataStore::SetTalentPoints("runtime-player", original_points);
    CHECK(CAccountDataStore::Flush(&error));
}

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

TEST(CombatStateBarrierKeepsTriggerDamageAndAllowsExplicitProtection)
{
    Session();
    struct CShieldOnHit final : CMob<SMobStats>
    {
        EStateApplyTiming timing = EStateApplyTiming::EventBoundary;
        bool visible_inside = false;
        explicit CShieldOnHit(CGameWorld& world) : CMob(&world, {1000.f, 1000.f}, 10.f, EMobType::Bee, ERarity::Common, {})
        { TickMode() = EEntityTickMode::Always; Health() = m_base_stats.max_health = m_final_stats.max_health = 100.f; }
        void OnTakeDamage(float damage, CEntity* attacker, EDamageType type) override
        {
            AddState(std::make_unique<CInvincibleState>(this, 1.f, ERarity::Common), timing);
            visible_inside = HasState<CInvincibleState>();
            CMob<SMobStats>::OnTakeDamage(damage, attacker, type);
        }
    };
    CGameWorld world("data/maps/test.tmj", 607); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    auto owned = std::make_unique<CShieldOnHit>(world); auto* mob = owned.get(); world.InsertEntity(std::move(owned));
    mob->TakeDamage(20.f, nullptr, EDamageType::Poison);
    CHECK(!mob->visible_inside && mob->HasState<CInvincibleState>()); CHECK_NEAR(mob->Health(), 80.f, 0.001);
    mob->Tick(0.05f); CHECK_NEAR(mob->Health(), 80.f, 0.001);
    for (auto* state : mob->FindStates<CInvincibleState>()) mob->RemoveState(state);
    mob->Health() = 1.f; mob->m_base_stats.damage = mob->m_final_stats.damage = 100.f; mob->SetTeam(1);
    auto peer = std::make_unique<CShieldOnHit>(world); peer->Health() = 1.f;
    peer->m_base_stats.damage = peer->m_final_stats.damage = 100.f; peer->SetTeam(2);
    const auto first_ref = mob->GetHandle(), second_ref = world.InsertEntity(std::move(peer))->GetHandle();
    world.Tick(0.05f);
    CHECK(!world.ResolveEntity(first_ref) && !world.ResolveEntity(second_ref));
    auto protected_actor = std::make_unique<CShieldOnHit>(world); auto* protected_mob = protected_actor.get();
    protected_mob->timing = EStateApplyTiming::DamageProtection; world.InsertEntity(std::move(protected_actor));
    protected_mob->TakeDamage(20.f, nullptr, EDamageType::Poison);
    CHECK(protected_mob->visible_inside); CHECK_NEAR(protected_mob->Health(), 100.f, 0.001);
}

TEST(StateConstructionIsPureAndMergingWaitsForTheEventBarrier)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 608); world.SetController(nullptr); world.ClearEntitiesForRestore();
    auto* mob = Spawn(world, EMobType::Bee); mob->Health() = 100.f;
    mob->AddState(std::make_unique<CPoisonState>(mob, 1.f, 10.f, ERarity::Common, nullptr));
    auto* old = mob->FindFirstState<CPoisonState>(); CHECK(old); if (!old) return;
    const auto old_source = old->EffectSourceId();
    { auto discarded = std::make_unique<CPoisonState>(mob, 2.f, 20.f, ERarity::Rare, nullptr); }
    CHECK(mob->FindStateBySource(old_source) == old && old->m_timer == 1.f);
    { auto discarded = std::make_unique<CUndeadState>(mob, 1.f, ERarity::Common, -1); }
    CHECK(mob->Health() == 100.f && !mob->IsMarkedForDes());
    mob->AddState(std::make_unique<CPsionicConnectionState>(mob, 1.f, ERarity::Common));
    auto* psi = mob->FindFirstState<CPsionicConnectionState>(); CHECK(psi); if (!psi) return;
    { auto discarded = std::make_unique<CPsionicConnectionState>(mob, endless, ERarity::Super); }
    CHECK(psi->m_timer == 1.f && psi->m_rarity == ERarity::Common);
    {
        CCombatEventScope event(&world.CombatEvents());
        mob->AddState(std::make_unique<CPoisonState>(mob, 2.f, 20.f, ERarity::Rare, nullptr));
        mob->AddState(std::make_unique<CPsionicConnectionState>(mob, endless, ERarity::Super));
        CHECK(mob->FindStateBySource(old_source) == old && psi->m_timer == 1.f);
    }
    CHECK(!mob->FindStateBySource(old_source));
    CHECK(mob->FindFirstState<CPoisonState>()->GetBasicDmg() == 20.f);
    CHECK(psi->m_timer == endless && psi->m_rarity == ERarity::Super);
    auto prototype = CreateMob(EMobType::PlayerFlower, &world, {2000.f, 2000.f}, ERarity::Common);
    auto* flower = dynamic_cast<CMobBase*>(world.InsertEntity(std::move(prototype))); CHECK(flower); if (!flower) return;
    CHECK(!flower->GetStates().empty());
    for (const auto& state : flower->GetStates()) CHECK(state->ResolveOwner() == flower);
}

TEST(IndependentFlowerStatesSurviveLoadoutMaintenanceAndBansPublishAtBarrier)
{
    Session();
    CGameWorld world("data/maps/test.tmj", 609); world.SetController(nullptr); world.ClearEntitiesForRestore();
    SFlowerStats stats{}; stats.max_health = 100.f;
    auto owned = std::make_unique<CFlower>(&world, florrbt::Vector2f{1000.f, 1000.f}, 30.f, EMobType::MechaFlower, ERarity::Common, stats);
    auto* flower = owned.get(); world.InsertEntity(std::move(owned)); flower->SetTeam(5);
    flower->SetPetalSlotCapacity(2); flower->SetPetalSlotCount(2);
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Nullification), ERarity::Common);
    flower->AddState(std::make_unique<CNullificationState>(flower, 0.1f, ERarity::Super));
    flower->AddState(std::make_unique<CCorruptionState>(flower, 0.1f, ERarity::Common));
    flower->Tick(0.05f);
    CHECK(flower->Team() == 0 && flower->FindStates<CNullificationState>().size() == 2);
    flower->ForceUnequipPetal(0); flower->Tick(0.05f);
    CHECK(flower->FindStates<CNullificationState>().size() == 1 && flower->Team() == 0);
    CHECK(!flower->FindFirstState<CNullificationState>()->IsRuleManaged());
    flower->Tick(0.05f);
    CHECK(!flower->HasState<CNullificationState>() && !flower->HasState<CCorruptionState>() && flower->Team() == 5);
    flower->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Leaf), ERarity::Common);
    auto& slot = flower->GetSlots()[0]; slot.SpawnCopy(0, flower); auto* petal = slot.m_p_petals[0]; CHECK(petal);
    {
        CCombatEventScope event(&world.CombatEvents());
        flower->AddState(std::make_unique<CBanSlotState>(flower, 1.f, 0, ERarity::Common));
        CHECK(!slot.IsBanned() && slot.m_p_petals[0] == petal);
    }
    CHECK(slot.IsBanned() && slot.m_p_petals[0] == nullptr);
    CHECK(petal && petal->IsMarkedForDes() && petal->RemovalReason() == EEntityRemovalReason::OwnerRemoved);
}

TEST(BodyGeometryPublishesShortcutRestrictionsBeforeActionsAndDamagesOnce)
{
    Session();
    struct SRestoreFloat { float& value; float old; ~SRestoreFloat() { value = old; } } push{
        game_config::nonflower_mob_overlap_push_multiplier, game_config::nonflower_mob_overlap_push_multiplier};
    struct SRestoreInt { int& value; int old; ~SRestoreInt() { value = old; } } max_hits{
        game_config::entity_max_physical_hits_per_tick, game_config::entity_max_physical_hits_per_tick}, min_hits{
        game_config::entity_mob_mob_hits_per_tick, game_config::entity_mob_mob_hits_per_tick};
    game_config::nonflower_mob_overlap_push_multiplier = 1.f;
    game_config::entity_max_physical_hits_per_tick = game_config::entity_mob_mob_hits_per_tick = 1;
    struct CActor : CMob<SMobStats>
    {
        CActor(CGameWorld& world, florrbt::Vector2f pos, float radius) : CMob(&world, pos, radius, EMobType::Bee, ERarity::Common, {})
        {
            TickMode() = EEntityTickMode::Always;
            Health() = m_base_stats.max_health = m_final_stats.max_health = 1000.f;
            Mass() = m_base_stats.mass = m_final_stats.mass = 1.f;
        }
    };
    struct CPusher final : CActor
    {
        using CActor::CActor;
        bool IsCollisionPositionLocked() const override { return true; }
        bool CollidesWithWalls() const override { return false; }
    };
    struct CObserve final : IController
    {
        const CEntity* other = nullptr;
        bool shortcut = false;
        float damage = 0.f, other_health = 0.f;
        bool IsPlayerControlled() const override { return true; }
        void OnTick(CMobBase*, float) override {}
        void OnActions(CMobBase* mob, float) override
        { shortcut = mob->IsInShortcut(); damage = mob->GetFinalStats()->damage; other_health = other->Health(); }
        std::string_view SnapshotKey() const override { return "test.geometry_view"; }
    };
    for (bool reversed : {false, true})
    {
        CGameWorld world("data/maps/test.tmj", 606); world.SetController(nullptr); world.ClearEntitiesForRestore();
        CServerTestAccess::SetShortcutTestTerrain(world);
        auto owned = std::make_unique<CActor>(world, florrbt::Vector2f{1900.f, 1000.f}, 30.f);
        owned->m_base_stats.damage = 1.f; owned->m_final_stats.damage = 99.f; owned->SetTeam(1);
        auto pusher = std::make_unique<CPusher>(world, florrbt::Vector2f{1800.f, 1000.f}, 250.f); pusher->SetTeam(2);
        auto* actor = owned.get(); auto* obstacle = pusher.get();
        auto control = std::make_unique<CObserve>(); auto* observation = control.get(); control->other = obstacle;
        actor->SetController(std::move(control));
        if (reversed) { world.InsertEntity(std::move(pusher)); world.InsertEntity(std::move(owned)); }
        else { world.InsertEntity(std::move(owned)); world.InsertEntity(std::move(pusher)); }
        world.Tick(0.05f);
        CHECK(actor->IsInShortcut() && observation->shortcut);
        CHECK_NEAR(observation->damage, 1.f, 0.0001);
        CHECK_NEAR(observation->other_health, 1000.f, 0.0001);
        CHECK_NEAR(obstacle->Health(), 999.f, 0.0001);
        CHECK(world.GetLastActiveEntityCount() >= 2);
    }
}

TEST(NewbornEntitiesWaitUntilNextFrameForMovementAndContact)
{
    Session();
    struct CActor final : CMob<SMobStats>
    {
        int ticks = 0;
        CActor(CGameWorld& world, float damage) : CMob(&world, {1000.f, 1000.f}, 10.f, EMobType::Bee, ERarity::Common, {})
        {
            TickMode() = EEntityTickMode::Always;
            Health() = m_base_stats.max_health = m_final_stats.max_health = 100.f;
            m_base_stats.damage = m_final_stats.damage = damage;
        }
        void OnTickActions(float) override { ++ticks; }
    };
    struct CSpawner final : CEntity
    {
        SEntityHandle spawned;
        explicit CSpawner(CGameWorld& world) : CEntity(&world, 0, 0, 0, MakeEntityType(EEntityType::Effect, 0))
        { TickMode() = EEntityTickMode::Always; }
        bool CanCollide() const override { return false; }
        void OnTickActions(float) override
        {
            if (spawned.IsValid()) return;
            auto actor = std::make_unique<CActor>(*GameWorld(), 200.f); actor->SetTeam(2);
            spawned = GameWorld()->InsertEntity(std::move(actor))->GetHandle();
        }
    };
    CGameWorld world("data/maps/test.tmj", 603); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    auto target = std::make_unique<CActor>(world, 0.f); target->SetTeam(1);
    const auto victim = world.InsertEntity(std::move(target))->GetHandle();
    auto spawner = std::make_unique<CSpawner>(world); auto* source = spawner.get(); world.InsertEntity(std::move(spawner));
    world.Tick(0.05f);
    CHECK(world.ResolveEntity(victim) && world.ResolveEntity(victim)->Health() == 100.f);
    const auto* born = dynamic_cast<CActor*>(world.ResolveEntity(source->spawned));
    CHECK(born && born->ticks == 0);
    world.Tick(0.05f);
    CHECK(world.ResolveEntity(victim) == nullptr);
    born = dynamic_cast<CActor*>(world.ResolveEntity(source->spawned)); CHECK(born && born->ticks == 1);
}

TEST(PetalFrameDriverDoesNotDoubleAdvanceAfterDetachingAndNewCopiesWait)
{
    Session();
    struct CDetachingPetal final : CPetal
    {
        int ticks = 0;
        CDetachingPetal(CFlower& owner, const SPetalStats& stats) : CPetal(stats.radius, &owner, 0, stats, EPetalType::Basic)
        { m_rarity = ERarity::Common; }
        void OnTickActions(float dt) override
        {
            ++ticks; CPetal::OnTickActions(dt);
            SkipWorldTick() = false; m_detach_from_slot = true;
        }
    };
    CGameWorld world("data/maps/test.tmj", 604); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    SFlowerStats stats{}; stats.max_health = 100.f;
    auto owned = std::make_unique<CFlower>(&world, florrbt::Vector2f{1000.f, 1000.f}, 30.f, EMobType::MechaFlower, ERarity::Common, stats);
    owned->AllowSkipTick() = false; owned->SetPetalSlotCapacity(2); owned->SetPetalSlotCount(2);
    auto* flower = owned.get(); world.InsertEntity(std::move(owned));
    const auto* proto = FindPetalPrototype(EPetalType::Basic); CHECK(proto); if (!proto) return;
    flower->LoadPetalSlot(0, proto, ERarity::Common); flower->LoadPetalSlot(1, proto, ERarity::Common);
    auto custom = std::make_unique<CDetachingPetal>(*flower, proto->m_p_behavior->GetPetalStats(ERarity::Common));
    auto* detached = custom.get(); world.InsertEntity(std::move(custom));
    auto& slots = flower->GetSlots();
    CHECK(slots.size() >= 2 && !slots[0].m_p_petals.empty() && !slots[1].m_p_petals.empty());
    slots[0].m_p_petals[0] = detached; slots[0].m_reload_timers[0] = 0.f; slots[1].m_reload_timers[0] = 0.f;
    world.Tick(0.05f);
    CHECK(detached->ticks == 1 && slots[0].m_p_petals[0] == nullptr);
    auto* born = slots[1].m_p_petals[0]; CHECK(born); if (!born) return;
    CHECK_NEAR(born->m_lifetime, 0.f, 0.00001);
    world.Tick(0.05f);
    CHECK(detached->ticks == 2); CHECK_NEAR(born->m_lifetime, 0.05f, 0.00001);
}

TEST(ManualAttackInputSurvivesMobAnimationFlagsAndResetsExplicitly)
{
    Session();
    struct CActor final : CAttackableMob<SMobStats>
    {
        int attacks = 0;
        explicit CActor(CGameWorld& world) : CAttackableMob(&world, {1000.f, 1000.f}, 10.f, EMobType::Bee, ERarity::Common, {})
        { Health() = m_base_stats.max_health = m_final_stats.max_health = 100.f; }
        void OnTickActions(float) override { SetAttacking(false); }
        bool TryAttack(CEntity*) override { ++attacks; return true; }
    };
    CGameWorld world("data/maps/test.tmj", 605); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    auto actor = std::make_unique<CActor>(world); auto* mob = actor.get(); world.InsertEntity(std::move(actor));
    auto control = std::make_unique<CPlayerController>(); auto* input = control.get(); mob->SetController(std::move(control));
    ClientOperate press; press.type = ClientOperate::Type::Chores; press.is_attacking = true;
    input->PushOperate(press); mob->Tick(0.05f); CHECK(mob->attacks == 1);
    mob->Tick(0.05f); CHECK(mob->attacks == 2);
    input->ResetOperate(); mob->Tick(0.05f); CHECK(mob->attacks == 2);
}

TEST(AiActionEmissionUsesPostMovementPosition)
{
    Session();
    struct SRestoreInterval { float& value; float old; ~SRestoreInterval() { value = old; } };
    SRestoreInterval spider{game_config::mob_spider_web_interval, game_config::mob_spider_web_interval};
    SRestoreInterval bumble{game_config::mob_bumblebee_pollen_interval, game_config::mob_bumblebee_pollen_interval};
    game_config::mob_spider_web_interval = game_config::server_fixed_dt;
    game_config::mob_bumblebee_pollen_interval = game_config::server_fixed_dt;
    for (const auto type : {EMobType::Spider, EMobType::BumbleBee})
    {
        CGameWorld world("data/maps/test.tmj", 599); world.SetController(nullptr); world.ClearEntitiesForRestore();
        CServerTestAccess::ClearTestWalls(world);
        auto* mob = Spawn(world, type, ERarity::Legendary, {1000.f, 1000.f});
        auto* target = Spawn(world, EMobType::Bee, ERarity::Common, {1600.f, 1000.f});
        mob->SetTeam(1); target->SetTeam(2);
        const auto before = mob->Pos(); const auto count = world.GetEntityCount();
        mob->Tick(game_config::server_fixed_dt);
        CHECK(mob->Pos() != before);
        CHECK(world.GetEntityCount() == count + 1);
        world.ForEachEntity([&](const CEntity* entity) {
            if (entity != mob && entity != target) CHECK(entity->Pos() == mob->Pos());
        });
    }
}

TEST(PlayerSkillRequestsUsePostMovementPositionAndKeepTheirFullNewWindup)
{
    Session();
    class CSkillProbe final : public CSkillCasterMob<SMobStats>
    {
      public:
        int casts = 0;
        float windup = 0.f;
        bool die_during_actions = false;
        florrbt::Vector2f cast_position;
        explicit CSkillProbe(CGameWorld& world) : CSkillCasterMob(&world, {1000.f, 1000.f}, 10.f, EMobType::Bee, ERarity::Common, {})
        {
            Health() = m_base_stats.max_health = m_final_stats.max_health = 100.f;
            m_base_stats.max_velocity = m_final_stats.max_velocity = 100.f;
            m_base_stats.acceleration = m_final_stats.acceleration = 1000.f;
        }
        int GetSkillCount() const override { return 1; }
        bool CanCastSkill(int index) const override { return index == 0 && windup <= 0.f; }
        bool TryCastSkill(int index, CEntity*) override
        {
            if (!CanCastSkill(index)) return false;
            ++casts; cast_position = Pos(); windup = 0.5f; return true;
        }
        void OnTickActions(float dt) override
        {
            CSkillCasterMob<SMobStats>::OnTickActions(dt);
            windup = std::max(0.f, windup - dt);
            if (die_during_actions) Health() = 0.f;
        }
    };
    CGameWorld world("data/maps/test.tmj", 600); world.SetController(nullptr); world.ClearEntitiesForRestore();
    CServerTestAccess::ClearTestWalls(world);
    auto owned = std::make_unique<CSkillProbe>(world); auto* mob = owned.get(); world.InsertEntity(std::move(owned));
    auto controller = std::make_unique<CPlayerController>(); auto* input = controller.get(); mob->SetController(std::move(controller));
    ClientOperate movement; movement.type = ClientOperate::Type::Input;
    movement.move_y = static_cast<std::int8_t>(game_config::player_input_axis_max);
    ClientOperate skill; skill.type = ClientOperate::Type::Skill; skill.skill_index = 0;
    input->PushOperate(movement); input->PushOperate(skill);
    const auto before = mob->Pos(); mob->Tick(0.05f);
    CHECK(mob->casts == 1 && mob->cast_position == mob->Pos() && mob->Pos() != before);
    CHECK_NEAR(mob->windup, 0.5f, 0.00001);
    mob->Tick(0.05f); CHECK(mob->casts == 1); CHECK_NEAR(mob->windup, 0.45f, 0.00001);
    CServerTestAccess::SetShortcutTestTerrain(world);
    mob->Pos() = {2050.f, 1000.f}; mob->SynchronizeEnvironment();
    CHECK(mob->IsInShortcut()); mob->windup = 0.f; input->PushOperate(skill); mob->Tick(0.05f);
    CHECK(mob->casts == 2 && mob->IsInShortcut());
    mob->windup = 0.f; input->PushOperate(skill); input->ResetOperate(); mob->Tick(0.05f);
    CHECK(mob->casts == 2);
    mob->die_during_actions = true; input->PushOperate(skill); mob->Tick(0.05f);
    CHECK(mob->IsDead() && mob->casts == 2);
    mob->die_during_actions = false; mob->Health() = 100.f; mob->Tick(0.05f);
    CHECK(mob->casts == 2);
}

TEST(PoisonReplacementKeepsTheAlreadyScheduledContinuousDamage)
{
    Session();
    struct CAlwaysMob final : CMob<SMobStats>
    {
        CAlwaysMob(CGameWorld& world, florrbt::Vector2f position) : CMob(&world, position, 10.f, EMobType::Bee, ERarity::Common, {})
        { TickMode() = EEntityTickMode::Always; Health() = m_base_stats.max_health = m_final_stats.max_health = 100.f; }
    };
    struct CRefresh final : IController
    {
        SEntityHandle target;
        void OnTick(CMobBase*, float) override {}
        std::string_view SnapshotKey() const override { return "test.poison_refresh"; }
        void OnActions(CMobBase* owner, float) override
        {
            auto* victim = dynamic_cast<CMobBase*>(owner->GameWorld()->ResolveEntity(target));
            if (!victim) return;
            auto poison = std::make_unique<CPoisonState>(victim, 1.f, 20.f, ERarity::Common, owner);
            if (poison->IsValid()) victim->AddState(std::move(poison));
        }
    };
    for (bool reversed : {false, true})
    {
        CGameWorld world("data/maps/test.tmj", 602); world.SetController(nullptr); world.ClearEntitiesForRestore();
        CServerTestAccess::ClearTestWalls(world);
        auto a = std::make_unique<CAlwaysMob>(world, florrbt::Vector2f{500.f, 500.f});
        auto b = std::make_unique<CAlwaysMob>(world, florrbt::Vector2f{1000.f, 1000.f});
        auto* source = a.get(); auto* target = b.get();
        if (reversed) { world.InsertEntity(std::move(b)); world.InsertEntity(std::move(a)); }
        else { world.InsertEntity(std::move(a)); world.InsertEntity(std::move(b)); }
        auto refresh = std::make_unique<CRefresh>(); refresh->target = target->GetHandle(); source->SetController(std::move(refresh));
        target->AddState(std::make_unique<CPoisonState>(target, 1.f, 10.f, ERarity::Common, source));
        world.Tick(0.05f); CHECK_NEAR(target->Health(), 99.5f, 0.0001);
        world.Tick(0.05f); CHECK_NEAR(target->Health(), 98.5f, 0.0001);
        world.Tick(0.05f); CHECK_NEAR(target->Health(), 97.5f, 0.0001);
    }
}

TEST(StateFrameExpiresBeforeIntentAndDoesNotAgeNewEffectsByEntityOrder)
{
    Session();
    struct CAlwaysMob final : CMob<SMobStats>
    {
        CAlwaysMob(CGameWorld& world, florrbt::Vector2f position)
            : CMob(&world, position, 10.f, EMobType::Bee, ERarity::Common, {})
        {
            TickMode() = EEntityTickMode::Always;
            m_base_stats.max_health = m_final_stats.max_health = Health() = 100.f;
            m_base_stats.damage = m_final_stats.damage = 0.f;
        }
    };
    struct CProbeController final : IController
    {
        std::function<void(CMobBase*)> action;
        void OnTick(CMobBase* mob, float) override { action(mob); }
        std::string_view SnapshotKey() const override { return "test.state_frame"; }
    };
    for (bool reversed : {false, true})
    {
        CGameWorld world("data/maps/test.tmj", 601); world.SetController(nullptr); world.ClearEntitiesForRestore();
        CServerTestAccess::ClearTestWalls(world);
        auto first = std::make_unique<CAlwaysMob>(world, florrbt::Vector2f{500.f, 500.f});
        auto second = std::make_unique<CAlwaysMob>(world, florrbt::Vector2f{1000.f, 1000.f});
        auto* source = first.get(); auto* target = second.get();
        if (reversed) { world.InsertEntity(std::move(second)); world.InsertEntity(std::move(first)); }
        else { world.InsertEntity(std::move(first)); world.InsertEntity(std::move(second)); }
        source->AddState(std::make_unique<CInvincibleState>(source, 0.f, ERarity::Common));
        target->AddState(std::make_unique<CInvincibleState>(target, 0.01f, ERarity::Common));
        bool emitted = false;
        auto controller = std::make_unique<CProbeController>();
        controller->action = [&](CMobBase* mob) {
            CHECK(!mob->HasState<CInvincibleState>());
            if (emitted) return;
            emitted = true;
            target->AddState(std::make_unique<CPoisonState>(target, 0.02f, 20.f, ERarity::Common, source));
        };
        source->SetController(std::move(controller));
        world.Tick(0.05f);
        CHECK(emitted && target->HasState<CInvincibleState>());
        const auto* poison = target->FindFirstState<CPoisonState>();
        CHECK(poison); if (!poison) continue;
        CHECK_NEAR(poison->m_timer, 0.02, 0.00001);
        CHECK_NEAR(target->Health(), 100.f, 0.0001);
        world.Tick(0.05f);
        CHECK(!target->HasState<CInvincibleState>());
        CHECK_NEAR(target->Health(), 99.6f, 0.0001);
        poison = target->FindFirstState<CPoisonState>();
        CHECK(poison && poison->m_timer == 0.f);
        world.Tick(0.05f);
        CHECK(!target->HasState<CPoisonState>());
        CHECK_NEAR(target->Health(), 99.6f, 0.0001);
    }
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
    CHECK(flower->HasState<CDiggingState>());
    flower->BeginStateTick();
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
    flower->BeginStateTick();
    flower->TickStates(0.3f);
    CHECK(flower->ArePetalsSuppressed());
    flower->BeginStateTick();
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
            auto data = MakeJsonObject(); CSnapshotWriter writer(data.get()); second->Terrain().CaptureSnapshot(writer);
            edited = json_array_size(json_object_get(data.get(), "terrain_dirty")) > 0 ||
                json_array_size(json_object_get(data.get(), "terrain_wall_removes")) > 0;
        }
    CHECK(edited);
    CHECK(!has_terrain_message(*observer));
    first.GetController()->OnTick(first, 1.f);
    auto data = MakeJsonObject(); CSnapshotWriter writer(data.get()); first.Terrain().CaptureSnapshot(writer);
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
        void OnTickActions(float) override { ++ticks; }
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
    enemy->SetTeam(123);
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
    source->SetTeam(123); target->SetTeam(456);
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

TEST(PossessionSameWorldKeepsSceneAndControlThroughCleanupAndTicks)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* network = fixture.server.GetNetworkModule();
    CServerTestAccess::ClearTestWalls(world);
    auto* target = Spawn(world, EMobType::Bee, ERarity::Super, {1500.f, 1000.f});
    const auto handle = target->GetHandle();
    CHECK(network->AssignPlayerEntity(player->GetId(), world, target->Id()));
    auto messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::Inventory; }));
    CHECK(std::any_of(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::OwnerState; }));
    CHECK(std::none_of(messages.begin(), messages.end(), [](const auto& msg) {
        return msg.type == ServerMessage::Type::Welcome || msg.type == ServerMessage::Type::TerrainDelta || msg.type == ServerMessage::Type::TerrainWallRemove;
    }));
    world.FlushEntityRemovals();
    for (int i = 0; i < 40; ++i)
    {
        target->Tick(0.05f);
        CPlayerLifecycleService{}.RespawnDeadControlledEntities(network->GetPlayers(), world, *network);
        CHECK(player->GetEntity() == world.ResolveEntity(handle));
    }
    CHECK(player->IsConnected() && target->IsPlayerControlled() && player->HasNpcInventory());
    player->m_send_buffer.clear(); player->m_send_offset = 0;
    CHECK(world.TransferPlayerEntityToWorld(*player, world, florrbt::Vector2f{1600.f, 1000.f}));
    messages = CServerTestAccess::QueuedMessages(*player);
    CHECK(std::none_of(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::Welcome; }));
    CHECK(player->GetEntity() == target);
}

TEST(PossessionCrossWorldStillInitializesDestinationScene)
{
    CServerTestAccess fixture; auto* player = fixture.Player(fixture.World());
    auto* destination = fixture.server.GetWorldModule()->CreateWorld("data/maps/test.tmj", world_controller_keys::open);
    CHECK(destination); if (!destination) return;
    auto* target = Spawn(*destination, EMobType::MechaFlower, ERarity::Super);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), *destination, target->Id()));
    const auto messages = CServerTestAccess::QueuedMessages(*player);
    const auto welcome = std::find_if(messages.begin(), messages.end(), [](const auto& msg) { return msg.type == ServerMessage::Type::Welcome; });
    CHECK(welcome != messages.end());
    if (welcome != messages.end())
        CHECK(welcome->world_id == destination->GetId() && welcome->owner_entity_id == target->Id());
    CHECK(player->GetEntity() == target && target->IsPlayerControlled() && player->HasNpcInventory());
}

TEST(LarvaControlUsesTheHeadForEverySegmentAndPreservesInventory)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* network = fixture.server.GetNetworkModule();
    CServerTestAccess::ClearTestWalls(world);
    auto* root = Spawn(world, EMobType::FireflyLarva, ERarity::Super, {2000.f, 1000.f});
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root);
    CHECK(assembly && assembly->GetSegmentReferences().size() == 5); if (!assembly) return;
    const auto parts = assembly->GetSegmentReferences();
    const auto root_ref = root->GetHandle();
    const auto* original_ai = root->GetController();
    CHECK(network->AssignPlayerEntity(player->GetId(), world, parts.front().entity_ref.id));
    CHECK(player->GetEntity() == root && root->IsPlayerControlled());
    CHECK(root->SuspendedController() == original_ai);
    const auto scope = player->GetInventoryAccountName();
    const auto* player_controller = root->GetController();
    for (const auto& part : parts)
    {
        CHECK(network->AssignPlayerEntity(player->GetId(), world, part.entity_ref.id));
        CHECK(player->GetEntity() == root && root->GetController() == player_controller);
        CHECK(player->GetInventoryAccountName() == scope);
        auto* body = dynamic_cast<CMobBase*>(world.ResolveEntity(part.entity_ref));
        CHECK(body && !body->IsPlayerControlled() && !body->HasPlayerControlLease());
    }
    CHECK(network->AssignPlayerEntity(player->GetId(), world, root->Id()));
    ClientOperate move; move.type = ClientOperate::Type::Input; move.move_y = 127; move.move_x = 0;
    CHECK(player->HandleOperate(move));
    const auto before = root->Pos();
    for (int i = 0; i < 40; ++i)
    {
        world.Tick(0.05f);
        CPlayerLifecycleService{}.RespawnDeadControlledEntities(network->GetPlayers(), world, *network);
        CHECK(player->GetEntity() == world.ResolveEntity(root_ref));
    }
    CHECK(DistanceSq(before, root->Pos()) > 100.f);
    CHECK(assembly->GetSegmentReferences().size() == parts.size());
    for (const auto& part : parts) CHECK(world.ResolveEntity(part.entity_ref) != nullptr);
    const auto* skills = dynamic_cast<ISkillCasterMob*>(root); CHECK(skills && skills->GetSkillCount() == 1);
    CHECK(network->KickPlayer(player->GetId(), "offline larva release"));
    world.FlushEntityRemovals();
    CHECK(world.ResolveEntity(root_ref) == root && root->GetController() == original_ai);
    for (const auto& part : parts) CHECK(world.ResolveEntity(part.entity_ref) != nullptr);
}

TEST(LarvaControlCannotStealAnotherPlayersHeadOrLegacyBody)
{
    CServerTestAccess fixture; auto& world = fixture.World();
    auto* first = fixture.Player(world, 1); auto* second = fixture.Player(world, 2);
    auto* network = fixture.server.GetNetworkModule();
    auto* root = Spawn(world, EMobType::FireflyLarva);
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root); CHECK(assembly); if (!assembly) return;
    const auto parts = assembly->GetSegmentReferences();
    CHECK(network->AssignPlayerEntity(first->GetId(), world, root->Id()));
    auto* second_original = second->GetEntity();
    for (const auto& part : parts) CHECK(!network->AssignPlayerEntity(second->GetId(), world, part.entity_ref.id));
    CHECK(second->GetEntity() == second_original && !second_original->IsMarkedForDes());
    // 兼容旧会话已经单独持有体节的情况，不能从另一部位抢占整条生物。
    first->SetOwnedEntity(world.ResolveEntity(parts.front().entity_ref));
    CHECK(!network->AssignPlayerEntity(second->GetId(), world, root->Id()));
    CHECK(!network->AssignPlayerEntity(second->GetId(), world, parts.back().entity_ref.id));
    CHECK(second->GetEntity() == second_original);
}

TEST(LarvaLegacyBodyControlSurvivesReloadNormalizationAndLogout)
{
    CServerTestAccess fixture; auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* network = fixture.server.GetNetworkModule();
    auto* root = Spawn(world, EMobType::FireflyLarva);
    const auto root_ref = root->GetHandle();
    auto* assembly = dynamic_cast<IMultiSegmentRootMob*>(root); CHECK(assembly); if (!assembly) return;
    const auto parts = assembly->GetSegmentReferences();
    auto* body = dynamic_cast<CMobBase*>(world.ResolveEntity(parts.front().entity_ref)); CHECK(body); if (!body) return;
    auto* old_flower = player->GetEntity();
    player->SetOwnedEntity(body); body->AcquirePlayerControl(std::make_unique<CPlayerController>(), {});
    old_flower->MarkForDestroy(EEntityRemovalReason::Replaced); world.FlushEntityRemovals();
    const auto path = Session().directory / "legacy-larva-control.json"; std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    player = network->FindPlayerById(1);
    root = dynamic_cast<CMobBase*>(world.ResolveEntity(root_ref));
    body = dynamic_cast<CMobBase*>(world.ResolveEntity(parts.front().entity_ref));
    CHECK(player && root && body); if (!player || !root || !body) return;
    CHECK(player->GetEntity() == body && body->IsPlayerControlled());
    CHECK(network->AssignPlayerEntity(player->GetId(), world, parts.back().entity_ref.id));
    world.FlushEntityRemovals();
    CHECK(player->GetEntity() == root && root->IsPlayerControlled());
    CHECK(!body->IsPlayerControlled() && !body->HasPlayerControlLease());
    for (const auto& part : parts) CHECK(world.ResolveEntity(part.entity_ref) != nullptr);
    // 旧体节会话直接登出也必须只撤销控制，不能触发整个多节体的级联删除。
    root->ReleasePlayerControl(FindMobPrototype(EMobType::FireflyLarva)->m_controller_factory(root->GetRarity()));
    player->SetOwnedEntity(body); body->AcquirePlayerControl(std::make_unique<CPlayerController>(), {});
    CHECK(network->KickPlayer(player->GetId(), "offline legacy larva logout"));
    world.FlushEntityRemovals();
    CHECK(world.ResolveEntity(root_ref) == root && !root->IsMarkedForDes());
    for (const auto& part : parts) CHECK(world.ResolveEntity(part.entity_ref) != nullptr);
}

TEST(PossessionReleaseRestoresOriginalAiAndRemovesPlayerBodyWithoutDefeat)
{
    struct CRemovalProbe final : IGameController
    {
        int defeated = 0;
        std::vector<EEntityRemovalReason> removed;
        std::string_view SnapshotKey() const override { return "test.control_release"; }
        void OnTick(CGameWorld&, float) override {}
        void OnMobDefeated(CGameWorld&, CMobBase&) override { ++defeated; }
        void OnEntityRemoved(CGameWorld&, CEntity&, EEntityRemovalReason reason) override { removed.push_back(reason); }
    };
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    auto probe = std::make_unique<CRemovalProbe>(); auto* observer = probe.get();
    world.SetController(std::move(probe), false);
    auto* original = player->GetEntity(); const auto original_ref = original->GetHandle();
    const auto inventory = CAccountDataStore::GetInventory(player->GetProgressAccountName());
    auto* bee = Spawn(world, EMobType::Bee); bee->SetTeam(27);
    auto* original_ai = bee->GetController();
    CHECK(original_ai && !original_ai->IsPlayerControlled());
    auto* network = fixture.server.GetNetworkModule();
    CHECK(network->AssignPlayerEntity(player->GetId(), world, bee->Id()));
    CHECK(bee->IsPlayerControlled() && bee->HasPlayerControlLease());
    CHECK(bee->SuspendedController() == original_ai);
    CHECK(original->IsMarkedForDes() && original->RemovalReason() == EEntityRemovalReason::Replaced);
    world.FlushEntityRemovals();
    CHECK(world.ResolveEntity(original_ref) == nullptr && observer->defeated == 0);
    CHECK(CAccountDataStore::GetInventory(player->GetProgressAccountName()) == inventory);
    bee->SetTeam(99);
    auto* next = Spawn(world, EMobType::Bee);
    CHECK(network->AssignPlayerEntity(player->GetId(), world, next->Id()));
    CHECK(!bee->IsMarkedForDes() && !bee->IsPlayerControlled() && !bee->HasPlayerControlLease());
    CHECK(bee->GetController() == original_ai && bee->Team() == 27);
    CHECK(observer->defeated == 0 && player->GetEntity() == next);
}

TEST(PossessionReleaseWithoutDefaultAiIsNotAKill)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    auto* egg = Spawn(world, EMobType::AntEgg);
    CHECK(egg->GetController() == nullptr);
    auto* network = fixture.server.GetNetworkModule();
    CHECK(network->AssignPlayerEntity(player->GetId(), world, egg->Id()));
    const auto egg_ref = egg->GetHandle(); const float health = egg->Health();
    const auto inventory = CAccountDataStore::GetInventory(player->GetProgressAccountName());
    auto* next = Spawn(world, EMobType::Bee);
    CHECK(network->AssignPlayerEntity(player->GetId(), world, next->Id()));
    CHECK(egg->IsMarkedForDes() && egg->RemovalReason() == EEntityRemovalReason::Replaced);
    CHECK(egg->Health() == health && egg->HasTag(EEntityTag::NoDefeatRewards));
    world.FlushEntityRemovals();
    CHECK(world.ResolveEntity(egg_ref) == nullptr);
    CHECK(CAccountDataStore::GetInventory(player->GetProgressAccountName()) == inventory);
    CHECK(player->GetEntity() == next);
}

TEST(PossessionSuspendedAiSurvivesHotReload)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* player = fixture.Player(world);
    auto* bee = Spawn(world, EMobType::Bee);
    bee->Tick(0.01f); bee->SetTeam(23);
    const auto reference = bee->GetHandle();
    const std::string original_key(bee->GetController()->SnapshotKey());
    CJsonOwner before = MakeJsonObject(); CSnapshotWriter before_writer(before.get());
    bee->GetController()->CaptureSnapshot(before_writer);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, bee->Id()));
    const auto path = Session().directory / "control-lease.json";
    std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    bee = dynamic_cast<CMobBase*>(world.ResolveEntity(reference));
    CHECK(bee && bee->HasPlayerControlLease() && bee->SuspendedController());
    if (!bee || !bee->SuspendedController()) return;
    CJsonOwner after = MakeJsonObject(); CSnapshotWriter after_writer(after.get());
    bee->SuspendedController()->CaptureSnapshot(after_writer);
    CHECK(json_equal(before.get(), after.get()) && bee->ControlReturnTeam() == 23);
    player = nullptr;
    for (const auto& candidate : fixture.server.GetNetworkModule()->GetPlayers())
        if (candidate && candidate->GetId() == 1) player = candidate.get();
    auto* next = Spawn(world, EMobType::Bee);
    CHECK(player && fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, next->Id()));
    CHECK(!bee->IsMarkedForDes() && !bee->IsPlayerControlled() && bee->GetController()->SnapshotKey() == original_key);
    CHECK(bee->Team() == 23);
}

TEST(NpcInventorySeparatesPresetCardsAndLootFromFormalAccount)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* npc = dynamic_cast<CFlower*>(Spawn(world, EMobType::MechaFlower));
    CHECK(npc != nullptr); if (!npc) return;
    npc->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Basic), ERarity::Common);
    CAccountDataStore::AddItem(player->GetAccountName(), static_cast<uint8_t>(EPetalType::Leaf), static_cast<uint8_t>(ERarity::Super), 1);
    const auto formal_inventory = CAccountDataStore::GetInventory(player->GetAccountName());
    const auto formal_slots = CAccountDataStore::GetSlots(player->GetAccountName());
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, npc->Id()));
    CHECK(player->HasNpcInventory() && player->HasIsolatedInventory() && !player->HasIsolatedProgress());
    const auto temporary = player->GetInventoryAccountName();
    CHECK(temporary != player->GetAccountName() && CAccountDataStore::GetInventory(temporary).empty());
    CHECK(!player->TryEquipPetal(0, static_cast<uint8_t>(EPetalType::Leaf), static_cast<uint8_t>(ERarity::Super)));
    CHECK(player->TryUnequipPetal(0));
    CHECK(CAccountDataStore::HasItem(temporary, static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Common)));
    auto drop = std::make_unique<CDrop>(&world, npc->Pos(), EPetalType::Rose, ERarity::Common);
    drop->RestoreRuntime(0.f, 0.f, 30.f);
    auto* loot = static_cast<CDrop*>(world.InsertEntity(std::move(drop)));
    CHECK(loot->PickUpTo(*player));
    CHECK(CAccountDataStore::HasItem(temporary, static_cast<uint8_t>(EPetalType::Rose), static_cast<uint8_t>(ERarity::Common)));
    auto* next = Spawn(world, EMobType::Bee);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, next->Id()));
    SPlayerAccount discarded; CHECK(!CAccountDataStore::CaptureSandboxAccount(temporary, discarded));
    CHECK(npc->GetSlots()[0].m_p_proto && npc->GetSlots()[0].m_p_proto->m_type == EPetalType::Basic);
    CHECK(!npc->IsPlayerControlled() && !npc->IsMarkedForDes());
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == formal_inventory);
    CHECK(CAccountDataStore::GetSlots(player->GetAccountName()) == formal_slots);
    CAccountDataStore::TakeItem(player->GetAccountName(), static_cast<uint8_t>(EPetalType::Leaf), static_cast<uint8_t>(ERarity::Super), 1);
}

TEST(NpcInventorySurvivesRetainedSessionAndReloadThenEndsOnLogout)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto* npc = dynamic_cast<CFlower*>(Spawn(world, EMobType::MechaFlower));
    CHECK(npc != nullptr); if (!npc) return;
    npc->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Basic), ERarity::Common);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, npc->Id()));
    const auto npc_ref = npc->GetHandle(); const auto scope = player->GetInventoryAccountName();
    CHECK(player->TryUnequipPetal(0));
    CAccountDataStore::AddItem(scope, static_cast<uint8_t>(EPetalType::Rose), static_cast<uint8_t>(ERarity::Rare), 3);
    std::uint64_t revision = 0, last = 0; CHECK(CAccountDataStore::GetEconomyCounters(scope, revision, last));
    const auto receipt = CAccountDataStore::ExecuteEconomyRequest(scope, {1, revision, scope}, EEconomyOperation::Loadout, "00",
        [](SCraftResult&) { return EEconomyOutcome::Completed; });
    CHECK(receipt.has_receipt);
    const auto inventory = CAccountDataStore::GetInventory(scope);
    player->DetachSocket(); CHECK(player->GetInventoryAccountName() == scope);
    const auto path = Session().directory / "npc-inventory.json"; std::string error;
    CHECK(CHotReloadSnapshotService::Capture(fixture.server, path, error));
    CHECK(CHotReloadSnapshotService::Restore(fixture.server, path, error));
    player = fixture.server.GetNetworkModule()->FindPlayerById(1);
    npc = dynamic_cast<CFlower*>(world.ResolveEntity(npc_ref));
    CHECK(player && npc); if (!player || !npc) return;
    CHECK(player->HasNpcInventory() && player->GetInventoryAccountName() == scope);
    CHECK(CAccountDataStore::GetInventory(scope) == inventory);
    SEconomyReceipt restored; CHECK(CAccountDataStore::FindEconomyReceipt(scope, 1, restored));
    CHECK(CAccountDataStore::EconomyReceiptCommitted(restored));
    CHECK(!npc->GetSlots()[0].m_p_proto && npc->ControlReturnEquipment());
    CHECK(fixture.server.GetNetworkModule()->KickPlayer(1, "offline NPC cleanup test"));
    CHECK(world.ResolveEntity(npc_ref) == npc && !npc->IsPlayerControlled());
    CHECK(npc->GetSlots()[0].m_p_proto && npc->GetSlots()[0].m_p_proto->m_type == EPetalType::Basic);
    SPlayerAccount expired; CHECK(!CAccountDataStore::CaptureSandboxAccount(scope, expired));
}

TEST(NpcInventoryRejectsOldDropsAndOldDamageScopeAfterControlChanges)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    auto owned = std::make_unique<CDrop>(&world, player->GetEntity()->Pos(), EPetalType::Basic, ERarity::Common, player->GetId());
    auto* formal_drop = static_cast<CDrop*>(world.InsertEntity(std::move(owned)));
    CHECK(formal_drop->IsOwnedBy(*player));
    auto* npc = Spawn(world, EMobType::Bee);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, npc->Id()));
    CHECK(!formal_drop->IsOwnedBy(*player));
    CDamageData credit{player->GetAccountName(), 10.f, 120.f, player->GetProgressAccountName(), player->GetInventoryAccountName()};
    CHECK(credit.MatchesInventoryScope(*player));
    auto local = std::make_unique<CDrop>(&world, npc->Pos(), EPetalType::Basic, ERarity::Common, player->GetId());
    auto* npc_drop = static_cast<CDrop*>(world.InsertEntity(std::move(local)));
    CHECK(npc_drop->IsOwnedBy(*player));
    auto* native = Spawn(world, EMobType::PlayerFlower);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, native->Id()));
    CHECK(!player->HasNpcInventory() && player->GetInventoryAccountName() == player->GetAccountName());
    CHECK(formal_drop->IsOwnedBy(*player) && !npc_drop->IsOwnedBy(*player));
    CHECK(!credit.MatchesInventoryScope(*player));
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

TEST(SnapshotVisibilityAuthorizationPrecedesSquadAndTitanPriority)
{
    struct CVisibilityProbe final : IGameController
    {
        bool allow = false;
        void OnTick(CGameWorld&, float) override {}
        std::string_view SnapshotKey() const override { return "test.visibility"; }
        bool SnapshotEntityVisibleToPlayer(CGameWorld&, const CPlayer&, const CEntity&) const override { return allow; }
    };
    CServerTestAccess fixture;
    auto& world = fixture.World(); world.ConfigureExtensions({true, false, false});
    auto* player = fixture.Player(world, 1); auto* teammate = fixture.Player(world, 2);
    auto* squads = fixture.server.Extensions().Globals().Find<CPartyExtension>();
    CHECK(squads && squads->Invite(*player, *teammate) && squads->Accept(*teammate, *player));
    auto* titan = Spawn(world, EMobType::Titan, ERarity::Common, player->GetEntity()->Pos());
    auto policy = std::make_unique<CVisibilityProbe>(); auto* visibility = policy.get();
    world.SetController(std::move(policy), false);
    CSnapshotService service; CSnapshotService::SBuildResult result;
    CHECK(service.BuildSnapshot(*player, 1, result));
    CHECK(result.message.entities.size() == 1 && result.visible_count == 1);
    CHECK(result.message.entities.front().entity_id == static_cast<net_entity_id>(player->GetEntity()->Id()));
    visibility->allow = true;
    CHECK(service.BuildSnapshot(*player, 2, result));
    const auto present = [&](int id) {
        return std::any_of(result.message.entities.begin(), result.message.entities.end(),
                           [&](const auto& entity) { return entity.entity_id == static_cast<net_entity_id>(id); });
    };
    CHECK(present(teammate->GetEntity()->Id()) && present(titan->Id()));
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
struct SExtensionTestHost : IPlayerDirectory, IChatDelivery, IPartyDelivery
{
    std::vector<std::unique_ptr<CPlayer>> players;
    std::vector<std::pair<std::uint32_t, SChatEntry>> delivered;
    std::unordered_map<std::uint32_t, SPartyState> party_delivered;
    CExtensionRuntime runtime;
    explicit SExtensionTestHost(std::function<void(CExtensionRuntime&)> configure = {})
    {
        Session(); std::string error;
        if (configure) configure(runtime);
        CHECK(runtime.Init(*this, *this, *this, error));
    }
    const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const override { return players; }
    CPlayer* FindPlayerById(std::uint32_t id) const override
    {
        for (const auto& player : players) if (player->GetId() == id) return player.get();
        return nullptr;
    }
    bool DeliverChat(CPlayer& player, const SChatEntry& entry) override
    { delivered.emplace_back(player.GetId(), entry); return true; }
    bool DeliverParty(CPlayer& player, const SPartyState& state) override
    { party_delivered.insert_or_assign(player.GetId(), state); return true; }
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

TEST(DummyReportsDoNotSurvivePlayerIdReuseAndLegacyUnqualifiedReportsAreDiscarded)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* first = fixture.Player(world, 1);
    auto* dummy = Spawn(world, EMobType::Dummy);
    dummy->TakeDamage(100.f, first->GetEntity(), EDamageType::Poison);
    auto state = MakeJsonObject(); CSnapshotWriter writer(state.get()); dummy->CaptureRuntimeSnapshot(writer);
    const auto* reports = json_object_get(state.get(), "reports");
    CHECK(json_array_size(reports) == 1);
    CHECK(CSnapshotReader(json_array_get(reports, 0)).UInt64("session_generation") == first->SessionGeneration());
    const auto old_generation = first->SessionGeneration();
    CHECK(fixture.server.GetNetworkModule()->KickPlayer(1, "fixture session replacement"));
    auto* second = fixture.Player(world, 1); CHECK(second->SessionGeneration() != old_generation);
    second->m_send_buffer.clear();
    dummy->Tick(30.f);
    CHECK(second->m_send_buffer.empty());
    std::string error;
    auto legacy = MakeJsonObject(); CSnapshotWriter legacy_writer(legacy.get());
    legacy_writer.Field("anchor_pos", dummy->Pos());
    legacy_writer.Node("reports", json_pack("[{s:i,s:f,s:b}]", "player_id", 1, "total_damage", 200.0, "pending", 1));
    CHECK(dummy->RestoreRuntimeSnapshot(CSnapshotReader(legacy.get()), 1, error));
    state = MakeJsonObject(); CSnapshotWriter restored_writer(state.get()); dummy->CaptureRuntimeSnapshot(restored_writer);
    CHECK(json_array_size(json_object_get(state.get(), "reports")) == 0);
}

TEST(NaturalSpawnPopulationIsInstanceScopedAndInvalidatesWithinTheSameTick)
{
    Session();
    struct SResolver final : IGameWorldResolver
    {
        CGameWorld* world;
        explicit SResolver(CGameWorld& value) : world(&value) {}
        CGameWorld* FindWorldById(std::uint32_t id) const override { return world->GetId() == id ? world : nullptr; }
        CGameWorld* GetMainWorld() const override { return world; }
        std::vector<CGameWorld*> GetWorlds() const override { return {world}; }
        std::vector<CGameWorld*> FindWorldsByMapName(const std::string&) const override { return {world}; }
        CGameWorld* CreateRoomGameWorld(int) override { return nullptr; }
        std::uint64_t GetTick() const override { return 0; }
    };
    CGameWorld first("data/maps/test.tmj", 1), second("data/maps/test.tmj", 1);
    first.SetController(nullptr); second.SetController(nullptr);
    first.ClearEntitiesForRestore(); second.ClearEntitiesForRestore();
    SResolver a(first), b(second); CNaturalSpawnPopulation left(a), right(b);
    auto* bee = Spawn(first, EMobType::Bee, ERarity::Eternal); bee->SetNaturallySpawned(true);
    std::unordered_set<int> types;
    left.Collect(types); CHECK(types.contains(static_cast<int>(EMobType::Bee)));
    right.Collect(types); CHECK(types.empty());
    bee->MarkForDestroy(); first.FlushEntityRemovals(); left.Invalidate();
    left.Collect(types); CHECK(types.empty());
    bee = Spawn(first, EMobType::Bee, ERarity::Eternal); bee->SetNaturallySpawned(true);
    left.Remember(*bee); left.Collect(types); CHECK(types.contains(static_cast<int>(EMobType::Bee)));
}

TEST(PartyPublicationUsesSessionAndEntityIdentityWithoutLeakingOtherWorldEntities)
{
    SExtensionTestHost host;
    CGameWorld first("data/maps/test.tmj", 401), second("data/maps/test.tmj", 402);
    first.SetController(nullptr); second.SetController(nullptr);
    host.runtime.AttachWorld(first, {true, false, false});
    host.runtime.AttachWorld(second, {true, false, false});
    auto& one = host.Player(first, 70000, "one"); auto& two = host.Player(first, 70001, "two");
    auto* party = host.runtime.Globals().Find<CPartyExtension>();
    CHECK(party->Invite(one, two)); CHECK(party->ConsumeInvite(two) == &one); CHECK(party->Accept(two, one));
    CHECK(party->PublishPlayerState(one));
    auto state = host.party_delivered.at(one.GetId());
    CHECK(state.members.size() == 2 && state.leader_generation == one.SessionGeneration());
    CHECK(state.members[1].has_entity && state.members[1].entity_generation == two.GetEntity()->Generation());
    CHECK(state.members[1].session_generation == two.SessionGeneration());
    auto* old_body = two.GetEntity();
    two.SetOwnedEntity(Spawn(second, EMobType::Bee));
    CHECK(party->PublishPlayerState(one));
    state = host.party_delivered.at(one.GetId());
    CHECK(state.members[1].world_id == second.GetId() && !state.members[1].has_entity);
    CHECK(state.members[1].entity_id == 0 && state.members[1].entity_generation == 0);
    two.SetOwnedEntity(old_body); two.DetachSocket();
    CHECK(party->PublishPlayerState(one));
    CHECK(!host.party_delivered.at(one.GetId()).members[1].connected);
    two.AttachSocket(sf::TcpSocket{});
    CHECK(party->PublishPlayerState(one));
    CHECK(host.party_delivered.at(one.GetId()).members[1].session_generation == two.SessionGeneration());
    auto saved = MakeJsonObject(); CSnapshotWriter writer(saved.get()); party->CaptureSnapshot(writer);
    party->BeginRestore(); std::string error;
    CHECK(party->RestoreSnapshot(CSnapshotReader(saved.get()), 1, error));
    CHECK(party->PublishPlayerState(one) && host.party_delivered.at(one.GetId()).members.size() == 2);
    auto stale = host.runtime.Globals().Find<IChatService>()->Submit(&first, {}, EChatFlag::Squad, two.GetId(), two.GetName(), "old session");
    host.players.erase(host.players.begin() + 1);
    auto& replacement = host.Player(first, 70001, "replacement");
    party->Refresh();
    CHECK(party->Members(one).size() == 1 && party->Members(replacement).size() == 1);
    host.delivered.clear(); host.runtime.Globals().Find<IChatService>()->Broadcast(stale);
    CHECK(host.delivered.empty());
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
    auto* commands = host.runtime.Parties();
    CHECK(commands->Invite(one, outsider));
    CHECK(commands->ConsumeInvite(outsider) == &one); CHECK(commands->Accept(outsider, one));
    CHECK(a.Find<ISquadView>()->Members(one).size() == 1 && b.Find<ISquadView>()->Members(outsider).size() == 1);
    CHECK(host.runtime.Parties()->Members(one).size() == 2);
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

TEST(ClientFeatureExtensionTopologyRetainsStateAndPublishesGlobalsOnce)
{
    struct CProbe : CExtension, IPlayerStateExtension
    {
        int kind, published = 0, value = 0; std::array<int, 3>& detached;
        CProbe(int k, std::array<int, 3>& counts) : kind(k), detached(counts) {}
        const SExtensionDescriptor& Descriptor() const override {
            static const std::array<SExtensionDescriptor, 3> values{{
                {"test.publisher", EExtensionScope::Server, 1}, {"test.retained", EExtensionScope::World, 1}, {"test.optional", EExtensionScope::World, 1}}};
            return values[kind];
        }
        bool PublishPlayerState(CPlayer&) override { ++published; return true; }
        void OnDetach() noexcept override { ++detached[kind]; }
    };
    std::array<int, 3> detached{};
    SExtensionTestHost host([&](CExtensionRuntime& runtime) {
        std::string error;
        for (int kind = 0; kind < 3; ++kind) CHECK(runtime.RegisterFactory(CProbe(kind, detached).Descriptor(),
            [&, kind](SWorldHandle, const SWorldExtensionConfig& config) -> std::unique_ptr<CExtension> {
                if (kind == 2 && !config.weather_spawns) return {};
                return std::make_unique<CProbe>(kind, detached);
            }, error));
    });
    CGameWorld world("data/maps/test.tmj", 802); world.SetController(nullptr); world.ConfigureExtensions({});
    auto view = host.runtime.AttachWorld(world, {}); auto* kept = view.Find<CProbe>(); CHECK(kept); if (!kept) return;
    kept->value = 42;
    auto& player = host.Player(world, 1, "feature-fixture");
    CHECK(host.runtime.PublishPlayerState(player));
    CHECK(kept->published == 1 && host.runtime.Globals().Find<CProbe>()->published == 1);
    host.runtime.AttachWorld(world, {false, false, true});
    CHECK(view.Find<CProbe>() == kept && kept->value == 42 && detached[1] == 0);
    host.runtime.AttachWorld(world, {});
    CHECK(view.Find<CProbe>() == kept && kept->value == 42 && detached[1] == 0 && detached[2] == 1);
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
    const auto* globals = json_object_get(state.get(), "globals");
    const auto saved_key = [&](std::string_view key) {
        for (size_t i = 0; i < json_array_size(globals); ++i)
            if (CSnapshotReader(json_array_get(globals, i)).String("key") == key) return true;
        return false;
    };
    CHECK(saved_key(global->Descriptor().key));
    CHECK(saved_key("extension.weather") && saved_key("extension.party"));
    host.runtime.Tick(0.05f);
    std::string error;
    CHECK(host.runtime.RestoreSnapshot(CSnapshotReader(state.get()), 1, {&world}, error));
    CHECK(global->ticks == 1 && local->ticks == 1);
    CHECK(!host.runtime.RegisterFactory(CCounter(true).Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CCounter>(true);
    }, error));
}

TEST(ExtensionDependenciesAttachInOrderAndRejectCyclesBeforeCallbacks)
{
    class CDependency final : public CExtension
    {
      public:
        std::string key; std::vector<std::string_view> dependencies; SExtensionDescriptor descriptor;
        std::vector<std::string>* order;
        CDependency(std::string name, std::vector<std::string_view> deps, std::vector<std::string>& output)
            : key(std::move(name)), dependencies(std::move(deps)),
              descriptor{key, EExtensionScope::Server, 1, dependencies}, order(&output) {}
        const SExtensionDescriptor& Descriptor() const override { return descriptor; }
        bool OnAttach(CExtensionContext&, std::string&) override { order->push_back(key); return true; }
    };
    std::vector<std::string> order;
    SExtensionTestHost host([&](CExtensionRuntime& runtime) {
        std::string error;
        CDependency dependent("test.second", {"test.first"}, order), first("test.first", {}, order);
        CHECK(runtime.RegisterFactory(dependent.Descriptor(), [&](SWorldHandle, const SWorldExtensionConfig&) {
            return std::make_unique<CDependency>("test.second", std::vector<std::string_view>{"test.first"}, order);
        }, error));
        CHECK(runtime.RegisterFactory(first.Descriptor(), [&](SWorldHandle, const SWorldExtensionConfig&) {
            return std::make_unique<CDependency>("test.first", std::vector<std::string_view>{}, order);
        }, error));
    });
    CHECK(order == (std::vector<std::string>{"test.first", "test.second"}));
    host.runtime.ShutDown(); order.clear();
    std::string error;
    CDependency a("test.cycle.a", {"test.cycle.b"}, order), b("test.cycle.b", {"test.cycle.a"}, order);
    CHECK(host.runtime.RegisterFactory(a.Descriptor(), [&](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CDependency>("test.cycle.a", std::vector<std::string_view>{"test.cycle.b"}, order);
    }, error));
    CHECK(host.runtime.RegisterFactory(b.Descriptor(), [&](SWorldHandle, const SWorldExtensionConfig&) {
        return std::make_unique<CDependency>("test.cycle.b", std::vector<std::string_view>{"test.cycle.a"}, order);
    }, error));
    CHECK(!host.runtime.Init(host, host, host, error));
    CHECK(error.find("cycle") != std::string::npos && order.empty());
}

TEST(ExtensionOptionalRestoreExplicitlyResetsMissingStateAndRejectsRequiredLoss)
{
    class COptional final : public CExtension, public IPersistentExtension
    {
      public:
        int value = 17;
        const SExtensionDescriptor& Descriptor() const override
        { static const SExtensionDescriptor descriptor{"test.optional", EExtensionScope::Server, 1, {}, false}; return descriptor; }
        void CaptureSnapshot(CSnapshotWriter& writer) const override { writer.Field("value", value); }
        bool RestoreSnapshot(const CSnapshotReader& reader, uint32_t version, std::string&) override
        { value = reader.Int("value"); return version == 1; }
        bool RestoreDefaultState(std::string&) override { value = 17; return true; }
    };
    SExtensionTestHost host([](CExtensionRuntime& runtime) {
        std::string error;
        CHECK(runtime.RegisterFactory(COptional{}.Descriptor(), [](SWorldHandle, const SWorldExtensionConfig&) {
            return std::make_unique<COptional>();
        }, error));
    });
    auto snapshot = MakeJsonObject(); CSnapshotWriter writer(snapshot.get()); host.runtime.CaptureSnapshot(writer);
    auto* optional = host.runtime.Globals().Find<COptional>(); CHECK(optional != nullptr); if (!optional) return;
    auto* globals = json_object_get(snapshot.get(), "globals");
    for (size_t i = 0; i < json_array_size(globals); ++i)
        if (CSnapshotReader(json_array_get(globals, i)).String("key") == "test.optional") { json_array_remove(globals, i); break; }
    optional->value = 99;
    std::string error;
    CHECK(host.runtime.RestoreSnapshot(CSnapshotReader(snapshot.get()), 1, {}, error));
    CHECK(optional->value == 17);
    auto unknown = MakeJsonObject(); CSnapshotWriter unknown_writer(unknown.get());
    unknown_writer.Field("key", "test.removed.optional"); unknown_writer.Field("version", 1u);
    unknown_writer.Field("required", false); unknown_writer.Node("data", json_object());
    AppendJson(globals, unknown.release());
    CHECK(host.runtime.RestoreSnapshot(CSnapshotReader(snapshot.get()), 1, {}, error));
    json_object_set_new(json_array_get(globals, json_array_size(globals) - 1), "required", json_true());
    CHECK(!host.runtime.RestoreSnapshot(CSnapshotReader(snapshot.get()), 1, {}, error));
    CHECK(error.find("Unknown required") != std::string::npos);
    json_array_clear(globals);
    CHECK(!host.runtime.RestoreSnapshot(CSnapshotReader(snapshot.get()), 1, {}, error));
    CHECK(error.find("Missing required") != std::string::npos);
}

TEST(ExtensionSelfDetachWaitsUntilDispatchCompletes)
{
    class CDetach final : public CExtension, public ITickExtension
    {
      public:
        CExtensionRuntime& runtime; SWorldHandle world; bool& finished; bool& detached;
        CDetach(CExtensionRuntime& r, SWorldHandle w, bool& f, bool& d) : runtime(r), world(w), finished(f), detached(d) {}
        const SExtensionDescriptor& Descriptor() const override
        { static const SExtensionDescriptor descriptor{"test.detach", EExtensionScope::World, 1}; return descriptor; }
        void Tick(float) override
        { runtime.DetachWorld(world); CHECK(!detached); finished = true; }
        void OnDetach() noexcept override { CHECK(finished); detached = true; }
    };
    bool finished = false, detached = false;
    SExtensionTestHost host([&](CExtensionRuntime& runtime) {
        std::string error;
        CDetach prototype(runtime, {}, finished, detached);
        CHECK(runtime.RegisterFactory(prototype.Descriptor(), [&](SWorldHandle world, const SWorldExtensionConfig&) {
            return std::make_unique<CDetach>(runtime, world, finished, detached);
        }, error));
    });
    CGameWorld world("data/maps/test.tmj", 303); world.SetController(nullptr); world.ConfigureExtensions({});
    const auto view = host.runtime.AttachWorld(world, {});
    CHECK(view.Find<CDetach>() != nullptr);
    host.runtime.Tick(0.05f);
    CHECK(finished && detached && view.Find<CDetach>() == nullptr);
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
    auto* squads = host.runtime.Globals().Find<CPartyExtension>();
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
    CHECK(chat->History().size() == 2); // 私聊保留原有不入公共历史的规则。
    CHECK(host.runtime.HandleCommand(first, {"invite", "second"})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(second, {"invite", "accept"})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(first, {"sq", "group-only"}));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    chat->HandleMessage(first, EChatFlag::Squad, "raw-group-only");
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    CHECK(host.runtime.HandleCommand(remote, {"sq", "do-not-broadcast"}));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{3}));
    CHECK(host.delivered.front().second.flag == EChatFlag::Server);
    CHECK(host.delivered.front().second.message.find("do-not-broadcast") == std::string::npos); host.delivered.clear();
    first.MuteFor(10.f); chat->HandleMessage(first, EChatFlag::Global, "muted");
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1})); host.delivered.clear();
    chat->Broadcast(chat->Submit(&world, {}, EChatFlag::Server, 0, "Server", "world-only"));
    CHECK(host.Recipients() == (std::vector<std::uint32_t>{1, 2})); host.delivered.clear();
    CHECK(host.runtime.Parties()->Leave(second));
    CHECK(scope.Find<ISquadView>()->Members(first).size() == 1);
}

TEST(ExtensionControllerReplacementDoesNotDestroySquads)
{
    CServerTestAccess fixture;
    auto& world = fixture.World();
    auto* first = fixture.Player(world, 1); auto* second = fixture.Player(world, 2);
    auto* commands = fixture.server.Extensions().Parties();
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
    auto* squads = fixture.server.Extensions().Parties();
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

    // 模拟旧版将队伍保存在控制器中的格式。
    json_object_set_new(root.get(), "container_version", json_integer(1));
    json_object_set_new(json_object_get(sections, "players"), "version", json_integer(1));
    json_t* extensions = json_object_get(json_object_get(sections, "extensions"), "data");
    json_t* weather_record = nullptr;
    json_t* party_record = nullptr;
    auto* globals = json_object_get(extensions, "globals");
    for (size_t i = 0; i < json_array_size(globals); ++i)
    {
        auto* record = json_array_get(globals, i);
        const auto key = CSnapshotReader(record).String("key");
        if (key == CWeatherExtension{}.Descriptor().key) weather_record = record;
        if (key == "extension.party") party_record = record;
    }
    CHECK(weather_record && party_record); if (!weather_record || !party_record) return;
    json_object_set(sections, "weather_diurnal", weather_record);
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
        if (world_reader.UInt32("world_id") == world.GetId())
        {
            auto* data = json_object_get(party_record, "data");
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
    auto* squads = host.runtime.Globals().Find<CPartyExtension>();
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
    CGameWorld replacement("data/maps/test.tmj", selected_id);
    auto replacement_terrain = MakeJsonObject();
    CSnapshotWriter terrain_writer(replacement_terrain.get());
    replacement.Terrain().CaptureSnapshot(terrain_writer);
    worlds = SavedWorlds(root.get());
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        json_t* entry = json_array_get(worlds, i);
        if (CSnapshotReader(entry).UInt32("world_id") == selected_id)
        {
            json_object_set_new(entry, "map_path", json_string("data/maps/test.tmj"));
            // 显式替换地图时一并替换地形基线，不能把旧地图修改套到新资源。
            json_object_set(entry, "terrain", replacement_terrain.get());
        }
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

TEST(WorldTransferCostSeparatesDependencyWorkFromCheckpointCopies)
{
    Session();
    for (const size_t count : {size_t{0}, size_t{1000}, size_t{10000}})
    {
        CGameWorld source("data/maps/test.tmj", 501), destination("data/maps/test.tmj", 502);
        source.SetController(nullptr); destination.SetController(nullptr);
        source.ClearEntitiesForRestore(); destination.ClearEntitiesForRestore();
        for (size_t i = 0; i < count; ++i)
            Spawn(source, EMobType::Bee, ERarity::Common, {float(i % 100) * 20.f, float(i / 100) * 20.f});
        CPlayer player(sf::TcpSocket{}, 1, "runtime-player"); player.Authenticate("runtime-player");
        auto* controlled = Spawn(source, EMobType::Bee); player.SetOwnedEntity(controlled);
        const auto same = CWorldTransfer::Move(player, source, source, {500.f, 500.f});
        CHECK(same.committed && !same.recovering);
        CHECK(same.cost.dependencies == 1 && same.cost.dependency_candidates == count + 1);
        CHECK(same.cost.entity_index_entries_copied == 0 && same.cost.spatial_entries_copied == count + 1);
        const auto moved = CWorldTransfer::Move(player, source, destination, {600.f, 600.f});
        CHECK(moved.committed && !moved.recovering && player.GetEntity() == controlled);
        CHECK(moved.cost.dependencies == 1 && moved.cost.dependency_candidates == count + 1);
        CHECK(moved.cost.spatial_entries_copied == count + 1 && moved.cost.entity_index_entries_copied >= count + 1);
        std::printf("TRANSFER_COST entities=%zu same_ms=%.3f cross_ms=%.3f checkpoint_ms=%.3f copied_indices=%zu\n",
            count + 1, same.cost.total_ms, moved.cost.total_ms, moved.cost.checkpoint_ms, moved.cost.entity_index_entries_copied);
    }
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
    CHECK(!flower->GetSlots()[0].IsBanned());
    source.FlushEntityRemovals(); target.FlushEntityRemovals();
    CHECK(source.ResolveEntity(zone_ref) == nullptr && source.GetEntityCount() == 0);
    CHECK(player.GetEntity() == flower);
}

TEST(EconomyForgeTransfersEquippedUniqueOnceAndPublishesAfterSave)
{
    CServerTestAccess fixture;
    auto& world = fixture.World(); auto* player = fixture.Player(world);
    const auto type = static_cast<uint8_t>(EPetalType::Basic);
    const auto super = static_cast<uint8_t>(ERarity::Super), unique = static_cast<uint8_t>(ERarity::Unique);
    std::string error;
    CHECK(CAccountDataStore::InitializeUniqueOwnership(Session().directory / "forge-legacy-absent.json", &error));
    CHECK(CAccountDataStore::CommitRegistration("forge-old-owner", "offline-fixture-hash", "", "", &error));
    CAccountDataStore::AddItem("forge-old-owner", type, super, 5);
    CHECK(CAccountDataStore::ExchangeUniquePetal("forge-old-owner", 5, type));
    CHECK(CAccountDataStore::TakeItem("forge-old-owner", type, unique, 1));
    CAccountDataStore::SetSlot("forge-old-owner", 0, type, unique);
    auto previous = std::make_unique<CPlayer>(sf::TcpSocket{}, 2, "forge-old-owner");
    previous->Authenticate("forge-old-owner");
    auto* previous_flower = dynamic_cast<CFlower*>(Spawn(world, EMobType::PlayerFlower));
    previous->SetOwnedEntity(previous_flower);
    previous->LoadSavedSlots();
    CHECK(fixture.server.GetNetworkModule()->InsertRestoredPlayer(std::move(previous)));
    CHECK(previous_flower->GetSlots()[0].m_p_proto != nullptr);
    CHECK(Spawn(world, EMobType::Titan, ERarity::Common, player->GetEntity()->Pos()) != nullptr);
    CAccountDataStore::AddItem(player->GetAccountName(), type, super, 5);
    CHECK(CAccountDataStore::Flush());
    auto request = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Forge);
    request.petal_type = type; request.rarity = super; request.count = 5;
    auto* chat = fixture.server.Extensions().Globals().Find<IChatService>();
    CHECK(chat != nullptr); if (!chat) return;
    const auto history_size = chat->History().size();
    {
        CAccountDataStore::CSaveBatch hold;
        CServerTestAccess::SendEconomy(fixture, *player, request, false);
        fixture.server.PublishCommittedPetalReports();
        CHECK(chat->History().size() == history_size);
    }
    CHECK(!previous_flower->GetSlots()[0].m_p_proto);
    CHECK(CAccountDataStore::GetSlots("forge-old-owner")[0].petal_type == 0);
    CHECK(CAccountDataStore::HasItem("forge-old-owner", type, super, 4));
    const auto* owner = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(owner && owner->owner == player->GetAccountName());
    const auto inventory = CAccountDataStore::GetInventory(player->GetAccountName());
    const auto previous_inventory = CAccountDataStore::GetInventory("forge-old-owner");
    const auto cooldown = fixture.server.GetTitanForgeCooldowns();
    CHECK(CAccountDataStore::Flush());
    fixture.server.PublishCommittedPetalReports();
    CHECK(chat->History().size() == history_size + 1);
    CServerTestAccess::SendEconomy(fixture, *player, request);
    CServerTestAccess::SendEconomy(fixture, *player, request);
    fixture.server.PublishCommittedPetalReports();
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == inventory);
    CHECK(CAccountDataStore::GetInventory("forge-old-owner") == previous_inventory);
    CHECK(fixture.server.GetTitanForgeCooldowns() == cooldown);
    CHECK(chat->History().size() == history_size + 1);

    // Even a preset Unique on a possessed NPC belongs exclusively to that
    // temporary bag. It cannot transfer or forge the formal ledger entry.
    auto* npc = dynamic_cast<CFlower*>(Spawn(world, EMobType::MechaFlower));
    CHECK(npc); if (!npc) return;
    npc->LoadPetalSlot(0, FindPetalPrototype(EPetalType::Basic), ERarity::Unique);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, npc->Id()));
    const auto temporary_scope = player->GetInventoryAccountName();
    CHECK(player->TryUnequipPetal(0));
    CHECK(CAccountDataStore::HasItem(temporary_scope, type, unique, 1));
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == inventory);
    auto forbidden = CServerTestAccess::EconomyRequest(fixture, *player, EEconomyOperation::Forge);
    forbidden.petal_type = type; forbidden.rarity = super; forbidden.count = 5;
    CServerTestAccess::SendEconomy(fixture, *player, forbidden);
    SEconomyReceipt rejected;
    CHECK(CAccountDataStore::FindEconomyReceipt(temporary_scope, forbidden.context.request_id, rejected));
    CHECK(rejected.outcome == EEconomyOutcome::Forbidden);
    owner = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(owner && owner->owner == player->GetAccountName());
    auto* next = Spawn(world, EMobType::Bee);
    CHECK(fixture.server.GetNetworkModule()->AssignPlayerEntity(player->GetId(), world, next->Id()));
    CHECK(npc->GetSlots()[0].m_p_proto && npc->GetSlots()[0].m_stored_rarity == ERarity::Unique);
    SPlayerAccount discarded; CHECK(!CAccountDataStore::CaptureSandboxAccount(temporary_scope, discarded));
    CHECK(CAccountDataStore::GetInventory(player->GetAccountName()) == inventory);
}

TEST(ServerCommandRegistrationSurvivesStaticLibraryLinking)
{
    CConsole console;
    console.InstallCommands();
    CHECK(console.CommandUsage("set").has_value());
    CHECK(console.CommandUsage("quit").has_value());
    CHECK(console.CommandUsage("world_controller").has_value());
    CHECK(console.CommandUsage("tele").has_value());
}
