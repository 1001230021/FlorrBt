import assert from "node:assert/strict";
import { createTalentRotation, stopTalentRotation, talentDragAngle, releaseTalentRotation,
  stepTalentRotation } from "../web/src/talent_rotation.js";
const near = (a,b,e=1e-8) => assert(Math.abs(a-b)<e,`${a} != ${b}`);
const pivot = {x:0,y:0};
near(talentDragAngle(pivot,{x:0,y:-100},{x:100,y:0}),Math.PI/2);
near(talentDragAngle(pivot,{x:-100,y:1},{x:-100,y:-1}),Math.atan2(200,9999));
assert(Math.abs(talentDragAngle(pivot,{x:-100,y:0},{x:100,y:0}))<=.5);
near(talentDragAngle(pivot,pivot,pivot),0);
for (const velocity of [-4,-1,0,1,4]) {
  const direct=createTalentRotation(.2);direct.target=1.1;releaseTalentRotation(direct,velocity);
  stepTalentRotation(direct,.25);
  for(const fps of [30,60,120]) {
    const motion=createTalentRotation(.2);motion.target=1.1;releaseTalentRotation(motion,velocity);
    let remaining=.25;
    while(remaining>1e-9){const dt=Math.min(1/fps,remaining);stepTalentRotation(motion,dt);remaining-=dt;}
    near(motion.angle,direct.angle);near(motion.target,direct.target);near(motion.velocity,direct.velocity);
    let count=0;while(stepTalentRotation(motion,1/fps) && ++count<fps*4){}
    assert(count<fps*4,"inertia must stop scheduling frames");
  }
}
const moving=createTalentRotation(0);moving.target=1;releaseTalentRotation(moving,3);
stepTalentRotation(moving,.03);const stopped=moving.angle;stopTalentRotation(moving);stepTalentRotation(moving,1);near(moving.angle,stopped);
moving.target=2;releaseTalentRotation(moving,2);assert.equal(stepTalentRotation(moving,.016,true),false);near(moving.angle,2);near(moving.velocity,0);
const wrap=createTalentRotation(20*Math.PI+.3);stepTalentRotation(wrap,.1);near(wrap.angle,.3);
console.log("PASS circular dragging, centre crossing, shortest angle, frame-rate-independent glide, stop/reduced motion and bounded idle state");
if(process.argv.includes('--browser')) {
  const { checkTalentRotationBrowser }=await import('./talent_rotation_browser.mjs');await checkTalentRotationBrowser();
}
