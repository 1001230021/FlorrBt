#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class CEmailVerificationService
{
  public:
    enum class EPurpose : std::uint8_t
    {
        Registration,
        Binding,
    };

    struct SDeliveryResult
    {
        std::uint32_t player_id = 0;
        std::uint64_t request_id = 0;
        bool success = false;
        std::string error;
    };

    CEmailVerificationService() = default;
    ~CEmailVerificationService();

    CEmailVerificationService(const CEmailVerificationService&) = delete;
    CEmailVerificationService& operator=(const CEmailVerificationService&) = delete;

    bool Start(std::string& error);
    void ShutDown();

    bool RequestCode(std::uint32_t player_id, const std::string& account_name, const std::string& email,
                     const std::string& client_ip, EPurpose purpose, std::uint64_t& request_id,
                     std::string& error);
    bool VerifyCode(const std::string& account_name, const std::string& email, EPurpose purpose,
                    const std::string& code, std::string& error);
    std::vector<SDeliveryResult> TakeDeliveryResults();
    void ForgetChallenge(const std::string& account_name, const std::string& email, EPurpose purpose);

    static std::string NormalizeEmail(const std::string& email);
    static bool IsValidEmail(const std::string& email);

  private:
    using clock = std::chrono::steady_clock;

    struct SSmtpSettings
    {
        std::string curl_path;
        std::string username;
        std::string password;
        std::string from_address;
        std::string from_name;
        std::string host;
        std::string security;
        int ssl_port = 0;
        int starttls_port = 0;
        int connect_timeout_seconds = 0;
        int send_timeout_seconds = 0;
    };

    struct SMailJob
    {
        std::uint32_t player_id = 0;
        std::uint64_t request_id = 0;
        std::string recipient;
        std::string subject;
        std::string plain_body;
        std::string html_body;
        SSmtpSettings settings;
    };

    struct SChallenge
    {
        std::uint64_t request_id = 0;
        std::string account_name;
        std::string email;
        std::string code;
        EPurpose purpose = EPurpose::Registration;
        clock::time_point requested_at{};
        clock::time_point expires_at{};
        int attempts = 0;
        bool ready = false;
    };

    struct SChallengeKey
    {
        std::string account_name;
        std::string email;
        EPurpose purpose = EPurpose::Registration;

        bool operator==(const SChallengeKey&) const = default;
    };

    struct SChallengeKeyHash
    {
        size_t operator()(const SChallengeKey& key) const noexcept;
    };

    static SChallengeKey ChallengeKey(const std::string& account_name, const std::string& email,
                                      EPurpose purpose);

    void WorkerLoop();
    static SDeliveryResult SendMail(const SMailJob& job);
    static std::string GenerateCode(int digits);
    static bool CodesEqual(const std::string& lhs, const std::string& rhs);

    std::thread m_worker;
    std::mutex m_worker_mutex;
    std::condition_variable m_worker_cv;
    std::deque<SMailJob> m_jobs;
    std::vector<SDeliveryResult> m_delivery_results;
    bool m_stopping = false;

    std::unordered_map<SChallengeKey, SChallenge, SChallengeKeyHash> m_challenges;
    std::unordered_map<std::string, clock::time_point> m_email_request_times;
    std::unordered_map<std::string, clock::time_point> m_account_request_times;
    std::unordered_map<std::string, clock::time_point> m_ip_request_times;
    std::uint64_t m_next_request_id = 1;
};
