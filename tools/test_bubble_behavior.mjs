// Compile the *current* Bubble behavior against small entity doubles. This
// runs no game world, sockets, player accounts or databases.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = readFileSync(path.join(root, "src/Server/Game/entities/petals/petals_behavior.h"), "utf8");
function definition(marker, suffix = "") {
  const start = source.indexOf(marker);
  assert.ok(start >= 0, `missing ${marker}`);
  const body = source.indexOf("{", start);
  let depth = 1, end = body + 1;
  for (; end < source.length && depth; ++end) {
    if (source[end] === "{") depth++;
    if (source[end] === "}") depth--;
  }
  assert.equal(depth, 0);
  return source.slice(start, end) + suffix;
}
const directory = path.join(root, "artifacts/test-bubble-behavior");
mkdirSync(directory, { recursive: true });
// Generated build inputs intentionally live outside source directories.
writeFileSync(path.join(directory, "bubble_check.cpp"), `
#include "Shared/game_config.h"
#include "Shared/math_types.h"
#include "Shared/rarity.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
struct SFlowerStats {};
SFlowerStats EmptyFlowerStats() { return {}; }
struct SPetalStats { float health=0, damage=0, reload=0, preload=0, mass=0, radius=0; int copy=0; };
enum class EDamageType { Normal };
struct CEntity {};
struct CPetal : CEntity {
    float m_lifetime = 0, health = 1; bool removed = false;
    florrbt::Vector2f pos{10,0}, m_vel{};
    float& Health() { return health; }
    florrbt::Vector2f Pos() const { return pos; }
    bool IsMarkedForDes() const { return removed; }
};
struct CFlower : CEntity {
    bool m_attacking=false, m_defending=false; CPetal* moon=nullptr;
    florrbt::Vector2f m_vel{};
    florrbt::Vector2f Pos() const { return {}; }
    CPetal* GetMoonPetal() { return moon; }
};
struct CPetalBehavior {
    virtual ~CPetalBehavior()=default;
    virtual bool IsOpen() const=0;
    virtual SFlowerStats GetStats(ERarity) const=0;
    virtual SPetalStats GetPetalStats(ERarity) const=0;
    virtual void OnTick(CPetal*,ERarity,CFlower*,float)=0;
    virtual void OnFlowerTakeDamage(CPetal*,ERarity,CFlower*,float&,EDamageType,CEntity*)=0;
    virtual void OnPetalSpawned(CPetal*,ERarity,CFlower*)=0;
    virtual void OnPetalDestroyed(CPetal*,ERarity,CFlower*)=0;
};
float Length(florrbt::Vector2f v) { return std::hypot(v.x,v.y); }
float GetNeutralOrbitDistance(CPetal*,CFlower*) { return 100; }
void MovePetalToOrbit(CPetal*,CFlower*,float,float,bool) {}
${definition("inline float BubbleReload(")}
${definition("class CBubbleBehavior :", ";")}
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr,"FAIL %s\\n",message); std::exit(1); }
}
int main() {
    CBubbleBehavior behavior;
    int cases=0;
    for(int r=1;r<=12;++r) for(bool attack : {false,true}) for(bool defense : {false,true})
    for(bool moon_target : {false,true}) for(bool ready : {false,true}) {
        const auto rarity=static_cast<ERarity>(r);
        CPetal bubble,moon; moon.pos={30,0};
        CFlower flower; flower.m_attacking=attack; flower.m_defending=defense;
        flower.moon=moon_target ? &moon : nullptr;
        bubble.m_lifetime=ready ? BubbleReload(rarity) : 0.f;
        behavior.OnTick(&bubble,rarity,&flower,.05f);
        const bool triggered=ready && defense;
        check((bubble.Health()==0)==triggered,"defense, not attack, decides trigger");
        const auto target_velocity=moon_target ? moon.m_vel : flower.m_vel;
        const float expected=triggered ? game_config::default_bubble_boost_speed *
            (moon_target ? game_config::default_bubble_moon_boost_multiplier : 1.f) : 0;
        check(std::abs(Length(target_velocity)-expected)<.001f,"exactly one Bubble impulse");
        check(Length(moon_target ? flower.m_vel : moon.m_vel)==0,"only correct body boosted");
        behavior.OnPetalDestroyed(&bubble,rarity,&flower);
        check((moon_target ? moon.m_vel : flower.m_vel)==target_velocity,"destruction adds no second boost");
        ++cases;
    }
    for(bool removed : {false,true}) for(bool attack : {false,true}) {
        CPetal bubble; CFlower flower;
        flower.m_attacking=attack; flower.m_defending=true;
        bubble.m_lifetime=100; bubble.removed=removed; bubble.health=removed ? 1 : 0;
        behavior.OnTick(&bubble,ERarity::Common,&flower,.05f);
        check(Length(flower.m_vel)==0,"dead or removed Bubble cannot give a delayed boost");
        ++cases;
    }
    std::printf("PASS %d production Bubble behavior cases (all rarities, both modes, Moon, preload, removal)\\n",cases);
}
`, "utf8");
writeFileSync(path.join(directory, "CMakeLists.txt"), `cmake_minimum_required(VERSION 3.20)
project(BubbleBehaviorCheck LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_executable(BubbleBehaviorCheck bubble_check.cpp)
target_include_directories(BubbleBehaviorCheck PRIVATE "${root.replaceAll("\\", "/")}/src")
enable_testing()
add_test(NAME production_bubble_behavior COMMAND BubbleBehaviorCheck)
`, "utf8");
for (const args of [
  ["-S", directory, "-B", path.join(directory, "build")],
  ["--build", path.join(directory, "build"), "--config", "Release", "--parallel", "2"],
]) {
  const result = spawnSync("cmake", args, { encoding: "utf8", timeout: 60000, windowsHide: true });
  process.stdout.write(result.stdout || ""); process.stderr.write(result.stderr || "");
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status || 1);
}
const result = spawnSync("ctest", ["--test-dir", path.join(directory, "build"), "-C", "Release", "-V"],
  { encoding: "utf8", timeout: 10000, windowsHide: true });
process.stdout.write(result.stdout || ""); process.stderr.write(result.stderr || "");
if (result.error) throw result.error;
process.exit(result.status === 0 ? 0 : result.status || 1);
