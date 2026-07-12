#include "server_gui_module.h"
#include "../server.h"
#include "../../Shared/game_config.h"
#include "../../Shared/fonts.h"
#include "../Game/entities/mob.h"
#include "../Game/entities/petals/petal.h"
#include <SFML/Window/Clipboard.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <sstream>
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

std::vector<std::string> RarityNames()
{
    return {
        "Common",
        "Unusual",
        "Rare",
        "Epic",
        "Legendary",
        "Mythic",
        "Ultra",
        "Super",
        "Eternal",
        "Unique",
        "Primordial",
    };
}

std::vector<std::string> PetalNames()
{
    std::vector<std::string> names;
    names.reserve(g_petal_registry.size());
    for (const auto& [type, proto] : g_petal_registry)
    {
        if (proto && !proto->m_name.empty())
            names.push_back(proto->m_name);
        else
            names.emplace_back(GetPetalTypeName(type));
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> MobNames()
{
    std::vector<std::string> names;
    names.reserve(g_mob_registry.size());
    for (const auto& [type, proto] : g_mob_registry)
    {
        if (proto && !proto->m_name.empty())
            names.push_back(proto->m_name);
        else
            names.emplace_back(GetMobTypeName(type));
    }
    std::sort(names.begin(), names.end());
    return names;
}

size_t TokenIndexAt(const std::string& input, size_t replace_begin)
{
    if (replace_begin == 0) return 0;

    size_t index = 0;
    bool in_token = false;
    for (size_t i = 0; i < replace_begin; ++i)
    {
        if (std::isspace(static_cast<unsigned char>(input[i])))
        {
            if (in_token)
            {
                ++index;
                in_token = false;
            }
        } else {
            in_token = true;
        }
    }
    return index;
}

bool IsUnsignedInteger(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char ch)
    {
        return std::isdigit(static_cast<unsigned char>(ch));
    });
}

std::vector<std::string> SplitWhitespace(const std::string& input)
{
    std::istringstream stream(input);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token)
    {
        tokens.push_back(token);
    }
    return tokens;
}

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
    const auto str_len = text.getString().getSize();
    const auto to_global_x = [&text](float x)
    {
        return text.getTransform().transformPoint({x, 0.f}).x;
    };

    if (str_len == 0) return to_global_x(0.f);

    index = std::min(index, str_len);

    const auto& glyphs = text.getShapedGlyphs();
    if (glyphs.empty()) return to_global_x(0.f);
    if (index == 0) return to_global_x(glyphs.front().position.x);

    const sf::Text::ShapedGlyph* previous = nullptr;
    for (const auto& glyph : glyphs)
    {
        const auto cluster = static_cast<size_t>(glyph.cluster);
        if (cluster >= index)
        {
            if (cluster == index) return to_global_x(glyph.position.x);
            break;
        }

        previous = &glyph;
    }

    if (previous) return to_global_x(previous->position.x + previous->glyph.advance);

    const auto& last = glyphs.back();
    return to_global_x(last.position.x + last.glyph.advance);
}

sf::String WrapText(const sf::String& str, const sf::Font& font, unsigned int char_size, float max_width)
{
    if (str.isEmpty() || max_width <= 0.f) return str;

    sf::Text mock_text(font, "", char_size);
    sf::String result;
    sf::String current_line;

    for (size_t i = 0; i < str.getSize(); ++i)
    {
        char32_t ch = str[i];

        if (ch == '\n')
        {
            result += current_line + '\n';
            current_line.clear();
            continue;
        }

        current_line += ch;
        mock_text.setString(current_line);

        if (mock_text.getLocalBounds().size.x > max_width)
        {
            if (current_line.getSize() > 1)
            {
                current_line.erase(current_line.getSize() - 1);
                result += current_line + '\n';
                current_line = ch;
            }
            else
            {
                result += current_line + '\n';
                current_line.clear();
            }
        }
    }

    result += current_line;
    return result;
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
    if (!game_config::gui_console_enabled) return true;
    return OpenGui();
}

bool IServerGuiModule::OpenGui()
{
    if (m_window.isOpen()) return true;

    m_window.create(sf::VideoMode({window_width, window_height}), "FlorrBt Server GUI Console");
    m_window.setFramerateLimit(60);

    if (!LoadFont(m_font))
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
    if (!game_config::gui_console_enabled)
    {
        CloseGui();
        return;
    }

    if (!m_window.isOpen() && !OpenGui())
    {
        game_config::gui_console_enabled = false;
        return;
    }

    while (const std::optional event = m_window.pollEvent())
    {
        if (event->is<sf::Event::Closed>())
        {
            game_config::gui_console_enabled = false;
            CloseGui();
            return;
        }

        if (const auto* resized = event->getIf<sf::Event::Resized>())
        {
            sf::FloatRect visibleArea({0.f, 0.f}, {static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
            m_window.setView(sf::View(visibleArea));
        }

        if (const auto* key = event->getIf<sf::Event::KeyPressed>())
        {
            if (key->control && key->code == sf::Keyboard::Key::A)
            {
                ResetCompletion();
                SelectAllInput();
            } else if (key->control && key->code == sf::Keyboard::Key::X) {
                ResetCompletion();
                CutSelectedInput();
            } else if (key->control && key->code == sf::Keyboard::Key::C) {
                if (HasInputSelection())
                    CopySelectedInput();
                else
                    CopySelectedLines();
            } else if (key->control && key->code == sf::Keyboard::Key::V) {
                ResetCompletion();
                PasteInput();
            } else if (key->control) {
                continue;
            } else if (key->code == sf::Keyboard::Key::Escape)
            {
                game_config::gui_console_enabled = false;
                CloseGui();
                return;
            } else if (key->code == sf::Keyboard::Key::Enter) {
                ResetCompletion();
                ExecuteInput();
            } else if (key->code == sf::Keyboard::Key::Backspace) {
                ResetCompletion();
                if (HasInputSelection())
                {
                    DeleteInputSelection();
                } else if (m_cursor_index > 0)
                {
                    m_input.erase(m_cursor_index - 1);
                    --m_cursor_index;
                }
            } else if (key->code == sf::Keyboard::Key::Delete) {
                ResetCompletion();
                if (HasInputSelection())
                    DeleteInputSelection();
                else if (m_cursor_index < m_input.getSize())
                    m_input.erase(m_cursor_index);
            } else if (key->code == sf::Keyboard::Key::Left) {
                ResetCompletion();
                if (m_cursor_index > 0) --m_cursor_index;
                ClearInputSelection();
            } else if (key->code == sf::Keyboard::Key::Right) {
                ResetCompletion();
                if (m_cursor_index < m_input.getSize()) ++m_cursor_index;
                ClearInputSelection();
            } else if (key->code == sf::Keyboard::Key::Home) {
                ResetCompletion();
                m_cursor_index = 0;
                ClearInputSelection();
            } else if (key->code == sf::Keyboard::Key::End) {
                ResetCompletion();
                m_cursor_index = m_input.getSize();
                ClearInputSelection();
            } else if (key->code == sf::Keyboard::Key::Tab) {
                CompleteCommand();
            }
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>())
        {
            const float height = static_cast<float>(m_window.getSize().y);
            const float input_top = height - input_height - padding;
            const sf::Vector2f mouse_position(static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y));
            if (mouse->button == sf::Mouse::Button::Left && mouse_position.y >= input_top &&
                mouse_position.y <= input_top + input_height)
            {
                ResetCompletion();
                m_has_output_selection = false;
                m_is_selecting_output = false;
                BeginInputSelection(mouse_position.x);
            } else if (mouse->button == sf::Mouse::Button::Left && HasScrollBar() && ScrollBarThumbRect().contains(mouse_position)) {
                m_is_dragging_scrollbar = true;
                m_is_selecting_output = false;
                m_is_selecting_input = false;
                m_scrollbar_drag_offset = mouse_position.y - ScrollBarThumbRect().position.y;
            } else if (mouse->button == sf::Mouse::Button::Left) {
                ClearInputSelection();
                m_is_selecting_input = false;
                BeginOutputSelection(sf::Vector2i(mouse_position));
            }
        }
        if (const auto* wheel = event->getIf<sf::Event::MouseWheelScrolled>())
        {
            const sf::Vector2f mouse_position(static_cast<float>(wheel->position.x), static_cast<float>(wheel->position.y));
            if (OutputRect().contains(mouse_position)) ScrollOutput(static_cast<int>(-wheel->delta * 3.f));
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseMoved>())
        {
            const sf::Vector2f mouse_position(static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y));
            if (m_is_dragging_scrollbar) UpdateScrollBarDrag(mouse_position.y);
            if (m_is_selecting_input) UpdateInputSelection(mouse_position.x);
            if (m_is_selecting_output) UpdateOutputSelection(sf::Vector2i(mouse_position));
        }
        if (const auto* mouse = event->getIf<sf::Event::MouseButtonReleased>())
        {
            const sf::Vector2f mouse_position(static_cast<float>(mouse->position.x), static_cast<float>(mouse->position.y));
            if (mouse->button == sf::Mouse::Button::Left) m_is_dragging_scrollbar = false;
            if (mouse->button == sf::Mouse::Button::Left && m_is_selecting_input)
            {
                UpdateInputSelection(mouse_position.x);
                m_is_selecting_input = false;
            }
            if (mouse->button == sf::Mouse::Button::Left && m_is_selecting_output)
            {
                UpdateOutputSelection(sf::Vector2i(mouse_position));
                m_is_selecting_output = false;
            }
        }
        if (const auto* text = event->getIf<sf::Event::TextEntered>())
        {
            char32_t unicode = text->unicode;
            if (unicode >= 32 && unicode != 127)
            {
                ResetCompletion();
                InsertInputText(sf::String(unicode));
            }
        }
    }

    Render();
}

void IServerGuiModule::ShutDown()
{
    CloseGui();
}

void IServerGuiModule::CloseGui()
{
    if (m_log_sink_id != 0)
    {
        CLogger::RemoveSink(m_log_sink_id);
        m_log_sink_id = 0;
    }
    if (m_window.isOpen()) m_window.close();
}

void IServerGuiModule::PushLine(std::string sender, ELogPriority priority, std::string text)
{
    const bool was_at_bottom = m_first_visible_line >= MaxFirstVisibleLine();
    const float max_text_width = OutputRect().size.x - padding * 3.f - scrollbar_width;

    sf::String full_sf_text = FromUtf8(CLogger::FormatLine(sender, priority, text));
    sf::String wrapped_sf_text = WrapText(full_sf_text, m_font, char_size, max_text_width);

    std::string wrapped_str = ToUtf8(wrapped_sf_text);
    std::istringstream stream(wrapped_str);
    std::string single_line;

    while (std::getline(stream, single_line))
    {
        m_lines.emplace_back(priority, FromUtf8(single_line), m_font);
        m_lines.back().render_text.setFillColor(PriorityColor(priority));

        if (m_lines.size() > max_lines)
        {
            m_lines.erase(m_lines.begin());
            m_has_output_selection = false;
            m_is_selecting_output = false;
        }
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
        const size_t token_index = TokenIndexAt(input, replace_begin);
        if (token_index == 0)
            source = m_console.CommandNames();
        else if ((command == "set" || command == "get") && token_index == 1)
            source = game_config::ConfigNames();
        else if (command == "equip" && token_index == 2)
            source = RarityNames();
        else if (command == "equip" && token_index == 3)
        {
            std::vector<std::string> tokens = SplitWhitespace(input);
            source = tokens.size() > 2 && IsUnsignedInteger(tokens[2]) ? RarityNames() : PetalNames();
        }
        else if (command == "equip" && token_index == 4)
            source = PetalNames();
        else if (command == "spawn" && token_index == 1)
            source = MobNames();
        else if (command == "spawn" && token_index == 2)
            source = RarityNames();
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

    if (HasInputSelection())
    {
        const auto [selection_begin, selection_end] = InputSelectionRange();
        const float selection_x = CharacterX(prompt, selection_begin + prompt_prefix_size);
        const float selection_end_x = CharacterX(prompt, selection_end + prompt_prefix_size);
        sf::RectangleShape selection({std::max(1.f, selection_end_x - selection_x), static_cast<float>(char_size) + 4.f});
        selection.setPosition({selection_x, height - input_height - padding + 10.f});
        selection.setFillColor(sf::Color(190, 218, 255));
        m_window.draw(selection);
        m_window.draw(prompt);
    }

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

void IServerGuiModule::BeginInputSelection(float x)
{
    MoveCursorToMouseX(x);
    m_input_selection_anchor = m_cursor_index;
    m_input_selection_cursor = m_cursor_index;
    m_has_input_selection = false;
    m_is_selecting_input = true;
}

void IServerGuiModule::UpdateInputSelection(float x)
{
    MoveCursorToMouseX(x);
    m_input_selection_cursor = m_cursor_index;
    m_has_input_selection = m_input_selection_anchor != m_input_selection_cursor;
}

void IServerGuiModule::ClearInputSelection()
{
    m_has_input_selection = false;
    m_input_selection_anchor = m_cursor_index;
    m_input_selection_cursor = m_cursor_index;
}

bool IServerGuiModule::HasInputSelection() const
{
    return m_has_input_selection && m_input_selection_anchor != m_input_selection_cursor;
}

std::pair<size_t, size_t> IServerGuiModule::InputSelectionRange() const
{
    return {std::min(m_input_selection_anchor, m_input_selection_cursor),
            std::max(m_input_selection_anchor, m_input_selection_cursor)};
}

void IServerGuiModule::DeleteInputSelection()
{
    if (!HasInputSelection()) return;

    const auto [selection_begin, selection_end] = InputSelectionRange();
    m_input.erase(selection_begin, selection_end - selection_begin);
    m_cursor_index = selection_begin;
    ClearInputSelection();
}

void IServerGuiModule::CopySelectedInput() const
{
    if (!HasInputSelection()) return;

    const auto [selection_begin, selection_end] = InputSelectionRange();
    sf::Clipboard::setString(m_input.substring(selection_begin, selection_end - selection_begin));
}

void IServerGuiModule::CutSelectedInput()
{
    if (!HasInputSelection()) return;

    CopySelectedInput();
    DeleteInputSelection();
}

void IServerGuiModule::PasteInput()
{
    InsertInputText(sf::Clipboard::getString());
}

void IServerGuiModule::SelectAllInput()
{
    m_input_selection_anchor = 0;
    m_input_selection_cursor = m_input.getSize();
    m_cursor_index = m_input.getSize();
    m_has_input_selection = !m_input.isEmpty();
}

void IServerGuiModule::InsertInputText(const sf::String& text)
{
    if (text.isEmpty()) return;
    if (HasInputSelection()) DeleteInputSelection();

    m_input.insert(m_cursor_index, text);
    m_cursor_index += text.getSize();
    ClearInputSelection();
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
