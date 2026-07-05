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
    void SetOwnedEntity(CEntity* entity);
    CEntity* GetEntity() const;
    int GetEntityId() const { return m_entity_id; }

    sf::TcpSocket& GetSocket() { return m_socket; }
    std::vector<uint8_t>& ReceiveBuffer() { return m_receive_buffer; }
    std::vector<uint8_t>& SendBuffer() { return m_send_buffer; }
    uint32_t GetId() const { return m_player_id; }
    const std::string& GetName() const { return m_name; }

  private:
    sf::TcpSocket m_socket;
    std::vector<uint8_t> m_receive_buffer;
    std::vector<uint8_t> m_send_buffer;
    CGameWorld* m_p_world = nullptr;
    int m_entity_id = -1;
    uint32_t m_player_id = 0;
    std::string m_name;
};
