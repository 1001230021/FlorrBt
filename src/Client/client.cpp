#include "../Shared/game_config.h"
#include "../Shared/network_msg.h"
#include "../Shared/petal_type.h"
#include "../Shared/rarity.h"
#include <SFML/Graphics.hpp>
#include <SFML/Network/IpAddress.hpp>
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/System/Time.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
constexpr unsigned int window_width = 960;
constexpr unsigned int window_height = 640;
constexpr float prediction_snap_distance = 220.f;
constexpr float orbit_radius = 54.f;
constexpr float flower_radius = 22.f;
constexpr float packet_interval = 1.f / 30.f;
constexpr float pi = 3.14159265359f;
constexpr float server_tick_dt = 0.016f;

struct connection_state
{
    sf::TcpSocket socket;
    std::vector<uint8_t> receive_buffer;
    std::vector<uint8_t> send_buffer;
    bool connected = false;
    std::string message = "Disconnected";
    uint32_t last_snapshot_sequence = 0;
    int32_t player_entity_id = -1;
};

struct visual_petal
{
    sf::Color color;
    float radius = 9.f;
};

struct client_entity
{
    ServerEntitySnapshot snapshot;
    sf::Vector2f render_pos;
    bool initialized = false;
};

struct delayed_snapshot
{
    float release_time = 0.f;
    ServerSnapshot snapshot;
};

struct latency_tool_state
{
    bool visible = true;
    bool prediction_enabled = true;
    bool auto_tune = true;
    float artificial_snapshot_latency = 0.f;
    float correction_gain = 0.12f;
    float effective_correction_gain = 0.f;
    float correction_deadzone = 3.f;
    float correction_rate = 8.f;
    float camera_follow_rate = 18.f;
    float remote_interp_rate = 12.f;
    float estimated_compensation_delay = 0.f;

    float last_snapshot_time = -1.f;
    float average_snapshot_interval = 0.f;
    float snapshot_age = 0.f;
    float authoritative_error = 0.f;
    float correction_offset = 0.f;
    float predicted_speed = 0.f;
    float camera_error = 0.f;
    uint32_t last_sequence = 0;
    uint32_t missed_snapshots = 0;
    std::deque<delayed_snapshot> pending_snapshots;
};

sf::Vector2f Vec2(float x, float y)
{
    return {x, y};
}

sf::Vector2f SnapshotPos(const ServerEntitySnapshot& entity)
{
    return {entity.x, entity.y};
}

sf::Vector2f Lerp(sf::Vector2f a, sf::Vector2f b, float t)
{
    return a + (b - a) * std::clamp(t, 0.f, 1.f);
}

float Lerp(float a, float b, float t)
{
    return a + (b - a) * std::clamp(t, 0.f, 1.f);
}

float SmoothFactor(float rate, float dt)
{
    return 1.f - std::exp(-rate * dt);
}

std::string FormatFloat(float value, int precision = 1)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string OnOff(bool value)
{
    return value ? "on" : "off";
}

float AutoCorrectionGain(const latency_tool_state& latency, float error)
{
    if (error <= latency.correction_deadzone) return 0.f;

    const float usable_error = error - latency.correction_deadzone;
    const float normalized = std::clamp(usable_error / (prediction_snap_distance - latency.correction_deadzone), 0.f, 1.f);
    float gain = 0.015f + 0.22f * std::pow(normalized, 1.35f);

    if (error < 14.f)
        gain = std::min(gain, 0.035f);
    else if (error < 36.f)
        gain = std::min(gain, 0.08f);

    if (latency.average_snapshot_interval > 0.f && latency.snapshot_age > latency.average_snapshot_interval * 2.f)
        gain += 0.025f;

    return std::clamp(gain, 0.f, 0.28f);
}

void AutoTuneLatency(latency_tool_state& latency, float dt)
{
    if (!latency.auto_tune) return;

    const float snapshot_interval = latency.average_snapshot_interval > 0.f ? latency.average_snapshot_interval : 0.05f;
    latency.estimated_compensation_delay =
        latency.artificial_snapshot_latency + snapshot_interval * 0.5f + std::min(latency.snapshot_age, snapshot_interval);

    const float target_deadzone = std::clamp(latency.predicted_speed * snapshot_interval * 0.18f + 2.5f, 2.5f, 8.f);
    const float target_correction_rate = std::clamp(1.15f / snapshot_interval, 6.f, 18.f);
    const float target_camera_rate = std::clamp(0.9f / snapshot_interval, 10.f, 24.f);
    const float target_remote_rate = std::clamp(0.65f / snapshot_interval, 8.f, 18.f);
    const float follow = SmoothFactor(5.f, dt);

    latency.correction_deadzone = Lerp(latency.correction_deadzone, target_deadzone, follow);
    latency.correction_rate = Lerp(latency.correction_rate, target_correction_rate, follow);
    latency.camera_follow_rate = Lerp(latency.camera_follow_rate, target_camera_rate, follow);
    latency.remote_interp_rate = Lerp(latency.remote_interp_rate, target_remote_rate, follow);
}

float LengthSq(sf::Vector2f value)
{
    return value.x * value.x + value.y * value.y;
}

float Length(sf::Vector2f value)
{
    return std::sqrt(LengthSq(value));
}

sf::Vector2f Normalize(sf::Vector2f value)
{
    const float len_sq = LengthSq(value);
    if (len_sq <= 0.0001f) return {0.f, 0.f};
    return value / std::sqrt(len_sq);
}

sf::Vector2f PredictRenderPos(sf::Vector2f predicted_pos, sf::Vector2f prediction_error)
{
    return predicted_pos + prediction_error;
}

void AdvancePrediction(sf::Vector2f& predicted_pos, sf::Vector2f& predicted_vel, sf::Vector2f move_dir, float dt)
{
    if (LengthSq(move_dir) > 0.0001f)
    {
        const sf::Vector2f desired_vel = move_dir * game_config::default_max_velocity;
        const sf::Vector2f diff = desired_vel - predicted_vel;
        const float diff_len = Length(diff);
        const float max_accel = game_config::default_acceleration * dt;

        if (diff_len <= max_accel || diff_len <= 0.0001f)
            predicted_vel = desired_vel;
        else
            predicted_vel += diff / diff_len * max_accel;
    } else {
        const float damping = std::pow(0.9f, dt / server_tick_dt);
        predicted_vel *= damping;
        if (LengthSq(predicted_vel) <= 1e-5f) predicted_vel = {0.f, 0.f};
    }

    predicted_pos += predicted_vel * dt;
}

int8_t AxisToPacket(float value)
{
    return static_cast<int8_t>(std::clamp(std::round(value * 127.f), -127.f, 127.f));
}

bool TryConnect(connection_state& conn)
{
    conn.socket.disconnect();
    conn.receive_buffer.clear();
    conn.send_buffer.clear();
    conn.connected = false;
    conn.last_snapshot_sequence = 0;
    conn.player_entity_id = -1;
    conn.message = "Connecting...";

    const sf::Socket::Status status = conn.socket.connect(sf::IpAddress::LocalHost, game_config::default_port, sf::seconds(2));
    if (status != sf::Socket::Status::Done)
    {
        conn.message = "Offline preview: server not reachable";
        return false;
    }

    conn.socket.setBlocking(false);
    conn.connected = true;
    conn.message = "Connected to 127.0.0.1:" + std::to_string(game_config::default_port);
    return true;
}

void Disconnect(connection_state& conn, const std::string& message)
{
    conn.socket.disconnect();
    conn.receive_buffer.clear();
    conn.send_buffer.clear();
    conn.connected = false;
    conn.message = message;
    conn.player_entity_id = -1;
}

void QueueOperate(connection_state& conn, const ClientOperate& op)
{
    if (!conn.connected) return;

    std::array<uint8_t, 3> buffer{};
    const size_t len = ClientOperate::pack(op, buffer.data());
    if (len == 0) return;
    conn.send_buffer.insert(conn.send_buffer.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(len));
}

void FlushOutgoing(connection_state& conn)
{
    while (conn.connected && !conn.send_buffer.empty())
    {
        size_t sent = 0;
        const sf::Socket::Status status = conn.socket.send(conn.send_buffer.data(), conn.send_buffer.size(), sent);
        if (sent > 0)
            conn.send_buffer.erase(conn.send_buffer.begin(), conn.send_buffer.begin() + static_cast<std::ptrdiff_t>(sent));

        if (status == sf::Socket::Status::Done || status == sf::Socket::Status::Partial) continue;
        if (status == sf::Socket::Status::NotReady) return;

        Disconnect(conn, "Disconnected while sending");
    }
}

void SendMove(connection_state& conn, sf::Vector2f dir)
{
    ClientOperate op;
    op.type = ClientOperate::Type::Input;
    op.move_x = AxisToPacket(dir.x);
    op.move_y = AxisToPacket(dir.y);
    QueueOperate(conn, op);
}

void SendAttackDefend(connection_state& conn, bool attacking, bool defending)
{
    ClientOperate op;
    op.type = ClientOperate::Type::AttackDefend;
    op.isAttacking = attacking;
    op.isDefending = defending;
    QueueOperate(conn, op);
}

void SendEquip(connection_state& conn, EPetalType type, uint8_t slot)
{
    ClientOperate op;
    op.type = ClientOperate::Type::Equip;
    op.petal_type = static_cast<uint8_t>(type);
    op.slot_index = slot;
    op.rarity = static_cast<uint8_t>(ERarity::Common);
    QueueOperate(conn, op);
}

void SendUnequip(connection_state& conn, uint8_t slot)
{
    ClientOperate op;
    op.type = ClientOperate::Type::Unequip;
    op.slot_index = slot;
    QueueOperate(conn, op);
}

void ApplySnapshot(connection_state& conn, const ServerSnapshot& snapshot, std::unordered_map<int32_t, client_entity>& entities,
                   sf::Vector2f& predicted_pos, sf::Vector2f& predicted_vel, sf::Vector2f& prediction_error,
                   bool& has_prediction, latency_tool_state& latency, float now)
{
    if (latency.last_snapshot_time >= 0.f)
    {
        const float interval = std::max(0.f, now - latency.last_snapshot_time);
        latency.average_snapshot_interval =
            latency.average_snapshot_interval <= 0.f ? interval : Lerp(latency.average_snapshot_interval, interval, 0.12f);
    }
    if (latency.last_sequence != 0 && snapshot.sequence > latency.last_sequence + 1)
        latency.missed_snapshots += snapshot.sequence - latency.last_sequence - 1;
    latency.last_sequence = snapshot.sequence;
    latency.last_snapshot_time = now;
    latency.snapshot_age = 0.f;

    conn.last_snapshot_sequence = snapshot.sequence;
    conn.player_entity_id = snapshot.player_entity_id;

    std::unordered_set<int32_t> live_ids;
    live_ids.reserve(snapshot.entities.size());

    for (const ServerEntitySnapshot& item : snapshot.entities)
    {
        live_ids.insert(item.id);
        client_entity& entity = entities[item.id];
        entity.snapshot = item;
        if (!entity.initialized)
        {
            entity.render_pos = SnapshotPos(item);
            entity.initialized = true;
        }
    }

    for (auto it = entities.begin(); it != entities.end();)
    {
        if (live_ids.find(it->first) == live_ids.end())
            it = entities.erase(it);
        else
            ++it;
    }

    auto player_it = entities.find(snapshot.player_entity_id);
    if (player_it != entities.end())
    {
        const sf::Vector2f authoritative = SnapshotPos(player_it->second.snapshot);
        if (!has_prediction || !latency.prediction_enabled)
        {
            predicted_pos = authoritative;
            predicted_vel = {0.f, 0.f};
            prediction_error = {0.f, 0.f};
            has_prediction = true;
        } else {
            const sf::Vector2f rendered = PredictRenderPos(predicted_pos, prediction_error);
            const sf::Vector2f delta = authoritative - rendered;
            latency.authoritative_error = Length(delta);
            latency.effective_correction_gain =
                latency.auto_tune ? AutoCorrectionGain(latency, latency.authoritative_error) : latency.correction_gain;
            if (LengthSq(delta) > prediction_snap_distance * prediction_snap_distance)
            {
                predicted_pos = authoritative;
                predicted_vel = {0.f, 0.f};
                prediction_error = {0.f, 0.f};
            } else {
                prediction_error += delta * latency.effective_correction_gain;
            }
        }
        latency.correction_offset = Length(prediction_error);
        player_it->second.render_pos = PredictRenderPos(predicted_pos, prediction_error);
    }
}

void ProcessIncoming(connection_state& conn, std::unordered_map<int32_t, client_entity>& entities, sf::Vector2f& predicted_pos,
                     sf::Vector2f& predicted_vel, sf::Vector2f& prediction_error, bool& has_prediction,
                     latency_tool_state& latency, float now)
{
    if (conn.connected)
    {
        std::array<uint8_t, 2048> buffer{};
        while (true)
        {
            size_t received = 0;
            const sf::Socket::Status status = conn.socket.receive(buffer.data(), buffer.size(), received);
            if (status == sf::Socket::Status::Done && received > 0)
            {
                conn.receive_buffer.insert(conn.receive_buffer.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(received));
                continue;
            }
            if (status == sf::Socket::Status::Disconnected)
            {
                Disconnect(conn, "Disconnected from server");
            }
            break;
        }

        ServerSnapshot snapshot;
        while (TryPopServerSnapshotFrame(conn.receive_buffer, snapshot))
        {
            if (latency.artificial_snapshot_latency > 0.f)
            {
                delayed_snapshot delayed;
                delayed.release_time = now + latency.artificial_snapshot_latency;
                delayed.snapshot = std::move(snapshot);
                latency.pending_snapshots.push_back(std::move(delayed));
            } else {
                ApplySnapshot(conn, snapshot, entities, predicted_pos, predicted_vel, prediction_error, has_prediction, latency, now);
            }
        }
    }

    while (!latency.pending_snapshots.empty() &&
           (latency.artificial_snapshot_latency <= 0.f || latency.pending_snapshots.front().release_time <= now))
    {
        ApplySnapshot(conn, latency.pending_snapshots.front().snapshot, entities, predicted_pos, predicted_vel, prediction_error,
                      has_prediction, latency, now);
        latency.pending_snapshots.pop_front();
    }
}

bool LoadFont(sf::Font& font)
{
    return font.openFromFile("C:/Windows/Fonts/msyh.ttc") || font.openFromFile("C:/Windows/Fonts/consola.ttf") ||
           font.openFromFile("C:/Windows/Fonts/arial.ttf");
}

std::optional<uint8_t> SlotFromKey(sf::Keyboard::Key key)
{
    const int key_code = static_cast<int>(key);
    const int num1 = static_cast<int>(sf::Keyboard::Key::Num1);
    const int num5 = static_cast<int>(sf::Keyboard::Key::Num5);
    const int numpad1 = static_cast<int>(sf::Keyboard::Key::Numpad1);
    const int numpad5 = static_cast<int>(sf::Keyboard::Key::Numpad5);

    if (key_code >= num1 && key_code <= num5) return static_cast<uint8_t>(key_code - num1);
    if (key_code >= numpad1 && key_code <= numpad5) return static_cast<uint8_t>(key_code - numpad1);
    return std::nullopt;
}

void DrawGrid(sf::RenderWindow& window, const sf::Vector2f& camera)
{
    sf::VertexArray lines(sf::PrimitiveType::Lines);
    constexpr float spacing = 48.f;
    const sf::Vector2f center(window_width * 0.5f, window_height * 0.5f);
    const sf::Vector2f offset(std::fmod(-camera.x, spacing), std::fmod(-camera.y, spacing));

    for (float x = offset.x; x < window_width; x += spacing)
    {
        lines.append(sf::Vertex({x, 0.f}, sf::Color(224, 231, 238)));
        lines.append(sf::Vertex({x, static_cast<float>(window_height)}, sf::Color(224, 231, 238)));
    }
    for (float y = offset.y; y < window_height; y += spacing)
    {
        lines.append(sf::Vertex({0.f, y}, sf::Color(224, 231, 238)));
        lines.append(sf::Vertex({static_cast<float>(window_width), y}, sf::Color(224, 231, 238)));
    }

    sf::VertexArray axes(sf::PrimitiveType::Lines);
    const sf::Vector2f origin = center - camera;
    axes.append(sf::Vertex({origin.x, 0.f}, sf::Color(194, 204, 216)));
    axes.append(sf::Vertex({origin.x, static_cast<float>(window_height)}, sf::Color(194, 204, 216)));
    axes.append(sf::Vertex({0.f, origin.y}, sf::Color(194, 204, 216)));
    axes.append(sf::Vertex({static_cast<float>(window_width), origin.y}, sf::Color(194, 204, 216)));

    window.draw(lines);
    window.draw(axes);
}

void DrawText(sf::RenderWindow& window, const sf::Font* font, const std::string& text, sf::Vector2f pos, unsigned int size,
              sf::Color color)
{
    if (!font) return;
    sf::Text drawable(*font, text, size);
    drawable.setFillColor(color);
    drawable.setPosition(pos);
    window.draw(drawable);
}

sf::Vector2f WorldToScreen(sf::Vector2f world, sf::Vector2f camera)
{
    return world - camera + Vec2(window_width * 0.5f, window_height * 0.5f);
}

sf::Color EntityColor(const ServerEntitySnapshot& entity, bool local_player)
{
    if (local_player) return sf::Color(255, 224, 231);
    switch (entity.kind)
    {
    case NetEntityKind::Flower:
        return sf::Color(255, 212, 224);
    case NetEntityKind::Petal:
        return sf::Color(232, 196, 82);
    case NetEntityKind::Projectile:
        return sf::Color(134, 182, 255);
    case NetEntityKind::Mob:
        return sf::Color(229, 118, 86);
    default:
        return sf::Color(160, 170, 184);
    }
}

void DrawEntity(sf::RenderWindow& window, const ServerEntitySnapshot& snapshot, sf::Vector2f position, sf::Vector2f camera,
                bool local_player, bool local_attacking, bool local_defending)
{
    const sf::Vector2f screen = WorldToScreen(position, camera);
    const float radius = std::max(3.f, snapshot.radius);
    sf::CircleShape body(radius);
    body.setOrigin({radius, radius});
    body.setPosition(screen);
    body.setFillColor(EntityColor(snapshot, local_player));

    const bool attacking = local_player ? local_attacking : ((snapshot.flags & (1 << 0)) != 0);
    const bool defending = local_player ? local_defending : ((snapshot.flags & (1 << 1)) != 0);
    if (snapshot.kind == NetEntityKind::Flower)
    {
        body.setFillColor(attacking ? sf::Color(255, 179, 181) : EntityColor(snapshot, local_player));
        body.setOutlineColor(defending ? sf::Color(70, 122, 205) : sf::Color(190, 94, 122));
        body.setOutlineThickness(defending ? 5.f : 3.f);
    } else {
        body.setOutlineColor(sf::Color(100, 112, 128));
        body.setOutlineThickness(1.5f);
    }

    window.draw(body);

    if (local_player)
    {
        sf::CircleShape marker(radius + 8.f);
        marker.setOrigin({radius + 8.f, radius + 8.f});
        marker.setPosition(screen);
        marker.setFillColor(sf::Color::Transparent);
        marker.setOutlineColor(sf::Color(54, 116, 220));
        marker.setOutlineThickness(2.f);
        window.draw(marker);
    }

    if (snapshot.health > 0.f && snapshot.kind != NetEntityKind::Petal && snapshot.kind != NetEntityKind::Projectile)
    {
        const float bar_width = radius * 2.f;
        sf::RectangleShape back({bar_width, 4.f});
        back.setPosition({screen.x - radius, screen.y - radius - 10.f});
        back.setFillColor(sf::Color(150, 158, 170));
        window.draw(back);

        sf::RectangleShape front({bar_width, 4.f});
        front.setPosition(back.getPosition());
        front.setFillColor(sf::Color(70, 170, 102));
        window.draw(front);
    }
}

void DrawOfflinePreview(sf::RenderWindow& window, const std::vector<std::optional<visual_petal>>& slots, sf::Vector2f camera,
                        bool attacking, bool defending, float orbit_angle)
{
    const sf::Vector2f screen_center = WorldToScreen(camera, camera);
    const float orbit = orbit_radius + (attacking ? 34.f : 0.f) - (defending ? 16.f : 0.f);

    int petal_count = 0;
    for (const auto& slot : slots)
    {
        if (slot) ++petal_count;
    }

    int petal_index = 0;
    for (const auto& slot : slots)
    {
        if (!slot) continue;
        const float angle = orbit_angle + (2.f * pi * petal_index) / static_cast<float>(std::max(1, petal_count));
        const sf::Vector2f petal_pos = screen_center + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit;
        sf::CircleShape petal(slot->radius);
        petal.setOrigin({slot->radius, slot->radius});
        petal.setPosition(petal_pos);
        petal.setFillColor(slot->color);
        petal.setOutlineColor(sf::Color(114, 124, 138));
        petal.setOutlineThickness(1.5f);
        window.draw(petal);
        ++petal_index;
    }

    sf::CircleShape flower(flower_radius);
    flower.setOrigin({flower_radius, flower_radius});
    flower.setPosition(screen_center);
    flower.setFillColor(attacking ? sf::Color(255, 179, 181) : sf::Color(255, 224, 231));
    flower.setOutlineColor(defending ? sf::Color(70, 122, 205) : sf::Color(190, 94, 122));
    flower.setOutlineThickness(defending ? 5.f : 3.f);
    window.draw(flower);
}

void DrawPredictedPetals(sf::RenderWindow& window, const std::vector<std::optional<visual_petal>>& slots, sf::Vector2f owner_pos,
                         sf::Vector2f camera, bool attacking, bool defending, float orbit_angle)
{
    const sf::Vector2f owner_screen = WorldToScreen(owner_pos, camera);
    const float orbit = orbit_radius + (attacking ? 34.f : 0.f) - (defending ? 16.f : 0.f);

    int petal_count = 0;
    for (const auto& slot : slots)
    {
        if (slot) ++petal_count;
    }
    if (petal_count <= 0) return;

    int petal_index = 0;
    for (const auto& slot : slots)
    {
        if (!slot) continue;
        const float angle = orbit_angle + (2.f * pi * petal_index) / static_cast<float>(petal_count);
        const sf::Vector2f petal_pos = owner_screen + sf::Vector2f(std::cos(angle), std::sin(angle)) * orbit;
        sf::CircleShape petal(slot->radius);
        petal.setOrigin({slot->radius, slot->radius});
        petal.setPosition(petal_pos);
        petal.setFillColor(sf::Color(slot->color.r, slot->color.g, slot->color.b, 180));
        petal.setOutlineColor(sf::Color(78, 108, 150));
        petal.setOutlineThickness(1.5f);
        window.draw(petal);
        ++petal_index;
    }
}

bool HasVisibleServerPetalNearPlayer(const std::unordered_map<int32_t, client_entity>& entities, int32_t player_id)
{
    const auto player_it = entities.find(player_id);
    if (player_it == entities.end()) return false;

    const sf::Vector2f player_pos = player_it->second.render_pos;
    for (const auto& [id, entity] : entities)
    {
        (void)id;
        if (entity.snapshot.kind != NetEntityKind::Petal && entity.snapshot.kind != NetEntityKind::Projectile) continue;
        const sf::Vector2f diff = entity.render_pos - player_pos;
        if (diff.x * diff.x + diff.y * diff.y <= 220.f * 220.f) return true;
    }
    return false;
}

void DrawLatencyTool(sf::RenderWindow& window, const sf::Font* font, const latency_tool_state& latency)
{
    if (!latency.visible) return;

    sf::RectangleShape panel({380.f, 222.f});
    panel.setPosition({static_cast<float>(window_width) - 380.f, 18.f});
    panel.setFillColor(sf::Color(248, 251, 253, 228));
    panel.setOutlineColor(sf::Color(142, 160, 184));
    panel.setOutlineThickness(1.5f);
    window.draw(panel);

    const sf::Vector2f pos(static_cast<float>(window_width) - 364.f, 30.f);
    const float line = 19.f;
    const float snapshot_hz = latency.average_snapshot_interval > 0.f ? 1.f / latency.average_snapshot_interval : 0.f;

    DrawText(window, font,
             "Latency tool  F2 auto " + OnOff(latency.auto_tune) + "  F3 hide  F4 pred " +
                 OnOff(latency.prediction_enabled),
             pos, 14,
             sf::Color(36, 50, 68));
    DrawText(window, font,
             "Sim snapshot latency: " + FormatFloat(latency.artificial_snapshot_latency * 1000.f, 0) + " ms  F5/F6",
             pos + Vec2(0.f, line), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Correction: eff " + FormatFloat(latency.effective_correction_gain, 3) + "  manual " +
                 FormatFloat(latency.correction_gain, 2) + "  F7/F8",
             pos + Vec2(0.f, line * 2.f), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Deadzone: " + FormatFloat(latency.correction_deadzone, 1) + "  rate: " + FormatFloat(latency.correction_rate, 1),
             pos + Vec2(0.f, line * 3.f), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Camera follow: " + FormatFloat(latency.camera_follow_rate, 1) + "  F9/F10", pos + Vec2(0.f, line * 4.f), 13,
             sf::Color(58, 72, 90));
    DrawText(window, font,
             "Remote interp: " + FormatFloat(latency.remote_interp_rate, 1) + "  F11/F12", pos + Vec2(0.f, line * 5.f), 13,
             sf::Color(58, 72, 90));
    DrawText(window, font,
             "Snapshot: " + FormatFloat(snapshot_hz, 1) + " hz  age " + FormatFloat(latency.snapshot_age * 1000.f, 0) +
                 " ms  missed " + std::to_string(latency.missed_snapshots),
             pos + Vec2(0.f, line * 6.f), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Auth error: " + FormatFloat(latency.authoritative_error, 1) + "  correction " +
                 FormatFloat(latency.correction_offset, 1),
             pos + Vec2(0.f, line * 7.f), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Speed: " + FormatFloat(latency.predicted_speed, 1) + "  camera lag " + FormatFloat(latency.camera_error, 1) +
                 "  queued " + std::to_string(latency.pending_snapshots.size()),
             pos + Vec2(0.f, line * 8.f), 13, sf::Color(58, 72, 90));
    DrawText(window, font,
             "Auto delay: " + FormatFloat(latency.estimated_compensation_delay * 1000.f, 0) +
                 " ms  F5/F6 sim net",
             pos + Vec2(0.f, line * 9.f), 12, sf::Color(88, 102, 120));
    DrawText(window, font, "Auto ignores tiny errors, reconciles only drift", pos + Vec2(0.f, line * 10.f), 12,
             sf::Color(88, 102, 120));
}
} // namespace

int main()
{
    sf::RenderWindow window(sf::VideoMode({window_width, window_height}), "FlorrBt Client");
    window.setVerticalSyncEnabled(true);

    sf::Font font;
    const sf::Font* font_ptr = LoadFont(font) ? &font : nullptr;

    connection_state conn;
    TryConnect(conn);

    std::unordered_map<int32_t, client_entity> entities;
    sf::Vector2f predicted_pos(0.f, 0.f);
    sf::Vector2f predicted_vel(0.f, 0.f);
    sf::Vector2f prediction_error(0.f, 0.f);
    sf::Vector2f camera_pos(0.f, 0.f);
    latency_tool_state latency;
    bool has_prediction = false;
    bool has_camera = false;
    sf::Vector2f move_dir(0.f, 0.f);
    sf::Vector2f last_sent_dir(99.f, 99.f);
    bool attacking = false;
    bool defending = false;
    bool last_sent_attack = false;
    bool last_sent_defend = false;
    uint8_t selected_slot = 0;
    float orbit_angle = 0.f;
    float send_timer = packet_interval;

    std::vector<std::optional<visual_petal>> slots(5);
    slots[0] = visual_petal{sf::Color(232, 196, 82), 10.f};

    sf::Clock clock;
    float now = 0.f;
    while (window.isOpen())
    {
        const float dt = std::min(clock.restart().asSeconds(), 0.05f);
        now += dt;
        latency.snapshot_age = latency.last_snapshot_time >= 0.f ? now - latency.last_snapshot_time : 0.f;

        while (const std::optional event = window.pollEvent())
        {
            if (event->is<sf::Event::Closed>()) window.close();

            if (const auto* key = event->getIf<sf::Event::KeyPressed>())
            {
                if (key->code == sf::Keyboard::Key::Escape) window.close();
                if (key->code == sf::Keyboard::Key::R)
                {
                    entities.clear();
                    predicted_vel = {0.f, 0.f};
                    prediction_error = {0.f, 0.f};
                    has_camera = false;
                    has_prediction = false;
                    latency.pending_snapshots.clear();
                    TryConnect(conn);
                }

                if (key->code == sf::Keyboard::Key::F2) latency.auto_tune = !latency.auto_tune;
                if (key->code == sf::Keyboard::Key::F3) latency.visible = !latency.visible;
                if (key->code == sf::Keyboard::Key::F4)
                {
                    latency.prediction_enabled = !latency.prediction_enabled;
                    predicted_vel = {0.f, 0.f};
                    prediction_error = {0.f, 0.f};
                }
                if (key->code == sf::Keyboard::Key::F5)
                    latency.artificial_snapshot_latency = std::clamp(latency.artificial_snapshot_latency + 0.025f, 0.f, 0.25f);
                if (key->code == sf::Keyboard::Key::F6)
                    latency.artificial_snapshot_latency = std::clamp(latency.artificial_snapshot_latency - 0.025f, 0.f, 0.25f);
                if (key->code == sf::Keyboard::Key::F7)
                    latency.correction_gain = std::clamp(latency.correction_gain + 0.02f, 0.f, 0.5f);
                if (key->code == sf::Keyboard::Key::F8)
                    latency.correction_gain = std::clamp(latency.correction_gain - 0.02f, 0.f, 0.5f);
                if (key->code == sf::Keyboard::Key::F9)
                    latency.camera_follow_rate = std::clamp(latency.camera_follow_rate + 2.f, 2.f, 40.f);
                if (key->code == sf::Keyboard::Key::F10)
                    latency.camera_follow_rate = std::clamp(latency.camera_follow_rate - 2.f, 2.f, 40.f);
                if (key->code == sf::Keyboard::Key::F11)
                    latency.remote_interp_rate = std::clamp(latency.remote_interp_rate + 2.f, 2.f, 40.f);
                if (key->code == sf::Keyboard::Key::F12)
                    latency.remote_interp_rate = std::clamp(latency.remote_interp_rate - 2.f, 2.f, 40.f);

                if (std::optional<uint8_t> slot = SlotFromKey(key->code)) selected_slot = *slot;

                if (key->code == sf::Keyboard::Key::Z)
                {
                    slots[selected_slot] = visual_petal{sf::Color(175, 212, 255), 8.f};
                    SendEquip(conn, EPetalType::Air, selected_slot);
                }
                if (key->code == sf::Keyboard::Key::X)
                {
                    slots[selected_slot] = visual_petal{sf::Color(184, 151, 112), 8.f};
                    SendEquip(conn, EPetalType::Dust, selected_slot);
                }
                if (key->code == sf::Keyboard::Key::C)
                {
                    slots[selected_slot] = visual_petal{sf::Color(232, 196, 82), 10.f};
                    SendEquip(conn, EPetalType::GoldenLeaf, selected_slot);
                }
                if (key->code == sf::Keyboard::Key::Backspace)
                {
                    slots[selected_slot] = std::nullopt;
                    SendUnequip(conn, selected_slot);
                }
            }
        }

        sf::Vector2f raw_dir(0.f, 0.f);
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left))
            raw_dir.x -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right))
            raw_dir.x += 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up))
            raw_dir.y -= 1.f;
        if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) || sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down))
            raw_dir.y += 1.f;

        move_dir = Normalize(raw_dir);
        attacking = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
        defending = sf::Mouse::isButtonPressed(sf::Mouse::Button::Right);

        AutoTuneLatency(latency, dt);
        if (has_prediction && latency.prediction_enabled)
        {
            AdvancePrediction(predicted_pos, predicted_vel, move_dir, dt);
            prediction_error = Lerp(prediction_error, {0.f, 0.f}, SmoothFactor(latency.correction_rate, dt));
        } else {
            predicted_vel = {0.f, 0.f};
        }
        latency.predicted_speed = Length(predicted_vel);
        orbit_angle += dt * (attacking ? 3.5f : 1.8f);

        send_timer += dt;
        const bool move_changed = AxisToPacket(move_dir.x) != AxisToPacket(last_sent_dir.x) ||
                                  AxisToPacket(move_dir.y) != AxisToPacket(last_sent_dir.y);
        if (conn.connected && (send_timer >= packet_interval || move_changed))
        {
            SendMove(conn, move_dir);
            last_sent_dir = move_dir;
            send_timer = 0.f;
        }
        if (attacking != last_sent_attack || defending != last_sent_defend)
        {
            SendAttackDefend(conn, attacking, defending);
            last_sent_attack = attacking;
            last_sent_defend = defending;
        }

        FlushOutgoing(conn);
        ProcessIncoming(conn, entities, predicted_pos, predicted_vel, prediction_error, has_prediction, latency, now);

        const sf::Vector2f local_render_pos = PredictRenderPos(predicted_pos, prediction_error);
        for (auto& [id, entity] : entities)
        {
            if (id == conn.player_entity_id && has_prediction)
            {
                entity.render_pos = local_render_pos;
            } else {
                entity.render_pos = Lerp(entity.render_pos, SnapshotPos(entity.snapshot), SmoothFactor(latency.remote_interp_rate, dt));
            }
        }

        sf::Vector2f camera(0.f, 0.f);
        if (!has_prediction)
        {
            camera_pos = {0.f, 0.f};
            has_camera = false;
        } else if (!has_camera)
        {
            camera_pos = predicted_pos;
            has_camera = true;
            camera = camera_pos;
        } else if (LengthSq(predicted_pos - camera_pos) > prediction_snap_distance * prediction_snap_distance) {
            camera_pos = predicted_pos;
            camera = camera_pos;
        } else {
            camera_pos = Lerp(camera_pos, predicted_pos, SmoothFactor(latency.camera_follow_rate, dt));
            camera = camera_pos;
        }
        latency.camera_error = has_prediction ? Length(predicted_pos - camera_pos) : 0.f;
        window.clear(sf::Color(238, 243, 247));
        DrawGrid(window, camera);

        if (entities.empty())
        {
            DrawOfflinePreview(window, slots, camera, attacking, defending, orbit_angle);
        } else {
            for (const auto& [id, entity] : entities)
            {
                if (id == conn.player_entity_id) continue;
                if (entity.snapshot.kind == NetEntityKind::Petal || entity.snapshot.kind == NetEntityKind::Projectile)
                    continue;
                DrawEntity(window, entity.snapshot, entity.render_pos, camera, id == conn.player_entity_id, attacking, defending);
            }
            for (const auto& [id, entity] : entities)
            {
                if (entity.snapshot.kind != NetEntityKind::Petal && entity.snapshot.kind != NetEntityKind::Projectile)
                    continue;
                DrawEntity(window, entity.snapshot, entity.render_pos, camera, id == conn.player_entity_id, attacking, defending);
            }
            const auto player_it = entities.find(conn.player_entity_id);
            if (player_it != entities.end())
            {
                if (!HasVisibleServerPetalNearPlayer(entities, conn.player_entity_id))
                    DrawPredictedPetals(window, slots, player_it->second.render_pos, camera, attacking, defending, orbit_angle);
                DrawEntity(window, player_it->second.snapshot, player_it->second.render_pos, camera, true, attacking, defending);
            }
        }

        const sf::Color status_color = conn.connected ? sf::Color(35, 112, 72) : sf::Color(172, 75, 52);
        DrawText(window, font_ptr, conn.message, {18.f, 14.f}, 17, status_color);
        DrawText(window, font_ptr, "WASD/Arrows move  |  LMB attack  RMB defend  |  R reconnect", {18.f, 42.f}, 15,
                 sf::Color(62, 72, 84));
        DrawText(window, font_ptr, "1-5 select slot  |  Z Air  X Dust  C GoldenLeaf  |  Backspace unequip", {18.f, 66.f}, 15,
                 sf::Color(62, 72, 84));
        DrawText(window, font_ptr, "Snapshot #" + std::to_string(conn.last_snapshot_sequence) + "  Entity " +
                                      std::to_string(conn.player_entity_id),
                 {18.f, 90.f}, 14, sf::Color(92, 104, 118));
        DrawText(window, font_ptr, "F3 latency tool", {18.f, 114.f}, 13, sf::Color(92, 104, 118));
        DrawLatencyTool(window, font_ptr, latency);

        for (size_t i = 0; i < slots.size(); ++i)
        {
            const sf::Vector2f slot_pos(18.f + static_cast<float>(i) * 42.f, static_cast<float>(window_height) - 48.f);
            sf::RectangleShape box({32.f, 32.f});
            box.setPosition(slot_pos);
            box.setFillColor(i == selected_slot ? sf::Color(221, 234, 255) : sf::Color(255, 255, 255));
            box.setOutlineColor(i == selected_slot ? sf::Color(70, 122, 205) : sf::Color(170, 181, 194));
            box.setOutlineThickness(2.f);
            window.draw(box);
            if (slots[i])
            {
                sf::CircleShape dot(8.f);
                dot.setOrigin({8.f, 8.f});
                dot.setPosition(slot_pos + sf::Vector2f(16.f, 16.f));
                dot.setFillColor(slots[i]->color);
                window.draw(dot);
            }
        }

        window.display();
    }

    if (conn.connected)
    {
        SendMove(conn, {0.f, 0.f});
        SendAttackDefend(conn, false, false);
        FlushOutgoing(conn);
        conn.socket.disconnect();
    }

    return 0;
}
