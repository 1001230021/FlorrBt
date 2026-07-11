#pragma once
#include <SFML/Network/TcpSocket.hpp>
#include <cstdint>
#include <string>
#include <vector>

class CEntity;
class CGameContext;
class CGameWorld;
struct ClientOperate;

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
    void ApplySavedSlots();
    bool TryEquipPetal(uint8_t slot_index, uint8_t petal_type, uint8_t rarity);
    bool TryUnequipPetal(uint8_t slot_index);

    void SetOwnedEntity(CEntity* entity);
    CEntity* GetEntity() const;
    CGameContext* GameContext() const;
    void Authenticate(const std::string& account_name);

    sf::TcpSocket& GetSocket() { return m_socket; }
    uint32_t GetId() const { return m_player_id; }
    const std::string& GetName() const { return m_name; }
    const std::string& GetAccountName() const { return m_account_name; }
    const std::string& GetRemoteAddress() const { return m_remote_address; }
    bool HasOwnedEntity() const { return m_entity_id >= 0; }
    bool IsConnected() const { return m_connected; }
    bool IsAuthenticated() const { return m_authenticated; }
    bool IsTimedOut() const { return !m_connected && m_timeout_left <= 0.f; }

    std::vector<uint8_t> m_send_buffer;
    std::vector<uint8_t> m_receive_buffer;
    bool m_logged_missing_entity = false;

  private:
    sf::TcpSocket m_socket;
    CGameWorld* m_p_world = nullptr;
    int m_entity_id = -1;
    bool m_connected = true;
    bool m_authenticated = false;
    bool m_report_disabled = false;
    float m_timeout_left = 0.f;
    float m_mute_timer = 0.f;
    int m_invalid_report_count = 0;
    uint32_t m_player_id = 0;
    std::string m_name;
    std::string m_account_name;
    std::string m_remote_address;
};
