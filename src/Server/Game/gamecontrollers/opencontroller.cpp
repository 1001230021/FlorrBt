#include "opencontroller.h"
#include "../player.h"
#include "../gameworld.h"
#include "../entities/flower.h"
#include "../../../Shared/game_config.h"

void COpenController::OnTick(CGameWorld& world, float dt)
{
    if (m_count <= 0)
    {
        auto mob = CreateMob(EMobType::NormalLadybug, &world, {game_config::open_spawn_x, game_config::open_spawn_y},
                             ERarity::Common);
        if (!mob) return;

        world.InsertEntity(std::move(mob));
        m_count = game_config::open_spawn_interval;
    } else {
        m_count -= dt;
    }
}

void COpenController::OnPlayerConnect(CGameWorld& world, CPlayer* player)
{
    if (!player) return;

    auto entity = CreateMob(EMobType::PlayerFlower, &world,
                            {game_config::player_respawn_x, game_config::player_respawn_y}, ERarity::Common);
    auto* raw_flower = dynamic_cast<CPlayerFlower*>(entity.get());
    if (!raw_flower) return;

    raw_flower->m_name = player->GetName();
    raw_flower->m_level = 1;
    raw_flower = dynamic_cast<CPlayerFlower*>(world.InsertEntity(std::move(entity)));
    if (raw_flower) player->SetOwnedEntity(raw_flower);
}
