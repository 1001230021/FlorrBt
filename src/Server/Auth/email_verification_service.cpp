#include "email_verification_service.h"
#include "../../Shared/game_config.h"
#include "../../Shared/tools.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace
{
std::string Trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string HeaderText(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(), [](char ch) { return ch == '\r' || ch == '\n'; }),
                value.end());
    return value;
}

std::string CurlConfigValue(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value)
    {
        if (ch == '\\' || ch == '"') escaped.push_back('\\');
        if (ch != '\r' && ch != '\n') escaped.push_back(ch);
    }
    return escaped;
}

std::string ShellQuotedExecutable(const std::string& path)
{
    std::string clean = path.empty() ? "curl.exe" : path;
    clean.erase(std::remove_if(clean.begin(), clean.end(), [](char ch) { return ch == '\r' || ch == '\n'; }),
                clean.end());
    std::string quoted = "\"";
    for (char ch : clean)
    {
        if (ch == '"') quoted += "\\\"";
        else quoted.push_back(ch);
    }
    quoted += "\"";
    return quoted;
}

} // namespace

CEmailVerificationService::~CEmailVerificationService() { ShutDown(); }

bool CEmailVerificationService::Start(std::string& error)
{
    error.clear();
    if (m_worker.joinable()) return true;
    try
    {
        m_stopping = false;
        m_worker = std::thread(&CEmailVerificationService::WorkerLoop, this);
        return true;
    } catch (const std::exception& exception)
    {
        error = "Failed to start email worker: " + std::string(exception.what());
        return false;
    }
}

void CEmailVerificationService::ShutDown()
{
    {
        std::lock_guard lock(m_worker_mutex);
        m_stopping = true;
        m_jobs.clear();
    }
    m_worker_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();

    std::lock_guard lock(m_worker_mutex);
    m_jobs.clear();
    m_delivery_results.clear();
    m_challenges.clear();
    m_email_request_times.clear();
    m_account_request_times.clear();
    m_ip_request_times.clear();
}

std::string CEmailVerificationService::NormalizeEmail(const std::string& email)
{
    std::string normalized = Trim(email);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

bool CEmailVerificationService::IsValidEmail(const std::string& email)
{
    const std::string normalized = NormalizeEmail(email);
    if (normalized.empty() || normalized.size() > 254 || normalized.find_first_of("\r\n \t") != std::string::npos)
        return false;
    const size_t at = normalized.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= normalized.size() || normalized.find('@', at + 1) != std::string::npos)
        return false;
    const size_t dot = normalized.find('.', at + 2);
    return dot != std::string::npos && dot + 1 < normalized.size();
}

size_t CEmailVerificationService::SChallengeKeyHash::operator()(const SChallengeKey& key) const noexcept
{
    size_t hash = std::hash<std::string>{}(key.account_name);
    hash ^= std::hash<std::string>{}(key.email) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
    hash ^= std::hash<unsigned int>{}(static_cast<unsigned int>(key.purpose)) + 0x9e3779b9U + (hash << 6U) +
            (hash >> 2U);
    return hash;
}

CEmailVerificationService::SChallengeKey
CEmailVerificationService::ChallengeKey(const std::string& account_name, const std::string& email,
                                         EPurpose purpose)
{
    return { account_name, NormalizeEmail(email), purpose };
}

bool CEmailVerificationService::RequestCode(std::uint32_t player_id, const std::string& account_name,
                                            const std::string& email, const std::string& client_ip,
                                            EPurpose purpose, std::uint64_t& request_id, std::string& error)
{
    error.clear();
    request_id = 0;
    if (!game_config::email_enabled)
    {
        error = "Email verification is disabled";
        return false;
    }

    const std::string normalized_email = NormalizeEmail(email);
    if (!IsValidEmail(normalized_email))
    {
        error = "Invalid email address";
        return false;
    }

    const std::string from_address = game_config::email_from_address.empty() ? game_config::email_username
                                                                             : game_config::email_from_address;
    if (game_config::email_username.empty() || game_config::email_password.empty() || from_address.empty() ||
        game_config::email_smtp_host.empty())
    {
        error = "Server email account is not configured";
        return false;
    }

    const auto now = clock::now();
    const auto cooldown = std::chrono::seconds(std::max(0, game_config::email_verification_resend_cooldown_seconds));
    const auto ip_cooldown =
        std::chrono::seconds(std::max(0, game_config::email_verification_ip_cooldown_seconds));
    auto prune_requests = [now](auto& requests, auto max_age) {
        for (auto entry = requests.begin(); entry != requests.end();)
        {
            if (now - entry->second >= max_age)
                entry = requests.erase(entry);
            else
                ++entry;
        }
    };
    prune_requests(m_email_request_times, cooldown);
    prune_requests(m_account_request_times, cooldown);
    prune_requests(m_ip_request_times, ip_cooldown);

    const auto pending_max_age = std::chrono::seconds(
        std::max(1, game_config::email_send_timeout_seconds) +
        std::max(1, game_config::email_verification_ttl_seconds));
    for (auto challenge = m_challenges.begin(); challenge != m_challenges.end();)
    {
        const bool expired = challenge->second.ready ? now >= challenge->second.expires_at
                                                     : now - challenge->second.requested_at >= pending_max_age;
        if (expired)
            challenge = m_challenges.erase(challenge);
        else
            ++challenge;
    }

    const std::string ip_key = client_ip.empty() ? "<unknown>" : client_ip;
    const SChallengeKey challenge_key = ChallengeKey(account_name, normalized_email, purpose);
    const auto competing_challenge =
        std::find_if(m_challenges.begin(), m_challenges.end(), [&challenge_key](const auto& entry) {
            return entry.first.email == challenge_key.email &&
                   entry.first.account_name != challenge_key.account_name;
        });
    if (competing_challenge != m_challenges.end())
    {
        error = "Email address is already being verified for another account";
        return false;
    }
    if (auto found = m_challenges.find(challenge_key); found != m_challenges.end() &&
                                                       now - found->second.requested_at < cooldown)
    {
        error = "Please wait before requesting another code";
        return false;
    }
    if (auto found = m_account_request_times.find(account_name); found != m_account_request_times.end() &&
                                                                  now - found->second < cooldown)
    {
        error = "Please wait before requesting another code";
        return false;
    }
    if (auto found = m_ip_request_times.find(ip_key); found != m_ip_request_times.end() &&
                                                     now - found->second < ip_cooldown)
    {
        error = "Please wait before requesting another code";
        return false;
    }
    if (auto found = m_email_request_times.find(normalized_email); found != m_email_request_times.end() &&
                                                               now - found->second < cooldown)
    {
        error = "Please wait before requesting another code";
        return false;
    }

    if (m_next_request_id == 0) m_next_request_id = 1;
    request_id = m_next_request_id++;
    const std::string code = GenerateCode(game_config::email_verification_code_digits);

    SChallenge challenge;
    challenge.request_id = request_id;
    challenge.account_name = account_name;
    challenge.email = normalized_email;
    challenge.code = code;
    challenge.purpose = purpose;
    challenge.requested_at = now;

    SMailJob job;
    job.player_id = player_id;
    job.request_id = request_id;
    job.recipient = normalized_email;
    job.subject = "FlorrBt verification code";
    const int expires_minutes =
        std::max(1, (std::max(1, game_config::email_verification_ttl_seconds) + 59) / 60);
    const std::string expires_text = std::to_string(expires_minutes);
    job.plain_body = "Your FlorrBt verification code is " + code +
                     ".\r\n\r\nIt expires in " + expires_text +
                     " minutes.\r\nIf you did not request this code, ignore this message.\r\n";
    job.html_body =
        "<html>\r\n"
        "<body style=\"font-family: sans-serif;\">\r\n"
        "<div style=\"max-width: 400px; border-radius: 12px; background-color: #f8f8f8; "
        "text-align: center; padding: 16px;\">\r\n"
        "<h2>FlorrBt verification code</h2>\r\n"
        "<div style=\"display: inline-block; background-color: #080808; padding: 12px; "
        "border-radius: 10px;\">\r\n"
        "<span style=\"color: #f0f0f0; font-family: monospace; font-size: 36px;\">" +
        code +
        "</span>\r\n"
        "</div>\r\n"
        "<p>It expires in <b>" +
        expires_text +
        " minutes</b>.</p>\r\n"
        "<p style=\"color: #404040;\">If you did not request this code, ignore this message.</p>\r\n"
        "</div>\r\n"
        "</body>\r\n"
        "</html>\r\n";
    job.settings.curl_path = game_config::email_curl_path;
    job.settings.username = game_config::email_username;
    job.settings.password = game_config::email_password;
    job.settings.from_address = from_address;
    job.settings.from_name = game_config::email_from_name;
    job.settings.host = game_config::email_smtp_host;
    job.settings.security = game_config::email_smtp_security;
    std::transform(job.settings.security.begin(), job.settings.security.end(), job.settings.security.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    job.settings.ssl_port = std::max(1, game_config::email_smtp_ssl_port);
    job.settings.starttls_port = std::max(1, game_config::email_smtp_starttls_port);
    job.settings.connect_timeout_seconds = std::max(1, game_config::email_connect_timeout_seconds);
    job.settings.send_timeout_seconds = std::max(1, game_config::email_send_timeout_seconds);

    {
        std::lock_guard lock(m_worker_mutex);
        if (m_stopping)
        {
            error = "Email service is unavailable";
            return false;
        }
        if (m_jobs.size() >= static_cast<size_t>(std::max(1, game_config::email_verification_queue_limit)))
        {
            request_id = 0;
            error = "Email service is busy";
            return false;
        }
        m_jobs.push_back(std::move(job));
    }
    m_challenges.insert_or_assign(challenge_key, std::move(challenge));
    m_email_request_times.insert_or_assign(normalized_email, now);
    m_account_request_times.insert_or_assign(account_name, now);
    m_ip_request_times.insert_or_assign(ip_key, now);
    m_worker_cv.notify_one();
    return true;
}

bool CEmailVerificationService::VerifyCode(const std::string& account_name, const std::string& email,
                                           EPurpose purpose, const std::string& code, std::string& error)
{
    error.clear();
    auto found = m_challenges.find(ChallengeKey(account_name, email, purpose));
    if (found == m_challenges.end())
    {
        error = "Request a verification code first";
        return false;
    }

    SChallenge& challenge = found->second;
    if (!challenge.ready)
    {
        error = "Verification email is still being sent";
        return false;
    }
    if (clock::now() >= challenge.expires_at)
    {
        m_challenges.erase(found);
        error = "Verification code expired";
        return false;
    }

    if (!CodesEqual(challenge.code, Trim(code)))
    {
        ++challenge.attempts;
        if (challenge.attempts >= std::max(1, game_config::email_verification_max_attempts))
        {
            m_challenges.erase(found);
            error = "Too many incorrect verification attempts";
        } else
            error = "Incorrect verification code";
        return false;
    }

    // The caller consumes the challenge with ForgetChallenge only after the
    // corresponding account mutation has been persisted successfully.
    return true;
}

std::vector<CEmailVerificationService::SDeliveryResult> CEmailVerificationService::TakeDeliveryResults()
{
    std::vector<SDeliveryResult> completed;
    {
        std::lock_guard lock(m_worker_mutex);
        completed.swap(m_delivery_results);
    }

    std::vector<SDeliveryResult> results;
    results.reserve(completed.size());
    const auto now = clock::now();
    for (SDeliveryResult& result : completed)
    {
        auto found = std::find_if(m_challenges.begin(), m_challenges.end(), [&result](const auto& entry) {
            return entry.second.request_id == result.request_id;
        });
        if (found == m_challenges.end()) continue;
        if (!result.success)
        {
            m_challenges.erase(found);
        } else
        {
            found->second.ready = true;
            found->second.expires_at =
                now + std::chrono::seconds(std::max(1, game_config::email_verification_ttl_seconds));
        }
        results.push_back(std::move(result));
    }
    return results;
}

void CEmailVerificationService::ForgetChallenge(const std::string& account_name, const std::string& email,
                                                EPurpose purpose)
{
    m_challenges.erase(ChallengeKey(account_name, email, purpose));
}

void CEmailVerificationService::WorkerLoop()
{
    for (;;)
    {
        SMailJob job;
        {
            std::unique_lock lock(m_worker_mutex);
            m_worker_cv.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
            if (m_stopping && m_jobs.empty()) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        SDeliveryResult result = SendMail(job);
        std::lock_guard lock(m_worker_mutex);
        m_delivery_results.push_back(std::move(result));
    }
}

CEmailVerificationService::SDeliveryResult CEmailVerificationService::SendMail(const SMailJob& job)
{
    SDeliveryResult result;
    result.player_id = job.player_id;
    result.request_id = job.request_id;

    std::error_code error_code;
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path(error_code);
    if (error_code)
    {
        result.error = "Unable to create verification email";
        return result;
    }
    const std::filesystem::path message_path =
        temp_dir / ("florrbt-mail-" + std::to_string(job.request_id) + ".eml");

    {
        std::ofstream message(message_path, std::ios::binary | std::ios::trunc);
        if (!message)
        {
            result.error = "Unable to create verification email";
            return result;
        }
        const std::string from_name = HeaderText(job.settings.from_name);
        message << "From: " << (from_name.empty() ? "FlorrBt" : from_name) << " <" << job.settings.from_address
                << ">\r\n";
        message << "To: <" << job.recipient << ">\r\n";
        message << "Subject: " << HeaderText(job.subject) << "\r\n";
        message << "MIME-Version: 1.0\r\n";
        const std::string boundary = "----FlorrBt-" + std::to_string(job.request_id);
        message << "Content-Type: multipart/alternative; boundary=\"" << boundary << "\"\r\n\r\n";
        message << "--" << boundary << "\r\n";
        message << "Content-Type: text/plain; charset=UTF-8\r\n";
        message << "Content-Transfer-Encoding: 8bit\r\n\r\n";
        message << job.plain_body;
        message << "--" << boundary << "\r\n";
        message << "Content-Type: text/html; charset=UTF-8\r\n";
        message << "Content-Transfer-Encoding: 8bit\r\n\r\n";
        message << job.html_body;
        message << "--" << boundary << "--\r\n";
        if (!message)
        {
            std::filesystem::remove(message_path, error_code);
            result.error = "Unable to create verification email";
            return result;
        }
    }

    const std::string command = ShellQuotedExecutable(job.settings.curl_path) +
                                " --silent --show-error --fail --config -";
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "w");
#else
    FILE* pipe = popen(command.c_str(), "w");
#endif
    if (!pipe)
    {
        std::filesystem::remove(message_path, error_code);
        result.error = "Unable to start mail transport";
        return result;
    }

    const bool starttls = job.settings.security == "starttls";
    const char* scheme = starttls ? "smtp" : "smtps";
    const int port = starttls ? job.settings.starttls_port : job.settings.ssl_port;
    const std::string mail_url =
        std::string(scheme) + "://" + job.settings.host + ":" + std::to_string(port);

    std::ostringstream config;
    config << "url = \"" << CurlConfigValue(mail_url) << "\"\n";
    config << "ssl-reqd\n";
    config << "user = \"" << CurlConfigValue(job.settings.username + ":" + job.settings.password) << "\"\n";
    config << "mail-from = \"" << CurlConfigValue(job.settings.from_address) << "\"\n";
    config << "mail-rcpt = \"" << CurlConfigValue(job.recipient) << "\"\n";
    config << "upload-file = \"" << CurlConfigValue(message_path.string()) << "\"\n";
    config << "connect-timeout = " << job.settings.connect_timeout_seconds << "\n";
    config << "max-time = " << job.settings.send_timeout_seconds << "\n";
    const std::string config_text = config.str();
    const size_t written = std::fwrite(config_text.data(), 1, config_text.size(), pipe);
#ifdef _WIN32
    const int exit_code = _pclose(pipe);
#else
    const int exit_code = pclose(pipe);
#endif
    std::filesystem::remove(message_path, error_code);

    result.success = written == config_text.size() && exit_code == 0;
    if (!result.success) result.error = "Verification email could not be delivered";
    return result;
}

std::string CEmailVerificationService::GenerateCode(int digits)
{
    const int count = std::clamp(digits, 4, 9);
    std::string code;
    code.reserve(static_cast<size_t>(count));
    std::array<uint8_t, 32> random_bytes{};
    while (code.size() < static_cast<size_t>(count))
    {
        if (!PlatformRandomBytes(random_bytes.data(), random_bytes.size()))
        {
            std::random_device fallback;
            std::uniform_int_distribution<int> digit(0, 9);
            while (code.size() < static_cast<size_t>(count))
                code.push_back(static_cast<char>('0' + digit(fallback)));
            break;
        }
        for (uint8_t value : random_bytes)
        {
            if (value >= 250) continue;
            code.push_back(static_cast<char>('0' + value % 10));
            if (code.size() == static_cast<size_t>(count)) break;
        }
    }
    return code;
}

bool CEmailVerificationService::CodesEqual(const std::string& lhs, const std::string& rhs)
{
    const size_t count = std::max(lhs.size(), rhs.size());
    unsigned char difference = static_cast<unsigned char>(lhs.size() ^ rhs.size());
    for (size_t index = 0; index < count; ++index)
    {
        const unsigned char left = index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0;
        const unsigned char right = index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0;
        difference |= static_cast<unsigned char>(left ^ right);
    }
    return difference == 0;
}
