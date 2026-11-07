#pragma once
#include "../../Shared/inventory.h"
#include "../../Shared/economy.h"
#include "../../Shared/account_identity.h"
#include "../../Shared/petal_type.h"
#include "../../Shared/rarity.h"
#include "../../Shared/talent_type.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

struct SAccountTalent
{
    ETalentId id = ETalentId::None;
    ERarity rarity = ERarity::Null;
    int rank = 0;
};

inline bool operator==(const SAccountTalent& lhs, const SAccountTalent& rhs)
{
    return lhs.id == rhs.id && lhs.rarity == rhs.rarity && lhs.rank == rhs.rank;
}

struct SAccountCodexEntry
{
    uint8_t mob_type = 0;
    uint8_t rarity = 0;
    uint32_t count = 0;
};

inline bool operator==(const SAccountCodexEntry& lhs, const SAccountCodexEntry& rhs)
{
    return lhs.mob_type == rhs.mob_type && lhs.rarity == rhs.rarity && lhs.count == rhs.count;
}

struct SAccountUniquePetal
{
    uint8_t petal_type = 0;
    int cost = 0;
    bool operator==(const SAccountUniquePetal&) const = default;
};

struct SUniquePetalOwner
{
    EPetalType type = EPetalType::None;
    int cost = 0;
    std::string owner;
};

struct SAccountRecoveryCredential
{
    std::string device_id;
    std::string verifier;
    std::int64_t expires_at = 0;
    bool operator==(const SAccountRecoveryCredential&) const = default;
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
    std::vector<SAccountCodexEntry> codex;
    // 唯一物品归属账本与实物在同一持久化事务内提交。
    std::vector<SAccountUniquePetal> unique_petals;
    std::vector<SAccountRecoveryCredential> recovery_credentials;
    SAccountEconomyState economy;
    std::string account_id;
    std::vector<SLinkedIdentity> linked_identities;
};

using SAccountSnapshot = std::vector<std::shared_ptr<const SPlayerAccount>>;

// 全量比较供数据库差异保存及迁移往返校验共用。
inline bool operator==(const SPlayerAccount& lhs, const SPlayerAccount& rhs)
{
    return lhs.name == rhs.name && lhs.password == rhs.password && lhs.email == rhs.email &&
           lhs.trusted_ip == rhs.trusted_ip && lhs.email_verified == rhs.email_verified && lhs.level == rhs.level &&
           lhs.exp == rhs.exp && lhs.inventory == rhs.inventory && lhs.slots == rhs.slots &&
           lhs.secondary_slots == rhs.secondary_slots && lhs.extra_json == rhs.extra_json &&
           lhs.talent_points == rhs.talent_points && lhs.talents == rhs.talents && lhs.codex == rhs.codex &&
           lhs.unique_petals == rhs.unique_petals && lhs.recovery_credentials == rhs.recovery_credentials && lhs.economy == rhs.economy &&
           lhs.account_id == rhs.account_id && lhs.linked_identities == rhs.linked_identities;
}
inline bool operator!=(const SPlayerAccount& lhs, const SPlayerAccount& rhs) { return !(lhs == rhs); }

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

struct SEconomyDispatch
{
    bool has_receipt = false;
    bool newly_executed = false;
    SEconomyReceipt receipt;
};

class CPreparedAccountLoadout
{
  public:
    CPreparedAccountLoadout(CPreparedAccountLoadout&& other) noexcept { *this = std::move(other); }
    CPreparedAccountLoadout& operator=(CPreparedAccountLoadout&& other) noexcept
    {
        if (this == &other) return *this;
        m_account = std::move(other.m_account); m_revision = other.m_revision;
        m_valid = std::exchange(other.m_valid, false); m_changed = other.m_changed;
        m_inventory = std::move(other.m_inventory); m_primary = std::move(other.m_primary); m_secondary = std::move(other.m_secondary);
        return *this;
    }
    CPreparedAccountLoadout(const CPreparedAccountLoadout&) = delete;
    const std::vector<SInventoryItem>& Primary() const { return m_primary; }
    const std::vector<SInventoryItem>& Secondary() const { return m_secondary; }
    bool Changed() const { return m_changed; }
  private:
    friend class CAccountDataStore;
    CPreparedAccountLoadout() = default;
    std::string m_account;
    std::uint64_t m_revision = 0;
    bool m_valid = true, m_changed = false;
    std::vector<SInventoryItem> m_inventory, m_primary, m_secondary;
};

// 服务器线程准备值数据，认证线程计算后回服务器线程提交，不跨线程访问可变账号存储。
struct SAccountAuthWork
{
    std::string account_name;
    std::string password;
    std::string email;
    std::string client_ip;
    std::string stored_password_hash;
    std::string device_id;
    std::string recovery_token;
    SAccountRecoveryCredential recovery_credential;
    bool register_mode = false;
    bool recovery_mode = false;
    bool email_verified = false;
};

struct SAccountAuthResult
{
    std::string account_name;
    std::string email;
    std::string client_ip;
    std::string generated_password_hash;
    std::string observed_password_hash;
    std::string device_id;
    std::string recovery_token;
    SAccountRecoveryCredential recovery_credential;
    std::string error;
    bool register_mode = false;
    bool success = false;
    bool password_verified = false;
    bool recovery_verified = false;
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
    // Save 捕获并排队，Flush/ShutDown 等待原子发布完成。
    static bool Save(std::string* error = nullptr);
    static bool Flush(std::string* error = nullptr);
    static bool ShutDown(std::string* error = nullptr);
    // 迁移工具可复制全部账号及代数，或替换全部账号；替换后需 Flush 等待落盘。
    static std::vector<SPlayerAccount> SnapshotAccounts();
    static std::uint64_t SnapshotGeneration();
    static bool ImportAccounts(std::vector<SPlayerAccount> accounts, std::string* error = nullptr);
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
                                      std::string* error = nullptr, const std::string& device_id = {},
                                      const std::string& recovery_token = {});
    static bool PrepareAuthentication(const std::string& name, const std::string& password, bool register_mode,
                                      SAccountAuthWork& work, std::string* error = nullptr);
    static SAccountAuthResult ExecuteAuthentication(SAccountAuthWork work);
    static bool CommitRegistration(const SAccountAuthResult& result, std::string* error = nullptr);
    // 只在注册验证码通过后调用。
    static bool CommitVerifiedRegistration(const SAccountAuthResult& result, std::string* error = nullptr);
    static bool CommitRegistration(const std::string& name, const std::string& password_hash,
                                   const std::string& email, const std::string& trusted_ip,
                                   std::string* error = nullptr);
    // 回服务器线程重新校验准备好的登录，并读取当前邮件验证要求。
    static bool CommitLogin(SAccountAuthResult& result, std::string* error = nullptr);
    static bool CommitRecoveryCredential(const SAccountAuthResult& result, std::string* error = nullptr);
    static bool RevokeRecoveryCredential(const std::string& name, const std::string& device_id,
                                         std::string* error = nullptr);
    static constexpr std::int64_t recovery_token_lifetime_seconds = 30 * 24 * 60 * 60;
    static constexpr size_t max_recovery_credentials = 4;
    // 登录被世界接纳后才轮换凭据。
    static bool CommitTrustedIp(const std::string& name, const std::string& client_ip,
                                std::string* error = nullptr);
    static bool CommitRegistration(const std::string& name, const std::string& password_hash,
                                   std::string* error = nullptr);
    // 同步兼容入口；网络请求使用上方分阶段接口。
    static bool LoginOrRegister(const std::string& name, const std::string& password, const std::string& email,
                                const std::string& client_ip, bool register_mode, SAccountAuthResult* result = nullptr,
                                std::string* error = nullptr);
    static bool LoginOrRegister(const std::string& name, const std::string& password, bool register_mode,
                                std::string* error = nullptr);

    static bool GetEmailState(const std::string& name, SAccountEmailState& state, std::string* error = nullptr);
    static bool GetIdentityState(const std::string& name, std::string& id, std::vector<SLinkedIdentity>& links);
    static bool IdentityProofCurrent(const std::string& name, const std::string& id, const std::string& password_hash);
    static bool LinkIdentity(const std::string& name, const std::string& id, const std::string& password_hash,
                             const SLinkedIdentity&, std::string* error = nullptr);
    static bool UnlinkIdentity(const std::string& name, const std::string& id, const std::string& password_hash,
                               const std::string& provider, std::string* error = nullptr);
    // 发验证码前只校验，不修改账号。
    static bool ValidateEmailForBinding(const std::string& name, const std::string& email,
                                        std::string* error = nullptr);
    // 验证码通过后原子保存并验证邮箱地址。
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
    static std::vector<SAccountCodexEntry> GetCodex(const std::string& name);
    static void AddCodexKill(const std::string& name, uint8_t mob_type, uint8_t rarity);
    static void SetSlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static void ClearSlot(const std::string& name, uint8_t slot_index);
    static bool SetSecondarySlot(const std::string& name, uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    static bool ClearSecondarySlot(const std::string& name, uint8_t slot_index);
    static bool TakeItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static void AddItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    static bool HasItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count = 1);
    // 旧归属账本与三个物品容器校验后只导入一次，后续以账号后端账本为准。
    static bool InitializeUniqueOwnership(const std::filesystem::path& legacy_path, std::string* error = nullptr);
    static const std::vector<SUniquePetalOwner>& UniqueOwners();
    static const SUniquePetalOwner* FindUniqueOwner(EPetalType type);
    static bool ExchangeUniquePetal(const std::string& getter, int cost,
                                    uint8_t petal_type, SUniquePetalExchangeResult* result = nullptr,
                                    std::string* error = nullptr);
    static bool CraftItem(const std::string& name, uint8_t petal_type, uint8_t rarity, uint32_t count,
                          SCraftResult* result = nullptr);
    static SEconomyDispatch ExecuteEconomyRequest(const std::string& account, const SEconomyRequestContext& request,
        EEconomyOperation operation, std::string fingerprint,
        const std::function<EEconomyOutcome(SCraftResult&)>& action, std::span<const std::string> other_accounts = {});
    static bool GetEconomyCounters(const std::string& account, std::uint64_t& revision, std::uint64_t& last_request);
    static bool FindEconomyReceipt(const std::string& account, std::uint64_t request, SEconomyReceipt& receipt);
    static bool EconomyReceiptCommitted(const SEconomyReceipt& receipt);
    static std::uint64_t CurrentSaveFence();
    static bool IsSaveCommitted(std::uint64_t generation);
    static bool IsEconomyOperationActive();
    static std::optional<CPreparedAccountLoadout> PrepareLoadout(const std::string& account,
        std::span<const SInventoryItem> primary, std::span<const SInventoryItem> secondary, std::string* error = nullptr);
    static bool CommitLoadout(CPreparedAccountLoadout&& prepared);
    // 沙盒复制装备和背包到独立临时键；保存时过滤临时键，变化不回写正式账号。
    static bool SandboxCloneAccount(const std::string& sandbox_key, const std::string& source_account);
    static bool CaptureSandboxAccount(const std::string& sandbox_key, SPlayerAccount& state);
    static bool RestoreSandboxAccount(const std::string& sandbox_key, const SPlayerAccount& state);
    static void RemoveSandboxAccount(const std::string& sandbox_key);

  private:
    static bool RestoreSandboxAccountInternal(const std::string& sandbox_key, const SPlayerAccount& state, bool restore_requests);
    static bool CommitRegistrationInternal(const std::string& name, const std::string& password_hash,
                                           const std::string& email, const std::string& trusted_ip,
                                           bool email_verified, std::string* error);
    static SPlayerAccount* FindAccount(const std::string& name);
    static const SPlayerAccount* FindAccountConst(const std::string& name);
};
