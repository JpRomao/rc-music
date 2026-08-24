$writerModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
$writerSchemaRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas'
$writerSchedule = Get-Content -Raw -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\protocol\schedule.v1.json') | ConvertFrom-Json -Depth 100

Test-Case 'Recoverable evidence writer contracts exist before publication starts' {
    foreach ($schemaName in @('evidence-intent.schema.json', 'evidence-snapshot.schema.json')) {
        Assert-True (Test-Path -LiteralPath (Join-Path $writerSchemaRoot $schemaName) -PathType Leaf) "M0E018 $schemaName is missing."
    }

    Import-Module $writerModulePath -Force -ErrorAction Stop
    $exports = @((Get-Module M0.Evidence).ExportedFunctions.Keys | Sort-Object)
    foreach ($functionName in @(
        'Open-M0EvidenceWriter', 'Close-M0EvidenceWriter',
        'Get-M0EvidenceRecoveryState', 'Resume-M0EvidenceRun', 'Assert-M0EvidenceSnapshot'
    )) {
        Assert-True ($functionName -cin $exports) "M0E018 public recovery API '$functionName' is missing."
    }
}

Import-Module $writerModulePath -Force -ErrorAction Stop

function Copy-WriterTestObject {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String)
}

function New-WriterTestEvent {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [int] $Sequence,
        [AllowNull()] $PreviousHash,
        [Parameter(Mandatory)] [string] $EventName,
        [Parameter(Mandatory)] $Payload,
        [ValidateSet('A', 'B', 'P')] [string] $CandidateCode = 'A',
        [string] $ScenarioId = 'process-cold',
        [int] $Attempt = 1
    )

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
        utc = [DateTimeOffset]::Parse('2026-08-23T12:00:00Z').AddMilliseconds($Sequence).ToString("yyyy-MM-ddTHH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
        qpc_ticks = 101000000L + $Sequence
        qpc_frequency = 10000000
        event = $EventName
        payload = $Payload
    }
}

function New-WriterTestRunRecord {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [string] $ArtifactHash = ('f' * 64),
        [ValidateSet('A', 'B', 'P')] [string] $CandidateCode = 'A'
    )

    $block = @($writerSchedule.blocks | Where-Object block -eq 1)[0]
    $position = [Array]::IndexOf([object[]]@($block.candidate_order), 'A') + 1
    return [ordered]@{
        schema_id = 'm0.run.v1'
        schema_version = 1
        experiment_id = 'exp-20260823-shell'
        run_id = $RunId
        candidate_code = $CandidateCode
        scenario_id = 'process-cold'
        attempt = 1
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
            seed = [int]$writerSchedule.seed; block = 1; block_order = $block.order; position_in_block = $position
        }
        status = 'valid'
        status_reason = $null
        error_code = $null
        raw = $null
        markers = @('run-started', 'launch-input', 'first-frame-externally-visible', 'window-actionable', 'measurement-complete')
        process_tree_sha256 = ('4' * 64)
        metrics = @([ordered]@{ metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null })
        actions = @([ordered]@{
            metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
            start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
        })
        external_noise = @()
        artifacts = @([ordered]@{
            artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = $ArtifactHash; size_bytes = 1234
        })
        checks_omitted = @()
    }
}

function New-WriterPreparedRun {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [string] $Suffix = 'base',
        [ValidateSet('A', 'B', 'P')] [string] $CandidateCode = 'A',
        [switch] $Stateful
    )

    $runId = "run-$CandidateCode-writer-$Suffix"
    $rawPath = Join-Path $Root "$runId.jsonl"
    $runPath = Join-Path $Root "$runId.run.json"
    $eventSchema = Join-Path $writerSchemaRoot 'event.schema.json'
    $writer = Open-M0EvidenceWriter -EvidenceRoot $Root -RunId $runId -SchemaRoot $writerSchemaRoot
    $specifications = @(
        [pscustomobject]@{ name = 'run_started'; payload = [ordered]@{ phase = 'started' } },
        [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'launch-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null } },
        [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'first-frame-externally-visible'; phase = 'instant'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null } },
        [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'window-actionable'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null } },
        [pscustomobject]@{ name = 'action_observation'; payload = [ordered]@{
            metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
            start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
        } },
        [pscustomobject]@{ name = 'resource_sample'; payload = [ordered]@{ metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null } },
        [pscustomobject]@{ name = 'artifact_recorded'; payload = [ordered]@{
            artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = ('f' * 64)
            size_bytes = 1234; transient = $false; redacted = $true
        } },
        [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'measurement-complete'; phase = 'end'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null } }
    )
    $previousHash = $null
    for ($index = 0; $index -lt $specifications.Count; $index++) {
        $sequence = $index + 1
        $event = New-WriterTestEvent -RunId $runId -Sequence $sequence -PreviousHash $previousHash -EventName $specifications[$index].name -Payload $specifications[$index].payload -CandidateCode $CandidateCode
        $appended = Add-M0EvidenceEvent -Writer $writer -Event $event
        $previousHash = $appended.line_sha256
    }
    $seal = New-WriterTestEvent -RunId $runId -Sequence 9 -PreviousHash $previousHash -EventName 'run_sealed' -Payload ([ordered]@{
        status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @()
    }) -CandidateCode $CandidateCode
    if (-not $Stateful) {
        $null = Close-M0EvidenceWriter -Writer $writer
        $writer = $null
    }
    return [pscustomobject][ordered]@{
        raw_path = $rawPath
        run_path = $runPath
        intent_path = Join-Path $Root "$runId.intent.json"
        snapshot_path = Join-Path $Root "$runId.snapshot.json"
        run_temp_path = Join-Path $Root "$runId.run.pending"
        snapshot_temp_path = Join-Path $Root "$runId.snapshot.pending"
        writer = $writer
        seal = $seal
        run = New-WriterTestRunRecord -RunId $runId -CandidateCode $CandidateCode
    }
}

function New-WriterSyntheticPreparedRun {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [string] $Suffix = 'pilot'
    )

    $runId = "run-P-synthetic-$Suffix"
    $rawPath = Join-Path $Root "$runId.jsonl"
    $runPath = Join-Path $Root "$runId.run.json"
    $writer = Open-M0EvidenceWriter -EvidenceRoot $Root -RunId $runId -SchemaRoot $writerSchemaRoot
    $previousHash = $null
    $specifications = @(
        [pscustomobject]@{ name = 'run_started'; payload = [ordered]@{ phase = 'started' } },
        [pscustomobject]@{ name = 'artifact_recorded'; payload = [ordered]@{
            artifact_id = 'process-tree'; relative_path = 'artifacts/process-tree.json'; sha256 = ('9' * 64)
            size_bytes = 321; transient = $false; redacted = $true
        } },
        [pscustomobject]@{ name = 'check_recorded'; payload = [ordered]@{
            check_id = 'CHK-PROC-LINEAGE'; status = 'passed'; reason_code = $null; status_reason = $null; error_code = $null
        } }
    )
    for ($index = 0; $index -lt $specifications.Count; $index++) {
        $sequence = $index + 1
        $event = New-WriterTestEvent -RunId $runId -Sequence $sequence -PreviousHash $previousHash `
            -EventName $specifications[$index].name -Payload $specifications[$index].payload `
            -CandidateCode P -ScenarioId 'synthetic-process-tree' -Attempt 1
        $appended = Add-M0EvidenceEvent -Writer $writer -Event $event
        $previousHash = $appended.line_sha256
    }
    $seal = New-WriterTestEvent -RunId $runId -Sequence 4 -PreviousHash $previousHash -EventName 'run_sealed' `
        -Payload ([ordered]@{ status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @() }) `
        -CandidateCode P -ScenarioId 'synthetic-process-tree' -Attempt 1
    $run = New-WriterTestRunRecord -RunId $runId -CandidateCode P
    $run.scenario_id = 'synthetic-process-tree'
    $run.attempt = 1
    $run.candidate_input = $null
    $run.schedule = $null
    $run.markers = @('run-started')
    $run.process_tree_sha256 = ('9' * 64)
    $run.metrics = @()
    $run.actions = @()
    $run.external_noise = @()
    $run.artifacts = @([ordered]@{
        artifact_id = 'process-tree'; relative_path = 'artifacts/process-tree.json'; sha256 = ('9' * 64); size_bytes = 321
    })
    $run.checks_omitted = @()
    return [pscustomobject][ordered]@{
        raw_path = $rawPath
        run_path = $runPath
        intent_path = Join-Path $Root "$runId.intent.json"
        snapshot_path = Join-Path $Root "$runId.snapshot.json"
        run_temp_path = Join-Path $Root "$runId.run.pending"
        snapshot_temp_path = Join-Path $Root "$runId.snapshot.pending"
        writer = $writer
        seal = $seal
        run = $run
    }
}

Test-Case 'Complete publishes a committed snapshot after durable raw and run bytes' {
    $root = Join-Path $TestTempRoot 'writer-commit'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root -Stateful
    $null = Close-M0EvidenceWriter -Writer $prepared.writer

    $completed = Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot

    Assert-Equal -Actual $completed.recovery_state -Expected 'committed'
    Assert-True (Test-Path -LiteralPath $prepared.intent_path -PathType Leaf) 'M0E022 transaction intent was not published.'
    Assert-True (Test-Path -LiteralPath $prepared.snapshot_path -PathType Leaf) 'M0E022 commit snapshot was not published.'
    $snapshot = Assert-M0EvidenceSnapshot -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $snapshot.state -Expected 'committed'
}

Test-Case 'Every persisted crash boundary has an explicit state and resumes exact bytes' {
    $cases = @(
        [pscustomobject]@{ boundary = 'intent_published'; state = 'intent_only'; run_temp = $false; snapshot_temp = $false },
        [pscustomobject]@{ boundary = 'raw_sealed'; state = 'raw_sealed'; run_temp = $false; snapshot_temp = $false },
        [pscustomobject]@{ boundary = 'run_temp_flushed'; state = 'raw_sealed'; run_temp = $true; snapshot_temp = $false },
        [pscustomobject]@{ boundary = 'run_published'; state = 'run_published'; run_temp = $false; snapshot_temp = $false },
        [pscustomobject]@{ boundary = 'snapshot_temp_flushed'; state = 'run_published'; run_temp = $false; snapshot_temp = $true },
        [pscustomobject]@{ boundary = 'committed'; state = 'committed'; run_temp = $false; snapshot_temp = $false }
    )
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot "writer-crash-$($case.boundary)"
        New-Item -ItemType Directory -Path $root | Out-Null
        $prepared = New-WriterPreparedRun -Root $root -Suffix $case.boundary
        Assert-Throws {
            Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary $case.boundary
        } "M0E023|$($case.boundary)"

        $state = Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
        Assert-Equal -Actual $state.state -Expected $case.state "Wrong recovery state after $($case.boundary)."
        Assert-Equal -Actual $state.run_temp_present -Expected $case.run_temp
        Assert-Equal -Actual $state.snapshot_temp_present -Expected $case.snapshot_temp
        if ($case.state -cne 'committed') {
            Assert-Throws {
                Assert-M0EvidenceSnapshot -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
            } 'M0E022|snapshot|committed'
        }
        $resumed = Resume-M0EvidenceRun -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
        Assert-Equal -Actual $resumed.recovery_state -Expected 'committed'
        Assert-True (-not (Test-Path -LiteralPath $prepared.run_temp_path)) 'Recovered run temp was not consumed by atomic rename.'
        Assert-True (-not (Test-Path -LiteralPath $prepared.snapshot_temp_path)) 'Recovered snapshot temp was not consumed by atomic rename.'
        $committed = Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
        Assert-Equal -Actual $committed.state -Expected 'committed'
    }
}

Test-Case 'Completion is idempotent only for the exact immutable intent' {
    $root = Join-Path $TestTempRoot 'writer-idempotent'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root -Stateful
    try {
        $first = Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $prepared.run -EvidenceRole candidate_run
        $intentBytes = [IO.File]::ReadAllBytes($prepared.intent_path)
        $snapshotBytes = [IO.File]::ReadAllBytes($prepared.snapshot_path)

        $second = Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $prepared.run -EvidenceRole candidate_run
        Assert-Equal -Actual $second.transaction_id -Expected $first.transaction_id
        Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($intentBytes, [IO.File]::ReadAllBytes($prepared.intent_path))) 'Idempotent replay changed intent bytes.'
        Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($snapshotBytes, [IO.File]::ReadAllBytes($prepared.snapshot_path))) 'Idempotent replay changed snapshot bytes.'

        $divergent = Copy-WriterTestObject $prepared.run
        $divergent.process_tree_sha256 = ('5' * 64)
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $divergent -EvidenceRole candidate_run
        } 'M0E019|immutable.*intent|diverge'
    }
    finally {
        $null = Close-M0EvidenceWriter -Writer $prepared.writer
    }
}

Test-Case 'Preflight failure leaves no intent, seal, run or snapshot' {
    $root = Join-Path $TestTempRoot 'writer-preflight'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root -Stateful
    $null = Close-M0EvidenceWriter -Writer $prepared.writer
    $invalid = Copy-WriterTestObject $prepared.run
    $invalid.actions[0].value_ns = 201
    $rawBytes = [IO.File]::ReadAllBytes($prepared.raw_path)
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $invalid -SchemaRoot $writerSchemaRoot
    } 'M0E015|action'
    Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($rawBytes, [IO.File]::ReadAllBytes($prepared.raw_path))) 'Failed preflight changed raw bytes.'
    foreach ($path in @($prepared.intent_path, $prepared.run_path, $prepared.snapshot_path, $prepared.run_temp_path, $prepared.snapshot_temp_path)) {
        Assert-True (-not (Test-Path -LiteralPath $path)) "Failed preflight left '$path'."
    }

    Assert-Throws {
        Add-M0EvidenceEvent -EvidenceRoot $root -LiteralPath $prepared.raw_path -Event $prepared.seal -SchemaPath (Join-Path $writerSchemaRoot 'event.schema.json')
    } 'M0E018|run_sealed|Complete'
    Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($rawBytes, [IO.File]::ReadAllBytes($prepared.raw_path))) 'Rejected public seal append changed raw bytes.'
}

Test-Case 'Recovery rejects stale tails and never truncates unjournaled bytes' {
    $root = Join-Path $TestTempRoot 'writer-tail-conflict'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary intent_published
    } 'M0E023'
    [IO.File]::AppendAllText($prepared.raw_path, '{', [Text.UTF8Encoding]::new($false))
    $length = (Get-Item -LiteralPath $prepared.raw_path).Length
    $state = Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $state.state -Expected 'conflict'
    Assert-Equal -Actual $state.reason_code -Expected 'unjournaled_raw_tail'
    Assert-Throws {
        Resume-M0EvidenceRun -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    } 'M0E019|tail|truncat'
    Assert-Equal -Actual (Get-Item -LiteralPath $prepared.raw_path).Length -Expected $length 'Recovery truncated an unjournaled partial tail.'

    $completeRoot = Join-Path $TestTempRoot 'writer-unjournaled-complete'
    New-Item -ItemType Directory -Path $completeRoot | Out-Null
    $complete = New-WriterPreparedRun -Root $completeRoot
    $sealLine = $complete.seal | ConvertTo-Json -Compress -Depth 100
    [IO.File]::AppendAllText($complete.raw_path, $sealLine + "`n", [Text.UTF8Encoding]::new($false))
    $completeState = Get-M0EvidenceRecoveryState -EvidenceRoot $completeRoot -RunId $complete.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $completeState.state -Expected 'conflict'
    Assert-Equal -Actual $completeState.reason_code -Expected 'unjournaled_seal'

    $staleRoot = Join-Path $TestTempRoot 'writer-stale-prefix'
    New-Item -ItemType Directory -Path $staleRoot | Out-Null
    $stale = New-WriterPreparedRun -Root $staleRoot
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $staleRoot -RawPath $stale.raw_path -RunPath $stale.run_path -SealEvent $stale.seal -RunRecord $stale.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary intent_published
    } 'M0E023'
    $staleBytes = [IO.File]::ReadAllBytes($stale.raw_path)
    $staleBytes[0] = if ($staleBytes[0] -eq 0x7B) { 0x5B } else { 0x7B }
    [IO.File]::WriteAllBytes($stale.raw_path, $staleBytes)
    $staleState = Get-M0EvidenceRecoveryState -EvidenceRoot $staleRoot -RunId $stale.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $staleState.state -Expected 'conflict'
    Assert-Equal -Actual $staleState.reason_code -Expected 'stale_precondition'
    Assert-Throws {
        Resume-M0EvidenceRun -EvidenceRoot $staleRoot -RunId $stale.run.run_id -SchemaRoot $writerSchemaRoot
    } 'M0E019|precondition|stale'
    Assert-True (-not (Test-Path -LiteralPath $stale.run_path)) 'Stale precondition published a run.'
    Assert-True (-not (Test-Path -LiteralPath $stale.snapshot_path)) 'Stale precondition published a snapshot.'
}

Test-Case 'Intent, temp, run and snapshot tamper map to closed recovery failures' {
    $intentRoot = Join-Path $TestTempRoot 'writer-intent-tamper'
    New-Item -ItemType Directory -Path $intentRoot | Out-Null
    $intentPrepared = New-WriterPreparedRun -Root $intentRoot
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $intentRoot -RawPath $intentPrepared.raw_path -RunPath $intentPrepared.run_path -SealEvent $intentPrepared.seal -RunRecord $intentPrepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary intent_published
    } 'M0E023'
    $intent = Get-Content -Raw -LiteralPath $intentPrepared.intent_path | ConvertFrom-Json -Depth 100
    $intent.expected.run.sha256 = ('0' + $intent.expected.run.sha256.Substring(1))
    [IO.File]::WriteAllText($intentPrepared.intent_path, (($intent | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Equal -Actual (Get-M0EvidenceRecoveryState -EvidenceRoot $intentRoot -RunId $intentPrepared.run.run_id -SchemaRoot $writerSchemaRoot).state -Expected 'corrupt'
    Assert-Throws { Resume-M0EvidenceRun -EvidenceRoot $intentRoot -RunId $intentPrepared.run.run_id -SchemaRoot $writerSchemaRoot } 'M0E021|intent|bytes|SHA'

    $tempRoot = Join-Path $TestTempRoot 'writer-temp-tamper'
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    $tempPrepared = New-WriterPreparedRun -Root $tempRoot
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $tempRoot -RawPath $tempPrepared.raw_path -RunPath $tempPrepared.run_path -SealEvent $tempPrepared.seal -RunRecord $tempPrepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary run_temp_flushed
    } 'M0E023'
    [IO.File]::AppendAllText($tempPrepared.run_temp_path, "`n", [Text.UTF8Encoding]::new($false))
    $tempState = Get-M0EvidenceRecoveryState -EvidenceRoot $tempRoot -RunId $tempPrepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $tempState.state -Expected 'corrupt'
    Assert-Equal -Actual $tempState.reason_code -Expected 'run_temp_corrupt'

    $runRoot = Join-Path $TestTempRoot 'writer-run-divergent'
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $runPrepared = New-WriterPreparedRun -Root $runRoot
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $runRoot -RawPath $runPrepared.raw_path -RunPath $runPrepared.run_path -SealEvent $runPrepared.seal -RunRecord $runPrepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary run_published
    } 'M0E023'
    $run = Get-Content -Raw -LiteralPath $runPrepared.run_path | ConvertFrom-Json -Depth 100
    $run.process_tree_sha256 = ('5' * 64)
    [IO.File]::WriteAllText($runPrepared.run_path, (($run | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    $runState = Get-M0EvidenceRecoveryState -EvidenceRoot $runRoot -RunId $runPrepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $runState.state -Expected 'conflict'
    Assert-Equal -Actual $runState.reason_code -Expected 'run_divergent'

    $snapshotRoot = Join-Path $TestTempRoot 'writer-snapshot-tamper'
    New-Item -ItemType Directory -Path $snapshotRoot | Out-Null
    $snapshotPrepared = New-WriterPreparedRun -Root $snapshotRoot
    $null = Complete-M0EvidenceRun -EvidenceRoot $snapshotRoot -RawPath $snapshotPrepared.raw_path -RunPath $snapshotPrepared.run_path -SealEvent $snapshotPrepared.seal -RunRecord $snapshotPrepared.run -SchemaRoot $writerSchemaRoot
    $snapshot = Get-Content -Raw -LiteralPath $snapshotPrepared.snapshot_path | ConvertFrom-Json -Depth 100
    $snapshot.transaction_id = ('0' + $snapshot.transaction_id.Substring(1))
    [IO.File]::WriteAllText($snapshotPrepared.snapshot_path, (($snapshot | ConvertTo-Json -Compress -Depth 100) + "`n"), [Text.UTF8Encoding]::new($false))
    Assert-Equal -Actual (Get-M0EvidenceRecoveryState -EvidenceRoot $snapshotRoot -RunId $snapshotPrepared.run.run_id -SchemaRoot $writerSchemaRoot).state -Expected 'corrupt'
    Assert-Throws { Assert-M0EvidenceSnapshot -EvidenceRoot $snapshotRoot -RunId $snapshotPrepared.run.run_id -SchemaRoot $writerSchemaRoot } 'M0E022|snapshot'

    $noOverwriteRoot = Join-Path $TestTempRoot 'writer-snapshot-no-overwrite'
    New-Item -ItemType Directory -Path $noOverwriteRoot | Out-Null
    $noOverwrite = New-WriterPreparedRun -Root $noOverwriteRoot
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $noOverwriteRoot -RawPath $noOverwrite.raw_path -RunPath $noOverwrite.run_path -SealEvent $noOverwrite.seal -RunRecord $noOverwrite.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary snapshot_temp_flushed
    } 'M0E023'
    $divergentSnapshotBytes = [Text.UTF8Encoding]::new($false).GetBytes("{}`n")
    [IO.File]::WriteAllBytes($noOverwrite.snapshot_path, $divergentSnapshotBytes)
    Assert-Throws {
        Resume-M0EvidenceRun -EvidenceRoot $noOverwriteRoot -RunId $noOverwrite.run.run_id -SchemaRoot $writerSchemaRoot
    } 'M0E022|snapshot.*diverge'
    Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($divergentSnapshotBytes, [IO.File]::ReadAllBytes($noOverwrite.snapshot_path))) 'Recovery overwrote an existing divergent snapshot destination.'
}

Test-Case 'A final run remains invisible until the final no-overwrite snapshot commit' {
    $root = Join-Path $TestTempRoot 'writer-run-published'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary run_published
    } 'M0E023'
    Assert-Equal -Actual (Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot).state -Expected 'run_published'
    Assert-Throws {
        Assert-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SchemaRoot $writerSchemaRoot
    } 'M0E022|snapshot|committed'

    $null = Resume-M0EvidenceRun -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    Remove-Item -LiteralPath $prepared.snapshot_path -Force
    Assert-Equal -Actual (Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot).state -Expected 'run_published'
    Assert-Throws {
        Assert-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SchemaRoot $writerSchemaRoot
    } 'M0E022|snapshot|committed'
    $recovered = Resume-M0EvidenceRun -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $recovered.recovery_state -Expected 'committed'

    $lateEvent = New-WriterTestEvent -RunId $prepared.run.run_id -Sequence 10 -PreviousHash $recovered.final_line_sha256 -EventName 'check_recorded' -Payload ([ordered]@{
        check_id = 'CHK-LATE'; status = 'passed'; reason_code = $null; status_reason = $null; error_code = $null
    })
    Assert-Throws {
        Add-M0EvidenceEvent -EvidenceRoot $root -LiteralPath $prepared.raw_path -Event $lateEvent -SchemaPath (Join-Path $writerSchemaRoot 'event.schema.json')
    } 'M0E019|intent|append'
}

Test-Case 'The stable root lock serializes writers and is never deleted' {
    $root = Join-Path $TestTempRoot 'writer-lock'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    $lockPath = Join-Path $root '.m0-evidence.lock'
    $held = [IO.FileStream]::new($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $state = Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
        Assert-Equal -Actual $state.state -Expected 'conflict'
        Assert-Equal -Actual $state.reason_code -Expected 'writer_locked'
        Assert-Throws {
            Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot
        } 'M0E020|lock'
    }
    finally {
        $held.Dispose()
    }

    $completed = Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $completed.recovery_state -Expected 'committed'
    Assert-True (Test-Path -LiteralPath $lockPath -PathType Leaf) 'The stable lock sentinel was deleted after completion.'
}

Test-Case 'Readers never create the stable lock and reject evidence whose sentinel is missing' {
    $emptyRoot = Join-Path $TestTempRoot 'writer-reader-empty'
    New-Item -ItemType Directory -Path $emptyRoot | Out-Null
    $emptyLock = Join-Path $emptyRoot '.m0-evidence.lock'
    $state = Get-M0EvidenceRecoveryState -EvidenceRoot $emptyRoot -RunId 'run-A-reader-empty' -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $state.state -Expected 'unstarted'
    Assert-True (-not (Test-Path -LiteralPath $emptyLock)) 'Read-only recovery state materialized the lock sentinel.'
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $emptyRoot -Force).Count -Expected 0 'Read-only recovery state changed an empty EvidenceRoot.'

    $root = Join-Path $TestTempRoot 'writer-reader-missing-lock'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    $null = Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path `
        -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot
    $lockPath = Join-Path $root '.m0-evidence.lock'
    Remove-Item -LiteralPath $lockPath -Force
    Assert-Throws {
        Assert-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SchemaRoot $writerSchemaRoot
    } 'M0E025|sentinel|lock'
    Assert-Throws {
        Assert-M0EvidenceSnapshot -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    } 'M0E025|sentinel|lock'
    Assert-Equal -Actual (Get-M0EvidenceRecoveryState -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot).reason_code -Expected 'lock_missing'
    Assert-True (-not (Test-Path -LiteralPath $lockPath)) 'A reader recreated the missing lock sentinel beside evidence data.'
}

Test-Case 'Only an empty EvidenceRoot may create the initial stable lock sentinel' {
    $occupiedRoot = Join-Path $TestTempRoot 'writer-open-occupied-without-lock'
    New-Item -ItemType Directory -Path $occupiedRoot | Out-Null
    $preexistingPath = Join-Path $occupiedRoot 'preexisting.txt'
    [IO.File]::WriteAllText($preexistingPath, "owned-by-caller`n", [Text.UTF8Encoding]::new($false))
    $beforeBytes = [IO.File]::ReadAllBytes($preexistingPath)
    Assert-Throws {
        $unexpectedWriter = Open-M0EvidenceWriter -EvidenceRoot $occupiedRoot -RunId 'run-A-open-occupied' -SchemaRoot $writerSchemaRoot
        try { throw 'M0T001 Open accepted a nonempty root without its sentinel.' }
        finally { Close-M0EvidenceWriter -Writer $unexpectedWriter }
    } 'M0E025|M0E020|sentinel|nonempty|conflict'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $occupiedRoot '.m0-evidence.lock'))) 'Open created a sentinel in a nonempty root.'
    Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($preexistingPath)) -Expected @($beforeBytes) 'Open changed preexisting root contents.'

    $resumeRoot = Join-Path $TestTempRoot 'writer-resume-missing-lock'
    New-Item -ItemType Directory -Path $resumeRoot | Out-Null
    $prepared = New-WriterPreparedRun -Root $resumeRoot -Suffix 'resume-missing-lock'
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $resumeRoot -RawPath $prepared.raw_path -RunPath $prepared.run_path `
            -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary run_published
    } 'M0E023.*run_published'
    $lockPath = Join-Path $resumeRoot '.m0-evidence.lock'
    Remove-Item -LiteralPath $lockPath -Force
    Assert-True (-not (Test-Path -LiteralPath $prepared.snapshot_path)) 'Crash fixture unexpectedly published a snapshot.'
    Assert-Throws {
        Resume-M0EvidenceRun -EvidenceRoot $resumeRoot -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    } 'M0E025|M0E020|sentinel|lock.*missing|conflict'
    Assert-True (-not (Test-Path -LiteralPath $lockPath)) 'Resume recreated a missing sentinel beside transaction artifacts.'
    Assert-True (-not (Test-Path -LiteralPath $prepared.snapshot_path)) 'Resume published a snapshot without the original sentinel.'
}

Test-Case 'EvidenceRoot containment and link-free identity are fail closed' {
    $root = Join-Path $TestTempRoot 'writer-paths'
    $outside = Join-Path $TestTempRoot 'writer-paths-outside'
    New-Item -ItemType Directory -Path $root, $outside | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    $outsideRaw = Join-Path $outside (Split-Path $prepared.raw_path -Leaf)
    [IO.File]::Copy($prepared.raw_path, $outsideRaw)
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $outsideRaw -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot
    } 'M0E018|derived|EvidenceRoot'

    if ($IsWindows) {
        $sourceRoot = Join-Path $TestTempRoot 'writer-hardlink-source'
        $linkRoot = Join-Path $TestTempRoot 'writer-hardlink-root'
        New-Item -ItemType Directory -Path $sourceRoot, $linkRoot | Out-Null
        $source = New-WriterPreparedRun -Root $sourceRoot -Suffix 'hardlink'
        $linkedRaw = Join-Path $linkRoot (Split-Path $source.raw_path -Leaf)
        $null = New-Item -ItemType HardLink -Path $linkedRaw -Target $source.raw_path
        Assert-Throws {
            Get-M0EvidenceRecoveryState -EvidenceRoot $linkRoot -RunId $source.run.run_id -SchemaRoot $writerSchemaRoot
        } 'M0E005|link'

        $realRoot = Join-Path $TestTempRoot 'writer-junction-real'
        $junctionRoot = Join-Path $TestTempRoot 'writer-junction-root'
        New-Item -ItemType Directory -Path $realRoot | Out-Null
        $null = New-Item -ItemType Junction -Path $junctionRoot -Target $realRoot
        Assert-Throws {
            Get-M0EvidenceRecoveryState -EvidenceRoot $junctionRoot -RunId 'run-A-writer-junction' -SchemaRoot $writerSchemaRoot
        } 'M0E005|link'
    }
}

Test-Case 'Post-open hardlinks invalidate identity before intent and every failed open releases handles' {
    if (-not $IsWindows) { return }

    $root = Join-Path $TestTempRoot 'writer-post-open-hardlink'
    $outside = Join-Path $TestTempRoot 'writer-post-open-hardlink-outside'
    New-Item -ItemType Directory -Path $root, $outside | Out-Null
    $prepared = New-WriterPreparedRun -Root $root -Suffix 'post-open-link' -Stateful
    $rawLink = Join-Path $outside 'raw-link.jsonl'
    $null = New-Item -ItemType HardLink -Path $rawLink -Target $prepared.raw_path
    try {
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $prepared.run -EvidenceRole candidate_run
        } 'M0E005|identity|link'
        Assert-True (-not (Test-Path -LiteralPath $prepared.intent_path)) 'Identity failure published an intent.'
        Assert-True (-not (Test-Path -LiteralPath $prepared.snapshot_path)) 'Identity failure published a snapshot.'
    }
    finally {
        $null = Close-M0EvidenceWriter -Writer $prepared.writer
    }
    $rawExclusive = [IO.FileStream]::new($prepared.raw_path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $rawExclusive.Dispose()
    Remove-Item -LiteralPath $rawLink -Force

    $leakRoot = Join-Path $TestTempRoot 'writer-post-open-lock-leak'
    $leakOutside = Join-Path $TestTempRoot 'writer-post-open-lock-leak-outside'
    New-Item -ItemType Directory -Path $leakRoot, $leakOutside | Out-Null
    $lockPath = Join-Path $leakRoot '.m0-evidence.lock'
    [IO.File]::WriteAllBytes($lockPath, [byte[]]::new(0))
    $lockLink = Join-Path $leakOutside 'lock-link'
    $null = New-Item -ItemType HardLink -Path $lockLink -Target $lockPath
    Assert-Throws {
        Open-M0EvidenceWriter -EvidenceRoot $leakRoot -RunId 'run-A-lock-leak' -SchemaRoot $writerSchemaRoot
    } 'M0E005|link'
    $lockExclusive = [IO.FileStream]::new($lockPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    $lockExclusive.Dispose()
    Remove-Item -LiteralPath $lockLink -Force
}

Test-Case 'Temp swap and pre-snapshot hardlink hooks fail before the snapshot commit point' {
    if (-not $IsWindows) { return }
    $module = Get-Module M0.Evidence

    $swapRoot = Join-Path $TestTempRoot 'writer-run-temp-swap'
    New-Item -ItemType Directory -Path $swapRoot | Out-Null
    $swap = New-WriterPreparedRun -Root $swapRoot -Suffix 'run-temp-swap' -Stateful
    & $module { param($hook) $script:M0EvidenceIdentityTestHook = $hook } ([pscustomobject]@{
        stage = 'run_temp_after_flush_before_move'; mode = 'replace_same_bytes'
    })
    try {
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $swap.writer -SealEvent $swap.seal -RunRecord $swap.run -EvidenceRole candidate_run
        } 'M0E005|identity|rename'
        Assert-True (-not (Test-Path -LiteralPath $swap.snapshot_path)) 'Temp identity swap published a commit snapshot.'
    }
    finally { $null = Close-M0EvidenceWriter -Writer $swap.writer }
    $runExclusive = [IO.FileStream]::new($swap.run_path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $runExclusive.Dispose()

    $linkRoot = Join-Path $TestTempRoot 'writer-before-snapshot-link'
    $outside = Join-Path $TestTempRoot 'writer-before-snapshot-link-outside'
    New-Item -ItemType Directory -Path $linkRoot, $outside | Out-Null
    $linked = New-WriterPreparedRun -Root $linkRoot -Suffix 'before-snapshot-link' -Stateful
    $linkPath = Join-Path $outside 'raw-link.jsonl'
    & $module { param($hook) $script:M0EvidenceIdentityTestHook = $hook } ([pscustomobject]@{
        stage = 'before_snapshot_identity_recheck'; mode = 'create_hardlink'; link_path = $linkPath
    })
    try {
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $linked.writer -SealEvent $linked.seal -RunRecord $linked.run -EvidenceRole candidate_run
        } 'M0E005|identity|link'
        Assert-True (-not (Test-Path -LiteralPath $linked.snapshot_path)) 'Hardlink before identity recheck published a commit snapshot.'
    }
    finally { $null = Close-M0EvidenceWriter -Writer $linked.writer }
    Remove-Item -LiteralPath $linkPath -Force
    $rawExclusive = [IO.FileStream]::new($linked.raw_path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $rawExclusive.Dispose()
}

Test-Case 'Committed transaction bytes cannot be relocated under another RunId leaf' {
    $sourceRoot = Join-Path $TestTempRoot 'writer-leaf-source'
    $targetRoot = Join-Path $TestTempRoot 'writer-leaf-target'
    New-Item -ItemType Directory -Path $sourceRoot, $targetRoot | Out-Null
    $prepared = New-WriterPreparedRun -Root $sourceRoot -Suffix 'leaf-source'
    $null = Complete-M0EvidenceRun -EvidenceRoot $sourceRoot -RawPath $prepared.raw_path -RunPath $prepared.run_path `
        -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot

    $targetRunId = 'run-A-writer-leaf-target'
    foreach ($mapping in @(
        [pscustomobject]@{ source = $prepared.raw_path; suffix = '.jsonl' },
        [pscustomobject]@{ source = $prepared.run_path; suffix = '.run.json' },
        [pscustomobject]@{ source = $prepared.intent_path; suffix = '.intent.json' },
        [pscustomobject]@{ source = $prepared.snapshot_path; suffix = '.snapshot.json' }
    )) {
        [IO.File]::Copy($mapping.source, (Join-Path $targetRoot "$targetRunId$($mapping.suffix)"), $false)
    }
    [IO.File]::WriteAllBytes((Join-Path $targetRoot '.m0-evidence.lock'), [byte[]]::new(0))
    Assert-Throws {
        Assert-M0EvidenceRun -EvidenceRoot $targetRoot -RawPath (Join-Path $targetRoot "$targetRunId.jsonl") `
            -RunPath (Join-Path $targetRoot "$targetRunId.run.json") -SchemaRoot $writerSchemaRoot
    } 'M0E018|RunId|leaf|intent.*path'
}

Test-Case 'Snapshot role conditionals are closed for candidate and future synthetic evidence' {
    $root = Join-Path $TestTempRoot 'writer-snapshot-schema'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    $null = Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot
    $schemaPath = Join-Path $writerSchemaRoot 'evidence-snapshot.schema.json'
    $candidate = Get-Content -Raw -LiteralPath $prepared.snapshot_path | ConvertFrom-Json -Depth 100

    $candidate.decision_eligible = $false
    Assert-True (-not (Test-Json -Json ($candidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) 'candidate_run=false must fail schema validation.'
    $candidate.evidence_role = 'synthetic_pilot'
    Assert-True (Test-Json -Json ($candidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue) 'synthetic_pilot=false should be format-valid for the future non-decisive pilot.'
    $candidate.decision_eligible = $true
    Assert-True (-not (Test-Json -Json ($candidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) 'synthetic_pilot=true must fail schema validation.'
    $candidate.decision_eligible = $false
    $candidate | Add-Member -NotePropertyName invented -NotePropertyValue $true
    Assert-True (-not (Test-Json -Json ($candidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) 'Snapshot schemas must reject extra properties.'
}

Test-Case 'Candidate digest readers reject loose run paths and run-published evidence remains invisible' {
    $root = Join-Path $TestTempRoot 'writer-digest-invisible'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root
    Assert-Throws {
        Complete-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SealEvent $prepared.seal -RunRecord $prepared.run -SchemaRoot $writerSchemaRoot -CrashAfterBoundary run_published
    } 'M0E023'
    $digestCommand = Get-Command Get-M0EvidenceCandidateDigests
    Assert-True (-not $digestCommand.Parameters.ContainsKey('RunPaths')) 'Decision digest API still accepts a loose run-path inventory.'
}

Test-Case 'A stateful writer owns one root lock and rejects forged, cross-run and closed contexts' {
    $root = Join-Path $TestTempRoot 'writer-stateful'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterPreparedRun -Root $root -Suffix 'stateful' -Stateful
    Assert-Throws {
        Open-M0EvidenceWriter -EvidenceRoot $root -RunId 'run-A-writer-concurrent' -SchemaRoot $writerSchemaRoot
    } 'M0E020|lock'

    $forged = [pscustomobject]@{ writer_token = ('0' * 64); run_id = $prepared.run.run_id; state = 'open' }
    Assert-Throws { Add-M0EvidenceEvent -Writer $forged -Event $prepared.seal } 'M0E024|writer.*context|forged'
    $crossRun = Copy-WriterTestObject $prepared.seal
    $crossRun.run_id = 'run-A-writer-other'
    Assert-Throws { Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $crossRun -RunRecord $prepared.run -EvidenceRole candidate_run } 'M0E024|cross.run|RunId'

    $completed = Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $prepared.run -EvidenceRole candidate_run
    Assert-Equal -Actual $completed.recovery_state -Expected 'committed'
    $closed = Close-M0EvidenceWriter -Writer $prepared.writer
    Assert-Equal -Actual $closed.state -Expected 'closed'
    Assert-Equal -Actual (Close-M0EvidenceWriter -Writer $prepared.writer).state -Expected 'closed'
    Assert-Throws { Add-M0EvidenceEvent -Writer $prepared.writer -Event $prepared.seal } 'M0E024|closed|writer.*context'

    $reloadRoot = Join-Path $TestTempRoot 'writer-stateful-reload'
    New-Item -ItemType Directory -Path $reloadRoot | Out-Null
    $old = Open-M0EvidenceWriter -EvidenceRoot $reloadRoot -RunId 'run-A-writer-reload' -SchemaRoot $writerSchemaRoot
    Import-Module $writerModulePath -Force -ErrorAction Stop
    Assert-Throws { Close-M0EvidenceWriter -Writer $old } 'M0E024|module|context'
    $replacement = Open-M0EvidenceWriter -EvidenceRoot $reloadRoot -RunId 'run-A-writer-reload' -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual (Close-M0EvidenceWriter -Writer $replacement).state -Expected 'closed'
}

Test-Case 'The public entrypoint clean-process gate preserves an active writer session' {
    $root = Join-Path $TestTempRoot 'entrypoint-preserves-writer'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-entrypoint-preserves-writer'
    $writer = Open-M0EvidenceWriter -EvidenceRoot $root -RunId $runId -SchemaRoot $writerSchemaRoot
    try {
        $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
        Assert-Throws {
            & $runner -BatchPath 'not-used.json' -SnapshotSetPath 'not-used.snapshot-set.json'
        } 'M0E027|clean PowerShell process|preloaded'

        $event = New-WriterTestEvent -RunId $runId -Sequence 1 -PreviousHash $null -EventName 'run_started' -Payload ([ordered]@{ phase = 'started' })
        $appended = Add-M0EvidenceEvent -Writer $writer -Event $event
        Assert-Equal -Actual $appended.event_seq -Expected 1
        Assert-True (-not $appended.replay) 'The first append after entrypoint rejection was unexpectedly classified as replay.'
    }
    finally {
        $closed = Close-M0EvidenceWriter -Writer $writer
    }
    Assert-Equal -Actual $closed.state -Expected 'closed'
}

Test-Case 'A lost append response can replay only the byte-identical head event' {
    $root = Join-Path $TestTempRoot 'writer-replay'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-A-writer-replay'
    $writer = Open-M0EvidenceWriter -EvidenceRoot $root -RunId $runId -SchemaRoot $writerSchemaRoot
    try {
        $firstEvent = New-WriterTestEvent -RunId $runId -Sequence 1 -PreviousHash $null -EventName run_started -Payload ([ordered]@{ phase = 'started' })
        $first = Add-M0EvidenceEvent -Writer $writer -Event $firstEvent
        $length = (Get-Item -LiteralPath (Join-Path $root "$runId.jsonl")).Length
        $replay = Add-M0EvidenceEvent -Writer $writer -Event $firstEvent
        Assert-True ([bool]$replay.replay) 'Exact head replay was not identified.'
        Assert-Equal -Actual $replay.event_seq -Expected $first.event_seq
        Assert-Equal -Actual $replay.line_sha256 -Expected $first.line_sha256
        Assert-Equal -Actual (Get-Item -LiteralPath (Join-Path $root "$runId.jsonl")).Length -Expected $length 'Exact replay appended bytes.'

        $divergent = Copy-WriterTestObject $firstEvent
        $divergent.utc = '2026-08-23T12:00:01.001Z'
        Assert-Throws { Add-M0EvidenceEvent -Writer $writer -Event $divergent } 'M0E003|divergent|replay'
        $secondEvent = New-WriterTestEvent -RunId $runId -Sequence 2 -PreviousHash $first.line_sha256 -EventName marker -Payload ([ordered]@{
            marker_id = 'launch-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null
        })
        $null = Add-M0EvidenceEvent -Writer $writer -Event $secondEvent
        Assert-Throws { Add-M0EvidenceEvent -Writer $writer -Event $firstEvent } 'M0E003|older|replay'
        $stale = New-WriterTestEvent -RunId $runId -Sequence 3 -PreviousHash ('0' * 64) -EventName marker -Payload ([ordered]@{
            marker_id = 'first-frame-externally-visible'; phase = 'instant'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null
        })
        Assert-Throws { Add-M0EvidenceEvent -Writer $writer -Event $stale } 'M0E003|previous|hash|stale'
    }
    finally {
        $null = Close-M0EvidenceWriter -Writer $writer
    }
}

Test-Case 'Synthetic pilot evidence is structurally committed but never decision eligible' {
    $root = Join-Path $TestTempRoot 'writer-role-pilot'
    New-Item -ItemType Directory -Path $root | Out-Null
    $prepared = New-WriterSyntheticPreparedRun -Root $root -Suffix 'pilot'
    try {
        $completed = Complete-M0EvidenceRun -Writer $prepared.writer -SealEvent $prepared.seal -RunRecord $prepared.run -EvidenceRole synthetic_pilot
        Assert-Equal -Actual $completed.recovery_state -Expected 'committed'
    }
    finally {
        $null = Close-M0EvidenceWriter -Writer $prepared.writer
    }
    $snapshot = Assert-M0EvidenceSnapshot -EvidenceRoot $root -RunId $prepared.run.run_id -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $snapshot.evidence_role -Expected 'synthetic_pilot'
    Assert-Equal -Actual $snapshot.decision_eligible -Expected $false
    $run = Assert-M0EvidenceRun -EvidenceRoot $root -RawPath $prepared.raw_path -RunPath $prepared.run_path -SchemaRoot $writerSchemaRoot
    Assert-Equal -Actual $run.evidence_role -Expected 'synthetic_pilot'
    Assert-True (-not (Get-Command Get-M0EvidenceCandidateDigests).Parameters.ContainsKey('RunPaths')) 'Synthetic evidence could reach a loose-path decision digest API.'

    $pilotMismatchRoot = Join-Path $TestTempRoot 'writer-role-mismatch-P-candidate'
    New-Item -ItemType Directory -Path $pilotMismatchRoot | Out-Null
    $pilotMismatch = New-WriterSyntheticPreparedRun -Root $pilotMismatchRoot -Suffix 'mismatch'
    try {
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $pilotMismatch.writer -SealEvent $pilotMismatch.seal -RunRecord $pilotMismatch.run -EvidenceRole candidate_run
        } 'M0E022|role|candidate'
    }
    finally { $null = Close-M0EvidenceWriter -Writer $pilotMismatch.writer }

    $candidateMismatchRoot = Join-Path $TestTempRoot 'writer-role-mismatch-A-pilot'
    New-Item -ItemType Directory -Path $candidateMismatchRoot | Out-Null
    $candidateMismatch = New-WriterPreparedRun -Root $candidateMismatchRoot -Suffix 'mismatch' -CandidateCode A -Stateful
    try {
        Assert-Throws {
            Complete-M0EvidenceRun -Writer $candidateMismatch.writer -SealEvent $candidateMismatch.seal -RunRecord $candidateMismatch.run -EvidenceRole synthetic_pilot
        } 'M0E022|role|candidate'
    }
    finally { $null = Close-M0EvidenceWriter -Writer $candidateMismatch.writer }

    $runSchemaPath = Join-Path $writerSchemaRoot 'run.schema.json'
    $fakePilot = Copy-WriterTestObject $prepared.run
    $fakePilot.schedule = [ordered]@{ seed = 20260823; block = 1; block_order = 'AB'; position_in_block = 1 }
    $fakePilot.candidate_input = (New-WriterTestRunRecord -RunId 'run-A-schema-probe').candidate_input
    Assert-True (-not (Test-Json -Json ($fakePilot | ConvertTo-Json -Compress -Depth 100) -SchemaFile $runSchemaPath -ErrorAction SilentlyContinue)) 'P must reject an AB schedule and candidate input.'
    $fakeCandidate = New-WriterTestRunRecord -RunId 'run-A-schema-null'
    $fakeCandidate.candidate_input = $null
    $fakeCandidate.schedule = $null
    Assert-True (-not (Test-Json -Json ($fakeCandidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $runSchemaPath -ErrorAction SilentlyContinue)) 'A/B must reject null candidate input and schedule.'
}
