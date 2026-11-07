#pragma once
#include "../extension.h"
#include "extension_services.h"
#include <unordered_map>

// 全局队伍只持有会话关系；战斗权限和世界内奖励由各世界另行决定。
class CPartyExtension final : public CExtension, public IPartyService, public IPlayerCommandExtension,
    public IPersistentExtension, public IMaintainedExtension, public IRestoreBoundaryExtension, public IPlayerStateExtension
{
  public:
    const SExtensionDescriptor& Descriptor() const override;
    bool OnAttach(CExtensionContext&, std::string&) override;
    void OnDetach() noexcept override;
    void Refresh() override;
    std::vector<CPlayer*> Members(const CPlayer&) const override;
    std::uint32_t RootId(const CPlayer&) const override;
    bool Invite(CPlayer&, CPlayer&) override;
    CPlayer* ConsumeInvite(CPlayer&) override;
    bool Accept(CPlayer&, CPlayer&) override;
    bool Leave(CPlayer&) override;
    std::span<const std::string_view> Commands() const override;
    bool HandleCommand(CPlayer&, const std::vector<std::string>&) override;
    void CaptureSnapshot(CSnapshotWriter&) const override;
    bool RestoreSnapshot(const CSnapshotReader&, std::uint32_t, std::string&) override;
    bool RestoreDefaultState(std::string&) override;
    void BeginRestore() override;
    bool PublishPlayerState(CPlayer&) override;
    void ImportLegacy(SWorldHandle, const std::vector<SLegacyPartyMember>&) override;
    static bool ReadLegacy(const CSnapshotReader&, std::vector<SLegacyPartyMember>&, std::string&);
  private:
    void RefreshMembership();
    void RebuildGroups();
    bool Publish(CPlayer&, bool force);
    bool InWorld(const CPlayer&) const;
    CPlayer* Resolve(std::uint32_t) const;
    std::uint32_t Root(std::uint32_t) const;
    void Ensure(const CPlayer&);
    std::vector<std::uint32_t> MemberIds(std::uint32_t) const;
    IPlayerDirectory* m_players = nullptr;
    IChatService* m_chat = nullptr;
    IPartyDelivery* m_delivery = nullptr;
    std::unordered_map<std::uint32_t, std::uint32_t> m_memberships;
    std::unordered_map<std::uint32_t, std::uint64_t> m_generations;
    std::unordered_map<std::uint32_t, std::string> m_accounts;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> m_groups;
    std::unordered_map<std::uint32_t, SPartyState> m_published;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> m_last_active;
    bool m_has_active_cache = false, m_accept_legacy = true;
    struct SInvite { std::uint32_t inviter; std::uint64_t inviter_generation, invitee_generation; };
    std::unordered_map<std::uint32_t, SInvite> m_invites;
};
