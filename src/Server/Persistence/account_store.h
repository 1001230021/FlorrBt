#pragma once
#include "../../Shared/inventory.h"
#include "../../Shared/rarity.h"
#include "../../Shared/talent_type.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct SAccountTalent
{
    ETalentId id = ETalentId::None;
    ERarity rarity = ERarity::Null;
    int rank = 0;
};

struct SPlayerAccount
{
    std::string name;
    std::string password;
    std::string email;
    std::string trusted_ip;
    bool email_verified = false;
    int level = 1;
    std::int64_t exp = 0;
    std::vector<SInventoryItem> inventory;
    std::vector<SInventoryItem> slots;
    std::vector<SInventoryItem> secondary_slots;
    std::string extra_json = "{}";
    int talent_points = 0;
    std::vector<SAccountTalent> talents;
};

struct SCraftResult
{
    bool changed = false;
    uint8_t petal_type = 0;
    uint8_t rarity = 0;
    uint32_t consumed = 0;
    uint32_t attempts = 0;
    uint32_t successes = 0;
    uint8_t result_rarity = 0;
    std::vector<SInventoryItem> items;
};

struct SAccountPetalMutation
{
    std::string account_name;
    std::vector<uint8_t> cleared_primary_slots;
    bool secondary_slots_changed = false;
};

struct SUniquePetalExchangeResult
{
    std::vector<SAccountPetalMutation> account_mutations;
};

// Prepared on the server thread, executed by an authentication worker without
// touching the mutable account store, then committed on the server thread.
struct SAccountAuthWork
{
    std::string account_name;
    std::string password;
    std::string email;
    std::string client_ip;
    std::string stored_password_hash;
    std::string stored_trusted_ip;
    bool register_mode = false;
    bool password_required = true;
    bool email_verified = false;
};

struct SAccountAuthResult
{
    std::string account_name;
    std::string email;
    std::string client_ip;
    std::string generated_password_hash;
    std::string observed_password_hash;
    std::string observed_trusted_ip;
    std::string error;
    bool register_mode = false;
    bool success = false;
    bool password_verified = false;
    bool used_trusted_ip = false;
    bool email_verification_required = false;
};

struct SAccountEmailState
{
    std::string email;
    bool verified = false;
};

class CAccountDataStore
{
  public:
    static bool Load(const std::filesystem::path& path, std::string* error = nullptr);
    // Save captures and queues current state; Flush and ShutDown wait for atomic file publication.
    static bool Save(std::string* error = nullptr);
    static bool Flush(std::string* error = nullptr);
    static bool ShutDown(std::string* error = nullptr);
    class CSaveBatch
    {
      public:
        CSaveBatch();
        ~CSaveBatch();
        CSaveBatch(const CSaveBatch&) = delete;
        CSaveBatch& operator=(const CSaveBatch&) = delete;
    };
    static bool PrepareAuthentication(const std::string& name, const std::string& password, const std::string& email,
                                      const std::string& client_ip, bool register_mode, SAccountAuthWork& work,
                                      std::string* error = nullptr);
    // Source-compatible login helper. Registration through this overload is rejected because no email is supplied.
    static bool PrepareAuthentication(const std::string& name, const std::string& password, bool register_mode,
                                      SAccountAuthWork& work, std::string* error = nullptr);
    static SAccountAuthResult ExecuteAuthentication(SAccountAuthWork work);
    static bool CommitRegistration(const SAccountAuthResult& result, std::string* error = nullptr);
    // Use only after the registration verification code has been accepted.
    static bool CommitVerifiedRegistration(const SAccountAuthResult& result, std::string* error = nullptr);
    static bool CommitRegistration(const std::string& name, const std::string& password_hash,
                                   const std::string& email, const std::string& trusted_ip,
                                   std::string* error = nullptr);
    // Revalidates the prepared login on the server thread and refreshes
    // email_verification_required from the current account state.
    static bool CommitLogin(SAccountAuthResult& result, std::string* error = nullptr);
    // Rotate only after the login has been accepted into the world.
    static bool CommitTrustedIp(const std::string& name, const std::string& client_ip,
                                std::string* error = nullptr);
    // Source-compatible overload; it cannot create an account without the now-required email.
    static bool CommitRegistration(const std::string& name, const std::string& password_hash,
                                   std::string* error = nullptr);
    // Synchronous compatibility helper. The network path uses the three methods above.
    static bool LoginOrRegister(const std::string& name, const std::string& password, const std::string& email,
                                const std::string& client_ip, bool register_mode, SAccountAuthResult* result = nullptr,
                                std::string* error = nullptr);
    static bool LoginOrRegister(const std::string& name, const std::string& password, bool register_mode,
                                std::string* error = nullptr);

    static bool GetEmailState(const std::string& name, SAccountEmailState& state, std::string* error = nullptr);
    // Validate without mutating account state before a code is sent.
    static bool ValidateEmailForBinding(const std::string& name, const std::string& email,
                                        std::string* error = nullptr);
    // Atomically stores and verifies the address after validating its code.
    static bool ConfirmEmail(const std::string& name, const std::string& email, std::string* error = nullptr);

    static std::vector<SInventoryItem> GetInventory(const std::string& name);
    static std::vector<SInventoryItem> GetSlots(const std::string& name);
    static std::vector<SInventoryItem> GetSecondarySlots(const std::string& name);
    static bool GetProgress(const std::string& name, int& level, std::int64_t& exp);
    static void SetProgress(const std::string& name, int level, std::int64_t exp);
    static int GetTalentPoints(const std::string& name);
    static void SetTalentPoints(const std::string& name, int talent_points);
    static void AddTalentPoints(const std::string& name, int talent_points);
    static std::vector<SAccountTalent> GetTalents(const std::string& name);
    static void SetTalents(const std::string& name, const std::vector<SAccountTalent>& talents);
    static void SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static void ClearSlot(const std::string& name, uint8_t slot_index);
    static bool SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static bool ClearSecondarySlot(const std::string& name, uint8_t slot_index);
    static bool TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static void AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static bool HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static bool ExchangeUniquePetal(const std::string& old_owner, int old_cost, const std::string& getter, int cost,
                                    uint8_t petal_type, SUniquePetalExchangeResult* result = nullptr,
                                    std::string* error = nullptr);
    static bool CraftItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count,
                          SCraftResult* result = nullptr);

  private:
    static bool CommitRegistrationInternal(const std::string& name, const std::string& password_hash,
                                           const std::string& email, const std::string& trusted_ip,
                                           bool email_verified, std::string* error);
    static SPlayerAccount* FindAccount(const std::string& name);
    static const SPlayerAccount* FindAccountConst(const std::string& name);
};
