import fs from "node:fs";

const zoneRepairs = [
    [118, 72.9],
    [120, 40],
    [121, 41.1],
    [122, 42.3],
    [123, 43.4],
    [124, 44.5],
    [125, 45.7],
    [126, 46.8],
    [127, 47.9],
    [128, 49.1],
    [129, 50.2],
    [130, 51.3],
    [131, 52.5],
    [132, 53.6],
    [133, 54.7],
    [134, 55.9],
    [135, 54.7],
    [136, 55.9],
    [137, 53.6],
    [138, 52.5],
    [139, 51.3],
    [140, 52.5],
    [141, 53.6],
    [142, 54.7],
    [143, 55.9],
    [144, 57],
    [145, 58.1],
    [146, 59.3],
    [147, 60.4],
    [148, 61.5],
    [149, 62.7],
    [150, 63.8],
    [151, 64.9],
    [152, 66.1],
    [153, 67.2],
    [154, 68.3],
    [155, 69.5],
    [156, 70.6],
    [157, 71.7],
    [158, 72.9],
    [159, 74],
    [161, 74],
    [162, 70.6],
    [163, 71.7],
    [164, 72.9],
    [166, 42.3],
    [168, 44.5],
    [169, 46.8],
    [170, 49.1],
    [171, 51.3],
    [172, 51.3],
    [174, 53.6],
    [175, 53.6],
    [176, 53.6],
    [177, 55.9],
    [178, 55.9],
    [179, 55.9],
    [180, 58.1],
    [181, 60.4],
    [182, 62.7],
    [183, 64.9],
    [184, 67.2],
    [185, 69.5],
    [186, 71.7],
    [187, 71.7],
];

const mapPath = "data/maps/ant_hel.tmj";
const backupPath = "data/maps/ant_hel.tmj.corrupt.bak";
const original = fs.readFileSync(mapPath, "utf8");
const newline = original.includes("\r\n") ? "\r\n" : "\n";
const markerPattern = /^(\s*)\$1([0-9]+(?:\.[0-9]+)?)\r?$/gm;
const markers = [...original.matchAll(markerPattern)];

if (markers.length !== zoneRepairs.length) {
    throw new Error(`Expected ${zoneRepairs.length} broken markers, found ${markers.length}`);
}

let repairIndex = 0;
const repaired = original.replace(markerPattern, (_, indent, encodedDifficulty) => {
    const [id, difficulty] = zoneRepairs[repairIndex++];
    if (Number(encodedDifficulty) !== difficulty) {
        throw new Error(`Marker ${repairIndex} for id ${id}: expected ${difficulty}, found ${encodedDifficulty}`);
    }

    const density = id <= 164 ? 7 : 0;
    const propertyIndent = `${indent}        `;
    return [
        `${indent}"id":${id},`,
        `${indent}"name":"",`,
        `${indent}"properties":[`,
        `${propertyIndent}{`,
        `${propertyIndent} "name":"density",`,
        `${propertyIndent} "type":"float",`,
        `${propertyIndent} "value":${density}`,
        `${propertyIndent}}, `,
        `${propertyIndent}{`,
        `${propertyIndent} "name":"difficulty",`,
        `${propertyIndent} "type":"float",`,
        `${propertyIndent} "value":${difficulty}`,
    ].join(newline);
});

JSON.parse(repaired);
if (!fs.existsSync(backupPath)) {
    fs.copyFileSync(mapPath, backupPath);
}
fs.writeFileSync(mapPath, repaired, "utf8");
console.log(`Repaired ${repairIndex} broken zone records; JSON is valid.`);
