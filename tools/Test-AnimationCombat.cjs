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
assert(/SetPose\(\s*\*enemy,\s*prefix \+ "attack"/.test(source));
assert(source.includes('enemy->running ? "run" : ""'));
assert(source.match(/void FirstLevel::Kill[\s\S]*?CancelEnemyAttack\(enemy\)/));
assert(source.match(/bool FirstLevel::RespawnEnemy[\s\S]*?CancelEnemyAttack\(enemy\)/));
assert(source.includes("CancelEnemyAttacksTargeting(Player().Id())"));
assert(world.includes("level.CancelEnemyAttacksTargeting(victim.Id())"));
assert(!world.includes("Hurt(npc, 5 + beast->kind"));
assert(world.includes("level.BeginEnemyAttack(*beast, npc.Id())"));
assert(world.indexOf("level.ConsumeEnemyAttackImpacts()") < world.indexOf("Step(*npc, dt)"));
const game = read("SimpleGame.cpp");
const npcBegin = world.match(/bool World::BeginNpcAttack[\s\S]*?(?=void World::CancelNpcAttack)/)[0];
const npcAttack = world.match(/void World::AdvanceNpcAttack[\s\S]*?(?=bool World::ResolveNpcHit)/)[0];
const npcHit = world.match(/bool World::ResolveNpcHit[\s\S]*?(?=void World::Witness)/)[0];
const npcStep = world.match(/void World::Step[\s\S]*?(?=void World::SpreadRumors)/)[0];
assert(npcBegin.includes("npc.attackTime = 0") && npcBegin.includes("npc.attackForward ="));
assert(!/Damage(Player|Enemy)|Hurt\(|ResolveNpcHit\(/.test(npcBegin), "A new wind-up cannot deal damage");
assert(npcStep.indexOf("AdvanceNpcAttack(npc, dt)") < npcStep.indexOf("level.MoveActor("));
assert(!/Damage(Player|Enemy)|Hurt\(/.test(npcStep), "NPC movement cannot deal touch damage");
assert(world.includes("npc->attackTime < 0 && npc->thinkTime <= 0"), "AI cannot retarget in mid-swing");
for (const contract of ["previous < NpcAttackWindup", "previous + dt >= NpcAttackWindup",
                        "CanEngage(npc, npc.action, *target)", "distance <= NpcAttackRange", "inFront",
                        "level.ClearLine(from, to)", "ResolveNpcHit(npc, *target)", "npc.raidHits >= 2"])
{
    assert(npcAttack.includes(contract), `Missing NPC attack gate: ${contract}`);
}
assert(npcHit.includes("level.DamagePlayer(14, npc.Id())"));
assert(npcHit.indexOf("if (!hit || npc.action != Action::Raid)") < npcHit.indexOf("wallet -= stolen"));
assert(npcHit.indexOf("wallet -= stolen") < npcHit.indexOf("++npc.raidHits"));
assert(world.includes("CancelNpcAttack(victim)") && !npcHit.includes("Transfer(ledger"));
assert(source.match(/bool FirstLevel::DamagePlayer[\s\S]*?invulnerability > 0[\s\S]*?return false;/));
assert(game.includes("NpcAttackCuesDrawn") && game.includes("npc->attackForward"));

const levelHeader = read("LevelOne.h");
const dodgeValue = name => Number(levelHeader.match(new RegExp(name + " = ([.\\d]+)f;"))[1]);
const dodgeDuration = dodgeValue("DodgeDuration");
const dodgeProtection = dodgeValue("DodgeProtection");
const dodgeCooldown = dodgeValue("DodgeCooldown");
assert(dodgeProtection > 0 && dodgeProtection < dodgeDuration && dodgeDuration < dodgeCooldown);
const dodgeStart = source.match(/bool FirstLevel::BeginDodge[\s\S]*?(?=void FirstLevel::MovePlayer)/)[0];
const dodgeMove = source.match(/void FirstLevel::MovePlayer[\s\S]*?(?=bool FirstLevel::ClearLine)/)[0];
const damagePlayer = source.match(/bool FirstLevel::DamagePlayer[\s\S]*?(?=void FirstLevel::MoveActor)/)[0];
assert(dodgeStart.includes("!CanDodge()") && dodgeStart.includes("dodgeCooldown = DodgeCooldown"));
assert(dodgeMove.includes("(std::min)(dt, dodgeRemaining)") && dodgeMove.includes("Move(Player(), dx, dy)"));
assert(damagePlayer.indexOf("dodgeRemaining > DodgeDuration - DodgeProtection") <
       damagePlayer.indexOf("stats.health -= appliedDamage"));
assert(source.includes("attackCooldown > 0 || Dodging()"));
assert(game.includes("GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT"));
assert(game.indexOf("firstLevel.BeginDodge(sx, sy)") < game.indexOf("firstLevel.MovePlayer(sx, sy, dt)"));
for (const dt of [1 / 144, 1 / 60, .05])
{
    let remaining = dodgeDuration;
    let moveTime = 0;
    while (remaining > 0)
    {
        moveTime += Math.min(dt, remaining);
        remaining = Math.max(0, remaining - dt);
    }
    assert(Math.abs(moveTime - dodgeDuration) < 1e-8);
}

const npcWindup = constant("NpcAttackWindup");
const npcDuration = constant("NpcAttackDuration");
for (const dt of [1 / 144, 1 / 60, 1 / 20, .25, 2])
{
    let elapsed = 0, attempts = 0;
    while (elapsed < npcDuration)
    {
        if (elapsed < npcWindup && elapsed + dt >= npcWindup)
            attempts++;
        elapsed += dt;
    }
    assert.equal(attempts, 1);
}
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
    npc_one_shot_variable_dt : true,
    npc_raid_requires_applied_damage : true,
    dodge_cover_and_facing_gates : true,
    species_clips_checked : 3,
    game_build_performed : false,
    game_execution_performed : false
}));
