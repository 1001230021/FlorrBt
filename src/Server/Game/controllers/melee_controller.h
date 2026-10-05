#pragma once
#include "../../../Shared/game_config.h"
#include "../../../Shared/math_types.h"
#include "../controller.h"
#include "../entities/flower.h"
#include "pickup_controller.h"
#include <cstdint>

class CGameWorld;

class CMeleeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.melee"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  protected:
    void PickRandomTargetPos(CMobBase* mob, const SMobStats& stats);
    void PickRandomTargetPosNear(CMobBase* mob, const florrbt::Vector2f& center, float half_range);
    bool IsRandomIdleDone(float dt);
    bool ShouldSwitchRandomWanderTarget(float dt) const;
    bool ReachedOrStalledRandomTarget(CMobBase* mob, float dt);
    void ResetWanderProgress();
    void SetTarget(CEntity* target);
    void ClearTarget();
    void LoseTarget(CMobBase* mob);
    CEntity* ResolveTarget(CMobBase* mob);
    virtual CEntity* FindTargetCandidate(CMobBase* mob, float search_range, int ignored_id = -1,
                                         int ignored_owner_id = -1);
    virtual bool IsTargetUnavailable(CMobBase* mob, const CEntity* target, float search_range, float dt,
                                     float& los_check_timer, int ignored_id = -1, int ignored_owner_id = -1);
    bool ShouldRunTargetScan(CMobBase* mob);
    bool TryAcquireWanderTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1);
    bool TryAcquireHoneyTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1);
    bool TryAcquireBulbTarget(CMobBase* mob, float search_range, int ignored_id = -1, int ignored_owner_id = -1);

    CEntity* m_p_target = nullptr;
    std::uint32_t m_target_world_id = 0;
    int m_target_id = -1;
    std::uint64_t m_target_generation = 0;
    florrbt::Vector2f m_target_pos = { 0.f, 0.f };
    // Radius of the entity currently chased: move approach stops at
    // mob.Radius() + target.Radius() so melee mobs reach contact range.
    float m_target_radius = 0.f;
    float m_change_target_count = game_config::melee_target_time;
    float m_target_los_check_timer = 0.f;
    bool m_has_random_target_pos = false;
    bool m_random_idle = false;
    float m_random_idle_timer = 0.f;
    florrbt::Vector2f m_wander_progress_anchor = { 0.f, 0.f };
    float m_wander_progress_timer = 0.f;
    bool m_wander_progress_initialized = false;
    int m_target_scan_cooldown = -1;
    int m_honey_target_scan_cooldown = -1;
};

// Firefly: neutral (only honey, retaliation and lit bulbs), plus a periodic
// habitat summon skill for Eternal/Unique/Primordial.
class CFireflyController; // defined after CNeutralMeleeController below

class CLeafcutterSoldierController : public CMeleeController, public CPickupController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.leafcutter_soldier"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    CEntity* ResolveLeafPieceReference(const CMobBase* mob, std::uint32_t world_id, int entity_id,
                                       std::uint64_t generation) const;
    CEntity* ResolveLeafPieceTarget(const CMobBase* mob) const;
    CEntity* FindTargetCandidate(CMobBase* mob, float search_range, int ignored_id,
                                 int ignored_owner_id) override;
    bool IsTargetUnavailable(CMobBase* mob, const CEntity* target, float search_range, float dt,
                             float& los_check_timer, int ignored_id, int ignored_owner_id) override;
    bool IsEligibleLeafPiece(const CMobBase* mob, const CEntity* entity, bool reject_carried = true) const;
    void ClearLeafPieceTarget();
    void SetLeafPieceTarget(CEntity* leaf_piece);

    bool CanCarry(const CMobBase* mob, const CEntity* carried) const override;
    float CarryContactRadius(const CMobBase* mob, const CEntity* carried) const override;
    float CarryPickupCooldown() const override;
    void OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying) override;

    std::uint32_t m_leaf_piece_target_world_id = 0;
    int m_leaf_piece_target_id = -1;
    std::uint64_t m_leaf_piece_target_generation = 0;
};

class CSummonedMeleeController : public CMeleeController
{
  public:
    explicit CSummonedMeleeController(CEntity* owner)
        : m_owner_id(owner ? owner->Id() : -1), m_owner_generation(owner ? owner->Generation() : 0)
    {
    }
    explicit CSummonedMeleeController(int owner_id, std::uint64_t owner_generation = 0)
        : m_owner_id(owner_id), m_owner_generation(owner_generation)
    {
    }

    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.summoned_melee"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
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
    bool m_owner_lost = false;
};

class CNeutralMeleeController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    void OnDamaged(CMobBase* mob, CEntity* attacker) override;
    std::string_view SnapshotKey() const override { return "controller.neutral_melee"; }
};

// Firefly: neutral (only honey, retaliation and lit bulbs), plus a periodic
// habitat summon skill for Eternal/Unique/Primordial.
class CFireflyController : public CNeutralMeleeController
{
  public:
    explicit CFireflyController(EMobType summon_type = EMobType::None) : m_summon_type(summon_type) {}
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.firefly"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    void SetSummoningDisabled(bool disabled) { m_summoning_disabled = disabled; }

  private:
    void TrySummonSkill(CMobBase* mob);
    int CountNearbySummons(const CMobBase* mob, ERarity rarity) const;
    float m_skill_timer = 8.f;
    bool m_summoning_disabled = false;
    EMobType m_summon_type = EMobType::None;
};

class CTermiteOvermindController final : public CNeutralMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.termite_overmind"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    void ApplyBanSkill(CMobBase* mob, float duration) const;

    float m_ban_skill_timer = 0.f;
    bool m_ban_skill_initialized = false;
};

class CRandomWanderController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.random_wander"; }
};

class CQueenAntController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.queen_ant"; }
};

class CSpiderController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.spider"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    float m_web_timer = 0.f;
};

class CCrabController final : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.crab"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    enum class EState
    {
        Search,
        Strafe,
        Dash,
        Recovery,
    };

    void BeginStrafe(CMobBase* mob, const CEntity* target);
    void BeginDash(CMobBase* mob, const CEntity* target);
    float OrbitDistance(const CEntity* target) const;
    bool TryAcquireCrabTarget(CMobBase* mob, float search_range);

    EState m_state = EState::Search;
    float m_state_timer = 0.f;
    float m_dash_cooldown = 0.f;
    float m_orbit_angle = 0.f;
    float m_orbit_direction = 1.f;
    float m_orbit_distance = 0.f;
    float m_strafe_duration = 0.f;
    float m_orbit_speed_scale = 1.f;
    float m_orbit_radius_scale = 1.f;
    float m_orbit_wobble_phase = 0.f;
    float m_orbit_wobble_rate = 1.f;
    float m_dash_remaining = 0.f;
    florrbt::Vector2f m_dash_direction = { 1.f, 0.f };
    bool m_low_health_dash_used = false;
};

class CHornetRangedController : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.hornet_ranged"; }
};

class CJellyfishController final : public CHornetRangedController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.jellyfish"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    float m_lightning_timer = 0.f;
};

class CMechaFlowerRangedController final : public CMeleeController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.mecha_flower_ranged"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

  private:
    void ClearOrbitTarget();
    void SyncOrbitTarget(const CEntity* target);
    void MoveAroundTarget(CMobBase* mob, const CEntity* target, float stop_distance, float dt) const;

    bool m_orbiting = false;
    float m_orbit_direction = 0.f;
    std::uint32_t m_orbit_target_world_id = 0;
    int m_orbit_target_id = -1;
    std::uint64_t m_orbit_target_generation = 0;
};

class CSpecialHornetController : public CMeleeController, public CPickupController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.hornet_special"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

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
    bool FireSkill2Missile(CMobBase* mob, const florrbt::Vector2f& origin);
    void FinishCurrentAction(CMobBase* mob);
    static ERarity PrevHornetRarity(ERarity rarity);
    static ERarity PickSkill1SummonRarity(ERarity caster_rarity, int& et_limit, int& s_limit);
    static float HornetMissileRadius(CMobBase* mob, ERarity rarity);
    static float HornetMissileDamage(ERarity rarity);
    static float HornetMissileHealth(ERarity rarity);

    bool CanCarry(const CMobBase* mob, const CEntity* carried) const override;
    float CarryContactRadius(const CMobBase* mob, const CEntity* carried) const override;
    void OnCarryChanged(CMobBase* mob, CEntity* carried, bool now_carrying) override;
    void ResolveCarriedConstraint(CMobBase* mob) override;

    EState m_state = EState::Idle;
    float m_state_timer = 0.f;
    float m_action_timer = 0.f;
    float m_fire_timer = 0.f;
    florrbt::Vector2f m_skill2_orbit_center = { 0.f, 0.f };
    float m_skill2_orbit_radius = 0.f;
    float m_skill2_orbit_angle = 0.f;
    float m_skill2_orbit_dir = 1.f;
    float m_skill2_remaining_angle = 0.f;
    florrbt::Vector2f m_skill2_tangent_dir = { 1.f, 0.f };
    int m_cycle_phase = 0;
    int m_cycle_attack_count = 0;
    std::uint32_t m_skill3_target_world_id = 0;
    int m_skill3_target_id = -1;
    std::uint64_t m_skill3_target_generation = 0;
    bool m_skill3_captured_prev_skip_tick = false;
    bool m_skill3_has_captured_prev_skip_tick = false;
    florrbt::Vector2f m_skill3_launch_pos = { 0.f, 0.f };
    florrbt::Vector2f m_skill3_launch_direction = { 1.f, 0.f };
    bool m_skill3_missile_suppressed = false;
};

class CBumbleBeeController : public IController
{
  public:
    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.bumble_bee"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

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
    std::uint32_t m_honey_target_world_id = 0;
    int m_honey_target_id = -1;
    std::uint64_t m_honey_target_generation = 0;
    bool m_initialized = false;
};
