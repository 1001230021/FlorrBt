#include "test_framework.h"
#include "Shared/immutable_snapshot.h"
#include "Shared/swept_collision.h"
#include "Shared/spawn_dominance.h"
#include <set>
#include <string>
#include <random>

namespace {
struct SweepEntry {
    florrbt::SweptCircleBounds bounds;
    int id;
    bool active;
    float prefix_max_x = 0;
};
struct Account {
    std::string name;
    std::vector<int> inventory;
    static inline int copies = 0;
    Account(std::string key, int value) : name(std::move(key)), inventory(100, value) {}
    Account(const Account& a) : name(a.name), inventory(a.inventory) { ++copies; }
    Account& operator=(const Account&) = default;
};
struct Zone { float difficulty; std::vector<florrbt::Vector2f> vertices; std::string mobs="ant",spawn_profile; };
}

TEST(SweptBroadphaseIncludesCrossingAndMovingTargetsExactlyOnce)
{
    std::vector<SweepEntry> entries{
        {florrbt::MakeSweptCircleBounds({-600,0},{600,0},10),1,true},
        {florrbt::MakeSweptCircleBounds({0,0},{0,0},20),2,false},
        {florrbt::MakeSweptCircleBounds({0,-600},{0,600},10),3,true},
        {florrbt::MakeSweptCircleBounds({2000,2000},{2000,2000},10),4,false}
    };
    std::set<std::pair<int,int>> pairs;
    florrbt::VisitSweptCirclePairs(entries,[&](const auto& a,const auto& b){
        CHECK(pairs.insert(std::minmax(a.id,b.id)).second);
    });
    CHECK(pairs.size()==3);
    CHECK(pairs.contains({1,2})); CHECK(pairs.contains({1,3})); CHECK(pairs.contains({2,3}));
}

TEST(SweptBroadphaseMatchesBruteForceAcrossSizesAndSleepingBodies)
{
    std::mt19937 rng(91); std::uniform_real_distribution<float> p(-10000,10000),r(1,900);
    std::vector<SweepEntry> entries;
    for(int i=0;i<600;++i) entries.push_back({florrbt::MakeSweptCircleBounds({p(rng),p(rng)}, {p(rng),p(rng)},r(rng)),i,i%7==0});
    std::set<std::pair<int,int>> expected, actual;
    for(std::size_t i=0;i<entries.size();++i) for(std::size_t j=i+1;j<entries.size();++j)
        if((entries[i].active||entries[j].active)&&florrbt::OverlapSweptBounds(entries[i].bounds,entries[j].bounds))
            expected.emplace(entries[i].id,entries[j].id);
    florrbt::VisitSweptCirclePairs(entries,[&](const auto& a,const auto& b){CHECK(actual.insert(std::minmax(a.id,b.id)).second);});
    CHECK(expected==actual);
}

TEST(ImmutableAccountSnapshotCopiesOnlyDirtyAccountsAndRetainsOldVersions)
{
    std::vector<Account> accounts; accounts.reserve(1000);
    for(int i=0;i<1000;++i) accounts.emplace_back(std::to_string(i),i);
    florrbt::ImmutableSnapshotCache<Account> cache;
    const auto include=[](const auto&){return true;};
    Account::copies=0;
    const auto initial=cache.Capture(accounts,include);
    CHECK(Account::copies==1000);
    const auto idle=cache.Capture(accounts,include);
    CHECK(Account::copies==1000); CHECK(initial[700]==idle[700]);
    cache.MarkDirty("7"); accounts[7].inventory[0]=9000;
    const auto changed=cache.Capture(accounts,include);
    CHECK(Account::copies==1001); CHECK(initial[7]->inventory[0]==7);
    CHECK(changed[7]->inventory[0]==9000); CHECK(initial[700]==changed[700]);
    // Pending snapshots can be coalesced or writes retried without reading mutable accounts.
    cache.MarkDirty("7"); accounts[7].inventory[0]=42;
    const auto newest=cache.Capture(accounts,include);
    CHECK(changed[7]->inventory[0]==9000); CHECK(newest[7]->inventory[0]==42);
    accounts.pop_back(); CHECK(cache.Capture(accounts,include).size()==999);
    cache.Reset(); Account::copies=0;
    CHECK(cache.Capture(accounts,include).size()==999); CHECK(Account::copies==999);
}

TEST(SpawnInfluenceGridMatchesOriginalPressureIncludingHugeAuras)
{
    std::mt19937 rng(93); std::uniform_real_distribution<float> p(-20000,20000),r(100,5000);
    florrbt::SpawnInfluenceIndex index;
    std::vector<florrbt::SpawnInfluence> sources;
    for(int i=0;i<90;++i) { sources.push_back({{p(rng),p(rng)},r(rng),float(1+i%4)}); index.Add(sources.back()); }
    sources.push_back({{0,0},100000,2}); index.Add(sources.back());
    for(int i=0;i<2000;++i) { const florrbt::Vector2f point{p(rng),p(rng)};
        CHECK_NEAR(index.Measure(point),florrbt::MeasureSpawnPressure(point,sources),.00001); }
    index.Clear(); CHECK_NEAR(index.Measure({0,0}),0,.00001);
}

TEST(SpawnBoundsPruningPreservesContainingNearestAndTieResults)
{
    const std::vector<Zone> zones{
        {60,{{0,0},{1000,0},{1000,20},{0,20}}},
        {80,{{1050,40},{1070,40},{1070,60},{1050,60}}},
        {70,{{0,0},{100,0},{100,20},{20,20},{20,100},{0,100}}}
    };
    const auto bounds=florrbt::BuildSpawnZoneBounds(zones);
    for(int x=-20;x<1100;x+=7) for(int y=-20;y<140;y+=7) {
        const florrbt::Vector2f pos{float(x),float(y)};
        CHECK(florrbt::FindSpawnHabitatDifficulty(zones,pos)==florrbt::FindSpawnHabitatDifficulty(zones,pos,&bounds));
    }
}

TEST(SpawnBoundsPreservesBoundaryToleranceForHarderNeighbour)
{
    const std::vector<Zone> zones{
        {60,{{0,0},{100,0},{100,100},{0,100}}},
        {80,{{100,0},{200,0},{200,100},{100,100}}}
    };
    const auto bounds=florrbt::BuildSpawnZoneBounds(zones);
    const florrbt::Vector2f point{99.9995f,50};
    CHECK(florrbt::FindSpawnHabitatDifficulty(zones,point)==florrbt::FindSpawnHabitatDifficulty(zones,point,&bounds));
}
