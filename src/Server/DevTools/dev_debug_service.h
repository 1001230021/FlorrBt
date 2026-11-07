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

// 只拥有观察者连接；世界和账号操作留在主线程，日志回调只写有界队列。
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

// 图形操作与背包命令共用，只修改可用库存，不改已装备槽位或玩法代理卡片。
bool EditDebugInventory(CServer& server, CPlayer& player, const std::string& operation,
                        std::uint8_t type, std::uint8_t rarity, std::uint32_t count, std::string& result);
