#include "mysql_account_backend.h"
#include "economy_codec.h"
#include "account_identity_codec.h"
#include "../../Engine/json_value.h"

#include "../../Shared/game_config.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <map>
#include <limits>
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
constexpr int kContainerInventory = 0;
constexpr int kContainerSlots = 1;
constexpr int kContainerSecondarySlots = 2;

// 缓存最后成功发布的状态；加载与保存顺序执行，额外由互斥锁保护。
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

class CMysqlConnection
{
  public:
    ~CMysqlConnection()
    {
        if (m_connection) mysql_close(m_connection);
    }

    bool Open(std::string* error)
    {
        if (m_connection && m_reusable && mysql_ping(m_connection) == 0) return true;
        if (m_connection) mysql_close(m_connection);
        m_connection = nullptr;
        m_schema_ready = false;
        m_reusable = false;
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
            // 初次部署可能尚无应用数据库；先不指定库连接，再创建已验证名称并选择。
            // 缺少建库权限时保留连接错误，不修改已有数据。
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
        m_reusable = true;
        return true;
    }

  public:

    bool Execute(const std::string& sql, std::string* error)
    {
        if (mysql_real_query(m_connection, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0)
        {
            m_reusable = false;
            if (error)
            {
                // 语句可能包含密码哈希和账号内容，不能直接记录。
                std::string head = sql.substr(0, sql.find(' '));
                std::replace(head.begin(), head.end(), '\n', ' ');
                *error = "MySQL query failed (" + head + (sql.size() > 96 ? "..." : "") +
                         "): " + mysql_error(m_connection);
            }
            return false;
        }
        return true;
    }

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
    bool SchemaReady() const { return m_schema_ready; }
    void MarkSchemaReady() { m_schema_ready = true; }
  private:
    MYSQL* m_connection = nullptr;
    bool m_schema_ready = false;
    bool m_reusable = false;
};

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
    const char* begin = row[index];
    const char* end = begin + std::strlen(begin);
    long long value = 0;
    const auto parsed = std::from_chars(begin, end, value, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end ? value : fallback;
}

bool EnsureSchema(CMysqlConnection& connection, std::string* error)
{
    if (connection.SchemaReady()) return true;
    // 采用二进制排序，保持账号名比较与内存中的大小写敏感查找一致。
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

        "CREATE TABLE IF NOT EXISTS account_unique_petals ("
        " petal_type SMALLINT NOT NULL PRIMARY KEY,"
        " account_name VARCHAR(64) NOT NULL,"
        " forge_cost INT UNSIGNED NOT NULL,"
        " CONSTRAINT fk_account_unique FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS account_recovery_credentials ("
        " account_name VARCHAR(64) NOT NULL,"
        " device_id CHAR(32) NOT NULL,"
        " verifier VARCHAR(128) NOT NULL,"
        " expires_at BIGINT NOT NULL,"
        " PRIMARY KEY (account_name, device_id),"
        " CONSTRAINT fk_account_recovery FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",
        "CREATE TABLE IF NOT EXISTS account_identity_state ("
        " account_name VARCHAR(64) NOT NULL PRIMARY KEY, account_id CHAR(32) NOT NULL UNIQUE, state_json MEDIUMTEXT NOT NULL,"
        " CONSTRAINT fk_account_identity FOREIGN KEY (account_name) REFERENCES accounts(name) ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",
        "CREATE TABLE IF NOT EXISTS account_external_identities ("
        " provider VARCHAR(16) NOT NULL, application VARCHAR(128) NOT NULL, subject VARCHAR(128) NOT NULL, account_name VARCHAR(64) NOT NULL,"
        " PRIMARY KEY (provider, application, subject),"
        " CONSTRAINT fk_account_external FOREIGN KEY (account_name) REFERENCES accounts(name) ON DELETE CASCADE ON UPDATE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin",

        "CREATE TABLE IF NOT EXISTS account_economy_state ("
        " account_name VARCHAR(64) NOT NULL PRIMARY KEY,"
        " state_json MEDIUMTEXT NOT NULL,"
        " CONSTRAINT fk_account_economy FOREIGN KEY (account_name) REFERENCES accounts(name)"
        "  ON DELETE CASCADE ON UPDATE CASCADE"
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
    connection.MarkSchemaReady();
    return true;
}

void AppendItemRows(std::ostringstream& values, CMysqlConnection& connection, const std::string& quoted_name,
                    int container, const std::vector<SInventoryItem>& items, bool skip_empty, bool& first)
{
    (void)connection;
    for (size_t index = 0; index < items.size(); ++index)
    {
        const SInventoryItem& item = items[index];
        // 背包可忽略空堆叠，装备槽必须保留空位以维持布局。
        if (skip_empty && (item.petal_type == 0 || item.rarity == 0 || item.count == 0)) continue;
        if (!first) values << ",";
        first = false;
        values << "(" << quoted_name << "," << container << "," << index << "," << static_cast<int>(item.petal_type)
               << "," << static_cast<int>(item.rarity) << "," << item.count << ")";
    }
}

bool WriteAccountRows(CMysqlConnection& connection, const SPlayerAccount& account, std::string* error,
                      bool items_changed, bool talents_changed, bool codex_changed)
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

    if (items_changed && !Query(connection, "DELETE FROM account_items WHERE account_name = " + quoted_name, error)) return false;
    if (talents_changed && !Query(connection, "DELETE FROM account_talents WHERE account_name = " + quoted_name, error)) return false;
    if (codex_changed && !Query(connection, "DELETE FROM account_codex WHERE account_name = " + quoted_name, error)) return false;

    std::ostringstream item_values;
    bool first_item = true;
    if (items_changed)
    {
        AppendItemRows(item_values, connection, quoted_name, kContainerInventory, account.inventory, true, first_item);
        AppendItemRows(item_values, connection, quoted_name, kContainerSlots, account.slots, false, first_item);
        AppendItemRows(item_values, connection, quoted_name, kContainerSecondarySlots, account.secondary_slots, false,
                       first_item);
    }
    if (!first_item)
    {
        if (!Query(connection,
                   "INSERT INTO account_items (account_name, container, slot_index, petal_type, rarity, item_count)"
                   " VALUES " +
                       item_values.str(),
                   error))
            return false;
    }

    if (talents_changed && !account.talents.empty())
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

    if (codex_changed && !account.codex.empty())
    {
        std::ostringstream codex_values;
        bool first_codex = true;
        std::set<std::pair<int, int>> seen_codex_keys;
        for (const SAccountCodexEntry& entry : account.codex)
        {
            if (entry.mob_type == 0 || entry.rarity == 0 || entry.count == 0) continue;
            // 按生物及稀有度合并重复项，与内存加载一致保留首项。
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
}

bool Available() { return true; }

bool Initialize(std::string* error)
{
    if (error) error->clear();
    CMysqlConnection connection;
    if (!connection.Open(error)) return false;
    return EnsureSchema(connection, error);
}

bool LoadAll(std::vector<SPlayerAccount>& accounts, std::uint64_t& generation, std::string* error,
             bool* unique_ledger_initialized)
{
    if (error) error->clear();
    accounts.clear();
    generation = 0;
    if (unique_ledger_initialized) *unique_ledger_initialized = false;

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

    ok = QueryRows(connection,
        "SELECT account_name, device_id, verifier, expires_at FROM account_recovery_credentials ORDER BY account_name, expires_at, device_id",
        error, [&](MYSQL_ROW row, unsigned long* lengths) {
            const auto account = index_by_name.find(FieldOrEmpty(row, lengths, 0));
            if (account == index_by_name.end()) return;
            accounts[account->second].recovery_credentials.push_back({FieldOrEmpty(row, lengths, 1), FieldOrEmpty(row, lengths, 2), FieldInt(row, 3)});
        });
    if (!ok) return false;

    bool invalid_economy_state = false;
    bool invalid_identity = false;
    ok = QueryRows(connection, "SELECT account_name, state_json FROM account_identity_state", error,
        [&](MYSQL_ROW row, unsigned long* lengths) {
            const auto account = index_by_name.find(FieldOrEmpty(row, lengths, 0));
            const auto value = CJsonValue::Parse(FieldOrEmpty(row, lengths, 1), error);
            if (account == index_by_name.end() || !value || !ReadAccountIdentity(&*value, accounts[account->second], error)) invalid_identity = true;
        });
    if (!ok || invalid_identity) return false;
    ok = QueryRows(connection, "SELECT account_name, state_json FROM account_economy_state", error,
        [&](MYSQL_ROW row, unsigned long* lengths) {
            const auto account = index_by_name.find(FieldOrEmpty(row, lengths, 0));
            const auto value = CJsonValue::Parse(FieldOrEmpty(row, lengths, 1), error);
            if (account == index_by_name.end() || !value || !ReadEconomyState(&*value, accounts[account->second].economy, error))
                invalid_economy_state = true;
        });
    if (!ok || invalid_economy_state) return false;

    bool invalid_unique_row = false;
    ok = QueryRows(connection,
                   "SELECT account_name, petal_type, forge_cost FROM account_unique_petals ORDER BY account_name, petal_type",
                   error, [&](MYSQL_ROW row, unsigned long* lengths) {
                       const auto account = index_by_name.find(FieldOrEmpty(row, lengths, 0));
                       const auto type = FieldInt(row, 1, -1), cost = FieldInt(row, 2, -1);
                       if (account == index_by_name.end() || type <= 0 || type > 255 || !IsKnownPetalTypeId(static_cast<int>(type)) ||
                           cost < 0 || cost > (std::numeric_limits<int>::max)())
                       { invalid_unique_row = true; return; }
                       accounts[account->second].unique_petals.push_back({static_cast<uint8_t>(type), static_cast<int>(cost)});
                   });
    if (!ok) return false;
    if (invalid_unique_row) { if (error) *error = "Invalid MySQL Unique ownership record"; return false; }
    bool invalid_ledger_version = false;
    ok = QueryRows(connection, "SELECT meta_value FROM store_meta WHERE meta_key = 'unique_ledger_version'", error,
                   [&](MYSQL_ROW row, unsigned long* lengths) {
                       const auto version = FieldOrEmpty(row, lengths, 0);
                       if (version != "0" && version != "1") invalid_ledger_version = true;
                       if (unique_ledger_initialized) *unique_ledger_initialized = version == "1";
                   });
    if (!ok) return false;
    if (invalid_ledger_version) { if (error) *error = "Unsupported MySQL Unique ledger version"; return false; }

    ok = QueryRows(connection, "SELECT meta_value FROM store_meta WHERE meta_key = 'save_generation'", error,
                   [&](MYSQL_ROW row, unsigned long*) {
                       if (!row[0])
                       {
                           generation = 0;
                           return;
                       }
                       const char* begin = row[0];
                       const char* end = begin + std::strlen(begin);
                       std::uint64_t parsed_generation = 0;
                       const auto parsed = std::from_chars(begin, end, parsed_generation, 10);
                       generation = parsed.ec == std::errc{} && parsed.ptr == end ? parsed_generation : 0;
                   });
    if (!ok) return false;

    // 加载后建立差异基线，首次保存只写入变化。
    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        g_published_accounts.clear();
        for (const SPlayerAccount& account : accounts) g_published_accounts[account.name] = account;
        g_publish_state_seeded = true;
    }
    return true;
}

bool PublishSnapshot(const SAccountSnapshot& accounts, std::uint64_t generation, std::string* error,
                     bool unique_ledger_initialized)
{
    if (error) error->clear();

    struct Change { const SPlayerAccount* account; bool items; bool talents; bool codex; bool unique; bool recovery; bool economy; bool identity; };
    std::vector<Change> changed;
    std::vector<std::string> removed;
    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        std::set<std::string> current_names;
        for (const auto& captured : accounts)
        {
            if (!captured) continue;
            const SPlayerAccount& account = *captured;
            if (IsSandboxAccount(account.name) || account.name.empty()) continue;
            current_names.insert(account.name);
            const auto it = g_published_accounts.find(account.name);
            if (!g_publish_state_seeded || it == g_published_accounts.end() || it->second != account)
            {
                const bool fresh = !g_publish_state_seeded || it == g_published_accounts.end();
                changed.push_back({ &account,
                    fresh || it->second.inventory != account.inventory || it->second.slots != account.slots ||
                             it->second.secondary_slots != account.secondary_slots,
                    fresh || it->second.talents != account.talents,
                    fresh || it->second.codex != account.codex,
                    fresh || it->second.unique_petals != account.unique_petals,
                    fresh || it->second.recovery_credentials != account.recovery_credentials,
                    fresh || it->second.economy != account.economy,
                    fresh || it->second.account_id != account.account_id || it->second.linked_identities != account.linked_identities });
            }
        }
        for (const auto& [name, unused] : g_published_accounts)
            if (!current_names.count(name)) removed.push_back(name);
    }

    thread_local CMysqlConnection connection;
    if (!connection.Open(error)) return false;
    if (!EnsureSchema(connection, error)) return false;
    if (!Query(connection, "START TRANSACTION", error)) return false;

    bool ok = true;
    std::string transaction_error;
    for (const Change& change : changed)
    {
        if (!WriteAccountRows(connection, *change.account, &transaction_error, change.items, change.talents, change.codex))
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
    {
        // 先删除双方旧归属再插入新归属，转移不依赖账号遍历顺序。
        for (const Change& change : changed)
            if (change.unique && !Query(connection,
                "DELETE FROM account_unique_petals WHERE account_name = " + connection.Quoted(change.account->name),
                &transaction_error)) { ok = false; break; }
    }
    if (ok)
    {
        for (const Change& change : changed)
        {
            if (!change.unique) continue;
            for (const auto& record : change.account->unique_petals)
                if (!Query(connection, "INSERT INTO account_unique_petals (petal_type, account_name, forge_cost) VALUES (" +
                    std::to_string(record.petal_type) + "," + connection.Quoted(change.account->name) + "," +
                    std::to_string(record.cost) + ")", &transaction_error)) { ok = false; break; }
            if (!ok) break;
        }
    }
    if (ok)
        for (const Change& change : changed)
        {
            if (!change.recovery) continue;
            const auto name = connection.Quoted(change.account->name);
            if (!Query(connection, "DELETE FROM account_recovery_credentials WHERE account_name = " + name, &transaction_error))
            { ok = false; break; }
            for (const auto& credential : change.account->recovery_credentials)
                if (!Query(connection, "INSERT INTO account_recovery_credentials (account_name, device_id, verifier, expires_at) VALUES (" +
                    name + "," + connection.Quoted(credential.device_id) + "," + connection.Quoted(credential.verifier) + "," +
                    std::to_string(credential.expires_at) + ")", &transaction_error)) { ok = false; break; }
            if (!ok) break;
        }
    if (ok)
        for (const auto& change : changed) if (change.identity)
            if (!Query(connection, "DELETE FROM account_external_identities WHERE account_name = " + connection.Quoted(change.account->name), &transaction_error)) { ok = false; break; }
    if (ok)
        for (const auto& change : changed) if (change.identity)
        {
            const auto& account = *change.account; const auto name = connection.Quoted(account.name);
            if (!Query(connection, "INSERT INTO account_identity_state (account_name, account_id, state_json) VALUES (" + name + "," +
                connection.Quoted(account.account_id) + "," + connection.Quoted(AccountIdentityJson(account)) +
                ") ON DUPLICATE KEY UPDATE account_id = VALUES(account_id), state_json = VALUES(state_json)", &transaction_error)) { ok = false; break; }
            for (const auto& identity : account.linked_identities)
                if (!Query(connection, "INSERT INTO account_external_identities (provider, application, subject, account_name) VALUES (" +
                    connection.Quoted(identity.provider) + "," + connection.Quoted(identity.application) + "," + connection.Quoted(identity.subject) + "," + name + ")", &transaction_error)) { ok = false; break; }
            if (!ok) break;
        }
    if (ok)
        for (const Change& change : changed)
            if (change.economy && !Query(connection,
                "INSERT INTO account_economy_state (account_name, state_json) VALUES (" + connection.Quoted(change.account->name) + "," +
                connection.Quoted(EconomyStateJson(change.account->economy)) + ") ON DUPLICATE KEY UPDATE state_json = VALUES(state_json)",
                &transaction_error)) { ok = false; break; }
    if (ok)
        ok = Query(connection,
            "INSERT INTO store_meta (meta_key, meta_value) VALUES ('unique_ledger_version', '" +
            std::string(unique_ledger_initialized ? "1" : "0") + "') ON DUPLICATE KEY UPDATE meta_value = VALUES(meta_value)",
            &transaction_error);
    if (ok)
        ok = Query(connection,
                   "INSERT INTO store_meta (meta_key, meta_value) VALUES ('save_generation', '" +
                       std::to_string(generation) +
                       "') ON DUPLICATE KEY UPDATE meta_value = VALUES(meta_value)",
                   &transaction_error);
    const bool commit_attempted = ok;
    if (ok) ok = Query(connection, "COMMIT", &transaction_error);

    if (!ok)
    {
        std::string rollback_error;
        const bool rolled_back = Query(connection, "ROLLBACK", &rollback_error);
        // COMMIT 回应丢失不代表未提交；下次完整重写，并考虑删除本次尝试新增的账号。
        if (commit_attempted || !rolled_back)
        {
            std::lock_guard<std::mutex> lock(g_publish_state_mutex);
            for (const auto& account : accounts)
                if (account && !IsSandboxAccount(account->name))
                    g_published_accounts.try_emplace(account->name, *account);
            g_publish_state_seeded = false;
        }
        if (error) *error = transaction_error;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_publish_state_mutex);
        for (const Change& change : changed) g_published_accounts[change.account->name] = *change.account;
        for (const std::string& name : removed) g_published_accounts.erase(name);
        g_publish_state_seeded = true;
    }
    return true;
}

bool PublishSnapshot(const std::vector<SPlayerAccount>& accounts, std::uint64_t generation, std::string* error,
                     bool unique_ledger_initialized)
{
    SAccountSnapshot snapshot;
    snapshot.reserve(accounts.size());
    for (const auto& account : accounts) snapshot.push_back(std::make_shared<const SPlayerAccount>(account));
    return PublishSnapshot(snapshot, generation, error, unique_ledger_initialized);
}

void ResetPublishState()
{
    std::lock_guard<std::mutex> lock(g_publish_state_mutex);
    g_published_accounts.clear();
    g_publish_state_seeded = false;
}
}

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
}

bool Available() { return false; }
bool Initialize(std::string* error) { return Unavailable(error); }
bool LoadAll(std::vector<SPlayerAccount>&, std::uint64_t&, std::string* error, bool*) { return Unavailable(error); }
bool PublishSnapshot(const std::vector<SPlayerAccount>&, std::uint64_t, std::string* error, bool)
{
    return Unavailable(error);
}
bool PublishSnapshot(const SAccountSnapshot&, std::uint64_t, std::string* error, bool) { return Unavailable(error); }
void ResetPublishState() {}
}

#endif // FLORRBT_WITH_MYSQL
