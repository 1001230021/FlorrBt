import assert from 'node:assert/strict';
import {setTimeout as delay} from 'node:timers/promises';

export async function checkRapidLoadout({page,until}) {
  await page.locator('#quickBackpackBtn').click();await delay(320);
  for(const [index,name] of ['Rose','Rock'].entries()) {
    const card=page.locator('.inventory-group').filter({has:page.locator('.inventory-group-title',{hasText:/^Common$/})})
      .locator('.inventory-item').filter({has:page.locator('.petal-card-name',{hasText:new RegExp('^'+name+'$')})}).first();
    await card.click();
    await until(async({index,name})=>{
      const {state}=await import('/src/app_context.js');const {PETAL_IDS}=await import('/src/generated_ids.js');
      return state.secondarySlots[index]?.petalType===PETAL_IDS[name.toLowerCase()]&&state.secondarySlots[index]?.rarity===1;
    },{index,name},'reserve card acknowledged');
  }
  await page.locator('#backpackCloseBtn').click();await delay(250);await page.locator('#game').focus();
  const base=await page.evaluate(async()=>{
    const {state}=await import('/src/app_context.js');
    return {primary:structuredClone(state.ownerSlots),secondary:structuredClone(state.secondarySlots),localId:state.slotTxSeq,
      sent:window.__qaWire.filter(x=>x.sent===250&&x.op===3).length};
  });
  await page.evaluate(async()=>{
    const {state}=await import('/src/app_context.js');
    __qaDelivery.socket=state.ws;__qaDelivery.initial=state.snapshotId;
    window.qaHeld=__qaDelivery;
  });
  try {
    await delay(150);
    assert(await page.evaluate(async()=>{const {state}=await import('/src/app_context.js');return qaHeld.pending.length>0&&state.snapshotId===qaHeld.initial;}),
      'the fixture really delays incoming snapshots and receipts');
    await page.keyboard.press('r');await delay(40);await page.keyboard.press('r');await delay(350);
    await until(() => {
      const wire = window.__qaWire, sent = wire.filter(item => item.sent === 250 && item.op === 3).at(-1);
      return wire.some(item => item.result?.requestId === sent?.requestId && item.result?.outcome === 0);
    }, null, 'the first swap receipt has arrived before releasing its delayed delivery');
    await page.evaluate(()=>qaHeld.flush());
    await until(async expected=>{
      const {state}=await import('/src/app_context.js');
      return JSON.stringify(state.ownerSlots)===JSON.stringify(expected);
    },base.secondary,'first delayed swap applied');
    await delay(60);
    const drawn=await page.locator('#primarySlots .slot').evaluateAll(buttons=>buttons.map(button=>{
      const [,petalType,rarity]=button.dataset.renderKey.split(':');return{petalType:Number(petalType),rarity:Number(rarity)};
    }));
    assert.deepEqual(drawn,base.primary,'old B receipt cannot roll back the newer A preview');
    for(let n=0;n<21;n++)await page.keyboard.press('r');
  }finally{await page.evaluate(()=>qaHeld.stop());}
  const waitSlots=async(primary,secondary)=>until(async expected=>{
    const {state}=await import('/src/app_context.js');
    const same=(a,b)=>a?.length===b.length&&a.every((s,i)=>s.petalType===b[i].petalType&&s.rarity===b[i].rarity);
    const wire=window.__qaWire, sent=wire.filter(x=>x.sent===250&&x.op===3).at(-1);
    return same(state.ownerSlots,expected.primary)&&same(state.secondarySlots,expected.secondary)&&
      same(state.entities.get(state.ownerEntityId)?.snapshot.primarySlots,expected.primary)&&
      !!wire.find(x=>x.result?.requestId===sent?.requestId&&x.result?.outcome===0)&&!document.querySelector('.petal-flight');
  },{primary,secondary},'authoritative account, flower snapshot and animation all converge',150);
  await waitSlots(base.secondary,base.primary);
  await until(async expected=>{
    const {state}=await import('/src/app_context.js');const {isPetalEntity,petalTypeFromEntity}=await import('/src/protocol.js');
    const types=new Set(expected.filter(s=>s.petalType).map(s=>s.petalType));
    const petals=[...state.entities.values()].filter(e=>isPetalEntity(e.snapshot.entityType)&&!e.dying);
    return petals.length===2&&petals.every(e=>types.has(petalTypeFromEntity(e.snapshot.entityType)));
  },base.secondary,'only the final two equipped petals remain in the world',120);
  await delay(1000);
  const ring=await page.evaluate(async()=>{
    const {state}=await import('/src/app_context.js');const {isPetalEntity}=await import('/src/protocol.js');
    const flower=state.entities.get(state.ownerEntityId),petals=[...state.entities.values()].filter(e=>isPetalEntity(e.snapshot.entityType)&&!e.dying);
    const opposition=render=>{
      const center=render?flower.renderPos:flower.snapshot.pos;
      const vectors=petals.map(e=>{const p=render?e.renderPos:e.snapshot.pos;return{x:p.x-center.x,y:p.y-center.y};});
      return (vectors[0].x*vectors[1].x+vectors[0].y*vectors[1].y)/(Math.hypot(vectors[0].x,vectors[0].y)*Math.hypot(vectors[1].x,vectors[1].y));
    };
    return {types:petals.map(e=>e.snapshot.entityType),serverDot:opposition(false),renderDot:opposition(true),
      acceptedInputs:state.slotTxSeq,sent:window.__qaWire.filter(x=>x.sent===250&&x.op===3).length};
  });
  assert(Math.abs(ring.serverDot+1)<.08&&Math.abs(ring.renderDot+1)<.12,'the two final petals occupy opposite orbit positions on server and client');
  assert.equal(ring.acceptedInputs-base.localId,23);
  assert(ring.sent-base.sent<=4,'rapid inputs do not become a long queue of reloads');
  const primary=structuredClone(base.secondary),secondary=structuredClone(base.primary);
  for(const i of [0,1])[primary[i],secondary[i]]=[secondary[i],primary[i]];
  await page.keyboard.press('1');await page.keyboard.press('2');
  await waitSlots(primary,secondary);
  // 两个不同槽位的连续输入不能被旧的全局100ms限流吞掉。
  await page.keyboard.press('1');await page.keyboard.press('2');await waitSlots(base.secondary,base.primary);
  await page.keyboard.press('r');await waitSlots(base.primary,base.secondary);
  return {...ring,acceptedInputs:23,sent:ring.sent-base.sent,twoSlotRapidSwap:true};
}
