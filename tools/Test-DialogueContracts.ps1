#requires -Version 7.0
# Source contracts only: no compiler, game build, execution or screen check.
$ErrorActionPreference = 'Stop'
$dialogueSourceRoot = Join-Path (Split-Path $PSScriptRoot -Parent) 'BaseProject_SE/SimpleGame'
$world = Get-Content -Raw (Join-Path $dialogueSourceRoot 'HarborWorld.cpp')
$game = Get-Content -Raw (Join-Path $dialogueSourceRoot 'SimpleGame.cpp')
$header = Get-Content -Raw (Join-Path $dialogueSourceRoot 'HarborWorld.h')

function World-Method([string]$name, [string]$next)
{
    $body = [regex]::Match($world, "(?s)void World::$name\(.*?(?=void World::$next\()")
    if (-not $body.Success) { throw "Missing dialogue method: $name" }
    return $body.Value
}

$talk = World-Method 'Talk' 'Trade'
foreach ($forbidden in @('briefed = true', 'returned = true', 'GainExperience(', 'knowledge |='))
{
    if ($talk.Contains($forbidden)) { throw "Opening E dialogue still performs action: $forbidden" }
}
if (-not $talk.Contains('BeginDialogue(npc)')) { throw 'E does not open the conversation.' }
$advance = World-Method 'AdvanceDialogue' 'CompleteFirstQuest'
if (-not $advance.Contains('!dialogue.pages[dialogue.page].choices.empty()'))
{ throw 'E/Enter can skip past a choice.' }
$choose = World-Method 'ChooseDialogue' 'BeginDialogue'
foreach ($contract in @('choice >= dialogue.pages[dialogue.page].choices.size()',
                       'selected.effect == DialogueEffect::AcceptQuest && !briefed',
                       'selected.effect == DialogueEffect::CompleteQuest',
                       'selected.effect == DialogueEffect::ShareKnowledge', 'EndDialogue()',
                       'person->health <= 0', 'source->WorldPosition()'))
{
    if (-not $choose.Contains($contract)) { throw "Missing choice safety: $contract" }
}
$complete = World-Method 'CompleteFirstQuest' 'ChooseDialogue'
if (-not $complete.Contains('returned || questStep != 5') -or
    [regex]::Matches($world, 'level.stats.GainExperience\(120\)').Count -ne 1)
{ throw 'First quest reward is not guarded/centralized.' }
$build = World-Method 'BeginDialogue' 'Talk'
foreach ($contract in @('npc->variant == 1', 'npc->variant == 2', 'npc->variant == 3', 'npc->variant == 4',
                       'Job::Hunter', 'Job::Merchant', 'Job::Raider', 'Job::Scholar',
                       'DialogueEffect::AcceptQuest', 'DialogueEffect::CompleteQuest',
                       'DialogueEffect::ShareKnowledge', 'npc->variant == 0 && returned'))
{
    if (-not $build.Contains($contract)) { throw "Missing contextual conversation: $contract" }
}
foreach ($contract in @('DialogueChoice', 'DialoguePage', 'DialogueState', 'const DialogueState& Conversation()'))
{
    if (-not $header.Contains($contract)) { throw "Missing dialogue data: $contract" }
}
foreach ($contract in @('UiPart::Dialogue', 'void DrawStoryIntro()', 'introPage < 2', 'skipIntroPressed',
                       'void DrawDialogue()', 'DrawWrapped(32, 584, page.text, 796, 3)',
                       "key >= '1' && key <= '3'", "key == 'e' || key == 13", 'dialogueClosePressed',
                       'ClearGameplayInput()', 'harbor.Conversation().Active()',
                       'worldMonitor.Tick(harbor, paused || helpVisible || harbor.Conversation().Active(), dt)'))
{
    if (-not $game.Contains($contract)) { throw "Missing dialogue UI/input behavior: $contract" }
}
foreach ($source in @($game, $world))
{
    if ([regex]::IsMatch($source, '[^\x00-\x7f]'))
    { throw 'Korean C++ text must use Unicode escapes to remain source-code-page independent.' }
}
[pscustomobject]@{
    event = 'dialogue_static_contracts'; passed = $true; intro_pages = 3
    e_opens_without_quest_commit = $true; explicit_accept_and_reward = $true
    contextual_main_npcs = 5; dialogue_is_actor_panel = $true
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
