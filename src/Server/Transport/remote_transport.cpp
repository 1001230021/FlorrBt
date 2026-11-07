#include "remote_transport.h"
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

namespace remote_transport
{
namespace
{
struct CCurlRuntime
{
    CURLcode result = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CCurlRuntime() { if (result == CURLE_OK) curl_global_cleanup(); }
};
bool Ready() { static CCurlRuntime runtime; return runtime.result == CURLE_OK; }
using TEasy = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using TMulti = std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)>;
using TUrl = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
using TText = std::unique_ptr<char, decltype(&curl_free)>;
using THeaders = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;

bool Clean(std::string_view text)
{
    return std::none_of(text.begin(), text.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
}
bool Address(std::string_view text)
{
    return !text.empty() && text.size() <= 254 && Clean(text) && text.find_first_of("<> ") == text.npos;
}
bool ValidTimeout(std::chrono::milliseconds value) { return value.count() > 0 && value.count() <= 300000; }
struct SBuffer
{
    SResult result;
    std::size_t limit;
    std::string_view upload;
    std::size_t uploaded = 0;
};
size_t Write(char* data, size_t size, size_t count, void* userdata) noexcept
{
    auto& buffer = *static_cast<SBuffer*>(userdata);
    if (size && count > (std::numeric_limits<size_t>::max)() / size) return 0;
    const size_t bytes = size * count;
    if (bytes > buffer.limit - buffer.result.body.size())
    { buffer.result.error = EError::ResponseTooLarge; return 0; }
    try { buffer.result.body.append(data, bytes); return bytes; }
    catch (...) { buffer.result.error = EError::Unavailable; return 0; }
}
size_t Read(char* data, size_t size, size_t count, void* userdata) noexcept
{
    auto& buffer = *static_cast<SBuffer*>(userdata);
    if (size && count > (std::numeric_limits<size_t>::max)() / size) return CURL_READFUNC_ABORT;
    const size_t bytes = std::min(size * count, buffer.upload.size() - buffer.uploaded);
    if (bytes) std::memcpy(data, buffer.upload.data() + buffer.uploaded, bytes);
    buffer.uploaded += bytes;
    return bytes;
}
template<class T> bool Set(CURL* curl, CURLoption option, T value) { return curl_easy_setopt(curl, option, value) == CURLE_OK; }
bool Configure(CURL* curl, SBuffer& buffer, std::chrono::milliseconds timeout, std::chrono::milliseconds connect)
{
    return Set(curl, CURLOPT_NOSIGNAL, 1L) &&
        Set(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count())) &&
        Set(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(std::min(timeout, connect).count())) &&
        Set(curl, CURLOPT_SSL_VERIFYPEER, 1L) && Set(curl, CURLOPT_SSL_VERIFYHOST, 2L) &&
        Set(curl, CURLOPT_FOLLOWLOCATION, 0L) &&
        Set(curl, CURLOPT_WRITEFUNCTION, &Write) && Set(curl, CURLOPT_WRITEDATA, &buffer);
}
SResult Perform(CURL* curl, SBuffer& buffer, std::stop_token cancellation)
{
    if (cancellation.stop_requested()) return {EError::Cancelled};
    TMulti multi(curl_multi_init(), curl_multi_cleanup);
    if (!multi || curl_multi_add_handle(multi.get(), curl) != CURLM_OK) return {EError::Unavailable};
    // 销毁任一拥有者前先从多请求句柄移除单请求句柄。
    struct CDetach { CURLM* multi; CURL* easy; ~CDetach() { curl_multi_remove_handle(multi, easy); } } detach{multi.get(), curl};
    int running = 0;
    CURLMcode status;
    do
    {
        if (cancellation.stop_requested()) return {EError::Cancelled};
        status = curl_multi_perform(multi.get(), &running);
        if (status != CURLM_OK) return {EError::Transport};
        if (running && curl_multi_poll(multi.get(), nullptr, 0, 50, nullptr) != CURLM_OK)
            return {EError::Transport};
    } while (running);
    CURLcode completed = CURLE_FAILED_INIT;
    int remaining = 0;
    while (const auto* message = curl_multi_info_read(multi.get(), &remaining))
        if (message->msg == CURLMSG_DONE && message->easy_handle == curl) completed = message->data.result;
    if (cancellation.stop_requested()) return {EError::Cancelled};
    auto& result = buffer.result;
    result.transport_code = static_cast<int>(completed);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (result.error == EError::None && completed != CURLE_OK)
        result.error = completed == CURLE_OPERATION_TIMEDOUT ? EError::Timeout : EError::Transport;
    return std::move(result);
}
bool AddHeader(THeaders& headers, const std::string& value)
{
    auto* updated = curl_slist_append(headers.get(), value.c_str());
    if (!updated) return false;
    headers.release(); headers.reset(updated); return true;
}
}

std::string Describe(const SResult& result)
{
    constexpr std::array names{"ok", "invalid request", "cancelled", "timeout", "response too large",
                               "transport unavailable", "transport failed", "HTTP status rejected"};
    return std::string(names[static_cast<size_t>(result.error)]) + " (status=" +
        std::to_string(result.status) + ", code=" + std::to_string(result.transport_code) + ")";
}

SResult PostJson(const SJsonPost& request, std::stop_token cancellation)
{
    if (cancellation.stop_requested()) return {EError::Cancelled};
    if (!ValidTimeout(request.timeout) || !ValidTimeout(request.connect_timeout) ||
        request.url.empty() || request.url.size() > 8192 || !Clean(request.url) ||
        request.bearer_token.size() > 8192 || !Clean(request.bearer_token) ||
        request.body.size() > 1024 * 1024 || !request.max_response_bytes || request.max_response_bytes > 4 * 1024 * 1024)
        return {EError::InvalidRequest};
    if (!Ready()) return {EError::Unavailable};
    TUrl url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, request.url.c_str(), 0) != CURLUE_OK) return {EError::InvalidRequest};
    char* raw = nullptr;
    if (curl_url_get(url.get(), CURLUPART_SCHEME, &raw, 0) != CURLUE_OK) return {EError::InvalidRequest};
    TText scheme(raw, curl_free);
    if (std::string_view(scheme.get()) != "https" && std::string_view(scheme.get()) != "http") return {EError::InvalidRequest};
    for (auto part : {CURLUPART_USER, CURLUPART_PASSWORD})
    {
        raw = nullptr;
        if (curl_url_get(url.get(), part, &raw, 0) == CURLUE_OK)
        { TText rejected(raw, curl_free); return {EError::InvalidRequest}; }
    }
    TEasy curl(curl_easy_init(), curl_easy_cleanup);
    THeaders headers(nullptr, curl_slist_free_all);
    if (!curl || !AddHeader(headers, "Content-Type: application/json") ||
        (!request.bearer_token.empty() && !AddHeader(headers, "Authorization: Bearer " + request.bearer_token)))
        return {EError::Unavailable};
    SBuffer buffer{{}, request.max_response_bytes, {}};
    if (!Configure(curl.get(), buffer, request.timeout, request.connect_timeout) ||
        !Set(curl.get(), CURLOPT_CURLU, url.get()) || !Set(curl.get(), CURLOPT_PROTOCOLS_STR, "http,https") ||
        !Set(curl.get(), CURLOPT_HTTPHEADER, headers.get()) ||
        !Set(curl.get(), CURLOPT_POSTFIELDS, request.body.data()) ||
        !Set(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size())))
        return {EError::Unavailable};
    auto result = Perform(curl.get(), buffer, cancellation);
    if (result && (result.status < 200 || result.status >= 300)) result.error = EError::HttpStatus;
    return result;
}

SResult SendMail(const SSmtpMail& request, std::stop_token cancellation)
{
    if (cancellation.stop_requested()) return {EError::Cancelled};
    if (!ValidTimeout(request.timeout) || !ValidTimeout(request.connect_timeout) ||
        !Address(request.from) || !Address(request.recipient) || request.host.empty() || request.host.size() > 253 ||
        !Clean(request.host) || request.host.find_first_of("/@?# ") != std::string::npos ||
        request.port <= 0 || request.port > 65535 || request.message.empty() || request.message.size() > 1024 * 1024 ||
        request.username.find('\0') != std::string::npos || request.password.find('\0') != std::string::npos)
        return {EError::InvalidRequest};
    if (!Ready()) return {EError::Unavailable};
    TUrl url(curl_url(), curl_url_cleanup);
    const auto port = std::to_string(request.port);
    if (!url || curl_url_set(url.get(), CURLUPART_SCHEME, request.starttls ? "smtp" : "smtps", 0) != CURLUE_OK ||
        curl_url_set(url.get(), CURLUPART_HOST, request.host.c_str(), 0) != CURLUE_OK ||
        curl_url_set(url.get(), CURLUPART_PORT, port.c_str(), 0) != CURLUE_OK) return {EError::InvalidRequest};
    TEasy curl(curl_easy_init(), curl_easy_cleanup);
    THeaders recipients(nullptr, curl_slist_free_all);
    if (!curl || !AddHeader(recipients, request.recipient)) return {EError::Unavailable};
    SBuffer buffer{{}, 65536, request.message};
    if (!Configure(curl.get(), buffer, request.timeout, request.connect_timeout) ||
        !Set(curl.get(), CURLOPT_CURLU, url.get()) || !Set(curl.get(), CURLOPT_PROTOCOLS_STR, "smtp,smtps") ||
        !Set(curl.get(), CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL)) ||
        !Set(curl.get(), CURLOPT_USERNAME, request.username.c_str()) || !Set(curl.get(), CURLOPT_PASSWORD, request.password.c_str()) ||
        !Set(curl.get(), CURLOPT_MAIL_FROM, request.from.c_str()) || !Set(curl.get(), CURLOPT_MAIL_RCPT, recipients.get()) ||
        !Set(curl.get(), CURLOPT_UPLOAD, 1L) || !Set(curl.get(), CURLOPT_READFUNCTION, &Read) ||
        !Set(curl.get(), CURLOPT_READDATA, &buffer) ||
        !Set(curl.get(), CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(request.message.size())))
        return {EError::Unavailable};
    return Perform(curl.get(), buffer, cancellation);
}

SResult Request(const SHttpRequest& request, std::stop_token cancellation)
{
    if (!request.url.starts_with("https://") || request.url.size() > 8192 || !Clean(request.url) || !Clean(request.bearer_token) ||
        request.bearer_token.size() > 8192 || request.body.size() > 8192 || !Clean(request.content_type) ||
        !ValidTimeout(request.timeout) || !ValidTimeout(request.connect_timeout) || !request.max_response_bytes || request.max_response_bytes > 262144)
        return {EError::InvalidRequest};
    if (!Ready()) return {EError::Unavailable};
    TEasy curl(curl_easy_init(), curl_easy_cleanup); SBuffer buffer{{}, request.max_response_bytes}; THeaders headers(nullptr, curl_slist_free_all);
    if (!curl || !Configure(curl.get(), buffer, request.timeout, request.connect_timeout) || !Set(curl.get(), CURLOPT_URL, request.url.c_str()) ||
        !Set(curl.get(), CURLOPT_PROTOCOLS_STR, "https")) return {EError::Unavailable};
    if (!request.bearer_token.empty() && !AddHeader(headers, "Authorization: Bearer " + request.bearer_token)) return {EError::Unavailable};
    if (request.post) {
        if (!AddHeader(headers, "Content-Type: " + request.content_type) || !Set(curl.get(), CURLOPT_POST, 1L) ||
            !Set(curl.get(), CURLOPT_POSTFIELDS, request.body.c_str()) || !Set(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(request.body.size()))) return {EError::Unavailable};
    }
    if (!Set(curl.get(), CURLOPT_HTTPHEADER, headers.get())) return {EError::Unavailable};
    auto result = Perform(curl.get(), buffer, cancellation);
    if (result && (result.status < 200 || result.status >= 300)) result.error = EError::HttpStatus;
    return result;
}
}
