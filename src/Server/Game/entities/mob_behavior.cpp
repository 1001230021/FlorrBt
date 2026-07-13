#include "../controllers/melee_controller.h"
#include "../controllers/player_controller.h"
#include "../gameworld.h"
#include "../states/states.h"
#include "flower.h"
#include "mob.h"
#include "projectile.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

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
    float radius_scale = std::pow(static_cast<float>(level), game_config::mob_radius_scale_exp);
    float horizon_scale = std::pow(static_cast<float>(level), game_config::mob_horizon_scale_exp);
    float mass_scale =
        std::pow(game_config::mob_mass_scale_base, static_cast<float>(level - 1) * game_config::mob_mass_scale_exp_multiplier);

    stats.max_health *= health_scale;
    stats.damage *= damage_scale;
    stats.radius *= radius_scale;
    stats.horizon *= horizon_scale;
    stats.mass *= mass_scale;
    return stats;
}

SMobStats ScaleSummonedMobStats(SMobStats stats, ERarity rarity)
{
    const float base_radius = stats.radius;
    stats = ScaleMobStats(stats, rarity);
    stats.radius = base_radius * (1.f + 0.2f * static_cast<float>(GetLevel(rarity)));
    return stats;
}

SMobStats ScaleHornetStats(SMobStats stats, ERarity rarity)
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
        if (m_is_marked_for_des && FindStates<CUndeadState>().empty())
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
    }

    bool IsFacingLocked() const override { return m_attack_recovery_timer > 0.f; }

    bool TryAttack(CEntity* target) override
    {
        if (m_attack_timer > 0.f || m_attack_windup_timer > 0.f || !GameWorld()) return false;

        m_attack_target_id = target ? target->m_id : -1;
        m_attack_windup_timer = game_config::mob_hornet_attack_windup;
        return true;
    }

  private:
    void FireQueuedAttack()
    {
        if (!GameWorld()) return;
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

        int level = GetLevel(GetRarity());
        float missile_damage =
            game_config::mob_hornet_missile_base_damage * std::pow(3.f, static_cast<float>(level - 1));
        float missile_health =
            game_config::mob_hornet_missile_base_health * std::pow(5.f, static_cast<float>(level - 1));

        sf::Vector2f spawn_pos = m_pos + rear_direction * (m_radius * 0.5f);
        auto missile = std::make_unique<CMissile>(GameWorld(), spawn_pos, game_config::mob_hornet_missile_radius,
                                                  rear_direction, game_config::mob_hornet_missile_speed,
                                                  missile_damage, missile_health,
                                                  game_config::default_missile_lifetime, this);
        missile->m_team = m_team;
        CEntity* inserted = GameWorld()->InsertEntity(std::move(missile));
        if (!inserted) return;

        m_vel += recoil_direction * game_config::mob_hornet_recoil_speed;
        m_attack_timer = game_config::mob_hornet_attack_interval;
        m_attack_recovery_timer = game_config::mob_hornet_attack_recovery;
        m_attack_target_id = -1;
    }

    float m_attack_timer = 0.f;
    float m_attack_windup_timer = 0.f;
    float m_attack_recovery_timer = 0.f;
    int m_attack_target_id = -1;
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
            return ScaleMobStats(base_stats, rarity);
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
