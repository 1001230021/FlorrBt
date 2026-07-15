#include "../controllers/melee_controller.h"
#include "../controllers/player_controller.h"
#include "../gameworld.h"
#include "../states/states.h"
#include "flower.h"
#include "mob.h"
#include "projectile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

namespace
{
using CBasicMob = CMob<SMobStats>;
using CAttackableBasicMob = CAttackableMob<SMobStats>;

std::once_flag g_beetle_registered;
std::once_flag g_bee_registered;
std::once_flag g_bumblebee_registered;
std::once_flag g_hornet_registered;
std::once_flag g_bandage_beetle_registered;
std::once_flag g_normal_ladybug_registered;
std::once_flag g_normal_flower_registered;
std::once_flag g_playerflower_registered;
std::once_flag g_soldier_ant_registered;
std::once_flag g_soldier_fire_ant_registered;
std::once_flag g_soldier_termite_registered;
std::once_flag g_summoned_beetle_registered;
std::once_flag g_summoned_soldier_ant_registered;
std::once_flag g_rock_registered;
std::once_flag g_baby_ant_registered;
std::once_flag g_worker_ant_registered;
std::once_flag g_queen_ant_registered;
std::once_flag g_ant_hole_registered;
std::once_flag g_spider_registered;

inline float GetHealthMult(int level)
{
    if (level <= 0 || level > 10) return 0.f;
    static const std::map<int, float> s_mult_list = {
        { 1, 1.f },    { 2, 3.75f },    { 3, 13.5f },     { 4, 54.f },       { 5, 324.f },
        { 6, 3159.f }, { 7, 145800.f }, { 8, 4374000.f }, { 9, 78732000.f }, { 10, 944784000.f },
    };
    return s_mult_list.at(level);
}
SMobStats ScaleMobStats(SMobStats stats, ERarity rarity)
{
    int level = GetLevel(rarity);
    float health_scale = GetHealthMult(level);
    float damage_scale = std::pow(game_config::mob_damage_scale_base, static_cast<float>(level - 1));
    float radius_scale = game_config::MobRadiusScaleForLevel(level);
    float horizon_scale = std::pow(static_cast<float>(level), game_config::mob_horizon_scale_exp);
    float mass_scale =
        std::pow(game_config::mob_mass_scale_base, static_cast<float>(level - 1) * game_config::mob_mass_scale_exp_multiplier);

    stats.max_health *= health_scale;
    stats.damage *= damage_scale;
    stats.armor *= damage_scale;
    stats.radius *= radius_scale;
    stats.horizon *= horizon_scale;
    stats.mass *= mass_scale;
    return stats;
}

SMobStats ScaleSummonedMobStats(SMobStats stats, ERarity rarity)
{
    const float base_radius = stats.radius;
    const float base_mass = stats.mass;
    const float linear_scale = 1.f + 0.2f * static_cast<float>(GetLevel(rarity));
    stats = ScaleMobStats(stats, rarity);
    stats.radius = base_radius * linear_scale;
    stats.mass = base_mass * linear_scale;
    return stats;
}

SMobStats ScaleHornetStats(SMobStats stats, ERarity rarity)
{
    const float base_horizon = stats.horizon;
    stats = ScaleMobStats(stats, rarity);
    stats.horizon = base_horizon;
    return stats;
}

SMobStats ScaleFixedHorizonMobStats(SMobStats stats, ERarity rarity)
{
    const float base_horizon = stats.horizon;
    stats = ScaleMobStats(stats, rarity);
    stats.horizon = base_horizon;
    return stats;
}

SFlowerStats ScaleFlowerStats(SFlowerStats stats, ERarity rarity)
{
    static_cast<SMobStats&>(stats) = ScaleMobStats(stats, rarity);
    return stats;
}

float DotProduct(sf::Vector2f lhs, sf::Vector2f rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

sf::Vector2f NormalizeOrZero(sf::Vector2f value)
{
    float len = Length(value);
    if (len <= game_config::entity_collision_epsilon) return {0.f, 0.f};
    return value / len;
}

sf::Vector2f GetEntityVelocity(CEntity* entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(entity)) return mob->m_vel;
    if (auto* projectile = dynamic_cast<CProjectile*>(entity)) return projectile->m_vel;
    return {0.f, 0.f};
}

sf::Vector2f GetHornetShotDirection(CEntity* shooter, CEntity* target, float missile_speed, ERarity rarity)
{
    if (!shooter || !target) return {0.f, 0.f};

    sf::Vector2f direct = NormalizeOrZero(target->m_pos - shooter->m_pos);
    if (GetLevel(rarity) <= GetLevel(ERarity::Legendary) || missile_speed <= game_config::entity_collision_epsilon)
        return direct;

    sf::Vector2f target_vel = GetEntityVelocity(target);
    if (LengthSq(target_vel) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        return direct;

    sf::Vector2f relative_pos = target->m_pos - shooter->m_pos;
    float a = DotProduct(target_vel, target_vel) - missile_speed * missile_speed;
    float b = 2.f * DotProduct(relative_pos, target_vel);
    float c = DotProduct(relative_pos, relative_pos);
    float t = -1.f;

    if (std::abs(a) <= game_config::entity_collision_epsilon)
    {
        if (std::abs(b) > game_config::entity_collision_epsilon)
            t = -c / b;
    } else {
        float disc = b * b - 4.f * a * c;
        if (disc >= 0.f)
        {
            float root = std::sqrt(disc);
            float t1 = (-b - root) / (2.f * a);
            float t2 = (-b + root) / (2.f * a);
            if (t1 > game_config::entity_collision_epsilon) t = t1;
            if (t2 > game_config::entity_collision_epsilon && (t <= 0.f || t2 < t)) t = t2;
        }
    }

    if (t <= game_config::entity_collision_epsilon) return direct;

    sf::Vector2f lead_pos = target->m_pos + target_vel * t;
    sf::Vector2f lead_dir = NormalizeOrZero(lead_pos - shooter->m_pos);
    return LengthSq(lead_dir) > game_config::entity_collision_epsilon ? lead_dir : direct;
}

ERarity PreviousRarity(ERarity rarity)
{
    int level = std::max(GetLevel(ERarity::Common), GetLevel(rarity) - 1);
    switch (level)
    {
    case 1: return ERarity::Common;
    case 2: return ERarity::Unusual;
    case 3: return ERarity::Rare;
    case 4: return ERarity::Epic;
    case 5: return ERarity::Legendary;
    case 6: return ERarity::Mythic;
    case 7: return ERarity::Ultra;
    case 8: return ERarity::Super;
    case 9: return ERarity::Eternal;
    default: return ERarity::Primordial;
    }
}

ERarity RarityFromNaturalMobLevel(int level)
{
    level = std::clamp(level, GetLevel(ERarity::Common), GetLevel(ERarity::Primordial));
    switch (level)
    {
    case 1: return ERarity::Common;
    case 2: return ERarity::Unusual;
    case 3: return ERarity::Rare;
    case 4: return ERarity::Epic;
    case 5: return ERarity::Legendary;
    case 6: return ERarity::Mythic;
    case 7: return ERarity::Ultra;
    case 8: return ERarity::Super;
    case 9: return ERarity::Eternal;
    default: return ERarity::Primordial;
    }
}

int CountOwnedSummonedSoldierAnts(CGameWorld* world, int owner_id)
{
    if (!world || owner_id < 0) return 0;

    int count = 0;
    world->ForEachEntity([&](CEntity* entity)
    {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;
        if (mob->m_mob_type != EMobType::SoldierAnt && mob->m_mob_type != EMobType::SummonedSoldierAnt) return;
        auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
        if (controller && controller->GetOwnerId() == owner_id) ++count;
    });
    return count;
}

class CBandageBeetleMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        ApplyDamageDirect(dmg, attacker);
        if (m_is_marked_for_des && !HasState<CUndeadState>())
        {
            m_is_marked_for_des = false;
            m_health = std::max(1.f, m_health);
            AddState(std::make_unique<CUndeadState>(this, GetBandageUndeadDuration(GetRarity()), GetRarity(), -1));
        }
    }
};

class CBeeMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void MoveTowards(const sf::Vector2f& target_pos, float dt) override
    {
        const SMobStats* stats = GetFinalStats();
        if (!stats) return;

        m_wave_timer += dt;

        float speed_multiplier = GetPincerSpeedMultiplier(this) * game_config::mob_velocity_multiplier;
        float max_velocity = stats->max_velocity * speed_multiplier;
        float acceleration = stats->acceleration * speed_multiplier;

        float current_speed_sq = LengthSq(m_vel);
        float max_speed_sq = max_velocity * max_velocity;
        if (current_speed_sq > max_speed_sq && game_config::mob_slow_to_max_velocity_time > 0.f)
        {
            float current_speed = std::sqrt(current_speed_sq);
            float slow_amount = (current_speed - max_velocity) * dt / game_config::mob_slow_to_max_velocity_time;
            float next_speed = std::max(max_velocity, current_speed - slow_amount);
            m_vel *= next_speed / current_speed;
        }

        sf::Vector2f delta = target_pos - m_pos;
        float len = Length(delta);
        if (len <= m_radius)
        {
            m_vel *= game_config::mob_stop_damping;
            if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = {0.f, 0.f};
            return;
        }

        sf::Vector2f forward = delta / len;
        sf::Vector2f left(-forward.y, forward.x);
        float near_factor = std::clamp((len - m_radius) / std::max(1.f, m_radius * 6.f), 0.2f, 1.f);
        float wave = std::sin(m_wave_timer * game_config::mob_bee_wave_frequency) *
                     game_config::mob_bee_wave_strength * near_factor;
        sf::Vector2f desired_dir = forward + left * wave;
        float desired_len = Length(desired_dir);
        if (desired_len <= game_config::entity_collision_epsilon) desired_dir = forward;
        else desired_dir /= desired_len;

        m_facing_angle = std::atan2(desired_dir.y, desired_dir.x);
        m_has_facing = true;

        sf::Vector2f desired_vel = desired_dir * max_velocity;
        sf::Vector2f diff = desired_vel - m_vel;
        float diff_len = Length(diff);
        float max_accel = acceleration * dt;
        if (diff_len <= max_accel)
        {
            m_vel = desired_vel;
        } else {
            m_vel += diff / diff_len * max_accel;
        }
    }

  private:
    float m_wave_timer = 0.f;
};

class CHornetMob : public CAttackableBasicMob
{
  public:
    using CAttackableBasicMob::CAttackableBasicMob;

    void Tick(float dt) override
    {
        m_attack_timer = std::max(0.f, m_attack_timer - dt);
        m_attack_recovery_timer = std::max(0.f, m_attack_recovery_timer - dt);
        if (m_attack_windup_timer > 0.f)
        {
            m_attack_windup_timer -= dt;
            if (m_attack_windup_timer <= 0.f)
                FireQueuedAttack();
        }
        CAttackableBasicMob::Tick(dt);
        UpdateLoadedMissile(dt);
    }

    bool IsFacingLocked() const override { return m_attack_recovery_timer > 0.f; }

    bool TryAttack(CEntity* target) override
    {
        if (m_attack_timer > 0.f || m_attack_windup_timer > 0.f || !GameWorld()) return false;
        if (!GetLoadedMissile()) return false;

        m_attack_target_id = target ? target->m_id : -1;
        m_attack_windup_timer = game_config::mob_hornet_attack_windup;
        return true;
    }

  private:
    CMissile* FindLoadedMissile()
    {
        if (!GameWorld() || m_loaded_missile_id < 0) return nullptr;

        CEntity* entity = GameWorld()->GetEntity(m_loaded_missile_id, m_loaded_missile_generation);
        auto* missile = dynamic_cast<CMissile*>(entity);
        if (!missile || missile->m_is_marked_for_des || missile->m_owner_id != m_id ||
            !missile->IsAttachedToOwner())
        {
            return nullptr;
        }
        return missile;
    }

    void RefreshLoadedMissileState()
    {
        if (m_loaded_missile_id < 0) return;
        if (FindLoadedMissile()) return;

        m_loaded_missile_id = -1;
        m_loaded_missile_generation = 0;
        if (m_missile_reload_timer <= 0.f)
            m_missile_reload_timer = game_config::mob_hornet_missile_reload;
    }

    CMissile* GetLoadedMissile()
    {
        RefreshLoadedMissileState();
        return FindLoadedMissile();
    }

    sf::Vector2f LoadedMissileDirection() const
    {
        sf::Vector2f facing = {std::cos(m_facing_angle), std::sin(m_facing_angle)};
        if (LengthSq(facing) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            facing = {1.f, 0.f};
        return -facing;
    }

    sf::Vector2f LoadedMissilePosition() const
    {
        return m_pos + LoadedMissileDirection() * (m_radius * game_config::mob_hornet_missile_attach_offset);
    }

    void UpdateLoadedMissile(float dt)
    {
        if (!GameWorld() || m_is_marked_for_des || IsDead()) return;

        RefreshLoadedMissileState();
        if (m_loaded_missile_id >= 0) return;

        if (m_missile_reload_timer > 0.f)
        {
            m_missile_reload_timer = std::max(0.f, m_missile_reload_timer - dt);
            if (m_missile_reload_timer > 0.f) return;
        }

        SpawnLoadedMissile();
    }

    void SpawnLoadedMissile()
    {
        if (!GameWorld()) return;

        int level = GetLevel(GetRarity());
        float missile_damage =
            game_config::mob_hornet_missile_base_damage * std::pow(3.f, static_cast<float>(level - 1));
        float missile_health =
            game_config::mob_hornet_missile_base_health * std::pow(5.f, static_cast<float>(level - 1));

        auto missile = std::make_unique<CMissile>(GameWorld(), LoadedMissilePosition(),
                                                  game_config::mob_hornet_missile_radius,
                                                  LoadedMissileDirection(), 0.f,
                                                  missile_damage, missile_health,
                                                  game_config::default_missile_lifetime, this);
        missile->m_team = m_team;
        missile->AttachToOwner();

        CEntity* inserted = GameWorld()->InsertEntity(std::move(missile));
        if (!inserted)
        {
            m_missile_reload_timer = game_config::mob_hornet_missile_reload;
            return;
        }

        m_loaded_missile_id = inserted->m_id;
        m_loaded_missile_generation = inserted->m_generation;
        m_missile_reload_timer = 0.f;
    }

    void FireQueuedAttack()
    {
        if (!GameWorld()) return;
        CMissile* missile = GetLoadedMissile();
        if (!missile)
        {
            m_attack_target_id = -1;
            return;
        }

        CEntity* target = m_attack_target_id >= 0 ? GameWorld()->GetEntity(m_attack_target_id) : nullptr;
        sf::Vector2f rear_direction = {std::cos(m_facing_angle), std::sin(m_facing_angle)};
        if (target && !target->m_is_marked_for_des && !target->IsDead())
        {
            sf::Vector2f shot_direction =
                GetHornetShotDirection(this, target, game_config::mob_hornet_missile_speed, GetRarity());
            if (LengthSq(shot_direction) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                rear_direction = shot_direction;
        }

        if (LengthSq(rear_direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            return;

        sf::Vector2f recoil_direction = -rear_direction;
        m_facing_angle = std::atan2(recoil_direction.y, recoil_direction.x);
        m_has_facing = true;

        sf::Vector2f spawn_pos = m_pos + rear_direction * (m_radius * 0.5f);
        missile->m_pos = spawn_pos;
        missile->m_prev_pos = spawn_pos;
        missile->m_team = m_team;
        if (!missile->Fire(rear_direction, game_config::mob_hornet_missile_speed,
                           game_config::default_missile_lifetime))
        {
            m_attack_target_id = -1;
            RefreshLoadedMissileState();
            return;
        }

        m_loaded_missile_id = -1;
        m_loaded_missile_generation = 0;
        m_missile_reload_timer = game_config::mob_hornet_missile_reload;

        m_vel += recoil_direction * game_config::mob_hornet_recoil_speed;
        m_attack_timer = game_config::mob_hornet_attack_interval;
        m_attack_recovery_timer = game_config::mob_hornet_attack_recovery;
        m_attack_target_id = -1;
    }

    float m_attack_timer = 0.f;
    float m_attack_windup_timer = 0.f;
    float m_attack_recovery_timer = 0.f;
    float m_missile_reload_timer = 0.f;
    int m_attack_target_id = -1;
    int m_loaded_missile_id = -1;
    std::uint64_t m_loaded_missile_generation = 0;
};

class CQueenAntMob : public CAttackableBasicMob
{
  public:
    using CAttackableBasicMob::CAttackableBasicMob;

    void Tick(float dt) override
    {
        m_attack_timer = std::max(0.f, m_attack_timer - dt);
        m_attack_recovery_timer = std::max(0.f, m_attack_recovery_timer - dt);
        if (m_attack_windup_timer > 0.f)
        {
            m_attack_windup_timer -= dt;
            if (m_attack_windup_timer <= 0.f)
                SpawnQueuedAnt();
        }

        bool paused = m_attack_windup_timer > 0.f || m_attack_recovery_timer > 0.f;
        m_attacking = paused;
        if (paused)
        {
            m_vel = {0.f, 0.f};
            TickStates(dt);
            return;
        }

        CAttackableBasicMob::Tick(dt);
    }

    bool IsFacingLocked() const override { return m_attack_windup_timer > 0.f || m_attack_recovery_timer > 0.f; }

    bool TryAttack(CEntity* target) override
    {
        if (!target || !GameWorld()) return false;
        if (m_attack_timer > 0.f || m_attack_windup_timer > 0.f || m_attack_recovery_timer > 0.f) return false;
        if (CountOwnedSummonedSoldierAnts(GameWorld(), m_id) >= game_config::mob_queen_ant_max_summons) return false;

        sf::Vector2f delta = target->m_pos - m_pos;
        if (LengthSq(delta) > game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
        {
            m_facing_angle = std::atan2(delta.y, delta.x);
            m_has_facing = true;
        }

        m_attack_target_id = target->m_id;
        m_attack_windup_timer = game_config::mob_queen_ant_attack_windup;
        m_attacking = true;
        return true;
    }

  private:
    void SpawnQueuedAnt()
    {
        CGameWorld* world = GameWorld();
        if (!world) return;
        if (CountOwnedSummonedSoldierAnts(world, m_id) >= game_config::mob_queen_ant_max_summons)
        {
            FinishAttack();
            return;
        }

        sf::Vector2f forward = {std::cos(m_facing_angle), std::sin(m_facing_angle)};
        if (LengthSq(forward) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            forward = {1.f, 0.f};

        ERarity spawn_rarity = PreviousRarity(GetRarity());
        sf::Vector2f spawn_pos = m_pos - forward * (m_radius * 0.5f);
        auto spawned = CreateMob(EMobType::SoldierAnt, world, spawn_pos, spawn_rarity);
        auto* ant = spawned ? dynamic_cast<CMobBase*>(spawned.get()) : nullptr;
        if (ant)
        {
            ant->m_team = m_team;
            ant->SetController(std::make_unique<CSummonedMeleeController>(m_id));
            world->InsertEntity(std::move(spawned));
        }
        FinishAttack();
    }

    void FinishAttack()
    {
        m_attack_timer = game_config::mob_queen_ant_attack_interval;
        m_attack_recovery_timer = game_config::mob_queen_ant_attack_recovery;
        m_attack_target_id = -1;
    }

    float m_attack_timer = 0.f;
    float m_attack_windup_timer = 0.f;
    float m_attack_recovery_timer = 0.f;
    int m_attack_target_id = -1;
};

class CAntHoleMob : public CBasicMob
{
  public:
    CAntHoleMob(CGameWorld* pworld, sf::Vector2f pos, float r, ERarity rarity, const SMobStats& stats)
        : CBasicMob(pworld, pos, r, rarity, stats)
    {
        m_mass = std::numeric_limits<float>::max();
    }

    void Tick(float dt) override
    {
        m_vel = {0.f, 0.f};
        TickStates(dt);
        ReleaseQueuedSpawns(dt);
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        float before = m_health;
        ApplyDamageDirect(dmg, attacker);
        float dealt = std::max(0.f, before - m_health);
        if (dealt <= 0.f) return;

        if (m_is_marked_for_des)
        {
            QueueAllRemainingHealthWaves();
            ReleaseAllQueuedSpawns();
        } else {
            QueueHealthTriggeredSpawns();
        }
    }

    void OnCollision(CEntity*) override {}

  private:
    struct SQueuedSpawn
    {
        EMobType type = EMobType::None;
        ERarity rarity = ERarity::Common;
    };

    struct SHealthWave
    {
        float health_fraction = 1.f;
        int baby_ants = 0;
        int worker_ants = 0;
        int soldier_ants = 0;
        bool queen = false;
    };

    void QueueHealthTriggeredSpawns()
    {
        const float max_health = std::max(1.f, m_base_stats.max_health);
        const float health_fraction = std::clamp(m_health / max_health, 0.f, 1.f);
        const auto health_waves = HealthWaves();

        while (m_health_wave_index < health_waves.size() &&
               health_fraction <= health_waves[m_health_wave_index].health_fraction)
        {
            QueueHealthWave(health_waves[m_health_wave_index]);
            ++m_health_wave_index;
        }
    }

    void QueueAllRemainingHealthWaves()
    {
        const auto health_waves = HealthWaves();
        while (m_health_wave_index < health_waves.size())
        {
            QueueHealthWave(health_waves[m_health_wave_index]);
            ++m_health_wave_index;
        }
    }

    static constexpr std::array<SHealthWave, 4> HealthWaves()
    {
        return {{
            {0.90f, 1, 2, 5, false},
            {0.65f, 1, 2, 6, false},
            {0.40f, 1, 2, 7, false},
            {0.25f, 1, 1, 7, true},
        }};
    }

    void QueueHealthWave(const SHealthWave& wave)
    {
        QueueSpawn(EMobType::BabyAnt, wave.baby_ants);
        QueueSpawn(EMobType::WorkerAnt, wave.worker_ants);
        QueueSpawn(EMobType::SoldierAnt, wave.soldier_ants);

        if (wave.queen && !m_same_rarity_queen_queued)
        {
            m_same_rarity_queen_queued = true;
            QueueSpawn(EMobType::QueenAnt, 1, GetRarity());
        }
    }

    ERarity PickAntRarity() const
    {
        const int level = GetLevel(GetRarity());
        const bool allow_same = level <= GetLevel(ERarity::Ultra);
        const float same_weight = allow_same ? std::max(0.015f, 0.08f - 0.007f * static_cast<float>(level - 1)) : 0.f;
        const float low_one_weight = std::max(0.25f, 0.44f - 0.018f * static_cast<float>(level - 1));
        constexpr float low_two_weight = 1.f;
        const float total_weight = low_two_weight + low_one_weight + same_weight;

        float roll = GetLimitedRng(0.f, total_weight);
        if (roll <= low_two_weight) return RarityFromNaturalMobLevel(level - 2);
        roll -= low_two_weight;
        if (roll <= low_one_weight) return RarityFromNaturalMobLevel(level - 1);
        return RarityFromNaturalMobLevel(level);
    }

    void QueueSpawn(EMobType type, int count)
    {
        count = std::max(0, count);
        for (int i = 0; i < count; ++i)
            QueueSpawn(type, 1, PickAntRarity());
    }

    void QueueSpawn(EMobType type, int count, ERarity rarity)
    {
        count = std::max(0, count);
        for (int i = 0; i < count; ++i)
            m_spawn_queue.push_back({type, rarity});
    }

    void ReleaseQueuedSpawns(float dt)
    {
        if (!GameWorld() || m_spawn_index >= m_spawn_queue.size()) return;

        m_release_timer += dt;
        float interval = std::max(game_config::server_fixed_dt, game_config::mob_ant_hole_release_interval);
        while (m_release_timer >= interval && m_spawn_index < m_spawn_queue.size())
        {
            const SQueuedSpawn& queued = m_spawn_queue[m_spawn_index];
            m_release_timer -= interval;
            ReleaseSpawn(queued);
            ++m_spawn_index;
        }
    }

    void ReleaseAllQueuedSpawns()
    {
        if (!GameWorld()) return;
        while (m_spawn_index < m_spawn_queue.size())
        {
            ReleaseSpawn(m_spawn_queue[m_spawn_index]);
            ++m_spawn_index;
        }
    }

    void ReleaseSpawn(const SQueuedSpawn& queued)
    {
        if (queued.type == EMobType::None || !GameWorld()) return;

        float angle = GetLimitedRng(-game_config::pi, game_config::pi);
        sf::Vector2f dir = {std::cos(angle), std::sin(angle)};
        sf::Vector2f spawn_pos = m_pos + dir * (m_radius + 8.f);
        auto mob = CreateMob(queued.type, GameWorld(), spawn_pos, queued.rarity);
        if (!mob) return;

        mob->m_team = m_team;
        if (auto* spawned = dynamic_cast<CMobBase*>(mob.get()))
            spawned->m_vel = dir * std::max(20.f, spawned->m_radius * 2.f);
        GameWorld()->InsertEntity(std::move(mob));
    }

    std::vector<SQueuedSpawn> m_spawn_queue;
    size_t m_spawn_index = 0;
    float m_release_timer = 0.f;
    size_t m_health_wave_index = 0;
    bool m_same_rarity_queen_queued = false;
};
} // namespace

void RegisterBeetle()
{
    std::call_once(g_beetle_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Beetle;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_beetle_team;
        proto.m_base_stats.max_health = game_config::mob_beetle_max_health;
        proto.m_base_stats.armor = game_config::mob_beetle_armor;
        proto.m_base_stats.damage = game_config::mob_beetle_damage;
        proto.m_base_stats.radius = game_config::mob_beetle_radius;
        proto.m_base_stats.mass = game_config::mob_beetle_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            SMobStats stats = ScaleMobStats(base_stats, rarity);
            SMobStats soldier_stats;
            soldier_stats.mass = game_config::mob_soldier_ant_mass;
            stats.mass = ScaleMobStats(soldier_stats, rarity).mass * 10.f;
            return stats;
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::Beetle, CBasicMob, proto);
    });
}

void RegisterBandageBeetle()
{
    std::call_once(g_bandage_beetle_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::BandageBeetle;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_bandage_beetle_team;
        proto.m_base_stats.max_health = game_config::mob_bandage_beetle_max_health;
        proto.m_base_stats.armor = game_config::mob_bandage_beetle_armor;
        proto.m_base_stats.damage = game_config::mob_bandage_beetle_damage;
        proto.m_base_stats.radius = game_config::mob_bandage_beetle_radius;
        proto.m_base_stats.mass = game_config::mob_bandage_beetle_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::BandageBeetle, CBandageBeetleMob, proto);
    });
}

void RegisterBee()
{
    std::call_once(g_bee_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Bee;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_bee_team;
        proto.m_base_stats.max_health = game_config::mob_bee_max_health;
        proto.m_base_stats.armor = game_config::mob_bee_armor;
        proto.m_base_stats.damage = game_config::mob_bee_damage;
        proto.m_base_stats.radius = game_config::mob_bee_radius;
        proto.m_base_stats.mass = game_config::mob_bee_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_bee_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_bee_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CNeutralMeleeController>(); };
        REGISTER_MOB(EMobType::Bee, CBeeMob, proto);
    });
}

void RegisterHornet()
{
    std::call_once(g_hornet_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Hornet;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_hornet_team;
        proto.m_base_stats.max_health = game_config::mob_hornet_max_health;
        proto.m_base_stats.armor = game_config::mob_hornet_armor;
        proto.m_base_stats.damage = game_config::mob_hornet_damage;
        proto.m_base_stats.radius = game_config::mob_hornet_radius;
        proto.m_base_stats.mass = game_config::mob_hornet_mass;
        proto.m_base_stats.horizon = game_config::mob_hornet_missile_speed * game_config::default_missile_lifetime * 0.5f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_hornet_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_hornet_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleHornetStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CHornetRangedController>(); };
        REGISTER_MOB(EMobType::Hornet, CHornetMob, proto);
    });
}

void RegisterBumbleBee()
{
    std::call_once(g_bumblebee_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::BumbleBee;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_bumblebee_team;
        proto.m_base_stats.max_health = game_config::mob_bumblebee_max_health;
        proto.m_base_stats.armor = game_config::mob_bumblebee_armor;
        proto.m_base_stats.damage = game_config::mob_bumblebee_damage;
        proto.m_base_stats.radius = game_config::mob_bumblebee_radius;
        proto.m_base_stats.mass = game_config::mob_bumblebee_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_bumblebee_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_bumblebee_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CBumbleBeeController>(); };
        REGISTER_MOB(EMobType::BumbleBee, CBeeMob, proto);
    });
}

void RegisterRock()
{
    std::call_once(g_rock_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Rock;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_rock_team;
        proto.m_base_stats.max_health = game_config::mob_rock_max_health_max;
        proto.m_base_stats.armor = game_config::mob_rock_armor;
        proto.m_base_stats.damage = game_config::mob_rock_damage;
        proto.m_base_stats.radius = game_config::mob_rock_radius;
        proto.m_base_stats.mass = game_config::mob_rock_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            SMobStats stats = base_stats;
            float min_health = std::min(game_config::mob_rock_max_health_min, game_config::mob_rock_max_health_max);
            float max_health = std::max(game_config::mob_rock_max_health_min, game_config::mob_rock_max_health_max);
            stats.max_health = GetLimitedRng(min_health, max_health);
            return ScaleMobStats(stats, rarity);
        };
        REGISTER_MOB(EMobType::Rock, CBasicMob, proto);
    });
}

void RegisterBabyAnt()
{
    std::call_once(g_baby_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::BabyAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_baby_ant_team;
        proto.m_base_stats.max_health = game_config::mob_baby_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_baby_ant_armor;
        proto.m_base_stats.damage = game_config::mob_baby_ant_damage;
        proto.m_base_stats.radius = game_config::mob_baby_ant_radius;
        proto.m_base_stats.mass = game_config::mob_baby_ant_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CRandomWanderController>(); };
        REGISTER_MOB(EMobType::BabyAnt, CBasicMob, proto);
    });
}

void RegisterWorkerAnt()
{
    std::call_once(g_worker_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::WorkerAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_worker_ant_team;
        proto.m_base_stats.max_health = game_config::mob_worker_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_worker_ant_armor;
        proto.m_base_stats.damage = game_config::mob_worker_ant_damage;
        proto.m_base_stats.radius = game_config::mob_worker_ant_radius;
        proto.m_base_stats.mass = game_config::mob_worker_ant_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CNeutralMeleeController>(); };
        REGISTER_MOB(EMobType::WorkerAnt, CBasicMob, proto);
    });
}

void RegisterQueenAnt()
{
    std::call_once(g_queen_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::QueenAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_queen_ant_team;
        proto.m_base_stats.max_health = game_config::mob_queen_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_queen_ant_armor;
        proto.m_base_stats.damage = game_config::mob_queen_ant_damage;
        proto.m_base_stats.radius = game_config::mob_queen_ant_radius;
        proto.m_base_stats.mass = game_config::mob_queen_ant_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CQueenAntController>(); };
        REGISTER_MOB(EMobType::QueenAnt, CQueenAntMob, proto);
    });
}

void RegisterAntHole()
{
    std::call_once(g_ant_hole_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::AntHole;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_ant_hole_team;
        proto.m_base_stats.max_health = game_config::mob_ant_hole_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_hole_armor;
        proto.m_base_stats.damage = game_config::mob_ant_hole_damage;
        proto.m_base_stats.radius = game_config::mob_ant_hole_radius;
        proto.m_base_stats.mass = std::numeric_limits<float>::max();
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            SMobStats stats = ScaleMobStats(base_stats, rarity);
            stats.mass = std::numeric_limits<float>::max();
            return stats;
        };
        REGISTER_MOB(EMobType::AntHole, CAntHoleMob, proto);
    });
}

void RegisterSpider()
{
    std::call_once(g_spider_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::Spider;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_spider_team;
        proto.m_base_stats.max_health = game_config::mob_spider_max_health;
        proto.m_base_stats.armor = game_config::mob_spider_armor;
        proto.m_base_stats.damage = game_config::mob_spider_damage;
        proto.m_base_stats.radius = game_config::mob_spider_radius;
        proto.m_base_stats.mass = game_config::mob_spider_mass;
        proto.m_base_stats.horizon = game_config::mob_spider_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_spider_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_spider_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleFixedHorizonMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CSpiderController>(); };
        REGISTER_MOB(EMobType::Spider, CBasicMob, proto);
    });
}

void RegisterNormalLadybug()
{
    std::call_once(g_normal_ladybug_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::NormalLadybug;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_normal_ladybug_team;
        proto.m_base_stats.max_health = game_config::mob_normal_ladybug_max_health;
        proto.m_base_stats.armor = game_config::mob_normal_ladybug_armor;
        proto.m_base_stats.damage = game_config::mob_normal_ladybug_damage;
        proto.m_base_stats.radius = game_config::mob_normal_ladybug_radius;
        proto.m_base_stats.mass = game_config::mob_normal_ladybug_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CNeutralMeleeController>(); };
        REGISTER_MOB(EMobType::NormalLadybug, CBasicMob, proto);
    });
}

void RegisterPlayerFlower()
{
    std::call_once(g_playerflower_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::PlayerFlower;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_player_flower_team;
        proto.m_base_flower_stats.max_health = game_config::mob_player_flower_max_health;
        proto.m_base_flower_stats.armor = game_config::mob_player_flower_armor;
        proto.m_base_flower_stats.damage = game_config::mob_player_flower_damage;
        proto.m_base_flower_stats.radius = game_config::mob_player_flower_radius;
        proto.m_base_flower_stats.mass = game_config::mob_player_flower_mass;
        proto.m_base_flower_stats.horizon = game_config::default_horizon;
        proto.m_base_flower_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_flower_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_flower_stats.acceleration = game_config::default_acceleration;
        proto.m_base_flower_stats.petal_rotation_speed = game_config::mob_player_flower_petal_rotation_speed;
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity) {
            return base_stats;
        };
        proto.m_controller_factory = []() { return std::make_unique<CPlayerController>(); };
        REGISTER_MOB(EMobType::PlayerFlower, CPlayerFlower, proto);
    });
}

void RegisterNormalFlower()
{
    std::call_once(g_normal_flower_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::NormalFlower;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_normal_flower_team;
        proto.m_base_flower_stats.max_health = game_config::mob_normal_flower_max_health;
        proto.m_base_flower_stats.armor = game_config::mob_normal_flower_armor;
        proto.m_base_flower_stats.damage = game_config::mob_normal_flower_damage;
        proto.m_base_flower_stats.radius = game_config::default_flower_radius;
        proto.m_base_flower_stats.mass = game_config::mob_normal_flower_mass;
        proto.m_base_flower_stats.horizon = game_config::default_horizon;
        proto.m_base_flower_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_flower_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_flower_stats.acceleration = game_config::default_acceleration;
        proto.m_base_flower_stats.petal_rotation_speed = game_config::mob_normal_flower_petal_rotation_speed;
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity rarity) {
            return ScaleFlowerStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::NormalFlower, CNormalFlower, proto);
    });
}

void RegisterSummonedBeetle()
{
    std::call_once(g_summoned_beetle_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::SummonedBeetle;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_summoned_beetle_team;
        proto.m_base_stats.max_health = game_config::mob_summoned_beetle_max_health;
        proto.m_base_stats.armor = game_config::mob_summoned_beetle_armor;
        proto.m_base_stats.damage = game_config::mob_summoned_beetle_damage;
        proto.m_base_stats.radius = game_config::mob_summoned_beetle_radius;
        proto.m_base_stats.mass = game_config::mob_summoned_beetle_mass;
        proto.m_base_stats.horizon = game_config::mob_summoned_beetle_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleSummonedMobStats(base_stats, rarity);
        };
        REGISTER_MOB(EMobType::SummonedBeetle, CBasicMob, proto);
    });
}

void RegisterSoldierAnt()
{
    std::call_once(g_soldier_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::SoldierAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_soldier_ant_team;
        proto.m_base_stats.max_health = game_config::mob_soldier_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_soldier_ant_armor;
        proto.m_base_stats.damage = game_config::mob_soldier_ant_damage;
        proto.m_base_stats.radius = game_config::mob_soldier_ant_radius;
        proto.m_base_stats.mass = game_config::mob_soldier_ant_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::SoldierAnt, CBasicMob, proto);
    });
}

void RegisterSoldierFireAnt()
{
    std::call_once(g_soldier_fire_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::SoldierFireAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_soldier_fire_ant_team;
        proto.m_base_stats.max_health = game_config::mob_soldier_fire_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_soldier_fire_ant_armor;
        proto.m_base_stats.damage = game_config::mob_soldier_fire_ant_damage;
        proto.m_base_stats.radius = game_config::mob_soldier_fire_ant_radius;
        proto.m_base_stats.mass = game_config::mob_soldier_fire_ant_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };
        REGISTER_MOB(EMobType::SoldierFireAnt, CBasicMob, proto);
    });
}

void RegisterSoldierTermite()
{
    std::call_once(g_soldier_termite_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::SoldierTermite;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_soldier_termite_team;
        proto.m_base_stats.max_health = game_config::mob_soldier_termite_max_health;
        proto.m_base_stats.armor = game_config::mob_soldier_termite_armor;
        proto.m_base_stats.damage = game_config::mob_soldier_termite_damage;
        proto.m_base_stats.radius = game_config::mob_soldier_termite_radius;
        proto.m_base_stats.mass = game_config::mob_soldier_termite_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = []() { return std::make_unique<CMeleeController>(); };

        CMobPrototype* proto_ptr = nullptr;
        auto holder = std::make_unique<CMobPrototype>(std::move(proto));
        proto_ptr = holder.get();
        proto_ptr->m_factory = [proto_ptr](CGameWorld* world, sf::Vector2f pos, ERarity rarity) -> std::unique_ptr<CMobBase>
        {
            if (!world) return nullptr;

            SMobStats stats = proto_ptr->BuildTypedStats<SMobStats>(rarity);
            auto mob = std::make_unique<CBasicMob>(world, pos, stats.radius, rarity, stats);
            mob->m_mob_type = proto_ptr->m_type;
            mob->m_team = proto_ptr->m_team;
            mob->AddState(std::make_unique<CPsionicConnectionState>(mob.get(), endless, rarity));
            if (proto_ptr->m_controller_factory) mob->SetController(proto_ptr->m_controller_factory());
            return mob;
        };
        g_mob_registry[EMobType::SoldierTermite] = std::move(holder);
    });
}

void RegisterSummonedSoldierAnt()
{
    std::call_once(g_summoned_soldier_ant_registered, []() {
        CMobPrototype proto;
        proto.m_type = EMobType::SummonedSoldierAnt;
        proto.m_name = std::string(GetMobTypeName(proto.m_type));
        proto.m_team = game_config::mob_summoned_soldier_ant_team;
        proto.m_base_stats.max_health = game_config::mob_summoned_soldier_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_summoned_soldier_ant_armor;
        proto.m_base_stats.damage = game_config::mob_summoned_soldier_ant_damage;
        proto.m_base_stats.radius = game_config::mob_summoned_soldier_ant_radius;
        proto.m_base_stats.mass = game_config::mob_summoned_soldier_ant_mass;
        proto.m_base_stats.horizon = game_config::mob_summoned_soldier_ant_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_stats.acceleration = game_config::default_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleSummonedMobStats(base_stats, rarity);
        };
        REGISTER_MOB(EMobType::SummonedSoldierAnt, CBasicMob, proto);
    });
}
