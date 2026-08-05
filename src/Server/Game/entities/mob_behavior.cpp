#include "../../Module/network_module.h"
#include "../controllers/melee_controller.h"
#include "../controllers/player_controller.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "../../HotReload/snapshot_archive.h"
#include "flower.h"
#include "mob.h"
#include "petals/petal.h"
#include "petals/petal_slot.h"
#include "projectile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using CBasicMob = CMob<SMobStats>;
using CAttackableBasicMob = CAttackableMob<SMobStats>;
using CSkillCasterBasicMob = CSkillCasterMob<SMobStats>;

float HornetMissileSpeed(ERarity rarity)
{
    int level = GetLevel(rarity);
    return game_config::mob_hornet_missile_speed *
           (1.f + game_config::mob_hornet_missile_speed_level_step * static_cast<float>(level));
}

inline float GetHealthMult(int level)
{
    switch (level)
    {
    case 1:
        return game_config::mob_health_scale_common;
    case 2:
        return game_config::mob_health_scale_unusual;
    case 3:
        return game_config::mob_health_scale_rare;
    case 4:
        return game_config::mob_health_scale_epic;
    case 5:
        return game_config::mob_health_scale_legendary;
    case 6:
        return game_config::mob_health_scale_mythic;
    case 7:
        return game_config::mob_health_scale_ultra * 1.25f;
    case 8:
        return game_config::mob_health_scale_super * 1.25f * 1.5f;
    case 9:
        return game_config::mob_health_scale_eternal * 1.25f * 1.5f * 1.2f;
    case 10:
        return game_config::mob_health_scale_primordial * 1.25f * 1.5f * 1.2f;
    default:
        return 0.f;
    }
}

float MobHealthScaleForRarity(ERarity rarity)
{
    return GetHealthMult(GetLevel(rarity));
}

SMobStats ScaleMobStats(SMobStats stats, ERarity rarity)
{
    int level = GetLevel(rarity);
    float health_scale = MobHealthScaleForRarity(rarity);
    float damage_scale = std::pow(game_config::mob_damage_scale_base, static_cast<float>(level - 1));
    float radius_scale = game_config::MobRadiusScaleForLevel(level);
    float horizon_scale = std::pow(static_cast<float>(level), game_config::mob_horizon_scale_exp);
    float mass_scale = std::pow(game_config::mob_mass_scale_base,
                                static_cast<float>(level - 1) * game_config::mob_mass_scale_exp_multiplier);

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
    const float linear_scale = 1.f + game_config::mob_summoned_rarity_linear_scale * GetRarityValueLevel(rarity);
    stats = ScaleMobStats(stats, rarity);
    stats.radius = base_radius * linear_scale;
    stats.mass = base_mass * linear_scale;
    return stats;
}

SMobStats ScaleHornetStats(SMobStats stats, ERarity rarity)
{
    stats = ScaleMobStats(stats, rarity);
    stats.horizon = HornetMissileSpeed(rarity) * game_config::default_missile_lifetime *
                    game_config::mob_hornet_horizon_lifetime_multiplier;
    return stats;
}

SMobStats ScaleFixedHorizonMobStats(SMobStats stats, ERarity rarity)
{
    const float base_horizon = stats.horizon;
    stats = ScaleMobStats(stats, rarity);
    stats.horizon = base_horizon;
    return stats;
}

SMobStats ScaleAntEggStats(SMobStats stats, ERarity rarity)
{
    const float fixed_mass = stats.mass;
    stats = ScaleMobStats(stats, rarity);
    stats.mass = fixed_mass;
    return stats;
}

SMobStats FireAntStats(SMobStats stats)
{
    stats.damage *= game_config::mob_fire_ant_damage_multiplier;
    return stats;
}

SMobStats TermiteStats(SMobStats stats)
{
    stats.max_health *= game_config::mob_termite_health_multiplier;
    return stats;
}

SFlowerStats ScaleFlowerStats(SFlowerStats stats, ERarity rarity)
{
    static_cast<SMobStats&>(stats) = ScaleMobStats(stats, rarity);
    return stats;
}

SFlowerStats NormalFlowerBaseStats()
{
    SFlowerStats stats;
    stats.max_health = game_config::mob_normal_flower_max_health;
    stats.armor = game_config::mob_normal_flower_armor;
    stats.damage = game_config::mob_normal_flower_damage;
    stats.radius = game_config::default_flower_radius;
    stats.mass = game_config::mob_normal_flower_mass;
    stats.horizon = game_config::default_horizon;
    stats.max_absorb_range = game_config::default_absorb_range;
    stats.max_velocity = game_config::default_max_velocity;
    stats.acceleration = game_config::default_acceleration;
    stats.petal_rotation_speed = game_config::mob_normal_flower_petal_rotation_speed;
    return stats;
}

float DotProduct(sf::Vector2f lhs, sf::Vector2f rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }

sf::Vector2f NormalizeOrZero(sf::Vector2f value)
{
    float len = Length(value);
    if (len <= game_config::entity_collision_epsilon) return { 0.f, 0.f };
    return value / len;
}

sf::Vector2f GetEntityVelocity(CEntity* entity)
{
    if (auto* mob = dynamic_cast<CMobBase*>(entity)) return mob->m_vel;
    if (auto* projectile = dynamic_cast<CProjectile*>(entity)) return projectile->m_vel;
    return { 0.f, 0.f };
}

std::string FormatDummyNumber(float value)
{
    if (!std::isfinite(value)) return "0";

    const float abs_value = std::abs(value);
    const char* suffix = "";
    float scaled = value;
    if (abs_value >= 1000000000.f)
    {
        scaled = value / 1000000000.f;
        suffix = "b";
    } else if (abs_value >= 1000000.f)
    {
        scaled = value / 1000000.f;
        suffix = "m";
    } else if (abs_value >= 1000.f)
    {
        scaled = value / 1000.f;
        suffix = "k";
    }

    float abs_scaled = std::abs(scaled);
    int precision = abs_scaled >= 100.f ? 0 : (abs_scaled >= 10.f ? 1 : 2);

    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << scaled;

    std::string text = out.str();
    if (text.find('.') != std::string::npos)
    {
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    text += suffix;
    return text;
}

void SendDummyDamageReport(uint32_t player_id, float total_damage, float dps)
{
    auto* server = CServer::GetInstance();
    if (!server) return;

    INetworkModule* network = server->GetNetworkModule();
    if (!network) return;

    CPlayer* player = network->FindPlayerById(player_id);
    if (!player || !player->IsAuthenticated()) return;

    std::string message = "Dummy total dmg: " + FormatDummyNumber(total_damage) + " | DPS: " + FormatDummyNumber(dps);
    if (const CServer::SChatEntry* chat = server->SubmitChat(nullptr, { 0.f, 0.f }, EChatFlag::Server, 0, "Dummy",
                                                             message, static_cast<int>(player->GetId())))
    {
        network->SendChatToPlayer(*player, *chat);
    }
}

float DummyInitialReportDelay(const CEntity* attacker)
{
    float delay = std::max(0.f, game_config::mob_dummy_damage_report_interval);
    const auto* flower = dynamic_cast<const CFlower*>(attacker);
    if (!flower) return delay;

    for (const CPetalSlot& slot : flower->GetSlots())
    {
        if (!slot.m_available || slot.m_banned) continue;
        for (const CPetal* petal : slot.m_p_petals)
        {
            if (!petal || petal->m_hidden || petal->m_is_marked_for_des || petal->m_health <= 0.f) continue;
            if (petal->m_final_petal_stats.damage <= 0.f) continue;
            delay = std::max(delay, std::max(0.f, petal->m_final_petal_stats.reload));
        }
    }
    return delay;
}

sf::Vector2f GetHornetShotDirection(CEntity* shooter, CEntity* target, float missile_speed, ERarity rarity)
{
    if (!shooter || !target) return { 0.f, 0.f };

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
        if (std::abs(b) > game_config::entity_collision_epsilon) t = -c / b;
    } else
    {
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
    case 1:
        return ERarity::Common;
    case 2:
        return ERarity::Unusual;
    case 3:
        return ERarity::Rare;
    case 4:
        return ERarity::Epic;
    case 5:
        return ERarity::Legendary;
    case 6:
        return ERarity::Mythic;
    case 7:
        return ERarity::Ultra;
    case 8:
        return ERarity::Super;
    case 9:
        return ERarity::Eternal;
    default:
        return ERarity::Primordial;
    }
}

ERarity RarityFromNaturalMobLevel(int level)
{
    level = std::clamp(level, GetLevel(ERarity::Common), GetLevel(ERarity::Primordial));
    switch (level)
    {
    case 1:
        return ERarity::Common;
    case 2:
        return ERarity::Unusual;
    case 3:
        return ERarity::Rare;
    case 4:
        return ERarity::Epic;
    case 5:
        return ERarity::Legendary;
    case 6:
        return ERarity::Mythic;
    case 7:
        return ERarity::Ultra;
    case 8:
        return ERarity::Super;
    case 9:
        return ERarity::Eternal;
    default:
        return ERarity::Primordial;
    }
}

float LeafPieceRegenPerSecond(ERarity rarity)
{
    return std::max(0.f, game_config::mob_leaf_piece_regen_per_second) *
           std::max(0.f, MobHealthScaleForRarity(rarity));
}

int CountOwnedSummonedSoldierAnts(CGameWorld* world, const CEntity* owner)
{
    if (!world || !owner) return 0;

    int count = 0;
    world->ForEachEntity([&](CEntity* entity) {
        auto* mob = dynamic_cast<CMobBase*>(entity);
        if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;
        switch (mob->GetMobType())
        {
        case EMobType::SoldierAnt:
        case EMobType::SoldierFireAnt:
        case EMobType::SummonedSoldierAnt:
        case EMobType::QueenAntEgg:
        case EMobType::QueenFireAntEgg:
            break;
        default:
            return;
        }
        auto* controller = dynamic_cast<CSummonedMeleeController*>(mob->GetController());
        if (controller && controller->IsOwnedBy(owner)) ++count;
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
            CancelDestroy();
            m_health = std::max(game_config::mob_bandage_min_revive_health, m_health);
            AddState(std::make_unique<CUndeadState>(this, GetBandageUndeadDuration(GetRarity()), GetRarity(), -1));
        }
    }
};

class CBeeMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override { writer.Field("wave_timer", m_wave_timer); }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_wave_timer = reader.Float("wave_timer", m_wave_timer);
        return true;
    }

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
            if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = { 0.f, 0.f };
            return;
        }

        sf::Vector2f forward = delta / len;
        sf::Vector2f left(-forward.y, forward.x);
        float near_factor =
            std::clamp((len - m_radius) / std::max(game_config::mob_bee_wave_near_range_min,
                                                   m_radius * game_config::mob_bee_wave_near_range_radius_multiplier),
                       game_config::mob_bee_wave_near_factor_min, 1.f);
        float wave = std::sin(m_wave_timer * game_config::mob_bee_wave_frequency) * game_config::mob_bee_wave_strength *
                     near_factor;
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
        } else
        {
            m_vel += diff / diff_len * max_accel;
        }
    }

  private:
    float m_wave_timer = 0.f;
};

class CHornetMob : public CSkillCasterBasicMob
{
  public:
    using CSkillCasterBasicMob::CSkillCasterBasicMob;

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("attack_timer", m_attack_timer);
        writer.Field("attack_windup_timer", m_attack_windup_timer);
        writer.Field("attack_recovery_timer", m_attack_recovery_timer);
        writer.Field("special_windup_timer", m_special_windup_timer);
        writer.Field("missile_reload_timer", m_missile_reload_timer);
        writer.Field("special_windup_id", static_cast<int>(m_special_windup_id));
        writer.Field("attack_target_id", m_attack_target_id);
        writer.UInt64("attack_target_generation", m_attack_target_generation);
        writer.Field("loaded_missile_id", m_loaded_missile_id);
        writer.UInt64("loaded_missile_generation", m_loaded_missile_generation);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CSkillCasterBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_attack_timer = reader.Float("attack_timer", m_attack_timer);
        m_attack_windup_timer = reader.Float("attack_windup_timer", m_attack_windup_timer);
        m_attack_recovery_timer = reader.Float("attack_recovery_timer", m_attack_recovery_timer);
        m_special_windup_timer = reader.Float("special_windup_timer", m_special_windup_timer);
        m_missile_reload_timer = reader.Float("missile_reload_timer", m_missile_reload_timer);
        m_special_windup_id = static_cast<std::uint8_t>(std::clamp(reader.Int("special_windup_id"), 0, 255));
        m_attack_target_id = reader.Int("attack_target_id", m_attack_target_id);
        m_attack_target_generation = reader.UInt64("attack_target_generation", m_attack_target_generation);
        m_loaded_missile_id = reader.Int("loaded_missile_id", m_loaded_missile_id);
        m_loaded_missile_generation = reader.UInt64("loaded_missile_generation", m_loaded_missile_generation);
        return true;
    }

    void Tick(float dt) override
    {
        m_attack_timer = std::max(0.f, m_attack_timer - dt);
        m_attack_recovery_timer = std::max(0.f, m_attack_recovery_timer - dt);
        if (m_special_windup_timer > 0.f) m_special_windup_timer = std::max(0.f, m_special_windup_timer - dt);
        if (m_attack_windup_timer > 0.f)
        {
            m_attack_windup_timer -= dt;
            if (m_attack_windup_timer <= 0.f) FireQueuedAttack();
        }
        CSkillCasterBasicMob::Tick(dt);
        UpdateLoadedMissile(dt);
    }

    bool IsFacingLocked() const override { return m_attack_recovery_timer > 0.f; }

    int GetSkillCount() const override { return 4; }

    bool CanCastSkill(int skill_index) const override
    {
        if (!GameWorld()) return false;
        if (skill_index == hornet_missile_skill)
        {
            return m_attack_timer <= 0.f && m_attack_windup_timer <= 0.f && m_special_windup_timer <= 0.f &&
                   (m_loaded_missile_id >= 0 || m_missile_reload_timer <= 0.f);
        }
        if (skill_index == hornet_summon_skill || skill_index == hornet_dash_skill || skill_index == hornet_grab_skill)
            return m_attack_windup_timer <= 0.f && m_special_windup_timer <= 0.f;
        return false;
    }

    bool TryCastSkill(int skill_index, CEntity* target) override
    {
        if (skill_index == hornet_summon_skill || skill_index == hornet_dash_skill || skill_index == hornet_grab_skill)
        {
            if (m_attack_windup_timer > 0.f || m_special_windup_timer > 0.f || !GameWorld()) return false;
            m_special_windup_id = SkillWindupId(skill_index);
            m_special_windup_timer = skill_index == hornet_grab_skill ? game_config::mob_hornet_skill3_charge_time
                                                                      : game_config::mob_hornet_skill_windup_time;
            return true;
        }

        if (skill_index != hornet_missile_skill) return false;
        if (m_attack_timer > 0.f || m_attack_windup_timer > 0.f || m_special_windup_timer > 0.f || !GameWorld())
            return false;
        if (!GetLoadedMissile()) return false;

        m_attack_target_id = target ? target->m_id : -1;
        m_attack_target_generation = target ? target->m_generation : 0;
        m_attack_windup_timer = game_config::mob_hornet_attack_windup;
        return true;
    }

    bool TryAttack(CEntity* target) override { return TryCastSkill(hornet_missile_skill, target); }
    bool IsSkillBusy() const override { return m_attack_windup_timer > 0.f; }
    uint8_t GetWindupSkillId() const override
    {
        if (m_special_windup_timer > 0.f) return m_special_windup_id;
        return m_attack_windup_timer > 0.f ? hornet_missile_windup_id : 0;
    }

  private:
    static constexpr int hornet_missile_skill = 0;
    static constexpr int hornet_summon_skill = 1;
    static constexpr int hornet_dash_skill = 2;
    static constexpr int hornet_grab_skill = 3;
    static constexpr uint8_t hornet_missile_windup_id = 1;
    static constexpr uint8_t hornet_summon_windup_id = 2;
    static constexpr uint8_t hornet_dash_windup_id = 3;
    static constexpr uint8_t hornet_grab_windup_id = 4;

    static uint8_t SkillWindupId(int skill_index)
    {
        if (skill_index == hornet_summon_skill) return hornet_summon_windup_id;
        if (skill_index == hornet_dash_skill) return hornet_dash_windup_id;
        if (skill_index == hornet_grab_skill) return hornet_grab_windup_id;
        return hornet_missile_windup_id;
    }

    CMissile* FindLoadedMissile()
    {
        if (!GameWorld() || m_loaded_missile_id < 0) return nullptr;

        CEntity* entity = GameWorld()->GetEntity(m_loaded_missile_id, m_loaded_missile_generation);
        auto* missile = dynamic_cast<CMissile*>(entity);
        if (!missile || missile->m_is_marked_for_des || missile->GetOwner() != this || !missile->IsAttachedToOwner())
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
        if (m_missile_reload_timer <= 0.f) m_missile_reload_timer = game_config::mob_hornet_missile_reload;
    }

    CMissile* GetLoadedMissile()
    {
        RefreshLoadedMissileState();
        if (m_loaded_missile_id < 0 && m_missile_reload_timer <= 0.f) SpawnLoadedMissile();
        return FindLoadedMissile();
    }

    sf::Vector2f LoadedMissileDirection() const
    {
        sf::Vector2f facing = { std::cos(m_facing_angle), std::sin(m_facing_angle) };
        if (LengthSq(facing) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            facing = { 1.f, 0.f };
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
        float missile_damage = game_config::mob_hornet_missile_base_damage *
                               std::pow(game_config::mob_damage_scale_base, static_cast<float>(level - 1));
        float missile_health = game_config::mob_hornet_missile_base_health *
                               std::pow(game_config::mob_projectile_health_scale_base, static_cast<float>(level - 1));
        float radius_scale = m_radius / std::max(game_config::entity_collision_epsilon, game_config::mob_hornet_radius);
        float missile_radius = game_config::mob_hornet_missile_radius * radius_scale;

        auto missile = std::make_unique<CMissile>(GameWorld(), LoadedMissilePosition(), missile_radius,
                                                  LoadedMissileDirection(), 0.f, missile_damage, missile_health,
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
            m_attack_target_generation = 0;
            return;
        }

        CEntity* target =
            m_attack_target_id >= 0 ? GameWorld()->GetEntity(m_attack_target_id, m_attack_target_generation) : nullptr;
        sf::Vector2f rear_direction = { std::cos(m_facing_angle), std::sin(m_facing_angle) };
        float missile_speed = HornetMissileSpeed(GetRarity());
        if (target && !target->m_is_marked_for_des && !target->IsDead())
        {
            sf::Vector2f shot_direction = GetHornetShotDirection(this, target, missile_speed, GetRarity());
            if (LengthSq(shot_direction) >
                game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
                rear_direction = shot_direction;
        }

        if (LengthSq(rear_direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            return;

        sf::Vector2f recoil_direction = -rear_direction;
        m_facing_angle = std::atan2(recoil_direction.y, recoil_direction.x);
        m_has_facing = true;

        sf::Vector2f spawn_pos =
            m_pos + rear_direction * (m_radius * game_config::mob_hornet_rear_spawn_radius_multiplier);
        missile->m_pos = spawn_pos;
        missile->m_prev_pos = spawn_pos;
        missile->m_team = m_team;
        if (!missile->Fire(rear_direction, missile_speed, game_config::default_missile_lifetime))
        {
            m_attack_target_id = -1;
            m_attack_target_generation = 0;
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
        m_attack_target_generation = 0;
    }

    float m_attack_timer = 0.f;
    float m_attack_windup_timer = 0.f;
    float m_attack_recovery_timer = 0.f;
    float m_special_windup_timer = 0.f;
    float m_missile_reload_timer = 0.f;
    uint8_t m_special_windup_id = 0;
    int m_attack_target_id = -1;
    std::uint64_t m_attack_target_generation = 0;
    int m_loaded_missile_id = -1;
    std::uint64_t m_loaded_missile_generation = 0;
};

class CDandelionMob : public CAttackableBasicMob
{
  public:
    static constexpr int missile_capacity = 32;

    CDandelionMob(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                  const SMobStats& stats)
        : CAttackableBasicMob(pworld, pos, r, mob_type, rarity, stats)
    {
        m_loaded_missile_ids.fill(-1);
        m_loaded_missile_generations.fill(0);
    }

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        CJsonOwner missiles = MakeJsonArray();
        for (int index = 0; index < missile_capacity; ++index)
        {
            CJsonOwner missile = MakeJsonObject();
            CSnapshotWriter missile_writer(missile.get());
            missile_writer.Field("index", index);
            missile_writer.Field("id", m_loaded_missile_ids[index]);
            missile_writer.UInt64("generation", m_loaded_missile_generations[index]);
            AppendJson(missiles.get(), missile.release());
        }
        writer.Node("loaded_missiles", missiles.release());
        writer.Field("spawned_initial_missiles", m_spawned_initial_missiles);
        writer.Field("attack_committed", m_attack_committed);
        writer.Field("fire_active", m_fire_active);
        writer.Field("fire_timer", m_fire_timer);
        writer.Field("next_fire_index", m_next_fire_index);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CAttackableBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_loaded_missile_ids.fill(-1);
        m_loaded_missile_generations.fill(0);
        const json_t* missiles = reader.Node("loaded_missiles");
        if (json_is_array(missiles))
        {
            const size_t count = json_array_size(missiles);
            for (size_t i = 0; i < count; ++i)
            {
                CSnapshotReader missile(json_array_get(missiles, i));
                const int index = missile.Int("index", -1);
                if (index < 0 || index >= missile_capacity) continue;
                m_loaded_missile_ids[index] = missile.Int("id", -1);
                m_loaded_missile_generations[index] = missile.UInt64("generation");
            }
        }
        m_spawned_initial_missiles = reader.Bool("spawned_initial_missiles", m_spawned_initial_missiles);
        m_attack_committed = reader.Bool("attack_committed", m_attack_committed);
        m_fire_active = reader.Bool("fire_active", m_fire_active);
        m_fire_timer = reader.Float("fire_timer", m_fire_timer);
        m_next_fire_index = std::clamp(reader.Int("next_fire_index", m_next_fire_index), 0, missile_capacity - 1);
        return true;
    }

    void Tick(float dt) override
    {
        CAttackableBasicMob::Tick(dt);
        m_vel *= game_config::mob_stop_damping;
        if (LengthSq(m_vel) <= game_config::mob_stop_velocity_epsilon) m_vel = { 0.f, 0.f };
        UpdateMissiles(dt);
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        float old_health = m_health;
        CAttackableBasicMob::TakeDamage(dmg, attacker, dmg_type);
        if (old_health > m_health && old_health > 0.f) TryAttack(attacker);
    }

    bool TryAttack(CEntity*) override
    {
        if (!GameWorld() || m_is_marked_for_des || IsDead() || m_attack_committed) return false;

        if (!m_spawned_initial_missiles)
        {
            SpawnInitialMissiles();
            m_spawned_initial_missiles = true;
        }
        if (!HasLoadedMissile()) return false;

        m_attack_committed = true;
        m_fire_active = true;
        m_fire_timer = 0.f;
        m_attacking = true;
        m_defending = false;
        return true;
    }

  private:
    static int MissileCount() { return std::clamp(game_config::mob_dandelion_missile_count, 0, missile_capacity); }

    static float RarityPowScale(ERarity rarity, float base)
    {
        return std::pow(base, static_cast<float>(GetLevel(rarity) - 1));
    }

    float MissileRadius() const
    {
        float radius_scale =
            m_radius / std::max(game_config::entity_collision_epsilon, game_config::mob_dandelion_radius);
        return game_config::mob_dandelion_missile_radius * radius_scale;
    }

    float MissileDamage() const
    {
        return game_config::mob_dandelion_missile_base_damage *
               RarityPowScale(GetRarity(), game_config::mob_damage_scale_base);
    }

    float MissileHealth() const
    {
        return game_config::mob_dandelion_missile_base_health *
               RarityPowScale(GetRarity(), game_config::mob_projectile_health_scale_base);
    }

    CDandelionMissile* FindMissile(int index)
    {
        if (!GameWorld() || index < 0 || index >= missile_capacity || m_loaded_missile_ids[index] < 0) return nullptr;

        CEntity* entity = GameWorld()->GetEntity(m_loaded_missile_ids[index], m_loaded_missile_generations[index]);
        auto* missile = dynamic_cast<CDandelionMissile*>(entity);
        if (!missile || missile->m_is_marked_for_des || missile->GetOwner() != this || !missile->IsAttachedToOwner())
        {
            m_loaded_missile_ids[index] = -1;
            m_loaded_missile_generations[index] = 0;
            return nullptr;
        }
        return missile;
    }

    bool HasLoadedMissile()
    {
        const int count = MissileCount();
        for (int i = 0; i < count; ++i)
        {
            if (FindMissile(i)) return true;
        }
        return false;
    }

    void SpawnInitialMissiles()
    {
        CGameWorld* world = GameWorld();
        if (!world) return;

        const int count = MissileCount();
        if (count <= 0) return;

        const float radius = MissileRadius();
        const float damage = MissileDamage();
        const float health = MissileHealth();
        const float lifetime = std::max(game_config::server_fixed_dt, game_config::mob_dandelion_missile_lifetime);
        for (int i = 0; i < count; ++i)
        {
            if (FindMissile(i)) continue;

            float attach_angle = 2.f * game_config::pi * static_cast<float>(i) / static_cast<float>(count);
            sf::Vector2f direction = { std::cos(m_facing_angle + attach_angle),
                                       std::sin(m_facing_angle + attach_angle) };
            sf::Vector2f pos = m_pos + direction * (m_radius * game_config::mob_dandelion_missile_attach_offset);
            auto missile = std::make_unique<CDandelionMissile>(world, pos, radius, attach_angle, damage, health,
                                                               lifetime, GetRarity(), this);
            missile->m_team = m_team;
            missile->m_mass = m_mass;
            missile->AttachToOwner();

            CEntity* inserted = world->InsertEntity(std::move(missile));
            if (!inserted) continue;
            m_loaded_missile_ids[i] = inserted->m_id;
            m_loaded_missile_generations[i] = inserted->m_generation;
        }
    }

    void UpdateMissiles(float dt)
    {
        if (!GameWorld() || m_is_marked_for_des || IsDead()) return;

        if (!m_spawned_initial_missiles)
        {
            SpawnInitialMissiles();
            m_spawned_initial_missiles = true;
        }

        if (!m_fire_active) return;
        m_fire_timer = std::max(0.f, m_fire_timer - dt);
        if (m_fire_timer > 0.f) return;

        if (FireNextMissile())
        {
            m_fire_timer = std::max(game_config::server_fixed_dt, game_config::mob_dandelion_missile_fire_interval);
            return;
        }
        m_fire_active = false;
        m_attacking = false;
    }

    bool FireNextMissile()
    {
        const int count = MissileCount();
        if (count <= 0 || !GameWorld()) return false;

        for (int attempt = 0; attempt < count; ++attempt)
        {
            int index = m_next_fire_index % count;
            m_next_fire_index = (m_next_fire_index + 1) % count;

            CDandelionMissile* missile = FindMissile(index);
            if (!missile) continue;

            sf::Vector2f direction = missile->m_pos - m_pos;
            if (LengthSq(direction) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            {
                float angle = m_facing_angle + missile->GetAttachAngle();
                direction = { std::cos(angle), std::sin(angle) };
            }

            float len = Length(direction);
            if (len <= game_config::entity_collision_epsilon) direction = { 1.f, 0.f };
            else direction /= len;

            missile->m_prev_pos = missile->m_pos;
            missile->m_team = m_team;
            if (!missile->Fire(direction, game_config::mob_dandelion_missile_speed,
                               game_config::mob_dandelion_missile_lifetime))
            {
                m_loaded_missile_ids[index] = -1;
                m_loaded_missile_generations[index] = 0;
                continue;
            }

            m_loaded_missile_ids[index] = -1;
            m_loaded_missile_generations[index] = 0;
            return true;
        }

        return false;
    }

    std::array<int, missile_capacity> m_loaded_missile_ids;
    std::array<std::uint64_t, missile_capacity> m_loaded_missile_generations;
    bool m_spawned_initial_missiles = false;
    bool m_attack_committed = false;
    bool m_fire_active = false;
    float m_fire_timer = 0.f;
    int m_next_fire_index = 0;
};

class CAntEggMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("initialized", m_initialized);
        writer.Field("auto_hatch_timer", m_auto_hatch_timer);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_initialized = reader.Bool("initialized", m_initialized);
        m_auto_hatch_timer = reader.Float("auto_hatch_timer", m_auto_hatch_timer);
        return true;
    }

    void Tick(float dt) override
    {
        InitializeByType();
        if (m_auto_hatch_timer >= 0.f)
        {
            m_auto_hatch_timer -= dt;
            if (m_auto_hatch_timer <= 0.f)
            {
                Hatch(AutoHatchType(), true);
                MarkForDestroy(EEntityRemovalReason::Transformed);
                return;
            }
        }

        m_pos += m_vel * dt;
        m_vel *=
            std::pow(game_config::mob_stop_damping, std::max(0.f, dt) * game_config::mob_stop_damping_ticks_per_second);
        TickStates(dt);
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        bool was_dead = m_is_marked_for_des || IsDead();
        CBasicMob::TakeDamage(dmg, attacker, dmg_type);
        if (was_dead || !m_is_marked_for_des) return;

        EMobType hatch_type = NaturalDeathHatchType();
        if (hatch_type == EMobType::None) return;
        if (!CheckChance(game_config::mob_ant_egg_death_hatch_chance)) return;
        Hatch(hatch_type, false);
    }

  private:
    void InitializeByType()
    {
        if (m_initialized) return;
        m_initialized = true;
        if (AutoHatchType() != EMobType::None)
            m_auto_hatch_timer = std::max(0.f, game_config::mob_queen_laid_egg_hatch_time);
    }

    EMobType AutoHatchType() const
    {
        switch (GetMobType())
        {
        case EMobType::QueenAntEgg:
            return EMobType::SoldierAnt;
        case EMobType::QueenFireAntEgg:
            return EMobType::SoldierFireAnt;
        default:
            return EMobType::None;
        }
    }

    EMobType NaturalDeathHatchType() const
    {
        switch (GetMobType())
        {
        case EMobType::AntEgg:
            return EMobType::BabyAnt;
        case EMobType::FireAntEgg:
            return EMobType::BabyFireAnt;
        case EMobType::TermiteEgg:
            return EMobType::BabyTermite;
        default:
            return EMobType::None;
        }
    }

    void Hatch(EMobType hatch_type, bool preserve_owner)
    {
        CGameWorld* world = GameWorld();
        if (!world || hatch_type == EMobType::None) return;

        CEntity* owner = nullptr;
        bool persist_after_owner_death = false;
        if (preserve_owner)
        {
            auto* summoned = dynamic_cast<CSummonedMeleeController*>(GetController());
            persist_after_owner_death = summoned && summoned->PersistsAfterOwnerDeath();
            owner = summoned ? summoned->GetOwner(world) : nullptr;
            if (owner && (owner->m_is_marked_for_des || owner->IsDead())) owner = nullptr;
        }

        auto spawned = CreateMob(hatch_type, world, m_pos, GetRarity());
        auto* mob = spawned ? dynamic_cast<CMobBase*>(spawned.get()) : nullptr;
        if (!mob) return;

        mob->m_team = m_team;
        mob->m_facing_angle = m_facing_angle;
        mob->m_has_facing = m_has_facing;
        if (preserve_owner)
        {
            if (owner)
            {
                auto controller = std::make_unique<CSummonedMeleeController>(owner);
                controller->SetPersistAfterOwnerDeath(persist_after_owner_death);
                mob->SetController(std::move(controller));
            } else
            {
                mob->SetController(std::make_unique<CMeleeController>());
            }
        }
        world->InsertEntity(std::move(spawned));
    }

    bool m_initialized = false;
    float m_auto_hatch_timer = -1.f;
};

class CQueenAntMob : public CAttackableBasicMob
{
  public:
    using CAttackableBasicMob::CAttackableBasicMob;

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("attack_timer", m_attack_timer);
        writer.Field("attack_windup_timer", m_attack_windup_timer);
        writer.Field("attack_recovery_timer", m_attack_recovery_timer);
        writer.Field("attack_target_id", m_attack_target_id);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CAttackableBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_attack_timer = reader.Float("attack_timer", m_attack_timer);
        m_attack_windup_timer = reader.Float("attack_windup_timer", m_attack_windup_timer);
        m_attack_recovery_timer = reader.Float("attack_recovery_timer", m_attack_recovery_timer);
        m_attack_target_id = reader.Int("attack_target_id", m_attack_target_id);
        return true;
    }

    void Tick(float dt) override
    {
        m_attack_timer = std::max(0.f, m_attack_timer - dt);
        m_attack_recovery_timer = std::max(0.f, m_attack_recovery_timer - dt);
        if (m_attack_windup_timer > 0.f)
        {
            m_attack_windup_timer -= dt;
            if (m_attack_windup_timer <= 0.f) SpawnQueuedAnt();
        }

        bool paused = m_attack_windup_timer > 0.f || m_attack_recovery_timer > 0.f;
        m_attacking = paused;
        if (paused)
        {
            m_vel = { 0.f, 0.f };
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
        if (CountOwnedSummonedSoldierAnts(GameWorld(), this) >= game_config::mob_queen_ant_max_summons) return false;

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
        if (CountOwnedSummonedSoldierAnts(world, this) >= game_config::mob_queen_ant_max_summons)
        {
            FinishAttack();
            return;
        }

        sf::Vector2f forward = { std::cos(m_facing_angle), std::sin(m_facing_angle) };
        if (LengthSq(forward) <= game_config::entity_collision_epsilon * game_config::entity_collision_epsilon)
            forward = { 1.f, 0.f };

        ERarity spawn_rarity = PreviousRarity(GetRarity());
        sf::Vector2f spawn_pos = m_pos - forward * (m_radius * game_config::mob_hornet_rear_spawn_radius_multiplier);
        EMobType egg_type =
            GetMobType() == EMobType::FireQueenAnt ? EMobType::QueenFireAntEgg : EMobType::QueenAntEgg;
        auto spawned = CreateMob(egg_type, world, spawn_pos, spawn_rarity);
        auto* egg = spawned ? dynamic_cast<CMobBase*>(spawned.get()) : nullptr;
        if (egg)
        {
            egg->m_team = m_team;
            egg->m_facing_angle = m_facing_angle;
            egg->m_has_facing = true;
            auto controller = std::make_unique<CSummonedMeleeController>(this);
            controller->SetPersistAfterOwnerDeath(true);
            egg->SetController(std::move(controller));
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
    CAntHoleMob(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
                const SMobStats& stats)
        : CBasicMob(pworld, pos, r, mob_type, rarity, stats)
    {
        m_mass = std::numeric_limits<float>::max();
    }

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        CJsonOwner queue = MakeJsonArray();
        for (const SQueuedSpawn& queued : m_spawn_queue)
        {
            CJsonOwner value = MakeJsonObject();
            CSnapshotWriter value_writer(value.get());
            value_writer.Field("type", static_cast<int>(queued.type));
            value_writer.Field("rarity", static_cast<int>(queued.rarity));
            AppendJson(queue.get(), value.release());
        }
        writer.Node("spawn_queue", queue.release());
        writer.UInt64("spawn_index", static_cast<std::uint64_t>(m_spawn_index));
        writer.Field("release_timer", m_release_timer);
        writer.UInt64("health_wave_index", static_cast<std::uint64_t>(m_health_wave_index));
        writer.Field("same_rarity_queen_queued", m_same_rarity_queen_queued);
        writer.Field("pending_death_cleanup", m_pending_death_cleanup);
        writer.Field("death_release_ticks", m_death_release_ticks);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_spawn_queue.clear();
        const json_t* queue = reader.Node("spawn_queue");
        if (json_is_array(queue))
        {
            const size_t count = json_array_size(queue);
            m_spawn_queue.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                CSnapshotReader value(json_array_get(queue, i));
                m_spawn_queue.push_back({ static_cast<EMobType>(value.Int("type", static_cast<int>(EMobType::None))),
                                          static_cast<ERarity>(value.Int("rarity", static_cast<int>(ERarity::Common))) });
            }
        }
        m_spawn_index = static_cast<size_t>(
            std::min<std::uint64_t>(reader.UInt64("spawn_index"), static_cast<std::uint64_t>(m_spawn_queue.size())));
        m_release_timer = reader.Float("release_timer", m_release_timer);
        m_health_wave_index = static_cast<size_t>(
            std::min<std::uint64_t>(reader.UInt64("health_wave_index"), HealthWaves().size()));
        m_same_rarity_queen_queued = reader.Bool("same_rarity_queen_queued", m_same_rarity_queen_queued);
        m_pending_death_cleanup = reader.Bool("pending_death_cleanup", m_pending_death_cleanup);
        m_death_release_ticks = std::max(0, reader.Int("death_release_ticks", m_death_release_ticks));
        return true;
    }

    void Tick(float dt) override
    {
        m_vel = { 0.f, 0.f };
        TickStates(dt);
        ReleaseQueuedSpawns(dt);
        if (m_pending_death_cleanup && m_spawn_index >= m_spawn_queue.size())
            MarkForDestroy(EEntityRemovalReason::Defeated);
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (m_pending_death_cleanup) return;
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        float before = m_health;
        ApplyDamageDirect(dmg, attacker);
        float dealt = std::max(0.f, before - m_health);
        if (dealt <= 0.f) return;

        if (m_is_marked_for_des)
        {
            CancelDestroy();
            m_health = 0.f;
            m_pending_death_cleanup = true;
            m_allow_skip_tick = false;
            QueueAllRemainingHealthWaves();
            if (IsAtLeastRarity(GetRarity(), ERarity::Super))
            {
                LOG_INFO("loot", "AntHole pending death id=" + std::to_string(m_id) +
                                     " rarity=" + std::string(GetRarityName(GetRarity())) +
                                     " recorded_damage=" + std::to_string(RecordedDamageTotal()) +
                                     " queued_spawns_left=" + std::to_string(m_spawn_queue.size() - m_spawn_index));
            }
        } else
        {
            QueueHealthTriggeredSpawns();
        }
    }

    bool CanPhysicallyCollideWith(const CEntity*) const override { return false; }
    bool IsDead() const override { return m_pending_death_cleanup || CBasicMob::IsDead(); }
    bool CanCollide() const override { return !m_pending_death_cleanup && CBasicMob::CanCollide(); }
    bool IsVisible() const override { return !m_pending_death_cleanup && CBasicMob::IsVisible(); }

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

    static std::array<SHealthWave, 4> HealthWaves()
    {
        return { {
            { game_config::mob_ant_hole_wave1_health_fraction, game_config::mob_ant_hole_wave1_baby_ants,
              game_config::mob_ant_hole_wave1_worker_ants, game_config::mob_ant_hole_wave1_soldier_ants, false },
            { game_config::mob_ant_hole_wave2_health_fraction, game_config::mob_ant_hole_wave2_baby_ants,
              game_config::mob_ant_hole_wave2_worker_ants, game_config::mob_ant_hole_wave2_soldier_ants, false },
            { game_config::mob_ant_hole_wave3_health_fraction, game_config::mob_ant_hole_wave3_baby_ants,
              game_config::mob_ant_hole_wave3_worker_ants, game_config::mob_ant_hole_wave3_soldier_ants, false },
            { game_config::mob_ant_hole_wave4_health_fraction, game_config::mob_ant_hole_wave4_baby_ants,
              game_config::mob_ant_hole_wave4_worker_ants, game_config::mob_ant_hole_wave4_soldier_ants,
              game_config::mob_ant_hole_wave4_spawn_queen },
        } };
    }

    float RecordedDamageTotal() const
    {
        float total = 0.f;
        for (const CDamageData& damage_data : GetDamageData())
        {
            if (damage_data.m_total_dmg > 0.f) total += damage_data.m_total_dmg;
        }
        return total;
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
        const float same_weight =
            allow_same ? std::max(game_config::mob_ant_hole_rarity_same_min_weight,
                                  game_config::mob_ant_hole_rarity_same_base_weight -
                                      game_config::mob_ant_hole_rarity_same_level_decay * static_cast<float>(level - 1))
                       : 0.f;
        const float low_one_weight =
            std::max(game_config::mob_ant_hole_rarity_low_one_min_weight,
                     game_config::mob_ant_hole_rarity_low_one_base_weight -
                         game_config::mob_ant_hole_rarity_low_one_level_decay * static_cast<float>(level - 1));
        const float low_two_weight = game_config::mob_ant_hole_rarity_low_two_weight;
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
            m_spawn_queue.push_back({ type, rarity });
    }

    void ReleaseQueuedSpawns(float dt)
    {
        if (!GameWorld() || m_spawn_index >= m_spawn_queue.size()) return;

        if (m_pending_death_cleanup)
        {
            int ticks_left = std::max(1, game_config::mob_ant_hole_death_release_ticks - m_death_release_ticks);
            size_t remaining = m_spawn_queue.size() - m_spawn_index;
            size_t release_limit = std::max<size_t>(1, (remaining + static_cast<size_t>(ticks_left) - 1) /
                                                           static_cast<size_t>(ticks_left));
            for (size_t released = 0; released < release_limit && m_spawn_index < m_spawn_queue.size(); ++released)
            {
                ReleaseSpawn(m_spawn_queue[m_spawn_index]);
                ++m_spawn_index;
            }
            ++m_death_release_ticks;
            return;
        }

        m_release_timer += dt;
        float interval = std::max(game_config::server_fixed_dt, game_config::mob_ant_hole_release_interval);
        int release_limit = std::max(1, game_config::mob_ant_hole_release_per_tick);
        int released = 0;
        while (m_release_timer >= interval && m_spawn_index < m_spawn_queue.size() && released < release_limit)
        {
            const SQueuedSpawn& queued = m_spawn_queue[m_spawn_index];
            m_release_timer -= interval;
            ReleaseSpawn(queued);
            ++m_spawn_index;
            ++released;
        }
    }

    void ReleaseSpawn(const SQueuedSpawn& queued)
    {
        if (queued.type == EMobType::None || !GameWorld()) return;

        float angle = GetLimitedRng(-game_config::pi, game_config::pi);
        sf::Vector2f dir = { std::cos(angle), std::sin(angle) };
        sf::Vector2f spawn_pos = m_pos + dir * (m_radius + game_config::mob_ant_hole_spawn_offset);
        auto mob = CreateMob(queued.type, GameWorld(), spawn_pos, queued.rarity);
        if (!mob) return;

        mob->m_team = m_team;
        if (auto* spawned = dynamic_cast<CMobBase*>(mob.get()))
            spawned->m_vel =
                dir * std::max(game_config::mob_ant_hole_spawn_velocity_min,
                               spawned->m_radius * game_config::mob_ant_hole_spawn_velocity_radius_multiplier);
        GameWorld()->InsertEntity(std::move(mob));
    }

    std::vector<SQueuedSpawn> m_spawn_queue;
    size_t m_spawn_index = 0;
    float m_release_timer = 0.f;
    size_t m_health_wave_index = 0;
    bool m_same_rarity_queen_queued = false;
    bool m_pending_death_cleanup = false;
    int m_death_release_ticks = 0;
};

class CRockAttackOnDamagedController : public IController
{
  public:
    void OnTick(CMobBase*, float) override {}
    std::string_view SnapshotKey() const override { return "controller.rock_attack_on_damaged"; }

    void OnDamaged(CMobBase* mob, CEntity* attacker) override
    {
        auto* attackable = dynamic_cast<IAttackableMob*>(mob);
        if (attackable) attackable->TryAttack(attacker);
    }
};

class CRockMob : public CAttackableBasicMob
{
  public:
    using CAttackableBasicMob::CAttackableBasicMob;

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("attack_cooldown_timer", m_attack_cooldown_timer);
        writer.Field("attack_flash_timer", m_attack_flash_timer);
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CAttackableBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_attack_cooldown_timer = reader.Float("attack_cooldown_timer", m_attack_cooldown_timer);
        m_attack_flash_timer = reader.Float("attack_flash_timer", m_attack_flash_timer);
        return true;
    }

    void Tick(float dt) override
    {
        CAttackableBasicMob::Tick(dt);
        m_attack_cooldown_timer = std::max(0.f, m_attack_cooldown_timer - dt);
        m_attack_flash_timer = std::max(0.f, m_attack_flash_timer - dt);
        m_attacking = m_attack_flash_timer > 0.f;
        m_defending = false;
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        float old_health = m_health;
        CAttackableBasicMob::TakeDamage(dmg, attacker, dmg_type);
        if (old_health > m_health && old_health > 0.f) TryAttack(attacker);
    }

    bool CanPhysicallyCollideWith(const CEntity* other) const override { return !IsHigherRarityThan(other); }

    bool TryAttack(CEntity*) override
    {
        if (!GameWorld()) return false;
        SRockAttackProfile profile;
        if (!GetAttackProfile(GetRarity(), profile)) return false;
        if (m_attack_cooldown_timer > 0.f) return false;

        SRockRarityCounts counts = CountExistingRocks(GameWorld());
        int spawn_count = RandomSpawnCount(profile);
        bool spawned = false;
        for (int i = 0; i < spawn_count; ++i)
        {
            ERarity rarity = DowngradeToAllowedRarity(PickSpawnRarity(profile), counts);
            if (TrySpawnRock(rarity))
            {
                IncrementRockCount(counts, rarity);
                spawned = true;
            }
        }

        if (!spawned) return false;
        m_attack_cooldown_timer = profile.cooldown;
        m_attack_flash_timer = std::max(game_config::server_fixed_dt, game_config::mob_rock_attack_flash_duration);
        m_attacking = true;
        return true;
    }

  private:
    struct SRockSpawnWeight
    {
        ERarity rarity = ERarity::Mythic;
        float weight = 0.f;
    };

    struct SRockAttackProfile
    {
        float cooldown = 1.f;
        int min_spawns = 1;
        int max_spawns = 1;
        std::array<SRockSpawnWeight, 4> weights{};
        int weight_count = 0;
    };

    struct SRockRarityCounts
    {
        int ultra = 0;
        int super = 0;
        int eternal = 0;
    };

    static bool GetAttackProfile(ERarity rarity, SRockAttackProfile& out)
    {
        switch (rarity)
        {
        case ERarity::Super:
            out.cooldown = game_config::mob_rock_attack_super_cooldown;
            out.min_spawns = game_config::mob_rock_attack_super_min_spawns;
            out.max_spawns = game_config::mob_rock_attack_super_max_spawns;
            out.weights = { SRockSpawnWeight{ ERarity::Mythic, game_config::mob_rock_attack_super_mythic_weight },
                            SRockSpawnWeight{ ERarity::Ultra, game_config::mob_rock_attack_super_ultra_weight },
                            SRockSpawnWeight{}, SRockSpawnWeight{} };
            out.weight_count = 2;
            return true;
        case ERarity::Eternal:
        case ERarity::Unique:
            out.cooldown = game_config::mob_rock_attack_eternal_cooldown;
            out.min_spawns = game_config::mob_rock_attack_eternal_min_spawns;
            out.max_spawns = game_config::mob_rock_attack_eternal_max_spawns;
            out.weights = { SRockSpawnWeight{ ERarity::Mythic, game_config::mob_rock_attack_eternal_mythic_weight },
                            SRockSpawnWeight{ ERarity::Ultra, game_config::mob_rock_attack_eternal_ultra_weight },
                            SRockSpawnWeight{ ERarity::Super, game_config::mob_rock_attack_eternal_super_weight },
                            SRockSpawnWeight{} };
            out.weight_count = 3;
            return true;
        case ERarity::Primordial:
            out.cooldown = game_config::mob_rock_attack_primordial_cooldown;
            out.min_spawns = game_config::mob_rock_attack_primordial_min_spawns;
            out.max_spawns = game_config::mob_rock_attack_primordial_max_spawns;
            out.weights = { SRockSpawnWeight{ ERarity::Mythic, game_config::mob_rock_attack_primordial_mythic_weight },
                            SRockSpawnWeight{ ERarity::Ultra, game_config::mob_rock_attack_primordial_ultra_weight },
                            SRockSpawnWeight{ ERarity::Super, game_config::mob_rock_attack_primordial_super_weight },
                            SRockSpawnWeight{ ERarity::Eternal,
                                              game_config::mob_rock_attack_primordial_eternal_weight } };
            out.weight_count = 4;
            return true;
        default:
            return false;
        }
    }

    static int RandomSpawnCount(const SRockAttackProfile& profile)
    {
        int min_spawns = std::min(profile.min_spawns, profile.max_spawns);
        int max_spawns = std::max(profile.min_spawns, profile.max_spawns);
        int range = max_spawns - min_spawns + 1;
        return min_spawns + std::clamp(static_cast<int>(GetLimitedRng(0.f, static_cast<float>(range))), 0, range - 1);
    }

    static ERarity PickSpawnRarity(const SRockAttackProfile& profile)
    {
        float total_weight = 0.f;
        for (int i = 0; i < profile.weight_count; ++i)
            total_weight += std::max(0.f, profile.weights[i].weight);
        if (total_weight <= 0.f) return ERarity::Mythic;

        float roll = GetLimitedRng(0.f, total_weight);
        for (int i = 0; i < profile.weight_count; ++i)
        {
            roll -= std::max(0.f, profile.weights[i].weight);
            if (roll <= 0.f) return profile.weights[i].rarity;
        }
        return profile.weights[std::max(0, profile.weight_count - 1)].rarity;
    }

    static SRockRarityCounts CountExistingRocks(CGameWorld* world)
    {
        SRockRarityCounts counts;
        if (!world) return counts;

        world->ForEachEntity([&](CEntity* entity) {
            const auto* mob = dynamic_cast<const CMobBase*>(entity);
            if (!mob || mob->m_is_marked_for_des || mob->IsDead()) return;
            if (mob->GetMobType() != EMobType::Rock) return;

            switch (mob->GetRarity())
            {
            case ERarity::Ultra:
                ++counts.ultra;
                break;
            case ERarity::Super:
                ++counts.super;
                break;
            case ERarity::Eternal:
                ++counts.eternal;
                break;
            default:
                break;
            }
        });
        return counts;
    }

    static bool CanAddRockRarity(ERarity rarity, const SRockRarityCounts& counts)
    {
        switch (rarity)
        {
        case ERarity::Ultra:
            return counts.ultra < game_config::mob_rock_ultra_cap;
        case ERarity::Super:
            return counts.super < game_config::mob_rock_super_cap;
        case ERarity::Eternal:
            return counts.eternal < game_config::mob_rock_eternal_cap;
        default:
            return true;
        }
    }

    static ERarity DowngradeOneRockRarity(ERarity rarity)
    {
        switch (rarity)
        {
        case ERarity::Primordial:
        case ERarity::Unique:
            return ERarity::Eternal;
        case ERarity::Eternal:
            return ERarity::Super;
        case ERarity::Super:
            return ERarity::Ultra;
        case ERarity::Exotic:
            return ERarity::Common;
        case ERarity::Ultra:
            return ERarity::Mythic;
        default:
            return ERarity::Mythic;
        }
    }

    static ERarity DowngradeToAllowedRarity(ERarity rarity, const SRockRarityCounts& counts)
    {
        while (!CanAddRockRarity(rarity, counts))
            rarity = DowngradeOneRockRarity(rarity);
        return rarity;
    }

    static void IncrementRockCount(SRockRarityCounts& counts, ERarity rarity)
    {
        switch (rarity)
        {
        case ERarity::Ultra:
            ++counts.ultra;
            break;
        case ERarity::Super:
            ++counts.super;
            break;
        case ERarity::Eternal:
            ++counts.eternal;
            break;
        default:
            break;
        }
    }

    bool TrySpawnRock(ERarity rarity)
    {
        CGameWorld* world = GameWorld();
        if (!world) return false;

        for (int attempt = 0; attempt < std::max(1, game_config::mob_rock_spawn_position_attempts); ++attempt)
        {
            float angle = GetLimitedRng(-game_config::pi, game_config::pi);
            float distance = GetLimitedRng(0.f, std::max(game_config::entity_collision_epsilon,
                                                         m_radius * game_config::mob_rock_spawn_radius_multiplier));
            sf::Vector2f pos = m_pos + sf::Vector2f(std::cos(angle), std::sin(angle)) * distance;

            auto rock = CreateMob(EMobType::Rock, world, pos, rarity);
            if (!rock) continue;
            rock->m_team = m_team;
            if (world->CircleBlockedByWall(pos, rock->WallCollisionRadius())) continue;

            return world->InsertEntity(std::move(rock)) != nullptr;
        }
        return false;
    }

    bool IsHigherRarityThan(const CEntity* other) const
    {
        int other_rank = CollisionRarityRank(other);
        return other_rank > 0 && GetRarityValueRank(GetRarity()) > other_rank;
    }

    static int CollisionRarityRank(const CEntity* entity)
    {
        if (const auto* mob = dynamic_cast<const CMobBase*>(entity)) return GetRarityValueRank(mob->GetRarity());
        if (const auto* petal = dynamic_cast<const CPetal*>(entity)) return GetRarityValueRank(petal->m_rarity);
        if (const auto* missile = dynamic_cast<const CDandelionMissile*>(entity))
            return GetRarityValueRank(missile->GetRarity());
        return 0;
    }

    float m_attack_cooldown_timer = 0.f;
    float m_attack_flash_timer = 0.f;
};

class CDummyMob : public CBasicMob
{
  public:
    CDummyMob(CGameWorld* pworld, sf::Vector2f pos, float r, EMobType mob_type, ERarity rarity,
              const SMobStats& stats)
        : CBasicMob(pworld, pos, r, mob_type, rarity, stats), m_anchor_pos(pos)
    {
        m_mass = std::numeric_limits<float>::max();
        m_health = DummyMaxHealth();
    }

    void CaptureRuntimeSnapshot(CSnapshotWriter& writer) const override
    {
        writer.Field("anchor_pos", m_anchor_pos);
        CJsonOwner reports = MakeJsonArray();
        for (const SDummyDamageReport& report : m_reports)
        {
            CJsonOwner value = MakeJsonObject();
            CSnapshotWriter value_writer(value.get());
            value_writer.Field("player_id", report.player_id);
            value_writer.Field("total_damage", report.total_damage);
            value_writer.Field("elapsed_time", report.elapsed_time);
            value_writer.Field("idle_time", report.idle_time);
            value_writer.Field("report_timer", report.report_timer);
            value_writer.Field("pending", report.pending);
            AppendJson(reports.get(), value.release());
        }
        writer.Node("reports", reports.release());
    }

    bool RestoreRuntimeSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override
    {
        if (!CBasicMob::RestoreRuntimeSnapshot(reader, version, error)) return false;
        m_anchor_pos = reader.Vector2("anchor_pos", m_anchor_pos);
        m_reports.clear();
        const json_t* reports = reader.Node("reports");
        if (!json_is_array(reports)) return true;

        const size_t count = json_array_size(reports);
        m_reports.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader value(json_array_get(reports, i));
            SDummyDamageReport report;
            report.player_id = value.UInt32("player_id");
            report.total_damage = value.Float("total_damage");
            report.elapsed_time = value.Float("elapsed_time");
            report.idle_time = value.Float("idle_time");
            report.report_timer = value.Float("report_timer");
            report.pending = value.Bool("pending");
            m_reports.push_back(report);
        }
        return true;
    }

    void Tick(float dt) override
    {
        m_pos = m_anchor_pos;
        m_prev_pos = m_anchor_pos;
        m_vel = { 0.f, 0.f };
        m_health = DummyMaxHealth();
        TickStates(dt);
        TickDamageReports(dt);
    }

    void TakeDamage(float dmg, CEntity* attacker, EDamageType dmg_type) override
    {
        if (dmg_type == EDamageType::Normal) dmg = std::max(0.f, dmg - m_base_stats.armor);
        if (dmg <= 0.f) return;
        if (TrySharePsionicDamage(this, dmg, attacker, dmg_type)) return;

        CGameContext* context = GameContext();
        CPlayer* player = context ? context->FindPlayerFromEntity(attacker) : nullptr;
        if (player && player->IsAuthenticated())
        {
            SDummyDamageReport& report = GetReport(player->GetId(), attacker);
            if (report.idle_time > game_config::mob_dummy_damage_session_timeout)
            {
                report.total_damage = 0.f;
                report.elapsed_time = 0.f;
                report.report_timer = DummyInitialReportDelay(attacker);
            }
            report.total_damage += dmg;
            report.idle_time = 0.f;
            report.pending = true;
        }

        if (m_p_controller) m_p_controller->OnDamaged(this, attacker);
        CancelDestroy();
        m_health = DummyMaxHealth();
    }

    bool CanPhysicallyCollideWith(const CEntity*) const override { return false; }

  private:
    struct SDummyDamageReport
    {
        uint32_t player_id = 0;
        float total_damage = 0.f;
        float elapsed_time = 0.f;
        float idle_time = 0.f;
        float report_timer = 0.f;
        bool pending = false;
    };

    float DummyMaxHealth() const
    {
        const SMobStats* stats = GetFinalStats();
        return stats ? std::max(1.f, stats->max_health) : game_config::mob_dummy_max_health;
    }

    SDummyDamageReport& GetReport(uint32_t player_id, const CEntity* attacker)
    {
        auto it = std::find_if(m_reports.begin(), m_reports.end(),
                               [player_id](const SDummyDamageReport& report) { return report.player_id == player_id; });
        if (it != m_reports.end()) return *it;

        SDummyDamageReport report;
        report.player_id = player_id;
        report.report_timer = DummyInitialReportDelay(attacker);
        m_reports.push_back(report);
        return m_reports.back();
    }

    void TickDamageReports(float dt)
    {
        for (SDummyDamageReport& report : m_reports)
        {
            report.elapsed_time += dt;
            report.idle_time += dt;
            if (report.report_timer > 0.f) report.report_timer = std::max(0.f, report.report_timer - dt);
            if (!report.pending || report.report_timer > 0.f) continue;

            const float elapsed = std::max(game_config::server_fixed_dt, report.elapsed_time);
            SendDummyDamageReport(report.player_id, report.total_damage, report.total_damage / elapsed);
            report.pending = false;
            report.report_timer = game_config::mob_dummy_damage_report_interval;
        }

        m_reports.erase(std::remove_if(m_reports.begin(), m_reports.end(),
                                       [](const SDummyDamageReport& report) {
                                           return report.idle_time > game_config::mob_dummy_damage_session_timeout &&
                                                  !report.pending;
                                       }),
                        m_reports.end());
    }

    sf::Vector2f m_anchor_pos;
    std::vector<SDummyDamageReport> m_reports;
};

class CLeafPieceMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void Tick(float dt) override
    {
        CBasicMob::Tick(dt);
        if (m_is_marked_for_des || m_health <= 0.f) return;

        const SMobStats* stats = GetFinalStats();
        if (!stats || stats->max_health <= 0.f) return;

        float regen = LeafPieceRegenPerSecond(GetRarity()) * GetMedicMultiplier(this);
        if (regen <= 0.f) return;

        m_health = std::min(stats->max_health, m_health + regen * dt);
    }
};

class CLeafcutterSoldierMob : public CBasicMob
{
  public:
    using CBasicMob::CBasicMob;

    void Tick(float dt) override
    {
        CBasicMob::Tick(dt);
        auto* controller = dynamic_cast<CLeafcutterSoldierController*>(GetController());
        if (controller) controller->SyncCarriedLeafPiece(this);
    }
};
} // namespace

bool RegisterMobs(std::string& error)
{
    CMobRegistry& registry = mob_registry_detail::MutableRegistry();
    if (registry.IsFrozen())
    {
        error.clear();
        return true;
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        RegisterMobPrototype<EMobType::Beetle, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        RegisterMobPrototype<EMobType::BandageBeetle, CBandageBeetleMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        RegisterMobPrototype<EMobType::Bee, CBeeMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_hornet_team;
        proto.m_base_stats.max_health = game_config::mob_hornet_max_health;
        proto.m_base_stats.armor = game_config::mob_hornet_armor;
        proto.m_base_stats.damage = game_config::mob_hornet_damage;
        proto.m_base_stats.radius = game_config::mob_hornet_radius;
        proto.m_base_stats.mass = game_config::mob_hornet_mass;
        proto.m_base_stats.horizon = game_config::mob_hornet_missile_speed * game_config::default_missile_lifetime *
                                     game_config::mob_hornet_horizon_lifetime_multiplier;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_hornet_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_hornet_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleHornetStats(base_stats, rarity);
        };
        proto.m_controller_factory = [](ERarity rarity) -> std::unique_ptr<IController> {
            if (IsAtLeastRarity(rarity, ERarity::Super))
                return std::make_unique<CSpecialHornetController>();
            return std::make_unique<CHornetRangedController>();
        };
        RegisterMobPrototype<EMobType::Hornet, CHornetMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_dandelion_team;
        proto.m_base_stats.max_health = game_config::mob_dandelion_max_health;
        proto.m_base_stats.armor = game_config::mob_dandelion_armor;
        proto.m_base_stats.damage = game_config::mob_dandelion_damage;
        proto.m_base_stats.radius = game_config::mob_dandelion_radius;
        proto.m_base_stats.mass = game_config::mob_dandelion_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_dandelion_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_dandelion_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        // Dandelions should stay passive, while their radius and mass still participate in physics.
        RegisterMobPrototype<EMobType::Dandelion, CDandelionMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CBumbleBeeController>(); };
        RegisterMobPrototype<EMobType::BumbleBee, CBeeMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
            stats.radius *=
                GetLimitedRng(game_config::mob_rock_radius_random_min, game_config::mob_rock_radius_random_max);
            return ScaleMobStats(stats, rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CRockAttackOnDamagedController>(); };
        RegisterMobPrototype<EMobType::Rock, CRockMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CRandomWanderController>(); };
        RegisterMobPrototype<EMobType::BabyAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        RegisterMobPrototype<EMobType::WorkerAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CQueenAntController>(); };
        RegisterMobPrototype<EMobType::QueenAnt, CQueenAntMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_ant_egg_team;
        proto.m_base_stats.max_health = game_config::mob_ant_egg_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_egg_armor;
        proto.m_base_stats.damage = game_config::mob_ant_egg_damage;
        proto.m_base_stats.radius = game_config::mob_ant_egg_radius;
        proto.m_base_stats.mass = game_config::mob_ant_egg_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleAntEggStats(base_stats, rarity);
        };
        RegisterMobPrototype<EMobType::AntEgg, CAntEggMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_fire_ant_team;
        proto.m_base_stats.max_health = game_config::mob_ant_egg_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_egg_armor;
        proto.m_base_stats.damage = game_config::mob_ant_egg_damage;
        proto.m_base_stats.radius = game_config::mob_ant_egg_radius;
        proto.m_base_stats.mass = game_config::mob_ant_egg_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleAntEggStats(base_stats, rarity);
        };
        RegisterMobPrototype<EMobType::FireAntEgg, CAntEggMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_termite_team;
        proto.m_base_stats.max_health = game_config::mob_ant_egg_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_egg_armor;
        proto.m_base_stats.damage = game_config::mob_ant_egg_damage;
        proto.m_base_stats.radius = game_config::mob_ant_egg_radius;
        proto.m_base_stats.mass = game_config::mob_ant_egg_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return TermiteStats(ScaleAntEggStats(base_stats, rarity));
        };
        proto.m_after_create = [](CMobBase& mob, ERarity rarity) {
            mob.AddState(std::make_unique<CPsionicConnectionState>(&mob, endless, rarity));
        };
        RegisterMobPrototype<EMobType::TermiteEgg, CAntEggMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_queen_ant_team;
        proto.m_base_stats.max_health = game_config::mob_ant_egg_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_egg_armor;
        proto.m_base_stats.damage = game_config::mob_ant_egg_damage;
        proto.m_base_stats.radius = game_config::mob_ant_egg_radius;
        proto.m_base_stats.mass = game_config::mob_ant_egg_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleAntEggStats(base_stats, rarity);
        };
        RegisterMobPrototype<EMobType::QueenAntEgg, CAntEggMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_fire_ant_team;
        proto.m_base_stats.max_health = game_config::mob_ant_egg_max_health;
        proto.m_base_stats.armor = game_config::mob_ant_egg_armor;
        proto.m_base_stats.damage = game_config::mob_ant_egg_damage;
        proto.m_base_stats.radius = game_config::mob_ant_egg_radius;
        proto.m_base_stats.mass = game_config::mob_ant_egg_mass;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleAntEggStats(base_stats, rarity);
        };
        RegisterMobPrototype<EMobType::QueenFireAntEgg, CAntEggMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_fire_ant_team;
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
            return ScaleMobStats(FireAntStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CRandomWanderController>(); };
        RegisterMobPrototype<EMobType::BabyFireAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_fire_ant_team;
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
            return ScaleMobStats(FireAntStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        RegisterMobPrototype<EMobType::WorkerFireAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_fire_ant_team;
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
            return ScaleMobStats(FireAntStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CQueenAntController>(); };
        RegisterMobPrototype<EMobType::FireQueenAnt, CQueenAntMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_termite_team;
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
            return ScaleMobStats(TermiteStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CRandomWanderController>(); };
        proto.m_after_create = [](CMobBase& mob, ERarity rarity) {
            mob.AddState(std::make_unique<CPsionicConnectionState>(&mob, endless, rarity));
        };
        RegisterMobPrototype<EMobType::BabyTermite, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_termite_team;
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
            return ScaleMobStats(TermiteStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        proto.m_after_create = [](CMobBase& mob, ERarity rarity) {
            mob.AddState(std::make_unique<CPsionicConnectionState>(&mob, endless, rarity));
        };
        RegisterMobPrototype<EMobType::WorkerTermite, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_termite_team;
        proto.m_base_stats.max_health = game_config::mob_queen_ant_max_health;
        proto.m_base_stats.armor = game_config::mob_queen_ant_armor;
        proto.m_base_stats.damage = game_config::mob_queen_ant_damage;
        proto.m_base_stats.radius =
            game_config::mob_soldier_ant_radius * game_config::mob_termite_overmind_radius_multiplier;
        proto.m_base_stats.mass =
            game_config::mob_soldier_ant_mass * game_config::mob_termite_overmind_radius_multiplier;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity =
            game_config::default_max_velocity * game_config::mob_termite_overmind_velocity_multiplier;
        proto.m_base_stats.acceleration =
            game_config::default_acceleration * game_config::mob_termite_overmind_velocity_multiplier;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(TermiteStats(base_stats), rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        proto.m_after_create = [](CMobBase& mob, ERarity rarity) {
            mob.AddState(std::make_unique<CPsionicConnectionState>(&mob, endless, rarity));
        };
        RegisterMobPrototype<EMobType::TermiteOvermind, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_leaf_piece_team;
        proto.m_base_stats.max_health = game_config::mob_leaf_piece_health;
        proto.m_base_stats.armor = game_config::mob_leaf_piece_armor;
        proto.m_base_stats.damage = game_config::mob_leaf_piece_damage;
        proto.m_base_stats.radius = game_config::mob_leaf_piece_radius;
        proto.m_base_stats.mass = game_config::mob_soldier_ant_mass * game_config::mob_leaf_piece_mass_multiplier;
        proto.m_base_stats.horizon = 0.f;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            SMobStats stats = base_stats;
            float min_scale =
                std::min(game_config::mob_leaf_piece_radius_scale_min, game_config::mob_leaf_piece_radius_scale_max);
            float max_scale =
                std::max(game_config::mob_leaf_piece_radius_scale_min, game_config::mob_leaf_piece_radius_scale_max);
            stats.radius *= GetLimitedRng(min_scale, max_scale);
            return ScaleMobStats(stats, rarity);
        };
        RegisterMobPrototype<EMobType::LeafPiece, CLeafPieceMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        RegisterMobPrototype<EMobType::AntHole, CAntHoleMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CSpiderController>(); };
        RegisterMobPrototype<EMobType::Spider, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_sandstorm_team;
        proto.m_base_stats.max_health = game_config::mob_sandstorm_max_health;
        proto.m_base_stats.armor = game_config::mob_sandstorm_armor;
        proto.m_base_stats.damage = game_config::mob_sandstorm_damage;
        proto.m_base_stats.radius = game_config::mob_sandstorm_radius;
        proto.m_base_stats.mass = game_config::mob_sandstorm_mass;
        proto.m_base_stats.horizon = game_config::default_horizon;
        proto.m_base_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_stats.max_velocity = game_config::mob_sandstorm_max_velocity;
        proto.m_base_stats.acceleration = game_config::mob_sandstorm_acceleration;
        proto.m_stats_factory = [base_stats = proto.m_base_stats](ERarity rarity) {
            return ScaleMobStats(base_stats, rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CRandomWanderController>(); };
        RegisterMobPrototype<EMobType::Sandstorm, CBasicMob>(std::move(proto));
    }

    {
        SMobStats soldier_ant_stats;
        soldier_ant_stats.max_health = game_config::mob_soldier_ant_max_health;
        soldier_ant_stats.armor = game_config::mob_soldier_ant_armor;
        soldier_ant_stats.damage = game_config::mob_soldier_ant_damage;
        soldier_ant_stats.radius = game_config::mob_soldier_ant_radius;
        soldier_ant_stats.mass = game_config::mob_soldier_ant_mass;
        soldier_ant_stats.horizon = game_config::default_horizon;
        soldier_ant_stats.max_absorb_range = game_config::default_absorb_range;
        soldier_ant_stats.max_velocity = game_config::default_max_velocity;
        soldier_ant_stats.acceleration = game_config::default_acceleration;

        CMobPrototype proto;
        proto.m_team = game_config::mob_soldier_ant_team;
        proto.m_base_stats = soldier_ant_stats;
        proto.m_base_stats.max_health = game_config::mob_dummy_max_health;
        proto.m_base_stats.mass = std::numeric_limits<float>::max();
        proto.m_base_stats.max_velocity = 0.f;
        proto.m_base_stats.acceleration = 0.f;
        proto.m_stats_factory = [soldier_ant_stats](ERarity rarity) {
            SMobStats stats = ScaleMobStats(soldier_ant_stats, rarity);
            stats.max_health = game_config::mob_dummy_max_health;
            stats.mass = std::numeric_limits<float>::max();
            stats.max_velocity = 0.f;
            stats.acceleration = 0.f;
            return stats;
        };
        proto.m_rarity_resolver = [](CGameWorld& world, sf::Vector2f pos, ERarity) {
            return world.GetSpawnZoneRarity(pos);
        };
        RegisterMobPrototype<EMobType::Dummy, CDummyMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        RegisterMobPrototype<EMobType::NormalLadybug, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity) { return base_stats; };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CPlayerController>(); };
        proto.m_allow_skip_tick = false;
        RegisterMobPrototype<EMobType::PlayerFlower, CPlayerFlower>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_normal_flower_team;
        proto.m_base_flower_stats = NormalFlowerBaseStats();
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity rarity) {
            return ScaleFlowerStats(base_stats, rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMechaFlowerRangedController>(); };
        RegisterMobPrototype<EMobType::MechaFlower, CMechaFlower>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::mob_normal_flower_team;
        proto.m_base_flower_stats = NormalFlowerBaseStats();
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity rarity) {
            return ScaleFlowerStats(base_stats, rarity);
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        RegisterMobPrototype<EMobType::NormalFlower, CNormalFlower>(std::move(proto));
    }

    {
        CMobPrototype proto;
        proto.m_team = game_config::titan_team;
        proto.m_base_flower_stats.max_health = game_config::mob_soldier_ant_max_health;
        proto.m_base_flower_stats.armor = game_config::mob_soldier_ant_armor;
        proto.m_base_flower_stats.damage = game_config::mob_soldier_ant_damage;
        proto.m_base_flower_stats.radius = game_config::mob_soldier_ant_radius;
        proto.m_base_flower_stats.mass = game_config::mob_soldier_ant_mass;
        proto.m_base_flower_stats.horizon = game_config::default_horizon;
        proto.m_base_flower_stats.max_absorb_range = game_config::default_absorb_range;
        proto.m_base_flower_stats.max_velocity = game_config::default_max_velocity;
        proto.m_base_flower_stats.acceleration = game_config::default_acceleration;
        proto.m_base_flower_stats.petal_rotation_speed = game_config::mob_normal_flower_petal_rotation_speed;
        proto.m_flower_stats_factory = [base_stats = proto.m_base_flower_stats](ERarity rarity) {
            SFlowerStats stats = ScaleFlowerStats(base_stats, rarity);
            const float multiplier = std::max(0.f, game_config::titan_stats_multiplier);
            stats.max_health *= multiplier;
            stats.armor *= multiplier;
            stats.damage *= multiplier;
            stats.mass *= multiplier;
            stats.horizon *= multiplier;
            stats.max_absorb_range *= multiplier;
            stats.max_velocity *= multiplier;
            stats.acceleration *= multiplier;
            stats.turn_speed *= multiplier;
            return stats;
        };
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CNeutralMeleeController>(); };
        proto.m_rarity_resolver = [](CGameWorld& world, sf::Vector2f pos, ERarity) {
            return world.GetSpawnZoneRarity(pos);
        };
        proto.m_allow_skip_tick = false;
        RegisterMobPrototype<EMobType::Titan, CTitanFlower>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        RegisterMobPrototype<EMobType::SummonedBeetle, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        RegisterMobPrototype<EMobType::SoldierAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CLeafcutterSoldierController>(); };
        RegisterMobPrototype<EMobType::LeafcutterSoldier, CLeafcutterSoldierMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        RegisterMobPrototype<EMobType::SoldierFireAnt, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        proto.m_controller_factory = [](ERarity) { return std::make_unique<CMeleeController>(); };
        proto.m_after_create = [](CMobBase& mob, ERarity rarity) {
            mob.AddState(std::make_unique<CPsionicConnectionState>(&mob, endless, rarity));
        };
        RegisterMobPrototype<EMobType::SoldierTermite, CBasicMob>(std::move(proto));
    }

    {
        CMobPrototype proto;
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
        RegisterMobPrototype<EMobType::SummonedSoldierAnt, CBasicMob>(std::move(proto));
    }

    registry.MarkUnsupported(EMobType::Gambler, "Reserved type with no implementation");
    return registry.Finalize(error);
}
