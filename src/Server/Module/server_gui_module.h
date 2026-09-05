#pragma once
#include "../../Engine/console.h"
#include "../../Engine/logger.h"
#include "module.h"
#include <SFML/Graphics.hpp>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// The GUI console runs on its own thread: a window pumped from the game tick
// thread freezes the whole simulation whenever Windows enters a modal loop
// (title-bar drag, resize) or a present blocks (RDP/occluded sessions), which
// showed up in game as multi-second world freezes. The game thread only
// executes the commands typed here and publishes completion data snapshots.
class IServerGuiModule : public IModule
{
  public:
    explicit IServerGuiModule(CConsole& console) : m_console(console) {}

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;

    // Completion data gathered on the game thread (live player/world/entity
    // lists), consumed by the GUI thread. Static registries (petals, mobs,
    // configs, commands) are read directly: they never change after Init.
    struct SCompletionSnapshot
    {
        std::vector<std::string> players;
        std::vector<std::string> world_ids;
        std::vector<std::string> world_ids_and_maps;
        std::vector<std::string> entity_ref_stubs; // "12:"
        std::vector<std::string> entity_refs;      // "12:345"
    };

  private:
    struct log_line
    {
        log_line(ELogPriority line_priority, sf::String line_text, const sf::Font& font);

        ELogPriority priority = ELogPriority::Info;
        sf::String text;
        sf::Text render_text;
    };

    struct pending_log_line
    {
        ELogPriority priority = ELogPriority::Info;
        std::string formatted;
    };

    // Game-thread side.
    void StartGuiThread();
    void StopGuiThread();
    void RefreshCompletionSnapshot();

    // GUI-thread side.
    void ThreadLoop();
    bool ProcessEvents();
    void DrainPendingLogLines();
    void PushLine(std::string sender, ELogPriority priority, std::string text); // any thread: enqueue only
    void AppendLine(ELogPriority priority, const std::string& formatted);       // GUI thread: wrap + layout
    void ExecuteInput();
    void CompleteCommand();
    void ResetCompletion();
    void Render();
    void MoveCursorToMouseX(float x);
    void BeginInputSelection(float x);
    void UpdateInputSelection(float x);
    void ClearInputSelection();
    bool HasInputSelection() const;
    std::pair<size_t, size_t> InputSelectionRange() const;
    void DeleteInputSelection();
    void CopySelectedInput() const;
    void CutSelectedInput();
    void PasteInput();
    void SelectAllInput();
    void InsertInputText(const sf::String& text);
    void BeginOutputSelection(sf::Vector2i position);
    void UpdateOutputSelection(sf::Vector2i position);
    void CopySelectedLines() const;
    void ScrollOutput(int line_delta);
    void ScrollToBottom();
    void ClampScroll();
    void UpdateScrollBarDrag(float y);
    bool OpenGui();
    void CloseGui();

    sf::Color PriorityColor(ELogPriority priority) const;
    sf::Text CreatePromptText() const;
    std::optional<size_t> LineIndexFromMouseY(float y) const;
    bool IsLineSelected(size_t index) const;
    bool HasScrollBar() const;
    size_t VisibleLineCount() const;
    size_t MaxFirstVisibleLine() const;
    sf::FloatRect OutputRect() const;
    sf::FloatRect ScrollBarTrackRect() const;
    sf::FloatRect ScrollBarThumbRect() const;

    // GUI-thread-owned state (window, text layout, selections).
    sf::RenderWindow m_window;
    sf::Font m_font;
    std::vector<log_line> m_lines;
    CConsole& m_console;
    sf::String m_input;
    sf::String m_completion_original_input;
    std::vector<std::string> m_completion_matches;
    size_t m_cursor_index = 0;
    size_t m_completion_index = 0;
    size_t m_input_selection_anchor = 0;
    size_t m_input_selection_cursor = 0;
    size_t m_selection_anchor = 0;
    size_t m_selection_cursor = 0;
    size_t m_first_visible_line = 0;
    float m_render_accumulator = 0.f;
    float m_scrollbar_drag_offset = 0.f;
    bool m_is_selecting_input = false;
    bool m_is_selecting_output = false;
    bool m_has_input_selection = false;
    bool m_has_output_selection = false;
    bool m_is_dragging_scrollbar = false;
    bool m_is_completing = false;

    // Cross-thread state.
    std::thread m_thread;
    std::atomic<bool> m_stop_requested{ false };
    std::atomic<bool> m_thread_exited{ false };
    size_t m_log_sink_id = 0; // managed on the game thread only

    std::mutex m_pending_lines_mutex;
    std::vector<pending_log_line> m_pending_lines;

    std::mutex m_pending_commands_mutex;
    std::vector<std::string> m_pending_commands;

    std::mutex m_snapshot_mutex;
    SCompletionSnapshot m_completion_snapshot;
    float m_snapshot_refresh_timer = 0.f;
};
