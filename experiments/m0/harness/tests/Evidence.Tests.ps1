$evidenceModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
$evidenceSchemaRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas'
$evidenceSchedulePath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schedule.v1.json'
$evidenceSchedule = Get-Content -Raw -LiteralPath $evidenceSchedulePath | ConvertFrom-Json -Depth 100
$expectedCommonInputs = [ordered]@{
    protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
    harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
}

Test-Case 'The secure evidence contract exists before raw evidence is written' {
    Assert-True (Test-Path -LiteralPath $evidenceModulePath -PathType Leaf) 'M0E001 M0.Evidence.psm1 is missing.'
    foreach ($schemaName in @('event.schema.json', 'run.schema.json', 'batch.schema.json')) {
        Assert-True (Test-Path -LiteralPath (Join-Path $evidenceSchemaRoot $schemaName) -PathType Leaf) "M0E001 $schemaName is missing."
    }
}

Import-Module $evidenceModulePath -Force -ErrorAction Stop

# Keep the pre-writer scenarios terse while exercising the mandatory EvidenceRoot API.
function Add-M0EvidenceEvent {
    [CmdletBinding()]
    param(
        [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Event,
        [Parameter(Mandatory)] [string] $SchemaPath
    )
    $root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { Split-Path ([IO.Path]::GetFullPath($LiteralPath)) -Parent } else { $EvidenceRoot }
    return M0.Evidence\Add-M0EvidenceEvent -EvidenceRoot $root -LiteralPath $LiteralPath -Event $Event -SchemaPath $SchemaPath
}

function Complete-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] $SealEvent,
        [Parameter(Mandatory)] $RunRecord,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [string] $CrashAfterBoundary = ''
    )
    $root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { Split-Path ([IO.Path]::GetFullPath($RunPath)) -Parent } else { $EvidenceRoot }
    return M0.Evidence\Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $RawPath -RunPath $RunPath -SealEvent $SealEvent -RunRecord $RunRecord -SchemaRoot $SchemaRoot -CrashAfterBoundary $CrashAfterBoundary
}

function Assert-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )
    $root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { Split-Path ([IO.Path]::GetFullPath($RunPath)) -Parent } else { $EvidenceRoot }
    return M0.Evidence\Assert-M0EvidenceRun -EvidenceRoot $root -RawPath $RawPath -RunPath $RunPath -SchemaRoot $SchemaRoot
}

function Get-M0EvidenceCandidateDigests {
    [CmdletBinding()]
    param(
        [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetPath,
        [Parameter(Mandatory)] [string] $CandidateCode,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [switch] $RequireClean
    )
    $root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { Split-Path ([IO.Path]::GetFullPath($SnapshotSetPath)) -Parent } else { $EvidenceRoot }
    return M0.Evidence\Get-M0EvidenceCandidateDigests -EvidenceRoot $root -SnapshotSetPath $SnapshotSetPath -CandidateCode $CandidateCode -SchemaRoot $SchemaRoot -RequireClean:$RequireClean
}

function Assert-M0EvidenceBatch {
    [CmdletBinding()]
    param(
        [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $BatchPath,
        [Parameter(Mandatory)] [string] $SnapshotSetPath,
        [Parameter(Mandatory)] [string] $SchedulePath,
        [Parameter(Mandatory)] $ExpectedCommonInputs,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )
    $root = if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) { Split-Path ([IO.Path]::GetFullPath($SnapshotSetPath)) -Parent } else { $EvidenceRoot }
    return M0.Evidence\Assert-M0EvidenceBatch -EvidenceRoot $root -BatchPath $BatchPath -SnapshotSetPath $SnapshotSetPath -SchedulePath $SchedulePath -ExpectedCommonInputs $ExpectedCommonInputs -SchemaRoot $SchemaRoot
}

$evidenceMarkersModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Markers.psm1'
Import-Module $evidenceMarkersModulePath -Force -ErrorAction Stop
$evidenceMarkerCatalog = (Assert-M0MarkerArtifacts -ProtocolRoot (Split-Path $evidenceSchemaRoot -Parent)).catalog

function Invoke-M0EvidenceChildScript {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Source
    )

    $scriptPath = Join-Path $TestTempRoot "$Name.ps1"
    [IO.File]::WriteAllText($scriptPath, $Source, [Text.UTF8Encoding]::new($false))
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $scriptPath)) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($startInfo)
    try {
        $output = $child.StandardOutput.ReadToEnd() + $child.StandardError.ReadToEnd()
        $child.WaitForExit()
        return [pscustomobject]@{ exit_code = $child.ExitCode; output = $output }
    }
    finally {
        $child.Dispose()
    }
}

function Copy-EvidenceTestObject {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100)
}

function New-EvidenceTestEvent {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [int] $Attempt = 1,
        [Parameter(Mandatory)] [int] $Sequence,
        [AllowNull()] $PreviousHash,
        [Parameter(Mandatory)] [string] $EventName,
        [Parameter(Mandatory)] $Payload,
        [string] $ScenarioId = 'process-cold',
        [ValidateRange(0, 2)] [int] $TemporalPositionOverride = 0,
        [ValidateRange(0, 22)] [int] $TemporalBlockOverride = 0
    )

    $scheduledBlock = @($evidenceSchedule.blocks | Where-Object block -eq $Attempt)[0]
    $scheduledPosition = [Array]::IndexOf([object[]]@($scheduledBlock.candidate_order), $CandidateCode) + 1
    $temporalPosition = if ($TemporalPositionOverride -eq 0) { $scheduledPosition } else { $TemporalPositionOverride }
    $temporalBlock = if ($TemporalBlockOverride -eq 0) { $Attempt } else { $TemporalBlockOverride }
    $runStartQpc = ([long]$temporalBlock * 100000000L) + ([long]$temporalPosition * 1000000L)
    $runStartUtc = [DateTimeOffset]::Parse('2026-08-23T12:00:00Z').AddMinutes($temporalBlock).AddSeconds($temporalPosition * 10)

    return [ordered]@{
        schema_id = 'm0.event.v1'
        schema_version = 1
        protocol_sha256 = ('a' * 64)
        environment_sha256 = ('b' * 64)
        fixture_sha256 = ('c' * 64)
        harness_sha256 = ('d' * 64)
        scenario_sha256 = ('e' * 64)
        experiment_id = 'exp-20260823-shell'
        run_id = $RunId
        candidate_code = $CandidateCode
        scenario_id = $ScenarioId
        attempt = $Attempt
        event_seq = $Sequence
        previous_line_sha256 = $PreviousHash
        utc = $runStartUtc.AddMilliseconds($Sequence).ToString("yyyy-MM-ddTHH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
        qpc_ticks = $runStartQpc + $Sequence
        qpc_frequency = 10000000
        event = $EventName
        payload = $Payload
    }
}

function New-EvidenceTestRun {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [string] $ArtifactHash,
        [string] $ScenarioId = 'process-cold',
        [int] $Attempt = 1,
        [string] $Status = 'valid',
        [AllowNull()] $StatusReason = $null,
        [AllowNull()] $ErrorCode = $null
    )

    $scheduledBlock = @($evidenceSchedule.blocks | Where-Object block -eq $Attempt)
    Assert-Equal -Actual $scheduledBlock.Count -Expected 1 'Test helper requires one real schedule block per attempt.'
    $position = [Array]::IndexOf([object[]]@($scheduledBlock[0].candidate_order), $CandidateCode) + 1

    return [ordered]@{
        schema_id = 'm0.run.v1'
        schema_version = 1
        experiment_id = 'exp-20260823-shell'
        run_id = $RunId
        candidate_code = $CandidateCode
        scenario_id = $ScenarioId
        attempt = $Attempt
        common_inputs = [ordered]@{
            protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64)
            fixture_sha256 = ('c' * 64); harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
        }
        candidate_input = [ordered]@{
            git_commit = ('1' * 40); dirty = $false; artifact_sha256 = $ArtifactHash; build_mode = 'release'
            toolchains = @([ordered]@{ name = 'test-toolchain'; version = '1.0.0'; sha256 = ('2' * 64) })
            lockfiles = @([ordered]@{ relative_path = 'candidate.lock'; sha256 = ('3' * 64) })
        }
        schedule = [ordered]@{
            seed = [int]$evidenceSchedule.seed
            block = $Attempt
            block_order = $scheduledBlock[0].order
            position_in_block = $position
        }
        status = $Status
        status_reason = $StatusReason
        error_code = $ErrorCode
        raw = $null
        markers = @('run-started', 'launch-input', 'first-frame-externally-visible', 'window-actionable', 'measurement-complete')
        process_tree_sha256 = ('4' * 64)
        metrics = @([ordered]@{ metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null })
        actions = @([ordered]@{
            metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
            start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
        })
        external_noise = @()
        artifacts = @([ordered]@{ artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = $ArtifactHash; size_bytes = 1234 })
        checks_omitted = @()
    }
}

function Complete-EvidenceTestRun {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [string] $ArtifactHash,
        [int] $Attempt = 1,
        [string] $Status = 'valid',
        [AllowNull()] $StatusReason = $null,
        [AllowNull()] $ErrorCode = $null,
        [ValidateRange(0, 2)] [int] $TemporalPositionOverride = 0,
        [ValidateRange(0, 22)] [int] $TemporalBlockOverride = 0,
        [ValidateRange(0, 120000)] [int] $SealDelayMilliseconds = 0,
        [ValidateSet('', 'AB', 'BA')] [string] $ScheduleOrderOverride = ''
    )

    $runId = 'run-{0}-process-cold-{1:D2}' -f $CandidateCode, $Attempt
    $rawPath = Join-Path $Root "$runId.jsonl"
    $runPath = Join-Path $Root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $launch = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'launch-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $launch -SchemaPath $eventSchema
    $frame = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'first-frame-externally-visible'; phase = 'instant'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $third = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $frame -SchemaPath $eventSchema
    $actionable = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 4 -PreviousHash $third.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'window-actionable'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $fourth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $actionable -SchemaPath $eventSchema
    $action = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 5 -PreviousHash $fourth.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
        start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $fifth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $action -SchemaPath $eventSchema
    $metric = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 6 -PreviousHash $fifth.line_sha256 -EventName 'resource_sample' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $sixth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $metric -SchemaPath $eventSchema
    $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 7 -PreviousHash $sixth.line_sha256 -EventName 'artifact_recorded' -Payload ([ordered]@{
        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = $ArtifactHash
        size_bytes = 1234; transient = $false; redacted = $true
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $seventh = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
    $marker = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 8 -PreviousHash $seventh.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'measurement-complete'; phase = 'end'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    $eighth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $marker -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode $CandidateCode -Attempt $Attempt -Sequence 9 -PreviousHash $eighth.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = $Status; status_reason = $StatusReason; error_code = $ErrorCode; checks_skipped = @()
    }) -TemporalPositionOverride $TemporalPositionOverride -TemporalBlockOverride $TemporalBlockOverride
    if ($SealDelayMilliseconds -gt 0) {
        $seal.utc = [DateTimeOffset]::Parse($seal.utc).AddMilliseconds($SealDelayMilliseconds).ToString("yyyy-MM-ddTHH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
        $seal.qpc_ticks = [long]$seal.qpc_ticks + ([long]$SealDelayMilliseconds * 10000L)
    }
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode $CandidateCode -ArtifactHash $ArtifactHash -Attempt $Attempt -Status $Status -StatusReason $StatusReason -ErrorCode $ErrorCode
    if (-not [string]::IsNullOrWhiteSpace($ScheduleOrderOverride)) {
        $run.schedule.block_order = $ScheduleOrderOverride
    }
    $completed = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
    return [pscustomobject]@{ raw_path = $rawPath; run_path = $runPath; completed = $completed }
}

function Copy-EvidenceCommittedRuns {
    param(
        [Parameter(Mandatory)] [string] $SourceRoot,
        [Parameter(Mandatory)] [string] $DestinationRoot,
        [string[]] $ExcludedRunIds = @()
    )

    $excluded = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($runId in $ExcludedRunIds) { $null = $excluded.Add($runId) }
    foreach ($snapshotPath in Get-ChildItem -LiteralPath $SourceRoot -File -Filter '*.snapshot.json') {
        $runId = $snapshotPath.Name.Substring(0, $snapshotPath.Name.Length - '.snapshot.json'.Length)
        if ($excluded.Contains($runId)) { continue }
        foreach ($suffix in @('.jsonl', '.run.json', '.intent.json', '.snapshot.json')) {
            [IO.File]::Copy((Join-Path $SourceRoot "$runId$suffix"), (Join-Path $DestinationRoot "$runId$suffix"), $false)
        }
    }
}

function Get-EvidenceCommittedRunPaths {
    param([Parameter(Mandatory)] [string] $Root)
    $paths = [Collections.Generic.List[string]]::new()
    foreach ($block in $evidenceSchedule.blocks) {
        foreach ($candidateCode in $block.candidate_order) {
            $runId = 'run-{0}-process-cold-{1:D2}' -f $candidateCode, [int]$block.block
            $path = Join-Path $Root "$runId.run.json"
            if (Test-Path -LiteralPath $path -PathType Leaf) { $paths.Add($path) }
        }
    }
    return @($paths)
}

function New-EvidenceTestSnapshotSet {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string[]] $RunPaths,
        [Parameter(Mandatory)] [string] $SnapshotSetId
    )

    $runIds = @($RunPaths | ForEach-Object {
        $leaf = Split-Path ([IO.Path]::GetFullPath($_)) -Leaf
        if ($leaf -cnotmatch '^(.+)\.run\.json$') { throw "Invalid test run leaf '$leaf'." }
        $Matches[1]
    })
    return New-M0EvidenceSnapshotSet -EvidenceRoot $Root -SnapshotSetId $SnapshotSetId -RunIds $runIds -SchedulePath $evidenceSchedulePath -SchemaRoot $evidenceSchemaRoot
}

function Get-EvidenceCandidateDigestsFromRunObjects {
    param(
        [Parameter(Mandatory)] [object[]] $Runs,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [switch] $RequireClean
    )

    $records = @($Runs | ForEach-Object {
        $runCopy = Copy-EvidenceTestObject $_
        if ($null -eq $runCopy.raw) {
            $runCopy.raw = [ordered]@{
                relative_path = "$($runCopy.run_id).jsonl"
                event_count = 1
                final_line_sha256 = ('7' * 64)
                sealed_utc = '2026-08-23T12:00:02Z'
            }
        }
        [pscustomobject]@{ run = $runCopy; run_record_sha256 = ('8' * 64); path = "$($runCopy.run_id).run.json" }
    })
    return & (Get-Module M0.Evidence) {
        param($InputRecords, $Code, $Clean)
        Get-M0EvidenceCandidateDigestsFromRecords -Records $InputRecords -CandidateCode $Code -RequireClean:$Clean
    } $records $CandidateCode ([bool]$RequireClean)
}

function Complete-EvidenceCatalogScenario {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [ValidateRange(1, 100)] [int] $QueuePairCount = 1,
        [switch] $CaseSpoofLaunchMarker,
        [switch] $OmitRequiredResourceMetrics
    )

    $scenario = @($evidenceMarkerCatalog.scenarios | Where-Object scenario_id -CEQ $ScenarioId)[0]
    $runId = "run-A-$ScenarioId-01"
    $rawPath = Join-Path $Root "$runId.jsonl"
    $runPath = Join-Path $Root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $markerEvents = [Collections.Generic.List[object]]::new()
    $resourceMetricEvents = [Collections.Generic.List[object]]::new()
    $actionEvents = [Collections.Generic.List[object]]::new()
    $sequence = 1
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' }) -ScenarioId $ScenarioId
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $baseTicks = [long]$started.qpc_ticks
    $lastTicks = $baseTicks
    $sequence++

    $markerSpecifications = [Collections.Generic.List[object]]::new()
    $resourcePolicy = $null
    if ($ScenarioId -ceq 'queue-stall') {
        $markerSpecifications.Add([pscustomobject]@{ contract = $scenario.markers[0]; occurrence_id = $null; qpc_ticks = $baseTicks + 1L })
        for ($pairIndex = 1; $pairIndex -le $QueuePairCount; $pairIndex++) {
            $occurrenceId = 'probe-{0:D4}' -f $pairIndex
            $inputTicks = $baseTicks + 1L + ([long]($pairIndex - 1) * 500000L)
            $markerSpecifications.Add([pscustomobject]@{ contract = $scenario.markers[1]; occurrence_id = $occurrenceId; qpc_ticks = $inputTicks })
            $markerSpecifications.Add([pscustomobject]@{ contract = $scenario.markers[2]; occurrence_id = $occurrenceId; qpc_ticks = $inputTicks + 100000L })
        }
        $lastQueueTicks = [long]$markerSpecifications[-1].qpc_ticks
        $markerSpecifications.Add([pscustomobject]@{ contract = $scenario.markers[3]; occurrence_id = $null; qpc_ticks = $lastQueueTicks })
        $markerSpecifications.Add([pscustomobject]@{ contract = $scenario.markers[4]; occurrence_id = $null; qpc_ticks = $lastQueueTicks })
    }
    else {
        $plannedMarkerIndex = 1
        $resourcePolicies = @($evidenceMarkerCatalog.resource_sampling_policies | Where-Object scenario_id -CEQ $ScenarioId)
        $resourceOriginTicks = $baseTicks + 1L
        $resourceSamplingStartTicks = $null
        $resourceSamplingEndTicks = $null
        if ($resourcePolicies.Count -eq 1) {
            $resourcePolicy = $resourcePolicies[0]
            $frequency = [Numerics.BigInteger]::Parse([string]$started.qpc_frequency)
            $stabilizationTicks = [Numerics.BigInteger]::Divide(
                [Numerics.BigInteger]::Parse([string]$resourcePolicy.minimum_stabilization_ns) * $frequency,
                1000000000L
            )
            $samplingTicks = [Numerics.BigInteger]::Divide(
                [Numerics.BigInteger]::Parse([string]$resourcePolicy.sampling_duration_ns) * $frequency,
                1000000000L
            )
            $resourceSamplingStartTicks = $resourceOriginTicks + [long]$stabilizationTicks
            $resourceSamplingEndTicks = $resourceSamplingStartTicks + [long]$samplingTicks
        }
        foreach ($marker in @($scenario.markers)) {
            $count = if (@($marker.cardinality.required_statuses) -ccontains 'valid') { [int]$marker.cardinality.minimum } else { 0 }
            for ($occurrenceIndex = 1; $occurrenceIndex -le $count; $occurrenceIndex++) {
                $occurrenceId = if ($marker.occurrence_id_policy -ceq 'required_unique') { 'occurrence-{0:D4}' -f $occurrenceIndex } else { $null }
                $markerKey = '{0}:{1}' -f $marker.marker_id, $marker.phase
                $plannedTicks = $baseTicks + ([long]$plannedMarkerIndex * 10000000L)
                if ($resourcePolicies.Count -eq 1) {
                    if ($markerKey -ceq [string]$resourcePolicy.stabilization_start_marker_key) {
                        $plannedTicks = $resourceOriginTicks
                    }
                    elseif ($markerKey -ceq [string]$resourcePolicy.sampling_start_marker_key) {
                        $plannedTicks = [long]$resourceSamplingStartTicks
                    }
                    elseif ($markerKey -ceq [string]$resourcePolicy.sampling_end_marker_key -or $markerKey -ceq 'measurement-complete:end') {
                        $plannedTicks = [long]$resourceSamplingEndTicks
                    }
                    elseif ($null -ne $resourcePolicy.memory_sample_marker_key -and $markerKey -ceq [string]$resourcePolicy.memory_sample_marker_key) {
                        $sampleOffsetTicks = [Numerics.BigInteger]::Divide(
                            [Numerics.BigInteger]::Parse([string]$resourcePolicy.memory_sample_offset_ns) * [Numerics.BigInteger]::Parse([string]$started.qpc_frequency),
                            1000000000L
                        )
                        $plannedTicks = [long]$resourceSamplingStartTicks + [long]$sampleOffsetTicks
                    }
                }
                $markerSpecifications.Add([pscustomobject]@{
                    contract = $marker
                    occurrence_id = $occurrenceId
                    qpc_ticks = $plannedTicks
                })
                $plannedMarkerIndex++
            }
        }
    }

    foreach ($specification in $markerSpecifications) {
        $observedMarkerId = if ($CaseSpoofLaunchMarker -and $specification.contract.marker_id -ceq 'launch-input') {
            'Launch-input'
        }
        else {
            $specification.contract.marker_id
        }
        $marker = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'marker' -ScenarioId $ScenarioId -Payload ([ordered]@{
            marker_id = $observedMarkerId
            phase = $specification.contract.phase
            source = $specification.contract.source
            role = $specification.contract.role
            occurrence_id = $specification.occurrence_id
        })
        $marker.qpc_ticks = [long]$specification.qpc_ticks
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $marker -SchemaPath $eventSchema
        $markerEvents.Add($marker)
        $lastTicks = [long]$marker.qpc_ticks
        $sequence++
        if ($null -ne $resourcePolicy -and -not $OmitRequiredResourceMetrics) {
            $markerKey = '{0}:{1}' -f $marker.payload.marker_id, $marker.payload.phase
            foreach ($requiredMetric in @($resourcePolicy.required_metrics | Where-Object anchor_marker_key -CEQ $markerKey)) {
                $metric = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'resource_sample' -ScenarioId $ScenarioId -Payload ([ordered]@{
                    metric_id = $requiredMetric.metric_id
                    value_integer = 64 + $resourceMetricEvents.Count
                    unit = $requiredMetric.unit
                    error_code = $null
                })
                $metric.qpc_ticks = [long]$marker.qpc_ticks
                $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $metric -SchemaPath $eventSchema
                $resourceMetricEvents.Add($metric)
                $lastTicks = [long]$metric.qpc_ticks
                $sequence++
            }
        }
    }

    foreach ($measurement in @($scenario.measurements)) {
        $starts = @($markerEvents | Where-Object {
            ('{0}:{1}' -f $_.payload.marker_id, $_.payload.phase) -ceq $measurement.start_marker_key
        })
        foreach ($start in $starts) {
            $ends = @($markerEvents | Where-Object {
                ('{0}:{1}' -f $_.payload.marker_id, $_.payload.phase) -ceq $measurement.end_marker_key -and
                (-not [bool]$measurement.correlated_occurrence -or $_.payload.occurrence_id -ceq $start.payload.occurrence_id)
            })
            Assert-Equal -Actual $ends.Count -Expected 1 "Scenario '$ScenarioId' test helper could not resolve one measurement endpoint."
            $end = $ends[0]
            $valueNs = [long](([Numerics.BigInteger]::Parse([string]$end.qpc_ticks) - [Numerics.BigInteger]::Parse([string]$start.qpc_ticks)) * 1000000000L / [Numerics.BigInteger]::Parse([string]$start.qpc_frequency))
            $outcome = if ($measurement.decision_role -ceq 'attempt_limit' -and $valueNs -gt [long]$measurement.attempt_limit_ns) { 'budget_exceeded' } else { 'observed' }
            $action = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'action_observation' -ScenarioId $ScenarioId -Payload ([ordered]@{
                metric_id = $measurement.metric_id
                value_ns = $valueNs
                outcome = $outcome
                source = 'external_harness'
                start_marker_event_seq = [int]$start.event_seq
                end_marker_event_seq = [int]$end.event_seq
                reason_code = if ($outcome -ceq 'budget_exceeded') { 'budget_violated' } else { $null }
                error_code = if ($outcome -ceq 'budget_exceeded') { 'attempt_limit_exceeded' } else { $null }
            })
            $action.qpc_ticks = $lastTicks
            $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $action -SchemaPath $eventSchema
            $actionEvents.Add($action)
            $sequence++
        }
    }

    $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'artifact_recorded' -ScenarioId $ScenarioId -Payload ([ordered]@{
        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
        size_bytes = 1234; transient = $false; redacted = $true
    })
    $artifact.qpc_ticks = $lastTicks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
    $sequence++
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'run_sealed' -ScenarioId $ScenarioId -Payload ([ordered]@{
        status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @()
    })
    $seal.qpc_ticks = $lastTicks

    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -ScenarioId $ScenarioId
    $run.metrics = @($resourceMetricEvents | ForEach-Object { $_.payload })
    $run.actions = @($actionEvents | ForEach-Object { $_.payload })
    $seenMarkers = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $markerInventory = [Collections.Generic.List[string]]::new()
    foreach ($markerId in @('run-started') + @($markerEvents | ForEach-Object { $_.payload.marker_id })) {
        if ($seenMarkers.Add([string]$markerId)) { $markerInventory.Add([string]$markerId) }
    }
    $run.markers = @($markerInventory)
    $completed = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
    return [pscustomobject]@{ raw_path = $rawPath; run_path = $runPath; completed = $completed }
}

function Complete-EvidencePendingQueueNonFailure {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [ValidateSet('discarded', 'inconclusive')] [string] $Status,
        [switch] $CauseBeforeInput
    )

    $runId = "run-A-queue-$Status-01"
    $rawPath = Join-Path $Root "$runId.jsonl"
    $runPath = Join-Path $Root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $sequence = 1
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $null -EventName 'run_started' -ScenarioId 'queue-stall' -Payload ([ordered]@{ phase = 'started' })
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $sequence++
    $windowTicks = [long]$started.qpc_ticks + 1L
    $window = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'marker' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        marker_id = 'queue-probe-window'; phase = 'begin'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    $window.qpc_ticks = $windowTicks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $window -SchemaPath $eventSchema
    $sequence++

    $causeName = if ($Status -ceq 'discarded') { 'noise_observation' } else { 'check_recorded' }
    $causePayload = if ($Status -ceq 'discarded') {
        [ordered]@{
            noise_code = 'external-os-event'; observed = $true; evidence_sha256 = ('7' * 64)
            discard_reason = 'external_os_event'; error_code = $null
        }
    }
    else {
        [ordered]@{
            check_id = 'CHK-QUEUE-PROBE'; status = 'skipped'; reason_code = 'queue_probe_interrupted'
            status_reason = 'incomplete_evidence'; error_code = $null
        }
    }
    $cause = $null
    if ($CauseBeforeInput) {
        $cause = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName $causeName -ScenarioId 'queue-stall' -Payload $causePayload
        $cause.qpc_ticks = $windowTicks
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $cause -SchemaPath $eventSchema
        $sequence++
    }

    $input = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'marker' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        marker_id = 'focus-probe-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = "probe-$Status-0001"
    })
    $input.qpc_ticks = $windowTicks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $input -SchemaPath $eventSchema
    $sequence++
    if (-not $CauseBeforeInput) {
        $cause = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName $causeName -ScenarioId 'queue-stall' -Payload $causePayload
        $cause.qpc_ticks = $windowTicks + 1L
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $cause -SchemaPath $eventSchema
        $sequence++
    }

    $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'artifact_recorded' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
        size_bytes = 1234; transient = $false; redacted = $true
    })
    $artifact.qpc_ticks = [long]$cause.qpc_ticks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
    $sequence++
    $statusReason = if ($Status -ceq 'discarded') { 'external_os_event' } else { 'incomplete_evidence' }
    $checksSkipped = @()
    if ($Status -ceq 'inconclusive') {
        $checksSkipped = @([ordered]@{ check_id = $causePayload.check_id; reason_code = $causePayload.reason_code })
    }
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'run_sealed' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        status = $Status; status_reason = $statusReason; error_code = $null; checks_skipped = $checksSkipped
    })
    $seal.qpc_ticks = [long]$cause.qpc_ticks

    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -ScenarioId 'queue-stall' -Status $Status -StatusReason $statusReason -ErrorCode $null
    $run.markers = @('run-started', 'queue-probe-window', 'focus-probe-input')
    $run.metrics = @()
    $run.actions = @()
    $run.external_noise = @()
    if ($Status -ceq 'discarded') { $run.external_noise = @($cause.payload) }
    $run.checks_omitted = $checksSkipped
    $null = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
    return Assert-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SchemaRoot $evidenceSchemaRoot
}

Test-Case 'JSONL evidence is chained, sealed and append-only' {
    $root = Join-Path $TestTempRoot 'evidence-chain'
    New-Item -ItemType Directory -Path $root | Out-Null
    $result = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $validated = Assert-M0EvidenceRun -RawPath $result.raw_path -RunPath $result.run_path -SchemaRoot $evidenceSchemaRoot
    Assert-Equal -Actual $validated.event_count -Expected 9
    Assert-True ($validated.final_line_sha256 -match '^[a-f0-9]{64}$')
    $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
    $runnerChild = Invoke-M0EvidenceChildScript -Name 'validate-committed-run' -Source @"
`$ErrorActionPreference = 'Stop'
`$result = & '$($runner.Replace("'", "''"))' -RawPath '$($result.raw_path.Replace("'", "''"))' -RunPath '$($result.run_path.Replace("'", "''"))'
`$result | ConvertTo-Json -Compress -Depth 20
"@
    Assert-Equal -Actual $runnerChild.exit_code -Expected 0 "The clean public runner child failed: $($runnerChild.output)"
    $runnerResult = $runnerChild.output.Trim() | ConvertFrom-Json -Depth 20
    Assert-Equal -Actual $runnerResult.run.run_id -Expected 'run-A-process-cold-01'

    $late = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 10 -PreviousHash $validated.final_line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'late'; phase = 'end'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath $result.raw_path -Event $late -SchemaPath (Join-Path $evidenceSchemaRoot 'event.schema.json') } 'M0E004|M0E019|sealed|intent'
}

Test-Case 'Evidence completes and revalidates every frozen shell marker scenario' {
    $root = Join-Path $TestTempRoot 'evidence-all-marker-scenarios'
    New-Item -ItemType Directory -Path $root | Out-Null
    foreach ($scenarioId in @($evidenceMarkerCatalog.scenarios.scenario_id)) {
        $result = Complete-EvidenceCatalogScenario -Root $root -ScenarioId $scenarioId
        $validated = Assert-M0EvidenceRun -RawPath $result.raw_path -RunPath $result.run_path -SchemaRoot $evidenceSchemaRoot
        Assert-Equal -Actual $validated.status -Expected 'valid' "Scenario '$scenarioId' did not round-trip through Evidence."
    }
}

Test-Case 'Valid resource evidence cannot omit its frozen anchored metrics' {
    foreach ($scenarioId in @('idle-memory', 'paused-cpu', 'active-progress')) {
        $root = Join-Path $TestTempRoot "evidence-missing-resource-$scenarioId"
        New-Item -ItemType Directory -Path $root | Out-Null
        Assert-Throws { Complete-EvidenceCatalogScenario -Root $root -ScenarioId $scenarioId -OmitRequiredResourceMetrics } 'M0M007|required|resource|metric'
    }
}

Test-Case 'A pending queue input can append one terminal action and round-trip as failed evidence' {
    $root = Join-Path $TestTempRoot 'evidence-queue-pending-terminal'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-queue-stall-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'

    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -ScenarioId 'queue-stall' -Payload ([ordered]@{ phase = 'started' })
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $windowTicks = [long]$started.qpc_ticks + 1L
    $window = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $head.line_sha256 -EventName 'marker' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        marker_id = 'queue-probe-window'; phase = 'begin'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    $window.qpc_ticks = $windowTicks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $window -SchemaPath $eventSchema
    $input = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $head.line_sha256 -EventName 'marker' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        marker_id = 'focus-probe-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = 'probe-terminal-0001'
    })
    $input.qpc_ticks = $windowTicks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $input -SchemaPath $eventSchema
    $timeout = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 4 -PreviousHash $head.line_sha256 -EventName 'action_observation' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        metric_id = 'queue_focus_stall_ns'; value_ns = $null; outcome = 'timeout'; source = 'external_harness'
        start_marker_event_seq = $null; end_marker_event_seq = $null; reason_code = 'scenario_timeout'; error_code = 'queue_focus_timeout'
    })
    $timeout.qpc_ticks = $windowTicks + 1L
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $timeout -SchemaPath $eventSchema
    $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 5 -PreviousHash $head.line_sha256 -EventName 'artifact_recorded' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
        size_bytes = 1234; transient = $false; redacted = $true
    })
    $artifact.qpc_ticks = $timeout.qpc_ticks
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 6 -PreviousHash $head.line_sha256 -EventName 'run_sealed' -ScenarioId 'queue-stall' -Payload ([ordered]@{
        status = 'failed'; status_reason = 'scenario_timeout'; error_code = 'queue_focus_timeout'; checks_skipped = @()
    })
    $seal.qpc_ticks = $timeout.qpc_ticks

    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -ScenarioId 'queue-stall' -Status failed -StatusReason scenario_timeout -ErrorCode queue_focus_timeout
    $run.markers = @('run-started', 'queue-probe-window', 'focus-probe-input')
    $run.metrics = @()
    $run.actions = @($timeout.payload)
    $null = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
    $validated = Assert-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SchemaRoot $evidenceSchemaRoot
    Assert-Equal -Actual $validated.status -Expected 'failed'
}

Test-Case 'Pending queue interruption requires one causal discarded or inconclusive event after input' {
    foreach ($status in @('discarded', 'inconclusive')) {
        $root = Join-Path $TestTempRoot "evidence-queue-pending-$status"
        New-Item -ItemType Directory -Path $root | Out-Null
        $validated = Complete-EvidencePendingQueueNonFailure -Root $root -Status $status
        Assert-Equal -Actual $validated.status -Expected $status

        $beforeRoot = Join-Path $TestTempRoot "evidence-queue-cause-before-$status"
        New-Item -ItemType Directory -Path $beforeRoot | Out-Null
        Assert-Throws { Complete-EvidencePendingQueueNonFailure -Root $beforeRoot -Status $status -CauseBeforeInput } 'M0M008|causal|after'
    }
}

Test-Case 'Evidence completion and assertion reject case-spoofed marker identities' {
    $completeRoot = Join-Path $TestTempRoot 'evidence-case-spoof-complete'
    New-Item -ItemType Directory -Path $completeRoot | Out-Null
    Assert-Throws { Complete-EvidenceCatalogScenario -Root $completeRoot -ScenarioId 'process-cold' -CaseSpoofLaunchMarker } 'M0M005|unknown|case|identity'

    $assertRoot = Join-Path $TestTempRoot 'evidence-case-spoof-assert'
    New-Item -ItemType Directory -Path $assertRoot | Out-Null
    $completed = Complete-EvidenceCatalogScenario -Root $assertRoot -ScenarioId 'process-cold'
    $events = @((Get-Content -LiteralPath $completed.raw_path) | ForEach-Object { $_ | ConvertFrom-Json -Depth 100 -DateKind String })
    $events[1].payload.marker_id = 'Launch-input'
    $lines = [Collections.Generic.List[string]]::new()
    $previousHash = $null
    foreach ($event in $events) {
        $event.previous_line_sha256 = $previousHash
        $line = $event | ConvertTo-Json -Compress -Depth 100
        $lines.Add($line)
        $previousHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.UTF8Encoding]::new($false).GetBytes($line))).ToLowerInvariant()
    }
    [IO.File]::WriteAllText($completed.raw_path, (($lines -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))

    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100 -DateKind String
    $run.markers[1] = 'Launch-input'
    $run.raw.final_line_sha256 = $previousHash
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0M005|unknown|case|identity'
}

Test-Case 'Repeated queue evidence projects unique markers and ordered per-probe actions' {
    $root = Join-Path $TestTempRoot 'evidence-queue-projection'
    New-Item -ItemType Directory -Path $root | Out-Null
    $result = Complete-EvidenceCatalogScenario -Root $root -ScenarioId 'queue-stall' -QueuePairCount 2
    $run = Get-Content -Raw -LiteralPath $result.run_path | ConvertFrom-Json -Depth 100
    Assert-SequenceEqual -Actual @($run.markers) -Expected @(
        'run-started', 'queue-probe-window', 'focus-probe-input', 'focus-visible-ack',
        'queue-accessibility-verified', 'measurement-complete'
    )
    Assert-Equal -Actual @($run.actions).Count -Expected 2
    Assert-True ([int]$run.actions[0].start_marker_event_seq -lt [int]$run.actions[1].start_marker_event_seq)

    $run.actions[0], $run.actions[1] = $run.actions[1], $run.actions[0]
    [IO.File]::WriteAllText($result.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $result.raw_path -RunPath $result.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|action'
}

Test-Case 'Sequence, previous hash and byte tampering are rejected' {
    $root = Join-Path $TestTempRoot 'evidence-tamper'
    New-Item -ItemType Directory -Path $root | Out-Null
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $rawPath = Join-Path $root 'run-A-process-cold-01.jsonl'
    $firstEvent = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $firstEvent -SchemaPath $eventSchema
    $wrong = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 3 -PreviousHash $first.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'wrong'; phase = 'begin'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath $rawPath -Event $wrong -SchemaPath $eventSchema } 'M0E003'

    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode B -ArtifactHash ('9' * 64)
    $bytes = [IO.File]::ReadAllBytes($completed.raw_path)
    $bytes[40] = $bytes[40] -bxor 1
    [IO.File]::WriteAllBytes($completed.raw_path, $bytes)
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E003|hash|schema'
}

Test-Case 'Raw JSONL rejects more than one final LF' {
    $root = Join-Path $TestTempRoot 'evidence-extra-final-lf'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $stream = [IO.File]::Open($completed.raw_path, [IO.FileMode]::Append, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.WriteByte(0x0A) } finally { $stream.Dispose() }
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E002|empty line'
}

Test-Case 'Evidence rejects identity and absolute-path leakage and provides deterministic redaction' {
    $root = Join-Path $TestTempRoot 'evidence-redaction'
    New-Item -ItemType Directory -Path $root | Out-Null
    $hostName = [Environment]::MachineName
    $userName = [Environment]::UserName
    $secret = "host=$hostName user=$userName path=C:\\Users\\$userName\\Music\\private.mp3"
    $redacted = ConvertTo-M0EvidenceRedactedText -Value $secret
    Assert-True ($redacted -notmatch [regex]::Escape($hostName))
    Assert-True ($redacted -notmatch [regex]::Escape($userName))
    Assert-True ($redacted -notmatch '(?i)[A-Z]:\\')

    $secretRunId = 'run-A-redaction-secret'
    $event = New-EvidenceTestEvent -RunId $secretRunId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{ check_id = 'CHK-SEC'; status = 'skipped'; reason = $secret; status_reason = 'incomplete_evidence'; error_code = $null })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath (Join-Path $root "$secretRunId.jsonl") -Event $event -SchemaPath (Join-Path $evidenceSchemaRoot 'event.schema.json') } 'M0E006'

    $leaks = @(
        'https://example.invalid/private?id=1',
        '/usr/local/bin/player',
        '/workspace/music/private.mp3',
        '[/usr/local/bin/player]',
        'prefix:/usr/local/bin/player',
        'file:C:\private\music.mp3',
        'mailto:private.listener@example.invalid',
        'private.listener@example.invalid',
        'endpoint=192.0.2.10:443',
        'fe80::1',
        'AA:BB:CC:DD:EE:FF',
        'serial=SN-PRIVATE-123',
        'SN-PRIVATE-123',
        'device Speakers (Realtek(R) Audio) disconnected',
        'SWD\MMDEVAPI\{PRIVATE-ENDPOINT-ID}',
        'private.listener@localhost',
        '/'
    )
    for ($index = 0; $index -lt $leaks.Count; $index++) {
        $leakRunId = "run-A-redaction-leak-$index"
        $event = New-EvidenceTestEvent -RunId $leakRunId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{ check_id = 'CHK-SEC'; status = 'skipped'; reason = $leaks[$index]; status_reason = 'incomplete_evidence'; error_code = $null })
        Assert-Throws { Add-M0EvidenceEvent -LiteralPath (Join-Path $root "$leakRunId.jsonl") -Event $event -SchemaPath (Join-Path $evidenceSchemaRoot 'event.schema.json') } 'M0E006'
    }
    $redactedEmail = ConvertTo-M0EvidenceRedactedText -Value 'contact mailto:private.listener@example.invalid'
    Assert-True ($redactedEmail -notmatch '(?i)mailto:|[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}') 'Email/URI redaction left PII in evidence text.'
    $redactedIdentifiers = ConvertTo-M0EvidenceRedactedText -Value 'serial=SN-PRIVATE-123 endpoint=Speakers (Realtek(R) Audio) SWD\MMDEVAPI\{PRIVATE-ENDPOINT-ID}'
    Assert-True ($redactedIdentifiers -notmatch '(?i)SN-PRIVATE|Speakers|Realtek|MMDEVAPI|PRIVATE-ENDPOINT') 'Serial/device endpoint redaction left a clear identifier tail.'

    $freeTextRunId = 'run-A-redaction-free-text'
    $freeTextEndpoint = New-EvidenceTestEvent -RunId $freeTextRunId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-SEC'; status = 'skipped'; reason = 'Speakers (Realtek(R) Audio) disconnected'
        status_reason = 'incomplete_evidence'; error_code = $null
    })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath (Join-Path $root "$freeTextRunId.jsonl") -Event $freeTextEndpoint -SchemaPath (Join-Path $evidenceSchemaRoot 'event.schema.json') } 'M0E002|schema'
}

Test-Case 'Closed schemas separate discarded causes from candidate failures' {
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $invalidSeal = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'discarded'; status_reason = 'candidate_crash'; error_code = $null; checks_skipped = @(); extra = 'forbidden'
    })
    Assert-True (-not (Test-Json -Json ($invalidSeal | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue))

    $invalidAction = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'visual_response'; value_ns = $null; outcome = 'observed'; error_code = $null
    })
    Assert-True (-not (Test-Json -Json ($invalidAction | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue))

    $invalidCrash = Copy-EvidenceTestObject $invalidAction
    $invalidCrash.payload.value_ns = 100
    $invalidCrash.payload.outcome = 'crash'
    Assert-True (-not (Test-Json -Json ($invalidCrash | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue))

    $invalidRun = New-EvidenceTestRun -RunId 'run-A-process-cold-01' -CandidateCode A -ArtifactHash ('f' * 64)
    $invalidRun.raw = [ordered]@{ relative_path = 'run-A-process-cold-01.jsonl'; event_count = 2; final_line_sha256 = ('0' * 64); sealed_utc = '2026-08-23T12:00:02Z' }
    $invalidRun.metrics[0].value_integer = $null
    $invalidRun.metrics[0].error_code = $null
    Assert-True (-not (Test-Json -Json ($invalidRun | ConvertTo-Json -Depth 100) -SchemaFile (Join-Path $evidenceSchemaRoot 'run.schema.json') -ErrorAction SilentlyContinue))
}

Test-Case 'Check event schema conditionals require reasons only for failed or skipped checks' {
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $unclassified = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-CONDITIONAL'; status = 'failed'; reason_code = 'missing_classification'
    })
    Assert-True (-not (Test-Json -Json ($unclassified | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue)) 'Unclassified failed check was accepted by event.schema.json.'
    foreach ($case in @(
        [ordered]@{ status = 'failed'; reason_code = $null; status_reason = 'codec_error'; error_code = 'check_failed' },
        [ordered]@{ status = 'skipped'; reason_code = $null; status_reason = 'incomplete_evidence'; error_code = $null },
        [ordered]@{ status = 'passed'; reason_code = 'must_be_null'; status_reason = $null; error_code = $null }
    )) {
        $event = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{
            check_id = 'CHK-CONDITIONAL'; status = $case.status; reason_code = $case.reason_code
            status_reason = $case.status_reason; error_code = $case.error_code
        })
        Assert-True (-not (Test-Json -Json ($event | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue)) 'Invalid check conditional was accepted by event.schema.json.'
    }

    $root = Join-Path $TestTempRoot 'evidence-check-conditional'
    New-Item -ItemType Directory -Path $root | Out-Null
    $invalidRunId = 'run-A-invalid-check-conditional'
    $invalid = New-EvidenceTestEvent -RunId $invalidRunId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-CONDITIONAL'; status = 'failed'; reason_code = $null; status_reason = 'codec_error'; error_code = 'check_failed'
    })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath (Join-Path $root "$invalidRunId.jsonl") -Event $invalid -SchemaPath $eventSchema } 'M0E002'
}

Test-Case 'Noise event schema conditionals bind observed state to an evidence hash' {
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    foreach ($case in @(
        [ordered]@{ observed = $true; evidence_sha256 = $null; discard_reason = 'external_os_event'; error_code = $null },
        [ordered]@{ observed = $false; evidence_sha256 = ('7' * 64); discard_reason = $null; error_code = $null }
    )) {
        $event = New-EvidenceTestEvent -RunId 'run-A-process-cold-01' -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'noise_observation' -Payload ([ordered]@{
            noise_code = 'external-os-event'; observed = $case.observed; evidence_sha256 = $case.evidence_sha256
            discard_reason = $case.discard_reason; error_code = $case.error_code
        })
        Assert-True (-not (Test-Json -Json ($event | ConvertTo-Json -Depth 100) -SchemaFile $eventSchema -ErrorAction SilentlyContinue)) 'Invalid noise conditional was accepted by event.schema.json.'
    }

    $root = Join-Path $TestTempRoot 'evidence-noise-conditional'
    New-Item -ItemType Directory -Path $root | Out-Null
    $invalidRunId = 'run-A-invalid-noise-conditional'
    $invalid = New-EvidenceTestEvent -RunId $invalidRunId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'noise_observation' -Payload ([ordered]@{
        noise_code = 'external-os-event'; observed = $true; evidence_sha256 = $null
        discard_reason = 'external_os_event'; error_code = $null
    })
    Assert-Throws { Add-M0EvidenceEvent -LiteralPath (Join-Path $root "$invalidRunId.jsonl") -Event $invalid -SchemaPath $eventSchema } 'M0E002'

    $invalidRun = New-EvidenceTestRun -RunId 'run-A-process-cold-01' -CandidateCode A -ArtifactHash ('f' * 64)
    $invalidRun.raw = [ordered]@{ relative_path = 'run-A-process-cold-01.jsonl'; event_count = 2; final_line_sha256 = ('0' * 64); sealed_utc = '2026-08-23T12:00:02Z' }
    $invalidRun.external_noise = @([ordered]@{
        noise_code = 'external-os-event'; observed = $true; evidence_sha256 = $null
        discard_reason = 'external_os_event'; error_code = $null
    })
    Assert-True (-not (Test-Json -Json ($invalidRun | ConvertTo-Json -Depth 100) -SchemaFile (Join-Path $evidenceSchemaRoot 'run.schema.json') -ErrorAction SilentlyContinue)) 'Invalid run noise conditional was accepted.'
}

Test-Case 'Each closed discard reason requires a matching classified raw observation' {
    $cases = @(
        [ordered]@{ reason = 'harness_failure'; noise_code = 'harness-capture-failure'; error_code = 'capture_failed' },
        [ordered]@{ reason = 'external_os_event'; noise_code = 'external-os-event'; error_code = $null },
        [ordered]@{ reason = 'operator_protocol_breach'; noise_code = 'operator-protocol-breach'; error_code = 'operator_input_detected' },
        [ordered]@{ reason = 'audio_endpoint_changed'; noise_code = 'audio-endpoint-change'; error_code = 'endpoint_changed' }
    )
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot "evidence-discard-$($case.reason)"
        New-Item -ItemType Directory -Path $root | Out-Null
        $runId = 'run-A-process-cold-01'
        $rawPath = Join-Path $root "$runId.jsonl"
        $runPath = Join-Path $root "$runId.run.json"
        $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
        $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
        $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $head.line_sha256 -EventName 'artifact_recorded' -Payload ([ordered]@{
            artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
            size_bytes = 1234; transient = $false; redacted = $true
        })
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
        $noisePayload = [ordered]@{
            noise_code = $case.noise_code; observed = $true; evidence_sha256 = ('7' * 64)
            discard_reason = $case.reason; error_code = $case.error_code
        }
        $noise = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $head.line_sha256 -EventName 'noise_observation' -Payload $noisePayload
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $noise -SchemaPath $eventSchema
        $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 4 -PreviousHash $head.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
            status = 'discarded'; status_reason = $case.reason; error_code = $case.error_code; checks_skipped = @()
        })
        $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status discarded -StatusReason $case.reason -ErrorCode $case.error_code
        $run.markers = @('run-started')
        $run.metrics = @()
        $run.actions = @()
        $run.external_noise = @($noisePayload)
        $completed = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
        Assert-Equal -Actual $completed.status -Expected 'discarded'
    }
}

Test-Case 'An invalid run record cannot orphan a valid append-only raw seal' {
    $root = Join-Path $TestTempRoot 'evidence-preflight-seal'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $launch = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'launch-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null
    })
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $launch -SchemaPath $eventSchema
    $frame = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'first-frame-externally-visible'; phase = 'instant'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    $third = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $frame -SchemaPath $eventSchema
    $actionable = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 4 -PreviousHash $third.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'window-actionable'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null
    })
    $fourth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $actionable -SchemaPath $eventSchema
    $action = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 5 -PreviousHash $fourth.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
        start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
    })
    $fifth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $action -SchemaPath $eventSchema
    $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 6 -PreviousHash $fifth.line_sha256 -EventName 'artifact_recorded' -Payload ([ordered]@{
        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
        size_bytes = 1234; transient = $false; redacted = $true
    })
    $sixth = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
    $marker = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 7 -PreviousHash $sixth.line_sha256 -EventName 'marker' -Payload ([ordered]@{
        marker_id = 'measurement-complete'; phase = 'end'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
    })
    $seventh = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $marker -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 8 -PreviousHash $seventh.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{ status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @() })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64)
    $run.metrics = @()
    foreach ($mutation in @('value', 'outcome', 'reference', 'missing', 'extra')) {
        $wrongProjection = Copy-EvidenceTestObject $run
        switch ($mutation) {
            'value' { $wrongProjection.actions[0].value_ns = 201 }
            'outcome' {
                $wrongProjection.actions[0].outcome = 'budget_exceeded'
                $wrongProjection.actions[0].reason_code = 'budget_violated'
                $wrongProjection.actions[0].error_code = 'invented_budget'
            }
            'reference' { $wrongProjection.actions[0].end_marker_event_seq = 3 }
            'missing' { $wrongProjection.actions = @() }
            'extra' { $wrongProjection.actions = @($wrongProjection.actions[0], (Copy-EvidenceTestObject $wrongProjection.actions[0])) }
        }
        Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $wrongProjection -SchemaRoot $evidenceSchemaRoot } 'M0E015|action'
    }
    $invalid = Copy-EvidenceTestObject $run
    $invalid.candidate_input | Add-Member -NotePropertyName unexpected -NotePropertyValue 'forbidden'
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $invalid -SchemaRoot $evidenceSchemaRoot } 'M0E007'

    $completed = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
    Assert-Equal -Actual $completed.event_count -Expected 8
}

Test-Case 'A candidate failure event cannot be sealed as discarded' {
    $root = Join-Path $TestTempRoot 'evidence-failure-precedence'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $crash = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_ns = $null; outcome = 'crash'; source = 'external_harness'
        start_marker_event_seq = $null; end_marker_event_seq = $null; reason_code = 'candidate_crash'; error_code = 'candidate_crash'
    })
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $crash -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'discarded'; status_reason = 'harness_failure'; error_code = $null; checks_skipped = @()
    })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status discarded -StatusReason harness_failure
    $run.markers = @('run-started')
    $run.metrics = @()
    $run.actions = @($crash.payload)
    $run.artifacts = @()
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E013'
}

Test-Case 'An action failure error code must survive unchanged into run and seal' {
    $root = Join-Path $TestTempRoot 'evidence-action-error-code'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $crash = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_ns = $null; outcome = 'crash'; source = 'external_harness'
        start_marker_event_seq = $null; end_marker_event_seq = $null; reason_code = 'candidate_crash'; error_code = 'candidate_crash'
    })
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $crash -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'failed'; status_reason = 'candidate_crash'; error_code = 'different_error'; checks_skipped = @()
    })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status failed -StatusReason candidate_crash -ErrorCode different_error
    $run.markers = @('run-started')
    $run.metrics = @()
    $run.actions = @($crash.payload)
    $run.artifacts = @()
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E013|error.code'
}

Test-Case 'Multiple action failures use the same severity order regardless of event order' {
    foreach ($order in @(
        @('timeout', 'crash'),
        @('crash', 'timeout')
    )) {
        $root = Join-Path $TestTempRoot ('evidence-action-order-' + ($order -join '-'))
        New-Item -ItemType Directory -Path $root | Out-Null
        $runId = 'run-A-process-cold-01'
        $rawPath = Join-Path $root "$runId.jsonl"
        $runPath = Join-Path $root "$runId.run.json"
        $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
        $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
        $sequence = 2
        $rawActions = [Collections.Generic.List[object]]::new()
        foreach ($outcome in $order) {
            $reasonCode = if ($outcome -ceq 'crash') { 'candidate_crash' } else { 'scenario_timeout' }
            $action = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
                metric_id = 'window_actionable_ns'; value_ns = $null; outcome = $outcome; source = 'external_harness'
                start_marker_event_seq = $null; end_marker_event_seq = $null; reason_code = $reasonCode; error_code = "${outcome}_error"
            })
            $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $action -SchemaPath $eventSchema
            $rawActions.Add($action.payload)
            $sequence++
        }
        $artifact = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'artifact_recorded' -Payload ([ordered]@{
            artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
            size_bytes = 1234; transient = $false; redacted = $true
        })
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $artifact -SchemaPath $eventSchema
        $sequence++
        $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence $sequence -PreviousHash $head.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
            status = 'failed'; status_reason = 'candidate_crash'; error_code = 'crash_error'; checks_skipped = @()
        })
        $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status failed -StatusReason candidate_crash -ErrorCode crash_error
        $run.markers = @('run-started')
        $run.metrics = @()
        $run.actions = @($rawActions)
        $completed = Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot
        Assert-Equal -Actual $completed.status -Expected 'failed'
    }
}

Test-Case 'A failed check cannot be contradicted by a valid run seal' {
    $root = Join-Path $TestTempRoot 'evidence-failed-check-status'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $check = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-CANDIDATE'; status = 'failed'; reason_code = 'candidate_check_failed'; status_reason = 'codec_error'; error_code = 'candidate_check_failed'
    })
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $check -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @()
    })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64)
    $run.markers = @('run-started')
    $run.metrics = @()
    $run.actions = @()
    $run.artifacts = @()
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E013|failed check'
}

Test-Case 'A skipped check requires an inconclusive incomplete-evidence seal' {
    $root = Join-Path $TestTempRoot 'evidence-skipped-check-status'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-B-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode B -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $first = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $check = New-EvidenceTestEvent -RunId $runId -CandidateCode B -Sequence 2 -PreviousHash $first.line_sha256 -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-OPTIONAL'; status = 'skipped'; reason_code = 'required_evidence_unavailable'; status_reason = 'incomplete_evidence'; error_code = $null
    })
    $second = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $check -SchemaPath $eventSchema
    $skipped = @([ordered]@{ check_id = 'CHK-OPTIONAL'; reason_code = 'required_evidence_unavailable' })
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode B -Sequence 3 -PreviousHash $second.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = $skipped
    })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode B -ArtifactHash ('9' * 64)
    $run.markers = @('run-started')
    $run.metrics = @()
    $run.actions = @()
    $run.artifacts = @()
    $run.checks_omitted = $skipped
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E013|skipped check'
}

Test-Case 'Non-valid terminal statuses require a deterministic adverse raw event' {
    $cases = @(
        [ordered]@{ status = 'failed'; status_reason = 'codec_error'; error_code = 'invented_error' },
        [ordered]@{ status = 'discarded'; status_reason = 'harness_failure'; error_code = $null },
        [ordered]@{ status = 'inconclusive'; status_reason = 'confidence_interval_too_wide'; error_code = $null }
    )
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot ("evidence-unsupported-status-$($case.status)")
        New-Item -ItemType Directory -Path $root | Out-Null
        $runId = 'run-A-process-cold-01'
        $rawPath = Join-Path $root "$runId.jsonl"
        $runPath = Join-Path $root "$runId.run.json"
        $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
        $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
        $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
        $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $head.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
            status = $case.status; status_reason = $case.status_reason; error_code = $case.error_code; checks_skipped = @()
        })
        $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status $case.status -StatusReason $case.status_reason -ErrorCode $case.error_code
        $run.markers = @('run-started')
        $run.metrics = @()
        $run.actions = @()
        $run.artifacts = @()
        Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E013|unsupported|adverse raw'
    }
}

Test-Case 'Run metric summaries must be the exact raw resource-sample projection' {
    $root = Join-Path $TestTempRoot 'evidence-metric-reconciliation'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
    $run.metrics[0].value_integer = 125000001
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|metric'
}

Test-Case 'Run action summaries must be the exact ordered raw action projection' {
    $mutations = @('value', 'outcome', 'reference', 'missing', 'extra')
    foreach ($mutation in $mutations) {
        $root = Join-Path $TestTempRoot "evidence-action-reconciliation-$mutation"
        New-Item -ItemType Directory -Path $root | Out-Null
        $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
        $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
        switch ($mutation) {
            'value' { $run.actions[0].value_ns = 201 }
            'outcome' {
                $run.actions[0].outcome = 'budget_exceeded'
                $run.actions[0].reason_code = 'budget_violated'
                $run.actions[0].error_code = 'invented_budget'
            }
            'reference' { $run.actions[0].end_marker_event_seq = 3 }
            'missing' { $run.actions = @() }
            'extra' { $run.actions = @($run.actions[0], (Copy-EvidenceTestObject $run.actions[0])) }
        }
        [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|action'
    }
}

Test-Case 'Run action schema mirrors the closed event outcome conditionals' {
    foreach ($mutation in @('candidate-source', 'observed-null-reference', 'timeout-with-value')) {
        $root = Join-Path $TestTempRoot "evidence-run-action-schema-$mutation"
        New-Item -ItemType Directory -Path $root | Out-Null
        $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
        $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
        switch ($mutation) {
            'candidate-source' { $run.actions[0].source = 'candidate_reported' }
            'observed-null-reference' { $run.actions[0].start_marker_event_seq = $null }
            'timeout-with-value' {
                $run.actions[0].outcome = 'timeout'
                $run.actions[0].start_marker_event_seq = $null
                $run.actions[0].end_marker_event_seq = $null
                $run.actions[0].reason_code = 'scenario_timeout'
                $run.actions[0].error_code = 'scenario_timeout'
            }
        }
        [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E008|schema'
    }
}

Test-Case 'Run marker inventory is deduplicated in first occurrence order and cannot be tampered' {
    foreach ($mutation in @('missing', 'swap', 'extra', 'duplicate')) {
        $root = Join-Path $TestTempRoot "evidence-marker-reconciliation-$mutation"
        New-Item -ItemType Directory -Path $root | Out-Null
        $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
        $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
        switch ($mutation) {
            'missing' { $run.markers = @($run.markers | Select-Object -Skip 1) }
            'swap' { $run.markers[1], $run.markers[2] = $run.markers[2], $run.markers[1] }
            'extra' { $run.markers += 'invented-marker' }
            'duplicate' { $run.markers += $run.markers[1] }
        }
        [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E008|M0E021|marker|schema'
    }
}

Test-Case 'Run external-noise summaries must be the exact raw noise projection' {
    $root = Join-Path $TestTempRoot 'evidence-noise-reconciliation'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
    $run.external_noise = @([ordered]@{
        noise_code = 'background-load'; observed = $false; evidence_sha256 = $null
        discard_reason = $null; error_code = $null
    })
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|noise'
}

Test-Case 'Run artifact summaries must be the exact raw artifact projection' {
    $root = Join-Path $TestTempRoot 'evidence-artifact-reconciliation'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
    $run.artifacts[0].sha256 = ('8' * 64)
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|artifact'
}

Test-Case 'Candidate input artifact hash must match the recorded candidate package' {
    $root = Join-Path $TestTempRoot 'evidence-candidate-artifact-binding'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
    $run.candidate_input.artifact_sha256 = ('8' * 64)
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|candidate.package|artifact.*input'
}

Test-Case 'A sealed run must attest exactly one matching candidate package' {
    $root = Join-Path $TestTempRoot 'evidence-candidate-artifact-required'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-process-cold-01'
    $rawPath = Join-Path $root "$runId.jsonl"
    $runPath = Join-Path $root "$runId.run.json"
    $eventSchema = Join-Path $evidenceSchemaRoot 'event.schema.json'
    $started = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $started -SchemaPath $eventSchema
    $crash = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 2 -PreviousHash $head.line_sha256 -EventName 'action_observation' -Payload ([ordered]@{
        metric_id = 'window_actionable_ns'; value_ns = $null; outcome = 'crash'; source = 'external_harness'
        start_marker_event_seq = $null; end_marker_event_seq = $null; reason_code = 'candidate_crash'; error_code = 'candidate_crash'
    })
    $head = Add-M0EvidenceEvent -LiteralPath $rawPath -Event $crash -SchemaPath $eventSchema
    $seal = New-EvidenceTestEvent -RunId $runId -CandidateCode A -Sequence 3 -PreviousHash $head.line_sha256 -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'failed'; status_reason = 'candidate_crash'; error_code = 'candidate_crash'; checks_skipped = @()
    })
    $run = New-EvidenceTestRun -RunId $runId -CandidateCode A -ArtifactHash ('f' * 64) -Status failed -StatusReason candidate_crash -ErrorCode candidate_crash
    $run.markers = @('run-started')
    $run.metrics = @()
    $run.actions = @($crash.payload)
    $run.artifacts = @()
    Assert-Throws { Complete-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SealEvent $seal -RunRecord $run -SchemaRoot $evidenceSchemaRoot } 'M0E015|candidate.package'
}

Test-Case 'Run omitted-check summaries must be the exact raw skipped-check projection' {
    $root = Join-Path $TestTempRoot 'evidence-check-reconciliation'
    New-Item -ItemType Directory -Path $root | Out-Null
    $completed = Complete-EvidenceTestRun -Root $root -CandidateCode A -ArtifactHash ('f' * 64)
    $run = Get-Content -Raw -LiteralPath $completed.run_path | ConvertFrom-Json -Depth 100
    $run.checks_omitted = @([ordered]@{ check_id = 'CHK-INVENTED'; reason_code = 'invented_check' })
    [IO.File]::WriteAllText($completed.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceRun -RawPath $completed.raw_path -RunPath $completed.run_path -SchemaRoot $evidenceSchemaRoot } 'M0E015|omitted|skipped'
}

Test-Case 'Candidate digest manifests reject toolchain drift between canonical run records' {
    $first = New-EvidenceTestRun -RunId 'run-A-process-cold-01' -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 1
    $second = New-EvidenceTestRun -RunId 'run-A-process-cold-02' -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 2
    $baseline = Get-EvidenceCandidateDigestsFromRunObjects -Runs @($first, $second) -CandidateCode A
    Assert-True ($baseline.toolchain_bundle_sha256 -match '^[a-f0-9]{64}$')
    $changed = Copy-EvidenceTestObject $second
    $changed.candidate_input.toolchains[0].version = '1.0.1'
    Assert-Throws { Get-EvidenceCandidateDigestsFromRunObjects -Runs @($first, $changed) -CandidateCode A } 'M0E016|input|invariant'
}

Test-Case 'Candidate digest manifests reject commit drift between canonical run records' {
    $first = New-EvidenceTestRun -RunId 'run-A-process-cold-01' -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 1
    $second = New-EvidenceTestRun -RunId 'run-A-process-cold-02' -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 2
    $changed = Copy-EvidenceTestObject $second
    $changed.candidate_input.git_commit = ('5' * 40)
    Assert-Throws { Get-EvidenceCandidateDigestsFromRunObjects -Runs @($first, $changed) -CandidateCode A } 'M0E016|candidate input|invariant'
}

Test-Case 'Decisive candidate digests reject consistently dirty run records' {
    $runs = foreach ($attempt in 1..2) {
        $run = New-EvidenceTestRun -RunId ('run-A-process-cold-{0:D2}' -f $attempt) -CandidateCode A -ArtifactHash ('f' * 64) -Attempt $attempt
        $run.candidate_input.dirty = $true
        $run
    }
    Assert-Throws { Get-EvidenceCandidateDigestsFromRunObjects -Runs @($runs) -CandidateCode A -RequireClean } 'M0E016.*dirty'
}

Test-Case 'Batch evidence freezes common inputs and keeps candidate artifacts distinct' {
    $root = Join-Path $TestTempRoot 'evidence-batch'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runPaths = [Collections.Generic.List[string]]::new()
    for ($attempt = 1; $attempt -le 22; $attempt++) {
        $scheduledBlock = @($evidenceSchedule.blocks | Where-Object block -eq $attempt)[0]
        foreach ($candidateCode in @($scheduledBlock.candidate_order)) {
            $artifactHash = if ($candidateCode -ceq 'A') { ('f' * 64) } else { ('9' * 64) }
            $run = Complete-EvidenceTestRun -Root $root -CandidateCode $candidateCode -ArtifactHash $artifactHash -Attempt $attempt
            $runPaths.Add($run.run_path)
        }
    }
    $candidateAPaths = @($runPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-A-*' })
    $candidateBPaths = @($runPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-B-*' })
    $snapshotSet = New-EvidenceTestSnapshotSet -Root $root -RunPaths @($runPaths) -SnapshotSetId 'process-cold-decisive-01'
    $candidateADigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $snapshotSet.snapshot_set_path -CandidateCode A -SchemaRoot $evidenceSchemaRoot
    $candidateBDigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $snapshotSet.snapshot_set_path -CandidateCode B -SchemaRoot $evidenceSchemaRoot
    $batchPath = Join-Path $root 'batch.v1.json'
    $batch = [ordered]@{
        schema_id = 'm0.batch.v1'; schema_version = 1; batch_id = 'batch-process-cold-001'
        experiment_id = 'exp-20260823-shell'; scenario_id = 'process-cold'
        common_inputs = [ordered]@{
            protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
            harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
        }
        snapshot_set_sha256 = $snapshotSet.snapshot_set_sha256
        candidate_results = @(
            [ordered]@{ candidate_code = 'A'; artifact_sha256 = ('f' * 64); toolchain_bundle_sha256 = $candidateADigests.toolchain_bundle_sha256; lockfile_bundle_sha256 = $candidateADigests.lockfile_bundle_sha256; runs_manifest_sha256 = $candidateADigests.runs_manifest_sha256; planned = 22; valid = 22; discarded = 0; failed = 0; inconclusive = 0; result = 'pass' },
            [ordered]@{ candidate_code = 'B'; artifact_sha256 = ('9' * 64); toolchain_bundle_sha256 = $candidateBDigests.toolchain_bundle_sha256; lockfile_bundle_sha256 = $candidateBDigests.lockfile_bundle_sha256; runs_manifest_sha256 = $candidateBDigests.runs_manifest_sha256; planned = 22; valid = 22; discarded = 0; failed = 0; inconclusive = 0; result = 'pass' }
        )
        decision = 'pass'
    }
    [IO.File]::WriteAllText($batchPath, (($batch | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    $validated = Assert-M0EvidenceBatch -BatchPath $batchPath -SnapshotSetPath $snapshotSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot
    Assert-Equal -Actual $validated.decision -Expected 'pass'

    $overlapRoot = Join-Path $root 'overlapping-block-1'
    New-Item -ItemType Directory -Path $overlapRoot | Out-Null
    $overlappingA = Complete-EvidenceTestRun -Root $overlapRoot -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 1 -SealDelayMilliseconds 20000
    Copy-EvidenceCommittedRuns -SourceRoot $root -DestinationRoot $overlapRoot -ExcludedRunIds @('run-A-process-cold-01')
    $overlapPaths = Get-EvidenceCommittedRunPaths -Root $overlapRoot
    $overlapAPaths = @($overlapPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-A-*' })
    $overlapSet = New-EvidenceTestSnapshotSet -Root $overlapRoot -RunPaths $overlapPaths -SnapshotSetId 'overlap-decisive'
    $overlapADigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $overlapSet.snapshot_set_path -CandidateCode A -SchemaRoot $evidenceSchemaRoot
    $overlapBatch = Copy-EvidenceTestObject $batch
    $overlapBatch.candidate_results[0].runs_manifest_sha256 = $overlapADigests.runs_manifest_sha256
    $overlapBatch.snapshot_set_sha256 = $overlapSet.snapshot_set_sha256
    $overlapBatchPath = Join-Path $overlapRoot 'overlap-batch.json'
    [IO.File]::WriteAllText($overlapBatchPath, (($overlapBatch | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $overlapBatchPath -SnapshotSetPath $overlapSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } 'M0E014|overlap|seal.*start'

    $wrongTemporalRoot = Join-Path $root 'wrong-temporal-block-2'
    New-Item -ItemType Directory -Path $wrongTemporalRoot | Out-Null
    $wrongA = Complete-EvidenceTestRun -Root $wrongTemporalRoot -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 2 -TemporalPositionOverride 1
    $wrongB = Complete-EvidenceTestRun -Root $wrongTemporalRoot -CandidateCode B -ArtifactHash ('9' * 64) -Attempt 2 -TemporalPositionOverride 2
    Copy-EvidenceCommittedRuns -SourceRoot $root -DestinationRoot $wrongTemporalRoot -ExcludedRunIds @('run-A-process-cold-02', 'run-B-process-cold-02')
    $wrongTemporalPaths = Get-EvidenceCommittedRunPaths -Root $wrongTemporalRoot
    $wrongTemporalAPaths = @($wrongTemporalPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-A-*' })
    $wrongTemporalBPaths = @($wrongTemporalPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-B-*' })
    $wrongTemporalSet = New-EvidenceTestSnapshotSet -Root $wrongTemporalRoot -RunPaths $wrongTemporalPaths -SnapshotSetId 'wrong-temporal-decisive'
    $wrongTemporalADigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $wrongTemporalSet.snapshot_set_path -CandidateCode A -SchemaRoot $evidenceSchemaRoot
    $wrongTemporalBDigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $wrongTemporalSet.snapshot_set_path -CandidateCode B -SchemaRoot $evidenceSchemaRoot
    $wrongTemporalBatch = Copy-EvidenceTestObject $batch
    $wrongTemporalBatch.candidate_results[0].runs_manifest_sha256 = $wrongTemporalADigests.runs_manifest_sha256
    $wrongTemporalBatch.candidate_results[1].runs_manifest_sha256 = $wrongTemporalBDigests.runs_manifest_sha256
    $wrongTemporalBatch.snapshot_set_sha256 = $wrongTemporalSet.snapshot_set_sha256
    $wrongTemporalBatchPath = Join-Path $wrongTemporalRoot 'wrong-temporal-batch.json'
    [IO.File]::WriteAllText($wrongTemporalBatchPath, (($wrongTemporalBatch | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $wrongTemporalBatchPath -SnapshotSetPath $wrongTemporalSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } 'M0E014|temporal|position 1'

    $swappedBlocksRoot = Join-Path $root 'wrong-temporal-blocks-2-3'
    New-Item -ItemType Directory -Path $swappedBlocksRoot | Out-Null
    $swappedRuns = @(
        (Complete-EvidenceTestRun -Root $swappedBlocksRoot -CandidateCode B -ArtifactHash ('9' * 64) -Attempt 2 -TemporalBlockOverride 3),
        (Complete-EvidenceTestRun -Root $swappedBlocksRoot -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 2 -TemporalBlockOverride 3),
        (Complete-EvidenceTestRun -Root $swappedBlocksRoot -CandidateCode A -ArtifactHash ('f' * 64) -Attempt 3 -TemporalBlockOverride 2),
        (Complete-EvidenceTestRun -Root $swappedBlocksRoot -CandidateCode B -ArtifactHash ('9' * 64) -Attempt 3 -TemporalBlockOverride 2)
    )
    Copy-EvidenceCommittedRuns -SourceRoot $root -DestinationRoot $swappedBlocksRoot -ExcludedRunIds @(
        'run-A-process-cold-02', 'run-B-process-cold-02', 'run-A-process-cold-03', 'run-B-process-cold-03'
    )
    $swappedBlockPaths = Get-EvidenceCommittedRunPaths -Root $swappedBlocksRoot
    $swappedAPaths = @($swappedBlockPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-A-*' })
    $swappedBPaths = @($swappedBlockPaths | Where-Object { (Split-Path $_ -Leaf) -like 'run-B-*' })
    $swappedSet = New-EvidenceTestSnapshotSet -Root $swappedBlocksRoot -RunPaths $swappedBlockPaths -SnapshotSetId 'swapped-blocks-decisive'
    $swappedADigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $swappedSet.snapshot_set_path -CandidateCode A -SchemaRoot $evidenceSchemaRoot
    $swappedBDigests = Get-M0EvidenceCandidateDigests -SnapshotSetPath $swappedSet.snapshot_set_path -CandidateCode B -SchemaRoot $evidenceSchemaRoot
    $swappedBatch = Copy-EvidenceTestObject $batch
    $swappedBatch.candidate_results[0].runs_manifest_sha256 = $swappedADigests.runs_manifest_sha256
    $swappedBatch.candidate_results[1].runs_manifest_sha256 = $swappedBDigests.runs_manifest_sha256
    $swappedBatch.snapshot_set_sha256 = $swappedSet.snapshot_set_sha256
    $swappedBatchPath = Join-Path $swappedBlocksRoot 'wrong-temporal-block-order.json'
    [IO.File]::WriteAllText($swappedBatchPath, (($swappedBatch | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $swappedBatchPath -SnapshotSetPath $swappedSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } 'M0E014|global|block.*order|position.*sequence'

    $sameArtifact = Copy-EvidenceTestObject $batch
    $sameArtifact.candidate_results[1].artifact_sha256 = $sameArtifact.candidate_results[0].artifact_sha256
    $samePath = Join-Path $root 'same-artifact.json'
    [IO.File]::WriteAllText($samePath, (($sameArtifact | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $samePath -SnapshotSetPath $snapshotSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } 'M0E010'

    $mutatedSchedule = Copy-EvidenceTestObject $evidenceSchedule
    $mutatedSchedule.blocks[1].order = 'AB'
    $mutatedSchedule.blocks[1].candidate_order = @('A', 'B')
    $mutatedSchedulePath = Join-Path $root 'mutated-schedule.v1.json'
    [IO.File]::WriteAllText($mutatedSchedulePath, (($mutatedSchedule | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $batchPath -SnapshotSetPath $snapshotSet.snapshot_set_path -SchedulePath $mutatedSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } 'M0E014|M0S013|schedule.*algorithm|anchored schedule'

    $wrongScheduleRoot = Join-Path $root 'wrong-schedule-run'
    New-Item -ItemType Directory -Path $wrongScheduleRoot | Out-Null
    $wrongScheduleRun = Get-Content -Raw -LiteralPath $runPaths[2] | ConvertFrom-Json -Depth 100
    $wrongScheduleOrder = if ($wrongScheduleRun.schedule.block_order -ceq 'AB') { 'BA' } else { 'AB' }
    $null = Complete-EvidenceTestRun -Root $wrongScheduleRoot -CandidateCode $wrongScheduleRun.candidate_code -ArtifactHash $wrongScheduleRun.candidate_input.artifact_sha256 -Attempt ([int]$wrongScheduleRun.attempt) -ScheduleOrderOverride $wrongScheduleOrder
    Copy-EvidenceCommittedRuns -SourceRoot $root -DestinationRoot $wrongScheduleRoot -ExcludedRunIds @($wrongScheduleRun.run_id)
    $wrongSchedulePaths = Get-EvidenceCommittedRunPaths -Root $wrongScheduleRoot
    Assert-Throws { New-EvidenceTestSnapshotSet -Root $wrongScheduleRoot -RunPaths $wrongSchedulePaths -SnapshotSetId 'wrong-schedule-decisive' } 'M0E014|M0E026|schedule|position'

    $wrongExpected = Copy-EvidenceTestObject $expectedCommonInputs
    $wrongExpected.protocol_sha256 = ('0' * 64)
    Assert-Throws { Assert-M0EvidenceBatch -BatchPath $batchPath -SnapshotSetPath $snapshotSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $wrongExpected -SchemaRoot $evidenceSchemaRoot } 'M0E016|common input'

    foreach ($digestField in @('toolchain_bundle_sha256', 'lockfile_bundle_sha256', 'runs_manifest_sha256')) {
        $wrongDigest = Copy-EvidenceTestObject $batch
        $wrongDigest.candidate_results[0].$digestField = ('0' * 64)
        $wrongDigestPath = Join-Path $root "wrong-$digestField.json"
        [IO.File]::WriteAllText($wrongDigestPath, (($wrongDigest | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
        Assert-Throws { Assert-M0EvidenceBatch -BatchPath $wrongDigestPath -SnapshotSetPath $snapshotSet.snapshot_set_path -SchedulePath $evidenceSchedulePath -ExpectedCommonInputs $expectedCommonInputs -SchemaRoot $evidenceSchemaRoot } "M0E016|$digestField"
    }
}

Test-Case 'Public runner fails closed for decisive batch validation until external candidate provenance exists' {
    $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
    $batchChild = Invoke-M0EvidenceChildScript -Name 'public-batch-fail-closed' -Source @"
`$ErrorActionPreference = 'Stop'
& '$($runner.Replace("'", "''"))' -BatchPath 'not-used.json' -SnapshotSetPath 'not-used.snapshot-set.json'
"@
    Assert-True ($batchChild.exit_code -ne 0) 'The public decisive batch child unexpectedly succeeded.'
    Assert-True ($batchChild.output -match 'M0E017|external candidate|provenance|fail.closed') "The public batch gate failed for the wrong reason: $($batchChild.output)"

    $orphanChild = Invoke-M0EvidenceChildScript -Name 'public-orphan-snapshot-set' -Source @"
`$ErrorActionPreference = 'Stop'
& '$($runner.Replace("'", "''"))' -SnapshotSetPath 'orphan.snapshot-set.json'
"@
    Assert-True ($orphanChild.exit_code -ne 0) 'The orphan snapshot-set child unexpectedly succeeded.'
    Assert-True ($orphanChild.output -match 'SnapshotSetPath.*unavailable') "The orphan snapshot-set gate failed for the wrong reason: $($orphanChild.output)"
    Assert-True (-not (Get-Command $runner).Parameters.ContainsKey('BatchRunPath')) 'Public runner still accepts loose batch run paths.'
}
