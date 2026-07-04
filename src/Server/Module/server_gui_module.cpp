#include "server_gui_module.h"
#include "../server.h"
#include "../../Shared/game_config.h"
#include <SFML/Window/Clipboard.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace
{
constexpr unsigned int window_width = 800;
constexpr unsigned int window_height = 450;
constexpr float padding = 12.f;
constexpr float input_height = 44.f;
constexpr float scrollbar_width = 10.f;
constexpr unsigned int char_size = 16;
constexpr size_t max_lines = 256;
constexpr size_t prompt_prefix_size = 2;

float LineHeight() { return static_cast<float>(char_size) + 5.f; }

sf::String FromUtf8(std::string_view str)
{
    return sf::String::fromUtf8(str.begin(), str.end());
}

std::string ToUtf8(const sf::String& str)
{
    const sf::U8String utf8 = str.toUtf8();
    return std::string(utf8.begin(), utf8.end());
}

float CharacterX(const sf::Text& text, size_t index)
{
    const std::vector<sf::Text::ShapedGlyph>& glyphs = text.getShapedGlyphs();
    const float origin_x = text.getPosition().x;
    float end_x = origin_x;

    for (const sf::Text::ShapedGlyph& glyph : glyphs)
    {
        const float glyph_x = origin_x + glyph.position.x;
        if (glyph.cluster >= index) return glyph_x;
        end_x = glyph_x + glyph.glyph.advance;
    }

    return end_x;
}
}

IServerGuiModule::log_line::log_line(ELogPriority line_priority, sf::String line_text, const sf::Font& font) :
    priority(line_priority),
    text(std::move(line_text)),
    render_text(font, text, char_size)
{
}

bool IServerGuiModule::Init()
{
    m_window.create(sf::VideoMode({window_width, window_height}), "FlorrBt Server GUI Console");
    m_window.setFramerateLimit(60);

    if (!m_font.openFromFile("C:/Windows/Fonts/msyh.ttc") && !m_font.openFromFile("C:/Windows/Fonts/simhei.ttf") &&
        !m_font.openFromFile("C:/Windows/Fonts/simsun.ttc") && !m_font.openFromFile("C:/Windows/Fonts/consola.ttf") &&
        !m_font.openFromFile("C:/Windows/Fonts/arial.ttf"))
    {
        LOG_FATAL("graphical", "Failed to open a system font.");
        return false;
    }

    m_log_sink_id = CLogger::AddSink([this](const std::string& sender, ELogPriority priority, const std::string& msg) {
        PushLine(sender, priority, msg);
    });

    PushLine("graphical", ELogPriority::Info, "Server GUI console started.");
    PushLine("graphical", ELogPriority::Info, "Press Tab to complete command names.");
    return true;
}

void IServerGuiModule::Tick(float)
{
    while (const std::optional event = m_window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            ShutDown();
            return;
        }
        if (const auto* key = event->getIf<sf::Event::KeyPressed>())
        {
            if (key->control && key->code == sf::Keyboard::Key::C)
            {
                CopySelectedLines();
            } else if (key->code == sf::Keyboard::Key::Escape)
            {
                ShutDown();
                return;
            } else if (key->code == sf::Keyboard::Key::Enter) {
                ResetCompletion();
                ExecuteInput();
            } else if (key->code == sf::Keyboard::Key::Backspace) {
                ResetCompletion();
                if (m_cursor_index > 0)
                {
                    m_input.erase(m_cursor_index - 1);
                    --m_cursor_index;
                }
            } else if (key->code == sf::Keyboard::Key::Delete) {
                ResetCompletion();
                if (m_cursor_index < m_input.getSize()) m_input.erase(m_cursor_index);
            } else if (key->code == sf::Keyboard::Key::Left) {
                ResetCompletion();
                if (m_cursor_index > 0) --m_cursor_index;
            } else if (key->code == sf::Keyboard::Key::Right) {
                ResetCompletion();
                if (m_cursor_index < m_input.getSize()) ++m_cursor_index;
            } else if (key->code == sf::Keyboard::Key::Home) {
                ResetCompletion();
                m_cursor_index = 0;
            } else if (key->code == sf::Keyboard::Key::End) {
                ResetCompletion();
                m_cursor_index = m_input.getSize();
            } else if (key->code == sf::Keyboard::Key::Tab) {
                CompleteCommand();
            }
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            const float height = static_cast<float>(m_window.getSize().y);
            const float input_top = height - input_height - padding;
            const sf::Vector2f mouse_position(static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y));
            if (mouse->button == sf::Mouse::Button::Left && static_cast<float>(mouse->position.y) >= input_top &&
                static_cast<float>(mouse->position.y) <= input_top + input_height)
            {
                ResetCompletion();
                m_has_output_selection = false;
                m_is_selecting_output = false;
                MoveCursorToMouseX(static_cast<float>(mouse->position.x));
            } else if (mouse->button == sf::Mouse::Button::Left && HasScrollBar() && ScrollBarThumbRect().contains(mouse_position)) {
                m_is_dragging_scrollbar = true;
                m_is_selecting_output = false;
                m_scrollbar_drag_offset = mouse_position.y - ScrollBarThumbRect().position.y;
            } else if (mouse->button == sf::Mouse::Button::Left) {
                BeginOutputSelection(mouse->position);
            }
        }
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            const sf::Vector2f mouse_position(static_cast<float>(wheel->position.x), static_cast<float>(wheel->position.y));
            if (OutputRect().contains(mouse_position)) ScrollOutput(static_cast<int>(-wheel->delta * 3.f));
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseMoved>())
        {
            if (m_is_dragging_scrollbar) UpdateScrollBarDrag(static_cast<float>(mouse->position.y));
            if (m_is_selecting_output) UpdateOutputSelection(mouse->position);
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseButtonReleased>())
        {
            if (mouse->button == sf::Mouse::Button::Left) m_is_dragging_scrollbar = false;
            if (mouse->button == sf::Mouse::Button::Left && m_is_selecting_output)
            {
                UpdateOutputSelection(mouse->position);
                m_is_selecting_output = false;
            }
        }
        if (const auto* text = event->getIf<sf::Event::TextEntered>())
        {
            char32_t unicode = text->unicode;
            if (unicode >= 32 && unicode != 127)
            {
                ResetCompletion();
                m_input.insert(m_cursor_index, sf::String(unicode));
                ++m_cursor_index;
            }
        }
    }

    Render();
}

void IServerGuiModule::ShutDown()
{
    if (m_log_sink_id != 0)
    {
        CLogger::RemoveSink(m_log_sink_id);
        m_log_sink_id = 0;
    }
    if (m_window.isOpen()) m_window.close();
    if (auto* server = CServer::GetInstance()) server->RequestStop();
}

void IServerGuiModule::PushLine(std::string sender, ELogPriority priority, std::string text)
{
    const bool was_at_bottom = m_first_visible_line >= MaxFirstVisibleLine();
    m_lines.emplace_back(priority, FromUtf8(CLogger::FormatLine(sender, priority, text)), m_font);
    m_lines.back().render_text.setFillColor(PriorityColor(priority));
    if (m_lines.size() > max_lines)
    {
        m_lines.erase(m_lines.begin(), m_lines.begin() + static_cast<std::ptrdiff_t>(m_lines.size() - max_lines));
        m_has_output_selection = false;
        m_is_selecting_output = false;
    }
    if (was_at_bottom)
        ScrollToBottom();
    else
        ClampScroll();
}

void IServerGuiModule::ExecuteInput()
{
    if (m_input.isEmpty()) return;

    const std::string input = ToUtf8(m_input);
    PushLine("graphical", ELogPriority::Debug, "> " + input);
    m_console.ExecuteLine(input);
    m_input.clear();
    m_cursor_index = 0;
}

void IServerGuiModule::CompleteCommand()
{
    const std::string input = ToUtf8(m_is_completing ? m_completion_original_input : m_input);
    const size_t cursor_index = m_is_completing ? m_completion_original_input.getSize() : m_cursor_index;
    const size_t token_begin = input.rfind(' ', cursor_index == 0 ? 0 : cursor_index - 1);
    const size_t replace_begin = (token_begin == std::string::npos) ? 0 : token_begin + 1;
    const size_t replace_end = input.find(' ', replace_begin);
    const std::string prefix = input.substr(replace_begin, cursor_index - replace_begin);
    if (prefix.empty() && replace_begin == 0) return;

    if (!m_is_completing)
    {
        std::vector<std::string> source;
        const std::string command = input.substr(0, input.find(' '));
        if ((command == "set" || command == "get") && replace_begin > 0)
            source = game_config::ConfigNames();
        else if (replace_begin == 0)
            source = m_console.CommandNames();
        else
            return;

        m_completion_matches.clear();
        for (const std::string& candidate : source)
        {
            if (candidate.rfind(prefix, 0) == 0) m_completion_matches.push_back(candidate);
        }
        std::sort(m_completion_matches.begin(), m_completion_matches.end());
        if (m_completion_matches.empty()) return;

        m_completion_original_input = m_input;
        m_completion_index = 0;
        m_is_completing = true;
    } else {
        m_completion_index = (m_completion_index + 1) % m_completion_matches.size();
    }

    const std::string& match = m_completion_matches[m_completion_index];
    const std::string suffix = (replace_end == std::string::npos) ? "" : input.substr(replace_end);
    m_input = FromUtf8(input.substr(0, replace_begin) + match + suffix);
    m_cursor_index = replace_begin + match.size();
}

void IServerGuiModule::ResetCompletion()
{
    m_is_completing = false;
    m_completion_original_input.clear();
    m_completion_matches.clear();
    m_completion_index = 0;
}

void IServerGuiModule::Render()
{
    if (!m_window.isOpen()) return;

    sf::Vector2u size = m_window.getSize();
    float width = static_cast<float>(size.x);
    float height = static_cast<float>(size.y);
    const sf::FloatRect output_rect = OutputRect();

    sf::RectangleShape output_box(output_rect.size);
    output_box.setPosition(output_rect.position);
    output_box.setFillColor(sf::Color(250, 252, 255));
    output_box.setOutlineColor(sf::Color(205, 214, 225));
    output_box.setOutlineThickness(1.f);

    sf::RectangleShape input_box({width - padding * 2.f, input_height});
    input_box.setPosition({padding, height - input_height - padding});
    input_box.setFillColor(sf::Color(255, 255, 255));
    input_box.setOutlineColor(sf::Color(160, 176, 196));
    input_box.setOutlineThickness(1.f);

    m_window.clear(sf::Color(238, 242, 247));
    m_window.draw(output_box);
    m_window.draw(input_box);

    float line_height = LineHeight();
    const size_t visible_count = VisibleLineCount();
    ClampScroll();
    size_t first_line = m_first_visible_line;
    size_t last_line = std::min(m_lines.size(), first_line + visible_count);
    float y = padding * 1.5f;
    for (size_t i = first_line; i < last_line; ++i)
    {
        if (IsLineSelected(i))
        {
            sf::RectangleShape selection({output_rect.size.x - padding * 2.f - scrollbar_width, line_height});
            selection.setPosition({padding * 1.25f, y - 1.f});
            selection.setFillColor(sf::Color(202, 225, 255));
            m_window.draw(selection);
        }

        m_lines[i].render_text.setPosition({padding * 1.5f, y});
        m_window.draw(m_lines[i].render_text);
        y += line_height;
    }

    if (HasScrollBar())
    {
        const sf::FloatRect track_rect = ScrollBarTrackRect();
        const sf::FloatRect thumb_rect = ScrollBarThumbRect();
        sf::RectangleShape track(track_rect.size);
        track.setPosition(track_rect.position);
        track.setFillColor(sf::Color(224, 230, 238));
        m_window.draw(track);

        sf::RectangleShape thumb(thumb_rect.size);
        thumb.setPosition(thumb_rect.position);
        thumb.setFillColor(m_is_dragging_scrollbar ? sf::Color(103, 132, 172) : sf::Color(135, 158, 190));
        m_window.draw(thumb);
    }

    sf::Text prompt = CreatePromptText();
    m_window.draw(prompt);

    sf::RectangleShape cursor({1.5f, static_cast<float>(char_size) + 4.f});
    cursor.setFillColor(sf::Color(35, 45, 60));
    cursor.setPosition({CharacterX(prompt, m_cursor_index + prompt_prefix_size), height - input_height - padding + 10.f});
    m_window.draw(cursor);

    m_window.display();
}

void IServerGuiModule::MoveCursorToMouseX(float x)
{
    sf::Text prompt = CreatePromptText();
    const size_t input_size = m_input.getSize();

    for (size_t i = 0; i < input_size; ++i)
    {
        const float current_x = CharacterX(prompt, i + prompt_prefix_size);
        const float next_x = CharacterX(prompt, i + prompt_prefix_size + 1);
        if (x < (current_x + next_x) * 0.5f)
        {
            m_cursor_index = i;
            return;
        }
    }

    m_cursor_index = input_size;
}

void IServerGuiModule::BeginOutputSelection(sf::Vector2i position)
{
    const std::optional<size_t> line_index = LineIndexFromMouseY(static_cast<float>(position.y));
    if (!line_index)
    {
        m_has_output_selection = false;
        m_is_selecting_output = false;
        return;
    }

    m_selection_anchor = *line_index;
    m_selection_cursor = *line_index;
    m_has_output_selection = true;
    m_is_selecting_output = true;
}

void IServerGuiModule::UpdateOutputSelection(sf::Vector2i position)
{
    const std::optional<size_t> line_index = LineIndexFromMouseY(static_cast<float>(position.y));
    if (!line_index) return;
    m_selection_cursor = *line_index;
}

void IServerGuiModule::CopySelectedLines() const
{
    if (!m_has_output_selection || m_lines.empty()) return;

    const size_t first = std::min(m_selection_anchor, m_selection_cursor);
    const size_t last = std::max(m_selection_anchor, m_selection_cursor);
    sf::String result;

    for (size_t i = first; i <= last && i < m_lines.size(); ++i)
    {
        if (!result.isEmpty()) result += FromUtf8("\n");
        result += m_lines[i].text;
    }

    if (!result.isEmpty()) sf::Clipboard::setString(result);
}

void IServerGuiModule::ScrollOutput(int line_delta)
{
    if (line_delta == 0) return;

    const int max_first = static_cast<int>(MaxFirstVisibleLine());
    const int current = static_cast<int>(m_first_visible_line);
    m_first_visible_line = static_cast<size_t>(std::clamp(current + line_delta, 0, max_first));
}

void IServerGuiModule::ScrollToBottom()
{
    m_first_visible_line = MaxFirstVisibleLine();
}

void IServerGuiModule::ClampScroll()
{
    m_first_visible_line = std::min(m_first_visible_line, MaxFirstVisibleLine());
}

void IServerGuiModule::UpdateScrollBarDrag(float y)
{
    const sf::FloatRect track = ScrollBarTrackRect();
    const sf::FloatRect thumb = ScrollBarThumbRect();
    const float movable_height = track.size.y - thumb.size.y;
    if (movable_height <= 0.f)
    {
        m_first_visible_line = 0;
        return;
    }

    const float top = std::clamp(y - m_scrollbar_drag_offset, track.position.y, track.position.y + movable_height);
    const float ratio = (top - track.position.y) / movable_height;
    m_first_visible_line = static_cast<size_t>(std::round(ratio * static_cast<float>(MaxFirstVisibleLine())));
    ClampScroll();
}

sf::Color IServerGuiModule::PriorityColor(ELogPriority priority) const
{
    switch (priority)
    {
    case ELogPriority::Debug:
        return sf::Color(70, 105, 155);
    case ELogPriority::Info:
        return sf::Color(74, 84, 98);
    case ELogPriority::Warning:
        return sf::Color(170, 112, 20);
    case ELogPriority::Error:
        return sf::Color(190, 72, 38);
    case ELogPriority::Fatal:
        return sf::Color(220, 0, 0);
    }
    return sf::Color(40, 45, 52);
}

std::optional<size_t> IServerGuiModule::LineIndexFromMouseY(float y) const
{
    const sf::FloatRect output_rect = OutputRect();
    if (y < output_rect.position.y || y > output_rect.position.y + output_rect.size.y || m_lines.empty()) return std::nullopt;

    const float line_height = LineHeight();
    const float first_y = padding * 1.5f;
    if (y < first_y) return m_first_visible_line;

    const size_t offset = static_cast<size_t>((y - first_y) / line_height);
    const size_t index = m_first_visible_line + offset;
    if (index >= m_lines.size()) return m_lines.size() - 1;
    return index;
}

bool IServerGuiModule::IsLineSelected(size_t index) const
{
    if (!m_has_output_selection) return false;

    const size_t first = std::min(m_selection_anchor, m_selection_cursor);
    const size_t last = std::max(m_selection_anchor, m_selection_cursor);
    return index >= first && index <= last;
}

bool IServerGuiModule::HasScrollBar() const
{
    return m_lines.size() > VisibleLineCount();
}

size_t IServerGuiModule::VisibleLineCount() const
{
    const float visible_height = OutputRect().size.y - padding;
    return std::max<size_t>(1, static_cast<size_t>(std::max(0.f, visible_height / LineHeight())));
}

size_t IServerGuiModule::MaxFirstVisibleLine() const
{
    const size_t visible_count = VisibleLineCount();
    if (m_lines.size() <= visible_count) return 0;
    return m_lines.size() - visible_count;
}

sf::FloatRect IServerGuiModule::OutputRect() const
{
    const sf::Vector2u size = m_window.getSize();
    const float width = static_cast<float>(size.x);
    const float height = static_cast<float>(size.y);
    return {{padding, padding}, {width - padding * 2.f, height - input_height - padding * 3.f}};
}

sf::FloatRect IServerGuiModule::ScrollBarTrackRect() const
{
    const sf::FloatRect output_rect = OutputRect();
    return {{output_rect.position.x + output_rect.size.x - scrollbar_width - 4.f, output_rect.position.y + 4.f},
            {scrollbar_width, output_rect.size.y - 8.f}};
}

sf::FloatRect IServerGuiModule::ScrollBarThumbRect() const
{
    const sf::FloatRect track = ScrollBarTrackRect();
    const size_t visible_count = VisibleLineCount();
    const float ratio = static_cast<float>(visible_count) / static_cast<float>(std::max<size_t>(visible_count, m_lines.size()));
    const float thumb_height = std::max(28.f, track.size.y * ratio);
    const float movable_height = track.size.y - thumb_height;
    const float scroll_ratio = MaxFirstVisibleLine() == 0 ? 0.f : static_cast<float>(m_first_visible_line) / static_cast<float>(MaxFirstVisibleLine());
    return {{track.position.x, track.position.y + movable_height * scroll_ratio}, {track.size.x, thumb_height}};
}

sf::Text IServerGuiModule::CreatePromptText() const
{
    sf::String prompt_text = FromUtf8("> ");
    prompt_text += m_input;

    const float height = static_cast<float>(m_window.getSize().y);
    sf::Text prompt(m_font, prompt_text, char_size);
    prompt.setFillColor(sf::Color(34, 40, 48));
    prompt.setPosition({padding * 1.5f, height - input_height - padding + 12.f});
    return prompt;
}
