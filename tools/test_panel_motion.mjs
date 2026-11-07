import assert from "node:assert/strict";
import { panelMotion, panelProgress, closedPanelOffset } from "../web/src/panel_motion.js";
import { craftResultPalette } from "../web/src/craft_button.js";
import { rarityColor } from "../web/src/protocol.js";
import { talentNodes } from "../web/src/talent_data.js";
import { buildTalentLayout } from "../web/src/talent_layout.js";
for(const duration of [panelMotion.enterMs,panelMotion.exitMs]) {
  assert.equal(panelProgress(0,duration),0); assert.equal(panelProgress(duration,duration),1);
  let previous=0;
  for(let t=0;t<duration;t+=1) {const k=panelProgress(t,duration);assert(k>=previous && k<=1);previous=k;}
}
assert.deepEqual(closedPanelOffset('backpackPanel',{left:92,top:262,width:404,viewportHeight:884}),{x:0,y:630});
assert.deepEqual(closedPanelOffset('settingsPanel',{left:8,top:62,width:480,viewportHeight:884}),{x:-496,y:0});
for(let rarity=1;rarity<=9;rarity++) assert.equal(craftResultPalette(rarity).color,rarityColor(rarity+1));
assert.equal(craftResultPalette(8,true).rarity,10);
assert.equal(craftResultPalette(undefined).rarity,0);
const layout=buildTalentLayout(talentNodes);
assert.equal(layout.positions.size,talentNodes.length);
assert([...layout.positions.values()].some(p=>p.y<0) && [...layout.positions.values()].some(p=>p.y>0),
  'rotating tree covers a complete circle around the root');
for(const node of talentNodes) assert(layout.edges.some(e=>e.to===node.key && e.from===(node.base||null)));
console.log("PASS video-derived panel axes and bounded easing, craft output colors, circular talent branches and unchanged prerequisites");
