// Tiny project-native vector glyphs, kept white like the reference's talent
// medallions. No raster assets, remote requests or icon-font dependency.
import { TalentId } from "./talent_data.js";
import { flowerGeometry } from "./flower_renderer.js";

const health = '<circle cx="13" cy="14" r="8"/><path d="M22 16v12m-6-6h12" stroke-width="5"/>';
const glyphs = {
  [TalentId.PetalHealth]: health,
  [TalentId.FlowerHealth]: health,
  [TalentId.PetalSplit]: '<rect x="4" y="6" width="15" height="20" rx="4"/><rect x="13" y="10" width="15" height="20" rx="4" fill="none" stroke-width="3"/>',
  [TalentId.PetalRotationSpeed]: '<path d="M25 9a11 11 0 1 0 1 13M25 3v8h-8" fill="none" stroke-width="4"/>',
  [TalentId.BodyDamage]: '<path d="m5 25 7-21 4 13 4-13 7 21-11-5Z"/>',
  [TalentId.BodyDamagePoison]: '<path d="M16 3C12 12 6 15 6 21a10 10 0 0 0 20 0c0-6-6-9-10-18Z"/><path d="M16 17v10m-5-5h10" stroke="#666" stroke-width="2"/>',
  [TalentId.PoisonDamage]: '<path d="M16 2C13 12 6 16 6 22a10 10 0 0 0 20 0c0-6-7-10-10-20Z"/>',
  [TalentId.PetalReload]: '<path d="M16 17c8 4 10-8 1-10-12-3-17 16-6 21M17 17c-7 4 1 13 8 6 9-9-4-23-14-17M17 17c0-8-13-6-12 3" fill="none" stroke-width="3"/>',
  [TalentId.SecondChance]: '<path d="M16 28 5 18C-1 9 8 1 16 10 24 1 33 9 27 18Z"/><path d="m13 13 6 4-6 4" stroke="#888" fill="none" stroke-width="3"/>',
  [TalentId.SlotNum]: '<path d="m10 4 12 0-3 7 7 8c6 11-26 11-20 0l7-8Z"/><path d="M11 11h10" fill="none" stroke="#999" stroke-width="2"/>',
  [TalentId.Regen]: '<path d="M12 3h8v9h9v8h-9v9h-8v-9H3v-8h9Z"/>',
  [TalentId.Reach]: '<path d="M4 16h24M9 10l-6 6 6 6m14-12 6 6-6 6" fill="none" stroke-width="4"/>',
  [TalentId.Magnetism]: '<path d="M6 5v13a10 10 0 0 0 20 0V5h-7v13a3 3 0 0 1-6 0V5Z"/>',
  [TalentId.Summoner]: '<ellipse cx="16" cy="18" rx="10" ry="13"/>',
  [TalentId.Antennae]: '<path d="M14 28C11 12 7 6 3 3 3 15 8 22 14 28m4 0C21 12 25 6 29 3c0 12-5 19-11 25Z"/>',
  [TalentId.ConcentratedPoison]: '<path d="M12 3h8v9l9 13q2 5-5 5H8q-7 0-5-5l9-13ZM7 21h18"/>',
  [TalentId.Movement]: '<ellipse cx="11" cy="10" rx="6" ry="9" transform="rotate(-25 11 10)"/><ellipse cx="22" cy="23" rx="6" ry="9" transform="rotate(-25 22 23)"/>',
};

export function getTalentGlyph(id) {
  return `<svg viewBox="0 0 32 32" aria-hidden="true" fill="currentColor" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">${glyphs[id] || health}</svg>`;
}

export function getTalentRootGlyph() {
  const g = flowerGeometry;
  const eyes = [-g.eyeX, g.eyeX].map(x => `<ellipse cx="${x}" cy="${g.eyeY}" rx="${g.eyeRX}" ry="${g.eyeRY}"/>`).join("");
  return `<svg viewBox="-27 -27 54 54" aria-hidden="true"><defs><clipPath id="talent-root-eyes">${eyes}</clipPath></defs><circle r="23.8" fill="#ffe763" stroke="#cfbc50" stroke-width="${g.outline}"/><g fill="#222" stroke="#222" stroke-width="1">${eyes}</g><g fill="white" clip-path="url(#talent-root-eyes)"><circle cx="-7" cy="-8" r="3"/><circle cx="7" cy="-8" r="3"/></g><path d="M-6 10Q0 16 6 10" fill="none" stroke="#222" stroke-width="1.5" stroke-linecap="round"/></svg>`;
}
