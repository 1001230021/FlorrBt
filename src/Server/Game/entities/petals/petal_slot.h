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
    void Tick(float dt, CFlower* flower);
    void ApplyStatsTo(SFlowerStats& target) const;
    int GetBonusCopyCount() const;
    int GetCurrentCopyCount() const;

    const CPetalPrototype* m_p_proto = nullptr;
    std::vector<CPetal*> m_p_petals;
    std::vector<uint8_t> m_bonus_active;
    std::vector<float> m_reload_timers;

    ERarity m_stored_rarity = ERarity::Null;
    int m_slot_index = -1;
    int m_start_copy_index = 0;

    bool m_available = true;
    bool m_banned = false;
};
