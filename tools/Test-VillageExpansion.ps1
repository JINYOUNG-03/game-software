#requires -Version 7.0
# Static integration contracts and layout/population/rumour arithmetic, not a game execution.
$ErrorActionPreference = 'Stop'
$root = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$layout = Get-Content -Raw (Join-Path $root 'WorldLayout.h')
$world = Get-Content -Raw (Join-Path $root 'HarborWorld.cpp')
$level = Get-Content -Raw (Join-Path $root 'LevelOne.cpp')
$levelHeader = Get-Content -Raw (Join-Path $root 'LevelOne.h')
function Constant($source, $name)
{
    $match = [regex]::Match($source, "\b$name\s*=\s*([.\d]+)")
    if (-not $match.Success) { throw "Missing constant $name" }
    return [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}
$villages = [int](Constant $layout 'VillageCount')
$population = [int](Constant $layout 'NpcsPerVillage')
$monsters = [int](Constant $layout 'MonsterCount')
$mapSize = [int](Constant $layout 'MapSize')
if ($villages -ne 5 -or $population -ne 20 -or $monsters -ne 100 -or $mapSize -ne 384)
{ throw 'Expected five inhabited villages, 100 NPCs and 100 monsters in a 384-square world.' }
$slots = [int[]]::new($villages)
$couriers = [int[]]::new($villages)
for ($i = 0; $i -lt $villages * $population; $i++)
{
    $village = 0
    if ($i -lt 18) { $village = if ($i -in @(7,8)) { 4 } elseif ($i -in @(5,6,16)) { 1 } else { 0 } }
    else { while ($slots[$village] -ge $population) { $village++ } }
    if ($slots[$village] -ge $population - 2) { $couriers[$village]++ }
    $slots[$village]++
}
if (($slots | Where-Object { $_ -ne 20 }) -or ($couriers | Where-Object { $_ -ne 2 }))
{ throw 'Resident allocation must be exactly twenty per village, including two travellers.' }
# Read the production coordinates; do not assume an unchanged placement.
$centres = ,@(((Constant $layout 'TownOffsetX') + 21.5), ((Constant $layout 'TownOffsetY') + 20.5))
foreach ($function in @('HunterCamp', 'Waystation', 'OldShrine'))
{
    $m = [regex]::Match($layout, "(?s)inline ActorPosition $function\(\).*?return \{([.\d]+)f, ([.\d]+)f")
    if (-not $m.Success) { throw "Missing village centre $function" }
    $centres += ,@([double]$m.Groups[1].Value, [double]$m.Groups[2].Value)
}
$m = [regex]::Match($layout, 'case 4:\s*return \{([.\d]+)f, ([.\d]+)f')
$centres += ,@([double]$m.Groups[1].Value, [double]$m.Groups[2].Value)
$hops = [regex]::Match($layout, 'const int next\[\] = \{([^}]+)\}').Groups[1].Value.Split(',')
$seconds = @()
for ($i = 0; $i -lt $villages; $i++)
{
    $a = $centres[$i]; $b = $centres[[int]$hops[$i]]
    $dx = $b[0] - $a[0]; $dy = $b[1] - $a[1]
    if ($dx -ne 0 -and $dy -ne 0) { throw 'Neighbour roads must not have an unaccounted diagonal detour.' }
    $projectedX = ($dx - $dy) * 48; $projectedY = ($dx + $dy) * 24
    $duration = [Math]::Sqrt($projectedX * $projectedX + $projectedY * $projectedY) / (Constant $levelHeader 'MoveSpeed')
    if ($duration -lt 19 -or $duration -gt 21) { throw "Village hop differs from approximately twenty seconds: $duration" }
    $seconds += $duration
}
foreach ($token in @('WorldLayout::VillageCount * WorldLayout::NpcsPerVillage', 'residentSlots[village]++',
                     'npc.appearance = appearance', 'npc.homeVillage = village', 'npc.courier = courier',
                     'npc.action == Action::Raid ? 2.0f : npc.courier ? 3.6f',
                     'SpreadRumors(npcs)', 'known.push_back(npc.knowledge)', 'int learned = known[i] & ~receiver.knowledge',
                     'Distance(p, receiver.WorldPosition()) > 1.6f', '!level.ClearLine(p, receiver.WorldPosition())',
                     'Distance(npc->WorldPosition(), origin) < 6', 'news >= 3', 'UpdateVillageStatistics(dt, npcs)',
                     'Perf::Cpu::RumorUpdate', 'Perf::Counter::RumorTransfers', 'Perf::Cpu::VillageStatistics',
                     'VillageMood(int village, const std::vector<const Npc*>& npcs) const',
                     'const std::vector<const Npc*> observers(npcs.begin(), npcs.end())'))
{
    if (-not $world.Contains($token)) { throw "Missing village/physical rumour integration: $token" }
}
foreach ($token in @('WorldLayout::BetweenVillages(p)', 'beastHomes.size() == WorldLayout::MonsterCount',
                     'enemy.respawn = WorldLayout::RespawnDelaySeconds', 'attempt < 128',
                     'Distance(p, enemy.deathPosition) < 12', 'Distance(p, Player().WorldPosition()) < 20',
                     'enemy.home = enemy.LocalPosition()', 'Perf::Cpu::EnemyRespawn', 'Perf::Counter::EnemyRespawns'))
{
    if (-not $level.Contains($token)) { throw "Missing remote respawn integration: $token" }
}
# Frozen sender masks stop a rumour from travelling multiple meeting links in the same tick.
$known = @(8, 0, 0); $next = $known.Clone()
foreach ($edge in @(@(0,1), @(1,2))) { $next[$edge[1]] = $next[$edge[1]] -bor $known[$edge[0]] }
if ($next[1] -ne 8 -or $next[2] -ne 0) { throw 'Rumours cascade without a later physical meeting.' }
if ((Constant $layout 'RespawnDelaySeconds') -ne 2) { throw 'Fast respawn delay must be two seconds.' }
[pscustomobject]@{
    event = 'village_expansion_static_contracts'; passed = $true; map_size_tiles = $mapSize
    village_count = $villages; resident_counts = $slots; courier_counts = $couriers
    projected_neighbour_travel_seconds = $seconds; monster_target = $monsters; respawn_delay_seconds = 2
    game_build_performed = $false; game_execution_performed = $false; actual_travel_measured = $false
} | ConvertTo-Json -Depth 4
