#pragma once
#include "../../Shared/rarity.h"
#include "../../Shared/talent_type.h"
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/System/Vector2.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CEntity;
class CGameContext;
class CGameWorld;
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
    ~CPlayer() = default;

    void HandleOperate(const ClientOperate& op);
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
    void UnequipAllPetals();
    void ApplySavedProgress();
    void ApplySavedTalents();
    void ApplySavedSlots();
    bool TryEquipPetal(uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    bool TryUnequipPetal(uint8_t slot_index);
    bool TryCraftPetal(uint8_t petal_type, uint8_t rarity, uint32_t count, SCraftResult* result = nullptr);
    bool ObtainPetalCard(uint8_t petal_type, uint8_t rarity, uint32_t count = 1, bool add_to_inventory = true);
    bool AddTalent(ETalentId id, ERarity rarity, int rank = 0);
    bool RemoveTalent(ETalentId id, ERarity rarity, int rank = 0);
    void AddTalentPoints(int amount);
    int GetTalentPoints() const { return m_talent_points; }
    const std::vector<ITalent*>& GetTalents() const { return m_talents; }
    bool HasTalent(const ITalent* talent) const;
    void ApplyTalents(ETalentEvent event, STalentContext& ctx) const;
    int CalculateTalentSlotCount() const;
    void RefreshTalentEffects(bool reload_petals = false);
    float GetSecondChanceCooldown() const { return m_second_chance_cooldown; }
    void SetSecondChanceCooldown(float cooldown);
    void SetUseNewPlayerSpawn(bool value) { m_use_new_player_spawn = value; }
    bool ConsumeUseNewPlayerSpawn();

    void SetOwnedEntity(CEntity* entity);
    CEntity* GetEntity() const;
    CGameContext* GameContext() const;
    void Authenticate(const std::string& account_name);

    sf::TcpSocket& GetSocket() { return m_socket; }
    uint32_t GetId() const { return m_player_id; }
    const std::string& GetName() const { return m_name; }
    const std::string& GetAccountName() const { return m_account_name; }
    const std::string& GetRemoteAddress() const { return m_remote_address; }
    bool HasOwnedEntity() const { return GetEntity() != nullptr; }
    bool IsConnected() const { return m_connected; }
    bool IsAuthenticated() const { return m_authenticated; }
    bool IsRconAuthorized() const { return m_rcon_authorized; }
    void SetRconAuthorized(bool authorized) { m_rcon_authorized = authorized; }
    bool IsTimedOut() const { return !m_connected && m_timeout_left <= 0.f; }

    std::vector<uint8_t> m_send_buffer;
    size_t m_send_offset = 0;
    std::vector<uint8_t> m_receive_buffer;
    bool m_logged_missing_entity = false;
    std::vector<SPlayerCheckpointEntry> m_cp_stack;
    uint8_t m_cp_check_phase = 0;

  private:
    sf::TcpSocket m_socket;
    CGameWorld* m_p_world = nullptr;
    int m_entity_id = -1;
    std::uint64_t m_entity_generation = 0;
    bool m_connected = true;
    bool m_authenticated = false;
    bool m_rcon_authorized = false;
    bool m_report_disabled = false;
    float m_timeout_left = 0.f;
    float m_mute_timer = 0.f;
    int m_invalid_report_count = 0;
    uint32_t m_player_id = 0;
    std::string m_name;
    std::string m_account_name;
    std::string m_remote_address;
    int m_talent_points = 0;
    float m_second_chance_cooldown = 0.f;
    bool m_use_new_player_spawn = false;
    std::vector<ITalent*> m_talents;

    void SaveTalentState() const;
    void NormalizeTalentOrder();
};
