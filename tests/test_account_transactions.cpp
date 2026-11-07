#include "test_framework.h"
#include "Server/Persistence/account_store.h"
#include "Engine/json_value.h"
#include "Shared/game_config.h"
#include "Shared/tools.h"
#include "Server/Persistence/economy_codec.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
constexpr auto basic = static_cast<uint8_t>(EPetalType::Basic);
constexpr auto unique_rarity = static_cast<uint8_t>(ERarity::Unique);
constexpr auto super_rarity = static_cast<uint8_t>(ERarity::Super);

std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    CHECK(static_cast<bool>(file));
}

struct Store
{
    std::filesystem::path directory = std::filesystem::path("artifacts") /
        ("account-transactions-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path accounts = directory / "accounts.json", legacy = directory / "unique.json";
    bool loaded = false;
    std::string error;
    Store() { std::filesystem::create_directories(directory); game_config::account_backend = "json"; }
    ~Store() { if (loaded) CAccountDataStore::ShutDown(); }
    bool Load()
    {
        loaded = CAccountDataStore::Load(accounts, &error);
        CHECK(loaded);
        return loaded;
    }
    void Close()
    {
        CHECK(CAccountDataStore::ShutDown(&error)); loaded = false;
    }
    void Register(const std::string& name)
    {
        CHECK(CAccountDataStore::CommitRegistration(name, "offline-test-hash", "", "127.0.0.1", &error));
    }
    void New()
    {
        CHECK(Load()); CHECK(CAccountDataStore::InitializeUniqueOwnership(legacy, &error));
        Register("owner"); Register("getter");
    }
};

std::string LegacyAccounts(int count = 1, bool equipped = false)
{
    const auto item = "{\"type\":" + std::to_string(basic) + ",\"rarity\":" +
        std::to_string(unique_rarity) + ",\"count\":" + std::to_string(count) + "}";
    return "{\"accounts\":[{\"name\":\"owner\",\"password\":\"fixture\",\"inventory\":[" +
        (equipped ? std::string{} : item) + "],\"slots\":[" + (equipped ? item : std::string{}) + "]}]}";
}

std::string LegacyRegistry(const std::string& owner = "owner", bool duplicate = false)
{
    const auto record = "{\"type\":" + std::to_string(basic) + ",\"cost\":5,\"owner\":\"" + owner + "\"}";
    return "{\"version\":1,\"owners\":[" + record + (duplicate ? "," + record : "") + "]}";
}
}

TEST(AccountTransactionUniqueTransferPersistsOneCompleteState)
{
    Store store; store.New();
    CAccountDataStore::AddItem("owner", basic, super_rarity, 5);
    CHECK(CAccountDataStore::ExchangeUniquePetal("owner", 5, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::HasItem("owner", basic, unique_rarity));
    CAccountDataStore::AddItem("getter", basic, super_rarity, 5);
    CHECK(CAccountDataStore::ExchangeUniquePetal("getter", 5, basic, nullptr, &store.error));
    CHECK(!CAccountDataStore::HasItem("owner", basic, unique_rarity));
    CHECK(CAccountDataStore::HasItem("owner", basic, super_rarity, 4));
    CHECK(CAccountDataStore::HasItem("getter", basic, unique_rarity));
    CHECK(!CAccountDataStore::HasItem("getter", basic, super_rarity));
    CHECK(CAccountDataStore::Flush(&store.error));
    // Recover exactly the already-published snapshot, with no registry shutdown.
    const auto checkpoint = store.directory / "crash-checkpoint.json";
    std::filesystem::copy_file(store.accounts, checkpoint);
    store.Close(); store.accounts = checkpoint;
    if (!store.Load()) return;
    Write(store.legacy, "This obsolete sidecar must not be loaded again.");
    CHECK(CAccountDataStore::InitializeUniqueOwnership(store.legacy, &store.error));
    const auto* owner = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(owner && owner->owner == "getter" && owner->cost == 5);
    CHECK(CAccountDataStore::HasItem("getter", basic, unique_rarity));
    CHECK(!CAccountDataStore::HasItem("owner", basic, unique_rarity));
}

TEST(AccountTransactionLegacyOwnershipMigratesEquippedItemsOnce)
{
    Store store;
    Write(store.accounts, LegacyAccounts(1, true)); Write(store.legacy, LegacyRegistry());
    const auto original_registry = Read(store.legacy);
    if (!store.Load()) return;
    CHECK(CAccountDataStore::InitializeUniqueOwnership(store.legacy, &store.error));
    const auto* record = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(record && record->owner == "owner" && record->cost == 5);
    CHECK(Read(store.legacy) == original_registry);
    store.Register("getter");
    CAccountDataStore::AddItem("getter", basic, super_rarity, 5);
    SUniquePetalExchangeResult result;
    CHECK(CAccountDataStore::ExchangeUniquePetal("getter", 5, basic, &result, &store.error));
    CHECK(!result.account_mutations.empty());
    if (!result.account_mutations.empty()) CHECK(result.account_mutations.front().cleared_primary_slots == std::vector<uint8_t>{0});
    const auto slots = CAccountDataStore::GetSlots("owner");
    CHECK(!slots.empty() && slots.front().petal_type == 0);
    CHECK(CAccountDataStore::Flush(&store.error));
    store.Close();
    if (!store.Load()) return;
    CHECK(CAccountDataStore::InitializeUniqueOwnership(store.legacy, &store.error));
    CHECK(CAccountDataStore::FindUniqueOwner(EPetalType::Basic)->owner == "getter");
}

TEST(AccountTransactionRejectsAmbiguousLegacyOwnershipWithoutGuessing)
{
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        Store store;
        Write(store.accounts, LegacyAccounts(scenario == 2 ? 2 : 1));
        if (scenario != 3) Write(store.legacy, LegacyRegistry(scenario == 1 ? "missing" : "owner", scenario == 0));
        const auto original_accounts = Read(store.accounts), original_registry = Read(store.legacy);
        if (!store.Load()) continue;
        CHECK(!CAccountDataStore::InitializeUniqueOwnership(store.legacy, &store.error));
        CHECK(!store.error.empty());
        CHECK(CAccountDataStore::UniqueOwners().empty());
        CHECK(Read(store.accounts) == original_accounts);
        CHECK(Read(store.legacy) == original_registry);
    }
}

TEST(AccountTransactionRejectedTransferRestoresLedgerAndAllItems)
{
    Store store; store.New();
    CAccountDataStore::AddItem("owner", basic, super_rarity, 5);
    CHECK(CAccountDataStore::ExchangeUniquePetal("owner", 5, basic, nullptr, &store.error));
    const auto before = CAccountDataStore::SnapshotAccounts();
    CHECK(!CAccountDataStore::ExchangeUniquePetal("getter", 5, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::SnapshotAccounts() == before);
    CHECK(CAccountDataStore::FindUniqueOwner(EPetalType::Basic)->owner == "owner");
    CHECK(CAccountDataStore::SandboxCloneAccount("@sandbox:test", "owner"));
    CHECK(!CAccountDataStore::ExchangeUniquePetal("@sandbox:test", 0, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::FindUniqueOwner(EPetalType::Basic)->owner == "owner");
}

TEST(AccountTransactionMigrationInvalidatesPreviouslyCapturedAccountRecords)
{
    Store store; if (!store.Load()) return;
    store.Register("owner");
    CAccountDataStore::AddItem("owner", basic, unique_rarity, 1);
    CHECK(CAccountDataStore::Flush(&store.error));
    Write(store.legacy, LegacyRegistry());
    CHECK(CAccountDataStore::InitializeUniqueOwnership(store.legacy, &store.error));
    store.Close();
    if (!store.Load()) return;
    const auto* owner = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(owner && owner->owner == "owner");
    CHECK(CAccountDataStore::HasItem("owner", basic, unique_rarity));
}

TEST(AccountTransactionSameOwnerKeepsOneUniqueAndRefundsPreviousCost)
{
    Store store; store.New();
    CAccountDataStore::AddItem("owner", basic, super_rarity, 6);
    CHECK(CAccountDataStore::ExchangeUniquePetal("owner", 5, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::ExchangeUniquePetal("owner", 5, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::UniqueOwners().size() == 1);
    CHECK(CAccountDataStore::HasItem("owner", basic, unique_rarity, 1));
    CHECK(!CAccountDataStore::HasItem("owner", basic, unique_rarity, 2));
    CHECK(!CAccountDataStore::HasItem("owner", basic, super_rarity));
    CHECK(CAccountDataStore::Flush(&store.error));
    store.Close(); CHECK(store.Load());
}

TEST(AccountTransactionFailedDiskPublishRetainsCompletePreviousGeneration)
{
    Store store; store.New();
    CAccountDataStore::AddItem("owner", basic, super_rarity, 5);
    CAccountDataStore::AddItem("getter", basic, super_rarity, 5);
    CHECK(CAccountDataStore::ExchangeUniquePetal("owner", 5, basic, nullptr, &store.error));
    CHECK(CAccountDataStore::Flush(&store.error));
    const auto published = Read(store.accounts);
    const std::filesystem::path blocked = store.accounts.string() + ".tmp";
    CHECK(std::filesystem::create_directory(blocked));
    CHECK(CAccountDataStore::ExchangeUniquePetal("getter", 5, basic, nullptr, &store.error));
    CHECK(!CAccountDataStore::Flush(&store.error));
    CHECK(Read(store.accounts) == published);
    CHECK(std::filesystem::remove(blocked));
    CHECK(CAccountDataStore::Flush(&store.error));
    store.Close();
    if (!store.Load()) return;
    const auto* owner = CAccountDataStore::FindUniqueOwner(EPetalType::Basic);
    CHECK(owner && owner->owner == "getter");
    CHECK(CAccountDataStore::HasItem("getter", basic, unique_rarity));
    CHECK(!CAccountDataStore::HasItem("owner", basic, unique_rarity));
}

namespace
{
SAccountAuthResult RegisterRecoveryUser(Store& store, const std::string& device = std::string(32, 'a'))
{
    SAccountAuthWork work;
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "offline-password", "", "127.0.0.1", true,
                                                   work, &store.error, device));
    auto result = CAccountDataStore::ExecuteAuthentication(std::move(work));
    CHECK(result.success && result.recovery_token.size() == 64);
    CHECK(CAccountDataStore::CommitRegistration(result, &store.error));
    CHECK(CAccountDataStore::CommitRecoveryCredential(result, &store.error));
    return result;
}
}

TEST(AccountRecoveryRejectsSharedIpAndAcceptsOnlyVerifiedCredential)
{
    Store store; store.New();
    const auto issued = RegisterRecoveryUser(store);
    SAccountAuthWork work;
    CHECK(!CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error));
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "wrong-password", "", "127.0.0.1", false, work, &store.error));
    CHECK(!CAccountDataStore::ExecuteAuthentication(std::move(work)).success);
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "", "", "192.0.2.42", false, work, &store.error,
                                                   issued.device_id, issued.recovery_token));
    auto resumed = CAccountDataStore::ExecuteAuthentication(std::move(work));
    CHECK(resumed.success && resumed.recovery_verified && !resumed.password_verified);
    CHECK(CAccountDataStore::CommitLogin(resumed, &store.error));
    CHECK(CAccountDataStore::CommitRecoveryCredential(resumed, &store.error));
    CHECK(!CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                    std::string(32, 'b'), issued.recovery_token));
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                   issued.device_id, std::string(64, '0')));
    CHECK(!CAccountDataStore::ExecuteAuthentication(std::move(work)).success);
}

TEST(AccountIdentityLinksPersistAndCannotCrossAccountsOrTemporaryInventories)
{
    Store store; store.New(); std::string owner, getter; std::vector<SLinkedIdentity> links;
    CHECK(CAccountDataStore::GetIdentityState("owner", owner, links));
    CHECK(CAccountDataStore::GetIdentityState("getter", getter, links));
    CHECK(owner.size() == 32 && getter.size() == 32 && owner != getter);
    SLinkedIdentity discord{"discord", "fixture-app", "123456789", "测试账户", 1700000000};
    CHECK(!CAccountDataStore::LinkIdentity("owner", getter, "offline-test-hash", discord, &store.error));
    CHECK(!CAccountDataStore::LinkIdentity("owner", owner, "wrong-proof", discord, &store.error));
    CHECK(CAccountDataStore::LinkIdentity("owner", owner, "offline-test-hash", discord, &store.error));
    CHECK(CAccountDataStore::LinkIdentity("owner", owner, "offline-test-hash", discord, &store.error));
    CHECK(!CAccountDataStore::LinkIdentity("getter", getter, "offline-test-hash", discord, &store.error));
    CHECK(CAccountDataStore::SandboxCloneAccount("@sandbox:identity-test", "owner"));
    std::string temporary; CHECK(!CAccountDataStore::GetIdentityState("@sandbox:identity-test", temporary, links));
    for (const auto& account : CAccountDataStore::SnapshotAccounts())
        if (account.name.starts_with("@sandbox:")) CHECK(account.account_id.empty() && account.linked_identities.empty());
    CHECK(CAccountDataStore::Flush()); store.Close(); CHECK(store.Load());
    std::string reloaded; CHECK(CAccountDataStore::GetIdentityState("owner", reloaded, links));
    CHECK(reloaded == owner && links.size() == 1 && links.front() == discord);
    CHECK(CAccountDataStore::UnlinkIdentity("owner", owner, "offline-test-hash", "discord", &store.error));
    CHECK(CAccountDataStore::LinkIdentity("getter", getter, "offline-test-hash", discord, &store.error));
    CHECK(CAccountDataStore::Flush()); store.Close(); CHECK(store.Load());
    CHECK(CAccountDataStore::GetIdentityState("owner", reloaded, links) && links.empty());
    CHECK(CAccountDataStore::GetIdentityState("getter", reloaded, links) && links.size() == 1);
}

TEST(AccountIdentityRejectsDuplicateIdsAndExternalBindingsDuringImport)
{
    Store store; store.New(); const auto original = CAccountDataStore::SnapshotAccounts();
    auto broken = original; broken[1].account_id = broken[0].account_id;
    CHECK(!CAccountDataStore::ImportAccounts(broken, &store.error));
    CHECK(CAccountDataStore::SnapshotAccounts() == original);
    broken = original;
    const SLinkedIdentity identity{"qq", "fixture-app", std::string(32, 'a'), "QQ", 1700000000};
    broken[0].linked_identities = {identity}; broken[1].linked_identities = {identity};
    CHECK(!CAccountDataStore::ImportAccounts(broken, &store.error));
    CHECK(CAccountDataStore::SnapshotAccounts() == original);
}

TEST(AccountRecoveryPersistsVerifierAndRevocationClosesWorkerRace)
{
    Store store; store.New();
    const auto issued = RegisterRecoveryUser(store);
    CHECK(CAccountDataStore::Flush(&store.error));
    const auto text = Read(store.accounts);
    CHECK(text.find(issued.recovery_token) == std::string::npos);
    CHECK(text.find(issued.recovery_credential.verifier) != std::string::npos);
    store.Close(); if (!store.Load()) return;
    SAccountAuthWork work;
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "", "", "192.0.2.42", false, work, &store.error,
                                                   issued.device_id, issued.recovery_token));
    auto in_flight = CAccountDataStore::ExecuteAuthentication(std::move(work));
    CHECK(in_flight.success);
    CHECK(CAccountDataStore::RevokeRecoveryCredential("secured", issued.device_id, &store.error));
    CHECK(!CAccountDataStore::CommitLogin(in_flight, &store.error));
    CHECK(!CAccountDataStore::CommitRecoveryCredential(in_flight, &store.error));
    CHECK(!CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                    issued.device_id, issued.recovery_token));
}

TEST(AccountRecoveryExpiryAndPasswordChangesInvalidateOldSecrets)
{
    Store store; store.New();
    const auto issued = RegisterRecoveryUser(store);
    auto original = CAccountDataStore::SnapshotAccounts();
    auto modified = original;
    for (auto& account : modified) if (account.name == "secured") account.recovery_credentials[0].expires_at = 1;
    CHECK(CAccountDataStore::ImportAccounts(modified, &store.error));
    SAccountAuthWork work;
    CHECK(!CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                    issued.device_id, issued.recovery_token));
    modified = original;
    for (auto& account : modified) if (account.name == "secured") account.password += "changed";
    CHECK(CAccountDataStore::ImportAccounts(modified, &store.error));
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                   issued.device_id, issued.recovery_token));
    CHECK(!CAccountDataStore::ExecuteAuthentication(std::move(work)).success);
}

TEST(AccountRecoveryDeviceLimitKeepsNewCredentialAndReplacesSameDevice)
{
    Store store; store.New();
    const auto first = RegisterRecoveryUser(store);
    SAccountAuthResult latest;
    for (char device : {'b', 'c', 'd', 'e'})
    {
        SAccountAuthWork work;
        CHECK(CAccountDataStore::PrepareAuthentication("secured", "offline-password", "", "127.0.0.1", false,
                                                       work, &store.error, std::string(32, device)));
        latest = CAccountDataStore::ExecuteAuthentication(std::move(work));
        CHECK(latest.success && CAccountDataStore::CommitLogin(latest, &store.error));
        CHECK(CAccountDataStore::CommitRecoveryCredential(latest, &store.error));
    }
    SAccountAuthWork work;
    CHECK(!CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                    first.device_id, first.recovery_token));
    CHECK(CAccountDataStore::PrepareAuthentication("secured", "", "", "127.0.0.1", false, work, &store.error,
                                                   latest.device_id, latest.recovery_token));
    CHECK(CAccountDataStore::ExecuteAuthentication(std::move(work)).success);
    CHECK(CAccountDataStore::CommitRecoveryCredential(latest, &store.error));
    for (const auto& account : CAccountDataStore::SnapshotAccounts())
        if (account.name == "secured") CHECK(account.recovery_credentials.size() == CAccountDataStore::max_recovery_credentials);
}

TEST(EconomyRequestReplayDoesNotChargeOrRerollAndSurvivesReload)
{
    Store store; store.New();
    CAccountDataStore::AddItem("owner", basic, static_cast<uint8_t>(ERarity::Common), 10);
    std::uint64_t revision = 0, last = 0;
    CHECK(CAccountDataStore::GetEconomyCounters("owner", revision, last));
    SEconomyRequestContext request{1, revision, "owner"};
    int calls = 0;
    auto action = [&](SCraftResult& craft) {
        ++calls;
        return CAccountDataStore::CraftItem("owner", basic, static_cast<uint8_t>(ERarity::Common), 5, &craft)
            ? EEconomyOutcome::Completed : EEconomyOutcome::InsufficientItems;
    };
    auto first = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Craft, "040105000000", action);
    CHECK(first.has_receipt && first.receipt.outcome == EEconomyOutcome::Completed && calls == 1);
    const auto after = CAccountDataStore::GetInventory("owner"); const auto rng = GetRng();
    auto replay = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Craft, "040105000000", action);
    CHECK(replay.has_receipt && replay.receipt.craft == first.receipt.craft && calls == 1);
    CHECK(CAccountDataStore::GetInventory("owner") == after && GetRng() == rng);
    CHECK(CAccountDataStore::Flush(&store.error)); CHECK(CAccountDataStore::EconomyReceiptCommitted(first.receipt));
    store.Close(); if (!store.Load()) return;
    replay = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Craft, "040105000000", action);
    CHECK(replay.has_receipt && replay.receipt.craft == first.receipt.craft && calls == 1);
    CHECK(replay.receipt.required_save_generation == 0 && CAccountDataStore::EconomyReceiptCommitted(replay.receipt));
}

TEST(EconomyRequestRejectionAndExceptionRestoreAccountsAndRng)
{
    Store store; store.New();
    const auto inventory = CAccountDataStore::GetInventory("owner"); const auto rng = GetRng();
    SEconomyRequestContext request{1, 0, "owner"};
    const auto rejected = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "00", [&](SCraftResult&) {
        CAccountDataStore::AddItem("owner", basic, 1, 1); GetRng()(); return EEconomyOutcome::Forbidden;
    });
    CHECK(rejected.has_receipt && rejected.receipt.outcome == EEconomyOutcome::Forbidden);
    CHECK(CAccountDataStore::GetInventory("owner") == inventory && GetRng() == rng);
    std::uint64_t revision = 0, last = 0;
    CHECK(CAccountDataStore::GetEconomyCounters("owner", revision, last)); CHECK(revision == 0 && last == 1);
    request.request_id = 2;
    bool threw = false;
    try {
        CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "00", [&](SCraftResult&) {
            CAccountDataStore::AddItem("owner", basic, 1, 1);
            CAccountDataStore::AddItem("getter", basic, 1, 1); // undeclared participant
            return EEconomyOutcome::Completed;
        });
    } catch (const std::logic_error&) { threw = true; }
    CHECK(threw && !CAccountDataStore::IsEconomyOperationActive());
    CHECK(CAccountDataStore::GetInventory("owner") == inventory);
    CHECK(CAccountDataStore::GetInventory("getter").empty());
    CHECK(CAccountDataStore::GetEconomyCounters("owner", revision, last)); CHECK(revision == 0 && last == 1);
}

TEST(CraftCapacityFailureKeepsMaterialsAndRandomStream)
{
    Store store; if (!store.Load()) return; store.Register("owner");
    const auto common = static_cast<uint8_t>(ERarity::Common), uncommon = static_cast<uint8_t>(ERarity::Unusual);
    CAccountDataStore::AddItem("owner", basic, common, 5);
    CAccountDataStore::AddItem("owner", basic, uncommon, max_inventory_item_count);
    const auto before = CAccountDataStore::GetInventory("owner");
    const auto original_rng = GetRng();
    for (unsigned seed = 0; seed < 1000; ++seed)
    {
        GetRng().seed(seed);
        const auto candidate = GetRng();
        if (CheckChance(0.64f)) { GetRng() = candidate; break; }
    }
    const auto rng = GetRng();
    SCraftResult craft;
    CHECK(!CAccountDataStore::CraftItem("owner", basic, common, 5, &craft));
    CHECK(CAccountDataStore::GetInventory("owner") == before && GetRng() == rng);
    CHECK(!craft.changed && craft.consumed == 0 && craft.items.empty());
    GetRng() = original_rng;
}

TEST(EconomyRequestChecksScopeRevisionSequenceAndPayloadBeforeAction)
{
    Store store; store.New();
    int calls = 0;
    const auto action = [&](SCraftResult&) { ++calls; return EEconomyOutcome::Completed; };
    SEconomyRequestContext request{1, 9, "owner"};
    auto result = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "00", action);
    CHECK(result.has_receipt && result.receipt.outcome == EEconomyOutcome::StaleRevision && calls == 0);
    result = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "01", action);
    CHECK(!result.has_receipt && result.receipt.outcome == EEconomyOutcome::ConflictingRequest && calls == 0);
    request.request_id = 3;
    result = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "00", action);
    CHECK(result.receipt.outcome == EEconomyOutcome::OutOfOrder && calls == 0);
    request = {2, 0, "@sandbox:different"};
    result = CAccountDataStore::ExecuteEconomyRequest("owner", request, EEconomyOperation::Loadout, "00", action);
    CHECK(result.receipt.outcome == EEconomyOutcome::WrongScope && calls == 0);
}

TEST(EconomyRequestCannotReportCommittedUntilItsGenerationPublishes)
{
    Store store; store.New(); CHECK(CAccountDataStore::Flush(&store.error));
    const auto blocked = std::filesystem::path(store.accounts.string() + ".tmp");
    CHECK(std::filesystem::create_directory(blocked));
    int calls = 0;
    auto result = CAccountDataStore::ExecuteEconomyRequest("owner", {1, 0, "owner"}, EEconomyOperation::Loadout, "00", [&](SCraftResult&) {
        ++calls; CAccountDataStore::AddItem("owner", basic, 1, 1); return EEconomyOutcome::Completed;
    });
    CHECK(result.has_receipt && calls == 1 && !CAccountDataStore::EconomyReceiptCommitted(result.receipt));
    CHECK(!CAccountDataStore::Flush(&store.error));
    auto replay = CAccountDataStore::ExecuteEconomyRequest("owner", {1, 0, "owner"}, EEconomyOperation::Loadout, "00", [&](SCraftResult&) {
        ++calls; return EEconomyOutcome::Completed;
    });
    CHECK(replay.has_receipt && calls == 1 && !CAccountDataStore::EconomyReceiptCommitted(replay.receipt));
    CHECK(std::filesystem::remove(blocked)); CHECK(CAccountDataStore::Flush(&store.error));
    CHECK(CAccountDataStore::EconomyReceiptCommitted(result.receipt));
}

TEST(EconomyReceiptEvictionKeepsReplayHighWaterAndSandboxIsIndependent)
{
    Store store; store.New();
    const auto action = [](SCraftResult&) { return EEconomyOutcome::Completed; };
    for (std::uint64_t id = 1; id <= 40; ++id)
    {
        const auto result = CAccountDataStore::ExecuteEconomyRequest("owner", {id, 0, "owner"}, EEconomyOperation::Loadout, "00", action);
        CHECK(result.has_receipt);
        if (id % 4 == 0) CHECK(CAccountDataStore::Flush(&store.error));
    }
    const auto old = CAccountDataStore::ExecuteEconomyRequest("owner", {1, 0, "owner"}, EEconomyOperation::Loadout, "00", action);
    CHECK(!old.has_receipt && old.receipt.outcome == EEconomyOutcome::OutcomeExpired);
    CHECK(CAccountDataStore::SandboxCloneAccount("@sandbox:scope", "owner"));
    std::uint64_t revision = 0, last = 0;
    CHECK(CAccountDataStore::GetEconomyCounters("@sandbox:scope", revision, last)); CHECK(revision == 0 && last == 0);
    const auto temporary = CAccountDataStore::ExecuteEconomyRequest("@sandbox:scope", {1, 0, "@sandbox:scope"}, EEconomyOperation::Loadout, "00", action);
    CHECK(temporary.has_receipt && CAccountDataStore::EconomyReceiptCommitted(temporary.receipt));
    SPlayerAccount captured; CHECK(CAccountDataStore::CaptureSandboxAccount("@sandbox:scope", captured));
    CAccountDataStore::RemoveSandboxAccount("@sandbox:scope");
    CHECK(CAccountDataStore::RestoreSandboxAccount("@sandbox:scope", captured));
    CHECK(CAccountDataStore::GetEconomyCounters("@sandbox:scope", revision, last)); CHECK(last == 1);
    CHECK(CAccountDataStore::GetEconomyCounters("owner", revision, last)); CHECK(last == 40);
}

TEST(EconomyLoadoutIsPreparedAsAWholeAndCanSwapWithoutSpareItems)
{
    Store store; store.New();
    // Build a complete authoritative fixture through the migration/import
    // boundary, rather than inventing items through an unsafe prepared plan.
    auto accounts = CAccountDataStore::SnapshotAccounts();
    for (auto& account : accounts) if (account.name == "owner")
    {
        account.inventory.clear(); account.slots = {{basic, 1, 1}};
        account.secondary_slots = {{static_cast<uint8_t>(EPetalType::Leaf), 1, 1}};
    }
    CHECK(CAccountDataStore::ImportAccounts(accounts, &store.error));
    const auto primary = CAccountDataStore::GetSlots("owner"), secondary = CAccountDataStore::GetSecondarySlots("owner");
    auto plan = CAccountDataStore::PrepareLoadout("owner", secondary, primary, &store.error);
    CHECK(plan.has_value()); if (!plan) return;
    CHECK(CAccountDataStore::GetSlots("owner") == primary); // preparation is read-only
    auto moved = std::move(*plan);
    CHECK(!CAccountDataStore::CommitLoadout(std::move(*plan)));
    CHECK(CAccountDataStore::CommitLoadout(std::move(moved)));
    CHECK(CAccountDataStore::GetSlots("owner") == secondary && CAccountDataStore::GetSecondarySlots("owner") == primary);
    CHECK(CAccountDataStore::GetInventory("owner").empty());
    CHECK(!CAccountDataStore::CommitLoadout(std::move(moved)));
    const auto before = CAccountDataStore::SnapshotAccounts();
    auto impossible = secondary; impossible.push_back({basic, 1, 1});
    CHECK(!CAccountDataStore::PrepareLoadout("owner", impossible, primary, &store.error));
    CHECK(CAccountDataStore::SnapshotAccounts() == before);
}

TEST(EconomyLoadoutRevisionFenceAndFinalBagCapProtectAllResources)
{
    Store store; store.New();
    auto accounts = CAccountDataStore::SnapshotAccounts();
    for (auto& account : accounts) if (account.name == "owner")
    {
        account.inventory = {{basic, 1, max_inventory_item_count}};
        account.slots = {{basic, 1, 1}}; account.secondary_slots.clear();
    }
    CHECK(CAccountDataStore::ImportAccounts(accounts, &store.error));
    const std::vector<SInventoryItem> empty, one{{basic, 1, 1}};
    CHECK(!CAccountDataStore::PrepareLoadout("owner", empty, empty, &store.error));
    auto plan = CAccountDataStore::PrepareLoadout("owner", empty, one, &store.error);
    CHECK(plan.has_value()); if (!plan) return;
    CHECK(CAccountDataStore::TakeItem("owner", basic, 1));
    CHECK(!CAccountDataStore::CommitLoadout(std::move(*plan)));
    CHECK(CAccountDataStore::GetSlots("owner") == one);
    plan = CAccountDataStore::PrepareLoadout("owner", empty, one, &store.error);
    CHECK(plan && CAccountDataStore::CommitLoadout(std::move(*plan)));
    CHECK(CAccountDataStore::HasItem("owner", basic, 1, max_inventory_item_count - 1));
}
