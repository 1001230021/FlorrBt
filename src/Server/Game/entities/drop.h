#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/shared.h"
#include "../entity.h"
#include <cstdint>
#include <string>

inline constexpr int drop_owner_all = -1;

class CPlayer;

class CDrop : public CEntity
{
  public:
    CDrop();
    CDrop(CGameWorld* world, florrbt::Vector2f pos, PetalType type, ERarity rarity, int owner_id = drop_owner_all,
          float lifetime = game_config::default_drop_lifetime, uint16_t stack_num = 1);
    ~CDrop() = default;

    void OnTickActions(float dt) override;
    void OnTickMotion(float dt) override;
    bool CanCollide() const override { return false; }
    bool CanBePickedUpBy(const CPlayer& player) const;
    bool IsOwnedBy(const CPlayer& player) const;
    bool PickUpTo(CPlayer& player);
    // 惯性阶段继承中心速度并逐渐归位，期间不可拾取。
    void BeginInertiaDrop(florrbt::Vector2f center_start, florrbt::Vector2f center_velocity, float delay);
    bool IsInertiaActive() const { return m_inertia_active; }

    PetalType GetType() const { return m_type; }
    ERarity GetRarity() const { return m_rarity; }
    int GetOwnerId() const { return m_owner_id; }
    const std::string& GetOwnerAccount() const { return m_owner_account; }
    const std::string& GetOwnerInventoryAccount() const { return m_owner_inventory_account; }
    void RestoreOwnerAccount(std::string account) { m_owner_account = std::move(account); }
    void RestoreOwnerInventoryAccount(std::string account) { m_owner_inventory_account = std::move(account); }
    uint16_t GetStackNum() const { return m_stack_num; }
    float GetPickupDelay() const { return m_unable_picked_timer; }
    float GetMergeTimer() const { return m_merge_timer; }
    float GetLifetime() const { return m_timer; }
    void RestoreRuntime(float pickup_delay, float merge_timer, float lifetime)
    {
        m_unable_picked_timer = pickup_delay;
        m_merge_timer = merge_timer;
        m_timer = lifetime;
    }

  private:
    void MergeNearbyDrops();

    PetalType m_type = PetalType::None;
    ERarity m_rarity = ERarity::Null;
    int m_owner_id = drop_owner_all;
    std::string m_owner_account;
    std::string m_owner_inventory_account;
    uint16_t m_stack_num = 1;

    float m_unable_picked_timer = game_config::default_drop_pickup_delay;
    float m_merge_timer = 0.f;
    float m_timer = game_config::default_drop_lifetime;

    bool m_inertia_active = false;
    float m_inertia_duration = game_config::default_drop_inertia_delay;
    float m_inertia_elapsed = 0.f;
    florrbt::Vector2f m_inertia_target{};
    florrbt::Vector2f m_inertia_center_start{};
    florrbt::Vector2f m_inertia_center_velocity{};
};
