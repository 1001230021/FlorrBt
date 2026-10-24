#pragma once
#include "Game/extensions/extension_services.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

class CPlayer;
class CServer;

// One bounded, joined worker. It receives value-only transcripts/settings,
// never live players, worlds or mutable configuration references.
class CReportReviewService
{
  public:
    using TReview = std::function<float(const std::string&)>;
    explicit CReportReviewService(TReview review = {});
    ~CReportReviewService();
    bool Submit(CPlayer& reporter, CPlayer& target, const std::vector<SChatEntry>& chats, std::string& error);
    void Process(CServer& server);
    void CancelPending();
    void ShutDown();
    std::size_t PendingCount() const;
  private:
    struct CImplementation;
    std::unique_ptr<CImplementation> m_impl;
};

namespace report
{
bool HandleServerCommand(CPlayer& reporter, const std::string& command_line);
void ProcessAsyncResults();
} // namespace report
