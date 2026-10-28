"use strict";
// Skeleton/geometry and presentation arithmetic only; no game or shader execution.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {
    load,
    sample,
    geometry,
    swordPose,
    poseSwordArm,
    raster,
    encodePng,
    humanDirectionYaws : directionYaws
} = require("./Compile-ResourceSprites.cjs");
const asset = load("human_protagonist");
const index = name => asset.doc.nodes.findIndex(n => n.name === name);
const position = (globals, name) => globals[index(name)].slice(12, 15);
const distance = (a, b) => Math.hypot(...a.map((n, i) => n - b[i]));
const close = (a, b, tolerance = .00001) => assert(distance(a, b) < tolerance);
const rest = sample(asset, "idle", 0);
const roots = position(rest, "root");
const armLengths = Object.fromEntries(
    [ "L", "R" ].map(side => [side, [
                         distance(position(rest, "upper_arm." + side), position(rest, "forearm." + side)),
                         distance(position(rest, "forearm." + side), position(rest, "hand." + side))
                     ]]));
let maximumTipStep = 0;
let previousTip;
for (let tick = 0; tick <= 100; tick++)
{
    const phase = tick / 100, globals = poseSwordArm(asset, sample(asset, "idle", 0), phase);
    assert(globals.every(m => m.every(Number.isFinite)));
    close(position(globals, "root"), roots);
    for (const side of ["L", "R"])
    {
        const lengths = [
            distance(position(globals, "upper_arm." + side), position(globals, "forearm." + side)),
            distance(position(globals, "forearm." + side), position(globals, "hand." + side))
        ];
        close(lengths, armLengths[side]);
    }
    close(position(globals, "hand.R"), swordPose(phase).hand, .002);
    const mesh = geometry({asset : "human_protagonist", weapon : true}, "sword_slash", phase);
    const {hand, tip} = mesh.weapon;
    const direction = tip.map((n, i) => (n - hand[i]) / .8);
    close(direction, swordPose(phase).blade);
    assert(Math.abs(distance(hand, tip) - .8) < .00001);
    // The recorded grip is the actual palm socket, not the wrist origin.
    const wrist = globals[index("hand.R")];
    close(hand, [ 0, 1, 2 ].map(i => wrist[12 + i] + wrist[4 + i] * .06));
    const blade = mesh.filter(t => t.material.color[0] === .66);
    assert(blade.length > 0);
    // Actual beam geometry must end at the same tip used by the runtime trail.
    const projections = blade.flatMap(
        t => t.vertices.map(v => v.p.reduce((sum, n, i) => sum + (n - hand[i]) * direction[i], 0)));
    assert(Math.abs(Math.max(...projections) - .8) < .00001);
    assert(Math.min(...projections) > .019);
    if (previousTip)
    {
        maximumTipStep = Math.max(maximumTipStep, distance(previousTip, tip));
    }
    previousTip = tip;
}
assert(maximumTipStep < .20, "Sword teleport between adjacent poses");
const ready = geometry({asset : "human_protagonist", weapon : true}, "idle", 0);
for (const phase of [0, 1])
{
    const endpoint = geometry({asset : "human_protagonist", weapon : true}, "sword_slash", phase);
    assert.equal(endpoint.length, ready.length);
    for (let i = 0; i < ready.length; i++)
    {
        for (let v = 0; v < 3; v++)
        {
            close(endpoint[i].vertices[v].p, ready[i].vertices[v].p);
        }
    }
}
const presentation =
    fs.readFileSync(path.join(__dirname, "../BaseProject_SE/SimpleGame/CharacterPresentation.h"), "utf8");
const scale = Number(presentation.match(/HumanWidthScale = ([\d.]+)f/)[1]);
assert.equal(scale, 1.4);
for (const anchor of [0, 312.5, 640])
{
    for (const local of [-30, 0, 19.5])
    {
        const stretched = anchor + local * scale;
        assert(Math.abs((stretched - anchor) / scale - local) < .00001);
    }
}
const renderer =
    fs.readFileSync(path.join(__dirname, "../BaseProject_SE/SimpleGame/RendererModels.cpp"), "utf8");
assert(renderer.includes("(r[0] + r[2] * .5f) * widthScale"));
assert(renderer.includes("r[2] * widthScale"));
for (const vertex of ["a[0]", "a[2]", "b[2]", "b[0]"])
{
    assert(renderer.includes(`x + ${vertex} * widthScale`));
}
assert(swordPose(.56).blade[2] > .95, "Impact must cut forward, not behind the player");
assert(swordPose(.30).blade[1] > .7 && swordPose(.72).blade[1] < -.5);
if (process.argv.includes("--preview"))
{
    const width = 12 * 128, height = directionYaws.length * 160;
    const pixels = Buffer.alloc(width * height * 4);
    for (let i = 0; i < pixels.length; i += 4)
    {
        pixels.set([ 206, 209, 197, 255 ], i);
    }
    for (let direction = 0; direction < directionYaws.length; direction++)
    {
        for (let frame = 0; frame < 12; frame++)
        {
            const mesh = geometry({asset : "human_protagonist", weapon : true}, "sword_slash", frame / 11);
            const image = raster(mesh, directionYaws[direction]);
            for (let y = 0; y < 160; y++)
            {
                for (let x = 0; x < 128; x++)
                {
                    const sx = Math.floor((x - 64) / scale - image.x), sy = y - 146 - image.y;
                    if (sx < 0 || sx >= image.width || sy < 0 || sy >= image.height)
                    {
                        continue;
                    }
                    const src = (sy * image.width + sx) * 4;
                    const dst = ((direction * 160 + y) * width + frame * 128 + x) * 4;
                    const alpha = image.rgba[src + 3] / 255;
                    for (let c = 0; c < 3; c++)
                    {
                        pixels[dst + c] =
                            Math.round(image.rgba[src + c] * alpha + pixels[dst + c] * (1 - alpha));
                    }
                }
            }
        }
    }
    const preview = path.resolve(__dirname, "../resource/haedong_runtime/sword_pose_preview.png");
    fs.writeFileSync(preview, encodePng(width, height, pixels));
    console.log(
        JSON.stringify({event : "sword_asset_preview", path : preview, game_execution_performed : false}));
}
console.log(JSON.stringify({
    event : "sword_animation_contracts",
    passed : true,
    poses_checked : 101,
    human_width_scale : scale,
    maximum_tip_step_m : maximumTipStep,
    arm_lengths_preserved : true,
    ready_endpoints_match : true,
    weapon_and_trail_aligned : true,
    game_build_performed : false,
    game_execution_performed : false
}));
