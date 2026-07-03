#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

class CJsonValue
{
  public:
    using array = std::vector<CJsonValue>;
    using object = std::unordered_map<std::string, CJsonValue>;
    using value = std::variant<std::nullptr_t, bool, double, std::string, array, object>;

    CJsonValue() = default;
    explicit CJsonValue(value data) : m_data(std::move(data)) {}

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(m_data); }
    bool IsBool() const { return std::holds_alternative<bool>(m_data); }
    bool IsNumber() const { return std::holds_alternative<double>(m_data); }
    bool IsString() const { return std::holds_alternative<std::string>(m_data); }
    bool IsArray() const { return std::holds_alternative<array>(m_data); }
    bool IsObject() const { return std::holds_alternative<object>(m_data); }

    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    int AsInt(int fallback = 0) const;
    const std::string& AsString() const;
    const array& AsArray() const;
    const object& AsObject() const;
    const CJsonValue* Find(const std::string& key) const;

  private:
    value m_data = nullptr;
};

struct STmjProperty
{
    std::string name;
    std::string type;
    CJsonValue value;
};

struct STmjTileset
{
    int first_gid = 0;
    std::string source;
    std::string name;
    int tile_width = 0;
    int tile_height = 0;
    int tile_count = 0;
    int columns = 0;
};

struct STmjLayer
{
    int id = 0;
    std::string name;
    std::string type;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool visible = true;
    float opacity = 1.f;
    std::vector<std::uint32_t> data;
    std::vector<STmjProperty> properties;
};

class CTmjMap
{
  public:
    static std::optional<CTmjMap> LoadFromFile(const std::filesystem::path& path, std::string* error = nullptr);
    static std::optional<CTmjMap> Parse(const std::string& text, std::string* error = nullptr);

    int width = 0;
    int height = 0;
    int tile_width = 0;
    int tile_height = 0;
    bool infinite = false;
    std::string orientation;
    std::string render_order;
    std::vector<STmjLayer> layers;
    std::vector<STmjTileset> tilesets;
    std::vector<STmjProperty> properties;
};
