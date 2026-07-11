#pragma once
#include "module.h"
#include "../../Engine/console.h"
#include "../../Engine/logger.h"
#include <SFML/Graphics.hpp>
#include <optional>
#include <string>
#include <vector>

class IServerGuiModule : public IModule
{
  public:
    explicit IServerGuiModule(CConsole& console) : m_console(console) {}

    bool Init() override;
    void Tick(float dt) override;
    void ShutDown() override;

  private:
    struct log_line
    {
        log_line(ELogPriority line_priority, sf::String line_text, const sf::Font& font);

        ELogPriority priority = ELogPriority::Info;
        sf::String text;
        sf::Text render_text;
    };

    void PushLine(std::string sender, ELogPriority priority, std::string text);
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
    size_t m_log_sink_id = 0;
    size_t m_first_visible_line = 0;
    float m_scrollbar_drag_offset = 0.f;
    bool m_is_selecting_input = false;
    bool m_is_selecting_output = false;
    bool m_has_input_selection = false;
    bool m_has_output_selection = false;
    bool m_is_dragging_scrollbar = false;
    bool m_is_completing = false;
};
