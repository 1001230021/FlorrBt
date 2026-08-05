#pragma once

#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class CEntity;
class CGameWorld;
class CMobBase;
class CSnapshotReader;
class CSnapshotWriter;
enum class EPlayerSpawnReason : std::uint8_t;

class COpenSpawnDirector
{
  public:
    using spawn_callback = void (*)(CGameWorld&, CMobBase&);

    COpenSpawnDirector();
    ~COpenSpawnDirector();

    COpenSpawnDirector(const COpenSpawnDirector&) = delete;
    COpenSpawnDirector& operator=(const COpenSpawnDirector&) = delete;

    void Tick(CGameWorld& world, float dt, spawn_callback on_spawn);
    void SpawnMobs(CGameWorld& world, spawn_callback on_spawn);
    std::optional<sf::Vector2f> SelectPlayerSpawn(CGameWorld& world, bool use_new_player_spawn,
                                                  EPlayerSpawnReason reason);
    void OnEntityRemoved(const CEntity& entity);

    void CaptureSnapshot(CSnapshotWriter& writer) const;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::string& error);

  private:
    struct implementation;
    std::unique_ptr<implementation> m_impl;
};
