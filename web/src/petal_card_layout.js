// The label limits are specified at the standard 56px card size, exactly
// like world-drop labels. DOM cards express that size as container units so
// primary, reserve, inventory and flight clones use the same proportions.
export const petalCardReferenceSize = 56;

export function getPetalLabelFontSize(widthAt100, maxFont = 14, maxWidth = 45) {
  const measured = Number.isFinite(widthAt100) && widthAt100 > 0 ? widthAt100 : 100;
  const fontLimit = Number.isFinite(maxFont) && maxFont > 0 ? maxFont : 14;
  const widthLimit = Number.isFinite(maxWidth) && maxWidth > 0 ? maxWidth : 45;
  return Math.min(fontLimit, widthLimit * 100 / measured);
}

export function getPetalLabelContainerSize(fontSize) {
  return fontSize / petalCardReferenceSize * 100;
}

export function getCardFlightPose(from, to, progress) {
  const t = Math.max(0, Math.min(1, Number.isFinite(progress) ? progress : 0));
  const amount = 1 - (1 - t) ** 3;
  return {
    x: from.x + (to.x - from.x) * amount,
    y: from.y + (to.y - from.y) * amount,
    size: from.size + (to.size - from.size) * amount,
  };
}
