$markersModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Markers.psm1'
$protocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$catalogPath = Join-Path $protocolRoot 'markers.v1.json'
$catalogSidecarPath = Join-Path $protocolRoot 'markers.v1.sha256'
$catalogSchemaPath = Join-Path $protocolRoot 'schemas\marker-catalog.schema.json'
$eventSchemaPath = Join-Path $protocolRoot 'schemas\event.schema.json'
$runSchemaPath = Join-Path $protocolRoot 'schemas\run.schema.json'

Test-Case 'The frozen marker contract exists before marker evidence is accepted' {
    foreach ($path in @($markersModulePath, $catalogPath, $catalogSidecarPath, $catalogSchemaPath)) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Missing marker contract '$path'."
    }
}

Import-Module $markersModulePath -Force -ErrorAction Stop
$markersModule = Get-Module -Name M0.Markers
$artifactResult = Assert-M0MarkerArtifacts -ProtocolRoot $protocolRoot
$catalog = $artifactResult.catalog

function Copy-MarkerTestObject {
    param([Parameter(Mandatory)] $Value)
    return $Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100
}

function New-MarkerTestEvent {
    param(
        [Parameter(Mandatory)] [int] $Sequence,
        [Parameter(Mandatory)] [string] $MarkerId,
        [Parameter(Mandatory)] [ValidateSet('begin', 'end', 'instant')] [string] $Phase,
        [Parameter(Mandatory)] [ValidateSet('external_harness', 'candidate_reported')] [string] $Source,
        [Parameter(Mandatory)] [ValidateSet('measurement_boundary', 'diagnostic')] [string] $Role,
        [AllowNull()] $OccurrenceId = $null,
        [long] $QpcTicks = $Sequence,
        [long] $QpcFrequency = 10000000
    )
    return [pscustomobject][ordered]@{
        event_seq = $Sequence
        qpc_ticks = $QpcTicks
        qpc_frequency = $QpcFrequency
        event = 'marker'
        payload = [pscustomobject][ordered]@{
            marker_id = $MarkerId
            phase = $Phase
            source = $Source
            role = $Role
            occurrence_id = $OccurrenceId
        }
    }
}

function New-MarkerTestAction {
    param(
        [Parameter(Mandatory)] [int] $Sequence,
        [Parameter(Mandatory)] [string] $MetricId,
        [Parameter(Mandatory)] [long] $ValueNs,
        [Parameter(Mandatory)] [int] $StartSequence,
        [Parameter(Mandatory)] [int] $EndSequence,
        [ValidateSet('external_harness', 'candidate_reported')] [string] $Source = 'external_harness',
        [long] $QpcTicks = $Sequence,
        [long] $QpcFrequency = 10000000
    )
    return [pscustomobject][ordered]@{
        event_seq = $Sequence
        qpc_ticks = $QpcTicks
        qpc_frequency = $QpcFrequency
        event = 'action_observation'
        payload = [pscustomobject][ordered]@{
            metric_id = $MetricId
            value_ns = $ValueNs
            outcome = 'observed'
            source = $Source
            start_marker_event_seq = $StartSequence
            end_marker_event_seq = $EndSequence
            reason_code = $null
            error_code = $null
        }
    }
}

function New-MarkerTestMetric {
    param(
        [Parameter(Mandatory)] [int] $Sequence,
        [Parameter(Mandatory)] [string] $MetricId,
        [Parameter(Mandatory)] [ValidateSet('bytes', 'basis_points')] [string] $Unit,
        [AllowNull()] $ValueInteger = 1,
        [AllowNull()] $ErrorCode = $null,
        [long] $QpcTicks = $Sequence,
        [long] $QpcFrequency = 10000000
    )
    return [pscustomobject][ordered]@{
        event_seq = $Sequence
        qpc_ticks = $QpcTicks
        qpc_frequency = $QpcFrequency
        event = 'resource_sample'
        payload = [pscustomobject][ordered]@{
            metric_id = $MetricId
            value_integer = $ValueInteger
            unit = $Unit
            error_code = $ErrorCode
        }
    }
}

function New-ValidProcessColdMarkerEvents {
    param([long] $EndTicks = 8000000)
    return @(
        (New-MarkerTestEvent -Sequence 1 -MarkerId 'launch-input' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 0),
        (New-MarkerTestEvent -Sequence 2 -MarkerId 'first-frame-externally-visible' -Phase instant -Source external_harness -Role diagnostic -QpcTicks ([long]($EndTicks / 2))),
        (New-MarkerTestEvent -Sequence 3 -MarkerId 'window-actionable' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks $EndTicks),
        (New-MarkerTestEvent -Sequence 4 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks $EndTicks)
    )
}

function New-MinimalValidScenarioMarkerEvents {
    param([Parameter(Mandatory)] $Scenario)

    if ($Scenario.scenario_id -ceq 'queue-stall') {
        return @(Add-MarkerTestActions -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 1))
    }
    if ($Scenario.scenario_id -cin @('idle-memory', 'paused-cpu', 'active-progress')) {
        $stabilizationNs = switch ($Scenario.scenario_id) {
            'paused-cpu' { 30000000000L }
            'active-progress' { 10000000000L }
            default { 0L }
        }
        return @(New-ResourceWindowMarkerEvents -ScenarioId $Scenario.scenario_id -StabilizationDurationNs $stabilizationNs)
    }
    $events = [Collections.Generic.List[object]]::new()
    $sequence = 1
    foreach ($marker in @($Scenario.markers)) {
        $count = if (@($marker.cardinality.required_statuses) -ccontains 'valid') { [int]$marker.cardinality.minimum } else { 0 }
        for ($occurrenceIndex = 1; $occurrenceIndex -le $count; $occurrenceIndex++) {
            $occurrenceId = if ($marker.occurrence_id_policy -ceq 'required_unique') { 'occurrence-{0:D4}' -f $occurrenceIndex } else { $null }
            $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId $marker.marker_id -Phase $marker.phase -Source $marker.source -Role $marker.role -OccurrenceId $occurrenceId -QpcTicks ([long]($sequence - 1) * 10000000L)))
            $sequence++
        }
    }
    return @(Add-MarkerTestActions -ScenarioId $Scenario.scenario_id -Events @($events))
}

function New-ResourceWindowMarkerEvents {
    param(
        [Parameter(Mandatory)] [ValidateSet('idle-memory', 'paused-cpu', 'active-progress')] [string] $ScenarioId,
        [long] $StabilizationDurationNs,
        [long] $SamplingDurationNs = 60000000000L,
        [long] $Memory5sOffsetNs = 5000000000L
    )

    $frequency = 1000000000L
    $origin = 1000000000L
    if ($ScenarioId -ceq 'idle-memory') {
        return @(
            (New-MarkerTestEvent -Sequence 1 -MarkerId 'idle-sampling-window' -Phase begin -Source external_harness -Role measurement_boundary -QpcTicks $origin -QpcFrequency $frequency),
            (New-MarkerTestEvent -Sequence 2 -MarkerId 'memory-5s-sample' -Phase instant -Source external_harness -Role diagnostic -QpcTicks ($origin + $Memory5sOffsetNs) -QpcFrequency $frequency),
            (New-MarkerTestMetric -Sequence 3 -MetricId 'memory_5s' -Unit bytes -ValueInteger 64 -QpcTicks ($origin + $Memory5sOffsetNs) -QpcFrequency $frequency),
            (New-MarkerTestEvent -Sequence 4 -MarkerId 'idle-sampling-window' -Phase end -Source external_harness -Role measurement_boundary -QpcTicks ($origin + $SamplingDurationNs) -QpcFrequency $frequency),
            (New-MarkerTestMetric -Sequence 5 -MetricId 'memory_60s' -Unit bytes -ValueInteger 65 -QpcTicks ($origin + $SamplingDurationNs) -QpcFrequency $frequency),
            (New-MarkerTestMetric -Sequence 6 -MetricId 'memory_peak' -Unit bytes -ValueInteger 66 -QpcTicks ($origin + $SamplingDurationNs) -QpcFrequency $frequency),
            (New-MarkerTestEvent -Sequence 7 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks ($origin + $SamplingDurationNs) -QpcFrequency $frequency)
        )
    }

    $prefix = if ($ScenarioId -ceq 'paused-cpu') { 'paused' } else { 'active' }
    return @(
        (New-MarkerTestEvent -Sequence 1 -MarkerId "$prefix-stabilization-window" -Phase begin -Source external_harness -Role diagnostic -QpcTicks $origin -QpcFrequency $frequency),
        (New-MarkerTestEvent -Sequence 2 -MarkerId "$prefix-sampling-window" -Phase begin -Source external_harness -Role measurement_boundary -QpcTicks ($origin + $StabilizationDurationNs) -QpcFrequency $frequency),
        (New-MarkerTestEvent -Sequence 3 -MarkerId "$prefix-sampling-window" -Phase end -Source external_harness -Role measurement_boundary -QpcTicks ($origin + $StabilizationDurationNs + $SamplingDurationNs) -QpcFrequency $frequency),
        (New-MarkerTestMetric -Sequence 4 -MetricId 'tree_cpu_percent' -Unit basis_points -ValueInteger 5 -QpcTicks ($origin + $StabilizationDurationNs + $SamplingDurationNs) -QpcFrequency $frequency),
        (New-MarkerTestEvent -Sequence 5 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks ($origin + $StabilizationDurationNs + $SamplingDurationNs) -QpcFrequency $frequency)
    )
}

function New-QueueMarkerEvents {
    param(
        [Parameter(Mandatory)] [int] $PairCount,
        [long] $WindowStartTicks = 1000000000,
        [long] $InputIntervalTicks = 500000,
        [long] $AckDelayTicks = 100000
    )
    $events = [Collections.Generic.List[object]]::new()
    $sequence = 1
    $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId 'queue-probe-window' -Phase begin -Source external_harness -Role diagnostic -QpcTicks $WindowStartTicks))
    $sequence++
    for ($index = 1; $index -le $PairCount; $index++) {
        $occurrence = 'probe-{0:D4}' -f $index
        $inputTicks = $WindowStartTicks + ([long]($index - 1) * $InputIntervalTicks)
        $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId 'focus-probe-input' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId $occurrence -QpcTicks $inputTicks))
        $sequence++
        $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId 'focus-visible-ack' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId $occurrence -QpcTicks ($inputTicks + $AckDelayTicks)))
        $sequence++
    }
    $completeTicks = if ($PairCount -eq 0) { $WindowStartTicks } else { [long]$events[$events.Count - 1].qpc_ticks }
    $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId 'queue-accessibility-verified' -Phase instant -Source external_harness -Role diagnostic -QpcTicks $completeTicks))
    $sequence++
    $events.Add((New-MarkerTestEvent -Sequence $sequence -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks $completeTicks))
    return @($events)
}

function Add-MarkerTestActions {
    param(
        [Parameter(Mandatory)] [string] $ScenarioId,
        [Parameter(Mandatory)] [object[]] $Events
    )

    $scenario = @($catalog.scenarios | Where-Object scenario_id -CEQ $ScenarioId)[0]
    $result = [Collections.Generic.List[object]]::new()
    foreach ($event in $Events) { $result.Add($event) }
    $nextSequence = if ($result.Count -eq 0) { 1 } else { [int](($result | Measure-Object -Property event_seq -Maximum).Maximum) + 1 }
    foreach ($measurement in @($scenario.measurements)) {
        $startMarkers = @($Events | Where-Object { $_.event -ceq 'marker' -and ('{0}:{1}' -f $_.payload.marker_id, $_.payload.phase) -ceq $measurement.start_marker_key })
        foreach ($start in $startMarkers) {
            $endMarkers = @($Events | Where-Object {
                $_.event -ceq 'marker' -and
                ('{0}:{1}' -f $_.payload.marker_id, $_.payload.phase) -ceq $measurement.end_marker_key -and
                (-not [bool]$measurement.correlated_occurrence -or $_.payload.occurrence_id -ceq $start.payload.occurrence_id)
            })
            if ($endMarkers.Count -ne 1) { continue }
            $end = $endMarkers[0]
            $valueNs = [long](([Numerics.BigInteger]::Parse([string]$end.qpc_ticks) - [Numerics.BigInteger]::Parse([string]$start.qpc_ticks)) * 1000000000L / [Numerics.BigInteger]::Parse([string]$start.qpc_frequency))
            $action = New-MarkerTestAction -Sequence $nextSequence -MetricId $measurement.metric_id -ValueNs $valueNs -StartSequence $start.event_seq -EndSequence $end.event_seq -QpcTicks ([long]$end.qpc_ticks) -QpcFrequency ([long]$end.qpc_frequency)
            if ($measurement.decision_role -ceq 'attempt_limit' -and $valueNs -gt [long]$measurement.attempt_limit_ns) {
                $action.payload.outcome = 'budget_exceeded'
                $action.payload.reason_code = 'budget_violated'
                $action.payload.error_code = 'attempt_limit_exceeded'
            }
            $result.Add($action)
            $nextSequence++
        }
    }
    return @($result)
}

Test-Case 'The marker module exports only its approved API' {
    Assert-SequenceEqual -Actual @($markersModule.ExportedFunctions.Keys | Sort-Object) -Expected @(
        'Assert-M0MarkerArtifacts',
        'Assert-M0MarkerCatalog',
        'Assert-M0MarkerSequence',
        'Get-M0MarkerInventory',
        'Resolve-M0MeasurementBoundary'
    )
}

Test-Case 'Evidence schemas expose closed marker, action and run action projection fields' {
    $eventSchema = Get-Content -Raw -LiteralPath $eventSchemaPath | ConvertFrom-Json -Depth 100
    $runSchema = Get-Content -Raw -LiteralPath $runSchemaPath | ConvertFrom-Json -Depth 100
    Assert-SequenceEqual -Actual @($eventSchema.definitions.marker_payload.required) -Expected @(
        'marker_id', 'phase', 'source', 'role', 'occurrence_id'
    )
    Assert-SequenceEqual -Actual @($eventSchema.definitions.action_payload.required) -Expected @(
        'metric_id', 'value_ns', 'outcome', 'source', 'start_marker_event_seq',
        'end_marker_event_seq', 'reason_code', 'error_code'
    )
    Assert-True (@($runSchema.required) -ccontains 'actions') 'run.schema must require the raw action projection.'
    Assert-True ($null -ne $runSchema.properties.actions) 'run.schema actions property is missing.'
}

Test-Case 'The catalog is anchored and covers every shell scenario once in frozen order' {
    Assert-Equal -Actual $artifactResult.catalog_id -Expected 'M0-MARKERS-1'
    Assert-Equal -Actual $artifactResult.scenario_count -Expected 11
    Assert-True ($artifactResult.catalog_sha256 -match '^[a-f0-9]{64}$')
    Assert-SequenceEqual -Actual @($catalog.scenarios.scenario_id) -Expected @(
        'profile-first', 'process-cold', 'warm', 'idle-memory', 'paused-cpu',
        'active-progress', 'visual-response-diagnostic', 'queue-stall',
        'a11y-manual', 'tray-basic', 'linux-smoke'
    )
    Assert-Equal -Actual $catalog.queue_probe_policy.window_interval -Expected '[t0,t0+5s)'
    Assert-Equal -Actual $catalog.queue_probe_policy.maximum_complete_pairs -Expected 100
    Assert-True $catalog.queue_probe_policy.ack_after_window_closes_pair
    Assert-True $catalog.queue_probe_policy.input_at_or_after_window_end_forbidden
    Assert-SequenceEqual -Actual @(
        $catalog.scenarios[0].measurements[0].decision_role,
        $catalog.scenarios[1].measurements[0].decision_role,
        $catalog.scenarios[2].measurements[0].decision_role
    ) -Expected @('diagnostic_only', 'batch_p95', 'diagnostic_only')
    Assert-Equal -Actual $catalog.scenarios[7].measurements[0].decision_role -Expected 'attempt_limit'
    Assert-Equal -Actual $catalog.scenarios[7].measurements[0].attempt_limit_ns -Expected 100000000
}

Test-Case 'Frozen measurement decision policies reject semantic downgrades even on an already loaded catalog' {
    foreach ($launchPolicy in @(
        @{ scenario_index = 0; expected_role = 'diagnostic_only' },
        @{ scenario_index = 1; expected_role = 'batch_p95' },
        @{ scenario_index = 2; expected_role = 'diagnostic_only' }
    )) {
        $mutated = Copy-MarkerTestObject $catalog
        $mutated.scenarios[$launchPolicy.scenario_index].measurements[0].decision_role = if ($launchPolicy.expected_role -ceq 'batch_p95') { 'diagnostic_only' } else { 'batch_p95' }
        Assert-Throws { Assert-M0MarkerCatalog -Catalog $mutated -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|decision|policy|role'
    }

    $visualDowngrade = Copy-MarkerTestObject $catalog
    $visualDowngrade.scenarios[6].measurements[0].decision_role = 'batch_p95'
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $visualDowngrade -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|decision|policy|role'

    $queueDowngrade = Copy-MarkerTestObject $catalog
    $queueDowngrade.scenarios[7].measurements[0].decision_role = 'batch_p95'
    $queueDowngrade.scenarios[7].measurements[0].attempt_limit_ns = $null
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $queueDowngrade -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|decision|policy|role'

    $queueLimitDrift = Copy-MarkerTestObject $catalog
    $queueLimitDrift.scenarios[7].measurements[0].attempt_limit_ns = 99999999
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $queueLimitDrift -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|decision|policy|limit'

    $queueAggregateDrift = Copy-MarkerTestObject $catalog
    $queueAggregateDrift.scenarios[7].measurements[0].attempt_aggregate = 'identity'
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $queueAggregateDrift -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|decision|policy|aggregate'
}

Test-Case 'The loaded catalog rejects any drift from the frozen marker and measurement semantics' {
    $weakenedA11y = Copy-MarkerTestObject $catalog
    $weakenedA11y.scenarios[8].markers[0].cardinality.minimum = 0
    $weakenedA11y.scenarios[8].markers[0].cardinality.required_statuses = @()
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $weakenedA11y -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|fingerprint|frozen|semantic'

    $missingDependency = Copy-MarkerTestObject $catalog
    $missingDependency.scenarios[8].markers[1].after = @()
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $missingDependency -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|fingerprint|frozen|semantic'

    $renamedMarker = Copy-MarkerTestObject $catalog
    $renamedMarker.scenarios[8].markers[0].marker_id = 'renamed-accessibility-check'
    $renamedMarker.scenarios[8].markers[1].after = @('renamed-accessibility-check:instant')
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $renamedMarker -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|fingerprint|frozen|semantic'

    $removedMeasurement = Copy-MarkerTestObject $catalog
    $removedMeasurement.scenarios[1].measurements = @()
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $removedMeasurement -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|fingerprint|frozen|semantic'

    $extraMeasurement = Copy-MarkerTestObject $catalog
    $extraMeasurement.scenarios[0].measurements += Copy-MarkerTestObject $extraMeasurement.scenarios[0].measurements[0]
    $extraMeasurement.scenarios[0].measurements[1].metric_id = 'invented_launch_metric_ns'
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $extraMeasurement -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|fingerprint|frozen|semantic'
}

Test-Case 'Resource sampling policies enforce frozen stabilization, collection and memory sample windows with exact QPC arithmetic' {
    Assert-SequenceEqual -Actual @($catalog.resource_sampling_policies.scenario_id) -Expected @('idle-memory', 'paused-cpu', 'active-progress')
    Assert-SequenceEqual -Actual @($catalog.resource_sampling_policies.minimum_stabilization_ns) -Expected @(0, 30000000000, 10000000000)
    Assert-SequenceEqual -Actual @($catalog.resource_sampling_policies.sampling_duration_ns) -Expected @(60000000000, 60000000000, 60000000000)
    Assert-Equal -Actual $catalog.resource_sampling_policies[0].memory_sample_offset_ns -Expected 5000000000
    Assert-Equal -Actual $catalog.resource_sampling_policies[0].memory_sample_tolerance_ns -Expected 125000000

    foreach ($case in @(
        @{ scenario = 'idle-memory'; stabilization = 0L },
        @{ scenario = 'paused-cpu'; stabilization = 30000000000L },
        @{ scenario = 'active-progress'; stabilization = 10000000000L }
    )) {
        $valid = New-ResourceWindowMarkerEvents -ScenarioId $case.scenario -StabilizationDurationNs $case.stabilization
        $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $valid -Status valid
        Assert-Equal -Actual $result.marker_event_count -Expected 4

        foreach ($delta in @(-1L, 1L)) {
            $wrongSampling = New-ResourceWindowMarkerEvents -ScenarioId $case.scenario -StabilizationDurationNs $case.stabilization -SamplingDurationNs (60000000000L + $delta)
            Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $wrongSampling -Status valid } 'M0M007|sampling|60'
        }
    }

    foreach ($case in @(
        @{ scenario = 'paused-cpu'; stabilization = 30000000000L },
        @{ scenario = 'active-progress'; stabilization = 10000000000L }
    )) {
        $shortStabilization = New-ResourceWindowMarkerEvents -ScenarioId $case.scenario -StabilizationDurationNs ($case.stabilization - 1L)
        Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $shortStabilization -Status valid } 'M0M007|stabilization'

        $aboveMinimum = New-ResourceWindowMarkerEvents -ScenarioId $case.scenario -StabilizationDurationNs ($case.stabilization + 1L)
        $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $aboveMinimum -Status valid
        Assert-Equal -Actual $result.marker_event_count -Expected 4
    }

    foreach ($offset in @(4874999999L, 5125000001L)) {
        $wrongMemorySample = New-ResourceWindowMarkerEvents -ScenarioId 'idle-memory' -StabilizationDurationNs 0 -Memory5sOffsetNs $offset
        Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'idle-memory' -Events $wrongMemorySample -Status valid } 'M0M007|memory.5s|125'
    }
    foreach ($offset in @(4875000000L, 5125000000L)) {
        $boundaryMemorySample = New-ResourceWindowMarkerEvents -ScenarioId 'idle-memory' -StabilizationDurationNs 0 -Memory5sOffsetNs $offset
        $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'idle-memory' -Events $boundaryMemorySample -Status valid
        Assert-Equal -Actual $result.marker_event_count -Expected 4
    }
}

Test-Case 'Valid resource scenarios require exact anchored metrics while non-valid runs may omit them' {
    foreach ($case in @(
        @{ scenario = 'idle-memory'; stabilization = 0L; required = @('memory_5s', 'memory_60s', 'memory_peak') },
        @{ scenario = 'paused-cpu'; stabilization = 30000000000L; required = @('tree_cpu_percent') },
        @{ scenario = 'active-progress'; stabilization = 10000000000L; required = @('tree_cpu_percent') }
    )) {
        $valid = @(New-ResourceWindowMarkerEvents -ScenarioId $case.scenario -StabilizationDurationNs $case.stabilization)
        foreach ($metricId in $case.required) {
            $missing = @($valid | Where-Object { $_.event -cne 'resource_sample' -or $_.payload.metric_id -cne $metricId })
            Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $missing -Status valid } 'M0M007|required|resource|metric'
        }

        $firstMetric = @($valid | Where-Object event -CEQ 'resource_sample')[0]
        $duplicate = @($valid) + @(Copy-MarkerTestObject $firstMetric)
        $duplicate[-1].event_seq = 100
        Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $duplicate -Status valid } 'M0M007|duplicate|resource|metric'

        foreach ($mutation in @('unit', 'anchor', 'negative', 'error')) {
            $invalid = Copy-MarkerTestObject $valid
            $metric = @($invalid | Where-Object event -CEQ 'resource_sample')[0]
            switch ($mutation) {
                'unit' { $metric.payload.unit = if ($metric.payload.unit -ceq 'bytes') { 'basis_points' } else { 'bytes' } }
                'anchor' { $metric.qpc_ticks = [long]$metric.qpc_ticks + 1L }
                'negative' { $metric.payload.value_integer = -1 }
                'error' { $metric.payload.error_code = 'unexpected_metric_error' }
            }
            Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $invalid -Status valid } 'M0M007|resource|metric|anchor|unit|value|error'
        }

        $omittedNonValid = @($valid | Where-Object event -CNE 'resource_sample')
        $failedResult = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $case.scenario -Events $omittedNonValid -Status failed
        Assert-True ($failedResult.marker_event_count -gt 0)
    }
}

Test-Case 'Catalog semantics reject missing dependencies, indirect cycles and candidate boundaries' {
    $candidateBoundary = Copy-MarkerTestObject $catalog
    $candidateBoundary.scenarios[0].markers[0].source = 'candidate_reported'
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $candidateBoundary -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|candidate_reported|measurement_boundary'

    $missingDependency = Copy-MarkerTestObject $catalog
    $missingDependency.scenarios[1].markers[1].after = @('missing-marker:instant')
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $missingDependency -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M004|dependency'

    $cycle = Copy-MarkerTestObject $catalog
    $cycle.scenarios[1].markers[0].after = @('window-actionable:instant')
    $cycle.scenarios[1].markers[1].after = @('launch-input:instant')
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $cycle -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M004|cycle'

    $missingScenario = Copy-MarkerTestObject $catalog
    $missingScenario.scenarios = @($missingScenario.scenarios | Select-Object -Skip 1)
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $missingScenario -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M002|scenario'

    $extraScenario = Copy-MarkerTestObject $catalog
    $extraScenario.scenarios += Copy-MarkerTestObject $extraScenario.scenarios[0]
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $extraScenario -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M002|scenario|duplicate'

    $swappedScenario = Copy-MarkerTestObject $catalog
    $swappedScenario.scenarios[0], $swappedScenario.scenarios[1] = $swappedScenario.scenarios[1], $swappedScenario.scenarios[0]
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $swappedScenario -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M002|order'

    $duplicateMarker = Copy-MarkerTestObject $catalog
    $duplicateMarker.scenarios[1].markers += Copy-MarkerTestObject $duplicateMarker.scenarios[1].markers[0]
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $duplicateMarker -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|duplicate'

    $negativeMinimum = Copy-MarkerTestObject $catalog
    $negativeMinimum.scenarios[1].markers[0].cardinality.minimum = -1
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $negativeMinimum -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|cardinality'

    $reversedRange = Copy-MarkerTestObject $catalog
    $reversedRange.scenarios[1].markers[0].cardinality.maximum = 0
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $reversedRange -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|cardinality|required'

    $selfCycle = Copy-MarkerTestObject $catalog
    $selfCycle.scenarios[1].markers[0].after = @('launch-input:instant')
    Assert-Throws { Assert-M0MarkerCatalog -Catalog $selfCycle -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M004|self|cycle'

    foreach ($mutation in @(
        @{ property = 'source'; value = 'unknown_source' },
        @{ property = 'role'; value = 'unknown_role' },
        @{ property = 'occurrence_id_policy'; value = 'sometimes' }
    )) {
        $unknownEnum = Copy-MarkerTestObject $catalog
        $unknownEnum.scenarios[1].markers[0].($mutation.property) = $mutation.value
        Assert-Throws { Assert-M0MarkerCatalog -Catalog $unknownEnum -ShellScenarioIds @($catalog.scenarios.scenario_id) } 'M0M003|unknown'
    }
}

Test-Case 'Marker sequence validation enforces identity, source, role, cardinality and order' {
    $valid = Add-MarkerTestActions -ScenarioId 'process-cold' -Events (New-ValidProcessColdMarkerEvents)
    $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $valid -Status valid
    Assert-Equal -Actual $result.marker_event_count -Expected 4

    $unknown = @($valid)
    $unknown[2] = New-MarkerTestEvent -Sequence 3 -MarkerId 'invented' -Phase instant -Source external_harness -Role measurement_boundary
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $unknown -Status valid } 'M0M005|unknown'

    $spoofed = @($valid)
    $spoofed[2] = New-MarkerTestEvent -Sequence 3 -MarkerId 'window-actionable' -Phase instant -Source candidate_reported -Role diagnostic
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $spoofed -Status valid } 'M0M005|source|role'

    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events @($valid[0], $valid[2], $valid[3]) -Status valid } 'M0M006|required|cardinality'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events @($valid[1], $valid[0], $valid[2], $valid[3]) -Status valid } 'M0M007|order|predecessor'
    $failedResult = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events @() -Status failed
    Assert-Equal -Actual $failedResult.marker_event_count -Expected 0
    foreach ($nonValidStatus in @('discarded', 'inconclusive')) {
        $emptyResult = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events @() -Status $nonValidStatus
        Assert-Equal -Actual $emptyResult.marker_event_count -Expected 0
    }

    $repeatableMissingOccurrence = New-QueueMarkerEvents -PairCount 1
    $repeatableMissingOccurrence[1].payload.occurrence_id = $null
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $repeatableMissingOccurrence -Status valid } 'M0M005|occurrence|required'

    $nonRepeatableOccurrence = Copy-MarkerTestObject $valid
    $nonRepeatableOccurrence[0].payload.occurrence_id = 'unexpected'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $nonRepeatableOccurrence -Status valid } 'M0M005|occurrence|forbidden'
    $nonRepeatableOccurrence[0].payload.occurrence_id = ''
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $nonRepeatableOccurrence -Status valid } 'M0M005|occurrence|forbidden'
}

Test-Case 'Required-unique occurrence identifiers are unique per repeatable marker key' {
    $repeatableCatalog = Copy-MarkerTestObject $catalog
    $repeatableCatalog.scenarios[1].markers[2].occurrence_id_policy = 'required_unique'
    $repeatableCatalog.scenarios[1].markers[2].cardinality.maximum = 2
    $events = @(
        (New-MarkerTestEvent -Sequence 1 -MarkerId 'launch-input' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 0),
        (New-MarkerTestEvent -Sequence 2 -MarkerId 'first-frame-externally-visible' -Phase instant -Source external_harness -Role diagnostic -QpcTicks 2000000),
        (New-MarkerTestEvent -Sequence 3 -MarkerId 'window-actionable' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId 'duplicate-occurrence' -QpcTicks 4000000),
        (New-MarkerTestEvent -Sequence 4 -MarkerId 'window-actionable' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId 'duplicate-occurrence' -QpcTicks 5000000),
        (New-MarkerTestEvent -Sequence 5 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks 5000000),
        (New-MarkerTestAction -Sequence 6 -MetricId 'window_actionable_ns' -ValueNs 400000000 -StartSequence 1 -EndSequence 3 -QpcTicks 5000000)
    )
    Assert-Throws { Assert-M0MarkerSequence -Catalog $repeatableCatalog -ScenarioId 'process-cold' -Events $events -Status valid } 'M0M005|occurrence|unique|duplicate'
}

Test-Case 'Marker identity matching is ordinal and rejects case-spoofed unknown markers' {
    $events = @(
        (New-MarkerTestEvent -Sequence 1 -MarkerId 'launch-input' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 0),
        (New-MarkerTestEvent -Sequence 2 -MarkerId 'first-frame-externally-visible' -Phase instant -Source external_harness -Role diagnostic -QpcTicks 2000000),
        (New-MarkerTestEvent -Sequence 3 -MarkerId 'window-actionable' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 8000000),
        (New-MarkerTestEvent -Sequence 4 -MarkerId 'Launch-input' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 8000000),
        (New-MarkerTestEvent -Sequence 5 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks 8000000),
        (New-MarkerTestAction -Sequence 6 -MetricId 'window_actionable_ns' -ValueNs 800000000 -StartSequence 1 -EndSequence 3 -QpcTicks 8000000)
    )
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $events -Status valid } 'M0M005|unknown|case|identity'
}

Test-Case 'Every materialized shell scenario has a minimal valid ordered marker sequence' {
    foreach ($scenario in @($catalog.scenarios)) {
        $events = New-MinimalValidScenarioMarkerEvents -Scenario $scenario
        $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $scenario.scenario_id -Events $events -Status valid
        Assert-True ($result.marker_event_count -gt 0) "Scenario '$($scenario.scenario_id)' did not materialize required markers."
    }
}

Test-Case 'Valid action cardinality is complete and one to one with catalog measurements' {
    $markers = New-ValidProcessColdMarkerEvents
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $markers -Status valid } 'M0M009|exactly one|action'

    $valid = Add-MarkerTestActions -ScenarioId 'process-cold' -Events $markers
    $duplicate = Copy-MarkerTestObject $valid[-1]
    $duplicate.event_seq = [int]$valid[-1].event_seq + 1
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events @($valid + $duplicate) -Status valid } 'M0M009|exactly one|duplicate'

    $queueMarkers = New-QueueMarkerEvents -PairCount 2
    $onlyFirstAction = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 1) | Where-Object event -CEQ 'action_observation' | Select-Object -First 1
    $onlyFirstAction.event_seq = [int](($queueMarkers | Measure-Object event_seq -Maximum).Maximum) + 1
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($queueMarkers + $onlyFirstAction) -Status valid } 'M0M009|requires exactly one|queue pair'

    $queueValid = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events $queueMarkers
    $orphan = Copy-MarkerTestObject $queueValid[-1]
    $orphan.event_seq = [int]$queueValid[-1].event_seq + 1
    $orphan.payload.start_marker_event_seq = 999999
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($queueValid + $orphan) -Status valid } 'M0M009|nonexistent|orphan'
}

Test-Case 'Candidate self reports stay optional diagnostics and never replace external launch boundaries' {
    $external = New-ValidProcessColdMarkerEvents
    $reported = New-MarkerTestEvent -Sequence 4 -MarkerId 'candidate-ready-reported' -Phase instant -Source candidate_reported -Role diagnostic -QpcTicks $external[2].qpc_ticks
    $complete = Copy-MarkerTestObject $external[3]
    $complete.event_seq = 5
    $withReportMarkers = @($external[0], $external[1], $external[2], $reported, $complete)
    $withReport = Add-MarkerTestActions -ScenarioId 'process-cold' -Events $withReportMarkers
    $null = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $withReport -Status valid

    $secondReport = Copy-MarkerTestObject $reported
    $secondReport.event_seq = 5
    $complete.event_seq = 6
    $duplicateReports = Add-MarkerTestActions -ScenarioId 'process-cold' -Events @($external[0], $external[1], $external[2], $reported, $secondReport, $complete)
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'process-cold' -Events $duplicateReports -Status valid } 'M0M006|maximum'

    $action = @($withReport | Where-Object event -CEQ 'action_observation')[0]
    $resolved = Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events $withReport -ActionEvent $action
    Assert-Equal -Actual $resolved.end_event_seq -Expected 3
}

Test-Case 'Queue probes form one to one correlated pairs inside the frozen 1 through 100 cardinality' {
    $oneEvents = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 1)
    $one = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $oneEvents -Status valid
    Assert-Equal -Actual $one.complete_pair_count -Expected 1
    $hundredEvents = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 100)
    $hundred = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $hundredEvents -Status valid
    Assert-Equal -Actual $hundred.complete_pair_count -Expected 100

    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 0) -Status valid } 'M0M008|pair|cardinality'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 101) -Status valid } 'M0M008|100'

    $reused = New-QueueMarkerEvents -PairCount 2
    $reused[2].payload.occurrence_id = 'probe-0001'
    $reused[3].payload.occurrence_id = 'probe-0001'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $reused -Status valid } 'M0M008|unique|occurrence'

    $crossed = New-QueueMarkerEvents -PairCount 2
    $crossed[1].payload.occurrence_id = 'probe-0002'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $crossed -Status valid } 'M0M008|pair|occurrence'

    $unmatchedOrder = New-QueueMarkerEvents -PairCount 2
    $unmatchedOrder[2], $unmatchedOrder[3] = $unmatchedOrder[3], $unmatchedOrder[2]
    for ($eventIndex = 0; $eventIndex -lt $unmatchedOrder.Count; $eventIndex++) {
        $unmatchedOrder[$eventIndex].event_seq = $eventIndex + 1
    }
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $unmatchedOrder -Status valid } 'M0M008|alternate|pair'

    $atWindowEnd = New-QueueMarkerEvents -PairCount 2
    $atWindowEnd[3].qpc_ticks = [long]$atWindowEnd[0].qpc_ticks + 50000000L
    $atWindowEnd[4].qpc_ticks = [long]$atWindowEnd[3].qpc_ticks + 1L
    $atWindowEnd[5].qpc_ticks = $atWindowEnd[4].qpc_ticks
    $atWindowEnd[6].qpc_ticks = $atWindowEnd[5].qpc_ticks
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $atWindowEnd -Status valid } 'M0M008|half.open|window'

    $tooSoon = New-QueueMarkerEvents -PairCount 2 -InputIntervalTicks 499999
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $tooSoon -Status valid } 'M0M008|50|cadence'

    $beforeAck = New-QueueMarkerEvents -PairCount 2 -AckDelayTicks 600000
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $beforeAck -Status valid } 'M0M008|previous ack|QPC'

    $ackAfterWindow = New-QueueMarkerEvents -PairCount 100
    $ackAfterWindow[-3].qpc_ticks = [long]$ackAfterWindow[0].qpc_ticks + 50000001L
    $ackAfterWindow[-2].qpc_ticks = $ackAfterWindow[-3].qpc_ticks
    $ackAfterWindow[-1].qpc_ticks = $ackAfterWindow[-3].qpc_ticks
    $ackAfterWindowEvents = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events $ackAfterWindow
    $afterWindowResult = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $ackAfterWindowEvents -Status valid
    Assert-Equal -Actual $afterWindowResult.complete_pair_count -Expected 100
}

Test-Case 'A queue failure may preserve one terminal input without inventing an ack' {
    $window = New-MarkerTestEvent -Sequence 1 -MarkerId 'queue-probe-window' -Phase begin -Source external_harness -Role diagnostic -QpcTicks 1000000000
    $probeInput = New-MarkerTestEvent -Sequence 2 -MarkerId 'focus-probe-input' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId 'probe-0001' -QpcTicks 1000000000
    $failure = New-MarkerTestAction -Sequence 3 -MetricId 'queue_focus_stall_ns' -ValueNs 0 -StartSequence 1 -EndSequence 2 -QpcTicks 1000000001
    $failure.payload.outcome = 'timeout'
    $failure.payload.value_ns = $null
    $failure.payload.start_marker_event_seq = $null
    $failure.payload.end_marker_event_seq = $null
    $failure.payload.reason_code = 'scenario_timeout'
    $failure.payload.error_code = 'queue_probe_timeout'
    $result = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($window, $probeInput, $failure) -Status failed
    Assert-Equal -Actual $result.complete_pair_count -Expected 0
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($window, $probeInput) -Status failed } 'M0M008|unmatched|terminal'
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($window, $probeInput, $failure) -Status valid } 'M0M008|unmatched|terminal'

    $lateInput = Copy-MarkerTestObject $probeInput
    $lateInput.qpc_ticks = [long]$window.qpc_ticks + 50000000L
    $lateFailure = Copy-MarkerTestObject $failure
    $lateFailure.qpc_ticks = $lateInput.qpc_ticks + 1L
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events @($window, $lateInput, $lateFailure) -Status failed } 'M0M008|half.open|t0'
}

Test-Case 'Measurement resolution uses external boundaries and exact floor QPC arithmetic above 2^53' {
    $endTicks = [long]9007199254740993
    $expectedNs = [long]900719925474099300
    $events = New-ValidProcessColdMarkerEvents -EndTicks $endTicks
    $action = New-MarkerTestAction -Sequence 5 -MetricId 'window_actionable_ns' -ValueNs $expectedNs -StartSequence 1 -EndSequence 3 -QpcTicks $endTicks
    $all = @($events[0], $events[1], $events[2], $events[3], $action)
    $resolved = Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events $all -ActionEvent $action
    Assert-Equal -Actual $resolved.value_ns -Expected $expectedNs
    Assert-Equal -Actual $resolved.start_event_seq -Expected 1
    Assert-Equal -Actual $resolved.end_event_seq -Expected 3

    foreach ($offset in @(-1, 1)) {
        $wrong = Copy-MarkerTestObject $action
        $wrong.payload.value_ns = [long]($expectedNs + $offset)
        Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events @($events + $wrong) -ActionEvent $wrong } 'M0M009|QPC|value_ns'
    }

    $candidate = Copy-MarkerTestObject $events[2]
    $candidate.payload.source = 'candidate_reported'
    $candidate.payload.role = 'diagnostic'
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events @($events[0], $events[1], $candidate, $events[3], $action) -ActionEvent $action } 'M0M009|external|boundary'

    $afterAction = Copy-MarkerTestObject $action
    $afterAction.event_seq = 3
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events @($events[0], $events[1], $afterAction, $events[2], $events[3]) -ActionEvent $afterAction } 'M0M009|order|action'

    $fabricated = Copy-MarkerTestObject $action
    $fabricated.qpc_ticks++
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events $all -ActionEvent $fabricated } 'M0M009|exact|raw'

    $wrongPerAttemptFailure = Copy-MarkerTestObject $action
    $wrongPerAttemptFailure.payload.outcome = 'budget_exceeded'
    $wrongPerAttemptFailure.payload.reason_code = 'budget_violated'
    $wrongPerAttemptFailure.payload.error_code = 'cold_p95_is_batch_only'
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'process-cold' -Events @($events + $wrongPerAttemptFailure) -ActionEvent $wrongPerAttemptFailure } 'M0M009|batch_p95|budget_exceeded'
}

Test-Case 'Queue attempt limit is exact at 100 ms and aggregate policy is max' {
    $probeInput = New-MarkerTestEvent -Sequence 1 -MarkerId 'focus-probe-input' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId 'probe-limit' -QpcTicks 0 -QpcFrequency 1000000000
    $ackAtLimit = New-MarkerTestEvent -Sequence 2 -MarkerId 'focus-visible-ack' -Phase instant -Source external_harness -Role measurement_boundary -OccurrenceId 'probe-limit' -QpcTicks 100000000 -QpcFrequency 1000000000
    $atLimit = New-MarkerTestAction -Sequence 3 -MetricId 'queue_focus_stall_ns' -ValueNs 100000000 -StartSequence 1 -EndSequence 2 -QpcTicks 100000000 -QpcFrequency 1000000000
    $resolved = Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'queue-stall' -Events @($probeInput, $ackAtLimit, $atLimit) -ActionEvent $atLimit
    Assert-Equal -Actual $resolved.attempt_aggregate -Expected 'max'
    Assert-Equal -Actual $resolved.decision_role -Expected 'attempt_limit'

    $budgetAtLimit = Copy-MarkerTestObject $atLimit
    $budgetAtLimit.payload.outcome = 'budget_exceeded'
    $budgetAtLimit.payload.reason_code = 'budget_violated'
    $budgetAtLimit.payload.error_code = 'queue_stall'
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'queue-stall' -Events @($probeInput, $ackAtLimit, $budgetAtLimit) -ActionEvent $budgetAtLimit } 'M0M009|attempt_limit|observed'

    $ackOverLimit = Copy-MarkerTestObject $ackAtLimit
    $ackOverLimit.qpc_ticks = 100000001
    $observedOverLimit = Copy-MarkerTestObject $atLimit
    $observedOverLimit.qpc_ticks = 100000001
    $observedOverLimit.payload.value_ns = 100000001
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'queue-stall' -Events @($probeInput, $ackOverLimit, $observedOverLimit) -ActionEvent $observedOverLimit } 'M0M009|attempt_limit|budget_exceeded'

    $budgetOverLimit = Copy-MarkerTestObject $observedOverLimit
    $budgetOverLimit.payload.outcome = 'budget_exceeded'
    $budgetOverLimit.payload.reason_code = 'budget_violated'
    $budgetOverLimit.payload.error_code = 'queue_stall'
    $overResolved = Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'queue-stall' -Events @($probeInput, $ackOverLimit, $budgetOverLimit) -ActionEvent $budgetOverLimit
    Assert-Equal -Actual $overResolved.value_ns -Expected 100000001
}

Test-Case 'A queue attempt-limit violation can only coexist with a failed run status' {
    $overLimit = Add-MarkerTestActions -ScenarioId 'queue-stall' -Events (New-QueueMarkerEvents -PairCount 1 -AckDelayTicks 1000001)
    $failedResult = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $overLimit -Status failed
    Assert-Equal -Actual $failedResult.action_event_count -Expected 1
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'queue-stall' -Events $overLimit -Status valid } 'M0M009|budget_exceeded|valid|failed'
}

Test-Case 'Visual timing remains diagnostic in M0.4 while baseline and UIA confirmation are enforced' {
    $visual = @(
        (New-MarkerTestEvent -Sequence 1 -MarkerId 'visual-baseline-captured' -Phase instant -Source external_harness -Role diagnostic -QpcTicks 0),
        (New-MarkerTestEvent -Sequence 2 -MarkerId 'visual-input' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 5000000),
        (New-MarkerTestEvent -Sequence 3 -MarkerId 'visual-change' -Phase instant -Source external_harness -Role measurement_boundary -QpcTicks 7000000),
        (New-MarkerTestEvent -Sequence 4 -MarkerId 'uia-change' -Phase instant -Source external_harness -Role diagnostic -QpcTicks 7000000),
        (New-MarkerTestEvent -Sequence 5 -MarkerId 'measurement-complete' -Phase end -Source external_harness -Role diagnostic -QpcTicks 7000000)
    )
    $observed = New-MarkerTestAction -Sequence 6 -MetricId 'visual_response_ns' -ValueNs 200000000 -StartSequence 2 -EndSequence 3 -QpcTicks 7000000
    $visualWithAction = @($visual + $observed)
    $null = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'visual-response-diagnostic' -Events $visualWithAction -Status valid
    $resolved = Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'visual-response-diagnostic' -Events $visualWithAction -ActionEvent $observed
    Assert-Equal -Actual $resolved.decision_role -Expected 'diagnostic_only'

    $budget = Copy-MarkerTestObject $observed
    $budget.payload.outcome = 'budget_exceeded'
    $budget.payload.reason_code = 'budget_violated'
    $budget.payload.error_code = 'visual_over_100ms'
    Assert-Throws { Resolve-M0MeasurementBoundary -Catalog $catalog -ScenarioId 'visual-response-diagnostic' -Events @($visual + $budget) -ActionEvent $budget } 'M0M009|diagnostic.only|budget_exceeded'

    $shortBaseline = Copy-MarkerTestObject $visual
    $shortBaseline[1].qpc_ticks = 4999999
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'visual-response-diagnostic' -Events $shortBaseline -Status valid } 'M0M007|500 ms|baseline'

    $lateUia = Copy-MarkerTestObject $visual
    $lateUia[3].qpc_ticks = 25000001
    $lateUia[4].qpc_ticks = $lateUia[3].qpc_ticks
    Assert-Throws { Assert-M0MarkerSequence -Catalog $catalog -ScenarioId 'visual-response-diagnostic' -Events $lateUia -Status valid } 'M0M007|2 s|UIA'
}

Test-Case 'Run marker inventory is unique and preserves first occurrence order' {
    $events = New-QueueMarkerEvents -PairCount 2
    $inventory = @(Get-M0MarkerInventory -Events $events)
    Assert-SequenceEqual -Actual $inventory -Expected @('run-started', 'queue-probe-window', 'focus-probe-input', 'focus-visible-ack', 'queue-accessibility-verified', 'measurement-complete')
}
