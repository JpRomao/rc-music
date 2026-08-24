$harnessManifestSchemaPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\schemas\harness-manifest.schema.json'
$harnessManifestPath = Join-Path $RepositoryRoot 'experiments\m0\harness\manifest.v1.json'
$harnessManifestSidecarPath = Join-Path $RepositoryRoot 'experiments\m0\harness\manifest.v1.sha256'
$harnessManifestGeneratorPath = Join-Path $RepositoryRoot 'scripts\m0\New-M0HarnessManifest.ps1'
$harnessProtocolPath = Join-Path $RepositoryRoot 'experiments\m0\protocol\protocol.v1.json'

function Copy-M0HarnessTrustTree {
    param([Parameter(Mandatory)] [string] $DestinationRoot)

    $experimentsRoot = Join-Path $DestinationRoot 'experiments\m0'
    $scriptsRoot = Join-Path $DestinationRoot 'scripts\m0'
    $docsRoot = Join-Path $DestinationRoot 'docs'
    New-Item -ItemType Directory -Path $experimentsRoot, (Split-Path $scriptsRoot -Parent), $docsRoot -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'docs\m0') -Destination $docsRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\harness') -Destination $experimentsRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\protocol') -Destination $experimentsRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\shared') -Destination $experimentsRoot -Recurse
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'scripts\m0') -Destination (Split-Path $scriptsRoot -Parent) -Recurse
    return $DestinationRoot
}

function Write-M0HarnessTestManifest {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] $Manifest
    )

    $manifestPath = Join-Path $Root 'experiments\m0\harness\manifest.v1.json'
    $sidecarPath = Join-Path $Root 'experiments\m0\harness\manifest.v1.sha256'
    $json = ($Manifest | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($manifestPath, $json, [Text.UTF8Encoding]::new($false))
    $hash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($sidecarPath, "$($hash.ToLowerInvariant())  manifest.v1.json`n", [Text.UTF8Encoding]::new($false))
    return $hash
}

function Get-M0CommittedHarnessHash {
    $protocol = Get-Content -LiteralPath $harnessProtocolPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
    return [string]$protocol.harness_manifest_sha256
}

function Invoke-M0HarnessChildProcess {
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
        return [pscustomobject]@{
            exit_code = $child.ExitCode
            output = $output
        }
    }
    finally {
        $child.Dispose()
    }
}

Test-Case 'The canonical harness trust contract exists before collection can use it' {
    foreach ($path in @(
        $harnessManifestSchemaPath,
        $harnessManifestPath,
        $harnessManifestSidecarPath,
        $harnessManifestGeneratorPath
    )) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "M0H001 required harness trust file is missing: '$path'."
    }
}

Test-Case 'The committed harness manifest is canonical, anchored and complete' {
    $expectedHash = Get-M0CommittedHarnessHash
    $result = Assert-M0HarnessManifest -RepositoryRoot $RepositoryRoot -ExpectedManifestSha256 $expectedHash
    Assert-Equal -Actual $result.runtime_file_count -Expected 10
    Assert-Equal -Actual $result.schema_file_count -Expected 20
    Assert-Equal -Actual $result.manifest_sha256 -Expected $expectedHash.ToLowerInvariant()
}

Test-Case 'Trusted common inputs are derived from the five real repository artifacts' {
    $trusted = Get-M0TrustedCommonInputs -RepositoryRoot $RepositoryRoot
    $protocol = Get-Content -LiteralPath $harnessProtocolPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
    $expected = [ordered]@{
        protocol_sha256 = (Get-FileHash -LiteralPath $harnessProtocolPath -Algorithm SHA256).Hash.ToLowerInvariant()
        environment_sha256 = $protocol.environment_bundle_sha256.ToLowerInvariant()
        fixture_sha256 = $protocol.fixture_manifest_sha256.ToLowerInvariant()
        harness_sha256 = $protocol.harness_manifest_sha256.ToLowerInvariant()
        scenario_sha256 = $protocol.shell_scenario_sha256.ToLowerInvariant()
    }
    Assert-SequenceEqual -Actual @($trusted.PSObject.Properties.Name) -Expected @($expected.Keys)
    foreach ($name in $expected.Keys) {
        Assert-Equal -Actual $trusted.$name -Expected $expected[$name] "Trusted common input '$name' differs."
    }
}

Test-Case 'Trusted common inputs deep-validate every transitive common artifact' {
    $mutations = @(
        [pscustomobject]@{ name = 'environment-component'; relative_path = 'experiments\m0\protocol\environments\windows-benchmark.v1.json' },
        [pscustomobject]@{ name = 'fixture-queue'; relative_path = 'experiments\m0\shared\shell-fixture\queue.v1.json' },
        [pscustomobject]@{ name = 'fixture-asset'; relative_path = 'experiments\m0\shared\shell-fixture\assets\cover-track.svg' },
        [pscustomobject]@{ name = 'statistics-schedule'; relative_path = 'experiments\m0\protocol\schedule.v1.json' },
        [pscustomobject]@{ name = 'statistics-golden'; relative_path = 'experiments\m0\protocol\goldens\statistics.v1.json' },
        [pscustomobject]@{ name = 'marker-catalog'; relative_path = 'experiments\m0\protocol\markers.v1.json' },
        [pscustomobject]@{ name = 'visual-descriptor'; relative_path = 'experiments\m0\protocol\visual.v1.json' },
        [pscustomobject]@{ name = 'visual-sidecar'; relative_path = 'experiments\m0\protocol\visual.v1.sha256' },
        [pscustomobject]@{ name = 'visual-golden'; relative_path = 'experiments\m0\protocol\goldens\visual.v1.json' },
        [pscustomobject]@{ name = 'scenario'; relative_path = 'experiments\m0\protocol\scenarios\shell.v1.json' }
    )
    foreach ($mutation in $mutations) {
        $root = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot "nested-$($mutation.name)")
        [IO.File]::AppendAllText((Join-Path $root $mutation.relative_path), "`n", [Text.UTF8Encoding]::new($false))
        Assert-Throws { Get-M0TrustedCommonInputs -RepositoryRoot $root } 'hash|canonical|schema|M0F|M0S|M0V|scenario'
    }
}

Test-Case 'The generator is deterministic, test-scoped and never overwrites silently' {
    $firstRoot = Join-Path $TestTempRoot 'generated-manifest-first'
    $secondRoot = Join-Path $TestTempRoot 'generated-manifest-second'
    Assert-Throws { & $harnessManifestGeneratorPath -DestinationRoot $firstRoot } 'AllowTestDestination|authorized'
    $null = & $harnessManifestGeneratorPath -DestinationRoot $firstRoot -AllowTestDestination
    $null = & $harnessManifestGeneratorPath -DestinationRoot $secondRoot -AllowTestDestination

    $firstManifest = [IO.File]::ReadAllBytes((Join-Path $firstRoot 'manifest.v1.json'))
    $secondManifest = [IO.File]::ReadAllBytes((Join-Path $secondRoot 'manifest.v1.json'))
    $committedManifest = [IO.File]::ReadAllBytes($harnessManifestPath)
    Assert-SequenceEqual -Actual @($firstManifest) -Expected @($secondManifest) 'Two generated manifests differ byte-for-byte.'
    Assert-SequenceEqual -Actual @($firstManifest) -Expected @($committedManifest) 'Generated manifest differs from the committed canonical bytes.'
    Assert-Throws { & $harnessManifestGeneratorPath -DestinationRoot $firstRoot -AllowTestDestination } 'exists|overwrite'
    Assert-Throws { & $harnessManifestGeneratorPath } 'exists|overwrite'
}

Test-Case 'Runtime tamper, omission and sidecar tamper fail closed' {
    $expectedHash = Get-M0CommittedHarnessHash

    $tamperedRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'tampered-runtime')
    [IO.File]::AppendAllText((Join-Path $tamperedRoot 'experiments\m0\harness\M0.Evidence.psm1'), "`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $tamperedRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|hash|size'

    $missingRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'missing-runtime')
    Remove-Item -LiteralPath (Join-Path $missingRoot 'experiments\m0\harness\M0.Evidence.psm1') -Force
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $missingRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|missing|coverage'

    $sidecarRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'tampered-sidecar')
    [IO.File]::WriteAllText((Join-Path $sidecarRoot 'experiments\m0\harness\manifest.v1.sha256'), "$('0' * 64)  manifest.v1.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $sidecarRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|sidecar|hash'
}

Test-Case 'Runtime and transitive common inputs reject hardlinks outside the trust root' {
    $expectedHash = Get-M0CommittedHarnessHash

    $runtimeRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'hardlink-runtime')
    $runtimePath = Join-Path $runtimeRoot 'experiments\m0\harness\M0.Evidence.psm1'
    $outsideRuntimePath = Join-Path $TestTempRoot 'outside-M0.Evidence.psm1'
    Copy-Item -LiteralPath $runtimePath -Destination $outsideRuntimePath
    Remove-Item -LiteralPath $runtimePath -Force
    New-Item -ItemType HardLink -Path $runtimePath -Target $outsideRuntimePath | Out-Null
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $runtimeRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|hardlink|link'

    $fixtureRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'hardlink-fixture')
    $queuePath = Join-Path $fixtureRoot 'experiments\m0\shared\shell-fixture\queue.v1.json'
    $outsideQueuePath = Join-Path $TestTempRoot 'outside-queue.v1.json'
    Copy-Item -LiteralPath $queuePath -Destination $outsideQueuePath
    Remove-Item -LiteralPath $queuePath -Force
    New-Item -ItemType HardLink -Path $queuePath -Target $outsideQueuePath | Out-Null
    Assert-Throws { Get-M0TrustedCommonInputs -RepositoryRoot $fixtureRoot } 'M0H0|hardlink|link'
}

Test-Case 'Verification bytes stay outside the decisive digest but executable coverage stays closed' {
    $expectedHash = Get-M0CommittedHarnessHash
    $verificationRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'verification-bytes')
    [IO.File]::AppendAllText((Join-Path $verificationRoot 'experiments\m0\harness\tests\Contract.Tests.ps1'), "`n# verification-only change`n", [Text.UTF8Encoding]::new($false))
    $result = Assert-M0HarnessManifest -RepositoryRoot $verificationRoot -ExpectedManifestSha256 $expectedHash
    Assert-Equal -Actual $result.manifest_sha256 -Expected $expectedHash.ToLowerInvariant()

    $unknownRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'unknown-executable')
    [IO.File]::WriteAllText((Join-Path $unknownRoot 'scripts\m0\Unexpected-Collector.ps1'), "'unexpected'`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $unknownRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|unclassified|coverage|Unexpected-Collector'

    $nativeRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'unknown-native-helper')
    [IO.File]::WriteAllBytes((Join-Path $nativeRoot 'experiments\m0\harness\rogue-helper.exe'), [byte[]](0x4D, 0x5A, 0x00, 0x00))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $nativeRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|unclassified|coverage|rogue-helper'
}

Test-Case 'Every protocol schema is classified as runtime or verification input' {
    $expectedHash = Get-M0CommittedHarnessHash
    $verificationRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'verification-schema-bytes')
    [IO.File]::AppendAllText((Join-Path $verificationRoot 'experiments\m0\protocol\schemas\candidate.schema.json'), " `n", [Text.UTF8Encoding]::new($false))
    $result = Assert-M0HarnessManifest -RepositoryRoot $verificationRoot -ExpectedManifestSha256 $expectedHash
    Assert-Equal -Actual $result.manifest_sha256 -Expected $expectedHash.ToLowerInvariant()

    $unknownRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'unknown-schema')
    [IO.File]::WriteAllText((Join-Path $unknownRoot 'experiments\m0\protocol\schemas\rogue.schema.json'), "{}`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $unknownRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|unclassified|coverage|rogue'

    $nonCanonicalRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'noncanonical-schema-case')
    [IO.File]::WriteAllText((Join-Path $nonCanonicalRoot 'experiments\m0\protocol\schemas\rogue.JSON'), "{}`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $nonCanonicalRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|unclassified|coverage|rogue'

    $collisionRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'schema-case-collision')
    $schemaRoot = Join-Path $collisionRoot 'experiments\m0\protocol\schemas'
    $collisionPath = Join-Path $schemaRoot 'candidate.schema.JSON'
    [IO.File]::WriteAllText($collisionPath, "{}`n", [Text.UTF8Encoding]::new($false))
    $caseVariants = @(Get-ChildItem -LiteralPath $schemaRoot -File | Where-Object { $_.Name -in @('candidate.schema.json', 'candidate.schema.JSON') })
    if ($caseVariants.Count -eq 2) {
        Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $collisionRoot -ExpectedManifestSha256 $expectedHash } 'M0H0|unclassified|coverage|candidate.schema.JSON'
    }
}

Test-Case 'Manifest paths, duplicates and ordinal order fail closed' {
    $mutations = @(
        [pscustomobject]@{ name = 'absolute'; apply = { param($manifest) $manifest.runtime_files[0].relative_path = 'C:/private/collector.ps1' } },
        [pscustomobject]@{ name = 'traversal'; apply = { param($manifest) $manifest.runtime_files[0].relative_path = '../collector.ps1' } },
        [pscustomobject]@{ name = 'ads'; apply = { param($manifest) $manifest.runtime_files[0].relative_path = 'scripts/m0/Test-M0Evidence.ps1:private' } },
        [pscustomobject]@{ name = 'duplicate'; apply = { param($manifest) $manifest.runtime_files[1].relative_path = $manifest.runtime_files[0].relative_path } },
        [pscustomobject]@{ name = 'order'; apply = {
            param($manifest)
            $first = $manifest.runtime_files[0]
            $manifest.runtime_files[0] = $manifest.runtime_files[1]
            $manifest.runtime_files[1] = $first
        } }
    )
    foreach ($mutation in $mutations) {
        $root = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot "manifest-$($mutation.name)")
        $manifestPath = Join-Path $root 'experiments\m0\harness\manifest.v1.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json -Depth 100
        & $mutation.apply $manifest
        $mutatedHash = Write-M0HarnessTestManifest -Root $root -Manifest $manifest
        Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $root -ExpectedManifestSha256 $mutatedHash } 'M0H0|schema|path|duplicate|order|coverage'
    }
}

Test-Case 'Canonical bytes and reparse-point paths fail closed' {
    $canonicalRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'noncanonical-manifest')
    $manifestPath = Join-Path $canonicalRoot 'experiments\m0\harness\manifest.v1.json'
    [IO.File]::AppendAllText($manifestPath, "`n", [Text.UTF8Encoding]::new($false))
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash
    [IO.File]::WriteAllText((Join-Path $canonicalRoot 'experiments\m0\harness\manifest.v1.sha256'), "$($manifestHash.ToLowerInvariant())  manifest.v1.json`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $canonicalRoot -ExpectedManifestSha256 $manifestHash } 'M0H0|canonical|newline'

    $reparseRoot = Copy-M0HarnessTrustTree -DestinationRoot (Join-Path $TestTempRoot 'reparse-runtime')
    $outsideScripts = Join-Path $TestTempRoot 'outside-scripts'
    Copy-Item -LiteralPath (Join-Path $reparseRoot 'scripts\m0') -Destination $outsideScripts -Recurse
    Remove-Item -LiteralPath (Join-Path $reparseRoot 'scripts\m0') -Recurse -Force
    $linkType = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
    New-Item -ItemType $linkType -Path (Join-Path $reparseRoot 'scripts\m0') -Target $outsideScripts | Out-Null
    Assert-Throws { Assert-M0HarnessManifest -RepositoryRoot $reparseRoot -ExpectedManifestSha256 (Get-M0CommittedHarnessHash) } 'M0H0|reparse|link'
}

Test-Case 'The public Evidence entrypoint rejects homonymous modules loaded outside the repository' {
    $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
    foreach ($moduleName in @('M0.Harness', 'M0.Evidence')) {
        $fakeRoot = Join-Path $TestTempRoot "preloaded-$moduleName"
        New-Item -ItemType Directory -Path $fakeRoot | Out-Null
        $fakePath = Join-Path $fakeRoot "$moduleName.psm1"
        $fakeSource = if ($moduleName -ceq 'M0.Harness') {
            "function Get-M0TrustedCommonInputs { throw 'FAKE-HARNESS-CALLED' }`nExport-ModuleMember -Function Get-M0TrustedCommonInputs`n"
        }
        else {
            "function Assert-M0EvidenceRun { throw 'FAKE-EVIDENCE-CALLED' }`nExport-ModuleMember -Function Assert-M0EvidenceRun`n"
        }
        [IO.File]::WriteAllText($fakePath, $fakeSource, [Text.UTF8Encoding]::new($false))
        $childPath = Join-Path $TestTempRoot "preloaded-$moduleName.ps1"
        $childSource = @"
`$ErrorActionPreference = 'Stop'
Import-Module -Name '$($fakePath.Replace("'", "''"))' -ErrorAction Stop
& '$($runner.Replace("'", "''"))' -BatchPath 'not-used.json' -SnapshotSetPath 'not-used.snapshot-set.json'
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $result = Invoke-M0HarnessChildProcess -ScriptPath $childPath
        Assert-True ($result.exit_code -ne 0) "The child with preloaded '$moduleName' unexpectedly succeeded."
        Assert-True ($result.output -match 'M0E027') "The preloaded '$moduleName' was not rejected. Child output: $($result.output)"
        Assert-True ($result.output -notmatch 'FAKE-(HARNESS|EVIDENCE)-CALLED') "The preloaded '$moduleName' executed before rejection. Child output: $($result.output)"
    }
}

Test-Case 'The public Evidence entrypoint rejects an exact-path module whose in-memory commands are stale' {
    $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
    $harnessModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
    $childPath = Join-Path $TestTempRoot 'stale-exact-path-entrypoint.ps1'
    $childSource = @"
`$ErrorActionPreference = 'Stop'
Import-Module -Name '$($harnessModulePath.Replace("'", "''"))' -ErrorAction Stop
`$loadedHarness = Get-Module -Name 'M0.Harness' -ErrorAction Stop
& `$loadedHarness {
    Set-Item -LiteralPath Function:Get-M0TrustedCommonInputs -Value {
        throw 'STALE-EXACT-PATH-MODULE-EXECUTED'
    }
}
& '$($runner.Replace("'", "''"))' -BatchPath 'not-used.json' -SnapshotSetPath 'not-used.snapshot-set.json'
"@
    [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))

    $result = Invoke-M0HarnessChildProcess -ScriptPath $childPath
    Assert-True ($result.exit_code -ne 0) 'The stale exact-path child unexpectedly succeeded.'
    Assert-True ($result.output -match 'M0E027') "The stale exact-path module was not rejected at the entrypoint boundary. Child output: $($result.output)"
    Assert-True ($result.output -notmatch 'STALE-EXACT-PATH-MODULE-EXECUTED') "The stale exact-path module executed before rejection. Child output: $($result.output)"
}

Test-Case 'The public Evidence entrypoint imports only anchored modules in a clean child process' {
    $runner = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Evidence.ps1'
    $childPath = Join-Path $TestTempRoot 'clean-entrypoint.ps1'
    $childSource = @"
`$ErrorActionPreference = 'Stop'
& '$($runner.Replace("'", "''"))' -BatchPath 'not-used.json' -SnapshotSetPath 'not-used.snapshot-set.json'
"@
    [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
    $result = Invoke-M0HarnessChildProcess -ScriptPath $childPath
    Assert-True ($result.exit_code -ne 0) 'The decisive batch entrypoint unexpectedly opened before candidate provenance exists.'
    Assert-True ($result.output -match 'M0E017') "The clean child did not reach the anchored candidate-provenance gate. Child output: $($result.output)"
    Assert-True ($result.output -notmatch 'M0E027') "The clean child was mistaken for a preloaded-module conflict. Child output: $($result.output)"
}
