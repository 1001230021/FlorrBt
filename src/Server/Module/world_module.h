#pragma once
#include "../Game/gameworld.h"
#include "module.h"
#include <cstdint>
#include <memory>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IWorldModule : public IModule
{
  public:
    struct SWorldRef
    {
        static constexpr std::uint32_t invalid_id = (std::numeric_limits<std::uint32_t>::max)();

        std::uint32_t id = invalid_id;
        std::uint64_t instance_generation = 0;

        bool IsValid() const { return id != invalid_id && instance_generation != 0; }
        bool operator==(const SWorldRef& other) const
        {
            return id == other.id && instance_generation == other.instance_generation;
        }
    };

    IWorldModule();
    bool Init() override;
    void Tick(float dt) override;
    bool DeferWorldTransfer(const SDeferredWorldTransfer& request);
    bool IsSimulationActive() const { return m_simulating_worlds || !m_deferred_transfers.empty(); }
    void ShutDown() override;
    const std::vector<std::unique_ptr<CGameWorld>>& GetWorlds() const { return m_worlds; }
    CGameWorld* GetMainWorld() const;
    bool SetMainWorld(std::uint32_t world_id);
    bool IsMainWorld(const CGameWorld& world) const;
    CGameWorld* FindWorldById(std::uint32_t world_id) const;
    std::vector<CGameWorld*> FindWorldsByMapName(const std::string& map_name) const;
    CGameWorld* FindRandomWorldByMapName(const std::string& map_name) const;

    // 运行时创建并接入世界；初始控制器参数来自等待房间的 /setting。
    CGameWorld* CreateWorld(const std::string& map_path, std::string_view controller_key,
                            const CControllerParams& controller_params = {});

    // 房间码指向当前活动世界，开始前是等待世界，开始后是玩法世界。
    CGameWorld* CreateRoomWorld(const std::string& map_path, std::string_view controller_key, int room_code);
    // 按等待控制器保存的地图和参数创建玩法世界，并更新房间登记。
    CGameWorld* CreateRoomGameWorld(int room_code);
    CGameWorld* FindRoomWorld(int room_code) const;
    int NextRoomCode() const;

    std::size_t OpenWorldInstancesForMap(std::string_view map_path) const;
    // 销毁世界前迁移玩家；无替代世界时释放受控实体。
    // 销毁玩法世界时同时关闭保留的等待世界。
    bool DestroyWorldById(std::uint32_t world_id);

    struct SWorldBlueprint
    {
        std::uint32_t id = 0;
        std::uint64_t generation = 0;
        std::string map_path;
    };
    struct SRoomBlueprint
    {
        int code = 0;
        std::uint32_t active_world = SWorldRef::invalid_id;
        std::uint32_t waiting_world = SWorldRef::invalid_id;
        float empty_seconds = 0.f;
    };
    struct STopologySnapshot
    {
        std::uint32_t main_world = SWorldRef::invalid_id;
        std::vector<SWorldBlueprint> worlds;
        std::vector<SRoomBlueprint> rooms;
    };
    STopologySnapshot CaptureTopology() const;
    bool ValidateTopology(const STopologySnapshot& snapshot, std::string& error) const;
    bool RestoreTopology(const STopologySnapshot& snapshot, std::string& error);

  private:
    SWorldRef MakeWorldRef(const CGameWorld* world) const;
    CGameWorld* ResolveWorld(const SWorldRef& reference) const;
    void UpdateRoomLifecycle(float dt);
    void DestroyRoom(int room_code);
    void DestroyRoomWaitingWorld(int room_code);

    std::vector<std::unique_ptr<CGameWorld>> m_worlds;
    bool m_simulating_worlds = false;
    std::vector<SDeferredWorldTransfer> m_deferred_transfers;
    SWorldRef m_main_world_ref;
    std::unordered_map<int, SWorldRef> m_rooms;
    std::unordered_map<int, SWorldRef> m_room_waiting_worlds;
    std::unordered_map<int, float> m_room_empty_seconds;
};
