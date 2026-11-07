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

// 等待房间的构造参数：先校验单值，再校验参数组合，创建玩法时应用。
struct SControllerParamInfo
{
    std::string_view key;
    EControllerParamType type = EControllerParamType::String;
    std::string_view config_default; // 配置项名称，空值表示无默认项
    std::string_view description;
    bool (*validate)(const std::string& value) = nullptr;
};

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

// 返回已注册配置项的默认值，未注册时返回空字符串。
std::string ResolveControllerParamDefault(std::string_view config_name);
bool ControllerParamValueLegal(EControllerParamType type, const std::string& value);
std::string_view ControllerParamTypeName(EControllerParamType type);
