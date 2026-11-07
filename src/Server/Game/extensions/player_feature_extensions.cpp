#include "player_feature_extensions.h"
#include "extension_runtime.h"
#include "../player.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../entities/flower.h"
#include "../../Persistence/account_store.h"
#include "../../../Shared/network_msg.h"
#include <array>

namespace
{
bool Ready(const CPlayer& player)
{ return player.IsAuthenticated() && player.GetWorld() && !player.GetWorld()->IsRecovering(); }
SClientFeature Feature(std::string key, std::string title, int order, std::uint8_t access)
{
    SClientFeature result; result.key = key; result.icon = key; result.title = std::move(title);
    result.order = order; result.access = access; if (!access) result.reason = "not_ready";
    return result;
}
}
const SExtensionDescriptor& CInventoryExtension::Descriptor() const
{ static const SExtensionDescriptor value{"feature.inventory", EExtensionScope::Server, 1, {}, false}; return value; }
const SExtensionDescriptor& CCraftExtension::Descriptor() const
{ static const SExtensionDescriptor value{"feature.craft", EExtensionScope::Server, 1, {}, false}; return value; }
const SExtensionDescriptor& CTalentExtension::Descriptor() const
{ static const SExtensionDescriptor value{"feature.talent", EExtensionScope::Server, 1, {}, false}; return value; }
std::span<const std::string_view> CInventoryExtension::FeatureKeys() const
{ static constexpr std::array<std::string_view, 1> keys{"inventory"}; return keys; }
std::span<const std::string_view> CTalentExtension::FeatureKeys() const
{ static constexpr std::array<std::string_view, 1> keys{"talent"}; return keys; }
std::span<const std::string_view> CCraftExtension::FeatureKeys() const
{ static constexpr std::array<std::string_view, 1> keys{"craft"}; return keys; }
void CInventoryExtension::DescribeFeatures(const CPlayer& player, std::vector<SClientFeature>& out) const
{
    auto feature = Feature("inventory", "ui.quick.inventory", 10, Ready(player) ? feature_access::view : 0);
    const auto* flower = dynamic_cast<const CFlower*>(player.GetEntity());
    const auto* rules = player.GetWorld() ? player.GetWorld()->GetController() : nullptr;
    if (feature.access && flower && !flower->IsMarkedForDes() && (!flower->IsDead() || dynamic_cast<const CPlayerFlower*>(flower)) &&
        (!rules || !rules->LocksPetalLoadout())) feature.access |= feature_access::edit;
    else if (feature.access) feature.reason = "loadout_locked";
    out.push_back(std::move(feature));
}
void CTalentExtension::DescribeFeatures(const CPlayer& player, std::vector<SClientFeature>& out) const
{
    auto feature = Feature("talent", "ui.quick.talents", 20, Ready(player) ? feature_access::view | feature_access::edit : 0);
    feature.badge = true; out.push_back(std::move(feature));
}
void CCraftExtension::DescribeFeatures(const CPlayer& player, std::vector<SClientFeature>& out) const
{
    auto feature = Feature("craft", "ui.quick.craft", 30, Ready(player) ? feature_access::view | feature_access::edit : 0);
    const auto* entity = player.GetEntity();
    if (feature.access && !player.HasIsolatedInventory() && entity && !entity->IsDead() && !entity->IsMarkedForDes()) feature.access |= feature_access::forge;
    out.push_back(std::move(feature));
}
EEconomyOutcome CInventoryExtension::Apply(CPlayer& player, const std::vector<SInventoryItem>& primary,
    const std::vector<SInventoryItem>& secondary, std::function<void()>* install)
{ return player.TryApplyLoadout(primary, secondary, install); }
EEconomyOutcome CCraftExtension::Craft(CPlayer& player, std::uint8_t type, std::uint8_t rarity, std::uint32_t count, SCraftResult& result)
{
    if (!player.GameContext() || !player.GameContext()->Extensions().CanUseFeature(player, "craft", feature_access::edit)) return EEconomyOutcome::Forbidden;
    return CAccountDataStore::CraftItem(player.GetInventoryAccountName(), type, rarity, count, &result) ? EEconomyOutcome::Completed : EEconomyOutcome::InsufficientItems;
}
bool CTalentExtension::Apply(CPlayer& player, const ClientTalentRequest& request)
{
    bool changed = false;
    for (const auto& item : request.talents)
        changed = (request.action == ClientTalentAction::Remove ? player.RemoveTalent(item.id, static_cast<ERarity>(item.rarity), item.rank) :
            player.AddTalent(item.id, static_cast<ERarity>(item.rarity), item.rank)) || changed;
    return changed;
}
