#pragma once
#include "../controller.h"
#include "../entities/flower.h"
#include "../../../Shared/game_config.h"
#include <cstdint>

class CGameWorld;

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
    bool ShouldRunTargetScan(CMobBase* mob);
    bool TryAcquireWanderTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1);
    bool TryAcquireHoneyTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1);

    CEntity* m_p_target = nullptr;
    int m_target_id = -1;
    std::uint64_t m_target_generation = 0;
    sf::Vector2f m_target_pos = {0.f, 0.f};
    float m_change_target_count = game_config::melee_target_time;
    float m_target_los_check_timer = 0.f;
    bool m_has_random_target_pos = false;
    bool m_random_idle = false;
    float m_random_idle_timer = 0.f;
    int m_target_scan_cooldown = -1;
    int m_honey_target_scan_cooldown = -1;
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
    void SetPersistAfterOwnerDeath(bool persist) { m_persist_after_owner_death = persist; }
    bool PersistsAfterOwnerDeath() const { return m_persist_after_owner_death; }

  private:
    int m_owner_id = -1;
    std::uint64_t m_owner_generation = 0;
    bool m_persist_after_owner_death = false;
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

class CSpecialHornetController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;

  private:
    enum class EState
    {
        Idle,
        Skill1Windup,
        Skill2Windup,
        Skill2Dash,
        Skill2Pause,
        Skill3Charge,
    };

    void TickMovementAndTarget(CMobBase* mob, float dt);
    CEntity* FindHighestHornetInRange(CMobBase* mob, float range);
    int CountNearbyPlayers(CMobBase* mob, float range) const;
    int CountExistingHornets(CGameWorld* world, ERarity rarity) const;
    bool TryStartSkill1(CMobBase* mob);
    bool TryStartSkill2(CMobBase* mob);
    bool TryStartSkill3(CMobBase* mob);
    void PerformSkill1(CMobBase* mob);
    bool FireSkill2Missile(CMobBase* mob, const sf::Vector2f& origin);
    void FinishCurrentAction(CMobBase* mob);
    static ERarity PrevHornetRarity(ERarity rarity);
    static ERarity PickSkill1SummonRarity(ERarity caster_rarity, int& et_limit, int& s_limit);
    static float HornetMissileRadius(CMobBase* mob, ERarity rarity);
    static float HornetMissileDamage(ERarity rarity);
    static float HornetMissileHealth(ERarity rarity);

    EState m_state = EState::Idle;
    float m_state_timer = 0.f;
    float m_action_timer = 0.f;
    float m_fire_timer = 0.f;
    sf::Vector2f m_skill2_orbit_center = {0.f, 0.f};
    float m_skill2_orbit_radius = 0.f;
    float m_skill2_orbit_angle = 0.f;
    float m_skill2_orbit_dir = 1.f;
    float m_skill2_remaining_angle = 0.f;
    sf::Vector2f m_skill2_tangent_dir = {1.f, 0.f};
    int m_cycle_phase = 0;
    int m_cycle_attack_count = 0;
    int m_skill3_target_id = -1;
    std::uint64_t m_skill3_target_generation = 0;
    int m_skill3_captured_id = -1;
    std::uint64_t m_skill3_captured_generation = 0;
    sf::Vector2f m_skill3_launch_pos = {0.f, 0.f};
    bool m_skill3_captured_prev_skip_tick = false;
    bool m_skill3_has_captured_prev_skip_tick = false;
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
    float m_honey_los_check_timer = 0.f;
    int m_honey_target_scan_cooldown = -1;
    CEntity* m_p_honey_target = nullptr;
    int m_honey_target_id = -1;
    std::uint64_t m_honey_target_generation = 0;
    bool m_initialized = false;
};
