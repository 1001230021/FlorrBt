import assert from 'node:assert/strict';
import {createLoadoutPreview} from '../web/src/loadout_preview.js';
import {createEconomyRequests,EconomyOperation as Op,EconomyOutcome as Outcome} from '../web/src/economy_requests.js';
const hex=n=>BigInt(n).toString(16).padStart(16,'0');
const A={petalType:4,rarity:1},B={petalType:21,rarity:1};
const preview=createLoadoutPreview(),sent=[],accepted=[];let now=0;
const client=createEconomyRequests({now:()=>now,send:bytes=>{sent.push(Uint8Array.from(bytes));return true;},
 onResult:(receipt,intent)=>{accepted.push(intent.clientId);preview.acknowledge(intent.clientId);if(receipt.outcome!==Outcome.Completed)preview.clear();},
 onScopeChanged:()=>preview.clear()});
const baseline=(revision,id,scope='test')=>({inventoryScope:scope,inventoryRevision:hex(revision),inventoryLastRequest:hex(id),actorScope:'body'});
client.connect('connection');client.owner();client.inventory(baseline(0,0));
function swap(id,owner,secondary,key){
 assert(client.submit({operation:Op.Loadout,clientId:id,primary:[owner],secondary:[secondary]}));
 preview.stage({id,ownerSlots:[owner],secondarySlots:[secondary],inventory:[],inventoryTouchedKeys:[key]});
}
swap(1,B,A,'21:1');swap(2,A,B,'4:1');
assert.equal(preview.current.id,2,'ABA must remain pending even though it equals the original authoritative layout');
now=5000;client.tick();assert.equal(preview.current.id,2,'elapsed time is not a rejection or acknowledgement');
assert.deepEqual(sent[0],sent[1],'timeouts resend the frozen request');
swap(3,B,A,'40:1');swap(4,A,B,'41:1');
assert.deepEqual(preview.current.inventoryTouchedKeys,['21:1','4:1','40:1','41:1']);
function complete(id,revision){client.result({inventoryScope:'test',requestId:hex(id),inventoryRevision:hex(revision),outcome:Outcome.Completed,recorded:true});client.inventory(baseline(revision,id));client.owner();}
complete(1,1);
assert.equal(preview.current.id,4,'an earlier operation cannot settle the newest display');
assert.deepEqual(accepted,[1]);assert.equal(sent.length,3,'intermediate unsent layouts are coalesced');
complete(2,2);assert.equal(preview.current,null);assert.deepEqual(accepted,[1,4]);
swap(5,B,A,'21:1');client.inventory(baseline(0,0,'different-inventory'));assert.equal(preview.current,null);

const operations=[];const ordered=createEconomyRequests({send:bytes=>{operations.push(bytes[1]);return true;}});
ordered.connect('x');ordered.owner();ordered.inventory(baseline(0,0));
ordered.submit({operation:Op.Loadout,primary:[A],secondary:[B]});
ordered.submit({operation:Op.Loadout,primary:[B],secondary:[A]});
ordered.submit({operation:Op.Craft,petalType:4,rarity:1,count:5});
ordered.submit({operation:Op.Loadout,primary:[A],secondary:[B]});
for(let id=1;id<=4;id++){ordered.result({inventoryScope:'test',requestId:hex(id),inventoryRevision:hex(id),outcome:Outcome.Completed,recorded:true});ordered.inventory(baseline(id,id));ordered.owner();}
assert.deepEqual(operations,[Op.Loadout,Op.Loadout,Op.Craft,Op.Loadout],'coalescing cannot cross an economic operation');
{
 const packets=[], outcomes=[];
 const retry=createEconomyRequests({send:bytes=>{packets.push(bytes);return true;},onResult:(result,intent)=>outcomes.push([result.outcome,intent.clientId])});
 retry.connect('pickup');retry.owner();retry.inventory(baseline(0,0));
 retry.submit({operation:Op.Loadout,clientId:1,primary:[B],secondary:[A]});
 retry.submit({operation:Op.Loadout,clientId:2,primary:[A],secondary:[B]});
 for(let id=1;id<=3;id++){
  retry.result({inventoryScope:'test',requestId:hex(id),inventoryRevision:hex(id),outcome:Outcome.StaleRevision,recorded:true});
  retry.inventory(baseline(id,id));retry.owner();
 }
 assert.equal(packets.length,3,'stale loadouts retry at most twice');
 assert.deepEqual(outcomes,[[Outcome.StaleRevision,2]],'only the latest layout is surfaced after the retry limit');
 assert.equal(new DataView(packets[1].buffer).getBigUint64(12,true),1n,'retry uses the refreshed revision');
}
console.log('PASS ABA receipts, delayed acknowledgements, coalesced layouts, bounded pickup-race retries and scope/operation isolation');
