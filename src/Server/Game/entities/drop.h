#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/shared.h"
#include "../entity.h"
#include <cstdint>
#include <string>

inline constexpr int drop_owner_all = -1;

class CDrop : public CEntity
{
  public:
    CDrop();
    CDrop(CGameWorld* world, sf::Vector2f pos, PetalType type, ERarity rarity, int owner_id = drop_owner_all,
          float lifetime = game_config::default_drop_lifetime, uint16_t stack_num = 1);
    ~CDrop() = default;

    void Tick(float dt) override;
    void OnCollision(CEntity* other) override {}
    bool CanCollide() const override { return false; }
    bool CanBePickedUpBy(uint32_t player_id) const;
    bool PickUpTo(const std::string& account_name, uint32_t player_id);

    PetalType GetType() const { return m_type; }
    ERarity GetRarity() const { return m_rarity; }
    int GetOwnerId() const { return m_owner_id; }
    uint16_t GetStackNum() const { return m_stack_num; }

  private:
    void MergeNearbyDrops();

    PetalType m_type = PetalType::None;
    ERarity m_rarity = ERarity::Null;
    int m_owner_id = drop_owner_all;
    uint16_t m_stack_num = 1;

    float m_unable_picked_timer = game_config::default_drop_pickup_delay;
    float m_timer = game_config::default_drop_lifetime;
};
