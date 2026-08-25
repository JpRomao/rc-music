$summaryModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Summary.psm1'
$summaryDescriptorPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\summary.v1.json'
$summaryInputPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\goldens\summary-input.v1.json'
$summaryGoldenPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\goldens\summary.v1.json'
$summaryInputSchemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\summary-input.schema.json'
$summarySchemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\summary.schema.json'
$summaryProtocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$summaryProtocolPath = Join-Path $summaryProtocolRoot 'protocol.v1.json'
$summaryProtocolSha256 = (Get-FileHash -LiteralPath $summaryProtocolPath -Algorithm SHA256).Hash.ToLowerInvariant()
$summaryRunnerPath = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Summary.ps1'

function Invoke-M0SummaryChildProcess {
    param([Parameter(Mandatory)] [string] $ScriptText, [Parameter(Mandatory)] [string] $Name)

    $path = Join-Path $TestTempRoot "$Name.ps1"
    [IO.File]::WriteAllText($path, $ScriptText, [Text.UTF8Encoding]::new($false))
    $previousNativePreference = $PSNativeCommandUseErrorActionPreference
    try {
        $PSNativeCommandUseErrorActionPreference = $false
        $output = & pwsh -NoLogo -NoProfile -NonInteractive -File $path 2>&1 | Out-String
        $exitCode = $LASTEXITCODE
    }
    finally {
        $PSNativeCommandUseErrorActionPreference = $previousNativePreference
    }
    return [pscustomobject][ordered]@{ exit_code = $exitCode; output = $output }
}

Test-Case 'The common M0 summary behavior contract exists independently from synthetic pilot evidence' {
    foreach ($path in @($summaryModulePath, $summaryInputPath, $summaryInputSchemaPath, $summarySchemaPath, $summaryRunnerPath)) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required common summary contract file is missing: '$path'."
    }
}

Test-Case 'Physical summary trust rejects a self-consistent in-repository fake ProtocolRoot before reading it' {
    if (-not $IsWindows) { return }
    if (-not (Get-Command Assert-M0SummaryArtifacts -ErrorAction SilentlyContinue)) {
        Import-Module -Name $summaryModulePath -ErrorAction Stop
    }
    $fakeRoot = Join-Path $TestTempRoot 'fake-protocol-root'
    $null = New-Item -ItemType Directory -Path $fakeRoot
    Assert-Throws { Assert-M0SummaryArtifacts -ProtocolRoot $fakeRoot -ExpectedProtocolSha256 $summaryProtocolSha256 } 'ProtocolRoot differs'
}

Test-Case 'Physical summary trust requires the exact protocol bytes captured under the outer trust lock' {
    if (-not $IsWindows) { return }
    Assert-Throws {
        Assert-M0SummaryArtifacts -ProtocolRoot $summaryProtocolRoot -ExpectedProtocolSha256 ('0' * 64)
    } 'protocol.*hash|protocol.*bytes'
}

Test-Case 'A self-consistent post-capture summary and protocol reanchor cannot replace the trusted domain' {
    if (-not $IsWindows) { return }
    $cloneRoot = Join-Path $TestTempRoot 'summary-reanchor-repository'
    $cloneM0Root = Join-Path $cloneRoot 'experiments\m0'
    $null = New-Item -ItemType Directory -Path $cloneM0Root -Force
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\harness') -Destination $cloneM0Root -Recurse
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\protocol') -Destination $cloneM0Root -Recurse
    $cloneSummaryModule = Join-Path $cloneM0Root 'harness\M0.Summary.psm1'
    $cloneProtocolRoot = Join-Path $cloneM0Root 'protocol'
    $escapedModule = $cloneSummaryModule.Replace("'", "''")
    $escapedProtocolRoot = $cloneProtocolRoot.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
`$utf8 = [Text.UTF8Encoding]::new(`$false)
Import-Module -Name '$escapedModule' -ErrorAction Stop
`$protocolPath = Join-Path '$escapedProtocolRoot' 'protocol.v1.json'
`$originalProtocolHash = (Get-FileHash -LiteralPath `$protocolPath -Algorithm SHA256).Hash
`$inputPath = Join-Path '$escapedProtocolRoot' 'goldens\summary-input.v1.json'
`$goldenPath = Join-Path '$escapedProtocolRoot' 'goldens\summary.v1.json'
`$descriptorPath = Join-Path '$escapedProtocolRoot' 'summary.v1.json'
`$sidecarPath = Join-Path '$escapedProtocolRoot' 'summary.v1.sha256'
`$input = Get-Content -LiteralPath `$inputPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$input.source.batch_id = 'batch-reanchored-summary'
`$inputText = (`$input | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
[IO.File]::WriteAllText(`$inputPath, `$inputText, `$utf8)
`$generated = New-M0Summary -SummaryInput `$input -ProtocolRoot '$escapedProtocolRoot'
[IO.File]::WriteAllBytes(`$goldenPath, `$generated.bytes)
`$descriptor = Get-Content -LiteralPath `$descriptorPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$descriptor.artifacts.input.sha256 = (Get-FileHash -LiteralPath `$inputPath -Algorithm SHA256).Hash.ToLowerInvariant()
`$descriptor.artifacts.golden.sha256 = (Get-FileHash -LiteralPath `$goldenPath -Algorithm SHA256).Hash.ToLowerInvariant()
`$descriptorText = (`$descriptor | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
[IO.File]::WriteAllText(`$descriptorPath, `$descriptorText, `$utf8)
`$descriptorHash = (Get-FileHash -LiteralPath `$descriptorPath -Algorithm SHA256).Hash
[IO.File]::WriteAllText(`$sidecarPath, "`$(`$descriptorHash.ToLowerInvariant())  summary.v1.json`n", `$utf8)
`$protocol = Get-Content -LiteralPath `$protocolPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$protocol.summary_descriptor_sha256 = `$descriptorHash.ToUpperInvariant()
`$protocolText = (`$protocol | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
[IO.File]::WriteAllText(`$protocolPath, `$protocolText, `$utf8)
try {
    Assert-M0SummaryArtifacts -ProtocolRoot '$escapedProtocolRoot' -ExpectedProtocolSha256 `$originalProtocolHash
    throw 'POST-CAPTURE-REANCHOR-ACCEPTED'
}
catch {
    if (`$_.Exception.Message -notmatch 'protocol descriptor hash differs') { throw }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-post-capture-reanchor'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Self-consistent post-capture reanchor was not rejected: $($result.output)"
    Assert-True ($result.output -match 'OK') "Self-consistent reanchor probe did not finish: $($result.output)"
    Assert-True ($result.output -notmatch 'POST-CAPTURE-REANCHOR-ACCEPTED') 'Self-consistent post-capture reanchor crossed the trusted protocol boundary.'
}

Test-Case 'Summary descriptor schema fixes each artifact slot to one literal path' {
    $descriptor = [pscustomobject][ordered]@{
        '$schema' = 'schemas/summary-contract.schema.json'
        schema_id = 'm0.summary-contract.v1'
        schema_version = 1
        protocol_id = 'M0-P1'
        summary_id = 'M0-SUMMARY-1'
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
            input_boundary = 'prevalidated-pure-input-no-evidence-io'
            runs_manifest_contract = 'm0.runs-manifest.v1'
            evidence_inventory_contract = 'm0.summary-evidence-inventory.v1'
            output_serialization = 'ordered-json-utf8-no-bom-lf'
        }
        artifacts = [pscustomobject][ordered]@{
            input = [pscustomobject][ordered]@{ relative_path = 'goldens/summary-input.v1.json'; sha256 = ('a' * 64) }
            golden = [pscustomobject][ordered]@{ relative_path = 'goldens/summary.v1.json'; sha256 = ('b' * 64) }
            input_schema = [pscustomobject][ordered]@{ relative_path = 'schemas/summary-input.schema.json'; sha256 = ('c' * 64) }
            summary_schema = [pscustomobject][ordered]@{ relative_path = 'schemas/summary.schema.json'; sha256 = ('d' * 64) }
        }
    }
    $schemaPath = Join-Path $summaryProtocolRoot 'schemas\summary-contract.schema.json'
    Assert-True (Test-Json -Json ($descriptor | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath)
    foreach ($mutation in @(
        @{ slot = 'input'; path = 'goldens/summary.v1.json' },
        @{ slot = 'golden'; path = 'goldens/summary-input.v1.json' },
        @{ slot = 'input_schema'; path = 'schemas/summary.schema.json' },
        @{ slot = 'summary_schema'; path = 'schemas/summary-input.schema.json' }
    )) {
        $bad = $descriptor | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
        $bad.artifacts.($mutation.slot).relative_path = $mutation.path
        Assert-True (-not (Test-Json -Json ($bad | ConvertTo-Json -Compress -Depth 100) -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) `
            "Descriptor slot '$($mutation.slot)' accepted aliased path '$($mutation.path)'."
    }
}

$summaryProcessModulesBeforeImport = @(Get-Module -All -Name M0.Process)
$summaryProcessInstanceIdsBeforeImport = @($summaryProcessModulesBeforeImport | ForEach-Object {
    & $_ { $script:M0ProcessModuleInstanceId }
})
$summaryNativeStateBeforeImport = [ordered]@{}
foreach ($nativeTypeName in @('M0ProcessNativeV1.PilotSession', 'M0ProcessNativeV1.ProcessCapability', 'M0ProcessNativeV1.TrustedFile')) {
    $summaryNativeStateBeforeImport[$nativeTypeName] = [bool]($nativeTypeName -as [type])
}
Import-Module -Name $summaryModulePath -Force -ErrorAction Stop
$summaryModule = Get-Module -Name M0.Summary
$summaryFixture = Get-Content -LiteralPath $summaryInputPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String

function Copy-M0SummaryTestObject {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String)
}

function New-M0SummaryAttemptValues {
    param([Parameter(Mandatory)] [int] $Count, [Parameter(Mandatory)] [long] $StartValue)
    return @(1..$Count | ForEach-Object {
        [pscustomobject][ordered]@{ attempt = $_; value_integer = $StartValue + $_ - 1 }
    })
}

function Sync-M0SummaryTestReceipts {
    param([Parameter(Mandatory)] $SummaryInput)

    $utf8 = [Text.UTF8Encoding]::new($false)
    foreach ($candidateCode in @('A', 'B')) {
        $candidate = @($SummaryInput.candidates | Where-Object candidate_code -CEQ $candidateCode)[0]
        $candidateRaws = @($SummaryInput.raws | Where-Object candidate_code -CEQ $candidateCode | Sort-Object attempt)
        foreach ($raw in $candidateRaws) {
            $raw.status = [string]$candidate.attempt_statuses[[int]$raw.attempt - 1]
        }
        $manifest = [pscustomobject][ordered]@{
            schema_id = 'm0.runs-manifest.v1'
            candidate_code = $candidateCode
            runs = @($candidateRaws | ForEach-Object {
                [pscustomobject][ordered]@{
                    attempt = [int]$_.attempt
                    run_id = [string]$_.run_id
                    run_record_sha256 = ([string]$_.run_record_sha256).ToLowerInvariant()
                    raw_final_line_sha256 = ([string]$_.raw_final_line_sha256).ToLowerInvariant()
                    status = [string]$_.status
                }
            })
        }
        $manifestJson = ConvertTo-Json -InputObject $manifest -Compress -Depth 100
        $candidate.runs_manifest_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($utf8.GetBytes($manifestJson + "`n"))
        ).ToLowerInvariant()
    }
    return $SummaryInput
}

Test-Case 'Pure summary APIs do not load or mutate the process module or native process trust types' {
    $afterImportModules = @(Get-Module -All -Name M0.Process)
    Assert-Equal -Actual $afterImportModules.Count -Expected $summaryProcessModulesBeforeImport.Count
    $afterImportInstanceIds = @($afterImportModules | ForEach-Object { & $_ { $script:M0ProcessModuleInstanceId } })
    Assert-Equal -Actual ($afterImportInstanceIds -join '|') -Expected ($summaryProcessInstanceIdsBeforeImport -join '|')
    foreach ($nativeTypeName in @('M0ProcessNativeV1.PilotSession', 'M0ProcessNativeV1.ProcessCapability', 'M0ProcessNativeV1.TrustedFile')) {
        Assert-Equal -Actual ([bool]($nativeTypeName -as [type])) -Expected ([bool]$summaryNativeStateBeforeImport[$nativeTypeName]) `
            "Pure summary import changed native type '$nativeTypeName'."
    }
    $result = New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot
    Assert-True (Assert-M0Summary -Summary $result.document -ProtocolRoot $summaryProtocolRoot)
    $afterDerivationModules = @(Get-Module -All -Name M0.Process)
    Assert-Equal -Actual $afterDerivationModules.Count -Expected $summaryProcessModulesBeforeImport.Count
    $afterDerivationInstanceIds = @($afterDerivationModules | ForEach-Object { & $_ { $script:M0ProcessModuleInstanceId } })
    Assert-Equal -Actual ($afterDerivationInstanceIds -join '|') -Expected ($summaryProcessInstanceIdsBeforeImport -join '|')
    foreach ($nativeTypeName in @('M0ProcessNativeV1.PilotSession', 'M0ProcessNativeV1.ProcessCapability', 'M0ProcessNativeV1.TrustedFile')) {
        Assert-Equal -Actual ([bool]($nativeTypeName -as [type])) -Expected ([bool]$summaryNativeStateBeforeImport[$nativeTypeName]) `
            "Pure summary derivation changed native type '$nativeTypeName'."
    }
}

Test-Case 'Common summary derives counts methods converted metrics budgets decisions and complete inventories' {
    $result = New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $result.document.method.quantile -Expected 'Hyndman-Fan-type-7'
    Assert-Equal -Actual $result.document.method.bootstrap -Expected 'independent-percentile-bootstrap'
    Assert-Equal -Actual $result.document.method.resamples -Expected 10000
    Assert-Equal -Actual $result.document.method.seed -Expected 20260823
    Assert-Equal -Actual $result.document.method.attempt_projection -Expected 'max-within-attempt-before-cross-attempt-quantiles'
    Assert-Equal -Actual $result.document.method.mean -Expected 'arithmetic-decimal'
    Assert-Equal -Actual $result.document.method.standard_deviation -Expected 'sample-n-minus-one-decimal-newton'
    Assert-Equal -Actual $result.document.method.standard_deviation_decimal_places -Expected 12
    Assert-Equal -Actual $result.document.method.standard_deviation_rounding -Expected 'midpoint-to-even'
    Assert-Equal -Actual $result.document.candidates[0].planned -Expected 22
    Assert-Equal -Actual $result.document.candidates[0].valid -Expected 1
    Assert-Equal -Actual $result.document.candidates[0].inconclusive -Expected 21
    Assert-Equal -Actual $result.document.metrics[0].unit -Expected 'milliseconds'
    Assert-Equal -Actual $result.document.metrics[0].candidate_a.p95 -Expected ([decimal]0.0001)
    Assert-Equal -Actual $result.document.metrics[0].candidate_a.mean -Expected ([decimal]0.0001)
    Assert-Equal -Actual $result.document.metrics[0].candidate_a.standard_deviation -Expected $null
    Assert-Equal -Actual $result.document.metrics[0].candidate_a.diagnostic_error_code -Expected 'insufficient_samples_for_standard_deviation'
    Assert-Equal -Actual $result.document.metrics[0].candidate_b.p95 -Expected ([decimal]0.00012)
    Assert-Equal -Actual $result.document.metrics[0].ci_lower -Expected ([decimal]-0.00002)
    Assert-Equal -Actual $result.document.metrics[0].ci_upper -Expected ([decimal]-0.00002)
    Assert-Equal -Actual $result.document.metrics[0].classification -Expected 'inconclusive'
    Assert-Equal -Actual $result.document.metrics[0].error_code -Expected 'insufficient_decisive_attempts'
    Assert-Equal -Actual $result.document.metrics[0].tie_band -Expected ([decimal]20)
    Assert-Equal -Actual $result.document.metrics[0].within_band -Expected $true
    Assert-Equal -Actual $result.document.metrics[0].candidate_a.budget_operator -Expected 'less-than-or-equal'
    Assert-Equal -Actual $result.document.decision -Expected 'inconclusive'
    Assert-Equal -Actual $result.document.source.snapshot_set_contract_id -Expected 'm0.evidence-snapshot-set.v1'
    Assert-Equal -Actual $result.document.source.batch_contract_id -Expected 'm0.batch.v1'
    Assert-Equal -Actual $result.document.common_inputs.protocol_sha256 -Expected ('a' * 64)
    Assert-Equal -Actual $result.document.candidates[0].runs_manifest_sha256 -Expected $summaryFixture.candidates[0].runs_manifest_sha256
    Assert-Equal -Actual $result.document.evidence_inventory.raws.Count -Expected 44
    Assert-Equal -Actual $result.document.evidence_inventory.traces.Count -Expected 2
    Assert-Equal -Actual $result.document.evidence_inventory.raws[0].run_id -Expected 'run-A-golden-01'
}

Test-Case 'Zero valid attempts serialize null statistics with explicit errors and remain decision-inconclusive' {
    $input = Copy-M0SummaryTestObject $summaryFixture
    $input.candidates[0].attempt_statuses = @(1..22 | ForEach-Object { 'inconclusive' })
    $input.candidates[0].metrics[0].values = @()
    $null = Sync-M0SummaryTestReceipts -SummaryInput $input
    $result = New-M0Summary -SummaryInput $input -ProtocolRoot $summaryProtocolRoot
    $metric = $result.document.metrics[0]
    Assert-Equal -Actual $metric.candidate_a.n -Expected 0
    Assert-Equal -Actual $metric.candidate_a.min -Expected $null
    Assert-Equal -Actual $metric.candidate_a.p50 -Expected $null
    Assert-Equal -Actual $metric.candidate_a.p95 -Expected $null
    Assert-Equal -Actual $metric.candidate_a.max -Expected $null
    Assert-Equal -Actual $metric.candidate_a.mean -Expected $null
    Assert-Equal -Actual $metric.candidate_a.standard_deviation -Expected $null
    Assert-Equal -Actual $metric.candidate_a.diagnostic_error_code -Expected 'no_valid_attempts'
    Assert-Equal -Actual $metric.candidate_a.error_code -Expected 'no_valid_attempts'
    Assert-Equal -Actual $metric.ci_lower -Expected $null
    Assert-Equal -Actual $metric.ci_upper -Expected $null
    Assert-Equal -Actual $metric.error_code -Expected 'insufficient_valid_attempts'
    Assert-Equal -Actual $metric.classification -Expected 'inconclusive'
    Assert-Equal -Actual $result.document.decision -Expected 'inconclusive'
}

Test-Case 'Summary schema rejects null numeric contradictions in available and unavailable branches' {
    $input = Copy-M0SummaryTestObject $summaryFixture
    $input.candidates[0].attempt_statuses = @(1..22 | ForEach-Object { 'inconclusive' })
    $input.candidates[0].metrics[0].values = @()
    $null = Sync-M0SummaryTestReceipts -SummaryInput $input
    $unavailable = (New-M0Summary -SummaryInput $input -ProtocolRoot $summaryProtocolRoot).document
    $contradiction = Copy-M0SummaryTestObject $unavailable
    $contradiction.metrics[0].candidate_a.p95 = 1
    Assert-True (-not (Test-Json -Json ($contradiction | ConvertTo-Json -Compress -Depth 100) -SchemaFile $summarySchemaPath -ErrorAction SilentlyContinue)) `
        'n=0 with a numeric P95 must fail the summary schema.'

    $available = (New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot).document
    $contradiction = Copy-M0SummaryTestObject $available
    $contradiction.metrics[0].candidate_a.p95 = $null
    $contradiction.metrics[0].candidate_a.error_code = 'no_valid_attempts'
    Assert-True (-not (Test-Json -Json ($contradiction | ConvertTo-Json -Compress -Depth 100) -SchemaFile $summarySchemaPath -ErrorAction SilentlyContinue)) `
        'n>0 with null P95/error must fail the summary schema.'
}

Test-Case 'Metric families apply distinct latency and visual tie floors after exact nanosecond conversion' {
    $latency = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $latency.candidates) {
        $candidate.attempt_statuses = @((1..20 | ForEach-Object { 'valid' }) + @('inconclusive', 'inconclusive'))
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $latency
    $latency.candidates[0].metrics[0].values = @(1..20 | ForEach-Object { [pscustomobject][ordered]@{ attempt = $_; value_integer = 100000000 } })
    $latency.candidates[1].metrics[0].values = @(1..20 | ForEach-Object { [pscustomobject][ordered]@{ attempt = $_; value_integer = 115000000 } })
    $latency.candidates[0].metrics[0].budget.threshold_integer = 200000000
    $latency.candidates[1].metrics[0].budget.threshold_integer = 200000000
    $latencyResult = New-M0Summary -SummaryInput $latency -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $latencyResult.document.metrics[0].classification -Expected 'tie'
    Assert-Equal -Actual $latencyResult.document.metrics[0].candidate_a.p95 -Expected ([decimal]100)
    Assert-Equal -Actual $latencyResult.document.metrics[0].tie_band -Expected ([decimal]20)
    Assert-Equal -Actual $latencyResult.document.metrics[0].within_band -Expected $true

    $visual = Copy-M0SummaryTestObject $latency
    $visual.candidates[0].metrics[0].metric_family = 'visual_response_ms'
    $visual.candidates[1].metrics[0].metric_family = 'visual_response_ms'
    $visualResult = New-M0Summary -SummaryInput $visual -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $visualResult.document.metrics[0].classification -Expected 'advantage_a'
    Assert-Equal -Actual $visualResult.document.metrics[0].tie_band -Expected ([decimal]1000 / [decimal]165)
    Assert-Equal -Actual $visualResult.document.metrics[0].within_band -Expected $false
}

Test-Case 'Attempt-keyed samples are canonical by attempt and preserve Int64 values above double precision' {
    $ordered = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $ordered.candidates) {
        $candidate.attempt_statuses = @('valid', 'valid') + @(1..20 | ForEach-Object { 'inconclusive' })
        $candidate.metrics[0].metric_family = 'memory_bytes'
        $candidate.metrics[0].unit = 'bytes'
        $candidate.metrics[0].budget.threshold_integer = 18014398509481984
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $ordered
    $ordered.candidates[0].metrics[0].values = @(
        [pscustomobject][ordered]@{ attempt = 1; value_integer = [long]9007199254740993 },
        [pscustomobject][ordered]@{ attempt = 2; value_integer = [long]9007199254740995 }
    )
    $ordered.candidates[1].metrics[0].values = @(
        [pscustomobject][ordered]@{ attempt = 1; value_integer = [long]9007199254740994 },
        [pscustomobject][ordered]@{ attempt = 2; value_integer = [long]9007199254740996 }
    )
    $first = New-M0Summary -SummaryInput $ordered -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $first.document.metrics[0].candidate_a.min -Expected ([decimal]9007199254740993 / [decimal]1048576)
    Assert-Equal -Actual $first.document.metrics[0].candidate_a.max -Expected ([decimal]9007199254740995 / [decimal]1048576)
    Assert-Equal -Actual $first.document.metrics[0].candidate_a.mean -Expected ([decimal]9007199254740994 / [decimal]1048576)
    Assert-Equal -Actual $first.document.metrics[0].candidate_a.standard_deviation -Expected ([decimal]0.000001348699)

    $permuted = Copy-M0SummaryTestObject $ordered
    foreach ($candidate in $permuted.candidates) {
        $candidate.metrics[0].values = @($candidate.metrics[0].values | Sort-Object attempt -Descending)
    }
    $second = New-M0Summary -SummaryInput $permuted -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $second.canonical_json -Expected $first.canonical_json
}

Test-Case 'Arithmetic mean and sample standard deviation are deterministic in converted units' {
    $input = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $input.candidates) {
        $candidate.attempt_statuses = @('valid', 'valid') + @(1..20 | ForEach-Object { 'inconclusive' })
        $candidate.metrics[0].values = @(
            [pscustomobject][ordered]@{ attempt = 1; value_integer = 100000000 },
            [pscustomobject][ordered]@{ attempt = 2; value_integer = 120000000 }
        )
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $input
    $result = New-M0Summary -SummaryInput $input -ProtocolRoot $summaryProtocolRoot
    $candidate = $result.document.metrics[0].candidate_a
    Assert-Equal -Actual $candidate.mean -Expected ([decimal]110)
    Assert-Equal -Actual $candidate.standard_deviation -Expected ([decimal]14.142135623731)
    Assert-Equal -Actual $candidate.diagnostic_error_code -Expected $null
}

Test-Case 'Strict and inclusive budget operators differ at the exact threshold' {
    $inclusive = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $inclusive.candidates) {
        $candidate.attempt_statuses = @((1..20 | ForEach-Object { 'valid' }) + @('inconclusive', 'inconclusive'))
        $candidate.metrics[0].values = @(1..20 | ForEach-Object { [pscustomobject][ordered]@{ attempt = $_; value_integer = 200000000 } })
        $candidate.metrics[0].budget.threshold_integer = 200000000
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $inclusive
    $inclusiveResult = New-M0Summary -SummaryInput $inclusive -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $inclusiveResult.document.candidates[0].budget_violated -Expected $false

    $strict = Copy-M0SummaryTestObject $inclusive
    foreach ($candidate in $strict.candidates) { $candidate.metrics[0].budget.operator = 'less-than' }
    $strictResult = New-M0Summary -SummaryInput $strict -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $strictResult.document.candidates[0].budget_violated -Expected $true
    Assert-Equal -Actual $strictResult.document.candidates[0].decision -Expected 'fail'
}

Test-Case 'CPU basis points and bytes convert exactly while family-unit spoofing fails closed' {
    $cpu = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $cpu.candidates) {
        $candidate.metrics[0].metric_family = 'cpu_percentage_points'
        $candidate.metrics[0].unit = 'basis_points'
        $candidate.metrics[0].budget.threshold_integer = 50
    }
    $cpu.candidates[0].metrics[0].values = @([pscustomobject][ordered]@{ attempt = 1; value_integer = 25 })
    $cpu.candidates[1].metrics[0].values = @([pscustomobject][ordered]@{ attempt = 1; value_integer = 40 })
    $cpuResult = New-M0Summary -SummaryInput $cpu -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $cpuResult.document.metrics[0].unit -Expected 'percentage_points'
    Assert-Equal -Actual $cpuResult.document.metrics[0].candidate_a.p95 -Expected ([decimal]0.25)
    Assert-Equal -Actual $cpuResult.document.metrics[0].candidate_a.budget -Expected ([decimal]0.5)
    Assert-Equal -Actual $cpuResult.document.metrics[0].difference_p95_a_minus_b -Expected ([decimal]-0.15)
    Assert-Equal -Actual $cpuResult.document.metrics[0].ci_lower -Expected ([decimal]-0.15)
    Assert-Equal -Actual $cpuResult.document.metrics[0].ci_upper -Expected ([decimal]-0.15)
    Assert-Equal -Actual $cpuResult.document.metrics[0].tie_band -Expected ([decimal]0.05)

    $memory = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $memory.candidates) {
        $candidate.metrics[0].metric_family = 'memory_bytes'
        $candidate.metrics[0].unit = 'bytes'
        $candidate.metrics[0].budget.threshold_integer = 2097152
    }
    $memory.candidates[0].metrics[0].values = @([pscustomobject][ordered]@{ attempt = 1; value_integer = 1048576 })
    $memory.candidates[1].metrics[0].values = @([pscustomobject][ordered]@{ attempt = 1; value_integer = 1572864 })
    $memoryResult = New-M0Summary -SummaryInput $memory -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $memoryResult.document.metrics[0].unit -Expected 'mebibytes'
    Assert-Equal -Actual $memoryResult.document.metrics[0].candidate_a.p95 -Expected ([decimal]1)
    Assert-Equal -Actual $memoryResult.document.metrics[0].candidate_a.budget -Expected ([decimal]2)
    Assert-Equal -Actual $memoryResult.document.metrics[0].difference_p95_a_minus_b -Expected ([decimal]-0.5)
    Assert-Equal -Actual $memoryResult.document.metrics[0].ci_lower -Expected ([decimal]-0.5)
    Assert-Equal -Actual $memoryResult.document.metrics[0].ci_upper -Expected ([decimal]-0.5)
    Assert-Equal -Actual $memoryResult.document.metrics[0].tie_band -Expected ([decimal]2)

    $spoof = Copy-M0SummaryTestObject $summaryFixture
    $spoof.candidates[0].metrics[0].metric_family = 'memory_bytes'
    $spoof.candidates[1].metrics[0].metric_family = 'memory_bytes'
    Assert-Throws { New-M0Summary -SummaryInput $spoof -ProtocolRoot $summaryProtocolRoot } 'family/unit'
}

Test-Case 'Canonical common summary ignores input property and inventory order but changes on semantic data' {
    $canonicalInput = Copy-M0SummaryTestObject $summaryFixture
    for ($candidateIndex = 0; $candidateIndex -lt 2; $candidateIndex++) {
        $secondMetric = Copy-M0SummaryTestObject $canonicalInput.candidates[$candidateIndex].metrics[0]
        $secondMetric.metric_id = 'cpu_percent_bp'
        $secondMetric.metric_family = 'cpu_percentage_points'
        $secondMetric.unit = 'basis_points'
        $secondMetric.values[0].value_integer = if ($candidateIndex -eq 0) { 25 } else { 40 }
        $secondMetric.budget.threshold_integer = 50
        $canonicalInput.candidates[$candidateIndex].metrics = @($canonicalInput.candidates[$candidateIndex].metrics) + @($secondMetric)
    }
    $first = New-M0Summary -SummaryInput $canonicalInput -ProtocolRoot $summaryProtocolRoot
    Assert-SequenceEqual -Actual @($first.document.metrics | ForEach-Object metric_id) -Expected @('cpu_percent_bp', 'window_actionable_ns')
    $reordered = Copy-M0SummaryTestObject $canonicalInput
    $reordered.candidates = @($reordered.candidates[1], $reordered.candidates[0])
    $reordered.raws = @($reordered.raws | Sort-Object attempt -Descending)
    $reordered.traces = @($reordered.traces | Sort-Object candidate_code -Descending)
    foreach ($candidate in $reordered.candidates) { $candidate.metrics = @($candidate.metrics | Sort-Object metric_id) }
    $second = New-M0Summary -SummaryInput $reordered -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $second.canonical_json -Expected $first.canonical_json
    Assert-Equal -Actual $second.sha256 -Expected $first.sha256

    $changed = Copy-M0SummaryTestObject $canonicalInput
    $changed.candidates[0].metrics[0].values[0].value_integer = 101
    $third = New-M0Summary -SummaryInput $changed -ProtocolRoot $summaryProtocolRoot
    Assert-True ($third.sha256 -cne $first.sha256) 'Changing a metric datum must change canonical summary bytes.'
}

Test-Case 'Public summary assertion rejects schema-valid semantic contradictions and noncanonical inventories' {
    $valid = (New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot).document
    Assert-True (Assert-M0Summary -Summary $valid -ProtocolRoot $summaryProtocolRoot)

    $sameCandidate = Copy-M0SummaryTestObject $valid
    $sameCandidate.candidates[1].candidate_code = 'A'
    Assert-True (-not (Test-Json -Json ($sameCandidate | ConvertTo-Json -Compress -Depth 100) -SchemaFile $summarySchemaPath -ErrorAction SilentlyContinue)) `
        'The output schema itself must fix positional A/B identity.'
    Assert-Throws { Assert-M0Summary -Summary $sameCandidate -ProtocolRoot $summaryProtocolRoot } 'schema|candidates'

    $badCounts = Copy-M0SummaryTestObject $valid
    $badCounts.candidates[0].valid = 2
    Assert-Throws { Assert-M0Summary -Summary $badCounts -ProtocolRoot $summaryProtocolRoot } 'sum to 22|counts'

    $badCandidateDecision = Copy-M0SummaryTestObject $valid
    $badCandidateDecision.candidates[0].decision = 'pass'
    Assert-Throws { Assert-M0Summary -Summary $badCandidateDecision -ProtocolRoot $summaryProtocolRoot } 'decision'

    $badGlobalDecision = Copy-M0SummaryTestObject $valid
    $badGlobalDecision.decision = 'pass'
    Assert-Throws { Assert-M0Summary -Summary $badGlobalDecision -ProtocolRoot $summaryProtocolRoot } 'global decision'

    $tinyDifferenceDrift = Copy-M0SummaryTestObject $valid
    $tinyDifferenceDrift.metrics[0].difference_p95_a_minus_b = [decimal]$tinyDifferenceDrift.metrics[0].difference_p95_a_minus_b + [decimal]'0.0000000000000001'
    Assert-Throws { Assert-M0Summary -Summary $tinyDifferenceDrift -ProtocolRoot $summaryProtocolRoot } 'difference'

    $tinyTieDrift = Copy-M0SummaryTestObject $valid
    $tinyTieDrift.metrics[0].tie_band = [decimal]$tinyTieDrift.metrics[0].tie_band + [decimal]'0.0000000000000001'
    Assert-Throws { Assert-M0Summary -Summary $tinyTieDrift -ProtocolRoot $summaryProtocolRoot } 'tie band'

    $wrongSmallSampleError = Copy-M0SummaryTestObject $valid
    $wrongSmallSampleError.metrics[0].error_code = $null
    Assert-Throws { Assert-M0Summary -Summary $wrongSmallSampleError -ProtocolRoot $summaryProtocolRoot } 'sample counts'

    $asymmetricBudget = Copy-M0SummaryTestObject $valid
    $asymmetricBudget.metrics[0].candidate_b.budget = [decimal]$asymmetricBudget.metrics[0].candidate_b.budget + 1
    Assert-Throws { Assert-M0Summary -Summary $asymmetricBudget -ProtocolRoot $summaryProtocolRoot } 'budget operator or threshold'

    $decisiveInput = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $decisiveInput.candidates) {
        $candidate.attempt_statuses = @(1..22 | ForEach-Object { 'valid' })
        $candidate.metrics[0].values = @(1..22 | ForEach-Object { [pscustomobject][ordered]@{ attempt = $_; value_integer = 100 } })
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $decisiveInput
    $wrongDecisiveError = Copy-M0SummaryTestObject (New-M0Summary -SummaryInput $decisiveInput -ProtocolRoot $summaryProtocolRoot).document
    $wrongDecisiveError.metrics[0].error_code = 'insufficient_decisive_attempts'
    $wrongDecisiveError.metrics[0].classification = 'inconclusive'
    Assert-Throws { Assert-M0Summary -Summary $wrongDecisiveError -ProtocolRoot $summaryProtocolRoot } 'sample counts'

    $badRaws = Copy-M0SummaryTestObject $valid
    $badRaws.evidence_inventory.raws[1] = Copy-M0SummaryTestObject $badRaws.evidence_inventory.raws[0]
    Assert-Throws { Assert-M0Summary -Summary $badRaws -ProtocolRoot $summaryProtocolRoot } 'raw inventory'

    foreach ($field in @('raw_sha256', 'raw_final_line_sha256', 'run_record_sha256', 'snapshot_sha256')) {
        $tamperedReceipt = Copy-M0SummaryTestObject $valid
        $tamperedReceipt.evidence_inventory.raws[0].$field = ('0' * 64)
        Assert-Throws { Assert-M0Summary -Summary $tamperedReceipt -ProtocolRoot $summaryProtocolRoot } 'manifest'
    }
    $tamperedStatus = Copy-M0SummaryTestObject $valid
    $tamperedStatus.evidence_inventory.raws[0].status = 'inconclusive'
    Assert-Throws { Assert-M0Summary -Summary $tamperedStatus -ProtocolRoot $summaryProtocolRoot } 'counts|manifest'
    $tamperedTrace = Copy-M0SummaryTestObject $valid
    $tamperedTrace.evidence_inventory.traces[0].sha256 = ('0' * 64)
    Assert-Throws { Assert-M0Summary -Summary $tamperedTrace -ProtocolRoot $summaryProtocolRoot } 'inventory manifest'

    $multiInput = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $multiInput.candidates) {
        $secondMetric = Copy-M0SummaryTestObject $candidate.metrics[0]
        $secondMetric.metric_id = 'z_second_metric'
        $candidate.metrics = @($candidate.metrics) + @($secondMetric)
    }
    $multi = (New-M0Summary -SummaryInput $multiInput -ProtocolRoot $summaryProtocolRoot).document
    $duplicateMetric = Copy-M0SummaryTestObject $multi
    $duplicateMetric.metrics[1].metric_id = $duplicateMetric.metrics[0].metric_id
    Assert-Throws { Assert-M0Summary -Summary $duplicateMetric -ProtocolRoot $summaryProtocolRoot } 'metrics'
    $reorderedMetrics = Copy-M0SummaryTestObject $multi
    $reorderedMetrics.metrics = @($reorderedMetrics.metrics[1], $reorderedMetrics.metrics[0])
    Assert-Throws { Assert-M0Summary -Summary $reorderedMetrics -ProtocolRoot $summaryProtocolRoot } 'metrics'
}

Test-Case 'Common summary excludes synthetic P and rejects linked or post-open aliased trusted files' {
    $pilotInput = Copy-M0SummaryTestObject $summaryFixture
    $pilotInput.candidates[0].candidate_code = 'P'
    Assert-Throws { New-M0Summary -SummaryInput $pilotInput -ProtocolRoot $summaryProtocolRoot } 'schema|candidates'

    $pilotOutput = (New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot).document
    $pilotOutput.candidates[0].candidate_code = 'P'
    Assert-Throws { Assert-M0Summary -Summary $pilotOutput -ProtocolRoot $summaryProtocolRoot } 'schema|candidates'

    if ($IsWindows) {
        $trustRoot = Join-Path $TestTempRoot 'summary-trust'
        $null = New-Item -ItemType Directory -Path $trustRoot
        $utf8 = [Text.UTF8Encoding]::new($false)

        $hardlinkSource = Join-Path $trustRoot 'hardlink-source.json'
        $hardlinkAlias = Join-Path $trustRoot 'hardlink-alias.json'
        [IO.File]::WriteAllText($hardlinkSource, "{}`n", $utf8)
        $null = New-Item -ItemType HardLink -Path $hardlinkAlias -Target $hardlinkSource
        Assert-Throws { & $summaryModule { param($path) Read-M0SummaryFile -LiteralPath $path -Name 'hardlink vector' } $hardlinkSource } 'links are forbidden|single-link'

        $targetRoot = Join-Path $trustRoot 'junction-target'
        $junctionRoot = Join-Path $trustRoot 'junction-link'
        $null = New-Item -ItemType Directory -Path $targetRoot
        [IO.File]::WriteAllText((Join-Path $targetRoot 'document.json'), "{}`n", $utf8)
        $null = New-Item -ItemType Junction -Path $junctionRoot -Target $targetRoot
        try {
            Assert-Throws { & $summaryModule { param($path) Read-M0SummaryFile -LiteralPath $path -Name 'junction vector' } (Join-Path $junctionRoot 'document.json') } 'ancestors|links are forbidden'
        }
        finally {
            Remove-Item -LiteralPath $junctionRoot -Force
        }

        $swapSource = Join-Path $trustRoot 'post-open-source.json'
        $swapAlias = Join-Path $trustRoot 'post-open-alias.json'
        [IO.File]::WriteAllText($swapSource, "{}`n", $utf8)
        & $summaryModule {
            param($linkPath)
            $script:M0SummaryReadTestHook = [pscustomobject]@{ stage = 'after-open-hardlink'; link_path = $linkPath }
        } $swapAlias
        Assert-Throws { & $summaryModule { param($path) Read-M0SummaryFile -LiteralPath $path -Name 'post-open vector' } $swapSource } 'links are forbidden|single-link'
    }
}

Test-Case 'Trusted common inputs load the summary module before releasing retained trust locks' {
    if (-not $IsWindows) { return }
    $harnessModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
    $summaryBackupPath = Join-Path $TestTempRoot 'M0.Summary.swap.psm1'
    $escapedHarness = $harnessModulePath.Replace("'", "''")
    $escapedRepository = $RepositoryRoot.Replace("'", "''")
    $escapedSummary = $summaryModulePath.Replace("'", "''")
    $escapedBackup = $summaryBackupPath.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
`$harness = Import-Module '$escapedHarness' -PassThru -ErrorAction Stop
& `$harness {
    param(`$target, `$destination)
    `$script:M0HarnessTrustedInputsTestHook = [pscustomobject][ordered]@{
        stage = 'before-summary-import-rename'
        target_path = `$target
        destination_path = `$destination
    }
} '$escapedSummary' '$escapedBackup'
`$null = M0.Harness\Get-M0TrustedCommonInputs -RepositoryRoot '$escapedRepository' -LoadSummary
`$loaded = @(Get-Module -All -Name M0.Summary)
if (`$loaded.Count -ne 1 -or [IO.Path]::GetFullPath(`$loaded[0].Path) -cne [IO.Path]::GetFullPath('$escapedSummary')) {
    throw 'Summary was not loaded exactly once from the trusted path while locks were retained.'
}
if (Test-Path -LiteralPath '$escapedBackup') { throw 'The attempted trust-window rename left a destination file.' }
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-import-under-trust-locks'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Trusted Summary import did not remain inside the lock window: $($result.output)"
    Assert-True ($result.output -match 'OK') "Trusted Summary import lock probe did not finish: $($result.output)"
}

Test-Case 'Clean summary runner rejects every preloaded transitive module and native trust type' {
    foreach ($moduleName in @('M0.Harness', 'M0.Summary', 'M0.Fixture', 'M0.Markers', 'M0.Statistics', 'M0.Visual', 'M0.Process')) {
        $escapedRunner = $summaryRunnerPath.Replace("'", "''")
        $escapedName = $moduleName.Replace("'", "''")
        $scriptText = @"
`$ErrorActionPreference = 'Stop'
`$fake = New-Module -Name '$escapedName' -ScriptBlock { }
Import-Module `$fake
& '$escapedRunner'
"@
        $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name ("summary-preload-" + $moduleName.Replace('.', '-'))
        Assert-True ($result.exit_code -ne 0) "Preloaded $moduleName unexpectedly reached the summary runner."
        Assert-True ($result.output -match 'M0Q040.*clean PowerShell process') "Preloaded $moduleName did not fail at the clean-process gate: $($result.output)"
    }

    foreach ($moduleName in @('M0.Fixture', 'M0.Markers', 'M0.Visual')) {
        $modulePath = Join-Path $RepositoryRoot "experiments\m0\harness\$moduleName.psm1"
        $escapedPath = $modulePath.Replace("'", "''")
        $escapedRunner = $summaryRunnerPath.Replace("'", "''")
        $scriptText = @"
`$ErrorActionPreference = 'Stop'
Import-Module -Name '$escapedPath' -ErrorAction Stop
& '$escapedRunner'
"@
        $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name ("summary-exact-preload-" + $moduleName.Replace('.', '-'))
        Assert-True ($result.exit_code -ne 0) "Exact-path preloaded $moduleName unexpectedly reached the summary runner."
        Assert-True ($result.output -match 'M0Q040.*clean PowerShell process') "Exact-path preload $moduleName did not fail at the clean-process gate: $($result.output)"
    }

    $escapedRunner = $summaryRunnerPath.Replace("'", "''")
    foreach ($nativeType in @('TrustedFile', 'ProcessCapability', 'PilotSession')) {
        $nativeScript = @"
`$ErrorActionPreference = 'Stop'
Add-Type 'namespace M0ProcessNativeV1 { public class $nativeType {} }'
& '$escapedRunner'
"@
        $nativeResult = Invoke-M0SummaryChildProcess -ScriptText $nativeScript -Name ("summary-preload-native-$nativeType")
        Assert-True ($nativeResult.exit_code -ne 0) "Preloaded native type $nativeType unexpectedly reached the summary runner."
        Assert-True ($nativeResult.output -match 'M0Q040.*native type') "Native preload $nativeType did not fail at the clean-process gate: $($nativeResult.output)"
    }
}

Test-Case 'Physical summary trust rejects forged native types even with a caller-forged AppDomain assembly marker' {
    if (-not $IsWindows) { return }
    $processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
    $escapedProcess = $processModulePath.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
Add-Type @'
using Microsoft.Win32.SafeHandles;
namespace M0ProcessNativeV1 {
    public sealed class PilotSession { }
    public sealed class ProcessCapability { }
    public static class TrustedFile { public static void Validate(SafeFileHandle handle, string path) { } }
}
'@
`$fakeAssembly = [M0ProcessNativeV1.TrustedFile].Assembly
[AppDomain]::CurrentDomain.SetData('music-player.m0.process-native-v1.assembly', `$fakeAssembly)
try {
    Import-Module '$escapedProcess' -ErrorAction Stop
    throw 'Forged native types unexpectedly loaded the exact M0.Process path.'
}
catch {
    if (`$_.Exception.Message -notmatch 'M0P001.*refuses all preloaded native process types') { throw }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-forged-process-native'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Forged native assembly marker bypassed M0.Process: $($result.output)"
    Assert-True ($result.output -match 'OK') "Forged native assembly rejection probe did not finish: $($result.output)"
}

Test-Case 'Removing the exact process module makes same-process native reload fail closed' {
    if (-not $IsWindows) { return }
    $processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
    $escapedProcess = $processModulePath.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
`$module = Import-Module '$escapedProcess' -PassThru -ErrorAction Stop
`$null = & `$module { Assert-M0ProcessNativeContract }
Remove-Module -ModuleInfo `$module -Force
try {
    Import-Module '$escapedProcess' -ErrorAction Stop
    throw 'Same-process native reload unexpectedly succeeded.'
}
catch {
    if (`$_.Exception.Message -notmatch 'M0P001.*refuses all preloaded native process types') { throw }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-process-reload-closed'
    Assert-Equal -Actual $result.exit_code -Expected 0 "M0.Process same-process reload was not fail-closed: $($result.output)"
    Assert-True ($result.output -match 'OK') "M0.Process reload rejection probe did not finish: $($result.output)"
}

Test-Case 'Importing summary reuses the exact process module without invalidating an active Job pilot' {
    if (-not $IsWindows) { return }
    $processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
    $escapedProcess = $processModulePath.Replace("'", "''")
    $escapedSummary = $summaryModulePath.Replace("'", "''")
    $escapedRoot = $RepositoryRoot.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
Import-Module '$escapedProcess' -ErrorAction Stop
`$before = Get-Module -Name M0.Process
`$beforeInstance = & `$before { `$script:M0ProcessModuleInstanceId }
`$pilot = `$null
try {
    `$pilot = Open-M0SyntheticProcessPilot -RepositoryRoot '$escapedRoot'
    Import-Module '$escapedSummary' -ErrorAction Stop
    `$after = Get-Module -Name M0.Process
    `$afterInstance = & `$after { `$script:M0ProcessModuleInstanceId }
    if (`$beforeInstance -cne `$afterInstance) { throw 'M0.Process module instance changed during summary import.' }
    `$snapshot = Get-M0SyntheticProcessPilotSnapshot -Pilot `$pilot
    if (`$snapshot.nodes.Count -ne 3) { throw 'Active pilot became unavailable during summary import.' }
}
finally {
    if (`$null -ne `$pilot) {
        `$cleanup = Close-M0SyntheticProcessPilot -Pilot `$pilot
        if (-not `$cleanup.cleanup_complete -or `$cleanup.survivor_count -ne 0) { throw 'Active pilot cleanup failed after summary import.' }
    }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-active-pilot-import'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Summary import invalidated an active process pilot: $($result.output)"
    Assert-True ($result.output -match 'OK') "Active pilot import probe did not finish: $($result.output)"
}

Test-Case 'Pure summary derivation can precede exact process import and a live Job pilot' {
    if (-not $IsWindows) { return }
    $processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
    $escapedProcess = $processModulePath.Replace("'", "''")
    $escapedSummary = $summaryModulePath.Replace("'", "''")
    $escapedInput = $summaryInputPath.Replace("'", "''")
    $escapedProtocol = $summaryProtocolRoot.Replace("'", "''")
    $escapedRoot = $RepositoryRoot.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
Import-Module '$escapedSummary' -ErrorAction Stop
if (Get-Module -Name M0.Process) { throw 'Pure summary import loaded M0.Process.' }
`$input = Get-Content -LiteralPath '$escapedInput' -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$summary = New-M0Summary -SummaryInput `$input -ProtocolRoot '$escapedProtocol'
`$null = Assert-M0Summary -Summary `$summary.document -ProtocolRoot '$escapedProtocol'
if (Get-Module -Name M0.Process) { throw 'Pure summary derivation loaded M0.Process.' }
Import-Module '$escapedProcess' -ErrorAction Stop
`$pilot = `$null
try {
    `$pilot = Open-M0SyntheticProcessPilot -RepositoryRoot '$escapedRoot'
}
finally {
    if (`$null -ne `$pilot) {
        `$cleanup = Close-M0SyntheticProcessPilot -Pilot `$pilot
        if (-not `$cleanup.cleanup_complete -or `$cleanup.survivor_count -ne 0) { throw 'Pilot cleanup failed after pure summary derivation.' }
    }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-before-process-live'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Pure summary then Process live probe failed: $($result.output)"
    Assert-True ($result.output -match 'OK') "Pure summary then Process live probe did not finish: $($result.output)"
}

Test-Case 'Summary and Evidence reuse one exact Statistics module instance in either import order' {
    $evidenceModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
    $escapedSummary = $summaryModulePath.Replace("'", "''")
    $escapedEvidence = $evidenceModulePath.Replace("'", "''")
    $escapedInput = $summaryInputPath.Replace("'", "''")
    $escapedProtocol = $summaryProtocolRoot.Replace("'", "''")
    foreach ($order in @('summary-first', 'evidence-first')) {
        $imports = if ($order -ceq 'summary-first') {
            "Import-Module '$escapedSummary' -ErrorAction Stop`n`$before = Get-Module -All -Name M0.Statistics`nImport-Module '$escapedEvidence' -ErrorAction Stop"
        }
        else {
            "Import-Module '$escapedEvidence' -ErrorAction Stop`n`$before = Get-Module -All -Name M0.Statistics`nImport-Module '$escapedSummary' -ErrorAction Stop"
        }
        $scriptText = @"
`$ErrorActionPreference = 'Stop'
$imports
`$after = Get-Module -All -Name M0.Statistics
if (`$before.Count -ne 1 -or `$after.Count -ne 1 -or -not [object]::ReferenceEquals(`$before[0], `$after[0])) {
    throw 'M0.Statistics module instance changed across Summary/Evidence import order.'
}
`$beforeContract = & `$before[0] { Assert-M0StatisticsRuntimeContract }
`$afterContract = & `$after[0] { Assert-M0StatisticsRuntimeContract }
if (`$beforeContract.module_instance_id -cne `$afterContract.module_instance_id) { throw 'Statistics contract instance changed.' }
`$input = Get-Content -LiteralPath '$escapedInput' -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$result = New-M0Summary -SummaryInput `$input -ProtocolRoot '$escapedProtocol'
if (-not (Assert-M0Summary -Summary `$result.document -ProtocolRoot '$escapedProtocol')) { throw 'Summary API failed after Evidence import.' }
'OK'
"@
        $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name "summary-evidence-order-$order"
        Assert-Equal -Actual $result.exit_code -Expected 0 "Summary/Evidence order '$order' replaced Statistics: $($result.output)"
        Assert-True ($result.output -match 'OK') "Summary/Evidence order '$order' did not finish: $($result.output)"
    }
}

Test-Case 'Independent fixture copies in distinct roots produce byte-identical common summaries' {
    $firstRoot = Join-Path $TestTempRoot 'summary-build-a'
    $secondRoot = Join-Path $TestTempRoot 'summary-build-b'
    $null = New-Item -ItemType Directory -Path $firstRoot, $secondRoot
    $firstInputPath = Join-Path $firstRoot 'summary-input.v1.json'
    $secondInputPath = Join-Path $secondRoot 'summary-input.v1.json'
    Copy-Item -LiteralPath $summaryInputPath -Destination $firstInputPath
    Copy-Item -LiteralPath $summaryInputPath -Destination $secondInputPath
    $firstOutputPath = Join-Path $firstRoot 'summary.v1.json'
    $secondOutputPath = Join-Path $secondRoot 'summary.v1.json'
    $escapedModule = $summaryModulePath.Replace("'", "''")
    $escapedProtocol = $summaryProtocolRoot.Replace("'", "''")
    $firstScript = @"
`$ErrorActionPreference = 'Stop'
[Globalization.CultureInfo]::CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('tr-TR')
Import-Module '$escapedModule' -ErrorAction Stop
`$input = Get-Content -LiteralPath '$($firstInputPath.Replace("'", "''"))' -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$result = New-M0Summary -SummaryInput `$input -ProtocolRoot '$escapedProtocol'
[IO.File]::WriteAllBytes('$($firstOutputPath.Replace("'", "''"))', `$result.bytes)
`$result.sha256
"@
    $secondScript = @"
`$ErrorActionPreference = 'Stop'
[Globalization.CultureInfo]::CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('fr-FR')
Import-Module '$escapedModule' -ErrorAction Stop
`$input = Get-Content -LiteralPath '$($secondInputPath.Replace("'", "''"))' -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$result = New-M0Summary -SummaryInput `$input -ProtocolRoot '$escapedProtocol'
[IO.File]::WriteAllBytes('$($secondOutputPath.Replace("'", "''"))', `$result.bytes)
`$result.sha256
"@
    $firstChild = Invoke-M0SummaryChildProcess -ScriptText $firstScript -Name 'summary-independent-a'
    $secondChild = Invoke-M0SummaryChildProcess -ScriptText $secondScript -Name 'summary-independent-b'
    Assert-Equal -Actual $firstChild.exit_code -Expected 0 "First independent summary process failed: $($firstChild.output)"
    Assert-Equal -Actual $secondChild.exit_code -Expected 0 "Second independent summary process failed: $($secondChild.output)"
    $firstBytes = [IO.File]::ReadAllBytes($firstOutputPath)
    $secondBytes = [IO.File]::ReadAllBytes($secondOutputPath)
    Assert-True ([Linq.Enumerable]::SequenceEqual[byte]($firstBytes, $secondBytes)) 'Independent roots/processes must produce the same canonical summary bytes.'
    Assert-Equal -Actual (Get-FileHash -LiteralPath $secondOutputPath -Algorithm SHA256).Hash `
        -Expected (Get-FileHash -LiteralPath $firstOutputPath -Algorithm SHA256).Hash
}

Test-Case 'Summary rejects incomplete inventories duplicate metrics invalid valid-count projection and incoherent soak' {
    $missingRaw = Copy-M0SummaryTestObject $summaryFixture
    $missingRaw.raws = @($missingRaw.raws | Select-Object -Skip 1)
    Assert-Throws { New-M0Summary -SummaryInput $missingRaw -ProtocolRoot $summaryProtocolRoot } 'schema|raw inventory'

    $duplicateRaw = Copy-M0SummaryTestObject $summaryFixture
    $duplicateRaw.raws[1] = Copy-M0SummaryTestObject $duplicateRaw.raws[0]
    Assert-Throws { New-M0Summary -SummaryInput $duplicateRaw -ProtocolRoot $summaryProtocolRoot } 'raw inventory'

    $mismatchedCommon = Copy-M0SummaryTestObject $summaryFixture
    $mismatchedCommon.candidates[1].common_inputs.protocol_sha256 = ('b' * 64)
    Assert-Throws { New-M0Summary -SummaryInput $mismatchedCommon -ProtocolRoot $summaryProtocolRoot } 'common inputs differ'

    $mutatedRawHash = Copy-M0SummaryTestObject $summaryFixture
    $mutatedRawHash.raws[0].raw_sha256 = $mutatedRawHash.raws[1].raw_sha256
    Assert-Throws { New-M0Summary -SummaryInput $mutatedRawHash -ProtocolRoot $summaryProtocolRoot } 'raw inventory'

    $swappedRunHash = Copy-M0SummaryTestObject $summaryFixture
    $swappedRunHash.raws[0].run_record_sha256 = ('0' * 64)
    Assert-Throws { New-M0Summary -SummaryInput $swappedRunHash -ProtocolRoot $summaryProtocolRoot } 'runs manifest'

    $mutatedFinalLine = Copy-M0SummaryTestObject $summaryFixture
    $mutatedFinalLine.raws[0].raw_final_line_sha256 = ('0' * 64)
    Assert-Throws { New-M0Summary -SummaryInput $mutatedFinalLine -ProtocolRoot $summaryProtocolRoot } 'runs manifest'

    $mismatchedStatus = Copy-M0SummaryTestObject $summaryFixture
    $mismatchedStatus.raws[0].status = 'inconclusive'
    Assert-Throws { New-M0Summary -SummaryInput $mismatchedStatus -ProtocolRoot $summaryProtocolRoot } 'status|runs manifest'

    $foreignTrace = Copy-M0SummaryTestObject $summaryFixture
    $foreignTrace.traces[0].run_id = 'run-B-golden-01'
    Assert-Throws { New-M0Summary -SummaryInput $foreignTrace -ProtocolRoot $summaryProtocolRoot } 'trace inventory'

    $countMismatch = Copy-M0SummaryTestObject $summaryFixture
    $countMismatch.candidates[0].metrics[0].values = @(
        [pscustomobject][ordered]@{ attempt = 1; value_integer = 100 },
        [pscustomobject][ordered]@{ attempt = 2; value_integer = 101 }
    )
    Assert-Throws { New-M0Summary -SummaryInput $countMismatch -ProtocolRoot $summaryProtocolRoot } 'valid[- ]attempt'

    $badSoak = Copy-M0SummaryTestObject $summaryFixture
    $badSoak.candidates[0].soak = [pscustomobject][ordered]@{ applicable = $true; result = 'inconclusive'; duration_seconds = 60; error_code = $null }
    Assert-Throws { New-M0Summary -SummaryInput $badSoak -ProtocolRoot $summaryProtocolRoot } 'schema|soak'

    $overflowSoak = Copy-M0SummaryTestObject $summaryFixture
    $overflowSoak.candidates[0].soak = [pscustomobject][ordered]@{ applicable = $true; result = 'pass'; duration_seconds = [decimal]2147483648; error_code = $null }
    Assert-Throws { New-M0Summary -SummaryInput $overflowSoak -ProtocolRoot $summaryProtocolRoot } 'schema'

    $overflowBudget = Copy-M0SummaryTestObject $summaryFixture
    $overflowBudget.candidates[0].metrics[0].budget.threshold_integer = [decimal]9223372036854775808
    Assert-Throws { New-M0Summary -SummaryInput $overflowBudget -ProtocolRoot $summaryProtocolRoot } 'schema'

    $unsafeCpuDomain = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $unsafeCpuDomain.candidates) {
        $candidate.attempt_statuses = @('valid', 'valid') + @(1..20 | ForEach-Object { 'inconclusive' })
        $candidate.metrics[0].metric_family = 'cpu_percentage_points'
        $candidate.metrics[0].unit = 'basis_points'
        $candidate.metrics[0].budget.threshold_integer = 1000000
        $candidate.metrics[0].values = @(
            [pscustomobject][ordered]@{ attempt = 1; value_integer = 0 },
            [pscustomobject][ordered]@{ attempt = 2; value_integer = [long]::MaxValue }
        )
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $unsafeCpuDomain
    Assert-Throws { New-M0Summary -SummaryInput $unsafeCpuDomain -ProtocolRoot $summaryProtocolRoot } 'schema|basis_points domain'
}

Test-Case 'Runs manifest receipts reproduce the exact canonical M0.Evidence manifest contract' {
    $evidenceModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Evidence.psm1'
    $escapedEvidence = $evidenceModulePath.Replace("'", "''")
    $escapedFixture = $summaryInputPath.Replace("'", "''")
    foreach ($candidateCode in @('A', 'B')) {
        $scriptText = @"
`$ErrorActionPreference = 'Stop'
Import-Module '$escapedEvidence' -ErrorAction Stop
`$evidence = Get-Module -Name M0.Evidence
`$fixture = Get-Content -LiteralPath '$escapedFixture' -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100 -DateKind String
`$candidate = @(`$fixture.candidates | Where-Object candidate_code -CEQ '$candidateCode')[0]
`$records = @(`$fixture.raws | Where-Object candidate_code -CEQ '$candidateCode' | Sort-Object attempt | ForEach-Object {
    [pscustomobject][ordered]@{
        run_record_sha256 = `$_.run_record_sha256
        run = [pscustomobject][ordered]@{
            candidate_code = '$candidateCode'
            attempt = [int]`$_.attempt
            run_id = [string]`$_.run_id
            status = [string]`$_.status
            raw = [pscustomobject][ordered]@{ final_line_sha256 = [string]`$_.raw_final_line_sha256 }
            candidate_input = [pscustomobject][ordered]@{
                git_commit = ('1' * 40)
                dirty = `$false
                build_mode = 'Release'
                artifact_sha256 = [string]`$candidate.artifact_sha256
                toolchains = @()
                lockfiles = @()
            }
        }
    }
})
`$digest = & `$evidence { param(`$records, `$code) Get-M0EvidenceCandidateDigestsFromRecords -Records `$records -CandidateCode `$code } `$records '$candidateCode'
`$digest.runs_manifest_sha256
"@
        $child = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name "summary-evidence-manifest-$candidateCode"
        Assert-Equal -Actual $child.exit_code -Expected 0 "Evidence manifest probe failed for candidate $candidateCode`: $($child.output)"
        $expectedHash = $child.output.Trim()
        $candidate = @($summaryFixture.candidates | Where-Object candidate_code -CEQ $candidateCode)[0]
        Assert-Equal -Actual $candidate.runs_manifest_sha256 -Expected $expectedHash "Fixture candidate $candidateCode does not use the canonical M0.Evidence runs manifest."
    }
    $summary = New-M0Summary -SummaryInput $summaryFixture -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $summary.document.candidates[0].runs_manifest_sha256 -Expected $summaryFixture.candidates[0].runs_manifest_sha256
    Assert-Equal -Actual $summary.document.candidates[1].runs_manifest_sha256 -Expected $summaryFixture.candidates[1].runs_manifest_sha256
}

Test-Case 'Soak failure is a separate gate and never falsifies budget violation' {
    $input = Copy-M0SummaryTestObject $summaryFixture
    $input.candidates[0].soak = [pscustomobject][ordered]@{ applicable = $true; result = 'fail'; duration_seconds = 60; error_code = 'soak_failed' }
    $result = New-M0Summary -SummaryInput $input -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $result.document.candidates[0].budget_violated -Expected $false
    Assert-Equal -Actual $result.document.candidates[0].decision -Expected 'fail'
    Assert-Equal -Actual $result.document.decision -Expected 'fail'
}

Test-Case 'Soak inconclusive with a closed error keeps an otherwise passing candidate inconclusive' {
    $input = Copy-M0SummaryTestObject $summaryFixture
    foreach ($candidate in $input.candidates) {
        $candidate.attempt_statuses = @(1..22 | ForEach-Object { 'valid' })
        $candidate.metrics[0].values = @(1..22 | ForEach-Object { [pscustomobject][ordered]@{ attempt = $_; value_integer = 100 } })
    }
    $null = Sync-M0SummaryTestReceipts -SummaryInput $input
    $input.candidates[0].soak = [pscustomobject][ordered]@{
        applicable = $true; result = 'inconclusive'; duration_seconds = 60; error_code = 'soak_incomplete'
    }
    $result = New-M0Summary -SummaryInput $input -ProtocolRoot $summaryProtocolRoot
    Assert-Equal -Actual $result.document.candidates[0].budget_violated -Expected $false
    Assert-Equal -Actual $result.document.candidates[0].decision -Expected 'inconclusive'
    Assert-Equal -Actual $result.document.decision -Expected 'inconclusive'
}

Test-Case 'The common summary descriptor and committed golden remain required after behavioral derivation' {
    foreach ($path in @($summaryDescriptorPath, $summaryGoldenPath)) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Required common summary anchor is missing: '$path'."
    }
}

Test-Case 'Physical artifact assertion reuses an already loaded exact process module instance' {
    if (-not $IsWindows) { return }
    $processModule = Get-Module -All -Name M0.Process
    if ($null -eq $processModule) {
        Import-Module -Name (Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1') -ErrorAction Stop
        $processModule = Get-Module -All -Name M0.Process
    }
    $beforeInstance = & $processModule { $script:M0ProcessModuleInstanceId }
    $null = Assert-M0SummaryArtifacts -ProtocolRoot $summaryProtocolRoot -ExpectedProtocolSha256 $summaryProtocolSha256
    $after = Get-Module -All -Name M0.Process
    $afterInstance = & $after { $script:M0ProcessModuleInstanceId }
    Assert-Equal -Actual $afterInstance -Expected $beforeInstance 'Physical summary assertion reloaded the exact M0.Process module.'
}

Test-Case 'Physical artifact assertion holds and revalidates every trusted file through semantic execution' {
    if (-not $IsWindows) { return }
    $protocolSchemaPath = Join-Path $summaryProtocolRoot 'schemas\protocol.schema.json'
    foreach ($targetPath in @($summaryProtocolPath, $protocolSchemaPath)) {
        foreach ($stage in @('after-all-open-overwrite', 'after-all-open-rename')) {
            $targetTag = [IO.Path]::GetFileName($targetPath).Replace('.', '-')
            $destinationPath = Join-Path $TestTempRoot "summary-held-$targetTag-$stage.json"
            & $summaryModule {
                param($stageName, $target, $destination)
                $script:M0SummaryArtifactsTestHook = [pscustomobject]@{
                    stage = $stageName
                    target_path = $target
                    destination_path = $destination
                }
            } $stage $targetPath $destinationPath
            $null = Assert-M0SummaryArtifacts -ProtocolRoot $summaryProtocolRoot -ExpectedProtocolSha256 $summaryProtocolSha256
            Assert-True (-not (Test-Path -LiteralPath $destinationPath)) "Retained file stage '$stage' left a renamed artifact."
        }
    }

    $hardlinkPath = Join-Path $TestTempRoot 'summary-held-hardlink.json'
    try {
        & $summaryModule {
            param($target, $link)
            $script:M0SummaryArtifactsTestHook = [pscustomobject]@{
                stage = 'after-all-open-hardlink'
                target_path = $target
                link_path = $link
            }
        } $summaryProtocolPath $hardlinkPath
        Assert-Throws { Assert-M0SummaryArtifacts -ProtocolRoot $summaryProtocolRoot -ExpectedProtocolSha256 $summaryProtocolSha256 } 'single-link|links are forbidden'
    }
    finally {
        if (Test-Path -LiteralPath $hardlinkPath -PathType Leaf) { Remove-Item -LiteralPath $hardlinkPath -Force }
    }
    $null = Assert-M0SummaryArtifacts -ProtocolRoot $summaryProtocolRoot -ExpectedProtocolSha256 $summaryProtocolSha256
}

Test-Case 'Physical summary assertion preserves an active Job pilot and its exact process module instance' {
    if (-not $IsWindows) { return }
    $processModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Process.psm1'
    $escapedProcess = $processModulePath.Replace("'", "''")
    $escapedSummary = $summaryModulePath.Replace("'", "''")
    $escapedProtocol = $summaryProtocolRoot.Replace("'", "''")
    $escapedRoot = $RepositoryRoot.Replace("'", "''")
    $scriptText = @"
`$ErrorActionPreference = 'Stop'
Import-Module '$escapedProcess' -ErrorAction Stop
`$process = Get-Module -Name M0.Process
`$beforeInstance = & `$process { `$script:M0ProcessModuleInstanceId }
Import-Module '$escapedSummary' -ErrorAction Stop
`$pilot = `$null
try {
    `$pilot = Open-M0SyntheticProcessPilot -RepositoryRoot '$escapedRoot'
    `$before = Get-M0SyntheticProcessPilotSnapshot -Pilot `$pilot
    if (`$before.nodes.Count -ne 3) { throw 'Active pilot topology was incomplete before physical summary assertion.' }
    `$protocolSha256 = (Get-FileHash -LiteralPath (Join-Path '$escapedProtocol' 'protocol.v1.json') -Algorithm SHA256).Hash
    `$null = Assert-M0SummaryArtifacts -ProtocolRoot '$escapedProtocol' -ExpectedProtocolSha256 `$protocolSha256
    `$afterProcess = Get-Module -Name M0.Process
    `$afterInstance = & `$afterProcess { `$script:M0ProcessModuleInstanceId }
    if (`$afterInstance -cne `$beforeInstance) { throw 'Physical summary assertion changed the process module instance.' }
    `$after = Get-M0SyntheticProcessPilotSnapshot -Pilot `$pilot
    if (`$after.nodes.Count -ne 3) { throw 'Physical summary assertion invalidated the active pilot.' }
}
finally {
    if (`$null -ne `$pilot) {
        `$cleanup = Close-M0SyntheticProcessPilot -Pilot `$pilot
        if (-not `$cleanup.cleanup_complete -or `$cleanup.survivor_count -ne 0) { throw 'Active pilot cleanup failed after physical summary assertion.' }
    }
}
'OK'
"@
    $result = Invoke-M0SummaryChildProcess -ScriptText $scriptText -Name 'summary-active-pilot-physical'
    Assert-Equal -Actual $result.exit_code -Expected 0 "Physical summary assertion invalidated an active pilot: $($result.output)"
    Assert-True ($result.output -match 'OK') "Active physical summary probe did not finish: $($result.output)"
}
