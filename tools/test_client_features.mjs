import assert from 'node:assert/strict';
import vm from 'node:vm';
import { readFileSync } from 'node:fs';
import { isNewerUint32Sequence, parseServerMessage, ServerType } from '../web/src/protocol.js';

const source = readFileSync(new URL('../web/src/client_features.js', import.meta.url), 'utf8')
  .replace(/^import[^\n]+\n/gm, '').replace(/^export /gm, '');
const state = { authenticated: true, playerSessionGeneration: '0020000000000001', worldId: 3, worldGeneration: '000000000000002a',
  ownerEntityId: 5, entities: new Map([[5, { snapshot: { entityGeneration: '0020000000000002' } }]]) };
const toggles = [], buttons = [];
const api = vm.runInNewContext(`${source};({registerClientFeature,applyClientFeatures,featureAllowed,invokeClientFeature,resetClientFeatures})`,
  { state, dom: {}, ui: {}, t: text => text, isNewerUint32Sequence });
for (const key of ['inventory', 'talent', 'craft']) {
  const hidden = new Map(); const button = { classList: { toggle: (key, value) => hidden.set(key, value) }, style: {}, dataset: {},
    addEventListener() {}, hidden, disabled: false }; buttons.push(button);
  api.registerClientFeature(key, { button, title: key, toggle: force => toggles.push([key, force]) });
}
const data = { schema: 1, revision: 1, session: '9007199254740993', world: 3, worldGeneration: '42', owner: 5, ownerGeneration: '9007199254740994',
  features: ['inventory', 'talent', 'craft'].map((key, order) => ({ key, order, surfaces: 1, visible: true, access: 3, interaction: 0, badge: false, title: key, icon: key })) };
assert(api.applyClientFeatures(data)); assert(api.featureAllowed('inventory', 2));
assert(api.invokeClientFeature('inventory')); assert.equal(buttons[0].hidden.get('hidden'), false);
const hidden = structuredClone(data); hidden.revision = 2; hidden.features[0].visible = false;
assert(api.applyClientFeatures(hidden)); assert(api.featureAllowed('inventory', 2)); assert(buttons[0].hidden.get('hidden'));
const disabled = structuredClone(data); disabled.revision = 3; disabled.features[2].access = 0;
assert(api.applyClientFeatures(disabled)); assert.equal(api.featureAllowed('craft'), false); assert(buttons[2].disabled);
assert.equal(api.invokeClientFeature('craft'), false); assert(toggles.some(([key, force]) => key === 'craft' && force === false));
assert.equal(api.applyClientFeatures(data), false, 'late manifests cannot restore removed permissions');
assert.equal(api.applyClientFeatures({ ...data, revision: 9, worldGeneration: '43' }), false);
assert.equal(api.applyClientFeatures({ ...data, revision: 9, session: '9007199254740992' }), false);
assert.equal(api.applyClientFeatures({ ...data, revision: 9, features: [data.features[0], data.features[0]] }), false);
api.resetClientFeatures(); assert.equal(api.featureAllowed('inventory'), false);
for (const type of [ServerType.ClientFeatures, ServerType.AccountLinks]) {
  const payload = new TextEncoder().encode(JSON.stringify(data)); const packet = new Uint8Array(payload.length + 3);
  packet[0] = type; new DataView(packet.buffer).setUint16(1, payload.length, true); packet.set(payload, 3);
  assert.equal(parseServerMessage(packet).type, type);
  for (let size = 0; size < packet.length; size++) assert.equal(parseServerMessage(packet.subarray(0, size)).type, 255);
}
console.log('PASS feature identity precision, stale/foreign rejection, visibility versus permissions, disabled actions and service framing');
