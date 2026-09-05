#include "account_migration.h"

#include "account_store.h"
#include "mysql_account_backend.h"
#include "../../Shared/game_config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
bool IsSandboxAccount(const std::string& name) { return name.rfind("@sandbox:", 0) == 0; }

// Minimal standalone server.cfg reader: applies every `set <key> <value>`
// line plus the MySQL secret override, without starting a server console.
void ApplyStartupConfig()
{
    std::ifstream file(game_config::startup_commands_path);
    std::string line;
    while (file && std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos || line.compare(begin, 2, "//") == 0) continue;
        if (line.compare(begin, 4, "set ") != 0 && line.compare(begin, 4, "set\t") != 0) continue;

        const size_t key_begin = line.find_first_not_of(" \t", begin + 4);
        if (key_begin == std::string::npos) continue;
        const size_t key_end = line.find_first_of(" \t", key_begin);
        if (key_end == std::string::npos) continue;
        const size_t value_begin = line.find_first_not_of(" \t", key_end);
        if (value_begin == std::string::npos) continue;

        std::string value = line.substr(value_begin);
        const size_t value_end = value.find_last_not_of(" \t");
        if (value_end != std::string::npos) value.resize(value_end + 1);
        game_config::SetConfig(line.substr(key_begin, key_end - key_begin), value);
    }

    if (const char* password = std::getenv("FLORRBT_MYSQL_PASSWORD"); password && *password)
        game_config::mysql_password = password;
}

struct SMigrationStats
{
    size_t accounts = 0;
    size_t inventory_items = 0;
    size_t slot_items = 0;
    size_t talents = 0;
    size_t codex_entries = 0;
};

SMigrationStats CollectStats(const std::vector<SPlayerAccount>& accounts)
{
    SMigrationStats stats;
    for (const SPlayerAccount& account : accounts)
    {
        if (IsSandboxAccount(account.name)) continue;
        ++stats.accounts;
        stats.inventory_items += account.inventory.size();
        stats.slot_items += account.slots.size() + account.secondary_slots.size();
        stats.talents += account.talents.size();
        stats.codex_entries += account.codex.size();
    }
    return stats;
}

// Lists which fields differ so a failed verification is actionable.
std::string DescribeAccountDifference(const SPlayerAccount& expected, const SPlayerAccount& actual)
{
    std::string fields;
    auto add = [&fields](const char* name) {
        if (!fields.empty()) fields += ", ";
        fields += name;
    };
    if (expected.password != actual.password) add("password");
    if (expected.email != actual.email) add("email");
    if (expected.email_verified != actual.email_verified) add("email_verified");
    if (expected.trusted_ip != actual.trusted_ip) add("trusted_ip");
    if (expected.level != actual.level) add("level");
    if (expected.exp != actual.exp) add("exp");
    if (expected.inventory != actual.inventory) add("inventory");
    if (expected.slots != actual.slots) add("slots");
    if (expected.secondary_slots != actual.secondary_slots) add("secondary_slots");
    if (expected.extra_json != actual.extra_json) add("extra_json");
    if (expected.talent_points != actual.talent_points) add("talent_points");
    if (expected.talents != actual.talents) add("talents");
    if (expected.codex != actual.codex) add("codex");
    return fields.empty() ? "unknown difference" : fields;
}

bool VerifyRoundTrip(const std::vector<SPlayerAccount>& source, const std::vector<SPlayerAccount>& reloaded)
{
    std::map<std::string, const SPlayerAccount*> reloaded_by_name;
    for (const SPlayerAccount& account : reloaded) reloaded_by_name[account.name] = &account;

    bool ok = true;
    size_t verified = 0;
    for (const SPlayerAccount& account : source)
    {
        if (IsSandboxAccount(account.name)) continue;
        const auto it = reloaded_by_name.find(account.name);
        if (it == reloaded_by_name.end())
        {
            std::cout << "  MISSING after migration: " << account.name << "\n";
            ok = false;
            continue;
        }
        if (!(account == *it->second))
        {
            std::cout << "  MISMATCH " << account.name << ": " << DescribeAccountDifference(account, *it->second)
                      << "\n";
            ok = false;
            continue;
        }
        ++verified;
        reloaded_by_name.erase(it);
    }
    for (const auto& [name, unused] : reloaded_by_name)
        std::cout << "  UNEXPECTED extra account after migration: " << name << "\n";
    if (!reloaded_by_name.empty()) ok = false;

    std::cout << "  Verified byte-identical accounts: " << verified << "\n";
    return ok;
}

void PrintStats(const char* label, const SMigrationStats& stats)
{
    std::cout << label << ": " << stats.accounts << " accounts, " << stats.inventory_items << " inventory stacks, "
              << stats.slot_items << " slot entries, " << stats.talents << " talents, " << stats.codex_entries
              << " codex entries\n";
}

int MigrateJsonToMysql(const std::string& json_path_override)
{
    ApplyStartupConfig();
    const std::string json_path = json_path_override.empty() ? game_config::account_data_path : json_path_override;

    std::cout << "Migrating accounts JSON -> MySQL\n";
    std::cout << "  Source: " << json_path << "\n";
    std::cout << "  Target: " << game_config::mysql_user << "@" << game_config::mysql_host << ":"
              << game_config::mysql_port << "/" << game_config::mysql_database << "\n";

    if (!mysql_account_backend::Available())
    {
        std::cout << "ERROR: this binary was built without MySQL support.\n";
        return 1;
    }

    // Force the JSON backend for the load regardless of server.cfg so the
    // migration always reads the file it was pointed at.
    game_config::account_backend = "json";
    std::string error;
    if (!CAccountDataStore::Load(json_path, &error))
    {
        std::cout << "ERROR: failed to load " << json_path << ": " << error << "\n";
        return 1;
    }
    if (!error.empty()) std::cout << "  Load warning: " << error << "\n";

    const std::vector<SPlayerAccount> accounts = CAccountDataStore::SnapshotAccounts();
    const std::uint64_t generation = CAccountDataStore::SnapshotGeneration();
    CAccountDataStore::ShutDown();
    PrintStats("  Loaded", CollectStats(accounts));

    if (!mysql_account_backend::Initialize(&error))
    {
        std::cout << "ERROR: " << error << "\n";
        return 1;
    }
    // Publish everything as one fresh snapshot (no change detection).
    mysql_account_backend::ResetPublishState();
    if (!mysql_account_backend::PublishSnapshot(accounts, generation, &error))
    {
        std::cout << "ERROR: failed to write accounts into MySQL: " << error << "\n";
        return 1;
    }

    std::vector<SPlayerAccount> reloaded;
    std::uint64_t reloaded_generation = 0;
    if (!mysql_account_backend::LoadAll(reloaded, reloaded_generation, &error))
    {
        std::cout << "ERROR: failed to reload accounts from MySQL for verification: " << error << "\n";
        return 1;
    }
    PrintStats("  Stored", CollectStats(reloaded));
    std::cout << "  Save generation: " << generation << " -> " << reloaded_generation << "\n";

    const bool verified = VerifyRoundTrip(accounts, reloaded) && reloaded_generation == generation;
    if (!verified)
    {
        std::cout << "MIGRATION VERIFICATION FAILED - MySQL data does not match the JSON source.\n";
        return 1;
    }
    std::cout << "Migration OK. Set `set account_backend mysql` in data/server.cfg to switch the server over.\n";
    return 0;
}

int MigrateMysqlToJson(const std::string& json_path)
{
    ApplyStartupConfig();
    std::cout << "Migrating accounts MySQL -> JSON\n";
    std::cout << "  Source: " << game_config::mysql_user << "@" << game_config::mysql_host << ":"
              << game_config::mysql_port << "/" << game_config::mysql_database << "\n";
    std::cout << "  Target: " << json_path << "\n";

    if (!mysql_account_backend::Available())
    {
        std::cout << "ERROR: this binary was built without MySQL support.\n";
        return 1;
    }

    std::string error;
    std::vector<SPlayerAccount> accounts;
    std::uint64_t generation = 0;
    if (!mysql_account_backend::LoadAll(accounts, generation, &error))
    {
        std::cout << "ERROR: failed to load accounts from MySQL: " << error << "\n";
        return 1;
    }
    PrintStats("  Loaded", CollectStats(accounts));

    game_config::account_backend = "json";
    if (!CAccountDataStore::Load(json_path, &error))
    {
        std::cout << "ERROR: failed to open target JSON store " << json_path << ": " << error << "\n";
        return 1;
    }
    if (!CAccountDataStore::ImportAccounts(accounts, &error) || !CAccountDataStore::Flush(&error))
    {
        std::cout << "ERROR: failed to write " << json_path << ": " << error << "\n";
        CAccountDataStore::ShutDown();
        return 1;
    }
    if (!CAccountDataStore::ShutDown(&error))
    {
        std::cout << "ERROR: failed to finalize " << json_path << ": " << error << "\n";
        return 1;
    }
    std::cout << "Rollback export OK: " << json_path << "\n";
    return 0;
}
} // namespace

std::optional<int> RunAccountMigrationFromArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i] ? argv[i] : "";
        if (argument == "--migrate-accounts-to-mysql")
        {
            std::string json_path;
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-') json_path = argv[i + 1];
            return MigrateJsonToMysql(json_path);
        }
        if (argument == "--migrate-accounts-to-json")
        {
            if (i + 1 >= argc || !argv[i + 1] || argv[i + 1][0] == '-')
            {
                std::cout << "Usage: --migrate-accounts-to-json <output.json>\n";
                return 1;
            }
            return MigrateMysqlToJson(argv[i + 1]);
        }
    }
    return std::nullopt;
}
