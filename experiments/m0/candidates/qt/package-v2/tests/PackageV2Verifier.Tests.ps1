#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$modulePath = Join-Path $packageRoot 'M0.QtShellCompositePackage.psm1'
$module = Import-Module $modulePath -Force -PassThru
$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'm0-package-v2-verifier-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$state = @{ Passed = 0; Failed = 0 }

function Test-Case([string] $Name, [scriptblock] $Body) {
    try {
        & $Body
        $state.Passed++
        Write-Host "[PASS] $Name"
    }
    catch {
        $state.Failed++
        Write-Host "[FAIL] $Name :: $($_.Exception.Message)"
    }
}

function Assert-True([bool] $Value, [string] $Message) {
    if (-not $Value) { throw $Message }
}

function Assert-Rejected([scriptblock] $Body, [string] $Context, [string] $ExpectedPattern) {
    try {
        & $Body | Out-Null
    }
    catch {
        if ($_.Exception.Message -clike 'Accepted verifier mutation:*') { throw }
        if ($_.Exception.Message -notmatch $ExpectedPattern) {
            throw "Wrong rejection for ${Context}: $($_.Exception.Message)"
        }
        return
    }
    throw "Accepted verifier mutation: $Context"
}

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-Json([string] $Path, $Value) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path)) | Out-Null
    [IO.File]::WriteAllText(
        $Path,
        (ConvertTo-Json $Value -Depth 100 -Compress),
        [Text.UTF8Encoding]::new($false))
}

function New-Pin([string] $Path, [string] $RelativePath) {
    return [pscustomobject][ordered]@{
        relative_path = $RelativePath
        size_bytes = [long]([IO.FileInfo]::new($Path).Length)
        sha256 = Get-Sha256 $Path
    }
}

function Update-ReceiptForManifest($Fixture) {
    $Fixture.receipt.manifest = New-Pin `
        $Fixture.manifest_path `
        $Fixture.contract.envelope.manifest_relative_path
    Write-Json $Fixture.receipt_path $Fixture.receipt
    $Fixture.arguments.ExpectedReceiptSha256 = Get-Sha256 $Fixture.receipt_path
}

function Update-PackageControls($Fixture) {
    Write-Json $Fixture.manifest_path $Fixture.manifest
    Update-ReceiptForManifest $Fixture
}

function Update-Contract($Fixture) {
    Write-Json $Fixture.contract_path $Fixture.contract
    $Fixture.arguments.ExpectedContractSha256 = Get-Sha256 $Fixture.contract_path
}

function New-Fixture {
    $directory = Join-Path $testRoot ([Guid]::NewGuid().ToString('N'))
    $anchors = Join-Path $directory 'anchors'
    $runRoot = Join-Path $directory 'run'
    $payloadRoot = Join-Path $runRoot 'app'
    $evidenceRoot = Join-Path $runRoot 'evidence'
    [IO.Directory]::CreateDirectory((Join-Path $payloadRoot 'bin')) | Out-Null
    [IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
    [IO.Directory]::CreateDirectory($anchors) | Out-Null

    $compositeContractPath = Join-Path $anchors 'qt-shell-composite-contract.v2.json'
    Write-Json $compositeContractPath ([ordered]@{
            schema_version = [long]2
            contract_id = 'm0-qt-shell-composite-v2-fixture'
        })
    $qtbaseReceiptPath = Join-Path $anchors 'qtbase-receipt.v2.json'
    Write-Json $qtbaseReceiptPath ([ordered]@{
            schema_version = [long]2
            receipt_id = 'm0-qtbase-v2-fixture'
        })
    $compositeReceiptPath = Join-Path $anchors 'qt-shell-composite-receipt.v2.json'
    Write-Json $compositeReceiptPath ([ordered]@{
            schema_version = [long]2
            contract_sha256 = Get-Sha256 $compositeContractPath
            qtbase_receipt = New-Pin $qtbaseReceiptPath 'anchors/qtbase-receipt.v2.json'
        })

    $contractPath = Join-Path $directory 'qt-shell-composite-package-contract.v2.json'
    $contract = [ordered]@{
        schema_version = [long]2
        package_id = 'm0.2-qt-shell-composite-package-v2'
        product = [ordered]@{
            qt_version = '6.11.2'
            edition = 'Desktop'
            architecture = 'x86_64'
            compiler_abi = 'msvc2022_64'
            windows_sdk_version = '10.0.26100.0'
            msvc_compiler_version = '19.44.35228'
            msvc_toolset_version = '14.44.35207'
            qt_config_msvc_version = '19.44.35228'
        }
        recipe = [ordered]@{
            files = @()
            core_exports = @(
                'Assert-M0QtShellCompositePackageReceipt',
                'Invoke-M0QtShellCompositePackage')
            native_export = 'Invoke-M0QtShellCompositePackageNative'
            smoke_export = 'Test-M0QtShellCompositePackageSmoke'
        }
        inputs = [ordered]@{
            composite_contract = New-Pin `
                $compositeContractPath 'anchors/qt-shell-composite-contract.v2.json'
            composite_receipt = New-Pin `
                $compositeReceiptPath 'anchors/qt-shell-composite-receipt.v2.json'
            qtbase_receipt = New-Pin $qtbaseReceiptPath 'anchors/qtbase-receipt.v2.json'
            sources = [ordered]@{}
            catch2 = [ordered]@{}
            tools = @()
            sbom_files = @()
        }
        build = [ordered]@{}
        deployment = [ordered]@{}
        envelope = [ordered]@{
            top_level = @('app', 'evidence')
            payload_relative_path = 'app'
            evidence_relative_path = 'evidence'
            manifest_relative_path = 'evidence/qt-shell-composite-package-manifest.v2.json'
            receipt_relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'
            receipt_is_final_entry = $true
            create_new = $true
            allow_reparse_points = $false
            allow_ads = $false
            allow_hardlinks = $false
            path_comparer = 'OrdinalIgnoreCase'
        }
        smoke = [ordered]@{}
        security = [ordered]@{}
        limits = @('fixture only')
    }
    Write-Json $contractPath $contract
    $contractHash = Get-Sha256 $contractPath

    $payloadPath = Join-Path $payloadRoot 'bin/player.exe'
    [IO.File]::WriteAllText($payloadPath, 'payload', [Text.UTF8Encoding]::new($false))
    $buildReportPath = Join-Path $evidenceRoot 'build-report.json'
    [IO.File]::WriteAllText(
        $buildReportPath,
        '{"configured":true,"ctest":65}',
        [Text.UTF8Encoding]::new($false))
    $deploymentInventoryPath = Join-Path $evidenceRoot 'deployment-inventory.tsv'
    [IO.File]::WriteAllText(
        $deploymentInventoryPath,
        "relative_path`tsize_bytes`napp/bin/player.exe`t7`n",
        [Text.UTF8Encoding]::new($false))

    $payloadFiles = @(
        New-Pin $payloadPath 'bin/player.exe'
    )
    $evidenceFiles = @(
        New-Pin $buildReportPath 'build-report.json'
        New-Pin $deploymentInventoryPath 'deployment-inventory.tsv'
    )
    $manifestPath = Join-Path $runRoot $contract.envelope.manifest_relative_path
    $manifest = [ordered]@{
        schema_version = [long]2
        package_id = $contract.package_id
        contract = [ordered]@{ sha256 = $contractHash }
        composite_receipt = [ordered]@{ sha256 = Get-Sha256 $compositeReceiptPath }
        qtbase_receipt = [ordered]@{ sha256 = Get-Sha256 $qtbaseReceiptPath }
        payload = [ordered]@{
            root_relative_path = 'app'
            files = $payloadFiles
        }
        evidence = [ordered]@{
            root_relative_path = 'evidence'
            files = $evidenceFiles
        }
    }
    Write-Json $manifestPath $manifest
    $receiptPath = Join-Path $runRoot $contract.envelope.receipt_relative_path
    $receipt = [ordered]@{
        schema_version = [long]2
        package_id = $contract.package_id
        contract = [ordered]@{ sha256 = $contractHash }
        composite_receipt = [ordered]@{ sha256 = Get-Sha256 $compositeReceiptPath }
        qtbase_receipt = [ordered]@{ sha256 = Get-Sha256 $qtbaseReceiptPath }
        manifest = New-Pin $manifestPath $contract.envelope.manifest_relative_path
        totals = [ordered]@{
            payload_files = [long]$payloadFiles.Count
            payload_bytes = [long](($payloadFiles | Measure-Object size_bytes -Sum).Sum)
            evidence_files = [long]$evidenceFiles.Count
            evidence_bytes = [long](($evidenceFiles | Measure-Object size_bytes -Sum).Sum)
        }
    }
    Write-Json $receiptPath $receipt

    return [pscustomobject]@{
        directory = $directory
        run_root = $runRoot
        payload_root = $payloadRoot
        evidence_root = $evidenceRoot
        payload_path = $payloadPath
        build_report_path = $buildReportPath
        deployment_inventory_path = $deploymentInventoryPath
        contract_path = $contractPath
        composite_contract_path = $compositeContractPath
        composite_receipt_path = $compositeReceiptPath
        qtbase_receipt_path = $qtbaseReceiptPath
        manifest_path = $manifestPath
        receipt_path = $receiptPath
        contract = $contract
        manifest = $manifest
        receipt = $receipt
        arguments = @{
            LiteralPath = $receiptPath
            ExpectedReceiptSha256 = Get-Sha256 $receiptPath
            ContractPath = $contractPath
            ExpectedContractSha256 = $contractHash
            CompositeReceiptPath = $compositeReceiptPath
            ExpectedCompositeReceiptSha256 = Get-Sha256 $compositeReceiptPath
            RepositoryRoot = $directory
            ExpectedQtBaseReceiptSha256 = Get-Sha256 $qtbaseReceiptPath
        }
    }
}

Test-Case 'verifier returns only the closed authenticated public result' {
    $fixture = New-Fixture
    $arguments = $fixture.arguments
    $result = Assert-M0QtShellCompositePackageReceipt @arguments
    Assert-True (($result.PSObject.Properties.Name -join ',') -ceq
        'verified,package_id,contract_sha256,composite_receipt_sha256,manifest_sha256,receipt_sha256,manifest_path,payload,evidence') `
        'Verifier result shape is open or reordered.'
    Assert-True ($result.verified -eq $true) 'Verifier did not return verified=true.'
    Assert-True ($result.package_id -ceq $fixture.contract.package_id) 'Package identity drifted.'
    Assert-True ($result.contract_sha256 -ceq $arguments.ExpectedContractSha256) 'Contract result anchor drifted.'
    Assert-True ($result.composite_receipt_sha256 -ceq $arguments.ExpectedCompositeReceiptSha256) `
        'Composite result anchor drifted.'
    Assert-True ($result.manifest_sha256 -ceq (Get-Sha256 $fixture.manifest_path)) `
        'Manifest result anchor drifted.'
    Assert-True ($result.receipt_sha256 -ceq $arguments.ExpectedReceiptSha256) 'Receipt result anchor drifted.'
    Assert-True ($result.manifest_path -ceq $fixture.contract.envelope.manifest_relative_path) `
        'Manifest result path drifted.'
    Assert-True ((@($result.payload.relative_path) -join ',') -ceq 'app/bin/player.exe') `
        'Returned payload inventory is not package-root-relative and canonical.'
    $expectedEvidence = @(
        'evidence/build-report.json',
        'evidence/deployment-inventory.tsv',
        $fixture.contract.envelope.manifest_relative_path,
        $fixture.contract.envelope.receipt_relative_path)
    Assert-True ((@($result.evidence.relative_path) -join ',') -ceq ($expectedEvidence -join ',')) `
        'Returned evidence inventory is incomplete or receipt is not final.'
    foreach ($row in @($result.payload) + @($result.evidence)) {
        Assert-True (($row.PSObject.Properties.Name -join ',') -ceq 'relative_path,size_bytes,sha256') `
            'Returned evidence row shape is open.'
    }
}

Test-Case 'verifier rejects payload tamper extra or missing leaves and top-level drift' {
    foreach ($kind in @('tamper', 'payload-extra', 'payload-missing', 'top-extra')) {
        $fixture = New-Fixture
        switch ($kind) {
            'tamper' { [IO.File]::AppendAllText($fixture.payload_path, 'x') }
            'payload-extra' {
                [IO.File]::WriteAllText(
                    (Join-Path $fixture.payload_root 'extra.bin'),
                    'x',
                    [Text.UTF8Encoding]::new($false))
            }
            'payload-missing' { [IO.File]::Delete($fixture.payload_path) }
            'top-extra' { [IO.Directory]::CreateDirectory((Join-Path $fixture.run_root 'extra')) | Out-Null }
        }
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } $kind `
            'payload|top-level|topology'
    }
}

Test-Case 'verifier authenticates every inventoried evidence leaf and exact evidence envelope' {
    foreach ($kind in @('evidence-tamper', 'evidence-extra', 'evidence-missing')) {
        $fixture = New-Fixture
        switch ($kind) {
            'evidence-tamper' { [IO.File]::AppendAllText($fixture.build_report_path, 'x') }
            'evidence-extra' {
                [IO.File]::WriteAllText(
                    (Join-Path $fixture.evidence_root 'extra.txt'),
                    'x',
                    [Text.UTF8Encoding]::new($false))
            }
            'evidence-missing' { [IO.File]::Delete($fixture.deployment_inventory_path) }
        }
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } $kind 'evidence'
    }
}

Test-Case 'verifier rejects recursively open JSON shapes and duplicate nested properties' {
    $fixture = New-Fixture
    $fixture.manifest.payload.Add('unexpected', $true)
    Update-PackageControls $fixture
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'open manifest payload' 'manifest.+shape|payload.+shape'

    $fixture = New-Fixture
    $fixture.receipt.totals.Add('unexpected', [long]1)
    Write-Json $fixture.receipt_path $fixture.receipt
    $fixture.arguments.ExpectedReceiptSha256 = Get-Sha256 $fixture.receipt_path
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'open receipt totals' 'receipt.+shape|totals.+shape'

    $fixture = New-Fixture
    $manifestText = ConvertTo-Json $fixture.manifest -Depth 100 -Compress
    $rowText = ConvertTo-Json $fixture.manifest.payload.files[0] -Depth 10 -Compress
    $duplicateRow = $rowText.Substring(0, $rowText.Length - 1) +
        ',"sha256":"' + ('0' * 64) + '"}'
    Assert-True (($manifestText.Split($rowText).Count - 1) -eq 1) `
        'Duplicate-key fixture row was not unique.'
    [IO.File]::WriteAllText(
        $fixture.manifest_path,
        $manifestText.Replace($rowText, $duplicateRow),
        [Text.UTF8Encoding]::new($false))
    Update-ReceiptForManifest $fixture
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'duplicate nested manifest key' 'duplicate'
}

Test-Case 'verifier rejects unsafe paths case aliases and extra empty directories' {
    $fixture = New-Fixture
    $fixture.manifest.payload.files[0].relative_path = 'BIN/player.exe'
    Update-PackageControls $fixture
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'case-aliased payload path' 'case|casing'

    foreach ($relative in @('app/empty-extra', 'evidence/empty-extra')) {
        $fixture = New-Fixture
        [IO.Directory]::CreateDirectory((Join-Path $fixture.run_root $relative)) | Out-Null
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            "empty directory $relative" 'directory|topology|extra'
    }

    foreach ($unsafe in @('../escape.exe', 'bin\\player.exe', 'bin/player.exe:ads', '/rooted.exe')) {
        $fixture = New-Fixture
        $fixture.manifest.payload.files[0].relative_path = $unsafe
        Update-PackageControls $fixture
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            "unsafe path $unsafe" 'relative path|unsafe'
    }
}

Test-Case 'verifier rejects hardlinks ADS and reparse leaves when Windows supports them' {
    if (-not $IsWindows) { return }

    $fixture = New-Fixture
    $hardlink = Join-Path $fixture.directory 'outside-hardlink.exe'
    New-Item -ItemType HardLink -Path $hardlink -Target $fixture.payload_path -ErrorAction Stop | Out-Null
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'payload hardlink' 'hardlink|link count'

    $fixture = New-Fixture
    [IO.File]::WriteAllText(
        ($fixture.payload_path + ':rogue'),
        'alternate stream',
        [Text.UTF8Encoding]::new($false))
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'payload ADS' 'alternate data stream|ADS'

    $fixture = New-Fixture
    $outside = Join-Path $fixture.directory 'outside-target.exe'
    [IO.File]::Copy($fixture.payload_path, $outside)
    [IO.File]::Delete($fixture.payload_path)
    try {
        New-Item -ItemType SymbolicLink -Path $fixture.payload_path -Target $outside -ErrorAction Stop | Out-Null
    }
    catch {
        Write-Host '[SKIP] symbolic-link creation is unavailable in this Windows environment.'
        return
    }
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'payload reparse leaf' 'reparse'
}

Test-Case 'verifier rejects external expectations and every contract anchor pin drift' {
    foreach ($name in @(
            'ExpectedContractSha256',
            'ExpectedCompositeReceiptSha256',
            'ExpectedQtBaseReceiptSha256')) {
        $fixture = New-Fixture
        $fixture.arguments[$name] = 'f' * 64
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            "$name drift" 'SHA-256|anchor|contract|receipt|Qtbase'
    }

    foreach ($pinName in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        $fixture = New-Fixture
        $fixture.contract.inputs[$pinName].sha256 = 'f' * 64
        Update-Contract $fixture
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            "$pinName pin drift" 'SHA-256|anchor|contract|receipt|Qtbase'
    }

    foreach ($pathName in @(
            'composite_contract_path',
            'composite_receipt_path',
            'qtbase_receipt_path')) {
        $fixture = New-Fixture
        [IO.File]::AppendAllText($fixture.$pathName, 'physical drift')
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            "$pathName physical drift" 'SHA-256|anchor|contract|receipt|Qtbase'
    }

    $fixture = New-Fixture
    $composite = Get-Content -Raw -LiteralPath $fixture.composite_receipt_path |
        ConvertFrom-Json -Depth 100 -DateKind String
    $composite.qtbase_receipt.relative_path = 'anchors/different-qtbase-receipt.v2.json'
    Write-Json $fixture.composite_receipt_path $composite
    $fixture.contract.inputs.composite_receipt = New-Pin `
        $fixture.composite_receipt_path 'anchors/qt-shell-composite-receipt.v2.json'
    Update-Contract $fixture
    $fixture.arguments.ExpectedCompositeReceiptSha256 = Get-Sha256 $fixture.composite_receipt_path
    $arguments = $fixture.arguments
    Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
        'composite Qtbase physical pin path drift' 'Qtbase|anchor|path'
}

Test-Case 'receipt and manifest close identity anchors manifest pin and inventory totals' {
    foreach ($kind in @(
            'receipt-package-id',
            'receipt-contract',
            'receipt-composite',
            'receipt-qtbase',
            'receipt-manifest',
            'receipt-totals')) {
        $fixture = New-Fixture
        switch ($kind) {
            'receipt-package-id' { $fixture.receipt.package_id = 'different-package' }
            'receipt-contract' { $fixture.receipt.contract.sha256 = 'f' * 64 }
            'receipt-composite' { $fixture.receipt.composite_receipt.sha256 = 'f' * 64 }
            'receipt-qtbase' { $fixture.receipt.qtbase_receipt.sha256 = 'f' * 64 }
            'receipt-manifest' { $fixture.receipt.manifest.sha256 = 'f' * 64 }
            'receipt-totals' { $fixture.receipt.totals.payload_bytes++ }
        }
        Write-Json $fixture.receipt_path $fixture.receipt
        $fixture.arguments.ExpectedReceiptSha256 = Get-Sha256 $fixture.receipt_path
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            $kind 'receipt|package_id|anchor|manifest|totals|SHA-256'
    }

    foreach ($kind in @('manifest-package-id', 'manifest-contract', 'manifest-qtbase')) {
        $fixture = New-Fixture
        switch ($kind) {
            'manifest-package-id' { $fixture.manifest.package_id = 'different-package' }
            'manifest-contract' { $fixture.manifest.contract.sha256 = 'f' * 64 }
            'manifest-qtbase' { $fixture.manifest.qtbase_receipt.sha256 = 'f' * 64 }
        }
        Update-PackageControls $fixture
        $arguments = $fixture.arguments
        Assert-Rejected { Assert-M0QtShellCompositePackageReceipt @arguments } `
            $kind 'manifest|package_id|anchor'
    }
}

Test-Case 'retained file leases block bytes and final resweep detects namespace mutation' {
    if (-not $IsWindows) { return }
    $fixture = New-Fixture
    $mutantPath = Join-Path $fixture.directory 'LeaseProbe.psm1'
    $moduleText = [IO.File]::ReadAllText($modulePath)
    $marker = '        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_VERIFIER_FINAL_RESWEEP'
    Assert-True (($moduleText.Split($marker).Count - 1) -eq 1) `
        'Verifier final-resweep seam must occur exactly once.'
    $probe = @'
        $fileMutationBlocked = $false
        try { [IO.File]::AppendAllText((Join-Path $payloadRoot 'bin/player.exe'), 'late mutation') }
        catch [IO.IOException] { $fileMutationBlocked = $true }
        catch [UnauthorizedAccessException] { $fileMutationBlocked = $true }
        if (-not $fileMutationBlocked) { throw 'Package verifier payload lease allowed mutation.' }
        try { [IO.Directory]::CreateDirectory((Join-Path $evidenceRoot 'late-empty')) | Out-Null }
        catch [IO.IOException] { }
        catch [UnauthorizedAccessException] { }
'@
    [IO.File]::WriteAllText(
        $mutantPath,
        $moduleText.Replace($marker, $probe + $marker),
        [Text.UTF8Encoding]::new($false))
    $mutant = Import-Module $mutantPath -Force -PassThru
    try {
        $command = $mutant.ExportedFunctions['Assert-M0QtShellCompositePackageReceipt']
        $arguments = $fixture.arguments
        Assert-Rejected { & $command @arguments } 'late namespace mutation' `
            'final resweep|topology|extra|missing'
        Assert-True ((Get-Sha256 $fixture.payload_path) -ceq $fixture.manifest.payload.files[0].sha256) `
            'Payload bytes changed despite the retained file lease.'
    }
    finally { Remove-Module $mutant -Force }
}

try {
    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    if ($state.Failed -ne 0) { exit 1 }
}
finally {
    Remove-Module $module -Force
    $fullTestRoot = [IO.Path]::GetFullPath($testRoot)
    $tempBoundary = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($fullTestRoot.StartsWith($tempBoundary, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Directory]::Exists($fullTestRoot)) {
        [IO.Directory]::Delete($fullTestRoot, $true)
    }
}
