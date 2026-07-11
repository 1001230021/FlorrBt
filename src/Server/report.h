#pragma once
#include "server.h"
#include <string>
#include <vector>

class CPlayer;

namespace report
{
bool HandleServerCommand(CPlayer& reporter, const std::string& command_line);
void ProcessAsyncResults();
float ReviewByKeywords(const std::vector<CServer::SChatEntry>& chats, uint32_t target_player_id);
float ReviewByDeepSeek(const std::vector<CServer::SChatEntry>& chats, uint32_t target_player_id);
}
