#include "controller_params.h"
#include "../../Shared/game_config.h"
#include "../HotReload/snapshot_archive.h"
#include <algorithm>
#include <cctype>
#include <sstream>

bool CControllerParams::Contains(std::string_view key) const
{
    return m_values.find(std::string(key)) != m_values.end();
}

std::string CControllerParams::Get(std::string_view key, std::string fallback) const
{
    const auto it = m_values.find(std::string(key));
    return it == m_values.end() ? fallback : it->second;
}

int CControllerParams::GetInt(std::string_view key, int fallback) const
{
    const std::string value = Get(key);
    if (value.empty()) return fallback;
    std::stringstream stream(value);
    int parsed = fallback;
    stream >> parsed;
    if (stream.fail() || !stream.eof()) return fallback;
    return parsed;
}

float CControllerParams::GetFloat(std::string_view key, float fallback) const
{
    const std::string value = Get(key);
    if (value.empty()) return fallback;
    std::stringstream stream(value);
    float parsed = fallback;
    stream >> parsed;
    if (stream.fail() || !stream.eof()) return fallback;
    return parsed;
}

bool CControllerParams::GetBool(std::string_view key, bool fallback) const
{
    std::string value = Get(key);
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
    if (value == "0" || value == "false" || value == "no" || value == "off") return false;
    return fallback;
}

void CControllerParams::Set(std::string key, std::string value)
{
    m_values[std::move(key)] = std::move(value);
}

void CControllerParams::Clear()
{
    m_values.clear();
}

void CControllerParams::CaptureSnapshot(CSnapshotWriter& writer) const
{
    CJsonOwner object = MakeJsonObject();
    for (const auto& [key, value] : m_values)
    {
        if (json_object_set_new(object.get(), key.c_str(), json_string(value.c_str())) != 0) return;
    }
    writer.Node("controller_params", object.release());
}

void CControllerParams::RestoreSnapshot(const CSnapshotReader& reader)
{
    m_values.clear();
    const json_t* node = reader.Node("controller_params");
    if (!node || !json_is_object(node)) return;

    const char* key = nullptr;
    json_t* value = nullptr;
    json_object_foreach(const_cast<json_t*>(node), key, value)
    {
        if (!key || !json_is_string(value)) continue;
        m_values[key] = json_string_value(value);
    }
}

std::string ResolveControllerParamDefault(std::string_view config_name)
{
    if (config_name.empty()) return {};
    auto& entries = game_config::GetConfigEntries();
    const auto it = entries.find(std::string(config_name));
    return it == entries.end() ? std::string{} : it->second.getter();
}

bool ControllerParamValueLegal(EControllerParamType type, const std::string& value)
{
    switch (type)
    {
    case EControllerParamType::Int: {
        std::stringstream stream(value);
        long long parsed = 0;
        stream >> parsed;
        if (stream.fail() || !stream.eof()) return false;
        break;
    }
    case EControllerParamType::Float: {
        std::stringstream stream(value);
        double parsed = 0.0;
        stream >> parsed;
        if (stream.fail() || !stream.eof()) return false;
        break;
    }
    case EControllerParamType::Bool: {
        std::string normalized = value;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (normalized != "1" && normalized != "true" && normalized != "yes" && normalized != "on" &&
            normalized != "0" && normalized != "false" && normalized != "no" && normalized != "off")
            return false;
        break;
    }
    case EControllerParamType::String:
        break;
    }
    return true;
}

std::string_view ControllerParamTypeName(EControllerParamType type)
{
    switch (type)
    {
    case EControllerParamType::Int:
        return "int";
    case EControllerParamType::Float:
        return "float";
    case EControllerParamType::Bool:
        return "bool";
    case EControllerParamType::String:
        return "string";
    }
    return "string";
}