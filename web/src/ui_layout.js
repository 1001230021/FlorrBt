// UI coordinates measured inside the game area of the supplied florr clips
// (1910 x 884, excluding browser chrome). These are presentation coordinates,
// not camera/world zoom, DPR, server slots or gameplay constants.
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));
const number = (value, fallback) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const rowWidth = (count, size, gap) => count * size + Math.max(0, count - 1) * gap;

export function calculateUiLayout({ width = 1910, height = 884, mobile = false,
  primaryCount = 10, secondaryCount = primaryCount, collapsed = false, insets = {} } = {}) {
  width = Math.max(240, number(width, 1910));
  height = Math.max(240, number(height, 884));
  const safe = Object.fromEntries(["top", "right", "bottom", "left"].map(side =>
    [side, clamp(number(insets[side], 0), 0, Math.min(width, height) / 4)]));
  const usableW = width - safe.left - safe.right, usableH = height - safe.top - safe.bottom;
  const scale = mobile
    ? clamp(Math.min(usableW, usableH) / 600, 0.55, 0.85)
    : clamp(Math.min(usableH / 884, usableW / 1280), 0.65, 1.6);
  const s = scale;
  const top = { x: safe.left + 8 * s, y: safe.top + 8 * s,
    size: mobile ? Math.max(40, 48 * s) : 48 * s, gap: 7 * s };
  const quick = { x: safe.left + 8 * s, bottom: safe.bottom + 10 * s,
    size: mobile ? Math.max(44, 64 * s) : 64 * s, gap: 10 * s };
  const pCount = clamp(Math.floor(number(primaryCount, 10)), 1, 32);
  const rCount = clamp(Math.floor(number(secondaryCount, pCount)), 1, 32);
  const slotLeft = mobile ? quick.x + quick.size + 10 * s : safe.left + 16 * s;
  const toggleRoom = mobile ? 50 : 0;
  const available = Math.max(80, mobile ? width - safe.right - slotLeft - toggleRoom - 8 : usableW - 32 * s);
  const natural = Math.max(rowWidth(pCount, 77, 3), rowWidth(rCount, 55, 2));
  // Keep every real slot. Only unusually long mobile rows scroll rather than
  // making their touch targets disappear outside the viewport.
  const slotScale = Math.max(mobile ? 26 / 77 : 32 / 77, Math.min(s, available / natural));
  const primary = 77 * slotScale, secondary = 55 * slotScale;
  const primaryGap = 3 * slotScale, secondaryGap = 2 * slotScale;
  const primaryWidth = rowWidth(pCount, primary, primaryGap);
  const secondaryWidth = rowWidth(rCount, secondary, secondaryGap);
  const loadoutWidth = Math.min(available, Math.max(primaryWidth, secondaryWidth));
  const rowGap = 6 * s, keyHeight = mobile ? 0 : 14 * s;
  const loadoutHeight = primary + (mobile && collapsed ? 0 : rowGap + secondary + keyHeight);
  const loadout = { x: mobile ? slotLeft : safe.left + (usableW - loadoutWidth) / 2,
    bottom: safe.bottom + 7 * s, width: loadoutWidth, height: loadoutHeight,
    primary, secondary, primaryGap, secondaryGap, primaryWidth, secondaryWidth, rowGap, keyHeight,
    scroll: primaryWidth > loadoutWidth + 0.5 || secondaryWidth > loadoutWidth + 0.5 };

  const chatX = quick.x + quick.size + 28 * s;
  const chatBeside = loadout.x - chatX - 18 * s;
  const chatRaised = mobile || chatBeside < 255 * s;
  const chat = { x: mobile ? loadout.x : chatX,
    width: Math.max(120, Math.min(420 * s, chatRaised ? width - safe.right - (mobile ? loadout.x : chatX) - 12 * s : chatBeside)),
    bottom: chatRaised ? loadout.bottom + loadout.height + 10 * s : safe.bottom + 20 * s };
  const panelLeft = mobile ? safe.left : quick.x + quick.size + 20 * s;
  const panelBottom = safe.bottom + 18 * s;
  const panelMaxHeight = Math.max(160, usableH - 76 * s);
  const panelWidth = value => Math.min(value * s, width - safe.right - panelLeft - 10 * s);
  const minimapSize = Math.min(100 * s, usableW * 0.24, usableH * 0.25);
  const minimap = { x: width - safe.right - 20 * s - minimapSize,
    y: safe.top + 18 * s, size: minimapSize };
  const fullSize = Math.min(272 * s, usableW * 0.38, usableH * 0.48);
  const fullMinimap = { x: width - safe.right - 20 * s - fullSize, y: minimap.y, size: fullSize };

  return { width, height, mobile, scale, safe, top, quick, loadout, chat, minimap, fullMinimap,
    portraitOrigin: { x: safe.left, y: top.y + top.size + 20 * s },
    panel: { left: panelLeft, bottom: panelBottom, maxHeight: panelMaxHeight,
      craftWidth: panelWidth(612), craftHeight: Math.min(698 * s, panelMaxHeight),
      talentWidth: panelWidth(654), talentHeight: Math.min(654 * s, panelMaxHeight),
      inventoryWidth: panelWidth(520), inventoryHeight: Math.min(640 * s, panelMaxHeight),
      top: top.y + top.size + 6 * s,
      settingsWidth: Math.min(480 * s, usableW - 16 * s),
      settingsHeight: Math.min(600 * s, usableH - top.size - 32 * s),
      codexWidth: Math.min(760 * s, usableW - 16 * s),
      codexHeight: Math.min(640 * s, usableH - top.size - 32 * s) },
    actionBottom: mobile && width < height ? loadout.bottom + loadout.height + 58 * s : safe.bottom + 10,
  };
}

// Returns the actual canvas coordinates used by both owner and squad HUDs.
export function getHudPortraitLayout(layout, memberIndex = -1) {
  const s = layout.scale, member = memberIndex >= 0;
  const centerY = layout.portraitOrigin.y + (member ? 132 + memberIndex * 62 : 32) * s;
  const x = layout.portraitOrigin.x;
  return { scale: s, flower: { x: x + 60 * s, y: centerY }, radius: (member ? 25 : 32) * s,
    health: { x: x + (member ? 91 : 100) * s, y: centerY - (member ? 14 : 18) * s,
      width: (member ? 156 : 195) * s, height: (member ? 28 : 36) * s },
    backX: x + 60 * s,
    experience: { x: x + 114 * s, y: centerY + 21 * s, width: 166 * s, height: 20 * s },
    levelY: centerY + 16 * s };
}

export function writeUiLayoutVariables(root, layout) {
  const { scale: s, top, quick, loadout: slots, chat, panel } = layout;
  const values = {
    "--ui-scale": String(s),
    "--slot-primary-size": `${slots.primary}px`, "--slot-secondary-size": `${slots.secondary}px`,
    "--slot-primary-gap": `${slots.primaryGap}px`, "--slot-secondary-gap": `${slots.secondaryGap}px`,
    "--slot-row-gap": `${slots.rowGap}px`, "--slot-key-height": `${slots.keyHeight}px`,
    "--loadout-left": `${slots.x}px`, "--loadout-bottom": `${slots.bottom}px`,
    "--loadout-width": `${slots.width}px`, "--loadout-height": `${slots.height}px`,
    "--top-action-left": `${top.x}px`, "--top-action-top": `${top.y}px`,
    "--top-action-size": `${top.size}px`, "--top-action-gap": `${top.gap}px`,
    "--quick-left": `${quick.x}px`, "--quick-bottom": `${quick.bottom}px`,
    "--quick-size": `${quick.size}px`, "--quick-gap": `${quick.gap}px`,
    "--chat-left": `${chat.x}px`, "--chat-bottom": `${chat.bottom}px`, "--chat-width": `${chat.width}px`,
    "--panel-left": `${panel.left}px`, "--panel-bottom": `${panel.bottom}px`, "--panel-top": `${panel.top}px`,
    "--craft-width": `${panel.craftWidth}px`, "--craft-height": `${panel.craftHeight}px`,
    "--talent-width": `${panel.talentWidth}px`, "--talent-height": `${panel.talentHeight}px`,
    "--inventory-width": `${panel.inventoryWidth}px`, "--inventory-height": `${panel.inventoryHeight}px`,
    "--settings-width": `${panel.settingsWidth}px`, "--settings-height": `${panel.settingsHeight}px`,
    "--codex-width": `${panel.codexWidth}px`, "--codex-height": `${panel.codexHeight}px`,
    "--touch-action-bottom": `${layout.actionBottom}px`,
  };
  for (const [name, value] of Object.entries(values)) {
    if (root.style.getPropertyValue(name) !== value) root.style.setProperty(name, value);
  }
}
