#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$module = Join-Path $repository 'scripts/Traceability.psm1'
if (Test-Path -LiteralPath $module) { Import-Module $module -Force }
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection'
}
$testRoot = Join-Path $repository ('out/product/traceability-contract-' + [guid]::NewGuid().ToString('N'))
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^traceability-contract-[0-9a-f]{32}$') { throw 'Unsafe test directory' }
    $ancestor = $resolved
    while ($ancestor) {
        if ((Test-Path -LiteralPath $ancestor) -and
            ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe reparse point' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function New-Manifest {
    return @{
        schemaVersion = 1; requirementsFile = 'docs/product/requirements.md'; planFile = 'docs/development-plan.md'
        records = @(
            @{ id = 'FR-001'; plan = 'M2.1'; status = 'em andamento'; verification = 'mixed'
                tests = @(@{ path = 'tests/example.cpp'; marker = 'opens one file' })
                evidence = @(@{ path = 'docs/evidence.md'; marker = 'Focused run passed' })
                remaining = @('Native dialog pending') },
            @{ id = 'UX-001'; plan = 'M3.8'; status = 'comprovado'; verification = 'automated'
                tests = @(@{ path = 'tests/example.cpp'; marker = 'opens one file' })
                evidence = @(@{ path = 'docs/evidence.md'; marker = 'Focused run passed' })
                remaining = @() }
        )
    }
}
function Write-Manifest($Value) {
    [IO.File]::WriteAllText((Join-Path $testRoot 'docs/quality/traceability.json'), ($Value | ConvertTo-Json -Depth 12))
}
function Read-Report { Get-RcMusicTraceabilityReport -RepositoryRoot $testRoot }
Assert-SafeTestRoot
$null = New-Item -ItemType Directory -Path (Join-Path $testRoot 'docs/quality'), (Join-Path $testRoot 'docs/product'), (Join-Path $testRoot 'tests'), (Join-Path $testRoot 'src')
try {
    $requirements = "| FR-001 | P0 | Open |`n| UX-001 | P0 | Empty state |"
    $plan = '### 15.1 Destino individual dos 94 requisitos' + "`n" + '| `FR-001` | M2.1 | Test |' + "`n" + '| `UX-001` | M3.8 | Test |' + "`n## 16. Lacunas que o plano fecha explicitamente"
    [IO.File]::WriteAllText((Join-Path $testRoot 'docs/product/requirements.md'), $requirements)
    [IO.File]::WriteAllText((Join-Path $testRoot 'docs/development-plan.md'), $plan)
    [IO.File]::WriteAllText((Join-Path $testRoot 'tests/example.cpp'), 'TEST_CASE("opens one file") {}')
    [IO.File]::WriteAllText((Join-Path $testRoot 'docs/evidence.md'), "# Focused run passed`nDecision FR-001 UX-001")
    [IO.File]::WriteAllText((Join-Path $testRoot 'src/example.cpp'), 'void openFile() {}')
    Test-Case 'valid mapping reports fulfillment separately and provides reverse references' {
        Write-Manifest (New-Manifest)
        $report = Read-Report
        Assert-True ($report.total -eq 2 -and $report.statusCounts['comprovado'] -eq 1) 'Wrong status counts'
        Assert-True (-not $report.allRequirementsProven) 'Mapping success must not mean fulfillment'
        $reverse = @($report.references | Where-Object { $_.path -ceq 'tests/example.cpp' })
        Assert-True ($reverse.Count -eq 1 -and ($reverse[0].requirements -join '|') -ceq 'FR-001|UX-001') 'Missing reverse links'
    }
    $mutations = @(
        @{ name = 'missing requirement'; pattern = 'inventory'; edit = { param($m) $m.records = @($m.records[0]) } },
        @{ name = 'duplicate requirement'; pattern = 'duplicate'; edit = { param($m) $m.records += $m.records[0] } },
        @{ name = 'unknown requirement'; pattern = 'inventory'; edit = { param($m) $m.records[0].id = 'FR-999' } },
        @{ name = 'wrong case ID'; pattern = 'inventory'; edit = { param($m) $m.records[0].id = 'fr-001' } },
        @{ name = 'wrong destination'; pattern = 'destination'; edit = { param($m) $m.records[0].plan = 'M9.9' } },
        @{ name = 'array masquerading as destination'; pattern = 'destination'; edit = { param($m) $m.records[0].plan = @('M2.1') } },
        @{ name = 'unknown status'; pattern = 'status'; edit = { param($m) $m.records[0].status = 'done' } },
        @{ name = 'unknown verification'; pattern = 'verification'; edit = { param($m) $m.records[0].verification = 'magic' } },
        @{ name = 'proven with remaining work'; pattern = 'remaining'; edit = { param($m) $m.records[0].status = 'comprovado' } },
        @{ name = 'partial without remaining work'; pattern = 'remaining'; edit = { param($m) $m.records[0].remaining = @() } },
        @{ name = 'blank remaining work'; pattern = 'remaining'; edit = { param($m) $m.records[0].remaining = @(' ') } },
        @{ name = 'missing evidence'; pattern = 'evidence'; edit = { param($m) $m.records[0].evidence = @() } },
        @{ name = 'proven behavior without tests'; pattern = 'tests'; edit = { param($m) $m.records[1].tests = @() } },
        @{ name = 'future marked proven'; pattern = 'future'; edit = { param($m) $m.records[1].verification = 'future' } },
        @{ name = 'missing reference file'; pattern = 'unavailable'; edit = { param($m) $m.records[0].tests[0].path = 'tests/missing.cpp' } },
        @{ name = 'stale literal marker'; pattern = 'marker'; edit = { param($m) $m.records[0].tests[0].marker = 'removed test' } },
        @{ name = 'regex marker is not a literal'; pattern = 'marker'; edit = { param($m) $m.records[0].tests[0].marker = 'opens.*file' } },
        @{ name = 'blank marker'; pattern = 'marker'; edit = { param($m) $m.records[0].tests[0].marker = ' ' } },
        @{ name = 'parent traversal'; pattern = 'path'; edit = { param($m) $m.records[0].evidence[0].path = '../secrets.txt' } },
        @{ name = 'absolute path'; pattern = 'path'; edit = { param($m) $m.records[0].evidence[0].path = 'C:/secrets.txt' } },
        @{ name = 'remote path'; pattern = 'path'; edit = { param($m) $m.records[0].evidence[0].path = 'https://example.com/a' } },
        @{ name = 'ignored output reference'; pattern = 'path'; edit = { param($m) $m.records[0].evidence[0].path = 'out/private.txt' } },
        @{ name = 'scalar array'; pattern = 'array'; edit = { param($m) $m.records[0].remaining = 'pending' } },
        @{ name = 'unknown record property'; pattern = 'fields'; edit = { param($m) $m.records[0].typo = 1 } },
        @{ name = 'unknown root property'; pattern = 'fields'; edit = { param($m) $m.typo = 1 } },
        @{ name = 'schema version'; pattern = 'schema'; edit = { param($m) $m.schemaVersion = 2 } },
        @{ name = 'alternate requirements'; pattern = 'requirementsFile'; edit = { param($m) $m.requirementsFile = 'docs/evidence.md' } },
        @{ name = 'exception without decision'; pattern = 'decision'; edit = { param($m) $m.records[1].status = 'excepcionado'; $m.records[1].remaining = @('Pending'); $m.records[1].tests = @() } },
        @{ name = 'empty decision array'; pattern = 'fields'; edit = { param($m) $m.records[1].status = 'excepcionado'; $m.records[1].remaining = @('Pending'); $m.records[1].decision = @() } },
        @{ name = 'decision object wrapped in array'; pattern = 'fields'; edit = { param($m) $m.records[1].status = 'excepcionado'; $m.records[1].remaining = @('Pending'); $m.records[1].decision = @(@{ path = 'docs/evidence.md'; marker = 'Decision FR-001 UX-001' }) } },
        @{ name = 'P0 functional cannot be waived'; pattern = 'P0'; edit = { param($m) $m.records[0].status = 'fora de escopo'; $m.records[0].decision = @{ path = 'docs/evidence.md'; marker = 'Decision FR-001 UX-001' } } }
    )
    foreach ($case in $mutations) {
        Test-Case "rejects $($case.name)" {
            $manifest = New-Manifest
            & $case.edit $manifest
            Write-Manifest $manifest
            Assert-Rejected { Read-Report } $case.pattern
        }
    }
    Test-Case 'documentary proof needs evidence but not a behavior test' {
        $manifest = New-Manifest
        $manifest.records[1].verification = 'documentary'; $manifest.records[1].tests = @()
        Write-Manifest $manifest
        $null = Read-Report
    }
    Test-Case 'implementation references can supplement a partial behavior record' {
        $manifest = New-Manifest
        $manifest.records[0].evidence += @{ path = 'src/example.cpp'; marker = 'void openFile()' }
        Write-Manifest $manifest
        $report = Read-Report
        Assert-True (-not $report.allRequirementsProven) 'Implementation became proof'
    }
    Test-Case 'promoted packaging tests are durable references without accepting output or traversal' {
        $null = New-Item -ItemType Directory -Path (Join-Path $testRoot 'packaging/windows/tests')
        [IO.File]::WriteAllText((Join-Path $testRoot 'packaging/windows/tests/Example.Tests.ps1'), '# plans a confined runtime')
        $manifest = New-Manifest
        $manifest.records[0].tests = @(@{ path = 'packaging/windows/tests/Example.Tests.ps1'; marker = 'plans a confined runtime' })
        Write-Manifest $manifest
        $report = Read-Report
        Assert-True (-not $report.allRequirementsProven) 'Packaging reference became fulfillment proof'
        foreach ($path in @('packaging/../out/private.txt', 'packaging/windows/../../../secrets.txt', 'out/product/runtime-manifest.json')) {
            $manifest.records[0].tests[0].path = $path
            Write-Manifest $manifest
            Assert-Rejected { Read-Report } 'path'
        }
    }
    Test-Case 'planned future work is valid without pretending to have tests' {
        $manifest = New-Manifest
        $manifest.records[1].status = 'planejado'; $manifest.records[1].verification = 'future'
        $manifest.records[1].tests = @(); $manifest.records[1].remaining = @('Await refinement')
        Write-Manifest $manifest
        $null = Read-Report
    }
    foreach ($document in @('docs/product/requirements.md', 'docs/development-plan.md')) {
        Test-Case "rejects duplicate canonical inventory in $document" {
            $path = Join-Path $testRoot $document
            $original = [IO.File]::ReadAllText($path)
            try {
                [IO.File]::WriteAllText($path, $original.Replace('| FR-001 |', "| FR-001 | P0 | Duplicate |`n| FR-001 |").Replace('| `FR-001` |', '| `FR-001` | M2.1 | Duplicate |' + "`n" + '| `FR-001` |'))
                Write-Manifest (New-Manifest)
                Assert-Rejected { Read-Report } 'duplicate'
            } finally { [IO.File]::WriteAllText($path, $original) }
        }
    }
    Test-Case 'duplicate JSON keys are rejected rather than silently overwritten' {
        Write-Manifest (New-Manifest)
        $path = Join-Path $testRoot 'docs/quality/traceability.json'
        $json = [IO.File]::ReadAllText($path).Replace('"schemaVersion": 1', '"schemaVersion": 1, "schemaVersion": 1')
        [IO.File]::WriteAllText($path, $json)
        Assert-Rejected { Read-Report } 'duplicate'
    }
    Test-Case 'a reference cannot traverse a junction even to an existing marker' {
        Write-Manifest (New-Manifest)
        $junction = Join-Path $testRoot 'tests/linked'
        Assert-SafeTestRoot
        $null = New-Item -ItemType Junction -Path $junction -Target (Join-Path $testRoot 'docs')
        try {
            $manifest = New-Manifest
            $manifest.records[0].evidence[0].path = 'tests/linked/evidence.md'
            Write-Manifest $manifest
            Assert-Rejected { Read-Report } 'reparse point'
        } finally {
            Assert-SafeTestRoot
            Remove-Item -LiteralPath $junction -Force
        }
    }
    Test-Case 'an exception requires a literal requirement-specific documentary decision' {
        $manifest = New-Manifest
        $manifest.records[1].status = 'excepcionado'; $manifest.records[1].remaining = @('Documented gate deferral')
        $manifest.records[1].decision = @{ path = 'docs/evidence.md'; marker = 'Decision FR-001 UX-001' }
        Write-Manifest $manifest
        $report = Read-Report
        Assert-True (-not $report.allRequirementsProven -and $report.statusCounts['excepcionado'] -eq 1) 'Exception was treated as proof'
        $manifest.records[1].decision.marker = 'Focused run passed'
        Write-Manifest $manifest
        Assert-Rejected { Read-Report } 'Decision.*UX-001'
    }
    Test-Case 'missing manifest fails instead of producing an empty success report' {
        $path = Join-Path $testRoot 'docs/quality/traceability.json'
        Assert-SafeTestRoot
        Remove-Item -LiteralPath $path
        Assert-Rejected { Read-Report } 'unavailable'
    }
    Test-Case 'CLI emits parseable JSON and is independent of caller directory' {
        Write-Manifest (New-Manifest)
        Push-Location -LiteralPath $testRoot
        try {
            $output = & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $repository 'scripts/validate-traceability.ps1') -RepositoryRoot $testRoot -Json 2>&1
            Assert-True ($LASTEXITCODE -eq 0) "CLI failed: $output"
            $report = ($output -join "`n") | ConvertFrom-Json
            Assert-True ($report.total -eq 2) 'CLI report missing'
        } finally { Pop-Location }
    }
    Test-Case 'CLI rejects invalid manifest with nonzero status' {
        $manifest = New-Manifest; $manifest.records = @(); Write-Manifest $manifest
        $null = & (Get-Process -Id $PID).Path -NoProfile -File (Join-Path $repository 'scripts/validate-traceability.ps1') -RepositoryRoot $testRoot 2>&1
        Assert-True ($LASTEXITCODE -ne 0) 'Invalid manifest passed'
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
Write-Host "Traceability: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
