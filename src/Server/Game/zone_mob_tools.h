#pragma once
#include "../../Engine/map_tools.h"
#include "../../Shared/mob_type.h"
#include "entities/mob.h"
#include <SFML/System/Vector2.hpp>
#include <algorithm>
#include <cctype>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

inline std::mt19937& ZoneMobRng()
{
    static std::mt19937 rng{std::random_device{}()};
    return rng;
}

inline std::string ZoneMobToLower(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (char ch : text)
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    return result;
}

inline bool TryParseZoneMobType(std::string_view text, EMobType& type)
{
    std::string lowered = ZoneMobToLower(text);
    for (const auto& [mob_type, proto] : g_mob_registry)
    {
        if (mob_type == EMobType::None || mob_type == EMobType::PlayerFlower) continue;
        if (MatchMobTypeAlias(lowered, mob_type) || ZoneMobToLower(GetMobTypeName(mob_type)) == lowered ||
            (proto && ZoneMobToLower(proto->m_name) == lowered))
        {
            type = mob_type;
            return true;
        }
    }
    return false;
}

inline std::vector<EMobType> ParseZoneMobs(std::string_view mobs)
{
    std::vector<EMobType> result;
    std::string lowered_pool = ZoneMobToLower(mobs);
    if (lowered_pool == "garden")
    {
        return {EMobType::NormalLadybug, EMobType::SoldierAnt, EMobType::Bee, EMobType::Hornet};
    }
    if (lowered_pool == "hornet")
    {
        return {EMobType::Hornet};
    }

    std::string token;
    std::stringstream stream{std::string(mobs)};
    while (std::getline(stream, token, ','))
    {
        token.erase(token.begin(), std::find_if(token.begin(), token.end(), [](unsigned char ch)
        {
            return !std::isspace(ch);
        }));
        token.erase(std::find_if(token.rbegin(), token.rend(), [](unsigned char ch)
        {
            return !std::isspace(ch);
        }).base(), token.end());

        EMobType type = EMobType::None;
        if (TryParseZoneMobType(token, type)) result.push_back(type);
    }

    return result;
}

inline EMobType PickZoneMobType(const std::vector<EMobType>& mob_types)
{
    if (mob_types.empty()) return EMobType::None;
    std::uniform_int_distribution<size_t> dist(0, mob_types.size() - 1);
    return mob_types[dist(ZoneMobRng())];
}

inline bool IsPointInZone(const FlorrBtMap::Zone& zone, const sf::Vector2f& pos)
{
    const auto& vertices = zone.vertices;
    if (vertices.size() < 3) return false;

    bool inside = false;
    size_t j = vertices.size() - 1;
    for (size_t i = 0; i < vertices.size(); j = i++)
    {
        const auto& vi = vertices[i];
        const auto& vj = vertices[j];
        bool crosses = ((vi.y > pos.y) != (vj.y > pos.y)) &&
                       (pos.x < (vj.x - vi.x) * (pos.y - vi.y) / (vj.y - vi.y) + vi.x);
        if (crosses) inside = !inside;
    }
    return inside;
}
