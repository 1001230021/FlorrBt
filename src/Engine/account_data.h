#pragma once
#include "../Shared/inventory.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SPlayerAccount
{
    std::string name;
    std::string password;
    int level = 1;
    int exp = 0;
    std::vector<SInventoryItem> inventory;
    std::vector<SInventoryItem> slots;
    std::vector<SInventoryItem> secondary_slots;
    std::string extra_json = "{}";
};

struct SCraftResult
{
    bool changed = false;
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    uint32_t consumed = 0;
    uint32_t attempts = 0;
    uint32_t successes = 0;
    std::vector<SInventoryItem> items;
};

class CAccountDataStore
{
  public:
    static bool Load(const std::filesystem::path& path, std::string* error = nullptr);
    static bool Save(std::string* error = nullptr);
    static bool LoginOrRegister(const std::string& name, const std::string& password, bool register_mode,
                                std::string* error = nullptr);

    static std::vector<SInventoryItem> GetInventory(const std::string& name);
    static std::vector<SInventoryItem> GetSlots(const std::string& name);
    static std::vector<SInventoryItem> GetSecondarySlots(const std::string& name);
    static bool GetProgress(const std::string& name, int& level, int& exp);
    static void SetProgress(const std::string& name, int level, int exp);
    static void SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static void ClearSlot(const std::string& name, uint8_t slot_index);
    static bool SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static bool ClearSecondarySlot(const std::string& name, uint8_t slot_index);
    static bool TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static void AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static bool HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static bool CraftItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count,
                          SCraftResult* result = nullptr);

  private:
    static SPlayerAccount* FindAccount(const std::string& name);
    static const SPlayerAccount* FindAccountConst(const std::string& name);
};
