#include "network_module.h"
#include "../server.h"
#include "../Game/player.h"
#include "../Game/gameworld.h"
#include "../HotReload/snapshot_archive.h"
#include <cstdlib>

bool INetworkModule::QueueClientFeatures(CPlayer& player, bool force)
{
    const auto* world = player.GetWorld(); const auto* owner = player.GetEntity();
    if (!player.IsConnected() || !player.IsAuthenticated() || !world || !owner || world->IsRecovering()) return false;
    if (!m_scene_build_player && m_pending_scene_syncs.contains(player.GetId())) return false;
    auto features = m_server.Extensions().ClientFeatures(player);
    const auto session = player.SessionRef(); const SWorldHandle world_ref{world->GetId(), world->GetInstanceGeneration()};
    auto& cached = m_feature_states[player.GetId()];
    if (!force && cached.session == session && cached.world == world_ref && cached.owner == owner->Id() &&
        cached.owner_generation == owner->Generation() && cached.features == features) return true;
    auto data = MakeJsonObject(); CSnapshotWriter writer(data.get());
    writer.Field("schema", 1); writer.Field("player", player.GetId()); writer.UInt64("session", player.SessionGeneration());
    writer.Field("world", world->GetId()); writer.UInt64("worldGeneration", world->GetInstanceGeneration());
    writer.Field("owner", owner->Id()); writer.UInt64("ownerGeneration", owner->Generation());
    auto values = MakeJsonArray();
    for (const auto& item : features)
    {
        auto value = MakeJsonObject(); CSnapshotWriter entry(value.get());
        entry.Field("key", item.key); entry.Field("title", item.title); entry.Field("icon", item.icon);
        entry.Field("surfaces", int(item.surfaces)); entry.Field("interaction", int(item.interaction)); entry.Field("order", item.order);
        entry.Field("visible", item.visible); entry.Field("badge", item.badge); entry.Field("access", int(item.access)); entry.Field("reason", item.reason);
        AppendJson(values.get(), value.release());
    }
    writer.Node("features", values.release());
    const auto revision = cached.revision + 1u;
    writer.Field("revision", revision);
    char* raw = json_dumps(data.get(), JSON_COMPACT);
    if (!raw) return false;
    ServerMessage message; message.type = ServerMessage::Type::ClientFeatures; message.service_payload = raw; std::free(raw);
    if (!QueueMessage(player, message)) return false;
    cached = {session, world_ref, owner->Id(), owner->Generation(), std::move(features), revision}; return true;
}
