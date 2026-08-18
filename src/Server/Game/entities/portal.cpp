#include "portal.h"
#include "../../../Shared/game_config.h"
#include "../../../Shared/tools.h"
#include "../../server.h"
#include "../gamecontext.h"
#include "../gameworld.h"
#include "../player.h"
#include "../states/states.h"
#include "flower.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <vector>

CPortal::CPortal(CGameWorld* world, sf::Vector2f pos, float radius, std::string target_world)
    : CEntity(world, pos.x, pos.y, std::max(game_config::portal_min_radius, radius),
              MakeEntityType(EEntityType::Portal, server_portal_entity_type)),
      m_target_world_name(std::move(target_world))
{
    Health() = std::numeric_limits<float>::infinity();
    Mass() = 0.f;
    Team() = 0;
    HasFacing() = true;
}

void CPortal::Tick(float dt)
{
    FacingAngle() += dt * game_config::portal_rotation_speed;
    if (FacingAngle() > game_config::pi * 2.f) FacingAngle() -= game_config::pi * 2.f;

    CGameContext* context = GameContext();
    if (!context) return;

    CGameWorld* world = GameWorld();
    for (const auto& player : context->Players())
    {
        if (!player || !player->IsConnected() || !player->IsAuthenticated()) continue;
        CEntity* entity = player->GetEntity();
        if (!entity || entity->GameWorld() != world) continue;
        AttractAndTransferPlayer(*player, dt);
    }
}

void CPortal::AttractAndTransferPlayer(CPlayer& player, float dt)
{
    CEntity* entity = player.GetEntity();
    if (!entity || entity->GameWorld() != GameWorld() || entity->IsMarkedForDes() || entity->IsDead()) return;

    auto* flower = dynamic_cast<CPlayerFlower*>(entity);
    if (!flower) return;
    if (flower->HasState<CInvincibleState>()) return;

    sf::Vector2f delta = Pos() - flower->Pos();
    float dist_sq = LengthSq(delta);
    float attract_radius = Radius() * game_config::portal_attraction_radius_multiplier;
    if (dist_sq > attract_radius * attract_radius) return;

    CServer* server = CServer::GetInstance();
    if (!server) return;
    std::vector<CGameWorld*> target_worlds = server->FindWorldsByMapName(m_target_world_name);
    CGameWorld* current_world = GameWorld();
    target_worlds.erase(std::remove(target_worlds.begin(), target_worlds.end(), current_world), target_worlds.end());
    if (target_worlds.empty()) return;

    float dist = std::sqrt(std::max(0.f, dist_sq));
    if (dist <= Radius() * game_config::portal_transfer_radius_multiplier)
    {
        std::uniform_int_distribution<size_t> target_dist(0, target_worlds.size() - 1);
        CGameWorld* target_world = target_worlds[target_dist(GetRng())];
        if (target_world && GameWorld())
            GameWorld()->TransferPlayerEntityToWorld(player, *target_world, m_from_point_name);
        return;
    }

    if (dist <= game_config::entity_collision_epsilon) return;
    sf::Vector2f dir = delta / dist;
    float t = std::clamp(1.f - dist / attract_radius, 0.f, 1.f);
    float acceleration = game_config::portal_attraction_acceleration * t * t;
    flower->m_vel += dir * acceleration * dt;
}
