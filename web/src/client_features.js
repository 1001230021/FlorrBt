import { state } from './app_context.js';
import { isNewerUint32Sequence } from './protocol.js';
import { t } from './i18n.js';

export const FeatureAccess = Object.freeze({ View: 1, Edit: 2, Forge: 4 });
const registrations = new Map();
let manifest = null;
export function registerClientFeature(key, registration) {
  if (registrations.has(key)) throw new Error(`Duplicate client feature ${key}`);
  registrations.set(key, registration);
  registration.button.addEventListener('click', () => invokeClientFeature(key));
}
export function featureAllowed(key, access = FeatureAccess.View) {
  if (!manifest || !state.authenticated || manifest.session !== state.playerSessionGeneration ||
      manifest.world !== state.worldId || manifest.worldGeneration !== state.worldGeneration) return false;
  const owner = state.entities.get(state.ownerEntityId)?.snapshot;
  if (manifest.owner !== state.ownerEntityId || !owner || manifest.ownerGeneration !== owner.entityGeneration) return false;
  const feature = manifest.features.find(item => item.key === key);
  return !!feature && registrations.has(key) && feature.interaction === (registrations.get(key).interaction ?? 0) && (feature.access & access) === access;
}
export function invokeClientFeature(key, force) {
  const feature = registrations.get(key);
  if (!feature || (force !== false && !featureAllowed(key))) return false;
  feature.toggle(force); return true;
}
export function applyClientFeatures(value) {
  try {
    const counter = text => {
      if (typeof text !== 'string' || !/^(0|[1-9][0-9]{0,19})$/.test(text)) throw new Error('Invalid identity');
      const number = BigInt(text); if (number > 0xffffffffffffffffn) throw new Error('Identity overflow');
      return number.toString(16).padStart(16, '0');
    };
    value = { ...value, session: counter(value.session), worldGeneration: counter(value.worldGeneration), ownerGeneration: counter(value.ownerGeneration) };
  } catch { return false; }
  if (value.schema !== 1 || !Number.isInteger(value.revision) || value.revision < 0 || value.revision > 0xffffffff ||
      !Array.isArray(value.features) || value.features.length > 64 || !/^[0-9a-f]{16}$/.test(value.session) ||
      !/^[0-9a-f]{16}$/.test(value.worldGeneration) || !Number.isInteger(value.world) || !Number.isInteger(value.owner) || value.owner < 0) return false;
  const keys = new Set();
  for (const item of value.features) {
    if (typeof item.key !== 'string' || !item.key.length || item.key.length > 64 || keys.has(item.key) ||
        !Number.isInteger(item.access) || item.access < 0 || item.access > 7 || !Number.isInteger(item.surfaces) ||
        item.surfaces < 0 || item.surfaces > 15 || !Number.isInteger(item.order) || typeof item.visible !== 'boolean' ||
        ![0, 1, 2].includes(item.interaction) || typeof item.badge !== 'boolean' || typeof item.title !== 'string' ||
        item.title.length > 128 || typeof item.icon !== 'string' || item.icon.length > 64) return false;
    keys.add(item.key);
  }
  if (value.session !== state.playerSessionGeneration || value.world !== state.worldId || value.worldGeneration !== state.worldGeneration) return false;
  if (manifest && !isNewerUint32Sequence(value.revision, manifest.revision)) return false;
  manifest = value; refreshClientFeatures(); return true;
}
export function refreshClientFeatures() {
  const entries = new Map((manifest?.features || []).map(item => [item.key, item]));
  for (const [key, registration] of registrations) {
    const item = entries.get(key), allowed = featureAllowed(key);
    const shown = state.authenticated && !!item?.visible && !!(item.surfaces & (registration.surface ?? 1));
    registration.button.classList.toggle('hidden', !shown);
    registration.button.disabled = !allowed;
    registration.button.style.order = String(item?.order || 0);
    registration.button.dataset.featureReason = item?.reason || '';
    if (!allowed) registration.toggle(false);
    registration.onAccess?.(item?.access || 0);
    registration.button.title = item?.reason ? `${t(registration.title)} — ${t('ui.features.' + item.reason)}` : t(registration.title);
  }
}
export function resetClientFeatures() { manifest = null; refreshClientFeatures(); }
export function featureUsesBadge(key) { return !!manifest?.features.find(item => item.key === key)?.badge; }
