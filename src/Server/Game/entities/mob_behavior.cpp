#include "../controllers/melee_controller.h"
#include "../controllers/player_controller.h"
#include "../states/states.h"
#include "flower.h"
#include "mob.h"
#include <cmath>
#include <map>
#include <mutex>

namespace
{
using CBasicMob = CMob<SMobStats>;

std::once_flag g_beetle_registered;
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

SFlowerStats ScaleFlowerStats(SFlowerStats stats, ERarity rarity)
{
    static_cast<SMobStats&>(stats) = ScaleMobStats(stats, rarity);
    return stats;
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
            return ScaleMobStats(base_stats, rarity);
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
            return ScaleMobStats(base_stats, rarity);
        };
        REGISTER_MOB(EMobType::SummonedSoldierAnt, CBasicMob, proto);
    });
}
