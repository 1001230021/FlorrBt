# Florr-style UI geometry

Reference: the user's `QQ20260925-022649-HD.mp4` and
`QQ20260925-015439-HD.mp4`, including retained full-resolution frames in
`logs/florr_full_20260925/` and `logs/florr_craft_talent_20260925/`.
Measurements exclude the browser chrome (roughly 1910 × 884 game pixels).
Video compression/window borders mean these are reference geometry, not a
claim of pixel-identical output on every device.

## Shared layout

`web/src/ui_layout.js` calculates Canvas and DOM geometry from one viewport,
safe-area, mobile mode and actual primary/reserve slot count. It does not
depend on world zoom or DPR. `web/layout.css` owns the layout overrides;
the existing sheets retain colours, animations and interaction styling.
No per-frame DOM measurement is needed: resize, mode, slot-count and
collapse changes update the CSS variables.

At reference size (10 equipped + 10 reserve slots):

| Component | Geometry |
|---|---|
| Top action buttons | 48px square, 8px outer margin, 7px gap |
| Owner portrait | Centre (60,108), radius 32px |
| Owner health | (100,90), 195×36px |
| Owner experience | (114,129), 166×20px |
| Squad | Smaller portrait, centred name, level below; 62px row step |
| Main/reserve cards | 77/55px elements, 70/50px visible SVG silhouettes |
| Loadout | Bottom centred; reserve centred beneath main; real bind hints |
| Quick actions | 64px squares, 10px gaps, bottom-left |
| Chat | (100px from left, 20px from bottom); raises on narrow screens |
| Local minimap | 100px, 20px right margin, 18px top margin |
| Craft | Left 92px, bottom 18px, 612×698px |
| Talents | Same left/bottom, 654×654px; root near lower centre |
| Inventory | Same left/bottom, content-height with scroll cap |
| Settings/gallery | Below the top action strip, content-height with cap |

Settings toggles now precede their labels. Craft remains a left orbit/right
action layout; empty wells follow the SVG's actual 100/110 silhouette, and
successful crafts do not leave dark wells. The talent footer overlays the
tree instead of consuming a tall empty row.

Card label limits (`petal_label_width`, default 45; font cap, default 14) are
specified at the same 56px reference size used by world-drop cards. Names,
stack-count labels and multi-copy offsets use the actual card container size.
Only the DOM label is drawn; embedded SVG labels are stripped consistently.
Slot swaps clone the displayed card, scale it as one layer and keep the source
DOM stable while network updates arrive. Repeated swaps reverse from the
current visual pose; resize, scene reset and backgrounding cancel cleanly.
`node tools/test_slot_swap_motion.mjs` covers these transitions without a game.

## Intentional differences

- No task cards, shop, guild, friend or other nonfunctional placeholder buttons.
- Keep real FBT rarity columns, stack mode, talent prerequisites/costs, inventory
  quantities, authentication/password/server fields and all input bindings.
- F1 remains the requested DDNet-style console; Dev Debug remains its own UI.
- Mobile preserves the requested corner icons, two slot rows, chat above
  them, simple A/B/X and touch-only joystick. Very long slot rows can scroll
  rather than vanish off-screen. Soft-keyboard and safe-area handling remain.
- Login fields float over the world, but the FBT name and authentication
  requirements are not replaced with Florr's branding or a fake login flow.

## Verification

`node tools/test_ui_layout.mjs` checks the reference anchors, safe areas,
desktop/mobile/4K sizes, 5–24 slots, reserve collapse and CSS/Canvas integration.
Existing mobile/input/craft/talent/codex/death/dev-debug checks also apply.

The Browser skill was consulted for a local visual preview, but its runtime
reported no available browsers. No alternative browser automation was used,
no game server/account was opened, and no pixel-perfect visual validation is
claimed. Hard-refresh the webpage to load the new layout; no server build or
protocol update is needed.
