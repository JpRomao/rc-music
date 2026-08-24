$modulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
Import-Module $modulePath -Force -ErrorAction Stop

Test-Case 'M0 layout is isolated from the product root' {
    Assert-M0Layout -RepositoryRoot $RepositoryRoot
}

Test-Case 'The harness module manifest exposes only the approved contract API' {
    $manifestPath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
    $manifest = Test-ModuleManifest -Path $manifestPath -ErrorAction Stop
    $expected = @(
        'Assert-M0EnvironmentContract',
        'Assert-M0FileHash',
        'Assert-M0JsonDocument',
        'Assert-M0Layout',
        'Assert-M0NoDynamicExecution',
        'Assert-M0ProtocolContract',
        'Get-M0CandidateRoot',
        'Get-M0Sha256',
        'Resolve-M0ScopedPath'
    )
    Assert-SequenceEqual -Actual @($manifest.ExportedFunctions.Keys | Sort-Object) -Expected $expected
}

Test-Case 'Scoped paths accept a child path' {
    $basePath = Join-Path $RepositoryRoot 'experiments\m0'
    $resolved = Resolve-M0ScopedPath -BasePath $basePath -CandidatePath 'protocol\protocol.v1.json'
    Assert-True ($resolved.StartsWith($basePath, [StringComparison]::OrdinalIgnoreCase))
}

Test-Case 'Scoped paths reject traversal outside the base' {
    $basePath = Join-Path $RepositoryRoot 'experiments\m0'
    Assert-Throws { Resolve-M0ScopedPath -BasePath $basePath -CandidatePath '..\..\README.md' } 'outside'
}

Test-Case 'Scoped paths reject alternate data streams and reparse-point escapes' {
    $basePath = Join-Path $TestTempRoot 'contained'
    $outsidePath = Join-Path $TestTempRoot 'outside'
    New-Item -ItemType Directory -Path $basePath, $outsidePath -Force | Out-Null
    Assert-Throws { Resolve-M0ScopedPath -BasePath $basePath -CandidatePath 'payload.json:stream' } 'stream'

    $linkPath = Join-Path $basePath 'linked'
    $linkType = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
    New-Item -ItemType $linkType -Path $linkPath -Target $outsidePath | Out-Null
    Assert-Throws { Resolve-M0ScopedPath -BasePath $basePath -CandidatePath 'linked\payload.json' } 'reparse|link'

    $linkedBase = Join-Path $TestTempRoot 'linked-base'
    New-Item -ItemType $linkType -Path $linkedBase -Target $outsidePath | Out-Null
    Assert-Throws { Resolve-M0ScopedPath -BasePath $linkedBase -CandidatePath 'payload.json' } 'reparse|link'
}

Test-Case 'The focused test selector cannot execute a script outside the harness test root' {
    Assert-Throws { Resolve-TestFilePath -InputPath '..\M0.Harness.psm1' } 'outside'
}

Test-Case 'The protocol descriptor satisfies its schema and constants' {
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $schemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\protocol.schema.json'
    $document = Assert-M0JsonDocument -DocumentPath $protocolPath -SchemaPath $schemaPath
    Assert-M0ProtocolContract -Protocol $document -RepositoryRoot $RepositoryRoot
}

Test-Case 'Protocol contract hashes bind the executable descriptor to the frozen documents' {
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $schemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\protocol.schema.json'
    $document = Assert-M0JsonDocument -DocumentPath $protocolPath -SchemaPath $schemaPath
    $isolatedRoot = Join-Path $TestTempRoot 'tampered-contract'
    $docsRoot = Join-Path $isolatedRoot 'docs\m0'
    New-Item -ItemType Directory -Path $docsRoot -Force | Out-Null
    foreach ($name in @('environment.md', 'protocol.md', 'layout.md')) {
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot "docs\m0\$name") -Destination (Join-Path $docsRoot $name)
    }
    [IO.File]::AppendAllText((Join-Path $docsRoot 'protocol.md'), "tamper`n", [Text.UTF8Encoding]::new($false))

    Assert-Throws { Assert-M0ProtocolContract -Protocol $document -RepositoryRoot $isolatedRoot } 'hash'
}

Test-Case 'The protocol binds the common fixture and shell scenario bytes' {
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $protocol = Get-Content -LiteralPath $protocolPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
    $fixtureManifest = Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture\manifest.v1.json'
    $shellScenario = Join-Path $RepositoryRoot 'experiments\m0\protocol\scenarios\shell.v1.json'
    Assert-M0FileHash -LiteralPath $fixtureManifest -ExpectedHash $protocol.fixture_manifest_sha256
    Assert-M0FileHash -LiteralPath $shellScenario -ExpectedHash $protocol.shell_scenario_sha256
}

Test-Case 'Candidate, environment and scenario descriptors satisfy their closed schemas' {
    $contracts = @(
        @('experiments\m0\candidates\qt\candidate.v1.json', 'candidate.schema.json'),
        @('experiments\m0\candidates\tauri\candidate.v1.json', 'candidate.schema.json'),
        @('experiments\m0\protocol\environments\windows-benchmark.v1.json', 'environment.schema.json'),
        @('experiments\m0\protocol\environments\linux-reference.v1.json', 'environment.schema.json'),
        @('experiments\m0\protocol\scenarios\shell.v1.json', 'scenario.schema.json')
    )
    foreach ($contract in $contracts) {
        $documentPath = Join-Path $RepositoryRoot $contract[0]
        $schemaPath = Join-Path $RepositoryRoot "experiments\m0\protocol\schemas\$($contract[1])"
        $null = Assert-M0JsonDocument -DocumentPath $documentPath -SchemaPath $schemaPath
    }
}

Test-Case 'Pattern-constrained descriptor fields reject non-string values' {
    $candidatePath = Join-Path $RepositoryRoot 'experiments\m0\candidates\qt\candidate.v1.json'
    $candidateSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\candidate.schema.json'
    $invalidCandidatePath = Join-Path $TestTempRoot 'invalid-candidate.json'
    $candidate = Get-Content -LiteralPath $candidatePath -Raw | ConvertFrom-Json
    $candidate.root_relative_path = 42
    [IO.File]::WriteAllText($invalidCandidatePath, ($candidate | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $invalidCandidatePath -SchemaPath $candidateSchema } 'schema'

    $environmentPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\environments\windows-benchmark.v1.json'
    $environmentSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\environment.schema.json'
    $invalidEnvironmentPath = Join-Path $TestTempRoot 'invalid-environment.json'
    $environment = Get-Content -LiteralPath $environmentPath -Raw | ConvertFrom-Json
    $environment.environment_id = 42
    [IO.File]::WriteAllText($invalidEnvironmentPath, ($environment | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $invalidEnvironmentPath -SchemaPath $environmentSchema } 'schema'
}

Test-Case 'Environment readiness and compatibility roles are explicit' {
    $environmentPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\environments\windows-benchmark.v1.json'
    $environmentSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\environment.schema.json'
    $windows = Assert-M0JsonDocument -DocumentPath $environmentPath -SchemaPath $environmentSchema
    $compatibility = @($windows.platform_matrix | Where-Object id -eq 'windows-11-24h2-x64')
    Assert-Equal -Actual $compatibility.Count -Expected 1
    Assert-Equal -Actual $compatibility[0].tier -Expected 'compatibility-smoke'

    $invalidPath = Join-Path $TestTempRoot 'invalid-ready-toolchain.json'
    $windows.toolchains[4].ready = $true
    $windows.toolchains[4].installed_version = $null
    [IO.File]::WriteAllText($invalidPath, ($windows | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $invalidPath -SchemaPath $environmentSchema } 'schema'
}

Test-Case 'The environment root manifest binds both platform descriptors' {
    $bundlePath = Join-Path $RepositoryRoot 'experiments\m0\protocol\environments\environment.v1.json'
    $bundleSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\environment-bundle.schema.json'
    $bundle = Assert-M0JsonDocument -DocumentPath $bundlePath -SchemaPath $bundleSchema
    Assert-M0EnvironmentContract -Environment $bundle -RepositoryRoot $RepositoryRoot
}

Test-Case 'A protocol missing its identifier is rejected' {
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $schemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\protocol.schema.json'
    $invalidPath = Join-Path $TestTempRoot 'invalid-protocol.json'
    $document = Get-Content -LiteralPath $protocolPath -Raw | ConvertFrom-Json
    $document.PSObject.Properties.Remove('protocol_id')
    [IO.File]::WriteAllText($invalidPath, ($document | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $invalidPath -SchemaPath $schemaPath } 'schema'
}

Test-Case 'Duplicate JSON keys and unknown descriptor properties are rejected' {
    $protocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'
    $protocolSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\protocol.schema.json'
    $duplicatePath = Join-Path $TestTempRoot 'duplicate-key.json'
    $json = Get-Content -LiteralPath $protocolPath -Raw
    $json = $json.Replace('"protocol_id": "M0-P1",', '"protocol_id": "M0-P1", "protocol_id": "M0-P1",')
    [IO.File]::WriteAllText($duplicatePath, $json, [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $duplicatePath -SchemaPath $protocolSchema } 'duplicate'

    $candidatePath = Join-Path $RepositoryRoot 'experiments\m0\candidates\qt\candidate.v1.json'
    $candidateSchema = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\candidate.schema.json'
    $unknownPath = Join-Path $TestTempRoot 'unknown-property.json'
    $candidate = Get-Content -LiteralPath $candidatePath -Raw | ConvertFrom-Json
    $candidate | Add-Member -NotePropertyName command -NotePropertyValue 'forbidden'
    [IO.File]::WriteAllText($unknownPath, ($candidate | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0JsonDocument -DocumentPath $unknownPath -SchemaPath $candidateSchema } 'schema'
}

Test-Case 'Known file hashes pass and tampering is rejected' {
    $path = Join-Path $RepositoryRoot 'docs\m0\protocol.md'
    $hash = Get-M0Sha256 -LiteralPath $path
    Assert-M0FileHash -LiteralPath $path -ExpectedHash $hash
    Assert-Throws { Assert-M0FileHash -LiteralPath $path -ExpectedHash ('0' * 64) } 'hash'
}

Test-Case 'Candidate roots use a closed allowlist' {
    $qt = Get-M0CandidateRoot -RepositoryRoot $RepositoryRoot -Candidate Qt
    $tauri = Get-M0CandidateRoot -RepositoryRoot $RepositoryRoot -Candidate Tauri
    Assert-True ($qt.EndsWith('experiments\m0\candidates\qt'))
    Assert-True ($tauri.EndsWith('experiments\m0\candidates\tauri'))
    Assert-Throws { Get-M0CandidateRoot -RepositoryRoot $RepositoryRoot -Candidate Unknown } 'candidate'
}

Test-Case 'Harness scripts contain no dynamic expression execution' {
    Assert-M0NoDynamicExecution -RepositoryRoot $RepositoryRoot
}

Test-Case 'The dynamic-execution scanner catches assignment and ScriptBlock creation' {
    $isolatedRoot = Join-Path $TestTempRoot 'dynamic-scan'
    $scriptsRoot = Join-Path $isolatedRoot 'scripts\m0'
    New-Item -ItemType Directory -Path $scriptsRoot -Force | Out-Null
    $probePath = Join-Path $scriptsRoot 'probe.ps1'
    [IO.File]::WriteAllText($probePath, '$value = Invoke-Expression "Get-Date"', [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0NoDynamicExecution -RepositoryRoot $isolatedRoot } 'dynamic'

    [IO.File]::WriteAllText($probePath, '$value = [scriptblock]::Create("Get-Date")', [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0NoDynamicExecution -RepositoryRoot $isolatedRoot } 'dynamic'
}
