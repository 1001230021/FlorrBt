#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CSnapshotReader;
class CSnapshotWriter;

enum class EControllerParamType : std::uint8_t
{
    Int,
    Float,
    Bool,
    String,
};

// Declared by each world controller for its construction-time parameters. The
// waiting room exposes them through /setting: values are checked against the
// type, run through the 审查函数 (`validate`), stored as overrides, and
// applied when the controller is constructed for the game world.
struct SControllerParamInfo
{
    std::string_view key;
    EControllerParamType type = EControllerParamType::String;
    std::string_view config_default; // registered game_config name; "" = none
    std::string_view description;
    // 审查函数: rejects illegal values before they are stored.
    bool (*validate)(const std::string& value) = nullptr;
};

// Generic key -> string parameter bag, decoupled from every concrete
// controller type. Carries construction-time overrides from the waiting room
// into a game world controller.
class CControllerParams
{
  public:
    bool Contains(std::string_view key) const;
    std::string Get(std::string_view key, std::string fallback = "") const;
    int GetInt(std::string_view key, int fallback) const;
    float GetFloat(std::string_view key, float fallback) const;
    bool GetBool(std::string_view key, bool fallback) const;
    void Set(std::string key, std::string value);
    void Clear();
    size_t Size() const { return m_values.size(); }
    const std::unordered_map<std::string, std::string>& Values() const { return m_values; }
    void CaptureSnapshot(CSnapshotWriter& writer) const;
    void RestoreSnapshot(const CSnapshotReader& reader);

  private:
    std::unordered_map<std::string, std::string> m_values;
};

// Resolves a registered game_config default by name ("" when unregistered).
std::string ResolveControllerParamDefault(std::string_view config_name);
// Type-level legality check used by /setting before a value is stored.
bool ControllerParamValueLegal(EControllerParamType type, const std::string& value);
// Human-readable type name ("int", "float", "bool", "string").
std::string_view ControllerParamTypeName(EControllerParamType type);