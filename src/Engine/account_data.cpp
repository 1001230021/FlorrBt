#include "account_data.h"
#include "json_value.h"
#include "../Shared/tools.h"
#include "../Shared/petal_type.h"
#include "../Shared/rarity.h"
#include <argon2.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace
{
std::filesystem::path g_account_path;
std::vector<SPlayerAccount> g_accounts;

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
            else
                out << ch;
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
            item.count = std::min<uint32_t>(static_cast<uint32_t>((std::max)(0, count->AsInt())),
                                            max_inventory_item_count);
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
            if (const CJsonValue* rarity = item_value.Find("rarity")) item.rarity = static_cast<uint8_t>(rarity->AsInt());
            if (const CJsonValue* count = item_value.Find("count"))
                item.count = std::min<uint32_t>(static_cast<uint32_t>((std::max)(0, count->AsInt())),
                                                max_inventory_item_count);
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
        if (const CJsonValue* rarity = talent_value.Find("rarity")) talent.rarity = static_cast<ERarity>(rarity->AsInt());
        if (const CJsonValue* rank = talent_value.Find("rank")) talent.rank = std::max(0, rank->AsInt());

        if (talent.id == ETalentId::None) continue;
        int rarity_value = static_cast<int>(talent.rarity);
        if (rarity_value <= static_cast<int>(ERarity::Null) ||
            rarity_value > static_cast<int>(ERarity::Primordial))
            continue;

        auto it = std::find_if(talents.begin(), talents.end(),
                               [&talent](const SAccountTalent& existing)
                               {
                                   return existing.id == talent.id && existing.rarity == talent.rarity &&
                                          existing.rank == talent.rank;
                               });
        if (it == talents.end()) talents.push_back(talent);
    }
    return talents;
}

std::vector<SInventoryItem> DefaultSlots()
{
    std::vector<SInventoryItem> slots(5);
    for (size_t i = 0; i < 4; ++i)
        slots[i] = {static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Common), 1};
    slots[4] = {static_cast<uint8_t>(EPetalType::Rose), static_cast<uint8_t>(ERarity::Common), 1};
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

    auto it = std::find_if(inventory.begin(), inventory.end(),
                           [petal_type, rarity](const SInventoryItem& item)
                           {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    if (it == inventory.end())
    {
        inventory.push_back({petal_type, rarity, std::min(count, max_inventory_item_count)});
        return;
    }

    uint64_t total = static_cast<uint64_t>(it->count) + count;
    it->count = static_cast<uint32_t>(std::min<uint64_t>(total, max_inventory_item_count));
}

void WriteItemsJson(std::ofstream& file, const std::vector<SInventoryItem>& items)
{
    file << "[";
    for (size_t j = 0; j < items.size(); ++j)
    {
        const SInventoryItem& item = items[j];
        if (j != 0) file << ", ";
        file << "{\"type\": " << static_cast<int>(item.petal_type) << ", \"rarity\": "
             << static_cast<int>(item.rarity) << ", \"count\": " << item.count << "}";
    }
    file << "]";
}

void WriteTalentsJson(std::ofstream& file, const std::vector<SAccountTalent>& talents)
{
    file << "[";
    bool first = true;
    for (const SAccountTalent& talent : talents)
    {
        if (talent.id == ETalentId::None ||
            static_cast<int>(talent.rarity) <= static_cast<int>(ERarity::Null))
            continue;
        if (!first) file << ", ";
        file << "{\"id\": " << static_cast<int>(talent.id) << ", \"rarity\": "
             << static_cast<int>(talent.rarity) << ", \"rank\": " << std::max(0, talent.rank) << "}";
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

        auto it = std::find_if(normalized.begin(), normalized.end(),
                               [&item](const SInventoryItem& existing)
                               {
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

    g_accounts.clear();
    std::string save_error;
    if (!CAccountDataStore::Save(&save_error))
    {
        if (warning) *warning = "Moved corrupted account data to " + backup.string() +
                                ", but failed to create fresh data: " + save_error;
        return false;
    }

    if (warning)
        *warning = "Account data parse failed (" + reason + "); moved corrupted file to " +
                   backup.string() + " and created a fresh account file.";
    return true;
}
}

bool CAccountDataStore::Load(const std::filesystem::path& path, std::string* error)
{
    g_account_path = path;
    g_accounts.clear();

    if (!std::filesystem::exists(path))
    {
        std::filesystem::create_directories(path.parent_path());
        return Save(error);
    }

    std::string parse_error;
    std::optional<CJsonValue> root_value = CJsonValue::LoadFromFile(path, &parse_error);
    if (!root_value) return BackupCorruptAccountData(path, parse_error, error);
    if (!root_value->IsObject()) return BackupCorruptAccountData(path, "Root is not an object", error);

    const CJsonValue* accounts_value = root_value->Find("accounts");
    if (!accounts_value || !accounts_value->IsArray()) return true;

    for (const CJsonValue& account_value : accounts_value->AsArray())
    {
        if (!account_value.IsObject()) continue;

        SPlayerAccount account;
        if (const CJsonValue* name = account_value.Find("name")) account.name = name->AsString();
        if (const CJsonValue* password = account_value.Find("password")) account.password = password->AsString();
        if (const CJsonValue* level = account_value.Find("level")) account.level = std::max(1, level->AsInt(1));
        if (const CJsonValue* exp = account_value.Find("exp")) account.exp = std::max(0, exp->AsInt(0));
        const CJsonValue* extra = account_value.Find("extra");
        if (extra) account.extra_json = JsonToString(*extra);
        bool loaded_talent_points = false;
        if (const CJsonValue* talent_points = account_value.Find("talent_points"))
        {
            account.talent_points = std::max(0, talent_points->AsInt());
            loaded_talent_points = true;
        }
        else if (extra && extra->IsObject())
        {
            if (const CJsonValue* talent_points = extra->Find("talent_points"))
            {
                account.talent_points = std::max(0, talent_points->AsInt());
                loaded_talent_points = true;
            }
            else if (const CJsonValue* talent_points_alt = extra->Find("tp"))
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
    return true;
}

bool CAccountDataStore::Save(std::string* error)
{
    if (g_account_path.empty())
    {
        if (error) *error = "Account data path is empty";
        return false;
    }

    std::filesystem::create_directories(g_account_path.parent_path());
    std::ofstream file(g_account_path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        if (error) *error = "Failed to write " + g_account_path.string();
        return false;
    }

    file << "{\n  \"accounts\": [\n";
    for (size_t i = 0; i < g_accounts.size(); ++i)
    {
        const SPlayerAccount& account = g_accounts[i];
        file << "    {\n";
        file << "      \"name\": \"" << EscapeJsonString(account.name) << "\",\n";
        file << "      \"password\": \"" << EscapeJsonString(account.password) << "\",\n";
        file << "      \"level\": " << std::max(1, account.level) << ",\n";
        file << "      \"exp\": " << std::max(0, account.exp) << ",\n";
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
        file << "    }" << (i + 1 == g_accounts.size() ? "\n" : ",\n");
    }
    file << "  ]\n}\n";
    return true;
}

bool CAccountDataStore::LoginOrRegister(const std::string& name, const std::string& password, bool register_mode,
                                        std::string* error)
{
    if (name.empty())
    {
        if (error) *error = "Account name is empty";
        return false;
    }
    if (password.empty())
    {
        if (error) *error = "Password is empty";
        return false;
    }

    SPlayerAccount* account = FindAccount(name);
    if (register_mode)
    {
        if (account)
        {
            if (error) *error = "Account already exists";
            return false;
        }

        uint8_t salt[16];
        if (!PlatformRandomBytes(salt, sizeof(salt)))
        {
            if (error) *error = "Failed to generate salt";
            return false;
        }

        char hash[128];
        int result = argon2id_hash_encoded(
            2,           // t_cost
            65536,       // m_cost (64MB)
            1,           // parallelism
            password.c_str(), password.length(),
            salt, sizeof(salt),
            32,          // desired hash length
            hash,
            sizeof(hash)
        );

        if (result != ARGON2_OK)
        {
            if (error) *error = "Password hashing failed";
            return false;
        }

        g_accounts.push_back({name, std::string(hash), 1, 0, {}, DefaultSlots(), {}, "{}"});
        Save(error);
        return true;
    }

    if (!account)
    {
        if (error) *error = "Account does not exist";
        return false;
    }
    if (argon2id_verify(account->password.c_str(), password.c_str(), password.length()) != ARGON2_OK)
    {
        if (error) *error = "Wrong password";
        return false;
    }
    return true;
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

bool CAccountDataStore::GetProgress(const std::string& name, int& level, int& exp)
{
    const SPlayerAccount* account = FindAccountConst(name);
    if (!account) return false;

    level = std::max(1, account->level);
    exp = std::max(0, account->exp);
    return true;
}

void CAccountDataStore::SetProgress(const std::string& name, int level, int exp)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account) return;

    account->level = std::max(1, level);
    account->exp = std::max(0, exp);
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
    account->slots[slot_index] = {petal_type, rarity, 1};
    Save();
}

void CAccountDataStore::ClearSlot(const std::string& name, uint8_t slot_index)
{
    SPlayerAccount* account = FindAccount(name);
    if (!account || slot_index >= account->slots.size()) return;

    account->slots[slot_index] = {};
    Save();
}

bool CAccountDataStore::SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity)
{
    if (petal_type == 0 || rarity == 0) return ClearSecondarySlot(name, slot_index);

    SPlayerAccount* account = FindAccount(name);
    if (!account) return false;

    auto inventory_it = std::find_if(account->inventory.begin(), account->inventory.end(),
                                     [petal_type, rarity](const SInventoryItem& item)
                                     {
                                         return item.petal_type == petal_type && item.rarity == rarity && item.count > 0;
                                     });
    if (inventory_it == account->inventory.end()) return false;

    if (account->secondary_slots.size() <= slot_index) account->secondary_slots.resize(static_cast<size_t>(slot_index) + 1);
    SInventoryItem old_item = account->secondary_slots[slot_index];

    inventory_it->count -= 1;
    if (inventory_it->count == 0) account->inventory.erase(inventory_it);
    if (old_item.petal_type != 0 && old_item.rarity != 0) AddItem(name, old_item.petal_type, old_item.rarity, 1);

    account = FindAccount(name);
    if (!account) return false;
    if (account->secondary_slots.size() <= slot_index) account->secondary_slots.resize(static_cast<size_t>(slot_index) + 1);
    account->secondary_slots[slot_index] = {petal_type, rarity, 1};
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
                           [petal_type, rarity](const SInventoryItem& item)
                           {
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
                           [petal_type, rarity](const SInventoryItem& item)
                           {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    if (it == account->inventory.end())
    {
        account->inventory.push_back({petal_type, rarity, std::min(count, max_inventory_item_count)});
    } else {
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
                           [petal_type, rarity](const SInventoryItem& item)
                           {
                               return item.petal_type == petal_type && item.rarity == rarity;
                           });
    return it != account->inventory.end() && it->count >= count;
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
                                  [petal_type, rarity](const SInventoryItem& item)
                                  {
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
        }
        else
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
