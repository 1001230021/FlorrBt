#include "pvp_controller.h"

void CAntHelPvpController::OnTick(CGameWorld& world, float dt)
{
    m_open.InitializePreciseSpawns(world);
    m_match.OnTick(world, dt);
}
void CAntHelPvpController::CaptureSnapshot(CSnapshotWriter& writer) const
{
    m_open.CaptureSnapshot(writer);
    m_match.CaptureSnapshot(writer);
}
bool CAntHelPvpController::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    return m_open.RestoreSnapshot(reader, version, error) && m_match.RestoreSnapshot(reader, version, error);
}
