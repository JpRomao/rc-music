Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0ProtocolId = 'M0-P1'
$script:M0StatisticsSeed = 20260823
$script:M0BootstrapResamples = 10000

function Get-M0StatisticsSha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Assert-M0StatisticsCanonicalText {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw [IO.InvalidDataException]::new("M0S001 UTF-8 BOM is forbidden in '$(Split-Path $LiteralPath -Leaf)'.")
    }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal)) {
        throw [IO.InvalidDataException]::new("M0S001 statistics text must use LF and a final newline: '$(Split-Path $LiteralPath -Leaf)'.")
    }
}

function Read-M0StatisticsJson {
    param(
        [Parameter(Mandatory)] [string] $DocumentPath,
        [Parameter(Mandatory)] [string] $SchemaPath
    )

    Assert-M0StatisticsCanonicalText -LiteralPath $DocumentPath
    $json = Get-Content -LiteralPath $DocumentPath -Raw -Encoding UTF8
    if (-not (Test-Json -Json $json -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0S002 JSON schema rejected '$(Split-Path $DocumentPath -Leaf)'.")
    }
    return ($json | ConvertFrom-Json -Depth 100)
}

function Assert-M0StatisticsSidecar {
    param(
        [Parameter(Mandatory)] [string] $DocumentPath,
        [Parameter(Mandatory)] [string] $SidecarPath
    )

    Assert-M0StatisticsCanonicalText -LiteralPath $SidecarPath
    $fileName = Split-Path $DocumentPath -Leaf
    $escaped = [regex]::Escape($fileName)
    $sidecar = (Get-Content -LiteralPath $SidecarPath -Raw -Encoding UTF8).TrimEnd("`n")
    if ($sidecar -notmatch "^(?<hash>[a-f0-9]{64})  $escaped$") {
        throw [IO.InvalidDataException]::new("M0S003 sidecar format differs for '$fileName'.")
    }
    $actual = Get-M0StatisticsSha256 -LiteralPath $DocumentPath
    if ($actual -cne $Matches.hash) {
        throw [IO.InvalidDataException]::new("M0S003 sidecar hash mismatch for '$fileName'.")
    }
    return $actual
}

function ConvertTo-M0Int64Values {
    param([AllowEmptyCollection()] [object[]] $Values)

    if ($null -eq $Values -or $Values.Count -eq 0) {
        throw [ArgumentException]::new('M0S004 quantile values must not be empty.')
    }
    $allowedTypes = @(
        [byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64]
    )
    $converted = [long[]]::new($Values.Count)
    for ($index = 0; $index -lt $Values.Count; $index++) {
        $value = $Values[$index]
        if ($null -eq $value -or $value.GetType() -notin $allowedTypes) {
            throw [ArgumentException]::new("M0S004 value at index $index is not a signed 64-bit-compatible integer.")
        }
        $converted[$index] = [Convert]::ToInt64($value)
    }
    return ,$converted
}

function Get-M0Type7QuantileCore {
    param(
        [Parameter(Mandatory)] [decimal[]] $Values,
        [Parameter(Mandatory)] [decimal] $Probability
    )

    [decimal[]]$sorted = @($Values)
    [Array]::Sort($sorted)
    if ($sorted.Count -eq 1) {
        return [decimal]$sorted[0]
    }
    $h = [decimal]1 + (($sorted.Count - 1) * $Probability)
    $j = [int][Math]::Floor($h)
    $gamma = $h - $j
    if ($j -ge $sorted.Count) {
        return [decimal]$sorted[$sorted.Count - 1]
    }
    $lower = [decimal]$sorted[$j - 1]
    $upper = [decimal]$sorted[$j]
    return $lower + ($gamma * ($upper - $lower))
}

function Get-M0Type7Quantile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Values,
        [Parameter(Mandatory)] [double] $Probability
    )

    if ([double]::IsNaN($Probability) -or [double]::IsInfinity($Probability) -or
        $Probability -lt 0.0 -or $Probability -gt 1.0) {
        throw [ArgumentException]::new('M0S004 probability must be finite and between zero and one.')
    }
    $integers = ConvertTo-M0Int64Values -Values $Values
    return Get-M0Type7QuantileCore -Values $integers -Probability ([decimal]$Probability)
}

function New-M0PairedSchedule {
    [CmdletBinding()]
    param()

    $rows = [Collections.Generic.List[object]]::new()
    [string[]]$hashes = for ($block = 1; $block -le 22; $block++) {
        $inputText = "$script:M0ProtocolId|$script:M0StatisticsSeed|block:$block"
        $hash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($inputText))
        ).ToLowerInvariant()
        $rows.Add([pscustomobject][ordered]@{
            block = $block
            input = $inputText
            ordering_sha256 = $hash
        })
        $hash
    }
    [string[]]$rankedHashes = @($hashes)
    [Array]::Sort($rankedHashes, [StringComparer]::Ordinal)
    $rankByHash = [Collections.Generic.Dictionary[string, int]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt $rankedHashes.Count; $index++) {
        $rankByHash.Add($rankedHashes[$index], $index + 1)
    }

    $blocks = foreach ($row in $rows) {
        $rank = $rankByHash[$row.ordering_sha256]
        $order = if ($rank -le 11) { 'AB' } else { 'BA' }
        [ordered]@{
            block = $row.block
            input = $row.input
            ordering_sha256 = $row.ordering_sha256
            rank = $rank
            order = $order
            candidate_order = if ($order -ceq 'AB') { @('A', 'B') } else { @('B', 'A') }
        }
    }

    return [pscustomobject][ordered]@{
        '$schema' = 'schemas/schedule.schema.json'
        schema_id = 'm0.paired-schedule.v1'
        schema_version = 1
        schedule_id = 'M0-SCHEDULE-22'
        protocol_id = $script:M0ProtocolId
        seed = $script:M0StatisticsSeed
        algorithm = [ordered]@{
            id = 'sha256-ranked-balanced-v1'
            input_template = 'M0-P1|20260823|block:<1..22>'
            ranking = 'unsigned-byte-lexicographic'
            lower_rank_order = 'AB'
            upper_rank_order = 'BA'
        }
        block_count = 22
        ab_blocks = 11
        ba_blocks = 11
        blocks = @($blocks)
    }
}

function Assert-M0BootstrapArguments {
    param(
        [Parameter(Mandatory)] [string] $Metric,
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [int] $Resample,
        [Parameter(Mandatory)] [int] $Slot,
        [Parameter(Mandatory)] [int] $SampleCount
    )

    if ($Metric -cnotmatch '^[a-z][a-z0-9_-]{0,63}$' -or $Candidate -cnotin @('A', 'B') -or
        $SampleCount -lt 1 -or $SampleCount -gt 22 -or $Resample -lt 1 -or
        $Resample -gt $script:M0BootstrapResamples -or $Slot -lt 1 -or $Slot -gt $SampleCount) {
        throw [ArgumentException]::new('M0S005 invalid bootstrap metric, candidate, resample, slot or sample count.')
    }
}

function Get-M0BootstrapIndexCore {
    param(
        [Parameter(Mandatory)] [string] $Metric,
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [int] $Resample,
        [Parameter(Mandatory)] [int] $Slot,
        [Parameter(Mandatory)] [int] $SampleCount,
        [switch] $IncludeDetails
    )

    $inputText = "$script:M0ProtocolId|bootstrap|$script:M0StatisticsSeed|$Metric|$Candidate|$Resample|$Slot"
    $hashBytes = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($inputText))
    $prefixHex = [Convert]::ToHexString($hashBytes, 0, 8)
    $unsignedPrefix = [Convert]::ToUInt64($prefixHex, 16)
    $index = [int]($unsignedPrefix % [uint64]$SampleCount)
    if ($IncludeDetails) {
        return [pscustomobject][ordered]@{
            input = $inputText
            sha256 = [Convert]::ToHexString($hashBytes).ToLowerInvariant()
            index_zero_based = $index
        }
    }
    return $index
}

function Get-M0BootstrapSampleIndex {
    param(
        [Parameter(Mandatory)] [string] $Metric,
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [int] $Resample,
        [Parameter(Mandatory)] [int] $Slot,
        [Parameter(Mandatory)] [int] $SampleCount
    )

    Assert-M0BootstrapArguments @PSBoundParameters
    return Get-M0BootstrapIndexCore @PSBoundParameters -IncludeDetails
}

function Get-M0BootstrapP95Difference {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $Metric,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $CandidateAValues,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $CandidateBValues
    )

    if ($Metric -cnotmatch '^[a-z][a-z0-9_-]{0,63}$') {
        throw [ArgumentException]::new('M0S005 invalid bootstrap metric identifier.')
    }
    [long[]]$valuesA = ConvertTo-M0Int64Values -Values $CandidateAValues
    [long[]]$valuesB = ConvertTo-M0Int64Values -Values $CandidateBValues
    if ($valuesA.Count -gt 22 -or $valuesB.Count -gt 22) {
        throw [ArgumentException]::new('M0S005 bootstrap inputs cannot exceed 22 planned attempts.')
    }

    [decimal[]]$differences = [decimal[]]::new($script:M0BootstrapResamples)
    [long[]]$sampleA = [long[]]::new($valuesA.Count)
    [long[]]$sampleB = [long[]]::new($valuesB.Count)
    for ($resample = 1; $resample -le $script:M0BootstrapResamples; $resample++) {
        for ($slot = 1; $slot -le $valuesA.Count; $slot++) {
            $index = Get-M0BootstrapIndexCore -Metric $Metric -Candidate A -Resample $resample -Slot $slot -SampleCount $valuesA.Count
            $sampleA[$slot - 1] = $valuesA[$index]
        }
        for ($slot = 1; $slot -le $valuesB.Count; $slot++) {
            $index = Get-M0BootstrapIndexCore -Metric $Metric -Candidate B -Resample $resample -Slot $slot -SampleCount $valuesB.Count
            $sampleB[$slot - 1] = $valuesB[$index]
        }
        $differences[$resample - 1] =
            (Get-M0Type7QuantileCore -Values $sampleA -Probability ([decimal]0.95)) -
            (Get-M0Type7QuantileCore -Values $sampleB -Probability ([decimal]0.95))
    }

    $p95A = Get-M0Type7QuantileCore -Values $valuesA -Probability ([decimal]0.95)
    $p95B = Get-M0Type7QuantileCore -Values $valuesB -Probability ([decimal]0.95)
    $lower = Get-M0Type7QuantileCore -Values $differences -Probability ([decimal]0.025)
    $upper = Get-M0Type7QuantileCore -Values $differences -Probability ([decimal]0.975)
    return [pscustomobject][ordered]@{
        metric = $Metric
        method = 'independent-percentile-bootstrap'
        resamples = $script:M0BootstrapResamples
        confidence_level = 0.95
        candidate_a_n = $valuesA.Count
        candidate_b_n = $valuesB.Count
        candidate_a_p95 = $p95A
        candidate_b_p95 = $p95B
        difference_p95_a_minus_b = $p95A - $p95B
        ci_lower = $lower
        ci_upper = $upper
        excludes_zero = ($lower -gt [decimal]0 -or $upper -lt [decimal]0)
    }
}

function ConvertTo-M0FiniteDecimal {
    param(
        [AllowNull()] $Value,
        [Parameter(Mandatory)] [string] $ErrorCode
    )

    $numericTypes = @(
        [byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64], [uint64],
        [single], [double], [decimal]
    )
    if ($null -eq $Value -or $Value.GetType() -notin $numericTypes -or
        ($Value -is [double] -and ([double]::IsNaN($Value) -or [double]::IsInfinity($Value))) -or
        ($Value -is [single] -and ([single]::IsNaN($Value) -or [single]::IsInfinity($Value)))) {
        throw [ArgumentException]::new("$ErrorCode value must be a finite number.")
    }
    return [Convert]::ToDecimal($Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Get-M0TieBand {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $MetricFamily,
        [Parameter(Mandatory)] $P95A,
        [Parameter(Mandatory)] $P95B
    )

    $valueA = ConvertTo-M0FiniteDecimal -Value $P95A -ErrorCode 'M0S006'
    $valueB = ConvertTo-M0FiniteDecimal -Value $P95B -ErrorCode 'M0S006'
    if ($valueA -lt [decimal]0 -or $valueB -lt [decimal]0) {
        throw [ArgumentException]::new('M0S006 P95 values must be finite and non-negative.')
    }
    $floor = switch -CaseSensitive ($MetricFamily) {
        'latency_ms' { [decimal]20 }
        'visual_response_ms' { [decimal]1000 / [decimal]165 }
        'memory_bytes' { [decimal]2097152 }
        'cpu_percentage_points' { [decimal]0.05 }
        default { throw [ArgumentException]::new("M0S006 unknown metric family '$MetricFamily'.") }
    }
    $band = [Math]::Max([decimal]0.05 * [Math]::Min($valueA, $valueB), $floor)
    $difference = $valueA - $valueB
    $absolute = [Math]::Abs($difference)
    return [pscustomobject][ordered]@{
        metric_family = $MetricFamily
        p95_a = $valueA
        p95_b = $valueB
        band = $band
        absolute_difference = $absolute
        within_band = ($absolute -le $band)
        point_estimate_lower_candidate = if ($absolute -le $band) { $null } elseif ($difference -lt [decimal]0) { 'A' } else { 'B' }
    }
}

function Resolve-M0P95Comparison {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $MetricFamily,
        [Parameter(Mandatory)] $P95A,
        [Parameter(Mandatory)] $P95B,
        [Parameter(Mandatory)] $CiLower,
        [Parameter(Mandatory)] $CiUpper
    )

    $lower = ConvertTo-M0FiniteDecimal -Value $CiLower -ErrorCode 'M0S008'
    $upper = ConvertTo-M0FiniteDecimal -Value $CiUpper -ErrorCode 'M0S008'
    if ($lower -gt $upper) {
        throw [ArgumentException]::new('M0S008 bootstrap CI lower bound exceeds its upper bound.')
    }
    $tie = Get-M0TieBand -MetricFamily $MetricFamily -P95A $P95A -P95B $P95B
    $difference = $tie.p95_a - $tie.p95_b
    $excludesZero = ($lower -gt [decimal]0 -or $upper -lt [decimal]0)
    $classification = if ($tie.within_band) {
        'tie'
    }
    elseif ($difference -lt [decimal]0 -and $upper -lt [decimal]0) {
        'advantage_a'
    }
    elseif ($difference -gt [decimal]0 -and $lower -gt [decimal]0) {
        'advantage_b'
    }
    else {
        'inconclusive'
    }
    return [pscustomobject][ordered]@{
        metric_family = $MetricFamily
        p95_a = $tie.p95_a
        p95_b = $tie.p95_b
        difference_p95_a_minus_b = $difference
        band = $tie.band
        within_band = $tie.within_band
        ci_lower = $lower
        ci_upper = $upper
        ci_excludes_zero = $excludesZero
        classification = $classification
    }
}

function Resolve-M0BatchDecision {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $AttemptStatuses,
        [Parameter(Mandatory)] [bool] $BudgetViolated
    )

    if ($null -eq $AttemptStatuses -or $AttemptStatuses.Count -ne 22) {
        throw [ArgumentException]::new('M0S007 a batch must contain exactly 22 attempt statuses.')
    }
    $counts = [ordered]@{ valid = 0; discarded = 0; failed = 0; inconclusive = 0 }
    for ($index = 0; $index -lt $AttemptStatuses.Count; $index++) {
        $status = $AttemptStatuses[$index]
        if ($status -isnot [string] -or $status -cnotin @('valid', 'discarded', 'failed', 'inconclusive')) {
            throw [ArgumentException]::new("M0S007 invalid attempt status at index $index.")
        }
        $counts[$status]++
    }
    $decision = if ($BudgetViolated -or $counts.failed -gt 0) {
        'fail'
    }
    elseif ($counts.inconclusive -gt 0 -or $counts.valid -lt 20 -or $counts.discarded -gt 2) {
        'inconclusive'
    }
    else {
        'pass'
    }
    return [pscustomobject][ordered]@{
        planned_count = 22
        valid_count = $counts.valid
        discarded_count = $counts.discarded
        failed_count = $counts.failed
        inconclusive_count = $counts.inconclusive
        budget_violated = $BudgetViolated
        decision = $decision
    }
}

function Assert-M0StatisticsArtifacts {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $ProtocolRoot)

    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0S010 protocol root is missing: '$root'.")
    }
    $paths = [ordered]@{
        descriptor = Join-Path $root 'statistics.v1.json'
        descriptor_sidecar = Join-Path $root 'statistics.v1.sha256'
        schedule = Join-Path $root 'schedule.v1.json'
        schedule_sidecar = Join-Path $root 'schedule.v1.sha256'
        golden = Join-Path $root 'goldens\statistics.v1.json'
        descriptor_schema = Join-Path $root 'schemas\statistics.schema.json'
        schedule_schema = Join-Path $root 'schemas\schedule.schema.json'
        golden_schema = Join-Path $root 'schemas\statistics-golden.schema.json'
    }
    foreach ($directory in @($root, (Join-Path $root 'goldens'), (Join-Path $root 'schemas'))) {
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
            throw [IO.DirectoryNotFoundException]::new("M0S010 required statistics directory is missing: '$directory'.")
        }
        $item = Get-Item -LiteralPath $directory -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0S010 links are forbidden in statistics artifacts: '$directory'.")
        }
    }
    foreach ($path in $paths.Values) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("M0S010 required statistics file is missing: '$path'.")
        }
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0S010 links are forbidden in statistics artifacts: '$path'.")
        }
        Assert-M0StatisticsCanonicalText -LiteralPath $path
    }

    $descriptor = Read-M0StatisticsJson -DocumentPath $paths.descriptor -SchemaPath $paths.descriptor_schema
    $schedule = Read-M0StatisticsJson -DocumentPath $paths.schedule -SchemaPath $paths.schedule_schema
    $golden = Read-M0StatisticsJson -DocumentPath $paths.golden -SchemaPath $paths.golden_schema
    $descriptorHash = Assert-M0StatisticsSidecar -DocumentPath $paths.descriptor -SidecarPath $paths.descriptor_sidecar
    $scheduleHash = Assert-M0StatisticsSidecar -DocumentPath $paths.schedule -SidecarPath $paths.schedule_sidecar
    $goldenHash = Get-M0StatisticsSha256 -LiteralPath $paths.golden

    if ($descriptor.protocol_id -cne $script:M0ProtocolId -or $descriptor.seed -ne $script:M0StatisticsSeed -or
        $descriptor.quantile.method -cne 'Hyndman-Fan-type-7' -or $descriptor.quantile.type -ne 7 -or
        $descriptor.bootstrap.resamples -ne $script:M0BootstrapResamples -or
        $descriptor.bootstrap.confidence_level -ne 0.95 -or $descriptor.bootstrap.resample_origin -ne 1 -or
        $descriptor.bootstrap.slot_origin -ne 1 -or
        $descriptor.bootstrap.index_hash_template -cne 'M0-P1|bootstrap|20260823|<metric>|<candidate>|<resample>|<slot>' -or
        $descriptor.bootstrap.uint64_encoding -cne 'first-8-bytes-big-endian' -or
        $descriptor.bootstrap.ci_quantile_type -ne 7 -or
        $descriptor.comparison_policy.lower_is_better -ne $true -or
        $descriptor.comparison_policy.inside_band -cne 'tie' -or
        $descriptor.comparison_policy.outside_band_other -cne 'inconclusive' -or
        $descriptor.status_policy.budget_violation_decision -cne 'fail') {
        throw [IO.InvalidDataException]::new('M0S011 statistics descriptor constants differ from M0-P1.')
    }
    if ($descriptor.artifacts.schedule.relative_path -cne 'schedule.v1.json' -or
        $descriptor.artifacts.schedule.sha256 -cne $scheduleHash -or
        $descriptor.artifacts.golden.relative_path -cne 'goldens/statistics.v1.json' -or
        $descriptor.artifacts.golden.sha256 -cne $goldenHash) {
        throw [IO.InvalidDataException]::new('M0S012 statistics artifact hashes differ.')
    }
    $generated = New-M0PairedSchedule
    if (($generated | ConvertTo-Json -Compress -Depth 100) -cne ($schedule | ConvertTo-Json -Compress -Depth 100)) {
        throw [IO.InvalidDataException]::new('M0S013 committed schedule differs from the SHA-256 rank algorithm.')
    }
    if ($golden.protocol_id -cne $script:M0ProtocolId -or $golden.seed -ne $script:M0StatisticsSeed) {
        throw [IO.InvalidDataException]::new('M0S014 golden vector identity differs.')
    }

    return [pscustomobject][ordered]@{
        protocol_id = $script:M0ProtocolId
        descriptor_sha256 = $descriptorHash
        schedule_sha256 = $scheduleHash
        golden_sha256 = $goldenHash
        block_count = $schedule.blocks.Count
        ab_blocks = @($schedule.blocks | Where-Object order -CEQ 'AB').Count
        ba_blocks = @($schedule.blocks | Where-Object order -CEQ 'BA').Count
    }
}

Export-ModuleMember -Function @(
    'Assert-M0StatisticsArtifacts',
    'Get-M0BootstrapP95Difference',
    'Get-M0TieBand',
    'Get-M0Type7Quantile',
    'New-M0PairedSchedule',
    'Resolve-M0BatchDecision',
    'Resolve-M0P95Comparison'
)
