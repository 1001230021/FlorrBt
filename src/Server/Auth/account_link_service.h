#pragma once
#include "../Game/extension.h"
#include "oauth_provider.h"
#include <functional>
#include <memory>
#include <string>

class CPlayer;
class CJsonValue;
class IPlayerDirectory;
// 工作线程只持有认证值和授权参数，提交时重新验证会话与账户身份。
class CAccountLinkService
{
  public:
    using TDelivery = std::function<bool(CPlayer&, const std::string&)>;
    explicit CAccountLinkService(std::vector<SOAuthProviderConfig> providers = {}, COAuthProvider::TTransport transport = remote_transport::Request);
    ~CAccountLinkService();
    CAccountLinkService(const CAccountLinkService&) = delete;
    void Request(CPlayer&, const CJsonValue&, const TDelivery&);
    std::string State(const CPlayer&) const;
    void Tick(IPlayerDirectory&, const TDelivery&);
    void CancelSession(SPlayerSessionRef);
    void ShutDown();
  private:
    struct SImpl;
    std::unique_ptr<SImpl> m_impl;
};
