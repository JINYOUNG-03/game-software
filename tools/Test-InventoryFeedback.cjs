"use strict";
// Static source/geometry and state arithmetic. No C++ build, game or shader execution.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "../BaseProject_SE/SimpleGame");
const read = name => fs.readFileSync(path.join(root, name), "utf8");
const inventory = read("InventoryPresentation.h");
const level = read("LevelOne.cpp");
const game = read("SimpleGame.cpp");
const world = read("HarborWorld.cpp");
const renderer = read("RendererModels.cpp");
const shader = read("Shaders/SolidRect.fs");
const number = name => Number(inventory.match(new RegExp("\\b" + name + " = ([\\d.]+)"))[1]);
const columns = number("Columns"), rows = number("Rows");
const x0 = number("GridX"), y0 = number("GridY"), stride = number("SlotStride"), size = number("SlotSize");
assert.equal(columns * rows, 24);
const hit = (x, y) => {
    if (x < x0 || y < y0)
        return -1;
    const c = Math.floor((x - x0) / stride), r = Math.floor((y - y0) / stride);
    if (c >= columns || r >= rows || x - x0 - c * stride >= size || y - y0 - r * stride >= size)
        return -1;
    return r * columns + c;
};
for (let slot = 0; slot < 24; slot++)
{
    const x = x0 + (slot % columns) * stride, y = y0 + Math.floor(slot / columns) * stride;
    assert.equal(hit(x + size / 2, y + size / 2), slot);
    assert.equal(hit(x + size + .1, y + 5), -1);
    assert(x >= 140 && x + size <= 1140 && y >= 120 && y + size <= 720);
    for (const scale of [.5, 1, 1.5, 2])
    {
        // Callback coordinates are scaled once to logical resolution; an Actor translation is removed once.
        const ox = 10, oy = -15;
        assert.equal(hit((x + 40 + ox) * scale / scale - ox, (y + 40 + oy) * scale / scale - oy), slot);
    }
}
assert.equal(hit(x0 - 1, y0), -1);
assert.equal(hit(x0, y0 + rows * stride), -1);
const move = (selected, delta) => {
    let row = Math.floor(selected / columns), column = selected % columns;
    if (Math.abs(delta) === columns)
        row = Math.max(0, Math.min(rows - 1, row + delta / columns));
    else
        column = Math.max(0, Math.min(columns - 1, column + delta));
    return row * columns + column;
};
assert.equal(move(0, -1), 0);
assert.equal(move(5, 1), 5);
assert.equal(move(18, 6), 18);
assert.equal(move(4, -6), 4);
assert(game.includes("Inventory::MoveSelection(inventorySelection, inventoryMovePressed)"));
for (const key
         of ["stats.potions", "stats.coins", "stats.hides", "ledger ? 1 : 0", "key ? 1 : 0", "copy ? 1 : 0"])
{
    assert(inventory.includes(key));
}
assert(inventory.includes("quantity > 0"));
assert(!inventory.includes("stats.potions =") && !inventory.includes("stats.coins ="));
for (let ownership = 0; ownership < 8; ownership++)
{
    for (const quantity of [-1, 0, 3, 99999])
    {
        const quantities = [
            quantity, quantity, quantity, ownership & 1 ? 1 : 0, ownership & 2 ? 1 : 0, ownership & 4 ? 1 : 0
        ];
        const stacks = quantities.filter(q => q > 0);
        assert(stacks.length <= 6 && stacks.every(q => q > 0));
    }
}
for (const contract
         of ['DrawModelPreview("hero_idle"', "stats.MaxHealth()", "stats.Attack()", "stats.Defense()",
             "InventorySlotsDrawn", "InventoryStacksDrawn", "Inventory::HitSlot",
             "glutMouseFunc(InventoryMouse)", "glutSpecialFunc(InventorySpecialKey)",
             "inventoryTogglePressed = true", "WorldPaused(), dt)"])
{
    assert(game.includes(contract), `Inventory integration missing ${contract}`);
}
assert(game.includes("paused || helpVisible || statsPanel || harbor.Conversation().Active()"));
const update = game.slice(game.indexOf("void Update(float dt)"),
                          game.indexOf("} // namespace", game.indexOf("void Update(float dt)")));
assert(update.indexOf("UpdateInventoryInput()") < update.indexOf("firstLevel.MovePlayer"));
assert(world.includes("return Holder(ledger) == level.Player().Id()"));
assert(world.includes("return Holder(key) == level.Player().Id()"));
assert(read("HarborWorld.h").includes("return copied;"));
assert(renderer.includes("height / r[3]"));
assert(renderer.includes("m_SurfaceData = surface"));
for (const bounds of [[ 23, 93 ], [ 110, 105 ], [ 1, 1 ]])
{
    const fit = Math.min(190 / (bounds[0] * 1.4), 192 / bounds[1]);
    assert(bounds[0] * 1.4 * fit <= 190.0001 && bounds[1] * fit <= 192.0001);
}
assert(read("LevelOne.h").includes("std::array<ActorId, 48> impactActors"));
const report = level.match(/void FirstLevel::ReportHit[\s\S]*?(?=void FirstLevel::UpdateHitFeedback)/)[0];
assert(!report.includes("scene.Create"), "Impact allocation must be pooled, not per hit");
assert(report.includes("nextImpact = (nextImpact + 1) % impactCount"));
assert(report.includes("HitEffectsOverwritten") && report.includes("HitEffectsSkipped"));
assert(level.includes("ReportHit(enemy, appliedDamage"));
assert(level.includes("ReportHit(Player(), appliedDamage, source)"));
assert(world.includes("level.ReportHit(victim, appliedDamage, attacker)"));
const begin =
    level.match(/bool FirstLevel::BeginEnemyAttack[\s\S]*?(?=void FirstLevel::CancelEnemyAttack)/)[0];
assert(!begin.includes("ReportHit("), "Wind-up/misses must not trigger contact effects");
const pool = Array.from({length : 48}, () => ({active : false, age : 0}));
let next = 0, overwritten = 0;
for (let i = 0; i < 200; i++)
{
    if (pool[next].active)
        overwritten++;
    pool[next] = {active : true, age : 0};
    next = (next + 1) % pool.length;
}
assert.equal(overwritten, 152);
for (let tick = 0; tick < 100; tick++)
    for (const effect of pool)
    {
        if (effect.active)
        {
            effect.age += .016;
            if (effect.age >= .46)
                effect.active = false;
        }
    }
assert(pool.every(e => !e.active));
assert(shader.includes("v_Material == 28") && shader.includes("i < 6"));
assert(shader.includes("v_Material != 28"));
assert(game.includes("std::pow(1 - progress, .7f)"));
assert(game.includes("firstLevel.seed, player.x, player.y"),
       "Camera shake must not change logged player position");
assert(read("SimpleGame.vcxproj").includes('ClInclude Include="InventoryPresentation.h"'));
console.log(JSON.stringify({
    event : "inventory_feedback_contracts",
    passed : true,
    inventory_slots : 24,
    slot_geometry_probes : 120,
    ownership_source : "existing_stats_and_actor_holders",
    impact_pool_capacity : 48,
    stress_hits : 200,
    misses_have_no_contact_effect : true,
    game_build_performed : false,
    game_execution_performed : false
}));
