export const clientCommandSpecs = Object.freeze([
  {
    name: "version",
    aliases: ["ver"],
    params: "",
    args: "",
    help: "ui.console.help.version",
  },
  {
    name: "change_language",
    aliases: ["change-language", "language", "lang"],
    params: "[locale]",
    args: "locale",
    help: "ui.console.help.language",
  },
  {
    name: "say",
    params: "[channel] [message]",
    args: "channel",
    help: "ui.console.help.say",
  },
  {
    name: "g",
    params: "[message]",
    args: "",
    help: "ui.console.help.g",
  },
  {
    name: "l",
    params: "[message]",
    args: "",
    help: "ui.console.help.l",
  },
  {
    name: "whisper",
    aliases: ["w"],
    params: "[player] [message]",
    args: "player",
    help: "ui.console.help.whisper",
  },
  {
    name: "map",
    params: "[map]",
    args: "map",
    help: "ui.console.help.map",
  },
  {
    name: "login_map",
    aliases: ["loginmap"],
    params: "[map|reset]",
    args: "map",
    help: "ui.console.help.login_map",
  },
  {
    name: "login_map_x",
    params: "[number|reset]",
    args: "number",
    help: "ui.console.help.login_map_x",
  },
  {
    name: "login_map_y",
    params: "[number|reset]",
    args: "number",
    help: "ui.console.help.login_map_y",
  },
  {
    name: "login_map_horizon",
    params: "[number|reset]",
    args: "number",
    help: "ui.console.help.login_map_horizon",
  },
  {
    name: "hitbox",
    aliases: ["hitboxes"],
    params: "[0|1|on|off]",
    args: "bool",
    help: "ui.console.help.hitbox",
  },
  {
    name: "colorful_map",
    params: "[0|1|on|off]",
    args: "bool",
    help: "ui.console.help.colorful_map",
  },
  {
    name: "set",
    params: "[config] [value]",
    args: "config",
    help: "ui.console.help.set",
  },
  {
    name: "slider",
    params: "[config] [start] [end] [step]",
    args: "config",
    help: "ui.console.help.slider",
  },
  {
    name: "keyboard_control",
    params: "[0|1|on|off]",
    args: "bool",
    help: "ui.console.help.keyboard_control",
  },
  {
    name: "mobile",
    aliases: ["mobile_mode", "touch"],
    params: "[on|off|auto|toggle]",
    args: "mobile_mode",
    help: "ui.console.help.mobile",
  },
]);

export function clientCommandNames() {
  const names = [];
  for (const spec of clientCommandSpecs) {
    names.push(spec.name);
    if (spec.aliases) names.push(...spec.aliases);
  }
  return names;
}

export function findClientCommandSpec(name) {
  const normalized = String(name || "").toLowerCase();
  if (!normalized) return null;
  return (
    clientCommandSpecs.find((spec) => spec.name === normalized) ||
    clientCommandSpecs.find(
      (spec) => spec.aliases?.some((alias) => alias === normalized),
    ) ||
    null
  );
}
