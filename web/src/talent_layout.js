// Pure layout / request planning. The visual tree always follows our actual
// prerequisites and prices; the reference video is not a talent-data source.
export function collectTalentChain(node, owned, byKey) {
  const chain = [], seen = new Set();
  while (node && !owned.has(node.key) && !seen.has(node.key)) {
    seen.add(node.key);
    chain.push(node);
    node = node.base ? byKey.get(node.base) : null;
  }
  return chain.reverse();
}

export function collectTalentRemoval(node, owned, nodes) {
  const keys = new Set([node.key]);
  // Data need not be topologically sorted.
  let changed = true;
  while (changed) {
    changed = false;
    for (const next of nodes) {
      if (owned.has(next.key) && keys.has(next.base) && !keys.has(next.key)) {
        keys.add(next.key);
        changed = true;
      }
    }
  }
  return nodes.filter(next => owned.has(next.key) && keys.has(next.key));
}

export function buildTalentLayout(nodes) {
  const byKey = new Map(nodes.map(node => [node.key, node]));
  const children = new Map();
  const roots = [];
  for (const node of nodes) {
    if (!node.base || !byKey.has(node.base)) roots.push(node);
    else {
      if (!children.has(node.base)) children.set(node.base, []);
      children.get(node.base).push(node);
    }
  }
  const leaves = new Map();
  function weight(node) {
    if (leaves.has(node.key)) return leaves.get(node.key);
    leaves.set(node.key, 1); // guard a malformed cycle
    const result = Math.max(1, (children.get(node.key) || []).reduce((sum, child) => sum + weight(child), 0));
    leaves.set(node.key, result);
    return result;
  }
  // Give every root a spoke, but reserve more angular space for real forks.
  const weights = roots.map(node => 1 + Math.sqrt(weight(node)));
  const total = weights.reduce((a, b) => a + b, 0);
  const positions = new Map();
  const edges = [];
  function place(node, depth, start, span) {
    if (positions.has(node.key)) return;
    const angle = start + span / 2;
    // A small deterministic bend keeps long chains readable, not ruler-straight.
    const bend = Math.sin(depth * 0.85) * Math.min(0.12, span * 0.16);
    let radius = 128 * depth;
    let position = { x: Math.cos(angle + bend) * radius, y: Math.sin(angle + bend) * radius };
    // Wide forks (notably the seven Rare poison ranks) must not overlap.
    // Move crowded nodes a little further along their own branch, retaining
    // the prerequisite links rather than inventing a simpler fake tree.
    for (let attempt = 0; attempt < 80; attempt++) {
      const crowded = [...positions.values()].some(other => Math.hypot(position.x - other.x, position.y - other.y) < 84);
      if (!crowded) break;
      radius += 24;
      position = { x: Math.cos(angle + bend) * radius, y: Math.sin(angle + bend) * radius };
    }
    positions.set(node.key, position);
    edges.push({ from: byKey.has(node.base) ? node.base : null, to: node.key });
    const kids = children.get(node.key) || [];
    const sum = kids.reduce((n, kid) => n + weight(kid), 0);
    let cursor = start;
    for (const kid of kids) {
      const part = span * weight(kid) / sum;
      place(kid, depth + 1, cursor, part);
      cursor += part;
    }
  }
  let start = -Math.PI;
  roots.forEach((node, index) => {
    const span = weights[index] / total * Math.PI * 2;
    place(node, 1, start, span);
    start += span;
  });
  return { positions, edges, roots };
}

export function zoomTalentView(view, point, nextScale) {
  const scale = Math.max(0.35, Math.min(1.65, nextScale));
  const ratio = scale / view.scale;
  return { x: point.x - (point.x - view.x) * ratio, y: point.y - (point.y - view.y) * ratio, scale };
}
