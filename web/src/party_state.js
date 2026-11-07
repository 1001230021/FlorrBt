export function partyRoster(state) {
  const result = [];
  for (const member of state.partyMembers || []) {
    if (member.playerId === state.playerId && member.sessionGeneration === state.playerSessionGeneration) continue;
    const sameWorld = member.worldId === state.worldId && member.worldGeneration === state.worldGeneration;
    const candidate = member.hasEntity && sameWorld ? state.entities.get(member.entityId) : null;
    const entity = candidate && candidate.snapshot?.entityGeneration === member.entityGeneration ? candidate : null;
    const status = !member.connected ? "offline" : !sameWorld ? "otherWorld" : !entity ? "unavailable" : "visible";
    result.push({ member, entity, status });
  }
  return result;
}

export function partyMemberForEntity(state, snapshot) {
  if (!snapshot) return null;
  return (state.partyMembers || []).find(member =>
    member.hasEntity && member.worldId === state.worldId && member.worldGeneration === state.worldGeneration &&
    member.entityId === snapshot.entityId && member.entityGeneration === snapshot.entityGeneration) || null;
}
