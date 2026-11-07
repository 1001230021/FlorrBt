#include "unique_petal_registry.h"

namespace
{
// 唯一归属与背包共用事务账本，持久化层不访问在线玩家或网络。
class CAccountUniquePetalRegistry final : public IUniquePetalRegistry
{
  public:
    bool Init(const std::filesystem::path& legacy_path, std::string& error) override
    {
        m_initialized = CAccountDataStore::InitializeUniqueOwnership(legacy_path, &error);
        return m_initialized;
    }

    bool ShutDown(std::string& error) override
    {
        error.clear();
        m_initialized = false;
        return true; // 账户存储统一落盘完整经济状态。
    }

    const std::vector<SUniquePetalOwner>& GetOwners() const override { return CAccountDataStore::UniqueOwners(); }
    const SUniquePetalOwner* FindOwner(EPetalType type) const override { return CAccountDataStore::FindUniqueOwner(type); }

    bool ExchangeUnique(EPetalType type, int cost, const std::string& getter,
                        SUniquePetalExchangeResult& result, std::string& error) override
    {
        if (!m_initialized) { error = "唯一归属账本尚未初始化"; return false; }
        return CAccountDataStore::ExchangeUniquePetal(getter, cost, static_cast<uint8_t>(type), &result, &error);
    }

  private:
    bool m_initialized = false;
};
}

std::unique_ptr<IUniquePetalRegistry> CreateUniquePetalRegistry()
{
    return std::make_unique<CAccountUniquePetalRegistry>();
}
