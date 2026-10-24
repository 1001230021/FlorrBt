#include "test_framework.h"
#include "Server/Game/entity.h"
#include "Shared/mod_petal_type.h"
#include "Shared/petal_type.h"
#include <unordered_set>

TEST(ControllerPetalsHaveSeparateManifestAndStableWireIds)
{
    CHECK(IsModPetalTypeId(static_cast<int>(EModPetalType::Magnet)));
    CHECK(IsModPetalTypeId(static_cast<int>(EModPetalType::MagicCactus)));
    CHECK(GetPetalTypeName(ToPetalType(EModPetalType::Linker)) == "Linker");
    CHECK(GetPetalTypeName(ToPetalType(EModPetalType::MagicLightning)) == "MagicLightning");
    CHECK(!IsKnownPetalTypeId(static_cast<int>(EPetalType::None)));
    CHECK(static_cast<int>(ToPetalType(EModPetalType::Magnet)) == 71);
    CHECK(static_cast<int>(ToPetalType(EModPetalType::MagicCactus)) == 80);
}

TEST(EntityHandlesDoNotAliasReusedIds)
{
    const SEntityHandle first{ 7, 11, 2, 99 };
    const SEntityHandle replacement{ 7, 12, 2, 99 };
    const SEntityHandle other_world{ 7, 11, 3, 100 };

    CHECK(first == first);
    CHECK(!(first == replacement));
    CHECK(!(first == other_world));

    std::unordered_set<SEntityHandle, SEntityHandleHash> handles;
    handles.insert(first);
    handles.insert(replacement);
    handles.insert(other_world);
    CHECK(handles.size() == 3);
}

TEST(QualifiedEntityHandlesRejectStaleGenerationsAndWorlds)
{
    const SEntityHandle live{ 4, 21, 8, 300 };
    const SEntityHandle reused_id{ 4, 22, 8, 300 };
    const SEntityHandle recreated_world{ 4, 21, 8, 301 };
    const SEntityHandle other_world{ 4, 21, 9, 400 };

    CHECK(live.RefersTo(live));
    CHECK(!live.RefersTo(reused_id));
    CHECK(!live.RefersTo(recreated_world));
    CHECK(!live.RefersTo(other_world));
}
