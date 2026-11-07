#pragma once
#include <chrono>
#include <cstddef>
#include <stop_token>
#include <string>
#include <string_view>

namespace remote_transport
{
enum class EError { None, InvalidRequest, Cancelled, Timeout, ResponseTooLarge, Unavailable, Transport, HttpStatus };
struct SResult
{
    EError error = EError::None;
    long status = 0;
    int transport_code = 0;
    std::string body;
    explicit operator bool() const { return error == EError::None; }
};
// 日志不包含地址、凭据或远程响应正文，避免泄露密钥及玩家聊天内容。
std::string Describe(const SResult& result);

struct SJsonPost
{
    std::string url, bearer_token, body;
    std::chrono::milliseconds timeout{6000}, connect_timeout{3000};
    std::size_t max_response_bytes = 256 * 1024;
};
struct SSmtpMail
{
    std::string host, username, password, from, recipient, message;
    int port = 465;
    bool starttls = false;
    std::chrono::milliseconds timeout{15000}, connect_timeout{5000};
};
struct SHttpRequest
{
    std::string url, bearer_token, body, content_type;
    bool post = false;
    std::chrono::milliseconds timeout{8000}, connect_timeout{3000};
    std::size_t max_response_bytes = 64 * 1024;
};
SResult Request(const SHttpRequest&, std::stop_token cancellation = {});
SResult PostJson(const SJsonPost& request, std::stop_token cancellation = {});
SResult SendMail(const SSmtpMail& request, std::stop_token cancellation = {});
}
