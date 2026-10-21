"use strict";
// Checks generated resource data, GLB math and source integration; does not build/run the game.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const importer = require("./Compile-ResourceSprites.cjs");
const root = path.resolve(__dirname, "..");
const read = p => fs.readFileSync(path.join(root, p));
const sha = b => crypto.createHash("sha256").update(b).digest("hex");
const report = JSON.parse(read("resource/haedong_runtime/import_report.json"));
const png = read("resource/haedong_runtime/world_atlas.png");
const binary = read("resource/haedong_runtime/world_sprites.bin");
assert.equal(sha(png), report.atlas_sha256);
assert.equal(sha(binary), report.bindings_sha256);
assert.equal(sha(read("tools/Compile-ResourceSprites.cjs").toString("utf8").replace(/\r\n/g, "\n")),
             report.compiler_sha256);
assert.equal(report.source_assets.length, 46);
for (const source of report.source_assets)
    assert.equal(sha(read(`resource/haedong_lowpoly/glb/${source.id}.glb`)), source.sha256);
assert.equal(binary.toString("ascii", 0, 8), "HDSPRITE");
assert.equal(binary.readUInt32LE(8), 2);
const width = binary.readUInt32LE(12), height = binary.readUInt32LE(16), count = binary.readUInt32LE(20);
let crc = 0xffffffff;
for (const byte of png)
{
    crc ^= byte;
    for (let j = 0; j < 8; j++)
        crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
}
assert.equal((crc ^ 0xffffffff) >>> 0, binary.readUInt32LE(24));
const decoded = importer.decodePng(png);
assert.equal(decoded.w, width);
assert.equal(decoded.h, height);
assert.equal(count, report.aliases);
let cursor = 28;
const records = new Map();
for (let i = 0; i < count; i++)
{
    assert(cursor + 4 <= binary.length);
    const length = binary.readUInt32LE(cursor);
    cursor += 4;
    assert(length > 0 && length <= 128 && cursor + length + 52 <= binary.length);
    const key = binary.toString("utf8", cursor, cursor + length);
    cursor += length;
    assert(!records.has(key));
    const rect = Array.from({length : 8}, (_, j) => binary.readFloatLE(cursor + j * 4));
    cursor += 32;
    const material = binary.readUInt32LE(cursor);
    cursor += 4;
    const weapon = Array.from({length : 4}, (_, j) => binary.readFloatLE(cursor + j * 4));
    cursor += 16;
    assert([...rect, ...weapon ].every(Number.isFinite));
    assert(rect[2] > 0 && rect[3] > 0 && rect[2] <= 1024 && rect[3] <= 1024);
    assert(rect[4] >= 0 && rect[5] >= 0 && rect[6] > 0 && rect[7] > 0);
    assert(rect[4] + rect[6] <= 1 && rect[5] + rect[7] <= 1);
    assert(material >= 22 && material <= 27);
    let opaque = 0;
    const left = Math.round(rect[4] * width), top = Math.round(rect[5] * height);
    for (let y = 0; y < rect[3]; y++)
        for (let x = 0; x < rect[2]; x++)
            if (decoded.pixels[((top + y) * width + left + x) * 4 + 3] > 128)
                opaque++;
    assert(opaque > 5, `Empty resource sprite ${key}`);
    records.set(key, {rect, material, weapon});
}
assert.equal(cursor, binary.length);
const expect = key => assert(records.has(key), `Missing binding ${key}`);
for (let d = 0; d < 4; d++)
{
    expect(`hero_idle@${d}`);
    for (let f = 0; f < 8; f++)
        expect(`hero${f}@${d}`);
    for (let f = 0; f < 12; f++)
    {
        const key = `hero_attack${f}@${d}`;
        expect(key);
        const w = records.get(key).weapon;
        assert(Math.hypot(w[2] - w[0], w[3] - w[1]) > 1, `Missing projected weapon socket ${key}`);
    }
    for (let look = 0; look < 18; look++)
    {
        expect(`citizen${look}_idle@${d}`);
        for (let f = 0; f < 8; f++)
            expect(`citizen${look}_${f}@${d}`);
    }
    for (let kind = 0; kind < 3; kind++)
    {
        expect(`beast${kind}_idle@${d}`);
        for (let f = 0; f < 8; f++)
            expect(`beast${kind}_${f}@${d}`);
    }
}
for (let p = 0; p < 20; p++)
    expect(`prop${p}`);
for (let k = 0; k < 6; k++)
{
    const key = `tile${k}`;
    expect(key);
    assert.deepEqual(records.get(key).rect.slice(0, 4), [ -48, -24, 96, 48 ]);
}
for (let k = 0; k < 3; k++)
    expect(`loot${k}`);
for (const key
         of ["drain", "sluice", "sluice_open", "notice", "wreck", "ledger", "key", "corpse", "bandit_sign"])
    expect(key);
// Rest skinning must reproduce original GLB positions, without double-applying the mesh transform.
const asset = importer.load("human_protagonist"), rest = importer.geometry({asset : "human_protagonist"});
const originals =
    asset.doc.meshes.flatMap(m => m.primitives.flatMap(p => asset.accessor(p.attributes.POSITION)));
let matched = 0;
for (const t of rest.slice(0, 100))
    for (const v of t.vertices)
        if (originals.some(p => Math.hypot(...p.map((n, k) => n - v.p[k])) < .0001))
            matched++;
assert(matched > 250, "Rest skinning differs from original mesh bind positions");
const walk = importer.geometry({asset : "human_protagonist"}, "walk", .25);
assert(walk.some((t, i) => t.vertices.some(
                     (v, j) => Math.hypot(...v.p.map((n, k) => n - rest[i].vertices[j].p[k])) > .01)));
// Each look has its own atlas content, and four directional images are not horizontal mirrors.
const hashes = new Set();
for (let look = 0; look < 18; look++)
    hashes.add(JSON.stringify(records.get(`citizen${look}_idle@1`).rect));
assert.equal(hashes.size, 18);
const project = read("BaseProject_SE/SimpleGame/SimpleGame.vcxproj").toString();
assert(project.includes('Name="CopyResourceSprites" AfterTargets="Build"'));
assert(project.includes('windowscodecs.lib;ole32.lib;'));
console.log(JSON.stringify({
    event : "resource_sprite_contracts",
    passed : true,
    source_assets : 46,
    bindings : count,
    sprites : report.sprites,
    atlas_rgba_bytes : width * height * 4,
    game_build_performed : false,
    game_execution_performed : false
}));
