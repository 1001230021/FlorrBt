#pragma once
#include "../controller.h"
#include "../entities/flower.h"
#include "../../../Shared/game_config.h"
#include <cstdint>

class CMeleeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  protected:
    void PickRandomTargetPos(CMobBase* mob, const SMobStats& stats);
    void PickRandomTargetPosNear(CMobBase* mob, const sf::Vector2f& center, float half_range);
    bool IsRandomIdleDone(float dt);
    void SetTarget(CEntity* target);
    void ClearTarget();
    CEntity* ResolveTarget(CMobBase* mob);

    CEntity* m_p_target = nullptr;
    int m_target_id = -1;
    std::uint64_t m_target_generation = 0;
    sf::Vector2f m_target_pos = {0.f, 0.f};
    float m_change_target_count = game_config::melee_target_time;
    float m_target_los_check_timer = 0.f;
    bool m_has_random_target_pos = false;
    bool m_random_idle = false;
    float m_random_idle_timer = 0.f;
};

class CSummonedMeleeController : public CMeleeController
{
  public:
    explicit CSummonedMeleeController(CEntity* owner)
        : m_owner_id(owner ? owner->m_id : -1), m_owner_generation(owner ? owner->m_generation : 0)
    {
    }
    explicit CSummonedMeleeController(int owner_id, std::uint64_t owner_generation = 0)
        : m_owner_id(owner_id), m_owner_generation(owner_generation)
    {
    }

    void OnTick(CMobBase* mob, float dt) override;
    int GetOwnerId() const { return m_owner_id; }
    std::uint64_t GetOwnerGeneration() const { return m_owner_generation; }
    CEntity* GetOwner(CGameWorld* world) const;
    bool IsOwnedBy(const CEntity* entity) const;

  private:
    int m_owner_id = -1;
    std::uint64_t m_owner_generation = 0;
};

class CNeutralMeleeController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    void OnDamaged(CMobBase* mob, CEntity* attacker) override;
};

class CRandomWanderController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
};

class CQueenAntController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
};

class CSpiderController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  private:
    float m_web_timer = 0.f;
};

class CHornetRangedController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
};

class CBumbleBeeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  private:
    void PickTurnTimer();
    void SpawnPollen(CMobBase* mob) const;
    void SetHoneyTarget(CEntity* target);
    void ClearHoneyTarget();
    CEntity* ResolveHoneyTarget(CMobBase* mob);

    float m_heading = 0.f;
    float m_turn_timer = 0.f;
    float m_wave_timer = 0.f;
    float m_pollen_timer = 0.f;
    float m_honey_target_timer = game_config::melee_target_time;
    float m_honey_los_check_timer = 0.f;
    CEntity* m_p_honey_target = nullptr;
    int m_honey_target_id = -1;
    std::uint64_t m_honey_target_generation = 0;
    bool m_initialized = false;
};
