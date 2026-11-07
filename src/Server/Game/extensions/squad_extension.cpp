#include "squad_extension.h"
#include "party_extension.h"
#include "../gameworld.h"
#include "../player.h"
#include "../../HotReload/snapshot_archive.h"
#include <algorithm>
#include <array>

const SExtensionDescriptor& CSquadExtension::Descriptor() const
{
    static constexpr std::array<std::string_view, 1> dependencies{"extension.party"};
    static const SExtensionDescriptor descriptor{"extension.squad", EExtensionScope::World, 2, dependencies};
    return descriptor;
}
bool CSquadExtension::OnAttach(CExtensionContext& context, std::string& error)
{
    m_party = context.squads ? context.squads->Parties() : nullptr;
    if (!m_party) { error = "世界队伍视图缺少全局队伍服务"; return false; }
    return true;
}
void CSquadExtension::OnDetach() noexcept { m_party = nullptr; m_legacy.clear(); m_legacy_pending = false; }
bool CSquadExtension::InWorld(const CPlayer& player) const
{
    const auto* world = player.GetWorld();
    return player.IsAuthenticated() && world && SWorldHandle{world->GetId(), world->GetInstanceGeneration()} == m_world;
}
std::vector<CPlayer*> CSquadExtension::Members(const CPlayer& player) const
{
    if (!m_party || !InWorld(player)) return {};
    auto members = m_party->Members(player);
    std::erase_if(members, [&](const auto* member) { return !member || !InWorld(*member); });
    return members;
}
std::uint32_t CSquadExtension::RootId(const CPlayer& player) const
{ return m_party && InWorld(player) ? m_party->RootId(player) : player.GetId(); }
void CSquadExtension::Refresh()
{
    if (!m_legacy_pending || !m_party) return;
    m_party->ImportLegacy(m_world, m_legacy);
    m_legacy.clear(); m_legacy_pending = false;
}
void CSquadExtension::CaptureSnapshot(CSnapshotWriter& writer) const { writer.Field("world_view", true); }
bool CSquadExtension::RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error)
{
    if (version == 1)
    {
        if (!CPartyExtension::ReadLegacy(reader, m_legacy, error)) return false;
        m_legacy_pending = true; return true;
    }
    if (version != 2 || !reader.IsValid() || !reader.Bool("world_view", false))
    { error = "世界队伍视图快照无效"; return false; }
    m_legacy.clear(); m_legacy_pending = false; return true;
}
