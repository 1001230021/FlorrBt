#pragma once
#include "../extension.h"
#include "extension_services.h"
#include <unordered_map>

class CSquadExtension final : public CExtension, public ISquadView, public ISquadCommands,
                               public IPlayerCommandExtension, public IPersistentExtension, public IMaintainedExtension
{
  public:
    explicit CSquadExtension(SWorldHandle world) : m_world(world) {}
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
    static std::string Metadata(const std::vector<CPlayer*>&);
  private:
    bool InWorld(const CPlayer&) const;
    CPlayer* Resolve(std::uint32_t id) const;
    std::uint32_t Root(std::uint32_t id) const;
    void Ensure(const CPlayer&);
    std::vector<std::uint32_t> MemberIds(std::uint32_t root) const;
    SWorldHandle m_world;
    IPlayerDirectory* m_players = nullptr;
    IChatService* m_chat = nullptr;
    // Normalized membership index. Reads never compress paths or prune state.
    std::unordered_map<std::uint32_t, std::uint32_t> m_memberships;
    std::unordered_map<std::uint32_t, std::uint64_t> m_generations;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> m_last_active;
    bool m_has_active_cache = false;
    struct SInvite { std::uint32_t inviter; std::uint64_t inviter_generation, invitee_generation; };
    std::unordered_map<std::uint32_t, SInvite> m_invites;
};
