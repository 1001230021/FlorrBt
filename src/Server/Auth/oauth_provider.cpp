#include "oauth_provider.h"
#include "../../Engine/json_value.h"
#include "../../Shared/utf8.h"
#include <algorithm>
#include <chrono>
#include <map>

namespace
{
std::string Encode(std::string_view text)
{
    constexpr char hex[] = "0123456789ABCDEF"; std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::optional<CJsonValue> Object(std::string text)
{
    if (text.starts_with("callback")) {
        const auto a = text.find('('), b = text.rfind(')');
        if (a == text.npos || b == text.npos || a >= b) return {};
        text = text.substr(a + 1, b - a - 1);
    }
    auto result = CJsonValue::Parse(text); if (!result || !result->IsObject()) return {}; return result;
}
std::string String(const CJsonValue& value, const char* field)
{ const auto* item = value.Find(field); return item && item->IsString() ? item->AsString() : ""; }
std::string QueryValue(std::string_view text, std::string_view key)
{
    while (!text.empty()) {
        const auto end = text.find('&'); const auto part = text.substr(0, end); const auto equals = part.find('=');
        if (equals != part.npos && part.substr(0, equals) == key) return std::string(part.substr(equals + 1));
        if (end == text.npos) break; text.remove_prefix(end + 1);
    }
    return {};
}
}
bool SOAuthProviderConfig::Enabled() const
{
    return (provider == "discord" || provider == "qq") && !client_id.empty() && client_id.size() <= 128 &&
        !client_secret.empty() && client_secret.size() <= 512 && redirect_uri.size() <= 2048 &&
        (redirect_uri.starts_with("https://") || redirect_uri.starts_with("http://127.0.0.1:") || redirect_uri.starts_with("http://localhost:")) &&
        redirect_uri.ends_with("/oauth/callback/" + provider) && redirect_uri.find_first_of("\r\n\t #?@") == std::string::npos;
}
COAuthProvider::COAuthProvider(SOAuthProviderConfig config, TTransport transport) : m_config(std::move(config)), m_transport(std::move(transport)) {}
std::string COAuthProvider::AuthorizationUrl(std::string_view state) const
{
    if (!m_config.Enabled()) return {};
    return (m_config.provider == "discord" ? "https://discord.com/oauth2/authorize" : "https://graph.qq.com/oauth2.0/authorize") +
        std::string("?response_type=code&client_id=") + Encode(m_config.client_id) + "&redirect_uri=" + Encode(m_config.redirect_uri) +
        "&scope=" + (m_config.provider == "discord" ? "identify" : "get_user_info") + "&state=" + Encode(state);
}
SOAuthIdentityResult COAuthProvider::Exchange(std::string_view code, std::stop_token stop) const
{
    SOAuthIdentityResult result; result.error = "authorization_failed";
    if (!m_config.Enabled() || code.empty() || code.size() > 4096) return result;
    remote_transport::SHttpRequest request;
    const auto form = "grant_type=authorization_code&client_id=" + Encode(m_config.client_id) + "&client_secret=" + Encode(m_config.client_secret) +
        "&code=" + Encode(code) + "&redirect_uri=" + Encode(m_config.redirect_uri);
    if (m_config.provider == "discord") {
        request.url = "https://discord.com/api/oauth2/token"; request.post = true; request.body = form; request.content_type = "application/x-www-form-urlencoded";
    } else request.url = "https://graph.qq.com/oauth2.0/token?" + form + "&fmt=json";
    auto response = m_transport(request, stop); if (!response) return result;
    auto token_response = Object(response.body);
    auto token = token_response ? String(*token_response, "access_token") : QueryValue(response.body, "access_token");
    if (token.empty() || token.size() > 4096) return result;
    request = {};
    if (m_config.provider == "discord") { request.url = "https://discord.com/api/users/@me"; request.bearer_token = token; }
    else request.url = "https://graph.qq.com/oauth2.0/me?access_token=" + Encode(token) + "&fmt=json";
    response = m_transport(request, stop); if (!response) return result;
    auto identity = Object(response.body); if (!identity) return result;
    result.identity.provider = m_config.provider; result.identity.application = m_config.client_id;
    if (m_config.provider == "discord") {
        result.identity.subject = String(*identity, "id"); result.identity.label = String(*identity, "global_name");
        if (result.identity.label.empty()) result.identity.label = String(*identity, "username");
        if (result.identity.subject.empty() || result.identity.subject.size() > 32 ||
            !std::all_of(result.identity.subject.begin(), result.identity.subject.end(), [](char c) { return c >= '0' && c <= '9'; })) return result;
    } else {
        if (String(*identity, "client_id") != m_config.client_id) return result;
        result.identity.subject = String(*identity, "openid");
        if (result.identity.subject.size() != 32 || !std::all_of(result.identity.subject.begin(), result.identity.subject.end(), [](unsigned char c) { return std::isxdigit(c); })) return result;
        std::transform(result.identity.subject.begin(), result.identity.subject.end(), result.identity.subject.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        result.identity.label = "QQ";
    }
    result.identity.label = std::string(TruncateUtf8(result.identity.label, 128));
    result.identity.linked_at = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    result.success = true; result.error.clear(); return result;
}
