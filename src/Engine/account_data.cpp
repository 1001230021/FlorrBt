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
        if (const CJsonValue* count = item_value.Find("count")) item.count = static_cast<uint16_t>((std::max)(0, count->AsInt()));
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
            if (const CJsonValue* count = item_value.Find("count")) item.count = static_cast<uint16_t>((std::max)(0, count->AsInt()));
        }
        slots.push_back(item);
    }
    return slots;
}

std::vector<SInventoryItem> DefaultSlots()
{
    std::vector<SInventoryItem> slots(5);
    for (size_t i = 0; i < 4; ++i)
        slots[i] = {static_cast<uint8_t>(EPetalType::Basic), static_cast<uint8_t>(ERarity::Common), 1};
    slots[4] = {static_cast<uint8_t>(EPetalType::Rose), static_cast<uint8_t>(ERarity::Common), 1};
    return slots;
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

        uint32_t total = static_cast<uint32_t>(it->count) + item.count;
        it->count = static_cast<uint16_t>(std::min<uint32_t>(total, UINT16_MAX));
    }
    inventory = std::move(normalized);
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

    std::optional<CJsonValue> root_value = CJsonValue::LoadFromFile(path, error);
    if (!root_value || !root_value->IsObject()) return false;

    const CJsonValue* accounts_value = root_value->Find("accounts");
    if (!accounts_value || !accounts_value->IsArray()) return true;

    for (const CJsonValue& account_value : accounts_value->AsArray())
    {
        if (!account_value.IsObject()) continue;

        SPlayerAccount account;
        if (const CJsonValue* name = account_value.Find("name")) account.name = name->AsString();
        if (const CJsonValue* password = account_value.Find("password")) account.password = password->AsString();
        if (const CJsonValue* extra = account_value.Find("extra")) account.extra_json = JsonToString(*extra);
        account.inventory = ParseInventory(account_value.Find("inventory"));
        account.slots = ParseSlots(account_value.Find("slots"));
        account.secondary_slots = ParseSlots(account_value.Find("secondary_slots"));
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
        file << "      \"inventory\": ";
        WriteItemsJson(file, account.inventory);
        file << ",\n";
        file << "      \"slots\": ";
        WriteItemsJson(file, account.slots);
        file << ",\n";
        file << "      \"secondary_slots\": ";
        WriteItemsJson(file, account.secondary_slots);
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

        g_accounts.push_back({name, std::string(hash), {}, DefaultSlots(), {}, "{}"});
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

    inventory_it->count = static_cast<uint16_t>(inventory_it->count - 1);
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

bool CAccountDataStore::TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count)
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

    it->count = static_cast<uint16_t>(it->count - count);
    if (it->count == 0) account->inventory.erase(it);
    Save();
    return true;
}

void CAccountDataStore::AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count)
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
        account->inventory.push_back({petal_type, rarity, count});
    } else {
        uint32_t total = static_cast<uint32_t>(it->count) + count;
        it->count = static_cast<uint16_t>(std::min<uint32_t>(total, UINT16_MAX));
    }
    Save();
}

bool CAccountDataStore::HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count)
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
