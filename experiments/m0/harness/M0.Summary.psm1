Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0SummaryUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0SummaryRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')).TrimEnd('\', '/')
$script:M0SummaryStatisticsPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'M0.Statistics.psm1'))
$script:M0SummaryProcessPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'M0.Process.psm1'))
$script:M0SummaryReadTestHook = $null
$script:M0SummaryArtifactsTestHook = $null
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$statisticsModules = @(Get-Module -All -Name 'M0.Statistics')
if ($statisticsModules.Count -gt 1 -or
    ($statisticsModules.Count -eq 1 -and -not [IO.Path]::GetFullPath($statisticsModules[0].Path).Equals($script:M0SummaryStatisticsPath, $comparison))) {
    throw [IO.InvalidDataException]::new('M0Q001 M0.Summary refuses a foreign or ambiguous M0.Statistics module.')
}
if ($statisticsModules.Count -eq 0) {
    Import-Module -Name $script:M0SummaryStatisticsPath -ErrorAction Stop
    $statisticsModules = @(Get-Module -All -Name 'M0.Statistics')
}
if ($statisticsModules.Count -ne 1 -or
    -not [IO.Path]::GetFullPath($statisticsModules[0].Path).Equals($script:M0SummaryStatisticsPath, $comparison)) {
    throw [IO.InvalidDataException]::new('M0Q001 M0.Summary could not bind the exact anchored M0.Statistics module.')
}
$script:M0SummaryStatisticsModule = $statisticsModules[0]
$script:M0SummaryStatisticsContract = & $script:M0SummaryStatisticsModule { Assert-M0StatisticsRuntimeContract }
if ([string]$script:M0SummaryStatisticsContract.contract_id -cne 'm0-statistics-runtime-v1' -or
    [int]$script:M0SummaryStatisticsContract.seed -ne 20260823 -or [int]$script:M0SummaryStatisticsContract.resamples -ne 10000) {
    throw [IO.InvalidDataException]::new('M0Q001 M0.Summary rejected the exact M0.Statistics runtime contract.')
}

function Assert-M0SummaryStatisticsContract {
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $loaded = @(Get-Module -All -Name 'M0.Statistics')
    if ($loaded.Count -ne 1 -or -not [object]::ReferenceEquals($loaded[0], $script:M0SummaryStatisticsModule) -or
        -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($script:M0SummaryStatisticsPath, $comparison)) {
        throw [IO.InvalidDataException]::new('M0Q001 M0.Summary statistics module instance changed after binding.')
    }
    $current = & $script:M0SummaryStatisticsModule { Assert-M0StatisticsRuntimeContract }
    if ([string]$current.contract_id -cne 'm0-statistics-runtime-v1' -or
        [string]$current.module_instance_id -cne [string]$script:M0SummaryStatisticsContract.module_instance_id -or
        [string]$current.module_sha256 -cne [string]$script:M0SummaryStatisticsContract.module_sha256 -or
        [int]$current.seed -ne 20260823 -or [int]$current.resamples -ne 10000) {
        throw [IO.InvalidDataException]::new('M0Q001 M0.Summary statistics runtime contract changed after binding.')
    }
}

function Get-M0SummaryPhysicalTrustModule {
    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0Q002 physical summary artifact identity is not proven outside Windows; pure summary derivation remains available.')
    }
    $pathComparison = [StringComparison]::OrdinalIgnoreCase
    $processModules = @(Get-Module -All -Name 'M0.Process')
    if ($processModules.Count -gt 1 -or
        ($processModules.Count -eq 1 -and -not [IO.Path]::GetFullPath($processModules[0].Path).Equals($script:M0SummaryProcessPath, $pathComparison))) {
        throw [IO.InvalidDataException]::new('M0Q002 physical summary trust refuses a foreign or ambiguous M0.Process module.')
    }
    if ($processModules.Count -eq 0) {
        foreach ($nativeTypeName in @(
            'M0ProcessNativeV1.PilotSession',
            'M0ProcessNativeV1.ProcessCapability',
            'M0ProcessNativeV1.TrustedFile'
        )) {
            if ($nativeTypeName -as [type]) {
                throw [IO.InvalidDataException]::new("M0Q002 physical summary trust refuses preloaded native type '$nativeTypeName' without the exact M0.Process module.")
            }
        }
        Import-Module -Name $script:M0SummaryProcessPath -ErrorAction Stop
        $processModules = @(Get-Module -All -Name 'M0.Process')
    }
    if ($processModules.Count -ne 1 -or
        -not [IO.Path]::GetFullPath($processModules[0].Path).Equals($script:M0SummaryProcessPath, $pathComparison) -or
        -not ('M0ProcessNativeV1.TrustedFile' -as [type])) {
        throw [IO.InvalidDataException]::new('M0Q002 physical summary trust could not bind the exact M0.Process module and native file identity.')
    }
    $nativeContract = & $processModules[0] { Assert-M0ProcessNativeContract }
    if ([string]$nativeContract.contract_id -cne 'm0-process-native-v1' -or [int]$nativeContract.type_count -ne 3 -or
        [string]$nativeContract.module_instance_id -cne (& $processModules[0] { $script:M0ProcessModuleInstanceId }) -or
        [string]$nativeContract.module_sha256 -notmatch '^[a-f0-9]{64}$') {
        throw [IO.InvalidDataException]::new('M0Q002 physical summary trust rejected the native M0.Process contract identity.')
    }
    return $processModules[0]
}

function Get-M0SummarySha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-M0SummaryCanonicalDocumentHash {
    param([Parameter(Mandatory)] $Value)

    $json = ConvertTo-Json -InputObject $Value -Compress -Depth 100
    if ($json.Contains("`r") -or $json.Contains("`n")) {
        throw [IO.InvalidDataException]::new('M0Q010 canonical manifest JSON must serialize to one line.')
    }
    return Get-M0SummarySha256Bytes -Bytes $script:M0SummaryUtf8.GetBytes($json + "`n")
}

function Get-M0SummaryRunsManifestHash {
    param(
        [Parameter(Mandatory)] [string] $CandidateCode,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string, object]] $RawByKey
    )

    $runs = [Collections.Generic.List[object]]::new()
    for ($attempt = 1; $attempt -le 22; $attempt++) {
        $raw = $RawByKey["$CandidateCode|$attempt"]
        $runs.Add([pscustomobject][ordered]@{
            attempt = $attempt
            run_id = [string]$raw.run_id
            run_record_sha256 = ([string]$raw.run_record_sha256).ToLowerInvariant()
            raw_final_line_sha256 = ([string]$raw.raw_final_line_sha256).ToLowerInvariant()
            status = [string]$raw.status
        })
    }
    $manifest = [pscustomobject][ordered]@{
        schema_id = 'm0.runs-manifest.v1'
        candidate_code = $CandidateCode
        runs = @($runs)
    }
    return Get-M0SummaryCanonicalDocumentHash -Value $manifest
}

function Get-M0SummaryEvidenceInventoryHash {
    param(
        [Parameter(Mandatory)] [object[]] $Raws,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Traces
    )

    $canonicalRaws = @($Raws | ForEach-Object {
        [pscustomobject][ordered]@{
            snapshot_ordinal = [int]$_.snapshot_ordinal
            run_id = [string]$_.run_id
            candidate_code = [string]$_.candidate_code
            attempt = [int]$_.attempt
            status = [string]$_.status
            raw_sha256 = ([string]$_.raw_sha256).ToLowerInvariant()
            raw_final_line_sha256 = ([string]$_.raw_final_line_sha256).ToLowerInvariant()
            run_record_sha256 = ([string]$_.run_record_sha256).ToLowerInvariant()
            snapshot_sha256 = ([string]$_.snapshot_sha256).ToLowerInvariant()
        }
    })
    $canonicalTraces = @($Traces | ForEach-Object {
        [pscustomobject][ordered]@{
            candidate_code = [string]$_.candidate_code
            run_id = [string]$_.run_id
            trace_id = [string]$_.trace_id
            sha256 = ([string]$_.sha256).ToLowerInvariant()
        }
    })
    return Get-M0SummaryCanonicalDocumentHash -Value ([pscustomobject][ordered]@{
        schema_id = 'm0.summary-evidence-inventory.v1'
        raws = $canonicalRaws
        traces = $canonicalTraces
    })
}

function Read-M0SummaryFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name,
        [switch] $KeepOpen
    )

    $null = Get-M0SummaryPhysicalTrustModule
    $path = [IO.Path]::GetFullPath($LiteralPath)
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $path.StartsWith($script:M0SummaryRepositoryRoot + [IO.Path]::DirectorySeparatorChar, $pathComparison)) {
        throw [UnauthorizedAccessException]::new("M0Q002 trusted $Name escaped the module repository.")
    }
    $relative = [IO.Path]::GetRelativePath($script:M0SummaryRepositoryRoot, $path)
    $segments = $relative.Split([IO.Path]::DirectorySeparatorChar)
    $ancestor = $script:M0SummaryRepositoryRoot
    for ($index = 0; $index -lt $segments.Count - 1; $index++) {
        $ancestor = Join-Path $ancestor $segments[$index]
        if (-not (Test-Path -LiteralPath $ancestor -PathType Container)) {
            throw [IO.DirectoryNotFoundException]::new("M0Q002 trusted $Name ancestor is missing: '$ancestor'.")
        }
        $ancestorItem = Get-Item -LiteralPath $ancestor -Force
        if (($ancestorItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($ancestorItem.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$ancestorItem.LinkType))) {
            throw [IO.InvalidDataException]::new("M0Q002 links are forbidden in trusted $Name ancestors: '$ancestor'.")
        }
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0Q002 required $Name is missing: '$path'.")
    }
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if (-not $IsWindows -or -not ('M0ProcessNativeV1.TrustedFile' -as [type])) {
            throw [PlatformNotSupportedException]::new("M0Q002 physical trusted-file identity is unavailable for $Name.")
        }
        foreach ($stage in @('after-open', 'after-read')) {
            [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $path)
            $item = Get-Item -LiteralPath $path -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
                throw [IO.InvalidDataException]::new("M0Q002 links are forbidden for trusted $Name '$path'.")
            }
            if ($stage -ceq 'after-open' -and $null -ne $script:M0SummaryReadTestHook) {
                $hook = $script:M0SummaryReadTestHook
                $script:M0SummaryReadTestHook = $null
                if ([string]$hook.stage -ceq 'after-open-hardlink') {
                    $null = New-Item -ItemType HardLink -Path ([string]$hook.link_path) -Target $path
                }
                else {
                    throw [IO.InvalidDataException]::new('M0Q002 unknown summary read identity test hook.')
                }
                [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $path)
            }
            if ($stage -ceq 'after-open') {
                if ($stream.Length -gt 33554432) {
                    throw [IO.InvalidDataException]::new("M0Q002 trusted $Name exceeds 32 MiB.")
                }
                $bytes = [byte[]]::new($stream.Length)
                if ($bytes.Length -gt 0) { $stream.ReadExactly($bytes, 0, $bytes.Length) }
                if ($stream.Position -ne $stream.Length) {
                    throw [IO.InvalidDataException]::new("M0Q002 trusted $Name changed while it was read.")
                }
            }
        }
        if ($bytes.Length -eq 0 -or
            ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
            throw [IO.InvalidDataException]::new("M0Q002 $Name must be nonempty UTF-8 without BOM.")
        }
        try { $text = $script:M0SummaryUtf8.GetString($bytes) }
        catch { throw [IO.InvalidDataException]::new("M0Q002 $Name is not strict UTF-8.", $_.Exception) }
        if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
            $text.EndsWith("`n`n", [StringComparison]::Ordinal) -or $text -match '(?m)[\t ]+$') {
            throw [IO.InvalidDataException]::new("M0Q002 $Name must use LF, exactly one final newline and no trailing whitespace.")
        }
        $result = [pscustomobject][ordered]@{
            path = $path
            bytes = $bytes
            text = $text
            sha256 = Get-M0SummarySha256Bytes -Bytes $bytes
            stream = if ($KeepOpen) { $stream } else { $null }
        }
        if ($KeepOpen) { $stream = $null }
        return $result
    }
    finally {
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

function ConvertTo-M0SummaryCanonicalText {
    param([Parameter(Mandatory)] $Value)
    return (($Value | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n")
}

function Assert-M0SummaryHeldFileStable {
    param(
        [Parameter(Mandatory)] $File,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($null -eq $File.stream) {
        throw [IO.InvalidDataException]::new("M0Q002 trusted $Name no longer has its retained handle.")
    }
    [M0ProcessNativeV1.TrustedFile]::Validate($File.stream.SafeFileHandle, [string]$File.path)
    if ($File.stream.Length -ne $File.bytes.Length) {
        throw [IO.InvalidDataException]::new("M0Q002 trusted $Name length changed while retained.")
    }
    $File.stream.Position = 0
    $currentBytes = [byte[]]::new($File.bytes.Length)
    if ($currentBytes.Length -gt 0) { $File.stream.ReadExactly($currentBytes, 0, $currentBytes.Length) }
    if (-not [Linq.Enumerable]::SequenceEqual[byte]($currentBytes, [byte[]]$File.bytes) -or
        (Get-M0SummarySha256Bytes -Bytes $currentBytes) -cne [string]$File.sha256) {
        throw [IO.InvalidDataException]::new("M0Q002 trusted $Name bytes changed while retained.")
    }
    [M0ProcessNativeV1.TrustedFile]::Validate($File.stream.SafeFileHandle, [string]$File.path)
}

function Get-M0SummaryOrdinalKeys {
    param([Parameter(Mandatory)] [Collections.Generic.IEnumerable[string]] $Keys)

    [string[]]$ordered = @($Keys)
    [Array]::Sort($ordered, [StringComparer]::Ordinal)
    return ,$ordered
}

function ConvertTo-M0SummaryCommonInputs {
    param([Parameter(Mandatory)] $CommonInputs)

    return [pscustomobject][ordered]@{
        protocol_sha256 = ([string]$CommonInputs.protocol_sha256).ToLowerInvariant()
        environment_sha256 = ([string]$CommonInputs.environment_sha256).ToLowerInvariant()
        fixture_sha256 = ([string]$CommonInputs.fixture_sha256).ToLowerInvariant()
        harness_sha256 = ([string]$CommonInputs.harness_sha256).ToLowerInvariant()
        scenario_sha256 = ([string]$CommonInputs.scenario_sha256).ToLowerInvariant()
    }
}

function Get-M0SummaryAttemptValues {
    param(
        [Parameter(Mandatory)] $Candidate,
        [Parameter(Mandatory)] $Metric
    )

    $byAttempt = [Collections.Generic.Dictionary[int, long]]::new()
    foreach ($entry in @($Metric.values)) {
        $attempt = [int]$entry.attempt
        if (-not $byAttempt.TryAdd($attempt, [long]$entry.value_integer)) {
            throw [IO.InvalidDataException]::new("M0Q010 candidate '$($Candidate.candidate_code)' metric '$($Metric.metric_id)' repeats attempt $attempt.")
        }
    }
    $expectedCount = 0
    $values = [Collections.Generic.List[long]]::new()
    for ($attempt = 1; $attempt -le 22; $attempt++) {
        $isValid = [string]$Candidate.attempt_statuses[$attempt - 1] -ceq 'valid'
        $hasValue = $byAttempt.ContainsKey($attempt)
        if ($isValid -ne $hasValue) {
            throw [IO.InvalidDataException]::new("M0Q010 candidate '$($Candidate.candidate_code)' metric '$($Metric.metric_id)' values do not match valid attempt $attempt.")
        }
        if ($isValid) {
            $expectedCount++
            $values.Add($byAttempt[$attempt])
        }
    }
    if ($byAttempt.Count -ne $expectedCount) {
        throw [IO.InvalidDataException]::new("M0Q010 candidate '$($Candidate.candidate_code)' metric '$($Metric.metric_id)' differs from its valid-attempt count.")
    }
    return ,([long[]]$values.ToArray())
}

function Get-M0SummaryMetricScale {
    param([Parameter(Mandatory)] [string] $MetricFamily)

    switch -CaseSensitive ($MetricFamily) {
        'latency_ms' {
            [pscustomobject][ordered]@{ input_unit = 'nanoseconds'; output_unit = 'milliseconds'; output_divisor = [decimal]1000000; comparison_divisor = [decimal]1000000; comparison_to_output_divisor = [decimal]1 }
        }
        'visual_response_ms' {
            [pscustomobject][ordered]@{ input_unit = 'nanoseconds'; output_unit = 'milliseconds'; output_divisor = [decimal]1000000; comparison_divisor = [decimal]1000000; comparison_to_output_divisor = [decimal]1 }
        }
        'memory_bytes' {
            [pscustomobject][ordered]@{ input_unit = 'bytes'; output_unit = 'mebibytes'; output_divisor = [decimal]1048576; comparison_divisor = [decimal]1; comparison_to_output_divisor = [decimal]1048576 }
        }
        'cpu_percentage_points' {
            [pscustomobject][ordered]@{ input_unit = 'basis_points'; output_unit = 'percentage_points'; output_divisor = [decimal]100; comparison_divisor = [decimal]100; comparison_to_output_divisor = [decimal]1 }
        }
        default { throw [IO.InvalidDataException]::new("M0Q010 unknown metric family '$MetricFamily'.") }
    }
}

function Test-M0SummaryBudgetViolation {
    param(
        [Parameter(Mandatory)] [decimal] $P95,
        [Parameter(Mandatory)] [string] $Operator,
        [Parameter(Mandatory)] [decimal] $Threshold
    )

    switch -CaseSensitive ($Operator) {
        'less-than' { $P95 -ge $Threshold }
        'less-than-or-equal' { $P95 -gt $Threshold }
        default { throw [IO.InvalidDataException]::new("M0Q010 unknown budget operator '$Operator'.") }
    }
}

function Get-M0SummaryExactRange {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [long[]] $Values)

    if ($Values.Count -eq 0) {
        return [pscustomobject][ordered]@{ minimum = $null; maximum = $null }
    }
    [long]$minimum = $Values[0]
    [long]$maximum = $Values[0]
    for ($index = 1; $index -lt $Values.Count; $index++) {
        if ($Values[$index] -lt $minimum) { $minimum = $Values[$index] }
        if ($Values[$index] -gt $maximum) { $maximum = $Values[$index] }
    }
    return [pscustomobject][ordered]@{ minimum = $minimum; maximum = $maximum }
}

function Get-M0SummaryDecimalSquareRoot {
    param([Parameter(Mandatory)] [decimal] $Value)

    if ($Value -lt [decimal]0) {
        throw [ArgumentOutOfRangeException]::new('Value', 'M0Q010 standard-deviation variance cannot be negative.')
    }
    if ($Value -eq [decimal]0) { return [decimal]0 }
    [decimal]$estimate = if ($Value -ge [decimal]1) { $Value / [decimal]2 } else { [decimal]1 }
    for ($iteration = 0; $iteration -lt 96; $iteration++) {
        $estimate = ($estimate + ($Value / $estimate)) / [decimal]2
    }
    return [Math]::Round($estimate, 12, [MidpointRounding]::ToEven)
}

function Get-M0SummaryDiagnostics {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [long[]] $Values,
        [Parameter(Mandatory)] [decimal] $OutputDivisor
    )

    if ($Values.Count -eq 0) {
        return [pscustomobject][ordered]@{ mean = $null; standard_deviation = $null; error_code = 'no_valid_attempts' }
    }
    [decimal]$sum = [decimal]0
    foreach ($value in $Values) { $sum += [decimal]$value }
    [decimal]$mean = ($sum / [decimal]$Values.Count) / $OutputDivisor
    if ($Values.Count -eq 1) {
        return [pscustomobject][ordered]@{
            mean = $mean
            standard_deviation = $null
            error_code = 'insufficient_samples_for_standard_deviation'
        }
    }
    [decimal]$sumSquared = [decimal]0
    foreach ($value in $Values) {
        [decimal]$converted = [decimal]$value / $OutputDivisor
        [decimal]$difference = $converted - $mean
        $sumSquared += $difference * $difference
    }
    [decimal]$variance = $sumSquared / [decimal]($Values.Count - 1)
    return [pscustomobject][ordered]@{
        mean = $mean
        standard_deviation = Get-M0SummaryDecimalSquareRoot -Value $variance
        error_code = $null
    }
}

function Assert-M0SummaryInputSemantics {
    param([Parameter(Mandatory)] $SummaryInput)

    if ($SummaryInput.candidates.Count -ne 2) {
        throw [IO.InvalidDataException]::new('M0Q010 common summary input requires candidates A and B exactly once.')
    }
    $candidateByCode = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($candidate in @($SummaryInput.candidates)) {
        if (-not $candidateByCode.TryAdd([string]$candidate.candidate_code, $candidate)) {
            throw [IO.InvalidDataException]::new('M0Q010 common summary input repeats a candidate code.')
        }
        $seenMetrics = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($metric in @($candidate.metrics)) {
            if (-not $seenMetrics.Add([string]$metric.metric_id)) {
                throw [IO.InvalidDataException]::new("M0Q010 candidate '$($candidate.candidate_code)' metric inventory contains a duplicate.")
            }
            $null = Get-M0SummaryAttemptValues -Candidate $candidate -Metric $metric
        }
        if ((-not [bool]$candidate.soak.applicable -and
                ([string]$candidate.soak.result -cne 'not_applicable' -or
                    $null -ne $candidate.soak.duration_seconds -or $null -ne $candidate.soak.error_code)) -or
            ([bool]$candidate.soak.applicable -and
                ([string]$candidate.soak.result -ceq 'not_applicable' -or
                    $null -eq $candidate.soak.duration_seconds -or [int]$candidate.soak.duration_seconds -le 0 -or
                    (([string]$candidate.soak.result -ceq 'pass') -ne ($null -eq $candidate.soak.error_code))))) {
            throw [IO.InvalidDataException]::new("M0Q010 candidate '$($candidate.candidate_code)' soak applicability/result/error fields contradict each other.")
        }
    }
    if (-not $candidateByCode.ContainsKey('A') -or -not $candidateByCode.ContainsKey('B')) {
        throw [IO.InvalidDataException]::new('M0Q010 common summary input requires candidates A and B exactly once.')
    }
    $candidateA = $candidateByCode['A']
    $candidateB = $candidateByCode['B']
    $commonA = ConvertTo-M0SummaryCommonInputs -CommonInputs $candidateA.common_inputs
    $commonB = ConvertTo-M0SummaryCommonInputs -CommonInputs $candidateB.common_inputs
    if (($commonA | ConvertTo-Json -Compress) -cne ($commonB | ConvertTo-Json -Compress)) {
        throw [IO.InvalidDataException]::new('M0Q010 candidate A/B trusted common inputs differ after canonical hash normalization.')
    }
    $metricsA = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $metricsB = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($metric in @($candidateA.metrics)) { $metricsA.Add([string]$metric.metric_id, $metric) }
    foreach ($metric in @($candidateB.metrics)) { $metricsB.Add([string]$metric.metric_id, $metric) }
    if ($metricsA.Count -ne $metricsB.Count) {
        throw [IO.InvalidDataException]::new('M0Q010 A/B metric identity sets differ.')
    }
    foreach ($metricId in $metricsA.Keys) {
        if (-not $metricsB.ContainsKey($metricId)) {
            throw [IO.InvalidDataException]::new('M0Q010 A/B metric identity sets differ.')
        }
        $left = $metricsA[$metricId]
        $right = $metricsB[$metricId]
        if ([string]$left.metric_family -cne [string]$right.metric_family -or
            [string]$left.unit -cne [string]$right.unit -or
            [string]$left.budget.operator -cne [string]$right.budget.operator -or
            [long]$left.budget.threshold_integer -ne [long]$right.budget.threshold_integer) {
            throw [IO.InvalidDataException]::new("M0Q010 A/B metric '$($left.metric_id)' unit or budget differs.")
        }
        $scale = Get-M0SummaryMetricScale -MetricFamily ([string]$left.metric_family)
        if ([string]$left.unit -cne [string]$scale.input_unit) {
            throw [IO.InvalidDataException]::new("M0Q010 metric '$($left.metric_id)' family/unit pair is incompatible.")
        }
    }

    $seenRaw = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $rawByKey = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $seenOrdinal = [Collections.Generic.HashSet[int]]::new()
    $seenRun = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenRawHash = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenFinalLineHash = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenRunRecordHash = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenSnapshotHash = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $runCandidate = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($raw in @($SummaryInput.raws)) {
        $key = "$([string]$raw.candidate_code)|$([int]$raw.attempt)"
        $runId = [string]$raw.run_id
        if (-not $seenRaw.Add($key) -or -not $seenOrdinal.Add([int]$raw.snapshot_ordinal) -or -not $seenRun.Add($runId) -or
            -not $seenRawHash.Add(([string]$raw.raw_sha256).ToLowerInvariant()) -or
            -not $seenFinalLineHash.Add(([string]$raw.raw_final_line_sha256).ToLowerInvariant()) -or
            -not $seenRunRecordHash.Add(([string]$raw.run_record_sha256).ToLowerInvariant()) -or
            -not $seenSnapshotHash.Add(([string]$raw.snapshot_sha256).ToLowerInvariant()) -or
            -not $runId.StartsWith("run-$([string]$raw.candidate_code)-", [StringComparison]::Ordinal)) {
            throw [IO.InvalidDataException]::new('M0Q010 common summary raw inventory contains a duplicate hash or mismatched candidate/attempt/ordinal/run identity.')
        }
        $rawByKey.Add($key, $raw)
        $runCandidate.Add($runId, [string]$raw.candidate_code)
    }
    foreach ($candidateCode in @('A', 'B')) {
        for ($attempt = 1; $attempt -le 22; $attempt++) {
            if (-not $seenRaw.Contains("$candidateCode|$attempt")) {
                throw [IO.InvalidDataException]::new("M0Q010 common summary raw inventory misses '$candidateCode/$attempt'.")
            }
            $raw = $rawByKey["$candidateCode|$attempt"]
            $candidate = $candidateByCode[$candidateCode]
            if ([string]$raw.status -cne [string]$candidate.attempt_statuses[$attempt - 1]) {
                throw [IO.InvalidDataException]::new("M0Q010 candidate '$candidateCode' receipt status contradicts attempt $attempt.")
            }
        }
        $expectedManifest = Get-M0SummaryRunsManifestHash -CandidateCode $candidateCode -RawByKey $rawByKey
        $candidate = $candidateByCode[$candidateCode]
        if (([string]$candidate.runs_manifest_sha256).ToLowerInvariant() -cne $expectedManifest) {
            throw [IO.InvalidDataException]::new("M0Q010 candidate '$candidateCode' runs manifest differs from its 22 canonical raw/run/snapshot receipts.")
        }
    }
    $seenTrace = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($trace in @($SummaryInput.traces)) {
        $traceRunId = [string]$trace.run_id
        if (-not $seenTrace.Add("$([string]$trace.candidate_code)|$traceRunId|$([string]$trace.trace_id)") -or
            -not $runCandidate.ContainsKey($traceRunId) -or $runCandidate[$traceRunId] -cne [string]$trace.candidate_code) {
            throw [IO.InvalidDataException]::new('M0Q010 common summary trace inventory contains a duplicate or foreign run binding.')
        }
    }
}

function Test-M0SummaryDecimalExact {
    param(
        [Parameter(Mandatory)] [decimal] $Actual,
        [Parameter(Mandatory)] [decimal] $Expected
    )

    return $Actual -eq $Expected
}

function Assert-M0SummaryDocumentSemantics {
    param([Parameter(Mandatory)] $Summary)

    if ($Summary.candidates.Count -ne 2 -or [string]$Summary.candidates[0].candidate_code -cne 'A' -or
        [string]$Summary.candidates[1].candidate_code -cne 'B') {
        throw [IO.InvalidDataException]::new('M0Q013 common summary requires output candidates A then B exactly once.')
    }
    $expectedGlobal = 'pass'
    for ($candidateIndex = 0; $candidateIndex -lt 2; $candidateIndex++) {
        $candidate = $Summary.candidates[$candidateIndex]
        if ([int]$candidate.valid + [int]$candidate.discarded + [int]$candidate.failed + [int]$candidate.inconclusive -ne 22) {
            throw [IO.InvalidDataException]::new("M0Q013 candidate '$($candidate.candidate_code)' counts do not sum to 22.")
        }
        $expectedDecision = if ([bool]$candidate.budget_violated -or [int]$candidate.failed -gt 0 -or [string]$candidate.soak.result -ceq 'fail') {
            'fail'
        }
        elseif ([int]$candidate.inconclusive -gt 0 -or [int]$candidate.valid -lt 20 -or [int]$candidate.discarded -gt 2 -or
            [string]$candidate.soak.result -ceq 'inconclusive') {
            'inconclusive'
        }
        else { 'pass' }
        if ([string]$candidate.decision -cne $expectedDecision) {
            throw [IO.InvalidDataException]::new("M0Q013 candidate '$($candidate.candidate_code)' decision contradicts counts, budgets or soak.")
        }
        if ($expectedDecision -ceq 'fail') { $expectedGlobal = 'fail' }
        elseif ($expectedDecision -ceq 'inconclusive' -and $expectedGlobal -cne 'fail') { $expectedGlobal = 'inconclusive' }
    }
    if ([string]$Summary.decision -cne $expectedGlobal) {
        throw [IO.InvalidDataException]::new('M0Q013 global decision contradicts candidate decisions.')
    }

    $seenMetrics = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $previousMetricId = $null
    $aggregateBudget = @($false, $false)
    foreach ($metric in @($Summary.metrics)) {
        $metricId = [string]$metric.metric_id
        if (-not $seenMetrics.Add($metricId) -or
            ($null -ne $previousMetricId -and [StringComparer]::Ordinal.Compare($previousMetricId, $metricId) -ge 0)) {
            throw [IO.InvalidDataException]::new('M0Q013 common summary metrics are duplicate or not in ordinal identity order.')
        }
        $previousMetricId = $metricId
        $scale = Get-M0SummaryMetricScale -MetricFamily ([string]$metric.metric_family)
        if ([string]$metric.unit -cne [string]$scale.output_unit) {
            throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' family/output-unit pair is incompatible.")
        }
        foreach ($side in 0..1) {
            $candidateMetric = if ($side -eq 0) { $metric.candidate_a } else { $metric.candidate_b }
            $candidate = $Summary.candidates[$side]
            if ([int]$candidateMetric.n -ne [int]$candidate.valid -or
                [int]$candidateMetric.failures -ne [int]$candidate.failed -or
                [int]$candidateMetric.discards -ne [int]$candidate.discarded) {
                throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' candidate counts contradict the candidate summary.")
            }
            if ([int]$candidateMetric.n -gt 0) {
                [decimal]$minimum = $candidateMetric.min
                [decimal]$p50 = $candidateMetric.p50
                [decimal]$p95 = $candidateMetric.p95
                [decimal]$maximum = $candidateMetric.max
                if ($minimum -gt $p50 -or $p50 -gt $p95 -or $p95 -gt $maximum) {
                    throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' candidate statistics are not monotonic.")
                }
                $violated = Test-M0SummaryBudgetViolation -P95 $p95 -Operator ([string]$candidateMetric.budget_operator) -Threshold ([decimal]$candidateMetric.budget)
                if ([bool]$candidateMetric.budget_violated -ne [bool]$violated) {
                    throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' budget outcome contradicts its operator and P95.")
                }
            }
            $expectedDiagnosticError = if ([int]$candidateMetric.n -eq 0) {
                'no_valid_attempts'
            }
            elseif ([int]$candidateMetric.n -eq 1) {
                'insufficient_samples_for_standard_deviation'
            }
            else { $null }
            if (($null -eq $expectedDiagnosticError -and $null -ne $candidateMetric.diagnostic_error_code) -or
                ($null -ne $expectedDiagnosticError -and [string]$candidateMetric.diagnostic_error_code -cne $expectedDiagnosticError)) {
                throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' diagnostic availability contradicts its sample count.")
            }
            if ([bool]$candidateMetric.budget_violated) { $aggregateBudget[$side] = $true }
        }
        if ([string]$metric.candidate_a.budget_operator -cne [string]$metric.candidate_b.budget_operator -or
            [decimal]$metric.candidate_a.budget -ne [decimal]$metric.candidate_b.budget) {
            throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' A/B budget operator or threshold differs.")
        }
        $expectedMetricError = if ([int]$metric.candidate_a.n -eq 0 -or [int]$metric.candidate_b.n -eq 0) {
            'insufficient_valid_attempts'
        }
        elseif ([int]$metric.candidate_a.n -lt 20 -or [int]$metric.candidate_b.n -lt 20) {
            'insufficient_decisive_attempts'
        }
        else { $null }
        if (($null -eq $expectedMetricError -and $null -ne $metric.error_code) -or
            ($null -ne $expectedMetricError -and [string]$metric.error_code -cne $expectedMetricError)) {
            throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' error code contradicts candidate sample counts.")
        }
        if ($null -ne $metric.difference_p95_a_minus_b) {
            [decimal]$expectedDifference = [decimal]$metric.candidate_a.p95 - [decimal]$metric.candidate_b.p95
            if (-not (Test-M0SummaryDecimalExact -Actual ([decimal]$metric.difference_p95_a_minus_b) -Expected $expectedDifference)) {
                throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' P95 difference contradicts candidate P95 values.")
            }
            [decimal]$comparisonMultiplier = [decimal]$scale.comparison_to_output_divisor
            $comparison = & $script:M0SummaryStatisticsModule {
                param($familyName, $a, $b, $lower, $upper)
                Resolve-M0P95Comparison -MetricFamily $familyName -P95A $a -P95B $b -CiLower $lower -CiUpper $upper
            } ([string]$metric.metric_family) `
                ([decimal]$metric.candidate_a.p95 * $comparisonMultiplier) `
                ([decimal]$metric.candidate_b.p95 * $comparisonMultiplier) `
                ([decimal]$metric.ci_lower * $comparisonMultiplier) `
                ([decimal]$metric.ci_upper * $comparisonMultiplier)
            [decimal]$expectedBand = [decimal]$comparison.band / $comparisonMultiplier
            if (-not (Test-M0SummaryDecimalExact -Actual ([decimal]$metric.tie_band) -Expected $expectedBand) -or
                [bool]$metric.within_band -ne [bool]$comparison.within_band) {
                throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' tie band contradicts candidate P95 values.")
            }
            $expectedClassification = if ([string]$metric.error_code -ceq 'insufficient_decisive_attempts') { 'inconclusive' } else { [string]$comparison.classification }
            if ([string]$metric.classification -cne $expectedClassification) {
                throw [IO.InvalidDataException]::new("M0Q013 metric '$metricId' classification contradicts its CI/tie policy.")
            }
        }
    }
    for ($side = 0; $side -lt 2; $side++) {
        if ([bool]$Summary.candidates[$side].budget_violated -ne [bool]$aggregateBudget[$side]) {
            throw [IO.InvalidDataException]::new("M0Q013 candidate '$($Summary.candidates[$side].candidate_code)' aggregate budget flag contradicts metric outcomes.")
        }
    }

    if ($Summary.evidence_inventory.raws.Count -ne 44) {
        throw [IO.InvalidDataException]::new('M0Q013 common summary raw inventory must contain 44 entries.')
    }
    $seenRawOrdinals = [Collections.Generic.HashSet[int]]::new()
    $seenRawRunIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenRawHashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenFinalLineHashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenRunRecordHashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenSnapshotHashes = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $rawRunCandidates = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    $rawByKey = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $statusCounts = @{
        A = @{ valid = 0; discarded = 0; failed = 0; inconclusive = 0 }
        B = @{ valid = 0; discarded = 0; failed = 0; inconclusive = 0 }
    }
    for ($index = 0; $index -lt 44; $index++) {
        $expectedCode = if ($index -lt 22) { 'A' } else { 'B' }
        $expectedAttempt = ($index % 22) + 1
        $raw = $Summary.evidence_inventory.raws[$index]
        $runId = [string]$raw.run_id
        if ([string]$raw.candidate_code -cne $expectedCode -or [int]$raw.attempt -ne $expectedAttempt -or
            -not $runId.StartsWith("run-$expectedCode-", [StringComparison]::Ordinal) -or
            -not $seenRawOrdinals.Add([int]$raw.snapshot_ordinal) -or -not $seenRawRunIds.Add($runId) -or
            -not $seenRawHashes.Add(([string]$raw.raw_sha256).ToLowerInvariant()) -or
            -not $seenFinalLineHashes.Add(([string]$raw.raw_final_line_sha256).ToLowerInvariant()) -or
            -not $seenRunRecordHashes.Add(([string]$raw.run_record_sha256).ToLowerInvariant()) -or
            -not $seenSnapshotHashes.Add(([string]$raw.snapshot_sha256).ToLowerInvariant())) {
            throw [IO.InvalidDataException]::new('M0Q013 common summary raw inventory is duplicate, incomplete or not in canonical order.')
        }
        $rawRunCandidates.Add($runId, $expectedCode)
        $rawByKey.Add("$expectedCode|$expectedAttempt", $raw)
        $statusCounts[$expectedCode][[string]$raw.status]++
    }
    foreach ($side in 0..1) {
        $candidateCode = if ($side -eq 0) { 'A' } else { 'B' }
        $candidate = $Summary.candidates[$side]
        foreach ($statusName in @('valid', 'discarded', 'failed', 'inconclusive')) {
            if ([int]$candidate.$statusName -ne [int]$statusCounts[$candidateCode][$statusName]) {
                throw [IO.InvalidDataException]::new("M0Q013 candidate '$candidateCode' counts contradict receipt statuses.")
            }
        }
        $expectedRunsManifest = Get-M0SummaryRunsManifestHash -CandidateCode $candidateCode -RawByKey $rawByKey
        if ([string]$candidate.runs_manifest_sha256 -cne $expectedRunsManifest) {
            throw [IO.InvalidDataException]::new("M0Q013 candidate '$candidateCode' runs manifest differs from canonical Evidence receipts.")
        }
    }
    $seenTraces = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $previousTraceKey = $null
    foreach ($trace in @($Summary.evidence_inventory.traces)) {
        $runId = [string]$trace.run_id
        $key = "$([string]$trace.candidate_code)|$runId|$([string]$trace.trace_id)"
        if (-not $seenTraces.Add($key) -or
            ($null -ne $previousTraceKey -and [StringComparer]::Ordinal.Compare($previousTraceKey, $key) -ge 0) -or
            -not $rawRunCandidates.ContainsKey($runId) -or $rawRunCandidates[$runId] -cne [string]$trace.candidate_code) {
            throw [IO.InvalidDataException]::new('M0Q013 common summary traces are duplicate or not in canonical order.')
        }
        $previousTraceKey = $key
    }
    $expectedInventoryHash = Get-M0SummaryEvidenceInventoryHash -Raws @($Summary.evidence_inventory.raws) -Traces @($Summary.evidence_inventory.traces)
    if ([string]$Summary.evidence_inventory.manifest_sha256 -cne $expectedInventoryHash) {
        throw [IO.InvalidDataException]::new('M0Q013 common summary evidence inventory manifest differs from its canonical receipts.')
    }
}

function Assert-M0Summary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Summary,
        [Parameter(Mandatory)] [string] $ProtocolRoot
    )

    Assert-M0SummaryStatisticsContract
    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $script:M0SummaryRepositoryRoot 'experiments\m0\protocol')).TrimEnd('\', '/')
    $rootComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $root.Equals($expectedRoot, $rootComparison)) {
        throw [UnauthorizedAccessException]::new('M0Q003 summary ProtocolRoot differs from the trusted module repository.')
    }
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $script:M0SummaryRepositoryRoot 'experiments\m0\protocol')).TrimEnd('\', '/')
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $root.Equals($expectedRoot, $pathComparison)) {
        throw [UnauthorizedAccessException]::new('M0Q013 common summary ProtocolRoot differs from the trusted module repository.')
    }
    $json = $Summary | ConvertTo-Json -Compress -Depth 100
    if (-not (Test-Json -Json $json -SchemaFile (Join-Path $root 'schemas\summary.schema.json') -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0Q013 common summary failed its closed schema.')
    }
    Assert-M0SummaryDocumentSemantics -Summary $Summary
    return $true
}

function New-M0Summary {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [Alias('Input')] $SummaryInput,
        [Parameter(Mandatory)] [string] $ProtocolRoot
    )

    Assert-M0SummaryStatisticsContract
    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $script:M0SummaryRepositoryRoot 'experiments\m0\protocol')).TrimEnd('\', '/')
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $root.Equals($expectedRoot, $pathComparison)) {
        throw [UnauthorizedAccessException]::new('M0Q010 common summary ProtocolRoot differs from the trusted module repository.')
    }
    $inputSchema = Join-Path $root 'schemas\summary-input.schema.json'
    $summarySchema = Join-Path $root 'schemas\summary.schema.json'
    $inputJson = $SummaryInput | ConvertTo-Json -Compress -Depth 100
    if (-not (Test-Json -Json $inputJson -SchemaFile $inputSchema -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0Q010 common summary input failed its closed schema.')
    }
    $copy = $inputJson | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-M0SummaryInputSemantics -SummaryInput $copy

    $candidateMap = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($candidate in @($copy.candidates)) { $candidateMap.Add([string]$candidate.candidate_code, $candidate) }
    $candidateAInput = $candidateMap['A']
    $candidateBInput = $candidateMap['B']
    $canonicalCommonInputs = ConvertTo-M0SummaryCommonInputs -CommonInputs $candidateAInput.common_inputs
    $metricMapA = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $metricMapB = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($metric in @($candidateAInput.metrics)) { $metricMapA.Add([string]$metric.metric_id, $metric) }
    foreach ($metric in @($candidateBInput.metrics)) { $metricMapB.Add([string]$metric.metric_id, $metric) }
    [string[]]$metricIds = Get-M0SummaryOrdinalKeys -Keys $metricMapA.Keys

    $candidateResults = [Collections.Generic.List[object]]::new()
    foreach ($candidate in @($candidateAInput, $candidateBInput)) {
        $budgetViolated = $false
        foreach ($metric in @($candidate.metrics)) {
            [long[]]$values = Get-M0SummaryAttemptValues -Candidate $candidate -Metric $metric
            if ($values.Count -gt 0) {
                $p95 = & $script:M0SummaryStatisticsModule {
                    param($values) Get-M0Type7Quantile -Values $values -Probability 0.95
                } $values
                if (Test-M0SummaryBudgetViolation -P95 $p95 -Operator ([string]$metric.budget.operator) -Threshold ([decimal][long]$metric.budget.threshold_integer)) {
                    $budgetViolated = $true
                }
            }
        }
        $decision = & $script:M0SummaryStatisticsModule {
            param($statuses, $violated) Resolve-M0BatchDecision -AttemptStatuses $statuses -BudgetViolated $violated
        } @($candidate.attempt_statuses) $budgetViolated
        $candidateDecision = [string]$decision.decision
        if ([string]$candidate.soak.result -ceq 'fail') {
            $candidateDecision = 'fail'
        }
        elseif ([string]$candidate.soak.result -ceq 'inconclusive' -and $candidateDecision -cne 'fail') {
            $candidateDecision = 'inconclusive'
        }
        $candidateResults.Add([pscustomobject][ordered]@{
            candidate_code = [string]$candidate.candidate_code
            planned = [int]$decision.planned_count
            valid = [int]$decision.valid_count
            discarded = [int]$decision.discarded_count
            failed = [int]$decision.failed_count
            inconclusive = [int]$decision.inconclusive_count
            budget_violated = [bool]$budgetViolated
            decision = $candidateDecision
            soak = [pscustomobject][ordered]@{
                applicable = [bool]$candidate.soak.applicable
                result = [string]$candidate.soak.result
                duration_seconds = if ($null -eq $candidate.soak.duration_seconds) { $null } else { [int]$candidate.soak.duration_seconds }
                error_code = if ($null -eq $candidate.soak.error_code) { $null } else { [string]$candidate.soak.error_code }
            }
            artifact_sha256 = ([string]$candidate.artifact_sha256).ToLowerInvariant()
            toolchain_bundle_sha256 = ([string]$candidate.toolchain_bundle_sha256).ToLowerInvariant()
            lockfile_bundle_sha256 = ([string]$candidate.lockfile_bundle_sha256).ToLowerInvariant()
            runs_manifest_sha256 = ([string]$candidate.runs_manifest_sha256).ToLowerInvariant()
        })
    }

    $metricResults = [Collections.Generic.List[object]]::new()
    foreach ($metricId in $metricIds) {
        $metricA = $metricMapA[$metricId]
        $metricB = $metricMapB[$metricId]
        [long[]]$valuesA = Get-M0SummaryAttemptValues -Candidate $candidateAInput -Metric $metricA
        [long[]]$valuesB = Get-M0SummaryAttemptValues -Candidate $candidateBInput -Metric $metricB
        $p50A = $null
        $p95A = $null
        $p50B = $null
        $p95B = $null
        if ($valuesA.Count -gt 0) {
            $p50A = & $script:M0SummaryStatisticsModule { param($v) Get-M0Type7Quantile -Values $v -Probability 0.5 } $valuesA
            $p95A = & $script:M0SummaryStatisticsModule { param($v) Get-M0Type7Quantile -Values $v -Probability 0.95 } $valuesA
        }
        if ($valuesB.Count -gt 0) {
            $p50B = & $script:M0SummaryStatisticsModule { param($v) Get-M0Type7Quantile -Values $v -Probability 0.5 } $valuesB
            $p95B = & $script:M0SummaryStatisticsModule { param($v) Get-M0Type7Quantile -Values $v -Probability 0.95 } $valuesB
        }
        $bootstrap = $null
        $comparisonResult = $null
        $classification = 'inconclusive'
        $metricError = 'insufficient_valid_attempts'
        $family = [string]$metricA.metric_family
        $scale = Get-M0SummaryMetricScale -MetricFamily $family
        if ($valuesA.Count -gt 0 -and $valuesB.Count -gt 0) {
            $bootstrap = & $script:M0SummaryStatisticsModule {
                param($id, $a, $b) Get-M0BootstrapP95Difference -Metric $id -CandidateAValues $a -CandidateBValues $b
            } ([string]$metricA.metric_id) $valuesA $valuesB
            $comparisonResult = & $script:M0SummaryStatisticsModule {
                param($familyName, $a, $b, $lower, $upper)
                Resolve-M0P95Comparison -MetricFamily $familyName -P95A $a -P95B $b -CiLower $lower -CiUpper $upper
            } $family ($p95A / $scale.comparison_divisor) ($p95B / $scale.comparison_divisor) `
                ($bootstrap.ci_lower / $scale.comparison_divisor) ($bootstrap.ci_upper / $scale.comparison_divisor)
            if ($valuesA.Count -lt 20 -or $valuesB.Count -lt 20) {
                $classification = 'inconclusive'
                $metricError = 'insufficient_decisive_attempts'
            }
            else {
                $classification = [string]$comparisonResult.classification
                $metricError = $null
            }
        }
        $countA = $candidateResults[0]
        $countB = $candidateResults[1]
        $rangeA = Get-M0SummaryExactRange -Values $valuesA
        $rangeB = Get-M0SummaryExactRange -Values $valuesB
        $diagnosticsA = Get-M0SummaryDiagnostics -Values $valuesA -OutputDivisor $scale.output_divisor
        $diagnosticsB = Get-M0SummaryDiagnostics -Values $valuesB -OutputDivisor $scale.output_divisor
        $violatedA = [bool]($valuesA.Count -gt 0 -and (Test-M0SummaryBudgetViolation -P95 $p95A -Operator ([string]$metricA.budget.operator) -Threshold ([decimal][long]$metricA.budget.threshold_integer)))
        $violatedB = [bool]($valuesB.Count -gt 0 -and (Test-M0SummaryBudgetViolation -P95 $p95B -Operator ([string]$metricB.budget.operator) -Threshold ([decimal][long]$metricB.budget.threshold_integer)))
        $outputP95A = if ($valuesA.Count -eq 0) { $null } else { [decimal]$p95A / $scale.output_divisor }
        $outputP95B = if ($valuesB.Count -eq 0) { $null } else { [decimal]$p95B / $scale.output_divisor }
        $outputCiLower = if ($null -eq $bootstrap) { $null } else { [decimal]$bootstrap.ci_lower / $scale.output_divisor }
        $outputCiUpper = if ($null -eq $bootstrap) { $null } else { [decimal]$bootstrap.ci_upper / $scale.output_divisor }
        $outputComparison = $null
        if ($null -ne $bootstrap) {
            $comparisonMultiplier = [decimal]$scale.comparison_to_output_divisor
            $outputComparison = & $script:M0SummaryStatisticsModule {
                param($familyName, $a, $b, $lower, $upper)
                Resolve-M0P95Comparison -MetricFamily $familyName -P95A $a -P95B $b -CiLower $lower -CiUpper $upper
            } $family ([decimal]$outputP95A * $comparisonMultiplier) ([decimal]$outputP95B * $comparisonMultiplier) `
                ([decimal]$outputCiLower * $comparisonMultiplier) ([decimal]$outputCiUpper * $comparisonMultiplier)
            if ($null -eq $metricError) { $classification = [string]$outputComparison.classification }
        }
        $metricResults.Add([pscustomobject][ordered]@{
            metric_id = [string]$metricA.metric_id
            metric_family = $family
            unit = [string]$scale.output_unit
            candidate_a = [pscustomobject][ordered]@{
                n = $valuesA.Count
                failures = [int]$countA.failed
                discards = [int]$countA.discarded
                min = if ($valuesA.Count -eq 0) { $null } else { [decimal]$rangeA.minimum / $scale.output_divisor }
                p50 = if ($valuesA.Count -eq 0) { $null } else { [decimal]$p50A / $scale.output_divisor }
                p95 = $outputP95A
                max = if ($valuesA.Count -eq 0) { $null } else { [decimal]$rangeA.maximum / $scale.output_divisor }
                mean = $diagnosticsA.mean
                standard_deviation = $diagnosticsA.standard_deviation
                diagnostic_error_code = $diagnosticsA.error_code
                error_code = if ($valuesA.Count -eq 0) { 'no_valid_attempts' } else { $null }
                budget_operator = [string]$metricA.budget.operator
                budget = [decimal][long]$metricA.budget.threshold_integer / $scale.output_divisor
                budget_violated = $violatedA
            }
            candidate_b = [pscustomobject][ordered]@{
                n = $valuesB.Count
                failures = [int]$countB.failed
                discards = [int]$countB.discarded
                min = if ($valuesB.Count -eq 0) { $null } else { [decimal]$rangeB.minimum / $scale.output_divisor }
                p50 = if ($valuesB.Count -eq 0) { $null } else { [decimal]$p50B / $scale.output_divisor }
                p95 = $outputP95B
                max = if ($valuesB.Count -eq 0) { $null } else { [decimal]$rangeB.maximum / $scale.output_divisor }
                mean = $diagnosticsB.mean
                standard_deviation = $diagnosticsB.standard_deviation
                diagnostic_error_code = $diagnosticsB.error_code
                error_code = if ($valuesB.Count -eq 0) { 'no_valid_attempts' } else { $null }
                budget_operator = [string]$metricB.budget.operator
                budget = [decimal][long]$metricB.budget.threshold_integer / $scale.output_divisor
                budget_violated = $violatedB
            }
            difference_p95_a_minus_b = if ($null -eq $outputComparison) { $null } else { [decimal]$outputP95A - [decimal]$outputP95B }
            ci_lower = $outputCiLower
            ci_upper = $outputCiUpper
            tie_band = if ($null -eq $outputComparison) { $null } else { [decimal]$outputComparison.band / $scale.comparison_to_output_divisor }
            within_band = if ($null -eq $outputComparison) { $null } else { [bool]$outputComparison.within_band }
            error_code = $metricError
            classification = $classification
        })
    }
    $decision = if (@($candidateResults | Where-Object decision -ceq 'fail').Count -gt 0) {
        'fail'
    }
    elseif (@($candidateResults | Where-Object decision -ceq 'inconclusive').Count -gt 0) {
        'inconclusive'
    }
    else { 'pass' }

    $rawMap = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($raw in @($copy.raws)) { $rawMap.Add("$([string]$raw.candidate_code)|$([int]$raw.attempt)", $raw) }
    $canonicalRaws = [Collections.Generic.List[object]]::new()
    foreach ($candidateCode in @('A', 'B')) {
        for ($attempt = 1; $attempt -le 22; $attempt++) {
            $raw = $rawMap["$candidateCode|$attempt"]
            $canonicalRaws.Add([pscustomobject][ordered]@{
                snapshot_ordinal = [int]$raw.snapshot_ordinal
                run_id = [string]$raw.run_id
                candidate_code = $candidateCode
                attempt = $attempt
                status = [string]$raw.status
                raw_sha256 = ([string]$raw.raw_sha256).ToLowerInvariant()
                raw_final_line_sha256 = ([string]$raw.raw_final_line_sha256).ToLowerInvariant()
                run_record_sha256 = ([string]$raw.run_record_sha256).ToLowerInvariant()
                snapshot_sha256 = ([string]$raw.snapshot_sha256).ToLowerInvariant()
            })
        }
    }
    $traceMap = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($trace in @($copy.traces)) { $traceMap.Add("$([string]$trace.candidate_code)|$([string]$trace.run_id)|$([string]$trace.trace_id)", $trace) }
    [string[]]$traceKeys = Get-M0SummaryOrdinalKeys -Keys $traceMap.Keys
    $canonicalTraces = [Collections.Generic.List[object]]::new()
    foreach ($key in $traceKeys) {
        $trace = $traceMap[$key]
        $canonicalTraces.Add([pscustomobject][ordered]@{
            candidate_code = [string]$trace.candidate_code
            run_id = [string]$trace.run_id
            trace_id = [string]$trace.trace_id
            sha256 = ([string]$trace.sha256).ToLowerInvariant()
        })
    }
    $document = [pscustomobject][ordered]@{
        '$schema' = 'schemas/summary.schema.json'
        schema_id = 'm0.summary.v1'
        schema_version = 1
        protocol_id = 'M0-P1'
        experiment_id = [string]$copy.experiment_id
        scenario_id = [string]$copy.scenario_id
        source = [pscustomobject][ordered]@{
            snapshot_set_contract_id = [string]$copy.source.snapshot_set_contract_id
            snapshot_set_id = [string]$copy.source.snapshot_set_id
            snapshot_set_sha256 = ([string]$copy.source.snapshot_set_sha256).ToLowerInvariant()
            batch_contract_id = [string]$copy.source.batch_contract_id
            batch_id = [string]$copy.source.batch_id
            batch_sha256 = ([string]$copy.source.batch_sha256).ToLowerInvariant()
            schedule_sha256 = ([string]$copy.source.schedule_sha256).ToLowerInvariant()
        }
        common_inputs = $canonicalCommonInputs
        method = [pscustomobject][ordered]@{
            quantile = 'Hyndman-Fan-type-7'
            bootstrap = 'independent-percentile-bootstrap'
            resamples = 10000
            confidence_level = 0.95
            seed = 20260823
            attempt_projection = 'max-within-attempt-before-cross-attempt-quantiles'
            mean = 'arithmetic-decimal'
            standard_deviation = 'sample-n-minus-one-decimal-newton'
            standard_deviation_decimal_places = 12
            standard_deviation_rounding = 'midpoint-to-even'
        }
        candidates = @($candidateResults)
        metrics = @($metricResults)
        decision = $decision
        evidence_inventory = [pscustomobject][ordered]@{
            manifest_sha256 = Get-M0SummaryEvidenceInventoryHash -Raws @($canonicalRaws) -Traces @($canonicalTraces)
            raws = @($canonicalRaws)
            traces = @($canonicalTraces)
        }
    }
    $null = Assert-M0Summary -Summary $document -ProtocolRoot $root
    $text = ConvertTo-M0SummaryCanonicalText -Value $document
    if (-not (Test-Json -Json $text -SchemaFile $summarySchema -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0Q011 generated common summary failed its closed schema.')
    }
    $bytes = $script:M0SummaryUtf8.GetBytes($text)
    return [pscustomobject][ordered]@{
        canonical_json = $text
        bytes = $bytes
        sha256 = Get-M0SummarySha256Bytes -Bytes $bytes
        document = $document
    }
}

function Assert-M0SummaryArtifacts {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ProtocolRoot,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedProtocolSha256
    )

    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    $rootComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $expectedRoot = [IO.Path]::GetFullPath((Join-Path $script:M0SummaryRepositoryRoot 'experiments\m0\protocol')).TrimEnd('\', '/')
    if (-not $root.Equals($expectedRoot, $rootComparison)) {
        throw [UnauthorizedAccessException]::new('M0Q003 physical summary ProtocolRoot differs from the trusted module repository.')
    }

    $heldFiles = [Collections.Generic.List[object]]::new()
    try {
        $protocolSchemaFile = Read-M0SummaryFile -LiteralPath (Join-Path $root 'schemas\protocol.schema.json') -Name 'protocol schema' -KeepOpen
        $heldFiles.Add($protocolSchemaFile)
        $protocolFile = Read-M0SummaryFile -LiteralPath (Join-Path $root 'protocol.v1.json') -Name 'protocol descriptor' -KeepOpen
        $heldFiles.Add($protocolFile)
        if ($protocolFile.sha256 -cne $ExpectedProtocolSha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0Q003 protocol descriptor hash differs from the bytes captured under the outer trust lock.')
        }
        if (-not (Test-Json -Json $protocolFile.text -SchemaFile $protocolSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0Q003 protocol descriptor failed its closed schema.')
        }
        $protocol = $protocolFile.text | ConvertFrom-Json -Depth 100 -DateKind String
        $descriptorSchemaFile = Read-M0SummaryFile -LiteralPath (Join-Path $root 'schemas\summary-contract.schema.json') -Name 'summary descriptor schema' -KeepOpen
        $heldFiles.Add($descriptorSchemaFile)
        $descriptorFile = Read-M0SummaryFile -LiteralPath (Join-Path $root 'summary.v1.json') -Name 'summary descriptor' -KeepOpen
        $heldFiles.Add($descriptorFile)
        if ($descriptorFile.sha256 -cne ([string]$protocol.summary_descriptor_sha256).ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0Q003 summary descriptor hash differs from the anchored protocol descriptor.')
        }
        $sidecarFile = Read-M0SummaryFile -LiteralPath (Join-Path $root 'summary.v1.sha256') -Name 'summary descriptor sidecar' -KeepOpen
        $heldFiles.Add($sidecarFile)
        if ($sidecarFile.text -cne "$($descriptorFile.sha256)  summary.v1.json`n") {
            throw [IO.InvalidDataException]::new('M0Q003 summary descriptor sidecar differs from exact bytes.')
        }
        if (-not (Test-Json -Json $descriptorFile.text -SchemaFile $descriptorSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0Q003 summary descriptor failed its closed schema.')
        }
        $descriptor = $descriptorFile.text | ConvertFrom-Json -Depth 100 -DateKind String
        $expectedPaths = [ordered]@{
            input = 'goldens/summary-input.v1.json'
            golden = 'goldens/summary.v1.json'
            input_schema = 'schemas/summary-input.schema.json'
            summary_schema = 'schemas/summary.schema.json'
        }
        $files = [ordered]@{}
        foreach ($name in @('input', 'golden', 'input_schema', 'summary_schema')) {
            $entry = $descriptor.artifacts.$name
            if ([string]$entry.relative_path -cne [string]$expectedPaths[$name]) {
                throw [IO.InvalidDataException]::new("M0Q003 summary descriptor $name path differs from the literal contract.")
            }
            $path = [IO.Path]::GetFullPath((Join-Path $root ([string]$entry.relative_path).Replace('/', [IO.Path]::DirectorySeparatorChar)))
            if (-not $path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, $rootComparison)) {
                throw [IO.InvalidDataException]::new("M0Q003 summary $name path escaped ProtocolRoot.")
            }
            $file = Read-M0SummaryFile -LiteralPath $path -Name "summary $name" -KeepOpen
            $heldFiles.Add($file)
            if ($file.sha256 -cne [string]$entry.sha256) {
                throw [IO.InvalidDataException]::new("M0Q003 summary $name hash differs from descriptor.")
            }
            $files[$name] = $file
        }
        if ($null -ne $script:M0SummaryArtifactsTestHook) {
            $hook = $script:M0SummaryArtifactsTestHook
            $script:M0SummaryArtifactsTestHook = $null
            switch -CaseSensitive ([string]$hook.stage) {
                'after-all-open-hardlink' {
                    $null = New-Item -ItemType HardLink -Path ([string]$hook.link_path) -Target ([string]$hook.target_path)
                }
                'after-all-open-overwrite' {
                    $writeStream = $null
                    try {
                        $writeStream = [IO.FileStream]::new([string]$hook.target_path, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
                    }
                    catch [IO.IOException] { }
                    finally { if ($null -ne $writeStream) { $writeStream.Dispose() } }
                    if ($null -ne $writeStream) {
                        throw [IO.InvalidDataException]::new('M0Q003 retained summary file unexpectedly allowed a writer.')
                    }
                }
                'after-all-open-rename' {
                    $renamed = $false
                    try { [IO.File]::Move([string]$hook.target_path, [string]$hook.destination_path); $renamed = $true }
                    catch [IO.IOException] { }
                    if ($renamed) {
                        [IO.File]::Move([string]$hook.destination_path, [string]$hook.target_path)
                        throw [IO.InvalidDataException]::new('M0Q003 retained summary file unexpectedly allowed a rename.')
                    }
                }
                default { throw [IO.InvalidDataException]::new('M0Q003 unknown summary artifact identity test hook.') }
            }
        }
        foreach ($fileName in @('protocol schema', 'protocol', 'descriptor schema', 'descriptor', 'descriptor sidecar')) {
            $file = switch -CaseSensitive ($fileName) {
                'protocol schema' { $protocolSchemaFile }
                'protocol' { $protocolFile }
                'descriptor schema' { $descriptorSchemaFile }
                'descriptor' { $descriptorFile }
                'descriptor sidecar' { $sidecarFile }
            }
            Assert-M0SummaryHeldFileStable -File $file -Name "summary $fileName"
        }
        foreach ($name in @('input', 'golden', 'input_schema', 'summary_schema')) {
            Assert-M0SummaryHeldFileStable -File $files[$name] -Name "summary $name"
        }
        $input = $files.input.text | ConvertFrom-Json -Depth 100 -DateKind String
        $generated = New-M0Summary -SummaryInput $input -ProtocolRoot $root
        if (-not [Linq.Enumerable]::SequenceEqual[byte]($generated.bytes, $files.golden.bytes)) {
            throw [IO.InvalidDataException]::new('M0Q012 committed common summary golden differs from semantic execution.')
        }
        foreach ($fileName in @('protocol schema', 'protocol', 'descriptor schema', 'descriptor', 'descriptor sidecar')) {
            $file = switch -CaseSensitive ($fileName) {
                'protocol schema' { $protocolSchemaFile }
                'protocol' { $protocolFile }
                'descriptor schema' { $descriptorSchemaFile }
                'descriptor' { $descriptorFile }
                'descriptor sidecar' { $sidecarFile }
            }
            Assert-M0SummaryHeldFileStable -File $file -Name "summary $fileName"
        }
        foreach ($name in @('input', 'golden', 'input_schema', 'summary_schema')) {
            Assert-M0SummaryHeldFileStable -File $files[$name] -Name "summary $name"
        }
        return [pscustomobject][ordered]@{
            summary_id = [string]$descriptor.summary_id
            protocol_sha256 = $protocolFile.sha256
            descriptor_sha256 = $descriptorFile.sha256
            input_sha256 = $files.input.sha256
            golden_sha256 = $files.golden.sha256
            generated_sha256 = $generated.sha256
        }
    }
    finally {
        for ($index = $heldFiles.Count - 1; $index -ge 0; $index--) {
            if ($null -ne $heldFiles[$index].stream) { $heldFiles[$index].stream.Dispose() }
        }
    }
}

Export-ModuleMember -Function @('Assert-M0Summary', 'Assert-M0SummaryArtifacts', 'New-M0Summary')
