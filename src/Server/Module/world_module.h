#pragma once
#include "../Game/gameworld.h"
#include "module.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class IWorldModule : public IModule
{
  public:
    IWorldModule();
    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;
    const std::vector<std::unique_ptr<CGameWorld>>& GetWorlds() const { return m_worlds; }
    CGameWorld* FindWorldById(std::uint32_t world_id) const;
    std::vector<CGameWorld*> FindWorldsByMapName(const std::string& map_name) const;
    CGameWorld* FindRandomWorldByMapName(const std::string& map_name) const;

    // Creates a brand new world (map + controller key) at runtime, wires it
    // into the game context, and returns it. Used by the room system.
    // Construction-time controller overrides come from the waiting room's
    // /setting parameters.
    CGameWorld* CreateWorld(const std::string& map_path, std::string_view controller_key,
                            const CControllerParams& controller_params = {});

    // Room registry: room code (int) -> the room's active world. Before the
    // room starts this is the waiting world; after /start it becomes the game
    // world created on demand.
    CGameWorld* CreateRoomWorld(const std::string& map_path, std::string_view controller_key, int room_code);
    // Lazily creates the room's game world (map + controller blueprint stored
    // in the waiting controller) and registers it as the room's active world.
    CGameWorld* CreateRoomGameWorld(int room_code);
    CGameWorld* FindRoomWorld(int room_code) const;
    int NextRoomCode() const;

  private:
    void UpdateRoomLifecycle(float dt);
    void DestroyRoom(int room_code);
    void DestroyRoomWaitingWorld(int room_code);

    std::vector<std::unique_ptr<CGameWorld>> m_worlds;
    std::unordered_map<int, CGameWorld*> m_rooms;
    std::unordered_map<int, CGameWorld*> m_room_waiting_worlds;
    std::unordered_map<int, float> m_room_empty_seconds;
};
