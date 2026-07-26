#pragma once
#include "../../../Shared/game_config.h"
#include "../gamecontroller.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

class CPlayer;

class COpenController : public IGameController
{
  public:
    COpenController() = default;
    void OnTick(CGameWorld& world, float dt) override;
    void OnPlayerConnect(CGameWorld& world, CPlayer* player) override;
    void OnPlayerSpawn(CGameWorld& world, CPlayer* player, CEntity* entity) override;
    void OnEntityDie(CGameWorld& world, CEntity* entity) override;
    void ModifyTalentContext(CGameWorld& world, CPlayer* player, ETalentEvent event, STalentContext& ctx) override;
    void SpawnMobs(CGameWorld& world);
    bool TrySquad(CGameWorld& world, CPlayer& joining_player, CPlayer& target_player);
    bool TryLeaveSquad(CGameWorld& world, CPlayer& player);
    std::vector<CPlayer*> GetSquadPlayerList(CGameWorld& world, CPlayer& player);
    std::uint32_t GetSquadRootId(CGameWorld& world, CPlayer& player);

  private:
    struct single_spawn_zone_mob
    {
        int id = -1;
        std::uint64_t generation = 0;
    };

    bool SingleSpawnZoneMobAlive(CGameWorld& world, size_t zone_index) const;
    void RememberSingleSpawnZoneMob(size_t zone_index, const CEntity* entity);
    void ForgetSingleSpawnZoneMob(const CEntity* entity);
    void PruneSquads(CGameWorld& world);
    bool IsSquadPlayerInWorld(const CGameWorld& world, const CPlayer& player) const;
    void EnsureSquadPlayer(std::uint32_t player_id);
    std::uint32_t FindSquadRoot(std::uint32_t player_id);
    std::vector<std::uint32_t> GetSquadMemberIds(std::uint32_t root_id);

    float m_count = game_config::open_initial_spawn_delay;
    size_t m_spawn_zone_cursor = 0;
    size_t m_idle_spawn_zone_cursor = 0;
    std::vector<single_spawn_zone_mob> m_single_spawn_zone_mobs;
    std::unordered_map<std::uint32_t, std::uint32_t> m_squad_parent;
};
