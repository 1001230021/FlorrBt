#pragma once
#include "../extension.h"
#include "extension_services.h"

// 世界视图只返回本世界成员；全局队伍由独立扩展持有。
class CSquadExtension final : public CExtension, public ISquadView,
    public IPersistentExtension, public IMaintainedExtension
{
  public:
    explicit CSquadExtension(SWorldHandle world) : m_world(world) {}
    const SExtensionDescriptor& Descriptor() const override;
    bool OnAttach(CExtensionContext&, std::string&) override;
    void OnDetach() noexcept override;
    void Refresh() override;
    std::vector<CPlayer*> Members(const CPlayer&) const override;
    std::uint32_t RootId(const CPlayer&) const override;
    void CaptureSnapshot(CSnapshotWriter&) const override;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string&) override;
  private:
    bool InWorld(const CPlayer&) const;
    SWorldHandle m_world;
    IPartyService* m_party = nullptr;
    std::vector<SLegacyPartyMember> m_legacy;
    bool m_legacy_pending = false;
};
