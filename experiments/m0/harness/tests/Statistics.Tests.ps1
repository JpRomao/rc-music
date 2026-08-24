$statisticsModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Statistics.psm1'
$protocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$statisticsPath = Join-Path $protocolRoot 'statistics.v1.json'
$schedulePath = Join-Path $protocolRoot 'schedule.v1.json'
$goldenPath = Join-Path $protocolRoot 'goldens\statistics.v1.json'

function Assert-TestNear {
    param(
        [Parameter(Mandatory)] [double] $Actual,
        [Parameter(Mandatory)] [double] $Expected,
        [double] $Tolerance = 0.000000001,
        [string] $Message = 'Numeric values differ.'
    )

    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        throw [InvalidOperationException]::new("$Message Expected '$Expected', got '$Actual'.")
    }
}

function Copy-TestStatisticsArtifacts {
    param([Parameter(Mandatory)] [string] $DestinationRoot)

    New-Item -ItemType Directory -Path (Join-Path $DestinationRoot 'schemas'), (Join-Path $DestinationRoot 'goldens') -Force | Out-Null
    foreach ($name in @('statistics.v1.json', 'statistics.v1.sha256', 'schedule.v1.json', 'schedule.v1.sha256')) {
        Copy-Item -LiteralPath (Join-Path $protocolRoot $name) -Destination (Join-Path $DestinationRoot $name)
    }
    foreach ($name in @('statistics.schema.json', 'schedule.schema.json', 'statistics-golden.schema.json')) {
        Copy-Item -LiteralPath (Join-Path $protocolRoot "schemas\$name") -Destination (Join-Path $DestinationRoot "schemas\$name")
    }
    Copy-Item -LiteralPath $goldenPath -Destination (Join-Path $DestinationRoot 'goldens\statistics.v1.json')
}

Test-Case 'The deterministic statistics contract files exist' {
    foreach ($path in @($statisticsModulePath, $statisticsPath, $schedulePath, $goldenPath)) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Missing statistics contract file '$path'."
    }
}

Import-Module $statisticsModulePath -Force -ErrorAction Stop
$statisticsModule = Get-Module -Name M0.Statistics
$golden = Get-Content -LiteralPath $goldenPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100

Test-Case 'The statistics module exports only its approved public API' {
    $expected = @(
        'Assert-M0StatisticsArtifacts',
        'Get-M0BootstrapP95Difference',
        'Get-M0TieBand',
        'Get-M0Type7Quantile',
        'New-M0PairedSchedule',
        'Resolve-M0BatchDecision',
        'Resolve-M0P95Comparison'
    )
    Assert-SequenceEqual -Actual @($statisticsModule.ExportedFunctions.Keys | Sort-Object) -Expected $expected
}

Test-Case 'An absolute budget violation fails even when all 22 attempts are valid' {
    $actual = Resolve-M0BatchDecision -AttemptStatuses (@('valid') * 22) -BudgetViolated $true
    Assert-Equal -Actual $actual.decision -Expected 'fail'
    Assert-True $actual.budget_violated
}

Test-Case 'The committed 22-block schedule matches the SHA-256 rank algorithm and sidecars' {
    $result = Assert-M0StatisticsArtifacts -ProtocolRoot $protocolRoot
    Assert-Equal -Actual $result.protocol_id -Expected 'M0-P1'
    Assert-Equal -Actual $result.block_count -Expected 22
    Assert-Equal -Actual $result.ab_blocks -Expected 11
    Assert-Equal -Actual $result.ba_blocks -Expected 11
    Assert-Equal -Actual $result.descriptor_sha256 -Expected 'fda650adc45ee0c10a0beaa2d69e75fe90f69539a587c05298c0d46793d5b7c3'
    Assert-Equal -Actual $result.schedule_sha256 -Expected 'aba171c3a2da46e7b9a28dd3293689e21794cb8bdb9dfb02778e9b6dd10ecae4'
    Assert-Equal -Actual $result.golden_sha256 -Expected '20761505cbe3f326c892d4885874819ba2f1aa7b27f337b81ce729a3a40c6367'

    $actual = New-M0PairedSchedule
    $committed = Get-Content -LiteralPath $schedulePath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
    Assert-Equal -Actual ($actual | ConvertTo-Json -Compress -Depth 100) -Expected ($committed | ConvertTo-Json -Compress -Depth 100)
    Assert-Equal -Actual ($actual.blocks.order -join '') -Expected 'ABBAABABABBABAABABBAABABABABBABAABBABABABABA'
    Assert-Equal -Actual $actual.blocks[0].ordering_sha256 -Expected '75c93ac3f0c36d58f65269b7e03673f000eaf61fe69240440ae4175ea1518d19'
    Assert-Equal -Actual $actual.blocks[4].ordering_sha256 -Expected '0a4de7dad7e0ac013e21d85d28f21125b0a79450793a14ddcf0fbd0f78ba4c65'
    Assert-Equal -Actual $actual.blocks[20].rank -Expected 22
}

Test-Case 'Statistics artifacts reject schedule tampering and unknown descriptor properties' {
    $tamperedRoot = Join-Path $TestTempRoot 'statistics-tamper'
    Copy-TestStatisticsArtifacts -DestinationRoot $tamperedRoot
    $tamperedSchedulePath = Join-Path $tamperedRoot 'schedule.v1.json'
    $scheduleText = [IO.File]::ReadAllText($tamperedSchedulePath).TrimEnd("`n") + " `n"
    [IO.File]::WriteAllText($tamperedSchedulePath, $scheduleText, [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0StatisticsArtifacts -ProtocolRoot $tamperedRoot } 'M0S003'

    $unknownRoot = Join-Path $TestTempRoot 'statistics-unknown'
    Copy-TestStatisticsArtifacts -DestinationRoot $unknownRoot
    $descriptorPath = Join-Path $unknownRoot 'statistics.v1.json'
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
    $descriptor | Add-Member -NotePropertyName unexpected -NotePropertyValue $true
    $text = ($descriptor | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($descriptorPath, $text, [Text.UTF8Encoding]::new($false))
    $hash = (Get-FileHash -LiteralPath $descriptorPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText((Join-Path $unknownRoot 'statistics.v1.sha256'), "$hash  statistics.v1.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0StatisticsArtifacts -ProtocolRoot $unknownRoot } 'M0S002|schema'
}

Test-Case 'Hyndman-Fan type 7 quantiles match frozen reference vectors' {
    foreach ($vector in $golden.quantile_vectors) {
        $actual = Get-M0Type7Quantile -Values @($vector.values) -Probability $vector.probability
        Assert-TestNear -Actual $actual -Expected $vector.expected -Message $vector.case_id
    }
    Assert-TestNear -Actual (Get-M0Type7Quantile -Values @(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) -Probability 0.95) -Expected 9.55
    $preciseMedian = Get-M0Type7Quantile -Values @([long]9007199254740992, [long]9007199254740994) -Probability 0.5
    Assert-True ($preciseMedian -is [decimal]) 'HF7 must return decimal so integers above 2^53 remain exact.'
    Assert-Equal -Actual $preciseMedian -Expected ([decimal]9007199254740993) 'HF7 lost integer precision above 2^53.'
    Assert-Throws { Get-M0Type7Quantile -Values @() -Probability 0.95 } 'M0S004'
    Assert-Throws { Get-M0Type7Quantile -Values @(1, 2) -Probability ([double]::NaN) } 'M0S004'
    Assert-Throws { Get-M0Type7Quantile -Values @(1, '2') -Probability 0.5 } 'M0S004'
}

Test-Case 'The portable golden preserves the above-2^53 HF7 vector as decimal strings' {
    Assert-True ($golden.quantile_decimal_vectors.Count -ge 1) 'Portable decimal quantile vectors are missing.'
    $vector = @($golden.quantile_decimal_vectors | Where-Object case_id -CEQ 'above-2pow53-median')[0]
    Assert-True ($vector.values_decimal[0] -is [string]) 'Portable values must remain JSON strings.'
    Assert-True ($vector.expected_decimal -is [string]) 'Portable expected value must remain a JSON string.'
    [long[]]$values = @($vector.values_decimal | ForEach-Object {
        [long]::Parse($_, [Globalization.NumberStyles]::Integer, [Globalization.CultureInfo]::InvariantCulture)
    })
    $probability = [double]::Parse($vector.probability_decimal, [Globalization.CultureInfo]::InvariantCulture)
    $expected = [decimal]::Parse($vector.expected_decimal, [Globalization.CultureInfo]::InvariantCulture)
    $actual = Get-M0Type7Quantile -Values $values -Probability $probability
    Assert-True ($actual -is [decimal])
    Assert-Equal -Actual $actual -Expected $expected
}

Test-Case 'Tie-band arithmetic preserves decimal precision above 2^53' {
    $actual = Get-M0TieBand -MetricFamily 'memory_bytes' -P95A ([long]9007199254740992) -P95B ([long]9007199254740994)
    Assert-True ($actual.band -is [decimal]) 'Tie bands must use decimal arithmetic.'
    Assert-True ($actual.PSObject.Properties.Name -contains 'point_estimate_lower_candidate')
    Assert-True ($actual.PSObject.Properties.Name -notcontains 'lower_candidate')
    $expected = [decimal]::Parse('450359962737049.6', [Globalization.CultureInfo]::InvariantCulture)
    Assert-Equal -Actual $actual.band -Expected $expected 'Tie-band relative fraction lost precision above 2^53.'
}

Test-Case 'Combined P95 classification requires both an outside-band delta and a sign-consistent CI' {
    $cases = @(
        @('tie', 400, 415, -25, -5, 'tie'),
        @('advantage-a', 400, 500, -120, -80, 'advantage_a'),
        @('advantage-b', 500, 400, 80, 120, 'advantage_b'),
        @('ci-crosses-zero', 500, 400, -5, 105, 'inconclusive'),
        @('ci-conflicts-with-delta', 500, 400, -120, -80, 'inconclusive')
    )
    foreach ($case in $cases) {
        $actual = Resolve-M0P95Comparison -MetricFamily 'latency_ms' -P95A $case[1] -P95B $case[2] -CiLower $case[3] -CiUpper $case[4]
        Assert-Equal -Actual $actual.classification -Expected $case[5] $case[0]
    }
    foreach ($vector in $golden.comparison_vectors) {
        $actual = Resolve-M0P95Comparison -MetricFamily $vector.metric_family -P95A $vector.p95_a -P95B $vector.p95_b -CiLower $vector.ci_lower -CiUpper $vector.ci_upper
        Assert-Equal -Actual $actual.classification -Expected $vector.expected_classification $vector.case_id
    }
    Assert-Throws { Resolve-M0P95Comparison -MetricFamily 'latency_ms' -P95A 1 -P95B 2 -CiLower 3 -CiUpper 2 } 'M0S008'
}

Test-Case 'Bootstrap sample indices match independent SHA-256 big-endian golden vectors' {
    foreach ($vector in $golden.bootstrap_index_vectors) {
        $actual = & $statisticsModule {
            param($metric, $candidate, $resample, $slot, $sampleCount)
            Get-M0BootstrapSampleIndex -Metric $metric -Candidate $candidate -Resample $resample -Slot $slot -SampleCount $sampleCount
        } $vector.metric $vector.candidate $vector.resample $vector.slot $vector.sample_count
        Assert-Equal -Actual $actual.index_zero_based -Expected $vector.expected_index_zero_based $vector.case_id
        Assert-Equal -Actual $actual.sha256 -Expected $vector.sha256 $vector.case_id
    }
    $first = & $statisticsModule {
        Get-M0BootstrapSampleIndex -Metric 'cold_start_ns' -Candidate A -Resample 1 -Slot 1 -SampleCount 20
    }
    Assert-Equal -Actual $first.sha256 -Expected '1a94cf315369df810cea0aea6d6a0f464bce86dae7e0f5f27219eaf651393bb7'
    Assert-Equal -Actual $first.index_zero_based -Expected 5
}

Test-Case 'Independent bootstrap returns the frozen percentile interval for delta P95 A minus B' {
    $vector = $golden.bootstrap_difference_vector
    $actual = Get-M0BootstrapP95Difference -Metric $vector.metric -CandidateAValues @($vector.candidate_a_values) -CandidateBValues @($vector.candidate_b_values)
    Assert-Equal -Actual $actual.resamples -Expected 10000
    Assert-Equal -Actual $actual.candidate_a_n -Expected $vector.candidate_a_values.Count
    Assert-Equal -Actual $actual.candidate_b_n -Expected $vector.candidate_b_values.Count
    Assert-TestNear -Actual $actual.candidate_a_p95 -Expected $vector.expected.candidate_a_p95
    Assert-TestNear -Actual $actual.candidate_b_p95 -Expected $vector.expected.candidate_b_p95
    Assert-TestNear -Actual $actual.difference_p95_a_minus_b -Expected $vector.expected.difference_p95_a_minus_b
    Assert-TestNear -Actual $actual.ci_lower -Expected $vector.expected.ci_lower
    Assert-TestNear -Actual $actual.ci_upper -Expected $vector.expected.ci_upper
    Assert-True $actual.excludes_zero
    Assert-TestNear -Actual $actual.difference_p95_a_minus_b -Expected 39.6
    Assert-TestNear -Actual $actual.ci_lower -Expected 26.05
    Assert-TestNear -Actual $actual.ci_upper -Expected 83
    Assert-Throws { Get-M0BootstrapP95Difference -Metric 'bad|metric' -CandidateAValues @(1) -CandidateBValues @(2) } 'M0S005'
}

Test-Case 'Tie bands use the frozen five-percent floors and preserve units' {
    foreach ($vector in $golden.tie_band_vectors) {
        $actual = Get-M0TieBand -MetricFamily $vector.metric_family -P95A $vector.p95_a -P95B $vector.p95_b
        Assert-TestNear -Actual $actual.band -Expected $vector.expected_band -Tolerance 0.000001 -Message $vector.case_id
        Assert-Equal -Actual $actual.within_band -Expected $vector.expected_within_band $vector.case_id
    }
    Assert-Throws { Get-M0TieBand -MetricFamily 'latency_ms' -P95A -1 -P95B 10 } 'M0S006'
    Assert-Throws { Get-M0TieBand -MetricFamily 'unknown' -P95A 1 -P95B 1 } 'M0S006'
}

Test-Case 'Batch decision precedence is failed then inconclusive then pass at the 20-of-22 boundary' {
    foreach ($vector in $golden.status_vectors) {
        $actual = Resolve-M0BatchDecision -AttemptStatuses @($vector.statuses) -BudgetViolated $vector.budget_violated
        Assert-Equal -Actual $actual.decision -Expected $vector.expected_decision $vector.case_id
        Assert-Equal -Actual $actual.valid_count -Expected $vector.expected_counts.valid $vector.case_id
        Assert-Equal -Actual $actual.discarded_count -Expected $vector.expected_counts.discarded $vector.case_id
        Assert-Equal -Actual $actual.failed_count -Expected $vector.expected_counts.failed $vector.case_id
        Assert-Equal -Actual $actual.inconclusive_count -Expected $vector.expected_counts.inconclusive $vector.case_id
        Assert-Equal -Actual $actual.budget_violated -Expected $vector.budget_violated $vector.case_id
    }
    Assert-Equal -Actual (Resolve-M0BatchDecision -AttemptStatuses (@('valid') * 20 + @('discarded') * 2) -BudgetViolated $false).decision -Expected 'pass'
    Assert-Equal -Actual (Resolve-M0BatchDecision -AttemptStatuses (@('valid') * 20 + 'inconclusive' + 'failed') -BudgetViolated $false).decision -Expected 'fail'
    Assert-Throws { Resolve-M0BatchDecision -AttemptStatuses (@('valid') * 21) -BudgetViolated $false } 'M0S007'
    Assert-Throws { Resolve-M0BatchDecision -AttemptStatuses (@('valid') * 21 + 'mystery') -BudgetViolated $false } 'M0S007'
}
