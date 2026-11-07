#pragma once
#include "Game/extensions/extension_services.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

class CPlayer;
class CServer;

// 单个有界工作线程，关闭时等待退出；只接收文本/设置值，不持有玩家或世界引用。
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
}
