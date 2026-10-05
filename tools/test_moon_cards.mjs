// Execute the actual renderer/card factory against fake DOM/Canvas objects.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { drawMoonSurface, moonSvgDrawSize, moonSvgGeometry } from "../web/src/moon_renderer.js";

const game = readFileSync(new URL("../web/src/game.js", import.meta.url), "utf8");
const moonSource = game.slice(game.indexOf("function drawProceduralMoonPetal("), game.indexOf("function drawDustParticlePetal("));
const drawn = [];
const ctx = {};
const drawMoon = vm.runInNewContext(`${moonSource};drawProceduralMoonPetal`, {
  ctx,
  assetImage: () => ({ complete: true, naturalWidth: 110, failed: false }),
  getPetalIconPath: () => moonSvgGeometry.source,
  petalMoonType: 42,
  imageReady: image => !!image?.complete && image.naturalWidth > 0 && !image.failed,
  drawSpriteImage: (...args) => drawn.push(args),
  drawMoonSurface,
  moonSvgGeometry,
});
for (const size of [1, 10, 134.4, 840, 4000]) {
  drawn.length = 0;
  drawMoon(size, 6, 1);
  assert.equal(drawn.length, 1, "ready Moon uses the source SVG directly");
  const drawSize = drawn[0][4];
  assert.ok(Math.abs(drawSize - moonSvgDrawSize(size * moonSvgGeometry.liveRadiusFactor)) < 1e-8);
  assert.ok(Math.abs(drawSize * moonSvgGeometry.artRadius / moonSvgGeometry.viewBoxSize -
    size * moonSvgGeometry.liveRadiusFactor) < 1e-8,
    "SVG art radius remains proportional to the client geometry data");
}
assert.match(game, /assetImage\(getPetalIconPath\(petalMoonType\)\)/);
assert.match(readFileSync(new URL("../web/assets/petals/moon.svg", import.meta.url), "utf8"), /viewBox="0 0 110 110"/);

function element(tag) {
  return { tagName: tag, className: "", children: [], style: { setProperty() {} }, classList: { add() {} },
    appendChild(node) { this.children.push(node); } };
}
const petal = readFileSync(new URL("../web/src/petal_info.js", import.meta.url), "utf8");
const start = petal.indexOf("export function makePetalStack(");
const end = petal.indexOf("// One label", start);
const makeCard = vm.runInNewContext(`${petal.slice(start, end).replace(/^export /, "")};makePetalStack`, {
  document: { createElement: element }, petalMimicType: 99, effectivePetalCardCopy: () => 1,
  assignVisualImage() {}, assetImage: () => ({}), getPetalBasePath: () => "base.svg", getPetalCopyLayout: () => [],
  makePetalDisplayName: () => element("span"), formatShortCount: String,
});
for (const count of [0, 1, 2, 99]) for (const showCount of [false, true]) {
  const card = makeCard(1, 8, count, 1, { showCount });
  const badge = card.children.find(child => child.className === "petal-count");
  assert.equal(!!badge && !badge.hidden, count > 1);
}
const css = readFileSync(new URL("../web/styles.css", import.meta.url), "utf8");
assert.match(css, /\.petal-count\[hidden\]\s*\{\s*display:\s*none\s*!important/);
assert.match(game, /if \(stackCount > 1\) drawDropStackBadge/);

// Verify both spawn paths consume pressure rejections, and retention and
// pressure share the same current/nearest-zone classification.
const server = file => readFileSync(new URL(`../src/Server/${file}`, import.meta.url), "utf8");
const world = server("Game/gameworld.cpp"), director = server("Game/gamecontrollers/open_spawn_director.cpp");
assert.match(world, /mob->ActiveTickMarker\(\) == m_active_tick_marker \|\| GetMobSpawnRarityExcess\(\*mob\) >= 2/);
assert.match(world, /FindSpawnHabitatDifficulty\(m_map->zones, mob.Pos\(\), &m_spawn_zone_bounds\)/);
assert.equal([...director.matchAll(/!ApplySpawnPressure\(pos, work.difficulty, rarity_roll, rarity\)/g)].length, 2);
assert.match(director, /world.GetMobSpawnRarityExcess\(mob\)/);
const reload = server("HotReload/hot_reload_snapshot.cpp");
for (const field of ["moon_orbit_center", "moon_orbit_phase", "moon_orbit_initialized", "moon_orbit_velocity"])
  assert.equal(reload.split(`"${field}"`).length - 1, 2, `${field} must be saved and restored`);
assert.match(server("Game/entities/petals/petals_behavior.h"), /default_wax_mass \* game_config::GetMobMassScaleForLevel\(GetLevel\(rarity\)\)/);
const waxContact = world.slice(world.indexOf("bool ResolveWaxMobHardCollision(const collision_entity_view& lhs," , world.indexOf("florrbt::Vector2f GetEntityVelocity(CEntity& entity)")), world.indexOf("bool BounceFiredCarrotFromEntity"));
assert.ok(waxContact.indexOf("if (flower)") < waxContact.indexOf("ComputeWaxContactWeights"));
assert.match(waxContact, /if \(flower\)[\s\S]*?return true;[\s\S]*?ComputeWaxContactWeights\(wax->Mass\(\), mob->Mass\(\)/);
const mobSource = server("Game/entities/mob_behavior.cpp");
const soldierEnd = mobSource.indexOf("RegisterMobPrototype<EMobType::SoldierAnt,");
const soldierStart = mobSource.lastIndexOf("        CMobPrototype proto;", soldierEnd);
const leafBlock = mobSource.slice(soldierEnd, mobSource.indexOf("RegisterMobPrototype<EMobType::LeafcutterSoldier,"));
assert.doesNotMatch(mobSource.slice(soldierStart, soldierEnd), /mob_leafcutter_soldier_\w+_multiplier/,
  "ordinary soldier ants must not receive the Leafcutter buff");
for (const stat of ["health", "mass", "radius", "damage"])
  assert.match(leafBlock, new RegExp(`mob_leafcutter_soldier_${stat}_multiplier`));
console.log("PASS world-space Moon texture/rim, no x1 badges, spawn classification, Moon reload and Wax/Leafcutter wiring");
