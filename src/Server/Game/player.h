#pragma once
#include <SFML/Network/TcpSocket.hpp>
#include <cstdint>
#include <string>

class CEntity;
struct ClientOperate;

class CPlayer
{
  public:
    CPlayer(sf::TcpSocket&& socket, uint32_t id, const std::string& name);
    ~CPlayer() = default;

    void HandleOperate(const ClientOperate& op);
    void SetOwnedEntity(CEntity* entity) { m_p_entity = entity; }
    CEntity* GetEntity() { return m_p_entity; }

    sf::TcpSocket& GetSocket() { return m_socket; }
    uint32_t GetId() const { return m_player_id; }
    const std::string& GetName() const { return m_name; }

  private:
    sf::TcpSocket m_socket;
    CEntity* m_p_entity = nullptr;
    uint32_t m_player_id = 0;
    std::string m_name;
};
