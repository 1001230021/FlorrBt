import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const petal = readFileSync(new URL("../src/Server/Game/entities/petals/petal.h", import.meta.url), "utf8");
const petalCpp = readFileSync(new URL("../src/Server/Game/entities/petals/petal.cpp", import.meta.url), "utf8");
const behavior = readFileSync(new URL("../src/Server/Game/entities/petals/petals_behavior.h", import.meta.url), "utf8");
const sawStart = behavior.indexOf("class CSawbladeBehavior");
const sawEnd = behavior.indexOf("class C", sawStart + 12);
const saw = behavior.slice(sawStart, sawEnd < 0 ? undefined : sawEnd);

assert.match(petal, /CEntity\* m_orbit_anchor = nullptr/);
assert.match(petal, /RefreshOrbitAnchor\(CFlower\* flower\)/);
assert.match(petalCpp, /GetPetalType\(\) == EPetalType::Moon[\s\S]*?m_orbit_anchor = flower/);
assert.match(petalCpp, /GetMoonPetal\(\)[\s\S]*?m_orbit_anchor = moon && moon != this/);
assert.match(petalCpp, /RefreshOrbitAnchor\(flower\);[\s\S]*?petal->Pos\(\) = petal->GetOrbitAnchor\(\)/);
assert.match(behavior, /CEntity\* anchor = owner->GetOrbitAnchor\(\);[\s\S]*?anchor == flower/);
assert.match(behavior, /CEntity\* anchor = owner->GetOrbitAnchor\(\);[\s\S]*?return anchor \? anchor->Pos\(\) : flower->Pos\(\)/);
assert.match(behavior, /const bool heavy = type == EPetalType::Heavy/);
assert.match(behavior, /m_defending && !heavy[\s\S]*?default_petal_defend_offset/);
assert.match(saw, /m_attacking[\s\S]*?default_petal_attack_offset/);
assert.doesNotMatch(saw, /GetFinalStats\(\)->reach/);
assert.match(behavior, /m_defending\)\s*orbit_distance \+= game_config::default_petal_defend_offset/);
const orangeStart = behavior.indexOf("class COrangeBehavior");
const orange = behavior.slice(orangeStart, behavior.indexOf("class C", orangeStart + 12));
assert.match(orange, /m_attacking[\s\S]*?default_petal_attack_offset[\s\S]*?m_defending[\s\S]*?default_petal_defend_offset/);
console.log("PASS Moon orbit anchor, generation anchor, defense reduction and Sawblade base attack reach");
