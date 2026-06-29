#include <iostream>
#include <cmath>
#include "Game/gameworld.h"
#include "Game/entities/flower.h"
#include "Game/entities/petals/petals_behavior.h"
#include "../Shared/shared.h"


int main()
{
    RegisterGoldenLeaf();

    CGameWorld world;
    CFlower flower(&world, 0.0f, 0.0f, 20.0f, ERarity::Common, SFlowerStats{});

    const auto* protoLegend = g_PetalRegistry[EPetalType::GoldenLeaf].get();
    const auto* protoCommon = protoLegend;

    // 装备 Legendary GoldenLeaf 到槽位 0
    std::cout << "Equipping Legendary GoldenLeaf to slot 0...\n";
    flower.EquipPetal(0, protoLegend, ERarity::Legendary);

    // 等待 preload 冷却结束，花瓣生成
    for (int i = 0; i < 250; ++i) flower.Tick(0.016f);

    auto& slot0 = flower.GetSlots()[0];
    std::cout << "Slot 0 Available: " << slot0.m_Available << "\n";
    std::cout << "Slot 0 Petal count: " << slot0.m_pPetals.size() << "\n";
    if (!slot0.m_pPetals.empty() && slot0.m_pPetals[0])
        std::cout << "Slot 0 Petal pos: (" << slot0.m_pPetals[0]->m_Pos.x << ", " << slot0.m_pPetals[0]->m_Pos.y << ")\n";

    // 装备 Common GoldenLeaf 到槽位 1
    std::cout << "\nEquipping Common GoldenLeaf to slot 1...\n";
    flower.EquipPetal(1, protoCommon, ERarity::Common);

    for (int i = 0; i < 10; ++i) flower.Tick(0.016f);

    auto& slot1 = flower.GetSlots()[1];
    std::cout << "Slot 0 Available: " << slot0.m_Available << " (expected 1)\n";
    std::cout << "Slot 1 Available: " << slot1.m_Available << " (expected 0)\n";

    // 卸下槽位 0
    std::cout << "\nUnequipping slot 0...\n";
    flower.UnequipPetal(0);

    // 等待足够帧数，让槽位 1 冷却完成并生成花瓣
    std::cout << "Waiting for cooldown...\n";
    for (int i = 0; i < 250; ++i) flower.Tick(0.016f);

    std::cout << "Slot 1 Available: " << slot1.m_Available << " (expected 1)\n";
    std::cout << "Slot 1 Petal count: " << slot1.m_pPetals.size() << "\n";
    if (!slot1.m_pPetals.empty() && slot1.m_pPetals[0])
    {
        std::cout << "Slot 1 Petal pos: (" << slot1.m_pPetals[0]->m_Pos.x << ", " << slot1.m_pPetals[0]->m_Pos.y << ")\n";
        std::cout << "✓ Slot 1 petal successfully spawned!\n";
    }
    else
        std::cout << "✗ Slot 1 Petal still not spawned.\n";

    std::cout << "\nAll exclusivity tests done.\n";
    return 0;
}
