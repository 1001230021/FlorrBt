#include "report_review.h"
#include "remote_transport.h"
#include "../../Engine/json_value.h"
#include <charconv>
#include <cmath>
#include <string_view>

namespace report_review
{
namespace
{
std::string Escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value)
    {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32)
        {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15];
        }
        else out += ch;
    }
    return out;
}
std::optional<float> Duration(double value)
{
    if (!std::isfinite(value) || value < 0 || value > 86400.0 * 30) return {};
    return static_cast<float>(value);
}
}
std::string BuildRequest(const std::string& transcript, const std::string& model)
{
    const std::string prompt = "You moderate a small multiplayer game chat. Return only JSON like {\"mute_seconds\":0}. "
        "Choose 0 for harmless speech. For harassment, hate, threats, explicit spam, or severe abuse, "
        "choose a mute duration in seconds. Reported chat from the last minute:\n" + transcript;
    return "{\"model\":\"" + Escape(model) + "\",\"messages\":[{\"role\":\"user\",\"content\":\"" +
        Escape(prompt) + "\"}],\"temperature\":0}";
}
std::optional<float> ParseResponse(const std::string& response)
{
    const auto root = CJsonValue::Parse(response);
    const auto* choices = root ? root->Find("choices") : nullptr;
    if (!choices || !choices->IsArray() || choices->AsArray().empty()) return {};
    const auto* message = choices->AsArray().front().Find("message");
    const auto* content = message ? message->Find("content") : nullptr;
    if (!content || !content->IsString()) return {};
    const auto parsed = CJsonValue::Parse(content->AsString());
    if (parsed && parsed->IsObject())
    {
        const auto* seconds = parsed->Find("mute_seconds");
        return seconds && seconds->IsNumber() ? Duration(seconds->AsNumber()) : std::nullopt;
    }
    // 兼容纯数字回复，但不从自然语言中截取数字，避免把 -60 误判为正惩罚。
    auto text = std::string_view(content->AsString());
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == text.npos) return {};
    text = text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
    double value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return {};
    return Duration(value);
}
SResult Review(const std::string& transcript, const std::string& url, const std::string& key,
               const std::string& model, std::stop_token cancellation)
{
    if (cancellation.stop_requested()) return {{}, "cancelled"};
    if (key.empty()) return {{}, "AI review is not configured"};
    if (transcript.empty()) return {0.f, {}};
    const auto response = remote_transport::PostJson({url, key, BuildRequest(transcript, model)}, cancellation);
    if (!response) return {{}, remote_transport::Describe(response)};
    auto duration = ParseResponse(response.body);
    return {duration, duration ? "" : "AI response has no valid mute duration"};
}
}
