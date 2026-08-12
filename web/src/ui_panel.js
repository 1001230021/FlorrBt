export class UiPanelManager {
  constructor() {
    this.panels = new Map();
  }

  register(panel) {
    if (!panel?.id) throw new Error("UI panel requires an id");
    if (this.panels.has(panel.id))
      throw new Error(`Duplicate UI panel id: ${panel.id}`);
    this.panels.set(panel.id, panel);
    panel.manager = this;
    return panel;
  }

  closeGroup(group, except = null) {
    for (const panel of this.panels.values()) {
      if (panel !== except && panel.group === group) panel.setOpen(false);
    }
  }

  isAnyOpen(group) {
    for (const panel of this.panels.values()) {
      if (panel.group === group && panel.isOpen()) return true;
    }
    return false;
  }

  closeAll() {
    for (const panel of this.panels.values()) panel.setOpen(false);
  }
}

export class UiPanel {
  constructor({
    id,
    root,
    state,
    stateKey,
    manager = null,
    group = "main",
    exclusive = true,
    openClass = "open",
    trigger = null,
    focusTarget = null,
    onOpen = null,
    onClose = null,
    onChange = null,
  }) {
    if (!id || !root || !state || !stateKey)
      throw new Error("UI panel requires id, root, state, and stateKey");

    this.id = id;
    this.root = root;
    this.state = state;
    this.stateKey = stateKey;
    this.group = group;
    this.exclusive = exclusive;
    this.openClass = openClass;
    this.trigger = trigger;
    this.focusTarget = focusTarget;
    this.onOpen = onOpen;
    this.onClose = onClose;
    this.onChange = onChange;
    this.manager = null;

    manager?.register(this);
    this.syncDom();
  }

  isOpen() {
    return !!this.state[this.stateKey];
  }

  syncDom() {
    const open = this.isOpen();
    this.root.classList.toggle(this.openClass, open);
    this.root.setAttribute("aria-hidden", open ? "false" : "true");
    this.trigger?.classList.toggle("active", open);
    this.trigger?.setAttribute("aria-pressed", open ? "true" : "false");
  }

  setOpen(force) {
    const next = typeof force === "boolean" ? force : !this.isOpen();
    if (next === this.isOpen()) return next;

    if (next && this.exclusive) this.manager?.closeGroup(this.group, this);
    this.state[this.stateKey] = next;
    this.syncDom();

    if (next) this.onOpen?.(this);
    else {
      this.onClose?.(this);
      this.focusTarget?.focus?.();
    }
    this.onChange?.(next, this);
    return next;
  }

  toggle(force) {
    return this.setOpen(force);
  }

  contains(target) {
    return !!this.root.contains(target);
  }
}
