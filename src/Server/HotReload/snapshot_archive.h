#pragma once

#include <SFML/System/Vector2.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

extern "C"
{
#include <jansson.h>
}

struct CJsonDeleter
{
    void operator()(json_t* value) const
    {
        if (value) json_decref(value);
    }
};

using CJsonOwner = std::unique_ptr<json_t, CJsonDeleter>;

class CSnapshotWriter
{
  public:
    explicit CSnapshotWriter(json_t* object) : m_object(object) {}

    bool IsValid() const { return json_is_object(m_object); }
    json_t* Object() const { return m_object; }

    void Field(std::string_view key, bool value) const;
    void Field(std::string_view key, int value) const;
    void Field(std::string_view key, std::uint32_t value) const;
    void Field(std::string_view key, float value) const;
    void Field(std::string_view key, double value) const;
    void Field(std::string_view key, const char* value) const;
    void Field(std::string_view key, std::string_view value) const;
    void Field(std::string_view key, sf::Vector2f value) const;
    void UInt64(std::string_view key, std::uint64_t value) const;
    void Int64(std::string_view key, std::int64_t value) const;
    void Node(std::string_view key, json_t* value) const;

  private:
    json_t* m_object = nullptr;
};

class CSnapshotReader
{
  public:
    explicit CSnapshotReader(const json_t* object) : m_object(object) {}

    bool IsValid() const { return json_is_object(m_object); }
    bool Has(std::string_view key) const;
    const json_t* Node(std::string_view key) const;

    bool Bool(std::string_view key, bool fallback = false) const;
    int Int(std::string_view key, int fallback = 0) const;
    std::uint32_t UInt32(std::string_view key, std::uint32_t fallback = 0) const;
    float Float(std::string_view key, float fallback = 0.f) const;
    double Double(std::string_view key, double fallback = 0.0) const;
    std::string String(std::string_view key, std::string fallback = {}) const;
    sf::Vector2f Vector2(std::string_view key, sf::Vector2f fallback = {}) const;
    std::uint64_t UInt64(std::string_view key, std::uint64_t fallback = 0) const;
    std::int64_t Int64(std::string_view key, std::int64_t fallback = 0) const;

  private:
    const json_t* m_object = nullptr;
};

CJsonOwner MakeJsonObject();
CJsonOwner MakeJsonArray();
bool AppendJson(json_t* array, json_t* value);
bool WriteJsonAtomically(const std::filesystem::path& path, const json_t* root, std::string& error);
CJsonOwner LoadJsonFile(const std::filesystem::path& path, std::string& error);
