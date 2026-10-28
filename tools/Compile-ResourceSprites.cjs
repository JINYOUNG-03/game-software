"use strict";
// Offline import of the supplied GLBs, not a game build or game/screen execution.
// Produces a deterministic, shared quarter-view atlas; never edits source art.
const fs = require("node:fs");
const path = require("node:path");
const zlib = require("node:zlib");
const crypto = require("node:crypto");
const assert = require("node:assert/strict");
const root = path.resolve(__dirname, "..");
const source = path.join(root, "resource/haedong_lowpoly/glb");
const destination = path.join(root, "resource/haedong_runtime");
const revision = 2; // The binary layout stays compatible; startup validates the expanded required bindings.
const presentation =
    fs.readFileSync(path.join(root, "BaseProject_SE/SimpleGame/CharacterPresentation.h"), "utf8");
const constant = name => Number(presentation.match(new RegExp(name + " = (\\d+);"))[1]);
const directionCount = constant("DirectionCount");
const walkFrames = constant("WalkFrames");
const beastAttackFrames = constant("BeastAttackFrames");
const screenVectors =
    [ [ 1, 0 ], [ 0, 1 ], [ -1, 0 ], [ 0, -1 ], [ 1, 1 ], [ -1, 1 ], [ -1, -1 ], [ 1, -1 ] ];
// Invert the 2:1 projection: equal screen diagonals are NOT equal 45-degree GLB yaw steps.
const directionYaws =
    screenVectors.map(([ x, y ]) => (Math.atan2(x + 2 * y, 2 * y - x) * 180 / Math.PI + 360) % 360);
const humanWidthScale = Number(presentation.match(/HumanWidthScale = ([.\d]+)f/)[1]);
const humanDirectionYaws = screenVectors.map(
    ([ x, y ]) =>
        (Math.atan2(x / humanWidthScale + 2 * y, 2 * y - x / humanWidthScale) * 180 / Math.PI + 360) % 360);
assert.equal(directionCount, directionYaws.length);
const I = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

function mul(a, b)
{
    const c = Array(16).fill(0);
    for (let j = 0; j < 4; j++)
        for (let i = 0; i < 4; i++)
            for (let k = 0; k < 4; k++)
                c[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
    return c;
}

function point(m, p)
{
    return [ 0, 1, 2 ].map(i => m[i] * p[0] + m[4 + i] * p[1] + m[8 + i] * p[2] + m[12 + i]);
}

function trs(t = [ 0, 0, 0 ], q = [ 0, 0, 0, 1 ], s = [ 1, 1, 1 ])
{
    const [x, y, z, w] = q;
    return [
        (1 - 2 * y * y - 2 * z * z) * s[0], (2 * x * y + 2 * z * w) * s[0], (2 * x * z - 2 * y * w) * s[0], 0,
        (2 * x * y - 2 * z * w) * s[1], (1 - 2 * x * x - 2 * z * z) * s[1], (2 * y * z + 2 * x * w) * s[1], 0,
        (2 * x * z + 2 * y * w) * s[2], (2 * y * z - 2 * x * w) * s[2], (1 - 2 * x * x - 2 * y * y) * s[2], 0,
        ...t, 1
    ];
}

function axis(axis, degrees)
{
    const q = [ 0, 0, 0, Math.cos(degrees * Math.PI / 360) ];
    q[axis] = Math.sin(degrees * Math.PI / 360);
    return trs(undefined, q);
}

function transform(part)
{
    let m = trs(part.translate, undefined,
                Array.isArray(part.scale) ? part.scale : Array(3).fill(part.scale ?? 1));
    for (const [key, index] of [[ "rotateY", 1 ], [ "rotateX", 0 ], [ "rotateZ", 2 ]])
        if (part[key])
            m = mul(m, axis(index, part[key]));
    return m;
}

function slerp(a, b, t)
{
    let dot = a.reduce((s, x, i) => s + x * b[i], 0);
    if (dot < 0)
    {
        b = b.map(x => -x);
        dot = -dot;
    }
    let out;
    if (dot > .9995)
        out = a.map((x, i) => x + (b[i] - x) * t);
    else
    {
        const theta = Math.acos(Math.min(1, dot)), den = Math.sin(theta);
        out = a.map((x, i) => (x * Math.sin((1 - t) * theta) + b[i] * Math.sin(t * theta)) / den);
    }
    const length = Math.hypot(...out);
    return out.map(x => x / length);
}

function crc32(bytes)
{
    let crc = 0xffffffff;
    for (const b of bytes)
    {
        crc ^= b;
        for (let j = 0; j < 8; j++)
            crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(name, bytes)
{
    const header = Buffer.alloc(8);
    header.writeUInt32BE(bytes.length);
    header.write(name, 4);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(Buffer.concat([ header.subarray(4), bytes ])));
    return Buffer.concat([ header, bytes, crc ]);
}

function encodePng(w, h, rgba)
{
    const header = Buffer.alloc(13);
    header.writeUInt32BE(w);
    header.writeUInt32BE(h, 4);
    header[8] = 8;
    header[9] = 6;
    const rows = Buffer.alloc(h * (1 + w * 4));
    for (let y = 0; y < h; y++)
        rgba.copy(rows, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
    return Buffer.concat([
        Buffer.from([ 137, 80, 78, 71, 13, 10, 26, 10 ]), pngChunk("IHDR", header),
        pngChunk("IDAT", zlib.deflateSync(rows, {level : 9})), pngChunk("IEND", Buffer.alloc(0))
    ]);
}

function decodePng(bytes)
{
    assert.equal(bytes.subarray(1, 4).toString(), "PNG");
    let w, h, channels, compressed = [];
    for (let offset = 8; offset < bytes.length;)
    {
        const size = bytes.readUInt32BE(offset), type = bytes.toString("ascii", offset + 4, offset + 8);
        const data = bytes.subarray(offset + 8, offset + 8 + size);
        if (type === "IHDR")
        {
            w = data.readUInt32BE(0);
            h = data.readUInt32BE(4);
            assert.equal(data[8], 8);
            assert.equal(data[12], 0);
            channels = ({0 : 1, 2 : 3, 4 : 2, 6 : 4})[data[9]];
            assert(channels && w <= 8192 && h <= 8192 && w * h <= 16 * 1024 * 1024, "Unsupported PNG format");
        }
        if (type === "IDAT")
            compressed.push(data);
        offset += size + 12;
    }
    const packed = zlib.inflateSync(Buffer.concat(compressed)), stride = w * channels;
    assert.equal(packed.length, (stride + 1) * h);
    const pixels = Buffer.alloc(w * h * 4), rows = Buffer.alloc(stride * h);
    for (let y = 0; y < h; y++)
    {
        const filter = packed[y * (stride + 1)];
        assert(filter <= 4);
        for (let x = 0; x < stride; x++)
        {
            const a = x >= channels ? rows[y * stride + x - channels] : 0;
            const b = y ? rows[(y - 1) * stride + x] : 0;
            const c = x >= channels && y ? rows[(y - 1) * stride + x - channels] : 0;
            const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
            const prediction = filter === 0           ? 0
                               : filter === 1         ? a
                               : filter === 2         ? b
                               : filter === 3         ? (a + b) >> 1
                               : pa <= pb && pa <= pc ? a
                               : pb <= pc             ? b
                                                      : c;
            rows[y * stride + x] = (packed[y * (stride + 1) + 1 + x] + prediction) & 255;
        }
        for (let x = 0; x < w; x++)
        {
            const src = y * stride + x * channels, dst = (y * w + x) * 4;
            pixels[dst] = rows[src];
            pixels[dst + 1] = channels < 3 ? rows[src] : rows[src + 1];
            pixels[dst + 2] = channels < 3 ? rows[src] : rows[src + 2];
            pixels[dst + 3] = channels === 4 ? rows[src + 3] : channels === 2 ? rows[src + 1] : 255;
        }
    }
    return {w, h, pixels};
}

const loaded = new Map(), usedSources = new Map();

function load(id)
{
    if (loaded.has(id))
        return loaded.get(id);
    assert(/^[a-z0-9_]+$/.test(id));
    const bytes = fs.readFileSync(path.join(source, id + ".glb"));
    assert.equal(bytes.readUInt32LE(0), 0x46546c67);
    assert.equal(bytes.readUInt32LE(4), 2);
    assert.equal(bytes.readUInt32LE(8), bytes.length);
    const jsonLength = bytes.readUInt32LE(12);
    assert.equal(bytes.readUInt32LE(16), 0x4e4f534a);
    const doc = JSON.parse(bytes.toString("utf8", 20, 20 + jsonLength));
    assert.equal(bytes.readUInt32LE(24 + jsonLength), 0x004e4942);
    const binary = bytes.subarray(28 + jsonLength), accessors = new Map(), images = new Map();

    function accessor(index)
    {
        if (accessors.has(index))
            return accessors.get(index);
        const a = doc.accessors[index], view = doc.bufferViews[a.bufferView];
        assert(!a.sparse && (view.buffer ?? 0) === 0);
        const [reader, size, divisor] = ({
            5120 : [ "readInt8", 1, 127 ],
            5121 : [ "readUInt8", 1, 255 ],
            5122 : [ "readInt16LE", 2, 32767 ],
            5123 : [ "readUInt16LE", 2, 65535 ],
            5125 : [ "readUInt32LE", 4, 4294967295 ],
            5126 : [ "readFloatLE", 4, 1 ]
        })[a.componentType];
        const width = ({SCALAR : 1, VEC2 : 2, VEC3 : 3, VEC4 : 4, MAT4 : 16})[a.type];
        assert(width);
        const offset = (view.byteOffset ?? 0) + (a.byteOffset ?? 0), stride = view.byteStride ?? size * width;
        assert(offset + Math.max(0, a.count - 1) * stride + width * size <= binary.length);
        const result = Array.from({length : a.count}, (_, i) => Array.from({length : width}, (_, k) => {
            const n = binary[reader](offset + i * stride + k * size);
            assert(Number.isFinite(n));
            return a.normalized ? Math.max(-1, n / divisor) : n;
        }));
        accessors.set(index, result);
        return result;
    }

    function image(index)
    {
        if (!images.has(index))
        {
            const v = doc.bufferViews[doc.images[index].bufferView];
            images.set(index,
                       decodePng(binary.subarray(v.byteOffset ?? 0, (v.byteOffset ?? 0) + v.byteLength)));
        }
        return images.get(index);
    }

    const materials = (doc.materials ?? []).map(m => {
        const p = m.pbrMetallicRoughness ?? {};
        assert(!p.baseColorTexture?.extensions, "Transformed UVs require an importer update");
        return {
            name : m.name ?? "",
            color : p.baseColorFactor ?? [ 1, 1, 1, 1 ],
            rough : p.roughnessFactor ?? 1,
            texture : p.baseColorTexture ? image(doc.textures[p.baseColorTexture.index].source) : null
        };
    });
    const result = {doc, accessor, materials};
    loaded.set(id, result);
    usedSources.set(id, crypto.createHash("sha256").update(bytes).digest("hex"));
    return result;
}

function sample(asset, clip, phase)
{
    const {doc, accessor} = asset;
    const states = doc.nodes.map(n => ({
                                     t : [...(n.translation ?? [ 0, 0, 0 ]) ],
                                     q : [...(n.rotation ?? [ 0, 0, 0, 1 ]) ],
                                     s : [...(n.scale ?? [ 1, 1, 1 ]) ]
                                 }));
    const animation = (doc.animations ?? []).find(a => a.name === clip);
    if (clip)
        assert(animation, `Missing animation ${clip}`);
    if (animation)
    {
        const duration = Math.max(...animation.samplers.map(s => accessor(s.input).at(-1)[0]));
        const time = Math.min(1, Math.max(0, phase)) * duration;
        for (const channel of animation.channels)
        {
            const sampler = animation.samplers[channel.sampler], times = accessor(sampler.input),
                  values = accessor(sampler.output);
            assert([ "LINEAR", "STEP" ].includes(sampler.interpolation ?? "LINEAR"));
            let at = 0;
            while (at + 1 < times.length && times[at + 1][0] <= time)
                at++;
            const next = Math.min(at + 1, times.length - 1), interval = times[next][0] - times[at][0];
            const t =
                sampler.interpolation === "STEP" || interval <= 0 ? 0 : (time - times[at][0]) / interval;
            const key = ({translation : "t", rotation : "q", scale : "s"})[channel.target.path];
            assert(key);
            states[channel.target.node][key] = key === "q"
                                                   ? slerp(values[at], values[next], t)
                                                   : values[at].map((x, i) => x + (values[next][i] - x) * t);
        }
    }
    const parents = Array(doc.nodes.length).fill(-1), globals = [];
    doc.nodes.forEach((n, i) => (n.children ?? []).forEach(c => {
        assert.equal(parents[c], -1);
        parents[c] = i;
    }));

    function world(i)
    {
        if (!globals[i])
        {
            const s = states[i], local = doc.nodes[i].matrix ?? trs(s.t, s.q, s.s);
            globals[i] = parents[i] >= 0 ? mul(world(parents[i]), local) : local;
        }
        return globals[i];
    }

    doc.nodes.forEach((_, i) => world(i));
    return globals;
}

function subtract(a, b)
{
    return a.map((v, i) => v - b[i]);
}

function add(a, b)
{
    return a.map((v, i) => v + b[i]);
}

function dot(a, b)
{
    return a.reduce((sum, v, i) => sum + v * b[i], 0);
}

function cross(a, b)
{
    return [ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] ];
}

function normalize(v)
{
    const length = Math.hypot(...v);
    assert(length > 1e-8, "Degenerate sword pose basis");
    return v.map(n => n / length);
}

function rotationBetween(from, to)
{
    const a = normalize(from), b = normalize(to), cosine = Math.max(-1, Math.min(1, dot(a, b)));
    if (cosine < -.99999)
    {
        const axis = normalize(cross(a, Math.abs(a[0]) < .8 ? [ 1, 0, 0 ] : [ 0, 1, 0 ]));
        return trs(undefined, [...axis, 0 ]);
    }
    const q = [...cross(a, b), 1 + cosine ], length = Math.hypot(...q);
    return trs(undefined, q.map(n => n / length));
}

// Model-space sword keys replace the supplied punch only for the armed protagonist.
// Shoulder/elbow/wrist matrices drive the actual skinned arm, not a detached sword overlay.
const swordKeys = [
    {time : 0, hand : [ -.34, 1.04, .17 ], blade : [ -.08, .12, .99 ], twist : 0, guard : 0},
    {time : .30, hand : [ -.40, 1.55, .08 ], blade : [ -.55, .78, .30 ], twist : -12, guard : 1},
    {time : .42, hand : [ -.37, 1.52, .17 ], blade : [ -.55, .70, .45 ], twist : -10, guard : 1},
    {time : .56, hand : [ -.16, 1.30, .40 ], blade : [ .15, -.15, .98 ], twist : 9, guard : 1},
    {time : .72, hand : [ .10, 1.12, .18 ], blade : [ .78, -.58, .18 ], twist : 20, guard : .8},
    {time : 1, hand : [ -.34, 1.04, .17 ], blade : [ -.08, .12, .99 ], twist : 0, guard : 0}
];

function swordPose(phase)
{
    const time = Math.max(0, Math.min(1, phase));
    let index = 0;
    while (index + 1 < swordKeys.length - 1 && swordKeys[index + 1].time <= time)
    {
        index++;
    }
    const a = swordKeys[index], b = swordKeys[index + 1];
    const linear = (time - a.time) / (b.time - a.time);
    const t = linear * linear * (3 - 2 * linear);
    const mix = (x, y) => x + (y - x) * t;
    return {
        hand : a.hand.map((v, i) => mix(v, b.hand[i])),
        blade : normalize(a.blade.map((v, i) => mix(v, b.blade[i]))),
        twist : mix(a.twist, b.twist),
        guard : mix(a.guard, b.guard)
    };
}

function poseSwordArm(asset, globals, phase)
{
    const {doc} = asset, pose = swordPose(phase);
    const node = name => {
        const index = doc.nodes.findIndex(n => n.name === name);
        assert(index >= 0, `Missing sword skeleton node ${name}`);
        return index;
    };
    const position = index => globals[index].slice(12, 15);
    const root = position(node("root"));
    const spine = node("spine"), pivot = position(spine);
    const torso = mul(trs(pivot), mul(axis(1, pose.twist), trs(pivot.map(v => -v))));

    function twist(index)
    {
        globals[index] = mul(torso, globals[index]);
        for (const child of doc.nodes[index].children ?? [])
        {
            twist(child);
        }
    }

    twist(spine);

    function solve(side, target, pole, blade)
    {
        const upper = node("upper_arm." + side), lower = node("forearm." + side), hand = node("hand." + side);
        const shoulder = position(upper), elbow = position(lower), wrist = position(hand);
        const lengthA = Math.hypot(...subtract(elbow, shoulder));
        const lengthB = Math.hypot(...subtract(wrist, elbow));
        const vector = subtract(target, shoulder), direction = normalize(vector);
        const distance = Math.max(Math.abs(lengthA - lengthB) + .001,
                                  Math.min(lengthA + lengthB - .001, Math.hypot(...vector)));
        const reach = add(shoulder, direction.map(v => v * distance));
        const along = (lengthA * lengthA - lengthB * lengthB + distance * distance) / (2 * distance);
        const height = Math.sqrt(Math.max(0, lengthA * lengthA - along * along));
        const bend = normalize(subtract(pole, direction.map(v => v * dot(pole, direction))));
        const bent = add(shoulder, add(direction.map(v => v * along), bend.map(v => v * height)));

        function aim(index, origin, before, after)
        {
            const matrix = mul(rotationBetween(before, after), globals[index]);
            matrix.splice(12, 3, ...origin);
            globals[index] = matrix;
        }

        aim(upper, shoulder, subtract(elbow, shoulder), subtract(bent, shoulder));
        aim(lower, bent, subtract(wrist, elbow), subtract(reach, bent));
        aim(hand, reach, subtract(wrist, elbow), subtract(reach, bent));
        if (blade)
        {
            // The source hand's -Z points forward; its +Y follows the fingers.
            const z = blade.map(v => -v);
            const x = normalize(cross(subtract(reach, bent), z)), y = cross(z, x);
            globals[hand] = [...x, 0, ...y, 0, ...z, 0, ...reach, 1 ];
        }
    }

    // The root's vertical walk bob is retained; no new horizontal root motion is introduced.
    solve("R", add(root, pose.hand), [ -1, -.05, -.35 ], pose.blade);
    if (pose.guard > 0)
    {
        const wrist = position(node("hand.L"));
        const guard = add(root, [ .29, 1.23, .20 ]);
        solve("L", wrist.map((v, i) => v + (guard[i] - v) * pose.guard), [ 1, -.1, -.3 ]);
    }
    return globals;
}

function geometry(part, defaultClip, phase)
{
    const asset = load(part.asset), {doc, accessor, materials} = asset;
    const clip = part.clip ?? defaultClip, progress = part.phase ?? phase;
    const sword = part.weapon && clip === "sword_slash";
    const globals = sample(asset, sword ? "idle" : clip, sword ? 0 : progress);
    if (part.weapon)
    {
        poseSwordArm(asset, globals, sword ? progress : 0);
    }
    const outer = transform(part), triangles = [];
    doc.nodes.forEach((node, ni) => {
        if (node.mesh === undefined)
            return;
        const skin = node.skin === undefined ? null : doc.skins[node.skin];
        const joints =
            skin ? skin.joints.map((n, i) => mul(globals[n], accessor(skin.inverseBindMatrices)[i])) : null;
        for (const primitive of doc.meshes[node.mesh].primitives)
        {
            assert.equal(primitive.mode ?? 4, 4);
            const positions = accessor(primitive.attributes.POSITION),
                  uv = accessor(primitive.attributes.TEXCOORD_0);
            const weights = skin ? accessor(primitive.attributes.WEIGHTS_0) : null;
            const indices = skin ? accessor(primitive.attributes.JOINTS_0) : null;
            const vertices = positions.map((p, i) => {
                let v;
                if (skin)
                {
                    v = [ 0, 0, 0 ];
                    for (let j = 0; j < 4; j++)
                    {
                        const transformed = point(joints[indices[i][j]], p);
                        for (let k = 0; k < 3; k++)
                            v[k] += transformed[k] * weights[i][j];
                    }
                }
                else
                    v = point(globals[ni], p);
                v = point(outer, v);
                return {p : v, uv : uv[i]};
            });
            const faces =
                primitive.indices === undefined ? positions.map((_, i) => [i]) : accessor(primitive.indices);
            const material = materials[primitive.material] ?? {color : [ 1, 1, 1, 1 ], rough : 1};
            for (let i = 0; i < faces.length; i += 3)
            {
                const tri = faces.slice(i, i + 3).map(v => vertices[v[0]]);
                assert.equal(tri.length, 3);
                if (part.topOnly)
                {
                    const ys = tri.map(v => v.p[1]);
                    if (Math.max(...ys) - Math.min(...ys) > .0001 || Math.abs(ys[0]) > .001)
                        continue;
                }
                const cloth = part.clothColor && /^(indigo|persimmon|ivory|hemp|red)$/.test(material.name);
                triangles.push({
                    vertices : tri,
                    material : part.color || cloth ? {
                        ...material,
                        color : part.color ?? part.clothColor,
                        texture : part.color ? null : material.texture
                    }
                                                   : material
                });
            }
        }
    });
    if (part.weapon)
    {
        const hand = doc.nodes.findIndex(n => n.name === "hand.R");
        assert(hand >= 0);
        // A resource beam is reshaped into a blade, grip and guard and bound to the animated hand.
        const socket = mul(outer, mul(globals[hand], mul(trs([ 0, .06, 0 ]), axis(1, 180))));
        triangles.weapon = {hand : point(socket, [ 0, 0, 0 ]), tip : point(socket, [ 0, 0, .8 ])};
        for (const piece of [{
                 asset : "module_beam_2m",
                 scale : [ .24, .06, .3545454545 ],
                 rotateY : 90,
                 translate : [ 0, -.0066, .41 ],
                 color : [ .66, .72, .71, 1 ]
             },
                             {
                                 asset : "module_beam_2m",
                                 scale : [ .13, .14, .0818181818 ],
                                 rotateY : 90,
                                 translate : [ 0, -.0154, -.07 ],
                                 color : [ .16, .09, .05, 1 ]
                             },
                             {
                                 asset : "module_beam_2m",
                                 scale : [ .10, .10, .15 ],
                                 translate : [ 0, -.011, .025 ],
                                 color : [ .37, .25, .10, 1 ]
                             }])
        {
            for (const tri of geometry(piece))
            {
                tri.vertices = tri.vertices.map(v => ({...v, p : point(socket, v.p)}));
                triangles.push(tri);
            }
        }
    }
    return triangles;
}

function raster(triangles, yaw, tint, exactBounds)
{
    const rotation = axis(1, yaw);
    const projected =
        triangles
            .map(tri => {
                const p = tri.vertices.map(v => point(rotation, v.p));
                const a = p[1].map((x, i) => x - p[0][i]), b = p[2].map((x, i) => x - p[0][i]);
                let n = [ a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] ];
                const length = Math.hypot(...n);
                if (length < 1e-9)
                    return null;
                n = n.map(x => x / length);
                if (n[0] + n[1] * .5 + n[2] < 0)
                    n = n.map(x => -x);
                const light = .53 + .47 * Math.max(0, n[0] * -.38 + n[1] * .86 + n[2] * .34);
                return {
                    material : tri.material,
                    light,
                    vertices : p.map((v, i) => ({
                                         x : (v[0] - v[2]) * 24,
                                         y : (v[0] + v[2]) * 12 - v[1] * 48,
                                         z : v[0] + v[2] + v[1] * .5,
                                         uv : tri.vertices[i].uv
                                     }))
                };
            })
            .filter(Boolean);
    assert(projected.length);
    const points = projected.flatMap(t => t.vertices);
    const bounds = exactBounds ?? [
        Math.floor(Math.min(...points.map(p => p.x))) - 2, Math.floor(Math.min(...points.map(p => p.y))) - 2,
        Math.ceil(Math.max(...points.map(p => p.x))) + 2, Math.ceil(Math.max(...points.map(p => p.y))) + 2
    ];
    const width = bounds[2] - bounds[0], height = bounds[3] - bounds[1], ss = 2, w = width * ss,
          h = height * ss;
    assert(width > 0 && height > 0 && width <= 1024 && height <= 1024);
    const depth = new Float32Array(w * h).fill(-Infinity), pixels = Buffer.alloc(w * h * 4);
    const gamma = x => Math.round(255 * (x <= .0031308 ? x * 12.92 : 1.055 * Math.pow(x, 1 / 2.4) - .055));
    for (const tri of projected)
    {
        const [a, b, c] =
            tri.vertices.map(v => ({...v, x : (v.x - bounds[0]) * ss, y : (v.y - bounds[1]) * ss}));
        const den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (Math.abs(den) < 1e-7)
            continue;
        const minX = Math.max(0, Math.floor(Math.min(a.x, b.x, c.x))),
              maxX = Math.min(w - 1, Math.ceil(Math.max(a.x, b.x, c.x)));
        const minY = Math.max(0, Math.floor(Math.min(a.y, b.y, c.y))),
              maxY = Math.min(h - 1, Math.ceil(Math.max(a.y, b.y, c.y)));
        const mat = tri.material, tex = mat.texture;
        const color = mat.color.map((v, i) => v * (tint?.[i] ?? 1));
        const solid = color.slice(0, 3).map(v => gamma(Math.min(1, v * tri.light)));
        for (let y = minY; y <= maxY; y++)
            for (let x = minX; x <= maxX; x++)
            {
                const u = ((b.y - c.y) * (x + .5 - c.x) + (c.x - b.x) * (y + .5 - c.y)) / den;
                const v = ((c.y - a.y) * (x + .5 - c.x) + (a.x - c.x) * (y + .5 - c.y)) / den, q = 1 - u - v;
                if (u < -1e-6 || v < -1e-6 || q < -1e-6)
                    continue;
                const z = u * a.z + v * b.z + q * c.z, at = y * w + x;
                if (z < depth[at])
                    continue;
                depth[at] = z;
                const dst = at * 4;
                if (tex)
                {
                    const tx = u * a.uv[0] + v * b.uv[0] + q * c.uv[0],
                          ty = u * a.uv[1] + v * b.uv[1] + q * c.uv[1];
                    const src = (Math.min(tex.h - 1, Math.floor((ty - Math.floor(ty)) * tex.h)) * tex.w +
                                 Math.min(tex.w - 1, Math.floor((tx - Math.floor(tx)) * tex.w))) *
                                4;
                    for (let k = 0; k < 3; k++)
                    {
                        const s = tex.pixels[src + k] / 255,
                              linear = s <= .04045 ? s / 12.92 : Math.pow((s + .055) / 1.055, 2.4);
                        pixels[dst + k] = gamma(Math.min(1, linear * color[k] * tri.light));
                    }
                    pixels[dst + 3] = Math.round(tex.pixels[src + 3] * color[3]);
                }
                else
                {
                    pixels[dst] = solid[0];
                    pixels[dst + 1] = solid[1];
                    pixels[dst + 2] = solid[2];
                    pixels[dst + 3] = Math.round(255 * color[3]);
                }
            }
    }
    const rgba = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++)
        {
            let alpha = 0, rgb = [ 0, 0, 0 ];
            for (let j = 0; j < ss; j++)
                for (let i = 0; i < ss; i++)
                {
                    const at = ((y * ss + j) * w + x * ss + i) * 4, a = pixels[at + 3];
                    alpha += a;
                    for (let k = 0; k < 3; k++)
                        rgb[k] += pixels[at + k] * a;
                }
            const dst = (y * width + x) * 4;
            for (let k = 0; k < 3; k++)
                rgba[dst + k] = alpha ? Math.round(rgb[k] / alpha) : 0;
            rgba[dst + 3] = Math.round(alpha / (ss * ss));
        }
    return {width, height, x : bounds[0], y : bounds[1], rgba};
}

function recipes()
{
    const p = (asset, translate, scale, extras = {}) => ({asset, translate, scale, ...extras});
    const house =
        roof => [p("module_foundation_2m"), p("module_floor_2m", [ 0, .25, 0 ]),
                 p("module_wall_plaster_2m", [ 0, .4, -1 ]),
                 p("module_wall_paper_lattice_2m", [ -1, .4, 0 ], undefined, {rotateY : 90}),
                 p("module_wall_plaster_2m", [ 1, .4, 0 ], undefined, {rotateY : 90}),
                 ...[-1, 1].flatMap(x => [-1, 1].map(z => p("module_post_2_4m", [ x, .4, z ]))),
                 p("module_door_hinged", [ -.45, .4, 1 ], undefined, {clip : "door_open", phase : 0}),
                 p(roof, [ 0, 2.8, 0 ]), p("module_gable_2m", [ 0, 2.8, -1 ]),
                 p("module_onggi_jar", [ 1.3, 0, .6 ], .6)];
    const raft = [
        p("module_floor_2m", [ 0, -.14, 0 ], [ .7, 1, 1.5 ]),
        p("module_beam_2m", [ -.8, 0, 0 ], [ .09, 1, 1.6 ], {rotateY : 90}),
        p("module_beam_2m", [ .8, 0, 0 ], [ .09, 1, 1.6 ], {rotateY : 90}),
        p("module_post_2_4m", [ 0, 0, .4 ], .7),
        p("module_reed_mat", [ .35, 1, .4 ], [ .5, 1, .5 ], {rotateX : 90})
    ];
    const gate = open => [p("module_post_2_4m", [ -.7, 0, 0 ]), p("module_post_2_4m", [ .7, 0, 0 ]),
                          p("module_beam_2m", [ 0, 2.4, 0 ], [ .8, 1, 1 ]),
                          p("prop_door_hinged", [ -.7, 0, 0 ], [ 1.5, 1, 1 ],
                            {clip : "door_open", phase : open ? 1 : 0})];
    return {
        prop0 : house("module_roof_giwa_2m"),
        prop1 : house("module_roof_thatch_2m"),
        prop2 : [
            p("module_soban_table", [ 0, 0, 0 ], 1.5), p("module_stool", [ -.7, 0, .5 ]),
            p("module_medicine_drawers", [ 0, .6, 0 ], .45)
        ],
        prop3 : [
            p("module_post_2_4m", undefined, .65),
            p("module_wall_paper_lattice_2m", [ 0, .8, 0 ], [ .2, .2, .3 ])
        ],
        prop4 : [
            ...[-1, 1].map(x => p("module_foundation_2m", [ x * .6, 0, 0 ], [ .15, 1, .7 ])),
            ...[-1, 1].map(z => p("module_foundation_2m", [ 0, 0, z * .6 ], [ .6, 1, .15 ])),
            p("module_wood_bucket", [ 0, .1, 0 ], .6)
        ],
        prop5 : [ p("module_post_2_4m", undefined, .65), p("module_paper_lantern", [ 0, 1.35, 0 ], 1.2) ],
        prop6 : [ p("module_fence_2m", undefined, .8) ],
        prop7 : [ p("nature_broadleaf", undefined, .8) ],
        prop8 : [ p("nature_reed_clump"), p("nature_grass_clump", [ .3, 0, 0 ], .7) ],
        prop9 : [
            p("nature_rock_00", undefined, .7), p("nature_rock_01", [ .5, 0, .15 ], .4),
            p("nature_rock_02", [ -.3, 0, 0 ], .3)
        ],
        prop10 : raft,
        prop11 : [
            p("module_soban_table", undefined, 1.3), p("module_reed_mat", [ 0, .65, 0 ], .7),
            p("nature_grass_clump", [ -.2, .7, 0 ], .3)
        ],
        prop12 : [
            p("module_post_2_4m", [ -.65, 0, 0 ], .65), p("module_post_2_4m", [ .65, 0, 0 ], .65),
            p("module_beam_2m", [ 0, 1.45, 0 ], .7),
            p("module_reed_mat", [ 0, 1.05, 0 ], [ .6, 1, .6 ], {rotateX : 90})
        ],
        prop13 : [
            p("module_floor_2m", undefined, .6), p("module_rice_sack", [ -.3, .15, 0 ], .7),
            p("module_storage_chest", [ .3, .15, 0 ], .6)
        ],
        prop14 : [
            p("module_post_2_4m", undefined, .7),
            p("module_reed_mat", [ .25, 1.1, 0 ], [ .5, 1, .5 ], {rotateX : 90})
        ],
        prop15 : [ p("module_floor_2m", [ 0, -.14, 0 ]) ],
        prop16 : [ p("nature_pine", undefined, .7) ],
        prop17 : [ p("nature_broadleaf", undefined, [ .55, .9, .55 ]) ],
        prop18 : [ p("animal_ox", undefined, .8, {clip : "idle", phase : 0}) ],
        prop19 : [ p("scene_medicine_shop", undefined, .8) ],
        drain : [
            p("module_foundation_2m", undefined, .8), p("module_fence_2m", [ 0, .2, 0 ], .7),
            p("terrain_slope_2m", [ 0, -.2, -.6 ], .5)
        ],
        sluice : gate(false),
        sluice_open : gate(true),
        notice : [
            p("module_wall_paper_lattice_2m", undefined, [ .55, .6, 1 ]),
            p("module_post_2_4m", [ -.55, 0, 0 ], .6), p("module_post_2_4m", [ .55, 0, 0 ], .6)
        ],
        wreck : raft.map(part => ({...part, rotateY : (part.rotateY ?? 0) + 15})),
        ledger : [
            p("module_reed_mat", undefined, [ .25, 1, .25 ]),
            p("module_beam_2m", [ 0, .03, 0 ], [ .01, .3, .28 ])
        ],
        key : [
            p("module_beam_2m", [ 0, .03, 0 ], [ .12, .2, .12 ]),
            p("module_beam_2m", [ .12, .04, .05 ], [ .015, .2, .35 ], {rotateY : 90})
        ],
        corpse : [ p("human_protagonist", [ .65, .18, 0 ], .7, {clip : "idle", phase : 0, rotateZ : 90}) ],
        bandit_sign :
            [ p("module_post_2_4m", undefined, .7), p("module_firewood", [ 0, .6, 0 ], [ .8, 2, .8 ]) ],
        loot0 : [ p("module_rice_sack", undefined, .22) ],
        loot1 : [ p("module_wood_bucket", undefined, .35) ],
        loot2 : [ p("module_reed_mat", undefined, .35) ],
        tile0 : [ p("terrain_grass_2m", undefined, 1, {topOnly : true}) ],
        tile1 : [ p("terrain_grass_2m", undefined, 1, {topOnly : true}) ],
        tile2 : [ p("terrain_grass_2m", undefined, 1, {topOnly : true}) ],
        tile3 : [ p("terrain_stone_2m", undefined, 1, {topOnly : true}) ],
        tile4 : [ p("terrain_earth_2m", undefined, 1, {topOnly : true}) ],
        tile5 : [ p("terrain_earth_2m", undefined, 1, {topOnly : true}) ]
    };
}

function compile()
{
    const images = [], aliases = new Map(), baked = new Map();

    function emit(key, parts, clip, phase, direction = -1, material = 22, tint)
    {
        const canonical = JSON.stringify([ parts, clip, phase, direction, material, tint ]);
        let image = baked.get(canonical);
        if (!image)
        {
            const pieces = parts.map(p => geometry(p, clip, phase));
            const triangles = pieces.flat();
            const weapon = pieces.find(p => p.weapon)?.weapon;
            const yaw = direction < 0
                            ? 0
                            : (parts.some(p => p.asset.startsWith("human_")) ? humanDirectionYaws
                                                                             : directionYaws)[direction];
            image = {
                ...raster(triangles, yaw, tint, key.startsWith("tile") ? [ -48, -24, 48, 24 ] : null),
                material,
                triangleCount : triangles.length
            };
            image.weapon = weapon ? [ weapon.hand, weapon.tip ].flatMap(p => {
                const v = point(axis(1, yaw), p);
                return [ (v[0] - v[2]) * 24, (v[0] + v[2]) * 12 - v[1] * 48 ];
            })
                                  : [ 0, 0, 0, 0 ];
            baked.set(canonical, image);
            images.push(image);
        }
        assert(!aliases.has(key));
        aliases.set(key, image);
    }

    const humans = [
        "human_mira_engineer", "human_doyun_clerk", "human_serat_inspector", "human_ian_broker",
        "human_haram_apprentice", "human_protagonist", "human_porter", "human_herbalist"
    ];
    const looks = [ 0, 1, 2, 3, 4, 5, 6, 6, 5, 6, 7, 1, 7, 6, 0, 2, 6, 3 ];
    for (let h = 0; h < humans.length; h++)
    {
        for (let d = 0; d < directionCount; d++)
            for (let frame = -1; frame < walkFrames; frame++)
            {
                const part = {asset : humans[h]};
                const key = `${humans[h]}/${frame}/${d}`;
                emit(key, [ part ], frame < 0 ? "idle" : "walk", frame < 0 ? 0 : frame / walkFrames, d);
                for (let look = 0; look < 5; look++)
                    if (looks[look] === h)
                        aliases.set(`citizen${look}_${frame < 0 ? "idle" : frame}@${d}`, aliases.get(key));
                if (h === 5)
                    emit(`hero${frame < 0 ? "_idle" : frame}@${d}`, [ {asset : humans[h], weapon : true} ],
                         frame < 0 ? "idle" : "walk", frame < 0 ? 0 : frame / walkFrames, d);
            }
        if (h === 5)
            for (let d = 0; d < directionCount; d++)
                for (let frame = 0; frame < 12; frame++)
                {
                    const key = `hero_attack${frame}@${d}`;
                    emit(key, [ {asset : humans[h], weapon : true} ], "sword_slash", frame / 11, d);
                    for (const prefix of ["hero_attack_up", "hero_attack_down"])
                        aliases.set(`${prefix}${frame}@${d}`, aliases.get(key));
                }
        process.stdout.write(`Imported ${humans[h]}\n`);
    }
    // Reuse the supplied bodies and equipment, but keep thirteen ordinary roles visually distinct.
    // Only clothing materials change: skin, hair, tools and story-character materials stay intact.
    const clothes = [
        [ .09, .19, .14, 1 ], [ .34, .24, .13, 1 ], [ .065, .07, .09, 1 ], [ .18, .075, .065, 1 ],
        [ .35, .23, .10, 1 ], [ .14, .27, .23, 1 ], [ .36, .34, .26, 1 ], [ .33, .17, .12, 1 ],
        [ .21, .19, .15, 1 ], [ .16, .27, .34, 1 ], [ .29, .09, .07, 1 ], [ .17, .17, .24, 1 ],
        [ .29, .13, .25, 1 ]
    ];
    for (let look = 5; look < looks.length; look++)
        for (let d = 0; d < directionCount; d++)
            for (let frame = -1; frame < walkFrames; frame++)
                emit(`citizen${look}_${frame < 0 ? "idle" : frame}@${d}`,
                     [ {asset : humans[looks[look]], clothColor : clothes[look - 5]} ],
                     frame < 0 ? "idle" : "walk", frame < 0 ? 0 : frame / walkFrames, d);
    for (const [kind, id] of ["animal_deer", "animal_wolf", "animal_tiger"].entries())
    {
        for (let d = 0; d < directionCount; d++)
            for (let frame = -1; frame < walkFrames; frame++)
                emit(`beast${kind}_${frame < 0 ? "idle" : frame}@${d}`, [ {asset : id} ],
                     frame < 0 ? "idle" : "walk", frame < 0 ? 0 : frame / walkFrames, d);
        for (let d = 0; d < directionCount; d++)
        {
            for (let frame = 0; frame < walkFrames; frame++)
            {
                emit(`beast${kind}_run${frame}@${d}`, [ {asset : id} ], "run", frame / walkFrames, d);
            }
            for (let frame = 0; frame < beastAttackFrames; frame++)
            {
                emit(`beast${kind}_attack${frame}@${d}`, [ {asset : id} ], "attack",
                     frame / (beastAttackFrames - 1), d);
            }
        }
        process.stdout.write(`Imported ${id}\n`);
    }
    for (const [key, parts] of Object.entries(recipes()))
    {
        const material = key === "tile5"                                          ? 25
                         : key === "tile4"                                        ? 24
                         : key.startsWith("tile")                                 ? 23
                         : [ "prop7", "prop8", "prop16", "prop17" ].includes(key) ? 26
                         : [ "prop12", "prop14" ].includes(key)                   ? 27
                                                                                  : 22;
        emit(key, parts, undefined, 0, -1, material);
    }
    // Height-ordered shelves avoid wasting another power-of-two texture on a short final row.
    images.sort((a, b) => b.height - a.height || b.width - a.width);
    const atlasWidth = 4096;
    let x = 2, y = 2, row = 0;
    for (const img of images)
    {
        if (x + img.width + 2 > atlasWidth)
        {
            x = 2;
            y += row + 4;
            row = 0;
        }
        img.atlasX = x;
        img.atlasY = y;
        x += img.width + 4;
        row = Math.max(row, img.height);
    }
    const atlasHeight = 2 ** Math.ceil(Math.log2(y + row + 2));
    assert(atlasHeight <= 8192);
    const pixels = Buffer.alloc(atlasWidth * atlasHeight * 4);
    for (const img of images)
        for (let j = 0; j < img.height; j++)
            img.rgba.copy(pixels, ((img.atlasY + j) * atlasWidth + img.atlasX) * 4, j * img.width * 4,
                          (j + 1) * img.width * 4);
    // Fill RGB of transparent edge pixels to prevent black bilinear fringes (alpha remains zero).
    for (let pass = 0; pass < 2; pass++)
        for (const img of images)
        {
            const old = Buffer.from(
                pixels.subarray(img.atlasY * atlasWidth * 4, (img.atlasY + img.height) * atlasWidth * 4));
            for (let j = 0; j < img.height; j++)
                for (let i = 0; i < img.width; i++)
                {
                    const at = ((img.atlasY + j) * atlasWidth + img.atlasX + i) * 4;
                    if (pixels[at + 3])
                        continue;
                    for (const [dx, dy] of [[ 1, 0 ], [ -1, 0 ], [ 0, 1 ], [ 0, -1 ]])
                    {
                        if (i + dx < 0 || i + dx >= img.width || j + dy < 0 || j + dy >= img.height)
                            continue;
                        const src = ((j + dy) * atlasWidth + img.atlasX + i + dx) * 4;
                        if (old[src + 3])
                        {
                            for (let k = 0; k < 3; k++)
                                pixels[at + k] = old[src + k];
                            break;
                        }
                    }
                }
        }
    const records = [...aliases ].sort(([ a ], [ b ]) => a.localeCompare(b, "en"));
    const png = encodePng(atlasWidth, atlasHeight, pixels);
    const chunks = [], header = Buffer.alloc(28);
    header.write("HDSPRITE");
    header.writeUInt32LE(revision, 8);
    header.writeUInt32LE(atlasWidth, 12);
    header.writeUInt32LE(atlasHeight, 16);
    header.writeUInt32LE(records.length, 20);
    chunks.push(header);
    header.writeUInt32LE(crc32(png), 24);
    for (const [key, img] of records)
    {
        const name = Buffer.from(key), record = Buffer.alloc(4 + name.length + 52);
        record.writeUInt32LE(name.length);
        name.copy(record, 4);
        let offset = 4 + name.length;
        for (const value of [img.x, img.y, img.width, img.height, img.atlasX / atlasWidth,
                             img.atlasY / atlasHeight, img.width / atlasWidth, img.height / atlasHeight])
        {
            record.writeFloatLE(value, offset);
            offset += 4;
        }
        record.writeUInt32LE(img.material, offset);
        offset += 4;
        for (const value of img.weapon)
        {
            record.writeFloatLE(value, offset);
            offset += 4;
        }
        chunks.push(record);
    }
    const binary = Buffer.concat(chunks);
    const report = {
        event : "resource_sprite_import",
        revision,
        source_assets : [...usedSources ].map(([ id, sha256 ]) => ({id, sha256})),
        compiler_sha256 : crypto.createHash("sha256")
                              .update(fs.readFileSync(__filename, "utf8").replace(/\r\n/g, "\n"))
                              .digest("hex"),
        atlas_sha256 : crypto.createHash("sha256").update(png).digest("hex"),
        bindings_sha256 : crypto.createHash("sha256").update(binary).digest("hex"),
        atlas_width : atlasWidth,
        atlas_height : atlasHeight,
        sprites : images.length,
        aliases : records.length,
        source_triangles_sampled : images.reduce((s, i) => s + i.triangleCount, 0),
        animation_sampling :
            "eight screen directions, twelve walk/run samples, twelve sword samples, sixteen beast attack samples, in-place root vertical only",
        direction_yaws : directionYaws,
        human_direction_yaws : humanDirectionYaws,
        human_width_scale : humanWidthScale,
        direction_count : directionCount,
        walk_frames : walkFrames,
        beast_attack_frames : beastAttackFrames,
        projection : "screen_x=(glb_x-glb_z)*24; screen_y=(glb_x+glb_z)*12-glb_y*48",
        weapon :
            "resource beam blade/grip/guard at hand.R palm; corrected -Z grip, authored sword_slash with two-bone arm IK",
        sword_animation_revision : 1,
        game_build_performed : false,
        game_execution_performed : false,
        bindings : records.map(([ key, i ]) => ({
                                   key,
                                   x : i.x,
                                   y : i.y,
                                   width : i.width,
                                   height : i.height,
                                   material : i.material,
                                   weapon : i.weapon
                               }))
    };
    fs.mkdirSync(destination, {recursive : true});
    fs.writeFileSync(path.join(destination, "world_atlas.png"), png);
    fs.writeFileSync(path.join(destination, "world_sprites.bin"), binary);
    fs.writeFileSync(path.join(destination, "import_report.json"), JSON.stringify(report, null, 2) + "\n");
    console.log(JSON.stringify({
        event : report.event,
        source_assets : usedSources.size,
        sprites : images.length,
        aliases : records.length,
        atlas : [ atlasWidth, atlasHeight ],
        png_bytes : png.length
    }));
}

function selfTest()
{
    const p = point(trs([ 3, 4, 5 ]), [ 1, 2, 3 ]);
    assert.deepEqual(p, [ 4, 6, 8 ]);
    const r = point(axis(1, 90), [ 0, 0, 1 ]);
    assert(Math.abs(r[0] - 1) < 1e-7 && Math.abs(r[2]) < 1e-7);
    assert.deepEqual(slerp([ 0, 0, 0, 1 ], [ 0, 0, 0, 1 ], .5), [ 0, 0, 0, 1 ]);
    const pixels = Buffer.from([ 255, 127, 0, 255, 1, 2, 3, 0 ]);
    assert.deepEqual(decodePng(encodePng(2, 1, pixels)).pixels, pixels);
    const a = load("human_protagonist"), rest = geometry({asset : "human_protagonist"}, "walk", 0),
          walk = geometry({asset : "human_protagonist"}, "walk", .25);
    assert(rest.length > 0 && rest.length === walk.length);
    assert(rest.some((t, i) => t.vertices.some(
                         (v, j) => v.p.some((p, k) => Math.abs(p - walk[i].vertices[j].p[k]) > .001))));
    assert(sample(a, "idle", 0).every(m => m.every(Number.isFinite)));
    console.log(JSON.stringify({
        event : "resource_importer_self_test",
        passed : true,
        game_build_performed : false,
        game_execution_performed : false
    }));
}

if (require.main === module)
{
    if (process.argv.includes("--self-test"))
        selfTest();
    else
        compile();
}
module.exports = {
    load,
    geometry,
    sample,
    raster,
    decodePng,
    encodePng,
    recipes,
    swordPose,
    poseSwordArm,
    directionYaws,
    humanDirectionYaws,
    screenVectors
};
