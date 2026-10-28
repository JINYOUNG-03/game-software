#requires -Version 7.0
param(
    [string[]]$Path,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
$sessions = @{}
$invalidLines = 0
$seen = [System.Collections.Generic.HashSet[string]]::new()

function Add-ProfileRecord([hashtable]$record)
{
    if ($record.schema_version -notin @(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) -or -not $record.session_id) { return }
    $sessionId = [string]$record.session_id
    if (-not $sessions.ContainsKey($sessionId))
    {
        $sessions[$sessionId] = @{
            frames = 0; gpuFrames = 0; cpu = @{}; gpu = @{}; counters = @{};
            intervals = [System.Collections.Generic.List[double]]::new()
            worst = @(); dropped = 0; failures = $false; pausedFrames = 0; metadata = $null
            worldSamples = 0; lastWorld = $null
        }
    }
    $session = $sessions[$sessionId]
    if ($record.event -eq 'session') { $session.metadata = $record; return }
    if ($record.event -eq 'world_state')
    {
        if (-not $seen.Add("$sessionId/world_state/$($record.frame_id)")) { return }
        $session.worldSamples++
        $session.lastWorld = $record.world
        return
    }
    if ($record.event -notin @('frame', 'gpu_frame')) { return }
    if (-not $seen.Add("$sessionId/$($record.event)/$($record.frame_id)")) { return }
    if ($record.event -eq 'gpu_frame')
    {
        $session.gpuFrames++
        foreach ($key in $record.gpu_interval_ms.Keys)
        {
            if (-not $session.gpu.ContainsKey($key)) { $session.gpu[$key] = @{sum = 0.0; count = 0} }
            $session.gpu[$key].sum += [double]$record.gpu_interval_ms[$key]
            $session.gpu[$key].count++
        }
        return
    }
    $session.frames++
    if ($record.scene_state.paused) { $session.pausedFrames++ }
    $session.dropped = [Math]::Max($session.dropped, [long]$record.log_dropped_total)
    $session.failures = $session.failures -or $record.log_file_failed
    foreach ($key in $record.cpu_ms.Keys)
    {
        if (-not $session.cpu.ContainsKey($key)) { $session.cpu[$key] = 0.0 }
        $session.cpu[$key] += [double]$record.cpu_ms[$key]
    }
    foreach ($key in $record.counters.Keys)
    {
        if (-not $session.counters.ContainsKey($key)) { $session.counters[$key] = 0.0 }
        $session.counters[$key] += [double]$record.counters[$key]
    }
    if ($record.frame_interval_ms -gt 0)
    {
        $session.intervals.Add([double]$record.frame_interval_ms)
        $session.worst += [pscustomobject]@{
            frame_id = $record.frame_id; frame_interval_ms = $record.frame_interval_ms;
            cpu_ms = $record.cpu_ms; counters = $record.counters; scene_state = $record.scene_state
        }
        $session.worst = @($session.worst | Sort-Object frame_interval_ms -Descending | Select-Object -First 10)
    }
}

if ($SelfTest)
{
    @(
        '{"schema_version":1,"session_id":"fixture","event":"frame","frame_id":1,"frame_interval_ms":10,"cpu_ms":{"update":2},"counters":{"draw_calls":8},"scene_state":{"paused":false},"log_dropped_total":0,"log_file_failed":false}',
        '{"schema_version":10,"session_id":"fixture","event":"frame","frame_id":2,"frame_interval_ms":30,"cpu_ms":{"update":4},"counters":{"draw_calls":12},"scene_state":{"paused":false},"log_dropped_total":1,"log_file_failed":false}',
        '{"schema_version":1,"session_id":"fixture","event":"gpu_frame","frame_id":1,"gpu_interval_ms":{"world":3}}',
        '{"schema_version":2,"session_id":"fixture","event":"world_state","frame_id":2,"world":{"world_minute":420,"npcs":[{"actor_id":10,"job":"raider","coins":35}]}}'
    ) | ForEach-Object { Add-ProfileRecord ($_ | ConvertFrom-Json -AsHashtable) }
}
else
{
    if (-not $Path) { throw 'Pass JSONL paths from one or more sessions, or use -SelfTest.' }
    foreach ($pattern in $Path)
    {
        foreach ($file in Get-ChildItem -Path $pattern -File)
        {
            foreach ($line in [System.IO.File]::ReadLines($file.FullName))
            {
                if ([string]::IsNullOrWhiteSpace($line)) { continue }
                try { $record = ConvertFrom-Json -InputObject $line -AsHashtable }
                catch { $invalidLines++; continue }
                Add-ProfileRecord $record
            }
        }
    }
}
$reports = @(
    foreach ($sessionId in ($sessions.Keys | Sort-Object))
    {
        $session = $sessions[$sessionId]
        if (-not $session.frames) { continue }
        $cpu = [ordered]@{}
        foreach ($key in ($session.cpu.Keys | Sort-Object)) { $cpu[$key] = $session.cpu[$key] / $session.frames }
        $counters = [ordered]@{}
        foreach ($key in ($session.counters.Keys | Sort-Object)) { $counters[$key] = $session.counters[$key] / $session.frames }
        $gpu = [ordered]@{}
        foreach ($key in ($session.gpu.Keys | Sort-Object)) { $gpu[$key] = $session.gpu[$key].sum / $session.gpu[$key].count }
        $sorted = @($session.intervals | Sort-Object)
        $sum = ($sorted | Measure-Object -Sum).Sum
        [pscustomobject]@{
            session_id = $sessionId; metadata = $session.metadata; frame_samples = $session.frames
            gpu_frame_samples = $session.gpuFrames; paused_frame_samples = $session.pausedFrames
            fps = $(if ($sum -gt 0) { 1000 * $sorted.Count / $sum } else { $null })
            frame_ms_p95 = $(if ($sorted.Count) { $sorted[[Math]::Ceiling($sorted.Count * .95) - 1] } else { $null })
            frame_ms_max = $(if ($sorted.Count) { $sorted[-1] } else { $null })
            cpu_ms_mean = $cpu; counters_mean = $counters
            gpu_interval_ms_mean = $(if ($session.gpuFrames) { $gpu } else { $null })
            log_dropped_total = $session.dropped; log_file_failed = $session.failures
            world_state_samples = $session.worldSamples; last_world_state = $session.lastWorld
            worst_frames = $session.worst
        }
    }
)
if ($SelfTest)
{
    if ($reports.Count -ne 1 -or $reports[0].fps -ne 50 -or $reports[0].frame_ms_p95 -ne 30 -or
        $reports[0].cpu_ms_mean.update -ne 3 -or $reports[0].counters_mean.draw_calls -ne 10 -or
        $reports[0].gpu_interval_ms_mean.world -ne 3 -or $reports[0].log_dropped_total -ne 1 -or
        $reports[0].world_state_samples -ne 1 -or $reports[0].last_world_state.npcs[0].coins -ne 35)
    { throw 'Profile analyzer self-test failed.' }
}
[pscustomobject]@{
    schema_version = 10; event = 'profile_analysis'; invalid_json_lines = $invalidLines
    self_test = [bool]$SelfTest
    notes = @('CPU scopes are inclusive; do not sum nested scopes.',
              'GPU timestamp intervals can include CPU submission starvation; they are not GPU utilization.',
              'Rotating logs retain only recent history. Missing GPU events are unavailable, not zero.',
              'Frame interval describes the interval ending at this frame; CPU scopes describe this frame.')
    sessions = $reports
} | ConvertTo-Json -Depth 12
