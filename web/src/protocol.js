const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

export const NETWORK_PETAL_TYPE_OFFSET = 100;
export const NETWORK_DROP_TYPE_OFFSET = 150;
export const MAX_CHAT_MESSAGE_SIZE = 180;
export const NET_COORD_SCALE = 64;
export const NET_RADIUS_SCALE = 16;
export const NET_ANGLE_SCALE = 1000;

export const ChatFlag = Object.freeze({
  Global: 0,
  Local: 1,
  Server: 2,
  Whisper: 3,
});

export const ServerType = Object.freeze({
  Welcome: 0x00,
  Snapshot: 0x01,
  AuthResult: 0x02,
  OwnerState: 0x10,
  Inventory: 0x11,
  Chat: 0x12,
  CraftResult: 0x13,
});

export const PetalNames = [
  "None", "Air", "AntEgg", "Antennae", "Basic", "BeetleEgg", "Bone", "Bubble", "Carrot",
  "Coin", "Compass", "Cogwheel", "Disc", "Dust", "GoldenLeaf", "Iris", "Lentil", "Moon",
  "Nullification", "Pincer", "Relic", "Rose", "YinYang", "Missile", "BloodSacrifice",
  "Corruption", "Bandage", "Heavy", "Faster", "Yggdrasil", "Dahlia", "Wing", "Triangle",
  "Sawblade", "Fragment", "Mimic", "Glass",
];

export const MobNames = [
  "None", "Beetle", "Gambler", "NormalLadybug", "MechaFlower", "NormalFlower", "PlayerFlower",
  "SoldierAnt", "SoldierFireAnt", "SoldierTermite", "SummonedBeetle", "SummonedSoldierAnt",
  "BandageBeetle",
];

export const RarityNames = [
  "Null", "Common", "Unusual", "Rare", "Epic", "Legendary", "Mythic", "Ultra", "Super",
  "Eternal", "Unique", "Primordial",
];

export const RarityColors = [
  [0, 0, 0, 0, 0, 0],
  [111, 211, 96, 75, 193, 164],
  [255, 230, 93, 34, 240, 164],
  [68, 72, 200, 159, 131, 126],
  [134, 31, 222, 182, 181, 119],
  [219, 31, 31, 0, 180, 118],
  [31, 219, 222, 121, 181, 119],
  [225, 38, 103, 226, 182, 124],
  [40, 240, 153, 103, 209, 132],
  [238, 238, 238, 160, 0, 224],
  [53, 53, 53, 160, 0, 50],
  [110, 110, 110, 0, 0, 103],
];

export function rarityColor(rarity, alpha = 1) {
  const color = RarityColors[rarity] || RarityColors[0];
  return `rgba(${color[0]}, ${color[1]}, ${color[2]}, ${alpha})`;
}

export function petalTypeName(type) {
  return PetalNames[type] || `Petal${type}`;
}

export function mobTypeName(type) {
  return MobNames[type] || `Mob${type}`;
}

export function rarityName(rarity) {
  return RarityNames[rarity] || `Rarity${rarity}`;
}

export function isPetalEntity(entityType) {
  return entityType >= NETWORK_PETAL_TYPE_OFFSET && entityType < NETWORK_DROP_TYPE_OFFSET;
}

export function isDropEntity(entityType) {
  return entityType >= NETWORK_DROP_TYPE_OFFSET;
}

export function petalTypeFromEntity(entityType) {
  return entityType >= NETWORK_DROP_TYPE_OFFSET
    ? entityType - NETWORK_DROP_TYPE_OFFSET
    : entityType - NETWORK_PETAL_TYPE_OFFSET;
}

export function clamp(value, low, high) {
  return Math.max(low, Math.min(high, value));
}

export function axisToPacket(value) {
  return clamp(Math.round(clamp(value, -1, 1) * 127), -127, 127);
}

function fitUtf8(text, maxBytes) {
  const bytes = textEncoder.encode(text);
  if (bytes.length <= maxBytes) return bytes;
  return bytes.slice(0, maxBytes);
}

function readString(bytes, offset, length) {
  return textDecoder.decode(bytes.subarray(offset, offset + length));
}

export function appendBytes(a, b) {
  if (!a || a.length === 0) return b;
  if (!b || b.length === 0) return a;
  const out = new Uint8Array(a.length + b.length);
  out.set(a, 0);
  out.set(b, a.length);
  return out;
}

export function popFrame(buffer) {
  if (!buffer || buffer.length < 2) return null;
  const length = buffer[0] | (buffer[1] << 8);
  if (length <= 0) {
    return { payload: new Uint8Array(), rest: buffer.slice(2) };
  }
  if (buffer.length < 2 + length) return null;
  return {
    payload: buffer.slice(2, 2 + length),
    rest: buffer.slice(2 + length),
  };
}

class Reader {
  constructor(bytes) {
    this.bytes = bytes;
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    this.offset = 0;
  }

  has(size) {
    return this.offset + size <= this.bytes.length;
  }

  u8() {
    if (!this.has(1)) throw new Error("packet underrun");
    return this.bytes[this.offset++];
  }

  u16() {
    if (!this.has(2)) throw new Error("packet underrun");
    const value = this.view.getUint16(this.offset, true);
    this.offset += 2;
    return value;
  }

  i16() {
    if (!this.has(2)) throw new Error("packet underrun");
    const value = this.view.getInt16(this.offset, true);
    this.offset += 2;
    return value;
  }

  u32() {
    if (!this.has(4)) throw new Error("packet underrun");
    const value = this.view.getUint32(this.offset, true);
    this.offset += 4;
    return value;
  }

  i32() {
    if (!this.has(4)) throw new Error("packet underrun");
    const value = this.view.getInt32(this.offset, true);
    this.offset += 4;
    return value;
  }

  string(length) {
    if (!this.has(length)) throw new Error("packet underrun");
    const value = readString(this.bytes, this.offset, length);
    this.offset += length;
    return value;
  }
}

function parseEntity(reader) {
  const entity = {};
  entity.entityId = reader.u16();
  entity.entityType = reader.u8();
  entity.team = reader.u8();
  entity.pos = {
    x: reader.i32() / NET_COORD_SCALE,
    y: reader.i32() / NET_COORD_SCALE,
  };
  entity.radius = reader.u16() / NET_RADIUS_SCALE;
  entity.hpPercent = reader.u8() / 255;
  entity.flags = reader.u8();
  entity.angle = reader.i16() / NET_ANGLE_SCALE;
  entity.rarity = reader.u8();
  const nameLength = reader.u8();
  entity.name = reader.string(nameLength);
  return entity;
}

export function parseServerMessage(payload) {
  try {
    const reader = new Reader(payload);
    const type = reader.u8();
    const msg = { type };

    if (type === ServerType.Welcome) {
      msg.playerId = reader.u16();
      msg.ownerEntityId = reader.u16();
      msg.tickRate = reader.u8();
      msg.mapName = reader.has(1) ? reader.string(reader.u8()) : "";
      return msg;
    }

    if (type === ServerType.Snapshot) {
      msg.snapshotId = reader.u32();
      msg.ownerEntityId = reader.u16();
      msg.viewRadius = reader.i32() / NET_COORD_SCALE;
      const count = reader.u16();
      msg.entities = [];
      for (let i = 0; i < count; i += 1) msg.entities.push(parseEntity(reader));
      return msg;
    }

    if (type === ServerType.AuthResult) {
      msg.success = reader.u8() !== 0;
      msg.message = reader.string(reader.u8());
      return msg;
    }

    if (type === ServerType.OwnerState) {
      msg.level = reader.u8();
      msg.flags = reader.u8();
      const primaryCount = reader.u8();
      const secondaryCount = reader.u8();
      const slotBytes = (primaryCount + secondaryCount) * 2;
      if (reader.has(8 + slotBytes)) {
        msg.exp = reader.u32();
        msg.expRequired = reader.u32();
      } else {
        msg.exp = 0;
        msg.expRequired = 0;
      }
      msg.ownerSlots = [];
      msg.secondarySlots = [];
      for (let i = 0; i < primaryCount; i += 1) {
        msg.ownerSlots.push({ petalType: reader.u8(), rarity: reader.u8() });
      }
      for (let i = 0; i < secondaryCount; i += 1) {
        msg.secondarySlots.push({ petalType: reader.u8(), rarity: reader.u8() });
      }
      return msg;
    }

    if (type === ServerType.Inventory) {
      const count = reader.u16();
      msg.inventory = [];
      for (let i = 0; i < count; i += 1) {
        msg.inventory.push({ petalType: reader.u8(), rarity: reader.u8(), count: reader.u32() });
      }
      return msg;
    }

    if (type === ServerType.Chat) {
      msg.chat = {};
      msg.chat.flag = reader.u8();
      msg.chat.playerId = reader.u16();
      msg.chat.time = reader.u32();
      const nameLength = reader.u8();
      const messageLength = reader.u8();
      msg.chat.playerName = reader.string(nameLength);
      msg.chat.message = reader.string(messageLength);
      return msg;
    }

    if (type === ServerType.CraftResult) {
      msg.success = reader.u8() !== 0;
      msg.petalType = reader.u8();
      msg.rarity = reader.u8();
      msg.consumed = reader.u32();
      const count = reader.u16();
      msg.items = [];
      for (let i = 0; i < count; i += 1) {
        msg.items.push({ petalType: reader.u8(), rarity: reader.u8(), count: reader.u32() });
      }
      return msg;
    }

    return { type: 0xff };
  } catch (error) {
    return { type: 0xff, error };
  }
}

export function packAuth(name, password, registerMode) {
  const nameBytes = fitUtf8(name, 32);
  const passwordBytes = fitUtf8(password, 64);
  if (nameBytes.length === 0) return null;
  const out = new Uint8Array(4 + nameBytes.length + passwordBytes.length);
  let offset = 0;
  out[offset++] = 0xf0;
  out[offset++] = registerMode ? 1 : 0;
  out[offset++] = nameBytes.length;
  out[offset++] = passwordBytes.length;
  out.set(nameBytes, offset);
  offset += nameBytes.length;
  out.set(passwordBytes, offset);
  return out;
}

export function packInput(moveX, moveY) {
  const out = new Uint8Array(3);
  out[0] = 0x00;
  out[1] = axisToPacket(moveX) & 0xff;
  out[2] = axisToPacket(moveY) & 0xff;
  return out;
}

export function packChores(attacking, defending, agree = false, disconnect = false) {
  let value = 0x03;
  if (attacking) value |= 1 << 2;
  if (defending) value |= 1 << 3;
  if (agree) value |= 1 << 4;
  if (disconnect) value |= 1 << 5;
  return new Uint8Array([value]);
}

export function packEquip(slotIndex, petalType, rarity) {
  return new Uint8Array([0x01, petalType & 0xff, ((slotIndex & 0x0f) << 4) | (rarity & 0x0f)]);
}

export function packUnequip(slotIndex) {
  return new Uint8Array([0x02 | ((slotIndex & 0x0f) << 2)]);
}

export function packSecondarySlot(slotIndex, petalType, rarity) {
  return new Uint8Array([0xf2, slotIndex & 0xff, petalType & 0xff, rarity & 0xff]);
}

export function packCraft(petalType, rarity, count) {
  const out = new Uint8Array(7);
  const safeCount = Math.max(0, Math.min(0xffffffff, Math.floor(count || 0)));
  out[0] = 0xf3;
  out[1] = petalType & 0xff;
  out[2] = rarity & 0xff;
  const view = new DataView(out.buffer);
  view.setUint32(3, safeCount, true);
  return out;
}

export function packChat(flag, message) {
  const messageBytes = fitUtf8(message, MAX_CHAT_MESSAGE_SIZE);
  if (messageBytes.length === 0) return null;
  const out = new Uint8Array(3 + messageBytes.length);
  out[0] = 0xf1;
  out[1] = flag & 0xff;
  out[2] = messageBytes.length;
  out.set(messageBytes, 3);
  return out;
}
