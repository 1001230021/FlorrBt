#include "snapshot_archive.h"
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
std::string KeyString(std::string_view key) { return std::string(key); }

const json_t* Find(const json_t* object, std::string_view key)
{
    if (!json_is_object(object)) return nullptr;
    return json_object_get(object, KeyString(key).c_str());
}

template <typename T> T ParseIntegerString(const json_t* value, T fallback)
{
    if (!json_is_string(value)) return fallback;
    const char* text = json_string_value(value);
    if (!text) return fallback;

    T parsed{};
    const char* end = text + std::char_traits<char>::length(text);
    auto [ptr, ec] = std::from_chars(text, end, parsed);
    return ec == std::errc{} && ptr == end ? parsed : fallback;
}
} // namespace

void CSnapshotWriter::Field(std::string_view key, bool value) const
{
    Node(key, json_boolean(value ? 1 : 0));
}

void CSnapshotWriter::Field(std::string_view key, int value) const { Node(key, json_integer(value)); }

void CSnapshotWriter::Field(std::string_view key, std::uint32_t value) const
{
    Node(key, json_integer(static_cast<json_int_t>(value)));
}

void CSnapshotWriter::Field(std::string_view key, float value) const
{
    Node(key, json_real(std::isfinite(value) ? static_cast<double>(value) : 0.0));
}

void CSnapshotWriter::Field(std::string_view key, double value) const
{
    Node(key, json_real(std::isfinite(value) ? value : 0.0));
}

void CSnapshotWriter::Field(std::string_view key, const char* value) const
{
    Field(key, value ? std::string_view(value) : std::string_view{});
}

void CSnapshotWriter::Field(std::string_view key, std::string_view value) const
{
    Node(key, json_stringn(value.data(), value.size()));
}

void CSnapshotWriter::Field(std::string_view key, sf::Vector2f value) const
{
    json_t* array = json_array();
    if (!array) return;
    json_array_append_new(array, json_real(std::isfinite(value.x) ? value.x : 0.f));
    json_array_append_new(array, json_real(std::isfinite(value.y) ? value.y : 0.f));
    Node(key, array);
}

void CSnapshotWriter::UInt64(std::string_view key, std::uint64_t value) const
{
    const std::string text = std::to_string(value);
    Field(key, text);
}

void CSnapshotWriter::Int64(std::string_view key, std::int64_t value) const
{
    const std::string text = std::to_string(value);
    Field(key, text);
}

void CSnapshotWriter::Node(std::string_view key, json_t* value) const
{
    if (!json_is_object(m_object) || !value)
    {
        if (value) json_decref(value);
        return;
    }
    json_object_set_new(m_object, KeyString(key).c_str(), value);
}

bool CSnapshotReader::Has(std::string_view key) const { return Find(m_object, key) != nullptr; }

const json_t* CSnapshotReader::Node(std::string_view key) const { return Find(m_object, key); }

bool CSnapshotReader::Bool(std::string_view key, bool fallback) const
{
    const json_t* value = Find(m_object, key);
    return json_is_boolean(value) ? json_is_true(value) != 0 : fallback;
}

int CSnapshotReader::Int(std::string_view key, int fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_integer(value)) return fallback;
    const json_int_t parsed = json_integer_value(value);
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max()) return fallback;
    return static_cast<int>(parsed);
}

std::uint32_t CSnapshotReader::UInt32(std::string_view key, std::uint32_t fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_integer(value)) return fallback;
    const json_int_t parsed = json_integer_value(value);
    if (parsed < 0 || static_cast<std::uint64_t>(parsed) > std::numeric_limits<std::uint32_t>::max()) return fallback;
    return static_cast<std::uint32_t>(parsed);
}

float CSnapshotReader::Float(std::string_view key, float fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_number(value)) return fallback;
    const double parsed = json_number_value(value);
    return std::isfinite(parsed) ? static_cast<float>(parsed) : fallback;
}

double CSnapshotReader::Double(std::string_view key, double fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_number(value)) return fallback;
    const double parsed = json_number_value(value);
    return std::isfinite(parsed) ? parsed : fallback;
}

std::string CSnapshotReader::String(std::string_view key, std::string fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_string(value)) return fallback;
    const char* text = json_string_value(value);
    return text ? std::string(text, json_string_length(value)) : fallback;
}

sf::Vector2f CSnapshotReader::Vector2(std::string_view key, sf::Vector2f fallback) const
{
    const json_t* value = Find(m_object, key);
    if (!json_is_array(value) || json_array_size(value) != 2) return fallback;
    const json_t* x = json_array_get(value, 0);
    const json_t* y = json_array_get(value, 1);
    if (!json_is_number(x) || !json_is_number(y)) return fallback;
    const double parsed_x = json_number_value(x);
    const double parsed_y = json_number_value(y);
    if (!std::isfinite(parsed_x) || !std::isfinite(parsed_y)) return fallback;
    return { static_cast<float>(parsed_x), static_cast<float>(parsed_y) };
}

std::uint64_t CSnapshotReader::UInt64(std::string_view key, std::uint64_t fallback) const
{
    return ParseIntegerString(Find(m_object, key), fallback);
}

std::int64_t CSnapshotReader::Int64(std::string_view key, std::int64_t fallback) const
{
    return ParseIntegerString(Find(m_object, key), fallback);
}

CJsonOwner MakeJsonObject() { return CJsonOwner(json_object()); }

CJsonOwner MakeJsonArray() { return CJsonOwner(json_array()); }

bool AppendJson(json_t* array, json_t* value)
{
    if (!json_is_array(array) || !value)
    {
        if (value) json_decref(value);
        return false;
    }
    return json_array_append_new(array, value) == 0;
}

bool WriteJsonAtomically(const std::filesystem::path& path, const json_t* root, std::string& error)
{
    error.clear();
    if (!root)
    {
        error = "Snapshot root is null";
        return false;
    }

    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "Failed to create snapshot directory: " + ec.message();
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    if (json_dump_file(root, temporary.string().c_str(), JSON_INDENT(2) | JSON_SORT_KEYS | JSON_ENSURE_ASCII) != 0)
    {
        error = "Failed to write temporary snapshot " + temporary.string();
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const DWORD win32_error = GetLastError();
        error = "Failed to publish snapshot: " + std::system_category().message(static_cast<int>(win32_error));
        std::filesystem::remove(temporary, ec);
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        error = "Failed to publish snapshot: " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

CJsonOwner LoadJsonFile(const std::filesystem::path& path, std::string& error)
{
    error.clear();
    json_error_t json_error{};
    json_t* root = json_load_file(path.string().c_str(), JSON_REJECT_DUPLICATES, &json_error);
    if (!root)
    {
        error = "Failed to parse " + path.string() + " at line " + std::to_string(json_error.line) + ": " +
                json_error.text;
        return {};
    }
    return CJsonOwner(root);
}
