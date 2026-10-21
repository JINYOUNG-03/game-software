#requires -Version 7.0
# Static source and geometry contracts only. This does not build or run the game.
$ErrorActionPreference = 'Stop'
$openWorldRoot = Split-Path $PSScriptRoot -Parent
$openWorldSourceRoot = Join-Path $openWorldRoot 'BaseProject_SE/SimpleGame'
$layoutSource = Get-Content -Raw (Join-Path $openWorldSourceRoot 'WorldLayout.h')
$levelSource = Get-Content -Raw (Join-Path $openWorldSourceRoot 'LevelOne.cpp')
$npcSource = Get-Content -Raw (Join-Path $openWorldSourceRoot 'HarborWorld.cpp')
$renderSource = Get-Content -Raw (Join-Path $openWorldSourceRoot 'SimpleGame.cpp')

# Catch accidental duplicate function prefixes independently of the geometry checks.
if ([regex]::Matches($npcSource, '\bvoid\s+World::Witness\s*\(').Count -ne 1 -or
    [regex]::IsMatch($npcSource, '\bvoid\s+World::Witness\s+void\b'))
{ throw 'World::Witness definition must have exactly one function prefix.' }

function Read-Constant([string]$name)
{
    $match = [regex]::Match($layoutSource, "\b$name\s*=\s*([0-9.]+)")
    if (-not $match.Success) { throw "Missing layout constant: $name" }
    return [double]::Parse($match.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
}

$mapSize = [int](Read-Constant 'MapSize')
$chunkSize = [int](Read-Constant 'ChunkSize')
$appearanceCount = [int](Read-Constant 'NpcAppearanceCount')
$townX = Read-Constant 'TownOffsetX'
$townY = Read-Constant 'TownOffsetY'
$radius = Read-Constant 'RaiderTerritoryRadius'
if ($mapSize -ne 384 -or $mapSize % $chunkSize -ne 0) { throw 'Map/chunk size contract failed.' }
$bandit = [regex]::Match($layoutSource, '(?s)RaiderCamp\(\).*?return \{([0-9.]+)f,\s*([0-9.]+)f')
if (-not $bandit.Success) { throw 'Raider camp position missing.' }
$campX = [double]::Parse($bandit.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$campY = [double]::Parse($bandit.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
$villageEast = $townX + [int][regex]::Match($layoutSource, 'p.x <= TownOffsetX \+ (\d+)').Groups[1].Value
$villageSouth = $townY + [int][regex]::Match($layoutSource, 'p.y <= TownOffsetY \+ (\d+)').Groups[1].Value
if ($campX - $radius -le $villageEast -and $campY - $radius -le $villageSouth)
{ throw 'Village and automatic raid territory overlap.' }
if ($campX + $radius -ge $mapSize -or $campY + $radius -ge $mapSize)
{ throw 'Raid territory exceeds the world bounds.' }

$people = [regex]::Match($npcSource, '(?s)const Definition people\[\] = \{(.*?)for \(int i = 0;').Groups[1].Value
$outfits = [regex]::Match($renderSource, '(?s)static const Outfit outfits.*?= \{(.*?)const Outfit& outfit').Groups[1].Value
if ([regex]::Matches($people, 'Job::\w+').Count -ne $appearanceCount) { throw 'NPC/appearance count mismatch.' }
if ([regex]::Matches($outfits, '\{\s*\.\d').Count -ne $appearanceCount) { throw 'Outfit palette count mismatch.' }
foreach ($contract in @('attacker.job == Job::Raider', '!WorldLayout::InVillage(target.WorldPosition())',
                       'victim->variant >= 5', 'Action::Defend', 'npc.action == Action::Raid'))
{
    if (-not $npcSource.Contains($contract)) { throw "Missing combat policy: $contract" }
}
if (-not $npcSource.Contains('npc.job != Job::Raider && IsMerchant(*candidate)'))
{ throw 'Raider village trade exclusion missing.' }
if (-not $renderSource.Contains('firstLevel.MapRevision()')) { throw 'Minimap depends on visible terrain.' }
if (-not $levelSource.Contains('std::priority_queue<Visit>')) { throw 'A* navigation contract missing.' }

if ($npcSource.Contains('const int departures[]')) { throw 'Story still progresses by an automatic departure clock.' }
$npcStep = [regex]::Match($npcSource, '(?s)void World::Step\(.*?(?=void World::Update\()').Value
if ($npcStep.Contains('Disclose(&npc)') -or $npcStep.Contains('Transfer(artifact, npc.Id())'))
{ throw 'NPC can still make the player record choice or take unassigned evidence.' }
foreach ($contract in @('World::Objective()', 'World::ObjectiveTarget()', 'World::UpdateQuest()',
                       'ChooseRecord(RecordChoice::Public)', 'recordChoice == RecordChoice::Authority',
                       'recordChoice == RecordChoice::Sold', 'level.DrainFloodedLane()',
                       'level.stats.GainExperience(120)', 'level.stats.GainExperience(40)'))
{
    if (-not $npcSource.Contains($contract)) { throw "Missing player progression contract: $contract" }
}
foreach ($contract in @('const int span = worldMap ? MapSize : 64', 'renderer->TextWidth(',
                       'ReserveWorldLabel(', 'UiPart::QuestMarker', 'worldMap = !worldMap'))
{
    if (-not $renderSource.Contains($contract)) { throw "Missing readable UI contract: $contract" }
}
if (-not $levelSource.Contains('tile.SetVariant(4)')) { throw 'Sluice changes visuals but not shared terrain.' }

$nameDistance = [regex]::Match($renderSource, 'NpcNameDistance\s*=\s*([0-9.]+)f').Groups[1].Value
if ($nameDistance -ne '2.5') { throw 'NPC name visibility distance must be 2.5 tiles.' }
foreach ($contract in @('bool helpVisible = true', 'UiPart::GuideHelp', 'continueHelpPressed',
                       'if (paused || helpVisible)', 'npc->Id() != focusedNpcId', 'QuestMarkerPosition(',
                       'guide.destination', 'guide.interactionKey', 'TravelHint(guide)'))
{
    if (-not $renderSource.Contains($contract)) { throw "Missing first-time guidance contract: $contract" }
}
if (-not $npcSource.Contains('ObjectiveGuide World::Guide() const') -or
    -not $npcSource.Contains('npc = questGiver;'))
{ throw 'Contextual objective guide or quest-giver interaction priority missing.' }

# Keep onboarding readable without reclaiming the large gameplay-covering panels.
foreach ($dimension in @(@('UiFontHeight', 15), @('TextLineHeight', 21),
                         @('QuestPanelWidth', 740), @('QuestPanelHeight', 88),
                         @('HelpPanelWidth', 720), @('HelpPanelHeight', 360)))
{
    $actual = [regex]::Match($renderSource, "\b$($dimension[0])\s*=\s*([0-9.]+)")
    if (-not $actual.Success -or [double]$actual.Groups[1].Value -ne $dimension[1])
    { throw "Compact UI dimension mismatch: $($dimension[0])" }
}
$trackerSource = [regex]::Match($renderSource,
    '(?s)void DrawQuestTracker\(\).*?(?=void DrawGuideHelp\()').Value
$helpSource = [regex]::Match($renderSource,
    '(?s)void DrawGuideHelp\(\).*?(?=void DrawHud\()').Value
if ($trackerSource.Contains('guide.reason') -or -not $helpSource.Contains('guide.reason') -or
    -not $trackerSource.Contains('guide.action') -or -not $trackerSource.Contains('TravelHint(guide)'))
{ throw 'Compact tracker must retain action/navigation and leave detail in H help.' }
if (-not $renderSource.Contains('CreateFontW(-UiFontHeight,') -or
    -not $trackerSource.Contains('QuestPanelWidth, QuestPanelHeight') -or
    -not $helpSource.Contains('HelpPanelWidth, HelpPanelHeight'))
{ throw 'Compact UI dimensions are not used by drawing code.' }
if (-not $renderSource.Contains('DrawSolidRect(430, 672, 820, 80,') -or
    -not $renderSource.Contains('DrawWrapped(32, 650, firstLevel.message, 796, 3)'))
{ throw 'Result message panel must align with the left HUD margin.' }

$bounds = [regex]::Match($levelSource,
    'bool visible = right > -(\d+) && left < (\d+) && bottom > -(\d+) && top < (\d+)')
if (-not $bounds.Success) { throw 'Chunk view bounds missing.' }
$chunksPerSide = $mapSize / $chunkSize
$probes = 0
foreach ($camera in @(@(53.5,68.5), @($campX,$campY), @(31.5,32.5), @(128.5,128.5), @(249.5,249.5)))
{
    for ($y = 0; $y -lt $mapSize; $y++)
    {
        for ($x = 0; $x -lt $mapSize; $x++)
        {
            $screenX = (($x - $y) - ($camera[0] - $camera[1])) * 48
            $screenY = (($x + $y + 1) - ($camera[0] + $camera[1])) * 24
            # Include chimney effects at z=103, not just the ground.
            $isVisible = $screenX -gt -800 -and $screenX -lt 800 -and
                         $screenY -gt -516 -and $screenY -lt (564 + 103)
            if (-not $isVisible) { continue }
            $minX = [Math]::Floor($x / $chunkSize) * $chunkSize
            $minY = [Math]::Floor($y / $chunkSize) * $chunkSize
            $maxX = $minX + $chunkSize
            $maxY = $minY + $chunkSize
            $left = (($minX - $maxY) - ($camera[0] - $camera[1])) * 48
            $right = (($maxX - $minY) - ($camera[0] - $camera[1])) * 48
            $top = (($minX + $minY) - ($camera[0] + $camera[1])) * 24
            $bottom = (($maxX + $maxY) - ($camera[0] + $camera[1])) * 24
            if (-not ($right -gt -[int]$bounds.Groups[1].Value -and $left -lt [int]$bounds.Groups[2].Value -and
                      $bottom -gt -[int]$bounds.Groups[3].Value -and $top -lt [int]$bounds.Groups[4].Value))
            { throw "Visible tile/effect culled with its chunk: $x,$y" }
            $probes++
        }
    }
}
if ($probes -eq 0) { throw 'No visible chunk coverage samples.' }
[pscustomobject]@{
    event = 'open_world_static_contracts'; passed = $true; map_size_tiles = $mapSize
    area_ratio_from_48 = [Math]::Pow($mapSize / 48, 2); chunk_groups = $chunksPerSide * $chunksPerSide
    npc_appearance_count = $appearanceCount; raid_territory_separated = $true
    visible_chunk_probes = $probes; player_driven_story_contracts = $true; readable_ui_contracts = $true
    first_time_guidance_contracts = $true; npc_name_distance_tiles = [double]$nameDistance
    compact_ui_contracts = $true; ui_font_height_pixels = 15; result_panel_left_pixels = 20
    quest_panel_pixels = @(740, 88); help_panel_pixels = @(720, 360)
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
