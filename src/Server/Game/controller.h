#pragma once
#include <cstdint>
#include <string>
#include <string_view>

class CMobBase;
class CEntity;
class CSnapshotReader;
class CSnapshotWriter;

class IController
{
  public:
    virtual ~IController() = default;
    virtual void OnTick(CMobBase* mob, float dt) = 0;
    virtual void OnDamaged(CMobBase*, CEntity*) {}
    virtual std::string_view SnapshotKey() const = 0;
    virtual std::uint32_t SnapshotVersion() const { return 1; }
    virtual void CaptureSnapshot(CSnapshotWriter&) const {}
    virtual bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t version, std::string& error)
    {
        if (version <= SnapshotVersion()) return true;
        error = "Unsupported controller snapshot version";
        return false;
    }
};
