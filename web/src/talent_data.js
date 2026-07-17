export const TalentId = Object.freeze({
  PetalHealth: 1,
  PetalSplit: 2,
  FlowerHealth: 3,
  PetalRotationSpeed: 4,
  BodyDamage: 5,
  BodyDamagePoison: 6,
  PoisonDamage: 7,
  PetalReload: 8,
  SecondChance: 9,
  SlotNum: 10,
  Medic: 11,
  Reach: 12,
  Magnetism: 13,
  Summoner: 14,
  Antennae: 15,
  ConcentratedPoison: 16,
});

const talentTierRarities = [1, 2, 3, 4, 5, 6, 7, 8, 9];

export function talentKey(id, rarity, rank = 0) {
  return `${id}:${rarity}:${rank || 0}`;
}

function buildTalentNodes() {
  const nodes = [];
  const add = (group, id, name, rarity, cost, base = null, rank = 0) => {
    const node = { group, id, name, rarity, cost, base, rank, key: talentKey(id, rarity, rank) };
    nodes.push(node);
    return node;
  };
  const chain = (group, id, name, rarities, costs, base = null, ranks = []) => {
    let previous = base;
    const made = [];
    for (let i = 0; i < rarities.length; i += 1) {
      const node = add(group, id, name, rarities[i], costs[i], previous, ranks[i] || 0);
      previous = node.key;
      made.push(node);
    }
    return made;
  };

  const flowerHealth = chain("Flower", TalentId.FlowerHealth, "Health", talentTierRarities, [2, 5, 8, 11, 14, 17, 20, 21, 24]);
  const sharpEdges = chain("Flower", TalentId.BodyDamage, "Sharp Edges", talentTierRarities.slice(0, 8), [2, 3, 4, 5, 6, 7, 8, 9]);
  add("Flower", TalentId.BodyDamagePoison, "Body Toxicity", 7, 10, sharpEdges[5].key);
  chain("Petals", TalentId.PetalHealth, "Petal Health", talentTierRarities, [2, 4, 6, 8, 10, 12, 14, 16, 18]);
  chain("Petals", TalentId.PetalReload, "Reload", talentTierRarities, [2, 6, 10, 12, 16, 20, 24, 28, 32]);
  chain("Petals", TalentId.PetalRotationSpeed, "Rotation", talentTierRarities.slice(0, 5), [1, 2, 3, 4, 5]);

  const loadout = chain("Loadout", TalentId.SlotNum, "Loadout", talentTierRarities.slice(0, 5), [3, 6, 9, 12, 15]);
  chain("Loadout", TalentId.Antennae, "Antennae", [3, 4, 5], [1, 3, 5], loadout[0].key);
  chain("Loadout", TalentId.Reach, "Reach", [3, 6, 8], [3, 6, 9], loadout[1].key);
  chain("Loadout", TalentId.PetalSplit, "Duplicator", [5, 7, 8, 9], [10, 20, 30, 40], loadout[3].key);
  add("Loadout", TalentId.Magnetism, "Magnetism", 6, 15, loadout[4].key);

  chain("Support", TalentId.Medic, "Medic", talentTierRarities, [2, 5, 8, 11, 14, 17, 20, 23, 26]);
  chain("Support", TalentId.Summoner, "Summoner", talentTierRarities, [2, 5, 8, 11, 14, 17, 20, 23, 26]);
  chain("Support", TalentId.SecondChance, "Second Chance", [5, 6], [10, 20], flowerHealth[2].key);

  const poisonCommon = add("Poison", TalentId.PoisonDamage, "Poison", 1, 1);
  const poisonUnusual = add("Poison", TalentId.PoisonDamage, "Poison", 2, 2, poisonCommon.key);
  for (let rank = 0; rank < 7; rank += 1) {
    add("Poison", TalentId.PoisonDamage, "Poison", 3, 3, poisonUnusual.key, rank);
  }
  add("Poison", TalentId.ConcentratedPoison, "Concentrated Poison", 8, 15, talentKey(TalentId.PoisonDamage, 3, 1));

  return nodes;
}

function buildTalentChildrenByBase(nodes, nodeByKey) {
  const childrenByBase = new Map();
  for (const node of nodes) {
    if (!node.base || !nodeByKey.has(node.base)) continue;
    if (!childrenByBase.has(node.base)) childrenByBase.set(node.base, []);
    childrenByBase.get(node.base).push(node);
  }
  for (const children of childrenByBase.values()) {
    children.sort((a, b) => a.order - b.order);
  }
  return childrenByBase;
}

export const talentNodes = buildTalentNodes();
talentNodes.forEach((node, index) => {
  node.order = index;
});

export const talentNodeByKey = new Map(talentNodes.map((node) => [node.key, node]));
export const talentChildrenByBase = buildTalentChildrenByBase(talentNodes, talentNodeByKey);
export const talentRootNodes = talentNodes.filter((node) => !node.base || !talentNodeByKey.has(node.base));
