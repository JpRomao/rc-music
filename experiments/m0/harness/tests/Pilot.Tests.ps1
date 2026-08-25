$pilotModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Pilot.psm1'
$pilotRunnerPath = Join-Path $RepositoryRoot 'scripts\m0\Invoke-M0Pilot.ps1'
$pilotProtocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$pilotScenarioPath = Join-Path $pilotProtocolRoot 'scenarios\synthetic-process-tree.v1.json'
$pilotScenarioSidecarPath = Join-Path $pilotProtocolRoot 'scenarios\synthetic-process-tree.v1.sha256'
$pilotScenarioSchemaPath = Join-Path $pilotProtocolRoot 'schemas\synthetic-process-tree-scenario.schema.json'
$pilotSummaryInputGoldenPath = Join-Path $pilotProtocolRoot 'goldens\pilot-summary-input.v1.json'
$pilotSummaryGoldenPath = Join-Path $pilotProtocolRoot 'goldens\pilot-summary.v1.json'
$pilotSummaryInputSchemaPath = Join-Path $pilotProtocolRoot 'schemas\pilot-summary-input.schema.json'
$pilotSummarySchemaPath = Join-Path $pilotProtocolRoot 'schemas\pilot-summary.schema.json'
$pilotIntentSchemaPath = Join-Path $pilotProtocolRoot 'schemas\pilot-intent.schema.json'
$pilotIntentGoldenPath = Join-Path $pilotProtocolRoot 'goldens\pilot-intent.v1.json'
$pilotEventSchemaPath = Join-Path $pilotProtocolRoot 'schemas\event.schema.json'
$pilotProcessDescriptorPath = Join-Path $pilotProtocolRoot 'process.v1.json'
$pilotProcessArtifactSchemaPath = Join-Path $pilotProtocolRoot 'schemas\process-tree-artifact.schema.json'
$pilotProcessModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
function Copy-M0PilotTestValue {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String)
}

function ConvertTo-M0PilotDeepReorderedValue {
    param([AllowNull()] $Value)
    if ($null -eq $Value -or $Value -is [string] -or $Value -is [ValueType]) {
        return $Value
    }
    if ($Value -is [array]) {
        [object[]]$items = @($Value | ForEach-Object { ConvertTo-M0PilotDeepReorderedValue -Value $_ })
        return ,$items
    }
    $result = [pscustomobject][ordered]@{}
    $properties = @($Value.PSObject.Properties)
    [array]::Reverse($properties)
    foreach ($property in $properties) {
        $result | Add-Member -NotePropertyName $property.Name `
            -NotePropertyValue (ConvertTo-M0PilotDeepReorderedValue -Value $property.Value)
    }
    return $result
}

function Test-M0PilotInputSchema {
    param([Parameter(Mandatory)] $Value)
    return Test-Json -Json ($Value | ConvertTo-Json -Compress -Depth 100) `
        -SchemaFile $pilotSummaryInputSchemaPath -ErrorAction SilentlyContinue
}

function Invoke-M0PilotTestChild {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Source,
        [string[]] $Arguments = @()
    )

    $scriptPath = Join-Path $TestTempRoot "$Name.ps1"
    [IO.File]::WriteAllText($scriptPath, $Source, [Text.UTF8Encoding]::new($false))
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $scriptPath) + $Arguments) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($startInfo)
    try {
        $output = $child.StandardOutput.ReadToEnd() + $child.StandardError.ReadToEnd()
        $child.WaitForExit()
        return [pscustomobject][ordered]@{ exit_code = $child.ExitCode; output = $output }
    }
    finally {
        $child.Dispose()
    }
}

function Get-M0PilotFileInventory {
    param([Parameter(Mandatory)] [string] $Root)
    $items = [Collections.Generic.List[object]]::new()
    foreach ($file in Get-ChildItem -LiteralPath $Root -File -Recurse) {
        $items.Add([pscustomobject][ordered]@{
            relative_path = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            size_bytes = [long]$file.Length
        })
    }
    return @($items | Sort-Object relative_path)
}

function Get-M0SyntheticHelperProcesses {
    $helperLeaf = 'Invoke-M0SyntheticProcessNode.ps1'
    if ($IsWindows) {
        return @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
            $_.ProcessId -ne $PID -and -not [string]::IsNullOrWhiteSpace($_.CommandLine) -and
            $_.CommandLine.IndexOf($helperLeaf, [StringComparison]::OrdinalIgnoreCase) -ge 0
        })
    }

    $psCommand = Get-Command -Name ps -CommandType Application -ErrorAction Stop
    $processLines = @(& $psCommand.Source -eo 'pid=,args=')
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect non-Windows process command lines with ps (exit $LASTEXITCODE)."
    }
    $matches = [Collections.Generic.List[object]]::new()
    foreach ($line in $processLines) {
        if ([string]$line -notmatch '^\s*(?<process_id>\d+)\s+(?<command_line>.+)$') { continue }
        $processId = [int]$Matches.process_id
        $commandLine = [string]$Matches.command_line
        if ($processId -ne $PID -and
            $commandLine.IndexOf($helperLeaf, [StringComparison]::Ordinal) -ge 0) {
            $matches.Add([pscustomobject][ordered]@{
                ProcessId = $processId
                CommandLine = $commandLine
            })
        }
    }
    return @($matches)
}

function New-M0PilotPhysicalArtifactFixture {
    param(
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId
    )

    $identities = @(
        [pscustomobject][ordered]@{ pid = 4101; creation_time_filetime_utc = '134000000000004101' },
        [pscustomobject][ordered]@{ pid = 4102; creation_time_filetime_utc = '134000000000004102' },
        [pscustomobject][ordered]@{ pid = 4103; creation_time_filetime_utc = '134000000000004103' }
    )
    $nodes = @(
        [pscustomobject][ordered]@{
            role = 'root'; identity = $identities[0]; parent_identity = $null
            executable_sha256 = 'a' * 64; job_membership_revalidated = $true
        },
        [pscustomobject][ordered]@{
            role = 'child'; identity = $identities[1]; parent_identity = $identities[0]
            executable_sha256 = 'b' * 64; job_membership_revalidated = $true
        },
        [pscustomobject][ordered]@{
            role = 'grandchild'; identity = $identities[2]; parent_identity = $identities[1]
            executable_sha256 = 'c' * 64; job_membership_revalidated = $true
        }
    )
    $snapshot = [pscustomobject][ordered]@{
        '$schema' = 'schemas/process-tree.schema.json'
        schema_id = 'm0.process-tree-smoke.v1'
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        platform = 'windows'
        state = 'partial'
        error_code = 'etw_live_not_implemented'
        missing_capabilities = @('etw-process-start-stop', 'job-completion-port')
        trace = [pscustomobject][ordered]@{
            state = 'partial'; adapter = 'etw-process-start-stop'; complete = $false; collected = $false
            gap_codes = @('not-collected'); error_code = 'etw_live_not_implemented'
        }
        capabilities = [pscustomobject][ordered]@{
            job_object_kill_on_close = $true; breakaway_flags = 'none'
            membership_handles_revalidated = $true; helper_sha256_verified = $true
            helper_file_locked_until_readiness = $true; completion_port = $false
            etw_process_start_stop = $false; linux_live_status = 'not_proven'
        }
        roles = @('root', 'child', 'grandchild')
        nodes = $nodes
        auxiliary_job_members = @()
        job_member_hint_count = 3
        retained_identity_count = 3
        qpc_before_create_ticks = '2000000'
        qpc_after_resume_ticks = '2500000'
        qpc_frequency = '10000000'
        cleanup_authority = 'private-opaque-registry-only'
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
    }
    $cleanup = [pscustomobject][ordered]@{
        schema_id = 'm0.process-tree-cleanup.v1'
        cleanup_complete = $true
        survivor_count = 0
        retained_identity_count = 3
        native_cleanup_actions = 1
        cleanup_action = 'close-last-job-handle-and-wait-retained-process-handles'
        individual_terminate_process_calls = 0
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        error_code = $null
        failure_codes = @()
    }
    $processModule = @(Get-Module -All -Name M0.Process)
    if ($processModule.Count -ne 1) { throw 'Physical fixture requires exactly one trust-loaded M0.Process module.' }
    return & $processModule[0] {
        param($tree, $closed, $experiment, $run)
        New-M0ProcessTreeArtifact -Snapshot $tree -CleanupResult $closed -ExperimentId $experiment -RunId $run
    } $snapshot $cleanup $ExperimentId $RunId
}

Test-Case 'The synthetic process scenario has its own closed identity and content hash' {
    foreach ($path in @(
        $pilotScenarioPath,
        $pilotScenarioSidecarPath,
        $pilotScenarioSchemaPath,
        $pilotSummaryInputGoldenPath,
        $pilotSummaryGoldenPath,
        $pilotSummaryInputSchemaPath,
        $pilotSummarySchemaPath,
        $pilotIntentSchemaPath,
        $pilotIntentGoldenPath,
        $pilotEventSchemaPath
    )) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required synthetic pilot contract file is missing: '$path'."
    }

    $scenarioJson = Get-Content -Raw -LiteralPath $pilotScenarioPath
    Assert-True (Test-Json -Json $scenarioJson -SchemaFile $pilotScenarioSchemaPath -ErrorAction SilentlyContinue) `
        'Synthetic process scenario does not satisfy its closed schema.'
    $scenario = $scenarioJson | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Equal -Actual $scenario.scenario_id -Expected 'synthetic-process-tree'
    Assert-Equal -Actual $scenario.candidate_code -Expected 'P'
    Assert-Equal -Actual $scenario.attempt -Expected 1
    Assert-Equal -Actual $scenario.evidence_role -Expected 'synthetic_pilot'
    Assert-Equal -Actual $scenario.decision_eligible -Expected $false
    Assert-SequenceEqual -Actual @($scenario.event_sequence) -Expected @(
        'run_started', 'artifact_recorded', 'check_recorded', 'run_sealed'
    )

    $actualHash = (Get-FileHash -LiteralPath $pilotScenarioPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sidecar = (Get-Content -Raw -LiteralPath $pilotScenarioSidecarPath).TrimEnd("`r", "`n")
    Assert-Equal -Actual $sidecar -Expected "$actualHash  synthetic-process-tree.v1.json"
    $shellHash = (Get-FileHash -LiteralPath (Join-Path $pilotProtocolRoot 'scenarios\shell.v1.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    $processHash = (Get-FileHash -LiteralPath (Join-Path $pilotProtocolRoot 'process.v1.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-True ($actualHash -cne $shellHash) 'Synthetic P scenario reused the shell scenario hash.'
    Assert-True ($actualHash -cne $processHash) 'Synthetic P scenario reused the process descriptor hash.'

    $protocol = Get-Content -Raw -LiteralPath (Join-Path $pilotProtocolRoot 'protocol.v1.json') |
        ConvertFrom-Json -Depth 100 -DateKind String
    Assert-True ($protocol.PSObject.Properties.Name -ccontains 'synthetic_process_tree_scenario_sha256') `
        'protocol.v1.json does not externally anchor the synthetic P scenario.'
    Assert-Equal -Actual ([string]$protocol.synthetic_process_tree_scenario_sha256.ToLowerInvariant()) `
        -Expected $actualHash 'Protocol synthetic scenario anchor differs from the scenario bytes.'

    $intentJson = Get-Content -Raw -LiteralPath $pilotIntentGoldenPath
    Assert-True (Test-Json -Json $intentJson -SchemaFile $pilotIntentSchemaPath -ErrorAction SilentlyContinue) `
        'Deterministic pilot-intent golden does not satisfy its closed schema.'
    Assert-Equal -Actual $scenario.pilot_intent_contract.schema_relative_path -Expected 'schemas/pilot-intent.schema.json'
    Assert-Equal -Actual $scenario.pilot_intent_contract.event_schema_relative_path -Expected 'schemas/event.schema.json'
    Assert-Equal -Actual $scenario.pilot_intent_contract.golden_relative_path -Expected 'goldens/pilot-intent.v1.json'
    Assert-Equal -Actual $scenario.pilot_intent_contract.relative_path_template -Expected '{run_id}.pilot-intent.json'

    $contractSlots = @(
        [pscustomobject]@{ id = 'intent-schema'; path = $pilotIntentSchemaPath; expected = $scenario.pilot_intent_contract.schema_sha256 },
        [pscustomobject]@{ id = 'event-schema'; path = $pilotEventSchemaPath; expected = $scenario.pilot_intent_contract.event_schema_sha256 },
        [pscustomobject]@{ id = 'intent-golden'; path = $pilotIntentGoldenPath; expected = $scenario.pilot_intent_contract.golden_sha256 },
        [pscustomobject]@{ id = 'summary-input-schema'; path = $pilotSummaryInputSchemaPath; expected = $scenario.summary_contract.input_schema_sha256 },
        [pscustomobject]@{ id = 'summary-input'; path = $pilotSummaryInputGoldenPath; expected = $scenario.summary_contract.input_sha256 },
        [pscustomobject]@{ id = 'summary-schema'; path = $pilotSummarySchemaPath; expected = $scenario.summary_contract.schema_sha256 },
        [pscustomobject]@{ id = 'summary-golden'; path = $pilotSummaryGoldenPath; expected = $scenario.summary_contract.golden_sha256 }
    )
    foreach ($slot in $contractSlots) {
        $slotHash = (Get-FileHash -LiteralPath $slot.path -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-Equal -Actual ([string]$slot.expected) -Expected $slotHash `
            "Scenario transitive slot '$($slot.id)' differs from its literal bytes."
    }
}

Test-Case 'Pilot summary schemas reject impossible process cleanup outcome clock and receipt combinations' {
    $valid = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-True (Test-M0PilotInputSchema -Value $valid) 'The deterministic pilot input fixture is not schema-valid.'

    $vectors = @(
        [pscustomobject]@{ case_id = 'complete-with-fallback-origin'; apply = {
                param($value) $value.process.state = 'complete'; $value.process.error_code = $null
                $value.process.missing_capabilities = @(); $value.process.snapshot_origin = 'pre_snapshot_fallback'
            } },
        [pscustomobject]@{ case_id = 'complete-with-error'; apply = {
                param($value) $value.process.state = 'complete'; $value.process.error_code = 'etw_live_not_implemented'
                $value.process.missing_capabilities = @()
            } },
        [pscustomobject]@{ case_id = 'complete-with-missing'; apply = {
                param($value) $value.process.state = 'complete'; $value.process.error_code = $null
            } },
        [pscustomobject]@{ case_id = 'cleanup-complete-with-survivor'; apply = {
                param($value) $value.cleanup.survivor_count = 1
            } },
        [pscustomobject]@{ case_id = 'cleanup-complete-with-failure'; apply = {
                param($value) $value.cleanup.failure_code = 'job_close_failed'
            } },
        [pscustomobject]@{ case_id = 'cleanup-incomplete-without-failure'; apply = {
                param($value) $value.cleanup.completed = $false
            } },
        [pscustomobject]@{ case_id = 'cleanup-not-attempted-on-observed-snapshot'; apply = {
                param($value) $value.cleanup.attempted = $false
            } },
        [pscustomobject]@{ case_id = 'check-passed-with-reason'; apply = {
                param($value) $value.check.status = 'passed'; $value.check.status_reason = $null
            } },
        [pscustomobject]@{ case_id = 'check-passed-with-status-reason'; apply = {
                param($value) $value.check.status = 'passed'; $value.check.reason_code = $null
            } },
        [pscustomobject]@{ case_id = 'check-passed-with-error'; apply = {
                param($value) $value.check.status = 'passed'; $value.check.reason_code = $null
                $value.check.status_reason = $null; $value.check.error_code = 'helper_survived_shutdown'
            } },
        [pscustomobject]@{ case_id = 'run-valid-with-status-reason'; apply = {
                param($value) $value.run.status = 'valid'
            } },
        [pscustomobject]@{ case_id = 'run-valid-with-error'; apply = {
                param($value) $value.run.status = 'valid'; $value.run.status_reason = $null
                $value.run.error_code = 'helper_survived_shutdown'
            } },
        [pscustomobject]@{ case_id = 'run-failed-without-cause'; apply = {
                param($value) $value.run.status = 'failed'; $value.run.status_reason = $null; $value.run.error_code = $null
            } },
        [pscustomobject]@{ case_id = 'etw-cause-without-etw-capability'; apply = {
                param($value) $value.process.missing_capabilities = @('job-completion-port')
            } },
        [pscustomobject]@{ case_id = 'external-cause-without-external-proof'; apply = {
                param($value) $value.process.error_code = 'unresolved_external_attribution'
                $value.process.missing_capabilities = @('etw-process-start-stop')
            } },
        [pscustomobject]@{ case_id = 'safe-cause-without-safe-identity'; apply = {
                param($value)
                $value.process.state = 'unavailable'; $value.process.error_code = 'safe_identity_unavailable'
                $value.process.snapshot_origin = 'pre_snapshot_fallback'
                $value.process.missing_capabilities = @('etw-process-start-stop')
                $value.receipt.process_identities = @(); $value.check.reason_code = 'process_tree_unavailable'
            } },
        [pscustomobject]@{ case_id = 'root-cause-without-root-handle'; apply = {
                param($value)
                $value.process.error_code = 'root_identity_missing'
                $value.process.missing_capabilities = @('etw-process-start-stop')
                $value.cleanup.attempted = $false; $value.receipt.process_identities = @()
            } },
        [pscustomobject]@{ case_id = 'external-proof-with-etw-cause'; apply = {
                param($value)
                $value.process.missing_capabilities = @('etw-process-start-stop', 'external-attribution-proof')
            } },
        [pscustomobject]@{ case_id = 'safe-identity-with-etw-unavailable-cause'; apply = {
                param($value)
                $value.process.state = 'unavailable'; $value.process.error_code = 'etw_trace_unavailable'
                $value.process.snapshot_origin = 'pre_snapshot_fallback'
                $value.process.missing_capabilities = @('etw-process-start-stop', 'safe-process-identity')
                $value.receipt.process_identities = @(); $value.check.reason_code = 'process_tree_unavailable'
            } },
        [pscustomobject]@{ case_id = 'partial-with-passed-valid-outcome'; apply = {
                param($value)
                $value.check.status = 'passed'; $value.check.reason_code = $null
                $value.check.status_reason = $null; $value.check.error_code = $null
                $value.run.status = 'valid'; $value.run.status_reason = $null; $value.run.error_code = $null
            } },
        [pscustomobject]@{ case_id = 'cleanup-failure-with-process-reason'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.failure_code = 'job_close_failed'
                $value.cleanup.failure_codes = @('job_close_failed')
            } },
        [pscustomobject]@{ case_id = 'cleanup-primary-mismatch'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.failure_code = 'identity_revalidation_failed'
                $value.cleanup.failure_codes = @('snapshot_validation_failed')
                $value.check.reason_code = 'identity_revalidation_failed'
            } },
        [pscustomobject]@{ case_id = 'cleanup-duplicate-failure'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.failure_code = 'job_close_failed'
                $value.cleanup.failure_codes = @('job_close_failed', 'job_close_failed')
                $value.check.reason_code = 'job_close_failed'
            } },
        [pscustomobject]@{ case_id = 'cleanup-reordered-failures'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.failure_code = 'job_close_failed'
                $value.cleanup.failure_codes = @('job_close_failed', 'snapshot_validation_failed')
                $value.check.reason_code = 'job_close_failed'
            } },
        [pscustomobject]@{ case_id = 'cleanup-survivor-without-survivor-cause'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.survivor_count = 1
                $value.cleanup.failure_code = 'snapshot_validation_failed'
                $value.cleanup.failure_codes = @('snapshot_validation_failed')
                $value.check.status = 'failed'; $value.check.reason_code = 'helper_survived_shutdown'
                $value.check.status_reason = 'helper_survived_shutdown'; $value.check.error_code = 'helper_survived_shutdown'
                $value.run.status = 'failed'; $value.run.status_reason = 'helper_survived_shutdown'
                $value.run.error_code = 'helper_survived_shutdown'
            } }
    )
    $falseAccepts = [Collections.Generic.List[string]]::new()
    foreach ($vector in $vectors) {
        $candidate = Copy-M0PilotTestValue $valid
        & $vector.apply $candidate
        if (Test-M0PilotInputSchema -Value $candidate) {
            $falseAccepts.Add($vector.case_id)
        }
    }
    Assert-Equal -Actual $falseAccepts.Count -Expected 0 `
        "Pilot input schema accepted contradictions: $([string]::Join(',', @($falseAccepts)))."

    $unresolvedRootPrefix = Copy-M0PilotTestValue $valid
    $unresolvedRootPrefix.process.error_code = 'unresolved_external_attribution'
    $unresolvedRootPrefix.process.missing_capabilities = @('external-attribution-proof')
    $unresolvedRootPrefix.receipt.process_identities = @($unresolvedRootPrefix.receipt.process_identities[0])
    Assert-True (Test-M0PilotInputSchema -Value $unresolvedRootPrefix) `
        'A valid unresolved-external root prefix was rejected.'

    foreach ($case in @(
        [pscustomobject]@{ id = 'empty'; identities = @() },
        [pscustomobject]@{ id = 'child-only'; identities = @($valid.receipt.process_identities[1]) },
        [pscustomobject]@{ id = 'hole'; identities = @($valid.receipt.process_identities[0], $valid.receipt.process_identities[2]) },
        [pscustomobject]@{ id = 'four'; identities = @(
                $valid.receipt.process_identities[0], $valid.receipt.process_identities[1],
                $valid.receipt.process_identities[2], $valid.receipt.process_identities[2]
            ) }
    )) {
        $candidate = Copy-M0PilotTestValue $unresolvedRootPrefix
        $candidate.receipt.process_identities = @($case.identities | ForEach-Object { Copy-M0PilotTestValue $_ })
        Assert-True (-not (Test-M0PilotInputSchema -Value $candidate)) `
            "Unresolved-external identity prefix '$($case.id)' was accepted."
    }
}

Test-Case 'Pilot receipts retain the launch intent and the sealed raw identity needed for recovery' {
    $input = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-True ($input.receipt.PSObject.Properties.Name -ccontains 'pilot_intent') `
        'Pilot receipt omits the pre-live launch intent.'
    Assert-True ($input.receipt.raws[0].PSObject.Properties.Name -ccontains 'event_count') `
        'Pilot raw receipt omits event_count.'
    Assert-True ($input.receipt.raws[0].PSObject.Properties.Name -ccontains 'final_line_sha256') `
        'Pilot raw receipt omits final_line_sha256.'
    Assert-Equal -Actual $input.receipt.raws[0].event_count -Expected 4
    Assert-SequenceEqual -Actual @($input.receipt.process_identities.role) -Expected @('root', 'child', 'grandchild')
    Assert-Equal -Actual $input.receipt.pilot_intent.relative_path `
        -Expected "$($input.receipt.run_id).pilot-intent.json"
}

Test-Case 'Trusted common inputs select synthetic-process-tree ordinally without changing shell defaults' {
    $trustedCommand = Get-Command Get-M0TrustedCommonInputs -ErrorAction Stop
    Assert-True ($trustedCommand.Parameters.ContainsKey('Scenario')) `
        'Get-M0TrustedCommonInputs does not expose the closed Scenario selector.'

    $pilotInputs = Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot -Scenario 'synthetic-process-tree'
    $defaultInputs = Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot
    $shellInputs = Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot -Scenario 'shell'
    Assert-Equal -Actual ($defaultInputs | ConvertTo-Json -Compress -Depth 100) `
        -Expected ($shellInputs | ConvertTo-Json -Compress -Depth 100)

    foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256')) {
        Assert-Equal -Actual $pilotInputs.$name -Expected $defaultInputs.$name "Trusted '$name' drifted for P."
    }
    $expectedPilotHash = (Get-FileHash -LiteralPath $pilotScenarioPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Equal -Actual $pilotInputs.scenario_sha256 -Expected $expectedPilotHash
    Assert-True ($pilotInputs.scenario_sha256 -cne $shellInputs.scenario_sha256) `
        'P still uses the shell scenario hash.'
    $script:M0PilotExpectedProtocolSha256 = [string]$pilotInputs.protocol_sha256

    foreach ($invalidScenario in @('Synthetic-Process-Tree', 'SHELL', ' synthetic-process-tree', 'synthetic-process-tree ', '')) {
        Assert-Throws {
            Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot -Scenario $invalidScenario
        } 'M0H010|scenario|ordinal|closed'
    }
    Assert-Throws {
        Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot -Scenario $null
    } 'M0H010|scenario|ordinal|closed'
}

Test-Case 'A self-consistently reanchored synthetic scenario still fails its external protocol anchor' {
    $harnessModules = @(Get-Module -All -Name M0.Harness)
    Assert-Equal -Actual $harnessModules.Count -Expected 1
    $scenario = Get-Content -Raw -LiteralPath $pilotScenarioPath | ConvertFrom-Json -Depth 100 -DateKind String
    $scenario.summary_contract.input_sha256 = 'f' * 64
    $mutatedText = (($scenario | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n")
    Assert-True (Test-Json -Json $mutatedText -SchemaFile $pilotScenarioSchemaPath -ErrorAction SilentlyContinue) `
        'Reanchored scenario negative must remain schema-valid.'
    $root = Join-Path $TestTempRoot 'pilot-scenario-self-reanchor'
    New-Item -ItemType Directory -Path $root | Out-Null
    $scenarioPath = Join-Path $root 'synthetic-process-tree.v1.json'
    $sidecarPath = Join-Path $root 'synthetic-process-tree.v1.sha256'
    [IO.File]::WriteAllText($scenarioPath, $mutatedText, [Text.UTF8Encoding]::new($false))
    $mutatedHash = (Get-FileHash -LiteralPath $scenarioPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($sidecarPath, "$mutatedHash  synthetic-process-tree.v1.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        & $harnessModules[0] {
            param($documentPath, $schemaPath, $sidecar, $expectedHash)
            Assert-M0TrustedSyntheticProcessScenario -ScenarioPath $documentPath -SchemaPath $schemaPath `
                -SidecarPath $sidecar -ExpectedSha256 $expectedHash
        } $scenarioPath $pilotScenarioSchemaPath $sidecarPath `
            ((Get-FileHash -LiteralPath $pilotScenarioPath -Algorithm SHA256).Hash)
    } 'M0H010|hash|synthetic|scenario'
}

Test-Case 'Trusted common inputs load Pilot exactly once under retained trust locks' {
    $source = @'
param($RepositoryRoot, $PilotPath, $RenameDestination)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -ErrorAction Stop
$withoutPilot = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot `
    -Scenario 'synthetic-process-tree'
if (@(Get-Module -All -Name M0.Pilot).Count -ne 0) { throw 'Default trusted selector loaded Pilot.' }
$processBefore = @(Get-Module -All -Name M0.Process)
if ($processBefore.Count -ne 1) { throw 'Default trusted selector did not retain one Process instance.' }
$contractBefore = & $processBefore[0] { Assert-M0ProcessNativeContract }
$harness = @(Get-Module -All -Name M0.Harness)
& $harness[0] {
    param($target, $destination)
    $script:M0HarnessTrustedInputsTestHook = [pscustomobject]@{
        stage = 'before-pilot-import-rename'
        target_path = $target
        destination_path = $destination
    }
} $PilotPath $RenameDestination
$withPilot = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot `
    -Scenario 'synthetic-process-tree' -LoadPilot
$loaded = @(Get-Module -All -Name M0.Pilot)
if ($loaded.Count -ne 1 -or [IO.Path]::GetFullPath($loaded[0].Path) -cne [IO.Path]::GetFullPath($PilotPath)) {
    throw 'Pilot was not loaded exactly once from the manifest-locked path.'
}
$processAfter = @(Get-Module -All -Name M0.Process)
$contractAfter = & $processAfter[0] { Assert-M0ProcessNativeContract }
if ($processAfter.Count -ne 1 -or -not [object]::ReferenceEquals($processBefore[0], $processAfter[0]) -or
    $contractBefore.module_instance_id -cne $contractAfter.module_instance_id) {
    throw 'LoadPilot reloaded or replaced the retained Process instance.'
}
if ($withPilot.protocol_sha256 -cne $withoutPilot.protocol_sha256 -or (Test-Path -LiteralPath $RenameDestination)) {
    throw 'LoadPilot changed common inputs or allowed the locked source rename.'
}
'OK'
'@
    $renameDestination = Join-Path $TestTempRoot 'M0.Pilot.rename.psm1'
    $child = Invoke-M0PilotTestChild -Name 'pilot-load-under-trust-locks' -Source $source `
        -Arguments @($RepositoryRoot, $pilotModulePath, $renameDestination)
    Assert-Equal -Actual $child.exit_code -Expected 0 "LoadPilot trust-lock probe failed: $($child.output)"
    Assert-True ($child.output -match 'OK') "LoadPilot trust-lock probe did not finish: $($child.output)"

    $preloadSource = @'
param($RepositoryRoot, $PilotPath)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1') -ErrorAction Stop
Import-Module $PilotPath -ErrorAction Stop
Import-Module (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -ErrorAction Stop
M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot `
    -Scenario 'synthetic-process-tree' -LoadPilot
'@
    $preloaded = Invoke-M0PilotTestChild -Name 'pilot-load-preloaded-reject' -Source $preloadSource `
        -Arguments @($RepositoryRoot, $pilotModulePath)
    Assert-True ($preloaded.exit_code -ne 0) 'LoadPilot accepted a preloaded Pilot module.'
    Assert-True ($preloaded.output -match 'M0H010|preloaded|manifest-locked') `
        "LoadPilot rejected the preload for the wrong reason: $($preloaded.output)"
}

Test-Case 'The synthetic pilot publication module and clean public runner exist before live publication' {
    Assert-True (Test-Path -LiteralPath $pilotModulePath -PathType Leaf) "Required synthetic pilot module is missing: '$pilotModulePath'."
    Assert-True (Test-Path -LiteralPath $pilotRunnerPath -PathType Leaf) "Required synthetic pilot runner is missing: '$pilotRunnerPath'."
}

Import-Module $pilotModulePath -ErrorAction Stop
$pilotModule = @(Get-Module -All -Name M0.Pilot)
if ($pilotModule.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($pilotModule[0].Path).Equals([IO.Path]::GetFullPath($pilotModulePath), [StringComparison]::OrdinalIgnoreCase)) {
    throw [IO.InvalidDataException]::new('Pilot.Tests could not bind exactly one trusted M0.Pilot module instance.')
}
$pilotModule = $pilotModule[0]

function Assert-M0PilotNonWindowsFailClosed {
    param(
        [Parameter(Mandatory)]
        [ValidatePattern('^[a-z0-9-]{1,40}$')]
        [string] $CaseId
    )

    if ($IsWindows) {
        throw [InvalidOperationException]::new(
            'The non-Windows Pilot fail-closed assertion must not replace a Windows live or physical test.'
        )
    }

    $evidenceRoot = Join-Path $TestTempRoot "pilot-not-proven-$CaseId"
    Assert-True (-not (Test-Path -LiteralPath $evidenceRoot)) `
        "Non-Windows fail-closed fixture '$CaseId' did not start with an absent EvidenceRoot."
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 `
        "Non-Windows fail-closed fixture '$CaseId' started with a synthetic helper process."

    $events = [Collections.Generic.List[string]]::new()
    & $pilotModule {
        param($ledger)
        $script:M0PilotLiveTestHook = [pscustomobject]@{ events = $ledger }
    } $events
    try {
        Assert-Throws {
            Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $evidenceRoot `
                -ExperimentId "exp-pilot-linux-$CaseId" -RunId "run-P-linux-$CaseId-01" `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        } 'M0I020|M0I040|not.proven|outside.Windows|physical.Pilot'
    }
    finally {
        & $pilotModule { $script:M0PilotLiveTestHook = $null }
    }

    Assert-True (-not (Test-Path -LiteralPath $evidenceRoot)) `
        "Non-Windows fail-closed fixture '$CaseId' created or mutated EvidenceRoot."
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 `
        "Non-Windows fail-closed fixture '$CaseId' launched a synthetic helper process."
    foreach ($mutatingStage in @(
        'writer_opened',
        'pilot_intent_published',
        'raw_started_flushed',
        'process_opened',
        'snapshot_captured',
        'snapshot_failed',
        'process_closed',
        'artifact_published',
        'evidence_committed',
        'summary_pending',
        'summary_published',
        'sidecar_published'
    )) {
        Assert-True (-not $events.Contains($mutatingStage)) `
            "Non-Windows fail-closed fixture '$CaseId' reached mutating stage '$mutatingStage'."
    }
}

Test-Case 'Every non-Windows Pilot branch proves fail-closed before returning' {
    $testPath = Join-Path $RepositoryRoot 'experiments\m0\harness\tests\Pilot.Tests.ps1'
    $tokens = $null
    $parseErrors = $null
    $testAst = [Management.Automation.Language.Parser]::ParseFile(
        $testPath,
        [ref] $tokens,
        [ref] $parseErrors
    )
    Assert-Equal -Actual @($parseErrors).Count -Expected 0 'Pilot.Tests did not parse for guard inspection.'

    $guardBlocks = [Collections.Generic.List[object]]::new()
    foreach ($ifStatement in @($testAst.FindAll({
        param($node)
        $node -is [Management.Automation.Language.IfStatementAst]
    }, $true))) {
        foreach ($clause in $ifStatement.Clauses) {
            $condition = $clause.Item1.Extent.Text -replace '\s', ''
            if ($condition -ceq '-not$IsWindows') {
                $guardBlocks.Add($clause.Item2)
            }
        }
    }
    Assert-Equal -Actual $guardBlocks.Count -Expected 8 `
        'The closed set of non-Windows Pilot branches changed without updating its fail-closed proof.'

    foreach ($guardBlock in $guardBlocks) {
        $proofs = @($guardBlock.FindAll({
            param($node)
            $node -is [Management.Automation.Language.CommandAst] -and
                $node.GetCommandName() -ceq 'Assert-M0PilotNonWindowsFailClosed'
        }, $true))
        $returns = @($guardBlock.FindAll({
            param($node)
            $node -is [Management.Automation.Language.ReturnStatementAst]
        }, $true))
        Assert-Equal -Actual $proofs.Count -Expected 1 `
            "Non-Windows guard at line $($guardBlock.Extent.StartLineNumber) lacks one explicit fail-closed proof."
        Assert-Equal -Actual $returns.Count -Expected 1 `
            "Non-Windows guard at line $($guardBlock.Extent.StartLineNumber) must have one explicit return."
        Assert-True ($proofs[0].Extent.StartOffset -lt $returns[0].Extent.StartOffset) `
            "Non-Windows guard at line $($guardBlock.Extent.StartLineNumber) returns before its fail-closed proof."
    }
}

Test-Case 'The pilot module exports only the closed publication and validation API' {
    Assert-SequenceEqual -Actual @($pilotModule.ExportedFunctions.Keys | Sort-Object) -Expected @(
        'Assert-M0PilotArtifacts',
        'Assert-M0SyntheticPilotPublication',
        'Assert-M0SyntheticPilotSummary',
        'Invoke-M0SyntheticPilot',
        'New-M0SyntheticPilotSummary'
    )
}

Test-Case 'Invoke requires an externally captured protocol anchor before any live mutation' {
    $command = Get-Command -Name Invoke-M0SyntheticPilot -Module M0.Pilot -ErrorAction Stop
    Assert-True $command.Parameters.ContainsKey('ExpectedProtocolSha256') `
        'Invoke does not expose the external protocol trust anchor.'
    $parameterAttribute = @($command.Parameters.ExpectedProtocolSha256.Attributes | Where-Object {
        $_ -is [Management.Automation.ParameterAttribute]
    })
    Assert-True ($parameterAttribute.Count -eq 1 -and $parameterAttribute[0].Mandatory) `
        'ExpectedProtocolSha256 is not mandatory.'

    $wrongRoot = Join-Path $TestTempRoot 'pilot-invoke-wrong-anchor-zero-mutation'
    Assert-True (-not (Test-Path -LiteralPath $wrongRoot))
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $wrongRoot `
            -ExperimentId 'exp-pilot-wrong-anchor' -RunId 'run-P-wrong-anchor-01' `
            -ExpectedProtocolSha256 ('0' * 64)
    } 'M0I021|protocol|expected|hash'
    Assert-True (-not (Test-Path -LiteralPath $wrongRoot)) `
        'Wrong protocol anchor created an Evidence root before failing.'
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0

    $cloneRoot = Join-Path $TestTempRoot 'pilot-fake-repository'
    New-Item -ItemType Directory -Path $cloneRoot | Out-Null
    $cloneEvidence = Join-Path $TestTempRoot 'pilot-clone-zero-mutation'
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $cloneRoot -EvidenceRoot $cloneEvidence `
            -ExperimentId 'exp-pilot-clone' -RunId 'run-P-clone-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I023|RepositoryRoot|exact|trusted'
    Assert-True (-not (Test-Path -LiteralPath $cloneEvidence))

    $stubRoot = Join-Path $TestTempRoot 'pilot-invoke-trusted-missing-root-zero-mutation'
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $stubRoot `
            -ExperimentId 'exp-pilot-trusted-root' -RunId 'run-P-trusted-root-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I023|EvidenceRoot|existing|out.m0'
    Assert-True (-not (Test-Path -LiteralPath $stubRoot))
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0
}

Test-Case 'The clean Pilot runner reaches trusted ordinal preflight without mutating Evidence' {
    $root = Join-Path $TestTempRoot 'pilot-runner-clean-preflight'
    Assert-True (-not (Test-Path -LiteralPath $root))
    $child = Invoke-M0PilotTestChild -Name 'pilot-runner-clean-preflight' -Source @'
param($RunnerPath, $EvidenceRoot)
& $RunnerPath -EvidenceRoot $EvidenceRoot -ExperimentId 'exp-pilot-runner-preflight' `
    -RunId 'run-P-runner-preflight-01' -CrashAfterBoundary Invalid_Boundary
'@ -Arguments @($pilotRunnerPath, $root)
    Assert-True ($child.exit_code -ne 0) 'Clean runner accepted a nonordinal live boundary.'
    Assert-True ($child.output -match 'M0I041|ordinal|boundary') `
        "Clean runner failed before trusted ordinal preflight: $($child.output)"
    Assert-True (-not (Test-Path -LiteralPath $root)) 'Clean runner mutated Evidence before preflight failed.'
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0
}

Test-Case 'The public pure summary parameter carries a non-null payload without colliding with automatic input' {
    $command = Get-Command -Name New-M0SyntheticPilotSummary -Module M0.Pilot -ErrorAction Stop
    Assert-True $command.Parameters.ContainsKey('SummaryInput') `
        'The pure builder did not expose its payload under the safe SummaryInput parameter.'
    Assert-True (@($command.Parameters.SummaryInput.Aliases) -ccontains 'Input') `
        'The compatibility Input spelling is not an explicit alias of SummaryInput.'
    $summaryInput = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath |
        ConvertFrom-Json -Depth 100 -DateKind String
    $built = New-M0SyntheticPilotSummary -Input $summaryInput
    Assert-True ($null -ne $built.document) 'The pure builder lost its non-null summary input.'
}

Test-Case 'Physical Pilot contract trust is externally anchored before live preflight and mutation' {
    $root = Join-Path $TestTempRoot 'pilot-pure-stub-no-mutation'
    Assert-True (-not (Test-Path -LiteralPath $root))
    $trusted = Assert-M0PilotArtifacts -ProtocolRoot $pilotProtocolRoot `
        -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    Assert-Equal -Actual $trusted.protocol_sha256 -Expected $script:M0PilotExpectedProtocolSha256
    Assert-Equal -Actual $trusted.scenario_sha256 -Expected (
        (Get-FileHash -LiteralPath $pilotScenarioPath -Algorithm SHA256).Hash.ToLowerInvariant()
    )
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\harness\manifest.v1.json') |
        ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($runtimePath in @($pilotModulePath, $pilotProcessModulePath)) {
        $relative = [IO.Path]::GetRelativePath($RepositoryRoot, $runtimePath).Replace('\', '/')
        $entry = @($manifest.runtime_files | Where-Object relative_path -ceq $relative)
        Assert-Equal -Actual $entry.Count -Expected 1 "Manifest omitted loaded runtime '$relative'."
        Assert-Equal -Actual $entry[0].sha256 -Expected (
            (Get-FileHash -LiteralPath $runtimePath -Algorithm SHA256).Hash.ToLowerInvariant()
        ) "Loaded runtime '$relative' differs from its manifest entry."
    }
    Assert-Throws {
        Assert-M0PilotArtifacts -ProtocolRoot $pilotProtocolRoot -ExpectedProtocolSha256 ('0' * 64)
    } 'M0I|protocol|hash|anchor'
    $cloneRoot = Join-Path $TestTempRoot 'pilot-protocol-clone'
    Copy-Item -LiteralPath $pilotProtocolRoot -Destination $cloneRoot -Recurse
    Assert-Throws {
        Assert-M0PilotArtifacts -ProtocolRoot $cloneRoot `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|ProtocolRoot|trusted|repository'

    foreach ($case in @(
        [pscustomobject]@{ stage = 'after-all-open-overwrite'; target = (Join-Path $pilotProtocolRoot 'protocol.v1.json') },
        [pscustomobject]@{ stage = 'after-all-open-rename'; target = $pilotProcessArtifactSchemaPath }
    )) {
        $hook = [pscustomobject]@{
            stage = $case.stage
            target_path = $case.target
            destination_path = (Join-Path $TestTempRoot "pilot-held-$($case.stage).moved")
        }
        & $pilotModule { param($value) $script:M0PilotArtifactsTestHook = $value } $hook
        $heldResult = Assert-M0PilotArtifacts -ProtocolRoot $pilotProtocolRoot `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        Assert-Equal -Actual $heldResult.protocol_sha256 -Expected $script:M0PilotExpectedProtocolSha256
        Assert-True (-not (Test-Path -LiteralPath $hook.destination_path)) `
            "Retained contract stage '$($case.stage)' left a renamed file."
    }

    $hardlinkPath = Join-Path $TestTempRoot 'pilot-held-scenario-schema-hardlink.json'
    $hardlinkHook = [pscustomobject]@{
        stage = 'after-all-open-hardlink'
        target_path = $pilotScenarioSchemaPath
        link_path = $hardlinkPath
    }
    & $pilotModule { param($value) $script:M0PilotArtifactsTestHook = $value } $hardlinkHook
    try {
        Assert-Throws {
            Assert-M0PilotArtifacts -ProtocolRoot $pilotProtocolRoot `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        } 'M0I|hardlink|link|identity|retained'
    }
    finally {
        if (Test-Path -LiteralPath $hardlinkPath) { Remove-Item -LiteralPath $hardlinkPath }
    }
    $released = Assert-M0PilotArtifacts -ProtocolRoot $pilotProtocolRoot `
        -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    Assert-Equal -Actual $released.protocol_sha256 -Expected $script:M0PilotExpectedProtocolSha256 `
        'Pilot contract handles were not released after an adversarial failure.'
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
            -ExperimentId 'exp-pilot-pure-stub' -RunId 'run-P-pure-stub-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary Invalid_Boundary
    } 'M0I041|ordinal|boundary'
    Assert-True (-not (Test-Path -LiteralPath $root)) 'Rejected live preflight mutated EvidenceRoot.'
}

Test-Case 'The private pure pilot-intent builder binds every path to RunId and matches its golden bytes' {
    $intent = Get-Content -Raw -LiteralPath $pilotIntentGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    $first = & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $intent
    $goldenBytes = [IO.File]::ReadAllBytes($pilotIntentGoldenPath)
    $goldenText = [Text.UTF8Encoding]::new($false, $true).GetString($goldenBytes)
    Assert-Equal -Actual $first.canonical_json -Expected $goldenText
    Assert-Equal -Actual $first.size_bytes -Expected ([long]$goldenBytes.Length)
    Assert-Equal -Actual $first.sha256 -Expected (
        [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($goldenBytes)).ToLowerInvariant()
    )

    $reordered = [pscustomobject][ordered]@{}
    $properties = @((Copy-M0PilotTestValue $intent).PSObject.Properties)
    [array]::Reverse($properties)
    foreach ($property in $properties) {
        $reordered | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
    }
    $second = & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $reordered
    Assert-Equal -Actual $second.canonical_json -Expected $first.canonical_json
    Assert-Equal -Actual $second.sha256 -Expected $first.sha256

    $vectors = @(
        [pscustomobject]@{ case_id = 'run-id'; apply = { param($value) $value.run_id = 'run-P-foreign-01' } },
        [pscustomobject]@{ case_id = 'slot-swap'; apply = {
                param($value) $raw = $value.expected_paths.raw
                $value.expected_paths.raw = $value.expected_paths.run; $value.expected_paths.run = $raw
            } },
        [pscustomobject]@{ case_id = 'slot-alias'; apply = {
                param($value) $value.expected_paths.evidence_intent = $value.expected_paths.snapshot
            } },
        [pscustomobject]@{ case_id = 'foreign-path'; apply = {
                param($value) $value.expected_paths.process_tree = 'artifacts/foreign.process-tree.json'
            } },
        [pscustomobject]@{ case_id = 'uppercase-common-hash'; apply = {
                param($value) $value.common_inputs.scenario_sha256 = 'A' * 64
            } },
        [pscustomobject]@{ case_id = 'extra-common'; apply = {
                param($value) $value.common_inputs | Add-Member -NotePropertyName foreign_sha256 -NotePropertyValue ('f' * 64)
            } },
        [pscustomobject]@{ case_id = 'missing-common'; apply = {
                param($value) $value.common_inputs.PSObject.Properties.Remove('fixture_sha256')
            } },
        [pscustomobject]@{ case_id = 'run-started-hash'; apply = {
                param($value) $value.run_started.canonical_line_sha256 = 'f' * 64
            } },
        [pscustomobject]@{ case_id = 'run-started-size'; apply = {
                param($value) $value.run_started.size_bytes++
            } }
    )
    foreach ($vector in $vectors) {
        $candidate = Copy-M0PilotTestValue $intent
        & $vector.apply $candidate
        Assert-Throws {
            & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $candidate
        } "M0I|intent|path|schema|$($vector.case_id)"
    }

    $alternate = Copy-M0PilotTestValue $intent
    $alternateRunId = 'run-P-alternate-synthetic-process-tree-02'
    $alternate.run_id = $alternateRunId
    $alternate.run_started.event.run_id = $alternateRunId
    $alternatePaths = & $pilotModule { param($runId) Get-M0PilotExpectedPaths -RunId $runId } $alternateRunId
    foreach ($name in @($alternatePaths.PSObject.Properties.Name)) {
        $alternate.expected_paths.$name = $alternatePaths.$name
    }
    $alternate.run_started = & $pilotModule {
        param($event) New-M0SyntheticPilotRunStartedAnchor -Event $event
    } $alternate.run_started.event
    $alternateBuilt = & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $alternate
    Assert-Equal -Actual $alternateBuilt.document.run_id -Expected $alternateRunId
    Assert-Equal -Actual $alternateBuilt.document.expected_paths.raw -Expected "$alternateRunId.jsonl"
    Assert-Equal -Actual $alternateBuilt.document.expected_paths.process_tree `
        -Expected "artifacts/$alternateRunId.process-tree.json"
}

Test-Case 'The pure pilot builder rejects noncausal clocks identities and RunId-derived path drift' {
    $valid = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    $vectors = @(
        [pscustomobject]@{ case_id = 'utc-regression'; apply = {
                param($value) $value.receipt.started_utc = '2026-08-24T12:00:02.0000000Z'
            } },
        [pscustomobject]@{ case_id = 'qpc-regression'; apply = {
                param($value) $value.receipt.started_qpc_ticks = 3000000
            } },
        [pscustomobject]@{ case_id = 'utc-noncanonical-short'; apply = {
                param($value) $value.receipt.started_utc = '2026-08-24T12:00:00Z'
            } },
        [pscustomobject]@{ case_id = 'utc-noncanonical-offset'; apply = {
                param($value) $value.receipt.started_utc = '2026-08-24T09:00:00.0000000-03:00'
            } },
        [pscustomobject]@{ case_id = 'utc-impossible-date'; apply = {
                param($value) $value.receipt.started_utc = '2026-99-99T12:00:00.0000000Z'
            } },
        [pscustomobject]@{ case_id = 'qpc-int64-overflow'; apply = {
                param($value) $value.receipt.sealed_qpc_ticks = [decimal]9223372036854775808
            } },
        [pscustomobject]@{ case_id = 'size-int64-overflow'; apply = {
                param($value) $value.receipt.raws[0].size_bytes = [decimal]9223372036854775808
            } },
        [pscustomobject]@{ case_id = 'filetime-uint64-overflow'; apply = {
                param($value) $value.receipt.process_identities[0].creation_time_filetime_utc = '18446744073709551616'
            } },
        [pscustomobject]@{ case_id = 'filetime-leading-zero'; apply = {
                param($value) $value.receipt.process_identities[0].creation_time_filetime_utc = '0134000000000000001'
            } },
        [pscustomobject]@{ case_id = 'empty-live-identities'; apply = {
                param($value) $value.receipt.process_identities = @()
            } },
        [pscustomobject]@{ case_id = 'identity-order'; apply = {
                param($value) $first = $value.receipt.process_identities[0]
                $value.receipt.process_identities[0] = $value.receipt.process_identities[1]
                $value.receipt.process_identities[1] = $first
            } },
        [pscustomobject]@{ case_id = 'identity-role-mismatch'; apply = {
                param($value) $value.receipt.process_identities[1].role = 'grandchild'
            } },
        [pscustomobject]@{ case_id = 'duplicate-pid-different-birth'; apply = {
                param($value) $value.receipt.process_identities[1].pid = $value.receipt.process_identities[0].pid
            } },
        [pscustomobject]@{ case_id = 'run-id-does-not-bind-paths'; apply = {
                param($value) $value.receipt.run_id = 'run-P-foreign-01'
            } },
        [pscustomobject]@{ case_id = 'raw-run-slot-swap'; apply = {
                param($value) $raw = $value.receipt.raws[0].relative_path
                $value.receipt.raws[0].relative_path = $value.receipt.run.relative_path
                $value.receipt.run.relative_path = $raw
            } },
        [pscustomobject]@{ case_id = 'intent-snapshot-alias'; apply = {
                param($value) $value.receipt.intent.relative_path = $value.receipt.snapshot.relative_path
            } },
        [pscustomobject]@{ case_id = 'artifact-path-drift'; apply = {
                param($value) $value.receipt.artifacts[0].relative_path = 'artifacts/foreign.process-tree.json'
            } }
    )
    foreach ($vector in $vectors) {
        $candidate = Copy-M0PilotTestValue $valid
        & $vector.apply $candidate
        Assert-Throws {
            New-M0SyntheticPilotSummary -Input $candidate
        } "M0I|clock|identity|path|run|$($vector.case_id)"
    }

    $equalClocks = Copy-M0PilotTestValue $valid
    $equalClocks.receipt.sealed_utc = $equalClocks.receipt.started_utc
    $equalClocks.receipt.sealed_qpc_ticks = $equalClocks.receipt.started_qpc_ticks
    $equalClockSummary = New-M0SyntheticPilotSummary -Input $equalClocks
    Assert-Equal -Actual $equalClockSummary.document.receipt.sealed_qpc_ticks `
        -Expected $equalClockSummary.document.receipt.started_qpc_ticks `
        'Evidence-compatible equal monotonic clocks were rejected.'

    $sameBirth = Copy-M0PilotTestValue $valid
    $sameBirth.receipt.process_identities[1].creation_time_filetime_utc = `
        $sameBirth.receipt.process_identities[0].creation_time_filetime_utc
    $sameBirthSummary = New-M0SyntheticPilotSummary -Input $sameBirth
    Assert-True ($sameBirthSummary.document.receipt.process_identities[0].pid -ne `
        $sameBirthSummary.document.receipt.process_identities[1].pid) `
        'Distinct PIDs sharing one FILETIME must remain valid strong identity pairs.'
}

Test-Case 'Pilot intent and summary input are one cross-bound fixture instead of independent self-anchored oracles' {
    $intent = Get-Content -Raw -LiteralPath $pilotIntentGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    $summaryInput = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath |
        ConvertFrom-Json -Depth 100 -DateKind String
    $binding = & $pilotModule {
        param($intentValue, $summaryValue)
        Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intentValue -SummaryInput $summaryValue
    } $intent $summaryInput
    Assert-Equal -Actual $binding.run_id -Expected $intent.run_id

    $alternateRunId = 'run-P-alternate-synthetic-process-tree-02'
    $alternateIntent = Copy-M0PilotTestValue $intent
    $alternateIntent.run_id = $alternateRunId
    $alternateIntent.run_started.event.run_id = $alternateRunId
    $alternatePaths = & $pilotModule { param($runId) Get-M0PilotExpectedPaths -RunId $runId } $alternateRunId
    foreach ($name in @($alternatePaths.PSObject.Properties.Name)) {
        $alternateIntent.expected_paths.$name = $alternatePaths.$name
    }
    $alternateIntent.run_started = & $pilotModule {
        param($event) New-M0SyntheticPilotRunStartedAnchor -Event $event
    } $alternateIntent.run_started.event
    $alternateIntentBuilt = & $pilotModule {
        param($value) New-M0SyntheticPilotIntent -Intent $value
    } $alternateIntent

    $alternateSummary = Copy-M0PilotTestValue $summaryInput
    $alternateSummary.receipt.run_id = $alternateRunId
    $alternateSummary.receipt.pilot_intent.relative_path = $alternatePaths.pilot_intent
    $alternateSummary.receipt.pilot_intent.sha256 = $alternateIntentBuilt.sha256
    $alternateSummary.receipt.pilot_intent.size_bytes = $alternateIntentBuilt.size_bytes
    $alternateSummary.receipt.raws[0].relative_path = $alternatePaths.raw
    $alternateSummary.receipt.raws[0].first_line_sha256 = $alternateIntentBuilt.document.run_started.canonical_line_sha256
    $alternateSummary.receipt.raws[0].first_line_size_bytes = $alternateIntentBuilt.document.run_started.size_bytes
    $alternateSummary.receipt.intent.relative_path = $alternatePaths.evidence_intent
    $alternateSummary.receipt.run.relative_path = $alternatePaths.run
    $alternateSummary.receipt.snapshot.relative_path = $alternatePaths.snapshot
    $alternateSummary.receipt.artifacts[0].relative_path = $alternatePaths.process_tree
    $alternateSummaryBuilt = New-M0SyntheticPilotSummary -Input $alternateSummary
    Assert-Equal -Actual $alternateSummaryBuilt.document.receipt.run_id -Expected $alternateRunId
    $alternateBinding = & $pilotModule {
        param($intentValue, $summaryValue)
        Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intentValue -SummaryInput $summaryValue
    } $alternateIntent $alternateSummary
    Assert-Equal -Actual $alternateBinding.run_id -Expected $alternateRunId

    $mutatedIntent = Copy-M0PilotTestValue $intent
    $mutatedIntent.experiment_id = 'exp-reanchored-pilot-intent'
    $mutatedIntent.run_started.event.experiment_id = $mutatedIntent.experiment_id
    $mutatedIntent.run_started = & $pilotModule {
        param($event) New-M0SyntheticPilotRunStartedAnchor -Event $event
    } $mutatedIntent.run_started.event
    $null = & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $mutatedIntent
    Assert-Throws {
        & $pilotModule {
            param($intentValue, $summaryValue)
            Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intentValue -SummaryInput $summaryValue
        } $mutatedIntent $summaryInput
    } 'M0I01|fixture|intent|experiment|binding'

    Assert-Throws {
        & $pilotModule {
            param($intentValue, $summaryValue)
            Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intentValue -SummaryInput $summaryValue
        } $intent $alternateSummary
    } 'M0I01|fixture|run|path|binding'

    $crossVectors = @(
        [pscustomobject]@{ case_id = 'pilot-intent-hash'; apply = {
                param($value) $value.receipt.pilot_intent.sha256 = 'f' * 64
            } },
        [pscustomobject]@{ case_id = 'pilot-intent-size'; apply = {
                param($value) $value.receipt.pilot_intent.size_bytes++
            } },
        [pscustomobject]@{ case_id = 'first-line'; apply = {
                param($value) $value.receipt.raws[0].first_line_sha256 = 'f' * 64
            } },
        [pscustomobject]@{ case_id = 'first-line-size'; apply = {
                param($value) $value.receipt.raws[0].first_line_size_bytes++
            } },
        [pscustomobject]@{ case_id = 'started-utc'; apply = {
                param($value) $value.receipt.started_utc = '2026-08-24T11:59:59.0000000Z'
            } },
        [pscustomobject]@{ case_id = 'started-qpc'; apply = {
                param($value) $value.receipt.started_qpc_ticks = 999999
            } },
        [pscustomobject]@{ case_id = 'qpc-frequency'; apply = {
                param($value) $value.receipt.qpc_frequency = 10000001
            } },
        [pscustomobject]@{ case_id = 'common-input'; apply = {
                param($value) $value.receipt.common_inputs.protocol_sha256 = 'e' * 64
            } }
    )
    foreach ($vector in $crossVectors) {
        $candidate = Copy-M0PilotTestValue $summaryInput
        & $vector.apply $candidate
        $null = New-M0SyntheticPilotSummary -Input $candidate
        Assert-Throws {
            & $pilotModule {
                param($intentValue, $summaryValue)
                Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intentValue -SummaryInput $summaryValue
            } $intent $candidate
        } "M0I01|fixture|binding|$($vector.case_id)"
    }

    $rawTooSmall = Copy-M0PilotTestValue $summaryInput
    $rawTooSmall.receipt.raws[0].size_bytes = $rawTooSmall.receipt.raws[0].first_line_size_bytes
    Assert-Throws {
        New-M0SyntheticPilotSummary -Input $rawTooSmall
    } 'M0I010|raw size|first canonical line'
}

Test-Case 'The pure synthetic pilot summary is canonical and byte-identical to its deterministic golden' {
    $input = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    $first = New-M0SyntheticPilotSummary -Input $input
    $reordered = ConvertTo-M0PilotDeepReorderedValue -Value (Copy-M0PilotTestValue $input)
    $second = New-M0SyntheticPilotSummary -Input $reordered

    $goldenBytes = [IO.File]::ReadAllBytes($pilotSummaryGoldenPath)
    $goldenText = [Text.UTF8Encoding]::new($false, $true).GetString($goldenBytes)
    Assert-Equal -Actual $first.canonical_json -Expected $goldenText
    Assert-Equal -Actual $second.canonical_json -Expected $first.canonical_json
    Assert-Equal -Actual $second.sha256 -Expected $first.sha256
    Assert-Equal -Actual $first.size_bytes -Expected ([long]$goldenBytes.Length)
    Assert-True (Test-Json -Json $first.canonical_json -SchemaFile $pilotSummarySchemaPath -ErrorAction SilentlyContinue)
    $asserted = Assert-M0SyntheticPilotSummary -Summary $first.document
    Assert-Equal -Actual $asserted.sha256 -Expected $first.sha256

    $noncanonicalCapabilities = Copy-M0PilotTestValue $input
    [array]::Reverse($noncanonicalCapabilities.process.missing_capabilities)
    Assert-Throws {
        New-M0SyntheticPilotSummary -Input $noncanonicalCapabilities
    } 'M0I010|missing_capabilities|canonical|order'

    $childSource = @'
param($ModulePath, $InputPath, $OutputPath, $CultureName)
$ErrorActionPreference = 'Stop'
$culture = [Globalization.CultureInfo]::GetCultureInfo($CultureName)
[Globalization.CultureInfo]::CurrentCulture = $culture
[Globalization.CultureInfo]::CurrentUICulture = $culture
Import-Module $ModulePath -ErrorAction Stop
$input = Get-Content -Raw -LiteralPath $InputPath | ConvertFrom-Json -Depth 100 -DateKind String
$built = New-M0SyntheticPilotSummary -Input $input
[IO.File]::WriteAllText($OutputPath, $built.canonical_json, [Text.UTF8Encoding]::new($false))
[Console]::Out.Write(([pscustomobject]@{ sha256 = $built.sha256; size_bytes = $built.size_bytes } | ConvertTo-Json -Compress))
'@
    $childOutputs = [Collections.Generic.List[string]]::new()
    $childHashes = [Collections.Generic.List[string]]::new()
    foreach ($case in @(
        [pscustomobject]@{ id = 'tr'; culture = 'tr-TR' },
        [pscustomobject]@{ id = 'fr'; culture = 'fr-FR' }
    )) {
        $root = Join-Path $TestTempRoot "pilot-summary-independent-$($case.id)"
        New-Item -ItemType Directory -Path $root | Out-Null
        $inputCopy = Join-Path $root 'pilot-summary-input.v1.json'
        $outputCopy = Join-Path $root 'pilot-summary.v1.json'
        Copy-Item -LiteralPath $pilotSummaryInputGoldenPath -Destination $inputCopy
        $child = Invoke-M0PilotTestChild -Name "pilot-summary-independent-$($case.id)" -Source $childSource `
            -Arguments @($pilotModulePath, $inputCopy, $outputCopy, $case.culture)
        Assert-Equal -Actual $child.exit_code -Expected 0 "Independent builder failed: $($child.output)"
        $childResult = $child.output | ConvertFrom-Json
        $childText = [IO.File]::ReadAllText($outputCopy, [Text.UTF8Encoding]::new($false, $true))
        $childOutputs.Add($childText)
        $childHashes.Add([string]$childResult.sha256)
        Assert-Equal -Actual $childText -Expected $goldenText
        Assert-Equal -Actual ([long](Get-Item -LiteralPath $outputCopy).Length) -Expected ([long]$goldenBytes.Length)
    }
    Assert-Equal -Actual $childOutputs[1] -Expected $childOutputs[0]
    Assert-Equal -Actual $childHashes[1] -Expected $childHashes[0]

    $volatile = Copy-M0PilotTestValue $input
    $volatile.receipt.experiment_id = 'exp-other-deterministic-fixture'
    $volatile.receipt.transaction_id = 'f' * 64
    $volatile.receipt.common_inputs.protocol_sha256 = 'e' * 64
    $volatile.receipt.started_utc = '2026-08-24T13:00:00.0000000Z'
    $volatile.receipt.sealed_utc = '2026-08-24T13:00:01.0000000Z'
    $volatile.receipt.started_qpc_ticks = 3000000
    $volatile.receipt.sealed_qpc_ticks = 4000000
    for ($identityIndex = 0; $identityIndex -lt 3; $identityIndex++) {
        $volatile.receipt.process_identities[$identityIndex].pid = 201 + $identityIndex
        $volatile.receipt.process_identities[$identityIndex].creation_time_filetime_utc = `
            "13400000000000010$identityIndex"
    }
    $volatileSummary = New-M0SyntheticPilotSummary -Input $volatile
    Assert-Equal -Actual $volatileSummary.semantic_json -Expected $first.semantic_json
    Assert-Equal -Actual $volatileSummary.semantic_sha256 -Expected $first.semantic_sha256
    Assert-True ($volatileSummary.canonical_json -cne $first.canonical_json) `
        'Receipt volatility was incorrectly erased from the full summary.'

    $differentOutcome = Copy-M0PilotTestValue $input
    $differentOutcome.process.state = 'complete'
    $differentOutcome.process.error_code = $null
    $differentOutcome.process.missing_capabilities = @()
    $differentOutcome.check.status = 'passed'
    $differentOutcome.check.reason_code = $null
    $differentOutcome.check.status_reason = $null
    $differentOutcome.check.error_code = $null
    $differentOutcome.run.status = 'valid'
    $differentOutcome.run.status_reason = $null
    $differentOutcome.run.error_code = $null
    $differentSummary = New-M0SyntheticPilotSummary -Input $differentOutcome
    Assert-True ($differentSummary.semantic_sha256 -cne $first.semantic_sha256) `
        'A changed process/check/run outcome did not change semantic bytes.'
}

Test-Case 'The public summary asserter rejects schema-valid semantic and receipt drift' {
    $summaryInput = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath |
        ConvertFrom-Json -Depth 100 -DateKind String
    $valid = New-M0SyntheticPilotSummary -Input $summaryInput
    $crossPair = Copy-M0PilotTestValue $valid.document
    $crossPair.semantic.check.status = 'passed'
    $crossPair.semantic.check.reason_code = $null
    $crossPair.semantic.check.status_reason = $null
    $crossPair.semantic.check.error_code = $null
    $crossPair.semantic.run.status = 'valid'
    $crossPair.semantic.run.status_reason = $null
    $crossPair.semantic.run.error_code = $null
    Assert-True (-not (Test-Json -Json ($crossPair | ConvertTo-Json -Compress -Depth 100) `
        -SchemaFile $pilotSummarySchemaPath -ErrorAction SilentlyContinue)) `
        'Pilot output schema accepted partial process with passed/valid outcome.'
    $vectors = @(
        [pscustomobject]@{ case_id = 'raw-path'; apply = {
                param($value) $value.receipt.raws[0].relative_path = 'run-P-schema-valid-foreign-01.jsonl'
            } },
        [pscustomobject]@{ case_id = 'clock-regression'; apply = {
                param($value) $value.receipt.started_qpc_ticks = $value.receipt.sealed_qpc_ticks + 1
            } }
    )
    foreach ($vector in $vectors) {
        $candidate = Copy-M0PilotTestValue $valid.document
        & $vector.apply $candidate
        $json = $candidate | ConvertTo-Json -Compress -Depth 100
        Assert-True (Test-Json -Json $json -SchemaFile $pilotSummarySchemaPath -ErrorAction SilentlyContinue) `
            "Tampered summary '$($vector.case_id)' must remain structurally schema-valid."
        Assert-Throws {
            Assert-M0SyntheticPilotSummary -Summary $candidate
        } "M0I010|canonical|projection|path|clock|$($vector.case_id)"
    }
}

Test-Case 'Pure Pilot import and builders do not load live modules or mutate the filesystem' {
    $root = Join-Path $TestTempRoot 'pilot-pure-isolation-root'
    New-Item -ItemType Directory -Path $root | Out-Null
    $source = @'
param($ModulePath, $IntentPath, $SummaryInputPath, $EmptyRoot)
$ErrorActionPreference = 'Stop'
$forbidden = @('M0.Process','M0.Evidence','M0.Summary','M0.Statistics','M0.Markers','M0.Fixture','M0.Visual')
if (@(Get-Module -All -Name $forbidden).Count -ne 0) { throw 'Forbidden module was preloaded.' }
$beforeFiles = @(Get-ChildItem -LiteralPath $EmptyRoot -Force -Recurse)
$module = Import-Module $ModulePath -PassThru -ErrorAction Stop
$intent = Get-Content -Raw -LiteralPath $IntentPath | ConvertFrom-Json -Depth 100 -DateKind String
$summaryInput = Get-Content -Raw -LiteralPath $SummaryInputPath | ConvertFrom-Json -Depth 100 -DateKind String
$null = & $module { param($value) New-M0SyntheticPilotIntent -Intent $value } $intent
$summary = New-M0SyntheticPilotSummary -Input $summaryInput
$null = Assert-M0SyntheticPilotSummary -Summary $summary.document
if (@(Get-Module -All -Name $forbidden).Count -ne 0) { throw 'Pure Pilot loaded a forbidden live dependency.' }
$afterFiles = @(Get-ChildItem -LiteralPath $EmptyRoot -Force -Recurse)
if ($beforeFiles.Count -ne 0 -or $afterFiles.Count -ne 0) { throw 'Pure Pilot mutated the supplied empty root.' }
'OK'
'@
    $child = Invoke-M0PilotTestChild -Name 'pilot-pure-isolation' -Source $source `
        -Arguments @($pilotModulePath, $pilotIntentGoldenPath, $pilotSummaryInputGoldenPath, $root)
    Assert-Equal -Actual $child.exit_code -Expected 0 "Pure isolation child failed: $($child.output)"
    Assert-True ($child.output -match 'OK')
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Force -Recurse).Count -Expected 0
}

Test-Case 'Pilot summary outcome branches are recomputed from process and canonical cleanup' {
    $base = Get-Content -Raw -LiteralPath $pilotSummaryInputGoldenPath | ConvertFrom-Json -Depth 100 -DateKind String
    $vectors = @(
        [pscustomobject]@{ case_id = 'complete'; expected_check = 'passed'; expected_run = 'valid'; apply = {
                param($value)
                $value.process.state = 'complete'; $value.process.error_code = $null; $value.process.missing_capabilities = @()
                $value.check.status = 'passed'; $value.check.reason_code = $null; $value.check.status_reason = $null; $value.check.error_code = $null
                $value.run.status = 'valid'; $value.run.status_reason = $null; $value.run.error_code = $null
            } },
        [pscustomobject]@{ case_id = 'partial'; expected_check = 'skipped'; expected_run = 'inconclusive'; apply = { param($value) } },
        [pscustomobject]@{ case_id = 'unavailable'; expected_check = 'skipped'; expected_run = 'inconclusive'; apply = {
                param($value)
                $value.process.state = 'unavailable'; $value.process.error_code = 'safe_identity_unavailable'
                $value.process.snapshot_origin = 'pre_snapshot_fallback'
                $value.process.missing_capabilities = @('etw-process-start-stop', 'safe-process-identity')
                $value.receipt.process_identities = @()
                $value.check.reason_code = 'process_tree_unavailable'
            } },
        [pscustomobject]@{ case_id = 'unavailable-no-authority'; expected_check = 'skipped'; expected_run = 'inconclusive'; apply = {
                param($value)
                $value.process.state = 'unavailable'; $value.process.error_code = 'etw_trace_unavailable'
                $value.process.snapshot_origin = 'pre_snapshot_fallback'
                $value.process.missing_capabilities = @('etw-process-start-stop')
                $value.cleanup.attempted = $false
                $value.receipt.process_identities = @()
                $value.check.reason_code = 'process_tree_unavailable'
            } },
        [pscustomobject]@{ case_id = 'root-missing-no-authority'; expected_check = 'skipped'; expected_run = 'inconclusive'; apply = {
                param($value)
                $value.process.state = 'partial'; $value.process.error_code = 'root_identity_missing'
                $value.process.snapshot_origin = 'observed'
                $value.process.missing_capabilities = @('root-handle')
                $value.cleanup.attempted = $false
                $value.receipt.process_identities = @()
            } },
        [pscustomobject]@{ case_id = 'cleanup-failure'; expected_check = 'skipped'; expected_run = 'inconclusive'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.failure_code = 'job_close_failed'
                $value.cleanup.failure_codes = @('job_close_failed')
                $value.check.reason_code = 'job_close_failed'
            } },
        [pscustomobject]@{ case_id = 'survivor'; expected_check = 'failed'; expected_run = 'failed'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.survivor_count = 1
                $value.cleanup.failure_code = 'helper_survived_shutdown'
                $value.cleanup.failure_codes = @('helper_survived_shutdown')
                $value.check.status = 'failed'; $value.check.reason_code = 'helper_survived_shutdown'
                $value.check.status_reason = 'helper_survived_shutdown'; $value.check.error_code = 'helper_survived_shutdown'
                $value.run.status = 'failed'; $value.run.status_reason = 'helper_survived_shutdown'
                $value.run.error_code = 'helper_survived_shutdown'
            } },
        [pscustomobject]@{ case_id = 'multicausal-primary-preserved'; expected_check = 'failed'; expected_run = 'failed'; apply = {
                param($value)
                $value.cleanup.completed = $false; $value.cleanup.survivor_count = 1
                $value.cleanup.failure_code = 'snapshot_validation_failed'
                $value.cleanup.failure_codes = @('snapshot_validation_failed', 'helper_survived_shutdown')
                $value.check.status = 'failed'; $value.check.reason_code = 'helper_survived_shutdown'
                $value.check.status_reason = 'helper_survived_shutdown'; $value.check.error_code = 'helper_survived_shutdown'
                $value.run.status = 'failed'; $value.run.status_reason = 'helper_survived_shutdown'
                $value.run.error_code = 'helper_survived_shutdown'
            } }
    )
    foreach ($vector in $vectors) {
        $candidate = Copy-M0PilotTestValue $base
        & $vector.apply $candidate
        $built = New-M0SyntheticPilotSummary -Input $candidate
        Assert-Equal -Actual $built.document.semantic.check.status -Expected $vector.expected_check $vector.case_id
        Assert-Equal -Actual $built.document.semantic.run.status -Expected $vector.expected_run $vector.case_id
        if ($vector.case_id -ceq 'multicausal-primary-preserved') {
            Assert-Equal -Actual $built.document.semantic.cleanup.failure_code -Expected 'snapshot_validation_failed'
            Assert-Equal -Actual ($built.document.semantic.cleanup.failure_codes -join ',') `
                -Expected 'snapshot_validation_failed,helper_survived_shutdown'
        }

        $tampered = Copy-M0PilotTestValue $candidate
        $tampered.check.reason_code = 'identity_revalidation_failed'
        Assert-Throws { New-M0SyntheticPilotSummary -Input $tampered } 'M0I|outcome|projection|reason'
    }
}

Test-Case 'The physical publisher rejects a self-consistently property-reordered Process artifact' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'artifact-order'
        return
    }
    $root = Join-Path $TestTempRoot 'pilot-physical-reordered-artifact'
    New-Item -ItemType Directory -Path $root | Out-Null
    $experimentId = 'exp-pilot-physical-reordered-artifact'
    $runId = 'run-P-physical-reordered-artifact-01'
    $artifact = New-M0PilotPhysicalArtifactFixture -ExperimentId $experimentId -RunId $runId
    $reordered = ConvertTo-M0PilotDeepReorderedValue -Value $artifact.document
    $reorderedJson = (($reordered | ConvertTo-Json -Compress -Depth 100) + "`n")
    Assert-True (Test-Json -Json $reorderedJson -SchemaFile $pilotProcessArtifactSchemaPath -ErrorAction SilentlyContinue) `
        'Reordered artifact Red must remain schema-valid.'
    Assert-True ($reorderedJson -cne $artifact.canonical_json) `
        'Reordered artifact Red did not alter outer and nested property order.'

    $request = [pscustomobject][ordered]@{
        repository_root = $RepositoryRoot
        protocol_root = $pilotProtocolRoot
        expected_protocol_sha256 = $script:M0PilotExpectedProtocolSha256
        evidence_root = $root
        experiment_id = $experimentId
        run_id = $runId
        process_artifact = $reordered
        clocks = [pscustomobject][ordered]@{
            qpc_frequency = 10000000
            started_utc = '2026-08-25T12:10:00.0000000Z'; started_qpc_ticks = 1000000
            artifact_utc = '2026-08-25T12:10:01.0000000Z'; artifact_qpc_ticks = 3000000
            check_utc = '2026-08-25T12:10:02.0000000Z'; check_qpc_ticks = 4000000
            sealed_utc = '2026-08-25T12:10:03.0000000Z'; sealed_qpc_ticks = 5000000
        }
        crash_after_boundary = ''
    }
    Assert-Throws {
        & $pilotModule { param($value) Publish-M0SyntheticPilotFixture -Request $value } $request
    } 'M0I025|canonical|property.order|Process artifact'
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Force).Count -Expected 0 `
        'Noncanonical Process artifact mutated the Evidence root before rejection.'
}

Test-Case 'A deterministic no-Job fixture publishes and validates the complete physical Pilot receipt once' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'physical-fixture'
        return
    }
    $root = Join-Path $TestTempRoot 'pilot-physical-fixture'
    New-Item -ItemType Directory -Path $root | Out-Null
    $experimentId = 'exp-pilot-physical-fixture'
    $runId = 'run-P-physical-fixture-01'
    $artifact = New-M0PilotPhysicalArtifactFixture -ExperimentId $experimentId -RunId $runId
    $request = [pscustomobject][ordered]@{
        repository_root = $RepositoryRoot
        protocol_root = $pilotProtocolRoot
        expected_protocol_sha256 = $script:M0PilotExpectedProtocolSha256
        evidence_root = $root
        experiment_id = $experimentId
        run_id = $runId
        process_artifact = $artifact
        clocks = [pscustomobject][ordered]@{
            qpc_frequency = 10000000
            started_utc = '2026-08-25T12:00:00.0000000Z'; started_qpc_ticks = 1000000
            artifact_utc = '2026-08-25T12:00:01.0000000Z'; artifact_qpc_ticks = 3000000
            check_utc = '2026-08-25T12:00:02.0000000Z'; check_qpc_ticks = 4000000
            sealed_utc = '2026-08-25T12:00:03.0000000Z'; sealed_qpc_ticks = 5000000
        }
        crash_after_boundary = ''
    }
    $beforeHelpers = @(Get-M0SyntheticHelperProcesses)
    Assert-Equal -Actual $beforeHelpers.Count -Expected 0
    $first = & $pilotModule { param($value) Publish-M0SyntheticPilotFixture -Request $value } $request
    Assert-Equal -Actual $first.publication_state -Expected 'committed'
    Assert-Equal -Actual $first.evidence_role -Expected 'synthetic_pilot'
    Assert-Equal -Actual $first.decision_eligible -Expected $false
    Assert-Equal -Actual $first.live_launch_performed -Expected $false
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 `
        'The deterministic physical fixture launched a Process helper.'

    $expectedFiles = @(
        '.m0-evidence.lock',
        "artifacts/$runId.process-tree.json",
        "$runId.intent.json",
        "$runId.jsonl",
        "$runId.pilot-intent.json",
        "$runId.pilot-summary.json",
        "$runId.pilot-summary.sha256",
        "$runId.run.json",
        "$runId.snapshot.json"
    ) | Sort-Object
    $before = @(Get-M0PilotFileInventory -Root $root)
    Assert-SequenceEqual -Actual @($before.relative_path) -Expected $expectedFiles
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.snapshot-set*').Count -Expected 0
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.batch*').Count -Expected 0

    $asserted = Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
        -ExperimentId $experimentId -RunId $runId -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    Assert-Equal -Actual $asserted.summary_sha256 -Expected $first.summary_sha256
    Assert-Equal -Actual $asserted.process_tree_sha256 -Expected $artifact.sha256

    $second = & $pilotModule { param($value) Publish-M0SyntheticPilotFixture -Request $value } $request
    Assert-Equal -Actual $second.live_launch_performed -Expected $false
    $after = @(Get-M0PilotFileInventory -Root $root)
    Assert-Equal -Actual ($after | ConvertTo-Json -Compress -Depth 20) `
        -Expected ($before | ConvertTo-Json -Compress -Depth 20) 'Exact fixture replay changed physical bytes.'
    $script:M0PilotPhysicalFixtureRoot = $root
    $script:M0PilotPhysicalFixtureRunId = $runId
    $script:M0PilotPhysicalFixtureExperimentId = $experimentId
}

Test-Case 'Physical validation rederives summary semantics from the artifact and intent clocks from the raw first line' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'physical-bindings'
        return
    }
    $sourceRoot = $script:M0PilotPhysicalFixtureRoot
    $runId = $script:M0PilotPhysicalFixtureRunId
    $experimentId = $script:M0PilotPhysicalFixtureExperimentId
    Assert-True (-not [string]::IsNullOrWhiteSpace($sourceRoot)) 'Physical fixture source was not retained.'

    $semanticRoot = Join-Path $TestTempRoot 'pilot-physical-semantic-reanchor'
    Copy-Item -LiteralPath $sourceRoot -Destination $semanticRoot -Recurse
    $semanticSummaryPath = Join-Path $semanticRoot "$runId.pilot-summary.json"
    $semanticSummary = Get-Content -Raw -LiteralPath $semanticSummaryPath | ConvertFrom-Json -Depth 100 -DateKind String
    $semanticSummary.semantic.process.state = 'complete'
    $semanticSummary.semantic.process.error_code = $null
    $semanticSummary.semantic.process.missing_capabilities = @()
    $semanticSummary.semantic.check.status = 'passed'
    $semanticSummary.semantic.check.reason_code = $null
    $semanticSummary.semantic.check.status_reason = $null
    $semanticSummary.semantic.check.error_code = $null
    $semanticSummary.semantic.run.status = 'valid'
    $semanticSummary.semantic.run.status_reason = $null
    $semanticSummary.semantic.run.error_code = $null
    $semanticBuilt = Assert-M0SyntheticPilotSummary -Summary $semanticSummary
    Assert-Equal -Actual $semanticBuilt.document.semantic.process.state -Expected 'complete' `
        'Semantic tamper negative did not remain internally schema/canonical valid.'
    [IO.File]::WriteAllText($semanticSummaryPath, $semanticBuilt.canonical_json, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $semanticRoot "$runId.pilot-summary.sha256"),
        "$($semanticBuilt.sha256)  $runId.pilot-summary.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $semanticRoot `
            -ExperimentId $experimentId -RunId $runId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I028|artifact-derived|summary|semantic'

    $intentRoot = Join-Path $TestTempRoot 'pilot-physical-intent-raw-reanchor'
    Copy-Item -LiteralPath $sourceRoot -Destination $intentRoot -Recurse
    $intentPath = Join-Path $intentRoot "$runId.pilot-intent.json"
    $intent = Get-Content -Raw -LiteralPath $intentPath | ConvertFrom-Json -Depth 100 -DateKind String
    $intent.run_started.event.utc = '2026-08-25T12:00:00.1000000Z'
    $intent.run_started.event.qpc_ticks = 1100000
    $newAnchor = & $pilotModule { param($event) New-M0SyntheticPilotRunStartedAnchor -Event $event } `
        $intent.run_started.event
    $intent.run_started.canonical_line_sha256 = $newAnchor.canonical_line_sha256
    $intent.run_started.size_bytes = $newAnchor.size_bytes
    $intentBuilt = & $pilotModule { param($value) New-M0SyntheticPilotIntent -Intent $value } $intent
    [IO.File]::WriteAllText($intentPath, $intentBuilt.canonical_json, [Text.UTF8Encoding]::new($false))

    $summaryPath = Join-Path $intentRoot "$runId.pilot-summary.json"
    $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json -Depth 100 -DateKind String
    $summary.receipt.pilot_intent.sha256 = $intentBuilt.sha256
    $summary.receipt.pilot_intent.size_bytes = $intentBuilt.size_bytes
    $summary.receipt.raws[0].first_line_sha256 = $newAnchor.canonical_line_sha256
    $summary.receipt.raws[0].first_line_size_bytes = $newAnchor.size_bytes
    $summary.receipt.started_utc = $newAnchor.event.utc
    $summary.receipt.started_qpc_ticks = $newAnchor.event.qpc_ticks
    $summaryBuilt = Assert-M0SyntheticPilotSummary -Summary $summary
    $null = & $pilotModule {
        param($launchIntent, $summaryInput)
        Assert-M0SyntheticPilotFixtureBindings -PilotIntent $launchIntent -SummaryInput $summaryInput
    } $intentBuilt.document ([pscustomobject][ordered]@{
        '$schema' = 'schemas/pilot-summary-input.schema.json'
        schema_id = 'm0.synthetic-pilot-summary-input.v1'; schema_version = 1
        candidate_code = $summaryBuilt.document.semantic.candidate_code
        scenario_id = $summaryBuilt.document.semantic.scenario_id; attempt = $summaryBuilt.document.semantic.attempt
        evidence_role = $summaryBuilt.document.semantic.evidence_role
        decision_eligible = $summaryBuilt.document.semantic.decision_eligible
        publication_state = $summaryBuilt.document.semantic.publication_state
        run = $summaryBuilt.document.semantic.run; process = $summaryBuilt.document.semantic.process
        cleanup = $summaryBuilt.document.semantic.cleanup; check = $summaryBuilt.document.semantic.check
        artifact_count = $summaryBuilt.document.semantic.artifact_count; receipt = $summaryBuilt.document.receipt
    })
    [IO.File]::WriteAllText($summaryPath, $summaryBuilt.canonical_json, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $intentRoot "$runId.pilot-summary.sha256"),
        "$($summaryBuilt.sha256)  $runId.pilot-summary.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $intentRoot `
            -ExperimentId $experimentId -RunId $runId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I027|raw first line|launch intent|byte'

    $hardlinkRoot = Join-Path $TestTempRoot 'pilot-physical-fixture-hardlink'
    Copy-Item -LiteralPath $sourceRoot -Destination $hardlinkRoot -Recurse
    $artifactPath = Join-Path $hardlinkRoot "artifacts\$runId.process-tree.json"
    $hardlinkSource = Join-Path $TestTempRoot 'pilot-physical-fixture-hardlink-source.json'
    Copy-Item -LiteralPath $artifactPath -Destination $hardlinkSource
    Remove-Item -LiteralPath $artifactPath
    New-Item -ItemType HardLink -Path $artifactPath -Target $hardlinkSource | Out-Null
    Assert-Throws {
        Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $hardlinkRoot `
            -ExperimentId $experimentId -RunId $runId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|hardlink|single-link|physical'

    Assert-Throws {
        Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $sourceRoot `
            -ExperimentId $experimentId -RunId $runId -ExpectedProtocolSha256 ('0' * 64)
    } 'M0I021|protocol|expected|hash'
}

Test-Case 'Live preflight rejects nonordinal IDs boundaries and abrupt combinations before mutation' {
    $cases = @(
        [pscustomobject]@{ id = 'experiment-space'; experiment = ' bad'; run = 'run-P-preflight-01'; boundary = ''; abrupt = $false },
        [pscustomobject]@{ id = 'experiment-path'; experiment = 'exp/bad'; run = 'run-P-preflight-02'; boundary = ''; abrupt = $false },
        [pscustomobject]@{ id = 'run-candidate'; experiment = 'exp-pilot-preflight'; run = 'run-A-preflight-03'; boundary = ''; abrupt = $false },
        [pscustomobject]@{ id = 'run-empty-suffix'; experiment = 'exp-pilot-preflight'; run = 'run-P-'; boundary = ''; abrupt = $false },
        [pscustomobject]@{ id = 'boundary-case'; experiment = 'exp-pilot-preflight'; run = 'run-P-preflight-04'; boundary = 'Intent_Published'; abrupt = $false },
        [pscustomobject]@{ id = 'boundary-foreign'; experiment = 'exp-pilot-preflight'; run = 'run-P-preflight-05'; boundary = 'foreign'; abrupt = $false },
        [pscustomobject]@{ id = 'abrupt-without-boundary'; experiment = 'exp-pilot-preflight'; run = 'run-P-preflight-06'; boundary = ''; abrupt = $true },
        [pscustomobject]@{ id = 'abrupt-after-commit'; experiment = 'exp-pilot-preflight'; run = 'run-P-preflight-07'; boundary = 'committed'; abrupt = $true }
    )
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot "pilot-preflight-$($case.id)"
        Assert-True (-not (Test-Path -LiteralPath $root)) $case.id
        Assert-Throws {
            Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
                -ExperimentId $case.experiment -RunId $case.run `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
                -CrashAfterBoundary $case.boundary -AbruptCrash:$case.abrupt
        } 'M0I041|ordinal|preflight|boundary|combination'
        Assert-True (-not (Test-Path -LiteralPath $root)) "Preflight '$($case.id)' mutated EvidenceRoot."
        if ($IsWindows) {
            Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 $case.id
        }
    }
}

Test-Case 'A live synthetic pilot commits once remains nondecisive and preserves an unrelated process' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'live-publication'
        return
    }

    $root = Join-Path $TestTempRoot 'pilot-live-publication'
    New-Item -ItemType Directory -Path $root | Out-Null
    $trustedBeforeInvoke = Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot `
        -Scenario 'synthetic-process-tree'
    $expectedProtocolSha256 = [string]$trustedBeforeInvoke.protocol_sha256
    $sentinelStart = [Diagnostics.ProcessStartInfo]::new()
    $sentinelStart.FileName = (Get-Process -Id $PID).Path
    $sentinelStart.UseShellExecute = $false
    $sentinelStart.CreateNoWindow = $true
    $null = $sentinelStart.ArgumentList.Add('-NoLogo')
    $null = $sentinelStart.ArgumentList.Add('-NoProfile')
    $null = $sentinelStart.ArgumentList.Add('-NonInteractive')
    $null = $sentinelStart.ArgumentList.Add('-Command')
    $null = $sentinelStart.ArgumentList.Add('[Threading.Thread]::Sleep(300000)')
    $sentinel = [Diagnostics.Process]::Start($sentinelStart)
    try {
        $liveLedger = [Collections.Generic.List[string]]::new()
        & $pilotModule {
            param($events)
            $script:M0PilotLiveTestHook = [pscustomobject]@{ events = $events }
        } $liveLedger
        $first = Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
            -ExperimentId 'exp-pilot-live-01' -RunId 'run-P-live-01' `
            -ExpectedProtocolSha256 $expectedProtocolSha256
        Assert-Equal -Actual $first.publication_state -Expected 'committed'
        Assert-Equal -Actual $first.evidence_role -Expected 'synthetic_pilot'
        Assert-Equal -Actual $first.decision_eligible -Expected $false
        Assert-Equal -Actual $first.run_status -Expected 'inconclusive'
        Assert-Equal -Actual $first.run_status_reason -Expected 'incomplete_evidence'
        Assert-Equal -Actual $first.run_error_code -Expected $null
        Assert-Equal -Actual $first.common_inputs.protocol_sha256 -Expected $expectedProtocolSha256 `
            'Invoke returned a protocol hash different from the independently trusted pre-invocation anchor.'
        Assert-Equal -Actual $first.cleanup_complete -Expected $true
        Assert-Equal -Actual $first.survivor_count -Expected 0
        Assert-True ([bool]$first.live_launch_performed) 'Fresh pilot did not report its one authorized live launch.'
        Assert-True (-not $sentinel.HasExited) 'Synthetic pilot terminated an unrelated sentinel process.'
        Assert-SequenceEqual -Actual @($liveLedger) -Expected @(
            'trust_opened', 'preflight_validated', 'writer_opened', 'pilot_intent_published',
            'raw_started_flushed', 'process_opened', 'snapshot_captured', 'process_closed',
            'artifact_published', 'evidence_committed', 'summary_pending', 'summary_published',
            'sidecar_published'
        ) 'Live causal ledger differs from the authorized writer-before-Process and Close-before-artifact order.'

        $rawEvents = @((Get-Content -LiteralPath (Join-Path $root 'run-P-live-01.jsonl')) | ForEach-Object {
            $_ | ConvertFrom-Json -Depth 100 -DateKind String
        })
        Assert-Equal -Actual $rawEvents.Count -Expected 4
        Assert-SequenceEqual -Actual @($rawEvents.event) -Expected @(
            'run_started', 'artifact_recorded', 'check_recorded', 'run_sealed'
        )
        Assert-SequenceEqual -Actual @($rawEvents.event_seq) -Expected @(1, 2, 3, 4)
        Assert-Equal -Actual $rawEvents[1].payload.artifact_id -Expected 'process-tree'
        Assert-Equal -Actual $rawEvents[2].payload.check_id -Expected 'CHK-PROCESS'

        $expectedFiles = @(
            '.m0-evidence.lock',
            'artifacts/run-P-live-01.process-tree.json',
            'run-P-live-01.intent.json',
            'run-P-live-01.jsonl',
            'run-P-live-01.pilot-intent.json',
            'run-P-live-01.pilot-summary.json',
            'run-P-live-01.pilot-summary.sha256',
            'run-P-live-01.run.json',
            'run-P-live-01.snapshot.json'
        ) | Sort-Object
        $before = @(Get-M0PilotFileInventory -Root $root)
        Assert-SequenceEqual -Actual @($before.relative_path) -Expected $expectedFiles
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.snapshot-set*').Count -Expected 0
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.batch*').Count -Expected 0

        $asserted = Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
            -ExperimentId 'exp-pilot-live-01' -RunId 'run-P-live-01' `
            -ExpectedProtocolSha256 $expectedProtocolSha256
        Assert-Equal -Actual $asserted.summary_sha256 -Expected $first.summary_sha256
        Assert-Equal -Actual $asserted.process_tree_sha256 -Expected $rawEvents[1].payload.sha256

        $second = Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
            -ExperimentId 'exp-pilot-live-01' -RunId 'run-P-live-01' `
            -ExpectedProtocolSha256 $expectedProtocolSha256
        Assert-Equal -Actual $second.publication_state -Expected 'committed'
        Assert-Equal -Actual $second.live_launch_performed -Expected $false
        $after = @(Get-M0PilotFileInventory -Root $root)
        Assert-Equal -Actual ($after | ConvertTo-Json -Compress -Depth 20) `
            -Expected ($before | ConvertTo-Json -Compress -Depth 20) 'Committed replay changed publication bytes.'
        Assert-True (-not $sentinel.HasExited) 'Committed replay terminated an unrelated sentinel process.'
        Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 'Synthetic helper survived committed publication.'

        $script:M0PilotLiveRoot = $root
        $script:M0PilotLiveProtocolSha256 = $expectedProtocolSha256
    }
    finally {
        if (-not $sentinel.HasExited) {
            $sentinel.Kill()
            $sentinel.WaitForExit()
        }
        $sentinel.Dispose()
    }
}

Test-Case 'A live snapshot failure closes Process before publishing exactly one unavailable fallback artifact' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'snapshot-fallback'
        return
    }
    $root = Join-Path $TestTempRoot 'pilot-live-snapshot-fallback'
    New-Item -ItemType Directory -Path $root | Out-Null
    $runId = 'run-P-live-snapshot-fallback-01'
    $experimentId = 'exp-pilot-live-snapshot-fallback'
    $ledger = [Collections.Generic.List[string]]::new()
    & $pilotModule {
        param($events)
        $script:M0PilotLiveTestHook = [pscustomobject]@{
            events = $events
            fail_snapshot = $true
        }
    } $ledger
    $result = Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
        -ExperimentId $experimentId -RunId $runId `
        -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    Assert-SequenceEqual -Actual @($ledger) -Expected @(
        'trust_opened', 'preflight_validated', 'writer_opened', 'pilot_intent_published',
        'raw_started_flushed', 'process_opened', 'snapshot_failed', 'process_closed',
        'fallback_artifact_built', 'artifact_published', 'evidence_committed',
        'summary_pending', 'summary_published', 'sidecar_published'
    ) 'Fallback causal ledger did not prove snapshot failure, real Close, then artifact publication.'
    Assert-Equal -Actual $result.publication_state -Expected 'committed'
    Assert-Equal -Actual $result.run_status -Expected 'inconclusive'
    Assert-Equal -Actual $result.run_status_reason -Expected 'incomplete_evidence'
    Assert-Equal -Actual $result.cleanup_complete -Expected $true
    Assert-Equal -Actual $result.survivor_count -Expected 0
    Assert-True ([bool]$result.live_launch_performed)
    $artifacts = @(Get-ChildItem -LiteralPath (Join-Path $root 'artifacts') -File -Force)
    Assert-Equal -Actual $artifacts.Count -Expected 1 'Snapshot fallback published more than one Process artifact.'
    Assert-Equal -Actual $artifacts[0].Name -Expected "$runId.process-tree.json"
    $artifact = Get-Content -Raw -LiteralPath $artifacts[0].FullName | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Equal -Actual $artifact.snapshot_origin -Expected 'pre_snapshot_fallback'
    Assert-Equal -Actual $artifact.snapshot.state -Expected 'unavailable'
    Assert-Equal -Actual $artifact.snapshot.error_code -Expected 'safe_identity_unavailable'
    Assert-Equal -Actual @($artifact.snapshot.nodes).Count -Expected 0
    Assert-Equal -Actual $artifact.cleanup.attempted -Expected $true
    Assert-Equal -Actual $artifact.cleanup.completed -Expected $true
    Assert-Equal -Actual $artifact.cleanup.survivor_count -Expected 0
    Assert-Equal -Actual @($artifact.cleanup.failure_codes).Count -Expected 0
    $raw = @((Get-Content -LiteralPath (Join-Path $root "$runId.jsonl")) | ForEach-Object {
        $_ | ConvertFrom-Json -Depth 100 -DateKind String
    })
    Assert-Equal -Actual $raw[2].payload.status -Expected 'skipped'
    Assert-Equal -Actual $raw[2].payload.reason_code -Expected 'process_tree_unavailable'
    Assert-Equal -Actual $raw[3].payload.status -Expected 'inconclusive'
    $asserted = Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
        -ExperimentId $experimentId -RunId $runId `
        -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    Assert-Equal -Actual $asserted.process_tree_sha256 -Expected $result.process_tree_sha256
    Assert-Equal -Actual $asserted.summary_sha256 -Expected $result.summary_sha256
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0
}

Test-Case 'Physical pilot validation rejects byte path binding link and inventory substitution' {
    Assert-True (-not [string]::IsNullOrWhiteSpace($script:M0PilotLiveRoot)) 'Live publication fixture was not retained.'
    $sourceRoot = $script:M0PilotLiveRoot
    $runId = 'run-P-live-01'

    $cases = @(
        [pscustomobject]@{ case_id = 'artifact-byte-append'; mutate = {
                param($root)
                [IO.File]::AppendAllText((Join-Path $root "artifacts\$runId.process-tree.json"), ' ', [Text.UTF8Encoding]::new($false))
            } },
        [pscustomobject]@{ case_id = 'raw-truncate'; mutate = {
                param($root)
                $path = Join-Path $root "$runId.jsonl"
                $bytes = [IO.File]::ReadAllBytes($path)
                [IO.File]::WriteAllBytes($path, $bytes[0..($bytes.Length - 2)])
            } },
        [pscustomobject]@{ case_id = 'run-snapshot-swap'; mutate = {
                param($root)
                [IO.File]::WriteAllBytes((Join-Path $root "$runId.run.json"), [IO.File]::ReadAllBytes((Join-Path $root "$runId.snapshot.json")))
            } },
        [pscustomobject]@{ case_id = 'summary-artifact-hash-reanchor'; mutate = {
                param($root)
                $summaryPath = Join-Path $root "$runId.pilot-summary.json"
                $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json -Depth 100 -DateKind String
                $summary.receipt.artifacts[0].sha256 = '0' * 64
                $text = (($summary | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")) + "`n"
                [IO.File]::WriteAllText($summaryPath, $text, [Text.UTF8Encoding]::new($false))
                $hash = (Get-FileHash -LiteralPath $summaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
                [IO.File]::WriteAllText((Join-Path $root "$runId.pilot-summary.sha256"), "$hash  $runId.pilot-summary.json`n", [Text.UTF8Encoding]::new($false))
            } },
        [pscustomobject]@{ case_id = 'summary-traversal-reanchor'; mutate = {
                param($root)
                $summaryPath = Join-Path $root "$runId.pilot-summary.json"
                $summary = Get-Content -Raw -LiteralPath $summaryPath | ConvertFrom-Json -Depth 100 -DateKind String
                $summary.receipt.raws[0].relative_path = '../outside.jsonl'
                $text = (($summary | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")) + "`n"
                [IO.File]::WriteAllText($summaryPath, $text, [Text.UTF8Encoding]::new($false))
                $hash = (Get-FileHash -LiteralPath $summaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
                [IO.File]::WriteAllText((Join-Path $root "$runId.pilot-summary.sha256"), "$hash  $runId.pilot-summary.json`n", [Text.UTF8Encoding]::new($false))
            } },
        [pscustomobject]@{ case_id = 'extra-file'; mutate = {
                param($root) [IO.File]::WriteAllText((Join-Path $root 'foreign.json'), '{}', [Text.UTF8Encoding]::new($false))
            } }
    )
    foreach ($case in $cases) {
        $root = Join-Path $TestTempRoot "pilot-physical-$($case.case_id)"
        Copy-Item -LiteralPath $sourceRoot -Destination $root -Recurse
        & $case.mutate $root
        Assert-Throws {
            Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
                -ExperimentId 'exp-pilot-live-01' -RunId $runId `
                -ExpectedProtocolSha256 $script:M0PilotLiveProtocolSha256
        } 'M0I|M0E|hash|size|path|inventory|canonical|physical|schema'
    }

    $hardlinkRoot = Join-Path $TestTempRoot 'pilot-physical-hardlink'
    Copy-Item -LiteralPath $sourceRoot -Destination $hardlinkRoot -Recurse
    $artifactPath = Join-Path $hardlinkRoot "artifacts\$runId.process-tree.json"
    $hardlinkSource = Join-Path $TestTempRoot 'pilot-hardlink-source.json'
    Copy-Item -LiteralPath $artifactPath -Destination $hardlinkSource
    Remove-Item -LiteralPath $artifactPath
    New-Item -ItemType HardLink -Path $artifactPath -Target $hardlinkSource | Out-Null
    Assert-Throws {
        Assert-M0SyntheticPilotPublication -RepositoryRoot $RepositoryRoot -EvidenceRoot $hardlinkRoot `
            -ExperimentId 'exp-pilot-live-01' -RunId $runId `
            -ExpectedProtocolSha256 $script:M0PilotLiveProtocolSha256
    } 'M0I|hardlink|links|physical|Trusted helper|single-link'
}

Test-Case 'Pilot recovery never relaunches or overwrites after a launch intent exists' {
    $preLiveRoot = Join-Path $TestTempRoot 'pilot-recovery-intent-only'
    New-Item -ItemType Directory -Path $preLiveRoot | Out-Null
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $preLiveRoot `
            -ExperimentId 'exp-pilot-recovery-intent' -RunId 'run-P-recovery-intent-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary pilot_intent_published
    } 'M0I|crash|pilot_intent_published'
    $launchIntentPath = Join-Path $preLiveRoot 'run-P-recovery-intent-01.pilot-intent.json'
    $intentBytes = [IO.File]::ReadAllBytes($launchIntentPath)
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $preLiveRoot `
            -ExperimentId 'exp-pilot-recovery-intent' -RunId 'run-P-recovery-intent-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|incomplete|new.*root|relaunch|artifact'
    Assert-Equal -Actual ([Convert]::ToHexString([IO.File]::ReadAllBytes($launchIntentPath))) `
        -Expected ([Convert]::ToHexString($intentBytes)) 'Intent-only retry changed launch intent bytes.'
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0

    $rawStartedRoot = Join-Path $TestTempRoot 'pilot-recovery-raw-started'
    $rawStartedRunId = 'run-P-recovery-raw-started-01'
    New-Item -ItemType Directory -Path $rawStartedRoot | Out-Null
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $rawStartedRoot `
            -ExperimentId 'exp-pilot-recovery-raw-started' -RunId $rawStartedRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary raw_started
    } 'M0I|crash|raw_started'
    $rawStartedIntent = Join-Path $rawStartedRoot "$rawStartedRunId.pilot-intent.json"
    $rawStartedRaw = Join-Path $rawStartedRoot "$rawStartedRunId.jsonl"
    Assert-True (Test-Path -LiteralPath $rawStartedIntent -PathType Leaf)
    Assert-True (Test-Path -LiteralPath $rawStartedRaw -PathType Leaf)
    Assert-Equal -Actual @((Get-Content -LiteralPath $rawStartedRaw)).Count -Expected 1 `
        'raw_started boundary did not retain exactly the flushed pre-live event.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $rawStartedRoot 'artifacts') -PathType Container))
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $rawStartedRoot "$rawStartedRunId.intent.json") -PathType Leaf))
    $rawStartedBefore = @(Get-M0PilotFileInventory -Root $rawStartedRoot)
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $rawStartedRoot `
            -ExperimentId 'exp-pilot-recovery-raw-started' -RunId $rawStartedRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|incomplete|new.*root|new.*run|no.relaunch|artifact'
    Assert-Equal -Actual (@(Get-M0PilotFileInventory -Root $rawStartedRoot) | ConvertTo-Json -Compress -Depth 20) `
        -Expected ($rawStartedBefore | ConvertTo-Json -Compress -Depth 20) `
        'raw_started retry changed the immutable intent/raw prefix.'
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0

    $partialRawRoot = Join-Path $TestTempRoot 'pilot-recovery-pre-intent-raw'
    New-Item -ItemType Directory -Path $partialRawRoot | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $partialRawRoot '.m0-evidence.lock'), [byte[]]::new(0))
    $partialRawPath = Join-Path $partialRawRoot 'run-P-partial-raw-01.jsonl'
    [IO.File]::WriteAllText($partialRawPath, "{`"partial`":true}`n", [Text.UTF8Encoding]::new($false))
    $partialBytes = [IO.File]::ReadAllBytes($partialRawPath)
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $partialRawRoot `
            -ExperimentId 'exp-pilot-partial-raw' -RunId 'run-P-partial-raw-01' `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|nonempty|partial|intent|fail.closed'
    Assert-Equal -Actual ([Convert]::ToHexString([IO.File]::ReadAllBytes($partialRawPath))) `
        -Expected ([Convert]::ToHexString($partialBytes)) 'Pre-intent partial raw was overwritten or truncated.'

    $artifactBoundaryRoot = Join-Path $TestTempRoot 'pilot-recovery-artifact-before-evidence-intent'
    $artifactBoundaryRunId = 'run-P-recovery-artifact-before-intent-01'
    New-Item -ItemType Directory -Path $artifactBoundaryRoot | Out-Null
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $artifactBoundaryRoot `
            -ExperimentId 'exp-pilot-recovery-artifact-before-intent' -RunId $artifactBoundaryRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary artifact_published
    } 'M0I|crash|artifact_published'
    $artifactBoundaryPath = Join-Path $artifactBoundaryRoot "artifacts\$artifactBoundaryRunId.process-tree.json"
    Assert-True (Test-Path -LiteralPath $artifactBoundaryPath -PathType Leaf)
    $artifactBoundaryHash = (Get-FileHash -LiteralPath $artifactBoundaryPath -Algorithm SHA256).Hash
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $artifactBoundaryRoot `
            -ExperimentId 'exp-pilot-recovery-artifact-before-intent' -RunId $artifactBoundaryRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|incomplete|new.*root|new.*run|no.relaunch|evidence.intent'
    Assert-Equal -Actual (Get-FileHash -LiteralPath $artifactBoundaryPath -Algorithm SHA256).Hash `
        -Expected $artifactBoundaryHash 'Pre-Evidence-intent retry changed the immutable artifact.'

    foreach ($boundary in @(
        'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published',
        'snapshot_temp_flushed', 'committed', 'summary_pending', 'summary_published'
    )) {
        $runId = "run-P-recovery-$($boundary.Replace('_', '-'))-01"
        $root = Join-Path $TestTempRoot "pilot-recovery-$boundary"
        New-Item -ItemType Directory -Path $root | Out-Null
        Assert-Throws {
            Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
                -ExperimentId "exp-pilot-recovery-$boundary" -RunId $runId `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 -CrashAfterBoundary $boundary
        } "M0I|crash|$boundary"
        $artifactPath = Join-Path $root "artifacts\$runId.process-tree.json"
        Assert-True (Test-Path -LiteralPath $artifactPath -PathType Leaf) "Boundary '$boundary' lost the post-Close artifact."
        $artifactHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        $recovered = Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
            -ExperimentId "exp-pilot-recovery-$boundary" -RunId $runId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        Assert-Equal -Actual $recovered.publication_state -Expected 'committed' $boundary
        Assert-Equal -Actual $recovered.live_launch_performed -Expected $false $boundary
        Assert-Equal -Actual (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash -Expected $artifactHash `
            "Boundary '$boundary' overwrote its immutable process-tree artifact."
        Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 "Boundary '$boundary' leaked a helper."
    }

    $prevalidationRoot = Join-Path $TestTempRoot 'pilot-recovery-prevalidate-before-resume'
    $prevalidationRunId = 'run-P-recovery-prevalidate-01'
    New-Item -ItemType Directory -Path $prevalidationRoot | Out-Null
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $prevalidationRoot `
            -ExperimentId 'exp-pilot-recovery-prevalidate' -RunId $prevalidationRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary run_published
    } 'M0I|M0E|crash|run_published'
    $missingArtifactRoot = Join-Path $TestTempRoot 'pilot-recovery-prevalidate-missing-artifact'
    Copy-Item -LiteralPath $prevalidationRoot -Destination $missingArtifactRoot -Recurse
    $missingArtifactPath = Join-Path $missingArtifactRoot "artifacts\$prevalidationRunId.process-tree.json"
    Remove-Item -LiteralPath $missingArtifactPath
    $beforeMissingArtifact = @(Get-M0PilotFileInventory -Root $missingArtifactRoot)
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $missingArtifactRoot `
            -ExperimentId 'exp-pilot-recovery-prevalidate' -RunId $prevalidationRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|missing|artifact|recovery|precondition'
    Assert-Equal -Actual (@(Get-M0PilotFileInventory -Root $missingArtifactRoot) | ConvertTo-Json -Compress -Depth 20) `
        -Expected ($beforeMissingArtifact | ConvertTo-Json -Compress -Depth 20) `
        'Pilot recovery mutated Evidence before rejecting a missing post-Close artifact.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $missingArtifactRoot "$prevalidationRunId.snapshot.json") -PathType Leaf)) `
        'Pilot recovery published a commit snapshot before validating the required post-Close artifact.'
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0

    foreach ($operation in @('overwrite', 'rename', 'hardlink', 'extra-leaf')) {
        $raceRoot = Join-Path $TestTempRoot "pilot-recovery-handoff-$operation"
        Copy-Item -LiteralPath $prevalidationRoot -Destination $raceRoot -Recurse
        $artifactPath = Join-Path $raceRoot "artifacts\$prevalidationRunId.process-tree.json"
        $intentPath = Join-Path $raceRoot "$prevalidationRunId.intent.json"
        $pilotIntentPath = Join-Path $raceRoot "$prevalidationRunId.pilot-intent.json"
        $hookLeaf = Join-Path $raceRoot "recovery-$operation-probe.json"
        $hook = [pscustomobject]@{
            operation = $operation
            target_path = if ($operation -ceq 'rename') { $intentPath } elseif ($operation -ceq 'hardlink') { $artifactPath } else { $pilotIntentPath }
            destination_path = $hookLeaf
        }
        $baseline = @(Get-M0PilotFileInventory -Root $raceRoot)
        & $pilotModule { param($value) $script:M0PilotRecoveryTestHook = $value } $hook
        try {
            Assert-Throws {
                Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $raceRoot `
                    -ExperimentId 'exp-pilot-recovery-prevalidate' -RunId $prevalidationRunId `
                    -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
            } 'M0I|recovery|retained|inventory|single-link|blocked|handoff'
            Assert-True (-not (Test-Path -LiteralPath (Join-Path $raceRoot "$prevalidationRunId.snapshot.json") -PathType Leaf)) `
                "Recovery handoff '$operation' advanced Evidence before rejecting the race."
            foreach ($file in $baseline) {
                $path = Join-Path $raceRoot $file.relative_path.Replace('/', [IO.Path]::DirectorySeparatorChar)
                Assert-True (Test-Path -LiteralPath $path -PathType Leaf) `
                    "Recovery handoff '$operation' moved baseline leaf '$($file.relative_path)'."
                Assert-Equal -Actual (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() `
                    -Expected $file.sha256 "Recovery handoff '$operation' changed '$($file.relative_path)'."
            }
        }
        finally {
            & $pilotModule { $script:M0PilotRecoveryTestHook = $null }
            if (Test-Path -LiteralPath $hookLeaf -PathType Leaf) { Remove-Item -LiteralPath $hookLeaf }
        }
    }

    $divergentRoot = Join-Path $TestTempRoot 'pilot-recovery-divergent-summary'
    $divergentRunId = 'run-P-recovery-divergent-summary-01'
    New-Item -ItemType Directory -Path $divergentRoot | Out-Null
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $divergentRoot `
            -ExperimentId 'exp-pilot-recovery-divergent-summary' -RunId $divergentRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256 `
            -CrashAfterBoundary committed
    } 'M0I|crash|committed'
    $divergentSummaryPath = Join-Path $divergentRoot "$divergentRunId.pilot-summary.json"
    [IO.File]::WriteAllText($divergentSummaryPath, "{}`n", [Text.UTF8Encoding]::new($false))
    $divergentSummaryBytes = [IO.File]::ReadAllBytes($divergentSummaryPath)
    Assert-Throws {
        Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $divergentRoot `
            -ExperimentId 'exp-pilot-recovery-divergent-summary' -RunId $divergentRunId `
            -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
    } 'M0I|summary|divergent|overwrite|conflict'
    Assert-Equal -Actual ([Convert]::ToHexString([IO.File]::ReadAllBytes($divergentSummaryPath))) `
        -Expected ([Convert]::ToHexString($divergentSummaryBytes)) 'Divergent summary was overwritten during retry.'
}

Test-Case 'The clean Pilot runner rejects every preloaded dependency and both native namespaces' {
    $preloadSource = @'
param($RunnerPath, $ModulePath, $EvidenceRoot)
$ErrorActionPreference = 'Stop'
Import-Module $ModulePath -ErrorAction Stop
& $RunnerPath -EvidenceRoot $EvidenceRoot -ExperimentId 'exp-pilot-preload' -RunId 'run-P-preload-01'
'@
    $moduleCases = @(
        [pscustomobject]@{ name = 'Harness'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1') },
        [pscustomobject]@{ name = 'Pilot'; path = $pilotModulePath },
        [pscustomobject]@{ name = 'Summary'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Summary.psm1') },
        [pscustomobject]@{ name = 'Statistics'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Statistics.psm1') },
        [pscustomobject]@{ name = 'Process'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1') },
        [pscustomobject]@{ name = 'Evidence'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1') },
        [pscustomobject]@{ name = 'Fixture'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Fixture.psm1') },
        [pscustomobject]@{ name = 'Markers'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Markers.psm1') },
        [pscustomobject]@{ name = 'Visual'; path = (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Visual.psm1') }
    )
    foreach ($case in $moduleCases) {
        $root = Join-Path $TestTempRoot "pilot-runner-preload-$($case.name)"
        New-Item -ItemType Directory -Path $root | Out-Null
        $child = Invoke-M0PilotTestChild -Name "pilot-runner-preload-$($case.name)" -Source $preloadSource `
            -Arguments @($pilotRunnerPath, $case.path, $root)
        Assert-True ($child.exit_code -ne 0) "Runner accepted preloaded exact module '$($case.name)'."
        Assert-True ($child.output -match 'clean PowerShell process|preloaded|M0I040') `
            "Runner rejected '$($case.name)' for the wrong reason: $($child.output)"
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Force).Count -Expected 0 `
            "Runner mutated EvidenceRoot after '$($case.name)' preload."
    }

    $nativeSource = @'
param($RunnerPath, $EvidenceRoot, $Namespace)
$ErrorActionPreference = 'Stop'
if ($Namespace -ceq 'process') {
    Add-Type 'namespace M0ProcessNativeV1 { public class PilotSession {} public class ProcessCapability {} public class TrustedFile {} }'
}
else {
    Add-Type 'namespace M0EvidenceNativeV1 { public class WindowsFileIdentity {} public class WindowsFileFingerprint {} }'
}
& $RunnerPath -EvidenceRoot $EvidenceRoot -ExperimentId 'exp-pilot-native' -RunId 'run-P-native-01'
'@
    foreach ($namespace in @('process', 'evidence')) {
        $root = Join-Path $TestTempRoot "pilot-runner-native-$namespace"
        New-Item -ItemType Directory -Path $root | Out-Null
        $child = Invoke-M0PilotTestChild -Name "pilot-runner-native-$namespace" -Source $nativeSource `
            -Arguments @($pilotRunnerPath, $root, $namespace)
        Assert-True ($child.exit_code -ne 0) "Runner accepted preloaded native namespace '$namespace'."
        Assert-True ($child.output -match 'native type|preloaded|M0I040') `
            "Runner rejected native namespace '$namespace' for the wrong reason: $($child.output)"
        Assert-Equal -Actual @(Get-ChildItem -LiteralPath $root -Force).Count -Expected 0
    }
}

Test-Case 'Importing Pilot and validating artifacts preserves an active Process capability and module instance' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'active-process'
        return
    }
    $source = @'
param($RepositoryRoot, $PilotModulePath)
$ErrorActionPreference = 'Stop'
$processPath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
Import-Module $processPath -ErrorAction Stop
$before = Get-Module M0.Process
$contractBefore = & $before { Assert-M0ProcessNativeContract }
$pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
try {
    Import-Module $PilotModulePath -ErrorAction Stop
    $after = Get-Module M0.Process
    $contractAfter = & $after { Assert-M0ProcessNativeContract }
    if (-not [object]::ReferenceEquals($before, $after) -or
        $contractBefore.module_instance_id -cne $contractAfter.module_instance_id) {
        throw 'M0.Process was reloaded while an active pilot capability existed.'
    }
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $protocolHash = (Get-FileHash -LiteralPath $protocolPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $null = Assert-M0PilotArtifacts -ProtocolRoot (Split-Path $protocolPath -Parent) -ExpectedProtocolSha256 $protocolHash
    $snapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
    if (@($snapshot.nodes).Count -ne 3) { throw 'Active process capability changed during Pilot import/trust validation.' }
}
finally {
    $closed = Close-M0SyntheticProcessPilot -Pilot $pilot
}
if (-not $closed.cleanup_complete -or $closed.survivor_count -ne 0) { throw 'Active capability cleanup failed.' }
'@
    $child = Invoke-M0PilotTestChild -Name 'pilot-active-process-import' -Source $source `
        -Arguments @($RepositoryRoot, $pilotModulePath)
    Assert-Equal -Actual $child.exit_code -Expected 0 "Active Process import-order probe failed: $($child.output)"
    Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0
}

Test-Case 'Abrupt live owner exit closes the Job before snapshot artifact or Evidence intent' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'abrupt-live'
        return
    }
    $root = Join-Path $TestTempRoot 'pilot-abrupt-live-opened-boundary'
    New-Item -ItemType Directory -Path $root | Out-Null
    $sentinelStart = [Diagnostics.ProcessStartInfo]::new()
    $sentinelStart.FileName = (Get-Process -Id $PID).Path
    $sentinelStart.UseShellExecute = $false
    $sentinelStart.CreateNoWindow = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-Command', '[Threading.Thread]::Sleep(300000)')) {
        $null = $sentinelStart.ArgumentList.Add($argument)
    }
    $sentinel = [Diagnostics.Process]::Start($sentinelStart)
    try {
        $source = @'
param($RunnerPath, $EvidenceRoot)
& $RunnerPath -EvidenceRoot $EvidenceRoot -ExperimentId 'exp-pilot-abrupt-live-opened' `
    -RunId 'run-P-abrupt-live-opened-01' -CrashAfterBoundary live_opened -AbruptCrash
'@
        $child = Invoke-M0PilotTestChild -Name 'pilot-abrupt-live-opened-boundary' -Source $source `
            -Arguments @($pilotRunnerPath, $root)
        Assert-True ($child.exit_code -ne 0) 'Abrupt live_opened injection unexpectedly exited successfully.'
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        while (@(Get-M0SyntheticHelperProcesses).Count -ne 0 -and [DateTime]::UtcNow -lt $deadline) {
            Start-Sleep -Milliseconds 100
        }
        Assert-True (-not $sentinel.HasExited) 'Abrupt live owner exit killed an unrelated sentinel.'
        Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 `
            'Owner exit did not close the scoped Job and its helper tree.'
        $runId = 'run-P-abrupt-live-opened-01'
        Assert-True (Test-Path -LiteralPath (Join-Path $root "$runId.pilot-intent.json") -PathType Leaf)
        Assert-Equal -Actual @((Get-Content -LiteralPath (Join-Path $root "$runId.jsonl"))).Count -Expected 1
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $root "artifacts\$runId.process-tree.json") -PathType Leaf))
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $root "$runId.intent.json") -PathType Leaf))
        $before = @(Get-M0PilotFileInventory -Root $root)
        Assert-Throws {
            Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
                -ExperimentId 'exp-pilot-abrupt-live-opened' -RunId $runId `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        } 'M0I|incomplete|new.*root|new.*run|no.relaunch|artifact'
        Assert-Equal -Actual (@(Get-M0PilotFileInventory -Root $root) | ConvertTo-Json -Compress -Depth 20) `
            -Expected ($before | ConvertTo-Json -Compress -Depth 20)
    }
    finally {
        if (-not $sentinel.HasExited) { $sentinel.Kill(); $sentinel.WaitForExit() }
        $sentinel.Dispose()
    }
}

Test-Case 'Abrupt post-artifact exit preserves the post-Close artifact and requires a new root and RunId' {
    if (-not $IsWindows) {
        Assert-M0PilotNonWindowsFailClosed -CaseId 'abrupt-artifact'
        return
    }
    $root = Join-Path $TestTempRoot 'pilot-abrupt-artifact-boundary'
    New-Item -ItemType Directory -Path $root | Out-Null
    $sentinelStart = [Diagnostics.ProcessStartInfo]::new()
    $sentinelStart.FileName = (Get-Process -Id $PID).Path
    $sentinelStart.UseShellExecute = $false
    $sentinelStart.CreateNoWindow = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-Command', '[Threading.Thread]::Sleep(300000)')) {
        $null = $sentinelStart.ArgumentList.Add($argument)
    }
    $sentinel = [Diagnostics.Process]::Start($sentinelStart)
    try {
        $source = @'
param($RunnerPath, $EvidenceRoot)
& $RunnerPath -EvidenceRoot $EvidenceRoot -ExperimentId 'exp-pilot-abrupt' -RunId 'run-P-abrupt-01' `
    -CrashAfterBoundary artifact_published -AbruptCrash
'@
        $child = Invoke-M0PilotTestChild -Name 'pilot-abrupt-artifact-boundary' -Source $source `
            -Arguments @($pilotRunnerPath, $root)
        Assert-True ($child.exit_code -ne 0) 'Abrupt crash injection unexpectedly exited successfully.'
        Assert-True (-not $sentinel.HasExited) 'Abrupt pilot exit killed an unrelated sentinel.'
        Assert-Equal -Actual @(Get-M0SyntheticHelperProcesses).Count -Expected 0 'Abrupt pilot exit leaked a helper.'
        $artifactPath = Join-Path $root 'artifacts\run-P-abrupt-01.process-tree.json'
        Assert-True (Test-Path -LiteralPath $artifactPath -PathType Leaf) 'Abrupt post-artifact exit lost the immutable artifact.'
        $artifactHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        Assert-Throws {
            Invoke-M0SyntheticPilot -RepositoryRoot $RepositoryRoot -EvidenceRoot $root `
                -ExperimentId 'exp-pilot-abrupt' -RunId 'run-P-abrupt-01' `
                -ExpectedProtocolSha256 $script:M0PilotExpectedProtocolSha256
        } 'M0I|incomplete|new.*root|new.*run|no.relaunch|evidence.intent'
        Assert-Equal -Actual (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash -Expected $artifactHash
    }
    finally {
        if (-not $sentinel.HasExited) { $sentinel.Kill(); $sentinel.WaitForExit() }
        $sentinel.Dispose()
    }
}
