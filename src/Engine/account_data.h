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
    std::vector<SInventoryItem> inventory;
    std::vector<SInventoryItem> slots;
    std::vector<SInventoryItem> secondary_slots;
    std::string extra_json = "{}";
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
    static void SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static void ClearSlot(const std::string& name, uint8_t slot_index);
    static bool SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static bool ClearSecondarySlot(const std::string& name, uint8_t slot_index);
    static bool TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count = 1);
    static void AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count = 1);
    static bool HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint16_t count = 1);

  private:
    static SPlayerAccount* FindAccount(const std::string& name);
    static const SPlayerAccount* FindAccountConst(const std::string& name);
};
