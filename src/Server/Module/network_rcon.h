#pragma once

#include <string>

class CPlayer;
class CServer;
class INetworkModule;

// Handles one chat message that starts with /rcon.  Returning false means the
// message belongs to the normal chat/input pipeline.
bool HandleRconCommand(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message);
void SendRconPrivateMessage(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message);
