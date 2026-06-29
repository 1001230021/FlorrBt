#pragma once
#include <vector>
#include "../../../../Shared/shared.h"

class CPetalPrototype;
class CPetal;
class CFlower;

class CPetalSlot {
public:
    CPetalSlot() = default;
    CPetalSlot(const CPetalSlot&) = delete;
    CPetalSlot& operator=(const CPetalSlot&) = delete;
    CPetalSlot(CPetalSlot&&) = default;
    CPetalSlot& operator=(CPetalSlot&&) = default;

    const CPetalPrototype* m_pProto = nullptr;
    std::vector<CPetal*> m_pPetals;
    std::vector<float> m_ReloadTimers;

    ERarity m_StoredRarity = ERarity::Null;
    int m_SlotIndex = -1;
    int m_StartCopyIndex = 0;

    bool m_Available = true;
    bool m_Banned = false;

    void SpawnCopy(int copyIndex, CFlower* flower);
    void SetPetal(const CPetalPrototype* proto, int slotIndex, ERarity rarity);
    void ClearPetal();
    void KillCopy(int copyIndex);
    void Tick(float dt, CFlower* flower);
    void ApplyStatsTo(SFlowerStats& target) const;
    int GetCurrentCopyCount() const;
};