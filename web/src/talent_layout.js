// 天赋图布局和请求计划遵循实际前置关系与价格。
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
  // 输入数据不要求拓扑排序。
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
    leaves.set(node.key, 1); // 防止损坏数据中的循环递归。
    const result = Math.max(1, (children.get(node.key) || []).reduce((sum, child) => sum + weight(child), 0));
    leaves.set(node.key, result);
    return result;
  }
  // 每个根分配方向，真实分叉较多时保留更大角空间。
  const branchOrder = [4, 8, 1, 14, 10, 11, 5, 3, 7];
  roots.sort((a, b) => (branchOrder.indexOf(a.id) + 1 || 99) - (branchOrder.indexOf(b.id) + 1 || 99));
  const weights = roots.map(node => 1 + Math.sqrt(weight(node)));
  const total = weights.reduce((a, b) => a + b, 0);
  const positions = new Map();
  const edges = [];
  function place(node, depth, start, span) {
    if (positions.has(node.key)) return;
    const angle = start + span / 2;
    const bend = Math.sin(depth * 0.85) * Math.min(0.12, span * 0.16);
    let radius = 108 * depth;
    let position = { x: Math.cos(angle + bend) * radius, y: Math.sin(angle + bend) * radius };
    // 拥挤分叉沿自身支路外移，保持真实前置关系。
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
  // 分支围绕根花朵铺满整圈；视口显示上方扇区，通过旋转查看其余部分。
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
