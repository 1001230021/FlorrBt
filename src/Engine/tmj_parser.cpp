#include "tmj_parser.h"
#include <cctype>
#include <fstream>
#include <sstream>

namespace
{
const std::string empty_string;
const CJsonValue::array empty_array;
const CJsonValue::object empty_object;

class CJsonParser
{
  public:
    explicit CJsonParser(const std::string& text) : m_text(text) {}

    std::optional<CJsonValue> Parse(std::string* error)
    {
        SkipWhitespace();
        std::optional<CJsonValue> value = ParseValue(error);
        if (!value) return std::nullopt;

        SkipWhitespace();
        if (!IsEnd())
        {
            SetError(error, "Unexpected trailing characters");
            return std::nullopt;
        }
        return value;
    }

  private:
    std::optional<CJsonValue> ParseValue(std::string* error)
    {
        SkipWhitespace();
        if (IsEnd())
        {
            SetError(error, "Unexpected end of JSON");
            return std::nullopt;
        }

        char ch = Peek();
        if (ch == '{') return ParseObject(error);
        if (ch == '[') return ParseArray(error);
        if (ch == '"') return ParseString(error);
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return ParseNumber(error);
        if (MatchLiteral("true")) return CJsonValue(true);
        if (MatchLiteral("false")) return CJsonValue(false);
        if (MatchLiteral("null")) return CJsonValue(nullptr);

        SetError(error, "Unexpected JSON token");
        return std::nullopt;
    }

    std::optional<CJsonValue> ParseObject(std::string* error)
    {
        Consume();
        CJsonValue::object object;
        SkipWhitespace();
        if (TryConsume('}')) return CJsonValue(std::move(object));

        while (!IsEnd())
        {
            SkipWhitespace();
            std::optional<CJsonValue> key = ParseString(error);
            if (!key || !key->IsString()) return std::nullopt;

            SkipWhitespace();
            if (!TryConsume(':'))
            {
                SetError(error, "Expected ':' after object key");
                return std::nullopt;
            }

            std::optional<CJsonValue> value = ParseValue(error);
            if (!value) return std::nullopt;
            object.emplace(key->AsString(), std::move(*value));

            SkipWhitespace();
            if (TryConsume('}')) return CJsonValue(std::move(object));
            if (!TryConsume(','))
            {
                SetError(error, "Expected ',' or '}' in object");
                return std::nullopt;
            }
        }

        SetError(error, "Unterminated object");
        return std::nullopt;
    }

    std::optional<CJsonValue> ParseArray(std::string* error)
    {
        Consume();
        CJsonValue::array array;
        SkipWhitespace();
        if (TryConsume(']')) return CJsonValue(std::move(array));

        while (!IsEnd())
        {
            std::optional<CJsonValue> value = ParseValue(error);
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));

            SkipWhitespace();
            if (TryConsume(']')) return CJsonValue(std::move(array));
            if (!TryConsume(','))
            {
                SetError(error, "Expected ',' or ']' in array");
                return std::nullopt;
            }
        }

        SetError(error, "Unterminated array");
        return std::nullopt;
    }

    std::optional<CJsonValue> ParseString(std::string* error)
    {
        if (!TryConsume('"'))
        {
            SetError(error, "Expected string");
            return std::nullopt;
        }

        std::string result;
        while (!IsEnd())
        {
            char ch = Consume();
            if (ch == '"') return CJsonValue(std::move(result));
            if (ch != '\\')
            {
                result.push_back(ch);
                continue;
            }

            if (IsEnd())
            {
                SetError(error, "Unterminated escape sequence");
                return std::nullopt;
            }

            char escaped = Consume();
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                SetError(error, "Unsupported string escape");
                return std::nullopt;
            }
        }

        SetError(error, "Unterminated string");
        return std::nullopt;
    }

    std::optional<CJsonValue> ParseNumber(std::string* error)
    {
        size_t start = m_pos;
        if (Peek() == '-') Consume();
        while (!IsEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) Consume();
        if (!IsEnd() && Peek() == '.')
        {
            Consume();
            while (!IsEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) Consume();
        }
        if (!IsEnd() && (Peek() == 'e' || Peek() == 'E'))
        {
            Consume();
            if (!IsEnd() && (Peek() == '+' || Peek() == '-')) Consume();
            while (!IsEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) Consume();
        }

        try
        {
            return CJsonValue(std::stod(m_text.substr(start, m_pos - start)));
        } catch (...) {
            SetError(error, "Invalid number");
            return std::nullopt;
        }
    }

    void SkipWhitespace()
    {
        while (!IsEnd() && std::isspace(static_cast<unsigned char>(Peek())))
        {
            ++m_pos;
        }
    }

    bool MatchLiteral(const char* literal)
    {
        size_t len = std::char_traits<char>::length(literal);
        if (m_text.compare(m_pos, len, literal) != 0) return false;
        m_pos += len;
        return true;
    }

    bool TryConsume(char expected)
    {
        if (IsEnd() || Peek() != expected) return false;
        ++m_pos;
        return true;
    }

    char Peek() const { return m_text[m_pos]; }
    char Consume() { return m_text[m_pos++]; }
    bool IsEnd() const { return m_pos >= m_text.size(); }

    void SetError(std::string* error, const std::string& message) const
    {
        if (error) *error = message + " at byte " + std::to_string(m_pos);
    }

    const std::string& m_text;
    size_t m_pos = 0;
};

std::vector<STmjProperty> ParseProperties(const CJsonValue* properties_value)
{
    std::vector<STmjProperty> properties;
    if (!properties_value || !properties_value->IsArray()) return properties;

    for (const CJsonValue& property_value : properties_value->AsArray())
    {
        const auto& object = property_value.AsObject();
        STmjProperty property;
        if (auto it = object.find("name"); it != object.end()) property.name = it->second.AsString();
        if (auto it = object.find("type"); it != object.end()) property.type = it->second.AsString();
        if (auto it = object.find("value"); it != object.end()) property.value = it->second;
        properties.push_back(std::move(property));
    }
    return properties;
}

std::vector<std::uint32_t> ParseTileData(const CJsonValue* data_value)
{
    std::vector<std::uint32_t> data;
    if (!data_value || !data_value->IsArray()) return data;

    for (const CJsonValue& tile_value : data_value->AsArray())
    {
        data.push_back(static_cast<std::uint32_t>(tile_value.AsInt()));
    }
    return data;
}
}

bool CJsonValue::AsBool(bool fallback) const
{
    return IsBool() ? std::get<bool>(m_data) : fallback;
}

double CJsonValue::AsNumber(double fallback) const
{
    return IsNumber() ? std::get<double>(m_data) : fallback;
}

int CJsonValue::AsInt(int fallback) const
{
    return IsNumber() ? static_cast<int>(std::get<double>(m_data)) : fallback;
}

const std::string& CJsonValue::AsString() const
{
    return IsString() ? std::get<std::string>(m_data) : empty_string;
}

const CJsonValue::array& CJsonValue::AsArray() const
{
    return IsArray() ? std::get<array>(m_data) : empty_array;
}

const CJsonValue::object& CJsonValue::AsObject() const
{
    return IsObject() ? std::get<object>(m_data) : empty_object;
}

const CJsonValue* CJsonValue::Find(const std::string& key) const
{
    if (!IsObject()) return nullptr;

    const auto& object = AsObject();
    auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

std::optional<CTmjMap> CTmjMap::LoadFromFile(const std::filesystem::path& path, std::string* error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        if (error) *error = "Failed to open " + path.string();
        return std::nullopt;
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return Parse(stream.str(), error);
}

std::optional<CTmjMap> CTmjMap::Parse(const std::string& text, std::string* error)
{
    CJsonParser parser(text);
    std::optional<CJsonValue> root_value = parser.Parse(error);
    if (!root_value || !root_value->IsObject()) return std::nullopt;

    const auto& root = root_value->AsObject();
    CTmjMap map;
    if (auto it = root.find("width"); it != root.end()) map.width = it->second.AsInt();
    if (auto it = root.find("height"); it != root.end()) map.height = it->second.AsInt();
    if (auto it = root.find("tilewidth"); it != root.end()) map.tile_width = it->second.AsInt();
    if (auto it = root.find("tileheight"); it != root.end()) map.tile_height = it->second.AsInt();
    if (auto it = root.find("infinite"); it != root.end()) map.infinite = it->second.AsBool();
    if (auto it = root.find("orientation"); it != root.end()) map.orientation = it->second.AsString();
    if (auto it = root.find("renderorder"); it != root.end()) map.render_order = it->second.AsString();
    map.properties = ParseProperties(root_value->Find("properties"));

    if (const CJsonValue* layers_value = root_value->Find("layers"); layers_value && layers_value->IsArray())
    {
        for (const CJsonValue& layer_value : layers_value->AsArray())
        {
            const auto& layer_object = layer_value.AsObject();
            STmjLayer layer;
            if (auto it = layer_object.find("id"); it != layer_object.end()) layer.id = it->second.AsInt();
            if (auto it = layer_object.find("name"); it != layer_object.end()) layer.name = it->second.AsString();
            if (auto it = layer_object.find("type"); it != layer_object.end()) layer.type = it->second.AsString();
            if (auto it = layer_object.find("width"); it != layer_object.end()) layer.width = it->second.AsInt();
            if (auto it = layer_object.find("height"); it != layer_object.end()) layer.height = it->second.AsInt();
            if (auto it = layer_object.find("x"); it != layer_object.end()) layer.x = it->second.AsInt();
            if (auto it = layer_object.find("y"); it != layer_object.end()) layer.y = it->second.AsInt();
            if (auto it = layer_object.find("visible"); it != layer_object.end()) layer.visible = it->second.AsBool(true);
            if (auto it = layer_object.find("opacity"); it != layer_object.end()) layer.opacity = static_cast<float>(it->second.AsNumber(1.0));
            layer.data = ParseTileData(layer_value.Find("data"));
            layer.properties = ParseProperties(layer_value.Find("properties"));
            map.layers.push_back(std::move(layer));
        }
    }

    if (const CJsonValue* tilesets_value = root_value->Find("tilesets"); tilesets_value && tilesets_value->IsArray())
    {
        for (const CJsonValue& tileset_value : tilesets_value->AsArray())
        {
            const auto& tileset_object = tileset_value.AsObject();
            STmjTileset tileset;
            if (auto it = tileset_object.find("firstgid"); it != tileset_object.end()) tileset.first_gid = it->second.AsInt();
            if (auto it = tileset_object.find("source"); it != tileset_object.end()) tileset.source = it->second.AsString();
            if (auto it = tileset_object.find("name"); it != tileset_object.end()) tileset.name = it->second.AsString();
            if (auto it = tileset_object.find("tilewidth"); it != tileset_object.end()) tileset.tile_width = it->second.AsInt();
            if (auto it = tileset_object.find("tileheight"); it != tileset_object.end()) tileset.tile_height = it->second.AsInt();
            if (auto it = tileset_object.find("tilecount"); it != tileset_object.end()) tileset.tile_count = it->second.AsInt();
            if (auto it = tileset_object.find("columns"); it != tileset_object.end()) tileset.columns = it->second.AsInt();
            map.tilesets.push_back(std::move(tileset));
        }
    }

    return map;
}
