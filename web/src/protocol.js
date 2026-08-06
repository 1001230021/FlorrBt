import { t } from "./i18n.js";

const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

export const NETWORK_PETAL_TYPE_OFFSET = 100;
export const NETWORK_DROP_TYPE_OFFSET = 180;
export const NETWORK_TRAP_PROJECTILE_ENTITY_TYPE = 93;
export const NETWORK_BLOOD_SACRIFICE_ENTITY_TYPE = 94;
export const NETWORK_DANDELION_MISSILE_ENTITY_TYPE = 95;
export const NETWORK_POLLEN_ENTITY_TYPE = 96;
export const NETWORK_SPIDER_WEB_ENTITY_TYPE = 97;
export const NETWORK_MISSILE_ENTITY_TYPE = 98;
export const NETWORK_PORTAL_ENTITY_TYPE = 99;
export const MAX_CHAT_MESSAGE_SIZE = 180;
export const NET_COORD_SCALE = 64;
export const NET_RELATIVE_COORD_SCALE = 1;
export const NET_RADIUS_SCALE = 1;
export const NET_ANGLE_SCALE = 1000;
export const NET_PERCENT_SCALE = 255;
export const NET_SLOT_SIZE_SCALE = 65535;
export const FULL_SNAPSHOT_BASE_ID = 0xffffffff;
const ENTITY_SNAPSHOT_FULL = 0;
const ENTITY_SNAPSHOT_COMPACT = 1;

export const ChatFlag = Object.freeze({
  Global: 0,
  Local: 1,
  Server: 2,
  Whisper: 3,
  Squad: 4,
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

export const PetalSlotCopyState = Object.freeze({
  Alive: 0,
  Loading: 1,
});

export const PetalSlotVisualType = Object.freeze({
  None: 0,
  Angle: 1,
  Size: 2,
});

export const AuthMode = Object.freeze({
  Login: 0,
  Register: 1,
  RequestRegistrationCode: 2,
  RequestBindingCode: 3,
  ConfirmBinding: 4,
});

export const AuthResultCode = Object.freeze({
  Failed: 0,
  Authenticated: 1,
  EmailBindingRequired: 2,
  VerificationCodeSending: 3,
  VerificationCodeSent: 4,
  EmailBound: 5,
});

export const PetalNames = [
  "None",
  "Air",
  "AntEgg",
  "Antennae",
  "Basic",
  "BeetleEgg",
  "Bone",
  "Bubble",
  "Carrot",
  "Coin",
  "Compass",
  "Cogwheel",
  "Disc",
  "Dust",
  "GoldenLeaf",
  "Iris",
  "Lentil",
  "Moon",
  "Nullification",
  "Pincer",
  "Relic",
  "Rose",
  "YinYang",
  "Missile",
  "BloodSacrifice",
  "Corruption",
  "Bandage",
  "Heavy",
  "Faster",
  "Yggdrasil",
  "Dahlia",
  "Wing",
  "Triangle",
  "Sawblade",
  "Fragment",
  "Mimic",
  "Glass",
  "Stinger",
  "BrokenEgg",
  "Light",
  "Leaf",
  "Rock",
  "Web",
  "Cactus",
  "Pollen",
  "Corn",
  "Rice",
  "Basil",
  "Soil",
  "Honey",
  "Wax",
  "ThirdEye",
  "Dandelion",
  "Orange",
  "Shovel",
  "Yucca",
  "WhiteFungus",
  "BlackFungus",
  "Broccoli",
  "Douli",
  "Trapper",
  "Amulet",
  "Plank",
  "Tomato",
];

export const MobNames = [
  "None",
  "Beetle",
  "Gambler",
  "NormalLadybug",
  "MechaFlower",
  "NormalFlower",
  "PlayerFlower",
  "SoldierAnt",
  "SoldierFireAnt",
  "SoldierTermite",
  "SummonedBeetle",
  "SummonedSoldierAnt",
  "BandageBeetle",
  "Bee",
  "Hornet",
  "BumbleBee",
  "Rock",
  "BabyAnt",
  "WorkerAnt",
  "QueenAnt",
  "AntHole",
  "Spider",
  "Sandstorm",
  "Dummy",
  "Dandelion",
  "AntEgg",
  "FireAntEgg",
  "TermiteEgg",
  "QueenAntEgg",
  "QueenFireAntEgg",
  "BabyFireAnt",
  "WorkerFireAnt",
  "FireQueenAnt",
  "BabyTermite",
  "WorkerTermite",
  "TermiteOvermind",
  "LeafPiece",
  "LeafcutterSoldier",
  "Titan",
];

export const RarityNames = [
  "Null",
  "Common",
  "Unusual",
  "Rare",
  "Epic",
  "Legendary",
  "Mythic",
  "Ultra",
  "Super",
  "Eternal",
  "Unique",
  "Primordial",
  "Exotic",
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
  [218, 218, 218, 170, 170, 170],
];

export function rarityColor(rarity, alpha = 1) {
  const color = RarityColors[rarity] || RarityColors[0];
  return `rgba(${color[0]}, ${color[1]}, ${color[2]}, ${alpha})`;
}

function translationSlug(name) {
  return String(name || "")
    .replace(/([a-z0-9])([A-Z])/g, "$1_$2")
    .replace(/[^A-Za-z0-9]+/g, "_")
    .replace(/^_+|_+$/g, "")
    .toLowerCase();
}

export function petalTypeName(type) {
  const fallback = PetalNames[type] || "Petal" + type;
  return t("entities.petals." + translationSlug(fallback), {}, {
    defaultValue: fallback,
  });
}

export function mobTypeName(type) {
  const fallback = MobNames[type] || "Mob" + type;
  return t("entities.mobs." + translationSlug(fallback), {}, {
    defaultValue: fallback,
  });
}

export function rarityName(rarity) {
  const fallback = RarityNames[rarity] || "Rarity" + rarity;
  return t("entities.rarities." + translationSlug(fallback), {}, {
    defaultValue: fallback,
  });
}

export function isPetalEntity(entityType) {
  return (
    entityType >= NETWORK_PETAL_TYPE_OFFSET &&
    entityType < NETWORK_DROP_TYPE_OFFSET
  );
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

  u64() {
    const low = this.u32();
    const high = this.u32();
    return low + high * 0x100000000;
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

function parseEntity(reader, origin = null) {
  const entity = {};
  const format = reader.u8();
  if (format === ENTITY_SNAPSHOT_COMPACT) {
    if (!origin) throw new Error("compact entity without origin");
    entity.entityId = reader.u16();
    entity.entityType = reader.u8();
    entity.team = reader.u8();
    entity.pos = {
      x: origin.x + reader.i16() / NET_RELATIVE_COORD_SCALE,
      y: origin.y + reader.i16() / NET_RELATIVE_COORD_SCALE,
    };
    entity.radius = reader.u16() / NET_RADIUS_SCALE;
    entity.hpPercent = reader.u8() / NET_PERCENT_SCALE;
    entity.shieldPercent = reader.u8() / NET_PERCENT_SCALE;
    entity.flags = reader.u16();
    entity.angle = reader.i16() / NET_ANGLE_SCALE;
    entity.rarity = reader.u8();
    entity.name = "";
    entity.primarySlots = [];
    entity.states = [];
    return entity;
  }
  if (format !== ENTITY_SNAPSHOT_FULL)
    throw new Error("unknown entity snapshot format");

  entity.entityId = reader.u16();
  entity.entityType = reader.u8();
  entity.team = reader.u8();
  entity.pos = {
    x: reader.i32() / NET_COORD_SCALE,
    y: reader.i32() / NET_COORD_SCALE,
  };
  entity.radius = reader.u16() / NET_RADIUS_SCALE;
  entity.hpPercent = reader.u8() / NET_PERCENT_SCALE;
  entity.shieldPercent = reader.u8() / NET_PERCENT_SCALE;
  entity.flags = reader.u16();
  entity.angle = reader.i16() / NET_ANGLE_SCALE;
  entity.rarity = reader.u8();
  const nameLength = reader.u8();
  entity.name = reader.string(nameLength);
  const primarySlotCount = reader.u8();
  entity.primarySlots = [];
  for (let i = 0; i < primarySlotCount; i += 1) {
    const slot = {
      petalType: reader.u8(),
      rarity: reader.u8(),
      visualType: reader.u8(),
      copies: [],
    };
    if (
      slot.visualType !== PetalSlotVisualType.None &&
      slot.visualType !== PetalSlotVisualType.Angle &&
      slot.visualType !== PetalSlotVisualType.Size
    )
      throw new Error("unknown petal slot visual type");
    const copyCount = reader.u8();
    for (let copy = 0; copy < copyCount; copy += 1) {
      const copySnap = {
        state: reader.u8(),
        progress: reader.u8() / NET_PERCENT_SCALE,
        visual: null,
      };
      if (slot.visualType === PetalSlotVisualType.Angle)
        copySnap.visual = reader.i16() / NET_ANGLE_SCALE;
      else if (slot.visualType === PetalSlotVisualType.Size)
        copySnap.visual = reader.u16() / NET_SLOT_SIZE_SCALE;
      slot.copies.push(copySnap);
    }
    entity.primarySlots.push(slot);
  }
  const stateCount = reader.u8();
  entity.states = [];
  for (let i = 0; i < stateCount; i += 1) {
    entity.states.push({ type: reader.u8(), rarity: reader.u8() });
  }
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
      msg.baseSnapshotId = reader.u32();
      msg.serverTick = reader.u64();
      msg.ownerEntityId = reader.u16();
      msg.viewRadius = reader.i32() / NET_COORD_SCALE;
      const count = reader.u16();
      const removedCount = reader.u16();
      msg.entities = [];
      let origin = null;
      for (let i = 0; i < count; i += 1) {
        const entity = parseEntity(reader, origin);
        if (!origin) origin = entity.pos;
        msg.entities.push(entity);
      }
      msg.removedEntityIds = [];
      for (let i = 0; i < removedCount; i += 1) {
        msg.removedEntityIds.push(reader.u16());
      }
      return msg;
    }

    if (type === ServerType.AuthResult) {
      msg.resultCode = reader.u8();
      msg.success =
        msg.resultCode === AuthResultCode.Authenticated ||
        msg.resultCode === AuthResultCode.EmailBound;
      msg.message = reader.string(reader.u8());
      return msg;
    }

    if (type === ServerType.OwnerState) {
      msg.level = reader.u8();
      msg.flags = reader.u8();
      const primaryCount = reader.u8();
      const secondaryCount = reader.u8();
      const slotBytes = (primaryCount + secondaryCount) * 2;
      if (reader.has(2 + slotBytes)) {
        msg.expProgress = clamp(reader.u16() / 10000, 0, 1);
      } else {
        msg.expProgress = 0;
      }
      msg.ownerSlots = [];
      msg.secondarySlots = [];
      for (let i = 0; i < primaryCount; i += 1) {
        msg.ownerSlots.push({ petalType: reader.u8(), rarity: reader.u8() });
      }
      for (let i = 0; i < secondaryCount; i += 1) {
        msg.secondarySlots.push({
          petalType: reader.u8(),
          rarity: reader.u8(),
        });
      }
      msg.talentPoints = 0;
      msg.talents = [];
      if (reader.has(3)) {
        msg.talentPoints = reader.u16();
        const talentCount = reader.u8();
        for (let i = 0; i < talentCount; i += 1) {
          msg.talents.push({
            id: reader.u16(),
            rarity: reader.u8(),
            rank: reader.u8(),
          });
        }
      }
      return msg;
    }

    if (type === ServerType.Inventory) {
      const count = reader.u16();
      msg.inventory = [];
      for (let i = 0; i < count; i += 1) {
        msg.inventory.push({
          petalType: reader.u8(),
          rarity: reader.u8(),
          count: reader.u32(),
        });
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
        msg.items.push({
          petalType: reader.u8(),
          rarity: reader.u8(),
          count: reader.u32(),
        });
      }
      return msg;
    }

    return { type: 0xff };
  } catch (error) {
    return { type: 0xff, error };
  }
}

export function packAuth({
  mode = AuthMode.Login,
  name = "",
  password = "",
  email = "",
  code = "",
} = {}) {
  const nameBytes = fitUtf8(name, 32);
  const passwordBytes = fitUtf8(password, 64);
  const emailBytes = fitUtf8(email, 254);
  const codeBytes = fitUtf8(code, 32);
  if (nameBytes.length === 0) return null;
  const out = new Uint8Array(
    6 +
      nameBytes.length +
      passwordBytes.length +
      emailBytes.length +
      codeBytes.length,
  );
  let offset = 0;
  out[offset++] = 0xf0;
  out[offset++] = mode;
  out[offset++] = nameBytes.length;
  out[offset++] = passwordBytes.length;
  out[offset++] = emailBytes.length;
  out[offset++] = codeBytes.length;
  out.set(nameBytes, offset);
  offset += nameBytes.length;
  out.set(passwordBytes, offset);
  offset += passwordBytes.length;
  out.set(emailBytes, offset);
  offset += emailBytes.length;
  out.set(codeBytes, offset);
  return out;
}

export function packInput(moveX, moveY) {
  const out = new Uint8Array(3);
  out[0] = 0x00;
  out[1] = axisToPacket(moveX) & 0xff;
  out[2] = axisToPacket(moveY) & 0xff;
  return out;
}

export function packInputFrame(
  sequence,
  targetServerTick,
  moveX,
  moveY,
  attacking,
  defending,
  digging,
) {
  const out = new Uint8Array(16);
  const view = new DataView(out.buffer);
  const safeTick = Math.max(
    0,
    Math.min(Number.MAX_SAFE_INTEGER, Math.floor(Number(targetServerTick) || 0)),
  );

  out[0] = 0xf8;
  view.setUint32(1, sequence >>> 0, true);
  view.setUint32(5, safeTick >>> 0, true);
  view.setUint32(9, Math.floor(safeTick / 0x100000000) >>> 0, true);
  view.setInt8(13, axisToPacket(moveX));
  view.setInt8(14, axisToPacket(moveY));
  out[15] =
    (attacking ? 1 << 0 : 0) |
    (defending ? 1 << 1 : 0) |
    (digging ? 1 << 2 : 0);
  return out;
}

export function packChores(
  attacking,
  defending,
  agree = false,
  disconnect = false,
  digging = false,
) {
  let value = 0x03;
  if (attacking) value |= 1 << 2;
  if (defending) value |= 1 << 3;
  if (agree) value |= 1 << 4;
  if (disconnect) value |= 1 << 5;
  if (digging) value |= 1 << 6;
  return new Uint8Array([value]);
}

export function packEquip(slotIndex, petalType, rarity) {
  return new Uint8Array([
    0x01,
    petalType & 0xff,
    ((slotIndex & 0x0f) << 4) | (rarity & 0x0f),
  ]);
}

export function packUnequip(slotIndex) {
  return new Uint8Array([0x02 | ((slotIndex & 0x0f) << 2)]);
}

export function packSecondarySlot(slotIndex, petalType, rarity) {
  return new Uint8Array([
    0xf2,
    slotIndex & 0xff,
    petalType & 0xff,
    rarity & 0xff,
  ]);
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

export function packForge(petalType) {
  const out = new Uint8Array(7);
  out[0] = 0xf6;
  out[1] = petalType & 0xff;
  out[2] = 8;
  const view = new DataView(out.buffer);
  view.setUint32(3, 5, true);
  return out;
}

export function packStateRequest() {
  return new Uint8Array([0xf5]);
}

export function packSnapshotAck(snapshotId = FULL_SNAPSHOT_BASE_ID) {
  const out = new Uint8Array(5);
  out[0] = 0xf7;
  new DataView(out.buffer).setUint32(1, snapshotId >>> 0, true);
  return out;
}

export function packTalentRequest(action, talents) {
  const safeTalents = (talents || [])
    .filter((talent) => talent && talent.id > 0 && talent.rarity > 0)
    .slice(0, 64);
  if (safeTalents.length === 0) return null;
  const out = new Uint8Array(3 + safeTalents.length * 4);
  const view = new DataView(out.buffer);
  out[0] = 0xf4;
  out[1] = action === "remove" || action === 2 ? 2 : 1;
  out[2] = safeTalents.length & 0xff;
  let offset = 3;
  for (const talent of safeTalents) {
    view.setUint16(offset, talent.id & 0xffff, true);
    offset += 2;
    out[offset++] = talent.rarity & 0xff;
    out[offset++] = (talent.rank || 0) & 0xff;
  }
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
