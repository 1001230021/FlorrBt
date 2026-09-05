#pragma once
#include "account_store.h"
#include <cstdint>
#include <string>
#include <vector>

// MySQL persistence backend for the account store (classic MySQL C API:
// libmysql or MariaDB Connector/C). Connection parameters come from
// game_config::mysql_* (server.cfg `set mysql_host ...`; the password may be
// injected through the FLORRBT_MYSQL_PASSWORD environment variable).
//
// Threading model: every call opens its own short-lived connection, so no
// handle ever crosses threads. LoadAll runs on the server thread during
// startup; PublishSnapshot runs on the account save worker thread and diffs
// against the previously published state, so an idle server costs zero SQL
// and a typical save only rewrites the accounts that actually changed.
//
// When the binary is built without FLORRBT_WITH_MYSQL every function fails
// with a clear "built without MySQL support" error, so account_backend=mysql
// configs surface the problem at startup instead of silently losing data.
namespace mysql_account_backend
{
// True when MySQL support was compiled in.
bool Available();

// Connects, creates the schema when missing, and seeds the change-detection
// state. Call once at startup (or before a migration) from the loading thread.
bool Initialize(std::string* error);

// Loads every persisted account plus the stored save generation.
bool LoadAll(std::vector<SPlayerAccount>& accounts, std::uint64_t& generation, std::string* error);

// Durably publishes a full account snapshot. Ephemeral "@sandbox:" accounts
// are skipped (same rule as the JSON writer). Runs one transaction; on any
// failure nothing is partially applied and the change-detection state keeps
// marking the affected accounts dirty for the next attempt.
bool PublishSnapshot(const std::vector<SPlayerAccount>& accounts, std::uint64_t generation, std::string* error);

// Drops the cached change-detection state (used between migration runs and
// by the store's ShutDown so a later Load starts fresh).
void ResetPublishState();
} // namespace mysql_account_backend
