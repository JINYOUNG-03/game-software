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

$talk = World-Method 'Talk' 'InteractSite'
foreach ($forbidden in @('briefed = true', 'returned = true', 'GainExperience(', 'knowledge |='))
{
    if ($talk.Contains($forbidden)) { throw "Opening E dialogue still performs action: $forbidden" }
}
if (-not $talk.Contains('BeginDialogue(npc)')) { throw 'E does not open the conversation.' }
$advance = World-Method 'AdvanceDialogue' 'CompleteFirstQuest'
if (-not $advance.Contains('!dialogue.pages[dialogue.page].choices.empty()'))
{ throw 'E/Enter can skip past a choice.' }
$choose = World-Method 'ChooseDialogue' 'BeginHideJob'
foreach ($contract in @('choice >= dialogue.pages[dialogue.page].choices.size()',
                       'selected.effect == DialogueEffect::AcceptQuest && !briefed',
                       'selected.effect == DialogueEffect::CompleteQuest',
                       'selected.effect == DialogueEffect::ShareKnowledge', 'EndDialogue()',
                       'person->health <= 0', 'source->WorldPosition()'))
{
    if (-not $choose.Contains($contract)) { throw "Missing choice safety: $contract" }
}
$complete = World-Method 'CompleteFirstQuest' 'ChooseDialogue'
if (-not $complete.Contains('returned || questStep != ReportSluice') -or
    [regex]::Matches($world, 'level.stats.GainExperience\(120\)').Count -ne 1)
{ throw 'First quest reward is not guarded/centralized.' }
$hideJob = World-Method 'BeginHideJob' 'BeginLedgerAccount'
foreach ($forbidden in @('hideJob =', 'level.stats.hides -=', 'level.stats.coins +='))
{
    if ($hideJob.Contains("$forbidden ")) { throw "Reading the hide order changes game state: $forbidden" }
}
foreach ($contract in @('selected.effect == DialogueEffect::AcceptHideJob && hideJob == HideJob::Offered',
                       'selected.effect == DialogueEffect::DeliverHideJob && hideJob == HideJob::Accepted',
                       'dialogue.source == sites[2] && level.stats.hides >= HideOrder',
                       'level.stats.hides -= HideOrder', 'level.stats.coins += HideOrder * HidePayPerPiece',
                       'hideJob = HideJob::Delivered',
                       'selected.effect == DialogueEffect::AcceptQuest && !briefed && hideJob == HideJob::Delivered'))
{
    if (-not $choose.Contains($contract)) { throw "Missing explicit hide order transaction: $contract" }
}
$site = World-Method 'InteractSite' 'Reveal'
if (-not $site.Contains('questStep == InspectDrain') -or
    -not $site.Contains('drainInspected && !Holder(artifact)') -or
    $site.Contains('briefed = true') -or
    -not $world.Contains('item.SetVisible(false)'))
{ throw 'Evidence must stay hidden and uncollectable until the drainage is investigated.' }
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

$accounts = [regex]::Match($world, '(?s)constexpr LedgerAccount\s+LedgerAccounts\s*\[\]\s*=\s*\{(.*?)\};').Groups[1].Value
$speakers = [regex]::Matches($accounts, '\{\s*(\d+),').ForEach({ $_.Groups[1].Value }) -join ','
if ($speakers -ne '1,4,2,3') { throw 'Ledger inquiry must cover the family, guarantor, investigator and broker.' }
$accountDialogue = World-Method 'BeginLedgerAccount' 'BeginDialogue'
if (-not $accountDialogue.Contains('{account.question, 2, DialogueEffect::None}') -or
    $accountDialogue.Contains('ledgerAccountsHeard.set(') -or
    -not $choose.Contains('selected.effect == DialogueEffect::RecordLedgerAccount && questStep == HearLedgerAccounts') -or
    [regex]::Matches($world, 'ledgerAccountsHeard\.set\(index\)').Count -ne 1)
{ throw 'Hearing or leaving a dialogue cannot record an account before an explicit choice.' }
if (-not $site.Contains('questStep == HearLedgerAccounts') -or
    -not $world.Contains('!CanHearFrom(Person(LedgerAccounts[index].person))') -or
    -not $world.Contains('lines.push_back(LedgerAccounts[i].journal)') -or
    -not $talk.Contains('level.Scene().Find(ObjectiveTarget())'))
{ throw 'An unavailable speaker needs a board fallback and the journal must retain recorded accounts.' }
if (-not (World-Method 'Reveal' 'Log').Contains('else if (!ledgerAccountsHeard.all())') -or
    -not (World-Method 'ChooseRecord' 'Initialize').Contains('|| !ledgerAccountsHeard.all()'))
{ throw 'Public disclosure and record disposition require all four accounts.' }

$stops = [regex]::Match($world, '(?s)constexpr TrailStop\s+TrailStops\[\]\s*=\s*\{(.*?)\};').Groups[1].Value
$trailSites = [regex]::Matches($stops, '\{\s*(\d+),').ForEach({ $_.Groups[1].Value }) -join ','
if ($trailSites -ne '4,5,6,8') { throw 'Trail must visit Solbaram, Gingil, Eunsol and Deulsaem in order.' }
$trailDialogue = World-Method 'BeginTrailDialogue' 'ApplyTrailChoice'
foreach ($forbidden in @('GainExperience(', '++trailClues', 'trailNotice = '))
{
    if ($trailDialogue.Contains($forbidden)) { throw "Opening the trail board changes progress: $forbidden" }
}
$trailCommit = World-Method 'ApplyTrailChoice' 'Talk'
foreach ($contract in @('!returned || trailNotice != TrailNotice::None || board != sites[TrailStops[trailClues].site]',
                       'level.ClearLine(hero, position)', 'trailClues < TrailClueCount', '++trailClues',
                       'effect != DialogueEffect::RecordTrailClue', 'DialogueEffect::PostTrailNames',
                       'DialogueEffect::PostTrailRoute', 'UpdateQuest()'))
{
    if (-not $trailCommit.Contains($contract)) { throw "Missing trail commit guard: $contract" }
}
if (-not $choose.Contains('ApplyTrailChoice(dialogue.source, selected.effect)') -or
    [regex]::Matches($world, 'level.stats.GainExperience\(40\)').Count -ne 1 -or
    [regex]::Matches($world, 'level.stats.GainExperience\(100\)').Count -ne 1)
{ throw 'Trail progress and one-time rewards must be committed by explicit dialogue choice.' }
if (-not (World-Method 'InteractSite' 'Reveal').Contains('landmark.Id() == ObjectiveTarget()') -or
    -not $world.Contains('trailNotice == TrailNotice::None ? sites[TrailStops[trailClues].site] : InvalidActorId'))
{ throw 'The marked board and interactive board must use the same route.' }
if (-not $build.Contains('npc->knowledge & (Knowledge::TrailNames | Knowledge::TrailRoute)') -or
    -not (World-Method 'SpreadRumors' 'UpdateVillageStatistics').Contains('level.ClearLine(npc->WorldPosition(), board)'))
{ throw 'Posted rumors must start in sight of the board and drive NPC dialogue through NPC knowledge.' }

foreach ($contract in @('UiPart::Dialogue', 'void DrawStoryIntro()', 'introPage < 2', 'skipIntroPressed',
                       'void DrawDialogue()', 'DrawWrapped(32, 584, page.text, 796, 3)',
                       "key >= '1' && key <= '3'", "key == 'e' || key == 13", 'dialogueClosePressed',
                       'ClearGameplayInput()', 'harbor.Conversation().Active()',
                       'worldMonitor.Tick(harbor, WorldPaused(), dt)'))
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
    hide_order_is_explicit = $true; drain_discovery_is_separate = $true
    ledger_accounts = 4; ledger_disposition_requires_accounts = $true
    trail_boards_in_order = 4; trail_notice_choices = 2
    game_build_performed = $false; game_execution_performed = $false
} | ConvertTo-Json
