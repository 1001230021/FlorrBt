#include "test_framework.h"
#include "Server/Transport/remote_transport.h"
#include "Server/Transport/report_review.h"
#include "Server/Auth/email_verification_service.h"
#include "Shared/game_config.h"
#include "Engine/json_value.h"
#include <SFML/Network.hpp>
#include <chrono>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using remote_transport::EError;

namespace
{
// Ephemeral loopback peer only: no game listener, credentials or remote
// service. Every socket operation is nonblocking and teardown is joined.
class CPeer
{
    sf::TcpListener listener;
    std::jthread worker;
    std::promise<std::string> received;
    std::future<std::string> request = received.get_future();
    unsigned short port = 0;
public:
    enum class EMode { Http, Stall, RejectStartTls };
    explicit CPeer(std::string response = {}, EMode mode = EMode::Http)
    {
        CHECK(listener.listen(0, sf::IpAddress::LocalHost) == sf::Socket::Status::Done);
        port = listener.getLocalPort(); listener.setBlocking(false);
        worker = std::jthread([this, response = std::move(response), mode](std::stop_token stop) {
            sf::TcpSocket socket;
            auto deadline = std::chrono::steady_clock::now() + 8s;
            const auto live = [&] { return !stop.stop_requested() && std::chrono::steady_clock::now() < deadline; };
            while (live() && listener.accept(socket) != sf::Socket::Status::Done) std::this_thread::sleep_for(1ms);
            socket.setBlocking(false);
            auto send = [&](std::string_view bytes) {
                size_t offset = 0;
                while (live() && offset < bytes.size())
                {
                    size_t sent = 0;
                    const auto status = socket.send(bytes.data() + offset, bytes.size() - offset, sent);
                    offset += sent;
                    if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) return;
                    if (offset < bytes.size()) std::this_thread::sleep_for(1ms);
                }
            };
            auto read = [&](bool http) {
                std::string text;
                while (live())
                {
                    char bytes[4096]; size_t size = 0;
                    const auto status = socket.receive(bytes, sizeof(bytes), size);
                    text.append(bytes, size);
                    if (!http && text.ends_with("\r\n")) break;
                    const auto header_end = text.find("\r\n\r\n");
                    if (http && header_end != std::string::npos)
                    {
                        const auto length_at = text.find("Content-Length: ");
                        const auto length = length_at == std::string::npos ? 0 : std::stoul(text.substr(length_at + 16));
                        if (text.size() >= header_end + 4 + length) break;
                    }
                    if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error) break;
                    if (!size) std::this_thread::sleep_for(1ms);
                }
                return text;
            };
            try
            {
                if (mode == EMode::RejectStartTls)
                {
                    send("220 localhost test SMTP\r\n");
                    auto text = read(false);
                    send("250-localhost\r\n250 STARTTLS\r\n");
                    text += read(false);
                    send("454 TLS unavailable\r\n");
                    text += read(false);
                    received.set_value(std::move(text));
                }
                else
                {
                    received.set_value(read(true));
                    if (mode == EMode::Http) send(response);
                    else while (live()) std::this_thread::sleep_for(1ms);
                }
            }
            catch (...) { try { received.set_value({}); } catch (...) {} }
        });
    }
    ~CPeer() { worker.request_stop(); worker.join(); }
    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port) + "/review"; }
    int Port() const { return port; }
    std::string Request()
    {
        if (request.wait_for(5s) != std::future_status::ready) { CHECK(false); return {}; }
        return request.get();
    }
};
std::string Reply(std::string body, int status = 200)
{
    return "HTTP/1.1 " + std::to_string(status) + " Test\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}
}

TEST(TransportRejectsNonHttpUrlsCredentialsAndHeaderInjection)
{
    for (const auto* url : {"file:///tmp/account", "ftp://localhost/x", "https://user:password@localhost/x", "http://localhost/\r\nX:1"})
        CHECK(remote_transport::PostJson({url, "fixture", "{}"}).error == EError::InvalidRequest);
    CHECK(remote_transport::PostJson({"https://localhost/", "fixture\r\nX-Injected:1", "{}"}).error == EError::InvalidRequest);
    std::stop_source stop; stop.request_stop();
    CHECK(remote_transport::PostJson({"https://localhost/", "fixture", "{}"}, stop.get_token()).error == EError::Cancelled);
}

TEST(TransportPostsLiteralJsonAndOnlyAcceptsSuccessfulHttpStatus)
{
    const std::string body = "{\"text\":\"literal & $(command) %PATH% \\\"quote\\\"\"}";
    CPeer peer(Reply("{\"ok\":true}"));
    const auto result = remote_transport::PostJson({peer.Url(), "fixture-token", body});
    CHECK(result && result.status == 200 && result.body == "{\"ok\":true}");
    const auto request = peer.Request();
    CHECK(request.ends_with(body));
    CHECK(request.find("Authorization: Bearer fixture-token\r\n") != std::string::npos);
    CHECK(request.find("Content-Type: application/json\r\n") != std::string::npos);
    for (int status : {301, 401, 429, 500})
    {
        CPeer rejected(Reply("{\"choices\":[{\"message\":{\"content\":\"60\"}}]}", status));
        const auto failed = remote_transport::PostJson({rejected.Url(), "fixture", "{}"});
        CHECK(failed.error == EError::HttpStatus && failed.status == status);
        CHECK(remote_transport::Describe(failed).find("fixture") == std::string::npos);
    }
}

TEST(TransportBoundsResponsesAndCancelsAnInflightRequest)
{
    CPeer peer(Reply(std::string(1024, 'x')));
    remote_transport::SJsonPost request{peer.Url(), "fixture", "{}"};
    request.max_response_bytes = 64;
    CHECK(remote_transport::PostJson(request).error == EError::ResponseTooLarge);
    CPeer slow({}, CPeer::EMode::Stall);
    request.url = slow.Url(); request.timeout = 100ms;
    CHECK(remote_transport::PostJson(request).error == EError::Timeout);
    CPeer cancelled({}, CPeer::EMode::Stall);
    request.url = cancelled.Url(); request.timeout = 8s;
    std::stop_source stop;
    auto response = std::async(std::launch::async, [&] { return remote_transport::PostJson(request, stop.get_token()); });
    CHECK(!cancelled.Request().empty()); stop.request_stop();
    CHECK(response.wait_for(2s) == std::future_status::ready);
    CHECK(response.get().error == EError::Cancelled);
}

TEST(SmtpRequiresTlsBeforeSendingCredentialsOrMessage)
{
    CPeer peer({}, CPeer::EMode::RejectStartTls);
    remote_transport::SSmtpMail mail;
    mail.host = "127.0.0.1"; mail.port = peer.Port(); mail.starttls = true;
    mail.username = "fixture-user"; mail.password = "fixture-password";
    mail.from = "sender@example.test"; mail.recipient = "receiver@example.test";
    mail.message = "Subject: fixture\r\n\r\nprivate-code";
    const auto response = remote_transport::SendMail(mail);
    CHECK(!response);
    const auto request = peer.Request();
    CHECK(request.find("STARTTLS") != std::string::npos);
    CHECK(request.find("AUTH") == std::string::npos && request.find("private-code") == std::string::npos);
    mail.from += "\r\nBcc: other@example.test";
    CHECK(remote_transport::SendMail(mail).error == EError::InvalidRequest);
}

TEST(ReportResponseRejectsProseNegativeDurationsAndMalformedSchema)
{
    // Explicit fixtures keep schema expectations independent of the builder.
    CHECK(report_review::ParseResponse(R"({"choices":[{"message":{"content":"{\"mute_seconds\":60}"}}]})") == 60.f);
    CHECK(report_review::ParseResponse(R"({"choices":[{"message":{"content":" 0 "}}]})") == 0.f);
    for (const auto* response : {
        R"({"choices":[{"message":{"content":"-60"}}]})",
        R"({"choices":[{"message":{"content":"mute 60"}}]})",
        R"({"choices":[{"message":{"content":"60 seconds"}}]})",
        R"({"choices":[{"message":{"content":"{\"mute_seconds\":\"60\"}"}}]})",
        R"({"choices":[{"message":{"content":"2592001"}}]})",
        R"({"choices":[{"message":{"content":60}}]})", R"({"choices":[]})", "not JSON"})
        CHECK(!report_review::ParseResponse(response));
    const auto built = CJsonValue::Parse(report_review::BuildRequest("quote \" newline\n & $(x)", "fixture"));
    CHECK(built && built->Find("messages") && built->Find("messages")->IsArray());
}

TEST(EmailWorkerUsesInMemoryMailAndJoinsCancellation)
{
    const auto old_policy = game_config::email_enabled;
    game_config::email_enabled = 1;
    const auto old_user = game_config::email_username, old_password = game_config::email_password;
    game_config::email_username = "sender@example.test"; game_config::email_password = "fixture-password";
    std::promise<remote_transport::SSmtpMail> started;
    CEmailVerificationService service([&](const auto& mail, std::stop_token stop) {
        started.set_value(mail);
        while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
        return remote_transport::SResult{EError::Cancelled};
    });
    std::string error; CHECK(service.Start(error));
    std::uint64_t request = 0;
    CHECK(service.RequestCode(1, "fixture", "receiver@example.test", "127.0.0.1",
        CEmailVerificationService::EPurpose::Registration, request, error));
    auto future = started.get_future();
    const bool ready = future.wait_for(3s) == std::future_status::ready; CHECK(ready);
    if (ready)
    {
        const auto mail = future.get();
        CHECK(mail.message.find("multipart/alternative") != std::string::npos);
        CHECK(mail.message.find("Content-Type: text/html") != std::string::npos);
        CHECK(mail.message.find("fixture-password") == std::string::npos);
    }
    service.ShutDown();
    CHECK(service.TakeDeliveryResults().empty());
    game_config::email_enabled = old_policy; game_config::email_username = old_user; game_config::email_password = old_password;
}
