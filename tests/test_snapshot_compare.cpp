#include "test_framework.h"
#include "Shared/network_msg.h"

TEST(SlotCounterRoundTripsAndParticipatesInDeltaComparison)
{
    SOwnerPetalSlot slot;
    slot.petal_type = 65;
    slot.rarity = 6;
    slot.visual_type = SOwnerPetalSlot::EVisualType::Counter;
    slot.copies.push_back({ SOwnerPetalSlot::ECopyState::Alive, 255, 0 });
    slot.copies.push_back({ SOwnerPetalSlot::ECopyState::Alive, 128, UINT16_MAX });
    slot.copies.push_back({ SOwnerPetalSlot::ECopyState::Loading, 50, 0 });
    std::vector<uint8_t> bytes(GetEntityOwnerPetalSlotPackedSize(slot));
    size_t offset = 0;
    PackEntityOwnerPetalSlot(slot, bytes.data(), offset);
    CHECK(offset == bytes.size());
    SOwnerPetalSlot parsed;
    offset = 0;
    CHECK(ParseEntityOwnerPetalSlot(bytes.data(), bytes.size(), offset, parsed));
    CHECK(offset == bytes.size());
    CHECK(snap_eq::OwnerSlots({ slot }, { parsed }));
    parsed.copies[0].visual_value = 1;
    CHECK(!snap_eq::OwnerSlots({ slot }, { parsed }));
    offset = 0;
    CHECK(!ParseEntityOwnerPetalSlot(bytes.data(), bytes.size() - 1, offset, parsed));
}

// Guards the macro-generated snapshot comparison: SameServerEntitySnap is
// generated from PER_SNAPSHOT_FIELD, the same table that declares
// ServerEntitySnap. These tests exist to catch a regression where the
// generation breaks, or where a comparison silently stops covering a field.

namespace
{
ServerEntitySnap SampleSnap()
{
    ServerEntitySnap s;
    s.entity_id = 42;
    s.entity_type = 7;
    s.team = 3;
    s.pos = { 100.f, -200.f };
    s.radius = 5.5f;
    s.hp_percent = 0.75f;
    s.shield_percent = 0.25f;
    s.flags = 0x1234;
    s.angle = -1234;
    s.rarity = 9;
    s.name = "player";

    SOwnerPetalSlot slot;
    slot.petal_type = 1;
    slot.rarity = 2;
    slot.visual_type = SOwnerPetalSlot::EVisualType::Angle;
    SOwnerPetalSlot::SCopy copy;
    copy.state = SOwnerPetalSlot::ECopyState::Alive;
    copy.progress = 128;
    copy.visual_value = 300;
    slot.copies.push_back(copy);
    s.primary_slots.push_back(slot);

    SEntityStateSnap st;
    st.type = EStateType::Poison;
    st.rarity = 3;
    st.value = 4;
    s.states.push_back(st);

    std::array<float, entity_hallucination_parameter_count> hallucination{};
    hallucination.fill(0.5f);
    s.hallucinations.push_back(hallucination);
    return s;
}
} // namespace

TEST(SnapshotCompareDefaultsEqual)
{
    CHECK(SameServerEntitySnap(ServerEntitySnap{}, ServerEntitySnap{}));
}

TEST(SnapshotCompareEqualContentIsEqual)
{
    const ServerEntitySnap a = SampleSnap();
    CHECK(SameServerEntitySnap(a, SampleSnap()));
}

TEST(SnapshotCompareDetectsScalarDifferences)
{
    const ServerEntitySnap base = SampleSnap();

    auto mutated = base;
    mutated.entity_id = 1;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.entity_type = 8;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.team = 9;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.pos.x += 1.f;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.pos.y += 1.f;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.radius += 1.f;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.hp_percent += 0.01f;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.shield_percent += 0.01f;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.flags = 0x4321;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.angle = 1234;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.rarity = 11;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.name = "other";
    CHECK(!SameServerEntitySnap(base, mutated));
}

TEST(SnapshotCompareDetectsArrayDifferences)
{
    const ServerEntitySnap base = SampleSnap();

    auto mutated = base;
    mutated.primary_slots.push_back(base.primary_slots.front());
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.primary_slots[0].petal_type = 77;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.primary_slots[0].copies[0].progress = 200;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.states[0].value = 99;
    CHECK(!SameServerEntitySnap(base, mutated));

    mutated = base;
    mutated.hallucinations[0][0] = 0.9f;
    CHECK(!SameServerEntitySnap(base, mutated));
}

TEST(SnapshotCompareSameArrayContentIsEqual)
{
    ServerEntitySnap a = SampleSnap();
    ServerEntitySnap b = SampleSnap();
    a.primary_slots[0].copies[0].visual_value = 301;
    b.primary_slots[0].copies[0].visual_value = 301;
    CHECK(SameServerEntitySnap(a, b));
}
