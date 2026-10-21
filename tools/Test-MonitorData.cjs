"use strict";
// Headless data tests only; no DOM/browser/server/game launch.
const assert = require("node:assert/strict");
const D = require("../BaseProject_SE/SimpleGame/Monitor/monitor.js");
const npc = { actor_id: 123, name: '미라 "벤" <script>', health: 80, max_health: 100,
    world_x: 53.5, world_y: 68.5, coins: 35, job: "engineer", action: "investigate",
    home_settlement_id: "muyeon", health_weight: 1, wealth_weight: .8,
    enlightenment_weight: .3, raid_weight: 0, knowledge_mask: 5 };
const dead = { ...npc, actor_id: 124, name: "사망한 주민", health: 0, action: "dead" };
const raider = { ...npc, actor_id: 125, name: "먹쇠", job: "raider", home_settlement_id: "raider_camp", raid_weight: 1.8 };
const fixture = { schema_version: 1, event: "monitor_snapshot", status: "ready", session_id: "test_1",
    sequence: 1, uptime_ms: 1000, paused: true, world: {
        counts: { npcs_total: 3, npcs_alive: 2, npcs_dead: 1, monsters_total: 192,
            monsters_alive: 190, monsters_dead_or_inactive: 2 },
        npcs: [npc, dead, raider], recent_events: ["장부를 찾았다."],
        settlements: [{ id: "muyeon", name: "무연항", rumors: { ledger_known: 1 },
            goal_weight_totals: { health: 1 }, faction_population: [0, 0, 1, 0] }]
    } };
assert.equal(D.validateSnapshot(JSON.parse(JSON.stringify(fixture))).world.npcs[0].name, npc.name);
assert.throws(() => D.validateSnapshot({ ...fixture, schema_version: 99 }));
assert.throws(() => D.validateSnapshot({ ...fixture, world: { ...fixture.world, counts: { npcs_total: -1 } } }));
assert.throws(() => D.validateSnapshot({ ...fixture, world: { ...fixture.world,
    counts: { ...fixture.world.counts, monsters_alive: 200 } } }));
assert.throws(() => D.validateSnapshot({ ...fixture, world: { ...fixture.world, npcs: [{ ...npc, world_x: NaN }] } }));
assert.equal(D.selectNpcs(fixture.world.npcs, { query: "미라" }).length, 1);
assert.equal(D.selectNpcs(fixture.world.npcs, { query: "125" })[0].name, "먹쇠");
assert.equal(D.selectNpcs(fixture.world.npcs, { showDead: false }).length, 2);
assert.equal(D.selectNpcs(fixture.world.npcs, { job: "raider", settlement: "raider_camp" }).length, 1);
assert.equal(D.selectNpcs(fixture.world.npcs, { action: "dead", showDead: false }).length, 0);
assert.equal(D.selectNpcs(fixture.world.npcs, { query: "없는이름" }).length, 0);
assert.equal(D.goal(raider), "약탈 (1.8)");
assert.equal(D.knowledge(5), "장부 · 수문 계획");
assert.equal(D.knowledge(0), "없음");
assert.equal(D.delta(npc, { ...npc, coins: 25 }, "coins"), "+10");
assert.equal(D.delta(npc, { ...npc, health: 100 }, "health"), "-20");
assert.equal(D.delta(npc, undefined, "health"), "");
assert.equal(D.delta(npc, npc, "coins"), "");
assert.equal(D.settlementName(fixture.world, "muyeon"), "무연항");
assert.equal(D.settlementName(fixture.world, "wilderness"), "마을 밖");
assert.equal(D.moodSummary(null), "분위기 표본 수집 중");
assert.equal(D.moodSummary({ observed_seconds: 0 }), "분위기 표본 수집 중");
assert.equal(D.moodSummary({ observed_seconds: 20, percent: { quiet: 25, busy: 50, restless: 25, conflict: 0 } }),
    "일상 유지 25.0% / 장터 활기 50.0% / 소문에 술렁임 25.0%");
assert.equal(D.actions.travel, "마을 간 왕래");
const villageIds = ["muyeon", "solbaram", "gingil", "eunsol", "deulsaem"];
const expandedNpcs = Array.from({ length: 100 }, (_, i) => ({ ...npc, actor_id: 1000 + i,
    home_settlement_id: villageIds[Math.floor(i / 20)], action: i % 20 >= 18 ? "travel" : "trade" }));
const expanded = { ...fixture, world: { ...fixture.world, npcs: expandedNpcs,
    settlements: villageIds.map(id => ({ ...fixture.world.settlements[0], id })),
    counts: { npcs_total: 100, npcs_alive: 100, npcs_dead: 0, monsters_total: 100, monsters_alive: 98, monsters_dead_or_inactive: 2 } } };
assert.equal(D.validateSnapshot(expanded).world.settlements.length, 5);
for (const id of villageIds) assert.equal(D.selectNpcs(expandedNpcs, { settlement: id }).length, 20);
assert.equal(D.selectNpcs(expandedNpcs, { action: "travel" }).length, 10);
assert.equal(D.validateSnapshot({ ...fixture, world: { ...fixture.world, npcs: [], settlements: [], recent_events: [],
    counts: { npcs_total: 0, npcs_alive: 0, npcs_dead: 0, monsters_total: 0, monsters_alive: 0, monsters_dead_or_inactive: 0 } } }).world.npcs.length, 0);
console.log(JSON.stringify({ event: "monitor_data_tests", passed: true, game_build_performed: false,
    game_execution_performed: false, browser_test_performed: false }));
