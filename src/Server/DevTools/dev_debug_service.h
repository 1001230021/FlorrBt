#pragma once
#include <SFML/Network/TcpSocket.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CServer;
class CPlayer;
class CGameWorld;
struct json_t;

// Owns observer sockets, never CPlayers. All world/account operations run on
// the server tick thread; the logger callback only fills a bounded queue.
class CDevDebugService
{
  public:
    explicit CDevDebugService(CServer& server);
    ~CDevDebugService();
    std::string IssueTicket(const CPlayer& authorizer);
    void Accept(sf::TcpSocket&& socket, std::string remote, std::vector<std::uint8_t> received);
    void Tick(float dt);
    void Reset();
    void BroadcastTerrain(CGameWorld& world, std::uint8_t layer, std::uint16_t row, std::uint16_t col, std::uint32_t gid);
    void BroadcastWallRemoval(CGameWorld& world, float min_x, float min_y, float max_x, float max_y);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Shared by the graphical action and the server's inventory command. Only
// available backpack quantities change; equipped slots/controller proxies
// are deliberately not touched.
bool EditDebugInventory(CServer& server, CPlayer& player, const std::string& operation,
                        std::uint8_t type, std::uint8_t rarity, std::uint32_t count, std::string& result);
