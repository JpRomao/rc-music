Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0MarkersUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0MarkersModuleInstanceId = [Guid]::NewGuid().ToString('N')
$script:M0MarkersModuleSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
$script:M0MarkerStatuses = @('valid', 'discarded', 'failed', 'inconclusive')
$script:M0MarkerCatalogSemanticSha256 = '1abda4d0e8b4383c33221c7ecf25ecf3c6337181c80d46c9879bcf84a5f2e148'

function Assert-M0MarkersRuntimeContract {
    $currentSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
    if ($currentSha256 -cne $script:M0MarkersModuleSha256) {
        throw [IO.InvalidDataException]::new('M0M001 loaded markers runtime differs from its exact source contract.')
    }
    return [pscustomobject][ordered]@{
        contract_id = 'm0-markers-runtime-v1'
        module_instance_id = $script:M0MarkersModuleInstanceId
        module_sha256 = $script:M0MarkersModuleSha256
    }
}

function Get-M0MarkerSha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Read-M0MarkerTextFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $path = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0M001 required $Name is missing: '$path'.")
    }
    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
        throw [IO.InvalidDataException]::new("M0M001 links are forbidden for trusted $Name '$path'.")
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new("M0M001 $Name must be non-empty strict UTF-8 without BOM.")
    }
    try {
        $text = $script:M0MarkersUtf8.GetString($bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0M001 $Name is not strict UTF-8.", $_.Exception)
    }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.EndsWith("`n`n", [StringComparison]::Ordinal) -or $text -match '(?m)[\t ]+$') {
        throw [IO.InvalidDataException]::new("M0M001 $Name must use LF, exactly one final newline and no trailing whitespace.")
    }
    return [pscustomobject][ordered]@{ path = $path; bytes = $bytes; text = $text }
}

function Get-M0MarkerKey {
    param([Parameter(Mandatory)] $Value)

    return '{0}:{1}' -f [string]$Value.marker_id, [string]$Value.phase
}

function Get-M0MarkerScenario {
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [string] $ScenarioId
    )

    $matches = @($Catalog.scenarios | Where-Object scenario_id -CEQ $ScenarioId)
    if ($matches.Count -ne 1) {
        throw [IO.InvalidDataException]::new("M0M002 scenario '$ScenarioId' is absent or duplicated in the marker catalog.")
    }
    return $matches[0]
}

function Assert-M0MarkerCatalog {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [string[]] $ShellScenarioIds
    )

    $actualScenarioIds = @($Catalog.scenarios | ForEach-Object { [string]$_.scenario_id })
    if ($actualScenarioIds.Count -ne $ShellScenarioIds.Count) {
        throw [IO.InvalidDataException]::new("M0M002 scenario coverage has $($actualScenarioIds.Count) entries; expected $($ShellScenarioIds.Count) with no missing, extra or duplicate scenario.")
    }
    for ($index = 0; $index -lt $ShellScenarioIds.Count; $index++) {
        if ($actualScenarioIds[$index] -cne $ShellScenarioIds[$index]) {
            throw [IO.InvalidDataException]::new("M0M002 scenario order/coverage differs at index $index; expected '$($ShellScenarioIds[$index])', got '$($actualScenarioIds[$index])'.")
        }
    }
    if (@($actualScenarioIds | Select-Object -Unique).Count -ne $actualScenarioIds.Count) {
        throw [IO.InvalidDataException]::new('M0M002 duplicate scenario identifiers are forbidden.')
    }

    $expectedResourcePolicies = @(
        [ordered]@{
            scenario_id = 'idle-memory'
            stabilization_start_marker_key = $null
            sampling_start_marker_key = 'idle-sampling-window:begin'
            sampling_end_marker_key = 'idle-sampling-window:end'
            minimum_stabilization_ns = 0
            sampling_duration_ns = 60000000000L
            memory_sample_marker_key = 'memory-5s-sample:instant'
            memory_sample_offset_ns = 5000000000L
            memory_sample_tolerance_ns = 125000000L
            qpc_duration_formula = 'floor((end_ticks-start_ticks)*1000000000/qpc_frequency)'
        },
        [ordered]@{
            scenario_id = 'paused-cpu'
            stabilization_start_marker_key = 'paused-stabilization-window:begin'
            sampling_start_marker_key = 'paused-sampling-window:begin'
            sampling_end_marker_key = 'paused-sampling-window:end'
            minimum_stabilization_ns = 30000000000L
            sampling_duration_ns = 60000000000L
            memory_sample_marker_key = $null
            memory_sample_offset_ns = $null
            memory_sample_tolerance_ns = $null
            qpc_duration_formula = 'floor((end_ticks-start_ticks)*1000000000/qpc_frequency)'
        },
        [ordered]@{
            scenario_id = 'active-progress'
            stabilization_start_marker_key = 'active-stabilization-window:begin'
            sampling_start_marker_key = 'active-sampling-window:begin'
            sampling_end_marker_key = 'active-sampling-window:end'
            minimum_stabilization_ns = 10000000000L
            sampling_duration_ns = 60000000000L
            memory_sample_marker_key = $null
            memory_sample_offset_ns = $null
            memory_sample_tolerance_ns = $null
            qpc_duration_formula = 'floor((end_ticks-start_ticks)*1000000000/qpc_frequency)'
        }
    )
    if ($null -eq $Catalog.PSObject.Properties['resource_sampling_policies']) {
        throw [IO.InvalidDataException]::new('M0M003 resource sampling policies are missing.')
    }
    $resourcePolicies = @($Catalog.resource_sampling_policies)
    if ($resourcePolicies.Count -ne $expectedResourcePolicies.Count) {
        throw [IO.InvalidDataException]::new('M0M003 resource sampling policy coverage must contain exactly idle, paused and active.')
    }
    for ($policyIndex = 0; $policyIndex -lt $expectedResourcePolicies.Count; $policyIndex++) {
        $actualPolicy = $resourcePolicies[$policyIndex]
        $expectedPolicy = $expectedResourcePolicies[$policyIndex]
        foreach ($propertyName in $expectedPolicy.Keys) {
            if ($null -eq $actualPolicy.PSObject.Properties[$propertyName]) {
                throw [IO.InvalidDataException]::new("M0M003 resource sampling policy '$policyIndex' is missing '$propertyName'.")
            }
            $actualValue = $actualPolicy.$propertyName
            $expectedValue = $expectedPolicy[$propertyName]
            if (($null -eq $expectedValue -and $null -ne $actualValue) -or
                ($null -ne $expectedValue -and [string]$actualValue -cne [string]$expectedValue)) {
                throw [IO.InvalidDataException]::new("M0M003 resource sampling policy '$propertyName' differs for '$($expectedPolicy.scenario_id)'.")
            }
        }

        $policyScenario = Get-M0MarkerScenario -Catalog $Catalog -ScenarioId ([string]$actualPolicy.scenario_id)
        $policyMarkers = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
        foreach ($policyMarker in @($policyScenario.markers)) {
            $policyMarkers[(Get-M0MarkerKey -Value $policyMarker)] = $policyMarker
        }
        foreach ($boundaryName in @('sampling_start_marker_key', 'sampling_end_marker_key')) {
            $boundaryKey = [string]$actualPolicy.$boundaryName
            if (-not $policyMarkers.ContainsKey($boundaryKey) -or
                $policyMarkers[$boundaryKey].source -cne 'external_harness' -or
                $policyMarkers[$boundaryKey].role -cne 'measurement_boundary') {
                throw [IO.InvalidDataException]::new("M0M003 resource sampling '$boundaryName' must be an external measurement boundary in '$($actualPolicy.scenario_id)'.")
            }
        }
        if ($null -ne $actualPolicy.stabilization_start_marker_key) {
            $stabilizationKey = [string]$actualPolicy.stabilization_start_marker_key
            if (-not $policyMarkers.ContainsKey($stabilizationKey) -or $policyMarkers[$stabilizationKey].source -cne 'external_harness') {
                throw [IO.InvalidDataException]::new("M0M003 stabilization start marker is invalid in '$($actualPolicy.scenario_id)'.")
            }
        }
        if ($null -ne $actualPolicy.memory_sample_marker_key) {
            $sampleKey = [string]$actualPolicy.memory_sample_marker_key
            if (-not $policyMarkers.ContainsKey($sampleKey) -or
                $policyMarkers[$sampleKey].source -cne 'external_harness' -or
                $policyMarkers[$sampleKey].role -cne 'diagnostic') {
                throw [IO.InvalidDataException]::new("M0M003 memory sample marker is invalid in '$($actualPolicy.scenario_id)'.")
            }
        }
        $requiredMetricIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($requiredMetric in @($actualPolicy.required_metrics)) {
            if (-not $requiredMetricIds.Add([string]$requiredMetric.metric_id)) {
                throw [IO.InvalidDataException]::new("M0M003 duplicate required resource metric '$($requiredMetric.metric_id)' in '$($actualPolicy.scenario_id)'.")
            }
            $anchorKey = [string]$requiredMetric.anchor_marker_key
            if (-not $policyMarkers.ContainsKey($anchorKey) -or $policyMarkers[$anchorKey].source -cne 'external_harness') {
                throw [IO.InvalidDataException]::new("M0M003 required resource metric '$($requiredMetric.metric_id)' has an invalid external anchor '$anchorKey'.")
            }
        }
    }

    foreach ($scenario in @($Catalog.scenarios)) {
        $markerByKey = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
        $markerIndexByKey = [Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
        for ($markerIndex = 0; $markerIndex -lt @($scenario.markers).Count; $markerIndex++) {
            $marker = $scenario.markers[$markerIndex]
            $key = Get-M0MarkerKey -Value $marker
            if ($markerByKey.ContainsKey($key)) {
                throw [IO.InvalidDataException]::new("M0M003 duplicate marker '$key' in scenario '$($scenario.scenario_id)'.")
            }
            $markerByKey[$key] = $marker
            $markerIndexByKey[$key] = $markerIndex
            if ($marker.source -cnotin @('external_harness', 'candidate_reported') -or
                $marker.role -cnotin @('measurement_boundary', 'diagnostic') -or
                $marker.occurrence_id_policy -cnotin @('forbidden', 'required_unique')) {
                throw [IO.InvalidDataException]::new("M0M003 marker '$key' has an unknown source, role or occurrence policy.")
            }
            if ($marker.source -ceq 'candidate_reported' -and $marker.role -cne 'diagnostic') {
                throw [IO.InvalidDataException]::new("M0M003 candidate_reported marker '$key' cannot be a measurement_boundary.")
            }
            if ($marker.role -ceq 'measurement_boundary' -and $marker.source -cne 'external_harness') {
                throw [IO.InvalidDataException]::new("M0M003 measurement_boundary marker '$key' must be external_harness.")
            }
            $minimum = [int]$marker.cardinality.minimum
            $maximum = [int]$marker.cardinality.maximum
            $requiredStatuses = @($marker.cardinality.required_statuses)
            if ($minimum -lt 0 -or $maximum -lt $minimum -or $maximum -gt 100 -or
                ($requiredStatuses.Count -gt 0 -and $maximum -eq 0) -or
                @($requiredStatuses | Where-Object { $_ -cnotin $script:M0MarkerStatuses }).Count -gt 0) {
                throw [IO.InvalidDataException]::new("M0M003 invalid cardinality/required status for marker '$key'.")
            }
            if ($marker.occurrence_id_policy -ceq 'required_unique' -and $maximum -le 1) {
                throw [IO.InvalidDataException]::new("M0M003 occurrence cardinality for repeatable marker '$key' must allow multiple observations.")
            }
        }

        foreach ($marker in @($scenario.markers)) {
            $key = Get-M0MarkerKey -Value $marker
            foreach ($dependency in @($marker.after)) {
                if ($dependency -ceq $key) {
                    throw [IO.InvalidDataException]::new("M0M004 self cycle in marker dependency '$key'.")
                }
                if (-not $markerByKey.ContainsKey([string]$dependency)) {
                    throw [IO.InvalidDataException]::new("M0M004 marker dependency '$dependency' for '$key' does not exist.")
                }
                if ([int]$markerIndexByKey[[string]$dependency] -ge [int]$markerIndexByKey[$key]) {
                    throw [IO.InvalidDataException]::new("M0M004 cycle or non-topological dependency order '$dependency' -> '$key'.")
                }
            }
        }

        $metrics = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($measurement in @($scenario.measurements)) {
            if (-not $metrics.Add([string]$measurement.metric_id)) {
                throw [IO.InvalidDataException]::new("M0M003 duplicate metric '$($measurement.metric_id)' in scenario '$($scenario.scenario_id)'.")
            }
            if ($measurement.source -cne 'external_harness' -or
                $measurement.decision_role -cnotin @('attempt_limit', 'batch_p95', 'diagnostic_only') -or
                $measurement.attempt_aggregate -cnotin @('identity', 'max')) {
                throw [IO.InvalidDataException]::new("M0M003 measurement '$($measurement.metric_id)' has an unknown source, decision role or aggregate.")
            }
            if (($measurement.decision_role -ceq 'attempt_limit' -and
                 ($null -eq $measurement.attempt_limit_ns -or [decimal]$measurement.attempt_limit_ns -le 0)) -or
                ($measurement.decision_role -cne 'attempt_limit' -and $null -ne $measurement.attempt_limit_ns)) {
                throw [IO.InvalidDataException]::new("M0M003 measurement '$($measurement.metric_id)' has an invalid attempt_limit_ns policy.")
            }
            foreach ($endpoint in @('start_marker_key', 'end_marker_key')) {
                $endpointKey = [string]$measurement.$endpoint
                if (-not $markerByKey.ContainsKey($endpointKey)) {
                    throw [IO.InvalidDataException]::new("M0M003 measurement '$($measurement.metric_id)' references unknown endpoint '$endpointKey'.")
                }
                $endpointMarker = $markerByKey[$endpointKey]
                if ($endpointMarker.source -cne 'external_harness' -or $endpointMarker.role -cne 'measurement_boundary') {
                    throw [IO.InvalidDataException]::new("M0M003 measurement '$($measurement.metric_id)' endpoint '$endpointKey' must be an external_harness measurement_boundary.")
                }
                if ([bool]$measurement.correlated_occurrence -and $endpointMarker.occurrence_id_policy -cne 'required_unique') {
                    throw [IO.InvalidDataException]::new("M0M003 correlated measurement '$($measurement.metric_id)' requires occurrence identifiers at both endpoints.")
                }
            }
            if ($measurement.attempt_aggregate -ceq 'max' -and -not [bool]$measurement.correlated_occurrence) {
                throw [IO.InvalidDataException]::new("M0M003 max aggregate measurement '$($measurement.metric_id)' must be occurrence-correlated.")
            }
        }
    }

    $semanticJson = ConvertTo-Json -InputObject $Catalog -Compress -Depth 100
    $semanticHash = Get-M0MarkerSha256Bytes -Bytes $script:M0MarkersUtf8.GetBytes($semanticJson)
    if ($semanticHash -cne $script:M0MarkerCatalogSemanticSha256) {
        throw [IO.InvalidDataException]::new('M0M003 frozen semantic fingerprint differs for marker/measurement decision policy role limit aggregate.')
    }

    return [pscustomobject][ordered]@{
        catalog_id = [string]$Catalog.catalog_id
        scenario_count = @($Catalog.scenarios).Count
    }
}

function Assert-M0QueueMarkerSequence {
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [object[]] $Events,
        [Parameter(Mandatory)] [object[]] $MarkerEvents,
        [AllowEmptyString()] [string] $Status
    )

    $window = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq 'queue-probe-window:begin' })
    $pairs = @($MarkerEvents | Where-Object { $_.payload.marker_id -cin @('focus-probe-input', 'focus-visible-ack') })
    $terminalFailures = @($Events | Where-Object {
        $_.event -ceq 'action_observation' -and
        $_.payload.metric_id -ceq 'queue_focus_stall_ns' -and
        $_.payload.outcome -cin @('timeout', 'crash', 'hang', 'control_unavailable')
    })
    $discardCauses = @($Events | Where-Object {
        $_.event -ceq 'noise_observation' -and [bool]$_.payload.observed
    })
    $inconclusiveCauses = @($Events | Where-Object {
        $_.event -ceq 'check_recorded' -and $_.payload.status -ceq 'skipped'
    })
    if ($terminalFailures.Count -gt 1) {
        throw [IO.InvalidDataException]::new('M0M008 queue pending input permits at most one terminal failed action.')
    }
    if ($pairs.Count -eq 0) {
        if ($terminalFailures.Count -gt 0) {
            throw [IO.InvalidDataException]::new('M0M008 queue terminal failed action requires one preceding pending input.')
        }
        if ($Status -ceq 'valid') {
            throw [IO.InvalidDataException]::new('M0M008 queue pair cardinality requires 1 through 100 complete pairs for a valid run.')
        }
        return 0
    }
    if ($window.Count -ne 1) {
        throw [IO.InvalidDataException]::new('M0M008 queue probes require exactly one queue-probe-window boundary.')
    }
    $hasPendingTerminalInput = ($pairs.Count % 2) -ne 0
    if ($hasPendingTerminalInput) {
        $pending = $pairs[-1]
        $terminalFailuresAfterPending = @($terminalFailures | Where-Object { [int]$_.event_seq -gt [int]$pending.event_seq })
        $discardCausesAfterPending = @($discardCauses | Where-Object { [int]$_.event_seq -gt [int]$pending.event_seq })
        $inconclusiveCausesAfterPending = @($inconclusiveCauses | Where-Object { [int]$_.event_seq -gt [int]$pending.event_seq })
        $causeCount = $terminalFailures.Count + $discardCauses.Count + $inconclusiveCauses.Count
        $causalAfterPendingCount = $terminalFailuresAfterPending.Count + $discardCausesAfterPending.Count + $inconclusiveCausesAfterPending.Count
        $isOpenPartialSequence = [string]::IsNullOrEmpty($Status) -and
            $pending.payload.marker_id -ceq 'focus-probe-input' -and
            $causeCount -le 1 -and $causeCount -eq $causalAfterPendingCount
        $isClosedFailedSequence = $Status -ceq 'failed' -and
            $pending.payload.marker_id -ceq 'focus-probe-input' -and
            $terminalFailures.Count -eq 1 -and $terminalFailuresAfterPending.Count -eq 1 -and
            $discardCauses.Count -eq 0 -and $inconclusiveCauses.Count -eq 0
        $isClosedDiscardedSequence = $Status -ceq 'discarded' -and
            $pending.payload.marker_id -ceq 'focus-probe-input' -and
            $discardCauses.Count -eq 1 -and $discardCausesAfterPending.Count -eq 1 -and
            $terminalFailures.Count -eq 0 -and $inconclusiveCauses.Count -eq 0
        $isClosedInconclusiveSequence = $Status -ceq 'inconclusive' -and
            $pending.payload.marker_id -ceq 'focus-probe-input' -and
            $inconclusiveCauses.Count -eq 1 -and $inconclusiveCausesAfterPending.Count -eq 1 -and
            $terminalFailures.Count -eq 0 -and $discardCauses.Count -eq 0
        if (-not $isOpenPartialSequence -and -not $isClosedFailedSequence -and
            -not $isClosedDiscardedSequence -and -not $isClosedInconclusiveSequence) {
            throw [IO.InvalidDataException]::new('M0M008 queue pair is unmatched without exactly one permitted causal failed, discarded or inconclusive event after its input.')
        }
    }
    elseif ($terminalFailures.Count -gt 0) {
        throw [IO.InvalidDataException]::new('M0M008 queue ack cannot follow a terminal failed action for the same pending input.')
    }
    $pairCount = [int][Math]::Floor($pairs.Count / 2)
    if ($pairCount -gt [int]$Catalog.queue_probe_policy.maximum_complete_pairs -or
        ($Status -ceq 'valid' -and $pairCount -lt [int]$Catalog.queue_probe_policy.minimum_complete_pairs)) {
        throw [IO.InvalidDataException]::new('M0M008 queue pair cardinality must be 1 through 100 complete pairs.')
    }

    $frequency = [Numerics.BigInteger]::Parse([string]$window[0].qpc_frequency)
    $windowTicks = [Numerics.BigInteger]::Parse([string]$window[0].qpc_ticks)
    if ($frequency -le [Numerics.BigInteger]::Zero) {
        throw [IO.InvalidDataException]::new('M0M008 queue QPC frequency must be positive.')
    }
    $seenOccurrences = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $previousInputTicks = $null
    $previousAckSequence = $null
    $previousAckTicks = $null
    for ($pairIndex = 0; $pairIndex -lt $pairCount; $pairIndex++) {
        $inputEvent = $pairs[$pairIndex * 2]
        $ackEvent = $pairs[($pairIndex * 2) + 1]
        if ($inputEvent.payload.marker_id -cne 'focus-probe-input' -or
            $ackEvent.payload.marker_id -cne 'focus-visible-ack') {
            throw [IO.InvalidDataException]::new('M0M008 queue probes must strictly alternate input then ack; the next input waits for the previous ack.')
        }
        if ([int]$inputEvent.event_seq -ge [int]$ackEvent.event_seq -or
            ($null -ne $previousAckSequence -and [int]$inputEvent.event_seq -le [int]$previousAckSequence)) {
            throw [IO.InvalidDataException]::new('M0M008 queue probes violate strict input/ack sequence order.')
        }
        $occurrence = [string]$inputEvent.payload.occurrence_id
        if ([string]::IsNullOrWhiteSpace($occurrence) -or $ackEvent.payload.occurrence_id -cne $occurrence) {
            throw [IO.InvalidDataException]::new('M0M008 queue pair occurrence identifiers do not correlate.')
        }
        if (-not $seenOccurrences.Add($occurrence)) {
            throw [IO.InvalidDataException]::new("M0M008 queue occurrence '$occurrence' is not unique.")
        }
        if ([string]$inputEvent.qpc_frequency -cne [string]$window[0].qpc_frequency -or
            [string]$ackEvent.qpc_frequency -cne [string]$window[0].qpc_frequency) {
            throw [IO.InvalidDataException]::new('M0M008 queue markers must share one QPC frequency.')
        }
        $inputTicks = [Numerics.BigInteger]::Parse([string]$inputEvent.qpc_ticks)
        $ackTicks = [Numerics.BigInteger]::Parse([string]$ackEvent.qpc_ticks)
        $inputOffsetNs = [Numerics.BigInteger]::Divide(($inputTicks - $windowTicks) * 1000000000L, $frequency)
        if ($pairIndex -eq 0 -and $inputTicks -ne $windowTicks) {
            throw [IO.InvalidDataException]::new('M0M008 first queue probe must start exactly at t0.')
        }
        if ($inputOffsetNs -lt 0 -or $inputOffsetNs -ge [Numerics.BigInteger]::Parse([string]$Catalog.queue_probe_policy.window_duration_ns)) {
            throw [IO.InvalidDataException]::new('M0M008 queue input violates the half-open [t0,t0+5s) window.')
        }
        if ($null -ne $previousInputTicks) {
            $intervalNs = [Numerics.BigInteger]::Divide(($inputTicks - $previousInputTicks) * 1000000000L, $frequency)
            if ($intervalNs -lt [Numerics.BigInteger]::Parse([string]$Catalog.queue_probe_policy.minimum_input_interval_ns)) {
                throw [IO.InvalidDataException]::new('M0M008 queue input cadence is shorter than 50 ms.')
            }
        }
        if ($null -ne $previousAckSequence -and $inputTicks -lt $previousAckTicks) {
            throw [IO.InvalidDataException]::new('M0M008 next queue input QPC precedes the previous ack.')
        }
        if ($ackTicks -lt $inputTicks) {
            throw [IO.InvalidDataException]::new('M0M008 queue ack QPC precedes its input.')
        }
        $previousInputTicks = $inputTicks
        $previousAckSequence = [int]$ackEvent.event_seq
        $previousAckTicks = $ackTicks
    }
    if ($hasPendingTerminalInput) {
        $pending = $pairs[-1]
        $pendingOccurrence = [string]$pending.payload.occurrence_id
        if ([string]::IsNullOrWhiteSpace($pendingOccurrence) -or -not $seenOccurrences.Add($pendingOccurrence)) {
            throw [IO.InvalidDataException]::new('M0M008 terminal pending queue input requires a unique occurrence_id.')
        }
        if ([string]$pending.qpc_frequency -cne [string]$window[0].qpc_frequency) {
            throw [IO.InvalidDataException]::new('M0M008 terminal pending queue input must share the window QPC frequency.')
        }
        $pendingTicks = [Numerics.BigInteger]::Parse([string]$pending.qpc_ticks)
        $pendingOffsetNs = [Numerics.BigInteger]::Divide(($pendingTicks - $windowTicks) * 1000000000L, $frequency)
        if (($pairCount -eq 0 -and $pendingTicks -ne $windowTicks) -or
            $pendingOffsetNs -lt 0 -or
            $pendingOffsetNs -ge [Numerics.BigInteger]::Parse([string]$Catalog.queue_probe_policy.window_duration_ns)) {
            throw [IO.InvalidDataException]::new('M0M008 terminal pending queue input violates t0 or the half-open [t0,t0+5s) window.')
        }
        if ($null -ne $previousInputTicks) {
            $pendingIntervalNs = [Numerics.BigInteger]::Divide(($pendingTicks - $previousInputTicks) * 1000000000L, $frequency)
            if ($pendingIntervalNs -lt [Numerics.BigInteger]::Parse([string]$Catalog.queue_probe_policy.minimum_input_interval_ns)) {
                throw [IO.InvalidDataException]::new('M0M008 terminal pending queue input violates the 50 ms cadence.')
            }
        }
        if (($null -ne $previousAckTicks -and $pendingTicks -lt $previousAckTicks) -or
            ($null -ne $previousAckSequence -and [int]$pending.event_seq -le [int]$previousAckSequence)) {
            throw [IO.InvalidDataException]::new('M0M008 terminal pending queue input precedes the previous ack.')
        }
    }
    return $pairCount
}

function Get-M0MarkerQpcDurationNs {
    param(
        [Parameter(Mandatory)] $StartEvent,
        [Parameter(Mandatory)] $EndEvent,
        [Parameter(Mandatory)] [string] $Context
    )

    if ([string]$StartEvent.qpc_frequency -cne [string]$EndEvent.qpc_frequency) {
        throw [IO.InvalidDataException]::new("M0M007 $Context markers use different QPC frequencies.")
    }
    $frequency = [Numerics.BigInteger]::Parse([string]$StartEvent.qpc_frequency)
    $startTicks = [Numerics.BigInteger]::Parse([string]$StartEvent.qpc_ticks)
    $endTicks = [Numerics.BigInteger]::Parse([string]$EndEvent.qpc_ticks)
    if ($frequency -le 0 -or $endTicks -lt $startTicks) {
        throw [IO.InvalidDataException]::new("M0M007 $Context QPC frequency or interval is invalid.")
    }
    return [Numerics.BigInteger]::Divide(($endTicks - $startTicks) * 1000000000L, $frequency)
}

function Assert-M0ResourceSamplingSequence {
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Events,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $MarkerEvents,
        [AllowEmptyString()] [string] $Status
    )

    $policies = @($Catalog.resource_sampling_policies | Where-Object scenario_id -CEQ $ScenarioId)
    if ($policies.Count -eq 0) { return }
    if ($policies.Count -ne 1) {
        throw [IO.InvalidDataException]::new("M0M007 resource sampling policy is ambiguous for '$ScenarioId'.")
    }
    $policy = $policies[0]
    $samplingStarts = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $policy.sampling_start_marker_key })
    $samplingEnds = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $policy.sampling_end_marker_key })
    if ($samplingStarts.Count -eq 1 -and $samplingEnds.Count -eq 1) {
        $samplingNs = Get-M0MarkerQpcDurationNs -StartEvent $samplingStarts[0] -EndEvent $samplingEnds[0] -Context "$ScenarioId sampling"
        $requiredSamplingNs = [Numerics.BigInteger]::Parse([string]$policy.sampling_duration_ns)
        if ($samplingNs -ne $requiredSamplingNs) {
            throw [IO.InvalidDataException]::new("M0M007 $ScenarioId sampling window must be exactly 60 s; got $samplingNs ns.")
        }
    }

    if ($null -ne $policy.stabilization_start_marker_key) {
        $stabilizationStarts = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $policy.stabilization_start_marker_key })
        if ($stabilizationStarts.Count -eq 1 -and $samplingStarts.Count -eq 1) {
            $stabilizationNs = Get-M0MarkerQpcDurationNs -StartEvent $stabilizationStarts[0] -EndEvent $samplingStarts[0] -Context "$ScenarioId stabilization"
            $minimumStabilizationNs = [Numerics.BigInteger]::Parse([string]$policy.minimum_stabilization_ns)
            if ($stabilizationNs -lt $minimumStabilizationNs) {
                throw [IO.InvalidDataException]::new("M0M007 $ScenarioId stabilization is shorter than $minimumStabilizationNs ns.")
            }
        }
    }

    if ($null -ne $policy.memory_sample_marker_key) {
        $memorySamples = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $policy.memory_sample_marker_key })
        if ($samplingStarts.Count -eq 1 -and $memorySamples.Count -eq 1) {
            $sampleOffsetNs = Get-M0MarkerQpcDurationNs -StartEvent $samplingStarts[0] -EndEvent $memorySamples[0] -Context "$ScenarioId memory-5s sample"
            $targetNs = [Numerics.BigInteger]::Parse([string]$policy.memory_sample_offset_ns)
            $toleranceNs = [Numerics.BigInteger]::Parse([string]$policy.memory_sample_tolerance_ns)
            if ($sampleOffsetNs -lt ($targetNs - $toleranceNs) -or $sampleOffsetNs -gt ($targetNs + $toleranceNs)) {
                throw [IO.InvalidDataException]::new("M0M007 memory-5s sample must be within 125 ms of 5 s; got $sampleOffsetNs ns.")
            }
        }
    }

    $resourceEvents = @($Events | Where-Object event -CEQ 'resource_sample')
    $completionMarkers = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq 'measurement-complete:end' })
    foreach ($requiredMetric in @($policy.required_metrics)) {
        $matchingMetrics = @($resourceEvents | Where-Object { $_.payload.metric_id -ceq $requiredMetric.metric_id })
        if ($matchingMetrics.Count -gt 1) {
            throw [IO.InvalidDataException]::new("M0M007 duplicate required resource metric '$($requiredMetric.metric_id)' is forbidden.")
        }
        if ($Status -ceq 'valid' -and $matchingMetrics.Count -ne 1) {
            throw [IO.InvalidDataException]::new("M0M007 valid '$ScenarioId' requires exactly one resource metric '$($requiredMetric.metric_id)'.")
        }
        if ($matchingMetrics.Count -eq 0) { continue }

        $metricEvent = $matchingMetrics[0]
        if ($metricEvent.payload.unit -cne $requiredMetric.unit -or
            $null -eq $metricEvent.payload.value_integer -or
            $null -ne $metricEvent.payload.error_code) {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' has an invalid unit, value or error contract.")
        }
        try {
            $metricValue = [Numerics.BigInteger]::Parse([string]$metricEvent.payload.value_integer)
        }
        catch {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' value is not an exact integer.", $_.Exception)
        }
        if ($metricValue -lt 0) {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' value must be non-negative.")
        }

        $anchorMarkers = @($MarkerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $requiredMetric.anchor_marker_key })
        if ($anchorMarkers.Count -ne 1) {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' has no unique marker anchor.")
        }
        $anchor = $anchorMarkers[0]
        if ([string]$metricEvent.qpc_frequency -cne [string]$anchor.qpc_frequency -or
            [string]$metricEvent.qpc_ticks -cne [string]$anchor.qpc_ticks -or
            [int]$metricEvent.event_seq -le [int]$anchor.event_seq) {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' differs from its QPC anchor or causal order.")
        }
        if ($completionMarkers.Count -eq 1 -and [int]$metricEvent.event_seq -ge [int]$completionMarkers[0].event_seq) {
            throw [IO.InvalidDataException]::new("M0M007 required resource metric '$($requiredMetric.metric_id)' must precede measurement-complete.")
        }
    }
}

function Assert-M0MarkerSequence {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Events,
        [ValidateSet('', 'valid', 'discarded', 'failed', 'inconclusive')] [string] $Status = ''
    )

    $scenario = Get-M0MarkerScenario -Catalog $Catalog -ScenarioId $ScenarioId
    $markerByKey = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($marker in @($scenario.markers)) {
        $markerByKey[(Get-M0MarkerKey -Value $marker)] = $marker
    }
    $markerEvents = @($Events | Where-Object event -CEQ 'marker')
    $seenEventSequences = [Collections.Generic.HashSet[int]]::new()
    $occurrencesByMarkerKey = [Collections.Generic.Dictionary[string, Collections.Generic.HashSet[string]]]::new([StringComparer]::Ordinal)
    $previousMarkerSequence = 0
    foreach ($event in $markerEvents) {
        if (-not $seenEventSequences.Add([int]$event.event_seq)) {
            throw [IO.InvalidDataException]::new('M0M005 duplicate marker event sequence.')
        }
        if ([int]$event.event_seq -le $previousMarkerSequence) {
            throw [IO.InvalidDataException]::new('M0M007 marker event order does not follow increasing raw event_seq.')
        }
        $previousMarkerSequence = [int]$event.event_seq
        $key = Get-M0MarkerKey -Value $event.payload
        if (-not $markerByKey.ContainsKey($key)) {
            throw [IO.InvalidDataException]::new("M0M005 unknown marker '$key' for scenario '$ScenarioId'.")
        }
        $contract = $markerByKey[$key]
        if ($event.payload.source -cne $contract.source -or $event.payload.role -cne $contract.role) {
            throw [IO.InvalidDataException]::new("M0M005 marker '$key' source or role differs from the catalog.")
        }
        if ($contract.occurrence_id_policy -ceq 'required_unique' -and [string]::IsNullOrWhiteSpace([string]$event.payload.occurrence_id)) {
            throw [IO.InvalidDataException]::new("M0M005 marker '$key' requires a non-empty occurrence_id.")
        }
        if ($contract.occurrence_id_policy -ceq 'required_unique') {
            if (-not $occurrencesByMarkerKey.ContainsKey($key)) {
                $occurrencesByMarkerKey[$key] = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            }
            if (-not $occurrencesByMarkerKey[$key].Add([string]$event.payload.occurrence_id)) {
                throw [IO.InvalidDataException]::new("M0M005 marker '$key' occurrence_id must be unique; duplicate '$($event.payload.occurrence_id)'.")
            }
        }
        if ($contract.occurrence_id_policy -ceq 'forbidden' -and $null -ne $event.payload.occurrence_id) {
            throw [IO.InvalidDataException]::new("M0M005 marker '$key' occurrence_id is forbidden.")
        }
    }

    $completePairCount = 0
    if ($ScenarioId -ceq 'queue-stall') {
        $completePairCount = Assert-M0QueueMarkerSequence -Catalog $Catalog -Events $Events -MarkerEvents $markerEvents -Status $Status
    }

    Assert-M0ResourceSamplingSequence -Catalog $Catalog -ScenarioId $ScenarioId -Events $Events -MarkerEvents $markerEvents -Status $Status

    if ($ScenarioId -ceq 'visual-response-diagnostic' -and $markerEvents.Count -gt 0) {
        $baseline = @($markerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq 'visual-baseline-captured:instant' })
        $input = @($markerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq 'visual-input:instant' })
        $uia = @($markerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq 'uia-change:instant' })
        if ($baseline.Count -eq 1 -and $input.Count -eq 1) {
            if ([string]$baseline[0].qpc_frequency -cne [string]$input[0].qpc_frequency) {
                throw [IO.InvalidDataException]::new('M0M007 visual baseline/input QPC frequency differs.')
            }
            $frequency = [Numerics.BigInteger]::Parse([string]$input[0].qpc_frequency)
            $baselineNs = [Numerics.BigInteger]::Divide(
                ([Numerics.BigInteger]::Parse([string]$input[0].qpc_ticks) - [Numerics.BigInteger]::Parse([string]$baseline[0].qpc_ticks)) * 1000000000L,
                $frequency
            )
            if ($baselineNs -lt [Numerics.BigInteger]::Parse([string]$Catalog.visual_diagnostic_policy.minimum_stable_baseline_ns)) {
                throw [IO.InvalidDataException]::new('M0M007 visual input requires the frozen 500 ms stable baseline.')
            }
        }
        if ($input.Count -eq 1 -and $uia.Count -eq 1) {
            if ([string]$input[0].qpc_frequency -cne [string]$uia[0].qpc_frequency) {
                throw [IO.InvalidDataException]::new('M0M007 visual input/UIA QPC frequency differs.')
            }
            $frequency = [Numerics.BigInteger]::Parse([string]$input[0].qpc_frequency)
            $uiaNs = [Numerics.BigInteger]::Divide(
                ([Numerics.BigInteger]::Parse([string]$uia[0].qpc_ticks) - [Numerics.BigInteger]::Parse([string]$input[0].qpc_ticks)) * 1000000000L,
                $frequency
            )
            if ($uiaNs -lt 0 -or $uiaNs -gt [Numerics.BigInteger]::Parse([string]$Catalog.visual_diagnostic_policy.uia_confirmation_timeout_ns)) {
                throw [IO.InvalidDataException]::new('M0M007 visual UIA confirmation exceeds the 2 s timeout.')
            }
        }
    }

    foreach ($key in $markerByKey.Keys) {
        $contract = $markerByKey[$key]
        $observed = @($markerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $key })
        $maximum = [int]$contract.cardinality.maximum
        if ($observed.Count -gt $maximum) {
            throw [IO.InvalidDataException]::new("M0M006 marker '$key' cardinality $($observed.Count) exceeds maximum $maximum.")
        }
        if ($Status -cin @($contract.cardinality.required_statuses) -and
            $observed.Count -lt [int]$contract.cardinality.minimum) {
            throw [IO.InvalidDataException]::new("M0M006 required marker '$key' cardinality is below minimum $($contract.cardinality.minimum) for status '$Status'.")
        }
    }
    foreach ($key in $markerByKey.Keys) {
        $contract = $markerByKey[$key]
        $observed = @($markerEvents | Where-Object { (Get-M0MarkerKey -Value $_.payload) -ceq $key })
        foreach ($event in $observed) {
            foreach ($dependency in @($contract.after)) {
                $predecessors = @($markerEvents | Where-Object {
                    (Get-M0MarkerKey -Value $_.payload) -ceq $dependency -and [int]$_.event_seq -lt [int]$event.event_seq
                })
                if ($predecessors.Count -eq 0) {
                    throw [IO.InvalidDataException]::new("M0M007 marker '$key' violates predecessor/order dependency '$dependency'.")
                }
            }
        }
    }

    $actionEvents = @($Events | Where-Object event -CEQ 'action_observation')
    $resolvedActions = [Collections.Generic.List[object]]::new()
    $terminalActions = [Collections.Generic.List[object]]::new()
    foreach ($actionEvent in $actionEvents) {
        $measurementMatches = @($scenario.measurements | Where-Object metric_id -CEQ $actionEvent.payload.metric_id)
        if ($measurementMatches.Count -ne 1) {
            throw [IO.InvalidDataException]::new("M0M009 unknown or ambiguous action metric '$($actionEvent.payload.metric_id)' for scenario '$ScenarioId'.")
        }
        if ($actionEvent.payload.source -cne 'external_harness') {
            throw [IO.InvalidDataException]::new('M0M009 action_observation source must be external_harness.')
        }
        if ($actionEvent.payload.outcome -cin @('observed', 'budget_exceeded')) {
            $resolvedActions.Add((Resolve-M0MeasurementBoundary -Catalog $Catalog -ScenarioId $ScenarioId -Events $Events -ActionEvent $actionEvent))
        }
        elseif ($actionEvent.payload.outcome -cin @('timeout', 'crash', 'hang', 'control_unavailable')) {
            if ($null -ne $actionEvent.payload.value_ns -or
                $null -ne $actionEvent.payload.start_marker_event_seq -or
                $null -ne $actionEvent.payload.end_marker_event_seq -or
                [string]::IsNullOrWhiteSpace([string]$actionEvent.payload.reason_code) -or
                [string]::IsNullOrWhiteSpace([string]$actionEvent.payload.error_code)) {
                throw [IO.InvalidDataException]::new('M0M009 terminal adverse action must have null measurement values/references and closed reason/error codes.')
            }
            $terminalActions.Add($actionEvent)
        }
        else {
            throw [IO.InvalidDataException]::new("M0M009 unknown action outcome '$($actionEvent.payload.outcome)'.")
        }
    }
    $budgetExceededActions = @($actionEvents | Where-Object { $_.payload.outcome -ceq 'budget_exceeded' })
    if ($Status -ceq 'valid' -and ($terminalActions.Count -gt 0 -or $budgetExceededActions.Count -gt 0)) {
        throw [IO.InvalidDataException]::new('M0M009 a valid marker sequence cannot contain budget_exceeded or terminal adverse actions; the run status must be failed.')
    }
    if (-not [string]::IsNullOrEmpty($Status) -and $ScenarioId -ceq 'queue-stall') {
        $pairMarkers = @($markerEvents | Where-Object { $_.payload.marker_id -cin @('focus-probe-input', 'focus-visible-ack') })
        for ($pairIndex = 0; $pairIndex -lt $completePairCount; $pairIndex++) {
            $startSequence = [int]$pairMarkers[$pairIndex * 2].event_seq
            $endSequence = [int]$pairMarkers[($pairIndex * 2) + 1].event_seq
            $matchingActions = @($actionEvents | Where-Object {
                $_.payload.outcome -cin @('observed', 'budget_exceeded') -and
                [int]$_.payload.start_marker_event_seq -eq $startSequence -and
                [int]$_.payload.end_marker_event_seq -eq $endSequence
            })
            if ($matchingActions.Count -ne 1) {
                throw [IO.InvalidDataException]::new("M0M009 queue pair $startSequence->$endSequence requires exactly one correlated action_observation.")
            }
        }
        if ($resolvedActions.Count -ne $completePairCount) {
            throw [IO.InvalidDataException]::new('M0M009 queue actions contain an extra, duplicate or orphan measurement.')
        }
    }
    elseif ($Status -ceq 'valid') {
        foreach ($measurement in @($scenario.measurements)) {
            $matchingActions = @($actionEvents | Where-Object {
                $_.payload.metric_id -ceq $measurement.metric_id -and $_.payload.outcome -cin @('observed', 'budget_exceeded')
            })
            if ($measurement.attempt_aggregate -ceq 'identity' -and $matchingActions.Count -ne 1) {
                throw [IO.InvalidDataException]::new("M0M009 valid identity metric '$($measurement.metric_id)' requires exactly one action_observation.")
            }
        }
    }

    return [pscustomobject][ordered]@{
        scenario_id = $ScenarioId
        marker_event_count = $markerEvents.Count
        complete_pair_count = [int]$completePairCount
        action_event_count = $actionEvents.Count
    }
}

function Resolve-M0MeasurementBoundary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Catalog,
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [object[]] $Events,
        [Parameter(Mandatory)] $ActionEvent
    )

    if ($ActionEvent.event -cne 'action_observation' -or $ActionEvent.payload.source -cne 'external_harness') {
        throw [IO.InvalidDataException]::new('M0M009 action source must be external_harness action_observation.')
    }
    $rawActionMatches = @($Events | Where-Object { [int]$_.event_seq -eq [int]$ActionEvent.event_seq -and $_.event -ceq 'action_observation' })
    if ($rawActionMatches.Count -ne 1 -or
        ($rawActionMatches[0] | ConvertTo-Json -Compress -Depth 100) -cne ($ActionEvent | ConvertTo-Json -Compress -Depth 100)) {
        throw [IO.InvalidDataException]::new('M0M009 action event must be the exact unique action_observation present in the raw event sequence.')
    }
    if ($ActionEvent.payload.outcome -cnotin @('observed', 'budget_exceeded')) {
        throw [IO.InvalidDataException]::new('M0M009 only observed or budget_exceeded actions may resolve measurement boundaries.')
    }
    $scenario = Get-M0MarkerScenario -Catalog $Catalog -ScenarioId $ScenarioId
    $measurements = @($scenario.measurements | Where-Object metric_id -CEQ $ActionEvent.payload.metric_id)
    if ($measurements.Count -ne 1) {
        throw [IO.InvalidDataException]::new("M0M009 action metric '$($ActionEvent.payload.metric_id)' has no unique catalog measurement.")
    }
    if ($null -eq $ActionEvent.payload.start_marker_event_seq -or $null -eq $ActionEvent.payload.end_marker_event_seq) {
        throw [IO.InvalidDataException]::new('M0M009 observed action requires start and end marker event sequences.')
    }
    $startMatches = @($Events | Where-Object { [int]$_.event_seq -eq [int]$ActionEvent.payload.start_marker_event_seq })
    $endMatches = @($Events | Where-Object { [int]$_.event_seq -eq [int]$ActionEvent.payload.end_marker_event_seq })
    if ($startMatches.Count -ne 1 -or $endMatches.Count -ne 1 -or
        $startMatches[0].event -cne 'marker' -or $endMatches[0].event -cne 'marker') {
        throw [IO.InvalidDataException]::new('M0M009 action references a nonexistent or non-marker event sequence.')
    }
    $start = $startMatches[0]
    $end = $endMatches[0]
    if ([int]$start.event_seq -ge [int]$end.event_seq -or [int]$end.event_seq -ge [int]$ActionEvent.event_seq) {
        throw [IO.InvalidDataException]::new('M0M009 boundary order requires start < end < action observation.')
    }
    $measurement = $measurements[0]
    if ((Get-M0MarkerKey -Value $start.payload) -cne $measurement.start_marker_key -or
        (Get-M0MarkerKey -Value $end.payload) -cne $measurement.end_marker_key) {
        throw [IO.InvalidDataException]::new('M0M009 action references the wrong marker pair, phase or metric endpoints.')
    }
    foreach ($endpoint in @($start, $end)) {
        if ($endpoint.payload.source -cne 'external_harness' -or $endpoint.payload.role -cne 'measurement_boundary') {
            throw [IO.InvalidDataException]::new('M0M009 action endpoints must be external measurement boundaries, never candidate diagnostics.')
        }
    }
    if ([bool]$measurement.correlated_occurrence -and
        ([string]::IsNullOrWhiteSpace([string]$start.payload.occurrence_id) -or
         $start.payload.occurrence_id -cne $end.payload.occurrence_id)) {
        throw [IO.InvalidDataException]::new('M0M009 correlated measurement endpoints require the same occurrence_id.')
    }
    if ([string]$start.qpc_frequency -cne [string]$end.qpc_frequency -or
        [string]$start.qpc_frequency -cne [string]$ActionEvent.qpc_frequency) {
        throw [IO.InvalidDataException]::new('M0M009 measurement endpoints and action require one QPC frequency.')
    }
    $frequency = [Numerics.BigInteger]::Parse([string]$start.qpc_frequency)
    $startTicks = [Numerics.BigInteger]::Parse([string]$start.qpc_ticks)
    $endTicks = [Numerics.BigInteger]::Parse([string]$end.qpc_ticks)
    if ($frequency -le 0 -or $endTicks -lt $startTicks) {
        throw [IO.InvalidDataException]::new('M0M009 QPC frequency/delta is invalid.')
    }
    $expectedNs = [Numerics.BigInteger]::Divide(($endTicks - $startTicks) * 1000000000L, $frequency)
    $actualNs = [Numerics.BigInteger]::Parse([string]$ActionEvent.payload.value_ns)
    if ($actualNs -ne $expectedNs) {
        throw [IO.InvalidDataException]::new("M0M009 QPC floor value_ns differs: expected $expectedNs, got $actualNs.")
    }
    if ($measurement.decision_role -ceq 'attempt_limit') {
        $limitNs = [Numerics.BigInteger]::Parse([string]$measurement.attempt_limit_ns)
        $expectedOutcome = if ($expectedNs -gt $limitNs) { 'budget_exceeded' } else { 'observed' }
        if ($ActionEvent.payload.outcome -cne $expectedOutcome) {
            throw [IO.InvalidDataException]::new("M0M009 attempt_limit metric '$($measurement.metric_id)' requires outcome '$expectedOutcome' at value $expectedNs with limit $limitNs.")
        }
    }
    elseif ($ActionEvent.payload.outcome -ceq 'budget_exceeded') {
        throw [IO.InvalidDataException]::new("M0M009 $($measurement.decision_role) metric '$($measurement.metric_id)' cannot produce per-attempt budget_exceeded.")
    }
    return [pscustomobject][ordered]@{
        metric_id = [string]$measurement.metric_id
        value_ns = [decimal]::Parse($expectedNs.ToString(), [Globalization.CultureInfo]::InvariantCulture)
        start_event_seq = [int]$start.event_seq
        end_event_seq = [int]$end.event_seq
        source = 'external_harness'
        decision_role = [string]$measurement.decision_role
        attempt_aggregate = [string]$measurement.attempt_aggregate
    }
}

function Get-M0MarkerInventory {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Events)

    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $result = [Collections.Generic.List[string]]::new()
    $null = $seen.Add('run-started')
    $result.Add('run-started')
    foreach ($event in @($Events | Where-Object event -CEQ 'marker')) {
        $markerId = [string]$event.payload.marker_id
        if ($seen.Add($markerId)) {
            $result.Add($markerId)
        }
    }
    return @($result)
}

function Assert-M0MarkerArtifacts {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $ProtocolRoot)

    $root = [IO.Path]::GetFullPath($ProtocolRoot)
    $catalogFile = Read-M0MarkerTextFile -LiteralPath (Join-Path $root 'markers.v1.json') -Name 'marker catalog'
    $sidecarFile = Read-M0MarkerTextFile -LiteralPath (Join-Path $root 'markers.v1.sha256') -Name 'marker catalog sidecar'
    $schemaFile = Read-M0MarkerTextFile -LiteralPath (Join-Path $root 'schemas\marker-catalog.schema.json') -Name 'marker catalog schema'
    $shellFile = Read-M0MarkerTextFile -LiteralPath (Join-Path $root 'scenarios\shell.v1.json') -Name 'shell scenario descriptor'
    $protocolFile = Read-M0MarkerTextFile -LiteralPath (Join-Path $root 'protocol.v1.json') -Name 'protocol descriptor'

    $catalogHash = Get-M0MarkerSha256Bytes -Bytes $catalogFile.bytes
    if ($sidecarFile.text -cnotmatch '^([a-f0-9]{64})  markers\.v1\.json\n$' -or $Matches[1] -cne $catalogHash) {
        throw [IO.InvalidDataException]::new('M0M001 marker catalog sidecar is noncanonical or its hash differs.')
    }
    $catalogJson = $catalogFile.text.Substring(0, $catalogFile.text.Length - 1)
    if (-not (Test-Json -Json $catalogJson -SchemaFile $schemaFile.path -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0M001 marker catalog failed marker-catalog.schema.json validation.')
    }
    $catalog = $catalogJson | ConvertFrom-Json -Depth 100
    $shell = $shellFile.text | ConvertFrom-Json -Depth 100
    $protocol = $protocolFile.text | ConvertFrom-Json -Depth 100
    if ($catalog.protocol_id -cne $protocol.protocol_id -or $catalog.scenario_set_id -cne $shell.scenario_set_id) {
        throw [IO.InvalidDataException]::new('M0M001 marker catalog protocol/scenario identity differs from trusted descriptors.')
    }
    if (-not $protocol.PSObject.Properties['marker_catalog_sha256'] -or
        $protocol.marker_catalog_sha256 -cne $catalogHash.ToUpperInvariant()) {
        throw [IO.InvalidDataException]::new('M0M001 protocol marker_catalog_sha256 anchor differs from the catalog bytes.')
    }
    $semantic = Assert-M0MarkerCatalog -Catalog $catalog -ShellScenarioIds @($shell.scenarios)
    return [pscustomobject][ordered]@{
        catalog_id = $semantic.catalog_id
        scenario_count = $semantic.scenario_count
        catalog_sha256 = $catalogHash
        catalog = $catalog
    }
}

Export-ModuleMember -Function @(
    'Assert-M0MarkerArtifacts',
    'Assert-M0MarkerCatalog',
    'Assert-M0MarkerSequence',
    'Get-M0MarkerInventory',
    'Resolve-M0MeasurementBoundary'
)
