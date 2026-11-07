#pragma once
#include "../extension.h"
#include "../../../Shared/economy.h"
#include "../../../Shared/inventory.h"
#include <functional>

struct ClientTalentRequest;
class IInventoryFeature
{
  public:
    virtual ~IInventoryFeature() = default;
    virtual EEconomyOutcome Apply(CPlayer&, const std::vector<SInventoryItem>&, const std::vector<SInventoryItem>&,
                                 std::function<void()>*) = 0;
};
class ICraftFeature
{
  public:
    virtual ~ICraftFeature() = default;
    virtual EEconomyOutcome Craft(CPlayer&, std::uint8_t, std::uint8_t, std::uint32_t, SCraftResult&) = 0;
};
class ITalentFeature
{
  public:
    virtual ~ITalentFeature() = default;
    virtual bool Apply(CPlayer&, const ClientTalentRequest&) = 0;
};
class CInventoryExtension final : public CExtension, public IClientFeatureExtension, public IInventoryFeature
{
  public:
    const SExtensionDescriptor& Descriptor() const override;
    std::span<const std::string_view> FeatureKeys() const override;
    void DescribeFeatures(const CPlayer&, std::vector<SClientFeature>&) const override;
    EEconomyOutcome Apply(CPlayer&, const std::vector<SInventoryItem>&, const std::vector<SInventoryItem>&, std::function<void()>*) override;
};
class CCraftExtension final : public CExtension, public IClientFeatureExtension, public ICraftFeature
{
  public:
    const SExtensionDescriptor& Descriptor() const override;
    std::span<const std::string_view> FeatureKeys() const override;
    void DescribeFeatures(const CPlayer&, std::vector<SClientFeature>&) const override;
    EEconomyOutcome Craft(CPlayer&, std::uint8_t, std::uint8_t, std::uint32_t, SCraftResult&) override;
};
class CTalentExtension final : public CExtension, public IClientFeatureExtension, public ITalentFeature
{
  public:
    const SExtensionDescriptor& Descriptor() const override;
    std::span<const std::string_view> FeatureKeys() const override;
    void DescribeFeatures(const CPlayer&, std::vector<SClientFeature>&) const override;
    bool Apply(CPlayer&, const ClientTalentRequest&) override;
};
