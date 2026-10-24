#pragma once
#include "../extension.h"
#include "../../../Shared/math_types.h"
#include "../../../Shared/network_msg.h"
#include <memory>
#include <string>
#include <vector>

class CPlayer;
class CGameWorld;

struct SChatEntry
{
    std::uint32_t world_id = 0;
    std::uint64_t world_instance_generation = 0;
    florrbt::Vector2f pos = {0.f, 0.f};
    EChatFlag flag = EChatFlag::Global;
    std::uint32_t player_id = 0;
    int target_player_id = -1;
    std::string player_name;
    std::uint32_t system_time = 0;
    std::string message;
    SPlayerSessionRef sender_session;
};

class IPlayerDirectory
{
  public:
    virtual ~IPlayerDirectory() = default;
    virtual const std::vector<std::unique_ptr<CPlayer>>& GetPlayers() const = 0;
    virtual CPlayer* FindPlayerById(std::uint32_t id) const = 0;
};

// Transport only: recipient selection and history belong to IChatService.
class IChatDelivery
{
  public:
    virtual ~IChatDelivery() = default;
    virtual bool DeliverChat(CPlayer&, const SChatEntry&) = 0;
};

class IChatService
{
  public:
    virtual ~IChatService() = default;
    virtual const std::vector<SChatEntry>& History() const = 0;
    virtual SChatEntry Submit(CGameWorld*, florrbt::Vector2f, EChatFlag, std::uint32_t,
                              const std::string&, const std::string&, int target = -1) = 0;
    virtual void Broadcast(const SChatEntry&) = 0;
    virtual bool SendTo(CPlayer&, const SChatEntry&) = 0;
    virtual void SendSystem(CPlayer&, const std::string&) = 0;
    virtual void HandleMessage(CPlayer&, EChatFlag, const std::string&) = 0;
};

class ISquadView
{
  public:
    virtual ~ISquadView() = default;
    virtual std::vector<CPlayer*> Members(const CPlayer&) const = 0;
    virtual std::uint32_t RootId(const CPlayer&) const = 0;
};

class ISquadCommands
{
  public:
    virtual ~ISquadCommands() = default;
    virtual bool Invite(CPlayer&, CPlayer&) = 0;
    virtual CPlayer* ConsumeInvite(CPlayer&) = 0;
    virtual bool Accept(CPlayer&, CPlayer&) = 0;
    virtual bool Leave(CPlayer&) = 0;
};

class ISquadDirectory
{
  public:
    virtual ~ISquadDirectory() = default;
    virtual const ISquadView* FindSquads(SWorldHandle) const = 0;
};
