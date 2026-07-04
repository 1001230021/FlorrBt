#include "../Engine/commands_registry.h"
#include "../Engine/console.h"
#include "../Engine/logger.h"
#include "server.h"
#include "../Shared/game_config.h"

REGISTER_CONSOLE_COMMAND(quit, {
    if (auto* server = CServer::GetInstance())
        server->ShutDown();
    else
        LOG_FATAL("server", "Can't find instance of the server");
})

REGISTER_CONSOLE_COMMAND(echo, {
    if (args.empty()) return;
    LOG_INFO("console", args[0]);
})

REGISTER_CONSOLE_COMMAND(set, {
    if (args.size() < 2) return;

    const std::string& value = args[1];
    if (game_config::SetConfig(args[0], value))
        LOG_INFO("console", "Set " + args[0] + " to " + value);
    else
        LOG_WARN("console", "Unknown config: " + args[0]);
})

REGISTER_CONSOLE_COMMAND(get, {
    if (args.empty()) return;

    std::string result = game_config::GetConfig(args[0]);
    LOG_INFO("console", "Value of the " + args[0] + ": " + result);
})
