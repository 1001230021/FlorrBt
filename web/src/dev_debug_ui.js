import { DebugFrameDecoder, packDebugMessage, DEBUG_SELECTION_LIMIT } from "./dev_debug_protocol.js";
import { getDebugEntityKey, getDebugWorldKey, pointToDebugWorld, pointToDebugScreen, zoomDebugCamera,
  hitTestDebugEntities, boxSelectDebugEntities, toggleDebugSelection, sortDebugPlayers, isDebugEntityInView } from "./dev_debug_model.js";
import { createMapRenderer } from "./map_renderer.js";
import { createDebugTypePicker } from "./dev_type_picker.js";
import { stepFlowerFace } from "./flower_renderer.js";
import { flagAttacking, flagDefending, flagDead, playerFlowerType, normalFlowerType, mechaFlowerType, dummyType, titanType } from "./game_ids.js";
import { createBurrowParticles } from "./burrow_particles.js";

const make = (tag, className = "", text = "") => {
  const node = document.createElement(tag); node.className = className; node.textContent = text; return node;
};
const button = (label, action, className = "") => {
  const node = make("button", className, label); node.type = "button"; node.addEventListener("click", action); return node;
};
const format = value => Number.isFinite(value) ? value.toLocaleString(undefined, { maximumFractionDigits: 2 }) : "—";
const coord = point => `${format(point.x)}, ${format(point.y)}`;

export function createDevDebugUi({ renderEntities, getTypeIcon, getTypeName, onOpen = () => {}, onClose = () => {},
  releaseVisual = () => {}, getCullScale = () => 3 } = {}) {
  let root = null, ws = null, ready = false, decoder = null, requestId = 0, visualId = -1000000;
  let ctx, viewport, canvas, worldSelect, playersPanel, inspector, logs, commandInput, statusLabel, selectionLabel, positionLabel;
  let filterSelect, hiddenToggle, idToggle, playerSearch, mapRenderer, mapState, resizeObserver, picker;
  let contextMenu = null, form = null, drag = null, lastPoint = null, followPlayer = null;
  let catalogue = { types: [], rarities: [], commands: [] }, worlds = [], players = [];
  let world = null, camera = { x: 0, y: 0, scale: 0.65 }, size = { width: 1, height: 1 }, dpr = 1;
  let selection = new Map(), records = new Map(), pendingTerrain = [], pendingRequests = new Map();
  let lastFrame = 0, lastView = 0, viewDirty = true, lastInspect = 0, inspected = null, lastWorldOptions = "";
  let history = [], historyAt = 0, consoleOptions;
  let backgroundUi = [];
  const renderEntitiesById = new Map();
  const flowerTypes = new Set([playerFlowerType, normalFlowerType, mechaFlowerType, dummyType, titanType]);
  const rawEntities = () => Array.from(records.values(), record => record.raw);
  const worldRef = () => ({ world: world?.id, worldGeneration: world?.generation });
  const mapPathOf = value => value?.mapPath || value?.map || "";
  const worldMatches = msg => world && getDebugWorldKey(msg.world, msg.worldGeneration) === getDebugWorldKey(world.id, world.generation);
  const visible = entity => (hiddenToggle?.checked || entity.visible !== false);
  const burrowParticles = createBurrowParticles({ isVisible: (snap, position) => {
    if (document.hidden || !visible(snap)) return false;
    const point = pointToDebugScreen(position, camera, size), radius = (snap.radius || 0) * camera.scale;
    return radius >= 3 && point.x + radius >= 0 && point.y + radius >= 0 &&
      point.x - radius <= size.width && point.y - radius <= size.height;
  } });
  const selectable = entity => visible(entity) && (filterSelect.value === "all" ||
    (filterSelect.value === "mob" ? entity.family === 1 : entity.family === 4 ||
      (entity.family === 2 && entity.entityType >= 100 && entity.entityType < 180)));

  function appendLog(text, severity = "") {
    if (!logs) return;
    const bottom = logs.scrollHeight - logs.scrollTop - logs.clientHeight < 32;
    const line = make("div", `dev-log ${severity || (/\[ERROR\]|\[FATAL\]/.test(text) ? "error" : /\[WARN\]/.test(text) ? "warn" : "")}`, String(text).slice(0, 8192));
    logs.append(line); while (logs.children.length > 600) logs.firstElementChild.remove();
    if (bottom) logs.scrollTop = logs.scrollHeight;
  }
  function setStatus(text, bad = false) {
    if (!statusLabel) return;
    statusLabel.textContent = text; statusLabel.classList.toggle("error", bad);
  }
  function send(message) {
    if (!ws || ws.readyState !== WebSocket.OPEN || (!ready && message.op !== "hello")) return false;
    if (ws.bufferedAmount > 65536) return false;
    ws.send(packDebugMessage(message)); return true;
  }
  function request(op, payload = {}) {
    if (!ready) return Promise.reject(new Error("调试通道未授权或已断开"));
    if (pendingRequests.size >= 32) return Promise.reject(new Error("等待服务端处理中的请求过多"));
    const id = ++requestId;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { pendingRequests.delete(id); reject(new Error("请求超时；请确认服务端状态")); }, 8000);
      pendingRequests.set(id, { resolve, reject, timer });
      try {
        if (!send({ op, request: id, ...payload })) throw new Error("调试连接忙或已断开");
      } catch (error) { clearTimeout(timer); pendingRequests.delete(id); reject(error); }
    });
  }
  async function act(op, payload) {
    try {
      const reply = await request(op, payload); appendLog(reply.message, "success"); return reply;
    } catch (error) { appendLog(error.message, "error"); return null; }
  }
  function cancelRequests(reason) {
    for (const pending of pendingRequests.values()) { clearTimeout(pending.timer); pending.reject(new Error(reason)); }
    pendingRequests.clear();
  }
  function closeMenu() { contextMenu?.remove(); contextMenu = null; }
  function closeForm() { picker?.close(); form?.remove(); form = null; }
  function showMenu(title, actions, x, y) {
    closeMenu(); contextMenu = make("div", "dev-menu"); contextMenu.setAttribute("role", "menu");
    contextMenu.append(make("strong", "", title));
    for (const item of actions) {
      const control = button(item.label, () => { closeMenu(); item.run(); }, item.danger ? "danger" : "");
      control.disabled = !!item.disabled; contextMenu.append(control);
    }
    root.append(contextMenu);
    const rect = contextMenu.getBoundingClientRect();
    contextMenu.style.left = `${Math.max(8, Math.min(x, window.innerWidth - rect.width - 8))}px`;
    contextMenu.style.top = `${Math.max(8, Math.min(y, window.innerHeight - rect.height - 8))}px`;
  }
  function copy(text) {
    if (navigator.clipboard?.writeText) navigator.clipboard.writeText(text).catch(() => appendLog(text));
    else appendLog(text);
  }
  function renderDetails(title, value, actions = []) {
    if (!inspector) return;
    const retain = inspector.dataset.detailTitle === title;
    const scroll = retain ? inspector.scrollTop : 0;
    const expanded = new Map(retain ? Array.from(inspector.querySelectorAll(".dev-detail-section"),
      detail => [detail.firstElementChild.textContent, detail.open]) : []);
    inspector.dataset.detailTitle = title;
    inspector.replaceChildren(make("h3", "", title));
    const controls = make("div", "dev-actions");
    for (const item of actions) controls.append(button(item.label, item.run, item.danger ? "danger" : ""));
    inspector.append(controls);
    const fields = make("dl", "dev-fields");
    for (const [key, item] of Object.entries(value || {})) {
      if (item !== null && typeof item === "object") continue;
      fields.append(make("dt", "", key), make("dd", "", typeof item === "number" ? format(item) : String(item ?? "—")));
    }
    inspector.append(fields);
    const sections = Object.entries(value || {}).filter(([, item]) => item !== null && typeof item === "object");
    for (const [key, item] of sections) {
      const detail = make("details", "dev-detail-section");
      detail.open = expanded.has(key) ? expanded.get(key) : ["pos", "stats", "velocity"].includes(key);
      detail.append(make("summary", "", key), make("pre", "", JSON.stringify(item, null, 2))); inspector.append(detail);
    }
    inspector.scrollTop = scroll;
  }
  async function inspectEntity(entity) {
    if (!entity || !world) return;
    const key = getDebugEntityKey(entity), atWorld = getDebugWorldKey(world.id, world.generation);
    inspected = { kind: "entity", key }; lastInspect = performance.now();
    try {
      const reply = await request("inspect", { ...worldRef(), id: entity.entityId, generation: entity.generation });
      if (!root || inspected?.key !== key || atWorld !== getDebugWorldKey(world?.id, world?.generation)) return;
      renderDetails(`实体 #${entity.entityId} · 已选 ${selection.size}`, reply.data, [
        { label: "实体操作…", run: () => entityMenu(entity, window.innerWidth - 280, 130) },
        { label: "复制引用", run: () => copy(`${world.id}:${entity.entityId}:${entity.generation}`) },
      ]);
    } catch (error) { if (root) appendLog(error.message, "warn"); }
  }
  async function inspectPlayer(player) {
    const identity = `${player.id}:${player.account}`;
    inspected = { kind: "player", key: identity };
    try {
      const reply = await request("player", { player: player.id, account: player.account });
      if (!root || inspected?.key !== identity) return;
      renderDetails(`玩家 ${player.name} · #${player.id}`, reply.data, [
        { label: "玩家操作…", run: () => playerMenu(player, window.innerWidth - 280, 130) },
        { label: "定位", run: () => locatePlayer(player) },
        { label: "背包", run: () => typeForm("inventory", { player }) },
      ]);
    } catch (error) { appendLog(error.message, "error"); }
  }
  function selectionChanged() {
    selectionLabel.textContent = `已选 ${selection.size} / ${DEBUG_SELECTION_LIMIT}`;
    if (selection.size === 1) {
      const record = records.get(selection.keys().next().value); if (record) void inspectEntity(record.raw);
    } else {
      inspected = null;
      renderDetails(selection.size ? "待操作组" : "选择实体", { count: selection.size,
        help: "左键点选 · 拖动框选 · Shift 左键增减 · 右键选中实体批量操作",
        targets: [...selection.values()] }, selection.size ? [
          { label: "整组操作…", run: () => groupMenu(window.innerWidth - 280, 130) },
          { label: "清空组", run: () => { selection.clear(); selectionChanged(); } },
        ] : []);
    }
  }
  function entityMenu(entity, x, y) {
    if (!selection.has(getDebugEntityKey(entity))) {
      selection = toggleDebugSelection(selection, entity); selectionChanged();
    }
    groupMenu(x, y);
  }
  function groupMenu(x, y) {
    const targets = [...selection.values()].map(ref => ({ ...ref })), ref = worldRef();
    if (!targets.length) return;
    const perform = (action, fields = {}) => act("entity_action", { ...ref, targets, action, ...fields });
    showMenu(`实体操作 · ${targets.length} 个`, [
      { label: "恢复生命", run: () => perform("heal") },
      { label: "设置阵营…", run: () => {
        const value = window.prompt("阵营 ID（0–255）", "2"); if (value === null) return;
        const team = Number(value); if (!Number.isInteger(team) || team < 0 || team > 255) return appendLog("阵营必须是 0–255 的整数", "error");
        void perform("team", { team });
      } },
      { label: "移动整组到坐标标记（保留队形）", disabled: !lastPoint, run: () => perform("teleport", lastPoint) },
      { label: "复制世界 / 实体 ID / 代次", run: () => copy(targets.map(item => `${ref.world}:${item.id}:${item.generation}`).join("\n")) },
      { label: "击杀（正常伤害结算）", danger: true, run: () => {
        if (window.confirm(`确认击杀 ${targets.length} 个实体？可能产生掉落。`)) void perform("kill");
      } },
      { label: "移除（不产生击杀奖励）", danger: true, run: () => {
        if (window.confirm(`确认移除 ${targets.length} 个实体？此操作无法撤销。`)) void perform("despawn");
      } },
    ], x, y);
  }
  function pointMenu(point, x, y, entity) {
    lastPoint = { ...point }; positionLabel.textContent = `坐标标记 ${coord(point)}`;
    showMenu(`坐标 ${coord(point)}`, [
      { label: "复制坐标", run: () => copy(`${point.x.toFixed(2)},${point.y.toFixed(2)}`) },
      { label: "在此生成生物…", run: () => typeForm("spawn", { kind: "mob", point }) },
      { label: "在此生成花瓣掉落…", run: () => typeForm("spawn", { kind: "petal", point }) },
      { label: `将待操作组移至此处（${selection.size}）`, disabled: !selection.size, run: () => act("entity_action", {
        ...worldRef(), targets: [...selection.values()], action: "teleport", ...point,
      }) },
      ...(entity ? [{ label: `选择实体 #${entity.entityId}`, run: () => {
        selection = toggleDebugSelection(selection, entity); selectionChanged();
      } }] : []),
    ], x, y);
  }
  function locatePlayer(player) {
    const targetWorld = worlds.find(item => item.id === player.world && item.generation === player.worldGeneration);
    if (!targetWorld || !player.pos) return appendLog("玩家当前没有可定位的实体", "warn");
    setWorld(targetWorld, player.pos); followPlayer = { id: player.id, account: player.account };
  }
  function playerMenu(player, x, y) {
    const ref = { player: player.id, account: player.account };
    const perform = (action, fields = {}) => act("player_action", { ...ref, action, ...fields });
    showMenu(`${player.name} · #${player.id}`, [
      { label: "详细信息", run: () => inspectPlayer(player) },
      { label: "定位 / 跟随视角", run: () => locatePlayer(player) },
      { label: "操作背包…", run: () => typeForm("inventory", { player }) },
      { label: "恢复生命", run: () => perform("heal") },
      { label: "传送到当前世界坐标标记", disabled: !lastPoint, run: () => {
        if (window.confirm(`把 ${player.name} 传送到世界 ${world.id} 的 ${coord(lastPoint)}？`))
          void perform("teleport", { ...worldRef(), ...lastPoint });
      } },
      { label: "禁言…", run: () => {
        const value = window.prompt("禁言秒数", "300"); if (value !== null) void perform("mute", { seconds: Number(value) });
      } },
      { label: "解除禁言", run: () => perform("unmute") },
      { label: "击杀玩家", danger: true, run: () => { if (window.confirm(`确认击杀 ${player.name}？`)) void perform("kill"); } },
      { label: "踢出玩家", danger: true, run: () => { if (window.confirm(`确认踢出 ${player.name}？`)) void perform("kick"); } },
    ], x, y);
  }
  function typeForm(mode, options) {
    closeForm();
    const ref = worldRef(), kind = options.kind || "petal";
    let type = null;
    const shade = make("div", "dev-form-shade"), panel = make("form", "dev-form"); form = shade;
    panel.append(make("h3", "", mode === "spawn" ? (kind === "mob" ? "生成生物" : "生成花瓣掉落") : `背包 · ${options.player.name}`));
    const pick = button("选择 type…", async () => {
      const chosen = await picker.open(kind, catalogue.types);
      if (chosen && shade.isConnected) { type = chosen; pick.textContent = `${getTypeName?.(type) || type.name} · #${type.id}`; }
    });
    const rarity = make("select");
    for (const item of catalogue.rarities) { const option = make("option", "", `${item.name} · ${item.id}`); option.value = String(item.id); rarity.append(option); }
    const count = make("input"); count.type = "number"; count.min = mode === "spawn" ? "1" : "0";
    count.max = mode === "spawn" ? "64" : "1000000000"; count.step = "1"; count.value = "1";
    const operation = make("select");
    for (const [value, label] of [["add", "增加"], ["remove", "移除"], ["set", "设置为指定数量"]]) {
      const option = make("option", "", label); option.value = value; operation.append(option);
    }
    const field = (label, input) => { const row = make("label", "dev-form-field"); row.append(make("span", "", label), input); panel.append(row); };
    field("类型", pick); field("稀有度（独立选择）", rarity);
    if (mode === "inventory") field("背包操作", operation);
    field("数量", count);
    panel.append(make("p", "dev-muted", mode === "spawn" ? `世界 ${ref.world} · ${coord(options.point)}` : "仅修改背包可用数量，不清空已装备主副槽。"));
    const error = make("p", "error"), controls = make("div", "dev-actions"), submit = make("button", "primary", "执行"); submit.type = "submit";
    controls.append(button("取消", closeForm), submit); panel.append(error, controls); shade.append(panel); root.append(shade);
    shade.addEventListener("pointerdown", event => { if (event.target === shade) closeForm(); });
    panel.addEventListener("submit", async event => {
      event.preventDefault();
      const amount = Number(count.value), r = Number(rarity.value);
      if (!type || !Number.isInteger(amount) || amount < Number(count.min) || amount > Number(count.max) || !r) {
        error.textContent = "请选择类型、稀有度并填写有效数量"; return;
      }
      if (mode === "inventory" && operation.value !== "add" &&
          !window.confirm(`确认对 ${options.player.name} 的 ${type.name} / ${rarity.selectedOptions[0].textContent} 执行 ${operation.selectedOptions[0].textContent} ${amount}？`)) return;
      submit.disabled = true;
      try {
        const reply = await request(mode === "spawn" ? "spawn" : "player_action", mode === "spawn"
          ? { ...ref, ...options.point, kind, type: type.id, rarity: r, count: amount }
          : { player: options.player.id, account: options.player.account, action: "inventory", mode: operation.value, type: type.id, rarity: r, count: amount });
        appendLog(reply.message, "success"); closeForm();
        if (mode === "inventory") void inspectPlayer(options.player);
      } catch (failure) { error.textContent = failure.message; }
      finally { submit.disabled = false; }
    });
  }
  function renderPlayers() {
    if (!playersPanel) return;
    const query = playerSearch.value.toLocaleLowerCase();
    const rows = sortDebugPlayers(players, world?.id, world?.generation)
      .filter(player => `${player.id} ${player.name} ${player.account}`.toLocaleLowerCase().includes(query));
    const fragment = document.createDocumentFragment();
    for (const player of rows) {
      const row = button("", () => inspectPlayer(player), "dev-player");
      const sameWorld = player.world === world?.id && player.worldGeneration === world?.generation;
      row.classList.toggle("nearby", sameWorld); row.classList.toggle("offline", !player.connected);
      row.append(make("strong", "", `${player.name} #${player.id}`),
        make("small", "", `${sameWorld ? "● 同世界" : `世界 ${player.world ?? "—"}`} · Lv${player.level} · ${player.connected ? "在线" : "离线保护"}`),
        make("small", "", `${player.account} · HP ${format(player.health)}`));
      row.addEventListener("contextmenu", event => { event.preventDefault(); playerMenu(player, event.clientX, event.clientY); });
      row.addEventListener("dblclick", () => locatePlayer(player)); fragment.append(row);
    }
    playersPanel.replaceChildren(fragment);
  }
  function loadWorldMap(next) {
    void mapRenderer.loadMap(mapPathOf(next)).catch(error => appendLog(error.message, "error"));
  }
  function setWorld(next, center = null) {
    const changed = !world || getDebugWorldKey(world.id, world.generation) !== getDebugWorldKey(next.id, next.generation);
    world = next;
    worldSelect.value = getDebugWorldKey(next.id, next.generation);
    if (changed) {
      burrowParticles.clear();
      for (const record of records.values()) releaseVisual(record.snapshot.entityId);
      records.clear(); selection.clear(); inspected = null; pendingTerrain = []; lastPoint = null; followPlayer = null;
      closeMenu(); closeForm(); drag = null; selectionChanged();
      mapState.mapLoadToken++; mapState.map = null; mapState.mapLoadPromise = null; mapState.mapLoadPath = "";
      loadWorldMap(next);
    }
    if (center) { camera.x = center.x; camera.y = center.y; }
    else if (changed) { camera.x = (next.width || 2000) / 2; camera.y = (next.height || 2000) / 2; }
    viewDirty = true; lastView = 0; renderPlayers();
  }
  function receive(message) {
    if (!root) return;
    if (message.kind === "hello") {
      ready = true; world = { id: message.world, generation: message.worldGeneration };
      camera.x = message.center?.x || 0; camera.y = message.center?.y || 0;
      setStatus(`已授权 · ${message.operator}`); viewDirty = true;
    } else if (message.kind === "catalog") {
      catalogue = { types: message.types || [], rarities: message.rarities || [], commands: message.commands || [] };
      consoleOptions.replaceChildren();
      for (const command of catalogue.commands) { const option = make("option"); option.value = command.name; option.label = command.usage; consoleOptions.append(option); }
    } else if (message.kind === "status") {
      worlds = message.worlds || []; players = message.players || [];
      const optionsKey = worlds.map(item => getDebugWorldKey(item.id, item.generation)).join(",");
      if (lastWorldOptions !== optionsKey) {
        lastWorldOptions = optionsKey; worldSelect.replaceChildren();
        for (const item of worlds) { const option = make("option"); option.value = getDebugWorldKey(item.id, item.generation); worldSelect.append(option); }
      }
      for (let i = 0; i < worlds.length; i++) worldSelect.options[i].textContent = `#${worlds[i].id} ${worlds[i].map} · ${worlds[i].controller} · ${worlds[i].entities}`;
      const current = worlds.find(item => item.id === world?.id && item.generation === world?.generation);
      if (current) {
        worldSelect.value = getDebugWorldKey(current.id, current.generation);
        if (mapPathOf(world) !== mapPathOf(current)) {
          world = current; loadWorldMap(current);
        } else world = current;
      } else if (worlds.length) setWorld(worlds[0]);
      else { ready = !!ws && ws.readyState === WebSocket.OPEN; setStatus("当前没有可观察的世界", true); }
      renderPlayers();
      if (current) setStatus(`独立观察者 · TPS ${format(message.tps)} · tick ${format(message.tickMs)}ms`);
    } else if (message.kind === "snapshot" && worldMatches(message)) {
      const next = new Map(), now = performance.now();
      for (const raw of message.entities || []) {
        if (!Number.isInteger(raw.entityId) || typeof raw.generation !== "string" || !raw.pos ||
            !Number.isFinite(raw.pos.x) || !Number.isFinite(raw.pos.y) || !Number.isFinite(raw.radius)) continue;
        const key = getDebugEntityKey(raw), old = records.get(key);
        burrowParticles.observe(old?.raw, raw, !!old, old?.renderPos || raw.pos);
        const entity = old || { snapshot: { entityId: --visualId }, renderPos: { ...raw.pos }, motionBlend: 0, snapshotAt: now / 1000 };
        const dt = Math.max(0.02, Math.min(0.5, now / 1000 - entity.snapshotAt));
        const previous = entity.raw?.pos || raw.pos;
        const speed = Math.hypot(raw.pos.x - previous.x, raw.pos.y - previous.y) / dt;
        entity.motionBlend = Math.max(0, Math.min(1, (speed * 0.05 - 0.16) / 1.25));
        entity.snapshot = { ...raw, entityId: entity.snapshot.entityId }; entity.raw = raw;
        entity.snapshotAt = now / 1000; next.set(key, entity);
      }
      for (const [key, record] of records) if (!next.has(key)) releaseVisual(record.snapshot.entityId);
      records = next;
      viewport.dataset.truncated = message.truncated ? "true" : "false";
      viewport.dataset.count = String(records.size);
      if (message.truncated) setStatus(`视野内 ${message.total} 个实体，仅显示最近 ${records.size} 个；请放大视图`, true);
    } else if ((message.kind === "terrain" || message.kind === "walls") && worldMatches(message)) {
      if (pendingTerrain.length < 4096) pendingTerrain.push(message);
      else setStatus("地形更新过多，请点击刷新重新同步", true);
    } else if (message.kind === "logs") {
      for (const line of message.lines || []) appendLog(String(line));
    } else if (message.kind === "result") {
      const pending = pendingRequests.get(message.request);
      if (pending) {
        pendingRequests.delete(message.request); clearTimeout(pending.timer);
        if (message.ok) pending.resolve(message); else pending.reject(new Error(message.message || "操作失败"));
      } else if (!message.ok) appendLog(message.message || "请求被拒绝", "error");
    }
  }

  function build() {
    root = make("section", "dev-debug ui"); root.setAttribute("aria-label", "Dev Debug 世界工作台");
    root.setAttribute("role", "dialog"); root.setAttribute("aria-modal", "true");
    const header = make("header", "dev-header"), brand = make("div", "dev-brand");
    brand.append(make("strong", "", "DEV / WORLD CONTROL"), make("small", "", "独立观察者 · RCON"));
    worldSelect = make("select", "dev-world-select"); worldSelect.setAttribute("aria-label", "选择世界");
    worldSelect.addEventListener("change", () => {
      const next = worlds.find(item => getDebugWorldKey(item.id, item.generation) === worldSelect.value);
      if (next) setWorld(next);
    });
    statusLabel = make("span", "dev-status", "连接调试通道…");
    header.append(brand, worldSelect, statusLabel, button("刷新", () => {
      viewDirty = true;
      if (world && !mapState.map) loadWorldMap(world);
      void act("refresh", {});
    }), button("返回游戏", close));
    const main = make("div", "dev-main"), people = make("aside", "dev-people"), view = make("section", "dev-world");
    people.append(make("h3", "", "玩家 · 同世界优先"));
    playerSearch = make("input"); playerSearch.type = "search"; playerSearch.placeholder = "玩家名 / 账号 / ID";
    playerSearch.addEventListener("input", renderPlayers); playersPanel = make("div", "dev-players"); people.append(playerSearch, playersPanel);
    const toolbar = make("div", "dev-toolbar");
    filterSelect = make("select");
    for (const [value, label] of [["mob", "选生物"], ["petal", "选花瓣 / 掉落"], ["all", "选全部实体"]]) {
      const option = make("option", "", label); option.value = value; filterSelect.append(option);
    }
    const checkbox = label => {
      const control = make("label", "dev-check"), input = make("input"); input.type = "checkbox";
      control.append(input, document.createTextNode(label)); toolbar.append(control); return input;
    };
    toolbar.append(filterSelect); hiddenToggle = checkbox("显示隐藏"); idToggle = checkbox("ID");
    toolbar.append(button("生物 type", () => picker.open("mob", catalogue.types).then(entry => { if (entry) appendLog(`mob type ${entry.id}: ${entry.name}`); })),
      button("花瓣 type", () => picker.open("petal", catalogue.types).then(entry => { if (entry) appendLog(`petal type ${entry.id}: ${entry.name}`); })));
    viewport = make("div", "dev-viewport"); canvas = make("canvas", "dev-canvas"); canvas.tabIndex = 0;
    canvas.setAttribute("aria-label", "世界视图：滚轮缩放，中键拖动，左键或框选，右键操作"); viewport.append(canvas);
    const footer = make("div", "dev-view-footer"); selectionLabel = make("span"); positionLabel = make("span", "", "滚轮缩放 · 中键平移 · 右键坐标");
    footer.append(selectionLabel, positionLabel, button("清空选择", () => { selection.clear(); selectionChanged(); }));
    view.append(toolbar, viewport, footer); inspector = make("aside", "dev-inspector"); main.append(people, view, inspector);
    const consolePanel = make("section", "dev-console"), consoleHeader = make("div", "dev-console-header");
    consoleHeader.append(make("strong", "", "SERVER CONSOLE"), make("span", "dev-muted", "直接输入服务端命令；↑↓ 历史 · Tab 补全"), button("清空日志", () => logs.replaceChildren()));
    logs = make("div", "dev-logs"); logs.setAttribute("role", "log");
    const commandForm = make("form", "dev-command-form"); commandInput = make("input"); commandInput.placeholder = "help / perf / inventory / spawn …";
    commandInput.autocomplete = "off"; commandInput.spellcheck = false;
    consoleOptions = make("datalist"); consoleOptions.id = "dev-command-options"; commandInput.setAttribute("list", consoleOptions.id);
    const execute = make("button", "primary", "执行"); execute.type = "submit";
    commandForm.append(make("span", "", ">"), commandInput, execute, consoleOptions);
    commandForm.addEventListener("submit", event => {
      event.preventDefault(); const line = commandInput.value.trim(); if (!line) return;
      if (/^(quit|hot_reload|world_destroy|inventory\s+\S+\s+(set|remove))\b/.test(line) && !window.confirm(`确认执行服务端指令？\n${line}`)) return;
      history.push(line); if (history.length > 100) history.shift(); historyAt = history.length;
      appendLog(`> ${line}`, "command"); commandInput.value = ""; void act("console", { line });
    });
    commandInput.addEventListener("keydown", event => {
      if (event.key === "ArrowUp" || event.key === "ArrowDown") {
        event.preventDefault(); historyAt = Math.max(0, Math.min(history.length, historyAt + (event.key === "ArrowUp" ? -1 : 1)));
        commandInput.value = history[historyAt] || "";
      } else if (event.key === "Tab") {
        const matches = catalogue.commands.filter(item => item.name.startsWith(commandInput.value.trim()));
        if (matches.length) { event.preventDefault(); commandInput.value = `${matches[0].name} `; }
      }
    });
    consolePanel.append(consoleHeader, logs, commandForm); root.append(header, main, consolePanel); document.body.append(root);
    backgroundUi = Array.from(document.body.children).filter(child => child !== root).map(child => [child, !!child.inert]);
    for (const [child] of backgroundUi) child.inert = true;
    picker = createDebugTypePicker({ host: root, getIcon: getTypeIcon, getName: getTypeName });
    root.addEventListener("pointerdown", event => { if (contextMenu && !contextMenu.contains(event.target)) closeMenu(); });
    root.addEventListener("keydown", event => {
      if (event.key === "Shift") closeMenu();
      if (event.key === "F1") { event.preventDefault(); event.stopPropagation(); commandInput.focus(); }
      if (event.key === "Escape") {
        event.preventDefault(); event.stopPropagation();
        if (picker.isOpen()) picker.close(); else if (form) closeForm(); else if (contextMenu) closeMenu();
        else { selection.clear(); selectionChanged(); }
      }
    });
    ctx = canvas.getContext("2d");
    mapState = { map: null, mapLoadToken: 0, mapLoadPromise: null, mapLoadPath: "", camera, entities: new Map(), ownerEntityId: -1,
      canvasWidth: 1, canvasHeight: 1 };
    mapRenderer = createMapRenderer({ ctx, state: mapState, worldScale: () => camera.scale,
      worldToScreen: point => pointToDebugScreen(point, camera, size), addConsoleLine: appendLog });
    resizeObserver = new ResizeObserver(resize); resizeObserver.observe(viewport); resize();
    wirePointer(); selectionChanged(); canvas.focus();
  }
  function resize() {
    if (!root) return;
    const rect = viewport.getBoundingClientRect(); size = { width: Math.max(1, rect.width), height: Math.max(1, rect.height) };
    dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
    const width = Math.round(size.width * dpr), height = Math.round(size.height * dpr);
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    mapState.canvasWidth = size.width; mapState.canvasHeight = size.height; mapState.dpr = dpr; viewDirty = true;
  }
  function localPoint(event) { const rect = canvas.getBoundingClientRect(); return { x: event.clientX - rect.left, y: event.clientY - rect.top }; }
  function wirePointer() {
    canvas.addEventListener("wheel", event => {
      event.preventDefault(); followPlayer = null;
      camera = zoomDebugCamera(camera, localPoint(event), size, Math.exp(-Math.max(-300, Math.min(300, event.deltaY)) * 0.0018));
      viewDirty = true;
    }, { passive: false });
    canvas.addEventListener("pointerdown", event => {
      canvas.focus(); closeMenu(); if (event.button !== 0 && event.button !== 1) return;
      event.preventDefault(); canvas.setPointerCapture(event.pointerId);
      const point = localPoint(event);
      drag = { mode: event.button === 1 ? "pan" : "select", id: event.pointerId, start: point, end: point,
        worldStart: pointToDebugWorld(point, camera, size), camera: { ...camera }, at: performance.now(), shift: event.shiftKey, box: false };
      if (drag.mode === "pan") followPlayer = null;
    });
    canvas.addEventListener("pointermove", event => {
      const point = localPoint(event), position = pointToDebugWorld(point, camera, size);
      positionLabel.textContent = `${coord(position)} · ${Math.round(camera.scale * 100)}%`;
      if (!drag || event.pointerId !== drag.id) return;
      drag.end = point;
      if (drag.mode === "pan") {
        camera.x = drag.camera.x - (point.x - drag.start.x) / camera.scale;
        camera.y = drag.camera.y - (point.y - drag.start.y) / camera.scale; viewDirty = true;
      } else if (Math.hypot(point.x - drag.start.x, point.y - drag.start.y) > 5 && performance.now() - drag.at >= 100) drag.box = true;
    });
    canvas.addEventListener("pointerup", event => {
      if (!drag || event.pointerId !== drag.id) return;
      const gesture = drag; drag = null; canvas.releasePointerCapture(event.pointerId);
      if (gesture.mode !== "select") return;
      const point = localPoint(event), position = pointToDebugWorld(point, camera, size);
      let clicked = null;
      if (gesture.box || Math.hypot(point.x - gesture.start.x, point.y - gesture.start.y) > 6) {
        if (!gesture.shift) selection = new Map();
        const picked = boxSelectDebugEntities(rawEntities(), gesture.worldStart, position, selectable);
        for (const entity of picked) {
          if (selection.size >= DEBUG_SELECTION_LIMIT) break;
          selection.set(getDebugEntityKey(entity), { id: entity.entityId, generation: entity.generation });
        }
        if (selection.size >= DEBUG_SELECTION_LIMIT) appendLog(`操作组上限 ${DEBUG_SELECTION_LIMIT}；请分批操作`, "warn");
      } else {
        clicked = hitTestDebugEntities(rawEntities(), position, selectable);
        selection = toggleDebugSelection(selection, clicked, gesture.shift);
      }
      selectionChanged();
      if (clicked && !gesture.shift) entityMenu(clicked, event.clientX + 12, event.clientY + 12);
    });
    for (const name of ["pointercancel", "lostpointercapture"]) canvas.addEventListener(name, () => { drag = null; });
    canvas.addEventListener("contextmenu", event => {
      event.preventDefault(); const point = pointToDebugWorld(localPoint(event), camera, size);
      const entity = hitTestDebugEntities(rawEntities(), point, selectable);
      if (entity && selection.has(getDebugEntityKey(entity))) groupMenu(event.clientX, event.clientY);
      else pointMenu(point, event.clientX, event.clientY, entity);
    });
  }

  function render(now = performance.now()) {
    if (!root || !ctx) return;
    const dt = Math.max(0, Math.min(0.1, (now - (lastFrame || now)) / 1000)); lastFrame = now;
    if (document.hidden) burrowParticles.clear(); else burrowParticles.step(dt);
    if (followPlayer) {
      const player = players.find(item => item.id === followPlayer.id && item.account === followPlayer.account);
      if (player?.world === world?.id && player.worldGeneration === world?.generation) {
        const record = [...records.values()].find(item => item.raw.entityId === player.entityId && item.raw.generation === player.generation);
        const target = record?.renderPos || player.pos;
        if (target) { camera.x = target.x; camera.y = target.y; viewDirty = true; }
      }
    }
    mapState.camera = camera;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, size.width, size.height);
    if (mapState.map) {
      for (let work = 0; work < 4 && pendingTerrain.length; work++) {
        const patch = pendingTerrain.shift(); if (!worldMatches(patch)) continue;
        if (patch.kind === "walls") mapRenderer.applyWallRemove({ x: patch.minX, y: patch.minY }, { x: patch.maxX, y: patch.maxY });
        else for (let i = 0; i < (patch.gids?.length || 0); i++) mapRenderer.applyTerrainDelta(patch.layer, patch.row, patch.col + i, patch.gids[i]);
      }
    }
    mapRenderer.drawGrid(now);
    const entities = renderEntitiesById;
    entities.clear();
    const blend = 1 - Math.exp(-20 * dt);
    for (const record of records.values()) {
      const target = record.raw.pos;
      const dx = target.x - record.renderPos.x, dy = target.y - record.renderPos.y;
      if (dx * dx + dy * dy > 1000000) record.renderPos = { ...target };
      else { record.renderPos.x += (target.x - record.renderPos.x) * blend; record.renderPos.y += (target.y - record.renderPos.y) * blend; }
      record.renderAngle = record.raw.angle || 0;
      // Keep authoritative records for selection/inspection. Only screen
      // visibility controls drawing; tiny but visible bodies are never cut.
      if (!visible(record.raw) || !isDebugEntityInView(record.raw, record.renderPos, camera, size,
          Math.max(3, getCullScale(record.raw.entityType)))) continue;
      if (flowerTypes.has(record.raw.entityType)) record.flowerFace = stepFlowerFace(record.flowerFace, { attacking: !!(record.raw.flags & flagAttacking),
        defending: !!(record.raw.flags & flagDefending), dead: !!(record.raw.flags & flagDead), lookX: Math.cos(record.renderAngle), lookY: Math.sin(record.renderAngle) }, dt);
      entities.set(record.snapshot.entityId, record);
    }
    renderEntities?.(ctx, { entities, camera, scale: camera.scale, width: size.width, height: size.height, dpr }, now);
    burrowParticles.draw(ctx, camera.scale, pointToDebugScreen({ x: 0, y: 0 }, camera, size));
    mapRenderer.drawShortcutCover?.(now);
    ctx.save(); ctx.lineWidth = 1.5; ctx.font = "12px monospace"; ctx.textAlign = "center"; ctx.textBaseline = "bottom";
    for (const [key, record] of records) {
      if ((!selection.has(key) && !idToggle.checked) || !entities.has(record.snapshot.entityId)) continue;
      const point = pointToDebugScreen(record.renderPos, camera, size), radius = Math.max(4, record.raw.radius * camera.scale);
      if (selection.has(key)) {
        ctx.strokeStyle = "#66ddff"; ctx.fillStyle = "rgba(64,180,255,.12)";
        ctx.beginPath(); ctx.arc(point.x, point.y, radius + 3, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
      }
      if (selection.has(key) || idToggle.checked) {
        const text = `#${record.raw.entityId}`; ctx.lineWidth = 3; ctx.strokeStyle = "#111827"; ctx.strokeText(text, point.x, point.y - radius - 5);
        ctx.fillStyle = "#fff"; ctx.fillText(text, point.x, point.y - radius - 5); ctx.lineWidth = 1.5;
      }
    }
    if (lastPoint) {
      const point = pointToDebugScreen(lastPoint, camera, size); ctx.strokeStyle = "#ffc45a";
      ctx.beginPath(); ctx.moveTo(point.x - 8, point.y); ctx.lineTo(point.x + 8, point.y); ctx.moveTo(point.x, point.y - 8); ctx.lineTo(point.x, point.y + 8); ctx.stroke();
    }
    if (drag?.box) {
      ctx.strokeStyle = "#66ddff"; ctx.fillStyle = "rgba(64,180,255,.15)";
      ctx.fillRect(drag.start.x, drag.start.y, drag.end.x - drag.start.x, drag.end.y - drag.start.y);
      ctx.strokeRect(drag.start.x, drag.start.y, drag.end.x - drag.start.x, drag.end.y - drag.start.y);
    }
    ctx.restore();
    if (ready && world && (viewDirty || now - lastView > 1500) && now - lastView >= 100) {
      const radius = Math.max(32, Math.min(65536, Math.hypot(size.width, size.height) / (2 * camera.scale) * 1.12));
      if (send({ op: "view", ...worldRef(), x: camera.x, y: camera.y, radius })) { lastView = now; viewDirty = false; }
    }
    if (ready && selection.size === 1 && inspected?.kind === "entity" && now - lastInspect > 1000) {
      const record = records.get(selection.keys().next().value);
      if (record) void inspectEntity(record.raw);
    }
  }
  function close() {
    if (!root) return;
    ready = false; const socket = ws; ws = null;
    burrowParticles.clear();
    if (socket) { socket.onclose = socket.onmessage = socket.onerror = socket.onopen = null; socket.close(); }
    cancelRequests("调试工作台已关闭"); resizeObserver?.disconnect(); closeMenu(); closeForm();
    if (mapState) mapState.mapLoadToken++;
    mapRenderer?.dispose?.();
    for (const record of records.values()) releaseVisual(record.snapshot.entityId);
    records.clear(); renderEntitiesById.clear(); selection.clear(); root.remove(); root = null; ctx = null;
    for (const [child, inert] of backgroundUi) child.inert = inert;
    backgroundUi = [];
    world = null; worlds = []; players = []; pendingTerrain = []; lastWorldOptions = ""; lastFrame = lastView = 0;
    catalogue = { types: [], rarities: [], commands: [] }; history = []; historyAt = 0;
    inspected = null; followPlayer = null; drag = null;
    logs = playersPanel = inspector = statusLabel = commandInput = null;
    onClose();
  }
  function open(url, ticket) {
    close(); if (!/^[a-f0-9]{64}$/.test(ticket)) throw new Error("无效的调试票据");
    onOpen(); build(); decoder = new DebugFrameDecoder();
    try { ws = new WebSocket(url); }
    catch (error) { appendLog(error.message, "error"); setStatus("连接失败", true); return; }
    ws.binaryType = "arraybuffer";
    ws.onopen = () => { send({ op: "hello", ticket }); ticket = ""; };
    ws.onmessage = event => {
      try { for (const message of decoder.push(event.data)) receive(message); }
      catch (error) { appendLog(`协议错误：${error.message}`, "error"); ws?.close(); }
    };
    ws.onerror = () => appendLog("调试连接错误", "error");
    ws.onclose = () => { ready = false; cancelRequests("调试连接断开"); setStatus("调试已断开 / 授权失效；返回游戏后重新输入 dev_debug", true); };
    appendLog("正在兑换一次性 RCON 票据；本连接不创建玩家。", "success");
  }
  return { open, close, render, isOpen: () => !!root, focusConsole: () => commandInput?.focus() };
}
