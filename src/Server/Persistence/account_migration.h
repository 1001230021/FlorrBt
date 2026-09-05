#pragma once
#include <optional>

// Account-store migration tool, exposed as server command-line modes:
//   FlorrBt.Server.exe --migrate-accounts-to-mysql [json_path]
//       Reads the JSON account file (default: account_data_path from
//       data/server.cfg), writes every account into MySQL, then reloads them
//       from MySQL and deep-compares the round trip before reporting.
//   FlorrBt.Server.exe --migrate-accounts-to-json <json_path>
//       Reads every account from MySQL and writes them into a JSON account
//       file (rollback path).
//
// Both modes read mysql_* settings from data/server.cfg and honor the
// FLORRBT_MYSQL_PASSWORD environment variable. Returns std::nullopt when the
// arguments contain no migration mode (normal server startup continues), or
// the process exit code after running one.
std::optional<int> RunAccountMigrationFromArgs(int argc, char** argv);
