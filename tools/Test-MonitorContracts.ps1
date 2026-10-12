#requires -Version 7.0
# Read-only/static checks. Does not compile, launch the game, bind a socket or open a browser.
$ErrorActionPreference = 'Stop'
$monitorRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$server = Get-Content -Raw (Join-Path $monitorRoot 'WorldMonitor.cpp')
$world = Get-Content -Raw (Join-Path $monitorRoot 'HarborWorld.cpp')
$game = Get-Content -Raw (Join-Path $monitorRoot 'SimpleGame.cpp')
$js = Get-Content -Raw (Join-Path $monitorRoot 'Monitor/monitor.js')
$html = Get-Content -Raw (Join-Path $monitorRoot 'Monitor/index.html')
[xml]$project = Get-Content -Raw (Join-Path $monitorRoot 'SimpleGame.vcxproj')
[xml]$filters = Get-Content -Raw (Join-Path $monitorRoot 'SimpleGame.vcxproj.filters')

foreach ($contract in @('htonl(INADDR_LOOPBACK)', 'SO_EXCLUSIVEADDRUSE', 'LocalRequest(request)',
                       'SnapshotIntervalMs = 1000', 'SubscriptionTimeoutMs = 5000',
                       'request.size() < 8192', 'state.lastRequest.load()', 'state.snapshot = std::move(json)',
                       'state.requests.exchange(0)', 'SIMPLEGAME_MONITOR', 'world.SnapshotJson()',
                       'application/json; charset=utf-8', 'Content-Security-Policy:'))
{
    if (-not $server.Contains($contract)) { throw "Missing monitor transport contract: $contract" }
}
$worker = [regex]::Match($server, '(?s)struct Server::Impl.*?(?=Server::Server\()').Value
foreach ($forbidden in @('SceneGraph', 'SnapshotJson()', 'Perf::Scope', 'Perf::Count', 'Harbor::World'))
{
    if ($worker.Contains($forbidden)) { throw "Network worker accesses game/profiler state: $forbidden" }
}
if ($server.Contains('Access-Control-Allow-Origin: *') -or $js.Contains('innerHTML'))
{ throw 'Monitor must be local/read-only and render game text with textContent.' }
foreach ($contract in @('worldMonitor.Start()', 'worldMonitor.Stop()',
                       'worldMonitor.Tick(harbor, paused || helpVisible, dt)'))
{
    if (-not $game.Contains($contract)) { throw "Missing monitor lifecycle hook: $contract" }
}
foreach ($key in @('counts', 'settlements', 'name', 'rumors', 'mood', 'mood_source', 'population',
                  'npcs_present', 'goal_weight_totals', 'faction_population', 'recent_events',
                  'home_settlement_id', 'current_settlement_id', 'monsters_alive', 'monsters_total'))
{
    if (-not $world.Contains('\"' + $key + '\"')) { throw "Missing JSON state key: $key" }
}
if (-not $world.Contains('scene.FindByKind(ActorKind::Enemy, false)') -or
    -not $world.Contains('scene.FindByKind(ActorKind::Npc, false)'))
{ throw 'Population counts must not be limited to visible or active entities.' }
foreach ($contract in @('/api/snapshot', 'AbortController', 'validateSnapshot(data)', 'latest.session_id',
                       'Math.min(8000', 'JSON.stringify(latest', 'textContent', 'selectNpcs'))
{
    if (-not $js.Contains($contract)) { throw "Missing monitor UI contract: $contract" }
}
foreach ($file in @('WorldMonitor.cpp', 'WorldMonitor.h', 'Monitor\index.html', 'Monitor\monitor.js', 'Monitor\monitor.css'))
{
    if (-not (Test-Path -LiteralPath (Join-Path $monitorRoot $file))) { throw "Missing file: $file" }
    $xpath = "//*[@Include='$file']"
    if (-not $project.SelectSingleNode($xpath) -or -not $filters.SelectSingleNode($xpath))
    { throw "Missing project/filter registration: $file" }
}
if (-not $project.SelectSingleNode("//*[@Name='CopyMonitorAssets']") -or
    -not $project.OuterXml.Contains('ws2_32.lib'))
{ throw 'Missing monitor asset deployment or Winsock library.' }
foreach ($id in @('overview', 'settlements', 'npcs', 'search', 'job', 'action', 'settlement',
                  'show-dead', 'npc-detail', 'events', 'json', 'toggle', 'retry', 'download'))
{
    if (-not $html.Contains('id="' + $id + '"')) { throw "Missing monitor element: $id" }
}
[pscustomobject]@{
    event = 'monitor_static_contracts'; passed = $true; loopback_only = $true
    snapshot_interval_ms = 1000; read_only = $true; background_network = $true
    game_build_performed = $false; game_execution_performed = $false; browser_test_performed = $false
} | ConvertTo-Json
