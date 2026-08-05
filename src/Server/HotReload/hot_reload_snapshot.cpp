#include "hot_reload_snapshot.h"
#include "../Persistence/account_store.h"
#include "../../Engine/logger.h"
#include "../../Shared/tools.h"
#include "../../Shared/version.h"
#include "../Game/controllers/melee_controller.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/entities/blood_sacrifice_ritual.h"
#include "../Game/entities/drop.h"
#include "../Game/entities/flower.h"
#include "../Game/entities/mob.h"
#include "../Game/entities/petals/petal.h"
#include "../Game/entities/portal.h"
#include "../Game/entities/projectile.h"
#include "../Game/gamecontrollers/world_controller_factory.h"
#include "../Game/gameworld.h"
#include "../Game/player.h"
#include "../Game/state_zone.h"
#include "../Game/states/states.h"
#include "../Game/zone_mob_tools.h"
#include "../Module/network_module.h"
#include "../server.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::string_view snapshot_magic = "florrbt-hot-reload";

std::vector<SSnapshotSectionCodec>& SectionCodecs()
{
    static std::vector<SSnapshotSectionCodec> codecs;
    return codecs;
}

std::mutex& SectionCodecMutex()
{
    static std::mutex mutex;
    return mutex;
}

const SSnapshotSectionCodec* FindSectionCodec(std::string_view key)
{
    for (const SSnapshotSectionCodec& codec : SectionCodecs())
        if (codec.key == key) return &codec;
    return nullptr;
}

CJsonOwner FloatArray(const std::vector<float>& values)
{
    CJsonOwner array = MakeJsonArray();
    for (float value : values)
        AppendJson(array.get(), json_real(std::isfinite(value) ? value : 0.f));
    return array;
}

CJsonOwner ByteArray(const std::vector<std::uint8_t>& values)
{
    CJsonOwner array = MakeJsonArray();
    for (std::uint8_t value : values)
        AppendJson(array.get(), json_integer(value));
    return array;
}

std::vector<float> ReadFloatArray(const json_t* value)
{
    std::vector<float> result;
    if (!json_is_array(value)) return result;
    const size_t count = json_array_size(value);
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const json_t* item = json_array_get(value, i);
        result.push_back(json_is_number(item) ? static_cast<float>(json_number_value(item)) : 0.f);
    }
    return result;
}

std::vector<std::uint8_t> ReadByteArray(const json_t* value)
{
    std::vector<std::uint8_t> result;
    if (!json_is_array(value)) return result;
    const size_t count = json_array_size(value);
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const json_t* item = json_array_get(value, i);
        const json_int_t parsed = json_is_integer(item) ? json_integer_value(item) : 0;
        result.push_back(static_cast<std::uint8_t>(std::clamp<json_int_t>(parsed, 0, 255)));
    }
    return result;
}

void CaptureEntityCommon(const CEntity& entity, CSnapshotWriter& writer)
{
    writer.Field("id", entity.m_id);
    writer.UInt64("generation", entity.m_generation);
    writer.Field("pos", entity.m_pos);
    writer.Field("prev_pos", entity.m_prev_pos);
    writer.Field("radius", entity.m_radius);
    writer.Field("skip_world_tick", entity.m_skip_world_tick);
    writer.Field("allow_skip_tick", entity.m_allow_skip_tick);
    writer.Field("tags", entity.m_tags);
    writer.Field("team", entity.m_team);
    writer.Field("mass", entity.m_mass);
    if (std::isfinite(entity.m_health)) writer.Field("health", entity.m_health);
    writer.Field("facing_angle", entity.m_facing_angle);
    writer.Field("has_facing", entity.m_has_facing);
    writer.Field("extra_hit_num", entity.m_entity_stats.extra_hit_num);
}

void RestoreEntityCommon(CEntity& entity, const CSnapshotReader& reader)
{
    entity.m_pos = reader.Vector2("pos", entity.m_pos);
    entity.m_prev_pos = reader.Vector2("prev_pos", entity.m_pos);
    entity.m_radius = std::max(0.f, reader.Float("radius", entity.m_radius));
    entity.m_skip_world_tick = reader.Bool("skip_world_tick", entity.m_skip_world_tick);
    entity.m_allow_skip_tick = reader.Bool("allow_skip_tick", entity.m_allow_skip_tick);
    entity.m_tags = reader.UInt32("tags", entity.m_tags);
    entity.m_team = reader.Int("team", entity.m_team);
    entity.m_mass = reader.Float("mass", entity.m_mass);
    if (reader.Has("health")) entity.m_health = reader.Float("health", entity.m_health);
    entity.m_facing_angle = reader.Float("facing_angle", entity.m_facing_angle);
    entity.m_has_facing = reader.Bool("has_facing", entity.m_has_facing);
    entity.m_entity_stats.extra_hit_num = reader.Int("extra_hit_num", entity.m_entity_stats.extra_hit_num);
}

CJsonOwner CaptureController(const IController* controller)
{
    if (!controller) return {};
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("type", controller->SnapshotKey());
    writer.Field("version", controller->SnapshotVersion());
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter data_writer(data.get());
    controller->CaptureSnapshot(data_writer);
    writer.Node("data", data.release());
    return result;
}

std::unique_ptr<IController> CreateController(std::string_view key)
{
    if (key == "controller.player") return std::make_unique<CPlayerController>();
    if (key == "controller.melee") return std::make_unique<CMeleeController>();
    if (key == "controller.leafcutter_soldier") return std::make_unique<CLeafcutterSoldierController>();
    if (key == "controller.summoned_melee") return std::make_unique<CSummonedMeleeController>(-1);
    if (key == "controller.neutral_melee") return std::make_unique<CNeutralMeleeController>();
    if (key == "controller.random_wander") return std::make_unique<CRandomWanderController>();
    if (key == "controller.queen_ant") return std::make_unique<CQueenAntController>();
    if (key == "controller.spider") return std::make_unique<CSpiderController>();
    if (key == "controller.hornet_ranged") return std::make_unique<CHornetRangedController>();
    if (key == "controller.mecha_flower_ranged") return std::make_unique<CMechaFlowerRangedController>();
    if (key == "controller.hornet_special") return std::make_unique<CSpecialHornetController>();
    if (key == "controller.bumble_bee") return std::make_unique<CBumbleBeeController>();
    return {};
}

bool RestoreController(CMobBase& mob, const json_t* value, std::string& error)
{
    if (!value || json_is_null(value))
    {
        mob.SetController(nullptr);
        return true;
    }
    CSnapshotReader reader(value);
    const std::string key = reader.String("type");
    const std::uint32_t version = reader.UInt32("version", 1);
    if (key.empty())
    {
        error = "Controller snapshot is missing its stable type key";
        return false;
    }

    IController* controller = mob.GetController();
    if (!controller || controller->SnapshotKey() != key)
    {
        std::unique_ptr<IController> replacement = CreateController(key);
        if (!replacement)
        {
            error = "No controller factory registered for " + key;
            return false;
        }
        mob.SetController(std::move(replacement));
        controller = mob.GetController();
    }

    CSnapshotReader data(reader.Node("data"));
    if (!data.IsValid())
    {
        error = "Controller " + key + " has invalid snapshot data";
        return false;
    }
    return controller->RestoreSnapshot(data, version, error);
}

CJsonOwner CaptureState(const CState& state, std::string& error)
{
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("version", 1u);
    writer.Field("timer", state.m_timer);
    writer.Field("rarity", static_cast<int>(state.m_rarity));

    if (const auto* poison = dynamic_cast<const CPoisonState*>(&state))
    {
        writer.Field("type", "state.poison");
        writer.Field("basic_damage", poison->GetBasicDmg());
        writer.Field("applier_id", poison->GetApplierId());
        writer.UInt64("applier_generation", poison->GetApplierGeneration());
    } else if (const auto* ban_slot = dynamic_cast<const CBanSlotState*>(&state))
    {
        writer.Field("type", "state.ban_slot");
        writer.Field("slot_index", ban_slot->GetSlotIndex());
    } else if (dynamic_cast<const CPincerSpeedReduceState*>(&state))
        writer.Field("type", "state.pincer_speed_reduce");
    else if (const auto* web = dynamic_cast<const CWebSpeedReduceState*>(&state))
    {
        writer.Field("type", "state.web_speed_reduce");
        writer.Field("multiplier", web->GetMultiplier());
    } else if (const auto* anti_heal = dynamic_cast<const CAntiHealState*>(&state))
    {
        writer.Field("type", "state.anti_heal");
        writer.Field("medic_multiplier", anti_heal->GetMedicMultiplier());
    } else if (dynamic_cast<const CNullificationState*>(&state))
        writer.Field("type", "state.nullification");
    else if (const auto* undead = dynamic_cast<const CUndeadState*>(&state))
    {
        writer.Field("type", "state.undead");
        writer.Field("source_slot", undead->SourceSlot());
    } else if (const auto* corruption = dynamic_cast<const CCorruptionState*>(&state))
    {
        writer.Field("type", "state.corruption");
        writer.Field("original_team", corruption->GetOriginalTeam());
    } else if (dynamic_cast<const CNoReviveState*>(&state))
        writer.Field("type", "state.no_revive");
    else if (dynamic_cast<const CInvincibleState*>(&state))
        writer.Field("type", "state.invincible");
    else if (dynamic_cast<const CDiggingState*>(&state))
        writer.Field("type", "state.digging");
    else if (dynamic_cast<const CPsionicConnectionState*>(&state))
        writer.Field("type", "state.psionic_connection");
    else
    {
        error = "Unregistered runtime state encountered while capturing snapshot";
        return {};
    }
    return result;
}

std::unique_ptr<CState> RestoreState(CMobBase& owner, const json_t* value, std::string& error)
{
    CSnapshotReader reader(value);
    const std::uint32_t version = reader.UInt32("version", 1);
    if (version != 1)
    {
        error = "Unsupported state snapshot version " + std::to_string(version);
        return {};
    }
    const std::string type = reader.String("type");
    const float timer = reader.Float("timer", endless);
    const ERarity rarity = static_cast<ERarity>(reader.Int("rarity", static_cast<int>(ERarity::Null)));

    if (type == "state.poison")
    {
        CEntity* applier = owner.GameWorld()
                               ? owner.GameWorld()->GetEntity(reader.Int("applier_id", -1),
                                                              reader.UInt64("applier_generation"))
                               : nullptr;
        return std::make_unique<CPoisonState>(&owner, timer, reader.Float("basic_damage"), rarity, applier);
    }
    if (type == "state.ban_slot")
        return std::make_unique<CBanSlotState>(&owner, timer, reader.Int("slot_index", -1), rarity);
    if (type == "state.pincer_speed_reduce") return std::make_unique<CPincerSpeedReduceState>(&owner, timer, rarity);
    if (type == "state.web_speed_reduce")
    {
        float reference_mass = owner.GetFinalStats() ? owner.GetFinalStats()->mass : owner.m_mass;
        return std::make_unique<CWebSpeedReduceState>(&owner, timer, reader.Float("multiplier", 1.f), reference_mass);
    }
    if (type == "state.anti_heal")
        return std::make_unique<CAntiHealState>(&owner, timer, rarity, reader.Float("medic_multiplier", 1.f));
    if (type == "state.nullification") return std::make_unique<CNullificationState>(&owner, timer, rarity);
    if (type == "state.undead")
        return std::make_unique<CUndeadState>(&owner, timer, rarity, reader.Int("source_slot", -1));
    if (type == "state.corruption")
    {
        auto state = std::make_unique<CCorruptionState>(&owner, timer, rarity);
        state->RestoreOriginalTeam(reader.Int("original_team", owner.m_team));
        return state;
    }
    if (type == "state.no_revive") return std::make_unique<CNoReviveState>(&owner, timer, rarity);
    if (type == "state.invincible") return std::make_unique<CInvincibleState>(&owner, timer, rarity);
    if (type == "state.digging") return std::make_unique<CDiggingState>(&owner, timer, rarity);
    if (type == "state.psionic_connection")
        return std::make_unique<CPsionicConnectionState>(&owner, timer, rarity);

    error = "No state factory registered for " + type;
    return {};
}

CJsonOwner CaptureFlower(const CFlower& flower)
{
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("shield", flower.GetShield());
    writer.Field("petal_rotation_angle", flower.GetStoredPetalRotationAngle());
    writer.Field("total_copies", flower.m_total_copies);

    CJsonOwner slots = MakeJsonArray();
    for (const CPetalSlot& slot : flower.GetSlots())
    {
        CJsonOwner slot_value = MakeJsonObject();
        CSnapshotWriter slot_writer(slot_value.get());
        const EPetalType type = slot.m_p_proto ? slot.m_p_proto->m_type : EPetalType::None;
        slot_writer.Field("petal_type", static_cast<int>(type));
        slot_writer.Field("petal_key", GetPetalTypeName(type));
        slot_writer.Field("rarity", static_cast<int>(slot.m_stored_rarity));
        slot_writer.Field("slot_index", slot.m_slot_index);
        slot_writer.Field("start_copy_index", slot.m_start_copy_index);
        slot_writer.Field("runtime_type", static_cast<int>(slot.m_runtime_type));
        slot_writer.Field("available", slot.m_available);
        slot_writer.Field("banned", slot.m_banned);
        slot_writer.Node("bonus_active", ByteArray(slot.m_bonus_active).release());
        slot_writer.Node("reload_timers", FloatArray(slot.m_reload_timers).release());
        slot_writer.Node("reload_durations", FloatArray(slot.m_reload_durations).release());
        slot_writer.Node("reload_ignore_multiplier", ByteArray(slot.m_reload_ignore_multiplier).release());
        AppendJson(slots.get(), slot_value.release());
    }
    writer.Node("slots", slots.release());

    if (const auto* player_flower = dynamic_cast<const CPlayerFlower*>(&flower))
    {
        writer.Field("player_name", player_flower->m_name);
        writer.Field("level", player_flower->m_level);
        writer.Int64("experience", player_flower->m_exp);
        writer.Field("dead", player_flower->m_is_dead);
    }
    return result;
}

bool RestoreFlower(CFlower& flower, const json_t* value, std::string& error)
{
    CSnapshotReader reader(value);
    if (!reader.IsValid())
    {
        error = "Flower snapshot data is invalid";
        return false;
    }

    const json_t* slots = reader.Node("slots");
    const size_t slot_count = json_is_array(slots) ? json_array_size(slots) : 0;
    flower.RestoreSlotCount(static_cast<int>(slot_count));
    auto& destination = flower.GetSlots();
    for (size_t i = 0; i < slot_count && i < destination.size(); ++i)
    {
        CSnapshotReader slot_reader(json_array_get(slots, i));
        CPetalSlot& slot = destination[i];
        const EPetalType type = static_cast<EPetalType>(slot_reader.Int("petal_type"));
        const ERarity rarity = static_cast<ERarity>(slot_reader.Int("rarity", static_cast<int>(ERarity::Null)));
        const CPetalPrototype* prototype = type == EPetalType::None ? nullptr : FindPetalPrototype(type);
        if (type != EPetalType::None && !prototype)
        {
            error = "No petal factory registered for " + slot_reader.String("petal_key", "unknown");
            return false;
        }

        slot.ClearPetal();
        if (prototype) slot.SetPetal(prototype, static_cast<int>(i), rarity);
        else
        {
            slot.m_p_proto = nullptr;
            slot.m_stored_rarity = rarity;
            slot.m_slot_index = static_cast<int>(i);
        }
        slot.m_slot_index = slot_reader.Int("slot_index", static_cast<int>(i));
        slot.m_start_copy_index = slot_reader.Int("start_copy_index");
        slot.m_runtime_type = static_cast<EPetalType>(slot_reader.Int("runtime_type", static_cast<int>(type)));
        slot.m_available = slot_reader.Bool("available", true);
        slot.m_banned = slot_reader.Bool("banned");
        slot.m_bonus_active = ReadByteArray(slot_reader.Node("bonus_active"));
        slot.m_reload_timers = ReadFloatArray(slot_reader.Node("reload_timers"));
        slot.m_reload_durations = ReadFloatArray(slot_reader.Node("reload_durations"));
        slot.m_reload_ignore_multiplier = ReadByteArray(slot_reader.Node("reload_ignore_multiplier"));
        const size_t copies = std::max({ slot.m_bonus_active.size(), slot.m_reload_timers.size(),
                                         slot.m_reload_durations.size(), slot.m_reload_ignore_multiplier.size() });
        slot.m_p_petals.assign(copies, nullptr);
        slot.m_bonus_active.resize(copies, 0);
        slot.m_reload_timers.resize(copies, 0.f);
        slot.m_reload_durations.resize(copies, 0.f);
        for (size_t copy = 0; copy < copies; ++copy)
            slot.m_reload_durations[copy] = std::max(slot.m_reload_durations[copy], slot.m_reload_timers[copy]);
        slot.m_reload_ignore_multiplier.resize(copies, 0);
    }

    flower.RestoreFlowerRuntime(reader.Float("shield"), reader.Float("petal_rotation_angle"));
    flower.m_total_copies = reader.Int("total_copies");
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(&flower))
    {
        player_flower->m_name = reader.String("player_name", player_flower->m_name);
        player_flower->m_level = std::max(1, reader.Int("level", player_flower->m_level));
        player_flower->m_exp = std::max<std::int64_t>(0, reader.Int64("experience", player_flower->m_exp));
        player_flower->m_is_dead = reader.Bool("dead", player_flower->m_is_dead);
    }
    return true;
}

CJsonOwner CapturePairFloatMap(const std::unordered_map<int, float>& values)
{
    CJsonOwner array = MakeJsonArray();
    for (const auto& [id, value] : values)
    {
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter writer(entry.get());
        writer.Field("id", id);
        writer.Field("value", value);
        AppendJson(array.get(), entry.release());
    }
    return array;
}

void RestorePairFloatMap(const json_t* value, std::unordered_map<int, float>& destination)
{
    destination.clear();
    if (!json_is_array(value)) return;
    const size_t count = json_array_size(value);
    for (size_t i = 0; i < count; ++i)
    {
        CSnapshotReader reader(json_array_get(value, i));
        destination[reader.Int("id", -1)] = reader.Float("value");
    }
}

void CaptureProjectile(const CProjectile& projectile, CSnapshotWriter& writer)
{
    writer.Field("owner_id", projectile.m_owner_id);
    writer.UInt64("owner_generation", projectile.m_owner_generation);
    writer.Field("velocity", projectile.m_vel);
}

void RestoreProjectile(CProjectile& projectile, const CSnapshotReader& reader)
{
    projectile.m_owner_id = reader.Int("owner_id", -1);
    projectile.m_owner_generation = reader.UInt64("owner_generation");
    projectile.m_p_owner = projectile.GameWorld() && projectile.m_owner_id >= 0
                               ? projectile.GameWorld()->GetEntity(projectile.m_owner_id,
                                                                   projectile.m_owner_generation)
                               : nullptr;
    projectile.m_vel = reader.Vector2("velocity");
}

CJsonOwner CaptureMob(const CMobBase& mob, std::string& error)
{
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter writer(data.get());
    writer.Field("mob_type", static_cast<int>(mob.GetMobType()));
    writer.Field("mob_key", GetMobTypeName(mob.GetMobType()));
    writer.Field("rarity", static_cast<int>(mob.GetRarity()));
    writer.Field("velocity", mob.m_vel);

    if (const auto* attackable = dynamic_cast<const IAttackableMob*>(&mob))
    {
        writer.Field("attacking", attackable->IsAttacking());
        writer.Field("defending", attackable->IsDefending());
    }
    if (const auto* flower = dynamic_cast<const CFlower*>(&mob)) writer.Node("flower", CaptureFlower(*flower).release());

    CJsonOwner controller = CaptureController(mob.GetController());
    writer.Node("controller", controller ? controller.release() : json_null());

    CJsonOwner runtime = MakeJsonObject();
    CSnapshotWriter runtime_writer(runtime.get());
    mob.CaptureRuntimeSnapshot(runtime_writer);
    writer.Field("runtime_version", mob.RuntimeSnapshotVersion());
    writer.Node("runtime", runtime.release());

    CJsonOwner states = MakeJsonArray();
    for (const auto& state : mob.GetStates())
    {
        if (!state) continue;
        CJsonOwner state_value = CaptureState(*state, error);
        if (!state_value) return {};
        AppendJson(states.get(), state_value.release());
    }
    writer.Node("states", states.release());

    CJsonOwner damage = MakeJsonArray();
    for (const CDamageData& record : mob.GetDamageData())
    {
        if (!record.m_player) continue;
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter entry_writer(entry.get());
        entry_writer.Field("player_id", record.m_player->GetId());
        entry_writer.Field("total_damage", record.m_total_dmg);
        entry_writer.Field("reset_timer", record.m_reset_timer);
        AppendJson(damage.get(), entry.release());
    }
    writer.Node("damage", damage.release());
    return data;
}

CJsonOwner CapturePetal(const CPetal& petal)
{
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter writer(data.get());
    CaptureProjectile(petal, writer);
    writer.Field("petal_type", static_cast<int>(petal.GetPetalType()));
    writer.Field("petal_key", GetPetalTypeName(petal.GetPetalType()));
    writer.Field("rarity", static_cast<int>(petal.m_rarity));
    writer.Field("max_slot_num", petal.m_max_slot_num);
    writer.Field("copy_index", petal.m_copy_index);
    writer.Field("slot_index", petal.m_slot_index);
    writer.Field("target_entity_id", petal.m_target_entity_id);
    writer.UInt64("target_entity_generation", petal.m_target_entity_generation);
    writer.Field("lifetime", petal.m_lifetime);
    writer.Field("timer", petal.m_timer);
    writer.Field("reload_override", petal.m_reload_override);
    writer.Field("reload_ignore_multiplier", petal.m_reload_ignore_multiplier);
    writer.Field("hidden", petal.m_hidden);
    writer.Field("detach_from_slot", petal.m_detach_from_slot);
    writer.Field("spawn_flight_boost", petal.m_spawn_flight_boost);
    bool linked_to_slot = false;
    if (const auto* owner = dynamic_cast<const CFlower*>(petal.GetOwner()); owner && petal.m_slot_index >= 0 &&
                                                                           petal.m_slot_index <
                                                                               static_cast<int>(owner->GetSlots().size()))
    {
        const CPetalSlot& slot = owner->GetSlots()[petal.m_slot_index];
        linked_to_slot = petal.m_copy_index >= 0 && petal.m_copy_index < static_cast<int>(slot.m_p_petals.size()) &&
                         slot.m_p_petals[petal.m_copy_index] == &petal;
    }
    writer.Field("linked_to_slot", linked_to_slot);
    writer.Node("hit_credits", CapturePairFloatMap(petal.m_hit_credits).release());

    if (const auto* egg = dynamic_cast<const CBeetleEggPetal*>(&petal))
    {
        writer.Field("summon_id", egg->m_summon_id);
        writer.UInt64("summon_generation", egg->m_summon_generation);
        writer.Field("has_spawned_summon", egg->m_has_spawned_summon);
    }
    if (const auto* relic = dynamic_cast<const CRelicPetal*>(&petal))
    {
        writer.Field("state_zone_id", relic->m_state_zone_id);
        writer.UInt64("state_zone_generation", relic->m_state_zone_generation);
    }
    if (const auto* thrown = dynamic_cast<const CThrownPetal*>(&petal))
    {
        writer.Field("thrown", thrown->m_thrown);
        writer.Field("throw_decelerates", thrown->m_throw_decelerates);
        writer.Field("destroy_when_stopped", thrown->m_destroy_when_stopped);
        writer.Field("throw_age", thrown->m_throw_age);
        writer.Field("throw_deceleration_time", thrown->m_throw_deceleration_time);
        writer.Field("throw_initial_speed", thrown->m_throw_initial_speed);
        writer.Field("throw_direction", thrown->m_throw_direction);
    }
    if (const auto* missile = dynamic_cast<const CMissilePetal*>(&petal))
    {
        writer.Field("fired", missile->m_fired);
        writer.Field("has_fired_angle", missile->m_has_fired_angle);
        writer.Field("fired_angle", missile->m_fired_angle);
        writer.Field("fired_lifetime", missile->m_fired_lifetime);
    }
    if (const auto* trapper = dynamic_cast<const CTrapperPetal*>(&petal))
    {
        writer.Field("fire_reload_timer", trapper->m_fire_reload_timer);
        writer.Field("recoil_timer", trapper->m_recoil_timer);
    }
    if (const auto* compass = dynamic_cast<const CCompassPetal*>(&petal))
    {
        writer.Field("compass_target_id", compass->m_compass_target_id);
        writer.UInt64("compass_target_generation", compass->m_compass_target_generation);
        writer.Field("compass_wait_timer", compass->m_compass_wait_timer);
    }
    if (const auto* glass = dynamic_cast<const CGlassPetal*>(&petal))
        writer.Node("hit_cooldowns", CapturePairFloatMap(glass->m_hit_cooldowns).release());
    if (const auto* yggdrasil = dynamic_cast<const CYggdrasilPetal*>(&petal))
        writer.Field("revive_timer", yggdrasil->m_revive_timer);
    return data;
}

CJsonOwner CaptureEntity(const CEntity& entity, bool& ephemeral, std::string& error)
{
    ephemeral = false;
    if (dynamic_cast<const CStateZone*>(&entity))
    {
        ephemeral = true;
        return {};
    }

    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("version", 1u);
    CJsonOwner common = MakeJsonObject();
    CSnapshotWriter common_writer(common.get());
    CaptureEntityCommon(entity, common_writer);
    writer.Node("common", common.release());

    CJsonOwner data;
    if (const auto* mob = dynamic_cast<const CMobBase*>(&entity))
    {
        writer.Field("kind", "mob");
        data = CaptureMob(*mob, error);
    } else if (const auto* petal = dynamic_cast<const CPetal*>(&entity))
    {
        writer.Field("kind", "petal");
        data = CapturePetal(*petal);
    } else if (const auto* dandelion = dynamic_cast<const CDandelionMissile*>(&entity))
    {
        writer.Field("kind", "dandelion_missile");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        CaptureProjectile(*dandelion, data_writer);
        data_writer.Field("damage", dandelion->m_damage);
        data_writer.Field("lifetime", dandelion->m_lifetime);
        data_writer.Field("age", dandelion->m_age);
        data_writer.Field("attached", dandelion->IsAttachedToOwner());
        data_writer.Field("attach_angle", dandelion->GetAttachAngle());
        data_writer.Field("rarity", static_cast<int>(dandelion->GetRarity()));
    } else if (const auto* missile = dynamic_cast<const CMissile*>(&entity))
    {
        writer.Field("kind", "missile");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        CaptureProjectile(*missile, data_writer);
        data_writer.Field("damage", missile->m_damage);
        data_writer.Field("lifetime", missile->m_lifetime);
        data_writer.Field("age", missile->m_age);
        data_writer.Field("attached", missile->IsAttachedToOwner());
    } else if (const auto* trap = dynamic_cast<const CTrapProjectile*>(&entity))
    {
        writer.Field("kind", "trap_projectile");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        CaptureProjectile(*trap, data_writer);
        data_writer.Field("damage", trap->m_damage);
        data_writer.Field("lifetime", trap->m_lifetime);
        data_writer.Field("age", trap->m_age);
        data_writer.Field("rarity", static_cast<int>(trap->GetRarity()));
        data_writer.Field("initial_velocity", trap->m_initial_velocity);
        data_writer.Field("move_age", trap->m_move_age);
        data_writer.Field("deceleration_time", trap->m_deceleration_time);
    } else if (const auto* pollen = dynamic_cast<const CPollenProjectile*>(&entity))
    {
        writer.Field("kind", "pollen_projectile");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        CaptureProjectile(*pollen, data_writer);
        data_writer.Field("damage", pollen->m_damage);
        data_writer.Field("lifetime", pollen->m_lifetime);
        data_writer.Field("age", pollen->m_age);
    } else if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
    {
        writer.Field("kind", "drop");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        data_writer.Field("petal_type", static_cast<int>(drop->GetType()));
        data_writer.Field("rarity", static_cast<int>(drop->GetRarity()));
        data_writer.Field("owner_id", drop->GetOwnerId());
        data_writer.Field("stack_num", static_cast<int>(drop->GetStackNum()));
        data_writer.Field("pickup_delay", drop->GetPickupDelay());
        data_writer.Field("merge_timer", drop->GetMergeTimer());
        data_writer.Field("lifetime", drop->GetLifetime());
    } else if (const auto* portal = dynamic_cast<const CPortal*>(&entity))
    {
        writer.Field("kind", "portal");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        data_writer.Field("target_world", portal->GetTargetWorldName());
        data_writer.Field("from_point", portal->GetFromPointName());
    } else if (const auto* ritual = dynamic_cast<const CBloodSacrificeRitual*>(&entity))
    {
        writer.Field("kind", "blood_sacrifice_ritual");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        data_writer.Field("mob_type", static_cast<int>(ritual->GetMobType()));
        data_writer.Field("rarity", static_cast<int>(ritual->GetRarity()));
        data_writer.Field("draw_duration", ritual->GetDrawDuration());
        data_writer.Field("fade_duration", ritual->GetFadeDuration());
        data_writer.Field("age", ritual->GetAge());
        data_writer.Field("spawned", ritual->HasSpawned());
    } else
    {
        error = "No entity snapshot codec registered for entity ID " + std::to_string(entity.m_id);
        return {};
    }

    if (!data) return {};
    writer.Node("data", data.release());
    return result;
}

bool ReadEntityIdentity(const json_t* record, int& id, std::uint64_t& generation, std::string& error)
{
    CSnapshotReader record_reader(record);
    CSnapshotReader common(record_reader.Node("common"));
    if (!record_reader.IsValid() || !common.IsValid())
    {
        error = "Entity record is not an object";
        return false;
    }
    id = common.Int("id", -1);
    generation = common.UInt64("generation");
    if (id < 0 || generation == 0)
    {
        error = "Entity record has an invalid identity";
        return false;
    }
    return true;
}

bool IsDependentEntityKind(std::string_view kind)
{
    return kind == "petal" || kind == "missile" || kind == "dandelion_missile" ||
           kind == "pollen_projectile" || kind == "trap_projectile";
}

bool RestoreMobInitialState(CMobBase& mob, const CSnapshotReader& data, std::string& error)
{
    mob.m_vel = data.Vector2("velocity");
    if (auto* attackable = dynamic_cast<IAttackableMob*>(&mob))
    {
        attackable->SetAttacking(data.Bool("attacking"));
        attackable->SetDefending(data.Bool("defending"));
    }
    if (auto* flower = dynamic_cast<CFlower*>(&mob))
    {
        if (!RestoreFlower(*flower, data.Node("flower"), error)) return false;
    }
    CSnapshotReader runtime(data.Node("runtime"));
    if (!runtime.IsValid())
    {
        error = "Mob runtime snapshot is invalid";
        return false;
    }
    return mob.RestoreRuntimeSnapshot(runtime, data.UInt32("runtime_version", 1), error);
}

bool CreateIndependentEntity(CGameWorld& world, const json_t* record, std::string& error)
{
    CSnapshotReader record_reader(record);
    const std::uint32_t version = record_reader.UInt32("version", 1);
    if (version != 1)
    {
        error = "Unsupported entity record version " + std::to_string(version);
        return false;
    }
    const std::string kind = record_reader.String("kind");
    if (IsDependentEntityKind(kind)) return true;

    int id = -1;
    std::uint64_t generation = 0;
    if (!ReadEntityIdentity(record, id, generation, error)) return false;
    CSnapshotReader common(record_reader.Node("common"));
    CSnapshotReader data(record_reader.Node("data"));
    const sf::Vector2f pos = common.Vector2("pos");
    std::unique_ptr<CEntity> entity;

    if (kind == "mob")
    {
        const EMobType type = static_cast<EMobType>(data.Int("mob_type"));
        const ERarity rarity = static_cast<ERarity>(data.Int("rarity", static_cast<int>(ERarity::Null)));
        std::unique_ptr<CMobBase> mob = CreateMob(type, &world, pos, rarity, false);
        if (!mob)
        {
            error = "No mob factory registered for " + data.String("mob_key", "unknown");
            return false;
        }
        RestoreEntityCommon(*mob, common);
        if (!RestoreMobInitialState(*mob, data, error)) return false;
        entity = std::move(mob);
    } else if (kind == "drop")
    {
        auto drop = std::make_unique<CDrop>(&world, pos, static_cast<PetalType>(data.Int("petal_type")),
                                            static_cast<ERarity>(data.Int("rarity")), data.Int("owner_id", -1),
                                            data.Float("lifetime"),
                                            static_cast<std::uint16_t>(std::clamp(data.Int("stack_num", 1), 0, 65535)));
        drop->RestoreRuntime(data.Float("pickup_delay"), data.Float("merge_timer"), data.Float("lifetime"));
        entity = std::move(drop);
    } else if (kind == "portal")
    {
        auto portal = std::make_unique<CPortal>(&world, pos, common.Float("radius"), data.String("target_world"));
        portal->SetFromPointName(data.String("from_point"));
        entity = std::move(portal);
    } else if (kind == "blood_sacrifice_ritual")
    {
        auto ritual = std::make_unique<CBloodSacrificeRitual>(
            &world, pos, static_cast<EMobType>(data.Int("mob_type")), static_cast<ERarity>(data.Int("rarity")),
            data.Float("draw_duration"));
        ritual->RestoreRuntime(data.Float("draw_duration"), data.Float("fade_duration"), data.Float("age"),
                               data.Bool("spawned"));
        entity = std::move(ritual);
    } else
    {
        error = "No entity factory registered for snapshot kind " + kind;
        return false;
    }

    RestoreEntityCommon(*entity, common);
    if (!world.InsertEntityWithIdentity(std::move(entity), id, generation))
    {
        error = "Failed to insert restored entity ID " + std::to_string(id);
        return false;
    }
    return true;
}

void RestorePetalRuntime(CPetal& petal, const CSnapshotReader& data)
{
    RestoreProjectile(petal, data);
    petal.SetPetalType(static_cast<EPetalType>(data.Int("petal_type")));
    petal.m_rarity = static_cast<ERarity>(data.Int("rarity"));
    petal.m_max_slot_num = data.Int("max_slot_num");
    petal.m_copy_index = data.Int("copy_index");
    petal.m_slot_index = data.Int("slot_index");
    petal.m_target_entity_id = data.Int("target_entity_id", -1);
    petal.m_target_entity_generation = data.UInt64("target_entity_generation");
    petal.m_lifetime = data.Float("lifetime");
    petal.m_timer = data.Float("timer");
    petal.m_reload_override = data.Float("reload_override", -1.f);
    petal.m_reload_ignore_multiplier = data.Bool("reload_ignore_multiplier");
    petal.m_hidden = data.Bool("hidden");
    petal.m_detach_from_slot = data.Bool("detach_from_slot");
    petal.m_spawn_flight_boost = data.Bool("spawn_flight_boost");
    RestorePairFloatMap(data.Node("hit_credits"), petal.m_hit_credits);

    if (auto* egg = dynamic_cast<CBeetleEggPetal*>(&petal))
    {
        egg->m_summon_id = data.Int("summon_id", -1);
        egg->m_summon_generation = data.UInt64("summon_generation");
        egg->m_has_spawned_summon = data.Bool("has_spawned_summon");
    }
    if (auto* relic = dynamic_cast<CRelicPetal*>(&petal))
    {
        relic->m_state_zone_id = data.Int("state_zone_id", -1);
        relic->m_state_zone_generation = data.UInt64("state_zone_generation");
    }
    if (auto* thrown = dynamic_cast<CThrownPetal*>(&petal))
    {
        thrown->m_thrown = data.Bool("thrown");
        thrown->m_throw_decelerates = data.Bool("throw_decelerates", true);
        thrown->m_destroy_when_stopped = data.Bool("destroy_when_stopped");
        thrown->m_throw_age = data.Float("throw_age");
        thrown->m_throw_deceleration_time = data.Float("throw_deceleration_time");
        thrown->m_throw_initial_speed = data.Float("throw_initial_speed");
        thrown->m_throw_direction = data.Vector2("throw_direction", { 1.f, 0.f });
    }
    if (auto* missile = dynamic_cast<CMissilePetal*>(&petal))
    {
        missile->m_fired = data.Bool("fired");
        missile->m_has_fired_angle = data.Bool("has_fired_angle");
        missile->m_fired_angle = data.Float("fired_angle");
        missile->m_fired_lifetime = data.Float("fired_lifetime");
    }
    if (auto* trapper = dynamic_cast<CTrapperPetal*>(&petal))
    {
        trapper->m_fire_reload_timer = data.Float("fire_reload_timer", game_config::default_trapper_fire_interval);
        trapper->m_recoil_timer = data.Float("recoil_timer");
    }
    if (auto* compass = dynamic_cast<CCompassPetal*>(&petal))
    {
        compass->m_compass_target_id = data.Int("compass_target_id", -1);
        compass->m_compass_target_generation = data.UInt64("compass_target_generation");
        compass->m_compass_wait_timer = data.Float("compass_wait_timer");
    }
    if (auto* glass = dynamic_cast<CGlassPetal*>(&petal))
        RestorePairFloatMap(data.Node("hit_cooldowns"), glass->m_hit_cooldowns);
    if (auto* yggdrasil = dynamic_cast<CYggdrasilPetal*>(&petal))
        yggdrasil->m_revive_timer = data.Float("revive_timer");
}

bool CreateDependentEntity(CGameWorld& world, const json_t* record, std::string& error)
{
    CSnapshotReader record_reader(record);
    const std::string kind = record_reader.String("kind");
    if (!IsDependentEntityKind(kind)) return true;

    int id = -1;
    std::uint64_t generation = 0;
    if (!ReadEntityIdentity(record, id, generation, error)) return false;
    CSnapshotReader common(record_reader.Node("common"));
    CSnapshotReader data(record_reader.Node("data"));
    const sf::Vector2f pos = common.Vector2("pos");
    const int owner_id = data.Int("owner_id", -1);
    const std::uint64_t owner_generation = data.UInt64("owner_generation");
    CEntity* owner = owner_id >= 0 ? world.GetEntity(owner_id, owner_generation) : nullptr;
    std::unique_ptr<CEntity> entity;

    if (kind == "petal")
    {
        auto* flower = dynamic_cast<CFlower*>(owner);
        const EPetalType type = static_cast<EPetalType>(data.Int("petal_type"));
        const CPetalPrototype* prototype = FindPetalPrototype(type);
        if (!flower || !prototype || !prototype->m_factory)
        {
            error = "Cannot restore petal " + data.String("petal_key", "unknown") + " without its owner/factory";
            return false;
        }
        std::unique_ptr<CPetal> petal =
            prototype->m_factory(flower, data.Int("slot_index"), static_cast<ERarity>(data.Int("rarity")));
        if (!petal)
        {
            error = "Petal factory failed for " + data.String("petal_key", "unknown");
            return false;
        }
        RestorePetalRuntime(*petal, data);
        entity = std::move(petal);
    } else if (kind == "missile" || kind == "dandelion_missile")
    {
        sf::Vector2f direction = data.Vector2("velocity", { 1.f, 0.f });
        const float length = std::sqrt(std::max(0.f, LengthSq(direction)));
        if (length > 0.f) direction /= length;
        else direction = { 1.f, 0.f };
        if (kind == "dandelion_missile")
        {
            entity = std::make_unique<CDandelionMissile>(
                &world, pos, common.Float("radius"), data.Float("attach_angle"), data.Float("damage"),
                common.Float("health", 1.f), data.Float("lifetime"), static_cast<ERarity>(data.Int("rarity")), owner);
        } else
        {
            entity = std::make_unique<CMissile>(&world, pos, common.Float("radius"), direction, 0.f,
                                                data.Float("damage"), common.Float("health", 1.f),
                                                data.Float("lifetime"), owner);
        }
        auto* missile = static_cast<CMissile*>(entity.get());
        RestoreProjectile(*missile, data);
        missile->m_allow_skip_tick = false;
        missile->m_damage = data.Float("damage");
        missile->m_lifetime = data.Float("lifetime");
        missile->m_age = data.Float("age");
        missile->RestoreAttachedToOwner(data.Bool("attached"));
    } else if (kind == "trap_projectile")
    {
        const sf::Vector2f initial_velocity = data.Vector2("initial_velocity");
        const float initial_speed = std::sqrt(std::max(0.f, LengthSq(initial_velocity)));
        const sf::Vector2f direction = initial_speed > game_config::entity_collision_epsilon
                                           ? initial_velocity / initial_speed
                                           : sf::Vector2f{ 1.f, 0.f };
        auto trap = std::make_unique<CTrapProjectile>(
            &world, pos, common.Float("radius"), direction, initial_speed, data.Float("damage"),
            common.Float("health", 1.f), data.Float("lifetime"), common.Float("mass"),
            data.Float("deceleration_time"), static_cast<ERarity>(data.Int("rarity")), owner);
        RestoreProjectile(*trap, data);
        trap->m_allow_skip_tick = false;
        trap->m_damage = data.Float("damage");
        trap->m_lifetime = data.Float("lifetime");
        trap->m_age = data.Float("age");
        trap->m_initial_velocity = initial_velocity;
        trap->m_move_age = data.Float("move_age");
        trap->m_deceleration_time = data.Float("deceleration_time");
        entity = std::move(trap);
    } else if (kind == "pollen_projectile")
    {
        auto pollen = std::make_unique<CPollenProjectile>(&world, pos, common.Float("radius"), data.Float("damage"),
                                                          common.Float("health", 1.f), data.Float("lifetime"),
                                                          common.Float("mass"), owner);
        RestoreProjectile(*pollen, data);
        pollen->m_allow_skip_tick = false;
        pollen->m_damage = data.Float("damage");
        pollen->m_lifetime = data.Float("lifetime");
        pollen->m_age = data.Float("age");
        entity = std::move(pollen);
    }

    if (!entity)
    {
        error = "Failed to construct dependent entity kind " + kind;
        return false;
    }
    RestoreEntityCommon(*entity, common);
    CEntity* inserted = world.InsertEntityWithIdentity(std::move(entity), id, generation);
    if (!inserted)
    {
        error = "Failed to insert restored dependent entity ID " + std::to_string(id);
        return false;
    }

    if (kind == "petal" && data.Bool("linked_to_slot"))
    {
        auto* petal = static_cast<CPetal*>(inserted);
        auto* flower = dynamic_cast<CFlower*>(petal->GetOwner());
        if (!flower || petal->m_slot_index < 0 || petal->m_slot_index >= static_cast<int>(flower->GetSlots().size()))
        {
            error = "Restored petal has an invalid owner slot";
            return false;
        }
        CPetalSlot& slot = flower->GetSlots()[petal->m_slot_index];
        if (petal->m_copy_index < 0) return true;
        const size_t copy_index = static_cast<size_t>(petal->m_copy_index);
        if (copy_index >= slot.m_p_petals.size())
        {
            slot.m_p_petals.resize(copy_index + 1, nullptr);
            slot.m_bonus_active.resize(copy_index + 1, 0);
            slot.m_reload_timers.resize(copy_index + 1, 0.f);
            slot.m_reload_durations.resize(copy_index + 1, 0.f);
            slot.m_reload_ignore_multiplier.resize(copy_index + 1, 0);
        }
        slot.m_p_petals[copy_index] = petal;
    }
    return true;
}

CJsonOwner CaptureWorldController(const IGameController* controller)
{
    if (!controller) return {};
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("type", controller->SnapshotKey());
    writer.Field("version", controller->SnapshotVersion());
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter data_writer(data.get());
    controller->CaptureSnapshot(data_writer);
    writer.Node("data", data.release());
    return result;
}

bool RestoreWorldController(CGameWorld& world, const json_t* value, std::string& error)
{
    if (!value || json_is_null(value))
    {
        world.SetController(nullptr);
        return true;
    }

    CSnapshotReader reader(value);
    const std::string key = reader.String("type");
    if (key.empty())
    {
        error = "World controller snapshot is missing its stable type key";
        return false;
    }

    IGameController* controller = world.GetController();
    if (!controller || controller->SnapshotKey() != key)
    {
        std::unique_ptr<IGameController> replacement = CreateWorldController(key);
        if (!replacement)
        {
            error = "No world controller factory registered for " + key;
            return false;
        }
        world.SetController(std::move(replacement));
        controller = world.GetController();
    }
    CSnapshotReader data(reader.Node("data"));
    if (!data.IsValid())
    {
        error = "World controller " + key + " has invalid data";
        return false;
    }
    return controller->RestoreSnapshot(data, reader.UInt32("version", 1), error);
}

CJsonOwner CaptureWorlds(CServer& server, std::string& error)
{
    CJsonOwner worlds = MakeJsonArray();
    for (CGameWorld* world : server.GetWorlds())
    {
        if (!world) continue;
        CJsonOwner world_value = MakeJsonObject();
        CSnapshotWriter writer(world_value.get());
        writer.Field("world_id", world->GetId());
        writer.Field("map_path", world->GetMapPath());
        CJsonOwner controller = CaptureWorldController(world->GetController());
        writer.Node("controller", controller ? controller.release() : json_null());

        std::vector<CEntity*> entities = world->GetAllEntities();
        std::sort(entities.begin(), entities.end(), [](const CEntity* lhs, const CEntity* rhs) {
            return lhs && rhs ? lhs->m_id < rhs->m_id : lhs != nullptr;
        });
        CJsonOwner entity_values = MakeJsonArray();
        std::uint32_t ephemeral_count = 0;
        for (const CEntity* entity : entities)
        {
            if (!entity || entity->m_is_marked_for_des) continue;
            bool ephemeral = false;
            CJsonOwner entity_value = CaptureEntity(*entity, ephemeral, error);
            if (ephemeral)
            {
                ++ephemeral_count;
                continue;
            }
            if (!entity_value) return {};
            AppendJson(entity_values.get(), entity_value.release());
        }
        writer.Field("ephemeral_entities_rebuilt", ephemeral_count);
        writer.Node("entities", entity_values.release());
        AppendJson(worlds.get(), world_value.release());
    }
    return worlds;
}

bool RestoreMobState(CServer& server, CGameWorld& world, const json_t* record, std::string& error)
{
    int id = -1;
    std::uint64_t generation = 0;
    if (!ReadEntityIdentity(record, id, generation, error)) return false;
    auto* mob = dynamic_cast<CMobBase*>(world.GetEntity(id, generation));
    if (!mob)
    {
        error = "Restored mob ID " + std::to_string(id) + " is missing";
        return false;
    }

    CSnapshotReader record_reader(record);
    CSnapshotReader data(record_reader.Node("data"));
    if (!RestoreController(*mob, data.Node("controller"), error)) return false;

    mob->ClearStatesForRestore();
    if (const json_t* states = data.Node("states"); json_is_array(states))
    {
        const size_t count = json_array_size(states);
        for (size_t i = 0; i < count; ++i)
        {
            std::unique_ptr<CState> state = RestoreState(*mob, json_array_get(states, i), error);
            if (!state) return false;
            mob->AddState(std::move(state));
        }
    }

    mob->GetDamageData().clear();
    INetworkModule* network = server.GetNetworkModule();
    if (const json_t* damage = data.Node("damage"); json_is_array(damage))
    {
        const size_t count = json_array_size(damage);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader entry(json_array_get(damage, i));
            CPlayer* player = network ? network->FindPlayerById(entry.UInt32("player_id")) : nullptr;
            if (!player) continue;
            mob->GetDamageData().push_back({ player, entry.Float("total_damage"), entry.Float("reset_timer") });
        }
    }
    return true;
}

bool RestoreWorlds(CServer& server, const json_t* data, std::uint32_t version, ESnapshotRestorePhase phase,
                   std::string& error)
{
    if (version != 1)
    {
        error = "Unsupported worlds section version " + std::to_string(version);
        return false;
    }
    if (!json_is_array(data))
    {
        error = "Worlds section is not an array";
        return false;
    }

    const size_t world_count = json_array_size(data);
    for (size_t world_index = 0; world_index < world_count; ++world_index)
    {
        CSnapshotReader world_reader(json_array_get(data, world_index));
        CGameWorld* world = server.FindWorldById(world_reader.UInt32("world_id"));
        if (!world)
        {
            error = "Snapshot references an unavailable world";
            return false;
        }
        if (world->GetMapPath() != world_reader.String("map_path"))
        {
            error = "World " + std::to_string(world->GetId()) + " map changed from " +
                    world_reader.String("map_path") + " to " + world->GetMapPath();
            return false;
        }

        const json_t* entities = world_reader.Node("entities");
        if (!json_is_array(entities))
        {
            error = "World entity list is invalid";
            return false;
        }
        const size_t entity_count = json_array_size(entities);

        if (phase == ESnapshotRestorePhase::CreateObjects)
        {
            world->ClearEntitiesForRestore();
            for (size_t i = 0; i < entity_count; ++i)
                if (!CreateIndependentEntity(*world, json_array_get(entities, i), error)) return false;
            continue;
        }

        if (phase == ESnapshotRestorePhase::RestoreState)
        {
            for (size_t i = 0; i < entity_count; ++i)
            {
                CSnapshotReader record(json_array_get(entities, i));
                if (record.String("kind") != "mob") continue;
                CSnapshotReader common(record.Node("common"));
                CEntity* entity = world->GetEntity(common.Int("id", -1), common.UInt64("generation"));
                if (auto* flower = dynamic_cast<CFlower*>(entity)) flower->RebuildFinalStats();
            }
            for (size_t i = 0; i < entity_count; ++i)
                if (!CreateDependentEntity(*world, json_array_get(entities, i), error)) return false;
            for (size_t i = 0; i < entity_count; ++i)
            {
                CSnapshotReader record(json_array_get(entities, i));
                if (record.String("kind") == "mob" &&
                    !RestoreMobState(server, *world, json_array_get(entities, i), error))
                    return false;
            }
            for (size_t i = 0; i < entity_count; ++i)
            {
                CSnapshotReader record(json_array_get(entities, i));
                CSnapshotReader common(record.Node("common"));
                CEntity* entity = world->GetEntity(common.Int("id", -1), common.UInt64("generation"));
                if (!entity)
                {
                    error = "Entity disappeared while restoring state";
                    return false;
                }
                RestoreEntityCommon(*entity, common);
            }
            if (!RestoreWorldController(*world, world_reader.Node("controller"), error)) return false;
            continue;
        }

        if (phase == ESnapshotRestorePhase::ResolveReferences)
        {
            IGameController* controller = world->GetController();
            if (controller && !controller->ResolveReferences(*world, server, error))
            {
                if (error.empty())
                    error = "Failed to resolve world " + std::to_string(world->GetId()) + " controller references";
                return false;
            }
            continue;
        }

        if (phase == ESnapshotRestorePhase::RebuildDerivedData)
        {
            world->FinalizeEntityRestore();
            continue;
        }

        if (phase == ESnapshotRestorePhase::Validate)
        {
            for (size_t i = 0; i < entity_count; ++i)
            {
                int id = -1;
                std::uint64_t generation = 0;
                if (!ReadEntityIdentity(json_array_get(entities, i), id, generation, error)) return false;
                if (!world->GetEntity(id, generation))
                {
                    error = "Entity identity validation failed for ID " + std::to_string(id);
                    return false;
                }
            }
        }
    }
    return true;
}

CJsonOwner CapturePlayers(CServer& server, std::string&)
{
    CJsonOwner players = MakeJsonArray();
    INetworkModule* network = server.GetNetworkModule();
    if (!network) return players;

    for (const auto& player_ptr : network->GetPlayers())
    {
        const CPlayer* player = player_ptr.get();
        if (!player || !player->IsAuthenticated() || player->GetAccountName().empty()) continue;
        CJsonOwner value = MakeJsonObject();
        CSnapshotWriter writer(value.get());
        writer.Field("player_id", player->GetId());
        writer.Field("name", player->GetName());
        writer.Field("account_name", player->GetAccountName());
        writer.Field("remote_address", player->GetRemoteAddress());
        writer.Field("mute_timer", player->GetMuteTimer());
        writer.Field("report_disabled", player->IsReportDisabled());
        writer.Field("invalid_report_count", player->GetInvalidReportCount());
        writer.Field("second_chance_cooldown", player->GetSecondChanceCooldown());
        writer.Field("use_new_player_spawn", player->GetUseNewPlayerSpawn());
        writer.Field("talent_points", player->GetTalentPoints());

        CEntity* entity = player->GetEntity();
        writer.Field("world_id", entity && entity->GameWorld() ? entity->GameWorld()->GetId() : 0u);
        writer.Field("entity_id", entity ? entity->m_id : -1);
        writer.UInt64("entity_generation", entity ? entity->m_generation : 0);

        CJsonOwner checkpoints = MakeJsonArray();
        for (const SPlayerCheckpointEntry& checkpoint : player->m_cp_stack)
        {
            CJsonOwner checkpoint_value = MakeJsonObject();
            CSnapshotWriter checkpoint_writer(checkpoint_value.get());
            checkpoint_writer.Field("checkpoint_id", checkpoint.checkpoint_id);
            checkpoint_writer.Field("level", checkpoint.level);
            checkpoint_writer.Field("count", static_cast<int>(checkpoint.count));
            AppendJson(checkpoints.get(), checkpoint_value.release());
        }
        writer.Node("checkpoints", checkpoints.release());
        writer.Field("checkpoint_phase", static_cast<int>(player->m_cp_check_phase));
        AppendJson(players.get(), value.release());
    }
    return players;
}

bool RestorePlayers(CServer& server, const json_t* data, std::uint32_t version, ESnapshotRestorePhase phase,
                    std::string& error)
{
    if (version != 1)
    {
        error = "Unsupported players section version " + std::to_string(version);
        return false;
    }
    if (!json_is_array(data))
    {
        error = "Players section is not an array";
        return false;
    }
    INetworkModule* network = server.GetNetworkModule();
    if (!network)
    {
        error = "Network module is unavailable while restoring players";
        return false;
    }

    if (phase == ESnapshotRestorePhase::CreateObjects)
    {
        network->ClearPlayersForRestore();
        const size_t count = json_array_size(data);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader reader(json_array_get(data, i));
            const std::uint32_t player_id = reader.UInt32("player_id");
            const std::string account_name = reader.String("account_name");
            if (player_id == 0 || account_name.empty())
            {
                error = "Player snapshot has an invalid identity";
                return false;
            }

            sf::TcpSocket socket;
            auto player = std::make_unique<CPlayer>(std::move(socket), player_id, reader.String("name", account_name));
            player->Authenticate(account_name);
            player->ApplySavedTalents();

            CGameWorld* world = server.FindWorldById(reader.UInt32("world_id"));
            CEntity* entity = world && reader.Int("entity_id", -1) >= 0
                                  ? world->GetEntity(reader.Int("entity_id", -1), reader.UInt64("entity_generation"))
                                  : nullptr;
            if (reader.Int("entity_id", -1) >= 0 && !entity)
            {
                error = "Player " + account_name + " references a missing controlled entity";
                return false;
            }
            player->SetOwnedEntity(entity);
            player->RestoreDisconnectedSession(
                reader.String("remote_address"), game_config::timeout_protection_seconds, reader.Float("mute_timer"),
                reader.Bool("report_disabled"), reader.Int("invalid_report_count"),
                reader.Float("second_chance_cooldown"), reader.Bool("use_new_player_spawn"));

            player->m_cp_stack.clear();
            if (const json_t* checkpoints = reader.Node("checkpoints"); json_is_array(checkpoints))
            {
                const size_t checkpoint_count = json_array_size(checkpoints);
                player->m_cp_stack.reserve(checkpoint_count);
                for (size_t checkpoint_index = 0; checkpoint_index < checkpoint_count; ++checkpoint_index)
                {
                    CSnapshotReader checkpoint(json_array_get(checkpoints, checkpoint_index));
                    player->m_cp_stack.push_back(
                        { checkpoint.UInt32("checkpoint_id"), checkpoint.Int("level"),
                          static_cast<std::uint8_t>(std::clamp(checkpoint.Int("count", 3), 0, 255)) });
                }
            }
            player->m_cp_check_phase =
                static_cast<std::uint8_t>(std::clamp(reader.Int("checkpoint_phase"), 0, 255));
            if (!network->InsertRestoredPlayer(std::move(player)))
            {
                error = "Failed to restore player ID " + std::to_string(player_id);
                return false;
            }
        }
        return true;
    }

    if (phase == ESnapshotRestorePhase::RebuildDerivedData)
    {
        network->FinalizePlayerRestore();
        return true;
    }

    if (phase == ESnapshotRestorePhase::Validate)
    {
        const size_t count = json_array_size(data);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader reader(json_array_get(data, i));
            CPlayer* player = network->FindPlayerById(reader.UInt32("player_id"));
            if (!player || player->GetAccountName() != reader.String("account_name"))
            {
                error = "Player restore validation failed";
                return false;
            }
        }
    }
    return true;
}

CJsonOwner CaptureRng(CServer&, std::string&)
{
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    std::ostringstream global_rng;
    global_rng << GetRng();
    writer.Field("global_mt19937", global_rng.str());
    std::ostringstream zone_rng;
    zone_rng << ZoneMobRng();
    writer.Field("zone_mt19937", zone_rng.str());
    return result;
}

CJsonOwner CaptureTitanForgeCooldowns(CServer& server, std::string& error)
{
    CJsonOwner entries = MakeJsonArray();
    if (!entries)
    {
        error = "Failed to allocate Titan forge cooldown array";
        return {};
    }

    const CServer::TTitanForgeCooldowns& cooldowns = server.GetTitanForgeCooldowns();
    for (std::size_t i = static_cast<std::size_t>(EPetalType::None) + 1; i < cooldowns.size(); ++i)
    {
        if (cooldowns[i] <= 0.f) continue;
        CJsonOwner entry = MakeJsonObject();
        if (!entry)
        {
            error = "Failed to allocate Titan forge cooldown entry";
            return {};
        }
        CSnapshotWriter writer(entry.get());
        writer.Field("type", static_cast<int>(i));
        writer.Field("remaining_seconds", cooldowns[i]);
        if (!AppendJson(entries.get(), entry.release()))
        {
            error = "Failed to append Titan forge cooldown entry";
            return {};
        }
    }
    return entries;
}

bool RestoreTitanForgeCooldowns(CServer& server, const json_t* data, std::uint32_t version,
                                ESnapshotRestorePhase phase, std::string& error)
{
    if (version != 1)
    {
        error = "Unsupported Titan forge cooldown section version " + std::to_string(version);
        return false;
    }
    if (phase != ESnapshotRestorePhase::CreateObjects) return true;
    if (!json_is_array(data))
    {
        error = "Titan forge cooldown section must be an array";
        return false;
    }

    CServer::TTitanForgeCooldowns cooldowns{};
    std::unordered_set<int> seen_types;
    const size_t count = json_array_size(data);
    for (size_t i = 0; i < count; ++i)
    {
        CSnapshotReader reader(json_array_get(data, i));
        const json_t* type_value = reader.Node("type");
        const json_t* remaining_value = reader.Node("remaining_seconds");
        const int type = reader.Int("type", -1);
        const float remaining = reader.Float("remaining_seconds", -1.f);
        if (!reader.IsValid() || !json_is_integer(type_value) || !json_is_number(remaining_value) ||
            type <= static_cast<int>(EPetalType::None) || type >= static_cast<int>(petal_type_names.size()) ||
            !std::isfinite(remaining) || remaining < 0.f)
        {
            error = "Invalid Titan forge cooldown entry at index " + std::to_string(i);
            return false;
        }
        if (!seen_types.insert(type).second)
        {
            error = "Duplicate Titan forge cooldown type " + std::to_string(type);
            return false;
        }
        cooldowns[static_cast<std::size_t>(type)] = remaining;
    }
    server.RestoreTitanForgeCooldowns(std::move(cooldowns));
    return true;
}

bool RestoreRng(CServer&, const json_t* data, std::uint32_t version, ESnapshotRestorePhase phase, std::string& error)
{
    if (version != 1)
    {
        error = "Unsupported RNG section version " + std::to_string(version);
        return false;
    }
    if (phase != ESnapshotRestorePhase::CreateObjects) return true;
    CSnapshotReader reader(data);
    std::istringstream global_rng(reader.String("global_mt19937"));
    global_rng >> GetRng();
    std::istringstream zone_rng(reader.String("zone_mt19937"));
    zone_rng >> ZoneMobRng();
    if (!global_rng || !zone_rng)
    {
        error = "Snapshot contains an invalid RNG state";
        return false;
    }
    return true;
}

void EnsureBuiltinSections()
{
    static std::once_flag once;
    std::call_once(once, []() {
        RegisterSnapshotSection({ "rng", 1, true, CaptureRng, RestoreRng, {} });
        RegisterSnapshotSection(
            { "titan_forge_cooldowns", 1, false, CaptureTitanForgeCooldowns, RestoreTitanForgeCooldowns, {} });
        RegisterSnapshotSection({ "worlds", 1, true, CaptureWorlds, RestoreWorlds, {} });
        RegisterSnapshotSection({ "players", 1, true, CapturePlayers, RestorePlayers, {} });
    });
}
} // namespace

bool RegisterSnapshotSection(SSnapshotSectionCodec codec, std::string* error)
{
    if (codec.key.empty() || codec.version == 0 || !codec.capture || !codec.restore)
    {
        if (error) *error = "Snapshot section codec is incomplete";
        return false;
    }
    std::lock_guard<std::mutex> lock(SectionCodecMutex());
    if (FindSectionCodec(codec.key))
    {
        if (error) *error = "Duplicate snapshot section codec: " + codec.key;
        return false;
    }
    SectionCodecs().push_back(std::move(codec));
    return true;
}

bool CHotReloadSnapshotService::Capture(CServer& server, const std::filesystem::path& path, std::string& error)
{
    EnsureBuiltinSections();
    error.clear();
    std::string account_error;
    if (!CAccountDataStore::Flush(&account_error))
    {
        error = "Failed to save accounts before hot reload: " + account_error;
        return false;
    }

    CJsonOwner root = MakeJsonObject();
    CSnapshotWriter writer(root.get());
    writer.Field("format", snapshot_magic);
    writer.Field("container_version", container_version);
    writer.Field("app_version", florrbt::version);
    writer.Field("writer_build", std::string(__DATE__) + " " + __TIME__);
    writer.UInt64("tick", server.GetTick());

    CJsonOwner sections = MakeJsonObject();
    for (const SSnapshotSectionCodec& codec : SectionCodecs())
    {
        CJsonOwner data = codec.capture(server, error);
        if (!data)
        {
            if (error.empty()) error = "Failed to capture snapshot section " + codec.key;
            return false;
        }
        CJsonOwner section = MakeJsonObject();
        CSnapshotWriter section_writer(section.get());
        section_writer.Field("version", codec.version);
        section_writer.Field("required", codec.required);
        section_writer.Node("data", data.release());
        json_object_set_new(sections.get(), codec.key.c_str(), section.release());
    }
    writer.Node("sections", sections.release());
    return WriteJsonAtomically(path, root.get(), error);
}

bool CHotReloadSnapshotService::Restore(CServer& server, const std::filesystem::path& path, std::string& error)
{
    EnsureBuiltinSections();
    CJsonOwner root = LoadJsonFile(path, error);
    if (!root || !json_is_object(root.get())) return false;
    CSnapshotReader root_reader(root.get());
    if (root_reader.String("format") != snapshot_magic)
    {
        error = "Not a FlorrBt hot reload snapshot";
        return false;
    }
    if (root_reader.UInt32("container_version") > container_version)
    {
        error = "Snapshot container is newer than this server";
        return false;
    }
    const std::string snapshot_app_version = root_reader.String("app_version");
    if (!snapshot_app_version.empty() && snapshot_app_version != florrbt::version)
    {
        LOG_WARN("hot_reload", "Restoring snapshot from FlorrBt v" + snapshot_app_version + " with " +
                                   std::string(florrbt::version_label));
    }
    server.SetTickForRestore(root_reader.UInt64("tick"));

    json_t* sections = const_cast<json_t*>(root_reader.Node("sections"));
    if (!json_is_object(sections))
    {
        error = "Snapshot sections object is missing";
        return false;
    }

    const char* key = nullptr;
    json_t* section = nullptr;
    json_object_foreach(sections, key, section)
    {
        const SSnapshotSectionCodec* codec = FindSectionCodec(key);
        CSnapshotReader section_reader(section);
        if (!codec && section_reader.Bool("required", true))
        {
            error = "This server does not understand required snapshot section " + std::string(key);
            return false;
        }
    }

    for (const SSnapshotSectionCodec& codec : SectionCodecs())
    {
        json_t* section_value = json_object_get(sections, codec.key.c_str());
        if (!section_value)
        {
            if (!codec.required) continue;
            error = "Required snapshot section is missing: " + codec.key;
            return false;
        }
        CSnapshotReader section_reader(section_value);
        const std::uint32_t saved_version = section_reader.UInt32("version");
        if (saved_version > codec.version)
        {
            error = "Snapshot section " + codec.key + " is newer than this server";
            return false;
        }
        if (saved_version < codec.version)
        {
            if (!codec.migrate)
            {
                error = "Snapshot section " + codec.key + " requires a migration from version " +
                        std::to_string(saved_version);
                return false;
            }
            json_t* section_data = json_object_get(section_value, "data");
            if (!codec.migrate(saved_version, section_data, error)) return false;
            json_object_set_new(section_value, "version", json_integer(codec.version));
        }
    }

    constexpr std::array phases = {
        ESnapshotRestorePhase::CreateObjects,      ESnapshotRestorePhase::RestoreState,
        ESnapshotRestorePhase::ResolveReferences, ESnapshotRestorePhase::RebuildDerivedData,
        ESnapshotRestorePhase::Validate,
    };
    for (ESnapshotRestorePhase phase : phases)
    {
        for (const SSnapshotSectionCodec& codec : SectionCodecs())
        {
            const json_t* section_value = json_object_get(sections, codec.key.c_str());
            if (!section_value) continue;
            CSnapshotReader section_reader(section_value);
            if (!codec.restore(server, section_reader.Node("data"), section_reader.UInt32("version"), phase, error))
            {
                if (error.empty()) error = "Failed to restore snapshot section " + codec.key;
                return false;
            }
        }
    }
    return true;
}
