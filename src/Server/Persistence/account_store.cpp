#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "account_store.h"
#include "mysql_account_backend.h"
#include "economy_codec.h"
#include "account_identity_codec.h"
#include "../../Engine/json_value.h"
#include "../../Shared/game_config.h"
#include "../../Shared/immutable_snapshot.h"
#include "../../Shared/petal_type.h"
#include "../../Shared/rarity.h"
#include "../../Shared/tools.h"
#include <algorithm>
#include <array>
#include <argon2.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
std::filesystem::path g_account_path;
std::vector<SPlayerAccount> g_accounts;
florrbt::ImmutableSnapshotCache<SPlayerAccount> g_snapshot_cache;
int g_save_batch_depth = 0;
bool g_save_batch_dirty = false;
// 启动保存线程前固定后端配置，工作线程不读取可变配置字符串；切换后端需重启。
bool g_backend_is_mysql = false;
bool g_unique_ledger_initialized = false;
bool g_unique_migration_pending = false;
std::vector<SUniquePetalOwner> g_unique_owners;
bool g_economy_operation_active = false;
std::vector<std::string> g_economy_accounts;

struct SAccountSaveSnapshot
{
    std::filesystem::path path;
    SAccountSnapshot accounts;
    std::uint64_t generation = 0;
    bool unique_ledger_initialized = false;
};

constexpr auto save_coalesce_delay = std::chrono::milliseconds(50);
constexpr auto save_max_coalesce_delay = std::chrono::milliseconds(250);
constexpr auto shutdown_retry_delay = std::chrono::milliseconds(25);
constexpr int shutdown_flush_attempts = 3;
std::mutex g_save_mutex;
std::condition_variable g_save_condition;
std::thread g_save_worker;
std::optional<SAccountSaveSnapshot> g_pending_save;
std::uint64_t g_next_save_generation = 0;
std::uint64_t g_finished_save_generation = 0;
std::uint64_t g_persisted_save_generation = 0;
std::uint64_t g_flush_generation = 0;
std::string g_last_save_error;
bool g_save_worker_running = false;
bool g_accept_saves = false;
bool g_stop_save_worker = false;

bool WriteAccountData(const SAccountSaveSnapshot& snapshot, std::string* error);
bool StartSaveWorker(std::uint64_t initial_generation, std::string* error);
bool QueueAccountSave(std::uint64_t* generation, std::string* error);

void TouchInventory(SPlayerAccount& account)
{
    if (account.economy.inventory_revision == (std::numeric_limits<std::uint64_t>::max)())
        throw std::overflow_error("Inventory revision exhausted");
    ++account.economy.inventory_revision;
}

std::int64_t UtcNowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool IsLowerHex(const std::string& value, size_t length)
{
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool ValidRecoveryCredential(const SAccountRecoveryCredential& credential)
{
    return IsLowerHex(credential.device_id, 32) && credential.verifier.starts_with("$argon2id$") &&
        credential.verifier.size() <= 128 && credential.expires_at > 0 && credential.expires_at <= 9007199254740991LL;
}

bool ValidateRecoveryCredentials(const std::vector<SPlayerAccount>& accounts, std::string* error)
{
    for (const auto& account : accounts)
    {
        std::vector<std::string> devices;
        if (account.recovery_credentials.size() > CAccountDataStore::max_recovery_credentials)
        { if (error) *error = "Too many account recovery credentials"; return false; }
        for (const auto& credential : account.recovery_credentials)
        {
            if (!ValidRecoveryCredential(credential) || std::find(devices.begin(), devices.end(), credential.device_id) != devices.end())
            { if (error) *error = "Invalid or duplicate account recovery credential"; return false; }
            devices.push_back(credential.device_id);
        }
    }
    return true;
}

bool IssueRecoveryCredential(SAccountAuthResult& result, const std::string& password_hash)
{
    if (result.device_id.empty()) return true; // 内部调用可仅验证密码。
    std::array<uint8_t, 32> random;
    std::array<uint8_t, 16> salt;
    if (!PlatformRandomBytes(random.data(), random.size()) || !PlatformRandomBytes(salt.data(), salt.size()))
    { result.error = "Failed to generate recovery credential"; return false; }
    constexpr char hex[] = "0123456789abcdef";
    result.recovery_token.clear(); result.recovery_token.reserve(64);
    for (auto byte : random) { result.recovery_token += hex[byte >> 4]; result.recovery_token += hex[byte & 15]; }
    // 验证信息绑定密码哈希，重置密码或导入后旧凭据自动失效。
    const auto secret = result.recovery_token + ":" + password_hash;
    char verifier[128];
    if (argon2id_hash_encoded(2, 65536, 1, secret.data(), secret.size(), salt.data(), salt.size(), 32,
                              verifier, sizeof(verifier)) != ARGON2_OK)
    { result.recovery_token.clear(); result.error = "Failed to prepare recovery credential"; return false; }
    result.recovery_credential = {result.device_id, verifier, UtcNowSeconds() + CAccountDataStore::recovery_token_lifetime_seconds};
    return true;
}

bool ValidateUniqueOwnership(const std::vector<SPlayerAccount>& accounts, std::string* error)
{
    std::array<const SPlayerAccount*, 256> owners{};
    std::array<std::uint64_t, 256> counts{};
    auto fail = [&](const std::string& message) { if (error) *error = message; return false; };
    for (const auto& account : accounts)
    {
        if (account.name.rfind("@sandbox:", 0) == 0) continue;
        for (const auto& record : account.unique_petals)
        {
            if (!IsKnownPetalTypeId(record.petal_type) || record.petal_type == 0 || record.cost < 0 ||
                owners[record.petal_type])
                return fail("Invalid or duplicate Unique ownership for petal " + std::to_string(record.petal_type));
            owners[record.petal_type] = &account;
        }
    }
    for (const auto& account : accounts)
    {
        if (account.name.rfind("@sandbox:", 0) == 0) continue;
        for (const auto* items : { &account.inventory, &account.slots, &account.secondary_slots })
            for (const auto& item : *items)
                if (item.rarity == static_cast<uint8_t>(ERarity::Unique) && item.count > 0)
                {
                    if (owners[item.petal_type] != &account)
                        return fail("Unique item/owner mismatch for petal " + std::to_string(item.petal_type) +
                                    " in account '" + account.name + "'");
                    counts[item.petal_type] += item.count;
                }
    }
    for (size_t type = 0; type < owners.size(); ++type)
        if (owners[type] && counts[type] != 1)
            return fail("Unique petal " + std::to_string(type) + " must exist exactly once across inventory and slots");
    return true;
}

void RebuildUniqueOwners()
{
    std::vector<SUniquePetalOwner> owners;
    for (const auto& account : g_accounts)
        if (account.name.rfind("@sandbox:", 0) != 0)
            for (const auto& record : account.unique_petals)
                owners.push_back({static_cast<EPetalType>(record.petal_type), record.cost, account.name});
    std::sort(owners.begin(), owners.end(), [](const auto& a, const auto& b) { return a.type < b.type; });
    g_unique_owners = std::move(owners);
}

std::string TrimAsciiWhitespace(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;

    size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

bool NormalizeEmailAddress(const std::string& input, std::string& normalized)
{
    normalized = TrimAsciiWhitespace(input);
    if (normalized.size() < 3 || normalized.size() > 254) return false;

    const size_t at = normalized.find('@');
    if (at == std::string::npos || at == 0 || at > 64 || at + 1 >= normalized.size() ||
        normalized.find('@', at + 1) != std::string::npos)
        return false;

    const std::string local = normalized.substr(0, at);
    const std::string domain = normalized.substr(at + 1);
    if (local.front() == '.' || local.back() == '.' || local.find("..") != std::string::npos ||
        domain.front() == '.' || domain.back() == '.' || domain.find("..") != std::string::npos ||
        domain.find('.') == std::string::npos)
        return false;

    size_t label_length = 0;
    for (size_t i = 0; i < normalized.size(); ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(normalized[i]);
        if (ch <= 0x20 || ch >= 0x7f) return false;
        if (i < at)
        {
            constexpr std::string_view allowed_local_specials = "!#$%&'*+-/=?^_`{|}~.";
            if (!(std::isalnum(ch) || allowed_local_specials.find(static_cast<char>(ch)) != std::string_view::npos))
                return false;
            continue;
        }
        if (i == at) continue;

        if (ch == '.')
        {
            if (label_length == 0 || label_length > 63 || normalized[i - 1] == '-') return false;
            label_length = 0;
            continue;
        }
        if (!(std::isalnum(ch) || ch == '-')) return false;
        if (label_length == 0 && ch == '-') return false;
        ++label_length;
    }
    if (label_length == 0 || label_length > 63 || normalized.back() == '-') return false;

    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return true;
}

bool IsValidClientIp(const std::string& client_ip)
{
    if (client_ip.size() > 64) return false;
    return std::none_of(client_ip.begin(), client_ip.end(),
                        [](unsigned char ch) { return ch <= 0x20 || ch == 0x7f; });
}

const SPlayerAccount* FindAccountByEmail(const std::string& normalized_email,
                                         const std::string* excluded_account_name = nullptr)
{
    for (const SPlayerAccount& account : g_accounts)
    {
        if (excluded_account_name && account.name == *excluded_account_name) continue;
        std::string existing_email;
        if (!NormalizeEmailAddress(account.email, existing_email)) continue;
        if (existing_email == normalized_email) return &account;
    }
    return nullptr;
}

std::int64_t JsonExpToInt64(const CJsonValue& value)
{
    long double number = static_cast<long double>(value.AsNumber(0.0));
    if (!std::isfinite(static_cast<double>(number)) || number <= 0.0L) return 0;
    const long double max_exp = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (number >= max_exp) return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(std::llround(number));
}

std::uint64_t JsonGenerationToUint64(const CJsonValue* value)
{
    if (!value) return 0;
    if (value->IsString())
    {
        const std::string& text = value->AsString();
        std::uint64_t generation = 0;
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), generation);
        if (ec == std::errc{} && end == text.data() + text.size()) return generation;
        return 0;
    }
    if (!value->IsNumber()) return 0;

    const long double number = static_cast<long double>(value->AsNumber(0.0));
    if (!std::isfinite(static_cast<double>(number)) || number <= 0.0L) return 0;
    const long double max_generation = static_cast<long double>((std::numeric_limits<std::uint64_t>::max)());
    if (number >= max_generation) return (std::numeric_limits<std::uint64_t>::max)();
    return static_cast<std::uint64_t>(number);
}

std::string EscapeJsonString(const std::string& text)
{
    std::ostringstream out;
    for (char ch : text)
    {
        switch (ch)
        {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch);
            else out << ch;
            break;
        }
    }
    return out.str();
}

std::string JsonToString(const CJsonValue& value)
{
    if (value.IsNull()) return "null";
    if (value.IsBool()) return value.AsBool() ? "true" : "false";
    if (value.IsNumber())
    {
        std::ostringstream out;
        out << value.AsNumber();
        return out.str();
    }
    if (value.IsString()) return "\"" + EscapeJsonString(value.AsString()) + "\"";
    if (value.IsArray())
    {
        std::string result = "[";
        bool first = true;
        for (const CJsonValue& child : value.AsArray())
        {
            if (!first) result += ",";
            result += JsonToString(child);
            first = false;
        }
        result += "]";
        return result;
    }
    if (value.IsObject())
    {
        std::string result = "{";
        bool first = true;
        for (const auto& [key, child] : value.AsObject())
        {
            if (!first) result += ",";
            result += "\"" + EscapeJsonString(key) + "\":" + JsonToString(child);
            first = false;
        }
        result += "}";
        return result;
    }
    return "{}";
}

std::vector<SInventoryItem> ParseInventory(const CJsonValue* value)
{
    std::vector<SInventoryItem> inventory;
    if (!value || !value->IsArray()) return inventory;

    for (const CJsonValue& item_value : value->AsArray())
    {
        if (!item_value.IsObject()) continue;

        SInventoryItem item;
        if (const CJsonValue* type = item_value.Find("type")) item.petal_type = static_cast<uint8_t>(type->AsInt());
        if (const CJsonValue* rarity = item_value.Find("rarity")) item.rarity = static_cast<uint8_t>(rarity->AsInt());
        if (const CJsonValue* count = item_value.Find("count"))
            item.count =
                std::min<uint32_t>(static_cast<uint32_t>((std::max)(0, count->AsInt())), max_inventory_item_count);
        if (item.petal_type != 0 && item.rarity != 0 && item.count > 0) inventory.push_back(item);
    }
    return inventory;
}

std::vector<SInventoryItem> ParseSlots(const CJsonValue* value)
{
    std::vector<SInventoryItem> slots;
    if (!value || !value->IsArray()) return slots;

    for (const CJsonValue& item_value : value->AsArray())
    {
        SInventoryItem item;
        if (item_value.IsObject())
        {
            if (const CJsonValue* type = item_value.Find("type")) item.petal_type = static_cast<uint8_t>(type->AsInt());
            if (const CJsonValue* rarity = item_value.Find("rarity"))
                item.rarity = static_cast<uint8_t>(rarity->AsInt());
            if (const CJsonValue* count = item_value.Find("count"))
                item.count =
                    std::min<uint32_t>(static_cast<uint32_t>((std::max)(0, count->AsInt())), max_inventory_item_count);
        }
        slots.push_back(item);
    }
    return slots;
}

std::vector<SAccountTalent> ParseTalents(const CJsonValue* value)
{
    std::vector<SAccountTalent> talents;
    if (!value || !value->IsArray()) return talents;

    for (const CJsonValue& talent_value : value->AsArray())
    {
        if (!talent_value.IsObject()) continue;

        SAccountTalent talent;
        if (const CJsonValue* id = talent_value.Find("id")) talent.id = static_cast<ETalentId>(id->AsInt());
        if (const CJsonValue* rarity = talent_value.Find("rarity"))
            talent.rarity = static_cast<ERarity>(rarity->AsInt());
        if (const CJsonValue* rank = talent_value.Find("rank")) talent.rank = std::max(0, rank->AsInt());

        if (talent.id == ETalentId::None) continue;
        if (!IsKnownRarity(talent.rarity)) continue;

        auto it = std::find_if(talents.begin(), talents.end(), [&talent](const SAccountTalent& existing) {
            return existing.id == talent.id && existing.rarity == talent.rarity && existing.rank == talent.rank;
        });
        if (it == talents.end()) talents.push_back(talent);
    }
    return talents;
}

std::vector<SInventoryItem> DefaultSlots()
{
    std::vector<SInventoryItem> slots(5);
    for (size_t i = 0; i < 4; ++i)
        slots[i] = { static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Common), 1 };
    slots[4] = { static_cast<uint8_t>(EPetalType::Rose), static_cast<uint8_t>(ERarity::Common), 1 };
    return slots;
}

int CalculateTalentPointsForLevel(int level)
{
    if (level <= 1) return 0;

    int gain = 1;
    if (level % 5 == 0) gain += 5;
    if (level % 10 == 0) gain += 10;
    return gain;
}

int TotalTalentPointsForLevel(int level)
{
    int total = 0;
    for (int current_level = 2; current_level <= std::max(1, level); ++current_level)
        total += CalculateTalentPointsForLevel(current_level);
    return total;
}

uint8_t CalculateCraftResultRarity(uint8_t rarity)
{
    if (rarity == static_cast<uint8_t>(ERarity::Super)) return static_cast<uint8_t>(ERarity::Eternal);
    if (rarity == static_cast<uint8_t>(ERarity::Eternal)) return static_cast<uint8_t>(ERarity::Primordial);
    if (rarity >= static_cast<uint8_t>(ERarity::Common) && rarity < static_cast<uint8_t>(ERarity::Super))
        return static_cast<uint8_t>(rarity + 1);
    return 0;
}

float CalculateCraftSuccessChance(uint8_t rarity)
{
    if (rarity == static_cast<uint8_t>(ERarity::Super)) return 0.0033f;
    if (rarity == static_cast<uint8_t>(ERarity::Eternal)) return 0.001f;
    if (rarity >= static_cast<uint8_t>(ERarity::Common) && rarity < static_cast<uint8_t>(ERarity::Super))
    {
        int level = GetLevel(static_cast<ERarity>(rarity));
        return 0.64f / std::pow(2.f, static_cast<float>(level - 1));
    }
    return 0.f;
}

void AddInventoryStack(std::vector<SInventoryItem>& inventory, uint8_t petal_type, uint8_t rarity, uint32_t count)
{
    if (petal_type == 0 || rarity == 0 || count == 0) return;

    auto it = std::find_if(inventory.begin(), inventory.end(), [petal_type, rarity](const SInventoryItem& item) {
        return item.petal_type == petal_type && item.rarity == rarity;
    });
    if (it == inventory.end())
    {
        inventory.push_back({ petal_type, rarity, std::min(count, max_inventory_item_count) });
        return;
    }

    uint64_t total = static_cast<uint64_t>(it->count) + count;
    it->count = static_cast<uint32_t>(std::min<uint64_t>(total, max_inventory_item_count));
}

bool AddInventoryStackExact(std::vector<SInventoryItem>& inventory, uint8_t petal_type, uint8_t rarity,
                            uint32_t count)
{
    if (count == 0) return true;
    if (petal_type == 0 || rarity == 0 || count > max_inventory_item_count) return false;

    auto it = std::find_if(inventory.begin(), inventory.end(), [petal_type, rarity](const SInventoryItem& item) {
        return item.petal_type == petal_type && item.rarity == rarity;
    });
    if (it == inventory.end())
    {
        inventory.push_back({ petal_type, rarity, count });
        return true;
    }
    if (it->count > max_inventory_item_count - count) return false;

    it->count += count;
    return true;
}

bool TakeStoredPetals(SPlayerAccount& account, uint8_t petal_type, uint8_t rarity, uint32_t count,
                      SAccountPetalMutation& mutation)
{
    if (count == 0) return true;

    auto inventory_it = std::find_if(
        account.inventory.begin(), account.inventory.end(), [petal_type, rarity](const SInventoryItem& item) {
            return item.petal_type == petal_type && item.rarity == rarity && item.count > 0;
        });
    if (inventory_it != account.inventory.end())
    {
        const uint32_t taken = std::min(inventory_it->count, count);
        inventory_it->count -= taken;
        count -= taken;
        if (inventory_it->count == 0) account.inventory.erase(inventory_it);
    }

    for (size_t index = 0; count > 0 && index < account.slots.size(); ++index)
    {
        SInventoryItem& slot = account.slots[index];
        if (slot.petal_type != petal_type || slot.rarity != rarity || slot.count == 0) continue;

        slot = {};
        --count;
        mutation.cleared_primary_slots.push_back(static_cast<uint8_t>(index));
    }

    for (SInventoryItem& slot : account.secondary_slots)
    {
        if (count == 0) break;
        if (slot.petal_type != petal_type || slot.rarity != rarity || slot.count == 0) continue;

        slot = {};
        --count;
        mutation.secondary_slots_changed = true;
    }
    return count == 0;
}

SAccountPetalMutation& FindOrAddMutation(SUniquePetalExchangeResult& result, const std::string& account_name)
{
    auto found = std::find_if(result.account_mutations.begin(), result.account_mutations.end(),
                              [&account_name](const SAccountPetalMutation& mutation) {
                                  return mutation.account_name == account_name;
                              });
    if (found != result.account_mutations.end()) return *found;

    result.account_mutations.push_back({ account_name });
    return result.account_mutations.back();
}

void WriteItemsJson(std::ofstream& file, const std::vector<SInventoryItem>& items)
{
    file << "[";
    for (size_t j = 0; j < items.size(); ++j)
    {
        const SInventoryItem& item = items[j];
        if (j != 0) file << ", ";
        file << "{\"type\": " << static_cast<int>(item.petal_type) << ", \"rarity\": " << static_cast<int>(item.rarity)
             << ", \"count\": " << item.count << "}";
    }
    file << "]";
}

void WriteTalentsJson(std::ofstream& file, const std::vector<SAccountTalent>& talents)
{
    file << "[";
    bool first = true;
    for (const SAccountTalent& talent : talents)
    {
        if (talent.id == ETalentId::None || static_cast<int>(talent.rarity) <= static_cast<int>(ERarity::Null))
            continue;
        if (!first) file << ", ";
        file << "{\"id\": " << static_cast<int>(talent.id) << ", \"rarity\": " << static_cast<int>(talent.rarity)
             << ", \"rank\": " << std::max(0, talent.rank) << "}";
        first = false;
    }
    file << "]";
}

void NormalizeInventory(std::vector<SInventoryItem>& inventory)
{
    std::vector<SInventoryItem> normalized;
    for (const SInventoryItem& item : inventory)
    {
        if (item.petal_type == 0 || item.rarity == 0 || item.count == 0) continue;

        auto it = std::find_if(normalized.begin(), normalized.end(), [&item](const SInventoryItem& existing) {
            return existing.petal_type == item.petal_type && existing.rarity == item.rarity;
        });
        if (it == normalized.end())
        {
            normalized.push_back(item);
            continue;
        }

        uint64_t total = static_cast<uint64_t>(it->count) + item.count;
        it->count = static_cast<uint32_t>(std::min<uint64_t>(total, max_inventory_item_count));
    }
    inventory = std::move(normalized);
}

bool BackupCorruptAccountData(const std::filesystem::path& path, const std::string& reason, std::string* warning)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::filesystem::path backup = path.string() + ".corrupt";
    for (int i = 1; i < 1000 && std::filesystem::exists(backup, ec); ++i)
    {
        ec.clear();
        backup = path.string() + ".corrupt." + std::to_string(i);
    }

    ec.clear();
    std::filesystem::rename(path, backup, ec);
    if (ec)
    {
        if (warning) *warning = "Failed to move corrupted account data " + path.string() + ": " + ec.message();
        return false;
    }

    if (warning)
        *warning = "Account data parse failed (" + reason + "); moved corrupted file to " + backup.string() + ".";
    return true;
}
}

CAccountDataStore::CSaveBatch::CSaveBatch() { ++g_save_batch_depth; }

CAccountDataStore::CSaveBatch::~CSaveBatch()
{
    if (g_save_batch_depth <= 0) return;
    --g_save_batch_depth;
    if (g_save_batch_depth != 0 || !g_save_batch_dirty) return;
    g_save_batch_dirty = false;
    if (!CAccountDataStore::Save()) g_save_batch_dirty = true;
}

bool CAccountDataStore::Load(const std::filesystem::path& path, std::string* error)
{
    if (error) error->clear();
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (g_save_worker_running || g_save_worker.joinable())
        {
            if (error) *error = "Account data store is already loaded";
            return false;
        }
    }

    g_account_path = path;
    g_accounts.clear();
    g_unique_owners.clear();
    g_unique_ledger_initialized = false;
    g_unique_migration_pending = false;
    g_snapshot_cache.Reset();
    g_save_batch_depth = 0;
    g_save_batch_dirty = false;
    g_backend_is_mysql = game_config::account_backend == "mysql";

    bool create_fresh_file = false;
    std::uint64_t loaded_generation = 0;
    std::string load_warning;

    if (g_backend_is_mysql)
    {
        std::string mysql_error;
        if (!mysql_account_backend::LoadAll(g_accounts, loaded_generation, &mysql_error, &g_unique_ledger_initialized))
        {
            if (error) *error = "Failed to load accounts from MySQL: " + mysql_error;
            return false;
        }
    } else if (!std::filesystem::exists(path))
    {
        create_fresh_file = true;
    } else
    {
        std::string parse_error;
        std::optional<CJsonValue> root_value = CJsonValue::LoadFromFile(path, &parse_error);
        if (!root_value || !root_value->IsObject())
        {
            const std::string reason = root_value ? "Root is not an object" : parse_error;
            if (!BackupCorruptAccountData(path, reason, &load_warning))
            {
                if (error) *error = load_warning;
                return false;
            }
            create_fresh_file = true;
        } else
        {
            loaded_generation = JsonGenerationToUint64(root_value->Find("generation"));
            if (const auto* version = root_value->Find("unique_ledger_version"))
            {
                if (!version->IsNumber() || (version->AsNumber() != 0 && version->AsNumber() != 1))
                {
                    if (error) *error = "Unsupported Unique ownership ledger version";
                    return false;
                }
                g_unique_ledger_initialized = version->AsNumber() == 1;
            }
            const CJsonValue* accounts_value = root_value->Find("accounts");
            if (accounts_value && accounts_value->IsArray())
            {
                for (const CJsonValue& account_value : accounts_value->AsArray())
                {
                    if (!account_value.IsObject()) continue;

                    SPlayerAccount account;
                    if (!ReadAccountIdentity(account_value.Find("identity"), account, error)) return false;
                    if (const CJsonValue* name = account_value.Find("name")) account.name = name->AsString();
                    if (const CJsonValue* password = account_value.Find("password"))
                        account.password = password->AsString();
                    if (const CJsonValue* email = account_value.Find("email"))
                    {
                        std::string normalized_email;
                        account.email = NormalizeEmailAddress(email->AsString(), normalized_email)
                                            ? std::move(normalized_email)
                                            : TrimAsciiWhitespace(email->AsString());
                    }
                    if (const CJsonValue* trusted_ip = account_value.Find("trusted_ip"))
                    {
                        const std::string loaded_ip = trusted_ip->AsString();
                        if (IsValidClientIp(loaded_ip)) account.trusted_ip = loaded_ip;
                    }
                    if (account.trusted_ip.empty())
                    {
                        // 兼容早期开发版本的字段名。
                        if (const CJsonValue* common_ip = account_value.Find("common_ip"))
                        {
                            const std::string loaded_ip = common_ip->AsString();
                            if (IsValidClientIp(loaded_ip)) account.trusted_ip = loaded_ip;
                        }
                    }
                    if (const CJsonValue* email_verified = account_value.Find("email_verified"))
                        account.email_verified = email_verified->AsBool();
                    std::string validated_email;
                    if (!NormalizeEmailAddress(account.email, validated_email)) account.email_verified = false;
                    if (const CJsonValue* level = account_value.Find("level"))
                        account.level = std::max(1, level->AsInt(1));
                    if (const CJsonValue* exp = account_value.Find("exp")) account.exp = JsonExpToInt64(*exp);
                    const CJsonValue* extra = account_value.Find("extra");
                    if (extra) account.extra_json = JsonToString(*extra);
                    bool loaded_talent_points = false;
                    if (const CJsonValue* talent_points = account_value.Find("talent_points"))
                    {
                        account.talent_points = std::max(0, talent_points->AsInt());
                        loaded_talent_points = true;
                    } else if (extra && extra->IsObject())
                    {
                        if (const CJsonValue* talent_points = extra->Find("talent_points"))
                        {
                            account.talent_points = std::max(0, talent_points->AsInt());
                            loaded_talent_points = true;
                        } else if (const CJsonValue* talent_points_alt = extra->Find("tp"))
                        {
                            account.talent_points = std::max(0, talent_points_alt->AsInt());
                            loaded_talent_points = true;
                        }
                    }
                    account.inventory = ParseInventory(account_value.Find("inventory"));
                    account.slots = ParseSlots(account_value.Find("slots"));
                    account.secondary_slots = ParseSlots(account_value.Find("secondary_slots"));
                    const auto* unique_petals = account_value.Find("unique_petals");
                    if ((g_unique_ledger_initialized && !unique_petals) || (unique_petals && !unique_petals->IsArray()))
                    {
                        if (error) *error = "Missing or invalid Unique ownership for '" + account.name + "'";
                        return false;
                    }
                    if (unique_petals)
                        for (const auto& record : unique_petals->AsArray())
                        {
                            if (!record.IsArray() || record.AsArray().size() != 2)
                            {
                                if (error) *error = "Invalid Unique ownership entry";
                                return false;
                            }
                            const auto& fields = record.AsArray();
                            const int type = fields[0].AsInt(-1), cost = fields[1].AsInt(-1);
                            if (!fields[0].IsNumber() || !fields[1].IsNumber() || fields[0].AsNumber() != type ||
                                fields[1].AsNumber() != cost || type <= 0 || type > 255 || cost < 0)
                            {
                                if (error) *error = "Invalid Unique ownership type/cost";
                                return false;
                            }
                            account.unique_petals.push_back({static_cast<uint8_t>(type), cost});
                        }
                    account.talents = ParseTalents(account_value.Find("talents"));
                    if (!ReadEconomyState(account_value.Find("economy"), account.economy, error)) return false;
                    if (const auto* credentials = account_value.Find("recovery_credentials"))
                    {
                        if (!credentials->IsArray()) { if (error) *error = "Invalid account recovery credentials"; return false; }
                        for (const auto& entry : credentials->AsArray())
                        {
                            const auto* device = entry.Find("device_id"), *verifier = entry.Find("verifier"), *expiry = entry.Find("expires_at");
                            if (!device || !device->IsString() || !verifier || !verifier->IsString() || !expiry || !expiry->IsNumber() ||
                                !std::isfinite(expiry->AsNumber()) || expiry->AsNumber() <= 0 || expiry->AsNumber() > 9007199254740991.0 ||
                                std::floor(expiry->AsNumber()) != expiry->AsNumber())
                            { if (error) *error = "Invalid recovery credential record"; return false; }
                            account.recovery_credentials.push_back({device->AsString(), verifier->AsString(), static_cast<std::int64_t>(expiry->AsNumber())});
                        }
                    }
                    if (const CJsonValue* codex_value = account_value.Find("codex"))
                    {
                        if (codex_value->IsArray())
                        {
                            for (const CJsonValue& entry_value : codex_value->AsArray())
                            {
                                if (!entry_value.IsArray() || entry_value.AsArray().size() < 3) continue;
                                const auto& parts = entry_value.AsArray();
                                SAccountCodexEntry entry;
                                entry.mob_type = static_cast<uint8_t>(parts[0].AsInt());
                                entry.rarity = static_cast<uint8_t>(parts[1].AsInt());
                                entry.count = static_cast<uint32_t>(std::max(0, parts[2].AsInt()));
                                if (entry.mob_type > 0 && entry.rarity > 0 && entry.count > 0)
                                    account.codex.push_back(entry);
                            }
                        }
                    }
                    if (account.talents.empty() && extra && extra->IsObject())
                        account.talents = ParseTalents(extra->Find("talents"));
                    if (!loaded_talent_points && account.talents.empty())
                        account.talent_points = TotalTalentPointsForLevel(account.level);
                    NormalizeInventory(account.inventory);

                    if (!account.name.empty() && !account.password.empty() && !FindAccount(account.name))
                        g_accounts.push_back(std::move(account));
                }
            }
        }
    }

    bool identity_migrated = false;
    if (!ValidateAccountIdentities(g_accounts, identity_migrated, error) || !ValidateRecoveryCredentials(g_accounts, error)) return false;
    if (g_unique_ledger_initialized && !ValidateUniqueOwnership(g_accounts, error)) return false;
    RebuildUniqueOwners();
    if (!StartSaveWorker(loaded_generation, error)) return false;
    if (create_fresh_file || (identity_migrated && g_unique_ledger_initialized))
    {
        std::string save_error;
        if (!Flush(&save_error))
        {
            std::string shutdown_error;
            ShutDown(&shutdown_error);
            if (error)
            {
                *error = "Failed to create fresh account data: " + save_error;
                if (!shutdown_error.empty()) *error += "; shutdown retry failed: " + shutdown_error;
            }
            return false;
        }
        if (!load_warning.empty()) load_warning += " Created a fresh account file.";
    }
    if (error) *error = load_warning;
    return true;
}

bool CAccountDataStore::Save(std::string* error)
{
    if (g_save_batch_depth > 0)
    {
        g_save_batch_dirty = true;
        if (error) error->clear();
        return true;
    }

    const bool saved = QueueAccountSave(nullptr, error);
    g_save_batch_dirty = !saved;
    return saved;
}

bool CAccountDataStore::Flush(std::string* error)
{
    if (g_economy_operation_active) { if (error) *error = "Cannot flush an unfinished economy operation"; return false; }
    std::uint64_t generation = 0;
    if (!QueueAccountSave(&generation, error)) return false;

    std::unique_lock<std::mutex> lock(g_save_mutex);
    g_flush_generation = std::max(g_flush_generation, generation);
    g_save_condition.notify_all();
    g_save_condition.wait(
        lock, [generation]() { return g_finished_save_generation >= generation || !g_save_worker_running; });

    if (g_persisted_save_generation >= generation)
    {
        if (error) error->clear();
        return true;
    }
    if (error)
        *error = g_last_save_error.empty()
                     ? "Account save worker stopped before flushing generation " + std::to_string(generation)
                     : g_last_save_error;
    return false;
}

bool CAccountDataStore::ShutDown(std::string* error)
{
    if (g_economy_operation_active) { if (error) *error = "Cannot stop an unfinished economy operation"; return false; }
    if (error) error->clear();
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (!g_save_worker_running && !g_save_worker.joinable()) return true;
    }

    std::string flush_error;
    bool flushed = false;
    for (int attempt = 0; attempt < shutdown_flush_attempts && !flushed; ++attempt)
    {
        if (attempt > 0) std::this_thread::sleep_for(shutdown_retry_delay * attempt);
        flushed = Flush(&flush_error);
    }
    {
        std::unique_lock<std::mutex> lock(g_save_mutex);
        g_accept_saves = false;
        g_stop_save_worker = true;
        const std::uint64_t stop_generation = g_next_save_generation;
        g_save_condition.notify_all();
        g_save_condition.wait(lock, [stop_generation]() {
            return g_finished_save_generation >= stop_generation || !g_save_worker_running;
        });
        if (g_persisted_save_generation < stop_generation)
        {
            flushed = false;
            flush_error = g_last_save_error.empty()
                              ? "Account save worker stopped before generation " + std::to_string(stop_generation)
                              : g_last_save_error;
        }
    }
    if (g_save_worker.joinable()) g_save_worker.join();
    if (g_backend_is_mysql) mysql_account_backend::ResetPublishState();

    if (!flushed && error) *error = flush_error;
    return flushed;
}

std::vector<SPlayerAccount> CAccountDataStore::SnapshotAccounts() { return g_accounts; }

std::uint64_t CAccountDataStore::SnapshotGeneration()
{
    std::lock_guard<std::mutex> lock(g_save_mutex);
    return g_next_save_generation;
}

bool CAccountDataStore::ImportAccounts(std::vector<SPlayerAccount> accounts, std::string* error)
{
    if (g_economy_operation_active) { if (error) *error = "Cannot replace accounts during an economy operation"; return false; }
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (!g_save_worker_running)
        {
            if (error) *error = "Account data store is not loaded";
            return false;
        }
    }
    bool identity_migrated = false;
    if (!ValidateAccountIdentities(accounts, identity_migrated, error) || !ValidateUniqueOwnership(accounts, error) || !ValidateRecoveryCredentials(accounts, error)) return false;
    for (const auto& account : accounts)
    {
        const auto json = CJsonValue::Parse(EconomyStateJson(account.economy), error);
        SAccountEconomyState checked;
        if (!json || !ReadEconomyState(&*json, checked, error)) return false;
    }
    std::uint64_t import_generation = 0;
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (g_next_save_generation == UINT64_MAX) { if (error) *error = "Account save generation exhausted"; return false; }
        import_generation = g_next_save_generation + 1;
    }
    for (auto& account : accounts)
        for (auto& receipt : account.economy.receipts)
            receipt.required_save_generation = account.name.starts_with("@sandbox:") ? 0 : import_generation;
    g_accounts = std::move(accounts);
    g_unique_ledger_initialized = true;
    RebuildUniqueOwners();
    g_snapshot_cache.Reset();
    return Save(error);
}

namespace
{
void AccountSaveWorkerMain()
{
    std::unique_lock<std::mutex> lock(g_save_mutex);
    auto retry_delay = std::chrono::milliseconds(100);
    while (true)
    {
        g_save_condition.wait(lock, []() { return g_stop_save_worker || g_pending_save.has_value(); });
        if (!g_pending_save && g_stop_save_worker) break;

        const auto max_coalesce_deadline = std::chrono::steady_clock::now() + save_max_coalesce_delay;
        while (g_pending_save && !g_stop_save_worker && g_flush_generation < g_pending_save->generation)
        {
            const std::uint64_t observed_generation = g_pending_save->generation;
            const auto quiet_deadline = std::chrono::steady_clock::now() + save_coalesce_delay;
            const bool interrupted = g_save_condition.wait_until(
                lock, std::min(quiet_deadline, max_coalesce_deadline), [observed_generation]() {
                    return g_stop_save_worker || !g_pending_save ||
                           g_flush_generation >= (g_pending_save ? g_pending_save->generation : 0) ||
                           (g_pending_save && g_pending_save->generation != observed_generation);
                });
            if (!interrupted || std::chrono::steady_clock::now() >= max_coalesce_deadline) break;
        }
        if (!g_pending_save) continue;

        SAccountSaveSnapshot snapshot = std::move(*g_pending_save);
        g_pending_save.reset();
        const bool already_persisted = snapshot.generation <= g_persisted_save_generation;
        lock.unlock();

        std::string save_error;
        bool success = already_persisted;
        if (!success)
        {
            try
            {
                success = WriteAccountData(snapshot, &save_error);
            } catch (const std::exception& exception)
            {
                save_error = "Unhandled account save exception: " + std::string(exception.what());
            } catch (...)
            {
                save_error = "Unhandled unknown account save exception";
            }
        }

        lock.lock();
        g_finished_save_generation = std::max(g_finished_save_generation, snapshot.generation);
        if (success)
        {
            g_persisted_save_generation = std::max(g_persisted_save_generation, snapshot.generation);
            g_last_save_error.clear();
            retry_delay = std::chrono::milliseconds(100);
        } else
        {
            g_last_save_error =
                "Failed to persist account generation " + std::to_string(snapshot.generation) + ": " + save_error;
        }
        g_save_condition.notify_all();
        if (!success && !g_stop_save_worker)
        {
            const auto failed_generation = snapshot.generation;
            // 临时存储失败不能永久丢弃最后一代未保存数据；较新的完整快照优先，旧重试不得覆盖。
            if (!g_pending_save || g_pending_save->generation < failed_generation)
                g_pending_save = std::move(snapshot);
            g_save_condition.wait_for(lock, retry_delay, [failed_generation]() {
                return g_stop_save_worker || !g_pending_save || g_pending_save->generation > failed_generation;
            });
            retry_delay = std::min(retry_delay * 2, std::chrono::milliseconds(2000));
        }
    }

    g_save_worker_running = false;
    g_save_condition.notify_all();
}

bool StartSaveWorker(std::uint64_t initial_generation, std::string* error)
{
    if (error) error->clear();
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (g_save_worker_running || g_save_worker.joinable())
        {
            if (error) *error = "Account save worker is already running";
            return false;
        }
        g_pending_save.reset();
        g_next_save_generation = initial_generation;
        g_finished_save_generation = initial_generation;
        g_persisted_save_generation = initial_generation;
        g_flush_generation = initial_generation;
        g_last_save_error.clear();
        g_stop_save_worker = false;
        g_accept_saves = true;
        g_save_worker_running = true;
    }

    try
    {
        g_save_worker = std::thread(AccountSaveWorkerMain);
    } catch (const std::exception& exception)
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        g_save_worker_running = false;
        g_accept_saves = false;
        if (error) *error = "Failed to start account save worker: " + std::string(exception.what());
        return false;
    }
    return true;
}

bool QueueAccountSave(std::uint64_t* generation, std::string* error)
{
    if (error) error->clear();
    if (g_account_path.empty())
    {
        if (error) *error = "Account data path is empty";
        return false;
    }

    SAccountSaveSnapshot snapshot;
    try
    {
        snapshot.path = g_account_path;
        snapshot.unique_ledger_initialized = g_unique_ledger_initialized;
        snapshot.accounts = g_snapshot_cache.Capture(g_accounts, [](const SPlayerAccount& account) {
            return !account.name.empty() && account.name.rfind("@sandbox:", 0) != 0;
        });
    } catch (const std::exception& exception)
    {
        if (error) *error = "Failed to capture account save snapshot: " + std::string(exception.what());
        return false;
    }

    std::lock_guard<std::mutex> lock(g_save_mutex);
    if (!g_accept_saves || !g_save_worker_running)
    {
        if (error) *error = "Account save worker is not running";
        return false;
    }
    if (g_next_save_generation == (std::numeric_limits<std::uint64_t>::max)())
    {
        if (error) *error = "Account save generation overflow";
        return false;
    }

    snapshot.generation = ++g_next_save_generation;
    if (generation) *generation = snapshot.generation;
    g_pending_save = std::move(snapshot);
    g_save_condition.notify_all();
    return true;
}

bool WriteAccountData(const SAccountSaveSnapshot& snapshot, std::string* error)
{
    if (error) error->clear();
    // 保存线程将快照发布到数据库。
    if (g_backend_is_mysql)
        return mysql_account_backend::PublishSnapshot(snapshot.accounts, snapshot.generation, error,
                                                       snapshot.unique_ledger_initialized);
    if (snapshot.path.empty())
    {
        if (error) *error = "Account data path is empty";
        return false;
    }

    std::error_code filesystem_error;
    if (!snapshot.path.parent_path().empty())
        std::filesystem::create_directories(snapshot.path.parent_path(), filesystem_error);
    if (filesystem_error)
    {
        if (error) *error = "Failed to create account data directory: " + filesystem_error.message();
        return false;
    }

    std::filesystem::path temporary_path = snapshot.path;
    temporary_path += ".tmp";
    std::ofstream file(temporary_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        if (error) *error = "Failed to write temporary account data " + temporary_path.string();
        return false;
    }

    file << "{\n  \"generation\": \"" << snapshot.generation << "\",\n  \"unique_ledger_version\": "
         << (snapshot.unique_ledger_initialized ? 1 : 0) << ",\n  \"accounts\": [\n";
    size_t saved_account_count = 0;
    for (size_t i = 0; i < snapshot.accounts.size(); ++i)
    {
        const SPlayerAccount& account = *snapshot.accounts[i];
        // 沙盒临时账号不写入持久化存储。
        if (account.name.rfind("@sandbox:", 0) == 0) continue;
        if (saved_account_count++ > 0) file << ",\n";
        file << "    {\n";
        file << "      \"name\": \"" << EscapeJsonString(account.name) << "\",\n";
        file << "      \"password\": \"" << EscapeJsonString(account.password) << "\",\n";
        file << "      \"email\": \"" << EscapeJsonString(account.email) << "\",\n";
        file << "      \"email_verified\": " << (account.email_verified ? "true" : "false") << ",\n";
        file << "      \"trusted_ip\": \"" << EscapeJsonString(account.trusted_ip) << "\",\n";
        file << "      \"identity\": " << AccountIdentityJson(account) << ",\n";
        file << "      \"economy\": "; WriteEconomyState(file, account.economy); file << ",\n";
        file << "      \"recovery_credentials\": [";
        for (size_t index = 0; index < account.recovery_credentials.size(); ++index)
        {
            if (index) file << ", ";
            const auto& credential = account.recovery_credentials[index];
            file << "{\"device_id\":\"" << EscapeJsonString(credential.device_id)
                 << "\",\"verifier\":\"" << EscapeJsonString(credential.verifier)
                 << "\",\"expires_at\":" << credential.expires_at << "}";
        }
        file << "],\n";
        file << "      \"level\": " << std::max(1, account.level) << ",\n";
        file << "      \"exp\": " << std::max<std::int64_t>(0, account.exp) << ",\n";
        file << "      \"inventory\": ";
        WriteItemsJson(file, account.inventory);
        file << ",\n";
        file << "      \"slots\": ";
        WriteItemsJson(file, account.slots);
        file << ",\n";
        file << "      \"secondary_slots\": ";
        WriteItemsJson(file, account.secondary_slots);
        file << ",\n";
        file << "      \"unique_petals\": [";
        for (size_t index = 0; index < account.unique_petals.size(); ++index)
        {
            if (index) file << ", ";
            file << "[" << static_cast<int>(account.unique_petals[index].petal_type) << ", "
                 << account.unique_petals[index].cost << "]";
        }
        file << "],\n";
        file << "      \"talent_points\": " << std::max(0, account.talent_points) << ",\n";
        file << "      \"talents\": ";
        WriteTalentsJson(file, account.talents);
        file << ",\n";
        file << "      \"codex\": [";
        for (size_t entry_index = 0; entry_index < account.codex.size(); ++entry_index)
        {
            const SAccountCodexEntry& entry = account.codex[entry_index];
            if (entry_index > 0) file << ", ";
            file << "[" << static_cast<int>(entry.mob_type) << ", " << static_cast<int>(entry.rarity) << ", "
                 << std::max<std::uint32_t>(0, entry.count) << "]";
        }
        file << "],\n";
        file << "      \"extra\": " << (account.extra_json.empty() ? "{}" : account.extra_json) << "\n";
        file << "    }\n";
    }
    file << "  ]\n}\n";
    file.flush();
    if (!file)
    {
        if (error) *error = "Failed to flush temporary account data " + temporary_path.string();
        file.close();
        std::filesystem::remove(temporary_path, filesystem_error);
        return false;
    }
    file.close();
    if (!file)
    {
        if (error) *error = "Failed to close temporary account data " + temporary_path.string();
        std::filesystem::remove(temporary_path, filesystem_error);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary_path.c_str(), snapshot.path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD win32_error = GetLastError();
        if (error)
            *error = "Failed to publish account data: " + std::system_category().message(static_cast<int>(win32_error));
        std::filesystem::remove(temporary_path, filesystem_error);
        return false;
    }
#else
    std::filesystem::rename(temporary_path, snapshot.path, filesystem_error);
    if (filesystem_error)
    {
        if (error) *error = "Failed to publish account data: " + filesystem_error.message();
        std::filesystem::remove(temporary_path, filesystem_error);
        return false;
    }
#endif
    return true;
}
}

bool CAccountDataStore::PrepareAuthentication(const std::string& name, const std::string& password,
                                               const std::string& email, const std::string& client_ip,
                                               bool register_mode, SAccountAuthWork& work, std::string* error,
                                               const std::string& device_id, const std::string& recovery_token)
{
    if (error) error->clear();
    work = {};
    if (name.empty() || name.starts_with("@sandbox:"))
    {
        if (error) *error = name.empty() ? "Account name is empty" : "Account name is reserved for internal gameplay state";
        return false;
    }
    if (!IsValidClientIp(client_ip))
    {
        if (error) *error = "Invalid client IP";
        return false;
    }

    const SPlayerAccount* account = FindAccountConst(name);
    std::string normalized_email;
    if (register_mode)
    {
        if (password.empty())
        {
            if (error) *error = "Password is empty";
            return false;
        }
        if (!email.empty() && !NormalizeEmailAddress(email, normalized_email))
        {
            if (error) *error = "Email address is invalid";
            return false;
        }
        if (account)
        {
            if (error) *error = "Account already exists";
            return false;
        }
        if (!normalized_email.empty() && FindAccountByEmail(normalized_email))
        {
            if (error) *error = "Email address is already in use";
            return false;
        }
    }
    else if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }

    if ((!device_id.empty() && !IsLowerHex(device_id, 32)) ||
        (!recovery_token.empty() && (!IsLowerHex(recovery_token, 64) || device_id.empty() || register_mode || !password.empty())))
    {
        if (error) *error = "Invalid recovery credential";
        return false;
    }
    if (!register_mode && password.empty() && recovery_token.empty())
    { if (error) *error = "Password or recovery credential is required"; return false; }
    if (!recovery_token.empty())
    {
        const auto credential = std::find_if(account->recovery_credentials.begin(), account->recovery_credentials.end(),
            [&](const auto& entry) { return entry.device_id == device_id; });
        if (credential == account->recovery_credentials.end() || credential->expires_at <= UtcNowSeconds())
        { if (error) *error = "Recovery credential expired or revoked; enter your password"; return false; }
        work.recovery_credential = *credential;
    }

    work.account_name = name;
    work.password = password;
    work.email = register_mode ? std::move(normalized_email) : account->email;
    work.client_ip = client_ip;
    work.register_mode = register_mode;
    work.device_id = device_id;
    work.recovery_token = recovery_token;
    work.recovery_mode = !recovery_token.empty();
    if (account)
    {
        work.stored_password_hash = account->password;
        work.email_verified = account->email_verified;
    }
    return true;
}

bool CAccountDataStore::PrepareAuthentication(const std::string& name, const std::string& password,
                                               bool register_mode, SAccountAuthWork& work, std::string* error)
{
    return PrepareAuthentication(name, password, {}, {}, register_mode, work, error);
}

SAccountAuthResult CAccountDataStore::ExecuteAuthentication(SAccountAuthWork work)
{
    SAccountAuthResult result;
    result.account_name = std::move(work.account_name);
    result.email = std::move(work.email);
    result.client_ip = std::move(work.client_ip);
    result.observed_password_hash = work.stored_password_hash;
    result.device_id = std::move(work.device_id);
    result.register_mode = work.register_mode;
    result.email_verification_required = !work.email_verified;

    if (work.register_mode)
    {
        uint8_t salt[16];
        if (!PlatformRandomBytes(salt, sizeof(salt)))
        {
            result.error = "Failed to generate salt";
            return result;
        }

        char hash[128];
        const int hash_result = argon2id_hash_encoded(2, // 迭代次数。
                                                      65536, // 内存开销，单位 KiB。
                                                      1, // 并行度。
                                                      work.password.c_str(), work.password.length(), salt,
                                                      sizeof(salt),
                                                      32, // 哈希字节数。
                                                      hash, sizeof(hash));
        if (hash_result != ARGON2_OK)
        {
            result.error = "Password hashing failed";
            return result;
        }

        result.generated_password_hash = hash;
        result.success = IssueRecoveryCredential(result, result.generated_password_hash);
        return result;
    }

    if (work.recovery_mode)
    {
        const auto secret = work.recovery_token + ":" + work.stored_password_hash;
        if (work.recovery_credential.expires_at <= UtcNowSeconds() ||
            argon2id_verify(work.recovery_credential.verifier.c_str(), secret.data(), secret.size()) != ARGON2_OK)
        { result.error = "Invalid or expired recovery credential"; return result; }
        result.recovery_verified = true;
        result.recovery_credential = std::move(work.recovery_credential);
        result.recovery_token = std::move(work.recovery_token);
        result.success = true;
        return result;
    }

    if (argon2id_verify(work.stored_password_hash.c_str(), work.password.c_str(), work.password.length()) != ARGON2_OK)
    {
        result.error = "Wrong password";
        return result;
    }

    result.password_verified = true;
    result.success = IssueRecoveryCredential(result, work.stored_password_hash);
    return result;
}

bool CAccountDataStore::CommitRegistration(const SAccountAuthResult& result, std::string* error)
{
    if (!result.success || !result.register_mode)
    {
        if (error) *error = "Invalid registration result";
        return false;
    }
    return CommitRegistrationInternal(result.account_name, result.generated_password_hash, result.email,
                                      result.client_ip, false, error);
}

bool CAccountDataStore::CommitVerifiedRegistration(const SAccountAuthResult& result, std::string* error)
{
    if (!result.success || !result.register_mode)
    {
        if (error) *error = "Invalid registration result";
        return false;
    }
    return CommitRegistrationInternal(result.account_name, result.generated_password_hash, result.email,
                                      result.client_ip, true, error);
}

bool CAccountDataStore::CommitRegistration(const std::string& name, const std::string& password_hash,
                                            const std::string& email, const std::string& trusted_ip,
                                            std::string* error)
{
    return CommitRegistrationInternal(name, password_hash, email, trusted_ip, false, error);
}

bool CAccountDataStore::CommitRegistrationInternal(const std::string& name, const std::string& password_hash,
                                                    const std::string& email, const std::string& trusted_ip,
                                                    bool email_verified, std::string* error)
{
    if (g_economy_operation_active) { if (error) *error = "Cannot register accounts during an economy operation"; return false; }
    if (error) error->clear();
    if (name.empty() || name.starts_with("@sandbox:") || password_hash.empty())
    {
        if (error) *error = "Invalid registration result";
        return false;
    }
    std::string normalized_email;
    if (email.empty())
    {
        if (email_verified)
        {
            if (error) *error = "Verified registration requires an email address";
            return false;
        }
    } else if (!NormalizeEmailAddress(email, normalized_email))
    {
        if (error) *error = "Email address is invalid";
        return false;
    }
    if (!IsValidClientIp(trusted_ip))
    {
        if (error) *error = "Invalid trusted IP";
        return false;
    }
    if (FindAccountConst(name))
    {
        if (error) *error = "Account already exists";
        return false;
    }
    if (!normalized_email.empty() && FindAccountByEmail(normalized_email))
    {
        if (error) *error = "Email address is already in use";
        return false;
    }

    SPlayerAccount new_account;
    new_account.name = name;
    new_account.password = password_hash;
    new_account.email = std::move(normalized_email);
    new_account.email_verified = email_verified;
    new_account.trusted_ip = trusted_ip;
    new_account.level = 1;
    new_account.exp = 0;
    new_account.slots = DefaultSlots();
    new_account.extra_json = "{}";
    if (!GenerateOpaqueId(new_account.account_id)) { if (error) *error = "Cannot allocate account identity"; return false; }
    g_accounts.push_back(std::move(new_account));
    if (Save(error)) return true;

    // 持久化管线接受后账号才算提交，同名排队请求仍可重试。
    g_accounts.pop_back();
    return false;
}

bool CAccountDataStore::CommitLogin(SAccountAuthResult& result, std::string* error)
{
    if (error) error->clear();
    if (!result.success || result.register_mode)
    {
        if (error) *error = "Invalid login result";
        return false;
    }

    SPlayerAccount* account = FindAccount(result.account_name);
    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }

    if (result.password_verified || result.recovery_verified)
    {
        if (result.observed_password_hash.empty() || account->password != result.observed_password_hash)
        {
            if (error) *error = "Account credentials changed during login";
            return false;
        }
    } else
    {
        if (error) *error = "Login result contains no verified credential";
        return false;
    }
    if (result.recovery_verified)
    {
        const auto found = std::find(account->recovery_credentials.begin(), account->recovery_credentials.end(), result.recovery_credential);
        if (found == account->recovery_credentials.end() || found->expires_at <= UtcNowSeconds())
        { if (error) *error = "Recovery credential changed during login"; return false; }
    }

    result.email = account->email;
    result.email_verification_required = !account->email_verified;
    return true;
}

bool CAccountDataStore::GetIdentityState(const std::string& name, std::string& id, std::vector<SLinkedIdentity>& links)
{
    const auto* account = FindAccountConst(name);
    if (!account || name.starts_with("@sandbox:")) return false;
    id = account->account_id; links = account->linked_identities; return !id.empty();
}
bool CAccountDataStore::IdentityProofCurrent(const std::string& name, const std::string& id, const std::string& password_hash)
{
    const auto* account = FindAccountConst(name);
    return account && !name.starts_with("@sandbox:") && account->account_id == id && !id.empty() &&
        !password_hash.empty() && account->password == password_hash;
}
bool CAccountDataStore::LinkIdentity(const std::string& name, const std::string& id, const std::string& password_hash,
                                    const SLinkedIdentity& identity, std::string* error)
{
    if (g_economy_operation_active || !IdentityProofCurrent(name, id, password_hash)) { if (error) *error = "account_changed"; return false; }
    auto* account = FindAccount(name); const auto previous = account->linked_identities;
    const auto existing = std::find_if(previous.begin(), previous.end(), [&](const auto& item) { return item.provider == identity.provider; });
    if (existing != previous.end()) {
        if (existing->application == identity.application && existing->subject == identity.subject) return true;
        if (error) *error = "provider_already_linked"; return false;
    }
    account->linked_identities.push_back(identity); bool migrated = false;
    if (!ValidateAccountIdentities(g_accounts, migrated, error) || !Save(error)) { account->linked_identities = previous; return false; }
    return true;
}
bool CAccountDataStore::UnlinkIdentity(const std::string& name, const std::string& id, const std::string& password_hash,
                                      const std::string& provider, std::string* error)
{
    if (g_economy_operation_active || !IdentityProofCurrent(name, id, password_hash)) { if (error) *error = "account_changed"; return false; }
    auto* account = FindAccount(name);
    if (provider == "email") {
        if (game_config::IsEmailBindingRequired()) { if (error) *error = "email_required"; return false; }
        const auto email = account->email; const bool verified = account->email_verified;
        account->email.clear(); account->email_verified = false;
        if (Save(error)) return true; account->email = email; account->email_verified = verified; return false;
    }
    const auto previous = account->linked_identities;
    std::erase_if(account->linked_identities, [&](const auto& item) { return item.provider == provider; });
    if (Save(error)) return true; account->linked_identities = previous; return false;
}

bool CAccountDataStore::CommitRecoveryCredential(const SAccountAuthResult& result, std::string* error)
{
    if (error) error->clear();
    if (result.device_id.empty()) return true;
    auto* account = FindAccount(result.account_name);
    const auto& expected_password = result.register_mode ? result.generated_password_hash : result.observed_password_hash;
    if (!account || !result.success || account->password != expected_password ||
        !ValidRecoveryCredential(result.recovery_credential) || result.recovery_credential.device_id != result.device_id ||
        result.recovery_credential.expires_at <= UtcNowSeconds())
    { if (error) *error = "Recovery credential is no longer valid"; return false; }
    if (result.recovery_verified)
        return std::find(account->recovery_credentials.begin(), account->recovery_credentials.end(), result.recovery_credential) !=
            account->recovery_credentials.end();
    if (!result.register_mode && !result.password_verified)
    { if (error) *error = "Recovery credential requires verified authentication"; return false; }
    auto previous = account->recovery_credentials;
    std::erase_if(account->recovery_credentials, [&](const auto& credential) {
        return credential.device_id == result.device_id || credential.expires_at <= UtcNowSeconds();
    });
    if (account->recovery_credentials.size() >= max_recovery_credentials) account->recovery_credentials.erase(account->recovery_credentials.begin());
    account->recovery_credentials.push_back(result.recovery_credential);
    std::sort(account->recovery_credentials.begin(), account->recovery_credentials.end(), [](const auto& a, const auto& b) {
        return a.expires_at != b.expires_at ? a.expires_at < b.expires_at : a.device_id < b.device_id;
    });
    if (Save(error)) return true;
    account->recovery_credentials = std::move(previous);
    return false;
}

bool CAccountDataStore::RevokeRecoveryCredential(const std::string& name, const std::string& device_id, std::string* error)
{
    auto* account = FindAccount(name);
    if (!account || !IsLowerHex(device_id, 32)) return false;
    auto previous = account->recovery_credentials;
    std::erase_if(account->recovery_credentials, [&](const auto& credential) { return credential.device_id == device_id; });
    if (previous == account->recovery_credentials || Save(error)) return true;
    account->recovery_credentials = std::move(previous);
    return false;
}

bool CAccountDataStore::CommitTrustedIp(const std::string& name, const std::string& client_ip, std::string* error)
{
    if (error) error->clear();
    if (client_ip.empty() || !IsValidClientIp(client_ip))
    {
        if (error) *error = "Invalid trusted IP";
        return false;
    }

    SPlayerAccount* account = FindAccount(name);
    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }
    if (account->trusted_ip == client_ip) return true;

    const std::string previous_trusted_ip = account->trusted_ip;
    account->trusted_ip = client_ip;
    if (Save(error)) return true;

    account->trusted_ip = previous_trusted_ip;
    return false;
}

bool CAccountDataStore::CommitRegistration(const std::string& name, const std::string& password_hash,
                                            std::string* error)
{
    (void)name;
    (void)password_hash;
    if (error) *error = "A valid email address is required";
    return false;
}

bool CAccountDataStore::LoginOrRegister(const std::string& name, const std::string& password,
                                         const std::string& email, const std::string& client_ip, bool register_mode,
                                         SAccountAuthResult* result_out, std::string* error)
{
    SAccountAuthWork work;
    if (!PrepareAuthentication(name, password, email, client_ip, register_mode, work, error)) return false;

    SAccountAuthResult result = ExecuteAuthentication(std::move(work));
    if (!result.success)
    {
        if (error) *error = result.error;
        if (result_out) *result_out = std::move(result);
        return false;
    }

    const bool committed = register_mode ? CommitRegistration(result, error) : CommitLogin(result, error);
    if (result_out) *result_out = result;
    if (!committed) return false;
    if (result.email_verification_required)
    {
        if (error) *error = "Email verification required";
        return false;
    }
    if (error) error->clear();
    return true;
}

bool CAccountDataStore::LoginOrRegister(const std::string& name, const std::string& password, bool register_mode,
                                         std::string* error)
{
    return LoginOrRegister(name, password, {}, {}, register_mode, nullptr, error);
}

bool CAccountDataStore::GetEmailState(const std::string& name, SAccountEmailState& state, std::string* error)
{
    if (error) error->clear();
    state = {};
    const SPlayerAccount* account = FindAccountConst(name);
    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }
    state.email = account->email;
    state.verified = account->email_verified;
    return true;
}

bool CAccountDataStore::ValidateEmailForBinding(const std::string& name, const std::string& email, std::string* error)
{
    if (error) error->clear();
    const SPlayerAccount* account = FindAccountConst(name);
    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }

    std::string normalized_email;
    if (!NormalizeEmailAddress(email, normalized_email))
    {
        if (error) *error = "A valid email address is required";
        return false;
    }
    if (FindAccountByEmail(normalized_email, &name))
    {
        if (error) *error = "Email address is already in use";
        return false;
    }
    if (account->email_verified)
    {
        if (error) *error = "Account email is already verified";
        return false;
    }
    return true;
}

bool CAccountDataStore::ConfirmEmail(const std::string& name, const std::string& email, std::string* error)
{
    if (error) error->clear();
    SPlayerAccount* account = FindAccount(name);
    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }

    std::string normalized_email;
    if (!NormalizeEmailAddress(email, normalized_email))
    {
        if (error) *error = "A valid email address is required";
        return false;
    }
    if (FindAccountByEmail(normalized_email, &name))
    {
        if (error) *error = "Email address is already in use";
        return false;
    }
    std::string current_email;
    const bool current_valid = NormalizeEmailAddress(account->email, current_email);
    if (account->email_verified)
    {
        if (current_valid && current_email == normalized_email) return true;
        if (error) *error = "Account email is already verified";
        return false;
    }

    const std::string previous_email = account->email;
    const bool previous_verified = account->email_verified;
    account->email = std::move(normalized_email);
    account->email_verified = true;
    if (Save(error)) return true;

    account->email = previous_email;
    account->email_verified = previous_verified;
    return false;
}

std::vector<SInventoryItem> CAccountDataStore::GetInventory(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? account->inventory : std::vector<SInventoryItem>{};
}

std::vector<SInventoryItem> CAccountDataStore::GetSlots(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? account->slots : std::vector<SInventoryItem>{};
}

std::vector<SInventoryItem> CAccountDataStore::GetSecondarySlots(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? account->secondary_slots : std::vector<SInventoryItem>{};
}

bool CAccountDataStore::GetProgress(const std::string& name, int& level, std::int64_t& exp)
{
    const SPlayerAccount* account = FindAccountConst(name);
    if (!account) return false;

    level = std::max(1, account->level);
    exp = std::max<std::int64_t>(0, account->exp);
    return true;
}

void CAccountDataStore::SetProgress(const std::string& name, int level, std::int64_t exp)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    account->level = std::max(1, level);
    account->exp = std::max<std::int64_t>(0, exp);
    Save();
}

int CAccountDataStore::GetTalentPoints(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? std::max(0, account->talent_points) : 0;
}

void CAccountDataStore::SetTalentPoints(const std::string& name, int talent_points)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    account->talent_points = std::max(0, talent_points);
    Save();
}

void CAccountDataStore::AddTalentPoints(const std::string& name, int talent_points)
{
    if (talent_points == 0) return;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    account->talent_points = std::max(0, account->talent_points + talent_points);
    Save();
}

std::vector<SAccountTalent> CAccountDataStore::GetTalents(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? account->talents : std::vector<SAccountTalent>{};
}

void CAccountDataStore::SetTalents(const std::string& name, const std::vector<SAccountTalent>& talents)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    account->talents = talents;
    Save();
}

std::vector<SAccountCodexEntry> CAccountDataStore::GetCodex(const std::string& name)
{
    const SPlayerAccount* account = FindAccountConst(name);
    return account ? account->codex : std::vector<SAccountCodexEntry>{};
}

void CAccountDataStore::AddCodexKill(const std::string& name, uint8_t mob_type, uint8_t rarity)
{
    if (name.empty() || mob_type == 0 || rarity == 0) return;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    for (SAccountCodexEntry& entry : account->codex)
    {
        if (entry.mob_type == mob_type && entry.rarity == rarity)
        {
            entry.count = std::min<uint32_t>(UINT32_MAX, entry.count + 1);
            Save();
            return;
        }
    }
    account->codex.push_back({ mob_type, rarity, 1 });
    Save();
}

void CAccountDataStore::SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    if (account->slots.size() <= slot_index) account->slots.resize(static_cast<size_t>(slot_index) + 1);
    account->slots[slot_index] = { petal_type, rarity, 1 };
    TouchInventory(*account);
    Save();
}

void CAccountDataStore::ClearSlot(const std::string& name, uint8_t slot_index)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account || slot_index >= account->slots.size()) return;

    account->slots[slot_index] = {};
    TouchInventory(*account);
    Save();
}

bool CAccountDataStore::SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type,
                                         uint8_t rarity)
{
    if (petal_type == 0 || rarity == 0) return ClearSecondarySlot(name, slot_index);

    SPlayerAccount* account = FindAccount(name);
    if (!account) return false;

    auto inventory_it = std::find_if(
        account->inventory.begin(), account->inventory.end(), [petal_type, rarity](const SInventoryItem& item) {
            return item.petal_type == petal_type && item.rarity == rarity && item.count > 0;
        });
    if (inventory_it == account->inventory.end()) return false;

    if (account->secondary_slots.size() <= slot_index)
        account->secondary_slots.resize(static_cast<size_t>(slot_index) + 1);
    SInventoryItem old_item = account->secondary_slots[slot_index];

    inventory_it->count -= 1;
    if (inventory_it->count == 0) account->inventory.erase(inventory_it);
    if (old_item.petal_type != 0 && old_item.rarity != 0) AddItem(name, old_item.petal_type, old_item.rarity, 1);

    account = FindAccount(name);
    if (!account) return false;
    if (account->secondary_slots.size() <= slot_index)
        account->secondary_slots.resize(static_cast<size_t>(slot_index) + 1);
    account->secondary_slots[slot_index] = { petal_type, rarity, 1 };
    TouchInventory(*account);
    Save();
    return true;
}

bool CAccountDataStore::ClearSecondarySlot(const std::string& name, uint8_t slot_index)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account || slot_index >= account->secondary_slots.size()) return false;

    SInventoryItem old_item = account->secondary_slots[slot_index];
    account->secondary_slots[slot_index] = {};
    TouchInventory(*account);
    if (old_item.petal_type != 0 && old_item.rarity != 0) AddItem(name, old_item.petal_type, old_item.rarity, 1);
    Save();
    return true;
}

bool CAccountDataStore::TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count)
{
    if (count == 0) return true;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return false;

    auto it = std::find_if(account->inventory.begin(), account->inventory.end(),
                           [petal_type, rarity](const SInventoryItem& item) {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    if (it == account->inventory.end() || it->count < count) return false;

    it->count -= count;
    if (it->count == 0) account->inventory.erase(it);
    TouchInventory(*account);
    Save();
    return true;
}

void CAccountDataStore::AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count)
{
    if (count == 0 || petal_type == 0 || rarity == 0) return;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    auto it = std::find_if(account->inventory.begin(), account->inventory.end(),
                           [petal_type, rarity](const SInventoryItem& item) {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    if (it == account->inventory.end())
    {
        account->inventory.push_back({ petal_type, rarity, std::min(count, max_inventory_item_count) });
    } else
    {
        uint64_t total = static_cast<uint64_t>(it->count) + count;
        it->count = static_cast<uint32_t>(std::min<uint64_t>(total, max_inventory_item_count));
    }
    TouchInventory(*account);
    Save();
}

bool CAccountDataStore::HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count)
{
    const SPlayerAccount* account = FindAccountConst(name);
    if (!account) return false;

    auto it = std::find_if(account->inventory.begin(), account->inventory.end(),
                           [petal_type, rarity](const SInventoryItem& item) {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    return it != account->inventory.end() && it->count >= count;
}

bool CAccountDataStore::InitializeUniqueOwnership(const std::filesystem::path& legacy_path, std::string* error)
{
    if (g_economy_operation_active) { if (error) *error = "Cannot migrate ownership during an economy operation"; return false; }
    if (error) error->clear();
    if (g_unique_ledger_initialized)
    {
        if (g_unique_migration_pending && !Flush(error)) return false;
        g_unique_migration_pending = false;
        return true;
    }
    auto migrated = g_accounts;
    auto fail = [&](std::string message) { if (error) *error = std::move(message); return false; };
    for (const auto& account : migrated)
        if (!account.unique_petals.empty()) return fail("Unversioned Unique ledger; migration requires an explicit source");

    std::error_code filesystem_error;
    const bool exists = !legacy_path.empty() && std::filesystem::exists(legacy_path, filesystem_error);
    if (filesystem_error) return fail("Cannot inspect legacy Unique registry: " + filesystem_error.message());
    if (exists)
    {
        const auto root = CJsonValue::LoadFromFile(legacy_path, error);
        if (!root || !root->IsObject()) return fail("Invalid legacy Unique registry (original file retained)");
        const auto* version = root->Find("version");
        if (version && (!version->IsNumber() || version->AsNumber() != 1))
            return fail("Unsupported legacy Unique registry version");
        const auto* owners = root->Find("owners");
        if (owners && !owners->IsArray()) return fail("Legacy Unique owners must be an array");
        if (owners)
            for (const auto& record : owners->AsArray())
            {
                const auto* type = record.Find("type");
                const auto* cost = record.Find("cost");
                const auto* owner = record.Find("owner");
                if (!type || !type->IsNumber() || type->AsNumber() != type->AsInt(-1) ||
                    type->AsInt() <= 0 || type->AsInt() > 255 || !IsKnownPetalTypeId(type->AsInt()) ||
                    !cost || !cost->IsNumber() || cost->AsNumber() != cost->AsInt(-1) || cost->AsInt(-1) < 0 ||
                    !owner || !owner->IsString() || owner->AsString().empty() || owner->AsString().rfind("@sandbox:", 0) == 0)
                    return fail("Invalid legacy Unique ownership record");
                const auto account = std::find_if(migrated.begin(), migrated.end(),
                    [&](const auto& value) { return value.name == owner->AsString(); });
                if (account == migrated.end()) return fail("Legacy Unique owner account is missing: " + owner->AsString());
                account->unique_petals.push_back({static_cast<uint8_t>(type->AsInt()), cost->AsInt()});
            }
    }
    if (!ValidateUniqueOwnership(migrated, error)) return false;
    g_accounts = std::move(migrated);
    g_snapshot_cache.Reset(); // 迁移绕过常规脏标记，需重置缓存。
    g_unique_ledger_initialized = true;
    g_unique_migration_pending = true;
    RebuildUniqueOwners();
    // 允许进入游戏前先提交迁移；保留旧源文件，已有新版账本时不再读旁路文件。
    if (!Flush(error)) return false;
    g_unique_migration_pending = false;
    return true;
}

const std::vector<SUniquePetalOwner>& CAccountDataStore::UniqueOwners() { return g_unique_owners; }

const SUniquePetalOwner* CAccountDataStore::FindUniqueOwner(EPetalType type)
{
    const auto found = std::find_if(g_unique_owners.begin(), g_unique_owners.end(),
                                  [type](const auto& owner) { return owner.type == type; });
    return found == g_unique_owners.end() ? nullptr : &*found;
}

bool CAccountDataStore::ExchangeUniquePetal(const std::string& getter, int cost,
                                            uint8_t petal_type, SUniquePetalExchangeResult* result,
                                            std::string* error)
{
    if (result) *result = {};
    if (error) error->clear();
    if (!g_unique_ledger_initialized || g_unique_migration_pending || getter.empty() ||
        getter.rfind("@sandbox:", 0) == 0 || !IsKnownPetalTypeId(petal_type) || petal_type == 0 || cost < 0)
    {
        if (error) *error = "Invalid unique petal exchange arguments";
        return false;
    }
    const auto* previous = FindUniqueOwner(static_cast<EPetalType>(petal_type));
    const std::string old_owner = previous ? previous->owner : std::string{};
    const int old_cost = previous ? previous->cost : 0;

    SPlayerAccount* getter_account = FindAccount(getter);
    if (!getter_account)
    {
        if (error) *error = "Getter account '" + getter + "' does not exist";
        return false;
    }

    SPlayerAccount* old_owner_account = nullptr;
    if (!old_owner.empty())
    {
        old_owner_account = FindAccount(old_owner);
        if (!old_owner_account)
        {
            if (error) *error = "Old owner account '" + old_owner + "' does not exist";
            return false;
        }
    }

    const SPlayerAccount getter_snapshot = *getter_account;
    const bool shared_account = old_owner_account == getter_account;
    const SPlayerAccount old_owner_snapshot = old_owner_account ? *old_owner_account : SPlayerAccount{};
    SUniquePetalExchangeResult exchange_result;

    auto rollback = [&]() {
        *getter_account = getter_snapshot;
        if (old_owner_account && !shared_account) *old_owner_account = old_owner_snapshot;
        RebuildUniqueOwners();
    };
    auto fail = [&](std::string message) {
        rollback();
        if (error) *error = std::move(message);
        return false;
    };

    if (old_owner_account)
    {
        SAccountPetalMutation& mutation = FindOrAddMutation(exchange_result, old_owner);
        if (!TakeStoredPetals(*old_owner_account, petal_type, static_cast<uint8_t>(ERarity::Unique), 1, mutation))
            return fail("Old owner '" + old_owner + "' does not have the registered Unique " +
                        std::string(GetPetalTypeName(petal_type)));

        const uint32_t refund = static_cast<uint32_t>(std::max(0, old_cost - 1));
        if (!AddInventoryStackExact(old_owner_account->inventory, petal_type, static_cast<uint8_t>(ERarity::Super),
                                    refund))
            return fail("Super refund would overflow old owner '" + old_owner + "' inventory");
    }

    SAccountPetalMutation& getter_mutation = FindOrAddMutation(exchange_result, getter);
    if (!TakeStoredPetals(*getter_account, petal_type, static_cast<uint8_t>(ERarity::Super),
                          static_cast<uint32_t>(cost), getter_mutation))
        return fail("Getter '" + getter + "' does not have " + std::to_string(cost) + " Super " +
                    std::string(GetPetalTypeName(petal_type)));

    if (!AddInventoryStackExact(getter_account->inventory, petal_type, static_cast<uint8_t>(ERarity::Unique), 1))
        return fail("Unique petal would overflow getter '" + getter + "' inventory");

    if (old_owner_account)
        std::erase_if(old_owner_account->unique_petals, [petal_type](const auto& entry) { return entry.petal_type == petal_type; });
    getter_account->unique_petals.push_back({petal_type, cost});
    std::sort(getter_account->unique_petals.begin(), getter_account->unique_petals.end(),
              [](const auto& a, const auto& b) { return a.petal_type < b.petal_type; });
    std::string consistency_error;
    if (!ValidateUniqueOwnership(g_accounts, &consistency_error)) return fail(std::move(consistency_error));
    TouchInventory(*getter_account);
    if (old_owner_account && !shared_account) TouchInventory(*old_owner_account);
    RebuildUniqueOwners();

    std::string save_error;
    if (!Save(&save_error))
    {
        rollback();
        std::string rollback_save_error;
        Save(&rollback_save_error);
        if (error)
        {
            *error = "Failed to save unique petal exchange: " + save_error;
            if (!rollback_save_error.empty()) *error += "; failed to persist rollback: " + rollback_save_error;
        }
        return false;
    }

    if (result) *result = std::move(exchange_result);
    return true;
}

bool CAccountDataStore::CraftItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count,
                                  SCraftResult* result)
{
    if (result) *result = {};
    if (petal_type == 0 || rarity == 0 || count < 5) return false;

    uint8_t result_rarity = CalculateCraftResultRarity(rarity);
    float success_chance = CalculateCraftSuccessChance(rarity);
    if (result_rarity == 0 || success_chance <= 0.f) return false;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return false;

    auto inventory = account->inventory;
    auto source_it = std::find_if(inventory.begin(), inventory.end(),
                                  [petal_type, rarity](const SInventoryItem& item) {
                                      return item.petal_type == petal_type && item.rarity == rarity;
                                  });
    if (source_it == inventory.end() || source_it->count < count) return false;

    source_it->count -= count;
    if (source_it->count == 0) inventory.erase(source_it);
    struct SRngRollback
    {
        std::mt19937 before = GetRng();
        bool committed = false;
        ~SRngRollback() { if (!committed) GetRng() = before; }
    } rng;

    constexpr uint32_t max_craft_attempts_per_request = 10000;
    SCraftResult craft_result;
    craft_result.changed = true;
    craft_result.petal_type = petal_type;
    craft_result.rarity = rarity;
    craft_result.result_rarity = result_rarity;
    craft_result.consumed = count;

    uint32_t source_pool = count;
    std::uniform_int_distribution<int> return_dist(1, 4);

    while (source_pool >= 5 && craft_result.attempts < max_craft_attempts_per_request)
    {
        source_pool -= 5;
        craft_result.attempts += 1;

        if (CheckChance(success_chance))
        {
            if (!AddInventoryStackExact(inventory, petal_type, result_rarity, 1)) return false;
            AddInventoryStack(craft_result.items, petal_type, result_rarity, 1);
            craft_result.successes += 1;
        } else
        {
            source_pool += static_cast<uint32_t>(return_dist(GetRng()));
        }
    }

    if (source_pool > 0)
    {
        if (!AddInventoryStackExact(inventory, petal_type, rarity, source_pool)) return false;
        AddInventoryStack(craft_result.items, petal_type, rarity, source_pool);
    }

    NormalizeInventory(inventory);
    NormalizeInventory(craft_result.items);
    // 结果也在提交前分配，失败不能消耗材料或推进随机流。
    if (result) *result = craft_result;
    account->inventory = std::move(inventory);
    TouchInventory(*account);
    Save();
    rng.committed = true;
    return true;
}

bool CAccountDataStore::IsEconomyOperationActive() { return g_economy_operation_active; }

std::optional<CPreparedAccountLoadout> CAccountDataStore::PrepareLoadout(const std::string& name,
    std::span<const SInventoryItem> primary, std::span<const SInventoryItem> secondary, std::string* error)
{
    if (error) error->clear();
    const auto* account = FindAccountConst(name);
    const auto fail = [&](const char* message) -> std::optional<CPreparedAccountLoadout> {
        if (error) *error = message; return std::nullopt;
    };
    if (!account || primary.size() > 255 || secondary.size() > 255) return fail("Invalid loadout shape");
    const auto empty = [](const auto& item) { return !item.petal_type && !item.rarity && !item.count; };
    const auto valid = [](const auto& item) {
        return item.petal_type && IsKnownPetalTypeId(item.petal_type) && IsKnownRarity(static_cast<ERarity>(item.rarity));
    };
    for (const auto items : {primary, secondary})
        for (const auto& item : items)
            if (!empty(item) && (!valid(item) || item.count != 1)) return fail("Invalid loadout item");

    CPreparedAccountLoadout plan;
    plan.m_account = name; plan.m_revision = account->economy.inventory_revision;
    plan.m_primary.assign(primary.begin(), primary.end()); plan.m_secondary.assign(secondary.begin(), secondary.end());
    plan.m_changed = plan.m_primary != account->slots || plan.m_secondary != account->secondary_slots;
    if (!plan.m_changed) return plan;

    // 暂存池包含卸下装备，可暂时超出背包堆叠上限；只校验最终背包。
    std::map<std::pair<uint8_t, uint8_t>, std::uint64_t> stock;
    for (const auto* items : {&account->inventory, &account->slots, &account->secondary_slots})
        for (const auto& item : *items)
        {
            if (empty(item)) continue;
            if (!valid(item) || !item.count) return fail("Invalid stored item");
            auto& count = stock[{item.petal_type, item.rarity}];
            if (count > UINT64_MAX - item.count) return fail("Stored resource count overflow");
            count += item.count;
        }
    for (const auto items : {primary, secondary})
        for (const auto& item : items)
        {
            if (empty(item)) continue;
            auto& count = stock[{item.petal_type, item.rarity}];
            if (!count) return fail("Insufficient items for complete loadout");
            --count;
        }
    for (const auto& [item, count] : stock)
    {
        if (count > max_inventory_item_count) return fail("Returned items exceed bag capacity");
        if (count) plan.m_inventory.push_back({item.first, item.second, static_cast<uint32_t>(count)});
    }
    NormalizeInventory(plan.m_inventory);
    return plan;
}

bool CAccountDataStore::CommitLoadout(CPreparedAccountLoadout&& plan)
{
    if (!plan.m_valid) return false;
    plan.m_valid = false;
    auto* account = FindAccount(plan.m_account);
    if (!account || account->economy.inventory_revision != plan.m_revision) return false;
    if (!plan.m_changed) return true;
    account->inventory = std::move(plan.m_inventory);
    account->slots = std::move(plan.m_primary);
    account->secondary_slots = std::move(plan.m_secondary);
    TouchInventory(*account); Save(); return true;
}

bool CAccountDataStore::GetEconomyCounters(const std::string& name, std::uint64_t& revision, std::uint64_t& last_request)
{
    const auto* account = FindAccountConst(name);
    if (!account) return false;
    revision = account->economy.inventory_revision; last_request = account->economy.last_request_id;
    return true;
}

bool CAccountDataStore::FindEconomyReceipt(const std::string& name, std::uint64_t request, SEconomyReceipt& receipt)
{
    const auto* account = FindAccountConst(name);
    if (!account) return false;
    const auto found = std::find_if(account->economy.receipts.begin(), account->economy.receipts.end(),
                                  [request](const auto& value) { return value.request_id == request; });
    if (found == account->economy.receipts.end()) return false;
    receipt = *found; return true;
}

bool CAccountDataStore::EconomyReceiptCommitted(const SEconomyReceipt& receipt)
{
    return IsSaveCommitted(receipt.required_save_generation);
}

std::uint64_t CAccountDataStore::CurrentSaveFence()
{
    std::lock_guard<std::mutex> lock(g_save_mutex);
    if (g_save_batch_dirty && g_next_save_generation != UINT64_MAX) return g_next_save_generation + 1;
    return g_next_save_generation;
}

bool CAccountDataStore::IsSaveCommitted(std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(g_save_mutex);
    return !generation || g_persisted_save_generation >= generation;
}

SEconomyDispatch CAccountDataStore::ExecuteEconomyRequest(const std::string& name, const SEconomyRequestContext& request,
    EEconomyOperation operation, std::string fingerprint, const std::function<EEconomyOutcome(SCraftResult&)>& action,
    std::span<const std::string> other_accounts)
{
    SEconomyDispatch result;
    auto& receipt = result.receipt;
    receipt.request_id = request.request_id; receipt.expected_revision = request.expected_revision;
    receipt.operation = operation; receipt.fingerprint = std::move(fingerprint);
    auto reject = [&](EEconomyOutcome outcome) { receipt.outcome = outcome; return result; };
    if (request.inventory_scope != name) return reject(EEconomyOutcome::WrongScope);
    const auto* current = FindAccountConst(name);
    if (!current || !request.request_id || !action || other_accounts.size() > 16 ||
        static_cast<unsigned>(operation) < 1 || static_cast<unsigned>(operation) > 3 ||
        receipt.fingerprint.empty() || receipt.fingerprint.size() > 2048 || receipt.fingerprint.size() % 2 ||
        !std::all_of(receipt.fingerprint.begin(), receipt.fingerprint.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) return reject(EEconomyOutcome::Invalid);
    receipt.result_revision = current->economy.inventory_revision;
    if (request.request_id <= current->economy.last_request_id)
    {
        SEconomyReceipt prior;
        if (!FindEconomyReceipt(name, request.request_id, prior)) return reject(EEconomyOutcome::OutcomeExpired);
        if (prior.operation != operation || prior.expected_revision != request.expected_revision || prior.fingerprint != receipt.fingerprint)
            return reject(EEconomyOutcome::ConflictingRequest);
        result.has_receipt = true; result.receipt = std::move(prior); return result;
    }
    if (request.request_id != current->economy.last_request_id + 1) return reject(EEconomyOutcome::OutOfOrder);
    if (g_economy_operation_active || current->economy.inventory_revision == UINT64_MAX)
        return reject(EEconomyOutcome::StorageBusy);
    {
        std::lock_guard<std::mutex> lock(g_save_mutex);
        if (!g_accept_saves || !g_save_worker_running || g_next_save_generation == UINT64_MAX)
            return reject(EEconomyOutcome::StorageBusy);
        const auto pending = std::count_if(current->economy.receipts.begin(), current->economy.receipts.end(), [&](const auto& item) {
            return item.required_save_generation > g_persisted_save_generation;
        });
        if (pending >= SAccountEconomyState::pending_limit) return reject(EEconomyOutcome::StorageBusy);
        receipt.required_save_generation = name.starts_with("@sandbox:") ? 0 : g_next_save_generation + 1;
    }

    // 操作前预分配回滚及回执；回调只能修改声明的账号，不能刷新或改存储拓扑。
    // 拒绝操作时恢复余额及随机流。
    CSaveBatch save_batch;
    std::vector<std::string> accounts{name};
    for (const auto& other : other_accounts)
        if (!other.empty() && std::find(accounts.begin(), accounts.end(), other) == accounts.end()) accounts.push_back(other);
    std::vector<std::pair<size_t, SPlayerAccount>> before;
    before.reserve(accounts.size());
    for (const auto& account_name : accounts)
    {
        auto* account = FindAccount(account_name);
        if (!account) return reject(EEconomyOutcome::Invalid);
        before.emplace_back(static_cast<size_t>(account - g_accounts.data()), *account);
    }
    auto old_unique_owners = g_unique_owners;
    auto rng_before = GetRng();
    SAccountEconomyState next = current->economy;
    next.receipts.reserve(SAccountEconomyState::receipt_limit + 1);
    g_economy_accounts = std::move(accounts);
    struct SActive
    {
        SActive() { g_economy_operation_active = true; }
        ~SActive() { g_economy_operation_active = false; g_economy_accounts.clear(); }
    } active;
    bool rolled_back = false;
    const auto rollback = [&]() noexcept {
        if (rolled_back) return;
        for (auto& [index, account] : before) g_accounts[index] = std::move(account);
        g_unique_owners.swap(old_unique_owners); GetRng() = rng_before;
        rolled_back = true;
    };
    try
    {
        receipt.outcome = request.expected_revision == current->economy.inventory_revision
            ? action(receipt.craft) : EEconomyOutcome::StaleRevision;
        if (receipt.outcome != EEconomyOutcome::Completed)
        {
            rollback();
            receipt.craft.changed = false; receipt.craft.consumed = 0; receipt.craft.attempts = 0;
            receipt.craft.successes = 0; receipt.craft.items.clear();
        }
        if (static_cast<unsigned>(receipt.outcome) >= static_cast<unsigned>(EEconomyOutcome::Pending))
            throw std::logic_error("Economy action returned a nonterminal outcome");
        auto& account = g_accounts[before.front().first];
        receipt.result_revision = account.economy.inventory_revision;
        next.inventory_revision = receipt.result_revision; next.last_request_id = request.request_id;
        if (next.receipts.size() >= SAccountEconomyState::receipt_limit) next.receipts.erase(next.receipts.begin());
        next.receipts.push_back(receipt);
        account.economy = std::move(next);
        Save(); // 与余额及最终回执合并保存。
        result.has_receipt = true;
        result.newly_executed = receipt.outcome == EEconomyOutcome::Completed;
        return result;
    }
    catch (...) { rollback(); throw; }
}

SPlayerAccount* CAccountDataStore::FindAccount(const std::string& name)
{
    if (g_economy_operation_active && std::find(g_economy_accounts.begin(), g_economy_accounts.end(), name) == g_economy_accounts.end())
        throw std::logic_error("Economy operation mutated an undeclared account");
    auto it = std::find_if(g_accounts.begin(), g_accounts.end(),
                           [&name](const SPlayerAccount& account) { return account.name == name; });
    if (it == g_accounts.end()) return nullptr;
    g_snapshot_cache.MarkDirty(name);
    return &*it;
}

const SPlayerAccount* CAccountDataStore::FindAccountConst(const std::string& name)
{
    auto it = std::find_if(g_accounts.begin(), g_accounts.end(),
                           [&name](const SPlayerAccount& account) { return account.name == name; });
    return it == g_accounts.end() ? nullptr : &*it;
}

bool CAccountDataStore::SandboxCloneAccount(const std::string& sandbox_key, const std::string& source_account)
{
    const SPlayerAccount* source = FindAccountConst(source_account);
    if (!source) return false;
    return RestoreSandboxAccountInternal(sandbox_key, *source, false);
}

bool CAccountDataStore::CaptureSandboxAccount(const std::string& sandbox_key, SPlayerAccount& state)
{
    if (!sandbox_key.starts_with("@sandbox:")) return false;
    const auto* account = FindAccountConst(sandbox_key);
    if (!account) return false;
    state = *account;
    return true;
}

bool CAccountDataStore::RestoreSandboxAccount(const std::string& sandbox_key, const SPlayerAccount& source)
{
    return RestoreSandboxAccountInternal(sandbox_key, source, true);
}

bool CAccountDataStore::RestoreSandboxAccountInternal(const std::string& sandbox_key, const SPlayerAccount& source, bool restore_requests)
{
    if (g_economy_operation_active) return false;
    if (!sandbox_key.starts_with("@sandbox:") || sandbox_key.size() <= 9) return false;
    // 仅复制玩法进度，进度作用域不承担认证身份。
    SPlayerAccount sandbox;
    sandbox.name = sandbox_key;
    sandbox.level = source.level;
    sandbox.exp = source.exp;
    sandbox.talent_points = source.talent_points;
    sandbox.talents = source.talents;
    sandbox.inventory = source.inventory;
    sandbox.slots = source.slots;
    sandbox.secondary_slots = source.secondary_slots;
    sandbox.codex = source.codex;
    if (restore_requests) sandbox.economy = source.economy;
    if (auto* existing = FindAccount(sandbox_key)) *existing = std::move(sandbox);
    else g_accounts.push_back(std::move(sandbox));
    return true;
}

void CAccountDataStore::RemoveSandboxAccount(const std::string& sandbox_key)
{
    if (!sandbox_key.starts_with("@sandbox:")) return;
    if (g_economy_operation_active) throw std::logic_error("Cannot remove progress scope during an economy operation");
    std::erase_if(g_accounts, [&](const auto& account) { return account.name == sandbox_key; });
}
