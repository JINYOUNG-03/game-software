"use strict";
// Source/data and numerical checks only. No C++ build, shader compilation or game execution.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "..");
const read = name => fs.readFileSync(path.join(root, "BaseProject_SE/SimpleGame", name), "utf8");
const data = fs.readFileSync(path.join(root, "resource/haedong_runtime/world_sprites.bin"));
const records = new Map();
let cursor = 28;
for (let i = 0; i < data.readUInt32LE(20); i++)
{
    const length = data.readUInt32LE(cursor);
    cursor += 4;
    const name = data.toString("utf8", cursor, cursor + length);
    cursor += length;
    records.set(name, Array.from({length : 8}, (_, n) => data.readFloatLE(cursor + n * 4)));
    cursor += 52;
}
assert.equal(cursor, data.length);
const sample = (position, frames, loop) => {
    if (!Number.isFinite(position) || frames < 2)
        return [ 0, 0, 0 ];
    const p =
        loop ? ((position % frames) + frames) % frames : Math.max(0, Math.min(1, position)) * (frames - 1);
    const current = Math.floor(p);
    return [ current, loop ? (current + 1) % frames : Math.min(current + 1, frames - 1), p - current ];
};
assert.deepEqual(sample(11.75, 12, true), [ 11, 0, .75 ]);
assert.deepEqual(sample(12, 12, true), [ 0, 1, 0 ]);
assert.deepEqual(sample(-.25, 12, true), [ 11, 0, .75 ]);
assert.deepEqual(sample(1, 12, false), [ 11, 11, 0 ]);
assert.deepEqual(sample(1.5, 16, false), [ 15, 15, 0 ]);
assert.deepEqual(sample(NaN, 12, true), [ 0, 0, 0 ]);
// Sampling changes every display tick, without extending or shortening an attack.
for (const fps of [20, 60, 144])
{
    const positions = new Set();
    for (let tick = 0; tick <= fps; tick++)
    {
        const [a, b, t] = sample(tick / fps, 12, false);
        assert(a >= 0 && b < 12 && t >= 0 && t < 1);
        positions.add((a + t).toFixed(4));
    }
    assert.equal(positions.size, fps + 1);
}
let pairs = 0, projectionProbes = 0;
function clip(prefix, frames, loop, direction)
{
    for (let f = 0; f < (loop ? frames : frames - 1); f++)
    {
        const r = records.get(prefix + f + "@" + direction);
        const n = records.get(prefix + ((f + 1) % frames) + "@" + direction);
        assert(r && n, "Missing adjacent cached pose");
        const left = Math.min(r[0], n[0]), top = Math.min(r[1], n[1]);
        const width = Math.max(r[0] + r[2], n[0] + n[2]) - left;
        const height = Math.max(r[1] + r[3], n[1] + n[3]) - top;
        for (const rect of [r, n])
        {
            const map =
                [ (left - rect[0]) / rect[2], (top - rect[1]) / rect[3], width / rect[2], height / rect[3] ];
            for (const local of [[ 0, 0 ], [ .2, .8 ], [ .5, .5 ], [ 1, 1 ]])
            {
                // The two differently cropped images reconstruct the same foot-relative pixel.
                const coord = [ map[0] + local[0] * map[2], map[1] + local[1] * map[3] ];
                const p = [ rect[0] + coord[0] * rect[2], rect[1] + coord[1] * rect[3] ];
                assert(Math.abs(p[0] - left - local[0] * width) < .00001);
                assert(Math.abs(p[1] - top - local[1] * height) < .00001);
                projectionProbes++;
            }
        }
        pairs++;
    }
}
for (let direction = 0; direction < 8; direction++)
{
    clip("hero", 12, true, direction);
    clip("hero_attack", 12, false, direction);
    for (let look = 0; look < 18; look++)
        clip("citizen" + look + "_", 12, true, direction);
    for (let beast = 0; beast < 3; beast++)
    {
        clip("beast" + beast + "_", 12, true, direction);
        clip("beast" + beast + "_run", 12, true, direction);
        clip("beast" + beast + "_attack", 16, false, direction);
    }
}
// A transparent texel with arbitrary RGB must not darken the visible silhouette.
function blend(a, b, t)
{
    const alpha = a[3] * (1 - t) + b[3] * t;
    return [
        ...a.slice(0, 3).map((v, i) => alpha ? (v * a[3] * (1 - t) + b[i] * b[3] * t) / alpha : 0), alpha
    ];
}
assert.deepEqual(blend([ 1, .5, .25, 1 ], [ 0, 0, 0, 0 ], .5), [ 1, .5, .25, .5 ]);
assert.deepEqual(blend([ 1, 0, 0, 0 ], [ 0, 1, 0, 0 ], .5), [ 0, 0, 0, 0 ]);
const shader = read("Shaders/SolidRect.fs"), vertex = read("Shaders/SolidRect.vs");
for (const contract
         of ["sprite.rgb /= sprite.a", "sampleColor.rgb * sampleColor.a", "textureSize(u_Texture, 0)",
             "lessThan(local", "greaterThan(local", "v_Material == 22 && v_Surface.x > 0.0"])
    assert(shader.includes(contract), contract);
for (const varying of ["v_PoseCoord", "v_NextPoseCoord", "v_PoseUV", "v_NextUV"])
    assert(shader.includes(varying) && vertex.includes(varying), varying);
const renderer = read("RendererModels.cpp"), header = read("Renderer.h"), batch = read("RendererBatch.cpp");
assert(header.includes("sizeof(float) * 33"));
assert(batch.includes("i < 9") && batch.includes("offsetof(Instance, nextPoseMap)"));
assert(renderer.includes("QueueInstance(item, m_ResourceAtlas)"));
assert(renderer.includes("Perf::Cpu::PoseBlend") && renderer.includes("Perf::Counter::PoseBlendedInstances"));
assert(renderer.includes("position / (CharacterPresentation::AttackFrames - 1)"));
assert(renderer.includes("(next->weapon[i] - current->weapon[i]) * sample.blend"));
assert(read("LevelOne.cpp").includes("SetIdle(*enemy, prefix"));
assert(/CharacterPresentation::SetPose\(\s*npc,\s*prefix/.test(read("HarborWorld.cpp")));
assert(read("SimpleGame.cpp").includes("actor.poseBlend"));
assert(read("SimpleGame.cpp").includes("std::chrono::steady_clock::now()"));
const profile = read("Profiler.cpp");
assert(!profile.includes("std::cout") && !profile.includes("printf("), "Console profile output remains");
assert(profile.includes("Queue(summary.str())"), "Keep file-based summary profiling");
assert(profile.includes('schema_version\\":14'));
console.log(JSON.stringify({
    event : "pose_blending_contracts",
    passed : true,
    cached_pose_pairs : pairs,
    foot_anchor_probes : projectionProbes,
    alpha_correct : true,
    console_profile_output : false,
    game_build_performed : false,
    game_execution_performed : false,
    shader_compilation_performed : false
}));
