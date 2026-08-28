#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$launcher = Join-Path $root 'Invoke-M0QtShellCompositePackage.ps1'
$contract = Join-Path $root 'qt-shell-composite-package-contract.v2.json'
$smokeModule = Join-Path $root 'M0.QtShellCompositePackageSmoke.psm1'
$failures = [Collections.Generic.List[string]]::new()
function Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; Write-Host "PASS: $Name" }
    catch { $failures.Add("FAIL: $Name`n$($_.Exception.Message)"); Write-Host $failures[-1] }
}
function Need([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Same($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', found '$Actual'." }
}
function Get-Sha256([string] $LiteralPath) {
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Write-Utf8Json([string] $LiteralPath, $Value) {
    $json = $Value | ConvertTo-Json -Depth 100
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($LiteralPath)) | Out-Null
    [IO.File]::WriteAllText($LiteralPath, $json + "`n", [Text.UTF8Encoding]::new($false))
}
function New-LauncherFixture([string] $Parent, [string] $Name, [scriptblock] $BeforeRecipe) {
    $repository = Join-Path $Parent $Name
    $package = Join-Path $repository 'experiments/m0/candidates/qt/package-v2'
    [IO.Directory]::CreateDirectory($package) | Out-Null
    $fixtureLauncher = Join-Path $package 'Invoke-M0QtShellCompositePackage.ps1'
    Copy-Item -LiteralPath $launcher -Destination $fixtureLauncher

    $core = Join-Path $package 'M0.QtShellCompositePackage.psm1'
    $native = Join-Path $package 'M0.QtShellCompositePackageNative.psm1'
    $smoke = Join-Path $package 'M0.QtShellCompositePackageSmoke.psm1'
    $smokeLauncher = Join-Path $package 'Test-M0QtShellCompositePackageSmoke.ps1'
    $qtConf = Join-Path $package 'qt.conf'
    $limitations = Join-Path $package 'PACKAGE-LIMITATIONS.txt'
    [IO.File]::WriteAllText($core, @'
function Assert-M0QtShellCompositePackageReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$LiteralPath,
        [Parameter(Mandatory)][string]$ExpectedReceiptSha256,
        [Parameter(Mandatory)][string]$ContractPath,
        [Parameter(Mandatory)][string]$ExpectedContractSha256,
        [Parameter(Mandatory)][string]$CompositeReceiptPath,
        [Parameter(Mandatory)][string]$ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$ExpectedQtBaseReceiptSha256
    )
    $receipt = [IO.Path]::GetFullPath($LiteralPath)
    $packageRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($receipt))
    $manifestPath = Join-Path $packageRoot 'evidence/manifest.json'
    $rows = foreach ($file in @(Get-ChildItem -LiteralPath $packageRoot -File -Recurse)) {
        [pscustomobject][ordered]@{
            relative_path = [IO.Path]::GetRelativePath($packageRoot, $file.FullName).Replace('\', '/')
            size_bytes = [long]$file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $payload = [object[]]@($rows | Where-Object { $_.relative_path -like 'app/*' } |
        Sort-Object -Property relative_path)
    $receiptRelative = 'evidence/qt-shell-composite-package-receipt.v2.json'
    $evidence = [object[]]@($rows | Where-Object {
            $_.relative_path -notlike 'app/*' -and $_.relative_path -cne $receiptRelative
        } | Sort-Object -Property relative_path)
    $receiptRow = @($rows | Where-Object { $_.relative_path -ceq $receiptRelative })
    if ($receiptRow.Count -ne 1 -or $receiptRow[0].sha256 -cne $ExpectedReceiptSha256) {
        throw 'Fixture receipt pin drifted.'
    }
    $evidence = [object[]]@($evidence + $receiptRow[0])
    [pscustomobject][ordered]@{
        verified = $true
        package_id = 'm0.2-qt-shell-composite-package-v2'
        contract_sha256 = $ExpectedContractSha256
        composite_receipt_sha256 = $ExpectedCompositeReceiptSha256
        manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        receipt_sha256 = $ExpectedReceiptSha256
        manifest_path = 'evidence/manifest.json'
        payload = $payload
        evidence = $evidence
    }
}
function Invoke-M0QtShellCompositePackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ContractPath,
        [Parameter(Mandatory)][string]$ExpectedContractSha256,
        [Parameter(Mandatory)][string]$CompositeReceiptPath,
        [Parameter(Mandatory)][string]$ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)][string]$ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$BuildRoot,
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][string]$ScratchRoot
    )
    $packageRoot = [IO.Path]::GetFullPath($OutputRoot)
    $appRoot = Join-Path $packageRoot 'app'
    $evidenceRoot = Join-Path $packageRoot 'evidence'
    [IO.Directory]::CreateDirectory($appRoot) | Out-Null
    [IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
    $executablePath = Join-Path $appRoot 'm0_qt_shell.exe'
    $manifestPath = Join-Path $evidenceRoot 'manifest.json'
    $receiptPath = Join-Path $evidenceRoot 'qt-shell-composite-package-receipt.v2.json'
    [IO.File]::WriteAllText($executablePath, 'fixture binary', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($manifestPath, '{}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($receiptPath, '{}', [Text.UTF8Encoding]::new($false))
    $manifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $receiptSha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $markerPath = Join-Path ([IO.Path]::GetDirectoryName($packageRoot)) 'fixture-invocation.json'
    $marker = [pscustomobject][ordered]@{
        repository_root = [IO.Path]::GetFullPath($RepositoryRoot)
        build_root = [IO.Path]::GetFullPath($BuildRoot)
        output_root = $packageRoot
        scratch_root = [IO.Path]::GetFullPath($ScratchRoot)
        native_loaded = $null -ne (Get-Module -Name M0.QtShellCompositePackageNative)
        smoke_loaded = $null -ne (Get-Module -Name M0.QtShellCompositePackageSmoke)
    } | ConvertTo-Json -Depth 10
    [IO.File]::WriteAllText($markerPath, $marker, [Text.UTF8Encoding]::new($false))
    [pscustomobject][ordered]@{
        exit_code = [long]0
        package_root = $packageRoot
        receipt_path = $receiptPath
        receipt_sha256 = $receiptSha256
        manifest_path = $manifestPath
        manifest_sha256 = $manifestSha256
        entries = [object[]]@(
            [pscustomobject][ordered]@{
                relative_path = 'app/m0_qt_shell.exe'
                size_bytes = [long](Get-Item -LiteralPath $executablePath).Length
                sha256 = (Get-FileHash -LiteralPath $executablePath -Algorithm SHA256).Hash.ToLowerInvariant()
            },
            [pscustomobject][ordered]@{
                relative_path = 'evidence/manifest.json'
                size_bytes = [long](Get-Item -LiteralPath $manifestPath).Length
                sha256 = $manifestSha256
            },
            [pscustomobject][ordered]@{
                relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'
                size_bytes = [long](Get-Item -LiteralPath $receiptPath).Length
                sha256 = $receiptSha256
            }
        )
    }
}
Export-ModuleMember -Function Assert-M0QtShellCompositePackageReceipt,Invoke-M0QtShellCompositePackage
'@, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($native, @'
function Invoke-M0QtShellCompositePackageNative {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$SourceRoot,
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][string]$ScratchRoot,
        [Parameter(Mandatory)][object[]]$Entries
    )
    [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputRoot)) | Out-Null
    foreach ($entry in @($Entries)) {
        $source = Join-Path $SourceRoot ([string]$entry.relative_path).Replace('/', '\')
        $destination = Join-Path $OutputRoot ([string]$entry.relative_path).Replace('/', '\')
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        [IO.File]::Copy($source, $destination, $false)
    }
    [pscustomobject][ordered]@{
        exit_code = [long]0
        package_root = '.'
        payload_root = 'app'
        evidence_root = 'evidence'
        entries = [object[]]$Entries
        published_root_identity = '01234567:0123456789ABCDEF'
    }
}
Export-ModuleMember -Function Invoke-M0QtShellCompositePackageNative
'@, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($smoke, @'
function Test-M0QtShellCompositePackageSmoke {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$PackageRoot,
        [Parameter(Mandatory)][string]$ReceiptPath,
        [Parameter(Mandatory)][string]$ExpectedReceiptSha256,
        [Parameter(Mandatory)][string]$ContractPath,
        [Parameter(Mandatory)][string]$ExpectedContractSha256,
        [Parameter(Mandatory)][string]$CompositeReceiptPath,
        [Parameter(Mandatory)][string]$ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)][scriptblock]$VerifyReceiptAction,
        [Parameter(Mandatory)][scriptblock]$WorkspaceFactory
    )
    $verification = & $VerifyReceiptAction 'tray-lifecycle-pre'
    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_SMOKE_AFTER_PRE_VERIFY
    $null = & $VerifyReceiptAction 'tray-lifecycle-post'
    $null = & $VerifyReceiptAction 'error-recovery-pre'
    $null = & $VerifyReceiptAction 'error-recovery-post'
    $verification = & $VerifyReceiptAction 'final'
    $workspace = [IO.Path]::GetFullPath((& $WorkspaceFactory))
    [pscustomobject][ordered]@{
        verified = $true
        package_id = 'm0.2-qt-shell-composite-package-v2'
        package_root = [IO.Path]::GetFullPath($PackageRoot)
        contract_sha256 = $ExpectedContractSha256
        composite_receipt_sha256 = $ExpectedCompositeReceiptSha256
        manifest_sha256 = [string]$verification.manifest_sha256
        receipt_sha256 = $ExpectedReceiptSha256
        journeys = @(
            [pscustomobject][ordered]@{
                journey = 'tray-lifecycle'
                working_directory = Join-Path $workspace 'tray-lifecycle'
                exit_code = [long]0
                stdout = ''
                stderr = ''
                timed_out = $false
                module_capture_ms = [double]42
                module_paths = [object[]]@([IO.Path]::GetFullPath((Join-Path $PackageRoot 'app/m0_qt_shell.exe')))
                survivor_count = [long]0
            },
            [pscustomobject][ordered]@{
                journey = 'error-recovery'
                working_directory = Join-Path $workspace 'error-recovery'
                exit_code = [long]0
                stdout = ''
                stderr = ''
                timed_out = $false
                module_capture_ms = [double]43
                module_paths = [object[]]@([IO.Path]::GetFullPath((Join-Path $PackageRoot 'app/m0_qt_shell.exe')))
                survivor_count = [long]0
            }
        )
        workspace = $workspace
        limits = @(
            'offscreen diagnostic only',
            'sampling is not a complete loader trace',
            'no accessibility ABI or redistribution proof'
        )
    }
}
Export-ModuleMember -Function Test-M0QtShellCompositePackageSmoke
'@, [Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $root 'Test-M0QtShellCompositePackageSmoke.ps1') `
        -Destination $smokeLauncher
    [IO.File]::WriteAllText($qtConf, "[Paths]`nPrefix=.`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($limitations, 'fixture limitations', [Text.UTF8Encoding]::new($false))

    $compositeReceipt = Join-Path $repository 'q/composite run/evidence/qt-shell-composite-receipt.v2.json'
    $qtbaseReceipt = Join-Path $repository 'q/qtbase/evidence/qtbase-refresh-receipt.v2.json'
    $compositeContract = Join-Path $repository 'experiments/m0/provisioning/qt-shell/qt-shell-composite-contract.v2.json'
    Write-Utf8Json $compositeReceipt ([ordered]@{ schema_version = [long]2 })
    Write-Utf8Json $qtbaseReceipt ([ordered]@{ schema_version = [long]2 })
    Write-Utf8Json $compositeContract ([ordered]@{ schema_version = [long]2 })
    if ($null -ne $BeforeRecipe) {
        & $BeforeRecipe $fixtureLauncher $core $package
    }

    $relativeMembers = @(
        'experiments/m0/candidates/qt/package-v2/Invoke-M0QtShellCompositePackage.ps1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackage.psm1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageNative.psm1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageSmoke.psm1',
        'experiments/m0/candidates/qt/package-v2/Test-M0QtShellCompositePackageSmoke.ps1',
        'experiments/m0/candidates/qt/package-v2/qt.conf',
        'experiments/m0/candidates/qt/package-v2/PACKAGE-LIMITATIONS.txt'
    )
    $recipe = foreach ($relative in $relativeMembers) {
        $path = Join-Path $repository $relative
        [ordered]@{
            relative_path = $relative
            size_bytes = [long](Get-Item -LiteralPath $path).Length
            sha256 = Get-Sha256 $path
        }
    }
    function New-Pin([string] $Path, [string] $RelativePath) {
        [ordered]@{
            relative_path = $RelativePath
            size_bytes = [long](Get-Item -LiteralPath $Path).Length
            sha256 = Get-Sha256 $Path
        }
    }
    $contractPath = Join-Path $package 'qt-shell-composite-package-contract.v2.json'
    Write-Utf8Json $contractPath ([ordered]@{
            schema_version = [long]2
            package_id = 'm0.2-qt-shell-composite-package-v2'
            recipe = [ordered]@{
                files = @($recipe)
                core_exports = @('Assert-M0QtShellCompositePackageReceipt', 'Invoke-M0QtShellCompositePackage')
                native_export = 'Invoke-M0QtShellCompositePackageNative'
                smoke_export = 'Test-M0QtShellCompositePackageSmoke'
            }
            inputs = [ordered]@{
                composite_contract = New-Pin $compositeContract 'experiments/m0/provisioning/qt-shell/qt-shell-composite-contract.v2.json'
                composite_receipt = New-Pin $compositeReceipt 'q/composite run/evidence/qt-shell-composite-receipt.v2.json'
                qtbase_receipt = New-Pin $qtbaseReceipt 'q/qtbase/evidence/qtbase-refresh-receipt.v2.json'
            }
        })
    return [pscustomobject][ordered]@{
        repository = $repository
        package = $package
        launcher = $fixtureLauncher
        contract = $contractPath
        contract_sha256 = Get-Sha256 $contractPath
        composite_receipt = $compositeReceipt
        composite_receipt_sha256 = Get-Sha256 $compositeReceipt
        qtbase_receipt_sha256 = Get-Sha256 $qtbaseReceipt
        core = $core
        smoke_launcher = $smokeLauncher
        build = Join-Path $repository 'out/build with spaces'
        output = Join-Path $repository 'out/package with spaces'
        scratch = Join-Path $repository 'out/scratch with spaces'
    }
}
function New-PackageFixture([string] $Parent, [string] $Name) {
    $package = Join-Path $Parent $Name
    $app = Join-Path $package 'app'
    [IO.Directory]::CreateDirectory($app) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $package 'evidence')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $app 'm0_qt_shell.exe'), 'fixture')
    foreach ($name in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll', 'qoffscreen.dll')) {
        [IO.File]::WriteAllText((Join-Path $app $name), 'fixture')
    }
    [IO.File]::WriteAllText((Join-Path $package 'evidence/manifest.json'), '{}')
    return $package
}
function Invoke-FakeSmoke([string] $PackageRoot, [string] $WorkspaceRoot) {
    $events = [Collections.Generic.List[string]]::new()
    $verify = {
        param($Phase)
        $events.Add("verify:$Phase")
        [pscustomobject][ordered]@{
            verified = $true
            package_id = 'm0.2-qt-shell-composite-package-v2'
            contract_sha256 = 'b' * 64
            composite_receipt_sha256 = 'c' * 64
            manifest_sha256 = 'd' * 64
            receipt_sha256 = 'a' * 64
            manifest_path = 'evidence/manifest.json'
            payload = @(
                'app/m0_qt_shell.exe', 'app/Qt6Core.dll', 'app/Qt6Gui.dll',
                'app/Qt6Widgets.dll', 'app/Qt6Quick.dll', 'app/qoffscreen.dll')
            evidence = @()
        }
    }.GetNewClosure()
    $run = {
        param($Executable, $Arguments, $WorkingDirectory, $Environment)
        $events.Add("run:$($Arguments -join '|')")
        $app = [IO.Path]::GetDirectoryName($Executable)
        $system32 = Join-Path $env:SystemRoot 'System32'
        [pscustomobject][ordered]@{
            exit_code = [long]0
            stdout = ''
            stderr = ''
            timed_out = $false
            capture_elapsed_ms = [double]42
            module_paths = @(
                $Executable,
                (Join-Path $app 'Qt6Core.dll'),
                (Join-Path $app 'Qt6Gui.dll'),
                (Join-Path $app 'Qt6Widgets.dll'),
                (Join-Path $app 'Qt6Quick.dll'),
                (Join-Path $app 'qoffscreen.dll'),
                (Join-Path $system32 'vcruntime140.dll')
            )
            survivor_count = [long]0
        }
    }.GetNewClosure()
    $arguments = @{
        PackageRoot = $PackageRoot
        ReceiptPath = Join-Path $PackageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        ExpectedReceiptSha256 = 'a' * 64
        ContractPath = $contract
        ExpectedContractSha256 = 'b' * 64
        CompositeReceiptPath = 'composite-receipt.json'
        ExpectedCompositeReceiptSha256 = 'c' * 64
        RepositoryRoot = [IO.Path]::GetDirectoryName($PackageRoot)
        ExpectedQtBaseReceiptSha256 = 'e' * 64
        VerifyReceiptAction = $verify
        ProcessOperations = @{
            RunJourney = $run
            Terminate = { param($Session) $events.Add('terminate') }.GetNewClosure()
            Dispose = { param($Session) $events.Add('dispose') }.GetNewClosure()
        }
        WorkspaceFactory = { $WorkspaceRoot }.GetNewClosure()
    }
    $result = Test-M0QtShellCompositePackageSmoke @arguments
    return [pscustomobject]@{ result = $result; events = $events }
}

Case 'launcher has a closed production API with no arbitrary QtRoot' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $command = Get-Command -Name $launcher
    $build = @('BuildRoot', 'OutputRoot', 'ScratchRoot')
    $smoke = @(
        'PackageRoot', 'ReceiptPath', 'ExpectedReceiptSha256', 'SmokeWorkspaceRoot', 'ReportPath')
    $relocate = @('SourcePackageRoot')
    $commonExpected = @('ContractPath', 'ExpectedContractSha256', 'CompositeReceiptPath',
            'ExpectedCompositeReceiptSha256', 'ExpectedQtBaseReceiptSha256', 'RepositoryRoot',
            'BuildRoot', 'OutputRoot', 'ScratchRoot')
    $expected = @($commonExpected + $smoke + $relocate)
    foreach ($name in $expected) {
        Need $command.Parameters.ContainsKey($name) "Launcher lacks mandatory parameter $name."
    }
    foreach ($name in $commonExpected) {
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory -and
                $_.ParameterSetName -in @('__AllParameterSets', 'Build')
            }).Count -eq 1
        Need $mandatory "Launcher build parameter $name is optional in Build."
    }
    foreach ($name in $smoke) {
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory -and
                $_.ParameterSetName -ceq 'Smoke'
            }).Count -eq 1
        Need $mandatory "Launcher smoke parameter $name is optional in Smoke."
    }
    foreach ($name in @('SourcePackageRoot', 'ReceiptPath', 'ExpectedReceiptSha256',
            'OutputRoot', 'ScratchRoot')) {
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory -and
                $_.ParameterSetName -ceq 'Relocate'
            }).Count -eq 1
        Need $mandatory "Launcher relocate parameter $name is optional in Relocate."
    }
    Need (-not $command.Parameters.ContainsKey('QtRoot')) 'Launcher accepts arbitrary QtRoot.'
    $common = @('Verbose', 'Debug', 'ErrorAction', 'WarningAction', 'InformationAction',
        'ProgressAction', 'ErrorVariable', 'WarningVariable', 'InformationVariable', 'OutVariable',
        'OutBuffer', 'PipelineVariable', 'WhatIf', 'Confirm')
    [string[]] $actual = @($command.Parameters.Keys | Where-Object { $_ -notin $common })
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [string[]] $wanted = @($expected)
    [Array]::Sort($wanted, [StringComparer]::Ordinal)
    Same ($actual -join "`n") ($wanted -join "`n") 'Launcher physical API is open or incomplete.'
}

Case 'standalone smoke wrapper delegates only through the authenticated launcher Smoke set' {
    $wrapperText = [IO.File]::ReadAllText((Join-Path $root 'Test-M0QtShellCompositePackageSmoke.ps1'))
    Need (-not $wrapperText.Contains('Import-Module', [StringComparison]::OrdinalIgnoreCase)) `
        'Standalone smoke wrapper imports a recipe module without launcher authentication.'
    Need $wrapperText.Contains('Invoke-M0QtShellCompositePackage.ps1', [StringComparison]::Ordinal) `
        'Standalone smoke wrapper does not delegate to the authenticated launcher.'

    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-wrapper-auth-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository'
        $packageRoot = Join-Path $fixture.repository 'out/package para smoke'
        $receiptPath = Join-Path $packageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'app')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'evidence')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $packageRoot 'app/m0_qt_shell.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'evidence/manifest.json'), '{}')
        [IO.File]::WriteAllText($receiptPath, '{}')
        $workspace = Join-Path $fixture.repository 'out/workspace smoke'
        $reportPath = Join-Path $fixture.repository 'out/reports/smoke-original.json'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath)) | Out-Null
        $result = & $fixture.smoke_launcher `
            -ContractPath $fixture.contract `
            -ExpectedContractSha256 $fixture.contract_sha256 `
            -CompositeReceiptPath $fixture.composite_receipt `
            -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
            -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
            -RepositoryRoot $fixture.repository `
            -PackageRoot $packageRoot `
            -ReceiptPath $receiptPath `
            -ExpectedReceiptSha256 (Get-Sha256 $receiptPath) `
            -SmokeWorkspaceRoot $workspace `
            -ReportPath $reportPath
        Need ($result.result.verified -eq $true) `
            'Authenticated standalone smoke did not return verified=true.'
        Same $result.result.package_root ([IO.Path]::GetFullPath($packageRoot)) `
            'Standalone smoke package drifted.'
        Same $result.result.workspace ([IO.Path]::GetFullPath($workspace)) `
            'Standalone smoke workspace drifted.'
        Same $result.report.relative_path 'out/reports/smoke-original.json' `
            'Standalone smoke report path is not repository-relative.'
        Same $result.report.size_bytes ([long](Get-Item -LiteralPath $reportPath).Length) `
            'Standalone smoke report size pin drifted.'
        Same $result.report.sha256 (Get-Sha256 $reportPath) `
            'Standalone smoke report hash pin drifted.'
        $reportBytes = [IO.File]::ReadAllBytes($reportPath)
        Need ($reportBytes.Length -gt 3 -and $reportBytes[0] -ne 0xEF -and
            $reportBytes[-1] -eq 0x0A -and -not ($reportBytes -contains 0x0D)) `
            'Standalone smoke report is not UTF-8 without BOM using LF endings.'
        $report = [Text.Json.JsonDocument]::Parse([Text.Encoding]::UTF8.GetString($reportBytes))
        try {
            Same ($report.RootElement.GetProperty('package_root').GetString()) `
                ([IO.Path]::GetFullPath($packageRoot)) 'Persisted smoke package drifted.'
        }
        finally { $report.Dispose() }
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher retains the verified package inventory through smoke module loading' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-smoke-lease-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $RecipeRoot)
            $fixtureSmoke = Join-Path $RecipeRoot 'M0.QtShellCompositePackageSmoke.psm1'
            $text = [IO.File]::ReadAllText($fixtureSmoke)
            $marker = '    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_SMOKE_AFTER_PRE_VERIFY'
            $injection = @'
    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_SMOKE_AFTER_PRE_VERIFY
    [IO.File]::WriteAllText((Join-Path $PackageRoot 'app/m0_qt_shell.exe'), 'substituted')
'@
            Need ($text.Contains($marker, [StringComparison]::Ordinal)) `
                'Fixture smoke inventory-mutation seam is missing.'
            [IO.File]::WriteAllText(
                $fixtureSmoke,
                $text.Replace($marker, $injection.TrimEnd("`r", "`n")),
                [Text.UTF8Encoding]::new($false))
        }
        $packageRoot = Join-Path $fixture.repository 'out/package para smoke'
        $receiptPath = Join-Path $packageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'app')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'evidence')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $packageRoot 'app/m0_qt_shell.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'evidence/manifest.json'), '{}')
        [IO.File]::WriteAllText($receiptPath, '{}')
        $workspace = Join-Path $fixture.repository 'out/workspace smoke'
        $reportPath = Join-Path $fixture.repository 'out/reports/smoke.json'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath)) | Out-Null
        $message = $null
        try {
            & $fixture.smoke_launcher `
                -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository `
                -PackageRoot $packageRoot `
                -ReceiptPath $receiptPath `
                -ExpectedReceiptSha256 (Get-Sha256 $receiptPath) `
                -SmokeWorkspaceRoot $workspace `
                -ReportPath $reportPath | Out-Null
        }
        catch { $message = $_.Exception.Message }
        Need ($null -ne $message) `
            'Package payload was substituted after pre-verification and before smoke loading.'
        Need ($message -match '(?i)(used by another process|access|authenticated|lease|sharing)') `
            "Smoke inventory substitution failed for the wrong reason: $message"
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'smoke report refuses package-local and preexisting targets without overwriting' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-smoke-report-boundary-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository'
        $packageRoot = Join-Path $fixture.repository 'out/package para smoke'
        $receiptPath = Join-Path $packageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'app')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'evidence')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $packageRoot 'app/m0_qt_shell.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'evidence/manifest.json'), '{}')
        [IO.File]::WriteAllText($receiptPath, '{}')
        $workspace = Join-Path $fixture.repository 'out/workspace smoke'
        $arguments = @{
            ContractPath = $fixture.contract
            ExpectedContractSha256 = $fixture.contract_sha256
            CompositeReceiptPath = $fixture.composite_receipt
            ExpectedCompositeReceiptSha256 = $fixture.composite_receipt_sha256
            ExpectedQtBaseReceiptSha256 = $fixture.qtbase_receipt_sha256
            RepositoryRoot = $fixture.repository
            PackageRoot = $packageRoot
            ReceiptPath = $receiptPath
            ExpectedReceiptSha256 = Get-Sha256 $receiptPath
            SmokeWorkspaceRoot = $workspace
        }

        $packageReport = Join-Path $packageRoot 'evidence/smoke-report.json'
        $message = $null
        try { & $fixture.smoke_launcher @arguments -ReportPath $packageReport | Out-Null }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)ReportPath.+outside PackageRoot') `
            "Package-local ReportPath failed for the wrong reason: $message"
        Need (-not [IO.File]::Exists($packageReport)) `
            'Rejected package-local report was created.'

        $existingReport = Join-Path $fixture.repository 'out/reports/existing.json'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($existingReport)) | Out-Null
        [IO.File]::WriteAllText($existingReport, 'sentinel')
        $beforeHash = Get-Sha256 $existingReport
        $message = $null
        try { & $fixture.smoke_launcher @arguments -ReportPath $existingReport | Out-Null }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)ReportPath target must be absent') `
            "Preexisting ReportPath failed for the wrong reason: $message"
        Same (Get-Sha256 $existingReport) $beforeHash `
            'Rejected preexisting report was overwritten.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'smoke does not orphan ReportPath when final authentication fails' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-smoke-report-final-auth-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $RecipeRoot)
            $text = [IO.File]::ReadAllText($FixtureLauncher)
            $needle = '    foreach ($lease in $DirectoryLeases) {'
            Need ($text.Contains($needle, [StringComparison]::Ordinal)) `
                'Fixture final-authentication injection seam is missing.'
            $injection = @'
    if ($Context -ceq 'Package-v2 launcher final authentication') {
        throw 'causal final authentication failure'
    }
    foreach ($lease in $DirectoryLeases) {
'@
            [IO.File]::WriteAllText(
                $FixtureLauncher,
                $text.Replace($needle, $injection.TrimEnd("`r", "`n")),
                [Text.UTF8Encoding]::new($false))
        }
        $packageRoot = Join-Path $fixture.repository 'out/package para smoke'
        $receiptPath = Join-Path $packageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'app')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'evidence')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $packageRoot 'app/m0_qt_shell.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'evidence/manifest.json'), '{}')
        [IO.File]::WriteAllText($receiptPath, '{}')
        $reportPath = Join-Path $fixture.repository 'out/reports/final-auth-failure.json'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath)) | Out-Null
        $message = $null
        try {
            & $fixture.smoke_launcher `
                -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository `
                -PackageRoot $packageRoot `
                -ReceiptPath $receiptPath `
                -ExpectedReceiptSha256 (Get-Sha256 $receiptPath) `
                -SmokeWorkspaceRoot (Join-Path $fixture.repository 'out/workspace smoke') `
                -ReportPath $reportPath | Out-Null
        }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)causal final authentication failure') `
            "Final authentication failed for the wrong reason: $message"
        Need (-not [IO.File]::Exists($reportPath)) `
            'Final authentication failure left an unauthenticated smoke ReportPath behind.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'smoke rolls back the exact ReportPath when report authentication fails after writing' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-smoke-report-rollback-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $RecipeRoot)
            $text = [IO.File]::ReadAllText($FixtureLauncher)
            $marker = `
                '        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_AFTER_REPORT_WRITE'
            Need ($text.Contains($marker, [StringComparison]::Ordinal)) `
                'Fixture post-write report-authentication seam is missing.'
            $injection = @'
        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_AFTER_REPORT_WRITE
        throw 'causal report authentication failure after write'
'@
            [IO.File]::WriteAllText(
                $FixtureLauncher,
                $text.Replace($marker, $injection.TrimEnd("`r", "`n")),
                [Text.UTF8Encoding]::new($false))
        }
        $packageRoot = Join-Path $fixture.repository 'out/package para smoke'
        $receiptPath = Join-Path $packageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'app')) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $packageRoot 'evidence')) | Out-Null
        [IO.File]::WriteAllText((Join-Path $packageRoot 'app/m0_qt_shell.exe'), 'fixture')
        [IO.File]::WriteAllText((Join-Path $packageRoot 'evidence/manifest.json'), '{}')
        [IO.File]::WriteAllText($receiptPath, '{}')
        $reportPath = Join-Path $fixture.repository 'out/reports/post-write-failure.json'
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath)) | Out-Null
        $message = $null
        try {
            & $fixture.smoke_launcher `
                -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository `
                -PackageRoot $packageRoot `
                -ReceiptPath $receiptPath `
                -ExpectedReceiptSha256 (Get-Sha256 $receiptPath) `
                -SmokeWorkspaceRoot (Join-Path $fixture.repository 'out/workspace smoke') `
                -ReportPath $reportPath | Out-Null
        }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)causal report authentication failure after write') `
            "Post-write report authentication failed for the wrong reason: $message"
        Need (-not [IO.File]::Exists($reportPath)) `
            'Post-write report authentication failure did not roll back the exact created ReportPath.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher preserves the primary error and diagnoses Remove-Module failure while retrying cleanup' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-remove-module-failure-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $RecipeRoot)
            $coreText = [IO.File]::ReadAllText($CorePath)
            $coreNeedle = '    $packageRoot = [IO.Path]::GetFullPath($OutputRoot)'
            Need ($coreText.Contains($coreNeedle, [StringComparison]::Ordinal)) `
                'Fixture primary-error injection seam is missing.'
            [IO.File]::WriteAllText(
                $CorePath,
                $coreText.Replace(
                    $coreNeedle,
                    "    throw 'causal primary operation failure'`n$coreNeedle"),
                [Text.UTF8Encoding]::new($false))

            $launcherText = [IO.File]::ReadAllText($FixtureLauncher)
            $removeMarker = `
                '        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_MODULE_REMOVE'
            Need ($launcherText.Contains($removeMarker, [StringComparison]::Ordinal)) `
                'Fixture Remove-Module injection seam is missing.'
            $removeInjection = @'
        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_MODULE_REMOVE
        if ($modules[$index].Name -ceq 'M0.QtShellCompositePackageNative' -and
            -not (Test-Path Variable:script:FixtureRemoveModuleFailureInjected)) {
            $script:FixtureRemoveModuleFailureInjected = $true
            throw 'causal Remove-Module failure'
        }
'@
            [IO.File]::WriteAllText(
                $FixtureLauncher,
                $launcherText.Replace($removeMarker, $removeInjection.TrimEnd("`r", "`n")),
                [Text.UTF8Encoding]::new($false))
        }
        $caught = $null
        try {
            & $fixture.launcher `
                -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository `
                -BuildRoot $fixture.build `
                -OutputRoot $fixture.output `
                -ScratchRoot $fixture.scratch | Out-Null
        }
        catch { $caught = $_ }
        Need ($null -ne $caught) 'Launcher swallowed the causal primary and module cleanup failures.'
        Same $caught.Exception.Message 'causal primary operation failure' `
            'Module cleanup replaced the original operation error.'
        $diagnostics = [string]$caught.Exception.Data['PackageV2ModuleCleanupDiagnostics']
        Need ($diagnostics -match '(?i)M0\.QtShellCompositePackageNative.+causal Remove-Module failure') `
            "Original error lacks causal module cleanup diagnostics: $diagnostics"
        $fixturePrefix = [IO.Path]::GetFullPath($fixture.repository).TrimEnd('\') + '\'
        $fixtureModules = @(Get-Module -All | Where-Object {
                $_.Path -and [IO.Path]::GetFullPath($_.Path).StartsWith(
                    $fixturePrefix, [StringComparison]::OrdinalIgnoreCase)
            })
        Need ($fixtureModules.Count -eq 0) `
            'Deterministic cleanup left a fixture recipe module preloaded.'
    }
    finally {
        foreach ($name in @(
                'M0.QtShellCompositePackageSmoke',
                'M0.QtShellCompositePackageNative',
                'M0.QtShellCompositePackage')) {
            Get-Module -Name $name -All -ErrorAction SilentlyContinue |
                Remove-Module -Force -ErrorAction SilentlyContinue
        }
        if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) }
    }
}

Case 'launcher authenticates contract and every imported recipe module before import' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($launcher, [ref]$tokens, [ref]$errors)
    Need (@($errors).Count -eq 0) 'Launcher does not parse.'
    $commands = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
    $firstImport = -1
    for ($index = 0; $index -lt $commands.Count; $index++) {
        if ($commands[$index].GetCommandName() -ceq 'Import-Module') { $firstImport = $index; break }
    }
    Need ($firstImport -gt 0) 'Launcher imports before any authentication.'
    $leasesBeforeImport = @($commands[0..($firstImport - 1)] | Where-Object {
            $_.GetCommandName() -ceq 'Open-M0PackageLauncherFileLease'
        })
    Need ($leasesBeforeImport.Count -ge 3) `
        'Launcher does not lease/authenticate the contract, complete recipe, and external receipts before import.'
    $text = [IO.File]::ReadAllText($launcher)
    foreach ($needle in @('M0.QtShellCompositePackage.psm1', 'M0.QtShellCompositePackageNative.psm1',
            'M0.QtShellCompositePackageSmoke.psm1', 'ExpectedContractSha256',
            'ExpectedCompositeReceiptSha256', 'Invoke-M0QtShellCompositePackage')) {
        Need $text.Contains($needle, [StringComparison]::Ordinal) "Launcher lacks $needle."
    }
}

Case 'launcher authenticates a relocated closed recipe and forwards the exact physical boundary' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-launcher-green-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repositório com espaços e acentos'
        $result = & $fixture.launcher -ContractPath $fixture.contract `
            -ExpectedContractSha256 $fixture.contract_sha256 `
            -CompositeReceiptPath $fixture.composite_receipt `
            -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
            -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
            -RepositoryRoot $fixture.repository -BuildRoot $fixture.build `
            -OutputRoot $fixture.output -ScratchRoot $fixture.scratch
        [string[]] $actualResultFields = @($result.PSObject.Properties.Name)
        [string[]] $expectedResultFields = @(
            'exit_code', 'package_root', 'receipt_path', 'receipt_sha256',
            'manifest_path', 'manifest_sha256', 'entries')
        [Array]::Sort($actualResultFields, [StringComparer]::Ordinal)
        [Array]::Sort($expectedResultFields, [StringComparer]::Ordinal)
        Same ($actualResultFields -join "`n") ($expectedResultFields -join "`n") `
            'Launcher accepted an open Build result from the authenticated core.'
        Need ($result.exit_code -is [long] -and $result.exit_code -eq 0) `
            'Authenticated core did not return a closed successful exit code.'
        Same $result.package_root ([IO.Path]::GetFullPath($fixture.output)) `
            'Build package root drifted.'
        $markerPath = Join-Path ([IO.Path]::GetDirectoryName($fixture.output)) `
            'fixture-invocation.json'
        $marker = [IO.File]::ReadAllText($markerPath) | ConvertFrom-Json
        Same $marker.repository_root ([IO.Path]::GetFullPath($fixture.repository)) `
            'Repository root drifted.'
        Same $marker.build_root ([IO.Path]::GetFullPath($fixture.build)) 'Build root drifted.'
        Same $marker.output_root ([IO.Path]::GetFullPath($fixture.output)) 'Output root drifted.'
        Same $marker.scratch_root ([IO.Path]::GetFullPath($fixture.scratch)) 'Scratch root drifted.'
        Need ($marker.native_loaded -eq $true) `
            'Authenticated native module was not loaded for the core boundary.'
        Need ($marker.smoke_loaded -eq $true) `
            'Authenticated smoke module was not loaded for the core boundary.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher relocates only the closed verified inventory and verifies the published destination' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-relocate-set-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository'
        $built = & $fixture.launcher `
            -ContractPath $fixture.contract `
            -ExpectedContractSha256 $fixture.contract_sha256 `
            -CompositeReceiptPath $fixture.composite_receipt `
            -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
            -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
            -RepositoryRoot $fixture.repository `
            -BuildRoot $fixture.build `
            -OutputRoot $fixture.output `
            -ScratchRoot $fixture.scratch
        $relocatedRoot = Join-Path $fixture.repository 'out/relocated package with spaces'
        $relocateScratch = Join-Path $fixture.repository 'out/relocate scratch'
        $relocated = & $fixture.launcher `
            -ContractPath $fixture.contract `
            -ExpectedContractSha256 $fixture.contract_sha256 `
            -CompositeReceiptPath $fixture.composite_receipt `
            -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
            -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
            -RepositoryRoot $fixture.repository `
            -SourcePackageRoot $built.package_root `
            -ReceiptPath $built.receipt_path `
            -ExpectedReceiptSha256 $built.receipt_sha256 `
            -OutputRoot $relocatedRoot `
            -ScratchRoot $relocateScratch
        Same $relocated.package_root ([IO.Path]::GetFullPath($relocatedRoot)) `
            'Relocate result did not bind the destination root.'
        Same $relocated.receipt_sha256 $built.receipt_sha256 `
            'Relocate changed the receipt identity.'
        Same $relocated.manifest_sha256 $built.manifest_sha256 `
            'Relocate changed the manifest identity.'
        Same (($relocated.entries | ConvertTo-Json -Depth 10 -Compress)) `
            (($built.entries | ConvertTo-Json -Depth 10 -Compress)) `
            'Relocate changed the closed inventory.'
        Need ([IO.File]::Exists($relocated.receipt_path)) `
            'Relocate did not publish its canonical receipt.'
        Need (-not [IO.Directory]::Exists($relocateScratch)) `
            'Relocate left a private scratch root behind.'

        $reportsRoot = Join-Path $fixture.repository 'out/smoke reports'
        [IO.Directory]::CreateDirectory($reportsRoot) | Out-Null
        $smokeCommon = @{
            ContractPath = $fixture.contract
            ExpectedContractSha256 = $fixture.contract_sha256
            CompositeReceiptPath = $fixture.composite_receipt
            ExpectedCompositeReceiptSha256 = $fixture.composite_receipt_sha256
            ExpectedQtBaseReceiptSha256 = $fixture.qtbase_receipt_sha256
            RepositoryRoot = $fixture.repository
        }
        $originalSmoke = & $fixture.smoke_launcher @smokeCommon `
            -PackageRoot $built.package_root `
            -ReceiptPath $built.receipt_path `
            -ExpectedReceiptSha256 $built.receipt_sha256 `
            -SmokeWorkspaceRoot (Join-Path $fixture.repository 'out/smoke original workspace') `
            -ReportPath (Join-Path $reportsRoot 'original.json')
        $relocatedSmoke = & $fixture.smoke_launcher @smokeCommon `
            -PackageRoot $relocated.package_root `
            -ReceiptPath $relocated.receipt_path `
            -ExpectedReceiptSha256 $relocated.receipt_sha256 `
            -SmokeWorkspaceRoot (Join-Path $fixture.repository 'out/smoke relocated workspace') `
            -ReportPath (Join-Path $reportsRoot 'relocated.json')
        Need ($originalSmoke.result.verified -and $relocatedSmoke.result.verified) `
            'Original or relocated authenticated smoke did not verify.'
        Same $relocatedSmoke.result.manifest_sha256 $originalSmoke.result.manifest_sha256 `
            'Relocated smoke changed the manifest binding.'
        Same $relocatedSmoke.result.receipt_sha256 $originalSmoke.result.receipt_sha256 `
            'Relocated smoke changed the receipt binding.'
        Same $originalSmoke.report.sha256 `
            (Get-Sha256 (Join-Path $reportsRoot 'original.json')) `
            'Original smoke report pin drifted.'
        Same $relocatedSmoke.report.sha256 `
            (Get-Sha256 (Join-Path $reportsRoot 'relocated.json')) `
            'Relocated smoke report pin drifted.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher rejects an open native relocation result before trusting publication' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-relocate-native-shape-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $RecipeRoot)
            $fixtureNative = Join-Path $RecipeRoot 'M0.QtShellCompositePackageNative.psm1'
            $text = [IO.File]::ReadAllText($fixtureNative)
            $needle = "        published_root_identity = '01234567:0123456789ABCDEF'"
            Need ($text.Contains($needle, [StringComparison]::Ordinal)) `
                'Fixture native result seam is missing.'
            [IO.File]::WriteAllText(
                $fixtureNative,
                $text.Replace($needle, $needle + "`n        extra = `$true"),
                [Text.UTF8Encoding]::new($false))
        }
        $built = & $fixture.launcher `
            -ContractPath $fixture.contract `
            -ExpectedContractSha256 $fixture.contract_sha256 `
            -CompositeReceiptPath $fixture.composite_receipt `
            -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
            -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
            -RepositoryRoot $fixture.repository `
            -BuildRoot $fixture.build `
            -OutputRoot $fixture.output `
            -ScratchRoot $fixture.scratch
        $message = $null
        try {
            & $fixture.launcher `
                -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository `
                -SourcePackageRoot $built.package_root `
                -ReceiptPath $built.receipt_path `
                -ExpectedReceiptSha256 $built.receipt_sha256 `
                -OutputRoot (Join-Path $fixture.repository 'out/relocated') `
                -ScratchRoot (Join-Path $fixture.repository 'out/relocate scratch') | Out-Null
        }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)native.+(open|shape|properties|result)') `
            "Open native result failed for the wrong reason: $message"
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher rejects preloaded recipe modules and recipe byte substitution before core invocation' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-launcher-reject-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository'
        $loaded = Import-Module $fixture.core -Force -PassThru
        try {
            $message = $null
            try {
                & $fixture.launcher -ContractPath $fixture.contract `
                    -ExpectedContractSha256 $fixture.contract_sha256 `
                    -CompositeReceiptPath $fixture.composite_receipt `
                    -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                    -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                    -RepositoryRoot $fixture.repository -BuildRoot $fixture.build `
                    -OutputRoot $fixture.output -ScratchRoot $fixture.scratch | Out-Null
            }
            catch { $message = $_.Exception.Message }
            Need ($message -match '(?i)preloaded') "Preloaded module failed for the wrong reason: $message"
        }
        finally { Remove-Module -ModuleInfo $loaded -Force -ErrorAction SilentlyContinue }

        [IO.File]::AppendAllText($fixture.core, '# substituted')
        $message = $null
        try {
            & $fixture.launcher -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository -BuildRoot $fixture.build `
                -OutputRoot $fixture.output -ScratchRoot $fixture.scratch | Out-Null
        }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)recipe.+(size|SHA-256)|(?:size|SHA-256).+recipe') `
            "Recipe substitution failed for the wrong reason: $message"
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'launcher binds each authenticated file handle to the pathname imported later' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $temp = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-v2-launcher-path-binding-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository' {
            param($FixtureLauncher, $CorePath, $PackageRoot)
            $substitute = Join-Path $PackageRoot 'substituted-core.psm1'
            [IO.File]::WriteAllText($substitute, @'
function Assert-M0QtShellCompositePackageReceipt { [pscustomobject]@{ verified = $true } }
function Invoke-M0QtShellCompositePackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$ContractPath,
        [Parameter(Mandatory)][string]$ExpectedContractSha256,
        [Parameter(Mandatory)][string]$CompositeReceiptPath,
        [Parameter(Mandatory)][string]$ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)][string]$ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)][string]$BuildRoot,
        [Parameter(Mandatory)][string]$OutputRoot,
        [Parameter(Mandatory)][string]$ScratchRoot
    )
    [pscustomobject][ordered]@{ invoked = 'substituted' }
}
Export-ModuleMember -Function Assert-M0QtShellCompositePackageReceipt,Invoke-M0QtShellCompositePackage
'@, [Text.UTF8Encoding]::new($false))
            $launcherText = [IO.File]::ReadAllText($FixtureLauncher)
            $marker = '    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_FILE_OPEN'
            Need ($launcherText.Contains($marker, [StringComparison]::Ordinal)) `
                'Launcher file-open mutation seam is missing.'
            $injection = @'
    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_FILE_OPEN
    if ($Context -ceq "Package-v2 recipe 'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackage.psm1'") {
        $authenticatedTarget = Join-Path ([IO.Path]::GetDirectoryName($path)) 'authenticated-core-target.psm1'
        [IO.File]::Copy($path, $authenticatedTarget, $false)
        [IO.File]::Copy((Join-Path ([IO.Path]::GetDirectoryName($path)) 'substituted-core.psm1'), $path, $true)
        $pathToOpen = $authenticatedTarget
    }
'@
            [IO.File]::WriteAllText(
                $FixtureLauncher,
                $launcherText.Replace($marker, $injection.TrimEnd("`r", "`n")),
                [Text.UTF8Encoding]::new($false))
        }
        $message = $null
        $rejected = $false
        try {
            $result = & $fixture.launcher -ContractPath $fixture.contract `
                -ExpectedContractSha256 $fixture.contract_sha256 `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository -BuildRoot $fixture.build `
                -OutputRoot $fixture.output -ScratchRoot $fixture.scratch
            if ($result.invoked -ceq 'substituted') {
                $message = 'Launcher accepted a different pathname leaf after authenticating another handle.'
            }
        }
        catch { $rejected = $true; $message = $_.Exception.Message }
        Need $rejected "Launcher executed the substituted pathname leaf: $message"
        Need ($message -match '(?i)(final physical path|physical alias|pathname|different.*handle)') `
            "Path/handle substitution was not rejected causally: $message"
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'original and relocated spaces accents packages execute identically without cwd dependence' {
    Need ([IO.File]::Exists($smokeModule)) 'Smoke module is missing.'
    $module = Import-Module $smokeModule -Force -PassThru
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-relocation-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $original = New-PackageFixture $temp 'original'
        $relocated = New-PackageFixture $temp 'relocação com espaços e acentos'
        $originalResult = Invoke-FakeSmoke $original (Join-Path $temp 'workspace-original')
        $relocatedResult = Invoke-FakeSmoke $relocated (Join-Path $temp 'workspace-relocado')
        Same $originalResult.result.package_root ([IO.Path]::GetFullPath($original)) 'Original root drifted.'
        Same $relocatedResult.result.package_root ([IO.Path]::GetFullPath($relocated)) 'Relocated root drifted.'
        Same (@($originalResult.result.journeys).Count) 2 'Original journey count drifted.'
        Same (@($relocatedResult.result.journeys).Count) 2 'Relocated journey count drifted.'
        Same (($originalResult.events | Where-Object { $_ -like 'verify:*' }).Count) 5 `
            'Original per-journey/final verification count drifted.'
        Same (($relocatedResult.events | Where-Object { $_ -like 'verify:*' }).Count) 5 `
            'Relocated per-journey/final verification count drifted.'
        Need (-not ([IO.File]::ReadAllText($smokeModule).Contains('Set-Location', [StringComparison]::OrdinalIgnoreCase))) `
            'Smoke depends on ambient current directory.'
    }
    finally {
        if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) }
        Remove-Module $module -Force
    }
}

Case 'wrong external contract hash fails before any recipe import or output creation' {
    Need ([IO.File]::Exists($launcher)) 'Package-v2 launcher is missing.'
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-launcher-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        $fixture = New-LauncherFixture $temp 'repository'
        $message = $null
        try {
            & $fixture.launcher -ContractPath $fixture.contract -ExpectedContractSha256 ('0' * 64) `
                -CompositeReceiptPath $fixture.composite_receipt `
                -ExpectedCompositeReceiptSha256 $fixture.composite_receipt_sha256 `
                -ExpectedQtBaseReceiptSha256 $fixture.qtbase_receipt_sha256 `
                -RepositoryRoot $fixture.repository -BuildRoot $fixture.build `
                -OutputRoot $fixture.output -ScratchRoot $fixture.scratch
            throw 'Launcher accepted the wrong external contract hash.'
        }
        catch { $message = $_.Exception.Message }
        Need ($message -match '(?i)contract.+SHA-256|SHA-256.+contract') `
            "Launcher failed for the wrong reason: $message"
        Need (-not [IO.Directory]::Exists($fixture.build)) 'Hash failure created BuildRoot.'
        Need (-not [IO.Directory]::Exists($fixture.output)) 'Hash failure created OutputRoot.'
        Need (-not [IO.Directory]::Exists($fixture.scratch)) 'Hash failure created ScratchRoot.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

if ($failures.Count) { throw "Package v2 relocation Reds: $($failures.Count) failure(s)." }
