#pragma once
#include "inventory.h"
#include <cstdint>
#include <string>
#include <vector>

enum class EEconomyOperation : std::uint8_t { Craft = 1, Forge = 2, Loadout = 3 };
enum class EEconomyOutcome : std::uint8_t
{
    Completed, Invalid, StaleRevision, Forbidden, InsufficientItems,
    StorageBusy, ConflictingRequest, OutcomeExpired, OutOfOrder, WrongScope, Pending,
};

struct SEconomyRequestContext
{
    std::uint64_t request_id = 0;
    std::uint64_t expected_revision = 0;
    std::string inventory_scope;
    std::string actor_scope;
};

struct SCraftResult
{
    bool changed = false;
    std::uint8_t petal_type = 0;
    std::uint8_t rarity = 0;
    std::uint32_t consumed = 0;
    std::uint32_t attempts = 0;
    std::uint32_t successes = 0;
    std::uint8_t result_rarity = 0;
    std::vector<SInventoryItem> items;
    bool operator==(const SCraftResult&) const = default;
};

struct SEconomyReceipt
{
    std::uint64_t request_id = 0;
    std::uint64_t expected_revision = 0;
    std::uint64_t result_revision = 0;
    EEconomyOperation operation = EEconomyOperation::Craft;
    EEconomyOutcome outcome = EEconomyOutcome::Invalid;
    std::string fingerprint; // 规范化载荷的小写十六进制编码。
    SCraftResult craft;
    // 仅用于运行时提交屏障；从持久化恢复的回执已经提交，不能继承另一存储实例的计数器。
    std::uint64_t required_save_generation = 0;
    bool operator==(const SEconomyReceipt&) const = default;
};

struct SAccountEconomyState
{
    static constexpr size_t receipt_limit = 32;
    static constexpr size_t pending_limit = 8;
    std::uint64_t inventory_revision = 0;
    std::uint64_t last_request_id = 0;
    std::vector<SEconomyReceipt> receipts;
    bool operator==(const SAccountEconomyState&) const = default;
};
