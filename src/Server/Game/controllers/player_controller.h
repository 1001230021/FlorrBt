#pragma once
#include "../../../Shared/shared.h"
#include "../controller.h"
#include <array>
#include <cstddef>
#include <cstdint>

class CFlower;

class CPlayerController : public IController
{
  public:
    static constexpr std::size_t INPUT_QUEUE_CAPACITY = 32;
    static constexpr std::uint32_t MIN_INPUT_DELAY_TICKS = 1;
    static constexpr std::uint32_t MAX_INPUT_DELAY_TICKS = 8;

    void OnTick(CMobBase* mob, float dt) override;
    std::string_view SnapshotKey() const override { return "controller.player"; }
    void CaptureSnapshot(CSnapshotWriter& writer) const override;
    bool RestoreSnapshot(const CSnapshotReader& reader, std::uint32_t version, std::string& error) override;

    void PushOperate(const ClientOperate& op);
    bool PushOperate(const ClientOperate& op, std::uint32_t delay_ticks, std::uint32_t sequence);
    void ResetOperate();

  private:
    struct SPendingOperate
    {
        std::uint64_t target_tick = 0;
        ClientOperate movement;
        ClientOperate chores;
        bool occupied = false;
        bool has_movement = false;
        bool has_chores = false;
    };

    bool ScheduleOperate(const ClientOperate& op, std::uint32_t delay_ticks);
    static bool IsNewerSequence(std::uint32_t sequence, std::uint32_t previous);
    static void MergeOperate(SPendingOperate& pending, const ClientOperate& op);
    void ClearPendingOperates();
    void ExecuteOperate(const ClientOperate& op, CMobBase* mob);
    void TryManualAttack(CMobBase* mob);

    sf::Vector2f m_move_dir = { 0.f, 0.f };
    sf::Vector2f m_aim_dir = { 1.f, 0.f };
    bool m_has_aim_dir = false;
    std::array<SPendingOperate, INPUT_QUEUE_CAPACITY> m_pending_operates{};
    std::uint64_t m_local_tick = 0;
    std::uint32_t m_last_sequence = 0;
    bool m_has_last_sequence = false;
};
