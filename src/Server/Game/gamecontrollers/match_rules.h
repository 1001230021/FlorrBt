#pragma once
#include "../gamecontroller.h"
#include "../../../Shared/petal_type.h"
#include <unordered_map>

// 模式显式组合角色分配和装备限制，不通过继承引入刷怪或奖励规则。
class CMatchRules
{
  public:
    enum class ERole : std::uint8_t { Spectator, Participant };
    static constexpr int kSpectatorTeam = 1, kMaxPlayers = 2;
    explicit CMatchRules(CControllerParams params = {}, EPetalType preserved = EPetalType::None);
    void OnTick(CGameWorld&, float);
    void OnPlayerEntityReady(CGameWorld&, CPlayer&, CPlayerFlower&, EPlayerSpawnReason);
    void OnPlayerArrivalCommitted(CGameWorld&, CPlayer&);
    void OnPlayerDepartureCommitted(CGameWorld&, CPlayer&, CEntity*);
    void CaptureSnapshot(CSnapshotWriter&) const;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string& error);
    bool ResolveReferences(CGameWorld&, const IGameWorldResolver&, std::string& error);
    bool LocksPetalLoadout() const { return m_lock_loadout; }
    void ForceLoadoutLock() { m_lock_loadout = true; }
    int PlayerTeam(std::uint32_t id) const;
    bool HasMatchTeam(const CPlayer& player) const;
    static std::vector<SControllerParamInfo> ParamSchemaStatic();
  private:
    static bool ValidateStartTeam(const std::string&);
    static bool ValidateSpectatorTeam(const std::string&);
    static bool ValidateLockLoadout(const std::string&);
    bool Preserves(EPetalType type) const { return type == m_preserved; }
    int AssignTeam(CPlayer&);
    void StripPlayerPetals(CPlayer&);
    void MaintainStrip(CGameWorld&) const;
    EPetalType m_preserved;
    bool m_started = false;
    int m_start_team = 2, m_next_team = 2, m_spectator_team = 1;
    bool m_lock_loadout = true;
    struct SParticipant { int team = 1; ERole role = ERole::Spectator; std::uint64_t session_generation = 0; };
    std::unordered_map<std::uint32_t, SParticipant> m_participants;
};
