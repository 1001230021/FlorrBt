import { UiPanel } from './ui_panel.js';
import { registerClientFeature } from './client_features.js';

export function installPlayerFeatureUi({ state, ui, dom, renderInventory, renderCraft, renderTalents, clearCraft, closeTalents, closeCodex, changed }) {
  const definitions = [
    { key: 'inventory', id: 'backpack', root: dom.backpackPanel, button: dom.quickBackpackBtn, field: 'backpackUiPanel', stateKey: 'backpackOpen', open: renderInventory, title: 'ui.quick.inventory' },
    { key: 'talent', id: 'talent', root: dom.talentPanel, button: dom.quickTalentBtn, field: 'talentUiPanel', stateKey: 'talentOpen', open: renderTalents, close: closeTalents, title: 'ui.quick.talents' },
    { key: 'craft', id: 'craft', root: dom.craftPanel, button: dom.quickCraftBtn, field: 'craftUiPanel', stateKey: 'craftOpen', open: renderCraft, close: clearCraft, title: 'ui.quick.craft' },
  ];
  for (const item of definitions) {
    const panel = new UiPanel({ id: item.id, root: item.root, state, stateKey: item.stateKey, manager: ui.panelManager,
      trigger: item.button, focusTarget: dom.canvas, onOpen: item.open, onClose: item.close, onChange: changed });
    ui[item.field] = panel;
    registerClientFeature(item.key, { button: item.button, title: item.title, toggle(force) { if (panel.toggle(force) && force !== false) closeCodex(); } });
  }
}
