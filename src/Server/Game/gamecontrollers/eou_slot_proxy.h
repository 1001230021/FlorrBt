#pragma once
#include "../../../Shared/network_msg.h"
#include "../../../Shared/petal_type.h"
#include "../../../Shared/rarity.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// 卡槽 proxy：把 eou 世界的逻辑卡槽（技能/状态）伪装成客户端原生识别的
// 花瓣槽（SOwnerPetalSlot），与 mod 实体伪装 vanilla 类型同一思路
// （m_visual_proxy_type）：逻辑是自己的，网络层输出客户端已有的结构，
// 客户端零改动即可渲染。
//
// 控制器持有 CEouSlotProxy，把每个逻辑卡槽映射为"某花瓣图标 + 稀有度 +
// 拷贝状态"。快照服务把玩家被锁/空的槽位替换成 proxy 槽输出。
struct SEouSlotProxySlot
{
    // 客户端渲染的花瓣图标与稀有度边框（伪装视觉）。
    EPetalType visual_petal_type = EPetalType::None;
    ERarity visual_rarity = ERarity::Null;
    // eou 逻辑技能标识（控制器自行解释）。
    int skill_id = 0;
    // 逻辑可用性：false 时客户端显示 Loading（装填中）。
    bool available = true;
    // 槽拷贝数（客户端显示多个拷贝圆点）。
    int copy_count = 1;
    // 冷却进度 0..1（1 = 就绪；配合 available 显示 Alive/Loading）。
    float cooldown_progress = 1.f;
};

class CEouSlotProxy
{
  public:
    void SetSlotCount(int count) { m_slots.resize(std::max(0, count)); }
    int GetSlotCount() const { return static_cast<int>(m_slots.size()); }
    SEouSlotProxySlot& Slot(int index) { return m_slots[static_cast<size_t>(index)]; }
    const SEouSlotProxySlot& Slot(int index) const { return m_slots[static_cast<size_t>(index)]; }

    // 网络层伪装输出：把第 index 个逻辑卡槽转成客户端原生槽结构。
    SOwnerPetalSlot MakeOwnerSlot(int index) const
    {
        const SEouSlotProxySlot& slot = m_slots[static_cast<size_t>(index)];
        SOwnerPetalSlot slot_snap;
        slot_snap.petal_type = static_cast<uint8_t>(slot.visual_petal_type);
        slot_snap.rarity = static_cast<uint8_t>(slot.visual_rarity);
        const int copies = std::clamp(slot.copy_count, 0, 255);
        slot_snap.copies.reserve(static_cast<size_t>(copies));
        for (int i = 0; i < copies; ++i)
        {
            SOwnerPetalSlot::SCopy copy;
            copy.state = slot.available ? SOwnerPetalSlot::ECopyState::Alive
                                        : SOwnerPetalSlot::ECopyState::Loading;
            copy.progress = PackPercent(std::clamp(slot.cooldown_progress, 0.f, 1.f));
            slot_snap.copies.push_back(copy);
        }
        return slot_snap;
    }

  private:
    std::vector<SEouSlotProxySlot> m_slots;
};