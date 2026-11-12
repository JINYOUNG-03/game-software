#requires -Version 7.0
# Lightweight source checks only: not a compiler, game build, execution or screen test.
$ErrorActionPreference = 'Stop'
$sourceCheckRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$checkedFiles = @('Actor.h', 'Actor.cpp', 'WorldLayout.h', 'CharacterPresentation.h', 'InventoryPresentation.h', 'LevelOne.h', 'LevelOne.cpp', 'GameSave.h', 'GameSave.cpp',
                  'HarborWorld.h', 'HarborWorld.cpp', 'HarborTrade.cpp', 'Trade.h', 'TradePresentation.h', 'Renderer.h', 'Renderer.cpp', 'RendererModels.cpp', 'RendererBatch.cpp',
                  'SimpleGame.cpp', 'GlfwInput.h', 'KoreanFont.h', 'KoreanFont.cpp', 'Profiler.h', 'Profiler.cpp', 'WorldMonitor.h', 'WorldMonitor.cpp')
$ignoredTokens = @'
(?s)/\*.*?\*/|//[^\r\n]*|L?"(?:\\.|[^"\\])*"|L?'(?:\\.|[^'\\])*'
'@
foreach ($sourceFile in $checkedFiles)
{
    $sourceText = Get-Content -Raw (Join-Path $sourceCheckRoot $sourceFile)
    if ([regex]::IsMatch($sourceText, '\b(void|bool|float)\s+\w+::\w+\s+\1\b'))
    { throw "Duplicate function prefix: $sourceFile" }
    foreach ($token in [regex]::Matches($sourceText, $ignoredTokens))
    {
        if (($token.Value.StartsWith('"') -or $token.Value.StartsWith('L"')) -and
            $token.Value.Contains("`n"))
        { throw "Unexpected multiline string literal: $sourceFile" }
    }
    $strippedText = [regex]::Replace($sourceText, $ignoredTokens, '')
    $delimiters = [Collections.Generic.Stack[char]]::new()
    foreach ($character in $strippedText.ToCharArray())
    {
        if ($character -in @('{', '(', '[')) { $delimiters.Push($character); continue }
        if ($character -notin @('}', ')', ']')) { continue }
        $expected = switch ($character) { '}' { '{' } ')' { '(' } ']' { '[' } }
        if ($delimiters.Count -eq 0 -or $delimiters.Pop() -ne $expected)
        { throw "Unbalanced delimiter: $sourceFile" }
    }
    if ($delimiters.Count -ne 0) { throw "Unclosed delimiter: $sourceFile" }
}

$profilerHeader = Get-Content -Raw (Join-Path $sourceCheckRoot 'Profiler.h')
$profilerSource = Get-Content -Raw (Join-Path $sourceCheckRoot 'Profiler.cpp')
foreach ($schemaPair in @(@('Cpu', 'CpuNames'), @('Counter', 'CounterNames')))
{
    $enumBody = [regex]::Match($profilerHeader, "(?s)enum class $($schemaPair[0])\s*\{(.*?)\}").Groups[1].Value
    $arrayBody = [regex]::Match($profilerSource, "(?s)$($schemaPair[1])\[\]\s*=\s*\{(.*?)\};").Groups[1].Value
    $enumCount = [regex]::Matches($enumBody, '\b[A-Za-z_]\w*\b').Count - 1
    $keyCount = [regex]::Matches($arrayBody, '"[a-z_]+"').Count
    if ($enumCount -ne $keyCount) { throw "Profiler enum/key mismatch: $($schemaPair[0])" }
}
$worldSource = (Get-Content -Raw (Join-Path $sourceCheckRoot 'HarborWorld.cpp')) +
               (Get-Content -Raw (Join-Path $sourceCheckRoot 'HarborTrade.cpp'))
$definitions = [regex]::Matches($worldSource, '(?m)^\s+(?:void|bool|ActorId|Npc\*|Actor\*|std::string|std::wstring|std::vector<std::wstring>)\s+World::(\w+)\(')
$duplicateMethods = $definitions | ForEach-Object { $_.Groups[1].Value } | Group-Object | Where-Object Count -gt 1
if ($duplicateMethods) { throw 'Duplicate World method definition.' }

$saveSource = Get-Content -Raw (Join-Path $sourceCheckRoot 'GameSave.cpp')
$load = $saveSource.Substring($saveSource.IndexOf('GameSave::Result GameSave::Load('))
$restoreAt = $load.IndexOf('Restore(state, level, world)')
foreach ($guard in @('version != SaveVersion', 'PayloadChecksum(payload) != checksum', '!decoded', '!state.Valid()'))
{
    if ($load.IndexOf($guard) -lt 0 -or $load.IndexOf($guard) -ge $restoreAt)
    { throw "Save slot validation must finish before replacing the current game: $guard" }
}
$replaceAt = $saveSource.IndexOf('MoveFileExW(temporary.c_str(), path.c_str()')
if ($replaceAt -lt 0 -or $saveSource.IndexOf('FlushFileBuffers(file)') -lt 0 -or
    $saveSource.IndexOf('FlushFileBuffers(file)') -ge $replaceAt)
{ throw 'The prior slot must survive failures writing or flushing the temporary file.' }
$restore = [regex]::Match($saveSource, '(?s)void GameSave::Restore\(.*?(?=GameSave::Result GameSave::Save)').Value
foreach ($operation in @('scene.Reset()', 'level.Generate(state.seed)', 'world.Initialize()', 'level.stats = state.stats',
                         'level.DrainFloodedLane()', 'State::Resident::Values(*npc)', 'ids[npc->variant] = npc->Id()',
                         'world.Transfer(id, actorFor(artifact.owner))', 'world.UpdateQuest()'))
{
    if (-not $restore.Contains($operation)) { throw "Missing checkpoint restoration: $operation" }
}
if ($restore -match '(?:OpenGate|ChooseRecord|CompleteFirstQuest|GainExperience)\(')
{ throw 'Restoration may not replay quest choices or award rewards.' }
$gameSource = Get-Content -Raw (Join-Path $sourceCheckRoot 'SimpleGame.cpp')
$keyCallback = [regex]::Match($gameSource, '(?s)void WindowKey\(.*?(?=void WindowMouse\()').Value
if (-not $keyCallback.Contains('GLFW_KEY_F5 || key == GLFW_KEY_F9') -or
    $keyCallback -match 'GameSave::(?:Save|Load)\(' -or
    -not $gameSource.Contains('UpdateCheckpoint(request)'))
{ throw 'Checkpoint input must queue work for Update instead of rebuilding inside the GLFW callback.' }

[pscustomobject]@{
    event = 'source_static_contracts'; passed = $true; files_checked = $checkedFiles.Count
    balanced_delimiters = $true; profiler_enum_keys_match = $true
    checkpoint_preflight = $true; checkpoint_no_reward_replay = $true
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
