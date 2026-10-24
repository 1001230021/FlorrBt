#include "hot_reload_snapshot.h"
#include "mob_stats_snapshot.h"
#include "../Persistence/account_store.h"
#include "../../Engine/logger.h"
#include "../../Shared/tools.h"
#include "../../Shared/version.h"
#include "../Game/controllers/melee_controller.h"
#include "../Game/controllers/player_controller.h"
#include "../Game/controller_factory.h"
#include "../Game/chain.h"
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
#include "../Game/talent.h"
#include "../Game/state_zone.h"
#include "../Game/states/states.h"
#include "../Game/extensions/weather_extension.h"
#include "../Game/extensions/weather_snapshot.h"
#include "../Game/gamecontext.h"
#include "../Game/zone_mob_tools.h"
#include "../Module/network_module.h"
#include "../Module/world_module.h"
#include "../server.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
constexpr std::string_view snapshot_magic = "florrbt-hot-reload";
// Snapshot files are local state, but they can still be truncated or manually
// edited.  Never let a corrupt array turn into an unbounded allocation during
// restore.  Runtime arrays in this project are all far smaller than this cap.
constexpr std::size_t max_snapshot_array_entries = 4096;
constexpr std::size_t max_snapshot_worlds = 1024;
constexpr std::size_t max_snapshot_entities_per_world = 65536;
constexpr std::size_t max_snapshot_players = 4096;
constexpr std::size_t max_snapshot_slots = 64;
constexpr std::size_t max_snapshot_states = 4096;
constexpr std::size_t max_snapshot_damage_entries = 65536;
constexpr std::size_t max_snapshot_checkpoints = 4096;

bool HasSerializedWorldIdentity(const CSnapshotReader& reader, std::string_view prefix);

bool ReadKnownRarity(const CSnapshotReader& reader, std::string_view key, ERarity& rarity,
                     std::string_view context, std::string& error, bool allow_null = false)
{
    const int value = reader.Int(key, std::numeric_limits<int>::min());
    const ERarity candidate = static_cast<ERarity>(value);
    if ((allow_null && candidate == ERarity::Null) || IsKnownRarity(candidate))
    {
        rarity = candidate;
        return true;
    }
    error = std::string(context) + " has invalid rarity " + std::to_string(value);
    return false;
}

bool ReadKnownMobType(const CSnapshotReader& reader, std::string_view key, EMobType& type,
                      std::string_view context, std::string& error)
{
    const int value = reader.Int(key, std::numeric_limits<int>::min());
    if (IsKnownMobTypeId(value))
    {
        type = static_cast<EMobType>(value);
        return true;
    }
    error = std::string(context) + " has invalid mob type " + std::to_string(value);
    return false;
}

bool ReadKnownPetalType(const CSnapshotReader& reader, std::string_view key, EPetalType& type,
                        std::string_view context, std::string& error, bool allow_none = false)
{
    const int value = reader.Int(key, std::numeric_limits<int>::min());
    if ((allow_none && value == static_cast<int>(EPetalType::None)) || IsKnownPetalTypeId(value))
    {
        type = static_cast<EPetalType>(value);
        return true;
    }
    error = std::string(context) + " has invalid petal type " + std::to_string(value);
    return false;
}

bool CheckSnapshotArrayLimit(const json_t* value, std::size_t limit, std::string_view label, std::string& error)
{
    if (!json_is_array(value))
    {
        error = std::string(label) + " is not an array";
        return false;
    }
    if (json_array_size(value) > limit)
    {
        error = std::string(label) + " exceeds the restore limit of " + std::to_string(limit);
        return false;
    }
    return true;
}

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
    const size_t count = std::min(json_array_size(value), max_snapshot_array_entries);
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        const json_t* item = json_array_get(value, i);
        if (!json_is_number(item))
        {
            result.push_back(0.f);
            continue;
        }
        const double parsed = json_number_value(item);
        const float converted = static_cast<float>(parsed);
        result.push_back(std::isfinite(parsed) && std::isfinite(converted) ? converted : 0.f);
    }
    return result;
}

std::vector<std::uint8_t> ReadByteArray(const json_t* value)
{
    std::vector<std::uint8_t> result;
    if (!json_is_array(value)) return result;
    const size_t count = std::min(json_array_size(value), max_snapshot_array_entries);
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
    writer.Field("id", entity.Id());
    writer.UInt64("generation", entity.Generation());
    writer.Field("pos", entity.Pos());
    writer.Field("prev_pos", entity.PrevPos());
    writer.Field("radius", entity.Radius());
    writer.Field("skip_world_tick", entity.SkipWorldTick());
    writer.Field("allow_skip_tick", entity.AllowSkipTick());
    writer.Field("tick_mode", static_cast<int>(entity.TickMode()));
    writer.Field("tick_horizon", entity.TickHorizon());
    writer.Field("tags", entity.Tags());
    writer.Field("team", entity.Team());
    writer.Field("mass", entity.Mass());
    if (std::isfinite(entity.Health())) writer.Field("health", entity.Health());
    writer.Field("facing_angle", entity.FacingAngle());
    writer.Field("has_facing", entity.HasFacing());
    writer.Field("extra_hit_num", entity.EntityStats().extra_hit_num);
}

void RestoreEntityCommon(CEntity& entity, const CSnapshotReader& reader)
{
    entity.Pos() = reader.Vector2("pos", entity.Pos());
    entity.PrevPos() = reader.Vector2("prev_pos", entity.Pos());
    entity.Radius() = std::max(0.f, reader.Float("radius", entity.Radius()));
    entity.SkipWorldTick() = reader.Bool("skip_world_tick", entity.SkipWorldTick());
    entity.AllowSkipTick() = reader.Bool("allow_skip_tick", entity.AllowSkipTick());
    entity.TickMode() = static_cast<EEntityTickMode>(
        std::clamp(reader.Int("tick_mode", static_cast<int>(entity.TickMode())),
                   static_cast<int>(EEntityTickMode::Default), static_cast<int>(EEntityTickMode::PlayerVisible)));
    entity.TickHorizon() = std::max(0.f, reader.Float("tick_horizon", entity.TickHorizon()));
    entity.Tags() = reader.UInt32("tags", entity.Tags());
    entity.Team() = reader.Int("team", entity.Team());
    entity.Mass() = std::max(0.f, reader.Float("mass", std::max(0.f, entity.Mass())));
    if (reader.Has("health")) entity.Health() = reader.Float("health", entity.Health());
    entity.FacingAngle() = reader.Float("facing_angle", std::isfinite(entity.FacingAngle())
                                                            ? entity.FacingAngle()
                                                            : 0.f);
    entity.HasFacing() = reader.Bool("has_facing", entity.HasFacing());
    entity.EntityStats().extra_hit_num =
        std::max(0, reader.Int("extra_hit_num", std::max(0, entity.EntityStats().extra_hit_num)));
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
    return CreateSnapshotController(key);
}

bool RestoreController(CMobBase& mob, const json_t* value, std::string& error)
{
    if (!value || json_is_null(value))
    {
        mob.SetController(nullptr);
        return true;
    }
    CSnapshotReader reader(value);
    std::string key = reader.String("type");
    const std::uint32_t version = reader.UInt32("version", 1);
    if (key.empty())
    {
        error = "Controller snapshot is missing its stable type key";
        return false;
    }

    // Firefly Larva originally used the generic melee controller. Its
    // serialized movement fields are layout-compatible with the dedicated
    // controller, so migrate the stable key instead of silently restoring the
    // old controller and losing skill dispatch after a hot reload.
    if (mob.GetMobType() == EMobType::FireflyLarva && key == "controller.melee")
        key = "controller.firefly_larva";

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
        const SEntityHandle& applier = poison->GetApplierRef();
        // Keep the legacy id/generation names while adding the world identity
        // needed to survive room recreation and id reuse.
        writer.Field("applier_id", applier.id);
        writer.UInt64("applier_generation", applier.generation);
        writer.Field("applier_world_id", applier.world_id);
        writer.UInt64("applier_world_instance_generation", applier.world_instance_generation);
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
    else if (const auto* hallucinogenic = dynamic_cast<const CHallucinogenicState*>(&state))
    {
        writer.Field("type", "state.hallucinogenic");
        writer.Field("parameter_mask", static_cast<int>(hallucinogenic->GetParameterMask()));
        writer.Field("effect_depth", hallucinogenic->GetEffectDepth());
        writer.Field("evolution_progress", hallucinogenic->GetEvolutionProgress());
        const auto& parameters = hallucinogenic->GetParameters();
        writer.Node("parameters", FloatArray(std::vector<float>(parameters.begin(), parameters.end())).release());
    }
    else if (dynamic_cast<const CPulseState*>(&state))
        writer.Field("type", "state.pulse");
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
    float timer = reader.Float("timer", endless);
    if (timer < 0.f && timer != endless) timer = 0.f;
    ERarity rarity = ERarity::Null;
    if (!ReadKnownRarity(reader, "rarity", rarity, "Runtime state", error)) return {};

    if (type == "state.poison")
    {
        SEntityHandle applier_ref;
        applier_ref.id = reader.Int("applier_id", -1);
        applier_ref.generation = reader.UInt64("applier_generation");
        applier_ref.world_id = reader.UInt32("applier_world_id");
        applier_ref.world_instance_generation = reader.UInt64("applier_world_instance_generation");
        CEntity* applier = owner.GameWorld() ? owner.GameWorld()->ResolveEntity(applier_ref) : nullptr;
        if (!applier && owner.GameWorld() && !applier_ref.IsValid() && applier_ref.id >= 0 &&
            !HasSerializedWorldIdentity(reader, "applier"))
            applier = owner.GameWorld()->GetEntity(applier_ref.id);
        return std::make_unique<CPoisonState>(&owner, timer, reader.Float("basic_damage"), rarity, applier);
    }
    if (type == "state.ban_slot")
        return std::make_unique<CBanSlotState>(&owner, timer, reader.Int("slot_index", -1), rarity);
    if (type == "state.pincer_speed_reduce") return std::make_unique<CPincerSpeedReduceState>(&owner, timer, rarity);
    if (type == "state.web_speed_reduce")
    {
        float reference_mass = owner.GetFinalStats() ? owner.GetFinalStats()->mass : owner.Mass();
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
        state->RestoreOriginalTeam(reader.Int("original_team", owner.Team()));
        return state;
    }
    if (type == "state.no_revive") return std::make_unique<CNoReviveState>(&owner, timer, rarity);
    if (type == "state.invincible") return std::make_unique<CInvincibleState>(&owner, timer, rarity);
    if (type == "state.digging") return std::make_unique<CDiggingState>(&owner, timer, rarity);
    if (type == "state.psionic_connection")
        return std::make_unique<CPsionicConnectionState>(&owner, timer, rarity);
    if (type == "state.hallucinogenic")
    {
        const std::vector<float> parameters = ReadFloatArray(reader.Node("parameters"));
        CHallucinogenicState::parameter_array parameter_values{};
        for (size_t i = 0; i < parameter_values.size() && i < parameters.size(); ++i)
            parameter_values[i] = parameters[i];
        return std::make_unique<CHallucinogenicState>(
            &owner, timer, rarity, static_cast<std::uint8_t>(std::clamp(reader.Int("parameter_mask"), 0, 255)),
            reader.Float("effect_depth"), reader.Float("evolution_progress"), parameter_values);
    }
    if (type == "state.pulse") return std::make_unique<CPulseState>(&owner, timer, rarity);

    error = "No state factory registered for " + type;
    return {};
}

CJsonOwner CaptureFlower(const CFlower& flower)
{
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("shield", flower.GetShield());
    writer.Field("mana", flower.Mana());
    writer.Field("petal_rotation_angle", flower.GetStoredPetalRotationAngle());
    writer.Field("total_copies", flower.m_total_copies);
    writer.Field("petals_suspended", flower.HasPendingPetalReload());

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
        slot_writer.Node("spawn_cost_paid", ByteArray(slot.m_spawn_cost_paid).release());
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
    if (slot_count > max_snapshot_slots)
    {
        error = "Flower snapshot contains too many slots";
        return false;
    }
    flower.RestoreSlotCount(static_cast<int>(slot_count));
    auto& destination = flower.GetSlots();
    for (size_t i = 0; i < slot_count && i < destination.size(); ++i)
    {
        CSnapshotReader slot_reader(json_array_get(slots, i));
        CPetalSlot& slot = destination[i];
        EPetalType type = EPetalType::None;
        if (!ReadKnownPetalType(slot_reader, "petal_type", type, "Flower slot", error, true)) return false;
        ERarity rarity = ERarity::Null;
        if (!ReadKnownRarity(slot_reader, "rarity", rarity, "Flower slot", error, type == EPetalType::None))
            return false;
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
        // Slot position is structural, not user data.  Trusting a serialized
        // arbitrary slot index would let a detached petal attach to a
        // different slot during the dependent-entity phase.
        slot.m_slot_index = static_cast<int>(i);
        slot.m_start_copy_index = std::clamp(slot_reader.Int("start_copy_index"), -1,
                                             static_cast<int>(max_snapshot_array_entries) - 1);
        if (slot_reader.Has("runtime_type"))
        {
            if (!ReadKnownPetalType(slot_reader, "runtime_type", slot.m_runtime_type, "Flower slot runtime", error,
                                    true))
                return false;
        }
        else
            slot.m_runtime_type = type;
        slot.m_available = slot_reader.Bool("available", true);
        slot.m_banned = slot_reader.Bool("banned");
        slot.m_bonus_active = ReadByteArray(slot_reader.Node("bonus_active"));
        slot.m_reload_timers = ReadFloatArray(slot_reader.Node("reload_timers"));
        slot.m_reload_durations = ReadFloatArray(slot_reader.Node("reload_durations"));
        slot.m_reload_ignore_multiplier = ReadByteArray(slot_reader.Node("reload_ignore_multiplier"));
        slot.m_spawn_cost_paid = ReadByteArray(slot_reader.Node("spawn_cost_paid"));
        // SetPetal() already established the canonical copy count from the
        // registered prototype.  Serialized vectors are state for those
        // copies, not authority to enlarge the slot.  Accepting their
        // largest length made a damaged snapshot manufacture thousands of
        // phantom copies and later caused the parallel arrays to drift.
        slot.NormalizeRuntimeState();
    }

    flower.RestoreFlowerRuntime(reader.Float("shield"), reader.Float("mana"), reader.Float("petal_rotation_angle"));
    flower.RestorePetalSuspension(reader.Bool("petals_suspended"));
    flower.m_total_copies = std::max(0, reader.Int("total_copies"));
    if (auto* player_flower = dynamic_cast<CPlayerFlower*>(&flower))
    {
        player_flower->m_name = reader.String("player_name", player_flower->m_name);
        player_flower->m_level = std::max(1, reader.Int("level", player_flower->m_level));
        player_flower->m_exp = std::max<std::int64_t>(0, reader.Int64("experience", player_flower->m_exp));
        player_flower->m_is_dead = reader.Bool("dead", player_flower->m_is_dead);
    }
    return true;
}

CJsonOwner CaptureEntityFloatMap(const std::unordered_map<SEntityHandle, float, SEntityHandleHash>& values)
{
    CJsonOwner array = MakeJsonArray();
    for (const auto& [handle, value] : values)
    {
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter writer(entry.get());
        writer.Field("id", handle.id);
        writer.UInt64("generation", handle.generation);
        writer.Field("world_id", handle.world_id);
        writer.UInt64("world_instance_generation", handle.world_instance_generation);
        writer.Field("value", value);
        AppendJson(array.get(), entry.release());
    }
    return array;
}

void RestoreEntityFloatMap(const json_t* value, const CGameWorld* world,
                           std::unordered_map<SEntityHandle, float, SEntityHandleHash>& destination)
{
    destination.clear();
    if (!json_is_array(value) || !world) return;
    const size_t count = std::min(json_array_size(value), max_snapshot_damage_entries);
    for (size_t i = 0; i < count; ++i)
    {
        CSnapshotReader reader(json_array_get(value, i));
        const int id = reader.Int("id", -1);
        if (id < 0) continue;

        const std::uint64_t generation = reader.UInt64("generation");
        const std::uint32_t world_id = reader.UInt32("world_id");
        const std::uint64_t world_instance_generation = reader.UInt64("world_instance_generation");
        SEntityHandle handle{ id, generation, world_id, world_instance_generation };

        const bool has_world_identity = reader.Has("world_id") && reader.Has("world_instance_generation");
        // Old snapshots only had an id (or id + generation). Resolve those
        // once against the restored world so the transient entry receives a
        // canonical, fully qualified handle. A qualified handle that belongs
        // to another world must not take this fallback.
        if (!has_world_identity)
        {
            CEntity* entity = handle.generation != 0 ? world->GetEntity(id, handle.generation) : world->GetEntity(id);
            if (!entity) continue;
            handle = entity->GetHandle();
        }
        if (!world->ResolveEntity(handle)) continue;
        destination[handle] = reader.Float("value");
    }
}

void CaptureHandle(const CSnapshotWriter& writer, std::string_view prefix, const SEntityHandle& handle)
{
    const std::string base(prefix);
    writer.Field(base + "_id", handle.id);
    writer.UInt64(base + "_generation", handle.generation);
    writer.Field(base + "_world_id", handle.world_id);
    writer.UInt64(base + "_world_instance_generation", handle.world_instance_generation);
}

bool HasSerializedWorldIdentity(const CSnapshotReader& reader, std::string_view prefix)
{
    const std::string base(prefix);
    // A qualified handle must never fall back to the legacy id/generation
    // fields when its world identity does not resolve in this world.
    return reader.Has(base + "_world_id") && reader.Has(base + "_world_instance_generation");
}

// Older snapshots may omit the generation; resolve such ids in the restored world.
CEntity* ResolveRestoredEntity(CGameWorld* world, int id, std::uint64_t generation)
{
    if (!world || id < 0) return nullptr;
    return generation != 0 ? world->GetEntity(id, generation) : world->GetEntity(id);
}

SEntityHandle RestoreHandle(const CSnapshotReader& reader, std::string_view prefix, CGameWorld* world)
{
    const std::string base(prefix);
    SEntityHandle handle;
    handle.id = reader.Int(base + "_id", -1);
    handle.generation = reader.UInt64(base + "_generation");
    handle.world_id = reader.UInt32(base + "_world_id");
    handle.world_instance_generation = reader.UInt64(base + "_world_instance_generation");
    const bool has_world_identity = HasSerializedWorldIdentity(reader, prefix);
    if (world && handle.IsValid() && !has_world_identity)
    {
        if (CEntity* entity = world->GetEntity(handle.id, handle.generation)) handle = entity->GetHandle();
    }
    else if (!handle.IsValid() && world && handle.id >= 0 && !has_world_identity)
    {
        if (CEntity* entity = world->GetEntity(handle.id)) handle = entity->GetHandle();
    }
    return handle;
}

void CaptureProjectile(const CProjectile& projectile, CSnapshotWriter& writer)
{
    CaptureHandle(writer, "owner", projectile.m_owner_ref);
    // Keep the legacy names for older tooling and snapshot readers.
    writer.Field("owner_id", projectile.m_owner_ref.id);
    writer.UInt64("owner_generation", projectile.m_owner_ref.generation);
    writer.Field("velocity", projectile.m_vel);
}

void RestoreProjectile(CProjectile& projectile, const CSnapshotReader& reader)
{
    projectile.m_owner_ref = RestoreHandle(reader, "owner", projectile.GameWorld());
    if (!projectile.m_owner_ref.IsValid() && projectile.GameWorld() &&
        !HasSerializedWorldIdentity(reader, "owner"))
    {
        const int id = reader.Int("owner_id", -1);
        const std::uint64_t generation = reader.UInt64("owner_generation");
        CEntity* owner = ResolveRestoredEntity(projectile.GameWorld(), id, generation);
        projectile.m_owner_ref = owner ? owner->GetHandle() : SEntityHandle{};
    }
    projectile.m_vel = reader.Vector2("velocity");
}

CJsonOwner CaptureMob(const CMobBase& mob, std::string& error)
{
    CJsonOwner data = MakeJsonObject();
    CSnapshotWriter writer(data.get());
    writer.Field("mob_type", static_cast<int>(mob.GetMobType()));
    writer.Field("mob_key", GetMobTypeName(mob.GetMobType()));
    writer.Field("rarity", static_cast<int>(mob.GetRarity()));
    writer.Field("naturally_spawned", mob.IsNaturallySpawned());
    writer.Field("shortcut_contact", mob.IsInShortcut());
    writer.Field("shortcut_probe_radius", mob.ShortcutProbeRadius());
    writer.Field("velocity", mob.m_vel);
    if (const SMobStats* stats = mob.GetBaseStats()) writer.Node("base_stats", CaptureMobBaseStats(*stats).release());

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
        if (record.m_account_name.empty()) continue;
        CJsonOwner entry = MakeJsonObject();
        CSnapshotWriter entry_writer(entry.get());
        entry_writer.Field("account_name", record.m_account_name);
        entry_writer.Field("progress_account", record.m_progress_account);
        entry_writer.Field("total_damage", record.m_total_dmg);
        entry_writer.Field("reset_timer", record.m_reset_timer);
        AppendJson(damage.get(), entry.release());
    }
    writer.Node("damage", damage.release());

    if (const auto* root = dynamic_cast<const IMultiSegmentRootMob*>(&mob))
    {
        writer.Field("multi_segment_role", "root");
        writer.Field("multi_segment_state", static_cast<int>(root->SegmentAssemblyState()));
        CJsonOwner segments = MakeJsonArray();
        for (const SMobSegmentReference& reference : root->GetSegmentReferences())
        {
            CJsonOwner value = MakeJsonObject();
            CSnapshotWriter segment_writer(value.get());
            segment_writer.Field("index", reference.index);
            CaptureHandle(segment_writer, "entity", reference.entity_ref);
            AppendJson(segments.get(), value.release());
        }
        writer.Node("multi_segments", segments.release());
    } else if (const auto* body = dynamic_cast<const IMultiSegmentBodyMob*>(&mob))
    {
        writer.Field("multi_segment_role", "body");
        writer.Field("multi_segment_index", body->SegmentIndex());
        CaptureHandle(writer, "multi_segment_root", body->RootHandle());
        CaptureHandle(writer, "multi_segment_next", body->NextSegmentHandle());
    }
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
    CaptureHandle(writer, "target_entity", petal.m_target_ref);
    CaptureHandle(writer, "target_release", petal.m_target_release_ref);
    writer.Field("target_entity_id", petal.m_target_ref.id);
    writer.UInt64("target_entity_generation", petal.m_target_ref.generation);
    writer.Field("target_release_id", petal.m_target_release_ref.id);
    writer.UInt64("target_release_generation", petal.m_target_release_ref.generation);
    writer.Field("target_release_timer", petal.m_target_release_timer);
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
    writer.Node("hit_credits", CaptureEntityFloatMap(petal.m_hit_credits).release());

    if (const auto* moon = dynamic_cast<const CMoonPetal*>(&petal))
    {
        writer.Field("moon_orbit_center", moon->m_orbit_center);
        writer.Field("moon_orbit_velocity", moon->m_orbit_velocity);
        writer.Field("moon_orbit_phase", moon->m_orbit_phase);
        writer.Field("moon_orbit_initialized", moon->m_orbit_initialized);
    }

    if (const auto* egg = dynamic_cast<const CBeetleEggPetal*>(&petal))
    {
        CaptureHandle(writer, "summon", egg->m_summon_ref);
        writer.Field("summon_id", egg->m_summon_ref.id);
        writer.UInt64("summon_generation", egg->m_summon_ref.generation);
        writer.Field("has_spawned_summon", egg->m_has_spawned_summon);
    }
    if (const auto* relic = dynamic_cast<const CRelicPetal*>(&petal))
    {
        CaptureHandle(writer, "state_zone", relic->m_state_zone_ref);
        writer.Field("state_zone_id", relic->m_state_zone_ref.id);
        writer.UInt64("state_zone_generation", relic->m_state_zone_ref.generation);
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
    if (const auto* pheromone = dynamic_cast<const CEouPheromonePetal*>(&petal))
    {
        writer.Field("pheromone_team_id", pheromone->m_team_id);
        writer.Field("pheromone_group_id", pheromone->m_group_id);
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
        CaptureHandle(writer, "compass_target", compass->m_compass_target_ref);
        writer.Field("compass_target_id", compass->m_compass_target_ref.id);
        writer.UInt64("compass_target_generation", compass->m_compass_target_ref.generation);
        writer.Field("compass_wait_timer", compass->m_compass_wait_timer);
    }
    if (const auto* glass = dynamic_cast<const CGlassPetal*>(&petal))
        writer.Node("hit_cooldowns", CaptureEntityFloatMap(glass->m_hit_cooldowns).release());
    if (const auto* yggdrasil = dynamic_cast<const CYggdrasilPetal*>(&petal))
        writer.Field("revive_timer", yggdrasil->m_revive_timer);
    if (const auto* magic_eye = dynamic_cast<const CMagicEyePetal*>(&petal))
        writer.Field("magic_eye_trigger_cooldown", magic_eye->m_trigger_cooldown);
    if (const auto* linker = dynamic_cast<const CLinkerPetal*>(&petal))
    {
        CaptureHandle(writer, "first_target", linker->m_first_target_ref);
        CaptureHandle(writer, "chain", linker->m_chain_ref);
        writer.Field("first_target_id", linker->m_first_target_ref.id);
        writer.UInt64("first_target_generation", linker->m_first_target_ref.generation);
        writer.Field("chain_id", linker->m_chain_ref.id);
        writer.UInt64("chain_generation", linker->m_chain_ref.generation);
        writer.Field("saved_chain_health", linker->m_saved_chain_health);
        writer.Field("released_orbit_angle_offset", linker->m_released_orbit_angle_offset);
        writer.Field("charges_remaining", linker->m_charges_remaining);
        writer.Field("released_from_slot", linker->m_released_from_slot);
        writer.Field("chain_finalized", linker->m_chain_finalized);
        writer.Field("has_released_orbit_angle", linker->m_has_released_orbit_angle);
    }
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
    } else if (const auto* chain = dynamic_cast<const CChainEntity*>(&entity))
    {
        writer.Field("kind", "chain");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        const SChainEndpointRef first = chain->FirstRef();
        const SChainEndpointRef second = chain->SecondRef();
        const SChainEndpointRef lease = chain->LeaseRef();
        CaptureHandle(data_writer, "first", first);
        CaptureHandle(data_writer, "second", second);
        CaptureHandle(data_writer, "lease", lease);
        data_writer.Field("first_id", first.id);
        data_writer.UInt64("first_generation", first.generation);
        data_writer.Field("second_id", second.id);
        data_writer.UInt64("second_generation", second.generation);
        data_writer.Field("lease_id", lease.id);
        data_writer.UInt64("lease_generation", lease.generation);
        data_writer.Field("max_length", chain->MaxLength());
        data_writer.Field("max_integrity", chain->MaxIntegrity());
        data_writer.Field("integrity", chain->Integrity());
        data_writer.Field("tension", chain->Tension());
        data_writer.Field("elasticity", chain->Elasticity());
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
        data_writer.Field("decay_base_health", dandelion->m_decay_base_health);
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
        data_writer.Field("decay_base_health", missile->m_decay_base_health);
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
        data_writer.Field("decay_base_health", pollen->m_decay_base_health);
    } else if (const auto* drop = dynamic_cast<const CDrop*>(&entity))
    {
        writer.Field("kind", "drop");
        data = MakeJsonObject();
        CSnapshotWriter data_writer(data.get());
        data_writer.Field("petal_type", static_cast<int>(drop->GetType()));
        data_writer.Field("rarity", static_cast<int>(drop->GetRarity()));
        data_writer.Field("owner_id", drop->GetOwnerId());
        data_writer.Field("owner_account", drop->GetOwnerAccount());
        data_writer.Field("owner_progress_account", drop->GetOwnerProgressAccount());
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
        error = "No entity snapshot codec registered for entity ID " + std::to_string(entity.Id());
        return {};
    }

    if (!data) return {};
    writer.Node("data", data.release());
    return result;
}

bool ReadEntityIdentity(const json_t* record, int& id, std::uint64_t& generation, std::string& error)
{
    constexpr int max_restore_entity_id = 1'000'000;
    CSnapshotReader record_reader(record);
    CSnapshotReader common(record_reader.Node("common"));
    if (!record_reader.IsValid() || !common.IsValid())
    {
        error = "Entity record is not an object";
        return false;
    }
    id = common.Int("id", -1);
    generation = common.UInt64("generation");
    if (id < 0 || id > max_restore_entity_id || generation == 0 ||
        generation == std::numeric_limits<std::uint64_t>::max())
    {
        error = "Entity record has an invalid identity";
        return false;
    }
    return true;
}

bool IsDependentEntityKind(std::string_view kind)
{
    return kind == "petal" || kind == "missile" || kind == "dandelion_missile" ||
           kind == "pollen_projectile" || kind == "trap_projectile" || kind == "chain";
}

int DependentEntityRestoreStage(std::string_view kind)
{
    // Restore owners before their dependants. Petals depend on independent
    // flowers; projectiles may depend on petals; chains may use either a
    // petal or a mob as endpoint/lease. Entity ids do not encode this graph.
    if (kind == "petal") return 0;
    if (kind == "missile" || kind == "dandelion_missile" || kind == "pollen_projectile" ||
        kind == "trap_projectile")
        return 1;
    if (kind == "chain") return 2;
    return -1;
}

bool RestoreMobInitialState(CMobBase& mob, const CSnapshotReader& data, std::string& error)
{
    if (data.Has("base_stats"))
    {
        CSnapshotReader stats(data.Node("base_stats"));
        if (!stats.IsValid()) { error = "Mob base stats are not an object"; return false; }
        mob.RestoreBaseStats(ReadMobBaseStats(stats, *mob.GetBaseStats()));
    }
    mob.SetNaturallySpawned(data.Bool("naturally_spawned"));
    mob.RestoreShortcutContact(data.Bool("shortcut_contact"), data.Float("shortcut_probe_radius"));
    mob.m_vel = data.Vector2("velocity");

    if (auto* root = dynamic_cast<IMultiSegmentRootMob*>(&mob))
    {
        root->ClearSegmentReferences();
        const int state = std::clamp(data.Int("multi_segment_state", static_cast<int>(EMultiSegmentAssemblyState::NotStarted)),
                                     static_cast<int>(EMultiSegmentAssemblyState::NotStarted),
                                     static_cast<int>(EMultiSegmentAssemblyState::Failed));
        root->SetSegmentAssemblyState(static_cast<EMultiSegmentAssemblyState>(state));
        if (const json_t* segments = data.Node("multi_segments"); json_is_array(segments))
        {
            if (!CheckSnapshotArrayLimit(segments, max_snapshot_array_entries, "Mob segment list", error)) return false;
            const size_t count = json_array_size(segments);
            for (size_t i = 0; i < count; ++i)
            {
                CSnapshotReader segment(json_array_get(segments, i));
                if (!segment.IsValid()) continue;
                const int index = segment.Int("index", -1);
                if (index <= 0) continue;
                root->RestoreSegmentReference(RestoreHandle(segment, "entity", mob.GameWorld()), index);
            }
        }
    } else if (auto* body = dynamic_cast<IMultiSegmentBodyMob*>(&mob))
    {
        const int index = data.Int("multi_segment_index", -1);
        body->SetRootReference(RestoreHandle(data, "multi_segment_root", mob.GameWorld()), index);
        body->SetNextSegmentReference(RestoreHandle(data, "multi_segment_next", mob.GameWorld()));
    }

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
    // Runtime handles may point to petals/projectiles or a later mob. Restore
    // them only after all independent and dependent objects have identities.
    return true;
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
    const florrbt::Vector2f pos = common.Vector2("pos");
    std::unique_ptr<CEntity> entity;

    if (kind == "mob")
    {
        EMobType type = EMobType::None;
        ERarity rarity = ERarity::Null;
        if (!ReadKnownMobType(data, "mob_type", type, "Mob entity", error) ||
            !ReadKnownRarity(data, "rarity", rarity, "Mob entity", error))
            return false;
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
        EPetalType petal_type = EPetalType::None;
        ERarity rarity = ERarity::Null;
        if (!ReadKnownPetalType(data, "petal_type", petal_type, "Drop entity", error) ||
            !ReadKnownRarity(data, "rarity", rarity, "Drop entity", error))
            return false;
        auto drop = std::make_unique<CDrop>(&world, pos, petal_type, rarity, data.Int("owner_id", -1),
                                            data.Float("lifetime"),
                                            static_cast<std::uint16_t>(std::clamp(data.Int("stack_num", 1), 0, 65535)));
        drop->RestoreRuntime(data.Float("pickup_delay"), data.Float("merge_timer"), data.Float("lifetime"));
        // A legacy numeric ID cannot prove which account originally owned a
        // private drop. Fail closed, never bind it to a possibly reused ID.
        drop->RestoreOwnerAccount(data.String("owner_account"));
        drop->RestoreOwnerProgressAccount(data.String("owner_progress_account", drop->GetOwnerAccount()));
        entity = std::move(drop);
    } else if (kind == "portal")
    {
        auto portal = std::make_unique<CPortal>(&world, pos, common.Float("radius"), data.String("target_world"));
        portal->SetFromPointName(data.String("from_point"));
        entity = std::move(portal);
    } else if (kind == "blood_sacrifice_ritual")
    {
        EMobType mob_type = EMobType::None;
        ERarity rarity = ERarity::Null;
        if (!ReadKnownMobType(data, "mob_type", mob_type, "Blood sacrifice ritual", error) ||
            !ReadKnownRarity(data, "rarity", rarity, "Blood sacrifice ritual", error))
            return false;
        auto ritual = std::make_unique<CBloodSacrificeRitual>(&world, pos, mob_type, rarity,
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

void RestorePetalRuntime(CPetal& petal, const CSnapshotReader& data, EPetalType type, ERarity rarity)
{
    RestoreProjectile(petal, data);
    petal.SetPetalType(type);
    petal.m_rarity = rarity;
    petal.m_max_slot_num = std::clamp(data.Int("max_slot_num"), 0, static_cast<int>(max_snapshot_array_entries));
    petal.m_copy_index = std::clamp(data.Int("copy_index"), -1,
                                    static_cast<int>(max_snapshot_array_entries) - 1);
    petal.m_slot_index = data.Int("slot_index", -1);
    if (const auto* owner = dynamic_cast<const CFlower*>(petal.GetOwner()); owner)
    {
        if (petal.m_slot_index < 0 || petal.m_slot_index >= static_cast<int>(owner->GetSlots().size()))
            petal.m_slot_index = -1;
    }
    petal.m_target_ref = RestoreHandle(data, "target_entity", petal.GameWorld());
    petal.m_target_release_ref = RestoreHandle(data, "target_release", petal.GameWorld());
    if (!petal.m_target_ref.IsValid() && petal.GameWorld() &&
        !HasSerializedWorldIdentity(data, "target_entity"))
    {
        const int id = data.Int("target_entity_id", -1);
        const std::uint64_t generation = data.UInt64("target_entity_generation");
        if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
            petal.m_target_ref = entity->GetHandle();
    }
    if (!petal.m_target_release_ref.IsValid() && petal.GameWorld() &&
        !HasSerializedWorldIdentity(data, "target_release"))
    {
        const int id = data.Int("target_release_id", -1);
        const std::uint64_t generation = data.UInt64("target_release_generation");
        if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
            petal.m_target_release_ref = entity->GetHandle();
    }
    petal.m_target_release_timer = data.Float("target_release_timer");
    petal.m_lifetime = data.Float("lifetime");
    petal.m_timer = data.Float("timer");
    petal.m_reload_override = data.Float("reload_override", -1.f);
    petal.m_reload_ignore_multiplier = data.Bool("reload_ignore_multiplier");
    petal.m_hidden = data.Bool("hidden");
    petal.m_detach_from_slot = data.Bool("detach_from_slot");
    petal.m_spawn_flight_boost = data.Bool("spawn_flight_boost");
    RestoreEntityFloatMap(data.Node("hit_credits"), petal.GameWorld(), petal.m_hit_credits);

    if (auto* moon = dynamic_cast<CMoonPetal*>(&petal))
    {
        moon->m_orbit_center = data.Vector2("moon_orbit_center");
        // Old snapshots saved only total orbital velocity (no external drift).
        moon->m_orbit_velocity = data.Vector2("moon_orbit_velocity", petal.m_vel);
        moon->m_orbit_phase = data.Float("moon_orbit_phase", game_config::pi);
        moon->m_orbit_initialized = data.Bool("moon_orbit_initialized");
    }

    if (auto* egg = dynamic_cast<CBeetleEggPetal*>(&petal))
    {
        egg->m_summon_ref = RestoreHandle(data, "summon", petal.GameWorld());
        if (!egg->m_summon_ref.IsValid() && petal.GameWorld() &&
            !HasSerializedWorldIdentity(data, "summon"))
        {
            const int id = data.Int("summon_id", -1);
            const std::uint64_t generation = data.UInt64("summon_generation");
            if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
                egg->m_summon_ref = entity->GetHandle();
        }
        egg->m_has_spawned_summon = data.Bool("has_spawned_summon");
    }
    if (auto* relic = dynamic_cast<CRelicPetal*>(&petal))
    {
        relic->m_state_zone_ref = RestoreHandle(data, "state_zone", petal.GameWorld());
        if (!relic->m_state_zone_ref.IsValid() && petal.GameWorld() &&
            !HasSerializedWorldIdentity(data, "state_zone"))
        {
            const int id = data.Int("state_zone_id", -1);
            const std::uint64_t generation = data.UInt64("state_zone_generation");
            if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
                relic->m_state_zone_ref = entity->GetHandle();
        }
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
    if (auto* pheromone = dynamic_cast<CEouPheromonePetal*>(&petal))
    {
        pheromone->m_team_id = data.Int("pheromone_team_id", 0);
        pheromone->m_group_id = data.Int("pheromone_group_id", 0);
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
        compass->m_compass_target_ref = RestoreHandle(data, "compass_target", petal.GameWorld());
        if (!compass->m_compass_target_ref.IsValid() && petal.GameWorld() &&
            !HasSerializedWorldIdentity(data, "compass_target"))
        {
            const int id = data.Int("compass_target_id", -1);
            const std::uint64_t generation = data.UInt64("compass_target_generation");
            if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
                compass->m_compass_target_ref = entity->GetHandle();
        }
        compass->m_compass_wait_timer = data.Float("compass_wait_timer");
    }
    if (auto* glass = dynamic_cast<CGlassPetal*>(&petal))
        RestoreEntityFloatMap(data.Node("hit_cooldowns"), petal.GameWorld(), glass->m_hit_cooldowns);
    if (auto* yggdrasil = dynamic_cast<CYggdrasilPetal*>(&petal))
        yggdrasil->m_revive_timer = data.Float("revive_timer");
    if (auto* magic_eye = dynamic_cast<CMagicEyePetal*>(&petal))
        magic_eye->m_trigger_cooldown = std::max(0.f, data.Float("magic_eye_trigger_cooldown"));
    if (auto* linker = dynamic_cast<CLinkerPetal*>(&petal))
    {
        linker->m_first_target_ref = RestoreHandle(data, "first_target", petal.GameWorld());
        linker->m_chain_ref = RestoreHandle(data, "chain", petal.GameWorld());
        if (!linker->m_first_target_ref.IsValid() && petal.GameWorld() &&
            !HasSerializedWorldIdentity(data, "first_target"))
        {
            const int id = data.Int("first_target_id", -1);
            const std::uint64_t generation = data.UInt64("first_target_generation");
            if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
                linker->m_first_target_ref = entity->GetHandle();
        }
        if (!linker->m_chain_ref.IsValid() && petal.GameWorld() &&
            !HasSerializedWorldIdentity(data, "chain"))
        {
            const int id = data.Int("chain_id", -1);
            const std::uint64_t generation = data.UInt64("chain_generation");
            if (CEntity* entity = ResolveRestoredEntity(petal.GameWorld(), id, generation))
                linker->m_chain_ref = entity->GetHandle();
        }
        linker->m_saved_chain_health = data.Float("saved_chain_health");
        linker->m_released_orbit_angle_offset = data.Float("released_orbit_angle_offset");
        linker->m_charges_remaining = data.Int("charges_remaining", 2);
        linker->m_released_from_slot = data.Bool("released_from_slot");
        linker->m_chain_finalized = data.Bool("chain_finalized");
        linker->m_has_released_orbit_angle = data.Bool("has_released_orbit_angle");
    }
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
    const florrbt::Vector2f pos = common.Vector2("pos");
    SEntityHandle owner_ref = RestoreHandle(data, "owner", &world);
    CEntity* owner = world.ResolveEntity(owner_ref);
    if (!owner && !HasSerializedWorldIdentity(data, "owner"))
    {
        const int owner_id = data.Int("owner_id", -1);
        const std::uint64_t owner_generation = data.UInt64("owner_generation");
        owner = ResolveRestoredEntity(&world, owner_id, owner_generation);
    }
    std::unique_ptr<CEntity> entity;

    if (kind == "chain")
    {
        const SEntityHandle first_ref = RestoreHandle(data, "first", &world);
        const SEntityHandle second_ref = RestoreHandle(data, "second", &world);
        const SEntityHandle lease_ref = RestoreHandle(data, "lease", &world);
        CEntity* first = world.ResolveEntity(first_ref);
        CEntity* second = world.ResolveEntity(second_ref);
        CEntity* lease = world.ResolveEntity(lease_ref);
        if (!first && !HasSerializedWorldIdentity(data, "first"))
            first = ResolveRestoredEntity(&world, data.Int("first_id", -1), data.UInt64("first_generation"));
        if (!second && !HasSerializedWorldIdentity(data, "second"))
            second = ResolveRestoredEntity(&world, data.Int("second_id", -1), data.UInt64("second_generation"));
        if (!lease && !HasSerializedWorldIdentity(data, "lease"))
            lease = ResolveRestoredEntity(&world, data.Int("lease_id", -1), data.UInt64("lease_generation"));
        if (!first || !second || first == second)
        {
            error = "Cannot restore chain " + std::to_string(id) + ": endpoint is missing or duplicated";
            return false;
        }
        if (lease_ref.IsValid() && !lease)
        {
            error = "Cannot restore chain " + std::to_string(id) + ": lease owner is missing";
            return false;
        }
        auto chain = std::make_unique<CChainEntity>(&world, *first, *second, data.Float("max_length"),
                                                    data.Float("max_integrity", 1.f),
                                                    data.Float("elasticity", game_config::default_linker_chain_elasticity),
                                                    lease);
        chain->RestoreState(data.Float("integrity", data.Float("max_integrity", 1.f)), data.Float("tension"));
        entity = std::move(chain);
    } else if (kind == "petal")
    {
        auto* flower = dynamic_cast<CFlower*>(owner);
        EPetalType type = EPetalType::None;
        ERarity rarity = ERarity::Null;
        if (!ReadKnownPetalType(data, "petal_type", type, "Petal entity", error) ||
            !ReadKnownRarity(data, "rarity", rarity, "Petal entity", error))
            return false;
        const CPetalPrototype* prototype = FindPetalPrototype(type);
        if (!flower || !prototype || !prototype->m_factory)
        {
            error = "Cannot restore petal " + data.String("petal_key", "unknown") + " without its owner/factory";
            return false;
        }
        const int requested_slot = data.Int("slot_index", -1);
        const int factory_slot = requested_slot >= 0 && requested_slot < static_cast<int>(flower->GetSlots().size())
                                     ? requested_slot
                                     : -1;
        std::unique_ptr<CPetal> petal = prototype->m_factory(flower, factory_slot, rarity);
        if (!petal)
        {
            error = "Petal factory failed for " + data.String("petal_key", "unknown");
            return false;
        }
        RestorePetalRuntime(*petal, data, type, rarity);
        entity = std::move(petal);
    } else if (kind == "missile" || kind == "dandelion_missile")
    {
        florrbt::Vector2f direction = data.Vector2("velocity", { 1.f, 0.f });
        const float length = std::sqrt(std::max(0.f, LengthSq(direction)));
        if (length > 0.f) direction /= length;
        else direction = { 1.f, 0.f };
        if (kind == "dandelion_missile")
        {
            ERarity rarity = ERarity::Null;
            if (!ReadKnownRarity(data, "rarity", rarity, "Dandelion missile", error)) return false;
            entity = std::make_unique<CDandelionMissile>(
                &world, pos, common.Float("radius"), data.Float("attach_angle"), data.Float("damage"),
                common.Float("health", 1.f), data.Float("lifetime"), rarity, owner);
        } else
        {
            entity = std::make_unique<CMissile>(&world, pos, common.Float("radius"), direction, 0.f,
                                                data.Float("damage"), common.Float("health", 1.f),
                                                data.Float("lifetime"), owner);
        }
        auto* missile = static_cast<CMissile*>(entity.get());
        RestoreProjectile(*missile, data);
        missile->AllowSkipTick() = false;
        missile->m_damage = data.Float("damage");
        missile->m_lifetime = data.Float("lifetime");
        missile->m_age = data.Float("age");
        missile->m_decay_base_health = data.Float("decay_base_health", common.Float("health", 1.f));
        missile->RestoreAttachedToOwner(data.Bool("attached"));
    } else if (kind == "trap_projectile")
    {
        ERarity rarity = ERarity::Null;
        if (!ReadKnownRarity(data, "rarity", rarity, "Trap projectile", error)) return false;
        const florrbt::Vector2f initial_velocity = data.Vector2("initial_velocity");
        const float initial_speed = std::sqrt(std::max(0.f, LengthSq(initial_velocity)));
        const florrbt::Vector2f direction = initial_speed > game_config::entity_collision_epsilon
                                           ? initial_velocity / initial_speed
                                           : florrbt::Vector2f{ 1.f, 0.f };
        auto trap = std::make_unique<CTrapProjectile>(
            &world, pos, common.Float("radius"), direction, initial_speed, data.Float("damage"),
            common.Float("health", 1.f), data.Float("lifetime"), common.Float("mass"),
            data.Float("deceleration_time"), rarity, owner);
        RestoreProjectile(*trap, data);
        trap->AllowSkipTick() = false;
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
        pollen->AllowSkipTick() = false;
        pollen->m_damage = data.Float("damage");
        pollen->m_lifetime = data.Float("lifetime");
        pollen->m_age = data.Float("age");
        pollen->m_decay_base_health = data.Float("decay_base_health", common.Float("health", 1.f));
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
        if (copy_index >= max_snapshot_array_entries)
        {
            error = "Restored petal copy index exceeds the restore limit";
            return false;
        }
        if (copy_index >= slot.m_p_petals.size())
        {
            error = "Restored petal copy index is outside the registered slot layout";
            return false;
        }
        if (slot.m_p_petals[copy_index] && slot.m_p_petals[copy_index] != petal)
        {
            error = "Multiple restored petals claim the same slot copy";
            return false;
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
        world.SetController(nullptr, false);
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
    if (!controller || !controller->CanRestoreSnapshotKey(key))
    {
        std::unique_ptr<IGameController> replacement = CreateWorldController(key);
        if (!replacement)
        {
            error = "No world controller factory registered for " + key;
            return false;
        }
        world.SetController(std::move(replacement), false);
        controller = world.GetController();
    }
    CSnapshotReader data(reader.Node("data"));
    if (!data.IsValid())
    {
        error = "World controller " + key + " has invalid data";
        return false;
    }
    if (!controller->RestoreSnapshot(data, reader.UInt32("version", 1), error)) return false;
    // Import old controller-owned squad state. New writers store it only in
    // the extensions section, which is restored after controllers/players.
    if (data.Has("squads") && world.GameContext())
        return world.GameContext()->Extensions().RestoreLegacySquads(world, data, error);
    return true;
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
        writer.UInt64("next_entity_generation", world->NextEntityGeneration());
        CJsonOwner config = MakeJsonObject(); CSnapshotWriter settings(config.get());
        settings.Field("squads", world->ExtensionConfig().squads);
        settings.Field("weather_visuals", world->ExtensionConfig().weather_visuals);
        settings.Field("weather_spawns", world->ExtensionConfig().weather_spawns);
        writer.Node("extension_config", config.release());
        CJsonOwner controller = CaptureWorldController(world->GetController());
        writer.Node("controller", controller ? controller.release() : json_null());

        std::vector<CEntity*> entities = world->GetAllEntities();
        std::sort(entities.begin(), entities.end(), [](const CEntity* lhs, const CEntity* rhs) {
            return lhs && rhs ? lhs->Id() < rhs->Id() : lhs != nullptr;
        });
        CJsonOwner entity_values = MakeJsonArray();
        std::uint32_t ephemeral_count = 0;
        for (const CEntity* entity : entities)
        {
            if (!entity || entity->IsMarkedForDes()) continue;
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
    CSnapshotReader runtime(data.Node("runtime"));
    if (!mob->RestoreRuntimeSnapshot(runtime, data.UInt32("runtime_version", 1), error)) return false;
    if (auto* hornet = dynamic_cast<IHornetMob*>(mob); hornet && !runtime.Has("special_actions"))
    {
        CSnapshotReader controller(data.Node("controller"));
        if (controller.String("type") == "controller.hornet_special" &&
            !hornet->RestoreSkillActions(CSnapshotReader(controller.Node("data")), controller.UInt32("version", 1), error))
            return false;
    }

    mob->ClearStatesForRestore();
    if (const json_t* states = data.Node("states"); json_is_array(states))
    {
        if (!CheckSnapshotArrayLimit(states, max_snapshot_states, "Mob state list", error)) return false;
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
        if (!CheckSnapshotArrayLimit(damage, max_snapshot_damage_entries, "Mob damage list", error)) return false;
        const size_t count = json_array_size(damage);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader entry(json_array_get(damage, i));
            if (!entry.IsValid()) continue;
            std::string account = entry.String("account_name");
            if (!entry.Has("account_name"))
                if (CPlayer* player = network ? network->FindPlayerById(entry.UInt32("player_id")) : nullptr)
                    account = player->GetAccountName();
            if (account.empty()) continue;
            const float total_damage = std::clamp(entry.Float("total_damage"), 0.f, 1.e30f);
            const float reset_timer = std::clamp(entry.Float("reset_timer"), 0.f, 86400.f);
            mob->GetDamageData().push_back({ account, total_damage, reset_timer, entry.String("progress_account") });
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

    if (!CheckSnapshotArrayLimit(data, max_snapshot_worlds, "World list", error)) return false;
    const size_t world_count = json_array_size(data);
    std::unordered_set<std::uint32_t> seen_world_ids;
    seen_world_ids.reserve(world_count);
    for (size_t world_index = 0; world_index < world_count; ++world_index)
    {
        CSnapshotReader world_reader(json_array_get(data, world_index));
        if (!world_reader.IsValid())
        {
            error = "World snapshot entry is not an object";
            return false;
        }
        const std::uint32_t world_id = world_reader.UInt32("world_id");
        if (!seen_world_ids.insert(world_id).second)
        {
            error = "Snapshot contains duplicate world ID " + std::to_string(world_id);
            return false;
        }
        CGameWorld* world = server.FindWorldById(world_id);
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
        if (!CheckSnapshotArrayLimit(entities, max_snapshot_entities_per_world, "World entity list", error))
            return false;
        const size_t entity_count = json_array_size(entities);

        if (phase == ESnapshotRestorePhase::CreateObjects)
        {
            world->ClearEntitiesForRestore();
            if (!world->ReloadMapForRestore(error)) return false;
            SWorldExtensionConfig config = map_declarations::ExtensionsForMap(world->GetMapPath());
            CSnapshotReader saved_controller(world_reader.Node("controller"));
            config.squads = saved_controller.String("type") != world_controller_keys::waiting;
            config.weather_spawns = saved_controller.String("type") == "world_controller.weather_diurnal_open";
            if (world_reader.Has("extension_config"))
            {
                CSnapshotReader settings(world_reader.Node("extension_config"));
                if (!settings.IsValid()) { error = "Invalid world extension configuration"; return false; }
                config = {settings.Bool("squads"), settings.Bool("weather_visuals"), settings.Bool("weather_spawns")};
            }
            // Recreate the scope even when restoring into the same world.
            if (world->GameContext())
                world->GameContext()->Extensions().DetachWorld({world->GetId(), world->GetInstanceGeneration()});
            world->ConfigureExtensions(config);
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
            for (int stage = 0; stage <= 2; ++stage)
            {
                for (size_t i = 0; i < entity_count; ++i)
                {
                    const json_t* record = json_array_get(entities, i);
                    CSnapshotReader record_reader(record);
                    if (DependentEntityRestoreStage(record_reader.String("kind")) != stage) continue;
                    if (!CreateDependentEntity(*world, record, error)) return false;
                }
            }
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
            ResolveMultiSegmentReferences(*world);
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
            if (!world->RestoreNextEntityGeneration(world_reader.UInt64("next_entity_generation", world->NextEntityGeneration())))
            { error = "Invalid next entity generation"; return false; }
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

CJsonOwner CaptureProgressScope(const CPlayer& player, std::string& error)
{
    SPlayerAccount state;
    if (!CAccountDataStore::CaptureSandboxAccount(player.GetProgressAccountName(), state))
    { error = "Missing isolated progress for " + player.GetAccountName(); return {}; }
    auto result = MakeJsonObject(); CSnapshotWriter writer(result.get());
    writer.Field("world_id", player.ProgressWorld().id);
    writer.UInt64("world_generation", player.ProgressWorld().generation);
    writer.Field("level", state.level); writer.Int64("exp", state.exp);
    writer.Field("talent_points", state.talent_points);
    auto items = [&](const char* key, const std::vector<SInventoryItem>& values) {
        auto array = MakeJsonArray();
        for (const auto& value : values)
            AppendJson(array.get(), json_pack("[i,i,I]", int(value.petal_type), int(value.rarity), static_cast<json_int_t>(value.count)));
        writer.Node(key, array.release());
    };
    items("inventory", state.inventory); items("slots", state.slots); items("secondary_slots", state.secondary_slots);
    auto talents = MakeJsonArray();
    for (const auto& talent : state.talents)
        AppendJson(talents.get(), json_pack("[i,i,i]", int(talent.id), int(talent.rarity), talent.rank));
    writer.Node("talents", talents.release());
    auto codex = MakeJsonArray();
    for (const auto& entry : state.codex)
        AppendJson(codex.get(), json_pack("[i,i,I]", int(entry.mob_type), int(entry.rarity), static_cast<json_int_t>(entry.count)));
    writer.Node("codex", codex.release());
    return result;
}

bool RestoreProgressScope(CPlayer& player, CGameWorld* world, const CSnapshotReader& reader, std::string& error)
{
    const SWorldHandle scope{reader.UInt32("world_id"), reader.UInt64("world_generation")};
    if (!reader.IsValid() || !world || scope != SWorldHandle{world->GetId(), world->GetInstanceGeneration()})
    { error = "Invalid isolated progress world identity"; return false; }
    SPlayerAccount state;
    state.level = reader.Int("level", -1); state.exp = reader.Int64("exp", -1);
    state.talent_points = reader.Int("talent_points", -1);
    if (state.level < 1 || state.exp < 0 || state.talent_points < 0)
    { error = "Invalid isolated progress values"; return false; }
    auto triples = [&](const char* key, size_t limit, auto accept) {
        const auto* values = reader.Node(key);
        if (!json_is_array(values) || json_array_size(values) > limit) return false;
        for (size_t i = 0; i < json_array_size(values); ++i)
        {
            const auto* value = json_array_get(values, i);
            if (!json_is_array(value) || json_array_size(value) != 3) return false;
            for (size_t n = 0; n < 3; ++n) if (!json_is_integer(json_array_get(value, n))) return false;
            if (!accept(json_integer_value(json_array_get(value, 0)), json_integer_value(json_array_get(value, 1)),
                        json_integer_value(json_array_get(value, 2)))) return false;
        }
        return true;
    };
    auto items = [&](const char* key, std::vector<SInventoryItem>& output, bool slots) {
        return triples(key, slots ? max_snapshot_slots : 65536, [&](json_int_t type, json_int_t rarity, json_int_t count) {
            if (slots && type == 0 && rarity == 0 && count == 0) { output.push_back({}); return true; }
            if (type <= 0 || type > UINT8_MAX || rarity <= 0 || rarity > UINT8_MAX ||
                !IsKnownPetalTypeId(int(type)) || !IsKnownRarity(static_cast<ERarity>(rarity)) ||
                count < 0 || count > max_inventory_item_count) return false;
            output.push_back({std::uint8_t(type), std::uint8_t(rarity), std::uint32_t(count)}); return true;
        });
    };
    if (!items("inventory", state.inventory, false) || !items("slots", state.slots, true) ||
        !items("secondary_slots", state.secondary_slots, true) ||
        !triples("talents", 4096, [&](json_int_t id, json_int_t rarity, json_int_t rank) {
            if (id < 0 || id > UINT8_MAX || rarity < 0 || rarity > UINT8_MAX || rank < 0 || rank > INT_MAX ||
                !FindBuiltinTalent(static_cast<ETalentId>(id), static_cast<ERarity>(rarity), int(rank))) return false;
            state.talents.push_back({static_cast<ETalentId>(id), static_cast<ERarity>(rarity), int(rank)}); return true;
        }) || !triples("codex", 65536, [&](json_int_t type, json_int_t rarity, json_int_t count) {
            if (type <= 0 || type > UINT8_MAX || rarity <= 0 || rarity > UINT8_MAX || count < 0 || count > UINT32_MAX ||
                !IsKnownMobTypeId(int(type)) || !IsKnownRarity(static_cast<ERarity>(rarity))) return false;
            state.codex.push_back({std::uint8_t(type), std::uint8_t(rarity), std::uint32_t(count)}); return true;
        }))
    { error = "Invalid isolated inventory/talent/codex state"; return false; }
    return player.RestoreIsolatedProgress(scope, state);
}

CJsonOwner CapturePlayers(CServer& server, std::string& error)
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
        writer.UInt64("session_generation", player->SessionGeneration());
        writer.Field("name", player->GetName());
        writer.Field("account_name", player->GetAccountName());
        if (player->HasIsolatedProgress())
        {
            auto progress = CaptureProgressScope(*player, error);
            if (!progress) return {};
            writer.Node("isolated_progress", progress.release());
        }
        writer.Field("remote_address", player->GetRemoteAddress());
        writer.Field("mute_timer", player->GetMuteTimer());
        writer.Field("report_disabled", player->IsReportDisabled());
        writer.Field("invalid_report_count", player->GetInvalidReportCount());
        writer.Field("second_chance_cooldown", player->GetSecondChanceCooldown());
        writer.Field("use_new_player_spawn", player->GetUseNewPlayerSpawn());
        writer.Field("talent_points", player->GetTalentPoints());

        CEntity* entity = player->GetEntity();
        const SEntityHandle entity_ref = entity ? entity->GetHandle() : SEntityHandle{};
        writer.Field("world_id", entity_ref.world_id);
        writer.UInt64("world_instance_generation", entity_ref.world_instance_generation);
        writer.Field("entity_id", entity_ref.id);
        writer.UInt64("entity_generation", entity_ref.generation);

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
    if (version < 1 || version > 2)
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
        if (!CheckSnapshotArrayLimit(data, max_snapshot_players, "Player list", error)) return false;
        const size_t count = json_array_size(data);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader reader(json_array_get(data, i));
            if (!reader.IsValid())
            {
                error = "Player snapshot entry is not an object";
                return false;
            }
            const std::uint32_t player_id = reader.UInt32("player_id");
            const std::string account_name = reader.String("account_name");
            if (account_name.starts_with("@sandbox:"))
            { error = "Legacy sandbox snapshot has no recoverable login identity/progress; exit sandbox before upgrading"; return false; }
            if (player_id == 0 || player_id >= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                account_name.empty())
            {
                error = "Player snapshot has an invalid identity";
                return false;
            }

            sf::TcpSocket socket;
            auto player = std::make_unique<CPlayer>(std::move(socket), player_id, reader.String("name", account_name));
            if (reader.Has("session_generation") && !player->RestoreSessionGeneration(reader.UInt64("session_generation")))
            { error = "Invalid player session generation"; return false; }
            player->Authenticate(account_name);

            CGameWorld* world = server.FindWorldById(reader.UInt32("world_id"));
            SEntityHandle entity_ref;
            entity_ref.id = reader.Int("entity_id", -1);
            entity_ref.generation = reader.UInt64("entity_generation");
            entity_ref.world_id = reader.UInt32("world_id");
            entity_ref.world_instance_generation = reader.UInt64("world_instance_generation");
            CEntity* entity = world ? world->ResolveEntity(entity_ref) : nullptr;
            if (!entity && world && !entity_ref.IsValid() && entity_ref.id >= 0 &&
                !(reader.Has("world_id") && reader.Has("world_instance_generation")))
                entity = world->GetEntity(entity_ref.id);
            if (reader.Int("entity_id", -1) >= 0 && !entity)
            {
                error = "Player " + account_name + " references a missing controlled entity";
                return false;
            }
            player->SetOwnedEntity(entity);
            if (reader.Has("isolated_progress") &&
                !RestoreProgressScope(*player, world, CSnapshotReader(reader.Node("isolated_progress")), error)) return false;
            player->LoadSavedTalents(false);
            player->LoadCodex(CAccountDataStore::GetCodex(player->GetProgressAccountName()));
            player->RestoreTalentPoints(std::max(0, reader.Int("talent_points", player->GetTalentPoints())));
            player->RestoreDisconnectedSession(
                reader.String("remote_address"), game_config::timeout_protection_seconds, reader.Float("mute_timer"),
                reader.Bool("report_disabled"), reader.Int("invalid_report_count"),
                reader.Float("second_chance_cooldown"), reader.Bool("use_new_player_spawn"));

            player->m_cp_stack.clear();
            if (const json_t* checkpoints = reader.Node("checkpoints"); json_is_array(checkpoints))
            {
                if (!CheckSnapshotArrayLimit(checkpoints, max_snapshot_checkpoints, "Player checkpoint list", error))
                    return false;
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

    if (phase == ESnapshotRestorePhase::RestoreState)
    {
        for (size_t i = 0; i < json_array_size(data); ++i)
        {
            CSnapshotReader saved(json_array_get(data, i));
            auto* player = network->FindPlayerById(saved.UInt32("player_id"));
            if (!player) { error = "Missing player while restoring progress policy"; return false; }
            auto* entity = player->GetEntity();
            auto* world = entity ? entity->GameWorld() : nullptr;
            const bool isolated = world && world->GetController() && world->GetController()->UsesIsolatedProgress();
            if (player->HasIsolatedProgress() != isolated)
            {
                if (!saved.Bool("legacy_progress_scope") || !isolated || !player->SynchronizeGameplayScope(*world))
                { error = "Player progress scope disagrees with world policy"; return false; }
            }
        }
        return true;
    }

    if (phase == ESnapshotRestorePhase::RebuildDerivedData)
    {
        network->FinalizePlayerRestore();
        for (const auto& player : network->GetPlayers())
            if (player && !player->IsConnected()) player->ResetControlledMob();
        return true;
    }

    if (phase == ESnapshotRestorePhase::Validate)
    {
        if (!CheckSnapshotArrayLimit(data, max_snapshot_players, "Player list", error)) return false;
        const size_t count = json_array_size(data);
        for (size_t i = 0; i < count; ++i)
        {
            CSnapshotReader reader(json_array_get(data, i));
            if (!reader.IsValid())
            {
                error = "Player snapshot entry is not an object";
                return false;
            }
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

bool MigratePlayers(std::uint32_t version, json_t* data, std::string& error)
{
    if (version != 1 || !json_is_array(data) || json_array_size(data) > max_snapshot_players)
    { error = "Unsupported legacy player snapshot"; return false; }
    for (size_t i = 0; i < json_array_size(data); ++i)
    {
        json_t* player = json_array_get(data, i);
        CSnapshotReader saved(player);
        if (!saved.IsValid() || saved.String("account_name").starts_with("@sandbox:"))
        { error = "Legacy sandbox snapshot has no recoverable login identity/progress; exit sandbox before upgrading"; return false; }
        json_object_set_new(player, "legacy_progress_scope", json_true());
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

bool RestoreWeatherDiurnal(CServer& server, const json_t* data, std::uint32_t version,
                           ESnapshotRestorePhase phase, std::string& error)
{
    if (phase != ESnapshotRestorePhase::RestoreState) return true;
    auto* weather = server.Extensions().Globals().Find<CWeatherExtension>();
    return weather && RestoreWeatherSnapshot(*weather, data, version, error);
}

CJsonOwner CaptureExtensions(CServer& server, std::string&)
{
    auto result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    server.Extensions().CaptureSnapshot(writer);
    return result;
}

bool RestoreExtensions(CServer& server, const json_t* data, std::uint32_t version,
                       ESnapshotRestorePhase phase, std::string& error)
{
    if (phase != ESnapshotRestorePhase::RestoreState) return true;
    return server.Extensions().RestoreSnapshot(CSnapshotReader(data), version, server.GetWorlds(), error);
}
bool MigrateWeatherDiurnal(std::uint32_t from_version, json_t* data, std::string& error)
{
    if (from_version < 1 || from_version > 3 || !json_is_object(data))
    {
        error = "Cannot migrate weather-diurnal section from version " + std::to_string(from_version);
        return false;
    }

    CSnapshotWriter writer(data);
    if (from_version == 1)
    {
        writer.UInt64("environment_revision", 1);
        const SMeteorology defaults;
        writer.Field("humidity", defaults.humidity);
        writer.Field("wind_direction_degrees", defaults.wind_direction_degrees);
        writer.Field("wind_speed", defaults.wind_speed);
        writer.Field("temperature_celsius", defaults.temperature_celsius);
        writer.Field("has_forced_weather", false);
        writer.Field("has_meteorology_override", false);
    }

    // Versions 1..3 store a discrete from/to weather plus a transition factor; convert it
    // to the per-type intensity model used by version 4.
    {
        CSnapshotReader reader(data);
        const int weather_from = reader.Int("weather_from", static_cast<int>(EWeatherType::Clear));
        const int weather_to = reader.Int("weather_to", static_cast<int>(EWeatherType::Clear));
        const float transition = reader.Float("weather_transition", 1.f);
        float intensity[weather_type_count] = {};
        if (IsKnownWeatherType(static_cast<EWeatherType>(weather_from)))
            intensity[weather_from] = 1.f - transition;
        if (IsKnownWeatherType(static_cast<EWeatherType>(weather_to)))
            intensity[weather_to] = transition;
        for (int value = 0; value < static_cast<int>(EWeatherType::Count); ++value)
            writer.Field("weather_intensity_" + std::to_string(value), intensity[value]);
        writer.Field("has_forced_intensity", false);
    }

    writer.Field("day_duration_seconds", static_cast<double>(game_config::weather_diurnal_day_duration));
    return true;
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
    if (!CheckSnapshotArrayLimit(data, max_snapshot_array_entries, "Titan forge cooldown list", error)) return false;
    const size_t count = json_array_size(data);
    for (size_t i = 0; i < count; ++i)
    {
        CSnapshotReader reader(json_array_get(data, i));
        const json_t* type_value = reader.Node("type");
        const json_t* remaining_value = reader.Node("remaining_seconds");
        const int type = reader.Int("type", -1);
        const float remaining = reader.Float("remaining_seconds", -1.f);
        if (!reader.IsValid() || !json_is_integer(type_value) || !json_is_number(remaining_value) ||
            !IsKnownPetalTypeId(type) || !std::isfinite(remaining) || remaining < 0.f)
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
    // Object factories consume randomness while reconstructing instances.
    // Resume the saved stream only after those factories and reference repair.
    if (phase != ESnapshotRestorePhase::RebuildDerivedData) return true;
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

CJsonOwner CaptureWorldTopology(CServer& server, std::string& error)
{
    IWorldModule* module = server.GetWorldModule();
    if (!module) { error = "World module unavailable"; return {}; }
    const auto topology = module->CaptureTopology();
    CJsonOwner result = MakeJsonObject();
    CSnapshotWriter writer(result.get());
    writer.Field("main_world", topology.main_world);
    CJsonOwner worlds = MakeJsonArray();
    for (const auto& world : topology.worlds)
    {
        CJsonOwner value = MakeJsonObject();
        CSnapshotWriter entry(value.get());
        entry.Field("id", world.id);
        entry.UInt64("generation", world.generation);
        entry.Field("map_path", world.map_path);
        AppendJson(worlds.get(), value.release());
    }
    writer.Node("worlds", worlds.release());
    CJsonOwner rooms = MakeJsonArray();
    for (const auto& room : topology.rooms)
    {
        CJsonOwner value = MakeJsonObject();
        CSnapshotWriter entry(value.get());
        entry.Field("code", room.code);
        entry.Field("active_world", room.active_world);
        entry.Field("waiting_world", room.waiting_world);
        entry.Field("empty_seconds", room.empty_seconds);
        AppendJson(rooms.get(), value.release());
    }
    writer.Node("rooms", rooms.release());
    return result;
}

bool ReadWorldTopology(const json_t* data, IWorldModule::STopologySnapshot& result, std::string& error)
{
    CSnapshotReader reader(data);
    if (!reader.IsValid() || !CheckSnapshotArrayLimit(reader.Node("worlds"), max_snapshot_worlds, "Topology worlds", error) ||
        !CheckSnapshotArrayLimit(reader.Node("rooms"), max_snapshot_worlds, "Topology rooms", error))
    { if (error.empty()) error = "Invalid world topology"; return false; }
    result.main_world = reader.UInt32("main_world", IWorldModule::SWorldRef::invalid_id);
    const json_t* worlds = reader.Node("worlds");
    for (size_t i = 0; i < json_array_size(worlds); ++i)
    {
        CSnapshotReader entry(json_array_get(worlds, i));
        result.worlds.push_back({entry.UInt32("id", IWorldModule::SWorldRef::invalid_id),
                                entry.UInt64("generation"), entry.String("map_path")});
    }
    const json_t* rooms = reader.Node("rooms");
    for (size_t i = 0; i < json_array_size(rooms); ++i)
    {
        CSnapshotReader entry(json_array_get(rooms, i));
        result.rooms.push_back({entry.Int("code"), entry.UInt32("active_world", IWorldModule::SWorldRef::invalid_id),
                               entry.UInt32("waiting_world", IWorldModule::SWorldRef::invalid_id), entry.Float("empty_seconds")});
    }
    return true;
}

bool RestoreWorldTopology(CServer& server, const json_t* data, std::uint32_t version,
                          ESnapshotRestorePhase phase, std::string& error)
{
    if (version != 1) { error = "Unsupported world topology version"; return false; }
    if (phase != ESnapshotRestorePhase::CreateObjects) return true;
    IWorldModule::STopologySnapshot topology;
    return server.GetWorldModule() && ReadWorldTopology(data, topology, error) &&
           server.GetWorldModule()->RestoreTopology(topology, error);
}

void EnsureBuiltinSections()
{
    static std::once_flag once;
    std::call_once(once, []() {
        // Optional when reading pre-topology snapshots of static worlds.
        RegisterSnapshotSection({ "world_topology", 1, false, CaptureWorldTopology, RestoreWorldTopology, {} });
        RegisterSnapshotSection({ "rng", 1, true, CaptureRng, RestoreRng, {} });
        RegisterSnapshotSection(
            { "titan_forge_cooldowns", 1, false, CaptureTitanForgeCooldowns, RestoreTitanForgeCooldowns, {} });
        RegisterSnapshotSection(
            { "weather_diurnal", 4, false, {}, RestoreWeatherDiurnal, MigrateWeatherDiurnal, false });
        RegisterSnapshotSection({ "worlds", 1, true, CaptureWorlds, RestoreWorlds, {} });
        RegisterSnapshotSection({ "players", 2, true, CapturePlayers, RestorePlayers, MigratePlayers });
        RegisterSnapshotSection({ "extensions", 1, false, CaptureExtensions, RestoreExtensions, {} });
    });
}

bool IsKnownEntitySnapshotKind(std::string_view kind)
{
    return kind == "mob" || kind == "drop" || kind == "portal" || kind == "blood_sacrifice_ritual" ||
           kind == "petal" || kind == "missile" || kind == "dandelion_missile" || kind == "trap_projectile" ||
           kind == "pollen_projectile" || kind == "chain";
}

// Validate the shape and identities of the destructive sections before the
// first restore phase clears live worlds or players.  The phase callbacks
// still validate their semantic fields, but malformed top-level data must not
// be allowed to leave a server half-restored merely because a later entry was
// bad.
bool ValidateSnapshotStructure(CServer& server, const json_t* sections, std::string& error)
{
    std::unordered_map<std::uint32_t, std::string> topology_maps;
    const json_t* topology_section = json_object_get(sections, "world_topology");
    if (topology_section)
    {
        IWorldModule::STopologySnapshot topology;
        if (!ReadWorldTopology(CSnapshotReader(topology_section).Node("data"), topology, error) ||
            !server.GetWorldModule() || !server.GetWorldModule()->ValidateTopology(topology, error)) return false;
        for (const auto& world : topology.worlds) topology_maps.emplace(world.id, world.map_path);
    }
    const json_t* worlds_section = json_object_get(sections, "worlds");
    if (worlds_section)
    {
        CSnapshotReader section_reader(worlds_section);
        const json_t* worlds = section_reader.Node("data");
        if (!CheckSnapshotArrayLimit(worlds, max_snapshot_worlds, "World list", error)) return false;
        if (topology_section && json_array_size(worlds) != topology_maps.size())
        { error = "World topology and entity sections disagree"; return false; }

        std::unordered_set<std::uint32_t> seen_world_ids;
        for (size_t world_index = 0; world_index < json_array_size(worlds); ++world_index)
        {
            CSnapshotReader world_reader(json_array_get(worlds, world_index));
            if (!world_reader.IsValid())
            {
                error = "World snapshot entry is not an object";
                return false;
            }

            const std::uint32_t world_id = world_reader.UInt32("world_id");
            if (!seen_world_ids.insert(world_id).second)
            {
                error = "Snapshot contains duplicate world ID " + std::to_string(world_id);
                return false;
            }
            CGameWorld* world = server.FindWorldById(world_id);
            const auto blueprint = topology_maps.find(world_id);
            const std::string* expected_map = topology_section
                ? (blueprint == topology_maps.end() ? nullptr : &blueprint->second)
                : (world ? &world->GetMapPath() : nullptr);
            if (!expected_map)
            {
                error = "Snapshot references an unavailable world (legacy snapshots do not contain room topology)";
                return false;
            }
            if (*expected_map != world_reader.String("map_path"))
            {
                error = "World " + std::to_string(world_id) + " map does not match the running world";
                return false;
            }

            const json_t* entities = world_reader.Node("entities");
            if (!CheckSnapshotArrayLimit(entities, max_snapshot_entities_per_world, "World entity list", error))
                return false;
            std::unordered_set<int> seen_entity_ids;
            for (size_t entity_index = 0; entity_index < json_array_size(entities); ++entity_index)
            {
                const json_t* record = json_array_get(entities, entity_index);
                CSnapshotReader record_reader(record);
                if (!record_reader.IsValid() || record_reader.UInt32("version", 1) != 1)
                {
                    error = "Invalid entity snapshot record at world " + std::to_string(world_id);
                    return false;
                }
                const std::string kind = record_reader.String("kind");
                if (!IsKnownEntitySnapshotKind(kind))
                {
                    error = "Unknown entity snapshot kind: " + kind;
                    return false;
                }
                CSnapshotReader data(record_reader.Node("data"));
                if (!data.IsValid())
                {
                    error = "Entity snapshot data is not an object";
                    return false;
                }
                int id = -1;
                std::uint64_t generation = 0;
                if (!ReadEntityIdentity(record, id, generation, error)) return false;
                if (!seen_entity_ids.insert(id).second)
                {
                    error = "Snapshot contains duplicate entity ID " + std::to_string(id) + " in world " +
                            std::to_string(world_id);
                    return false;
                }
            }
        }
    }

    const json_t* players_section = json_object_get(sections, "players");
    if (players_section)
    {
        CSnapshotReader section_reader(players_section);
        const json_t* players = section_reader.Node("data");
        if (!CheckSnapshotArrayLimit(players, max_snapshot_players, "Player list", error)) return false;
        std::unordered_set<std::uint32_t> seen_player_ids;
        std::unordered_set<std::uint64_t> controlled_entity_slots;
        for (size_t player_index = 0; player_index < json_array_size(players); ++player_index)
        {
            CSnapshotReader player_reader(json_array_get(players, player_index));
            const std::uint32_t player_id = player_reader.UInt32("player_id");
            if (!player_reader.IsValid() || player_id == 0 ||
                player_id >= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                player_reader.String("account_name").empty())
            {
                error = "Invalid player snapshot entry";
                return false;
            }
            if (!seen_player_ids.insert(player_id).second)
            {
                error = "Snapshot contains duplicate player ID " + std::to_string(player_id);
                return false;
            }
            const int entity_id = player_reader.Int("entity_id", -1);
            if (entity_id >= 0)
            {
                const std::uint64_t slot = (std::uint64_t{player_reader.UInt32("world_id")} << 32u) |
                                           static_cast<std::uint32_t>(entity_id);
                if (!controlled_entity_slots.insert(slot).second)
                {
                    error = "Snapshot assigns the same controlled entity to multiple players";
                    return false;
                }
            }
            const json_t* checkpoints = player_reader.Node("checkpoints");
            if (checkpoints && !CheckSnapshotArrayLimit(checkpoints, max_snapshot_checkpoints,
                                                        "Player checkpoint list", error))
                return false;
        }
    }
    return true;
}
} // namespace

bool RegisterSnapshotSection(SSnapshotSectionCodec codec, std::string* error)
{
    if (codec.key.empty() || codec.version == 0 || (codec.write && !codec.capture) || !codec.restore)
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
    if (server.GetWorldModule() && server.GetWorldModule()->IsSimulationActive())
    { error = "Cannot capture an incomplete world simulation frame"; return false; }
    for (CGameWorld* world : server.GetWorlds())
        if (world && world->IsTransitionActive()) { error = "Cannot capture a transition in progress"; return false; }
    for (CGameWorld* world : server.GetWorlds())
        if (world) world->FlushEntityRemovals();
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
        if (!codec.write) continue;
        CJsonOwner data = codec.capture(server, error);
        if (!data)
        {
            if (error.empty()) error = "Failed to capture snapshot section " + codec.key;
            return false;
        }
        CJsonOwner section = MakeJsonObject();
        CSnapshotWriter section_writer(section.get());
        section_writer.Field("version", codec.version);
        section_writer.Field("required", codec.required || codec.key == "extensions");
        section_writer.Node("data", data.release());
        json_object_set_new(sections.get(), codec.key.c_str(), section.release());
    }
    writer.Node("sections", sections.release());
    return WriteJsonAtomically(path, root.get(), error);
}

bool CHotReloadSnapshotService::Restore(CServer& server, const std::filesystem::path& path, std::string& error)
{
    // Restore is destructive: CreateObjects clears live entity/player storage.
    // Keep one in-process rollback snapshot so a semantic failure in a later
    // phase cannot strand the server in a half-restored state.  The guard
    // prevents the rollback restore itself from recursively creating another
    // backup of the already-mutated state.
    static thread_local bool rollback_in_progress = false;
    std::filesystem::path rollback_path;
    bool rollback_ready = false;

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
    json_t* sections = const_cast<json_t*>(root_reader.Node("sections"));
    if (!json_is_object(sections))
    {
        error = "Snapshot sections object is missing";
        return false;
    }
    if (root_reader.UInt32("container_version") >= 2 && !json_object_get(sections, "extensions"))
    { error = "Required extensions section is missing"; return false; }

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

    if (!ValidateSnapshotStructure(server, sections, error)) return false;

    if (!rollback_in_progress)
    {
        rollback_path = path;
        rollback_path += ".before_restore." + std::to_string(server.GetTick());
        std::string backup_error;
        if (!Capture(server, rollback_path, backup_error))
        {
            error = "Cannot create hot-reload rollback snapshot: " + backup_error;
            return false;
        }
        rollback_ready = true;
    }

    server.SetTickForRestore(root_reader.UInt64("tick"));

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

                const std::string restore_error = error;
                if (rollback_ready)
                {
                    std::string rollback_error;
                    rollback_in_progress = true;
                    const bool rolled_back = CHotReloadSnapshotService::Restore(server, rollback_path, rollback_error);
                    rollback_in_progress = false;
                    std::error_code cleanup_error;
                    std::filesystem::remove(rollback_path, cleanup_error);
                    if (!rolled_back)
                    {
                        error = restore_error + "; rollback failed: " + rollback_error;
                        LOG_ERROR("hot_reload", error);
                    }
                    else
                    {
                        error = restore_error;
                        LOG_WARN("hot_reload", "Snapshot restore failed and live state was rolled back: " + error);
                    }
                }
                return false;
            }
        }
    }

    if (rollback_ready)
    {
        std::error_code cleanup_error;
        std::filesystem::remove(rollback_path, cleanup_error);
    }
    return true;
}
