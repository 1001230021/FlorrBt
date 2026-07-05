#include "opencontroller.h"
#include "../player.h"
#include "../gameworld.h"
#include "../entities/flower.h"

void COpenController::OnTick(CGameWorld& world, float dt)
{
    if (m_count <= 0)
    {
        auto mob = CreateMob(EMobType::NormalLadybug, &world, {500.f, 0.f}, ERarity::Common);
        world.InsertEntity(std::move(mob));
        m_count = 9999.9f;
    } else {
        m_count -= dt;
    }
}

void COpenController::OnPlayerConnect(CGameWorld& world, CPlayer* player)
{
    auto entity = CreateMob(EMobType::PlayerFlower, &world, {0.f, 0.f}, ERarity::Common);
    auto* raw_flower = dynamic_cast<CFlower*>(entity.get());
    if (!player || !raw_flower) return;

    auto flower = std::unique_ptr<CFlower>(static_cast<CFlower*>(entity.release()));
    raw_flower = static_cast<CFlower*>(world.InsertEntity(std::move(flower)));
    if (raw_flower) player->SetOwnedEntity(raw_flower);
}
