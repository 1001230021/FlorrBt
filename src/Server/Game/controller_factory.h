#pragma once

#include "controllers/melee_controller.h"
#include "controllers/player_controller.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>

struct SControllerFactory
{
    std::string_view key;
    std::unique_ptr<IController> (*create)();
};

template <class T> SControllerFactory SnapshotControllerFactory()
{
    // The key is defined by the controller itself, never repeated here.
    return { T{}.SnapshotKey(), []() -> std::unique_ptr<IController> { return std::make_unique<T>(); } };
}

inline const auto& SnapshotControllerFactories()
{
    static const auto factories = [] {
        std::array<SControllerFactory, 18> result{{
            SnapshotControllerFactory<CPlayerController>(),
            SnapshotControllerFactory<CMeleeController>(),
            SnapshotControllerFactory<CLeafcutterSoldierController>(),
            SnapshotControllerFactory<CSummonedMeleeController>(),
            SnapshotControllerFactory<CNeutralMeleeController>(),
            SnapshotControllerFactory<CFireflyController>(),
            SnapshotControllerFactory<CFireflyLarvaController>(),
            SnapshotControllerFactory<CTermiteOvermindController>(),
            SnapshotControllerFactory<CRandomWanderController>(),
            SnapshotControllerFactory<CQueenAntController>(),
            SnapshotControllerFactory<CSpiderController>(),
            SnapshotControllerFactory<CCrabController>(),
            SnapshotControllerFactory<CHornetRangedController>(),
            SnapshotControllerFactory<CJellyfishController>(),
            SnapshotControllerFactory<CMechaFlowerRangedController>(),
            SnapshotControllerFactory<CSpecialHornetController>(),
            SnapshotControllerFactory<CBumbleBeeController>(),
            SnapshotControllerFactory<CRockAttackOnDamagedController>(),
        }};
        for (size_t i = 0; i < result.size(); ++i)
            for (size_t j = 0; j < i; ++j)
                if (result[i].key == result[j].key) throw std::logic_error("Duplicate controller snapshot key");
        return result;
    }();
    return factories;
}

inline std::unique_ptr<IController> CreateSnapshotController(std::string_view key)
{
    for (const auto& factory : SnapshotControllerFactories())
        if (factory.key == key) return factory.create();
    return {};
}
