#pragma once

#include <string>

class CPlayer;
class CServer;
class INetworkModule;

// 处理 /rcon 消息；返回 false 表示交回普通聊天/输入管线。
bool HandleRconCommand(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message);
void SendRconPrivateMessage(CServer& server, INetworkModule& network, CPlayer& player, const std::string& message);
