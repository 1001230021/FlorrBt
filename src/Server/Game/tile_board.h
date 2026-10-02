#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class CSnapshotReader;
class CSnapshotWriter;
class CGameWorld;

// Abstract base for tile-grid overlays, styled after CEntity: virtual
// lifecycle and interface, concrete subclasses implement one kind of overlay
// (walls, dirt, tunnels...). A board is a marker/authority layer over the
// shared map asset - it never writes into FlorrBtMap itself.
class CTileBoard
{
  public:
    virtual ~CTileBoard() = default;

    // Geometry (filled in by the board's loader).
    virtual int Width() const = 0;
    virtual int Height() const = 0;
    virtual float TileSize() const = 0;
    bool InBounds(int row, int col) const;

    // Queries.
    virtual bool TileSolid(int row, int col) const = 0;
    virtual bool HasTile(int row, int col) const = 0;

    // Atomic, validated mutations (the only write entry points).
    virtual bool CommitRemove(int row, int col, int team = 0) = 0;
    virtual bool CommitPlace(int row, int col, std::uint32_t gid) = 0;

    // Snapshot / hot-reload: capture stores only the recorded changes; the
    // board rebuilds its base state from the world's map afterwards.
    virtual void CaptureSnapshot(CSnapshotWriter& writer) const = 0;
    virtual bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) = 0;
    // Rebuilds the base state from the world's map and replays recorded
    // changes (called after a restore or when the map reloads).
    virtual void Reload(CGameWorld& world) = 0;
};

// Tile-board template base that owns one mob per cell via unique_ptr.
// Concrete boards (dig targets, carried objects...) derive from this with
// their mob type; each cell may hold at most one mob, and the board is the
// sole owner. GetMobAt gives a non-owning view, ReleaseMob moves ownership out.
// The remaining CTileBoard pure virtuals stay open for the concrete board.
//
// Bindings are runtime state only: mobs cannot survive a hot reload, so
// nothing here is snapshotted.
template <typename TMob>
class CBoundTileBoard : public CTileBoard
{
  public:
    TMob* GetMobAt(int row, int col) const
    {
        if (!InBounds(row, col)) return nullptr;
        const auto it = m_mobs.find(static_cast<std::uint32_t>(row * Width() + col));
        return it == m_mobs.end() ? nullptr : it->second.get();
    }

    bool IsCellBound(int row, int col) const { return GetMobAt(row, col) != nullptr; }

    // Moves ownership in on success; on failure (out of bounds, cell taken)
    // the caller keeps the mob.
    bool BindMob(int row, int col, std::unique_ptr<TMob>& mob)
    {
        if (!mob || !InBounds(row, col)) return false;
        const std::uint32_t cell = static_cast<std::uint32_t>(row * Width() + col);
        if (m_mobs.find(cell) != m_mobs.end()) return false;
        m_mobs[cell] = std::move(mob);
        return true;
    }

    // Moves ownership out (null when the cell holds no mob).
    std::unique_ptr<TMob> ReleaseMob(int row, int col)
    {
        if (!InBounds(row, col)) return nullptr;
        const auto it = m_mobs.find(static_cast<std::uint32_t>(row * Width() + col));
        if (it == m_mobs.end()) return nullptr;
        std::unique_ptr<TMob> mob = std::move(it->second);
        m_mobs.erase(it);
        return mob;
    }

    void ClearMobs() { m_mobs.clear(); }

    size_t GetMobCount() const { return m_mobs.size(); }

  private:
    std::unordered_map<std::uint32_t, std::unique_ptr<TMob>> m_mobs; // cell -> owned mob
};