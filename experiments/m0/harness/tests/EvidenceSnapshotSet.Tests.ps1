$snapshotSetModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
$snapshotSetSchemaRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas'
$snapshotSetSchemaPath = Join-Path $snapshotSetSchemaRoot 'evidence-snapshot-set.schema.json'
$snapshotSetSchedulePath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schedule.v1.json'
$snapshotSetSchedule = Get-Content -Raw -LiteralPath $snapshotSetSchedulePath -Encoding UTF8 | ConvertFrom-Json -Depth 100

function Copy-SnapshotSetTestObject {
    param([Parameter(Mandatory)] $Value)

    return $Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
}

function Get-SnapshotSetTestSha256 {
    param([Parameter(Mandatory)] [string] $Value)

    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
}

function Get-SnapshotSetTestRunIds {
    $runIds = [Collections.Generic.List[string]]::new()
    foreach ($block in $snapshotSetSchedule.blocks) {
        foreach ($candidateCode in $block.candidate_order) {
            $runIds.Add(('run-{0}-process-cold-{1:D2}' -f $candidateCode, [int]$block.block))
        }
    }
    return @($runIds)
}

function New-SnapshotSetTestDocument {
    $ordinal = 0
    $entries = foreach ($runId in Get-SnapshotSetTestRunIds) {
        $ordinal++
        [ordered]@{
            ordinal = $ordinal
            run_id = $runId
            snapshot_relative_path = "$runId.snapshot.json"
            snapshot_sha256 = Get-SnapshotSetTestSha256 -Value "snapshot:$runId"
        }
    }
    return [ordered]@{
        schema_id = 'm0.evidence-snapshot-set.v1'
        schema_version = 1
        snapshot_set_id = 'process-cold-decisive-01'
        hash_algorithm = 'sha256'
        experiment_id = 'exp-20260823-shell'
        scenario_id = 'process-cold'
        common_inputs = [ordered]@{
            protocol_sha256 = ('a' * 64)
            environment_sha256 = ('b' * 64)
            fixture_sha256 = ('c' * 64)
            harness_sha256 = ('d' * 64)
            scenario_sha256 = ('e' * 64)
        }
        schedule_sha256 = (Get-FileHash -LiteralPath $snapshotSetSchedulePath -Algorithm SHA256).Hash.ToLowerInvariant()
        entry_count = 44
        entries = @($entries)
    }
}

function Test-SnapshotSetSchemaDocument {
    param([Parameter(Mandatory)] $Value)

    return Test-Json -Json ($Value | ConvertTo-Json -Compress -Depth 100) -SchemaFile $snapshotSetSchemaPath -ErrorAction SilentlyContinue
}

function Get-SnapshotSetOutputPaths {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetId
    )

    return [pscustomobject]@{
        set = Join-Path $EvidenceRoot "$SnapshotSetId.snapshot-set.json"
        sidecar = Join-Path $EvidenceRoot "$SnapshotSetId.snapshot-set.sha256"
    }
}

function New-SnapshotSetEvidenceEvent {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [int] $Attempt,
        [Parameter(Mandatory)] [int] $Ordinal,
        [Parameter(Mandatory)] [int] $Sequence,
        [AllowNull()] $PreviousHash,
        [Parameter(Mandatory)] [string] $EventName,
        [Parameter(Mandatory)] $Payload,
        [AllowNull()] $CommonInputs = $null
    )

    if ($null -eq $CommonInputs) {
        $CommonInputs = [ordered]@{
            protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
            harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
        }
    }
    $baseUtc = [DateTimeOffset]::Parse('2026-08-23T12:00:00Z').AddSeconds($Ordinal)
    return [ordered]@{
        schema_id = 'm0.event.v1'
        schema_version = 1
        protocol_sha256 = $CommonInputs.protocol_sha256
        environment_sha256 = $CommonInputs.environment_sha256
        fixture_sha256 = $CommonInputs.fixture_sha256
        harness_sha256 = $CommonInputs.harness_sha256
        scenario_sha256 = $CommonInputs.scenario_sha256
        experiment_id = 'exp-20260823-shell'
        run_id = $RunId
        candidate_code = $CandidateCode
        scenario_id = 'process-cold'
        attempt = $Attempt
        event_seq = $Sequence
        previous_line_sha256 = $PreviousHash
        utc = $baseUtc.AddMilliseconds($Sequence).ToString("yyyy-MM-ddTHH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
        qpc_ticks = 101000000L + ([long]$Ordinal * 100000L) + $Sequence
        qpc_frequency = 10000000
        event = $EventName
        payload = $Payload
    }
}

function New-SnapshotSetEvidenceRunRecord {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [int] $Attempt,
        [Parameter(Mandatory)] $ScheduleBlock,
        [Parameter(Mandatory)] [int] $Position,
        [AllowNull()] $CommonInputs = $null
    )

    if ($null -eq $CommonInputs) {
        $CommonInputs = [ordered]@{
            protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
            harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
        }
    }
    $artifactHash = if ($CandidateCode -ceq 'A') { 'f' * 64 } else { '9' * 64 }
    return [ordered]@{
        schema_id = 'm0.run.v1'
        schema_version = 1
        experiment_id = 'exp-20260823-shell'
        run_id = $RunId
        candidate_code = $CandidateCode
        scenario_id = 'process-cold'
        attempt = $Attempt
        common_inputs = $CommonInputs
        candidate_input = [ordered]@{
            git_commit = ('1' * 40)
            dirty = $false
            artifact_sha256 = $artifactHash
            build_mode = 'release'
            toolchains = @([ordered]@{ name = 'test-toolchain'; version = '1.0.0'; sha256 = ('2' * 64) })
            lockfiles = @([ordered]@{ relative_path = 'candidate.lock'; sha256 = ('3' * 64) })
        }
        schedule = [ordered]@{
            seed = [int]$snapshotSetSchedule.seed
            block = $Attempt
            block_order = $ScheduleBlock.order
            position_in_block = $Position
        }
        status = 'valid'
        status_reason = $null
        error_code = $null
        raw = $null
        markers = @('run-started', 'launch-input', 'first-frame-externally-visible', 'window-actionable', 'measurement-complete')
        process_tree_sha256 = ('4' * 64)
        metrics = @([ordered]@{
            metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null
        })
        actions = @([ordered]@{
            metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
            start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
        })
        external_noise = @()
        artifacts = @([ordered]@{
            artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'
            sha256 = $artifactHash; size_bytes = 1234
        })
        checks_omitted = @()
    }
}

function New-SnapshotSetCommittedFixture {
    param([Parameter(Mandatory)] [string] $EvidenceRoot)

    New-Item -ItemType Directory -Path $EvidenceRoot -Force | Out-Null
    $runIds = [Collections.Generic.List[string]]::new()
    $ordinal = 0
    foreach ($block in $snapshotSetSchedule.blocks) {
        for ($positionIndex = 0; $positionIndex -lt 2; $positionIndex++) {
            $ordinal++
            $candidateCode = [string]$block.candidate_order[$positionIndex]
            $attempt = [int]$block.block
            $runId = ('run-{0}-process-cold-{1:D2}' -f $candidateCode, $attempt)
            $runIds.Add($runId)
            $writer = Open-M0EvidenceWriter -EvidenceRoot $EvidenceRoot -RunId $runId -SchemaRoot $snapshotSetSchemaRoot
            try {
                $artifactHash = if ($candidateCode -ceq 'A') { 'f' * 64 } else { '9' * 64 }
                $specifications = @(
                    [pscustomobject]@{ name = 'run_started'; payload = [ordered]@{ phase = 'started' } },
                    [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'launch-input'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null } },
                    [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'first-frame-externally-visible'; phase = 'instant'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null } },
                    [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'window-actionable'; phase = 'instant'; source = 'external_harness'; role = 'measurement_boundary'; occurrence_id = $null } },
                    [pscustomobject]@{ name = 'action_observation'; payload = [ordered]@{
                        metric_id = 'window_actionable_ns'; value_ns = 200; outcome = 'observed'; source = 'external_harness'
                        start_marker_event_seq = 2; end_marker_event_seq = 4; reason_code = $null; error_code = $null
                    } },
                    [pscustomobject]@{ name = 'resource_sample'; payload = [ordered]@{
                        metric_id = 'window_actionable_ns'; value_integer = 125000000; unit = 'nanoseconds'; error_code = $null
                    } },
                    [pscustomobject]@{ name = 'artifact_recorded'; payload = [ordered]@{
                        artifact_id = 'candidate-package'; relative_path = 'artifacts/candidate.zip'; sha256 = $artifactHash
                        size_bytes = 1234; transient = $false; redacted = $true
                    } },
                    [pscustomobject]@{ name = 'marker'; payload = [ordered]@{ marker_id = 'measurement-complete'; phase = 'end'; source = 'external_harness'; role = 'diagnostic'; occurrence_id = $null } }
                )
                $previousHash = $null
                for ($index = 0; $index -lt $specifications.Count; $index++) {
                    $sequence = $index + 1
                    $event = New-SnapshotSetEvidenceEvent -RunId $runId -CandidateCode $candidateCode -Attempt $attempt -Ordinal $ordinal -Sequence $sequence -PreviousHash $previousHash -EventName $specifications[$index].name -Payload $specifications[$index].payload
                    $appended = Add-M0EvidenceEvent -Writer $writer -Event $event
                    $previousHash = $appended.line_sha256
                }
                $seal = New-SnapshotSetEvidenceEvent -RunId $runId -CandidateCode $candidateCode -Attempt $attempt -Ordinal $ordinal -Sequence 9 -PreviousHash $previousHash -EventName 'run_sealed' -Payload ([ordered]@{
                    status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @()
                })
                $run = New-SnapshotSetEvidenceRunRecord -RunId $runId -CandidateCode $candidateCode -Attempt $attempt -ScheduleBlock $block -Position ($positionIndex + 1)
                $completed = Complete-M0EvidenceRun -Writer $writer -SealEvent $seal -RunRecord $run -EvidenceRole candidate_run
                Assert-Equal -Actual $completed.recovery_state -Expected 'committed' "Fixture run '$runId' did not commit."
            }
            finally {
                $null = Close-M0EvidenceWriter -Writer $writer
            }
        }
    }
    return @($runIds)
}

function New-SnapshotSetAdditionalCandidateRun {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [string] $RunId = 'run-A-process-cold-extra',
        [AllowNull()] $CommonInputs = $null
    )

    $block = @($snapshotSetSchedule.blocks | Where-Object block -eq 1)[0]
    $position = [Array]::IndexOf([object[]]@($block.candidate_order), 'A') + 1
    $writer = Open-M0EvidenceWriter -EvidenceRoot $EvidenceRoot -RunId $RunId -SchemaRoot $snapshotSetSchemaRoot
    try {
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
            $event = New-SnapshotSetEvidenceEvent -RunId $RunId -CandidateCode A -Attempt 1 -Ordinal 45 `
                -Sequence ($index + 1) -PreviousHash $previousHash -EventName $specifications[$index].name `
                -Payload $specifications[$index].payload -CommonInputs $CommonInputs
            $previousHash = (Add-M0EvidenceEvent -Writer $writer -Event $event).line_sha256
        }
        $seal = New-SnapshotSetEvidenceEvent -RunId $RunId -CandidateCode A -Attempt 1 -Ordinal 45 -Sequence 9 `
            -PreviousHash $previousHash -EventName run_sealed -Payload ([ordered]@{ status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @() }) `
            -CommonInputs $CommonInputs
        $run = New-SnapshotSetEvidenceRunRecord -RunId $RunId -CandidateCode A -Attempt 1 -ScheduleBlock $block `
            -Position $position -CommonInputs $CommonInputs
        $null = Complete-M0EvidenceRun -Writer $writer -SealEvent $seal -RunRecord $run -EvidenceRole candidate_run
    }
    finally { $null = Close-M0EvidenceWriter -Writer $writer }
}

function New-SnapshotSetSyntheticPilotRun {
    param([Parameter(Mandatory)] [string] $EvidenceRoot)

    $runId = 'run-P-synthetic-process-tree-01'
    $writer = Open-M0EvidenceWriter -EvidenceRoot $EvidenceRoot -RunId $runId -SchemaRoot $snapshotSetSchemaRoot
    try {
        $base = [ordered]@{
            schema_id = 'm0.event.v1'; schema_version = 1
            protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
            harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64); experiment_id = 'exp-20260823-shell'
            run_id = $runId; candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            utc = '2026-08-23T13:00:00.001Z'; qpc_ticks = 900000001L; qpc_frequency = 10000000
        }
        $specifications = @(
            [pscustomobject]@{ name = 'run_started'; payload = [ordered]@{ phase = 'started' } },
            [pscustomobject]@{ name = 'artifact_recorded'; payload = [ordered]@{
                artifact_id = 'process-tree'; relative_path = 'artifacts/process-tree.json'; sha256 = ('7' * 64)
                size_bytes = 321; transient = $false; redacted = $true
            } },
            [pscustomobject]@{ name = 'check_recorded'; payload = [ordered]@{
                check_id = 'CHK-PROC-LINEAGE'; status = 'passed'; reason_code = $null; status_reason = $null; error_code = $null
            } }
        )
        $previousHash = $null
        for ($index = 0; $index -lt $specifications.Count; $index++) {
            $event = [ordered]@{}
            foreach ($key in $base.Keys) { $event[$key] = $base[$key] }
            $event.event_seq = $index + 1; $event.previous_line_sha256 = $previousHash
            $event.utc = [DateTimeOffset]::Parse($base.utc).AddMilliseconds($index).ToString("yyyy-MM-ddTHH:mm:ss.fff'Z'", [Globalization.CultureInfo]::InvariantCulture)
            $event.qpc_ticks = [long]$base.qpc_ticks + $index; $event.event = $specifications[$index].name; $event.payload = $specifications[$index].payload
            $previousHash = (Add-M0EvidenceEvent -Writer $writer -Event $event).line_sha256
        }
        $seal = [ordered]@{}
        foreach ($key in $base.Keys) { $seal[$key] = $base[$key] }
        $seal.event_seq = 4; $seal.previous_line_sha256 = $previousHash; $seal.utc = '2026-08-23T13:00:00.004Z'; $seal.qpc_ticks = 900000004L
        $seal.event = 'run_sealed'; $seal.payload = [ordered]@{ status = 'valid'; status_reason = $null; error_code = $null; checks_skipped = @() }
        $run = [ordered]@{
            schema_id = 'm0.run.v1'; schema_version = 1; experiment_id = 'exp-20260823-shell'; run_id = $runId
            candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            common_inputs = [ordered]@{ protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64); harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64) }
            candidate_input = $null; schedule = $null; status = 'valid'; status_reason = $null; error_code = $null; raw = $null
            markers = @('run-started'); process_tree_sha256 = ('7' * 64); metrics = @(); actions = @(); external_noise = @()
            artifacts = @([ordered]@{ artifact_id = 'process-tree'; relative_path = 'artifacts/process-tree.json'; sha256 = ('7' * 64); size_bytes = 321 })
            checks_omitted = @()
        }
        $null = Complete-M0EvidenceRun -Writer $writer -SealEvent $seal -RunRecord $run -EvidenceRole synthetic_pilot
    }
    finally { $null = Close-M0EvidenceWriter -Writer $writer }
}

$snapshotSetAdversarialMatrix = @(
    [pscustomobject]@{ case_id = 'count-43'; enforcing_layer = 'schema+builder'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'count-45'; enforcing_layer = 'schema+builder'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'duplicate-run-id'; enforcing_layer = 'builder'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'case-spoof'; enforcing_layer = 'schema+builder'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'schedule-order'; enforcing_layer = 'builder+batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'snapshot-hash'; enforcing_layer = 'builder+batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'unsafe-path'; enforcing_layer = 'schema+builder'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'mixed-root'; enforcing_layer = 'builder+batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'synthetic-pilot-P'; enforcing_layer = 'schema+builder+batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'noncommitted-snapshot'; enforcing_layer = 'builder+batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'post-build-tamper'; enforcing_layer = 'batch'; expected = 'reject' },
    [pscustomobject]@{ case_id = 'evidence-root-busy'; enforcing_layer = 'builder+batch'; expected = 'reject' }
)

Test-Case 'The closed decisive snapshot-set schema exists and fixes the 44-entry shape' {
    Assert-True (Test-Path -LiteralPath $snapshotSetSchemaPath -PathType Leaf) 'M0E026 evidence-snapshot-set.schema.json is missing.'
    $schema = Get-Content -Raw -LiteralPath $snapshotSetSchemaPath -Encoding UTF8 | ConvertFrom-Json -Depth 100
    Assert-Equal -Actual $schema.'$schema' -Expected 'http://json-schema.org/draft-07/schema#'
    Assert-Equal -Actual $schema.type -Expected 'object'
    Assert-Equal -Actual $schema.additionalProperties -Expected $false
    Assert-Equal -Actual $schema.properties.entry_count.const -Expected 44
    Assert-Equal -Actual $schema.properties.entries.minItems -Expected 44
    Assert-Equal -Actual $schema.properties.entries.maxItems -Expected 44
    Assert-Equal -Actual $schema.definitions.entry.additionalProperties -Expected $false
    Assert-SequenceEqual -Actual @($schema.definitions.entry.required) -Expected @('ordinal', 'run_id', 'snapshot_relative_path', 'snapshot_sha256')
}

Test-Case 'The snapshot-set schema accepts only its closed canonical value shape' {
    $valid = New-SnapshotSetTestDocument
    Assert-True (Test-SnapshotSetSchemaDocument -Value $valid) 'The valid 44-entry snapshot-set example did not satisfy its schema.'

    $extraRoot = Copy-SnapshotSetTestObject -Value $valid
    $extraRoot | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
    Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $extraRoot)) 'Snapshot-set root accepted an extra property.'

    $extraEntry = Copy-SnapshotSetTestObject -Value $valid
    $extraEntry.entries[0] | Add-Member -NotePropertyName candidate_code -NotePropertyValue A
    Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $extraEntry)) 'Snapshot-set entry accepted an extra property.'

    $short = Copy-SnapshotSetTestObject -Value $valid
    $short.entries = @($short.entries | Select-Object -First 43)
    Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $short)) 'A 43-entry snapshot set passed schema validation.'

    $long = Copy-SnapshotSetTestObject -Value $valid
    $fortyFifth = Copy-SnapshotSetTestObject -Value $long.entries[43]
    $long.entries = @($long.entries) + @($fortyFifth)
    Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $long)) 'A 45-entry snapshot set passed schema validation.'

    $duplicate = Copy-SnapshotSetTestObject -Value $valid
    $duplicate.entries[1] = Copy-SnapshotSetTestObject -Value $duplicate.entries[0]
    Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $duplicate)) 'An exactly duplicated snapshot entry passed schema validation.'
}

Test-Case 'The schema rejects P, case-spoofs, unsafe paths and noncanonical hashes' {
    $valid = New-SnapshotSetTestDocument
    $mutations = @(
        [pscustomobject]@{ name = 'P-run'; apply = { param($set) $set.entries[0].run_id = 'run-P-synthetic-process-tree-01'; $set.entries[0].snapshot_relative_path = 'run-P-synthetic-process-tree-01.snapshot.json' } },
        [pscustomobject]@{ name = 'case-spoof'; apply = { param($set) $set.entries[0].run_id = 'run-A-Process-cold-01'; $set.entries[0].snapshot_relative_path = 'run-A-Process-cold-01.snapshot.json' } },
        [pscustomobject]@{ name = 'absolute-path'; apply = { param($set) $set.entries[0].snapshot_relative_path = 'C:/outside.snapshot.json' } },
        [pscustomobject]@{ name = 'traversal'; apply = { param($set) $set.entries[0].snapshot_relative_path = '../outside.snapshot.json' } },
        [pscustomobject]@{ name = 'alternate-stream'; apply = { param($set) $set.entries[0].snapshot_relative_path = 'run-A-process-cold-01.snapshot.json:secret' } },
        [pscustomobject]@{ name = 'backslash'; apply = { param($set) $set.entries[0].snapshot_relative_path = 'nested\run-A-process-cold-01.snapshot.json' } },
        [pscustomobject]@{ name = 'uppercase-entry-hash'; apply = { param($set) $set.entries[0].snapshot_sha256 = ('F' * 64) } },
        [pscustomobject]@{ name = 'uppercase-schedule-hash'; apply = { param($set) $set.schedule_sha256 = ('A' * 64) } },
        [pscustomobject]@{ name = 'short-hash'; apply = { param($set) $set.entries[0].snapshot_sha256 = ('f' * 63) } }
    )
    foreach ($mutation in $mutations) {
        $candidate = Copy-SnapshotSetTestObject -Value $valid
        & $mutation.apply $candidate
        Assert-True (-not (Test-SnapshotSetSchemaDocument -Value $candidate)) "Schema accepted snapshot-set mutation '$($mutation.name)'."
    }
}

Test-Case 'Schedule order, ordinal identity, snapshot binding and digest bytes remain semantic checks' {
    $valid = New-SnapshotSetTestDocument
    $semanticMutations = @(
        [pscustomobject]@{ name = 'global-order'; apply = { param($set) $first = $set.entries[0]; $set.entries[0] = $set.entries[1]; $set.entries[1] = $first } },
        [pscustomobject]@{ name = 'duplicate-ordinal'; apply = { param($set) $set.entries[1].ordinal = 1 } },
        [pscustomobject]@{ name = 'run-path-mismatch'; apply = { param($set) $set.entries[0].snapshot_relative_path = $set.entries[1].snapshot_relative_path } },
        [pscustomobject]@{ name = 'well-formed-tampered-hash'; apply = { param($set) $set.entries[0].snapshot_sha256 = ('0' * 64) } },
        [pscustomobject]@{ name = 'well-formed-foreign-schedule-hash'; apply = { param($set) $set.schedule_sha256 = ('1' * 64) } }
    )
    foreach ($mutation in $semanticMutations) {
        $candidate = Copy-SnapshotSetTestObject -Value $valid
        & $mutation.apply $candidate
        Assert-True (Test-SnapshotSetSchemaDocument -Value $candidate) "Mutation '$($mutation.name)' should remain format-valid and be rejected by the builder/batch semantic validator."
    }
}

Test-Case 'The adversarial snapshot-set matrix keeps every fail-closed obligation explicit' {
    Assert-SequenceEqual -Actual @($snapshotSetAdversarialMatrix.case_id) -Expected @(
        'count-43', 'count-45', 'duplicate-run-id', 'case-spoof', 'schedule-order', 'snapshot-hash',
        'unsafe-path', 'mixed-root', 'synthetic-pilot-P', 'noncommitted-snapshot', 'post-build-tamper',
        'evidence-root-busy'
    )
    foreach ($case in $snapshotSetAdversarialMatrix) {
        Assert-Equal -Actual $case.expected -Expected 'reject' "Adversarial case '$($case.case_id)' is not fail-closed."
    }
}

Test-Case 'The snapshot-set builder is a public evidence API with explicit trusted inputs' {
    Import-Module $snapshotSetModulePath -Force -ErrorAction Stop
    $exports = @((Get-Module M0.Evidence).ExportedFunctions.Keys)
    Assert-True ('New-M0EvidenceSnapshotSet' -cin $exports) "M0E026 public snapshot-set builder 'New-M0EvidenceSnapshotSet' is missing."
    $command = Get-Command New-M0EvidenceSnapshotSet -ErrorAction Stop
    foreach ($parameterName in @('EvidenceRoot', 'SnapshotSetId', 'RunIds', 'SchedulePath', 'SchemaRoot')) {
        Assert-True ($command.Parameters.ContainsKey($parameterName)) "M0E026 snapshot-set builder parameter '$parameterName' is missing."
    }
    foreach ($exportName in @(
        'Add-M0EvidenceEvent', 'Complete-M0EvidenceRun', 'Assert-M0EvidenceRun',
        'Get-M0EvidenceCandidateDigests', 'Assert-M0EvidenceBatch'
    )) {
        $exportCommand = Get-Command $exportName -ErrorAction Stop
        Assert-Equal -Actual $exportCommand.ModuleName -Expected 'M0.Evidence' `
            "M0E026 command '$exportName' was shadowed by a test helper from another file."
    }
    foreach ($writerCommandName in @('Add-M0EvidenceEvent', 'Complete-M0EvidenceRun')) {
        $writerCommand = Get-Command $writerCommandName -ErrorAction Stop
        Assert-True ($writerCommand.Parameters.ContainsKey('Writer')) `
            "M0E026 command '$writerCommandName' does not expose the real opaque Writer boundary."
    }
}

Test-Case 'Builder publishes one flushed canonical set and its exact SHA-256 sidecar' {
    $root = Join-Path $TestTempRoot 'snapshot-set-canonical'
    $runIds = New-SnapshotSetCommittedFixture -EvidenceRoot $root
    $setId = 'process-cold-decisive-01'
    $paths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId $setId
    $result = New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $setId -RunIds $runIds -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot

    Assert-True (Test-Path -LiteralPath $paths.set -PathType Leaf) 'Builder did not publish the final snapshot set.'
    Assert-True (Test-Path -LiteralPath $paths.sidecar -PathType Leaf) 'Builder did not publish the snapshot-set sidecar.'
    Assert-Equal -Actual ([IO.Path]::GetFullPath($result.snapshot_set_path)) -Expected ([IO.Path]::GetFullPath($paths.set))
    Assert-Equal -Actual ([IO.Path]::GetFullPath($result.sidecar_path)) -Expected ([IO.Path]::GetFullPath($paths.sidecar))
    Assert-Equal -Actual $result.entry_count -Expected 44

    $setStream = [IO.FileStream]::new($paths.set, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $sidecarStream = $null
    try {
        $setBytes = [byte[]]::new($setStream.Length)
        $null = $setStream.Read($setBytes, 0, $setBytes.Length)
        $sidecarStream = [IO.FileStream]::new($paths.sidecar, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
        $sidecarBytes = [byte[]]::new($sidecarStream.Length)
        $null = $sidecarStream.Read($sidecarBytes, 0, $sidecarBytes.Length)
    }
    finally {
        if ($null -ne $sidecarStream) { $sidecarStream.Dispose() }
        $setStream.Dispose()
    }

    Assert-True (-not ($setBytes.Length -ge 3 -and $setBytes[0] -eq 0xEF -and $setBytes[1] -eq 0xBB -and $setBytes[2] -eq 0xBF)) 'Snapshot set contains a UTF-8 BOM.'
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $setText = $utf8.GetString($setBytes)
    Assert-True ($setText.EndsWith("`n", [StringComparison]::Ordinal)) 'Snapshot set lacks its single canonical LF terminator.'
    Assert-True (-not $setText.Contains("`r")) 'Snapshot set contains CRLF/CR bytes.'
    $setDocument = $setText | ConvertFrom-Json -Depth 100 -DateKind String
    $canonicalText = ($setDocument | ConvertTo-Json -Compress -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    Assert-Equal -Actual $setText -Expected $canonicalText 'Snapshot set is not the canonical compact JSON document.'
    Assert-True (Test-SnapshotSetSchemaDocument -Value $setDocument) 'Published snapshot set fails its closed schema.'
    Assert-SequenceEqual -Actual @($setDocument.entries.run_id) -Expected @($runIds) 'Published entries differ from global schedule order.'
    Assert-SequenceEqual -Actual @($setDocument.entries.ordinal) -Expected @(1..44) 'Published ordinals are not exactly 1 through 44.'

    $setHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($setBytes)).ToLowerInvariant()
    Assert-Equal -Actual $result.snapshot_set_sha256 -Expected $setHash
    $expectedSidecar = "$setHash  $([IO.Path]::GetFileName($paths.set))`n"
    Assert-Equal -Actual $utf8.GetString($sidecarBytes) -Expected $expectedSidecar 'Snapshot-set sidecar is not canonical.'

    $beforeSet = [IO.File]::ReadAllBytes($paths.set)
    $beforeSidecar = [IO.File]::ReadAllBytes($paths.sidecar)
    Assert-Throws {
        New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $setId -RunIds $runIds -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E0|exists|overwrite|collision'
    Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($paths.set)) -Expected @($beforeSet) 'Builder modified the final set on replay.'
    Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($paths.sidecar)) -Expected @($beforeSidecar) 'Builder modified the final sidecar on replay.'

    $script:snapshotSetCommittedFixtureRoot = $root
    $script:snapshotSetCommittedFixtureRunIds = @($runIds)
}

Test-Case 'Builder rejects schedule reorder, noncommitted membership and snapshot tamper without publication' {
    Assert-True (-not [string]::IsNullOrWhiteSpace($script:snapshotSetCommittedFixtureRoot)) 'Canonical fixture was not initialized.'
    $root = $script:snapshotSetCommittedFixtureRoot
    $runIds = @($script:snapshotSetCommittedFixtureRunIds)

    $reordered = @($runIds)
    $first = $reordered[0]
    $reordered[0] = $reordered[1]
    $reordered[1] = $first
    Assert-Throws {
        New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'reordered' -RunIds $reordered -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E0|order|schedule|ordinal|position'
    $reorderedPaths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId 'reordered'
    Assert-True (-not (Test-Path -LiteralPath $reorderedPaths.set)) 'Reordered inventory published a set.'
    Assert-True (-not (Test-Path -LiteralPath $reorderedPaths.sidecar)) 'Reordered inventory published a sidecar.'

    $missingRunId = $runIds[43]
    $missingSnapshot = Join-Path $root "$missingRunId.snapshot.json"
    $missingBackup = Join-Path $root "$missingRunId.snapshot.test-backup"
    Move-Item -LiteralPath $missingSnapshot -Destination $missingBackup
    try {
        Assert-Throws {
            New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'noncommitted' -RunIds $runIds -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
        } 'M0E0|commit|snapshot|missing'
    }
    finally {
        Move-Item -LiteralPath $missingBackup -Destination $missingSnapshot
    }
    $noncommittedPaths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId 'noncommitted'
    Assert-True (-not (Test-Path -LiteralPath $noncommittedPaths.set)) 'Noncommitted inventory published a set.'
    Assert-True (-not (Test-Path -LiteralPath $noncommittedPaths.sidecar)) 'Noncommitted inventory published a sidecar.'

    $tamperedRunId = $runIds[0]
    $tamperedSnapshot = Join-Path $root "$tamperedRunId.snapshot.json"
    $originalBytes = [IO.File]::ReadAllBytes($tamperedSnapshot)
    try {
        [IO.File]::WriteAllBytes($tamperedSnapshot, [byte[]](@($originalBytes) + @(0x0A)))
        Assert-Throws {
            New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'tampered' -RunIds $runIds -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
        } 'M0E0|snapshot|canonical|hash|intent|bytes'
    }
    finally {
        [IO.File]::WriteAllBytes($tamperedSnapshot, $originalBytes)
    }
    $tamperedPaths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId 'tampered'
    Assert-True (-not (Test-Path -LiteralPath $tamperedPaths.set)) 'Tampered inventory published a set.'
    Assert-True (-not (Test-Path -LiteralPath $tamperedPaths.sidecar)) 'Tampered inventory published a sidecar.'
}

Test-Case 'Builder rejects incomplete, excessive, duplicate, case-spoofed, P and path-like run inventories without publication' {
    $baseRunIds = Get-SnapshotSetTestRunIds
    $mutations = @(
        [pscustomobject]@{ name = 'count-43'; run_ids = @($baseRunIds | Select-Object -First 43) },
        [pscustomobject]@{ name = 'count-45'; run_ids = @($baseRunIds) + @('run-A-process-cold-23') },
        [pscustomobject]@{ name = 'duplicate'; run_ids = @($baseRunIds[0]) + @($baseRunIds[0]) + @($baseRunIds | Select-Object -Skip 2) },
        [pscustomobject]@{ name = 'case-spoof'; run_ids = @('run-A-Process-cold-01') + @($baseRunIds | Select-Object -Skip 1) },
        [pscustomobject]@{ name = 'pilot-P'; run_ids = @('run-P-synthetic-process-tree-01') + @($baseRunIds | Select-Object -Skip 1) },
        [pscustomobject]@{ name = 'absolute'; run_ids = @('C:/outside/run-A-process-cold-01') + @($baseRunIds | Select-Object -Skip 1) }
    )
    foreach ($mutation in $mutations) {
        $root = Join-Path $TestTempRoot "snapshot-set-invalid-$($mutation.name)"
        New-Item -ItemType Directory -Path $root | Out-Null
        $setId = "invalid-$($mutation.name)"
        $paths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId $setId
        Assert-Throws {
            New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $setId -RunIds $mutation.run_ids -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
        } 'M0E0|44|duplicate|case|candidate|run|path'
        Assert-True (-not (Test-Path -LiteralPath $paths.set)) "Invalid inventory '$($mutation.name)' published a snapshot set."
        Assert-True (-not (Test-Path -LiteralPath $paths.sidecar)) "Invalid inventory '$($mutation.name)' published a sidecar."
    }
}

Test-Case 'Builder observes the EvidenceRoot lock before reading a complete inventory' {
    $root = Join-Path $TestTempRoot 'snapshot-set-busy'
    New-Item -ItemType Directory -Path $root | Out-Null
    $lockPath = Join-Path $root '.m0-evidence.lock'
    $lock = [IO.FileStream]::new($lockPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        Assert-Throws {
            New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'busy-set' -RunIds (Get-SnapshotSetTestRunIds) -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
        } 'M0E0|busy|lock|writer'
    }
    finally {
        $lock.Dispose()
    }
}

Test-Case 'Snapshot-set publication never overwrites a pre-existing set or sidecar' {
    $root = $script:snapshotSetCommittedFixtureRoot
    $runIds = @($script:snapshotSetCommittedFixtureRunIds)
    foreach ($case in @(
        [pscustomobject]@{ set_id = 'collision-set'; leaf = 'collision-set.snapshot-set.json' },
        [pscustomobject]@{ set_id = 'collision-sidecar'; leaf = 'collision-sidecar.snapshot-set.sha256' }
    )) {
        $existingPath = Join-Path $root $case.leaf
        $sentinel = [Text.UTF8Encoding]::new($false).GetBytes("owned-by-user`n")
        [IO.File]::WriteAllBytes($existingPath, $sentinel)
        Assert-Throws {
            New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $case.set_id -RunIds $runIds -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
        } 'M0E0|exists|overwrite|collision'
        Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($existingPath)) -Expected @($sentinel) 'Builder modified a pre-existing publication file.'
        $otherPaths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId $case.set_id
        $otherPath = if ($existingPath -ceq $otherPaths.set) { $otherPaths.sidecar } else { $otherPaths.set }
        Assert-True (-not (Test-Path -LiteralPath $otherPath)) 'Builder partially published the other snapshot-set file after a collision.'
    }
}

Test-Case 'The sidecar is the sole snapshot-set commit point and exact retry recovers a published set' {
    $root = $script:snapshotSetCommittedFixtureRoot
    $runIds = @($script:snapshotSetCommittedFixtureRunIds)
    $setId = 'recoverable-set'
    $paths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId $setId
    Assert-Throws {
        New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $setId -RunIds $runIds `
            -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot `
            -CrashAfterBoundary snapshot_set_published
    } 'M0E023|snapshot_set_published'
    Assert-True (Test-Path -LiteralPath $paths.set -PathType Leaf) 'Crash injection did not leave the flushed set boundary.'
    Assert-True (-not (Test-Path -LiteralPath $paths.sidecar)) 'Snapshot-set sidecar was published before its commit boundary.'
    $setBeforeRetry = [IO.File]::ReadAllBytes($paths.set)

    $dummyBatchPath = Join-Path $root 'incomplete-set-probe.batch.json'
    [IO.File]::WriteAllText($dummyBatchPath, "{}`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-M0EvidenceBatch -EvidenceRoot $root -BatchPath $dummyBatchPath -SnapshotSetPath $paths.set `
            -SchedulePath $snapshotSetSchedulePath -ExpectedCommonInputs ([ordered]@{
                protocol_sha256 = ('a' * 64); environment_sha256 = ('b' * 64); fixture_sha256 = ('c' * 64)
                harness_sha256 = ('d' * 64); scenario_sha256 = ('e' * 64)
            }) -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E005|M0E026|sidecar|commit'

    $recovered = New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $setId -RunIds $runIds `
        -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    Assert-True (Test-Path -LiteralPath $paths.sidecar -PathType Leaf) 'Exact retry did not publish the sidecar commit point.'
    Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($paths.set)) -Expected @($setBeforeRetry) 'Exact retry rewrote set bytes.'
    Assert-Equal -Actual $recovered.snapshot_set_sha256 -Expected ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($setBeforeRetry)).ToLowerInvariant())

    $divergentId = 'divergent-uncommitted-set'
    $divergentPaths = Get-SnapshotSetOutputPaths -EvidenceRoot $root -SnapshotSetId $divergentId
    $ownedBytes = [Text.UTF8Encoding]::new($false).GetBytes("{}`n")
    [IO.File]::WriteAllBytes($divergentPaths.set, $ownedBytes)
    Assert-Throws {
        New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId $divergentId -RunIds $runIds `
            -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E026|diverge|overwrite'
    Assert-SequenceEqual -Actual @([IO.File]::ReadAllBytes($divergentPaths.set)) -Expected @($ownedBytes) 'Divergent uncommitted set was overwritten.'
    Assert-True (-not (Test-Path -LiteralPath $divergentPaths.sidecar)) 'Divergent uncommitted set gained a commit sidecar.'
}

Test-Case 'Unlisted decisive inventory normalizes common-input hash case and property order' {
    $sourceRoot = $script:snapshotSetCommittedFixtureRoot
    $runIds = @($script:snapshotSetCommittedFixtureRunIds)
    $cases = @(
        [pscustomobject]@{
            name = 'uppercase-hashes'
            run_id = 'run-A-process-cold-extra-uppercase'
            common_inputs = [ordered]@{
                protocol_sha256 = ('A' * 64); environment_sha256 = ('B' * 64); fixture_sha256 = ('C' * 64)
                harness_sha256 = ('D' * 64); scenario_sha256 = ('E' * 64)
            }
        },
        [pscustomobject]@{
            name = 'reordered-properties'
            run_id = 'run-A-process-cold-extra-reordered'
            common_inputs = [ordered]@{
                scenario_sha256 = ('e' * 64); fixture_sha256 = ('c' * 64); protocol_sha256 = ('a' * 64)
                harness_sha256 = ('d' * 64); environment_sha256 = ('b' * 64)
            }
        }
    )
    $redFailures = [Collections.Generic.List[string]]::new()
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot "snapshot-set-domain-$($case.name)"
        New-Item -ItemType Directory -Path $root | Out-Null
        foreach ($item in Get-ChildItem -LiteralPath $sourceRoot -File | Where-Object {
            $_.Name -ceq '.m0-evidence.lock' -or $_.Name -match '\.(?:jsonl|intent\.json|run\.json|snapshot\.json)$'
        }) {
            Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $root $item.Name)
        }
        New-SnapshotSetAdditionalCandidateRun -EvidenceRoot $root -RunId $case.run_id -CommonInputs $case.common_inputs
        try {
            Assert-Throws {
                New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId "unlisted-$($case.name)" -RunIds $runIds `
                    -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
            } 'M0E026|unlisted|decision.eligible|snapshot'
        }
        catch {
            $redFailures.Add("$($case.name): $($_.Exception.Message)")
        }
    }
    Assert-Equal -Actual $redFailures.Count -Expected 0 ("Common-input normalization bypasses: " + ($redFailures -join '; '))
}

Test-Case 'Inventory ignores committed P but rejects any unlisted decisive candidate snapshot' {
    $root = $script:snapshotSetCommittedFixtureRoot
    $runIds = @($script:snapshotSetCommittedFixtureRunIds)
    New-SnapshotSetSyntheticPilotRun -EvidenceRoot $root
    $withPilot = New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'pilot-coexists' -RunIds $runIds `
        -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    Assert-Equal -Actual $withPilot.entry_count -Expected 44
    $digest = Get-M0EvidenceCandidateDigests -EvidenceRoot $root -SnapshotSetPath $withPilot.snapshot_set_path -CandidateCode A -SchemaRoot $snapshotSetSchemaRoot
    Assert-True ($digest.runs_manifest_sha256 -match '^[a-f0-9]{64}$') 'P coexistence prevented candidate digest validation.'

    New-SnapshotSetAdditionalCandidateRun -EvidenceRoot $root
    Assert-Throws {
        New-M0EvidenceSnapshotSet -EvidenceRoot $root -SnapshotSetId 'unlisted-candidate' -RunIds $runIds `
            -SchedulePath $snapshotSetSchedulePath -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E026|unlisted|decision.eligible|snapshot'
    Assert-Throws {
        Get-M0EvidenceCandidateDigests -EvidenceRoot $root -SnapshotSetPath $withPilot.snapshot_set_path -CandidateCode A -SchemaRoot $snapshotSetSchemaRoot
    } 'M0E026|unlisted|decision.eligible|snapshot'
}
