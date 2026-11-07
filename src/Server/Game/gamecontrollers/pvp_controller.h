#pragma once
#include "open_world_rules.h"
#include "match_rules.h"

class CAntHelPvpController : public IGameController
{
  public:
    using ERole = CMatchRules::ERole;
    static constexpr int kSpectatorTeam = CMatchRules::kSpectatorTeam;
    static constexpr int kMaxPlayers = CMatchRules::kMaxPlayers;
    explicit CAntHelPvpController(CControllerParams params = {}) : m_match(std::move(params)) {}
    void OnTick(CGameWorld& world, float dt) override;
    std::optional<florrbt::Vector2f> SelectPlayerSpawn(CGameWorld& world, CPlayer& player, EPlayerSpawnReason reason) override
    { return m_open.SelectPlayerSpawn(world, player, reason); }
    EMobType PickHabitatMobType(CGameWorld& world, florrbt::Vector2f pos) override { return m_open.PickHabitatMobType(world, pos); }
    void OnEntityRemoved(CGameWorld& world, CEntity& entity, EEntityRemovalReason reason) override
    { m_open.OnEntityRemoved(world, entity, reason); }
    void OnMobDefeated(CGameWorld& world, CMobBase& mob) override { m_open.OnMobDefeated(world, mob); }
    void OnPlayerEntityReady(CGameWorld& world, CPlayer& player, CPlayerFlower& flower, EPlayerSpawnReason reason) override
    { m_match.OnPlayerEntityReady(world, player, flower, reason); }
    void OnPlayerArrivalCommitted(CGameWorld& world, CPlayer& player) override { m_match.OnPlayerArrivalCommitted(world, player); }
    void OnPlayerDepartureCommitted(CGameWorld& world, CPlayer& player, CEntity* entity) override
    { m_match.OnPlayerDepartureCommitted(world, player, entity); }
    std::string_view SnapshotKey() const override { return "world_controller.pvp"; }
    std::uint32_t SnapshotVersion() const override { return 2; }
    void CaptureSnapshot(CSnapshotWriter&) const override;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string& error) override;
    bool ResolveReferences(CGameWorld& world, const IGameWorldResolver& resolver, std::string& error) override
    { return m_match.ResolveReferences(world, resolver, error); }
    bool LocksPetalLoadout() const override { return m_match.LocksPetalLoadout(); }
    std::vector<SControllerParamInfo> ParamSchema() const override { return CMatchRules::ParamSchemaStatic(); }
    int PlayerTeam(std::uint32_t id) const { return m_match.PlayerTeam(id); }
    bool HasMatchTeam(const CPlayer& player) const { return m_match.HasMatchTeam(player); }
  private:
    COpenWorldRules m_open;
    CMatchRules m_match;
};
