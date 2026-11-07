import assert from "node:assert/strict";
import { stepSnapshotPose } from "../web/src/snapshot_pose.js";
import { readFileSync } from "node:fs";
import vm from "node:vm";
import { PETAL_IDS, MOB_IDS } from "../web/src/generated_ids.js";
const near = (a,b) => assert.ok(Math.abs(a-b) < 1e-7, `${a} != ${b}`);
for (const owner of [false, true]) for (const type of [1, 104]) {
  const entity = { snapshot: { entityType:type, pos:{x:100,y:50}, radius:30, angle:.7 },
    renderPos:{x:0,y:0}, renderAngle:0, snapshotVelocity:{x:1e6,y:1e6}, localMoveInput:{x:1,y:1} };
  const one = structuredClone(entity), many = structuredClone(entity);
  stepSnapshotPose(one, .12, owner);
  for(let i=0;i<8;i++) stepSnapshotPose(many,.015,owner);
  near(one.renderPos.x,many.renderPos.x); near(one.renderPos.y,many.renderPos.y);
  for(let i=0;i<600;i++) { stepSnapshotPose(entity,1/60,owner); assert(entity.renderPos.x<=100 && entity.renderPos.y<=50); }
  near(entity.renderPos.x,100);
  const stopped = structuredClone(entity.renderPos);
  for(let i=0;i<60;i++) stepSnapshotPose(entity,.05,owner);
  near(entity.renderPos.x,stopped.x); near(entity.renderPos.y,stopped.y);
  entity.snapshot.pos={x:5000,y:-2000}; stepSnapshotPose(entity,.016,owner);
  assert.deepEqual(entity.renderPos,entity.snapshot.pos,"teleport snaps without replaying input");
}
{
  const owner = { snapshot: {entityId:1,entityType:MOB_IDS.player_flower,flags:0,pos:{x:0,y:0}}, renderPos:{x:0,y:0} };
  const moon = { snapshot: {entityId:2,entityType:100+PETAL_IDS.moon,flags:0,pos:{x:1000,y:0},radius:10}, renderPos:{x:980,y:0} };
  const trapper = { snapshot: {entityId:3,entityType:100+PETAL_IDS.trapper,flags:0,pos:{x:1010,y:0}}, renderPos:{x:1010,y:0} };
  const state = {entities:new Map([[1,owner],[2,moon],[3,trapper]]),ownerEntityId:1,camera:{},authenticated:true};
  const source = readFileSync(new URL('../web/src/sim_entities.js',import.meta.url),'utf8');
  const body = source.slice(source.indexOf('export function updateRenderPositions('),source.indexOf('export function hasPlayerSnapPetal(')).replace(/^export /,'');
  const update = vm.runInNewContext(body+'\nupdateRenderPositions',{state,performance:{now:()=>0},flowerFaceTypes:new Set(),flagDead:4,
    stepDeathViewFactor:x=>x,stepSnapshotPose,stepEntityAppearance:()=>false,stepEntityGait(){},hurtFlashDuration:.1,serverFixedDt:.05});
  update(0);
  assert.deepEqual(trapper.renderPos,{x:1010,y:0},'nearby flowers/Moons cannot override an authoritative petal position');
}
console.log("PASS authoritative owner/petal/NPC interpolation, no guessed mounts, no stale extrapolation, frame-rate equivalence and teleport");
