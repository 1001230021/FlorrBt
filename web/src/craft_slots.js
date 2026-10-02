// Match server-returned cards back to the existing ring without inventing
// inventory. Loss positions may vary visually, but quantities never do.
export function placeCraftReturns(previous, items, random = Math.random) {
  const result = previous.map(() => null);
  for (const item of items) {
    let remaining = Math.max(0, Math.floor(item.count || 0));
    const indices = previous.map((slot, index) => ({ slot, index }))
      .filter(({ slot }) => slot?.petalType === item.petalType && slot.rarity === item.rarity);
    for (let i = indices.length - 1; i > 0; i--) {
      const j = Math.min(i, Math.floor(random() * (i + 1)));
      [indices[i], indices[j]] = [indices[j], indices[i]];
    }
    // Fill fairly across survivors; large-stack crafts retain their xN badges.
    while (remaining > 0) {
      const available = indices.filter(({ slot, index }) => (result[index]?.count || 0) < (slot.count || 1));
      if (!available.length) break;
      const batch = Math.max(1, Math.floor(remaining / available.length));
      for (const { slot, index } of available) {
        const count = Math.min(batch, remaining, (slot.count || 1) - (result[index]?.count || 0));
        if (count <= 0) continue;
        result[index] = { petalType: item.petalType, rarity: item.rarity, count: (result[index]?.count || 0) + count };
        remaining -= count;
      }
    }
    // Defensive fallback for unusual forge responses: still display every
    // returned item even if it was not one of the staged input types.
    if (remaining > 0) {
      const free = result.findIndex(slot => !slot);
      if (free >= 0) result[free] = { ...item, count: remaining };
      else {
        const match = result.find(slot => slot.petalType === item.petalType && slot.rarity === item.rarity);
        if (match) match.count += remaining;
      }
    }
  }
  return result;
}
