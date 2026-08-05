const manifestUrl = new URL("../locales/manifest.json", import.meta.url);

const builtInManifest = Object.freeze({
  schemaVersion: 1,
  defaultLocale: "en",
  aliases: {},
  locales: [
    { id: "en", label: "English", direction: "ltr", file: "en.json" },
  ],
});

let manifest = builtInManifest;
let manifestPromise = null;
let fallbackCatalog = {};
let activeCatalog = {};
let activeLocale = builtInManifest.defaultLocale;
let activeCollator = new Intl.Collator(activeLocale, {
  numeric: true,
  sensitivity: "base",
});
let activationSequence = 0;

const catalogPromises = new Map();
const listeners = new Set();
const warnedMissingKeys = new Set();

function normalizeLocaleLookup(value) {
  return String(value || "")
    .trim()
    .replaceAll("_", "-")
    .toLowerCase();
}

async function fetchJson(url) {
  const response = await fetch(url, { cache: "no-store" });
  if (!response.ok)
    throw new Error(
      "Failed to load " + url.href + ": HTTP " + response.status,
    );
  return response.json();
}

function validManifest(value) {
  return (
    value &&
    Number(value.schemaVersion) >= 1 &&
    typeof value.defaultLocale === "string" &&
    Array.isArray(value.locales) &&
    value.locales.some((entry) => entry?.id === value.defaultLocale)
  );
}

async function ensureManifest() {
  if (!manifestPromise) {
    manifestPromise = fetchJson(manifestUrl)
      .then((value) => {
        if (!validManifest(value)) throw new Error("Invalid locale manifest");
        manifest = value;
        return manifest;
      })
      .catch((error) => {
        console.error("Failed to load locale manifest", error);
        manifest = builtInManifest;
        return manifest;
      });
  }
  return manifestPromise;
}

function localeEntry(locale) {
  const lookup = normalizeLocaleLookup(locale);
  return (
    manifest.locales.find(
      (entry) => normalizeLocaleLookup(entry?.id) === lookup,
    ) || null
  );
}

function resolvedAlias(value) {
  const lookup = normalizeLocaleLookup(value);
  for (const [alias, locale] of Object.entries(manifest.aliases || {})) {
    if (normalizeLocaleLookup(alias) === lookup) return locale;
  }
  return null;
}

export function resolveLocale(value) {
  const requested = String(value || "").trim();
  if (!requested) return null;

  const direct = localeEntry(requested);
  if (direct) return direct.id;

  const aliasedEntry = localeEntry(resolvedAlias(requested));
  if (aliasedEntry) return aliasedEntry.id;

  const language = normalizeLocaleLookup(requested).split("-")[0];
  const languageEntry = localeEntry(language);
  return languageEntry?.id || null;
}

async function loadCatalog(locale) {
  const entry = localeEntry(locale);
  if (!entry) throw new Error("Unknown locale: " + locale);
  if (!catalogPromises.has(entry.id)) {
    const url = new URL(entry.file, manifestUrl);
    catalogPromises.set(
      entry.id,
      fetchJson(url).then((catalog) => {
        if (!catalog || typeof catalog !== "object" || Array.isArray(catalog))
          throw new Error("Invalid locale catalog: " + entry.id);
        return catalog;
      }),
    );
  }
  return catalogPromises.get(entry.id);
}

function valueAtPath(catalog, key) {
  let value = catalog;
  for (const part of String(key || "").split(".")) {
    if (!part || !value || typeof value !== "object") return undefined;
    value = value[part];
  }
  return typeof value === "string" ? value : undefined;
}

function interpolate(template, params) {
  return String(template).replace(/\{([A-Za-z0-9_]+)\}/g, (match, key) =>
    Object.hasOwn(params || {}, key) ? String(params[key]) : match,
  );
}

function applyLocaleMetadata() {
  const entry = localeEntry(activeLocale);
  document.documentElement.lang = activeLocale;
  document.documentElement.dir = entry?.direction === "rtl" ? "rtl" : "ltr";
}

function notifyLanguageChanged() {
  const detail = { locale: activeLocale, entry: getCurrentLocale() };
  for (const listener of listeners) {
    try {
      listener(detail);
    } catch (error) {
      console.error("Language change listener failed", error);
    }
  }
  window.dispatchEvent(new CustomEvent("florrbt:languagechange", { detail }));
}

async function activateLocale(locale, { notify = true } = {}) {
  const sequence = ++activationSequence;
  const catalog = await loadCatalog(locale);
  if (sequence !== activationSequence)
    return { ok: false, reason: "superseded", locale: activeLocale };

  activeLocale = locale;
  activeCatalog = catalog;
  activeCollator = new Intl.Collator(activeLocale, {
    numeric: true,
    sensitivity: "base",
  });
  warnedMissingKeys.clear();
  applyLocaleMetadata();
  applyDocumentTranslations();
  if (notify) notifyLanguageChanged();
  return { ok: true, locale: activeLocale, entry: getCurrentLocale() };
}

export async function initI18n(requestedLocale = "") {
  await ensureManifest();
  const fallbackLocale = resolveLocale(manifest.defaultLocale) || "en";
  try {
    fallbackCatalog = await loadCatalog(fallbackLocale);
  } catch (error) {
    console.error("Failed to load fallback locale", error);
    fallbackCatalog = {};
  }

  const locale = resolveLocale(requestedLocale) || fallbackLocale;
  try {
    return await activateLocale(locale, { notify: false });
  } catch (error) {
    console.error("Failed to activate locale " + locale, error);
    activeLocale = fallbackLocale;
    activeCatalog = fallbackCatalog;
    applyLocaleMetadata();
    applyDocumentTranslations();
    return { ok: false, reason: "load", locale: activeLocale, error };
  }
}

export async function changeLanguage(requestedLocale) {
  await ensureManifest();
  const locale = resolveLocale(requestedLocale);
  if (!locale)
    return {
      ok: false,
      reason: "unknown",
      requested: String(requestedLocale || ""),
      locale: activeLocale,
    };

  try {
    return await activateLocale(locale);
  } catch (error) {
    return { ok: false, reason: "load", locale, error };
  }
}

export function t(key, params = {}, options = {}) {
  const translated = valueAtPath(activeCatalog, key);
  const fallback = valueAtPath(fallbackCatalog, key);
  const template = translated ?? fallback ?? options.defaultValue;
  if (template !== undefined) return interpolate(template, params);

  if (!warnedMissingKeys.has(key)) {
    warnedMissingKeys.add(key);
    console.warn("Missing translation: " + key);
  }
  return String(key || "");
}

export function applyDocumentTranslations(root = document) {
  const scope = root?.querySelectorAll ? root : document;
  const attributeKeys = [
    ["data-i18n-placeholder", "placeholder"],
    ["data-i18n-title", "title"],
    ["data-i18n-aria-label", "aria-label"],
  ];

  for (const element of scope.querySelectorAll("[data-i18n]"))
    element.textContent = t(element.dataset.i18n);
  for (const [dataAttribute, targetAttribute] of attributeKeys) {
    const selector = "[" + dataAttribute + "]";
    for (const element of scope.querySelectorAll(selector))
      element.setAttribute(
        targetAttribute,
        t(element.getAttribute(dataAttribute)),
      );
  }
}

export function getLocale() {
  return activeLocale;
}

export function getCurrentLocale() {
  const entry = localeEntry(activeLocale);
  return entry ? { ...entry } : null;
}

export function getAvailableLocales() {
  return (manifest.locales || []).map((entry) => ({ ...entry }));
}

export function compareLocalized(a, b) {
  return activeCollator.compare(String(a || ""), String(b || ""));
}

export function onLanguageChanged(listener) {
  if (typeof listener !== "function") return () => {};
  listeners.add(listener);
  return () => listeners.delete(listener);
}
