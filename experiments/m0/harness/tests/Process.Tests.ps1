$processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
$evidenceModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
$protocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$processDescriptorPath = Join-Path $protocolRoot 'process.v1.json'
$processSidecarPath = Join-Path $protocolRoot 'process.v1.sha256'
$processGoldenPath = Join-Path $protocolRoot 'goldens\process.v1.json'
$processSchemaPath = Join-Path $protocolRoot 'schemas\process.schema.json'
$processGoldenSchemaPath = Join-Path $protocolRoot 'schemas\process-golden.schema.json'
$processTreeSchemaPath = Join-Path $protocolRoot 'schemas\process-tree.schema.json'
$processTreeArtifactSchemaPath = Join-Path $protocolRoot 'schemas\process-tree-artifact.schema.json'
$processRunnerPath = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Process.ps1'
$processHelperPath = Join-Path $RepositoryRoot 'scripts\m0\Invoke-M0SyntheticProcessNode.ps1'

function Invoke-M0ProcessRunnerChild {
    param([Parameter(Mandatory)] [string] $ScriptPath)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $ScriptPath)) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($startInfo)
    try {
        $output = $child.StandardOutput.ReadToEnd() + $child.StandardError.ReadToEnd()
        $child.WaitForExit()
        return [pscustomobject][ordered]@{
            exit_code = $child.ExitCode
            output = $output
        }
    }
    finally {
        $child.Dispose()
    }
}

function New-M0ProcessSnapshotFixture {
    param(
        [Parameter(Mandatory)]
        [ValidateSet('complete', 'partial-root', 'partial-unresolved', 'partial-trace', 'unavailable')]
        [string] $Variant
    )

    $rootIdentity = [pscustomobject][ordered]@{ pid = 101; creation_time_filetime_utc = '1000' }
    $childIdentity = [pscustomobject][ordered]@{ pid = 102; creation_time_filetime_utc = '1001' }
    $grandchildIdentity = [pscustomobject][ordered]@{ pid = 103; creation_time_filetime_utc = '1002' }
    $allNodes = @(
        [pscustomobject][ordered]@{
            role = 'root'; identity = $rootIdentity; parent_identity = $null
            executable_sha256 = 'a' * 64; job_membership_revalidated = $true
        },
        [pscustomobject][ordered]@{
            role = 'child'; identity = $childIdentity; parent_identity = $rootIdentity
            executable_sha256 = 'b' * 64; job_membership_revalidated = $true
        },
        [pscustomobject][ordered]@{
            role = 'grandchild'; identity = $grandchildIdentity; parent_identity = $childIdentity
            executable_sha256 = 'c' * 64; job_membership_revalidated = $true
        }
    )
    $traceComplete = [pscustomobject][ordered]@{
        state = 'complete'; adapter = 'etw-process-start-stop'; complete = $true; collected = $true
        gap_codes = @(); error_code = $null
    }
    $capabilities = [pscustomobject][ordered]@{
        job_object_kill_on_close = $true
        breakaway_flags = 'none'
        membership_handles_revalidated = $true
        helper_sha256_verified = $true
        helper_file_locked_until_readiness = $true
        completion_port = $true
        etw_process_start_stop = $true
        linux_live_status = 'not_proven'
    }
    $snapshot = [pscustomobject][ordered]@{
        '$schema' = 'schemas/process-tree.schema.json'
        schema_id = 'm0.process-tree-smoke.v1'
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        platform = 'windows'
        state = 'complete'
        error_code = $null
        missing_capabilities = @()
        trace = $traceComplete
        capabilities = $capabilities
        roles = @('root', 'child', 'grandchild')
        nodes = $allNodes
        auxiliary_job_members = @()
        job_member_hint_count = 3
        retained_identity_count = 3
        qpc_before_create_ticks = '100'
        qpc_after_resume_ticks = '200'
        qpc_frequency = '1000'
        cleanup_authority = 'private-opaque-registry-only'
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
    }
    switch ($Variant) {
        'partial-root' {
            $snapshot.state = 'partial'
            $snapshot.error_code = 'root_identity_missing'
            $snapshot.missing_capabilities = @('root-handle')
            $snapshot.capabilities.job_object_kill_on_close = $false
            $snapshot.capabilities.membership_handles_revalidated = $false
            $snapshot.roles = @()
            $snapshot.nodes = @()
            $snapshot.job_member_hint_count = 0
            $snapshot.retained_identity_count = 0
            $snapshot.qpc_after_resume_ticks = $null
            $snapshot.cleanup_authority = 'none-no-mutation-capability'
        }
        'partial-unresolved' {
            $snapshot.state = 'partial'
            $snapshot.error_code = 'unresolved_external_attribution'
            $snapshot.missing_capabilities = @('external-attribution-proof')
            $snapshot.roles = @('root')
            $snapshot.nodes = @($allNodes[0])
            $snapshot.job_member_hint_count = 1
            $snapshot.retained_identity_count = 1
        }
        'partial-trace' {
            $snapshot.state = 'partial'
            $snapshot.error_code = 'etw_trace_incomplete'
            $snapshot.missing_capabilities = @('etw-process-start-stop')
            $snapshot.trace = [pscustomobject][ordered]@{
                state = 'partial'; adapter = 'etw-process-start-stop'; complete = $false; collected = $true
                gap_codes = @('lost-event'); error_code = 'etw_trace_incomplete'
            }
            $snapshot.capabilities.etw_process_start_stop = $false
        }
        'unavailable' {
            $snapshot.state = 'unavailable'
            $snapshot.error_code = 'safe_identity_unavailable'
            $snapshot.missing_capabilities = @('etw-process-start-stop', 'safe-process-identity')
            $snapshot.trace = [pscustomobject][ordered]@{
                state = 'unavailable'; adapter = 'etw-process-start-stop'; complete = $false; collected = $false
                gap_codes = @('adapter-unavailable'); error_code = 'etw_trace_unavailable'
            }
            foreach ($name in @(
                'job_object_kill_on_close', 'membership_handles_revalidated', 'helper_sha256_verified',
                'helper_file_locked_until_readiness', 'completion_port', 'etw_process_start_stop'
            )) {
                $snapshot.capabilities.$name = $false
            }
            $snapshot.roles = @()
            $snapshot.nodes = @()
            $snapshot.job_member_hint_count = 0
            $snapshot.retained_identity_count = 0
            $snapshot.qpc_before_create_ticks = $null
            $snapshot.qpc_after_resume_ticks = $null
            $snapshot.qpc_frequency = $null
            $snapshot.cleanup_authority = 'none-no-mutation-capability'
        }
    }
    return $snapshot
}

function New-M0ProcessCleanupResultFixture {
    param([Parameter(Mandatory)] $Snapshot)

    $attempted = [string]$Snapshot.cleanup_authority -ceq 'private-opaque-registry-only'
    return [pscustomobject][ordered]@{
        schema_id = 'm0.process-tree-cleanup.v1'
        cleanup_complete = $true
        survivor_count = 0
        retained_identity_count = [int]$Snapshot.retained_identity_count
        native_cleanup_actions = $(if ($attempted) { 1 } else { 0 })
        cleanup_action = $(if ($attempted) {
                'close-last-job-handle-and-wait-retained-process-handles'
            }
            else {
                'none-no-mutation-capability'
            })
        individual_terminate_process_calls = 0
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        error_code = $null
        failure_codes = @()
    }
}

Test-Case 'The frozen process contract exists before attribution or cleanup is accepted' {
    foreach ($path in @(
        $processModulePath,
        $processDescriptorPath,
        $processSidecarPath,
        $processGoldenPath,
        $processSchemaPath,
        $processGoldenSchemaPath,
        $processTreeSchemaPath,
        $processTreeArtifactSchemaPath,
        $processRunnerPath,
        $processHelperPath
    )) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Missing process contract '$path'."
    }
}

Test-Case 'The public process runner validates the anchored smoke in a clean child and publishes nothing' {
    $clean = Invoke-M0ProcessRunnerChild -ScriptPath $processRunnerPath
    Assert-Equal -Actual $clean.exit_code -Expected 0 "Clean process runner failed: $($clean.output)"
    Assert-True ($clean.output -match 'M0-PROCESS-1') "Runner did not report the frozen process contract: $($clean.output)"
    Assert-True ($clean.output -match 'not published or sealed') "Runner overclaimed Evidence publication: $($clean.output)"

    foreach ($moduleName in @('M0.Harness', 'M0.Process')) {
        $fakeRoot = Join-Path $TestTempRoot "process-runner-preloaded-$moduleName"
        New-Item -ItemType Directory -Path $fakeRoot -Force | Out-Null
        $fakePath = Join-Path $fakeRoot "$moduleName.psm1"
        $fakeSource = "function Get-FakeM0Entrypoint { throw 'FAKE-$moduleName-CALLED' }`nExport-ModuleMember -Function Get-FakeM0Entrypoint`n"
        [IO.File]::WriteAllText($fakePath, $fakeSource, [Text.UTF8Encoding]::new($false))
        $wrapperPath = Join-Path $TestTempRoot "process-runner-preloaded-$moduleName.ps1"
        $wrapperSource = @"
`$ErrorActionPreference = 'Stop'
Import-Module -Name '$($fakePath.Replace("'", "''"))' -ErrorAction Stop
& '$($processRunnerPath.Replace("'", "''"))'
"@
        [IO.File]::WriteAllText($wrapperPath, $wrapperSource, [Text.UTF8Encoding]::new($false))
        $preloaded = Invoke-M0ProcessRunnerChild -ScriptPath $wrapperPath
        Assert-True ($preloaded.exit_code -ne 0) "Runner accepted preloaded '$moduleName'."
        Assert-True ($preloaded.output -match 'M0P040') "Runner did not fail at the clean-process gate: $($preloaded.output)"
        Assert-True ($preloaded.output -notmatch "FAKE-$([regex]::Escape($moduleName))-CALLED") `
            "Runner executed the homonymous preloaded '$moduleName'."
    }

    foreach ($nativeTypeName in @('PilotSession', 'ProcessCapability', 'TrustedFile')) {
        $typeWrapperPath = Join-Path $TestTempRoot "process-runner-preloaded-$nativeTypeName.ps1"
        $typeWrapperSource = @"
`$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition 'namespace M0ProcessNativeV1 { public class $nativeTypeName {} }'
& '$($processRunnerPath.Replace("'", "''"))'
"@
        [IO.File]::WriteAllText($typeWrapperPath, $typeWrapperSource, [Text.UTF8Encoding]::new($false))
        $preloadedType = Invoke-M0ProcessRunnerChild -ScriptPath $typeWrapperPath
        Assert-True ($preloadedType.exit_code -ne 0) "Runner accepted preloaded native type '$nativeTypeName'."
        Assert-True ($preloadedType.output -match 'M0P040' -and
            $preloadedType.output -match 'native type' -and $preloadedType.output -match $nativeTypeName) `
            "Runner did not reject '$nativeTypeName' before importing trusted modules: $($preloadedType.output)"
    }
}

$expectedProcessModulePath = [IO.Path]::GetFullPath($processModulePath)
$processComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$processModules = @(Get-Module -All -Name M0.Process)
if ($processModules.Count -gt 1 -or
    ($processModules.Count -eq 1 -and -not [IO.Path]::GetFullPath($processModules[0].Path).Equals($expectedProcessModulePath, $processComparison))) {
    throw [IO.InvalidDataException]::new('Process.Tests refuses a foreign or ambiguous M0.Process module.')
}
if ($processModules.Count -eq 0) {
    Import-Module $processModulePath -ErrorAction Stop
    $processModules = @(Get-Module -All -Name M0.Process)
}
if ($processModules.Count -ne 1 -or -not [IO.Path]::GetFullPath($processModules[0].Path).Equals($expectedProcessModulePath, $processComparison)) {
    throw [IO.InvalidDataException]::new('Process.Tests could not bind the exact M0.Process module.')
}
$processModule = $processModules[0]
$nativeContractBeforeProjection = $null
$nativeAssemblyBeforeProjection = $null
if ($IsWindows) {
    $nativeState = & $processModule {
        [pscustomobject]@{
            contract = Assert-M0ProcessNativeContract
            assembly = $script:M0ProcessNativeAssembly
        }
    }
    $nativeContractBeforeProjection = $nativeState.contract
    $nativeAssemblyBeforeProjection = $nativeState.assembly
}

# The exact module may already be loaded as a nested dependency of M0.Harness.
# Project that same ModuleInfo into this test-file scope without reloading its
# source bytes, native assembly or private pilot registry.
Import-Module -ModuleInfo $processModule -Scope Local -ErrorAction Stop
$projectedProcessModules = @(Get-Module -All -Name M0.Process)
if ($projectedProcessModules.Count -ne 1 -or
    -not [object]::ReferenceEquals($projectedProcessModules[0], $processModule)) {
    throw [IO.InvalidDataException]::new('Process.Tests changed the exact M0.Process module instance while projecting its exports.')
}
if ($IsWindows) {
    $nativeStateAfterProjection = & $processModule {
        [pscustomobject]@{
            contract = Assert-M0ProcessNativeContract
            assembly = $script:M0ProcessNativeAssembly
        }
    }
    if ($nativeStateAfterProjection.contract.module_instance_id -cne $nativeContractBeforeProjection.module_instance_id -or
        $nativeStateAfterProjection.contract.module_sha256 -cne $nativeContractBeforeProjection.module_sha256 -or
        -not [object]::ReferenceEquals($nativeStateAfterProjection.assembly, $nativeAssemblyBeforeProjection)) {
        throw [IO.InvalidDataException]::new('Process.Tests reloaded or replaced the native M0.Process contract while projecting its exports.')
    }
}

Test-Case 'The process module exports only the approved safe API' {
    Assert-SequenceEqual -Actual @($processModule.ExportedFunctions.Keys | Sort-Object) -Expected @(
        'Assert-M0ProcessArtifacts',
        'Close-M0SyntheticProcessPilot',
        'Get-M0SyntheticProcessPilotSnapshot',
        'New-M0ProcessTreeArtifact',
        'Open-M0SyntheticProcessPilot',
        'Resolve-M0LinuxProcessIdentity',
        'Resolve-M0ProcessAttribution',
        'Resolve-M0ProcessCleanupPlan',
        'Resolve-M0ProcessPilotOutcome'
    )
    $outcomeCommand = Get-Command -Name Resolve-M0ProcessPilotOutcome -Module M0.Process -ErrorAction Stop
    Assert-True $outcomeCommand.Parameters.ContainsKey('ProcessArtifact') `
        'The public outcome mapper must require the bound process-tree artifact.'
    foreach ($legacyParameter in @(
        'ProcessState', 'ProcessErrorCode', 'CleanupComplete', 'SurvivorCount', 'CleanupErrorCode'
    )) {
        Assert-True (-not $outcomeCommand.Parameters.ContainsKey($legacyParameter)) `
            "The public outcome mapper exposed legacy caller-authoritative field '$legacyParameter'."
    }
}

Test-Case 'The pure process-tree builder requires one canonical prevalidated cleanup projection' {
    $snapshot = New-M0ProcessSnapshotFixture -Variant complete
    Assert-Throws {
        New-M0ProcessTreeArtifact -Snapshot $snapshot `
            -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-red-01'
    } 'M0P060|cleanup|required|closed'

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
    $artifact = New-M0ProcessTreeArtifact -Snapshot $snapshot -CleanupResult $cleanup `
        -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-red-01'
    Assert-Equal -Actual $artifact.document.cleanup.attempted -Expected $true
    Assert-Equal -Actual $artifact.document.cleanup.completed -Expected $true
    Assert-Equal -Actual $artifact.document.cleanup.survivor_count -Expected 0
    Assert-Equal -Actual $artifact.document.cleanup.failure_code -Expected $null
    Assert-Equal -Actual $artifact.document.cleanup.retained_identity_count -Expected 3
    Assert-Equal -Actual $artifact.document.cleanup.native_cleanup_actions -Expected 1
    Assert-Equal -Actual $artifact.document.cleanup.cleanup_action `
        -Expected 'close-last-job-handle-and-wait-retained-process-handles'
    Assert-Equal -Actual $artifact.document.cleanup.individual_terminate_process_calls -Expected 0
    Assert-Equal -Actual @($artifact.document.cleanup.failure_codes).Count -Expected 0
    Assert-Equal -Actual $artifact.document.cleanup.candidate_code -Expected 'P'
    Assert-Equal -Actual $artifact.document.cleanup.scenario_id -Expected 'synthetic-process-tree'
    Assert-Equal -Actual $artifact.document.cleanup.attempt -Expected 1
    Assert-Equal -Actual $artifact.document.cleanup.evidence_role -Expected 'synthetic_pilot'
    Assert-Equal -Actual $artifact.document.cleanup.decision_eligible -Expected $false
    $artifactOutcome = Resolve-M0ProcessPilotOutcome -ProcessArtifact $artifact.document
    Assert-Equal -Actual $artifactOutcome.check_payload.status -Expected 'passed'
    Assert-Equal -Actual $artifactOutcome.run_status -Expected 'valid'
    Assert-Equal -Actual $artifactOutcome.run_status_reason -Expected $null
    Assert-Equal -Actual $artifactOutcome.run_error_code -Expected $null
    $schemaValidDuplicateIdentity = $artifact.document | ConvertTo-Json -Compress -Depth 30 |
        ConvertFrom-Json -Depth 30
    $schemaValidDuplicateIdentity.snapshot.nodes[1].identity =
        $schemaValidDuplicateIdentity.snapshot.nodes[0].identity
    $duplicateIdentityJson = $schemaValidDuplicateIdentity | ConvertTo-Json -Compress -Depth 30
    Assert-True (Test-Json -Json $duplicateIdentityJson -SchemaFile $processTreeArtifactSchemaPath `
        -ErrorAction SilentlyContinue) `
        'Artifact-level semantic Red must remain structurally schema-valid.'
    Assert-Throws {
        Resolve-M0ProcessPilotOutcome -ProcessArtifact $schemaValidDuplicateIdentity
    } 'M0P06|duplicate identity|process-tree semantic'
    Assert-True (Test-Json -Json $artifact.canonical_json -SchemaFile $processTreeArtifactSchemaPath `
        -ErrorAction SilentlyContinue) 'Canonical process-tree artifact failed its closed schema.'

    $fallbackOriginWithObservedSnapshot = $artifact.document | ConvertTo-Json -Compress -Depth 30 |
        ConvertFrom-Json -Depth 30
    $fallbackOriginWithObservedSnapshot.snapshot_origin = 'pre_snapshot_fallback'
    $fallbackOriginJson = $fallbackOriginWithObservedSnapshot | ConvertTo-Json -Compress -Depth 30
    Assert-True (-not (Test-Json -Json $fallbackOriginJson -SchemaFile $processTreeArtifactSchemaPath `
        -ErrorAction SilentlyContinue)) 'Schema accepted fallback origin with an observed complete snapshot.'
    Assert-Throws {
        Resolve-M0ProcessPilotOutcome -ProcessArtifact $fallbackOriginWithObservedSnapshot
    } 'M0P061|artifact|schema|origin|fallback'
    Assert-Throws {
        & $processModule {
            param($cleanupDocument, $snapshotDocument)
            Assert-M0CanonicalProcessCleanup -Cleanup $cleanupDocument -Snapshot $snapshotDocument `
                -SnapshotOrigin pre_snapshot_fallback
        } $artifact.document.cleanup $artifact.document.snapshot
    } 'M0P060|origin|fallback|unavailable'

    $rootMissingSnapshot = New-M0ProcessSnapshotFixture -Variant partial-root
    $rootMissingCleanup = New-M0ProcessCleanupResultFixture -Snapshot $rootMissingSnapshot
    $rootMissingArtifact = New-M0ProcessTreeArtifact -Snapshot $rootMissingSnapshot `
        -CleanupResult $rootMissingCleanup -ExperimentId 'exp-process-cleanup-authority' `
        -RunId 'run-P-cleanup-authority-root-01'
    $forgedRootCleanup = $rootMissingArtifact.document | ConvertTo-Json -Compress -Depth 30 |
        ConvertFrom-Json -Depth 30
    $forgedRootCleanup.cleanup.attempted = $true
    $forgedRootCleanup.cleanup.native_cleanup_actions = 1
    $forgedRootCleanup.cleanup.cleanup_action = 'close-last-job-handle-and-wait-retained-process-handles'
    Assert-True (-not (Test-Json -Json ($forgedRootCleanup | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeArtifactSchemaPath -ErrorAction SilentlyContinue)) `
        'Artifact schema accepted attempted cleanup without observed mutation authority.'
    Assert-Throws {
        Resolve-M0ProcessPilotOutcome -ProcessArtifact $forgedRootCleanup
    } 'M0P061|artifact|schema'
    Assert-Throws {
        & $processModule {
            param($cleanupDocument, $snapshotDocument)
            Assert-M0CanonicalProcessCleanup -Cleanup $cleanupDocument -Snapshot $snapshotDocument `
                -SnapshotOrigin observed
        } $forgedRootCleanup.cleanup $forgedRootCleanup.snapshot
    } 'M0P060|authority|attempt'

    $forgedPrivateCleanup = $artifact.document.cleanup | ConvertTo-Json -Compress -Depth 20 |
        ConvertFrom-Json -Depth 20
    $forgedPrivateCleanup.attempted = $false
    $forgedPrivateCleanup.native_cleanup_actions = 0
    $forgedPrivateCleanup.cleanup_action = 'none-no-mutation-capability'
    Assert-Throws {
        & $processModule {
            param($cleanupDocument, $snapshotDocument)
            Assert-M0CanonicalProcessCleanup -Cleanup $cleanupDocument -Snapshot $snapshotDocument `
                -SnapshotOrigin observed
        } $forgedPrivateCleanup $artifact.document.snapshot
    } 'M0P060|authority|attempt'

    $reorderedCleanup = [pscustomobject][ordered]@{}
    foreach ($property in @($cleanup.PSObject.Properties | Sort-Object Name -Descending)) {
        $reorderedCleanup | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
    }
    $reorderedArtifact = New-M0ProcessTreeArtifact -Snapshot $snapshot -CleanupResult $reorderedCleanup `
        -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-red-01'
    Assert-Equal -Actual $reorderedArtifact.canonical_json -Expected $artifact.canonical_json
    Assert-Equal -Actual $reorderedArtifact.sha256 -Expected $artifact.sha256

    $sameBirthDifferentPid = $snapshot | ConvertTo-Json -Compress -Depth 30 | ConvertFrom-Json -Depth 30
    $sameBirthDifferentPid.nodes[1].identity.creation_time_filetime_utc = `
        $sameBirthDifferentPid.nodes[0].identity.creation_time_filetime_utc
    $sameBirthDifferentPid.nodes[2].parent_identity.creation_time_filetime_utc = `
        $sameBirthDifferentPid.nodes[1].identity.creation_time_filetime_utc
    $sameBirthArtifact = New-M0ProcessTreeArtifact -Snapshot $sameBirthDifferentPid -CleanupResult $cleanup `
        -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-same-birth-01'
    Assert-Equal -Actual @($sameBirthArtifact.document.snapshot.nodes).Count -Expected 3
    Assert-Equal -Actual $sameBirthArtifact.document.snapshot.nodes[0].identity.creation_time_filetime_utc `
        -Expected $sameBirthArtifact.document.snapshot.nodes[1].identity.creation_time_filetime_utc `
        'Distinct PIDs with the same FILETIME token must remain valid strong identity pairs.'

    $invalidCases = @(
        [pscustomobject]@{ case_id = 'complete-with-survivor'; apply = { param($value) $value.survivor_count = 1 } },
        [pscustomobject]@{ case_id = 'incomplete-without-primary'; apply = {
                param($value) $value.cleanup_complete = $false
            } },
        [pscustomobject]@{ case_id = 'open-caller-error'; apply = {
                param($value) $value.cleanup_complete = $false; $value.error_code = 'caller_supplied_reason'
                $value.failure_codes = @('caller_supplied_reason')
            } },
        [pscustomobject]@{ case_id = 'identity-mismatch'; apply = { param($value) $value.candidate_code = 'A' } },
        [pscustomobject]@{ case_id = 'schema-id-mismatch'; apply = { param($value) $value.schema_id = 'm0.process-tree-cleanup.v2' } },
        [pscustomobject]@{ case_id = 'retained-count-mismatch'; apply = { param($value) $value.retained_identity_count = 2 } },
        [pscustomobject]@{ case_id = 'native-action-count-mismatch'; apply = { param($value) $value.native_cleanup_actions = 0 } },
        [pscustomobject]@{ case_id = 'cleanup-action-mismatch'; apply = { param($value) $value.cleanup_action = 'caller-supplied-action' } },
        [pscustomobject]@{ case_id = 'individual-terminate-call'; apply = { param($value) $value.individual_terminate_process_calls = 1 } },
        [pscustomobject]@{ case_id = 'duplicate-failure-code'; apply = {
                param($value) $value.cleanup_complete = $false; $value.error_code = 'job_close_failed'
                $value.failure_codes = @('job_close_failed', 'job_close_failed')
            } },
        [pscustomobject]@{ case_id = 'primary-failure-mismatch'; apply = {
                param($value) $value.cleanup_complete = $false; $value.error_code = 'job_close_failed'
                $value.failure_codes = @('unsafe_run_root_contents', 'job_close_failed')
            } },
        [pscustomobject]@{ case_id = 'secondary-failure-order'; apply = {
                param($value) $value.cleanup_complete = $false; $value.error_code = 'snapshot_validation_failed'
                $value.failure_codes = @('snapshot_validation_failed', 'unsafe_run_root_contents', 'job_close_failed')
            } },
        [pscustomobject]@{ case_id = 'survivor-error-with-zero-survivors'; apply = {
                param($value) $value.cleanup_complete = $false; $value.error_code = 'helper_survived_shutdown'
                $value.failure_codes = @('helper_survived_shutdown')
            } }
    )
    foreach ($invalidCase in $invalidCases) {
        $invalidCleanup = $cleanup | ConvertTo-Json -Compress -Depth 20 | ConvertFrom-Json -Depth 20
        & $invalidCase.apply $invalidCleanup
        Assert-Throws {
            New-M0ProcessTreeArtifact -Snapshot $snapshot -CleanupResult $invalidCleanup `
                -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-invalid-01'
        } "M0P060|cleanup|survivor|failure|identity|$($invalidCase.case_id)"
    }

    Assert-Throws {
        New-M0ProcessTreeArtifact -UnavailableErrorCode safe_identity_unavailable `
            -ExperimentId 'exp-process-cleanup-red' -RunId 'run-P-cleanup-fallback-01'
    } 'M0P060|cleanup|required|closed'
}

Test-Case 'The process descriptor, sidecar and golden form one anchored closed contract' {
    $result = Assert-M0ProcessArtifacts -ProtocolRoot $protocolRoot
    Assert-Equal -Actual $result.process_id -Expected 'M0-PROCESS-1'
    Assert-Equal -Actual $result.golden_id -Expected 'M0-PROCESS-GOLDEN-1'
    Assert-True ($result.descriptor_sha256 -match '^[a-f0-9]{64}$')
    Assert-True ($result.golden_sha256 -match '^[a-f0-9]{64}$')
}

Test-Case 'Process artifact validation executes golden semantics instead of trusting reanchored expected output' {
    $tamperedRoot = Join-Path $TestTempRoot 'reanchored-process-protocol'
    Copy-Item -LiteralPath $protocolRoot -Destination $tamperedRoot -Recurse
    $tamperedGoldenPath = Join-Path $tamperedRoot 'goldens\process.v1.json'
    $tamperedDescriptorPath = Join-Path $tamperedRoot 'process.v1.json'
    $tamperedSidecarPath = Join-Path $tamperedRoot 'process.v1.sha256'
    $golden = Get-Content -LiteralPath $tamperedGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $golden.attribution_vectors[0].expected.sources[0] = 'nonce-executable-match'
    [IO.File]::WriteAllText(
        $tamperedGoldenPath,
        (($golden | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")) + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $descriptor = Get-Content -LiteralPath $tamperedDescriptorPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $descriptor.golden_sha256 = (Get-FileHash -LiteralPath $tamperedGoldenPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        $tamperedDescriptorPath,
        (($descriptor | ConvertTo-Json -Depth 100).Replace("`r`n", "`n")) + "`n",
        [Text.UTF8Encoding]::new($false)
    )
    $descriptorHash = (Get-FileHash -LiteralPath $tamperedDescriptorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        $tamperedSidecarPath,
        "$descriptorHash  process.v1.json`n",
        [Text.UTF8Encoding]::new($false)
    )

    Assert-Throws { Assert-M0ProcessArtifacts -ProtocolRoot $tamperedRoot } 'M0P005|semantic|golden'
}

Test-Case 'The live helper is transitively anchored and a modified copy is rejected before launch' {
    $descriptor = Get-Content -LiteralPath $processDescriptorPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Equal -Actual $descriptor.pilot_contract.helper_relative_path `
        -Expected 'scripts/m0/Invoke-M0SyntheticProcessNode.ps1'
    Assert-True ([string]$descriptor.pilot_contract.helper_sha256 -cmatch '^[a-f0-9]{64}$')
    $actualHelperHash = (Get-FileHash -LiteralPath $processHelperPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Equal -Actual $actualHelperHash -Expected $descriptor.pilot_contract.helper_sha256

    if (-not $IsWindows) { return }
    $fakeRoot = Join-Path $TestTempRoot 'process-helper-tamper-repository'
    New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'experiments\m0') -Force | Out-Null
    Copy-Item -LiteralPath $protocolRoot -Destination (Join-Path $fakeRoot 'experiments\m0\protocol') -Recurse
    New-Item -ItemType Directory -Path (Join-Path $fakeRoot 'scripts\m0') -Force | Out-Null
    $fakeHelper = Join-Path $fakeRoot 'scripts\m0\Invoke-M0SyntheticProcessNode.ps1'
    Copy-Item -LiteralPath $processHelperPath -Destination $fakeHelper
    Assert-Throws {
        & $processModule { param($path) Assert-M0PilotRepositoryRoot -RepositoryRoot $path } $fakeRoot
    } 'M0P04|trusted|repository root'
    [IO.File]::AppendAllText($fakeHelper, "# tampered`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws {
        & $processModule {
            param($path, $expectedHash)
            $null = Assert-M0PilotHelperFile -LiteralPath $path -ExpectedSha256 $expectedHash
        } $fakeHelper $descriptor.pilot_contract.helper_sha256
    } 'M0P04|helper|SHA|hash|anchor'
    $liveMarkers = @(Get-ChildItem -LiteralPath $fakeRoot -Filter '*.ready.json' -File -Recurse -ErrorAction SilentlyContinue)
    Assert-Equal -Actual $liveMarkers.Count -Expected 0 'A modified helper must be rejected before CreateProcess.'
}

Test-Case 'Process pilot directory validation rejects a linked out ancestor before creating a run leaf' {
    if (-not $IsWindows) { return }

    $fakeRoot = Join-Path $TestTempRoot 'linked-output-repository'
    $outside = Join-Path $TestTempRoot 'linked-output-target'
    New-Item -ItemType Directory -Path $fakeRoot -Force | Out-Null
    New-Item -ItemType Directory -Path $outside -Force | Out-Null
    $linkedOut = Join-Path $fakeRoot 'out'
    New-Item -ItemType Junction -Path $linkedOut -Target $outside | Out-Null
    Assert-Throws {
        & $processModule { param($path) Assert-M0PilotDirectoryChain -RepositoryRoot $path -CreateParents } $fakeRoot
    } 'M0P040 linked output directory ancestor is forbidden'
    Assert-Equal -Actual @(Get-ChildItem -LiteralPath $outside -Force).Count -Expected 0
}

Test-Case 'Process attribution golden distinguishes Job, ETW lineage, nonce fallback and unrelated processes' {
    $golden = Get-Content -LiteralPath $processGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.attribution_vectors)) {
        $actual = Resolve-M0ProcessAttribution -RootIdentity $vector.root_identity -Records @($vector.records) `
            -ExpectedExecutableSha256 $vector.expected_executable_sha256 -Nonce $vector.nonce `
            -LaunchQpcTicks $vector.launch_qpc_ticks -AttemptEndQpcTicks $vector.attempt_end_qpc_ticks `
            -TraceState complete
        Assert-Equal -Actual $actual.state -Expected $vector.expected.state "State differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.error_code -Expected $vector.expected.error_code "Error differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.missing_capabilities.Count -Expected $vector.expected.missing_capabilities.Count `
            "Missing capability count differs for '$($vector.case_id)'."
        if ($vector.expected.missing_capabilities.Count -gt 0) {
            Assert-SequenceEqual -Actual @($actual.missing_capabilities) -Expected @($vector.expected.missing_capabilities)
        }
        Assert-Equal -Actual $actual.identities.Count -Expected $vector.expected.identities.Count "Identity count differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.sources.Count -Expected $vector.expected.sources.Count "Source count differs for '$($vector.case_id)'."
        if ($vector.expected.sources.Count -gt 0) {
            Assert-SequenceEqual -Actual @($actual.sources) -Expected @($vector.expected.sources) "Source order differs for '$($vector.case_id)'."
        }
        for ($index = 0; $index -lt $vector.expected.identities.Count; $index++) {
            Assert-Equal -Actual $actual.identities[$index].pid -Expected $vector.expected.identities[$index].pid
            Assert-Equal -Actual $actual.identities[$index].creation_time_filetime_utc `
                -Expected $vector.expected.identities[$index].creation_time_filetime_utc
        }
    }
}

Test-Case 'Synthetic ETW breakaway uses an attributed ancestor ledger without claiming live parent-exit coverage' {
    $descriptor = Get-Content -LiteralPath $processDescriptorPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Equal -Actual $descriptor.attribution_contract.attributed_ancestor_identity_remains_in_resolver_ledger `
        -Expected $true
    Assert-Equal -Actual $descriptor.cleanup_contract.breakaway_live_status -Expected 'not-implemented'
    Assert-Equal -Actual $descriptor.cleanup_contract.future_direct_termination_identity_mismatch_policy `
        -Expected 'do-not-signal-and-fail-cleanup'
    Assert-Equal -Actual $descriptor.cleanup_contract.job_revalidation_failure_policy `
        -Expected 'close-scoped-job-and-fail-cleanup'
    Assert-True ($descriptor.cleanup_contract.PSObject.Properties.Name -cnotcontains 'identity_mismatch_policy') `
        'Direct termination and scoped Job cleanup must not share an ambiguous mismatch policy.'
    $golden = Get-Content -LiteralPath $processGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $vector = @($golden.attribution_vectors | Where-Object case_id -CEQ 'job-etw-synthetic-breakaway-nonce-and-unrelated')[0]
    Assert-Equal -Actual $vector.records[2].job_member -Expected $false
    Assert-Equal -Actual $vector.records[2].etw_birth_observed -Expected $true
    Assert-Equal -Actual $vector.records[2].parent_identity.pid -Expected $vector.records[1].identity.pid
    $actual = Resolve-M0ProcessAttribution -RootIdentity $vector.root_identity -Records @($vector.records) `
        -ExpectedExecutableSha256 $vector.expected_executable_sha256 -Nonce $vector.nonce `
        -LaunchQpcTicks $vector.launch_qpc_ticks -AttemptEndQpcTicks $vector.attempt_end_qpc_ticks `
        -TraceState complete
    Assert-Equal -Actual $actual.sources[2] -Expected 'etw-birth-descendant'
}

Test-Case 'PID reuse is a distinct identity and an unrelated later birth is excluded without corrupting the tree' {
    $hash = 'a' * 64
    $nonce = '1' * 32
    $root = [pscustomobject][ordered]@{ pid = 501; creation_time_filetime_utc = '1000' }
    $records = @(
        [pscustomobject][ordered]@{
            observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
            job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '110'
        },
        [pscustomobject][ordered]@{
            observation_seq = 2
            identity = [pscustomobject][ordered]@{ pid = 501; creation_time_filetime_utc = '2000' }
            parent_identity = $null; executable_sha256 = ('b' * 64)
            job_member = $false; etw_birth_observed = $false; nonce_argument_match = $false
            observed_nonce = $null
            external_attribution_required = $false; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '200'
        }
    )

    $actual = Resolve-M0ProcessAttribution -RootIdentity $root -Records $records `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState complete
    Assert-Equal -Actual $actual.state -Expected 'complete'
    Assert-Equal -Actual $actual.error_code -Expected $null
    Assert-Equal -Actual $actual.identities.Count -Expected 1
    Assert-Equal -Actual $actual.identities[0].creation_time_filetime_utc -Expected '1000'
}

Test-Case 'Nonce fallback requires expected executable and a birth inside the attempt window' {
    $hash = 'a' * 64
    $nonce = '2' * 32
    $root = [pscustomobject][ordered]@{ pid = 601; creation_time_filetime_utc = '1000' }
    $records = @(
        [pscustomobject][ordered]@{
            observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
            job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '110'
        },
        [pscustomobject][ordered]@{
            observation_seq = 2
            identity = [pscustomobject][ordered]@{ pid = 602; creation_time_filetime_utc = '900' }
            parent_identity = $null; executable_sha256 = $hash
            job_member = $false; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $false; observed_after_launch = $false; within_attempt_window = $false
            birth_qpc_ticks = '90'
        },
        [pscustomobject][ordered]@{
            observation_seq = 3
            identity = [pscustomobject][ordered]@{ pid = 603; creation_time_filetime_utc = '2000' }
            parent_identity = $null; executable_sha256 = $hash
            job_member = $false; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '120'
        }
    )

    $actual = Resolve-M0ProcessAttribution -RootIdentity $root -Records $records `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState complete
    Assert-Equal -Actual $actual.state -Expected 'complete'
    Assert-Equal -Actual $actual.identities.Count -Expected 2
    Assert-SequenceEqual -Actual @($actual.identities.pid) -Expected @(601, 603)
    Assert-SequenceEqual -Actual @($actual.sources) -Expected @('root-handle', 'nonce-executable-match')
}

Test-Case 'Attribution validates closed typed records while identity property order is irrelevant' {
    $hash = 'a' * 64
    $nonce = '3' * 32
    $reorderedRoot = [pscustomobject][ordered]@{
        creation_time_filetime_utc = '1000'
        pid = 701
    }
    $validRootRecord = [pscustomobject][ordered]@{
        observation_seq = 1; identity = $reorderedRoot; parent_identity = $null; executable_sha256 = $hash
        job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true
        observed_nonce = $nonce
        external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '110'
    }
    $valid = Resolve-M0ProcessAttribution -RootIdentity $reorderedRoot -Records @($validRootRecord) `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState complete
    Assert-Equal -Actual $valid.state -Expected 'complete'

    $stringBoolean = [pscustomobject][ordered]@{
        observation_seq = 2
        identity = [pscustomobject][ordered]@{ pid = 702; creation_time_filetime_utc = '2000' }
        parent_identity = $null; executable_sha256 = ('b' * 64)
        job_member = 'false'; etw_birth_observed = $false; nonce_argument_match = $false
        observed_nonce = $null
        external_attribution_required = $false; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '120'
    }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $reorderedRoot -Records @($validRootRecord, $stringBoolean) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
            -TraceState complete
    } 'M0P01|boolean|closed|record'
}

Test-Case 'Nonce timing is derived from closed QPC boundaries and cannot be spoofed by booleans' {
    $hash = 'a' * 64
    $nonce = '4' * 32
    $root = [pscustomobject][ordered]@{ pid = 801; creation_time_filetime_utc = '1000' }
    $records = @(
        [pscustomobject][ordered]@{
            observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
            job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '110'
        },
        [pscustomobject][ordered]@{
            observation_seq = 2
            identity = [pscustomobject][ordered]@{ pid = 802; creation_time_filetime_utc = '900' }
            parent_identity = $null; executable_sha256 = $hash
            job_member = $false; etw_birth_observed = $false; nonce_argument_match = $true
            observed_nonce = $nonce
            external_attribution_required = $false; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '90'
        }
    )

    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records $records `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '200' `
            -TraceState complete
    } 'M0P01|QPC|timing|window'
}

Test-Case 'Incomplete or unavailable ETW state is explicit and can never produce a complete attribution' {
    $hash = 'a' * 64
    $nonce = '5' * 32
    $root = [pscustomobject][ordered]@{ pid = 901; creation_time_filetime_utc = '1000' }
    $record = [pscustomobject][ordered]@{
        observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
        job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true
        observed_nonce = $nonce
        external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '110'
    }

    $partial = Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState partial -TraceGapCode 'lost-event'
    Assert-Equal -Actual $partial.state -Expected 'partial'
    Assert-Equal -Actual $partial.error_code -Expected 'etw_trace_incomplete'
    Assert-SequenceEqual -Actual @($partial.missing_capabilities) -Expected @('etw-process-start-stop')

    $unavailable = Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState unavailable
    Assert-Equal -Actual $unavailable.state -Expected 'unavailable'
    Assert-Equal -Actual $unavailable.identities.Count -Expected 0
    Assert-Equal -Actual $unavailable.error_code -Expected 'etw_trace_unavailable'
}

Test-Case 'Every frozen ETW loss or conflict cause is explicit and prevents complete attribution' {
    $hash = 'a' * 64
    $nonce = '6' * 32
    $root = [pscustomobject][ordered]@{ pid = 911; creation_time_filetime_utc = '1000' }
    $record = [pscustomobject][ordered]@{
        observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
        job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true; observed_nonce = $nonce
        external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '110'
    }
    $causes = @('lost-event', 'buffer-overflow', 'trace-started-after-launch', 'identity-conflict', 'parentage-conflict')
    foreach ($cause in $causes) {
        $actual = Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
            -TraceState partial -TraceGapCode $cause
        Assert-Equal -Actual $actual.state -Expected 'partial'
        Assert-Equal -Actual $actual.error_code -Expected 'etw_trace_incomplete'
        Assert-Equal -Actual $actual.trace_gap_code -Expected $cause
    }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
            -TraceState partial
    } 'M0P013|trace gap|cause'
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
            -TraceState Unavailable
    } 'M0P013|canonical|trace state'
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @($record) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
            -TraceState partial -TraceGapCode Lost-Event
    } 'M0P013|canonical|trace gap'
}

Test-Case 'An unresolved ETW parent does not shadow valid nonce fallback and partial trace gaps survive other incompleteness' {
    $hash = 'a' * 64
    $nonce = '7' * 32
    $root = [pscustomobject][ordered]@{ pid = 921; creation_time_filetime_utc = '1000' }
    $records = @(
        [pscustomobject][ordered]@{
            observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
            job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true; observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = '110'
        },
        [pscustomobject][ordered]@{
            observation_seq = 2
            identity = [pscustomobject][ordered]@{ pid = 922; creation_time_filetime_utc = '2000' }
            parent_identity = [pscustomobject][ordered]@{ pid = 999; creation_time_filetime_utc = '9999' }
            executable_sha256 = $hash; job_member = $false; etw_birth_observed = $true
            nonce_argument_match = $true; observed_nonce = $nonce; external_attribution_required = $true
            observed_after_launch = $true; within_attempt_window = $true; birth_qpc_ticks = '120'
        }
    )
    $fallback = Resolve-M0ProcessAttribution -RootIdentity $root -Records $records `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState complete
    Assert-Equal -Actual $fallback.state -Expected 'complete'
    Assert-SequenceEqual -Actual @($fallback.sources) -Expected @('root-handle', 'nonce-executable-match')

    $records[1].executable_sha256 = 'b' * 64
    $partial = Resolve-M0ProcessAttribution -RootIdentity $root -Records $records `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState partial -TraceGapCode 'buffer-overflow'
    Assert-Equal -Actual $partial.state -Expected 'partial'
    Assert-Equal -Actual $partial.error_code -Expected 'unresolved_external_attribution'
    Assert-Equal -Actual $partial.trace_gap_code -Expected 'buffer-overflow'
    Assert-SequenceEqual -Actual @($partial.missing_capabilities) `
        -Expected @('etw-process-start-stop', 'external-attribution-proof')

    $rootlessRecord = [pscustomobject][ordered]@{
        observation_seq = 1
        identity = [pscustomobject][ordered]@{ pid = 923; creation_time_filetime_utc = '3000' }
        parent_identity = $null; executable_sha256 = ('b' * 64); job_member = $false
        etw_birth_observed = $false; nonce_argument_match = $false; observed_nonce = $null
        external_attribution_required = $false; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '130'
    }
    $rootMissing = Resolve-M0ProcessAttribution -RootIdentity $root -Records @($rootlessRecord) `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState partial -TraceGapCode 'lost-event'
    Assert-Equal -Actual $rootMissing.error_code -Expected 'root_identity_missing'
    Assert-Equal -Actual $rootMissing.trace_gap_code -Expected 'lost-event'
    Assert-SequenceEqual -Actual @($rootMissing.missing_capabilities) `
        -Expected @('etw-process-start-stop', 'root-handle')

    $externalWithoutRoot = $rootlessRecord | ConvertTo-Json -Compress -Depth 20 | ConvertFrom-Json -Depth 20
    $externalWithoutRoot.external_attribution_required = $true
    $rootPrecedence = Resolve-M0ProcessAttribution -RootIdentity $root -Records @($externalWithoutRoot) `
        -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' `
        -TraceState complete
    Assert-Equal -Actual $rootPrecedence.error_code -Expected 'root_identity_missing'
    Assert-SequenceEqual -Actual @($rootPrecedence.missing_capabilities) -Expected @('root-handle')
}

Test-Case 'Name path hash process-group and session metadata never confer process ownership' {
    $descriptor = Get-Content -LiteralPath $processDescriptorPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($property in @(
        'name_only_forbidden', 'path_only_forbidden', 'hash_only_forbidden',
        'process_group_only_forbidden', 'session_only_forbidden'
    )) {
        Assert-Equal -Actual $descriptor.identity_contract.$property -Expected $true
    }

    $hash = 'a' * 64
    $nonce = '6' * 32
    $root = [pscustomobject][ordered]@{ pid = 912; creation_time_filetime_utc = '1000' }
    $spoof = [pscustomobject][ordered]@{
        observation_seq = 1; identity = $root; parent_identity = $null; executable_sha256 = $hash
        job_member = $false; etw_birth_observed = $false; nonce_argument_match = $false; observed_nonce = $null
        external_attribution_required = $false; observed_after_launch = $true; within_attempt_window = $true
        birth_qpc_ticks = '110'; process_group_id = 912; session_id = 1; executable_path = 'C:\spoof\same.exe'
    }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @($spoof) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce `
            -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P011|closed|properties'
}

Test-Case 'Nonce fallback compares the observed attempt nonce ordinally instead of trusting a boolean' {
    $golden = Get-Content -LiteralPath $processGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $vector = $golden.attribution_vectors[0]
    $actual = Resolve-M0ProcessAttribution -RootIdentity $vector.root_identity -Records @($vector.records) `
        -ExpectedExecutableSha256 $vector.expected_executable_sha256 -Nonce ('f' * 32) `
        -LaunchQpcTicks $vector.launch_qpc_ticks -AttemptEndQpcTicks $vector.attempt_end_qpc_ticks `
        -TraceState complete
    Assert-Equal -Actual $actual.state -Expected 'partial'
    Assert-Equal -Actual $actual.error_code -Expected 'unresolved_external_attribution'
}

Test-Case 'Synthetic cleanup golden never authorizes a PID-only mutation and models exact retained identities' {
    $golden = Get-Content -LiteralPath $processGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.cleanup_vectors)) {
        $actual = Resolve-M0ProcessCleanupPlan -Observations @($vector.observations)
        Assert-Equal -Actual $actual.complete -Expected $vector.expected.complete "Cleanup state differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.actions.Count -Expected $vector.observations.Count
        for ($index = 0; $index -lt $vector.observations.Count; $index++) {
            Assert-Equal -Actual $actual.actions[$index].action -Expected $vector.expected.actions[$index]
            Assert-Equal -Actual $actual.actions[$index].pid -Expected $vector.observations[$index].recorded_identity.pid
        }
    }
}

Test-Case 'Process identities, sequences and root launch causality are canonical and globally unique' {
    $hash = 'a' * 64
    $nonce = '6' * 32
    function New-StrictProcessRecord {
        param($Identity, $Sequence = 1, $BirthQpc = '110')
        return [pscustomobject][ordered]@{
            observation_seq = $Sequence; identity = $Identity; parent_identity = $null; executable_sha256 = $hash
            job_member = $true; etw_birth_observed = $false; nonce_argument_match = $true; observed_nonce = $nonce
            external_attribution_required = $true; observed_after_launch = $true; within_attempt_window = $true
            birth_qpc_ticks = $BirthQpc
        }
    }

    $stringPid = [pscustomobject][ordered]@{ pid = '1001'; creation_time_filetime_utc = '1000' }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $stringPid -Records @((New-StrictProcessRecord $stringPid)) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P01|PID|integer'

    $zeroBirth = [pscustomobject][ordered]@{ pid = 1001; creation_time_filetime_utc = '0' }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $zeroBirth -Records @((New-StrictProcessRecord $zeroBirth)) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P01|creation|birth|positive'

    $root = [pscustomobject][ordered]@{ pid = 1001; creation_time_filetime_utc = '1000' }
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @((New-StrictProcessRecord $root 1.0)) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P01|sequence|integer'

    $duplicate = New-StrictProcessRecord $root 2 '120'
    $duplicate.job_member = $false
    $duplicate.external_attribution_required = $false
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @((New-StrictProcessRecord $root), $duplicate) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P01|duplicate|identity'

    $preLaunchRoot = New-StrictProcessRecord $root 1 '90'
    $preLaunchRoot.observed_after_launch = $false
    $preLaunchRoot.within_attempt_window = $false
    Assert-Throws {
        Resolve-M0ProcessAttribution -RootIdentity $root -Records @($preLaunchRoot) `
            -ExpectedExecutableSha256 $hash -Nonce $nonce -LaunchQpcTicks '100' -AttemptEndQpcTicks '1000' -TraceState complete
    } 'M0P01|root|launch|causal'
}

Test-Case 'Linux proc stat parsing and pidfd capability remain pure, explicit and not proven live' {
    $golden = Get-Content -LiteralPath $processGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.linux_vectors)) {
        $actual = Resolve-M0LinuxProcessIdentity -ProcStat $vector.proc_stat -ExpectedPid $vector.expected_pid `
            -ExpectedStartTicks $vector.expected_start_ticks -ExpectedBootIdSha256 $vector.expected_boot_id_sha256 `
            -ObservedBootIdSha256 $vector.observed_boot_id_sha256 -PidfdAvailable $vector.pidfd_available
        Assert-Equal -Actual $actual.state -Expected $vector.expected.state "Linux state differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.error_code -Expected $vector.expected.error_code
        Assert-Equal -Actual $actual.comm -Expected $vector.expected.comm
        Assert-Equal -Actual $actual.start_ticks -Expected $vector.expected.start_ticks
        Assert-SequenceEqual -Actual @($actual.missing_capabilities) -Expected @($vector.expected.missing_capabilities)
    }
}

Test-Case 'The process-tree schema represents complete partial and unavailable P evidence without weakening state invariants' {
    $accepted = [Collections.Generic.List[string]]::new()
    foreach ($variant in @('complete', 'partial-root', 'partial-unresolved', 'partial-trace', 'unavailable')) {
        $snapshot = New-M0ProcessSnapshotFixture -Variant $variant
        if (Test-Json -Json ($snapshot | ConvertTo-Json -Compress -Depth 30) `
                -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue) {
            $accepted.Add($variant)
        }
    }
    Assert-Equal -Actual $accepted.Count -Expected 5 `
        "Only these process states satisfied the schema: $([string]::Join(',', @($accepted)))."
    Assert-SequenceEqual -Actual @($accepted) -Expected @(
        'complete', 'partial-root', 'partial-unresolved', 'partial-trace', 'unavailable'
    )
}

Test-Case 'Canonical process-tree artifacts are deterministic for every publishable process state' {
    $hashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($variant in @('complete', 'partial-root', 'partial-unresolved', 'partial-trace', 'unavailable')) {
        $snapshot = New-M0ProcessSnapshotFixture -Variant $variant
        $cleanup = New-M0ProcessCleanupResultFixture -Snapshot $snapshot
        $artifact = New-M0ProcessTreeArtifact -Snapshot $snapshot -CleanupResult $cleanup `
            -ExperimentId 'exp-process-states' -RunId "run-P-$variant-01"
        Assert-Equal -Actual @($artifact).Count -Expected 1
        Assert-Equal -Actual $artifact.artifact_id -Expected 'process-tree'
        Assert-True (Test-Json -Json $artifact.canonical_json -SchemaFile $processTreeArtifactSchemaPath `
            -ErrorAction SilentlyContinue) "Artifact '$variant' failed its closed schema."

        $reordered = [pscustomobject][ordered]@{}
        foreach ($property in @($snapshot.PSObject.Properties | Sort-Object Name -Descending)) {
            $reordered | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
        }
        $artifactFromReordered = New-M0ProcessTreeArtifact -Snapshot $reordered -CleanupResult $cleanup `
            -ExperimentId 'exp-process-states' -RunId "run-P-$variant-01"
        Assert-Equal -Actual $artifactFromReordered.canonical_json -Expected $artifact.canonical_json
        Assert-Equal -Actual $artifactFromReordered.sha256 -Expected $artifact.sha256
        Assert-True ($hashes.Add($artifact.sha256)) "Artifact '$variant' collided with another state."
    }
    Assert-Equal -Actual $hashes.Count -Expected 5
}

Test-Case 'A pre-snapshot failure still produces one canonical unavailable process-tree artifact' {
    $unavailableSnapshot = New-M0ProcessSnapshotFixture -Variant unavailable
    $fallbackCleanup = New-M0ProcessCleanupResultFixture -Snapshot $unavailableSnapshot
    $fallback = New-M0ProcessTreeArtifact -UnavailableErrorCode 'safe_identity_unavailable' -CleanupResult $fallbackCleanup `
        -ExperimentId 'exp-process-fallback' -RunId 'run-P-unavailable-01'
    $replay = New-M0ProcessTreeArtifact -UnavailableErrorCode 'safe_identity_unavailable' -CleanupResult $fallbackCleanup `
        -ExperimentId 'exp-process-fallback' -RunId 'run-P-unavailable-01'
    $observedUnavailable = New-M0ProcessTreeArtifact -Snapshot $unavailableSnapshot -CleanupResult $fallbackCleanup `
        -ExperimentId 'exp-process-fallback' -RunId 'run-P-unavailable-01'
    $realCloseCleanup = New-M0ProcessCleanupResultFixture -Snapshot (New-M0ProcessSnapshotFixture -Variant complete)
    $fallbackAfterOpen = New-M0ProcessTreeArtifact -UnavailableErrorCode 'safe_identity_unavailable' `
        -CleanupResult $realCloseCleanup -ExperimentId 'exp-process-fallback' -RunId 'run-P-unavailable-after-open-01'

    Assert-Equal -Actual @($fallback).Count -Expected 1
    Assert-Equal -Actual $fallback.canonical_json -Expected $replay.canonical_json
    Assert-True ($fallback.canonical_json -cne $observedUnavailable.canonical_json) `
        'Fallback and observed unavailable artifacts erased their distinct snapshot origins.'
    Assert-Equal -Actual $fallback.document.snapshot_origin -Expected 'pre_snapshot_fallback'
    Assert-Equal -Actual $observedUnavailable.document.snapshot_origin -Expected 'observed'
    Assert-Equal -Actual $fallbackAfterOpen.document.snapshot_origin -Expected 'pre_snapshot_fallback'
    Assert-Equal -Actual $fallbackAfterOpen.document.snapshot.retained_identity_count -Expected 0
    Assert-Equal -Actual $fallbackAfterOpen.document.cleanup.retained_identity_count -Expected 3
    Assert-Equal -Actual $fallbackAfterOpen.document.cleanup.attempted -Expected $true
    Assert-Equal -Actual $fallback.document.snapshot.state -Expected 'unavailable'
    Assert-Equal -Actual $fallback.document.snapshot.error_code -Expected 'safe_identity_unavailable'
    Assert-Equal -Actual @($fallback.document.snapshot.roles).Count -Expected 0
    Assert-Equal -Actual @($fallback.document.snapshot.nodes).Count -Expected 0
    Assert-Equal -Actual @($fallback.document.snapshot.auxiliary_job_members).Count -Expected 0
    Assert-Equal -Actual $fallback.document.snapshot.retained_identity_count -Expected 0
    Assert-Equal -Actual $fallback.document.snapshot.cleanup_authority -Expected 'none-no-mutation-capability'
}

Test-Case 'Process-tree state contradictions topology duplicates and QPC drift fail closed' {
    function Copy-SnapshotFixture([string] $Variant) {
        return (New-M0ProcessSnapshotFixture -Variant $Variant | ConvertTo-Json -Compress -Depth 30 | ConvertFrom-Json)
    }

    $completeWithError = Copy-SnapshotFixture complete
    $completeWithError.error_code = 'etw_trace_incomplete'
    Assert-True (-not (Test-Json -Json ($completeWithError | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $unresolvedWithoutRoot = Copy-SnapshotFixture partial-unresolved
    $unresolvedWithoutRoot.roles = @()
    $unresolvedWithoutRoot.nodes = @()
    $unresolvedWithoutRoot.retained_identity_count = 0
    Assert-True (-not (Test-Json -Json ($unresolvedWithoutRoot | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue)) `
        'Schema accepted unresolved external attribution without a proven root identity.'
    $unresolvedCleanup = New-M0ProcessCleanupResultFixture -Snapshot $unresolvedWithoutRoot
    Assert-Throws {
        New-M0ProcessTreeArtifact -Snapshot $unresolvedWithoutRoot -CleanupResult $unresolvedCleanup `
            -ExperimentId 'exp-process-invalid' -RunId 'run-P-unresolved-without-root-01'
    } 'M0P060|schema|external|root|topology'

    $completeMissingCapability = Copy-SnapshotFixture complete
    $completeMissingCapability.missing_capabilities = @('etw-process-start-stop')
    Assert-True (-not (Test-Json -Json ($completeMissingCapability | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $completeWithPartialTrace = Copy-SnapshotFixture complete
    $completeWithPartialTrace.trace = (Copy-SnapshotFixture partial-trace).trace
    Assert-True (-not (Test-Json -Json ($completeWithPartialTrace | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $partialWithoutCause = Copy-SnapshotFixture partial-trace
    $partialWithoutCause.error_code = $null
    $partialWithoutCause.missing_capabilities = @()
    Assert-True (-not (Test-Json -Json ($partialWithoutCause | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $liveWithoutTraceGap = Copy-SnapshotFixture partial-trace
    $liveWithoutTraceGap.error_code = 'etw_live_not_implemented'
    $liveWithoutTraceGap.trace = (Copy-SnapshotFixture complete).trace
    $liveWithoutTraceGap.capabilities.etw_process_start_stop = $true
    Assert-True (-not (Test-Json -Json ($liveWithoutTraceGap | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $traceGapWithEtwCapability = Copy-SnapshotFixture partial-trace
    $traceGapWithEtwCapability.capabilities.etw_process_start_stop = $true
    Assert-True (-not (Test-Json -Json ($traceGapWithEtwCapability | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $missingRootWithTree = Copy-SnapshotFixture complete
    $missingRootWithTree.state = 'partial'
    $missingRootWithTree.error_code = 'root_identity_missing'
    $missingRootWithTree.missing_capabilities = @('root-handle')
    Assert-True (-not (Test-Json -Json ($missingRootWithTree | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $emptyWithMutationAuthority = Copy-SnapshotFixture partial-root
    $emptyWithMutationAuthority.cleanup_authority = 'private-opaque-registry-only'
    $emptyWithMutationAuthority.capabilities.job_object_kill_on_close = $true
    Assert-True (-not (Test-Json -Json ($emptyWithMutationAuthority | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $unavailableWithNode = Copy-SnapshotFixture unavailable
    $unavailableWithNode.roles = @('root')
    $unavailableWithNode.nodes = @((Copy-SnapshotFixture complete).nodes[0])
    $unavailableWithNode.job_member_hint_count = 1
    $unavailableWithNode.retained_identity_count = 1
    Assert-True (-not (Test-Json -Json ($unavailableWithNode | ConvertTo-Json -Compress -Depth 30) `
        -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue))

    $roleMismatch = Copy-SnapshotFixture partial-unresolved
    $roleMismatch.roles = @('root', 'child')
    $roleMismatchCleanup = New-M0ProcessCleanupResultFixture -Snapshot $roleMismatch
    Assert-Throws {
        New-M0ProcessTreeArtifact -Snapshot $roleMismatch -CleanupResult $roleMismatchCleanup -ExperimentId 'exp-process-invalid' `
            -RunId 'run-P-role-mismatch-01'
    } 'M0P060|role'

    $duplicate = Copy-SnapshotFixture partial-trace
    $duplicate.nodes[1].identity = $duplicate.nodes[0].identity
    $duplicateCleanup = New-M0ProcessCleanupResultFixture -Snapshot $duplicate
    Assert-Throws {
        New-M0ProcessTreeArtifact -Snapshot $duplicate -CleanupResult $duplicateCleanup -ExperimentId 'exp-process-invalid' `
            -RunId 'run-P-duplicate-01'
    } 'M0P060|duplicate identity'

    $noncausal = Copy-SnapshotFixture partial-trace
    $noncausal.qpc_after_resume_ticks = $noncausal.qpc_before_create_ticks
    $noncausalCleanup = New-M0ProcessCleanupResultFixture -Snapshot $noncausal
    Assert-Throws {
        New-M0ProcessTreeArtifact -Snapshot $noncausal -CleanupResult $noncausalCleanup -ExperimentId 'exp-process-invalid' `
            -RunId 'run-P-qpc-01'
    } 'M0P060|QPC'
}

Test-Case 'Contradictory process error branches cannot mint a process-tree artifact' {
    function Copy-BranchFixture([string] $Variant) {
        return (New-M0ProcessSnapshotFixture -Variant $Variant | ConvertTo-Json -Compress -Depth 30 | ConvertFrom-Json)
    }
    $liveTrace = [pscustomobject][ordered]@{
        state = 'partial'; adapter = 'etw-process-start-stop'; complete = $false; collected = $false
        gap_codes = @('not-collected'); error_code = 'etw_live_not_implemented'
    }
    $vectors = [Collections.Generic.List[object]]::new()

    $value = Copy-BranchFixture partial-trace
    $value.error_code = 'etw_live_not_implemented'; $value.trace = (Copy-BranchFixture complete).trace
    $value.capabilities.etw_process_start_stop = $true
    $vectors.Add([pscustomobject]@{ id = 'live-with-complete-trace'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.capabilities.etw_process_start_stop = $true
    $vectors.Add([pscustomobject]@{ id = 'trace-gap-with-etw-capability'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.trace = (Copy-BranchFixture complete).trace
    $vectors.Add([pscustomobject]@{ id = 'trace-gap-error-with-complete-trace'; value = $value })

    $value = Copy-BranchFixture complete
    $value.state = 'partial'; $value.error_code = 'root_identity_missing'; $value.missing_capabilities = @('root-handle')
    $vectors.Add([pscustomobject]@{ id = 'missing-root-with-complete-tree'; value = $value })

    $value = Copy-BranchFixture partial-root
    $value.cleanup_authority = 'private-opaque-registry-only'; $value.capabilities.job_object_kill_on_close = $true
    $vectors.Add([pscustomobject]@{ id = 'empty-tree-with-mutation-authority'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.error_code = 'etw_live_not_implemented'; $value.trace = $liveTrace
    $value.missing_capabilities = @('job-completion-port')
    $vectors.Add([pscustomobject]@{ id = 'live-without-etw-missing-capability'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.error_code = 'etw_live_not_implemented'; $value.trace = $liveTrace
    $value.missing_capabilities = @('etw-process-start-stop', 'safe-process-identity')
    $vectors.Add([pscustomobject]@{ id = 'live-tree-with-safe-identity-missing'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.error_code = 'etw_live_not_implemented'; $value.trace = $liveTrace
    $value.missing_capabilities = @('etw-process-start-stop', 'root-handle')
    $vectors.Add([pscustomobject]@{ id = 'live-tree-with-root-handle-missing'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.error_code = 'etw_live_not_implemented'; $value.trace = $liveTrace
    $value.missing_capabilities = @('etw-process-start-stop', 'external-attribution-proof')
    $vectors.Add([pscustomobject]@{ id = 'live-tree-with-unresolved-external-proof'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.missing_capabilities = @('root-handle')
    $vectors.Add([pscustomobject]@{ id = 'trace-gap-without-etw-missing-capability'; value = $value })

    $value = Copy-BranchFixture partial-root
    $value.missing_capabilities = @('external-attribution-proof')
    $vectors.Add([pscustomobject]@{ id = 'root-error-without-root-missing-capability'; value = $value })

    $value = Copy-BranchFixture partial-unresolved
    $value.missing_capabilities = @('root-handle')
    $vectors.Add([pscustomobject]@{ id = 'external-error-without-proof-missing-capability'; value = $value })

    $value = Copy-BranchFixture partial-root
    $value.trace = (Copy-BranchFixture partial-trace).trace
    $value.capabilities.etw_process_start_stop = $true
    $vectors.Add([pscustomobject]@{ id = 'root-error-with-unacknowledged-secondary-trace-gap'; value = $value })

    $value = Copy-BranchFixture partial-unresolved
    $value.trace = (Copy-BranchFixture partial-trace).trace
    $value.capabilities.etw_process_start_stop = $true
    $vectors.Add([pscustomobject]@{ id = 'external-error-with-unacknowledged-secondary-trace-gap'; value = $value })

    $value = Copy-BranchFixture partial-root
    $value.capabilities.etw_process_start_stop = $false
    $vectors.Add([pscustomobject]@{ id = 'complete-trace-without-etw-capability'; value = $value })

    $value = Copy-BranchFixture partial-unresolved
    $value.missing_capabilities = @('etw-process-start-stop', 'external-attribution-proof')
    $vectors.Add([pscustomobject]@{ id = 'complete-trace-with-etw-listed-missing'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.missing_capabilities = @('etw-process-start-stop', 'job-completion-port')
    $value.capabilities.completion_port = $true
    $vectors.Add([pscustomobject]@{ id = 'missing-completion-port-with-live-capability'; value = $value })

    $value = Copy-BranchFixture partial-trace
    $value.capabilities.completion_port = $false
    $vectors.Add([pscustomobject]@{ id = 'absent-completion-port-without-missing-capability'; value = $value })

    $value = Copy-BranchFixture unavailable
    $value.missing_capabilities = @('etw-process-start-stop')
    $vectors.Add([pscustomobject]@{ id = 'safe-identity-error-without-identity-missing-capability'; value = $value })

    $value = Copy-BranchFixture unavailable
    $value.error_code = 'etw_trace_unavailable'; $value.missing_capabilities = @('safe-process-identity')
    $vectors.Add([pscustomobject]@{ id = 'etw-unavailable-without-etw-missing-capability'; value = $value })

    foreach ($vector in $vectors) {
        $json = $vector.value | ConvertTo-Json -Compress -Depth 30
        Assert-True (-not (Test-Json -Json $json -SchemaFile $processTreeSchemaPath `
            -ErrorAction SilentlyContinue)) "Schema accepted contradictory branch '$($vector.id)'."
        $cleanup = New-M0ProcessCleanupResultFixture -Snapshot $vector.value
        Assert-Throws {
            New-M0ProcessTreeArtifact -Snapshot $vector.value -CleanupResult $cleanup `
                -ExperimentId 'exp-process-branch' -RunId "run-P-$($vector.id)-01"
        } 'closed synthetic P process-tree snapshot'
    }
}

Test-Case 'Future synthetic-pilot outcome mapping is closed and respects cleanup precedence' {
    $expectedEvidencePath = [IO.Path]::GetFullPath($evidenceModulePath)
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $namedEvidenceModules = @(Get-Module -All -Name M0.Evidence)
    if (@($namedEvidenceModules | Where-Object {
        -not [IO.Path]::GetFullPath($_.Path).Equals($expectedEvidencePath, $pathComparison)
    }).Count -ne 0) {
        throw [IO.InvalidDataException]::new('M0P062 homonymous M0.Evidence module is outside the trusted repository path.')
    }
    $exactEvidenceModules = @($namedEvidenceModules | Where-Object {
        [IO.Path]::GetFullPath($_.Path).Equals($expectedEvidencePath, $pathComparison)
    })
    if ($exactEvidenceModules.Count -gt 1) {
        throw [IO.InvalidDataException]::new('M0P062 more than one exact-path M0.Evidence module is loaded.')
    }
    $evidenceModule = if ($exactEvidenceModules.Count -eq 1) {
        $exactEvidenceModules[0]
    }
    else {
        Import-Module $expectedEvidencePath -PassThru -ErrorAction Stop
    }
    $resolvePrivateFields = {
        param($state, $processError, $cleanupComplete, $survivorCount, $cleanupError)
        & $processModule {
            param($innerState, $innerProcessError, $innerCleanupComplete, $innerSurvivorCount, $innerCleanupError)
            Resolve-M0ProcessPilotOutcomeFields -ProcessState $innerState -ProcessErrorCode $innerProcessError `
                -CleanupComplete $innerCleanupComplete -SurvivorCount $innerSurvivorCount `
                -CleanupErrorCode $innerCleanupError
        } $state $processError $cleanupComplete $survivorCount $cleanupError
    }
    $matrix = @(
        [pscustomobject]@{ state = 'complete'; process_error = $null; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'passed'; check_reason = $null; run = 'valid'; reason = $null; run_error = $null },
        [pscustomobject]@{ state = 'partial'; process_error = 'etw_live_not_implemented'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_partial'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'partial'; process_error = 'etw_trace_incomplete'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_partial'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'partial'; process_error = 'unresolved_external_attribution'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_partial'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'partial'; process_error = 'root_identity_missing'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_partial'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'unavailable'; process_error = 'safe_identity_unavailable'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_unavailable'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'unavailable'; process_error = 'etw_trace_unavailable'; cleanup = $true; survivors = 0; cleanup_error = $null; check = 'skipped'; check_reason = 'process_tree_unavailable'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'complete'; process_error = $null; cleanup = $false; survivors = 0; cleanup_error = 'identity_revalidation_failed'; check = 'skipped'; check_reason = 'identity_revalidation_failed'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'complete'; process_error = $null; cleanup = $false; survivors = 0; cleanup_error = 'snapshot_validation_failed'; check = 'skipped'; check_reason = 'snapshot_validation_failed'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'complete'; process_error = $null; cleanup = $false; survivors = 0; cleanup_error = 'unsafe_run_root_contents'; check = 'skipped'; check_reason = 'unsafe_run_root_contents'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'complete'; process_error = $null; cleanup = $false; survivors = 0; cleanup_error = 'job_close_failed'; check = 'skipped'; check_reason = 'job_close_failed'; run = 'inconclusive'; reason = 'incomplete_evidence'; run_error = $null },
        [pscustomobject]@{ state = 'partial'; process_error = 'etw_live_not_implemented'; cleanup = $false; survivors = 1; cleanup_error = 'helper_survived_shutdown'; check = 'failed'; check_reason = 'helper_survived_shutdown'; run = 'failed'; reason = 'helper_survived_shutdown'; run_error = 'helper_survived_shutdown' }
    )
    foreach ($case in $matrix) {
        $actual = & $resolvePrivateFields $case.state $case.process_error $case.cleanup $case.survivors $case.cleanup_error
        Assert-Equal -Actual $actual.candidate_code -Expected 'P'
        Assert-Equal -Actual $actual.scenario_id -Expected 'synthetic-process-tree'
        Assert-Equal -Actual $actual.attempt -Expected 1
        Assert-Equal -Actual $actual.check_payload.status -Expected $case.check
        Assert-Equal -Actual $actual.check_payload.reason_code -Expected $case.check_reason
        Assert-Equal -Actual $actual.run_status -Expected $case.run
        Assert-Equal -Actual $actual.run_status_reason -Expected $case.reason
        Assert-Equal -Actual $actual.run_error_code -Expected $case.run_error
        if ($case.check -ceq 'skipped') {
            Assert-Equal -Actual @($actual.checks_omitted).Count -Expected 1
            Assert-Equal -Actual $actual.checks_omitted[0].reason_code -Expected $case.check_reason
        }
        else {
            Assert-Equal -Actual @($actual.checks_omitted).Count -Expected 0
        }
        $null = & $evidenceModule {
            param($checkPayload, $runStatus, $runStatusReason, $runErrorCode)
            $events = @([pscustomobject][ordered]@{
                event = 'check_recorded'
                event_seq = 1
                payload = $checkPayload
            })
            Assert-M0EvidenceDerivedStatus -Events $events -Status $runStatus `
                -StatusReason $runStatusReason -ErrorCode $runErrorCode
        } $actual.check_payload $actual.run_status $actual.run_status_reason $actual.run_error_code
    }
    Assert-Throws {
        & $resolvePrivateFields complete $null $false 0 $null
    } 'M0P06|cleanup|error'
    Assert-Throws {
        & $resolvePrivateFields complete $null $false 0 caller_supplied_reason
    } 'M0P061|closed cleanup error'
    Assert-Throws {
        & $resolvePrivateFields complete $null $false 0 helper_survived_shutdown
    } 'M0P061|survivor'
    Assert-Throws {
        & $resolvePrivateFields complete $null $false 0 retained_handle_wait_failed
    } 'M0P061|survivor'
    Assert-Throws {
        & $resolvePrivateFields partial caller_supplied_reason $true 0 $null
    } 'M0P061|closed process error'
    Assert-Throws {
        & $resolvePrivateFields partial Etw_Trace_Incomplete $true 0 $null
    } 'M0P061|closed process error'
    Assert-Throws {
        & $resolvePrivateFields Partial etw_live_not_implemented $true 0 $null
    } 'M0P061|canonical|process state'
}

Test-Case 'Windows synthetic Job smoke keeps opaque authority and retains every Job member before one scoped close' {
    if (-not $IsWindows) {
        Assert-Throws { Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot } 'M0P04|Windows|platform'
        return
    }

    $pilot = $null
    try {
        $pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
        $snapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        Assert-Equal -Actual $snapshot.'$schema' -Expected 'schemas/process-tree.schema.json'
        Assert-True (Test-Json -Json ($snapshot | ConvertTo-Json -Compress -Depth 30) `
            -SchemaFile $processTreeSchemaPath -ErrorAction SilentlyContinue) `
            'The public process-tree snapshot must satisfy its closed artifact schema.'
        Assert-Equal -Actual $snapshot.candidate_code -Expected 'P'
        Assert-Equal -Actual $snapshot.scenario_id -Expected 'synthetic-process-tree'
        Assert-Equal -Actual $snapshot.attempt -Expected 1
        Assert-Equal -Actual $snapshot.evidence_role -Expected 'synthetic_pilot'
        Assert-Equal -Actual $snapshot.decision_eligible -Expected $false
        Assert-Equal -Actual $snapshot.state -Expected 'partial'
        Assert-Equal -Actual $snapshot.error_code -Expected 'etw_live_not_implemented'
        Assert-SequenceEqual -Actual @($snapshot.roles) -Expected @('root', 'child', 'grandchild')
        Assert-Equal -Actual $snapshot.nodes.Count -Expected 3
        Assert-True ($snapshot.job_member_hint_count -ge 3)
        Assert-Equal -Actual $snapshot.retained_identity_count -Expected $snapshot.job_member_hint_count
        Assert-Equal -Actual $snapshot.auxiliary_job_members.Count -Expected ($snapshot.job_member_hint_count - 3)
        Assert-True (@($snapshot.nodes | Where-Object { -not $_.job_membership_revalidated }).Count -eq 0) `
            'Every Job PID hint must be converted to a retained handle and revalidated with IsProcessInJob.'
        Assert-True (@($snapshot.auxiliary_job_members | Where-Object { -not $_.job_membership_revalidated }).Count -eq 0) `
            'Every auxiliary Job PID hint must also be retained and revalidated instead of being ignored.'
        $rootNode = @($snapshot.nodes | Where-Object role -CEQ 'root')
        $childNode = @($snapshot.nodes | Where-Object role -CEQ 'child')
        $grandchildNode = @($snapshot.nodes | Where-Object role -CEQ 'grandchild')
        Assert-Equal -Actual $rootNode.Count -Expected 1
        Assert-Equal -Actual $childNode.Count -Expected 1
        Assert-Equal -Actual $grandchildNode.Count -Expected 1
        Assert-Equal -Actual $rootNode[0].parent_identity -Expected $null
        Assert-Equal -Actual $childNode[0].parent_identity.pid -Expected $rootNode[0].identity.pid
        Assert-Equal -Actual $childNode[0].parent_identity.creation_time_filetime_utc `
            -Expected $rootNode[0].identity.creation_time_filetime_utc
        Assert-Equal -Actual $grandchildNode[0].parent_identity.pid -Expected $childNode[0].identity.pid
        Assert-Equal -Actual $grandchildNode[0].parent_identity.creation_time_filetime_utc `
            -Expected $childNode[0].identity.creation_time_filetime_utc
        $identityPairs = @($snapshot.nodes | ForEach-Object {
                "$($_.identity.pid)|$($_.identity.creation_time_filetime_utc)"
            })
        Assert-Equal -Actual @($identityPairs | Sort-Object -Unique).Count -Expected 3
        $births = @($snapshot.nodes | ForEach-Object { $_.identity.creation_time_filetime_utc })
        Assert-True (@($births | Where-Object { [uint64]$_ -eq 0 }).Count -eq 0) 'Birth tokens must be positive.'
        Assert-True ([uint64]$snapshot.qpc_after_resume_ticks -gt [uint64]$snapshot.qpc_before_create_ticks)

        $clone = [pscustomobject][ordered]@{}
        foreach ($property in $pilot.PSObject.Properties) {
            $clone | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
        }
        Assert-Throws { Get-M0SyntheticProcessPilotSnapshot -Pilot $clone } 'M0P04|opaque|capability|token'
        Assert-Throws { Close-M0SyntheticProcessPilot -Pilot $clone } 'M0P04|opaque|capability|token'
        $afterClone = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        Assert-Equal -Actual $afterClone.nodes.Count -Expected 3

        $snapshot.state = 'complete'
        $freshSnapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        Assert-Equal -Actual $freshSnapshot.state -Expected 'partial' `
            'A caller mutation must not alter the private snapshot or later reads.'
        $snapshotJson = $freshSnapshot | ConvertTo-Json -Compress -Depth 20
        Assert-True (-not $snapshotJson.Contains($RepositoryRoot, [StringComparison]::OrdinalIgnoreCase))
        Assert-True (-not $snapshotJson.Contains([Environment]::UserName, [StringComparison]::OrdinalIgnoreCase))
        Assert-True (-not $snapshotJson.Contains([string]$pilot.opaque_id, [StringComparison]::Ordinal))
        Assert-True (-not $snapshotJson.Contains([string]$pilot.module_instance_id, [StringComparison]::Ordinal))

        $firstClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-True $firstClose.cleanup_complete
        Assert-Equal -Actual $firstClose.survivor_count -Expected 0
        Assert-Equal -Actual $firstClose.native_cleanup_actions -Expected 1
        Assert-Equal -Actual $firstClose.individual_terminate_process_calls -Expected 0
        Assert-Equal -Actual $firstClose.candidate_code -Expected 'P'
        Assert-Equal -Actual $firstClose.scenario_id -Expected 'synthetic-process-tree'
        Assert-Equal -Actual $firstClose.attempt -Expected 1
        $firstClose.survivor_count = 999
        $secondClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual $secondClose.survivor_count -Expected 0 `
            'A caller mutation must not alter the private close result or idempotent replay.'
        Assert-Equal -Actual $secondClose.native_cleanup_actions -Expected 1
        $closeJson = $secondClose | ConvertTo-Json -Compress -Depth 20
        Assert-True (-not $closeJson.Contains($RepositoryRoot, [StringComparison]::OrdinalIgnoreCase))
        Assert-True (-not $closeJson.Contains([Environment]::UserName, [StringComparison]::OrdinalIgnoreCase))
        Assert-True (-not $closeJson.Contains([string]$pilot.opaque_id, [StringComparison]::Ordinal))
        Assert-True (-not $closeJson.Contains([string]$pilot.module_instance_id, [StringComparison]::Ordinal))

        # New-M0ProcessTreeArtifact is a pure builder over prevalidated data. The live
        # orchestration proof is the explicit Close above; the Pilot publisher must
        # preserve this order and never accept caller-supplied cleanup data.
        $artifact = New-M0ProcessTreeArtifact -Snapshot $freshSnapshot -CleanupResult $secondClose `
            -ExperimentId 'exp-process-smoke' -RunId 'run-P-synthetic-process-tree-01'
        $artifactReplay = New-M0ProcessTreeArtifact -Snapshot $freshSnapshot -CleanupResult $secondClose `
            -ExperimentId 'exp-process-smoke' -RunId 'run-P-synthetic-process-tree-01'
        $reorderedSnapshot = [pscustomobject][ordered]@{}
        foreach ($property in @($freshSnapshot.PSObject.Properties | Sort-Object Name -Descending)) {
            $reorderedSnapshot | Add-Member -NotePropertyName $property.Name -NotePropertyValue $property.Value
        }
        $artifactFromReorderedSnapshot = New-M0ProcessTreeArtifact -Snapshot $reorderedSnapshot `
            -CleanupResult $secondClose `
            -ExperimentId 'exp-process-smoke' -RunId 'run-P-synthetic-process-tree-01'
        Assert-Equal -Actual $artifact.canonical_json -Expected $artifactReplay.canonical_json
        Assert-Equal -Actual $artifact.canonical_json -Expected $artifactFromReorderedSnapshot.canonical_json `
            'Schema-equivalent property order must not change canonical artifact bytes.'
        Assert-Equal -Actual $artifact.sha256 -Expected $artifactReplay.sha256
        Assert-Equal -Actual $artifact.size_bytes -Expected $artifactReplay.size_bytes
        $artifactBytes = [Text.UTF8Encoding]::new($false).GetBytes($artifact.canonical_json)
        Assert-Equal -Actual $artifactBytes.Length -Expected $artifact.size_bytes
        Assert-Equal -Actual ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($artifactBytes)).ToLowerInvariant()) `
            -Expected $artifact.sha256
        Assert-True (Test-Json -Json $artifact.canonical_json -SchemaFile $processTreeArtifactSchemaPath `
            -ErrorAction SilentlyContinue) 'Bound process-tree artifact must satisfy its closed schema.'
        Assert-Equal -Actual $artifact.document.experiment_id -Expected 'exp-process-smoke'
        Assert-Equal -Actual $artifact.document.run_id -Expected 'run-P-synthetic-process-tree-01'
        Assert-Equal -Actual $artifact.document.candidate_code -Expected 'P'
        Assert-Equal -Actual $artifact.document.evidence_role -Expected 'synthetic_pilot'
        Assert-Equal -Actual $artifact.document.decision_eligible -Expected $false
        Assert-Equal -Actual $artifact.relative_path -Expected 'artifacts/run-P-synthetic-process-tree-01.process-tree.json'

        $outcome = Resolve-M0ProcessPilotOutcome -ProcessArtifact $artifact.document
        $commonInputs = [ordered]@{
            protocol_sha256 = 'a' * 64; environment_sha256 = 'b' * 64; fixture_sha256 = 'c' * 64
            harness_sha256 = 'd' * 64; scenario_sha256 = 'e' * 64
        }
        $artifactEvent = [ordered]@{
            schema_id = 'm0.event.v1'; schema_version = 1
            protocol_sha256 = $commonInputs.protocol_sha256; environment_sha256 = $commonInputs.environment_sha256
            fixture_sha256 = $commonInputs.fixture_sha256; harness_sha256 = $commonInputs.harness_sha256
            scenario_sha256 = $commonInputs.scenario_sha256; experiment_id = $artifact.document.experiment_id
            run_id = $artifact.document.run_id; candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            event_seq = 2; previous_line_sha256 = 'f' * 64; utc = '2026-08-24T12:00:00.0000000Z'
            qpc_ticks = 2; qpc_frequency = 10000000; event = 'artifact_recorded'
            payload = [ordered]@{
                artifact_id = $artifact.artifact_id; relative_path = $artifact.relative_path
                sha256 = $artifact.sha256; size_bytes = $artifact.size_bytes; transient = $false; redacted = $true
            }
        }
        Assert-True (Test-Json -Json ($artifactEvent | ConvertTo-Json -Compress -Depth 30) `
            -SchemaFile (Join-Path $protocolRoot 'schemas\event.schema.json') -ErrorAction SilentlyContinue)
        $futureRun = [ordered]@{
            schema_id = 'm0.run.v1'; schema_version = 1; experiment_id = $artifact.document.experiment_id
            run_id = $artifact.document.run_id; candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            common_inputs = $commonInputs; candidate_input = $null; schedule = $null
            status = $outcome.run_status; status_reason = $outcome.run_status_reason; error_code = $outcome.run_error_code
            raw = [ordered]@{ relative_path = 'run-P-synthetic-process-tree-01.jsonl'; event_count = 4; final_line_sha256 = '1' * 64; sealed_utc = '2026-08-24T12:00:01.0000000Z' }
            markers = @('run-started'); process_tree_sha256 = $artifact.sha256
            metrics = @(); actions = @(); external_noise = @()
            artifacts = @([ordered]@{ artifact_id = $artifact.artifact_id; relative_path = $artifact.relative_path; sha256 = $artifact.sha256; size_bytes = $artifact.size_bytes })
            checks_omitted = @($outcome.checks_omitted)
        }
        Assert-True (Test-Json -Json ($futureRun | ConvertTo-Json -Compress -Depth 30) `
            -SchemaFile (Join-Path $protocolRoot 'schemas\run.schema.json') -ErrorAction SilentlyContinue) `
            'Future P run shape must remain compatible with exactly one bound process-tree artifact.'

    }
    finally {
        if ($null -ne $pilot) {
            try { $null = Close-M0SyntheticProcessPilot -Pilot $pilot } catch { }
        }
    }
}

Test-Case 'Synthetic pilot cleanup fails closed and replays the failure when its exact run leaf has unexpected content' {
    if (-not $IsWindows) { return }

    $pilot = $null
    $runRoot = $null
    $unexpectedPath = $null
    try {
        $pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
        $null = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        $runRoot = & $processModule { param($value) (Get-M0ProcessPilotState -Pilot $value).run_root } $pilot
        $unexpectedPath = Join-Path $runRoot 'unexpected.bin'
        [IO.File]::WriteAllBytes($unexpectedPath, [byte[]](1, 2, 3))

        $firstClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual $firstClose.cleanup_complete -Expected $false
        Assert-Equal -Actual $firstClose.error_code -Expected 'unsafe_run_root_contents'
        Assert-Equal -Actual $firstClose.survivor_count -Expected 0
        Assert-Equal -Actual $firstClose.native_cleanup_actions -Expected 1
        Assert-True (Test-Path -LiteralPath $unexpectedPath -PathType Leaf) `
            'Cleanup must preserve and report an unexpected file instead of deleting the run leaf recursively.'

        $secondClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual ($secondClose | ConvertTo-Json -Compress -Depth 20) `
            -Expected ($firstClose | ConvertTo-Json -Compress -Depth 20)
    }
    finally {
        if ($null -ne $pilot) {
            try { $null = Close-M0SyntheticProcessPilot -Pilot $pilot } catch { }
        }
        if ($null -ne $unexpectedPath -and (Test-Path -LiteralPath $unexpectedPath -PathType Leaf)) {
            Remove-Item -LiteralPath $unexpectedPath -Force
        }
        if ($null -ne $runRoot -and (Test-Path -LiteralPath $runRoot -PathType Container)) {
            $remaining = @(Get-ChildItem -LiteralPath $runRoot -Force)
            if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $runRoot -Force }
        }
    }
}

Test-Case 'Synthetic pilot cleanup reports a missing or renamed run leaf instead of claiming completion' {
    if (-not $IsWindows) { return }

    $pilot = $null
    try {
        $pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
        $null = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        $runRoot = & $processModule { param($value) (Get-M0ProcessPilotState -Pilot $value).run_root } $pilot
        foreach ($markerName in @('root.ready.json', 'child.ready.json', 'grandchild.ready.json')) {
            Remove-Item -LiteralPath (Join-Path $runRoot $markerName) -Force
        }
        Remove-Item -LiteralPath $runRoot -Force

        $closed = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual $closed.cleanup_complete -Expected $false
        Assert-Equal -Actual $closed.error_code -Expected 'unsafe_run_root_contents'
        Assert-Equal -Actual $closed.survivor_count -Expected 0
        Assert-Equal -Actual $closed.native_cleanup_actions -Expected 1
    }
    finally {
        if ($null -ne $pilot) {
            try { $null = Close-M0SyntheticProcessPilot -Pilot $pilot } catch { }
        }
    }
}

Test-Case 'A retained helper that exits naturally after snapshot is already-exited during scoped idempotent Close' {
    if (-not $IsWindows) { return }

    $pilot = $null
    $naturalExitProcess = $null
    $gatePath = $null
    try {
        $pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
        $snapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
        $grandchild = @($snapshot.nodes | Where-Object role -CEQ 'grandchild')[0]
        $naturalExitProcess = [Diagnostics.Process]::GetProcessById([int]$grandchild.identity.pid)
        $birth = $naturalExitProcess.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
        Assert-Equal -Actual $birth -Expected $grandchild.identity.creation_time_filetime_utc
        $runRoot = & $processModule { param($value) (Get-M0ProcessPilotState -Pilot $value).run_root } $pilot
        $gatePath = Join-Path $runRoot 'grandchild.exit'
        [IO.File]::WriteAllBytes($gatePath, [byte[]](1))
        Assert-True ($naturalExitProcess.WaitForExit(5000)) `
            'The retained grandchild did not take its natural-exit gate before Close.'
        Remove-Item -LiteralPath $gatePath -Force
        $gatePath = $null

        $firstClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual $firstClose.cleanup_complete -Expected $true
        Assert-Equal -Actual $firstClose.survivor_count -Expected 0
        Assert-Equal -Actual $firstClose.native_cleanup_actions -Expected 1
        $secondClose = Close-M0SyntheticProcessPilot -Pilot $pilot
        Assert-Equal -Actual ($secondClose | ConvertTo-Json -Compress -Depth 20) `
            -Expected ($firstClose | ConvertTo-Json -Compress -Depth 20)
    }
    finally {
        if ($null -ne $gatePath -and (Test-Path -LiteralPath $gatePath -PathType Leaf)) {
            Remove-Item -LiteralPath $gatePath -Force
        }
        if ($null -ne $pilot) {
            try { $null = Close-M0SyntheticProcessPilot -Pilot $pilot } catch { }
        }
        if ($null -ne $naturalExitProcess) { $naturalExitProcess.Dispose() }
    }
}

Test-Case 'Removing the owner module and abrupt owner exit both close the last Job handle without killing an unrelated process' {
    if (-not $IsWindows) { return }

    $ownerScript = Join-Path $TestTempRoot 'Invoke-ProcessPilotOwner.ps1'
    $sentinelScript = Join-Path $TestTempRoot 'Invoke-UnrelatedSentinel.ps1'
    $ownerSource = @'
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ModulePath,
    [Parameter(Mandatory)] [string] $RepositoryRoot,
    [Parameter(Mandatory)] [string] $SnapshotPath,
    [Parameter(Mandatory)] [string] $GatePath,
    [Parameter(Mandatory)] [string] $AfterRemovePath,
    [Parameter(Mandatory)] [string] $FinishPath,
    [Parameter(Mandatory)] [ValidateSet('remove-module', 'abrupt-exit')] [string] $Mode
)
$ErrorActionPreference = 'Stop'
$module = Import-Module -Name $ModulePath -Force -PassThru -ErrorAction Stop
$pilot = Open-M0SyntheticProcessPilot -RepositoryRoot $RepositoryRoot
$snapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot $pilot
$temporarySnapshotPath = "$SnapshotPath.tmp"
[IO.File]::WriteAllText($temporarySnapshotPath, ($snapshot | ConvertTo-Json -Compress -Depth 20) + "`n", [Text.UTF8Encoding]::new($false))
[IO.File]::Move($temporarySnapshotPath, $SnapshotPath, $false)
while (-not (Test-Path -LiteralPath $GatePath -PathType Leaf)) { [Threading.Thread]::Sleep(20) }
if ($Mode -ceq 'abrupt-exit') { [Environment]::Exit(23) }
Remove-Module -ModuleInfo $module -Force
[IO.File]::WriteAllBytes($AfterRemovePath, [byte[]](1))
while (-not (Test-Path -LiteralPath $FinishPath -PathType Leaf)) { [Threading.Thread]::Sleep(20) }
'@
    [IO.File]::WriteAllText($ownerScript, $ownerSource.TrimStart() + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($sentinelScript, "while (`$true) { [Threading.Thread]::Sleep(1000) }`n", [Text.UTF8Encoding]::new($false))

    $sentinelInfo = [Diagnostics.ProcessStartInfo]::new()
    $sentinelInfo.FileName = (Get-Process -Id $PID).Path
    $sentinelInfo.UseShellExecute = $false
    $sentinelInfo.CreateNoWindow = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $sentinelScript)) {
        $null = $sentinelInfo.ArgumentList.Add($argument)
    }
    $sentinel = [Diagnostics.Process]::Start($sentinelInfo)
    try {
        Start-Sleep -Milliseconds 250
        Assert-True (-not $sentinel.HasExited) 'The unrelated sentinel must be alive before either Job owner closes.'
        foreach ($mode in @('remove-module', 'abrupt-exit')) {
            $owner = $null
            $retainedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()
            $modeRunRoot = $null
            $pilotParent = Join-Path $RepositoryRoot 'out\m0\process-pilot'
            $existingRunRoots = @(
                if (Test-Path -LiteralPath $pilotParent -PathType Container) {
                    Get-ChildItem -LiteralPath $pilotParent -Directory -Force | Select-Object -ExpandProperty FullName
                }
            )
            $snapshotPath = Join-Path $TestTempRoot "$mode.snapshot.json"
            $gatePath = Join-Path $TestTempRoot "$mode.gate"
            $afterRemovePath = Join-Path $TestTempRoot "$mode.after-remove"
            $finishPath = Join-Path $TestTempRoot "$mode.finish"
            try {
                $ownerInfo = [Diagnostics.ProcessStartInfo]::new()
                $ownerInfo.FileName = (Get-Process -Id $PID).Path
                $ownerInfo.UseShellExecute = $false
                $ownerInfo.CreateNoWindow = $true
                foreach ($argument in @(
                    '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $ownerScript,
                    '-ModulePath', $processModulePath, '-RepositoryRoot', $RepositoryRoot,
                    '-SnapshotPath', $snapshotPath, '-GatePath', $gatePath,
                    '-AfterRemovePath', $afterRemovePath, '-FinishPath', $finishPath, '-Mode', $mode
                )) {
                    $null = $ownerInfo.ArgumentList.Add($argument)
                }
                $owner = [Diagnostics.Process]::Start($ownerInfo)
                $deadline = [DateTime]::UtcNow.AddSeconds(20)
                while (-not (Test-Path -LiteralPath $snapshotPath -PathType Leaf) -and
                    -not $owner.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                    [Threading.Thread]::Sleep(25)
                }
                Assert-True (Test-Path -LiteralPath $snapshotPath -PathType Leaf) `
                    "The '$mode' owner did not publish its public retained-identity snapshot."
                $newRunRoots = @(
                    Get-ChildItem -LiteralPath $pilotParent -Directory -Force |
                        Where-Object { $_.FullName -notin $existingRunRoots } |
                        Select-Object -ExpandProperty FullName
                )
                Assert-Equal -Actual $newRunRoots.Count -Expected 1
                $modeRunRoot = $newRunRoots[0]
                $snapshot = Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json -Depth 30 -DateKind String
                $identities = @($snapshot.nodes.identity) + @($snapshot.auxiliary_job_members.identity)
                Assert-Equal -Actual $identities.Count -Expected $snapshot.retained_identity_count
                foreach ($identity in $identities) {
                    $process = [Diagnostics.Process]::GetProcessById([int]$identity.pid)
                    $birth = $process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString([Globalization.CultureInfo]::InvariantCulture)
                    Assert-Equal -Actual $birth -Expected $identity.creation_time_filetime_utc
                    $retainedProcesses.Add($process)
                }
                [IO.File]::WriteAllBytes($gatePath, [byte[]](1))
                if ($mode -ceq 'remove-module') {
                    while (-not (Test-Path -LiteralPath $afterRemovePath -PathType Leaf) -and
                        -not $owner.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                        [Threading.Thread]::Sleep(25)
                    }
                    Assert-True (Test-Path -LiteralPath $afterRemovePath -PathType Leaf) `
                        'The owner must remain alive after Remove-Module so the test proves OnRemove, not process exit.'
                    Assert-True (-not $owner.HasExited)
                }
                else {
                    Assert-True ($owner.WaitForExit(10000)) 'The abrupt owner did not exit within ten seconds.'
                    Assert-Equal -Actual $owner.ExitCode -Expected 23
                }
                foreach ($process in $retainedProcesses) {
                    Assert-True ($process.WaitForExit(10000)) `
                        "Retained Job member $($process.Id) survived '$mode'."
                }
                Assert-True (-not $sentinel.HasExited) "The unrelated sentinel was killed by '$mode'."
                if ($mode -ceq 'remove-module') {
                    [IO.File]::WriteAllBytes($finishPath, [byte[]](1))
                    Assert-True ($owner.WaitForExit(10000)) 'The normal owner did not exit after the test released it.'
                    Assert-Equal -Actual $owner.ExitCode -Expected 0
                }
            }
            finally {
                foreach ($process in $retainedProcesses) {
                    try {
                        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
                    }
                    catch { }
                    $process.Dispose()
                }
                if ($null -ne $owner) {
                    try {
                        if (-not $owner.HasExited) { $owner.Kill(); $owner.WaitForExit() }
                    }
                    catch { }
                    $owner.Dispose()
                }
                if ($null -ne $modeRunRoot -and (Test-Path -LiteralPath $modeRunRoot -PathType Container)) {
                    foreach ($markerName in @('root.ready.json', 'child.ready.json', 'grandchild.ready.json')) {
                        $markerPath = Join-Path $modeRunRoot $markerName
                        if (Test-Path -LiteralPath $markerPath -PathType Leaf) {
                            Remove-Item -LiteralPath $markerPath -Force
                        }
                    }
                    if (@(Get-ChildItem -LiteralPath $modeRunRoot -Force).Count -eq 0) {
                        Remove-Item -LiteralPath $modeRunRoot -Force
                    }
                }
            }
        }
    }
    finally {
        try {
            if (-not $sentinel.HasExited) { $sentinel.Kill(); $sentinel.WaitForExit() }
        }
        catch { }
        $sentinel.Dispose()
    }
}
