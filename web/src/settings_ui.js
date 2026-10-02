import { UiPanel } from "./ui_panel.js";

export class SettingsPanel extends UiPanel {
  constructor({
    list,
    translate,
    definitions = [],
    onError = null,
    ...panelOptions
  }) {
    super(panelOptions);
    if (!list) throw new Error("Settings panel requires a list element");

    this.list = list;
    this.list.setAttribute("role", "tabpanel");
    this.list.addEventListener("keydown", event => {
      // Keep control activation/arrows from also becoming gameplay holds.
      // Key releases still bubble to the shared input cleanup.
      if (event.key !== "Escape" && event.key !== "F1") event.stopPropagation();
    });
    this.translate = translate;
    this.onError = onError;
    this.definitions = [];
    this.controlFactories = new Map();
    this.registerControlType("choice", (definition) =>
      this.createChoiceControl(definition),
    );
    this.registerControlType("toggle", (definition) =>
      this.createToggleControl(definition),
    );
    for (const definition of definitions) this.registerSetting(definition);
    this.category = "game";
    this.tabs = document.createElement("div");
    this.tabs.className = "settings-tabs";
    this.tabs.setAttribute("role", "tablist");
    for (const category of ["game", "graphics", "controls"]) {
      const tab = document.createElement("button");
      tab.type = "button";
      tab.dataset.category = category;
      tab.setAttribute("role", "tab");
      tab.setAttribute("aria-controls", this.list.id);
      tab.addEventListener("click", () => { this.category = category; this.render(); });
      this.tabs.appendChild(tab);
    }
    this.list.parentElement.insertBefore(this.tabs, this.list);
  }

  registerControlType(type, factory) {
    if (!type || typeof factory !== "function")
      throw new Error("Settings control type requires a name and factory");
    this.controlFactories.set(type, factory);
    return this;
  }

  registerSetting(definition) {
    if (!definition?.id || !definition?.type)
      throw new Error("Setting requires id and type");
    if (this.definitions.some((entry) => entry.id === definition.id))
      throw new Error(`Duplicate setting id: ${definition.id}`);
    this.definitions.push(definition);
    return this;
  }

  setOpen(force) {
    const open = super.setOpen(force);
    if (open) this.render();
    return open;
  }

  refreshTranslations() {
    if (this.isOpen()) this.render();
  }

  render() {
    this.root.dataset.settingsCategory = this.category;
    for (const tab of this.tabs.children) {
      const selected = tab.dataset.category === this.category;
      tab.textContent = this.translate(`ui.settings.tabs.${tab.dataset.category}`);
      tab.classList.toggle("active", selected);
      tab.setAttribute("aria-selected", String(selected));
    }
    const groups = {
      movement: "controls", "hold-attack": "controls", "hold-defense": "controls", movement_arrow: "controls",
      "render-scale": "graphics", fullscreen: "graphics", damage_numbers: "graphics", screen_shake: "graphics", live_icons: "graphics",
    };
    const fragment = document.createDocumentFragment();
    for (const definition of this.definitions) {
      if ((definition.group || groups[definition.id] || "game") !== this.category) continue;
      const factory = this.controlFactories.get(definition.type);
      if (!factory) continue;

      const row = document.createElement("div");
      row.className = "settings-row";
      row.dataset.settingId = definition.id;

      const label = document.createElement("label");
      label.className = "settings-label";
      label.textContent = this.translate(definition.labelKey);
      label.htmlFor = `setting-${definition.id}`;

      const control = factory(definition);
      control.id = `setting-${definition.id}`;
      row.append(label, control);
      fragment.appendChild(row);
    }
    this.list.replaceChildren(fragment);
  }

  createChoiceControl(definition) {
    const select = document.createElement("select");
    select.className = "settings-choice";
    const current = String(definition.getValue?.() ?? "");
    const options = definition.options?.() || [];
    for (const option of options) {
      const element = document.createElement("option");
      element.value = String(option.value);
      element.textContent = option.labelKey
        ? this.translate(option.labelKey)
        : String(option.label ?? option.value);
      element.selected = element.value === current;
      select.appendChild(element);
    }
    select.addEventListener("change", () =>
      this.commitValue(definition, select.value, select),
    );
    return select;
  }

  createToggleControl(definition) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "settings-square-toggle";
    const enabled = !!definition.getValue?.();
    button.classList.toggle("enabled", enabled);
    button.setAttribute("aria-pressed", enabled ? "true" : "false");
    button.setAttribute(
      "aria-label",
      this.translate(definition.labelKey),
    );
    const fill = document.createElement("span");
    fill.className = "settings-square-toggle-fill";
    fill.setAttribute("aria-hidden", "true");
    button.appendChild(fill);
    button.addEventListener("click", () =>
      this.commitValue(definition, !definition.getValue?.(), button),
    );
    return button;
  }

  async commitValue(definition, value, control) {
    control.disabled = true;
    try {
      await definition.setValue?.(value);
    } catch (error) {
      if (this.onError) this.onError(error, definition);
      else console.error(`Failed to update setting ${definition.id}`, error);
    } finally {
      this.render();
    }
  }
}
