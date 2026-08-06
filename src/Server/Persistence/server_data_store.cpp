#include "server_data_store.h"
#include "../../Engine/logger.h"
#include "../../Shared/game_config.h"
#include "account_store.h"
#include "unique_petal_registry.h"
#include <utility>

namespace
{
class CServerDataStore final : public IServerDataStore
{
  public:
    CServerDataStore() : m_unique_petals(CreateUniquePetalRegistry()) {}
    ~CServerDataStore() override
    {
        std::string ignored_error;
        ShutDown(ignored_error);
    }

    bool Init(std::string& error) override
    {
        error.clear();
        if (m_initialized) return true;

        std::string account_error;
        if (!CAccountDataStore::Load(game_config::account_data_path, &account_error))
        {
            error = "Failed to load account data: " + account_error;
            return false;
        }
        m_accounts_initialized = true;
        if (!account_error.empty()) LOG_WARN("account", account_error);

        std::string unique_error;
        if (!m_unique_petals->Init(game_config::unique_petal_data_path, unique_error))
        {
            error = "Failed to load unique petal data: " + unique_error;
            return false;
        }
        m_unique_petals_initialized = true;
        m_initialized = true;
        return true;
    }

    bool ShutDown(std::string& error) override
    {
        error.clear();
        bool success = true;

        if (m_accounts_initialized)
        {
            std::string account_error;
            if (!CAccountDataStore::ShutDown(&account_error))
            {
                error = "Failed to flush account data: " + account_error;
                success = false;
            }
            m_accounts_initialized = false;
        }

        if (m_unique_petals_initialized)
        {
            std::string unique_error;
            if (!m_unique_petals->ShutDown(unique_error))
            {
                if (!error.empty()) error += "; ";
                error += "Failed to save unique petal data: " + unique_error;
                success = false;
            }
            m_unique_petals_initialized = false;
        }

        m_initialized = false;
        return success;
    }

    IUniquePetalRegistry& UniquePetals() override { return *m_unique_petals; }
    const IUniquePetalRegistry& UniquePetals() const override { return *m_unique_petals; }

  private:
    std::unique_ptr<IUniquePetalRegistry> m_unique_petals;
    bool m_accounts_initialized = false;
    bool m_unique_petals_initialized = false;
    bool m_initialized = false;
};
} // namespace

std::unique_ptr<IServerDataStore> CreateServerDataStore() { return std::make_unique<CServerDataStore>(); }
