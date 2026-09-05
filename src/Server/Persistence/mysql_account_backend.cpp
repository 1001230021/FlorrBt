#include "mysql_account_backend.h"

#include "../../Shared/game_config.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

#ifdef FLORRBT_WITH_MYSQL

#include <mysql.h>
#include <mysqld_error.h>

namespace mysql_account_backend
{
namespace
{
constexpr int kSchemaVersion = 1;
// container column of account_items
constexpr int kContainerInventory = 0;
constexpr int kContainerSlots = 1;
constexpr int kContainerSecondarySlots = 2;

// Last successfully published state, used to skip unchanged accounts.
// Guarded by a mutex for safety, though callers are already sequential
// (startup load, then the single save worker).
std::mutex g_publish_state_mutex;
std::map<std::string, SPlayerAccount> g_published_accounts;
bool g_publish_state_seeded = false;

bool IsSandboxAccount(const std::string& name) { return name.rfind("@sandbox:", 0) == 0; }

bool IsSafeDatabaseIdentifier(const std::string& name)
{
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '$';
    });
}

// RAII wrapper over one MYSQL connection.
class CMysqlConnection
{
  public:
    ~CMysqlConnection()
    {
        if (m_connection) mysql_close(m_connection);
    }

    bool Open(std::string* error)
    {
        if (!IsSafeDatabaseIdentifier(game_config::mysql_database))
        {
            if (error) *error = "mysql_database must contain only letters, digits, '_' or '$'";
            return false;
        }

        m_connection = mysql_init(nullptr);
        if (!m_connection)
        {
            if (error) *error = "mysql_init failed (out of memory?)";
            return false;
        }
        unsigned int timeout_seconds = 10;
        mysql_options(m_connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);
        mysql_options(m_connection, MYSQL_OPT_READ_TIMEOUT, &timeout_seconds);
        mysql_options(m_connection, MYSQL_OPT_WRITE_TIMEOUT, &timeout_seconds);
        if (!mysql_real_connect(m_connection, game_config::mysql_host.c_str(), game_config::mysql_user.c_str(),
                                game_config::mysql_password.c_str(), game_config::mysql_database.c_str(),
                                static_cast<unsigned int>(std::max(0, game_config::mysql_port)), nullptr, 0))
        {
            // A fresh deployment often has the server running but not the
            // application database. Reconnect without a default database,
            // create this validated identifier, then select it. If the user
            // lacks CREATE privilege, the original connection error remains
            // actionable and no data is touched.
            if (mysql_errno(m_connection) == ER_BAD_DB_ERROR)
            {
                mysql_close(m_connection);
                m_connection = mysql_init(nullptr);
                if (!m_connection)
                {
                    if (error) *error = "mysql_init failed while retrying without a database";
                    return false;
                }
                unsigned int retry_timeout_seconds = 10;
                mysql_options(m_connection, MYSQL_OPT_CONNECT_TIMEOUT, &retry_timeout_seconds);
                mysql_options(m_connection, MYSQL_OPT_READ_TIMEOUT, &retry_timeout_seconds);
                mysql_options(m_connection, MYSQL_OPT_WRITE_TIMEOUT, &retry_timeout_seconds);
                if (mysql_real_connect(m_connection, game_config::mysql_host.c_str(), game_config::mysql_user.c_str(),
                                       game_config::mysql_password.c_str(), nullptr,
                                       static_cast<unsigned int>(std::max(0, game_config::mysql_port)), nullptr, 0))
                {
                    const std::string quoted_database = "`" + game_config::mysql_database + "`";
                    const std::string create_database =
                        "CREATE DATABASE IF NOT EXISTS " + quoted_database +
                        " CHARACTER SET utf8mb4 COLLATE utf8mb4_bin";
                    if (mysql_real_query(m_connection, create_database.c_str(),
                                         static_cast<unsigned long>(create_database.size())) == 0 &&
                        mysql_select_db(m_connection, game_config::mysql_database.c_str()) == 0)
                        return SetCharacterSet(error);
                }
            }
            if (error)
                *error = "Failed to connect to MySQL " + game_config::mysql_user + "@" + game_config::mysql_host +
                         ":" + std::to_string(game_config::mysql_port) + "/" + game_config::mysql_database + ": " +
                         mysql_error(m_connection);
            return false;
        }
        return SetCharacterSet(error);
    }

  private:
    bool SetCharacterSet(std::string* error)
    {
        if (mysql_set_character_set(m_connection, "utf8mb4") != 0)
        {
            if (error) *error = std::string("Failed to set utf8mb4 charset: ") + mysql_error(m_connection);
            return false;
        }
        return true;
    }

  public:

    bool Execute(const std::string& sql, std::string* error)
    {
        if (mysql_real_query(m_connection, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
        {
            if (error)
            {
                std::string head = sql.substr(0, 96);
                std::replace(head.begin(), head.end(), '\n', ' ');
                *error = "MySQL query failed (" + head + (sql.size() > 96 ? "..." : "") +
                         "): " + mysql_error(m_connection);
            }
            return false;
        }
        return true;
    }

    // Escapes arbitrary bytes for inclusion inside a single-quoted literal.
    std::string Escape(const std::string& value) const
    {
        std::string buffer;
        buffer.resize(value.size() * 2 + 1);
        const unsigned long written =
            mysql_real_escape_string(m_connection, buffer.data(), value.c_str(), static_cast<unsigned long>(value.size()));
        buffer.resize(written);
        return buffer;
    }

    std::string Quoted(const std::string& value) const { return "'" + Escape(value) + "'"; }

    MYSQL* Handle() { return m_connection; }

    MYSQL* m_connection = nullptr;
};

// RAII result set.
class CMysqlResult
{
  public:
    explicit CMysqlResult(MYSQL_RES* result) : m_result(result) {}
    ~CMysqlResult()
    {
        if (m_result) mysql_free_result(m_result);
    }
    CMysqlResult(const CMysqlResult&) = delete;
    CMysqlResult& operator=(const CMysqlResult&) = delete;

    bool Valid() const { return m_result != nullptr; }
    MYSQL_ROW Next() { return m_result ? mysql_fetch_row(m_result) : nullptr; }
    unsigned long* Lengths() const { return mysql_fetch_lengths(m_result); }

  private:
    MYSQL_RES* m_result = nullptr;
};

bool Query(CMysqlConnection& connection, const std::string& sql, std::string* error)
{
    return connection.Execute(sql, error);
}

// Runs a SELECT and hands rows to the visitor: visitor(MYSQL_ROW, lengths).
template <typename TVisitor>
bool QueryRows(CMysqlConnection& connection, const std::string& sql, std::string* error, TVisitor&& visitor)
{
    if (!connection.Execute(sql, error)) return false;
    CMysqlResult result(mysql_store_result(connection.Handle()));
    if (!result.Valid())
    {
        if (error) *error = std::string("MySQL result fetch failed: ") + mysql_error(connection.Handle());
        return false;
    }
    while (MYSQL_ROW row = result.Next()) visitor(row, result.Lengths());
    return true;
}

std::string FieldOrEmpty(MYSQL_ROW row, unsigned long* lengths, int index)
{
    if (!row[index]) return {};
    return std::string(row[index], lengths ? lengths[index] : strlen(row[index]));
}

long long FieldInt(MYSQL_ROW row, int index, long long fallback = 0)
{
    if (!row[index]) return fallback;
    try
    {
        return std::stoll(row[index]);
    } catch (...)
    {
        return fallback;
    }
}

bool EnsureSchema(CMysqlConnection& connection, std::string* error)
{
    // utf8mb4_bin keeps account-name comparisons byte-exact, matching the
    // in-memory store's case-sensitive lookups.
    static const char* kStatements[] = {
        "CREATE TABLE IF NOT EXISTS accounts ("
        " name VARCHAR(64) NOT NULL PRIMARY KEY,"
        " password_hash TEXT NOT NULL,"
        " email VARCHAR(255) NOT NULL DEFAULT '',"
        " email_verified TINYINT NOT NULL DEFAULT 0,"
        " trusted_ip VARCHAR(64) NOT NULL DEFAULT '',"
        " level INT NOT NULL DEFAULT 1,"
        " exp BIGINT NOT NULL DEFAULT 0,"
        " talent_points INT NOT NULL DEFAULT 0,"
        " extra_json MEDIUMTEXT NULL,"
        " updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS account_items ("
        " account_name VARCHAR(64) NOT NULL,"
        " container TINYINT NOT NULL,"
        " slot_index INT NOT NULL,"
        " petal_type SMALLINT NOT NULL,"
        " rarity SMALLINT NOT NULL,"
        " item_count INT UNSIGNED NOT NULL,"
        " PRIMARY KEY (account_name, container, slot_index),"
        " CONSTRAINT fk_account_items FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS account_talents ("
        " account_name VARCHAR(64) NOT NULL,"
        " ordinal INT NOT NULL,"
        " talent_id INT NOT NULL,"
        " rarity SMALLINT NOT NULL,"
        " talent_rank INT NOT NULL,"
        " PRIMARY KEY (account_name, ordinal),"
        " CONSTRAINT fk_account_talents FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS account_codex ("
        " account_name VARCHAR(64) NOT NULL,"
        " mob_type SMALLINT NOT NULL,"
        " rarity SMALLINT NOT NULL,"
        " kill_count INT UNSIGNED NOT NULL,"
        " PRIMARY KEY (account_name, mob_type, rarity),"
        " CONSTRAINT fk_account_codex FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS store_meta ("
        " meta_key VARCHAR(64) NOT NULL PRIMARY KEY,"
        " meta_value VARCHAR(255) NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",
    };
    for (const char* statement : kStatements)
        if (!Query(connection, statement, error)) return false;

    if (!Query(connection,
               "INSERT INTO store_meta (meta_key, meta_value) VALUES ('schema_version', '" +
                   std::to_string(kSchemaVersion) + "') ON DUPLICATE KEY UPDATE meta_key = meta_key",
               error))
        return false;
    return true;
}

void AppendItemRows(std::ostringstream& values, CMysqlConnection& connection, const std::string& quoted_name,
                    int container, const std::vector<SInventoryItem>& items, bool skip_empty, bool& first)
{
    (void)connection;
    for (size_t index = 0; index < items.size(); ++index)
    {
        const SInventoryItem& item = items[index];
        // The inventory is a bag (empty stacks carry no meaning), while slot
        // containers must keep their empty positions to preserve layout.
        if (skip_empty && (item.petal_type == 0 || item.rarity == 0 || item.count == 0)) continue;
        if (!first) values << ",";
        first = false;
        values << "(" << quoted_name << "," << container << "," << index << "," << static_cast<int>(item.petal_type)
               << "," << static_cast<int>(item.rarity) << "," << item.count << ")";
    }
}

bool WriteAccountRows(CMysqlConnection& connection, const SPlayerAccount& account, std::string* error)
{
    const std::string quoted_name = connection.Quoted(account.name);

    std::ostringstream upsert;
    upsert << "INSERT INTO accounts (name, password_hash, email, email_verified, trusted_ip, level, exp,"
           << " talent_points, extra_json) VALUES (" << quoted_name << "," << connection.Quoted(account.password)
           << "," << connection.Quoted(account.email) << "," << (account.email_verified ? 1 : 0) << ","
           << connection.Quoted(account.trusted_ip) << "," << std::max(1, account.level) << ","
           << std::max<std::int64_t>(0, account.exp) << "," << std::max(0, account.talent_points) << ","
           << connection.Quoted(account.extra_json.empty() ? std::string("{}") : account.extra_json) << ")"
           << " ON DUPLICATE KEY UPDATE password_hash = VALUES(password_hash), email = VALUES(email),"
           << " email_verified = VALUES(email_verified), trusted_ip = VALUES(trusted_ip), level = VALUES(level),"
           << " exp = VALUES(exp), talent_points = VALUES(talent_points), extra_json = VALUES(extra_json)";
    if (!Query(connection, upsert.str(), error)) return false;

    if (!Query(connection, "DELETE FROM account_items WHERE account_name = " + quoted_name, error)) return false;
    if (!Query(connection, "DELETE FROM account_talents WHERE account_name = " + quoted_name, error)) return false;
    if (!Query(connection, "DELETE FROM account_codex WHERE account_name = " + quoted_name, error)) return false;

    std::ostringstream item_values;
    bool first_item = true;
    AppendItemRows(item_values, connection, quoted_name, kContainerInventory, account.inventory, true, first_item);
    AppendItemRows(item_values, connection, quoted_name, kContainerSlots, account.slots, false, first_item);
    AppendItemRows(item_values, connection, quoted_name, kContainerSecondarySlots, account.secondary_slots, false,
                   first_item);
    if (!first_item)
    {
        if (!Query(connection,
                   "INSERT INTO account_items (account_name, container, slot_index, petal_type, rarity, item_count)"
                   " VALUES " +
                       item_values.str(),
                   error))
            return false;
    }

    if (!account.talents.empty())
    {
        std::ostringstream talent_values;
        for (size_t index = 0; index < account.talents.size(); ++index)
        {
            const SAccountTalent& talent = account.talents[index];
            if (index > 0) talent_values << ",";
            talent_values << "(" << quoted_name << "," << index << "," << static_cast<int>(talent.id) << ","
                          << static_cast<int>(talent.rarity) << "," << talent.rank << ")";
        }
        if (!Query(connection,
                   "INSERT INTO account_talents (account_name, ordinal, talent_id, rarity, talent_rank) VALUES " +
                       talent_values.str(),
                   error))
            return false;
    }

    if (!account.codex.empty())
    {
        std::ostringstream codex_values;
        bool first_codex = true;
        std::set<std::pair<int, int>> seen_codex_keys;
        for (const SAccountCodexEntry& entry : account.codex)
        {
            if (entry.mob_type == 0 || entry.rarity == 0 || entry.count == 0) continue;
            // The table key is (mob, rarity); collapse duplicates the same way
            // the in-memory store treats them (first entry wins on load).
            if (!seen_codex_keys.insert({ entry.mob_type, entry.rarity }).second) continue;
            if (!first_codex) codex_values << ",";
            first_codex = false;
            codex_values << "(" << quoted_name << "," << static_cast<int>(entry.mob_type) << ","
                         << static_cast<int>(entry.rarity) << "," << entry.count << ")";
        }
        if (!first_codex)
        {
            if (!Query(connection,
                       "INSERT INTO account_codex (account_name, mob_type, rarity, kill_count) VALUES " +
                           codex_values.str(),
                       error))
                return false;
        }
    }
    return true;
}
} // namespace

bool Available() { return true; }

bool Initialize(std::string* error)
{
    if (error) error->clear();
    CMysqlConnection connection;
    if (!connection.Open(error)) return false;
    return EnsureSchema(connection, error);
}

bool LoadAll(std::vector<SPlayerAccount>& accounts, std::uint64_t& generation, std::string* error)
{
    if (error) error->clear();
    accounts.clear();
    generation = 0;

    CMysqlConnection connection;
    if (!connection.Open(error)) return false;
    if (!EnsureSchema(connection, error)) return false;

    std::map<std::string, size_t> index_by_name;
    bool ok = QueryRows(
        connection,
        "SELECT name, password_hash, email, email_verified, trusted_ip, level, exp, talent_points, extra_json"
        " FROM accounts ORDER BY name",
        error, [&](MYSQL_ROW row, unsigned long* lengths) {
            SPlayerAccount account;
            account.name = FieldOrEmpty(row, lengths, 0);
            account.password = FieldOrEmpty(row, lengths, 1);
            account.email = FieldOrEmpty(row, lengths, 2);
            account.email_verified = FieldInt(row, 3) != 0;
            account.trusted_ip = FieldOrEmpty(row, lengths, 4);
            account.level = std::max(1, static_cast<int>(FieldInt(row, 5, 1)));
            account.exp = FieldInt(row, 6, 0);
            account.talent_points = std::max(0, static_cast<int>(FieldInt(row, 7, 0)));
            account.extra_json = FieldOrEmpty(row, lengths, 8);
            if (account.extra_json.empty()) account.extra_json = "{}";
            index_by_name[account.name] = accounts.size();
            accounts.push_back(std::move(account));
        });
    if (!ok) return false;

    ok = QueryRows(connection,
                   "SELECT account_name, container, slot_index, petal_type, rarity, item_count FROM account_items"
                   " ORDER BY account_name, container, slot_index",
                   error, [&](MYSQL_ROW row, unsigned long* lengths) {
                       const auto it = index_by_name.find(FieldOrEmpty(row, lengths, 0));
                       if (it == index_by_name.end()) return;
                       SPlayerAccount& account = accounts[it->second];
                       const int container = static_cast<int>(FieldInt(row, 1));
                       const int slot_index = static_cast<int>(FieldInt(row, 2));
                       SInventoryItem item;
                       item.petal_type = static_cast<uint8_t>(FieldInt(row, 3));
                       item.rarity = static_cast<uint8_t>(FieldInt(row, 4));
                       item.count = static_cast<uint32_t>(std::max<long long>(0, FieldInt(row, 5)));
                       if (container == kContainerInventory)
                       {
                           if (item.petal_type != 0 && item.rarity != 0 && item.count > 0)
                               account.inventory.push_back(item);
                           return;
                       }
                       std::vector<SInventoryItem>& slots =
                           container == kContainerSlots ? account.slots : account.secondary_slots;
                       if (slot_index < 0 || slot_index > 255) return;
                       if (static_cast<size_t>(slot_index) >= slots.size())
                           slots.resize(static_cast<size_t>(slot_index) + 1);
                       slots[static_cast<size_t>(slot_index)] = item;
                   });
    if (!ok) return false;

    ok = QueryRows(connection,
                   "SELECT account_name, ordinal, talent_id, rarity, talent_rank FROM account_talents"
                   " ORDER BY account_name, ordinal",
                   error, [&](MYSQL_ROW row, unsigned long* lengths) {
                       const auto it = index_by_name.find(FieldOrEmpty(row, lengths, 0));
                       if (it == index_by_name.end()) return;
                       SAccountTalent talent;
                       talent.id = static_cast<ETalentId>(FieldInt(row, 2));
                       talent.rarity = static_cast<ERarity>(FieldInt(row, 3));
                       talent.rank = static_cast<int>(FieldInt(row, 4));
                       accounts[it->second].talents.push_back(talent);
                   });
    if (!ok) return false;

    ok = QueryRows(connection,
                   "SELECT account_name, mob_type, rarity, kill_count FROM account_codex"
                   " ORDER BY account_name, mob_type, rarity",
                   error, [&](MYSQL_ROW row, unsigned long* lengths) {
                       const auto it = index_by_name.find(FieldOrEmpty(row, lengths, 0));
                       if (it == index_by_name.end()) return;
                       SAccountCodexEntry entry;
                       entry.mob_type = static_cast<uint8_t>(FieldInt(row, 1));
                       entry.rarity = static_cast<uint8_t>(FieldInt(row, 2));
                       entry.count = static_cast<uint32_t>(std::max<long long>(0, FieldInt(row, 3)));
                       if (entry.mob_type > 0 && entry.rarity > 0 && entry.count > 0)
                           accounts[it->second].codex.push_back(entry);
                   });
    if (!ok) return false;

    ok = QueryRows(connection, "SELECT meta_value FROM store_meta WHERE meta_key = 'save_generation'", error,
                   [&](MYSQL_ROW row, unsigned long*) {
                       try
                       {
                           generation = row[0] ? std::stoull(row[0]) : 0;
                       } catch (...)
                       {
                           generation = 0;
                       }
                   });
    if (!ok) return false;

    // Seed change detection so the first post-load publish only writes what
    // actually changed since startup.
    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        g_published_accounts.clear();
        for (const SPlayerAccount& account : accounts) g_published_accounts[account.name] = account;
        g_publish_state_seeded = true;
    }
    return true;
}

bool PublishSnapshot(const std::vector<SPlayerAccount>& accounts, std::uint64_t generation, std::string* error)
{
    if (error) error->clear();

    // Work out the change set against the last published state.
    std::vector<const SPlayerAccount*> changed;
    std::vector<std::string> removed;
    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        std::set<std::string> current_names;
        for (const SPlayerAccount& account : accounts)
        {
            if (IsSandboxAccount(account.name) || account.name.empty()) continue;
            current_names.insert(account.name);
            const auto it = g_published_accounts.find(account.name);
            if (!g_publish_state_seeded || it == g_published_accounts.end() || it->second != account)
                changed.push_back(&account);
        }
        for (const auto& [name, unused] : g_published_accounts)
            if (!current_names.count(name)) removed.push_back(name);
    }

    CMysqlConnection connection;
    if (!connection.Open(error)) return false;
    if (!EnsureSchema(connection, error)) return false;
    if (!Query(connection, "START TRANSACTION", error)) return false;

    bool ok = true;
    std::string transaction_error;
    for (const SPlayerAccount* account : changed)
    {
        if (!WriteAccountRows(connection, *account, &transaction_error))
        {
            ok = false;
            break;
        }
    }
    if (ok)
    {
        for (const std::string& name : removed)
        {
            if (!Query(connection, "DELETE FROM accounts WHERE name = " + connection.Quoted(name),
                       &transaction_error))
            {
                ok = false;
                break;
            }
        }
    }
    if (ok)
        ok = Query(connection,
                   "INSERT INTO store_meta (meta_key, meta_value) VALUES ('save_generation', '" +
                       std::to_string(generation) +
                       "') ON DUPLICATE KEY UPDATE meta_value = VALUES(meta_value)",
                   &transaction_error);
    if (ok) ok = Query(connection, "COMMIT", &transaction_error);

    if (!ok)
    {
        std::string rollback_error;
        Query(connection, "ROLLBACK", &rollback_error);
        if (error) *error = transaction_error;
        return false;
    }

    // Commit succeeded: fold the change set into the published state.
    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        for (const SPlayerAccount* account : changed) g_published_accounts[account->name] = *account;
        for (const std::string& name : removed) g_published_accounts.erase(name);
        g_publish_state_seeded = true;
    }
    return true;
}

void ResetPublishState()
{
    std::lock_guard<std::mutex> lock(g_publish_state_mutex);
    g_published_accounts.clear();
    g_publish_state_seeded = false;
}
} // namespace mysql_account_backend

#else // !FLORRBT_WITH_MYSQL

namespace mysql_account_backend
{
namespace
{
bool Unavailable(std::string* error)
{
    if (error)
        *error = "This server binary was built without MySQL support (FLORRBT_WITH_MYSQL); "
                 "set account_backend json or rebuild with the MySQL client library.";
    return false;
}
} // namespace

bool Available() { return false; }
bool Initialize(std::string* error) { return Unavailable(error); }
bool LoadAll(std::vector<SPlayerAccount>&, std::uint64_t&, std::string* error) { return Unavailable(error); }
bool PublishSnapshot(const std::vector<SPlayerAccount>&, std::uint64_t, std::string* error)
{
    return Unavailable(error);
}
void ResetPublishState() {}
} // namespace mysql_account_backend

#endif // FLORRBT_WITH_MYSQL
