#pragma once

#include "json_value.h"
#include "logger.h"

#define template tmpl
#define class clazz
#include <tmj.h>
#undef class
#undef template

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include <optional>

inline void TmjLogCb(tmj_log_priority priority, const char* msg)
{
    switch(priority)
    {
        case TMJ_LOG_DEBUG:
            LOG_DEBUG("tmj", msg);
            break;
        case TMJ_LOG_INFO:
            LOG_INFO("tmj", msg);
            break;
        case TMJ_LOG_WARNING:
            LOG_WARN("tmj", msg);
            break;
        case TMJ_LOG_ERR:
            LOG_ERROR("tmj", msg);
            break;
        case TMJ_LOG_CRIT:
            LOG_FATAL("tmj", msg);
            break;
    }
}

struct FlorrBtMap
{
    int width; int height;
    int tile_width = 512;
    int tile_height = 512;
    std::string source_path;

    FlorrBtMap(int w, int h, int tw = 512, int th = 512)
        : width(w), height(h), tile_width(tw), tile_height(th)
    {}

    struct Point { float x, y; };

    struct Layer
    {
        int width; int height;
        std::vector<int> tiles;
        std::string name;

        int* operator[](int row) {
            return tiles.data() + row * width;
        }

        const int* operator[](int row) const {
            return tiles.data() + row * width;
        }
    };
    std::vector<Layer> tile_layers;

    struct Checkpoint
    {
        float x, y, w, h;
        int level;
        std::string name;
        bool is_save = false;
        bool is_respawn_area = false;
    };
    std::vector<Checkpoint> checkpoints;

    struct Warp
    {
        float x, y;
        float radius = 0.f;
        std::string name;
        std::string point;
        std::string goal;
    };
    std::vector<Warp> warps;

    struct Zone
    {
        float density;
        float difficulty;
        std::vector<Point> vertices;
        std::string mobs;
    };
    std::vector<Zone> zones;

    struct Wall
    {
        int id = -1;
        std::vector<Point> vertices;
        bool closed = true;
        Point center = {0.f, 0.f};
        float query_radius = 0.f;
        float min_x = 0.f;
        float min_y = 0.f;
        float max_x = 0.f;
        float max_y = 0.f;
    };
    std::vector<Wall> walls;
};

inline float Scale512(float n)
{
    return n;
}
inline float Scale512(int n)
{
    return static_cast<float>(n);
}
inline float Scale512(double n)
{
    return static_cast<float>(n);
}

inline std::string MapObjectText(const char* text)
{
    return text ? std::string(text) : std::string();
}

inline std::string JsonStringField(const CJsonValue& object, const std::string& key)
{
    const CJsonValue* value = object.Find(key);
    return value && value->IsString() ? value->AsString() : std::string();
}

inline float JsonNumberField(const CJsonValue& object, const std::string& key, float fallback = 0.f)
{
    const CJsonValue* value = object.Find(key);
    return value && value->IsNumber() ? static_cast<float>(value->AsNumber()) : fallback;
}

inline const CJsonValue* JsonPropertyValue(const CJsonValue& object, const std::string& name)
{
    const CJsonValue* properties = object.Find("properties");
    if (!properties || !properties->IsArray()) return nullptr;

    for (const CJsonValue& property : properties->AsArray())
    {
        if (JsonStringField(property, "name") == name)
            return property.Find("value");
    }
    return nullptr;
}

inline float JsonPropertyFloat(const CJsonValue& object, const std::string& name, float fallback = 0.f)
{
    const CJsonValue* value = JsonPropertyValue(object, name);
    if (!value) return fallback;
    if (value->IsNumber()) return static_cast<float>(value->AsNumber());
    if (value->IsString())
    {
        try
        {
            return std::stof(value->AsString());
        }
        catch (...)
        {
            return fallback;
        }
    }
    return fallback;
}

inline std::string JsonPropertyString(const CJsonValue& object, const std::string& name)
{
    const CJsonValue* value = JsonPropertyValue(object, name);
    if (!value) return {};
    if (value->IsString()) return value->AsString();
    if (value->IsNumber()) return std::to_string(value->AsNumber());
    return {};
}

inline bool MapPropertyFloat(const Property& prop, float& out)
{
    if (!prop.type) return false;
    if (strcmp(prop.type, "float") == 0)
    {
        out = static_cast<float>(prop.value_float);
        return true;
    }
    if (strcmp(prop.type, "int") == 0)
    {
        out = static_cast<float>(prop.value_int);
        return true;
    }
    if (strcmp(prop.type, "string") == 0 && prop.value_string)
    {
        try
        {
            out = std::stof(prop.value_string);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    return false;
}

inline bool MapPropertyInt(const Property& prop, int& out)
{
    if (!prop.type) return false;
    if (strcmp(prop.type, "int") == 0)
    {
        out = prop.value_int;
        return true;
    }
    if (strcmp(prop.type, "float") == 0)
    {
        out = static_cast<int>(prop.value_float);
        return true;
    }
    if (strcmp(prop.type, "string") == 0 && prop.value_string)
    {
        try
        {
            out = std::stoi(prop.value_string);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    return false;
}

inline std::string MapToLower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

inline bool IsWallObject(const Layer& layer, const Object& obj)
{
    std::string layer_name = MapToLower(MapObjectText(layer.name));
    if (layer_name == "collision" || layer_name == "collisions" || layer_name == "walls" || layer_name == "wall")
        return true;

    std::string type = MapToLower(MapObjectText(obj.type));
    std::string name = MapToLower(MapObjectText(obj.name));
    return type == "wall" || type == "collision" || type == "collisions" || name == "wall" || name == "collision" ||
           name == "collisions";
}

inline FlorrBtMap::Point TransformObjectPoint(const Object& obj, float local_x, float local_y)
{
    float x = Scale512(static_cast<float>(obj.x + local_x));
    float y = Scale512(static_cast<float>(obj.y + local_y));
    if (std::abs(obj.rotation) <= 0.0001) return {x, y};

    float origin_x = Scale512(static_cast<float>(obj.x));
    float origin_y = Scale512(static_cast<float>(obj.y));
    float angle = static_cast<float>(obj.rotation) * 3.14159265359f / 180.f;
    float s = std::sin(angle);
    float c = std::cos(angle);
    float dx = x - origin_x;
    float dy = y - origin_y;
    return {origin_x + dx * c - dy * s, origin_y + dx * s + dy * c};
}

inline void FinalizeWallBounds(FlorrBtMap::Wall& wall)
{
    if (wall.vertices.empty()) return;

    wall.min_x = wall.max_x = wall.vertices.front().x;
    wall.min_y = wall.max_y = wall.vertices.front().y;
    for (const auto& point : wall.vertices)
    {
        wall.min_x = std::min(wall.min_x, point.x);
        wall.min_y = std::min(wall.min_y, point.y);
        wall.max_x = std::max(wall.max_x, point.x);
        wall.max_y = std::max(wall.max_y, point.y);
    }
    wall.center = {(wall.min_x + wall.max_x) * 0.5f, (wall.min_y + wall.max_y) * 0.5f};
    float max_dist_sq = 0.f;
    for (const auto& point : wall.vertices)
    {
        float dx = point.x - wall.center.x;
        float dy = point.y - wall.center.y;
        max_dist_sq = std::max(max_dist_sq, dx * dx + dy * dy);
    }
    wall.query_radius = std::sqrt(max_dist_sq);
}

inline bool BuildWallFromObject(const Object& obj, FlorrBtMap::Wall& wall)
{
    wall = {};
    wall.closed = true;

    if (obj.is_polygon && obj.polygon_point_count > 0 && obj.polygon)
    {
        wall.vertices.reserve(obj.polygon_point_count);
        for (size_t v = 0; v < obj.polygon_point_count; ++v)
            wall.vertices.push_back(TransformObjectPoint(obj, static_cast<float>(obj.polygon[v].x),
                                                         static_cast<float>(obj.polygon[v].y)));
    }
    else if (!obj.is_polygon && obj.polyline_point_count > 0 && obj.polyline)
    {
        wall.closed = false;
        wall.vertices.reserve(obj.polyline_point_count);
        for (size_t v = 0; v < obj.polyline_point_count; ++v)
            wall.vertices.push_back(TransformObjectPoint(obj, static_cast<float>(obj.polyline[v].x),
                                                         static_cast<float>(obj.polyline[v].y)));
    }
    else if (obj.width > 0.0 && obj.height > 0.0)
    {
        wall.vertices.push_back(TransformObjectPoint(obj, 0.f, 0.f));
        wall.vertices.push_back(TransformObjectPoint(obj, static_cast<float>(obj.width), 0.f));
        wall.vertices.push_back(TransformObjectPoint(obj, static_cast<float>(obj.width), static_cast<float>(obj.height)));
        wall.vertices.push_back(TransformObjectPoint(obj, 0.f, static_cast<float>(obj.height)));
    }

    if (wall.vertices.size() < 2 || (wall.closed && wall.vertices.size() < 3)) return false;
    FinalizeWallBounds(wall);
    return true;
}

inline FlorrBtMap::Point TransformJsonObjectPoint(const CJsonValue& obj, float local_x, float local_y)
{
    float obj_x = static_cast<float>(obj.Find("x") ? obj.Find("x")->AsNumber() : 0.0);
    float obj_y = static_cast<float>(obj.Find("y") ? obj.Find("y")->AsNumber() : 0.0);
    float x = obj_x + local_x;
    float y = obj_y + local_y;
    float rotation = static_cast<float>(obj.Find("rotation") ? obj.Find("rotation")->AsNumber() : 0.0);
    if (std::abs(rotation) <= 0.0001f) return {x, y};

    float angle = rotation * 3.14159265359f / 180.f;
    float s = std::sin(angle);
    float c = std::cos(angle);
    float dx = x - obj_x;
    float dy = y - obj_y;
    return {obj_x + dx * c - dy * s, obj_y + dx * s + dy * c};
}

inline bool BuildWallFromJsonObject(const CJsonValue& obj, FlorrBtMap::Wall& wall)
{
    wall = {};
    wall.closed = true;

    if (const CJsonValue* polygon = obj.Find("polygon"); polygon && polygon->IsArray())
    {
        for (const CJsonValue& point : polygon->AsArray())
        {
            float x = static_cast<float>(point.Find("x") ? point.Find("x")->AsNumber() : 0.0);
            float y = static_cast<float>(point.Find("y") ? point.Find("y")->AsNumber() : 0.0);
            wall.vertices.push_back(TransformJsonObjectPoint(obj, x, y));
        }
    }
    else if (const CJsonValue* polyline = obj.Find("polyline"); polyline && polyline->IsArray())
    {
        wall.closed = false;
        for (const CJsonValue& point : polyline->AsArray())
        {
            float x = static_cast<float>(point.Find("x") ? point.Find("x")->AsNumber() : 0.0);
            float y = static_cast<float>(point.Find("y") ? point.Find("y")->AsNumber() : 0.0);
            wall.vertices.push_back(TransformJsonObjectPoint(obj, x, y));
        }
    }
    else
    {
        float w = static_cast<float>(obj.Find("width") ? obj.Find("width")->AsNumber() : 0.0);
        float h = static_cast<float>(obj.Find("height") ? obj.Find("height")->AsNumber() : 0.0);
        if (w > 0.f && h > 0.f)
        {
            wall.vertices.push_back(TransformJsonObjectPoint(obj, 0.f, 0.f));
            wall.vertices.push_back(TransformJsonObjectPoint(obj, w, 0.f));
            wall.vertices.push_back(TransformJsonObjectPoint(obj, w, h));
            wall.vertices.push_back(TransformJsonObjectPoint(obj, 0.f, h));
        }
    }

    if (wall.vertices.size() < 2 || (wall.closed && wall.vertices.size() < 3)) return false;
    FinalizeWallBounds(wall);
    return true;
}

inline int CanonicalTileGid(int gid)
{
    return gid & 0x1fffffff;
}

inline const Tileset* FindTilesetForGid(const Map& map, int gid)
{
    const Tileset* result = nullptr;
    for (size_t i = 0; i < map.tileset_count; ++i)
    {
        const Tileset& tileset = map.tilesets[i];
        if (gid >= tileset.firstgid) result = &tileset;
    }
    return result;
}

inline const Tile* FindTilesetTile(const Tileset& tileset, int local_id)
{
    for (size_t i = 0; i < tileset.tile_count; ++i)
    {
        if (tileset.tiles[i].id == local_id) return &tileset.tiles[i];
    }
    return nullptr;
}

inline void TransformTileWall(FlorrBtMap::Wall& wall, int raw_gid, int tile_x, int tile_y,
                              int map_tile_width, int map_tile_height, int source_width, int source_height)
{
    const bool flip_h = (raw_gid & 0x80000000) != 0;
    const bool flip_v = (raw_gid & 0x40000000) != 0;
    const bool flip_d = (raw_gid & 0x20000000) != 0;
    const float source_w = static_cast<float>(std::max(1, source_width));
    const float source_h = static_cast<float>(std::max(1, source_height));
    const float target_w = static_cast<float>(map_tile_width);
    const float target_h = static_cast<float>(map_tile_height);

    for (auto& point : wall.vertices)
    {
        float x = point.x / source_w * target_w;
        float y = point.y / source_h * target_h;
        if (flip_d) std::swap(x, y);
        if (flip_h) x = target_w - x;
        if (flip_v) y = target_h - y;
        point.x = static_cast<float>(tile_x * map_tile_width) + x;
        point.y = static_cast<float>(tile_y * map_tile_height) + y;
    }
    FinalizeWallBounds(wall);
}

inline void AddTileCollisionWalls(const Map& map, FlorrBtMap& fbt_map)
{
    for (const auto& layer : fbt_map.tile_layers)
    {
        for (int y = 0; y < layer.height; ++y)
        {
            for (int x = 0; x < layer.width; ++x)
            {
                int raw_gid = layer[y][x];
                int gid = CanonicalTileGid(raw_gid);
                if (gid <= 0) continue;

                const Tileset* tileset = FindTilesetForGid(map, gid);
                if (!tileset) continue;

                int local_id = gid - tileset->firstgid;
                const Tile* tile = FindTilesetTile(*tileset, local_id);
                if (!tile || !tile->objectgroup) continue;

                int source_width = tile->imagewidth > 0 ? tile->imagewidth : tileset->tilewidth;
                int source_height = tile->imageheight > 0 ? tile->imageheight : tileset->tileheight;
                for (size_t object_index = 0; object_index < tile->objectgroup->object_count; ++object_index)
                {
                    FlorrBtMap::Wall wall;
                    if (!BuildWallFromObject(tile->objectgroup->objects[object_index], wall)) continue;
                    TransformTileWall(wall, raw_gid, x, y, fbt_map.tile_width, fbt_map.tile_height,
                                      source_width, source_height);
                    wall.id = static_cast<int>(fbt_map.walls.size());
                    fbt_map.walls.push_back(std::move(wall));
                }
            }
        }
    }

    for (size_t i = 0; i < fbt_map.walls.size(); ++i)
        fbt_map.walls[i].id = static_cast<int>(i);
}

struct JsonTileCollision
{
    int image_width = 256;
    int image_height = 256;
    std::vector<FlorrBtMap::Wall> walls;
};

inline void AddJsonTilesetCollisionDefs(const CJsonValue& tileset, int first_gid,
                                        std::unordered_map<int, JsonTileCollision>& out_defs)
{
    int tileset_tile_width = tileset.Find("tilewidth") ? tileset.Find("tilewidth")->AsInt(256) : 256;
    int tileset_tile_height = tileset.Find("tileheight") ? tileset.Find("tileheight")->AsInt(256) : 256;
    const CJsonValue* tiles = tileset.Find("tiles");
    if (!tiles || !tiles->IsArray()) return;

    for (const CJsonValue& tile : tiles->AsArray())
    {
        const CJsonValue* objectgroup = tile.Find("objectgroup");
        const CJsonValue* objects = objectgroup ? objectgroup->Find("objects") : nullptr;
        if (!objects || !objects->IsArray()) continue;

        JsonTileCollision def;
        def.image_width = tile.Find("imagewidth") ? tile.Find("imagewidth")->AsInt(tileset_tile_width) : tileset_tile_width;
        def.image_height = tile.Find("imageheight") ? tile.Find("imageheight")->AsInt(tileset_tile_height) : tileset_tile_height;

        for (const CJsonValue& object : objects->AsArray())
        {
            FlorrBtMap::Wall wall;
            if (BuildWallFromJsonObject(object, wall)) def.walls.push_back(std::move(wall));
        }
        if (!def.walls.empty())
        {
            int tile_id = tile.Find("id") ? tile.Find("id")->AsInt(0) : 0;
            out_defs[first_gid + tile_id] = std::move(def);
        }
    }
}

inline void AddJsonTileCollisionWalls(const std::filesystem::path& map_path, FlorrBtMap& fbt_map)
{
    std::string error;
    std::optional<CJsonValue> map_json = CJsonValue::LoadFromFile(map_path, &error);
    if (!map_json)
    {
        LOG_WARN("maploader", "Failed to read map json for tile collisions: " + map_path.string() + " (" + error + ")");
        return;
    }

    std::unordered_map<int, JsonTileCollision> collision_defs;
    const CJsonValue* tilesets = map_json->Find("tilesets");
    if (!tilesets || !tilesets->IsArray()) return;

    for (const CJsonValue& tileset_ref : tilesets->AsArray())
    {
        int first_gid = tileset_ref.Find("firstgid") ? tileset_ref.Find("firstgid")->AsInt(1) : 1;
        if (const CJsonValue* source = tileset_ref.Find("source"); source && source->IsString())
        {
            std::filesystem::path source_path = map_path.parent_path() / std::filesystem::path(source->AsString());
            std::optional<CJsonValue> tileset_json = CJsonValue::LoadFromFile(source_path, &error);
            if (!tileset_json)
            {
                LOG_WARN("maploader", "Failed to read tileset json: " + source_path.string() + " (" + error + ")");
                continue;
            }
            AddJsonTilesetCollisionDefs(*tileset_json, first_gid, collision_defs);
        }
        else
        {
            AddJsonTilesetCollisionDefs(tileset_ref, first_gid, collision_defs);
        }
    }

    for (const auto& layer : fbt_map.tile_layers)
    {
        for (int y = 0; y < layer.height; ++y)
        {
            for (int x = 0; x < layer.width; ++x)
            {
                int raw_gid = layer[y][x];
                int gid = CanonicalTileGid(raw_gid);
                auto it = collision_defs.find(gid);
                if (it == collision_defs.end()) continue;

                const JsonTileCollision& def = it->second;
                for (FlorrBtMap::Wall wall : def.walls)
                {
                    TransformTileWall(wall, raw_gid, x, y, fbt_map.tile_width, fbt_map.tile_height,
                                      def.image_width, def.image_height);
                    wall.id = static_cast<int>(fbt_map.walls.size());
                    fbt_map.walls.push_back(std::move(wall));
                }
            }
        }
    }

    for (size_t i = 0; i < fbt_map.walls.size(); ++i)
        fbt_map.walls[i].id = static_cast<int>(i);
}

inline bool BuildZoneFromJsonObject(const CJsonValue& obj, FlorrBtMap::Zone& zone)
{
    zone = {};
    zone.density = JsonPropertyFloat(obj, "density", 0.f);
    zone.difficulty = JsonPropertyFloat(obj, "difficulty", 0.f);
    zone.mobs = JsonPropertyString(obj, "mobs");

    if (const CJsonValue* polygon = obj.Find("polygon"); polygon && polygon->IsArray())
    {
        for (const CJsonValue& point : polygon->AsArray())
        {
            float x = JsonNumberField(point, "x", 0.f);
            float y = JsonNumberField(point, "y", 0.f);
            zone.vertices.push_back(TransformJsonObjectPoint(obj, x, y));
        }
    }
    else
    {
        float w = JsonNumberField(obj, "width", 0.f);
        float h = JsonNumberField(obj, "height", 0.f);
        if (w > 0.f && h > 0.f)
        {
            zone.vertices.push_back(TransformJsonObjectPoint(obj, 0.f, 0.f));
            zone.vertices.push_back(TransformJsonObjectPoint(obj, w, 0.f));
            zone.vertices.push_back(TransformJsonObjectPoint(obj, w, h));
            zone.vertices.push_back(TransformJsonObjectPoint(obj, 0.f, h));
        }
    }

    return zone.vertices.size() >= 3;
}

inline bool BuildCheckpointFromJsonObject(const CJsonValue& obj, const std::string& object_type,
                                          FlorrBtMap::Checkpoint& checkpoint)
{
    checkpoint = {};
    checkpoint.x = Scale512(JsonNumberField(obj, "x", 0.f));
    checkpoint.y = Scale512(JsonNumberField(obj, "y", 0.f));
    checkpoint.w = Scale512(JsonNumberField(obj, "width", 0.f));
    checkpoint.h = Scale512(JsonNumberField(obj, "height", 0.f));
    checkpoint.level = std::max(0, static_cast<int>(JsonPropertyFloat(obj, "level", 0.f)));
    checkpoint.name = JsonStringField(obj, "name");
    if (checkpoint.name.empty() && object_type == "new_players") checkpoint.name = "new_players";
    checkpoint.is_save = object_type == "checkpoint";
    checkpoint.is_respawn_area = object_type == "respawn_area";
    return object_type == "checkpoint" || object_type == "respawn_area" || object_type == "new_players";
}

inline bool SameCheckpointObject(const FlorrBtMap::Checkpoint& lhs, const FlorrBtMap::Checkpoint& rhs)
{
    constexpr float epsilon = 0.5f;
    return std::abs(lhs.x - rhs.x) <= epsilon &&
           std::abs(lhs.y - rhs.y) <= epsilon &&
           std::abs(lhs.w - rhs.w) <= epsilon &&
           std::abs(lhs.h - rhs.h) <= epsilon &&
           lhs.is_save == rhs.is_save &&
           lhs.is_respawn_area == rhs.is_respawn_area;
}

inline bool HasCheckpointObject(const FlorrBtMap& map, const FlorrBtMap::Checkpoint& checkpoint)
{
    for (const FlorrBtMap::Checkpoint& existing : map.checkpoints)
    {
        if (SameCheckpointObject(existing, checkpoint)) return true;
    }
    return false;
}

inline void AddJsonClassObjectLayers(const std::filesystem::path& map_path, FlorrBtMap& fbt_map)
{
    std::string error;
    std::optional<CJsonValue> map_json = CJsonValue::LoadFromFile(map_path, &error);
    if (!map_json)
    {
        LOG_WARN("maploader", "Failed to read map json for object classes: " + map_path.string() + " (" + error + ")");
        return;
    }

    const CJsonValue* layers = map_json->Find("layers");
    if (!layers || !layers->IsArray()) return;

    size_t zones_before = fbt_map.zones.size();
    size_t warps_before = fbt_map.warps.size();
    for (const CJsonValue& layer : layers->AsArray())
    {
        if (JsonStringField(layer, "type") != "objectgroup") continue;

        const CJsonValue* objects = layer.Find("objects");
        if (!objects || !objects->IsArray()) continue;

        for (const CJsonValue& obj : objects->AsArray())
        {
            std::string object_type = MapToLower(JsonStringField(obj, "class"));
            if (object_type.empty()) object_type = MapToLower(JsonStringField(obj, "type"));
            if (object_type.empty()) continue;

            if (object_type == "spawn_mobs")
            {
                if (zones_before != 0) continue;
                FlorrBtMap::Zone zone;
                if (BuildZoneFromJsonObject(obj, zone)) fbt_map.zones.push_back(std::move(zone));
            }
            else if (object_type == "checkpoint" || object_type == "respawn_area" || object_type == "new_players")
            {
                FlorrBtMap::Checkpoint checkpoint;
                if (BuildCheckpointFromJsonObject(obj, object_type, checkpoint) &&
                    !HasCheckpointObject(fbt_map, checkpoint))
                    fbt_map.checkpoints.push_back(std::move(checkpoint));
            }
            else if (object_type == "warp" || object_type == "portal")
            {
                if (warps_before != 0) continue;
                FlorrBtMap::Warp warp;
                warp.x = JsonNumberField(obj, "x", 0.f);
                warp.y = JsonNumberField(obj, "y", 0.f);
                warp.name = JsonStringField(obj, "name");
                warp.radius = JsonPropertyFloat(obj, "radius", 0.f);
                if (warp.radius <= 0.f)
                    warp.radius = std::max(JsonNumberField(obj, "width", 0.f), JsonNumberField(obj, "height", 0.f)) * 0.5f;
                warp.point = JsonPropertyString(obj, "warp_point");
                if (warp.point.empty()) warp.point = JsonPropertyString(obj, "from");
                if (warp.point.empty()) warp.point = JsonPropertyString(obj, "point");
                warp.goal = JsonPropertyString(obj, "world");
                if (warp.goal.empty()) warp.goal = JsonPropertyString(obj, "world_name");
                if (warp.goal.empty()) warp.goal = JsonPropertyString(obj, "target");
                if (warp.goal.empty()) warp.goal = JsonPropertyString(obj, "map");
                fbt_map.warps.push_back(std::move(warp));
            }
        }
    }

    if (fbt_map.zones.size() != zones_before || fbt_map.warps.size() != warps_before)
    {
        LOG_INFO("maploader", "Map json object classes registered: zones +" +
                                  std::to_string(fbt_map.zones.size() - zones_before) + ", warps +" +
                                  std::to_string(fbt_map.warps.size() - warps_before));
    }
}

inline std::unique_ptr<FlorrBtMap> LoadMapFromTmj(const std::string& path)
{
    tmj_log_regcb(true, TmjLogCb);

    std::filesystem::path resolved_path = path;
    if (!std::filesystem::exists(resolved_path))
    {
        const std::filesystem::path candidates[] = {
            std::filesystem::path("..") / path,
            std::filesystem::path("..") / ".." / path,
            std::filesystem::path("..") / ".." / ".." / path,
        };
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
            {
                resolved_path = candidate;
                break;
            }
        }
    }

    Map* map = tmj_map_loadf(resolved_path.string().c_str(), true);
    if (!map) {
        LOG_ERROR("maploader", "Failed to load map file: " + path);
        return nullptr;
    }

    auto map_guard = std::unique_ptr<Map, decltype(&tmj_map_free)>(map, tmj_map_free);
    std::string info_msg = "Map file loaded (" + resolved_path.string() + "): " + std::to_string(map->width) + "x" + std::to_string(map->height);
    LOG_INFO("maploader", info_msg);

    auto fbt_map = std::make_unique<FlorrBtMap>(map->width, map->height, map->tilewidth, map->tileheight);
    fbt_map->source_path = std::filesystem::path(path).generic_string();

    for (size_t i = 0; i < map->layer_count; ++i)
    {
        const Layer& layer_ori = map->layers[i];

        if (layer_ori.type && strcmp(layer_ori.type, "tilelayer") == 0)
        {
            FlorrBtMap::Layer layer;
            layer.width = map->width;
            layer.height = map->height;
            layer.name = layer_ori.name ? layer_ori.name : "";

            layer.tiles.resize(layer.width * layer.height);

            if (!layer_ori.data_is_str && layer_ori.data_uint) {
                for (int y = 0; y < map->height; ++y) {
                    for (int x = 0; x < map->width; ++x) {
                        size_t idx = y * map->width + x;
                        if (idx < layer_ori.data_count) {
                            layer[y][x] = static_cast<int>(layer_ori.data_uint[idx]);
                        } else {
                            layer[y][x] = 0;
                        }
                    }
                }
            }
            fbt_map->tile_layers.push_back(layer);
        }
        else if (layer_ori.type && strcmp(layer_ori.type, "objectgroup") == 0)
        {
            for (size_t j = 0; j < layer_ori.object_count; ++j)
            {
                const Object& obj = layer_ori.objects[j];
                std::string layer_name = MapToLower(MapObjectText(layer_ori.name));
                std::string object_type = MapToLower(MapObjectText(obj.type));
                if (IsWallObject(layer_ori, obj))
                {
                    FlorrBtMap::Wall wall;
                    if (BuildWallFromObject(obj, wall)) fbt_map->walls.push_back(std::move(wall));
                    continue;
                }

                if (object_type == "spawn_mobs")
                {
                    FlorrBtMap::Zone zone;
                    zone.density = 0.0f;
                    zone.difficulty = 0.0f;
                    zone.mobs = "";

                    for (size_t p = 0; p < obj.property_count; ++p) {
                        const Property& prop = obj.properties[p];
                        if (strcmp(prop.name, "density") == 0) {
                            MapPropertyFloat(prop, zone.density);
                        } else if (strcmp(prop.name, "difficulty") == 0) {
                            MapPropertyFloat(prop, zone.difficulty);
                        } else if (strcmp(prop.name, "mobs") == 0) {
                            if (prop.value_string) {
                                zone.mobs = prop.value_string;
                            }
                        }
                    }

                    if (obj.polygon_point_count > 0 && obj.polygon) {
                        for (size_t v = 0; v < obj.polygon_point_count; ++v) {
                            float vx = Scale512(static_cast<float>(obj.x + obj.polygon[v].x));
                            float vy = Scale512(static_cast<float>(obj.y + obj.polygon[v].y));
                            zone.vertices.push_back({vx, vy});
                        }
                    }
                    else {
                        float x1 = Scale512(static_cast<float>(obj.x));
                        float y1 = Scale512(static_cast<float>(obj.y));
                        float x2 = Scale512(static_cast<float>(obj.x + obj.width));
                        float y2 = Scale512(static_cast<float>(obj.y + obj.height));

                        zone.vertices.push_back({x1, y1});
                        zone.vertices.push_back({x2, y1});
                        zone.vertices.push_back({x2, y2});
                        zone.vertices.push_back({x1, y2});
                    }
                    fbt_map->zones.push_back(zone);
                }
                else if (object_type == "checkpoint" || object_type == "respawn_area" ||
                         object_type == "new_players")
                {
                    FlorrBtMap::Checkpoint cp;
                    cp.x = Scale512(static_cast<float>(obj.x));
                    cp.y = Scale512(static_cast<float>(obj.y));
                    cp.w = Scale512(static_cast<float>(obj.width));
                    cp.h = Scale512(static_cast<float>(obj.height));
                    cp.level = 0;
                    cp.name = MapObjectText(obj.name);
                    if (cp.name.empty() && object_type == "new_players") cp.name = "new_players";
                    cp.is_save = object_type == "checkpoint";
                    cp.is_respawn_area = object_type == "respawn_area";

                    for (size_t p = 0; p < obj.property_count; ++p) {
                        const Property& prop = obj.properties[p];
                        if (strcmp(prop.name, "level") == 0) {
                            MapPropertyInt(prop, cp.level);
                        }
                    }
                    fbt_map->checkpoints.push_back(cp);
                }
                else if (object_type == "warp" || object_type == "portal")
                {
                    FlorrBtMap::Warp warp;
                    warp.x = Scale512(static_cast<float>(obj.x));
                    warp.y = Scale512(static_cast<float>(obj.y));
                    warp.radius = Scale512(static_cast<float>(std::max(obj.width, obj.height) * 0.5));
                    warp.name = MapObjectText(obj.name);
                    warp.point = "";
                    warp.goal = "";

                    for (size_t p = 0; p < obj.property_count; ++p) {
                        const Property& prop = obj.properties[p];
                        if (strcmp(prop.name, "radius") == 0) {
                            float radius = 0.f;
                            if (MapPropertyFloat(prop, radius)) warp.radius = Scale512(radius);
                        } else if ((strcmp(prop.name, "warp_point") == 0 || strcmp(prop.name, "from") == 0 ||
                                    strcmp(prop.name, "point") == 0) && prop.value_string) {
                            warp.point = prop.value_string;
                        } else if ((strcmp(prop.name, "world") == 0 || strcmp(prop.name, "world_name") == 0 ||
                                    strcmp(prop.name, "target") == 0 || strcmp(prop.name, "map") == 0) &&
                                   prop.value_string) {
                            warp.goal = prop.value_string;
                        }
                    }
                    fbt_map->warps.push_back(warp);
                }
            }
        }
    }

    size_t walls_before_tiles = fbt_map->walls.size();
    AddTileCollisionWalls(*map, *fbt_map);
    if (fbt_map->walls.size() == walls_before_tiles)
        AddJsonTileCollisionWalls(resolved_path, *fbt_map);
    AddJsonClassObjectLayers(resolved_path, *fbt_map);
    LOG_INFO("maploader", "Map collision walls registered: " + std::to_string(fbt_map->walls.size()));
    return fbt_map;
}
