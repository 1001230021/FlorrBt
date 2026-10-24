#pragma once
#include "../../Shared/rarity.h"
#include "../../Shared/talent_type.h"
#include "../Persistence/account_store.h"
#include <SFML/Network/TcpSocket.hpp>
#include "../../Shared/math_types.h"
#include "entity.h"
#include "extension.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class CEntity;
class CGameContext;
class CGameWorld;
class CFlower;
class CPlayerFlower;
class ITalent;
struct SCraftResult;
struct STalentContext;
struct ClientOperate;

struct SPlayerCheckpointEntry
{
    uint32_t checkpoint_id = 0;
    int level = 0;
    uint8_t count = 3;
};

class CPlayer
{
  public:
    CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name);
    ~CPlayer();

    bool HandleOperate(const ClientOperate& op);
    bool HandleScheduledOperate(const ClientOperate& op, std::uint32_t delay_ticks, std::uint32_t sequence);
    void AttachSocket(sf::TcpSocket&& socket);
    void SetRemoteAddress(const std::string& address) { m_remote_address = address; }
    void DetachSocket();
    void TickTimeout(float dt);
    void MuteFor(float seconds);
    void Unmute();
    bool IsMuted() const { return m_mute_timer > 0.f; }
    float GetMuteTimer() const { return m_mute_timer; }
    bool IsReportDisabled() const { return m_report_disabled; }
    int GetInvalidReportCount() const { return m_invalid_report_count; }
    void RegisterValidReport();
    void RegisterInvalidReport();
    bool TickDropPickup();
    void ResetControlledMob();
    void UnequipAllEquippedPetals();
    void ApplySavedProgress();
    void LoadSavedTalents(bool refresh_effects = true);
    void LoadSavedSlots(CFlower* target = nullptr);
    // Rebuild the runtime loadout from the account after a controller has
    // temporarily replaced or stripped the player's slots (for example, a
    // PVP/EOU room). Account slots remain the source of truth.
    void RestoreSavedLoadout(CPlayerFlower* target = nullptr);
    bool TryEquipPetal(uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    bool TryUnequipPetal(uint8_t slot_index);
    bool TryCraftPetal(uint8_t petal_type, uint8_t rarity, uint32_t count, SCraftResult* result = nullptr);
    bool ObtainPetalCard(uint8_t petal_type, uint8_t rarity, uint32_t count = 1, bool add_to_inventory = true);
    bool AddTalent(ETalentId id, ERarity rarity, int rank = 0);
    bool RemoveTalent(ETalentId id, ERarity rarity, int rank = 0);
    void AddTalentPoints(int amount);
    int GetTalentPoints() const { return m_talent_points; }
    // Hot-reload restores the in-memory value captured with the session.  It
    // deliberately does not write the account store: persistence remains the
    // responsibility of the normal talent mutation paths.
    void RestoreTalentPoints(int points) { m_talent_points = std::max(0, points); }
    const std::vector<ITalent*>& GetTalents() const { return m_talents; }
    bool HasTalent(const ITalent* talent) const;
    void ApplyTalents(ETalentEvent event, STalentContext& ctx) const;
    int CountTalentSlots() const;
    void RefreshTalentEffects(bool reload_petals = false);
    float GetSecondChanceCooldown() const { return m_second_chance_cooldown; }
    void SetSecondChanceCooldown(float cooldown);
    void SetUseNewPlayerSpawn(bool value) { m_use_new_player_spawn = value; }
    bool ConsumeUseNewPlayerSpawn();

    void SetOwnedEntity(CEntity* entity);
    bool IsTransitionActive() const { return m_transition_active; }
    // Used by world restore/destruction before entity storage is replaced.
    // Unlike SetOwnedEntity(nullptr), this also clears a stale world/handle
    // pair when the old entity is already missing.
    void ClearOwnedEntityFromWorld(CGameWorld* world);
    CEntity* GetEntity() const;
    CGameWorld* GetWorld() const { return m_p_world; }
    CGameContext* GameContext() const;
    void Authenticate(const std::string& account_name);

    // Bestiary: record a mob defeat and read the runtime codex.
    void RecordCodexKill(uint8_t mob_type, uint8_t rarity);
    const std::unordered_map<uint32_t, uint32_t>& GetCodex() const { return m_codex; }
    void LoadCodex(const std::vector<SAccountCodexEntry>& entries);

    sf::TcpSocket& GetSocket() { return m_socket; }
    uint32_t GetId() const { return m_player_id; }
    std::uint64_t SessionGeneration() const { return m_session_generation; }
    SPlayerSessionRef SessionRef() const { return {m_player_id, m_session_generation, m_account_name}; }
    bool MatchesSession(const SPlayerSessionRef& reference) const
    { return m_authenticated && m_player_id == reference.id && m_session_generation == reference.generation && m_account_name == reference.account; }
    bool RestoreSessionGeneration(std::uint64_t generation)
    {
        if (!generation || generation == UINT64_MAX) return false;
        m_session_generation = generation;
        auto previous = s_next_session_generation.load();
        while (previous < generation && !s_next_session_generation.compare_exchange_weak(previous, generation)) {}
        return true;
    }
    const std::string&     GetName() const { return m_name; }
    const std::string& GetAccountName() const { return m_account_name; }
    // Stable login identity is separate from the current gameplay data scope.
    const std::string& GetProgressAccountName() const
    { return m_progress_account.empty() ? m_account_name : m_progress_account; }
    bool HasIsolatedProgress() const { return !m_progress_account.empty(); }
    SWorldHandle ProgressWorld() const { return m_progress_world; }
    bool SynchronizeGameplayScope(CGameWorld& world, CEntity* departing = nullptr);
    bool RestoreIsolatedProgress(SWorldHandle world, const SPlayerAccount& state);
    const std::string& GetRemoteAddress() const { return m_remote_address; }
    bool HasOwnedEntity() const { return m_p_world != nullptr && m_entity_ref.IsValid(); }
    bool NeedsRespawn() const { return m_needs_respawn; }
    bool IsConnected() const { return m_connected; }
    bool IsAuthenticated() const { return m_authenticated; }
    bool IsVersionVerified() const { return m_version_verified; }
    void SetVersionVerified(bool verified) { m_version_verified = verified; }
    bool IsRconAuthorized() const { return m_rcon_authorized; }
    void SetRconAuthorized(bool authorized) { m_rcon_authorized = authorized; }
    bool IsTimedOut() const { return !m_connected && m_timeout_left <= 0.f; }
    float GetTimeoutLeft() const { return m_timeout_left; }
    bool GetUseNewPlayerSpawn() const { return m_use_new_player_spawn; }
    void RestoreDisconnectedSession(const std::string& remote_address, float timeout_left, float mute_timer,
                                    bool report_disabled, int invalid_report_count, float second_chance_cooldown,
                                    bool use_new_player_spawn);

    std::vector<uint8_t> m_send_buffer;
    size_t m_send_offset = 0;
    std::vector<uint8_t> m_receive_buffer;
    bool m_logged_missing_entity = false;
    std::vector<SPlayerCheckpointEntry> m_cp_stack;
    uint8_t m_cp_check_phase = 0;

  private:
    friend class CWorldTransfer;
    friend class CGameWorld;
    void CommitOwnedEntity(CEntity* entity);
    bool m_transition_active = false;
    inline static std::atomic<std::uint64_t> s_next_session_generation{0};
    std::uint64_t m_session_generation = ++s_next_session_generation;
    sf::TcpSocket m_socket;
    CGameWorld* m_p_world = nullptr;
    SEntityHandle m_entity_ref;
    bool m_connected = true;
    bool m_authenticated = false;
    bool m_version_verified = false;
    bool m_rcon_authorized = false;
    bool m_needs_respawn = false;
    bool m_report_disabled = false;
    float m_timeout_left = 0.f;
    float m_mute_timer = 0.f;
    int m_invalid_report_count = 0;
    uint32_t m_player_id = 0;
    std::string m_name;
    std::string m_account_name;
    std::string m_progress_account;
    SWorldHandle m_progress_world;
    std::string m_remote_address;
    int m_talent_points = 0;
    float m_second_chance_cooldown = 0.f;
    bool m_use_new_player_spawn = false;
    std::vector<ITalent*> m_talents;
    // Bestiary runtime cache: key = (mob_type << 8) | rarity, value = kill count.
    std::unordered_map<uint32_t, uint32_t> m_codex;

    void SaveTalentState() const;
    void NormalizeTalentOrder();
    void RefreshScopedProgress(CPlayerFlower* flower);
    std::string ProgressKey(SWorldHandle world) const;
};
