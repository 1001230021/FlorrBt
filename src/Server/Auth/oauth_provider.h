#pragma once
#include "../../Shared/account_identity.h"
#include "../Transport/remote_transport.h"
#include <functional>
#include <stop_token>
#include <string>

struct SOAuthProviderConfig
{
    std::string provider, client_id, client_secret, redirect_uri;
    bool Enabled() const;
};
struct SOAuthIdentityResult { bool success = false; SLinkedIdentity identity; std::string error; };
class COAuthProvider
{
  public:
    using TTransport = std::function<remote_transport::SResult(const remote_transport::SHttpRequest&, std::stop_token)>;
    explicit COAuthProvider(SOAuthProviderConfig config, TTransport transport = remote_transport::Request);
    const SOAuthProviderConfig& Config() const { return m_config; }
    std::string AuthorizationUrl(std::string_view state) const;
    SOAuthIdentityResult Exchange(std::string_view code, std::stop_token stop = {}) const;
  private:
    SOAuthProviderConfig m_config;
    TTransport m_transport;
};
