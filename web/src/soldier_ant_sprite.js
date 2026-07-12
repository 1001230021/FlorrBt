const SOLDIER_ANT_VIEWBOX_SIZE = 110;
const SOLDIER_ANT_BODY_RADIUS = 19;
const SOLDIER_ANT_BASE_FACE_ANGLE = -Math.PI * 0.75;
const SOLDIER_ANT_WING_ALPHA = 0.498;
const SOLDIER_ANT_FORELIMB_WIDTH = 5.833;
const SOLDIER_ANT_FORELIMB_COLOR = "#292929";
const SOLDIER_ANT_MOTION_DEADZONE = 0.12;
const SUMMONED_BODY_FILL = "#f4dc59";
const SUMMONED_BODY_DARK_FILL = "#d7bd3c";
const SUMMONED_BODY_STROKE = "#c8ad35";
const SUMMONED_BODY_CACHE = new Map();

const abdomenSrc = new URL("../assets/soldier_ant_head.svg", import.meta.url).href;
const headSrc = new URL("../assets/soldier_ant_abdomen.svg", import.meta.url).href;
const abdomenImage = makeImage(abdomenSrc);
const headImage = makeImage(headSrc);
const wingParts = [
  {
    pivot: { x: 48.736, y: 52.701 },
    direction: -1,
    path: "M48.736 52.701q.127-.065.262-.116.134-.052.275-.09.14-.038.287-.062t.299-.034.31-.007q.157.003.32.02t.33.048.338.076.346.102.353.13.36.156.364.183q.183.098.369.208.185.111.372.234t.375.259.377.283.378.306.377.33.377.35q.188.18.375.372.186.191.371.392.185.202.368.412t.364.43.358.447.352.463.344.479q.17.242.337.492.166.25.327.505.162.255.318.516.157.26.308.527.151.265.297.535.145.27.284.544.14.273.272.55.133.276.26.554.126.278.245.558t.23.561.216.562.2.562.185.56.168.556.152.553.134.547.116.54q.054.267.1.53.044.264.08.522.036.259.063.511.026.253.044.5.017.245.026.485.008.239.007.47-.001.233-.011.456-.01.224-.03.44-.02.214-.048.42-.029.206-.067.402-.037.197-.084.383t-.103.362-.12.34q-.065.165-.138.318t-.155.295-.172.271-.187.247-.204.221-.22.196-.233.17-.248.143-.262.117-.275.09q-.14.037-.287.061-.147.025-.3.035-.152.01-.31.007-.157-.003-.32-.02-.162-.018-.33-.048-.166-.031-.337-.076-.172-.044-.346-.103-.175-.058-.354-.13-.178-.07-.359-.156-.18-.084-.364-.182t-.37-.209-.372-.234-.375-.258-.377-.283-.377-.306q-.19-.16-.378-.33-.189-.17-.376-.35-.188-.181-.375-.373-.187-.19-.372-.392-.185-.2-.368-.411t-.363-.43-.359-.447q-.177-.228-.351-.464-.174-.235-.345-.478t-.336-.492-.328-.505q-.161-.255-.318-.516-.156-.261-.308-.527-.15-.266-.296-.536-.145-.27-.285-.543t-.272-.55-.259-.554-.245-.559q-.12-.28-.231-.56-.112-.281-.216-.562t-.2-.562-.185-.56-.168-.557-.151-.552-.134-.547-.117-.54-.099-.53-.08-.523-.063-.51-.044-.5-.026-.485-.008-.471.012-.456.03-.438.048-.421.066-.403.085-.382.102-.362.12-.34.139-.319.154-.294.172-.271.188-.247.204-.222q.105-.104.219-.196.113-.091.234-.17.12-.078.248-.143",
  },
  {
    pivot: { x: 52.701, y: 48.736 },
    direction: 1,
    path: "M52.701 48.736q.065-.128.144-.248t.17-.234.195-.22.222-.203.247-.188.27-.172.295-.154.319-.138q.164-.065.34-.12.176-.056.362-.103t.382-.085.403-.066.42-.049.44-.03.455-.01.471.007q.24.008.486.026t.498.044.511.062.522.081q.264.045.531.1.268.053.54.116t.547.134.552.151.557.168.56.185q.28.096.562.2.28.104.561.216t.561.23q.28.12.559.246.278.126.554.259.277.133.55.272.274.14.543.285.27.145.536.296.266.152.527.308.26.157.516.318.256.162.505.328.25.166.492.336.243.17.478.345.236.174.464.351.227.178.447.359.22.18.43.363t.411.368.392.372.373.375q.18.187.35.376t.33.378.306.377q.147.189.283.377t.258.375q.124.187.234.373.11.185.209.369.098.183.182.364.085.181.157.36.071.178.13.353.058.174.102.346.045.17.076.338.03.167.048.33.017.162.02.32.003.157-.007.31-.01.152-.035.299-.024.146-.062.287t-.09.275q-.05.134-.116.262-.065.127-.143.248-.078.12-.17.234-.091.113-.196.219-.104.106-.221.204t-.247.187-.27.172q-.143.082-.296.155t-.318.138-.34.12-.362.103-.383.084-.402.067-.42.048-.44.03-.455.011-.471-.007q-.24-.009-.486-.026t-.499-.044-.51-.063q-.259-.036-.522-.08-.264-.046-.532-.1t-.54-.116q-.271-.063-.546-.134t-.553-.152-.556-.168-.56-.184-.562-.2q-.28-.105-.562-.216-.28-.112-.56-.231t-.559-.246-.555-.259-.55-.272-.543-.284q-.27-.146-.535-.297-.266-.151-.527-.308-.26-.156-.516-.318-.256-.161-.505-.327-.25-.167-.492-.337-.243-.17-.479-.344-.235-.175-.463-.352-.228-.178-.447-.358-.22-.18-.43-.364-.21-.183-.412-.368-.2-.185-.392-.371-.191-.187-.372-.375-.18-.188-.35-.377-.171-.188-.33-.377-.159-.19-.306-.378-.148-.189-.283-.377t-.259-.375-.234-.372-.208-.37-.183-.364-.156-.359-.13-.353-.102-.346-.076-.338-.048-.33-.02-.32q-.004-.158.007-.31t.034-.3q.024-.146.062-.286.038-.141.09-.275.051-.135.116-.262",
  },
];
const forelimbParts = [
  {
    pivot: { x: 46.161, y: 54.41 },
    direction: -1,
    path: "M46.161 54.41q-8.25-4.713-11.785-14.141",
  },
  {
    pivot: { x: 54.41, y: 46.161 },
    direction: 1,
    path: "M54.41 46.161q-4.713-8.25-14.141-11.785",
  },
];
let wingPaths = null;
let forelimbPaths = null;

export function drawSoldierAnt(ctx, pos, radius, entityId, angle, motion, time, options = {}) {
  const spriteSize = Math.max(1, radius * (SOLDIER_ANT_VIEWBOX_SIZE / SOLDIER_ANT_BODY_RADIUS));
  const spriteHalf = spriteSize * 0.5;
  const rotation = (Number.isFinite(angle) ? angle : 0) - SOLDIER_ANT_BASE_FACE_ANGLE;
  const move = clamp01(motion || 0);
  const animation = antAnimation(entityId, move, time);
  const summoned = Boolean(options.summoned);
  const abdomen = summoned ? summonedBodyImage(abdomenSrc) : abdomenImage;
  const head = summoned ? summonedBodyImage(headSrc) : headImage;

  ctx.save();
  ctx.translate(pos.x, pos.y);
  ctx.rotate(rotation);

  drawForelimbs(ctx, spriteSize, animation);
  if (isImageReady(abdomen)) {
    ctx.drawImage(abdomen, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  } else {
    drawFallback(ctx, radius, summoned);
  }
  drawWings(ctx, spriteSize, animation, move);
  if (isImageReady(head)) {
    ctx.drawImage(head, -spriteHalf, -spriteHalf, spriteSize, spriteSize);
  }
  ctx.restore();
}

function antAnimation(entityId, motion, time) {
  const activeMotion = motion < SOLDIER_ANT_MOTION_DEADZONE ? 0 : motion;
  if (activeMotion <= 0) return { swing: 0 };
  const phase = time * ((26 + activeMotion * 22) / 3) + entityId * 0.73;
  return {
    swing: Math.sin(phase) * activeMotion * 0.22,
  };
}

function drawWings(ctx, spriteSize, animation, motion) {
  const paths = getWingPaths();
  if (!paths) return;
  const scale = spriteSize / SOLDIER_ANT_VIEWBOX_SIZE;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-SOLDIER_ANT_VIEWBOX_SIZE * 0.5, -SOLDIER_ANT_VIEWBOX_SIZE * 0.5);
  clipSvgBounds(ctx);
  ctx.fillStyle = "#eee";
  for (const part of paths) {
    const amount = animation.swing * part.direction;
    drawPathAroundPivot(ctx, part.path, part.pivot, amount, () => {
      ctx.globalAlpha = SOLDIER_ANT_WING_ALPHA;
      ctx.fill(part.path);
    });
  }
  ctx.restore();
}

function drawForelimbs(ctx, spriteSize, animation) {
  const paths = getForelimbPaths();
  if (!paths) return;
  const scale = spriteSize / SOLDIER_ANT_VIEWBOX_SIZE;

  ctx.save();
  ctx.scale(scale, scale);
  ctx.translate(-SOLDIER_ANT_VIEWBOX_SIZE * 0.5, -SOLDIER_ANT_VIEWBOX_SIZE * 0.5);
  clipSvgBounds(ctx);
  ctx.lineWidth = SOLDIER_ANT_FORELIMB_WIDTH;
  ctx.lineCap = "round";
  ctx.lineJoin = "round";
  ctx.strokeStyle = SOLDIER_ANT_FORELIMB_COLOR;
  for (const part of paths) {
    const amount = animation.swing * part.direction * 0.35;
    drawPathAroundPivot(ctx, part.path, part.pivot, amount, () => ctx.stroke(part.path));
  }
  ctx.restore();
}

function clipSvgBounds(ctx) {
  if (typeof ctx.roundRect === "function") {
    ctx.beginPath();
    ctx.roundRect(9.167, 9.167, 91.667, 91.667, 4.167);
    ctx.clip();
    return;
  }

  ctx.beginPath();
  ctx.rect(9.167, 9.167, 91.667, 91.667);
  ctx.clip();
}

function drawPathAroundPivot(ctx, path, pivot, angle, draw) {
  ctx.save();
  ctx.translate(pivot.x, pivot.y);
  ctx.rotate(angle);
  ctx.translate(-pivot.x, -pivot.y);
  draw(path);
  ctx.restore();
}

function getWingPaths() {
  if (typeof Path2D === "undefined") return null;
  if (!wingPaths) {
    wingPaths = wingParts.map((part) => ({
      pivot: part.pivot,
      direction: part.direction,
      path: new Path2D(part.path),
    }));
  }
  return wingPaths;
}

function getForelimbPaths() {
  if (typeof Path2D === "undefined") return null;
  if (!forelimbPaths) {
    forelimbPaths = forelimbParts.map((part) => ({
      pivot: part.pivot,
      direction: part.direction,
      path: new Path2D(part.path),
    }));
  }
  return forelimbPaths;
}

function makeImage(src) {
  const image = new Image();
  image.decoding = "async";
  image.src = src;
  return image;
}

function summonedBodyImage(src) {
  let entry = SUMMONED_BODY_CACHE.get(src);
  if (entry) return entry.image;

  const image = new Image();
  image.decoding = "async";
  entry = { image, url: "" };
  SUMMONED_BODY_CACHE.set(src, entry);

  fetch(src)
    .then((response) => {
      if (!response.ok) throw new Error(`Soldier ant SVG failed: ${src}`);
      return response.text();
    })
    .then((svgText) => {
      const blob = new Blob([recolorSummonedBodySvg(svgText)], { type: "image/svg+xml" });
      const url = URL.createObjectURL(blob);
      entry.url = url;
      image.src = url;
    })
    .catch(() => {
      image.failed = true;
    });

  return image;
}

function recolorSummonedBodySvg(svgText) {
  if (typeof DOMParser === "undefined" || typeof XMLSerializer === "undefined") {
    return svgText
      .replace(/#454545/gi, SUMMONED_BODY_DARK_FILL)
      .replace(/#555\b/gi, SUMMONED_BODY_FILL)
      .replace(/#905db0/gi, SUMMONED_BODY_FILL)
      .replace(/#754b8f/gi, SUMMONED_BODY_DARK_FILL);
  }

  const doc = new DOMParser().parseFromString(svgText, "image/svg+xml");
  const root = doc.documentElement;
  if (!root || root.tagName.toLowerCase() !== "svg") return svgText;

  root.querySelectorAll("[fill]").forEach((node) => {
    const fill = (node.getAttribute("fill") || "").trim().toLowerCase();
    if (!fill || fill === "none") return;
    node.setAttribute("fill", summonedBodyFill(fill));
  });
  root.querySelectorAll("[stroke]").forEach((node) => {
    const stroke = (node.getAttribute("stroke") || "").trim().toLowerCase();
    if (!stroke || stroke === "none") return;
    node.setAttribute("stroke", SUMMONED_BODY_STROKE);
  });
  return new XMLSerializer().serializeToString(root);
}

function summonedBodyFill(fill) {
  if (fill === "#555" || fill === "#905db0") return SUMMONED_BODY_FILL;
  if (fill === "#454545" || fill === "#754b8f") return SUMMONED_BODY_DARK_FILL;
  return SUMMONED_BODY_FILL;
}

function isImageReady(image) {
  return image && !image.failed && image.complete && image.naturalWidth > 0;
}

function clamp01(value) {
  return Math.max(0, Math.min(1, value));
}

function drawFallback(ctx, radius, summoned = false) {
  ctx.fillStyle = summoned ? SUMMONED_BODY_FILL : "#454545";
  ctx.beginPath();
  ctx.arc(radius * 0.34, radius * 0.34, radius * 0.95, 0, Math.PI * 2);
  ctx.arc(-radius * 0.34, -radius * 0.34, radius * 1.15, 0, Math.PI * 2);
  ctx.fill();
}
