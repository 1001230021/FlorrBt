#pragma once
#include "../../../../Shared/shared.h"
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
    void RefreshPetalState(CFlower* flower);
    void Tick(float dt, CFlower* flower, bool state_refreshed = false);
    void ApplyStatsTo(SFlowerStats& target, const CFlower* flower = nullptr) const;
    int GetBonusCopyCount(const CFlower* flower = nullptr) const;
    int GetCurrentCopyCount(const CFlower* flower = nullptr) const;

    const CPetalPrototype* m_p_proto = nullptr;
    std::vector<CPetal*> m_p_petals;
    std::vector<uint8_t> m_bonus_active;
    std::vector<float> m_reload_timers;
    std::vector<uint8_t> m_reload_ignore_multiplier;

    ERarity m_stored_rarity = ERarity::Null;
    int m_slot_index = -1;
    int m_start_copy_index = 0;
    EPetalType m_runtime_type = EPetalType::None;

    bool m_available = true;
    bool m_banned = false;
};
