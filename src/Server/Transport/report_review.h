#pragma once
#include <optional>
#include <stop_token>
#include <string>

namespace report_review
{
std::string BuildRequest(const std::string& transcript, const std::string& model);
std::optional<float> ParseResponse(const std::string& response);
struct SResult
{
    std::optional<float> mute_seconds;
    std::string diagnostic;
};
SResult Review(const std::string& transcript, const std::string& url, const std::string& key,
               const std::string& model, std::stop_token cancellation);
}
