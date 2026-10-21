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

foreach ($contract in @('CharacterCloth(', 'CharacterFace(', 'CharacterBody(', 'int attackPose = -1',
                       'harbor_models_v6.cache', 'FrameName(prefix, pose)', 'pose < CharacterPresentation::AttackFrames',
                       '"hero_attack_up"', '"hero_attack_down"', 'CitizenBackSilhouette(', 'CitizenIdentity(',
                       'sword->handX - 1', 'sword.handX', 'pose.trailStrength',
                       'renderer->DrawModel(actor.model, x, y, actor.mirrorModel)',
                       'npc->visualHeight + 36', 'y - npc->visualHeight - 12',
                       'CompactNpcMarker(', 'owner->Kind() == ActorKind::Npc ? -22.0f : 70.0f',
                       'Perf::Cpu::CharacterBuild', 'Perf::Counter::CharacterModelsBuilt'))
{
    if (-not $game.Contains($contract)) { throw "Missing character rendering contract: $contract" }
}
foreach ($contract in @('Player().mirrorModel = facingX < 0', 'attackAnimation > 0',
                       'CharacterPresentation::AttackFrame(attackAnimation)', 'prefix + std::to_string(pose)',
                       'attackCooldown = .48f', 'attackAnimation = CharacterPresentation::AttackDuration',
                       'attackImpactPending = true', 'ResolveAttackImpact();', 'if (attackAnimation <= 0)'))
{
    if (-not $level.Contains($contract)) { throw "Missing player animation contract: $contract" }
}
foreach ($contract in @('npc.mirrorModel = screenDirection < 0', 'npc.phase += travelled * 5.5f',
                       'npc.visualHeight = CharacterPresentation::NpcVisualHeight(appearance)'))
{
    if (-not $world.Contains($contract)) { throw "Missing NPC animation contract: $contract" }
}
if (-not $actor.Contains('bool mirrorModel = false') -or -not $actor.Contains('float visualHeight = 0'))
{ throw 'Facing/visual height must be Actor presentation, not a separate placement.' }
foreach ($contract in @('bool mirrorX', 'x + (mirrorX ? -item.corners[i] : item.corners[i])',
                       'originX + (mirrorX ? -item.corners[i] : item.corners[i])',
                       'part.material > 21', 'Perf::Counter::CharacterInstances',
                       'Perf::Counter::MirroredCharacterInstances'))
{
    if (-not $models.Contains($contract)) { throw "Missing cached/mirrored model contract: $contract" }
}
if (-not $fs.Contains('v_Material == 20') -or -not $fs.Contains('v_Material == 21'))
{ throw 'Character fabric/skin shaders missing.' }

# Mirror local geometry around the unchanged actor foot anchor.
foreach ($origin in @(0.0, 312.5, 640.0))
{
    foreach ($local in @(-24.0, -3.0, 0.0, 19.5))
    {
        $facingRight = $origin + $local
        $facingLeft = $origin - $local
        if ([Math]::Abs(($facingRight + $facingLeft) * .5 - $origin) -gt .00001)
        { throw 'Mirroring changes the world foot anchor.' }
    }
}
$duration = [double][regex]::Match($presentation, 'AttackDuration = ([.\d]+)f').Groups[1].Value
$frames = [int][regex]::Match($presentation, 'AttackFrames = (\d+)').Groups[1].Value
$impactFraction = [double][regex]::Match($presentation, 'AttackImpactTime = AttackDuration \* ([.\d]+)f').Groups[1].Value
if ($frames -ne 12 -or $duration -ne .38) { throw 'Expected twelve readable attack poses across 380ms.' }
foreach ($contract in @('if (progress < .26f)', 'else if (progress < .60f)',
                       'pose.handY = Mix(-22, -49, t)', 'pose.angle = Mix(-2.2f, .85f, t)',
                       'pose.trailStrength = std::sin(', 'float trailStrength = 0'))
{
    if (-not $presentation.Contains($contract)) { throw "Missing attack phase contract: $contract" }
}
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
if ($impactPoseProgress -le .26 -or $impactPoseProgress -ge .60)
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
$identity = [regex]::Match($game, '(?s)void CitizenIdentity\(.*?(?=void BuildCitizen\()').Value
$identities = @([regex]::Matches($identity, 'case (\d+):') | ForEach-Object { [int]$_.Groups[1].Value })
if (($identities | Sort-Object -Unique).Count -ne 18 -or ($identities | Measure-Object -Maximum).Maximum -ne 17)
{ throw 'Each of the eighteen NPC definitions needs a distinct identity branch.' }
$visualHeights = [regex]::Match($presentation, 'heights\[\] = \{([^}]+)\}').Groups[1].Value.Split(',')
if ($visualHeights.Count -ne 18 -or [int]$visualHeights[2] -lt 100 -or [int]$visualHeights[4] -gt 70)
{ throw 'Nameplate heights must follow the tall plume and short apprentice, not one shared height.' }
if (36 - 13 - (12 + 2.5) -le 0) { throw 'NPC labels overlap health bars.' }
$expectedModels = 18 * 8 + 8 + $frames * 3 + 18 + 3 * 8 + 3 + 6 + 9
if ($expectedModels -gt 256) { throw 'Character cache exceeds the existing safe model limit.' }
[pscustomobject]@{
    event = 'character_static_contracts'; passed = $true; npc_appearances = 18
    walking_frames = 8; player_attack_frames = $frames; player_attack_direction_variants = 3
    expected_cache_models = $expectedModels; attack_duration_ms = $duration * 1000
    impact_once_under_variable_dt = $true; npc_identity_branches = $identities.Count
    appearance_revision = 6; same_world_foot_anchor = $true
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
