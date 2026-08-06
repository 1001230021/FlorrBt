#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "account_store.h"
#include "../../Engine/json_value.h"
#include "../../Shared/petal_type.h"
#include "../../Shared/rarity.h"
#include "../../Shared/tools.h"
#include <algorithm>
#include <argon2.h>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
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
int g_save_batch_depth = 0;
bool g_save_batch_dirty = false;

struct SAccountSaveSnapshot
{
    std::filesystem::path path;
    std::vector<SPlayerAccount> accounts;
    std::uint64_t generation = 0;
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

int TalentPointGainForLevel(int level)
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
        total += TalentPointGainForLevel(current_level);
    return total;
}

uint8_t CraftResultRarity(uint8_t rarity)
{
    if (rarity == static_cast<uint8_t>(ERarity::Super)) return static_cast<uint8_t>(ERarity::Eternal);
    if (rarity == static_cast<uint8_t>(ERarity::Eternal)) return static_cast<uint8_t>(ERarity::Primordial);
    if (rarity >= static_cast<uint8_t>(ERarity::Common) && rarity < static_cast<uint8_t>(ERarity::Super))
        return static_cast<uint8_t>(rarity + 1);
    return 0;
}

float CraftSuccessChance(uint8_t rarity)
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
} // namespace

CAccountDataStore::CSaveBatch::CSaveBatch() { ++g_save_batch_depth; }

CAccountDataStore::CSaveBatch::~CSaveBatch()
{
    if (g_save_batch_depth <= 0) return;
    --g_save_batch_depth;
    if (g_save_batch_depth != 0 || !g_save_batch_dirty) return;
    g_save_batch_dirty = false;
    CAccountDataStore::Save();
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
    g_save_batch_depth = 0;
    g_save_batch_dirty = false;

    bool create_fresh_file = false;
    std::uint64_t loaded_generation = 0;
    std::string load_warning;

    if (!std::filesystem::exists(path))
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
            const CJsonValue* accounts_value = root_value->Find("accounts");
            if (accounts_value && accounts_value->IsArray())
            {
                for (const CJsonValue& account_value : accounts_value->AsArray())
                {
                    if (!account_value.IsObject()) continue;

                    SPlayerAccount account;
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
                        // Accept the early development name if an account file already contains it.
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
                    account.talents = ParseTalents(account_value.Find("talents"));
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

    if (!StartSaveWorker(loaded_generation, error)) return false;
    if (create_fresh_file)
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

    return QueueAccountSave(nullptr, error);
}

bool CAccountDataStore::Flush(std::string* error)
{
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

    if (!flushed && error) *error = flush_error;
    return flushed;
}

namespace
{
void AccountSaveWorkerMain()
{
    std::unique_lock<std::mutex> lock(g_save_mutex);
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
        } else
        {
            g_last_save_error =
                "Failed to persist account generation " + std::to_string(snapshot.generation) + ": " + save_error;
        }
        g_save_condition.notify_all();
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
        snapshot.accounts = g_accounts;
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

    file << "{\n  \"generation\": \"" << snapshot.generation << "\",\n  \"accounts\": [\n";
    for (size_t i = 0; i < snapshot.accounts.size(); ++i)
    {
        const SPlayerAccount& account = snapshot.accounts[i];
        file << "    {\n";
        file << "      \"name\": \"" << EscapeJsonString(account.name) << "\",\n";
        file << "      \"password\": \"" << EscapeJsonString(account.password) << "\",\n";
        file << "      \"email\": \"" << EscapeJsonString(account.email) << "\",\n";
        file << "      \"email_verified\": " << (account.email_verified ? "true" : "false") << ",\n";
        file << "      \"trusted_ip\": \"" << EscapeJsonString(account.trusted_ip) << "\",\n";
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
        file << "      \"talent_points\": " << std::max(0, account.talent_points) << ",\n";
        file << "      \"talents\": ";
        WriteTalentsJson(file, account.talents);
        file << ",\n";
        file << "      \"extra\": " << (account.extra_json.empty() ? "{}" : account.extra_json) << "\n";
        file << "    }" << (i + 1 == snapshot.accounts.size() ? "\n" : ",\n");
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
} // namespace

bool CAccountDataStore::PrepareAuthentication(const std::string& name, const std::string& password,
                                               const std::string& email, const std::string& client_ip,
                                               bool register_mode, SAccountAuthWork& work, std::string* error)
{
    if (error) error->clear();
    work = {};
    if (name.empty())
    {
        if (error) *error = "Account name is empty";
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
        if (!NormalizeEmailAddress(email, normalized_email))
        {
            if (error) *error = "A valid email address is required";
            return false;
        }
        if (account)
        {
            if (error) *error = "Account already exists";
            return false;
        }
        if (FindAccountByEmail(normalized_email))
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

    const bool trusted_ip_match = account && !client_ip.empty() && !account->trusted_ip.empty() &&
                                  account->trusted_ip == client_ip;
    if (!register_mode && !trusted_ip_match && password.empty())
    {
        if (error) *error = "Password is required from this IP";
        return false;
    }

    work.account_name = name;
    work.password = password;
    work.email = register_mode ? std::move(normalized_email) : account->email;
    work.client_ip = client_ip;
    work.register_mode = register_mode;
    // An omitted password may use the trusted IP. If the client explicitly sends one,
    // verify it instead of silently accepting a wrong credential.
    work.password_required = register_mode || !trusted_ip_match || !password.empty();
    if (account)
    {
        work.stored_password_hash = account->password;
        work.stored_trusted_ip = account->trusted_ip;
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
    result.observed_trusted_ip = std::move(work.stored_trusted_ip);
    result.register_mode = work.register_mode;
    result.email_verification_required = work.register_mode || !work.email_verified;

    if (work.register_mode)
    {
        uint8_t salt[16];
        if (!PlatformRandomBytes(salt, sizeof(salt)))
        {
            result.error = "Failed to generate salt";
            return result;
        }

        char hash[128];
        const int hash_result = argon2id_hash_encoded(2,     // t_cost
                                                      65536, // m_cost (64MB)
                                                      1,     // parallelism
                                                      work.password.c_str(), work.password.length(), salt,
                                                      sizeof(salt),
                                                      32, // desired hash length
                                                      hash, sizeof(hash));
        if (hash_result != ARGON2_OK)
        {
            result.error = "Password hashing failed";
            return result;
        }

        result.generated_password_hash = hash;
        result.success = true;
        return result;
    }

    if (!work.password_required)
    {
        if (result.client_ip.empty() || result.observed_trusted_ip != result.client_ip)
        {
            result.error = "Trusted IP authentication is no longer valid";
            return result;
        }
        result.used_trusted_ip = true;
        result.success = true;
        return result;
    }

    if (argon2id_verify(work.stored_password_hash.c_str(), work.password.c_str(), work.password.length()) != ARGON2_OK)
    {
        result.error = "Wrong password";
        return result;
    }

    result.password_verified = true;
    result.success = true;
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
    if (error) error->clear();
    if (name.empty() || password_hash.empty())
    {
        if (error) *error = "Invalid registration result";
        return false;
    }
    std::string normalized_email;
    if (!NormalizeEmailAddress(email, normalized_email))
    {
        if (error) *error = "A valid email address is required";
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
    if (FindAccountByEmail(normalized_email))
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
    g_accounts.push_back(std::move(new_account));
    if (Save(error)) return true;

    // The account is not visible as committed unless its state was accepted by
    // the persistence pipeline. This also lets a queued same-name request retry.
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

    if (result.password_verified)
    {
        if (result.observed_password_hash.empty() || account->password != result.observed_password_hash)
        {
            if (error) *error = "Account credentials changed during login";
            return false;
        }
    } else if (result.used_trusted_ip)
    {
        if (result.client_ip.empty() || account->trusted_ip != result.client_ip ||
            result.observed_trusted_ip != result.client_ip)
        {
            if (error) *error = "Trusted IP changed during login";
            return false;
        }
    } else
    {
        if (error) *error = "Login result contains no verified credential";
        return false;
    }

    result.email = account->email;
    result.email_verification_required = !account->email_verified;
    return true;
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

void CAccountDataStore::SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    if (account->slots.size() <= slot_index) account->slots.resize(static_cast<size_t>(slot_index) + 1);
    account->slots[slot_index] = { petal_type, rarity, 1 };
    Save();
}

void CAccountDataStore::ClearSlot(const std::string& name, uint8_t slot_index)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account || slot_index >= account->slots.size()) return;

    account->slots[slot_index] = {};
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
    Save();
    return true;
}

bool CAccountDataStore::ClearSecondarySlot(const std::string& name, uint8_t slot_index)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account || slot_index >= account->secondary_slots.size()) return false;

    SInventoryItem old_item = account->secondary_slots[slot_index];
    account->secondary_slots[slot_index] = {};
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

bool CAccountDataStore::ExchangeUniquePetal(const std::string& old_owner, int old_cost, const std::string& getter,
                                            int cost, uint8_t petal_type, SUniquePetalExchangeResult* result,
                                            std::string* error)
{
    if (result) *result = {};
    if (error) error->clear();
    if (getter.empty() || petal_type == 0 || old_cost < 0 || cost < 0)
    {
        if (error) *error = "Invalid unique petal exchange arguments";
        return false;
    }

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

    uint8_t result_rarity = CraftResultRarity(rarity);
    float success_chance = CraftSuccessChance(rarity);
    if (result_rarity == 0 || success_chance <= 0.f) return false;

    SPlayerAccount* account = FindAccount(name);
    if (!account) return false;

    auto source_it = std::find_if(account->inventory.begin(), account->inventory.end(),
                                  [petal_type, rarity](const SInventoryItem& item) {
                                      return item.petal_type == petal_type && item.rarity == rarity;
                                  });
    if (source_it == account->inventory.end() || source_it->count < count) return false;

    source_it->count -= count;
    if (source_it->count == 0) account->inventory.erase(source_it);

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
            AddInventoryStack(account->inventory, petal_type, result_rarity, 1);
            AddInventoryStack(craft_result.items, petal_type, result_rarity, 1);
            craft_result.successes += 1;
        } else
        {
            source_pool += static_cast<uint32_t>(return_dist(GetRng()));
        }
    }

    if (source_pool > 0)
    {
        AddInventoryStack(account->inventory, petal_type, rarity, source_pool);
        AddInventoryStack(craft_result.items, petal_type, rarity, source_pool);
    }

    NormalizeInventory(account->inventory);
    NormalizeInventory(craft_result.items);
    Save();
    if (result) *result = craft_result;
    return true;
}

SPlayerAccount* CAccountDataStore::FindAccount(const std::string& name)
{
    auto it = std::find_if(g_accounts.begin(), g_accounts.end(),
                           [&name](const SPlayerAccount& account) { return account.name == name; });
    return it == g_accounts.end() ? nullptr : &*it;
}

const SPlayerAccount* CAccountDataStore::FindAccountConst(const std::string& name)
{
    auto it = std::find_if(g_accounts.begin(), g_accounts.end(),
                           [&name](const SPlayerAccount& account) { return account.name == name; });
    return it == g_accounts.end() ? nullptr : &*it;
}
