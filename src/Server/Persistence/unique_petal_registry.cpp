#include "unique_petal_registry.h"
#include "../../Engine/logger.h"
#include "../Game/entities/flower.h"
#include "../Game/player.h"
#include "../HotReload/snapshot_archive.h"
#include "../Module/network_module.h"
#include "../server.h"
#include "account_store.h"
#include <algorithm>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace
{
constexpr int unique_petal_data_version = 1;

bool IsValidPetalType(int type)
{
    return type > static_cast<int>(EPetalType::None) && type < static_cast<int>(petal_type_names.size());
}

class CJsonUniquePetalRegistry final : public IUniquePetalRegistry
{
  public:
    bool Init(const std::filesystem::path& path, std::string& error) override
    {
        error.clear();
        m_path = path;
        m_owners.clear();
        m_initialized = false;

        std::error_code filesystem_error;
        const bool exists = std::filesystem::exists(path, filesystem_error);
        if (filesystem_error)
        {
            error = "Failed to inspect unique petal data " + path.string() + ": " + filesystem_error.message();
            return false;
        }
        if (!exists)
        {
            m_initialized = true;
            return true;
        }

        CJsonOwner root = LoadJsonFile(path, error);
        if (!root) return false;

        CSnapshotReader root_reader(root.get());
        if (!root_reader.IsValid())
        {
            error = "Unique petal data root must be an object";
            return false;
        }

        const int version = root_reader.Int("version", unique_petal_data_version);
        if (version != unique_petal_data_version)
        {
            error = "Unsupported unique petal data version " + std::to_string(version);
            return false;
        }

        const json_t* owners = root_reader.Node("owners");
        if (owners && !json_is_array(owners))
        {
            error = "Unique petal owners must be an array";
            return false;
        }

        std::unordered_set<int> seen_types;
        const size_t owner_count = owners ? json_array_size(owners) : 0;
        m_owners.reserve(owner_count);
        for (size_t index = 0; index < owner_count; ++index)
        {
            CSnapshotReader owner_reader(json_array_get(owners, index));
            const int type = owner_reader.Int("type");
            const int cost = owner_reader.Int("cost", -1);
            std::string owner = owner_reader.String("owner");

            if (!owner_reader.IsValid() || !IsValidPetalType(type) || cost < 0 || owner.empty())
            {
                error = "Invalid unique petal owner at index " + std::to_string(index);
                m_owners.clear();
                return false;
            }
            if (!seen_types.insert(type).second)
            {
                error = "Duplicate unique petal type " + std::to_string(type);
                m_owners.clear();
                return false;
            }

            m_owners.push_back({ static_cast<EPetalType>(type), cost, std::move(owner) });
        }

        SortOwners();
        m_initialized = true;
        return true;
    }

    bool ShutDown(std::string& error) override
    {
        error.clear();
        if (!m_initialized) return true;
        if (m_path.empty())
        {
            error = "Unique petal data path is empty";
            return false;
        }

        CJsonOwner root = MakeJsonObject();
        CJsonOwner owners = MakeJsonArray();
        if (!root || !owners)
        {
            error = "Failed to allocate unique petal JSON";
            return false;
        }

        for (const SUniquePetalOwner& owner : m_owners)
        {
            CJsonOwner entry = MakeJsonObject();
            if (!entry)
            {
                error = "Failed to allocate unique petal owner JSON";
                return false;
            }

            CSnapshotWriter entry_writer(entry.get());
            entry_writer.Field("type", static_cast<int>(owner.type));
            entry_writer.Field("cost", owner.cost);
            entry_writer.Field("owner", owner.owner);
            if (!AppendJson(owners.get(), entry.release()))
            {
                error = "Failed to append unique petal owner JSON";
                return false;
            }
        }

        CSnapshotWriter root_writer(root.get());
        root_writer.Field("version", unique_petal_data_version);
        root_writer.Node("owners", owners.release());
        return WriteJsonAtomically(m_path, root.get(), error);
    }

    const std::vector<SUniquePetalOwner>& GetOwners() const override { return m_owners; }

    const SUniquePetalOwner* FindOwner(EPetalType type) const override
    {
        const auto found = std::find_if(m_owners.begin(), m_owners.end(),
                                        [type](const SUniquePetalOwner& entry) { return entry.type == type; });
        return found == m_owners.end() ? nullptr : &*found;
    }

    bool GetUnique(EPetalType type, int cost, const std::string& getter) override
    {
        if (!m_initialized || !IsValidPetalType(static_cast<int>(type)) || cost < 0 || getter.empty())
        {
            LOG_FATAL("unique_petal", "GetUnique received invalid arguments");
            return false;
        }

        const SUniquePetalOwner* current_owner = FindOwner(type);
        const bool had_owner = current_owner != nullptr;
        const SUniquePetalOwner old_record = had_owner ? *current_owner : SUniquePetalOwner{};

        if (!SetOwner(type, cost, getter))
        {
            LOG_FATAL("unique_petal", "Failed to stage ownership of " + std::string(GetPetalTypeName(type)) +
                                          " for '" + getter + "'");
            return false;
        }

        SUniquePetalExchangeResult exchange_result;
        std::string exchange_error;
        if (!CAccountDataStore::ExchangeUniquePetal(had_owner ? old_record.owner : std::string{},
                                                     had_owner ? old_record.cost : 0, getter, cost,
                                                     static_cast<uint8_t>(type), &exchange_result, &exchange_error))
        {
            const bool registry_restored =
                had_owner ? SetOwner(old_record.type, old_record.cost, old_record.owner) : RemoveOwner(type);
            std::string message = "GetUnique failed for " + std::string(GetPetalTypeName(type)) + " and getter '" +
                                  getter + "'; account changes were rolled back: " + exchange_error;
            if (!registry_restored) message += "; failed to restore the unique owner registry";
            LOG_FATAL("unique_petal", message);
            return false;
        }

        CServer* server = CServer::GetInstance();
        INetworkModule* network = server ? server->GetNetworkModule() : nullptr;
        if (network)
        {
            for (const SAccountPetalMutation& mutation : exchange_result.account_mutations)
            {
                for (const auto& player_ptr : network->GetPlayers())
                {
                    CPlayer* player = player_ptr.get();
                    if (!player || !player->IsAuthenticated() || player->GetAccountName() != mutation.account_name)
                        continue;

                    if (auto* flower = dynamic_cast<CFlower*>(player->GetEntity()))
                    {
                        for (uint8_t slot_index : mutation.cleared_primary_slots)
                            flower->ForceUnequipPetal(slot_index);
                    }
                    network->QueueInventoryUpdate(*player);
                    network->QueueOwnerStateUpdate(*player);
                    break;
                }
            }
        }

        if (server && !server->BroadcastPetalReport(cost > 0 ? "forged" : "stolen", ERarity::Unique,
                                                     GetPetalTypeName(type), getter))
            LOG_ERROR("unique_petal", "Failed to broadcast GetUnique result for '" + getter + "'");
        return true;
    }

    bool SetOwner(EPetalType type, int cost, std::string owner) override
    {
        if (!m_initialized || !IsValidPetalType(static_cast<int>(type)) || cost < 0 || owner.empty()) return false;

        const auto found = std::find_if(m_owners.begin(), m_owners.end(),
                                        [type](const SUniquePetalOwner& entry) { return entry.type == type; });
        if (found == m_owners.end())
        {
            m_owners.push_back({ type, cost, std::move(owner) });
            SortOwners();
        } else
        {
            found->cost = cost;
            found->owner = std::move(owner);
        }
        return true;
    }

    bool RemoveOwner(EPetalType type) override
    {
        if (!m_initialized) return false;

        const auto old_size = m_owners.size();
        std::erase_if(m_owners, [type](const SUniquePetalOwner& entry) { return entry.type == type; });
        return m_owners.size() != old_size;
    }

  private:
    void SortOwners()
    {
        std::sort(m_owners.begin(), m_owners.end(), [](const SUniquePetalOwner& lhs, const SUniquePetalOwner& rhs) {
            return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
        });
    }

    std::filesystem::path m_path;
    std::vector<SUniquePetalOwner> m_owners;
    bool m_initialized = false;
};
} // namespace

std::unique_ptr<IUniquePetalRegistry> CreateUniquePetalRegistry()
{
    return std::make_unique<CJsonUniquePetalRegistry>();
}
