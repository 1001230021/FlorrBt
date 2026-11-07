#pragma once
#include "entity.h"
#include "extension.h"
#include <string>
#include <memory>

class CPlayer;
class CWall;
struct FlorrBtMap;

// 准备阶段只能申请资源，不能操作活跃实体或发送消息。
class CWorldPreparation
{
  public:
    explicit CWorldPreparation(CGameWorld& world);
    ~CWorldPreparation();
    bool RequireTerrain(std::string& error);
    bool RequireDigBoard(std::string& error);
    const FlorrBtMap* Map() const;
    void Commit() noexcept;
  private:
    CGameWorld& m_world;
    std::unique_ptr<CWall> m_board;
};

struct SControllerChangeResult
{
    bool committed = false;
    bool recovering = false;
    std::string error;
    explicit operator bool() const { return committed && !recovering; }
};
enum class EPlayerTransitionKind { Transfer, Possession };
enum class EPlayerTransitionSide { Departure, Arrival };

// 预检仅接收值，不暴露可修改的世界、玩家或实体。
struct SPlayerTransitionView
{
    EPlayerTransitionKind kind = EPlayerTransitionKind::Transfer;
    std::uint32_t player_id = 0;
    std::uint64_t session_generation = 0;
    SWorldHandle source, destination;
    SEntityHandle previous_entity, destination_entity;
    florrbt::Vector2f destination_position{};
    std::size_t entity_count = 0;
};

// 实测耗时与复制条目数，用于区分依赖收集和全世界检查点的成本。
struct SWorldTransferCost
{
    std::size_t dependencies = 0;
    std::size_t dependency_candidates = 0;
    std::size_t spatial_entries_copied = 0;
    std::size_t entity_index_entries_copied = 0;
    double checkpoint_ms = 0;
    double total_ms = 0;
};

// 提交后发生死亡等事件，不能把迁移结果改成拒绝；不返回实体裸指针。
struct SPlayerTransitionResult
{
    bool committed = false;
    SEntityHandle entity;
    std::string error;
    bool deferred = false;
    bool recovering = false;
    SWorldTransferCost cost;
    explicit operator bool() const { return committed; }
};

struct SDeferredWorldTransfer
{
    SPlayerSessionRef player;
    SWorldHandle source, destination;
    SEntityHandle entity;
    florrbt::Vector2f position;
};

class CWorldTransfer
{
  public:
    static SPlayerTransitionResult Move(CPlayer&, CGameWorld& source, CGameWorld& destination,
                                        florrbt::Vector2f destination_position);
    static SPlayerTransitionResult Possess(CPlayer&, CGameWorld& destination, const SEntityHandle& entity);
  private:
    struct CWorldCheckpoint;
    static SPlayerTransitionResult MovePrepared(CPlayer&, CGameWorld&, CGameWorld&, florrbt::Vector2f,
                                                SWorldTransferCost&);
};
