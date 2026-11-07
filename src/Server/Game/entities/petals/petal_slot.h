#pragma once
#include "../../../../Shared/shared.h"
#include "../../../../Shared/source_effects.h"
#include <vector>

class CPetalPrototype;
class CPetal;
class CFlower;

class CPetalSlot
{
  public:
    CPetalSlot() = default;
    CPetalSlot(const CPetalSlot&) = delete;
    CPetalSlot& operator=(const CPetalSlot&) = delete;
    CPetalSlot(CPetalSlot&&) = default;
    CPetalSlot& operator=(CPetalSlot&&) = default;

    void SpawnCopy(int copy_index, CFlower* flower);
    void SetPetal(const CPetalPrototype* proto, int slot_index, ERarity rarity);
    void ClearPetal();
    void KillCopy(int copy_index);
    void StartReload(int copy_index, float duration);
    float GetLoadProgress(int copy_index) const;
    void RefreshPetalState(CFlower* flower);
    void Tick(float dt, CFlower* flower, bool state_refreshed = false);
    // 所有副本数组必须与槽位布局一致，恢复旧数据后先补齐再按下标访问。
    void NormalizeRuntimeState();
    void ApplyStatsTo(SFlowerStats& target, const CFlower* flower = nullptr) const;
    int GetBonusCopyCount(const CFlower* flower = nullptr) const;
    int GetCurrentCopyCount(const CFlower* flower = nullptr) const;

    const CPetalPrototype* m_p_proto = nullptr;
    std::vector<CPetal*> m_p_petals;
    std::vector<uint8_t> m_bonus_active;
    std::vector<float> m_reload_timers;
    std::vector<float> m_reload_durations;
    std::vector<uint8_t> m_reload_ignore_multiplier;
    std::vector<uint8_t> m_spawn_cost_paid;

    ERarity m_stored_rarity = ERarity::Null;
    int m_slot_index = -1;
    int m_start_copy_index = 0;
    EPetalType m_runtime_type = EPetalType::None;

    bool m_available = true;
    bool IsBanned() const { return m_ban.Value(); }
    bool IsPolicyBanned() const { return m_ban.Policy(); }
    void SetPolicyBan(bool banned) { m_ban.SetPolicy(banned); }
    void SetBanSource(std::uint64_t source, bool banned) { m_ban.SetSource(source, banned); }
    // 旧花瓣撤销来源后再调用。
    void TakeRestrictionsFrom(CPetalSlot& previous) noexcept { m_ban = std::move(previous.m_ban); }
  private:
    CSourcedFlag m_ban;
};
