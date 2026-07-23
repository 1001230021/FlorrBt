#pragma once

#include "../../Shared/shared.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

class CEntity;
class CFlower;
class CGameWorld;
class CPlayer;
class CPlayerFlower;
class CPetal;
class CPetalSlot;

struct STalentContext
{
    CGameWorld* world = nullptr;
    CPlayer* player = nullptr;
    CEntity* entity = nullptr;
    CEntity* target = nullptr;
    CEntity* attacker = nullptr;
    CFlower* flower = nullptr;
    CPlayerFlower* player_flower = nullptr;
    CPetalSlot* slot = nullptr;
    CPetal* petal = nullptr;

    SFlowerStats* flower_stats = nullptr;
    SPetalStats* petal_stats = nullptr;
    int* slot_num = nullptr;
    bool is_leftmost_slot = false;

    EPetalType petal_type = EPetalType::None;
    ERarity rarity = ERarity::Null;
    EDamageType damage_type = EDamageType::Normal;

    float* damage = nullptr;
    float* poison_damage = nullptr;
    float* invincible_time = nullptr;
    float* cooldown = nullptr;
    bool* prevent_death = nullptr;
    float dt = 0.f;
};

class ITalent
{
  public:
    ITalent() = default;
    ITalent(ETalentId id, ERarity rarity, int cost, ITalent* based = nullptr, int rank = 0)
        : m_id(id), m_rarity(rarity), m_cost(cost), m_rank(rank), m_based(based)
    {
    }
    virtual ~ITalent() = default;

    virtual void Apply(ETalentEvent event, STalentContext& ctx) = 0;

    ETalentId m_id = ETalentId::None;
    ERarity m_rarity = ERarity::Null;
    int m_cost = 1;
    int m_rank = 0;
    ITalent* m_based = nullptr;
};

inline constexpr int talent_tier_count = 9;
inline constexpr int sharp_edges_tier_count = 8;
inline constexpr int short_talent_tier_count = 5;
inline constexpr int antennae_tier_count = 3;
inline constexpr int reach_tier_count = 3;
inline constexpr int second_chance_tier_count = 2;
inline constexpr int magnetism_tier_count = 1;
inline constexpr int body_toxicity_tier_count = 1;
inline constexpr int duplicator_tier_count = 4;
inline constexpr int poison_tier_count = 9;
inline constexpr int concentrated_poison_tier_count = 1;
inline constexpr int movement_tier_count = 7;
inline constexpr std::array<ERarity, talent_tier_count> talent_tier_rarities = {
    ERarity::Common,
    ERarity::Unusual,
    ERarity::Rare,
    ERarity::Epic,
    ERarity::Legendary,
    ERarity::Mythic,
    ERarity::Ultra,
    ERarity::Super,
    ERarity::Eternal,
};

class CFlowerHealthTalent final : public ITalent
{
  public:
    CFlowerHealthTalent() = default;
    CFlowerHealthTalent(ERarity rarity, int cost, float total_multiplier)
        : ITalent(ETalentId::FlowerHealth, rarity, cost), m_total_multiplier(total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->max_health *= m_step_multiplier;
        ctx.flower_stats->max_health_multiplier *= m_step_multiplier;
    }

    float m_step_multiplier = 1.3f;
    float m_total_multiplier = 1.3f;
};

class CBodyDamageTalent final : public ITalent
{
  public:
    CBodyDamageTalent() = default;
    CBodyDamageTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::BodyDamage, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->damage *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CMovementTalent final : public ITalent
{
  public:
    CMovementTalent() = default;
    CMovementTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::Movement, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->max_velocity *= m_step_multiplier;
        ctx.flower_stats->acceleration *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CBodyToxicityTalent final : public ITalent
{
  public:
    CBodyToxicityTalent() = default;
    CBodyToxicityTalent(ERarity rarity, int cost, float damage_multiplier, float duration)
        : ITalent(ETalentId::BodyDamagePoison, rarity, cost), m_damage_multiplier(damage_multiplier),
          m_duration(duration)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->body_poison_damage_multiplier =
            std::max(ctx.flower_stats->body_poison_damage_multiplier, m_damage_multiplier);
        ctx.flower_stats->body_poison_duration = std::max(ctx.flower_stats->body_poison_duration, m_duration);
    }

    float m_damage_multiplier = 0.f;
    float m_duration = 0.f;
};

class CPetalHealthTalent final : public ITalent
{
  public:
    CPetalHealthTalent() = default;
    CPetalHealthTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::PetalHealth, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->petal_health_multiplier *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CMedicTalent final : public ITalent
{
  public:
    CMedicTalent() = default;
    CMedicTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::Medic, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->petal_medicine_multiplier *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CPetalReloadTalent final : public ITalent
{
  public:
    CPetalReloadTalent() = default;
    CPetalReloadTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier,
                       float swap_min_reload)
        : ITalent(ETalentId::PetalReload, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier),
          m_swap_min_reload(swap_min_reload)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->petal_reload_multiplier *= m_step_multiplier;
        ctx.flower_stats->petal_swap_min_reload = std::min(ctx.flower_stats->petal_swap_min_reload, m_swap_min_reload);
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
    float m_swap_min_reload = 2.5f;
};

class CPetalRotationTalent final : public ITalent
{
  public:
    CPetalRotationTalent() = default;
    CPetalRotationTalent(ERarity rarity, int cost, float rotation_speed)
        : ITalent(ETalentId::PetalRotationSpeed, rarity, cost), m_rotation_speed(rotation_speed)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->petal_rotation_speed = std::max(ctx.flower_stats->petal_rotation_speed, m_rotation_speed);
    }

    float m_rotation_speed = 0.f;
};

class CSlotNumTalent final : public ITalent
{
  public:
    CSlotNumTalent() = default;
    CSlotNumTalent(ERarity rarity, int cost, int slot_num)
        : ITalent(ETalentId::SlotNum, rarity, cost), m_slot_num(slot_num)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildSlotNum || !ctx.slot_num) return;
        *ctx.slot_num = std::max(*ctx.slot_num, m_slot_num);
    }

    int m_slot_num = 5;
};

class CReachTalent final : public ITalent
{
  public:
    CReachTalent() = default;
    CReachTalent(ERarity rarity, int cost, float total_reach_bonus)
        : ITalent(ETalentId::Reach, rarity, cost), m_total_reach_bonus(total_reach_bonus)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->reach = std::max(ctx.flower_stats->reach, m_total_reach_bonus);
    }

    float m_total_reach_bonus = 0.f;
};

class CMagnetismTalent final : public ITalent
{
  public:
    CMagnetismTalent() = default;
    CMagnetismTalent(ERarity rarity, int cost, float absorb_range_bonus)
        : ITalent(ETalentId::Magnetism, rarity, cost), m_absorb_range_bonus(absorb_range_bonus)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->max_absorb_range += m_absorb_range_bonus;
    }

    float m_absorb_range_bonus = 0.f;
};

class CSummonerTalent final : public ITalent
{
  public:
    CSummonerTalent() = default;
    CSummonerTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::Summoner, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->mult_summoned_health *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CAntennaeTalent final : public ITalent
{
  public:
    CAntennaeTalent() = default;
    CAntennaeTalent(ERarity rarity, int cost, float total_multiplier, float previous_total_multiplier)
        : ITalent(ETalentId::Antennae, rarity, cost), m_total_multiplier(total_multiplier),
          m_step_multiplier(previous_total_multiplier > 0.f ? total_multiplier / previous_total_multiplier : total_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->horizon *= m_step_multiplier;
    }

    float m_total_multiplier = 1.f;
    float m_step_multiplier = 1.f;
};

class CPetalSplitTalent final : public ITalent
{
  public:
    enum class Mode
    {
        MultiCopy,
        LeftmostNonEternalUnique,
        LeftmostEternalUnique
    };

    CPetalSplitTalent() = default;
    CPetalSplitTalent(ERarity rarity, int cost, Mode mode)
        : ITalent(ETalentId::PetalSplit, rarity, cost), m_mode(mode)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildPetalStats || !ctx.petal_stats) return;
        if (!ctx.petal_stats->stack || ctx.petal_stats->copy <= 0) return;

        switch (m_mode)
        {
        case Mode::MultiCopy:
            if (ctx.petal_stats->copy >= 2) ++ctx.petal_stats->copy;
            break;
        case Mode::LeftmostNonEternalUnique:
            if (ctx.is_leftmost_slot && ctx.rarity != ERarity::Eternal && ctx.rarity != ERarity::Unique)
                ++ctx.petal_stats->copy;
            break;
        case Mode::LeftmostEternalUnique:
            if (ctx.is_leftmost_slot && (ctx.rarity == ERarity::Eternal || ctx.rarity == ERarity::Unique))
                ++ctx.petal_stats->copy;
            break;
        }
    }

    Mode m_mode = Mode::MultiCopy;
};

class CPoisonDamageTalent final : public ITalent
{
  public:
    CPoisonDamageTalent() = default;
    CPoisonDamageTalent(ERarity rarity, int cost, int rank, float damage_bonus)
        : ITalent(ETalentId::PoisonDamage, rarity, cost, nullptr, rank), m_damage_bonus(damage_bonus)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->poison_damage_multiplier += m_damage_bonus;
    }

    float m_damage_bonus = 0.f;
};

class CConcentratedPoisonTalent final : public ITalent
{
  public:
    CConcentratedPoisonTalent() = default;
    CConcentratedPoisonTalent(ERarity rarity, int cost, float damage_multiplier, float duration_multiplier)
        : ITalent(ETalentId::ConcentratedPoison, rarity, cost), m_damage_multiplier(damage_multiplier),
          m_duration_multiplier(duration_multiplier)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::RebuildFlowerStats || !ctx.flower_stats) return;
        ctx.flower_stats->poison_damage_multiplier *= m_damage_multiplier;
        ctx.flower_stats->poison_duration_multiplier *= m_duration_multiplier;
    }

    float m_damage_multiplier = 1.f;
    float m_duration_multiplier = 1.f;
};

class CSecondChanceTalent final : public ITalent
{
  public:
    CSecondChanceTalent() = default;
    CSecondChanceTalent(ERarity rarity, int cost, float invincible_duration, float cooldown)
        : ITalent(ETalentId::SecondChance, rarity, cost), m_invincible_duration(invincible_duration),
          m_cooldown(cooldown)
    {
    }

    void Apply(ETalentEvent event, STalentContext& ctx) override
    {
        if (event != ETalentEvent::OnFlowerFatalDamage || !ctx.prevent_death || !ctx.invincible_time) return;
        if (ctx.cooldown && *ctx.cooldown > 0.f && !*ctx.prevent_death) return;

        bool already_preventing = *ctx.prevent_death;

        *ctx.prevent_death = true;
        *ctx.invincible_time = std::max(*ctx.invincible_time, m_invincible_duration);
        if (ctx.cooldown) *ctx.cooldown = already_preventing ? std::min(*ctx.cooldown, m_cooldown) : m_cooldown;
    }

    float m_invincible_duration = 0.f;
    float m_cooldown = 0.f;
};

template <typename TTalent, size_t N>
inline bool LinkTalentChain(std::array<TTalent, N>& talents)
{
    for (size_t i = 1; i < talents.size(); ++i)
        talents[i].m_based = &talents[i - 1];
    return true;
}

template <typename TTalent, size_t N>
inline ITalent* FindTalentByRarity(std::array<TTalent, N>& talents, ERarity rarity, int rank = 0)
{
    for (auto& talent : talents)
    {
        if (talent.m_rarity == rarity && talent.m_rank == rank) return &talent;
    }
    return nullptr;
}

inline std::array<CFlowerHealthTalent, talent_tier_count>& FlowerHealthTalents()
{
    static std::array<CFlowerHealthTalent, talent_tier_count> talents = {
        CFlowerHealthTalent(ERarity::Common, 2, 1.3f),
        CFlowerHealthTalent(ERarity::Unusual, 5, 1.69f),
        CFlowerHealthTalent(ERarity::Rare, 8, 2.197f),
        CFlowerHealthTalent(ERarity::Epic, 11, 2.8561f),
        CFlowerHealthTalent(ERarity::Legendary, 14, 3.71293f),
        CFlowerHealthTalent(ERarity::Mythic, 17, 4.826809f),
        CFlowerHealthTalent(ERarity::Ultra, 20, 6.274852f),
        CFlowerHealthTalent(ERarity::Super, 21, 8.157307f),
        CFlowerHealthTalent(ERarity::Eternal, 24, 10.604499f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CBodyDamageTalent, sharp_edges_tier_count>& BodyDamageTalents()
{
    static std::array<CBodyDamageTalent, sharp_edges_tier_count> talents = {
        CBodyDamageTalent(ERarity::Common, 2, 1.38f, 1.f),
        CBodyDamageTalent(ERarity::Unusual, 3, 1.9044f, 1.38f),
        CBodyDamageTalent(ERarity::Rare, 4, 2.628072f, 1.9044f),
        CBodyDamageTalent(ERarity::Epic, 5, 3.626739f, 2.628072f),
        CBodyDamageTalent(ERarity::Legendary, 6, 5.0049f, 3.626739f),
        CBodyDamageTalent(ERarity::Mythic, 7, 6.906762f, 5.0049f),
        CBodyDamageTalent(ERarity::Ultra, 8, 9.531331f, 6.906762f),
        CBodyDamageTalent(ERarity::Super, 9, 13.153236f, 9.531331f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CMovementTalent, movement_tier_count>& MovementTalents()
{
    static std::array<CMovementTalent, movement_tier_count> talents = {
        CMovementTalent(ERarity::Rare, 3, 1.1f, 1.f),
        CMovementTalent(ERarity::Epic, 4, 1.2f, 1.1f),
        CMovementTalent(ERarity::Legendary, 5, 1.3f, 1.2f),
        CMovementTalent(ERarity::Mythic, 6, 1.4f, 1.3f),
        CMovementTalent(ERarity::Ultra, 7, 1.5f, 1.4f),
        CMovementTalent(ERarity::Super, 9, 1.75f, 1.5f),
        CMovementTalent(ERarity::Eternal, 13, 2.f, 1.75f),
    };
    static const bool linked = []()
    {
        auto& health = FlowerHealthTalents();
        talents[0].m_based = &health[1];
        for (size_t i = 1; i < talents.size(); ++i)
            talents[i].m_based = &talents[i - 1];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CBodyToxicityTalent, body_toxicity_tier_count>& BodyToxicityTalents()
{
    static std::array<CBodyToxicityTalent, body_toxicity_tier_count> talents = {
        CBodyToxicityTalent(ERarity::Ultra, 10, 0.8f, 1.f),
    };
    static const bool linked = []()
    {
        auto& sharp_edges = BodyDamageTalents();
        talents[0].m_based = &sharp_edges[5];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CPetalHealthTalent, talent_tier_count>& PetalHealthTalents()
{
    static std::array<CPetalHealthTalent, talent_tier_count> talents = {
        CPetalHealthTalent(ERarity::Common, 2, 1.15f, 1.f),
        CPetalHealthTalent(ERarity::Unusual, 4, 1.322f, 1.15f),
        CPetalHealthTalent(ERarity::Rare, 6, 1.521f, 1.322f),
        CPetalHealthTalent(ERarity::Epic, 8, 1.749f, 1.521f),
        CPetalHealthTalent(ERarity::Legendary, 10, 2.011f, 1.749f),
        CPetalHealthTalent(ERarity::Mythic, 12, 2.313f, 2.011f),
        CPetalHealthTalent(ERarity::Ultra, 14, 2.66f, 2.313f),
        CPetalHealthTalent(ERarity::Super, 16, 3.059f, 2.66f),
        CPetalHealthTalent(ERarity::Eternal, 18, 4.046f, 3.059f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CMedicTalent, talent_tier_count>& MedicTalents()
{
    static std::array<CMedicTalent, talent_tier_count> talents = {
        CMedicTalent(ERarity::Common, 2, 1.15f, 1.f),
        CMedicTalent(ERarity::Unusual, 5, 1.322f, 1.15f),
        CMedicTalent(ERarity::Rare, 8, 1.521f, 1.322f),
        CMedicTalent(ERarity::Epic, 11, 1.749f, 1.521f),
        CMedicTalent(ERarity::Legendary, 14, 2.011f, 1.749f),
        CMedicTalent(ERarity::Mythic, 17, 2.313f, 2.011f),
        CMedicTalent(ERarity::Ultra, 20, 2.66f, 2.313f),
        CMedicTalent(ERarity::Super, 23, 3.059f, 2.66f),
        CMedicTalent(ERarity::Eternal, 26, 4.046f, 3.059f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CSummonerTalent, talent_tier_count>& SummonerTalents()
{
    static std::array<CSummonerTalent, talent_tier_count> talents = {
        CSummonerTalent(ERarity::Common, 2, 1.1f, 1.f),
        CSummonerTalent(ERarity::Unusual, 5, 1.21f, 1.1f),
        CSummonerTalent(ERarity::Rare, 8, 1.331f, 1.21f),
        CSummonerTalent(ERarity::Epic, 11, 1.4641f, 1.331f),
        CSummonerTalent(ERarity::Legendary, 14, 1.61051f, 1.4641f),
        CSummonerTalent(ERarity::Mythic, 17, 1.771561f, 1.61051f),
        CSummonerTalent(ERarity::Ultra, 20, 1.948717f, 1.771561f),
        CSummonerTalent(ERarity::Super, 23, 2.143589f, 1.948717f),
        CSummonerTalent(ERarity::Eternal, 26, 2.593742f, 2.143589f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CPetalReloadTalent, talent_tier_count>& PetalReloadTalents()
{
    static std::array<CPetalReloadTalent, talent_tier_count> talents = {
        CPetalReloadTalent(ERarity::Common, 2, 0.9f, 1.f, 2.f),
        CPetalReloadTalent(ERarity::Unusual, 6, 0.81f, 0.9f, 1.5f),
        CPetalReloadTalent(ERarity::Rare, 10, 0.729f, 0.81f, 1.f),
        CPetalReloadTalent(ERarity::Epic, 12, 0.656f, 0.729f, 0.5f),
        CPetalReloadTalent(ERarity::Legendary, 16, 0.525f, 0.656f, 0.f),
        CPetalReloadTalent(ERarity::Mythic, 20, 0.42f, 0.525f, 0.f),
        CPetalReloadTalent(ERarity::Ultra, 24, 0.336f, 0.42f, 0.f),
        CPetalReloadTalent(ERarity::Super, 28, 0.269f, 0.336f, 0.f),
        CPetalReloadTalent(ERarity::Eternal, 32, 0.188f, 0.269f, 0.f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CPetalRotationTalent, short_talent_tier_count>& PetalRotationTalents()
{
    static std::array<CPetalRotationTalent, short_talent_tier_count> talents = {
        CPetalRotationTalent(ERarity::Common, 1, 3.1f),
        CPetalRotationTalent(ERarity::Unusual, 2, 3.7f),
        CPetalRotationTalent(ERarity::Rare, 3, 4.3f),
        CPetalRotationTalent(ERarity::Epic, 4, 4.9f),
        CPetalRotationTalent(ERarity::Legendary, 5, 5.5f),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CSlotNumTalent, short_talent_tier_count>& SlotNumTalents()
{
    static std::array<CSlotNumTalent, short_talent_tier_count> talents = {
        CSlotNumTalent(ERarity::Common, 3, 6),
        CSlotNumTalent(ERarity::Unusual, 6, 7),
        CSlotNumTalent(ERarity::Rare, 9, 8),
        CSlotNumTalent(ERarity::Epic, 12, 9),
        CSlotNumTalent(ERarity::Legendary, 15, 10),
    };
    static const bool linked = LinkTalentChain(talents);
    (void)linked;
    return talents;
}

inline std::array<CAntennaeTalent, antennae_tier_count>& AntennaeTalents()
{
    static std::array<CAntennaeTalent, antennae_tier_count> talents = {
        CAntennaeTalent(ERarity::Rare, 1, 1.25f, 1.f),
        CAntennaeTalent(ERarity::Epic, 3, 1.5f, 1.25f),
        CAntennaeTalent(ERarity::Legendary, 5, 2.f, 1.5f),
    };
    static const bool linked = []()
    {
        auto& slots = SlotNumTalents();
        talents[0].m_based = &slots[0];
        talents[1].m_based = &talents[0];
        talents[2].m_based = &talents[1];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CPetalSplitTalent, duplicator_tier_count>& PetalSplitTalents()
{
    static std::array<CPetalSplitTalent, duplicator_tier_count> talents = {
        CPetalSplitTalent(ERarity::Legendary, 10, CPetalSplitTalent::Mode::MultiCopy),
        CPetalSplitTalent(ERarity::Ultra, 20, CPetalSplitTalent::Mode::MultiCopy),
        CPetalSplitTalent(ERarity::Super, 30, CPetalSplitTalent::Mode::LeftmostNonEternalUnique),
        CPetalSplitTalent(ERarity::Eternal, 40, CPetalSplitTalent::Mode::LeftmostEternalUnique),
    };
    static const bool linked = []()
    {
        auto& slots = SlotNumTalents();
        talents[0].m_based = &slots[3];
        talents[1].m_based = &talents[0];
        talents[2].m_based = &talents[1];
        talents[3].m_based = &talents[2];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CReachTalent, reach_tier_count>& ReachTalents()
{
    static std::array<CReachTalent, reach_tier_count> talents = {
        CReachTalent(ERarity::Rare, 3, 64.f),
        CReachTalent(ERarity::Mythic, 6, 128.f),
        CReachTalent(ERarity::Super, 9, 192.f),
    };
    static const bool linked = []()
    {
        auto& slots = SlotNumTalents();
        talents[0].m_based = &slots[1];
        talents[1].m_based = &talents[0];
        talents[2].m_based = &talents[1];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CPoisonDamageTalent, poison_tier_count>& PoisonDamageTalents()
{
    static std::array<CPoisonDamageTalent, poison_tier_count> talents = {
        CPoisonDamageTalent(ERarity::Common, 1, 0, 0.0625f),
        CPoisonDamageTalent(ERarity::Unusual, 2, 0, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 0, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 1, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 2, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 3, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 4, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 5, 0.0625f),
        CPoisonDamageTalent(ERarity::Rare, 3, 6, 0.0625f),
    };
    static const bool linked = []()
    {
        talents[1].m_based = &talents[0];
        for (size_t i = 2; i < talents.size(); ++i)
            talents[i].m_based = &talents[1];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CConcentratedPoisonTalent, concentrated_poison_tier_count>& ConcentratedPoisonTalents()
{
    static std::array<CConcentratedPoisonTalent, concentrated_poison_tier_count> talents = {
        CConcentratedPoisonTalent(ERarity::Super, 15, 1.2f, 1.f / 1.2f),
    };
    static const bool linked = []()
    {
        auto& poison = PoisonDamageTalents();
        talents[0].m_based = &poison[3];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CMagnetismTalent, magnetism_tier_count>& MagnetismTalents()
{
    static std::array<CMagnetismTalent, magnetism_tier_count> talents = {
        CMagnetismTalent(ERarity::Mythic, 15, 1024.f),
    };
    static const bool linked = []()
    {
        auto& slots = SlotNumTalents();
        talents[0].m_based = &slots[4];
        return true;
    }();
    (void)linked;
    return talents;
}

inline std::array<CSecondChanceTalent, second_chance_tier_count>& SecondChanceTalents()
{
    static std::array<CSecondChanceTalent, second_chance_tier_count> talents = {
        CSecondChanceTalent(ERarity::Legendary, 10, 0.3f, 60.f),
        CSecondChanceTalent(ERarity::Mythic, 20, 1.5f, 30.f),
    };
    static const bool linked = []()
    {
        auto& health = FlowerHealthTalents();
        talents[0].m_based = &health[2];
        talents[1].m_based = &talents[0];
        return true;
    }();
    (void)linked;
    return talents;
}

inline int GetTalentTierIndex(ERarity rarity)
{
    for (size_t i = 0; i < talent_tier_rarities.size(); ++i)
    {
        if (talent_tier_rarities[i] == rarity) return static_cast<int>(i);
    }
    return -1;
}

inline ITalent* FindBuiltinTalent(ETalentId id, ERarity rarity, int rank = 0)
{
    int index = GetTalentTierIndex(rarity);
    if (index < 0 || index >= talent_tier_count) return nullptr;

    switch (id)
    {
    case ETalentId::PetalHealth:
        return FindTalentByRarity(PetalHealthTalents(), rarity);
    case ETalentId::FlowerHealth:
        return &FlowerHealthTalents()[index];
    case ETalentId::Movement:
        return FindTalentByRarity(MovementTalents(), rarity);
    case ETalentId::BodyDamage:
        return FindTalentByRarity(BodyDamageTalents(), rarity);
    case ETalentId::BodyDamagePoison:
        return FindTalentByRarity(BodyToxicityTalents(), rarity);
    case ETalentId::PetalSplit:
        return FindTalentByRarity(PetalSplitTalents(), rarity);
    case ETalentId::PoisonDamage:
        return FindTalentByRarity(PoisonDamageTalents(), rarity, rank);
    case ETalentId::PetalRotationSpeed:
        return FindTalentByRarity(PetalRotationTalents(), rarity);
    case ETalentId::PetalReload:
        return &PetalReloadTalents()[index];
    case ETalentId::SecondChance:
        return FindTalentByRarity(SecondChanceTalents(), rarity);
    case ETalentId::SlotNum:
        return FindTalentByRarity(SlotNumTalents(), rarity);
    case ETalentId::Medic:
        return FindTalentByRarity(MedicTalents(), rarity);
    case ETalentId::Reach:
        return FindTalentByRarity(ReachTalents(), rarity);
    case ETalentId::Magnetism:
        return FindTalentByRarity(MagnetismTalents(), rarity);
    case ETalentId::Summoner:
        return &SummonerTalents()[index];
    case ETalentId::Antennae:
        return FindTalentByRarity(AntennaeTalents(), rarity);
    case ETalentId::ConcentratedPoison:
        return FindTalentByRarity(ConcentratedPoisonTalents(), rarity);
    default:
        return nullptr;
    }
}

inline std::vector<ITalent*> BuiltinTalents()
{
    std::vector<ITalent*> talents;
    talents.reserve(talent_tier_count * 4 + short_talent_tier_count * 2 + reach_tier_count + second_chance_tier_count +
                    magnetism_tier_count + sharp_edges_tier_count + body_toxicity_tier_count + duplicator_tier_count +
                    poison_tier_count + concentrated_poison_tier_count + antennae_tier_count + movement_tier_count);
    for (auto& talent : FlowerHealthTalents())
        talents.push_back(&talent);
    for (auto& talent : MovementTalents())
        talents.push_back(&talent);
    for (auto& talent : BodyDamageTalents())
        talents.push_back(&talent);
    for (auto& talent : BodyToxicityTalents())
        talents.push_back(&talent);
    for (auto& talent : PetalHealthTalents())
        talents.push_back(&talent);
    for (auto& talent : MedicTalents())
        talents.push_back(&talent);
    for (auto& talent : SummonerTalents())
        talents.push_back(&talent);
    for (auto& talent : PetalReloadTalents())
        talents.push_back(&talent);
    for (auto& talent : PetalRotationTalents())
        talents.push_back(&talent);
    for (auto& talent : SlotNumTalents())
        talents.push_back(&talent);
    for (auto& talent : AntennaeTalents())
        talents.push_back(&talent);
    for (auto& talent : PetalSplitTalents())
        talents.push_back(&talent);
    for (auto& talent : ReachTalents())
        talents.push_back(&talent);
    for (auto& talent : PoisonDamageTalents())
        talents.push_back(&talent);
    for (auto& talent : ConcentratedPoisonTalents())
        talents.push_back(&talent);
    for (auto& talent : MagnetismTalents())
        talents.push_back(&talent);
    for (auto& talent : SecondChanceTalents())
        talents.push_back(&talent);
    return talents;
}
