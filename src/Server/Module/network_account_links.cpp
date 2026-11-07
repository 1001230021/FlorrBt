#include "network_module.h"
#include "../server.h"
#include "../Auth/account_link_service.h"
#include "../Game/player.h"
#include "../../Engine/json_value.h"

bool INetworkModule::QueueAccountLinkPayload(CPlayer& player, const std::string& payload)
{
    ServerMessage message; message.type = ServerMessage::Type::AccountLinks; message.service_payload = payload;
    return QueueMessage(player, message);
}
bool INetworkModule::QueueAccountLinks(CPlayer& player)
{ return player.IsAuthenticated() && QueueAccountLinkPayload(player, m_server.AccountLinks().State(player)); }
void INetworkModule::HandleAccountLinkRequest(CPlayer& player, const std::string& payload)
{
    auto value = CJsonValue::Parse(payload);
    if (!value || !value->IsObject()) return;
    m_server.AccountLinks().Request(player, *value, [this](CPlayer& target, const std::string& data) { return QueueAccountLinkPayload(target, data); });
}
