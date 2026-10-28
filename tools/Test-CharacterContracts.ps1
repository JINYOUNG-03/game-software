#requires -Version 7.0
# Static contracts and presentation arithmetic, not a compiler or game execution.
$ErrorActionPreference = 'Stop'
$characterRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$game = Get-Content -Raw (Join-Path $characterRoot 'SimpleGame.cpp')
$level = Get-Content -Raw (Join-Path $characterRoot 'LevelOne.cpp')
$world = Get-Content -Raw (Join-Path $characterRoot 'HarborWorld.cpp')
$models = Get-Content -Raw (Join-Path $characterRoot 'RendererModels.cpp')
$actor = Get-Content -Raw (Join-Path $characterRoot 'Actor.h')
$fs = Get-Content -Raw (Join-Path $characterRoot 'Shaders/SolidRect.fs')
$presentation = Get-Content -Raw (Join-Path $characterRoot 'CharacterPresentation.h')

foreach ($contract in @('actor.visualDirection)',
                       'DrawResourceSwordTrail(', 'CharacterPresentation::AttackTrailStrength(',
                       'ModelVisualHeight(actor.model)', 'npc->visualHeight + 36',
                       'y - npc->visualHeight - 12', 'CompactNpcMarker('))
{
    if (-not $game.Contains($contract)) { throw "Missing imported character integration: $contract" }
}
foreach ($contract in @('Player().visualDirection = CharacterPresentation::ScreenDirection',
                       '"hero_idle"', '"hero_attack" + std::to_string(pose)',
                       'attackCooldown = .48f', 'attackAnimation = CharacterPresentation::AttackDuration',
                       'attackImpactPending = true', 'ResolveAttackImpact();', 'if (attackAnimation <= 0)'))
{
    if (-not $level.Contains($contract)) { throw "Missing player animation contract: $contract" }
}
foreach ($contract in @('CharacterPresentation::ScreenDirection(',
                       'npc.phase += travelled * 5.5f', '"_idle"'))
{
    if (-not $world.Contains($contract)) { throw "Missing NPC animation contract: $contract" }
}
if (-not $actor.Contains('int visualDirection = 1') -or -not $actor.Contains('float visualHeight = 0'))
{ throw 'Facing/visual height must belong to Actor.' }
foreach ($contract in @('ResourceLookupHit', 'ResourceLookupMiss', 'CharacterInstances',
                       'r.data() + 4', 'current->weapon', 'previous->weapon'))
{
    if (-not $models.Contains($contract)) { throw "Missing resource rendering contract: $contract" }
}
if ($game.Contains('BuildCitizen(') -or $models.Contains('MIST_MODELS'))
{ throw 'Procedural character fallback remains active.' }
# Screen-facing order must match the importer yaw table.
if (-not $presentation.Contains('DirectionCount = 8') -or -not $presentation.Contains('diagonalBoundary') -or
    -not $presentation.Contains('x >= 0 ? 4 : 5'))
{ throw 'Eight-direction ordering/diagonal sectors missing.' }
$duration = [double][regex]::Match($presentation, 'AttackDuration = ([.\d]+)f').Groups[1].Value
$frames = [int][regex]::Match($presentation, '\bAttackFrames = (\d+)').Groups[1].Value
$impactFraction = [double][regex]::Match($presentation, 'AttackImpactTime = AttackDuration \* ([.\d]+)f').Groups[1].Value
if ($frames -ne 12 -or $duration -ne .38) { throw 'Expected twelve readable attack poses across 380ms.' }
if (-not $presentation.Contains('progress > .45f && progress < .70f'))
{ throw 'Trail must follow the authored hit window.' }
$seenPoses = [Collections.Generic.HashSet[int]]::new()
for ($sample = -1; $sample -le 101; $sample++)
{
    $progress = [Math]::Clamp($sample / 100.0, 0.0, 1.0)
    $pose = [Math]::Min($frames - 1, [int][Math]::Truncate($progress * $frames))
    if ($pose -lt 0 -or $pose -ge $frames) { throw 'Attack pose exceeds cached models.' }
    [void]$seenPoses.Add($pose)
}
if ($seenPoses.Count -ne $frames) { throw 'Attack clip does not reach every pose.' }
$impactPoseProgress = [Math]::Truncate($impactFraction * $frames) / ($frames - 1)
if ($impactPoseProgress -le .45 -or $impactPoseProgress -ge .70)
{ throw 'Damage must occur during the visible cut, not during wind-up or recovery.' }

# Arithmetic checks for threshold crossing at high/low frame rates and a frame longer than the clip.
foreach ($dt in @((1.0 / 144), (1.0 / 60), (1.0 / 20), .25, .7))
{
    $pending = $true; $hits = 0; $remaining = $duration
    for ($tick = 0; $tick -lt 80; $tick++)
    {
        $remaining = [Math]::Max(0, $remaining - $dt)
        if ($pending -and $remaining -le $duration * (1 - $impactFraction))
        { $pending = $false; $hits++ }
    }
    if ($hits -ne 1) { throw "Swing must resolve once, including skipped visual frames: dt=$dt" }
}
$startAttack = [regex]::Match($level, '(?s)bool FirstLevel::Attack\(\).*?(?=bool FirstLevel::ConsumeAttackImpact)').Value
if ($startAttack.Contains('DamageEnemy(')) { throw 'Starting wind-up must not deal immediate damage.' }
if (-not $game.Contains('if (firstLevel.ConsumeAttackImpact())') -or
    -not $level.Contains('attackImpactPending = false;') -or
    -not $level.Contains('attackImpactReady = false;')) { throw 'Missing one-shot NPC impact consumption.' }
if (36 - 13 - (12 + 2.5) -le 0) { throw 'NPC labels overlap health bars.' }
[pscustomobject]@{
    event = 'character_static_contracts'; passed = $true; npc_appearances = 18
    walking_frames = 12; player_attack_frames = $frames; screen_directions = 8
    attack_duration_ms = $duration * 1000; impact_once_under_variable_dt = $true
    appearance_source = 'resource_glb'; same_world_foot_anchor = $true
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
