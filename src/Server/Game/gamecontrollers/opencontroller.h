#pragma once
#include "open_spawn_director.h"
#include "../gamecontroller.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

class CPlayer;

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnActivate(CGameWorld& world) override;
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<sf::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player,
                                                  EPlayerSpawnReason reason) override;
    void OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason) override;
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override;
    std::string_view SnapshotKey() const override { return "world_controller.open"; }
    std::uint32_t SnapshotVersion() const override { return 2; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;
    void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx) override;
    void SpawnMobs(CGameWorld& world);
    bool TrySquad(CGameWorld& world, CPlayer& joining_player, CPlayer& target_player);
    bool TryLeaveSquad(CGameWorld& world, CPlayer& player);
    std::vector<CPlayer*> GetSquadPlayerList(CGameWorld& world, CPlayer& player);
    std::uint32_t GetSquadRootId(CGameWorld& world, CPlayer& player);

  private:
    void InitializePreciseSpawns(CGameWorld& world);
    void UpdateSpawnWave(float dt);
    void PruneSquads(CGameWorld& world);
    bool IsSquadPlayerInWorld(const CGameWorld& world, const CPlayer& player) const;
    void EnsureSquadPlayer(std::uint32_t player_id);
    std::uint32_t FindSquadRoot(std::uint32_t player_id);
    std::vector<std::uint32_t> GetSquadMemberIds(std::uint32_t root_id);

    COpenSpawnDirector m_spawn_director;
    bool m_precise_spawns_initialized = false;
    float m_spawn_wave_time = 0.f;
    float m_spawn_density_multiplier = 0.6f;
    std::unordered_map<std::uint32_t, std::uint32_t> m_squad_parent;
    std::vector<std::uint32_t> m_pruned_squad_player_ids;
    bool m_has_pruned_squads = false;
};
