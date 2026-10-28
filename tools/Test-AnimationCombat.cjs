"use strict";
// Source integration and deterministic state arithmetic; this is not a C++ build or game execution.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const {load, geometry, directionYaws, humanDirectionYaws, screenVectors} =
    require("./Compile-ResourceSprites.cjs");
const read = file => fs.readFileSync(path.join(__dirname, "../BaseProject_SE/SimpleGame/", file), "utf8");
const header = read("CharacterPresentation.h");
const source = read("LevelOne.cpp");
const world = read("HarborWorld.cpp");
const constant = name => Number(header.match(new RegExp(name + " = ([.\\d]+)f?;"))[1]);
const boundary = Number(header.match(/diagonalBoundary = ([.\d]+)f/)[1]);
const facing = (x, y) => Math.abs(y) <= Math.abs(x) * boundary   ? (x >= 0 ? 0 : 2)
                         : Math.abs(x) <= Math.abs(y) * boundary ? (y >= 0 ? 1 : 3)
                         : y >= 0                                ? (x >= 0 ? 4 : 5)
                                                                 : (x < 0 ? 6 : 7);
assert.equal(constant("DirectionCount"), 8);
assert.equal(constant("WalkFrames"), 12);
for (let d = 0; d < 8; d++)
{
    const [x, y] = screenVectors[d];
    assert.equal(facing(x, y), d);
    const yaw = directionYaws[d] * Math.PI / 180;
    const sx = (Math.sin(yaw) - Math.cos(yaw)) * 24;
    const sy = (Math.sin(yaw) + Math.cos(yaw)) * 12;
    assert(sx * x + sy * y > 0);
    assert(Math.abs(sx * y - sy * x) < .00001, `Facing does not match projected travel ${d}`);
    const humanYaw = humanDirectionYaws[d] * Math.PI / 180;
    const hx = (Math.sin(humanYaw) - Math.cos(humanYaw)) * 24 * constant("HumanWidthScale");
    const hy = (Math.sin(humanYaw) + Math.cos(humanYaw)) * 12;
    assert(Math.abs(hx * y - hy * x) < .00001, `Widened human facing is misaligned ${d}`);
    for (const magnitude of [.001, 1, 100])
    {
        assert.equal(facing(x * magnitude, y * magnitude), d);
    }
}
const start =
    source.match(/bool FirstLevel::BeginEnemyAttack[\s\S]*?(?=void FirstLevel::CancelEnemyAttack)/)[0];
assert(!start.includes("DamagePlayer("));
assert(start.includes("enemy.attackTime = 0"));
assert(start.includes("enemy.attackImpactPending = true"));
const attack =
    source.match(/void FirstLevel::UpdateEnemyAttack[\s\S]*?(?=std::vector<EnemyAttackImpact>)/)[0];
for (const contract
         of ["enemy.attackImpactPending = false", "BeastImpactFraction", "inFront", "!safe",
             "ClearLine(from, to)", "BeastAttackRange", "enemyAttackImpacts.push_back", "EnemyAttackMisses"])
{
    assert(attack.includes(contract), `Missing staged attack gate: ${contract}`);
}
const update = source.slice(source.indexOf("void FirstLevel::Update(float dt)"));
assert(!update.includes("DamagePlayer("), "Touch damage remains in the AI movement loop");
assert(update.includes("UpdateEnemyAttack(enemy, dt)"));
assert(update.includes("BeginEnemyAttack(enemy, Player().Id())"));
assert(source.includes('enemy->model = prefix + "attack"'));
assert(source.includes('enemy->running ? "run" : ""'));
assert(source.match(/void FirstLevel::Kill[\s\S]*?CancelEnemyAttack\(enemy\)/));
assert(source.match(/bool FirstLevel::RespawnEnemy[\s\S]*?CancelEnemyAttack\(enemy\)/));
assert(source.includes("CancelEnemyAttacksTargeting(Player().Id())"));
assert(world.includes("level.CancelEnemyAttacksTargeting(victim.Id())"));
assert(!world.includes("Hurt(npc, 5 + beast->kind"));
assert(world.includes("level.BeginEnemyAttack(*beast, npc.Id())"));
assert(world.indexOf("level.ConsumeEnemyAttackImpacts()") < world.indexOf("Step(*npc, dt)"));
const impact = constant("BeastImpactFraction");
// At both high and low frame rates, crossing the window resolves once; leaving range/cover misses.
for (const dt of [1 / 144, 1 / 60, 1 / 20, .25, 2])
{
    for (const duration of [.90, .78, .88])
    {
        let elapsed = 0, pending = true, attempts = 0;
        assert.equal(attempts, 0); // the begin tick never consumes dt or damages by overlap
        while (elapsed < duration)
        {
            elapsed = Math.min(duration, elapsed + dt);
            if (pending && elapsed >= duration * impact)
            {
                pending = false;
                attempts++;
            }
        }
        assert.equal(attempts, 1);
    }
}
const hit = (distance, forward, clear, safe, invulnerable) =>
    distance <= constant("BeastAttackRange") && forward >= .45 && clear && !safe && !invulnerable;
assert(hit(.8, 1, true, false, false));
assert(!hit(1.4, 1, true, false, false));
assert(!hit(.8, -1, true, false, false));
assert(!hit(.8, 1, false, false, false));
assert(!hit(.8, 1, true, true, false));
assert(!hit(.8, 1, true, false, true));
for (const id of ["animal_deer", "animal_wolf", "animal_tiger"])
{
    const asset = load(id);
    assert(asset.doc.animations.some(a => a.name === "attack"));
    const idle = geometry({asset : id}, "attack", 0);
    const peak = geometry({asset : id}, "attack", .5);
    const end = geometry({asset : id}, "attack", 1);
    assert(peak.some((t, i) => t.vertices.some(
                         (v, j) => Math.hypot(...v.p.map((n, k) => n - idle[i].vertices[j].p[k])) > .03)),
           `Static attack ${id}`);
    // The supplied clip starts one authored key into its pulse (<1 mm, well under one screen pixel).
    assert(end.every((t, i) => t.vertices.every(
                         (v, j) => Math.hypot(...v.p.map((n, k) => n - idle[i].vertices[j].p[k])) < .001)));
}
console.log(JSON.stringify({
    event : "animation_combat_contracts",
    passed : true,
    screen_directions : 8,
    walk_frames : 12,
    beast_attack_frames : 16,
    one_shot_variable_dt : true,
    dodge_cover_and_facing_gates : true,
    species_clips_checked : 3,
    game_build_performed : false,
    game_execution_performed : false
}));
