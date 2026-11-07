#pragma once
#include <cstdint>
#include <string>

class CSnapshotReader;
class CSnapshotWriter;
class CGameWorld;

// 地形板提供网格覆盖层的生命周期与接口，保存标记和操作权限，不直接修改共享地图资源。
class CTileBoard
{
  public:
    virtual ~CTileBoard() = default;

    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual float TileSize() const = 0;
    bool InBounds(int row, int col) const;

    virtual bool TileSolid(int row, int col) const = 0;
    virtual bool HasTile(int row, int col) const = 0;

    // 所有写入通过校验后的原子变更入口。
    virtual bool CommitRemove(int row, int col, int team = 0) = 0;
    virtual bool CommitPlace(int row, int col, std::uint32_t gid) = 0;

    // 快照只保存变更，恢复时从世界地图重建基础布局。
    virtual void CaptureSnapshot(CSnapshotWriter& writer) const = 0;
    virtual bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) = 0;
    // 恢复快照或重载地图后，重建基础布局并回放变更。
    virtual void Reload(CGameWorld& world) = 0;
};
