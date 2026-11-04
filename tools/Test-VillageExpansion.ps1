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

# All five markets must permit local trade, including merchants visiting from another home village.
# Coordinates/radius come from production layout; these are geometry and source checks, not a C++ execution.
$markets = $centres.Clone()
$spawn = [regex]::Match($layout, '(?s)inline ActorPosition VillageSpawn\(\).*?return Town\(([.\d]+)f, ([.\d]+)f')
if (-not $spawn.Success) { throw 'Missing first village market position.' }
$markets[0] = @(((Constant $layout 'TownOffsetX') + [double]$spawn.Groups[1].Value),
               ((Constant $layout 'TownOffsetY') + [double]$spawn.Groups[2].Value))
$marketRadius = Constant $layout 'MarketRadius'
function Find-Market([double]$x, [double]$y)
{
    for ($village = 0; $village -lt $markets.Count; $village++)
    {
        $dx = $x - $markets[$village][0]; $dy = $y - $markets[$village][1]
        if ([Math]::Sqrt($dx * $dx + $dy * $dy) -le $marketRadius) { return $village }
    }
    return -1
}
function Share-Market($first, $second)
{
    $market = Find-Market $first[0] $first[1]
    return $market -ge 0 -and $market -eq (Find-Market $second[0] $second[1])
}
for ($village = 0; $village -lt $villages; $village++)
{
    $market = $markets[$village]
    if ((Find-Market $market[0] $market[1]) -ne $village -or
        -not (Share-Market $market @(($market[0] + 1), $market[1])))
    { throw "Local trade rejected in village $village" }
    if ((Find-Market ($market[0] + $marketRadius) $market[1]) -ne $village -or
        (Find-Market ($market[0] + $marketRadius + .01) $market[1]) -ne -1)
    { throw "Market radius boundary incorrect in village $village" }
    $inside = @(($market[0] + $marketRadius - .1), $market[1])
    $outside = @(($market[0] + $marketRadius + .1), $market[1])
    if ((Share-Market $inside $outside) -or (Share-Market $outside $inside))
    { throw 'Both trade participants must be inside the same market.' }
    for ($other = 0; $other -lt $villages; $other++)
    {
        if ($other -ne $village -and (Share-Market $market $markets[$other]))
        { throw 'Trade participants in different villages share a market.' }
    }
}
if ((Find-Market 0 0) -ne -1 -or (Share-Market @(0,0) @(0,0)))
{ throw 'Being together outside all markets must not allow trade.' }
$marketPolicy = [regex]::Match($layout, '(?s)inline int MarketAt\(.*?(?=inline bool BetweenVillages)').Value
if (-not $marketPolicy.Contains('village < VillageCount') -or
    -not $marketPolicy.Contains('VillageMarket(village)') -or
    -not $marketPolicy.Contains('<= MarketRadius') -or
    -not $marketPolicy.Contains('market >= 0 && market == MarketAt(second)') -or
    $marketPolicy.Contains('homeVillage'))
{ throw 'Trade locations must use all current physical markets, not a home-village restriction.' }
$playerTrade = Get-Content -Raw (Join-Path $root 'HarborTrade.cpp')
$exchange = [regex]::Match($world, '(?s)void World::Exchange\(.*?(?=void World::Disclose)').Value
$npcStep = [regex]::Match($world, '(?s)void World::Step\(.*?(?=void World::SpreadRumors)').Value
if (-not $playerTrade.Contains('WorldLayout::ShareMarket(hero, partner)') -or
    -not $exchange.Contains('WorldLayout::ShareMarket(seller.WorldPosition(), buyer.WorldPosition())') -or
    -not $npcStep.Contains('WorldLayout::ShareMarket(npc.WorldPosition(), merchant->WorldPosition())') -or
    $playerTrade.Contains('Site(2)') -or $npcStep.Contains('Distance(merchant->WorldPosition(), Site(2))'))
{ throw 'Player, NPC hide trade and NPC food purchases must share the five-market policy.' }
foreach ($token in @('npc.health > 0', 'npc.affinity >= -20', 'npc.threatTime <= 0', 'Npc* npc = Nearest()',
                     'npc->variant == 2 || npc->variant == 3', 'recordChoice == RecordChoice::Undecided',
                     'TransferLimit(npc->coins, stats.hides, 6', 'TransferLimit(stats.coins, npc->food, 8'))
{
    if (-not $playerTrade.Contains($token)) { throw "Trade safeguard/story condition missing: $token" }
}
[pscustomobject]@{
    event = 'village_expansion_static_contracts'; passed = $true; map_size_tiles = $mapSize
    village_count = $villages; resident_counts = $slots; courier_counts = $couriers
    projected_neighbour_travel_seconds = $seconds; monster_target = $monsters; respawn_delay_seconds = 2
    local_trade_markets = $markets.Count; shared_market_boundary_checks = $true
    game_build_performed = $false; game_execution_performed = $false; actual_travel_measured = $false
} | ConvertTo-Json -Depth 4
