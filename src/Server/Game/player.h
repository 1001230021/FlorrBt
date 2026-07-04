#pragma once
#include <SFML/Network/TcpSocket.hpp>
#include <cstdint>
#include <string>
#include <vector>

class CEntity;
class CGameWorld;
struct ClientOperate;

class CPlayer
{
  public:
    CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name);
    ~CPlayer() = default;

    void HandleOperate(const ClientOperate& op);
    void AttachSocket(sf::TcpSocket&& socket);
    void DetachSocket();
    void TickTimeout(float dt);
    void ResetControlledMob();

    void SetOwnedEntity(CEntity* entity);
    CEntity* GetEntity() const;

    sf::TcpSocket& GetSocket() { return m_socket; }
    uint32_t GetId() const { return m_player_id; }
    const std::string& GetName() const { return m_name; }
    bool HasOwnedEntity() const { return m_entity_id >= 0; }
    bool IsConnected() const { return m_connected; }
    bool IsTimedOut() const { return !m_connected && m_timeout_left <= 0.f; }

    std::vector<uint8_t> m_send_buffer;
    std::vector<uint8_t> m_receive_buffer;
    bool m_logged_missing_entity = false;

  private:
    sf::TcpSocket m_socket;
    CGameWorld* m_p_world = nullptr;
    int m_entity_id = -1;
    bool m_connected = true;
    float m_timeout_left = 0.f;
    uint32_t m_player_id = 0;
    std::string m_name;
};
