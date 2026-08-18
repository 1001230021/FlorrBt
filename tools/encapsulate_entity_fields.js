#!/usr/bin/env node
"use strict";
// Encapsulates CEntity public fields: rewrites `X->m_field` / `X.m_field` /
// bare `m_field` into reference accessors `X->Field()`. Skips entity.h
// (edited manually), network_module.cpp (owns its own m_generation), and the
// SMobProto `proto.m_team` pattern.

const fs = require("fs");
const path = require("path");
const root = path.join(__dirname, "..");

const fields = {
  m_pos: "Pos",
  m_prev_pos: "PrevPos",
  m_radius: "Radius",
  m_skip_world_tick: "SkipWorldTick",
  m_allow_skip_tick: "AllowSkipTick",
  m_tick_mode: "TickMode",
  m_tick_horizon: "TickHorizon",
  m_id: "Id",
  m_generation: "Generation",
  m_live_index: "LiveIndex",
  m_cleanup_index: "CleanupIndex",
  m_always_tick_index: "AlwaysTickIndex",
  m_conditional_tick_index: "ConditionalTickIndex",
  m_large_spatial_index: "LargeSpatialIndex",
  m_active_tick_marker: "ActiveTickMarker",
  m_collision_sync_marker: "CollisionSyncMarker",
  m_is_marked_for_des: "IsMarkedForDes",
  m_tags: "Tags",
  m_team: "Team",
  m_mass: "Mass",
  m_health: "Health",
  m_facing_angle: "FacingAngle",
  m_has_facing: "HasFacing",
  m_entity_stats: "EntityStats",
};
const fieldRe = new RegExp(`\\b(${Object.keys(fields).join("|")})\\b`, "g");

function rewrite(text, allowBare, file) {
  // 1. `X->m_field` -> `X->Field()`
  text = text.replace(
    new RegExp(`\\b([A-Za-z_$][\\w$]*)\\s*->\\s*(${Object.keys(fields).join("|")})\\b`, "g"),
    (m, recv, fld) => `${recv}->${fields[fld]}()`,
  );
  // 2. `X.m_field` -> `X.Field()` except SMobProto `proto.m_team`.
  text = text.replace(
    new RegExp(`\\b([A-Za-z_$][\\w$]*)\\.(${Object.keys(fields).join("|")})\\b`, "g"),
    (m, recv, fld) => (recv === "proto" ? m : `${recv}.${fields[fld]}()`),
  );
  // 3. bare `m_field` (member context) -> `Field()`, skipping ctor init lists.
  if (allowBare) {
    text = text.replace(
      new RegExp(`\\b(${Object.keys(fields).join("|")})(?!\\s*\\()` , "g"),
      (m, fld) => `${fields[fld]}()`,
    );
  }
  return text;
}

for (const file of walk(path.join(root, "src", "Server"))) {
  if (!file.endsWith(".cpp") && !file.endsWith(".h")) continue;
  if (file.endsWith("entity.h")) continue;
  const allowBare = !file.endsWith("network_module.cpp");
  const before = fs.readFileSync(file, "utf8");
  const after = rewrite(before, allowBare, file);
  if (after !== before) {
    fs.writeFileSync(file, after, "utf8");
    console.log(`rewrote ${path.relative(root, file)}`);
  }
}
console.log("done");

function walk(dir) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(p));
    else out.push(p);
  }
  return out;
}
