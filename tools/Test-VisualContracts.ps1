#requires -Version 7.0
# Static source contracts and projection/edge-mask arithmetic. No game/shader compilation or screen test.
$ErrorActionPreference = 'Stop'
$visualRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$game = Get-Content -Raw (Join-Path $visualRoot 'SimpleGame.cpp')
$level = Get-Content -Raw (Join-Path $visualRoot 'LevelOne.cpp')
$actor = Get-Content -Raw (Join-Path $visualRoot 'Actor.h')
$renderer = Get-Content -Raw (Join-Path $visualRoot 'Renderer.cpp')
$header = Get-Content -Raw (Join-Path $visualRoot 'Renderer.h')
$batch = Get-Content -Raw (Join-Path $visualRoot 'RendererBatch.cpp')
$models = Get-Content -Raw (Join-Path $visualRoot 'RendererModels.cpp')
$vs = Get-Content -Raw (Join-Path $visualRoot 'Shaders/SolidRect.vs')
$fs = Get-Content -Raw (Join-Path $visualRoot 'Shaders/SolidRect.fs')
$post = Get-Content -Raw (Join-Path $visualRoot 'Shaders/Post.fs')

foreach ($contract in @('surfaceData', 'shadowHeight', 'WarmLight', 'HarborMist'))
{
    if (-not $actor.Contains($contract)) { throw "Missing Actor presentation data: $contract" }
}
foreach ($contract in @('u_WorldView', 'v_World', 'a_Surface', 'u_WorldShading'))
{
    if (-not $vs.Contains($contract)) { throw "Missing world-coordinate shader input: $contract" }
}
foreach ($match in [regex]::Matches($fs, '(?m)^(?:flat )?in (\w+) (\w+);'))
{
    if (-not [regex]::IsMatch($vs, "(?:flat )?out $($match.Groups[1].Value) $($match.Groups[2].Value);"))
    { throw "Shader interface mismatch: $($match.Value)" }
}
foreach ($contract in @('edgeDistance(cell, int(v_Surface.x))', 'u_Lights[8]', 'v_Surface.w',
                       'vec2 cell = clamp(vec2(p.x - p.y + .5, 1.5 - p.x - p.y)', 'v_Material == 13', 'v_Material == 16',
                       'v_Material == 17', 'v_Material == 19', 'foamLine', 'rainCell'))
{
    if (-not $fs.Contains($contract)) { throw "Missing visual material contract: $contract" }
}
foreach ($contract in @('offsetof(Instance, surface)', 'i < 6',
                       'std::copy(m_SurfaceData.begin(), m_SurfaceData.end(), instance.surface)',
                       'glUniform4fv(m_BatchLocations.lights, 8, m_Lights.data())'))
{
    if (-not $batch.Contains($contract)) { throw "Missing instanced surface/light binding: $contract" }
}
if (-not $header.Contains('sizeof(float) * 21') -or -not $header.Contains('float surface[4]'))
{ throw 'CPU/GPU instance layout mismatch.' }
foreach ($contract in @('LoadResourceSprites(directory + L"Resources")', 'kind < 20', 'SetSurfaceData(actor.surfaceData)',
                       'UpdateWorldLights()', 'count < 8', 'ActorEffect::WarmLight', 'ActorEffect::HarborMist'))
{
    if (-not $game.Contains($contract)) { throw "Missing graphics integration: $contract" }
}
if (-not $models.Contains('revision != 2') -or -not $models.Contains('crc != Crc32(png)') -or
    -not $models.Contains('GUID_WICPixelFormat32bppRGBA')) { throw 'Resource atlas needs version/CRC/RGBA validation.' }
if ([regex]::Matches($level, 'RefreshTerrainAppearance\(\);').Count -ne 2)
{ throw 'Terrain appearance must update after generation and draining, not every frame.' }
foreach ($contract in @('waterMask |= 1 << edge', 'roadMask |= 1 << edge', 'wetLane ? 1.0f : 0.0f',
                       'Perf::Cpu::TerrainAppearance', 'Perf::Cpu::SceneryBuild', 'BuildHarborScenery()',
                       'At(int(boat.x), int(boat.y)).terrain == Terrain::Water'))
{
    if (-not $level.Contains($contract)) { throw "Missing terrain/scenery contract: $contract" }
}
$scenery = [regex]::Match($level,
    '(?s)void FirstLevel::BuildHarborScenery\(\).*?(?=void FirstLevel::UpdateVisibleChunks\()').Value
if ($scenery.Contains('Carve(') -or $scenery.Contains('true)') -or $scenery.Contains('SetVariant('))
{ throw 'Decoration must not carve roads, block paths or change gameplay terrain.' }
$drawModel = [regex]::Match($models,
    '(?s)void Renderer::DrawModel\(.*?(?=void Renderer::DrawResourceSwordTrail)').Value
if ([regex]::Matches($drawModel, '\bQueueRect\(').Count -ne 1 -or $drawModel.Contains('glDraw'))
{ throw 'Each imported model must submit one quad without an immediate draw call.' }
$endWorld = [regex]::Match($renderer, '(?s)void Renderer::EndWorld\(.*?(?=void Renderer::DrawQuad\()').Value
if ([regex]::Matches($endWorld, '\bDrawPost\(').Count -ne 4 -or
    -not $endWorld.Contains('m_WorldShading = false'))
{ throw 'Lighting must not add post passes or shade UI.' }
if (-not $post.Contains('uniform float u_Time;')) { throw 'Atmospheric postprocessing not connected.' }

# The inverse shader projection must reconstruct one shared world coordinate under camera movement.
$shaderWidth = [double][regex]::Match($vs, 'delta.x / ([0-9.]+)').Groups[1].Value
$shaderHeight = [double][regex]::Match($vs, 'delta.y / ([0-9.]+)').Groups[1].Value
if ($shaderWidth -ne 96 -or $shaderHeight -ne 48)
{ throw 'Shader tile scale differs from the shared quarter-view projection.' }
foreach ($camera in @(@(53.5, 68.5), @(53.57, 68.41), @(128.1, 129.8), @(249.5, 249.5)))
{
    foreach ($point in @(@(54.0, 68.25), @(53.999, 69.0), @(101.4, 112.7), @(210.5, 204.5)))
    {
        $sx = (($point[0] - $point[1]) - ($camera[0] - $camera[1])) * 48
        $sy = (($point[0] + $point[1]) - ($camera[0] + $camera[1])) * 24
        $rx = $camera[0] + $sx / $shaderWidth + $sy / $shaderHeight
        $ry = $camera[1] + $sy / $shaderHeight - $sx / $shaderWidth
        if ([Math]::Abs($rx - $point[0]) -gt .00001 -or [Math]::Abs($ry - $point[1]) -gt .00001)
        { throw 'Camera-stable surface projection failed.' }
    }
}
# Read direction order from the production C++ rather than maintaining a separate edge convention.
$appearance = [regex]::Match($level,
    '(?s)void FirstLevel::RefreshTerrainAppearance\(\).*?(?=void FirstLevel::BuildHarborScenery\()').Value
$dx = [regex]::Match($appearance, 'const int dx\[\] = \{([^}]+)\}').Groups[1].Value.Split(',') | ForEach-Object { [int]$_ }
$dy = [regex]::Match($appearance, 'const int dy\[\] = \{([^}]+)\}').Groups[1].Value.Split(',') | ForEach-Object { [int]$_ }
if (($dx -join ',') -ne '-1,1,0,0' -or ($dy -join ',') -ne '0,0,-1,1')
{ throw 'CPU shoreline bits differ from shader edgeDistance convention.' }
$mask = 0
for ($i = 0; $i -lt 4; $i++)
{
    # A water cell with only its west neighbour on land.
    if ($dx[$i] -eq -1) { $mask = $mask -bor (1 -shl $i) }
}
if ($mask -ne 1) { throw 'West shoreline bit contract failed.' }
[pscustomobject]@{
    event = 'visual_static_contracts'; passed = $true; world_coordinate_projection_probes = 16
    maximum_lights = 8; post_passes = 4; resource_atlas_format = 2
    game_build_performed = $false; game_execution_performed = $false; shader_compilation_performed = $false
} | ConvertTo-Json
