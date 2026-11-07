import assert from "node:assert/strict";
import { calculateUiLayout } from "../web/src/ui_layout.js";
import { defaultSocketEndpoint } from "../web/src/socket_endpoint.js";

const desktop = calculateUiLayout({ width: 1910, height: 884, primaryCount: 10, profile: "reference" });
assert.equal(desktop.panel.inventoryWidth, 360);
assert.equal(desktop.panel.craftWidth, 536);
assert.equal(desktop.panel.talentWidth, 576);
assert.equal(desktop.panel.codexWidth, 528);
assert.equal(desktop.minimap.size, 100);
for (const [width, height, mobile] of [[1910, 884, false], [1280, 720, false], [390, 844, true], [844, 390, true]]) {
  const layout = calculateUiLayout({ width, height, mobile, profile: "reference", primaryCount: 10, secondaryCount: 10 });
  assert(layout.loadout.x >= 0 && layout.loadout.x + layout.loadout.width <= width);
  assert(layout.panel.inventoryWidth > 0);
  assert(layout.chat.x + layout.chat.width <= width);
}
assert.equal(calculateUiLayout({ width: 1910, height: 884 }).panel.inventoryWidth, 360);
assert.equal(defaultSocketEndpoint({ protocol: "https:", host: "game.example:8443" }), "wss://game.example:8443/ws");
assert.equal(defaultSocketEndpoint({ protocol: "http:", host: "localhost:8080" }), "ws://localhost:8080/ws");
assert.equal(defaultSocketEndpoint({ protocol: "file:", host: "" }), "ws://127.0.0.1:8080/ws");
console.log("Shared default/reference layout and secure endpoint checks passed.");
