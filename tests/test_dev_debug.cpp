#include "test_framework.h"
#include "Shared/dev_debug_protocol.h"
#include "Shared/network_msg.h"
#include "Engine/logger.h"
#include <atomic>
#include <memory>
#include <thread>
#include <future>

TEST(DebugProtocolRejectsWrongBranchVersionAndOversizedFrames)
{
    const auto frame = dev_debug::PackFrame("{\"op\":\"ping\"}");
    std::size_t size = 0;
    for (std::size_t split = 0; split < frame.size(); ++split)
        CHECK(dev_debug::ReadFrameSize(frame.data(), split, dev_debug::request_limit, size) == dev_debug::FrameStatus::Incomplete);
    CHECK(dev_debug::ReadFrameSize(frame.data(), frame.size(), dev_debug::request_limit, size) == dev_debug::FrameStatus::Ready);
    CHECK(size == frame.size() - dev_debug::header_size);
    auto bad = frame; bad[0] = client_version_packet_type;
    CHECK(dev_debug::ReadFrameSize(bad.data(), bad.size(), dev_debug::request_limit, size) == dev_debug::FrameStatus::Invalid);
    bad = frame; bad[1]++;
    CHECK(dev_debug::ReadFrameSize(bad.data(), bad.size(), dev_debug::request_limit, size) == dev_debug::FrameStatus::Invalid);
    bad = frame; bad[2] = bad[3] = bad[4] = bad[5] = 0xff;
    CHECK(dev_debug::ReadFrameSize(bad.data(), dev_debug::header_size, dev_debug::request_limit, size) == dev_debug::FrameStatus::Invalid);
    bad = frame; bad[2] = bad[3] = bad[4] = bad[5] = 0;
    CHECK(dev_debug::ReadFrameSize(bad.data(), bad.size(), dev_debug::request_limit, size) == dev_debug::FrameStatus::Invalid);
    CHECK(dev_debug::PackFrame("").empty());
    CHECK(dev_debug::PackFrame(std::string(dev_debug::response_limit + 1, 'x')).empty());
}

TEST(DebugTicketIsStrictlyTypedAndRoundTrips)
{
    ServerMessage ticket;
    ticket.type = ServerMessage::Type::DebugTicket;
    ticket.debug_ticket = std::string(dev_debug::ticket_size, 'a');
    CHECK(dev_debug::IsTicket(ticket.debug_ticket));
    CHECK(!dev_debug::IsTicket(std::string(dev_debug::ticket_size, 'g')));
    CHECK(!dev_debug::IsTicket(ticket.debug_ticket + 'a'));
    CHECK(!dev_debug::IsTicket(""));
    std::vector<std::uint8_t> bytes(ServerMessage::GetPackedSize(ticket));
    CHECK(ServerMessage::pack(ticket, bytes.data()) == 65);
    const auto parsed = ServerMessage::parse(bytes.data(), bytes.size());
    CHECK(parsed.type == ServerMessage::Type::DebugTicket);
    CHECK(parsed.debug_ticket == ticket.debug_ticket);
    CHECK(ServerMessage::parse(bytes.data(), bytes.size() - 1).type == ServerMessage::Type::Unknown);
    bytes[4] = 'z';
    CHECK(ServerMessage::parse(bytes.data(), bytes.size()).type == ServerMessage::Type::Unknown);
}

TEST(DebugRequestRateIsBoundedAndRecovers)
{
    dev_debug::RateLimit rate;
    for (int i = 0; i < 32; ++i) CHECK(rate.Take(100));
    CHECK(!rate.Take(100));
    CHECK(!rate.Take(99));
    CHECK(rate.Take(100.1));
    for (int i = 0; i < 31; ++i) CHECK(rate.Take(200));
    CHECK(rate.Take(200));
    CHECK(!rate.Take(200));
}

TEST(LoggerSubscriptionCopiesKeepConcurrentCallbackStateAlive)
{
    auto count = std::make_shared<std::atomic<int>>(0);
    const auto sink = CLogger::AddSink([count](const std::string&, ELogPriority, const std::string&) { ++*count; });
    std::thread worker([] { for (int i = 0; i < 20; ++i) CLogger("debug-test").Info("offline log"); });
    for (int i = 0; i < 20; ++i)
    {
        const auto transient = CLogger::AddSink([state = std::make_shared<int>(i)](const std::string&, ELogPriority, const std::string&) {
            (void)*state;
        });
        CLogger::RemoveSink(transient);
    }
    worker.join(); CLogger::RemoveSink(sink);
    CHECK(count->load() == 20);
}

TEST(RemovedLoggerSinkCannotRunFromAnOlderDispatchCopy)
{
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    auto observed = entered.get_future();
    const auto blocker = CLogger::AddSink([&](const std::string&, ELogPriority, const std::string&) {
        entered.set_value(); released.wait();
    });
    std::atomic<int> stale_calls{0};
    const auto removed = CLogger::AddSink([&](const std::string&, ELogPriority, const std::string&) { ++stale_calls; });
    std::thread worker([] { CLogger("remove-test").Info("offline dispatch"); });
    const auto status = observed.wait_for(std::chrono::seconds(2));
    CHECK(status == std::future_status::ready);
    CLogger::RemoveSink(removed);
    release.set_value(); worker.join(); CLogger::RemoveSink(blocker);
    CHECK(stale_calls.load() == 0);
}
