#include "opencontroller.h"

void COpenController::OnTick(CGameWorld& world, float dt) { m_open_rules.OnTick(world, dt); }
std::optional<florrbt::Vector2f> COpenController::SelectPlayerSpawn(CGameWorld& world, CPlayer& player, EPlayerSpawnReason reason)
{ return m_open_rules.SelectPlayerSpawn(world, player, reason); }
void COpenController::OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason)
{ m_open_rules.OnEntityRemoved(world, entity, reason); }
void COpenController::OnMobDefeated(CGameWorld& world, CMobBase& mob) { m_open_rules.OnMobDefeated(world, mob); }
EMobType COpenController::PickHabitatMobType(CGameWorld& world, florrbt::Vector2f pos) { return m_open_rules.PickHabitatMobType(world, pos); }
void COpenController::CaptureSnapshot(CSnapshotWriter& writer) const { m_open_rules.CaptureSnapshot(writer); }
bool COpenController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{ return m_open_rules.RestoreSnapshot(reader, version, error); }
void COpenController::SpawnMobs(CGameWorld& world) { m_open_rules.SpawnMobs(world); }
