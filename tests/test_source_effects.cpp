#include "test_framework.h"
#include "Shared/source_effects.h"

TEST(SourceFlagsReleaseOnlyTheirOwnRestriction)
{
    CSourcedFlag flag;
    flag.SetSource(10, true); flag.SetSource(20, true);
    flag.SetSource(10, false); CHECK(flag.Value());
    flag.SetPolicy(true); flag.SetSource(20, false); CHECK(flag.Value());
    flag.SetSource(30, true); flag.SetPolicy(false); CHECK(flag.Value());
    flag.SetSource(30, false); CHECK(!flag.Value());
    flag.SetSource(0, true); CHECK(!flag.Value());
}

TEST(SourceOverridesPreserveNewBaseAndResolvePriorityExplicitly)
{
    CSourcedValue<int> team;
    team.SetBase(2); team.SetSource(10, 0, 10); team.SetSource(20, 0, 10);
    team.RemoveSource(10); CHECK(team.Value() == 0);
    team.SetBase(7); CHECK(team.Value() == 0 && team.Base() == 7);
    team.RemoveSource(20); CHECK(team.Value() == 7);
    team.SetSource(30, 9, 10); team.SetSource(40, 11, 5); CHECK(team.Value() == 9);
    team.SetSource(50, 13, 10); CHECK(team.Value() == 13);
    team.RemoveSource(50); CHECK(team.Value() == 9);
    team.ClearSources(); CHECK(team.Value() == 7);
}
