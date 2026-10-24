#pragma once
#include "../extension.h"
#include "extension_services.h"

class CChatExtension final : public CExtension, public IChatService, public IPlayerCommandExtension
{
  public:
    const SExtensionDescriptor& Descriptor() const override;
    bool OnAttach(CExtensionContext&, std::string&) override;
    void OnDetach() noexcept override;
    const std::vector<SChatEntry>& History() const override { return m_history; }
    SChatEntry Submit(CGameWorld*, florrbt::Vector2f, EChatFlag, std::uint32_t,
                      const std::string&, const std::string&, int target = -1) override;
    void Broadcast(const SChatEntry&) override;
    bool SendTo(CPlayer&, const SChatEntry&) override;
    void SendSystem(CPlayer&, const std::string&) override;
    void HandleMessage(CPlayer&, EChatFlag, const std::string&) override;
    std::span<const std::string_view> Commands() const override;
    bool HandleCommand(CPlayer&, const std::vector<std::string>&) override;
  private:
    IPlayerDirectory* m_players = nullptr;
    IChatDelivery* m_delivery = nullptr;
    const ISquadDirectory* m_squads = nullptr;
    std::vector<SChatEntry> m_history;
};
