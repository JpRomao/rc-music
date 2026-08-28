#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$contractRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = (Resolve-Path (Join-Path $contractRoot '../../../..')).Path
$modulePath = Join-Path $contractRoot 'M0.QtShellComposite.psm1'
$nativeModulePath = Join-Path $contractRoot 'M0.QtShellCompositeNative.psm1'
$launcherPath = Join-Path $contractRoot 'Invoke-M0QtShellComposite.ps1'
$contractPath = Join-Path $contractRoot 'qt-shell-composite-contract.v2.json'
$sidecarPath = $contractPath + '.sha256'
$expectedContractSha256 = 'f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28'
$state = [ordered]@{ Passed = 0; Failed = 0 }
$module = $null
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('m0-qt-shell-composite-core-' + [Guid]::NewGuid().ToString('N'))

function Test-Case {
    param([Parameter(Mandatory)][string] $Name, [Parameter(Mandatory)][scriptblock] $Body)
    try {
        & $Body
        $state.Passed++
        Write-Host "PASS: $Name"
    }
    catch {
        $state.Failed++
        Write-Host "FAIL: $Name`n$($_.Exception.ToString())" -ForegroundColor Red
    }
}

function Assert-True {
    param([Parameter(Mandatory)][bool] $Condition, [Parameter(Mandatory)][string] $Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Actual, $Expected, [string] $Message = 'Values differ.')
    if ($Actual -is [array] -or $Expected -is [array]) {
        $actualValues = @($Actual)
        $expectedValues = @($Expected)
        if ($actualValues.Count -ne $expectedValues.Count) {
            throw "$Message Expected $($expectedValues.Count) values, got $($actualValues.Count)."
        }
        for ($index = 0; $index -lt $expectedValues.Count; $index++) {
            if ($actualValues[$index] -cne $expectedValues[$index]) {
                throw "$Message Difference at ${index}: '$($actualValues[$index])' != '$($expectedValues[$index])'."
            }
        }
        return
    }
    if ($Actual -cne $Expected) { throw "$Message '$Actual' != '$Expected'." }
}

function Assert-Rejected {
    param([Parameter(Mandatory)][scriptblock] $Body, [Parameter(Mandatory)][string] $Pattern)
    try { & $Body }
    catch {
        if ($_.Exception.ToString() -notmatch $Pattern) {
            throw "Expected rejection matching '$Pattern', got: $($_.Exception.ToString())"
        }
        return
    }
    throw "Expected rejection matching '$Pattern', but the operation succeeded."
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string] $LiteralPath)
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Read-Json {
    param([Parameter(Mandatory)][string] $LiteralPath)
    return Get-Content -Raw -LiteralPath $LiteralPath | ConvertFrom-Json -Depth 100
}

function Write-TestBytes {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][byte[]] $Bytes)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LiteralPath)) | Out-Null
    [IO.File]::WriteAllBytes($LiteralPath, $Bytes)
}

function New-TestOwnedRow {
    param(
        [Parameter(Mandatory)][string] $OwnerId,
        [Parameter(Mandatory)][string] $RelativePath,
        [byte] $Value = 65
    )
    $bytes = [byte[]]@($Value, $Value + 1, $Value + 2, $Value + 3)
    return [pscustomobject][ordered]@{
        owner_id = $OwnerId
        source_relative_path = $RelativePath
        relative_path = $RelativePath
        size_bytes = [long]$bytes.Length
        sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
        bytes = $bytes
    }
}

Test-Case 'Qt shell composite implementation exists' {
    if (-not [IO.File]::Exists($modulePath)) {
        throw [IO.FileNotFoundException]::new("Composite implementation is missing: $modulePath")
    }
}

if (-not [IO.File]::Exists($modulePath)) {
    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    exit 1
}

$module = Import-Module -Name $modulePath -Force -PassThru
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

try {
    Test-Case 'public API is exact and the complete recipe exists' {
        Assert-Equal @($module.ExportedFunctions.Keys | Sort-Object) @(
            'Assert-M0QtShellCompositeContract',
            'Assert-M0QtShellCompositeReceipt',
            'Invoke-M0QtShellComposite'
        ) 'Composite public API drifted.'
        foreach ($path in @($nativeModulePath, $launcherPath, $contractPath, $sidecarPath)) {
            Assert-True ([IO.File]::Exists($path)) "Composite recipe member is missing: $path"
        }
    }

    Test-Case 'production contract is externally anchored and canonical' {
        Assert-True ($expectedContractSha256 -match '^[0-9a-f]{64}$') `
            'The test still carries the pre-Green contract hash sentinel.'
        Assert-Equal (Get-Sha256 $contractPath) $expectedContractSha256 `
            'Production contract bytes differ from the external anchor.'
        $expectedSidecar = "$expectedContractSha256 *qt-shell-composite-contract.v2.json`n"
        Assert-Equal ([IO.File]::ReadAllText($sidecarPath)) $expectedSidecar `
            'Composite contract sidecar is not exact LF text.'
        $accepted = Assert-M0QtShellCompositeContract -LiteralPath $contractPath `
            -ExpectedSha256 $expectedContractSha256
        Assert-Equal $accepted.composite_id 'm0.2-qt-shell-composite-sdk-v2' `
            'Contract validator returned the wrong identity.'
    }

    Test-Case 'historical v1 contract remains byte-identical with eight members' {
        $v1Path = Join-Path $contractRoot 'qt-shell-toolchain-contract.v1.json'
        Assert-Equal (Get-Sha256 $v1Path) `
            'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952' `
            'Historical Qt shell v1 contract drifted.'
        $v1 = Read-Json $v1Path
        Assert-Equal @($v1.files).Count 8 'Historical Qt shell v1 member count drifted.'
    }

    Test-Case 'contract closes product provenance ownership and isolation' {
        $contract = Read-Json $contractPath
        Assert-Equal @($contract.PSObject.Properties.Name) @(
            'schema_version', 'composite_id', 'product', 'provenance', 'recipe', 'inputs',
            'ownership', 'required_artifacts', 'isolation', 'limits'
        ) 'Top-level composite contract shape drifted.'
        Assert-Equal ([long]$contract.schema_version) ([long]2) 'Composite schema drifted.'
        Assert-Equal $contract.product.qt_version '6.11.2' 'Qt version drifted.'
        Assert-Equal $contract.product.architecture 'x86_64' 'Architecture drifted.'
        Assert-Equal $contract.product.compiler_abi 'msvc2022_64' 'Qt ABI label drifted.'
        Assert-Equal $contract.product.windows_sdk_version '10.0.26100.0' 'Windows SDK drifted.'
        Assert-Equal $contract.provenance.qt_shell_v1_contract.sha256 `
            'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952' `
            'Composite lost historical v1 provenance.'
        Assert-Equal $contract.provenance.qtbase_refresh.receipt_sha256 `
            '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619' `
            'Qtbase receipt is not anchored externally.'
        Assert-Equal $contract.provenance.qtbase_refresh.receipt_relative_path `
            'q/96bvah3m0t0t71m990olv5gpvs/evidence/qtbase-refresh-receipt.v2.json' `
            'Qtbase receipt canonical path drifted.'
        Assert-Equal @($contract.ownership.owners) `
            @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d') `
            'Composite owners drifted.'
        Assert-Equal $contract.ownership.path_comparer 'OrdinalIgnoreCase' 'Path comparer drifted.'
        Assert-Equal $contract.ownership.leaf_collision_policy 'reject-even-identical' `
            'Leaf collision policy drifted.'
        Assert-Equal $contract.ownership.directory_overlap_allowed $true `
            'Directory overlap must remain allowed.'
        foreach ($flag in @('allow_hardlinks', 'allow_reparse_points', 'allow_ads', 'official_sdk_fallback')) {
            Assert-Equal $contract.ownership.$flag $false "Unsafe ownership flag '$flag' drifted."
        }
        Assert-Equal $contract.isolation.run_parent_relative_path 'q' 'Run parent drifted.'
        Assert-Equal $contract.isolation.require_new_direct_child $true 'RunRoot policy drifted.'
        Assert-Equal $contract.isolation.sdk_relative_path 'sdk' 'SDK publication path drifted.'
        Assert-Equal $contract.isolation.receipt_relative_path `
            'evidence/qt-shell-composite-receipt.v2.json' 'Receipt path drifted.'
    }

    Test-Case 'recipe and source archives are hash and size pinned' {
        $contract = Read-Json $contractPath
        Assert-Equal @($contract.recipe.files.id) @('orchestrator', 'runner', 'entrypoint') `
            'Composite recipe order drifted.'
        Assert-Equal $contract.recipe.runner_export 'Invoke-M0QtShellCompositeNative' `
            'Native runner binding drifted.'
        foreach ($recipeFile in @($contract.recipe.files)) {
            Assert-True ($recipeFile.sha256 -match '^[0-9a-f]{64}$') `
                "Recipe '$($recipeFile.id)' lacks a digest."
            Assert-True ([long]$recipeFile.size_bytes -gt 0) "Recipe '$($recipeFile.id)' lacks a size."
            $path = Join-Path $repositoryRoot $recipeFile.relative_path
            Assert-Equal (Get-Sha256 $path) $recipeFile.sha256 "Recipe '$($recipeFile.id)' bytes drifted."
            Assert-Equal (Get-Item -LiteralPath $path).Length ([long]$recipeFile.size_bytes) `
                "Recipe '$($recipeFile.id)' size drifted."
        }
        Assert-Equal @($contract.inputs.archives.owner_id) @('qtdeclarative', 'qtsvg', 'd3d') `
            'Archive owner order drifted.'
        Assert-Equal @($contract.inputs.archives.sha256) @(
            '5591ca564c1a9299a45a15b6b1d324c86e0aaed7cf6c47b7e6fe464aecae2587',
            '417f44499c835b2303f3ff78043179bb23442f33d5d2816fbf7e8dbf275b1c89',
            'b1c379f739fa59a495a388c0878757e3d90fb72e0df771237a1ee4a2c1675d15'
        ) 'Archive digest pins drifted.'
        Assert-Equal @($contract.inputs.archives.relative_path) @(
            'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017qtdeclarative-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
            'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017qtsvg-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z',
            'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017d3dcompiler_47-x64.7z'
        ) 'Archive relative-path pins drifted.'
        foreach ($archive in @($contract.inputs.archives)) {
            Assert-True ([long]$archive.size_bytes -gt 0) "Archive '$($archive.owner_id)' lacks a size."
        }

        $mutant = $contract | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100
        $mutant.inputs.archives[0].relative_path = 'out/m0/downloads/substitute.7z'
        $mutantPath = Join-Path $testRoot 'archive-relative-path-mutant.json'
        [IO.File]::WriteAllText(
            $mutantPath, ($mutant | ConvertTo-Json -Depth 100) + "`n", [Text.UTF8Encoding]::new($false))
        Assert-Rejected {
            Assert-M0QtShellCompositeContract -LiteralPath $mutantPath `
                -ExpectedSha256 (Get-Sha256 $mutantPath)
        } 'archive|relative_path|pin|drift'
    }

    Test-Case 'required artifacts close mixed ownership and missing QML codegen gaps' {
        $contract = Read-Json $contractPath
        $required = @{}
        foreach ($artifact in @($contract.required_artifacts)) {
            $key = [string]$artifact.relative_path
            Assert-True (-not $required.ContainsKey($key.ToLowerInvariant())) `
                "Required artifact path is duplicated: $key"
            $required[$key.ToLowerInvariant()] = [string]$artifact.owner_id
            Assert-True ($artifact.sha256 -match '^[0-9a-f]{64}$') "Required artifact '$key' lacks a hash."
            Assert-True ([long]$artifact.size_bytes -gt 0) "Required artifact '$key' lacks a size."
        }
        $expected = [ordered]@{
            'bin/Qt6Core.dll' = 'qtbase-refresh-v2'
            'bin/Qt6Gui.dll' = 'qtbase-refresh-v2'
            'bin/Qt6Widgets.dll' = 'qtbase-refresh-v2'
            'bin/Qt6Test.dll' = 'qtbase-refresh-v2'
            'bin/qtpaths.exe' = 'qtbase-refresh-v2'
            'bin/windeployqt.exe' = 'qtbase-refresh-v2'
            'bin/moc.exe' = 'qtbase-refresh-v2'
            'bin/rcc.exe' = 'qtbase-refresh-v2'
            'bin/uic.exe' = 'qtbase-refresh-v2'
            'plugins/platforms/qwindows.dll' = 'qtbase-refresh-v2'
            'plugins/platforms/qoffscreen.dll' = 'qtbase-refresh-v2'
            'bin/Qt6Qml.dll' = 'qtdeclarative'
            'bin/Qt6Quick.dll' = 'qtdeclarative'
            'bin/Qt6QuickControls2.dll' = 'qtdeclarative'
            'bin/Qt6QuickLayouts.dll' = 'qtdeclarative'
            'bin/Qt6QuickTest.dll' = 'qtdeclarative'
            'bin/qmlformat.exe' = 'qtdeclarative'
            'bin/qmllint.exe' = 'qtdeclarative'
            'bin/qmltestrunner.exe' = 'qtdeclarative'
            'bin/qmlimportscanner.exe' = 'qtdeclarative'
            'bin/qmlcachegen.exe' = 'qtdeclarative'
            'bin/qmltyperegistrar.exe' = 'qtdeclarative'
            'bin/Qt6Svg.dll' = 'qtsvg'
            'plugins/imageformats/qsvg.dll' = 'qtsvg'
            'plugins/iconengines/qsvgicon.dll' = 'qtsvg'
            'd3dcompiler_47.dll' = 'd3d'
        }
        Assert-Equal $required.Count 26 'Required artifacts must be the exact closed 26-member set.'
        Assert-Equal $required.Count $expected.Count 'Required artifact test expectation is incomplete.'
        foreach ($entry in $expected.GetEnumerator()) {
            Assert-True ($required.ContainsKey($entry.Key.ToLowerInvariant())) `
                "Required artifact is missing: $($entry.Key)"
            Assert-Equal $required[$entry.Key.ToLowerInvariant()] $entry.Value `
                "Required artifact owner drifted: $($entry.Key)"
        }
    }

    Test-Case 'archive members reject unsafe namespaces links and case aliases' {
        $valid = @(
            [pscustomobject]@{ path = 'bin'; kind = 'directory'; size_bytes = 0 },
            [pscustomobject]@{ path = 'bin/Qt6Qml.dll'; kind = 'file'; size_bytes = 12 }
        )
        $null = & $module { param($Rows) Assert-M0QtShellCompositeArchiveMembers -Members $Rows } $valid
        $invalid = @(
            @([pscustomobject]@{ path = '../escape'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = '/absolute'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = 'C:/absolute'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = 'bin/file:ads'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = 'bin//file'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = 'bin/./file'; kind = 'file'; size_bytes = 1 }),
            @([pscustomobject]@{ path = 'link'; kind = 'link'; size_bytes = 0 }),
            @(
                [pscustomobject]@{ path = 'bin/A.dll'; kind = 'file'; size_bytes = 1 },
                [pscustomobject]@{ path = 'BIN/a.DLL'; kind = 'file'; size_bytes = 1 }
            ),
            @(
                [pscustomobject]@{ path = 'bin'; kind = 'file'; size_bytes = 1 },
                [pscustomobject]@{ path = 'bin/a.dll'; kind = 'file'; size_bytes = 1 }
            ),
            @(
                [pscustomobject]@{ path = 'bin/a.dll'; kind = 'file'; size_bytes = 1 },
                [pscustomobject]@{ path = 'bin'; kind = 'file'; size_bytes = 1 }
            )
        )
        foreach ($rows in $invalid) {
            Assert-Rejected {
                & $module { param($Rows) Assert-M0QtShellCompositeArchiveMembers -Members $Rows } $rows
            } 'archive|unsafe|path|duplicate|alias|link|ADS|member'
        }
    }

    Test-Case 'archive member validation performs bounded path-depth probes' {
        $rows = [Collections.Generic.List[object]]::new()
        $rows.Add([pscustomobject]@{ path = 'payload'; kind = 'directory'; size_bytes = [long]0 })
        for ($index = 0; $index -lt 3000; $index++) {
            $rows.Add([pscustomobject]@{
                    path = "payload/leaf-$index.bin"; kind = 'file'; size_bytes = [long]1
                })
        }
        $metrics = [ordered]@{ path_probes = [long]0 }
        $null = & $module {
            param($Members, $Metrics)
            Assert-M0QtShellCompositeArchiveMembers -Members $Members -Metrics $Metrics
        } $rows.ToArray() $metrics
        Assert-True ([long]$metrics.path_probes -le 18000) `
            "Archive validation exceeded the O(n * path-depth) probe budget: $($metrics.path_probes)."
    }

    Test-Case 'ownership plan permits directory overlap and rejects every leaf collision' {
        $owners = [ordered]@{
            'qtbase-refresh-v2' = @([pscustomobject]@{
                    source_relative_path = 'bin/Core.dll'; relative_path = 'bin/Core.dll';
                    size_bytes = 4; sha256 = ('a' * 64)
                })
            'qtdeclarative' = @([pscustomobject]@{
                    source_relative_path = 'bin/Qml.dll'; relative_path = 'bin/Qml.dll';
                    size_bytes = 4; sha256 = ('b' * 64)
                })
            'qtsvg' = @([pscustomobject]@{
                    source_relative_path = 'plugins/svg.dll'; relative_path = 'plugins/svg.dll';
                    size_bytes = 4; sha256 = ('c' * 64)
                })
            'd3d' = @([pscustomobject]@{
                    source_relative_path = 'd3dcompiler_47.dll'; relative_path = 'd3dcompiler_47.dll';
                    size_bytes = 4; sha256 = ('d' * 64)
                })
        }
        $plan = & $module { param($Rows) New-M0QtShellCompositeOwnershipPlan -OwnerEntries $Rows } $owners
        Assert-Equal @($plan).Count 4 'Directory overlap incorrectly removed an owned leaf.'

        $owners.d3d[0].relative_path = 'BIN/core.DLL'
        $owners.d3d[0].sha256 = 'a' * 64
        Assert-Rejected {
            & $module { param($Rows) New-M0QtShellCompositeOwnershipPlan -OwnerEntries $Rows } $owners
        } 'collision|duplicate|case-insensitive|destination'
    }

    Test-Case 'ownership planning performs bounded path-depth probes instead of pairwise scans' {
        $owners = [ordered]@{}
        $ownerIndex = 0
        foreach ($ownerId in @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')) {
            $rows = [Collections.Generic.List[object]]::new()
            for ($index = 0; $index -lt 300; $index++) {
                $path = "owner-$ownerIndex/leaf-$index.bin"
                $rows.Add([pscustomobject][ordered]@{
                        source_relative_path = $path
                        relative_path = $path
                        size_bytes = [long]4
                        sha256 = ('a' * 64)
                    })
            }
            $owners[$ownerId] = $rows.ToArray()
            $ownerIndex++
        }
        $metrics = [ordered]@{ path_probes = [long]0 }
        $plan = @(& $module {
                param($Rows, $Metrics)
                New-M0QtShellCompositeOwnershipPlan -OwnerEntries $Rows -Metrics $Metrics
            } $owners $metrics)
        Assert-Equal $plan.Count 1200 'Instrumented ownership plan lost leaves.'
        Assert-True ([long]$metrics.path_probes -le 19200) `
            "Ownership planning exceeded the O(n * path-depth) probe budget: $($metrics.path_probes)."
    }

    Test-Case 'tree snapshots reject ADS on the root and every nested directory' {
        foreach ($caseId in @('root', 'nested')) {
            $root = Join-Path $testRoot "ads-$caseId"
            $nested = Join-Path $root 'nested'
            [IO.Directory]::CreateDirectory($nested) | Out-Null
            Write-TestBytes (Join-Path $nested 'leaf.bin') ([byte[]]@(1, 2, 3))
            $adsTarget = if ($caseId -ceq 'root') { $root } else { $nested }
            [IO.File]::WriteAllText("${adsTarget}:composite-test", 'ads', [Text.UTF8Encoding]::new($false))
            Assert-Rejected {
                & $module {
                    param($TreeRoot)
                    Get-M0TreeSnapshot -TreeRoot $TreeRoot -Context 'ADS fixture'
                } $root
            } 'ADS|alternate data stream|stream'
        }
    }

    Test-Case 'tree snapshots retain directory anchors and close a post-sweep namespace mutation' {
        $root = Join-Path $testRoot 'snapshot-anchor'
        $nested = Join-Path $root 'nested'
        $victim = Join-Path $testRoot 'snapshot-victim'
        [IO.Directory]::CreateDirectory($nested) | Out-Null
        [IO.Directory]::CreateDirectory($victim) | Out-Null
        Write-TestBytes (Join-Path $nested 'leaf.bin') ([byte[]]@(1, 2, 3))
        Write-TestBytes (Join-Path $victim 'victim.bin') ([byte[]]@(9, 9, 9))
        $displaced = Join-Path $testRoot 'snapshot-displaced'
        $probeState = [pscustomobject]@{ blocked = $false }
        $swapProbe = {
            param($Snapshot)
            try {
                [IO.Directory]::Move($nested, $displaced)
                New-Item -ItemType Junction -Path $nested -Target $victim -ErrorAction Stop | Out-Null
            }
            catch { $probeState.blocked = $true }
        }.GetNewClosure()
        $snapshot = & $module {
            param($Root, $Probe)
            Get-M0TreeSnapshot -TreeRoot $Root -Context 'anchored snapshot fixture' `
                -AfterSweep $Probe
        } $root $swapProbe
        Assert-True $probeState.blocked 'A retained directory anchor allowed a post-sweep rename/junction swap.'
        Assert-Equal ([long]$snapshot.file_count) ([long]1) 'Anchored snapshot lost its owned leaf.'
        Assert-True ([IO.File]::Exists((Join-Path $victim 'victim.bin'))) `
            'Snapshot namespace hardening touched the junction victim.'

        foreach ($mutation in @('extra', 'ads')) {
            $mutationRoot = Join-Path $testRoot "snapshot-$mutation-mutation"
            $mutationNested = Join-Path $mutationRoot 'nested'
            [IO.Directory]::CreateDirectory($mutationNested) | Out-Null
            Write-TestBytes (Join-Path $mutationNested 'leaf.bin') ([byte[]]@(4, 5, 6))
            $probe = if ($mutation -ceq 'extra') {
                { param($Snapshot) Write-TestBytes (Join-Path $mutationNested 'extra.bin') ([byte[]]@(7)) }.GetNewClosure()
            }
            else {
                {
                    param($Snapshot)
                    [IO.File]::WriteAllText(
                        "${mutationNested}:post-sweep", 'ads', [Text.UTF8Encoding]::new($false))
                }.GetNewClosure()
            }
            Assert-Rejected {
                & $module {
                    param($Root, $Probe)
                    Get-M0TreeSnapshot -TreeRoot $Root -Context 'post-sweep mutation fixture' `
                        -AfterSweep $Probe
                } $mutationRoot $probe
            } 'snapshot|tree|extra|ADS|stream|changed|envelope'
        }
    }

    Test-Case 'post-native SDK validation is exact and precedes both manifest and receipt publication' {
        $sdkRoot = Join-Path $testRoot 'exact-sdk'
        [IO.Directory]::CreateDirectory($sdkRoot) | Out-Null
        $plan = [Collections.Generic.List[object]]::new()
        $value = [byte]10
        foreach ($binding in @(
                @('qtbase-refresh-v2', 'bin/Core.dll'),
                @('qtdeclarative', 'qml/Quick.dll'),
                @('qtsvg', 'plugins/svg.dll'),
                @('d3d', 'd3dcompiler_47.dll'))) {
            $row = New-TestOwnedRow -OwnerId $binding[0] -RelativePath $binding[1] -Value $value
            Write-TestBytes (Join-Path $sdkRoot $row.relative_path) $row.bytes
            $row.PSObject.Properties.Remove('bytes')
            $plan.Add($row)
            $value += 10
        }
        $leases = [Collections.Generic.List[object]]::new()
        try {
            $snapshot = & $module {
                param($Root, $Rows, $LeaseSink)
                Assert-M0ExactSdkTree -SdkRoot $Root -Plan $Rows -LeaseSink $LeaseSink
            } $sdkRoot $plan.ToArray() $leases
            Assert-Equal ([long]$snapshot.file_count) ([long]4) 'Exact SDK snapshot lost a leaf.'
        }
        finally {
            foreach ($lease in $leases) {
                if ($null -ne $lease.PSObject.Properties['stream']) { $lease.stream.Dispose() }
                if ($null -ne $lease.PSObject.Properties['handle']) { $lease.handle.Dispose() }
            }
        }
        Write-TestBytes (Join-Path $sdkRoot 'unexpected.bin') ([byte[]]@(9))
        Assert-Rejected {
            & $module {
                param($Root, $Rows)
                Assert-M0ExactSdkTree -SdkRoot $Root -Plan $Rows
            } $sdkRoot $plan.ToArray()
        } 'SDK|extra|missing|exact|physical'

        $tokens = $null
        $errors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$tokens, [ref]$errors)
        Assert-Equal @($errors).Count 0 'Composite module must parse for publication-order inspection.'
        $invoke = @($ast.FindAll({
                    param($node)
                    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                    $node.Name -ceq 'Invoke-M0QtShellComposite'
                }, $true))[0]
        $commands = @($invoke.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
        $sdkChecks = @($commands | Where-Object { $_.GetCommandName() -ceq 'Assert-M0ExactSdkTree' })
        $manifestWrites = @($commands | Where-Object {
                $_.GetCommandName() -ceq 'Write-M0CreateNewFile' -and $_.Extent.Text -match 'manifest'
            })
        $receiptWrites = @($commands | Where-Object {
                $_.GetCommandName() -ceq 'Write-M0CreateNewFile' -and $_.Extent.Text -match 'receipt'
            })
        Assert-True ($sdkChecks.Count -ge 2) 'SDK must be revalidated before both evidence commit points.'
        Assert-True ($manifestWrites.Count -eq 1 -and $receiptWrites.Count -eq 1) `
            'Manifest and receipt must each have one publication command.'
        Assert-True ($sdkChecks[0].Extent.StartOffset -lt $manifestWrites[0].Extent.StartOffset -and
            $sdkChecks[-1].Extent.StartOffset -lt $receiptWrites[0].Extent.StartOffset -and
            $manifestWrites[0].Extent.StartOffset -lt $receiptWrites[0].Extent.StartOffset) `
            'Exact SDK validation/publication order drifted; receipt must remain last.'
    }

    Test-Case 'publication uses validated pending bytes and atomic rename without replacement' {
        $root = Join-Path $testRoot 'atomic-publication'
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $destination = Join-Path $root 'manifest.json'
        $observedPending = [Collections.Generic.List[string]]::new()
        $probe = {
            param($PendingPath, $FinalPath)
            if ([IO.File]::Exists($FinalPath)) { throw 'Final path appeared before pending validation.' }
            if (-not [IO.File]::Exists($PendingPath)) { throw 'Pending path was not flushed before validation.' }
            $observedPending.Add($PendingPath)
        }.GetNewClosure()
        $publication = & $module {
            param($Path, $Bytes, $Root, $Probe)
            Write-M0CreateNewFile -LiteralPath $Path -Bytes $Bytes -BoundaryRoot $Root `
                -Context 'atomic test manifest' -AfterFlush $Probe
        } $destination ([Text.Encoding]::UTF8.GetBytes("manifest`n")) $root $probe
        try {
            Assert-Equal $observedPending.Count 1 'Publication did not expose exactly one flushed pending file.'
            Assert-True (-not [IO.File]::Exists($observedPending[0])) 'Pending file survived atomic publication.'
            Assert-Equal ([IO.File]::ReadAllText($destination)) "manifest`n" 'Published bytes drifted.'
        }
        finally { $publication.lease.stream.Dispose() }

        $preserved = [Text.Encoding]::UTF8.GetBytes("preserve`n")
        Write-TestBytes (Join-Path $root 'receipt.json') $preserved
        Assert-Rejected {
            & $module {
                param($Path, $Bytes, $Root)
                Write-M0CreateNewFile -LiteralPath $Path -Bytes $Bytes -BoundaryRoot $Root `
                    -Context 'no replace receipt'
            } (Join-Path $root 'receipt.json') ([byte[]]@(1, 2, 3)) $root
        } 'exist|replace|publication|destination'
        Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes((Join-Path $root 'receipt.json')))) `
            ([Convert]::ToBase64String($preserved)) 'No-replace publication changed the existing receipt.'

        $tamperedDestination = Join-Path $root 'tampered.json'
        $tamper = {
            param($PendingPath, $FinalPath, $PendingStream)
            $PendingStream.Position = $PendingStream.Length
            $PendingStream.WriteByte([byte]0xFF)
            $PendingStream.Flush($true)
        }
        Assert-Rejected {
            & $module {
                param($Path, $Bytes, $Root, $Probe)
                Write-M0CreateNewFile -LiteralPath $Path -Bytes $Bytes -BoundaryRoot $Root `
                    -Context 'tampered pending' -AfterFlush $Probe
            } $tamperedDestination ([byte[]]@(4, 5, 6)) $root $tamper
        } 'pending|hash|size|drift|changed|publication'
        Assert-True (-not [IO.File]::Exists($tamperedDestination)) `
            'Tampered pending bytes were promoted to the final path.'
        Assert-Equal @(Get-ChildItem -LiteralPath $root -Force -Filter '*.pending').Count 0 `
            'Failed publication left a pending leaf.'
    }

    Test-Case 'publication anchors its parent and pending identity through the handle-relative commit' {
        foreach ($attack in @('parent', 'pending')) {
            $root = Join-Path $testRoot "atomic-$attack-anchor"
            $parent = Join-Path $root 'owned-parent'
            $victim = Join-Path $root 'victim'
            [IO.Directory]::CreateDirectory($parent) | Out-Null
            [IO.Directory]::CreateDirectory($victim) | Out-Null
            $victimLeaf = Join-Path $victim 'victim.bin'
            Write-TestBytes $victimLeaf ([byte[]]@(9, 8, 7, 6))
            $destination = Join-Path $parent 'manifest.json'
            $displaced = Join-Path $root "displaced-$attack"
            $probeState = [pscustomobject]@{ blocked = $false }
            $probe = {
                param($PendingPath, $FinalPath)
                try {
                    if ($attack -ceq 'parent') {
                        [IO.Directory]::Move($parent, $displaced)
                        New-Item -ItemType Junction -Path $parent -Target $victim -ErrorAction Stop | Out-Null
                    }
                    else {
                        [IO.File]::Move($PendingPath, $displaced, $false)
                        [IO.File]::Copy($victimLeaf, $PendingPath, $false)
                    }
                }
                catch { $probeState.blocked = $true }
            }.GetNewClosure()
            $publication = & $module {
                param($Path, $Bytes, $Boundary, $Probe)
                Write-M0CreateNewFile -LiteralPath $Path -Bytes $Bytes -BoundaryRoot $Boundary `
                    -Context 'anchored atomic publication' -BeforeCommit $Probe
            } $destination ([Text.Encoding]::UTF8.GetBytes("manifest-$attack`n")) $root $probe
            try {
                Assert-True $probeState.blocked "Atomic publication allowed the $attack namespace swap."
                Assert-Equal ([IO.File]::ReadAllText($destination)) "manifest-$attack`n" `
                    'Atomic publication did not commit its retained pending identity.'
                Assert-Equal ([Convert]::ToBase64String([IO.File]::ReadAllBytes($victimLeaf))) `
                    ([Convert]::ToBase64String([byte[]]@(9, 8, 7, 6))) `
                    'Atomic publication touched an external victim.'
            }
            finally { $publication.lease.stream.Dispose() }
        }
    }

    Test-Case 'owned cleanup anchors its root and successful cleanup leaves no scratch' {
        $parent = Join-Path $testRoot 'cleanup-parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        $owned = Join-Path $parent 'scratch'
        [IO.Directory]::CreateDirectory($owned) | Out-Null
        Write-TestBytes (Join-Path $owned 'owned.bin') ([byte[]]@(1, 2, 3))
        $state = & $module {
            param($Root, $Boundary)
            $result = New-M0OwnedTreeState -Root $Root -BoundaryRoot $Boundary -Context 'owned scratch'
            $null = Get-M0TreeSnapshot -TreeRoot $Root -Context 'owned scratch snapshot' `
                -DirectoryLedger $result.directory_ledger -FileLedger $result.file_ledger `
                -RootAnchor $result.root_anchor
            return $result
        } $owned $parent
        $preserved = Join-Path $parent 'original-preserved'
        $blocked = $false
        try { [IO.Directory]::Move($owned, $preserved) } catch { $blocked = $true }
        Assert-True $blocked 'The retained owned-root anchor allowed substitution before cleanup.'
        & $module {
            param($State)
            Remove-M0OwnedTree -LiteralPath $State.root -ExpectedExactPath $State.root `
                -BoundaryRoot $State.boundary_root -RootAnchor $State.root_anchor `
                -DirectoryLedger $State.directory_ledger -FileLedger $State.file_ledger
        } $state
        Assert-True (-not [IO.Directory]::Exists($owned)) 'Successful cleanup left its scratch root.'
        Assert-True (-not [IO.Directory]::Exists($preserved)) 'Root-anchor probe displaced the owned root.'
        Assert-Equal @(Get-ChildItem -LiteralPath $parent -Force |
                Where-Object Name -Like '*.delete-pending-*').Count 0 `
            'Successful cleanup left a quarantine directory.'
    }

    Test-Case 'published-root adoption requires the authenticated runner identity' {
        $parent = Join-Path $testRoot 'publisher-handoff-parent'
        $published = Join-Path $parent 'published'
        $victim = Join-Path $parent 'victim'
        [IO.Directory]::CreateDirectory($published) | Out-Null
        [IO.Directory]::CreateDirectory($victim) | Out-Null
        $expected = & $module {
            param($Path, $Boundary)
            Get-M0DirectoryIdentity $Path $Boundary 'runner handoff fixture'
        } $published $parent
        [IO.Directory]::Delete($published)
        [IO.Directory]::Move($victim, $published)
        Assert-Rejected {
            & $module {
                param($Path, $Boundary, $Identity)
                New-M0OwnedTreeState -Root $Path -BoundaryRoot $Boundary `
                    -Context 'published RunRoot fixture' -ExpectedIdentity $Identity
            } $published $parent $expected
        } 'identity|publisher|handoff'
        Assert-True ([IO.Directory]::Exists($published)) `
            'Rejected publisher handoff removed the substituted victim.'
    }

    Test-Case 'atomic owned-directory creation closes create-to-anchor swaps and preserves victims' {
        foreach ($kind in @('archive-scratch', 'extract-root')) {
            $parent = Join-Path $testRoot "atomic-create-$kind"
            $created = Join-Path $parent 'created'
            $victim = Join-Path $parent 'victim'
            $displaced = Join-Path $parent 'displaced'
            [IO.Directory]::CreateDirectory($parent) | Out-Null
            [IO.Directory]::CreateDirectory($victim) | Out-Null
            Write-TestBytes (Join-Path $victim 'victim.bin') ([byte[]]@(7, 7, 7))
            $probe = [pscustomobject]@{ blocked = $false }
            $attack = {
                param($Path, $Anchor)
                try {
                    [IO.Directory]::Move($Path, $displaced)
                    [IO.Directory]::Move($victim, $Path)
                }
                catch { $probe.blocked = $true }
            }.GetNewClosure()
            $state = & $module {
                param($Path, $Boundary, $Hook)
                New-M0AtomicOwnedTreeState -Root $Path -BoundaryRoot $Boundary `
                    -Context 'atomic owned fixture' -AfterCreate $Hook
            } $created $parent $attack
            try {
                Assert-True $probe.blocked "$kind allowed a swap between create and anchor."
                Assert-True ([IO.File]::Exists((Join-Path $victim 'victim.bin'))) `
                    "$kind touched the external victim."
            }
            finally { $state.root_anchor.handle.Dispose() }
        }
    }

    Test-Case 'failed atomic directory validation deletes only the authenticated new directory by handle' {
        $parent = Join-Path $testRoot 'atomic-create-failure-cleanup'
        $created = Join-Path $parent 'created'
        $victim = Join-Path $parent 'victim'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        [IO.Directory]::CreateDirectory($victim) | Out-Null
        Write-TestBytes (Join-Path $victim 'victim.bin') ([byte[]]@(8, 8, 8))
        $failAfterCreate = {
            param($Path, $Anchor)
            throw 'causal post-create validation failure'
        }
        Assert-Rejected {
            & $module {
                param($Path, $Boundary, $Hook)
                New-M0AtomicOwnedTreeState -Root $Path -BoundaryRoot $Boundary `
                    -Context 'failed atomic owned fixture' -AfterCreate $Hook
            } $created $parent $failAfterCreate
        } 'causal post-create validation failure'
        Assert-True (-not [IO.Directory]::Exists($created)) `
            'Failed atomic creation left its authenticated directory orphaned.'
        Assert-True ([IO.File]::Exists((Join-Path $victim 'victim.bin'))) `
            'Failed atomic creation cleanup touched an external victim.'
    }

    Test-Case 'atomic child creation reuses a retained delete-capable parent anchor' {
        $parent = Join-Path $testRoot 'retained-parent-child'
        $child = Join-Path $parent 'child'
        $state = & $module {
            param($Path, $Boundary)
            New-M0AtomicOwnedTreeState -Root $Path -BoundaryRoot $Boundary `
                -Context 'retained parent fixture'
        } $parent $testRoot
        $childAnchor = $null
        try {
            $childAnchor = & $module {
                param($Path, $Boundary, $ParentAnchor)
                New-M0AtomicDirectoryAnchor -LiteralPath $Path -BoundaryRoot $Boundary `
                    -Context 'retained child fixture' -ParentAnchor $ParentAnchor
            } $child $parent $state.root_anchor
            Assert-True ([IO.Directory]::Exists($child)) `
                'Atomic child creation failed under its retained parent anchor.'
        }
        finally {
            if ($null -ne $childAnchor) { $childAnchor.handle.Dispose() }
            $state.root_anchor.handle.Dispose()
        }
    }

    Test-Case 'create-new publication atomically anchors its previously missing parent' {
        $root = Join-Path $testRoot 'atomic-missing-parent'
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $published = & $module {
            param($Path, $Boundary)
            Write-M0CreateNewFile -LiteralPath $Path -Bytes ([byte[]]@(1, 2)) `
                -BoundaryRoot $Boundary -Context 'missing parent fixture'
        } (Join-Path $root 'inventories/owner.tsv') $root
        try { Assert-True ([IO.File]::Exists((Join-Path $root 'inventories/owner.tsv'))) `
                'Create-new publication did not publish through its atomically created parent.' }
        finally { $published.lease.stream.Dispose() }
    }

    Test-Case 'extracted archive validation reuses its delete-capable authenticated root anchor' {
        $parent = Join-Path $testRoot 'extract-anchor-reuse'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        $state = & $module {
            param($Root, $Boundary)
            New-M0AtomicOwnedTreeState -Root $Root -BoundaryRoot $Boundary `
                -Context 'extract anchor reuse fixture'
        } (Join-Path $parent 'owner') $parent
        $leaf = Join-Path $state.root 'leaf.bin'
        Write-TestBytes $leaf ([byte[]]@(1, 2, 3))
        try {
            $snapshot = & $module {
                param($State)
                $leaseSink = [Collections.Generic.List[object]]::new()
                $leaseSink.Add($State.root_anchor)
                $members = @([pscustomobject]@{ path = 'leaf.bin'; kind = 'file'; size_bytes = [long]3 })
                $spec = [pscustomobject]@{
                    owner_id = 'fixture'; expected_file_count = [long]1
                    expected_total_size_bytes = [long]3
                }
                try {
                    Assert-M0ExtractedArchive -Root $State.root -Members $members `
                        -ArchiveSpec $spec -LeaseSink $leaseSink `
                        -DirectoryLedger $State.directory_ledger -FileLedger $State.file_ledger `
                        -RootAnchor $State.root_anchor
                }
                finally { Close-M0Leases $leaseSink }
            } $state
            Assert-Equal ([long]$snapshot.file_count) ([long]1) `
                'Extracted archive snapshot lost its authenticated leaf.'
        }
        finally {
            if (-not $state.root_anchor.handle.IsClosed) { $state.root_anchor.handle.Dispose() }
        }
    }

    Test-Case 'create-new publication creates a missing parent under a retained root envelope' {
        $root = Join-Path $testRoot 'retained-publication-root'
        $state = & $module {
            param($Path, $Boundary)
            New-M0AtomicOwnedTreeState -Root $Path -BoundaryRoot $Boundary `
                -Context 'retained publication fixture'
        } $root $testRoot
        $published = $null
        try {
            $published = & $module {
                param($Path, $Boundary, $ParentAnchor)
                Write-M0CreateNewFile -LiteralPath $Path -Bytes ([byte[]]@(3, 4)) `
                    -BoundaryRoot $Boundary -Context 'retained parent publication fixture' `
                    -MissingParentAnchor $ParentAnchor
            } (Join-Path $root 'evidence/owner.tsv') $root $state.root_anchor
            Assert-True ([IO.File]::Exists((Join-Path $root 'evidence/owner.tsv'))) `
                'Create-new publication failed below a retained root envelope.'
        }
        finally {
            if ($null -ne $published) { $published.lease.stream.Dispose() }
            $state.root_anchor.handle.Dispose()
        }
    }

    Test-Case 'receipt commit resweeps its immutable SDK envelope after a concurrent mutation' {
        $root = Join-Path $testRoot 'immutable-commit-tree'
        [IO.Directory]::CreateDirectory((Join-Path $root 'sdk')) | Out-Null
        Write-TestBytes (Join-Path $root 'sdk/leaf.bin') ([byte[]]@(1, 2, 3))
        $receipt = Join-Path $root 'receipt.json'
        $beforeCommit = {
            param($Pending, $Final)
            Write-TestBytes (Join-Path $root 'sdk/concurrent.bin') ([byte[]]@(9))
        }.GetNewClosure()
        $validate = {
            $null = & $module {
                param($Path)
                $snapshot = Get-M0TreeSnapshot -TreeRoot $Path -Context 'receipt commit SDK envelope'
                if ($snapshot.file_count -ne 1) { throw 'Receipt commit SDK envelope changed.' }
            } (Join-Path $root 'sdk')
        }.GetNewClosure()
        Assert-Rejected {
            & $module {
                param($Path, $Boundary, $Mutation, $Validation)
                Write-M0CreateNewFile -LiteralPath $Path -Bytes ([byte[]]@(1)) `
                    -BoundaryRoot $Boundary -Context 'receipt commit fixture' `
                    -BeforeCommit $Mutation -ValidateCommitEnvelope $Validation
            } $receipt $root $beforeCommit $validate
        } 'SDK|envelope|changed'
        Assert-True (-not [IO.File]::Exists($receipt)) `
            'Receipt committed after the concurrent SDK mutation.'
    }

    Test-Case 'owned cleanup audits an exact ledger and retains every handle before deletion' {
        $parent = Join-Path $testRoot 'ledger-cleanup-parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null

        $replaced = Join-Path $parent 'replaced-scratch'
        [IO.Directory]::CreateDirectory((Join-Path $replaced 'nested')) | Out-Null
        Write-TestBytes (Join-Path $replaced 'nested/owned.bin') ([byte[]]@(1, 2, 3))
        $replacedState = & $module {
            param($Root, $Boundary)
            $state = New-M0OwnedTreeState -Root $Root -BoundaryRoot $Boundary -Context 'replaced tree'
            $null = Get-M0TreeSnapshot -TreeRoot $Root -Context 'replaced tree snapshot' `
                -DirectoryLedger $state.directory_ledger -FileLedger $state.file_ledger `
                -RootAnchor $state.root_anchor
            return $state
        } $replaced $parent
        $displacedLeaf = Join-Path $parent 'displaced-owned.bin'
        [IO.File]::Move((Join-Path $replaced 'nested/owned.bin'), $displacedLeaf)
        Write-TestBytes (Join-Path $replaced 'nested/owned.bin') ([byte[]]@(9, 9, 9))
        try {
            Assert-Rejected {
                & $module {
                    param($State)
                    Remove-M0OwnedTree -LiteralPath $State.root -ExpectedExactPath $State.root `
                        -BoundaryRoot $State.boundary_root -RootAnchor $State.root_anchor `
                        -DirectoryLedger $State.directory_ledger -FileLedger $State.file_ledger
                } $replacedState
            } 'ledger|identity|replaced|unowned|cleanup'
            Assert-True ([IO.File]::Exists((Join-Path $replaced 'nested/owned.bin'))) `
                'Ledger mismatch cleanup deleted a substituted child.'
            Assert-True ([IO.File]::Exists($displacedLeaf)) `
                'Ledger mismatch cleanup followed and deleted the displaced owned identity.'
        }
        finally { $replacedState.root_anchor.handle.Dispose() }

        $junctionScratch = Join-Path $parent 'junction-scratch'
        $junctionNested = Join-Path $junctionScratch 'nested'
        $junctionVictim = Join-Path $parent 'junction-victim'
        [IO.Directory]::CreateDirectory($junctionNested) | Out-Null
        [IO.Directory]::CreateDirectory($junctionVictim) | Out-Null
        Write-TestBytes (Join-Path $junctionNested 'owned.bin') ([byte[]]@(2, 2, 2))
        Write-TestBytes (Join-Path $junctionVictim 'victim.bin') ([byte[]]@(8, 8, 8))
        $junctionState = & $module {
            param($Root, $Boundary)
            $state = New-M0OwnedTreeState -Root $Root -BoundaryRoot $Boundary -Context 'junction tree'
            $null = Get-M0TreeSnapshot -TreeRoot $Root -Context 'junction tree snapshot' `
                -DirectoryLedger $state.directory_ledger -FileLedger $state.file_ledger `
                -RootAnchor $state.root_anchor
            return $state
        } $junctionScratch $parent
        $displacedDirectory = Join-Path $parent 'junction-owned-preserved'
        [IO.Directory]::Move($junctionNested, $displacedDirectory)
        New-Item -ItemType Junction -Path $junctionNested -Target $junctionVictim | Out-Null
        try {
            Assert-Rejected {
                & $module {
                    param($State)
                    Remove-M0OwnedTree -LiteralPath $State.root -ExpectedExactPath $State.root `
                        -BoundaryRoot $State.boundary_root -RootAnchor $State.root_anchor `
                        -DirectoryLedger $State.directory_ledger -FileLedger $State.file_ledger
                } $junctionState
            } 'ledger|reparse|junction|replaced|cleanup'
            Assert-True ([IO.File]::Exists((Join-Path $junctionVictim 'victim.bin'))) `
                'Cleanup traversed and deleted a substituted junction victim.'
            Assert-True ([IO.File]::Exists((Join-Path $displacedDirectory 'owned.bin'))) `
                'Cleanup followed and deleted the displaced owned directory.'
        }
        finally {
            $junctionState.root_anchor.handle.Dispose()
            if ([IO.Directory]::Exists($junctionNested)) { [IO.Directory]::Delete($junctionNested, $false) }
        }

        $success = Join-Path $parent 'audited-scratch'
        [IO.Directory]::CreateDirectory((Join-Path $success 'nested')) | Out-Null
        Write-TestBytes (Join-Path $success 'nested/leaf.bin') ([byte[]]@(4, 5, 6))
        $successState = & $module {
            param($Root, $Boundary)
            $state = New-M0OwnedTreeState -Root $Root -BoundaryRoot $Boundary -Context 'audited tree'
            $null = Get-M0TreeSnapshot -TreeRoot $Root -Context 'audited tree snapshot' `
                -DirectoryLedger $state.directory_ledger -FileLedger $state.file_ledger `
                -RootAnchor $state.root_anchor
            return $state
        } $success $parent
        $attackTarget = Join-Path $parent 'cleanup-attack-target.bin'
        $probeState = [pscustomobject]@{ blocked = $false }
        $probe = {
            param($Audit)
            try { [IO.File]::Move((Join-Path $success 'nested/leaf.bin'), $attackTarget, $false) }
            catch { $probeState.blocked = $true }
        }.GetNewClosure()
        & $module {
            param($State, $Probe)
            Remove-M0OwnedTree -LiteralPath $State.root -ExpectedExactPath $State.root `
                -BoundaryRoot $State.boundary_root -RootAnchor $State.root_anchor `
                -DirectoryLedger $State.directory_ledger -FileLedger $State.file_ledger `
                -BeforeDelete $Probe
        } $successState $probe
        Assert-True $probeState.blocked 'Cleanup allowed a child swap after its retained-handle audit.'
        Assert-True (-not [IO.Directory]::Exists($success)) 'Audited cleanup left its owned scratch root.'
        Assert-True (-not [IO.File]::Exists($attackTarget)) 'Audited cleanup moved a child outside its root.'
    }

    Test-Case 'receipt shape requires external contract binding and fail-closed limits' {
        $contract = Read-Json $contractPath
        $receipt = [pscustomobject][ordered]@{
            schema_version = [long] 2
            receipt_id = 'm0.2-qt-shell-composite-sdk-v2-receipt'
            composite_id = $contract.composite_id
            captured_utc = '2026-08-28T12:00:00.0000000+00:00'
            contract_sha256 = $expectedContractSha256
            execution = [pscustomobject][ordered]@{ exit_code = [long] 0; stdout = ''; stderr = '' }
            qtbase_receipt = [pscustomobject][ordered]@{
                relative_path = $contract.provenance.qtbase_refresh.receipt_relative_path
                size_bytes = [long]$contract.provenance.qtbase_refresh.receipt_size_bytes
                sha256 = $contract.provenance.qtbase_refresh.receipt_sha256
            }
            sdk = [pscustomobject][ordered]@{
                root_relative_path = 'sdk'; file_count = 4; total_size_bytes = 16;
                inventory_relative_path = 'evidence/composite-inventory.tsv';
                inventory_size_bytes = 200; inventory_sha256 = ('e' * 64)
            }
            evidence = [pscustomobject][ordered]@{
                relative_path = 'evidence/qt-shell-composite-manifest.v2.json';
                size_bytes = 300; sha256 = ('f' * 64)
            }
            limits = @($contract.limits)
        }
        $null = & $module {
            param($Receipt, $Contract, $Hash)
            Assert-M0QtShellCompositeReceiptDocument -Receipt $Receipt -Contract $Contract `
                -ExpectedContractSha256 $Hash
        } $receipt $contract $expectedContractSha256

        $receipt.contract_sha256 = '0' * 64
        Assert-Rejected {
            & $module {
                param($Receipt, $Contract, $Hash)
                Assert-M0QtShellCompositeReceiptDocument -Receipt $Receipt -Contract $Contract `
                    -ExpectedContractSha256 $Hash
            } $receipt $contract $expectedContractSha256
        } 'contract|anchor|sha256'
    }

    Test-Case 'verification RunRoot envelope rejects a child injected after first enumeration' {
        $runRoot = Join-Path $testRoot 'verify-runroot-envelope'
        [IO.Directory]::CreateDirectory((Join-Path $runRoot 'sdk')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $runRoot 'evidence')) | Out-Null
        $inject = {
            param($Snapshot)
            [IO.Directory]::CreateDirectory((Join-Path $runRoot 'extra')) | Out-Null
        }.GetNewClosure()
        Assert-Rejected {
            & $module {
                param($Root, $Hook)
                Assert-M0RunRootEnvelope -RunRoot $Root -AfterSweep $Hook
            } $runRoot $inject
        } 'final resweep detected an extra child'
    }

    Test-Case 'verification RunRoot envelope remains anchored through the external final revalidation' {
        $runRoot = Join-Path $testRoot 'verify-runroot-retained-envelope'
        [IO.Directory]::CreateDirectory((Join-Path $runRoot 'sdk')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $runRoot 'evidence')) | Out-Null
        $leases = [Collections.Generic.List[object]]::new()
        try {
            & $module {
                param($Root, $LeaseSink)
                Assert-M0RunRootEnvelope -RunRoot $Root -LeaseSink $LeaseSink
            } $runRoot $leases
            [IO.Directory]::CreateDirectory((Join-Path $runRoot 'extra')) | Out-Null
            Assert-Rejected {
                & $module {
                    param($LeaseSink)
                    Assert-M0LeasesUnchanged $LeaseSink 'RunRoot retained-envelope fixture'
                } $leases
            } 'RunRoot envelope contains an extra or replaced child'
        }
        finally {
            & $module { param($LeaseSink) Close-M0Leases $LeaseSink } $leases
        }
    }

    Test-Case 'minimal integration crosses extracted snapshots native publication and final evidence' {
        $fixtureRoot = Join-Path $testRoot 'minimal-real-handle-integration'
        $fixtureRepository = Join-Path $fixtureRoot 'repository'
        $runParent = Join-Path $fixtureRepository 'q'
        $parentInstall = Join-Path $fixtureRepository 'parent-install'
        [IO.Directory]::CreateDirectory($runParent) | Out-Null
        [IO.Directory]::CreateDirectory($parentInstall) | Out-Null

        $contract = Read-Json $contractPath
        $fixtureContractPath = Join-Path $fixtureRepository 'fixture-contract.json'
        [IO.File]::WriteAllText($fixtureContractPath, '{}', [Text.UTF8Encoding]::new($false))
        $fixtureContractSha256 = Get-Sha256 $fixtureContractPath
        $fixtureEntrypoint = $PSCommandPath
        $fixtureRunner = $nativeModulePath
        $fixtureOrchestrator = $modulePath
        $fixtureQtbaseHash = [string]$contract.provenance.qtbase_refresh.receipt_sha256
        $utf8 = [Text.UTF8Encoding]::new($false)

        $ownerRows = [ordered]@{
            'qtbase-refresh-v2' = [Collections.Generic.List[object]]::new()
            qtdeclarative = [Collections.Generic.List[object]]::new()
            qtsvg = [Collections.Generic.List[object]]::new()
            d3d = [Collections.Generic.List[object]]::new()
        }
        $value = [byte]1
        foreach ($artifact in @($contract.required_artifacts)) {
            $bytes = [byte[]]@($value, ($value + 1), ($value + 2))
            $digest = [Convert]::ToHexString(
                [Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
            $artifact.size_bytes = [long]$bytes.Length
            $artifact.sha256 = $digest
            $row = [pscustomobject][ordered]@{
                owner_id = [string]$artifact.owner_id
                source_relative_path = [string]$artifact.relative_path
                relative_path = [string]$artifact.relative_path
                size_bytes = [long]$bytes.Length
                sha256 = $digest
                bytes = $bytes
            }
            $ownerRows[[string]$artifact.owner_id].Add($row)
            $value++
        }

        $parentInventory = [Text.StringBuilder]::new()
        foreach ($row in $ownerRows['qtbase-refresh-v2']) {
            Write-TestBytes (Join-Path $parentInstall $row.source_relative_path) $row.bytes
            [void]$parentInventory.Append($row.source_relative_path).Append("`t").Append(
                $row.size_bytes).Append("`t").Append($row.sha256).Append("`n")
        }
        $parentInventoryPath = Join-Path $fixtureRepository 'parent-install.tsv'
        [IO.File]::WriteAllText($parentInventoryPath, $parentInventory.ToString(), $utf8)
        $contract.provenance.qtbase_refresh.install_file_count =
            [long]$ownerRows['qtbase-refresh-v2'].Count
        $contract.provenance.qtbase_refresh.install_total_size_bytes = [long](
            ($ownerRows['qtbase-refresh-v2'] | Measure-Object -Property size_bytes -Sum).Sum)

        $extractorPath = Join-Path $fixtureRepository 'inputs/fake-7za.exe'
        Write-TestBytes $extractorPath ([byte[]]@(7, 0))
        $contract.inputs.extractor.relative_path = 'inputs/fake-7za.exe'
        $contract.inputs.extractor.size_bytes = [long]2
        $contract.inputs.extractor.sha256 = Get-Sha256 $extractorPath
        $archiveFixtures = [ordered]@{}
        for ($index = 0; $index -lt @($contract.inputs.archives).Count; $index++) {
            $archive = $contract.inputs.archives[$index]
            $ownerId = [string]$archive.owner_id
            $relativeArchive = "inputs/$ownerId.archive"
            $archivePath = Join-Path $fixtureRepository $relativeArchive
            Write-TestBytes $archivePath ([byte[]]@([byte](20 + $index)))
            $archive.relative_path = $relativeArchive
            $archive.size_bytes = [long]1
            $archive.sha256 = Get-Sha256 $archivePath
            $archive.expected_file_count = [long]$ownerRows[$ownerId].Count
            $archive.expected_total_size_bytes = [long](
                ($ownerRows[$ownerId] | Measure-Object -Property size_bytes -Sum).Sum)
            $archiveFixtures[[IO.Path]::GetFullPath($archivePath)] = @($ownerRows[$ownerId])
        }

        $fixture = [pscustomobject]@{
            contract = $contract
            runner = $fixtureRunner
            orchestrator = $fixtureOrchestrator
            entrypoint = $fixtureEntrypoint
            parent_install = $parentInstall
            parent_inventory = $parentInventoryPath
            archives = $archiveFixtures
            trace = [Collections.Generic.List[string]]::new()
        }
        & $module {
            param($Value)
            $script:CompositeIntegrationFixture = $Value
            Set-Item Function:script:Assert-M0QtShellCompositeContract -Value {
                param([string] $LiteralPath, [string] $ExpectedSha256)
                return $script:CompositeIntegrationFixture.contract
            }
            Set-Item Function:script:Assert-M0RecipeFiles -Value {
                param($Contract, [string] $RepositoryRoot, $Leases)
                return [ordered]@{
                    orchestrator = $script:CompositeIntegrationFixture.orchestrator
                    runner = $script:CompositeIntegrationFixture.runner
                    entrypoint = $script:CompositeIntegrationFixture.entrypoint
                }
            }
            Set-Item Function:script:Assert-M0HistoricalV1 -Value {
                param($Contract, [string] $RepositoryRoot, $Leases)
            }
            Set-Item Function:script:Assert-M0QtBaseParent -Value {
                param($Contract, [string] $ExpectedReceiptSha256, [string] $RepositoryRoot, $Leases)
                return [pscustomobject]@{
                    install_root = $script:CompositeIntegrationFixture.parent_install
                    inventory_path = $script:CompositeIntegrationFixture.parent_inventory
                }
            }
            Set-Item Function:script:Invoke-M0BoundedProcess -Value {
                param([string] $FilePath, [string[]] $ArgumentList, [string] $WorkingDirectory)
                $archivePath = [IO.Path]::GetFullPath($ArgumentList[-1])
                $rows = @($script:CompositeIntegrationFixture.archives[$archivePath])
                if ($ArgumentList[0] -ceq 'l') {
                    $script:CompositeIntegrationFixture.trace.Add("list:$archivePath")
                    $listing = [Text.StringBuilder]::new()
                    foreach ($row in $rows) {
                        [void]$listing.Append("----------`nPath = ").Append(
                            $row.source_relative_path).Append("`nFolder = -`nSize = ").Append(
                            $row.size_bytes).Append("`n`n")
                    }
                    return [pscustomobject][ordered]@{
                        exit_code = [long]0; stdout = $listing.ToString(); stderr = ''
                    }
                }
                if ($ArgumentList[0] -ceq 'x') {
                    $script:CompositeIntegrationFixture.trace.Add("extract:$archivePath")
                    $output = [string]$ArgumentList[3]
                    if (-not $output.StartsWith('-o', [StringComparison]::Ordinal)) {
                        throw 'Fixture extraction output argument drifted.'
                    }
                    $output = $output.Substring(2)
                    foreach ($row in $rows) {
                        $path = Join-Path $output $row.source_relative_path
                        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
                        [IO.File]::WriteAllBytes($path, [byte[]]$row.bytes)
                    }
                    return [pscustomobject][ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
                }
                throw 'Fixture received an unexpected extractor operation.'
            }
        } $fixture

        $runRoot = Join-Path $runParent 'minimal-integration-run'
        $stopwatch = [Diagnostics.Stopwatch]::StartNew()
        $result = & $module {
            param($ContractPath, $ContractSha256, $QtbaseHash, $Repository, $Output)
            Invoke-M0QtShellComposite `
                -ContractPath $ContractPath `
                -ExpectedContractSha256 $ContractSha256 `
                -ExpectedQtBaseReceiptSha256 $QtbaseHash `
                -RepositoryRoot $Repository `
                -RunRoot $Output
        } $fixtureContractPath $fixtureContractSha256 $fixtureQtbaseHash $fixtureRepository $runRoot
        $stopwatch.Stop()

        Assert-True ($stopwatch.Elapsed -lt [TimeSpan]::FromSeconds(15)) `
            "Minimal integration exceeded its 15 second timeout: $($stopwatch.Elapsed)."
        Assert-Equal @($fixture.trace).Count ([long]6) 'Fixture did not list and extract all three archives.'
        Assert-True ([IO.Directory]::Exists((Join-Path $runRoot 'sdk'))) `
            'Native runner did not publish the minimal SDK.'
        foreach ($relativePath in @(
                $contract.isolation.global_inventory_relative_path,
                $contract.isolation.manifest_relative_path,
                $contract.isolation.receipt_relative_path)) {
            Assert-True ([IO.File]::Exists((Join-Path $runRoot $relativePath))) `
                "Integration evidence is missing: $relativePath"
        }
        foreach ($ownerId in @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')) {
            Assert-True ([IO.File]::Exists((Join-Path $runRoot `
                            "$($contract.isolation.owner_inventory_directory_relative_path)/$ownerId.tsv"))) `
                "Owner evidence is missing: $ownerId"
        }
        Assert-Equal (Get-Sha256 $result.manifest_path) $result.manifest_sha256 `
            'Published manifest hash differs from the public result.'
        Assert-Equal (Get-Sha256 $result.receipt_path) $result.receipt_sha256 `
            'Published receipt hash differs from the public result.'
        Assert-True (-not [IO.Directory]::Exists(
                (Join-Path $runParent '.minimal-integration-run.composite-archives'))) `
            'Successful integration left archive scratch behind.'
        Assert-True (-not [IO.Directory]::Exists(
                (Join-Path $runParent '.minimal-integration-run.composite-native'))) `
            'Successful integration left native scratch behind.'
    }

    Test-Case 'launcher requires external anchors and authenticates both modules before import' {
        $text = [IO.File]::ReadAllText($launcherPath)
        foreach ($needle in @(
                '[Parameter(Mandatory)]',
                '$ExpectedContractSha256',
                '$ExpectedQtBaseReceiptSha256',
                'M0.QtShellComposite.psm1',
                'M0.QtShellCompositeNative.psm1',
                'Import-Module'
            )) {
            Assert-True ($text.Contains($needle)) "Launcher lacks required binding: $needle"
        }
        $recipeAuthentication = $text.IndexOf(
            '-ExpectedSha256 $spec.sha256', [StringComparison]::Ordinal)
        $moduleImport = $text.IndexOf('Import-Module', [StringComparison]::Ordinal)
        Assert-True ($recipeAuthentication -ge 0 -and $moduleImport -ge 0 -and
            $recipeAuthentication -lt $moduleImport) `
            'Launcher imports code before authenticating recipe bytes.'
    }
}
finally {
    if ($null -ne $module) { Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue }
    if ([IO.Directory]::Exists($testRoot)) {
        $fullTestRoot = [IO.Path]::GetFullPath($testRoot)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
        if ([IO.Path]::GetDirectoryName($fullTestRoot).TrimEnd('\') -cne $tempRoot -or
            [IO.Path]::GetFileName($fullTestRoot) -notmatch '^m0-qt-shell-composite-core-[0-9a-f]{32}$') {
            throw 'Refusing to clean a core-test root outside its fixed temporary boundary.'
        }
        [IO.Directory]::Delete($fullTestRoot, $true)
    }
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -ne 0) { exit 1 }
