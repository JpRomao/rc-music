#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$modulePath = Join-Path $packageRoot 'M0.QtShellCompositePackage.psm1'
$module = Import-Module $modulePath -Force -PassThru
$state = @{ Passed = 0; Failed = 0 }
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-workflow-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}

function Assert-Equal($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', found '$Actual'." }
}

function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Wrong rejection: $($_.Exception.Message)"
        }
        return
    }
    throw 'Expected rejection.'
}

function Invoke-Private([string] $Name, [hashtable] $Arguments) {
    return & $module {
        param($FunctionName, $NamedArguments)
        $command = Get-Command -Name $FunctionName -CommandType Function -ErrorAction Stop
        & $command @NamedArguments
    } $Name $Arguments
}

function New-WorkflowFixture {
    $events = [Collections.Generic.List[string]]::new()
    $receiptHash = '9' * 64
    $outputRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot 'app')) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot 'evidence')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $outputRoot 'app/m0_qt_shell.exe'), 'x')
    [IO.File]::WriteAllText((Join-Path $outputRoot 'evidence/qt-shell-composite-package-manifest.v2.json'), 'x')
    [IO.File]::WriteAllText((Join-Path $outputRoot 'evidence/qt-shell-composite-package-receipt.v2.json'), 'x')
    $closedResult = [pscustomobject][ordered]@{
        verified = $true
        package_id = 'm0.2-qt-shell-composite-package-v2'
        contract_sha256 = 'a' * 64
        composite_receipt_sha256 = 'b' * 64
        manifest_sha256 = 'c' * 64
        receipt_sha256 = $receiptHash
        manifest_path = 'evidence/qt-shell-composite-package-manifest.v2.json'
        payload = @([pscustomobject]@{
                relative_path = 'app/m0_qt_shell.exe'; size_bytes = [long]1; sha256 = 'd' * 64
            })
        evidence = @(
            [pscustomobject]@{
                relative_path = 'evidence/qt-shell-composite-package-manifest.v2.json'
                size_bytes = [long]1; sha256 = 'c' * 64
            },
            [pscustomobject]@{
                relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'
                size_bytes = [long]1; sha256 = $receiptHash
            })
    }
    $context = [pscustomobject][ordered]@{
        output_root = $outputRoot
        expected_contract_sha256 = 'a' * 64
        expected_composite_receipt_sha256 = 'b' * 64
    }
    $operations = [pscustomobject][ordered]@{
        verify_composite = {
            param($Context, $Phase)
            $events.Add("verify-composite-$Phase")
            [pscustomobject]@{ verified = $true; sdk_root = 'C:\repo\q\run\sdk' }
        }.GetNewClosure()
        build_candidate = {
            param($Context)
            $events.Add('build-candidate')
            [pscustomobject][ordered]@{
                source_root = 'C:\repo\out\build\package-source'
                manifest_relative_path = 'evidence/qt-shell-composite-package-manifest.v2.json'
                receipt_relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'
                receipt_sha256 = $receiptHash
                entries = @(
                    [pscustomobject]@{
                        relative_path = 'app/m0_qt_shell.exe'
                        size_bytes = [long]1
                        sha256 = 'd' * 64
                    },
                    [pscustomobject]@{
                        relative_path = 'evidence/qt-shell-composite-package-manifest.v2.json'
                        size_bytes = [long]1
                        sha256 = 'c' * 64
                    },
                    [pscustomobject]@{
                        relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'
                        size_bytes = [long]1
                        sha256 = $receiptHash
                    })
            }
        }.GetNewClosure()
        publish = {
            param($Context, $Candidate)
            $events.Add('publish-native')
            [pscustomobject]@{ exit_code = [long]0; package_root = '.' }
        }.GetNewClosure()
        verify_package = {
            param($Context, $Candidate)
            $events.Add('verify-package')
            $closedResult
        }.GetNewClosure()
    }
    return [pscustomobject]@{
        events = $events
        context = $context
        operations = $operations
        result = $closedResult
    }
}

Test-Case 'public production boundary is exactly nine physical paths and anchors' {
    $command = $module.ExportedFunctions['Invoke-M0QtShellCompositePackage']
    if ($null -eq $command) { throw 'Expected public production export.' }
    $common = @(
        'Verbose', 'Debug', 'ErrorAction', 'WarningAction', 'InformationAction',
        'ProgressAction', 'ErrorVariable', 'WarningVariable', 'InformationVariable',
        'OutVariable', 'OutBuffer', 'PipelineVariable'
    )
    [string[]] $actual = @($command.Parameters.Keys | Where-Object { $_ -notin $common })
    [string[]] $expected = @(
        'ContractPath', 'ExpectedContractSha256', 'CompositeReceiptPath',
        'ExpectedCompositeReceiptSha256', 'ExpectedQtBaseReceiptSha256',
        'RepositoryRoot', 'BuildRoot', 'OutputRoot', 'ScratchRoot'
    )
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [Array]::Sort($expected, [StringComparer]::Ordinal)
    Assert-Equal ($actual -join ',') ($expected -join ',') 'Public parameter surface drifted.'
    foreach ($name in $expected) {
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory
            }).Count -eq 1
        if (-not $mandatory) { throw "Public parameter '$name' is optional." }
    }
}

Test-Case 'public receipt verifier requires its exact eight physical paths and anchors' {
    $command = $module.ExportedFunctions['Assert-M0QtShellCompositePackageReceipt']
    if ($null -eq $command) { throw 'Expected public receipt verifier export.' }
    $common = @(
        'Verbose', 'Debug', 'ErrorAction', 'WarningAction', 'InformationAction',
        'ProgressAction', 'ErrorVariable', 'WarningVariable', 'InformationVariable',
        'OutVariable', 'OutBuffer', 'PipelineVariable'
    )
    [string[]] $actual = @($command.Parameters.Keys | Where-Object { $_ -notin $common })
    [string[]] $expected = @(
        'LiteralPath', 'ExpectedReceiptSha256', 'ContractPath',
        'ExpectedContractSha256', 'CompositeReceiptPath',
        'ExpectedCompositeReceiptSha256', 'RepositoryRoot',
        'ExpectedQtBaseReceiptSha256'
    )
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [Array]::Sort($expected, [StringComparer]::Ordinal)
    Assert-Equal ($actual -join ',') ($expected -join ',') `
        'Public verifier parameter surface drifted.'
    foreach ($name in $expected) {
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory
            }).Count -eq 1
        if (-not $mandatory) { throw "Public verifier parameter '$name' is optional." }
    }
}

Test-Case 'private workflow verifies composite around all candidate work then publishes and verifies' {
    $fixture = New-WorkflowFixture
    $result = Invoke-Private 'Invoke-M0PackageV2ProductionWorkflow' @{
        Context = $fixture.context
        Operations = $fixture.operations
    }
    Assert-Equal ($fixture.events -join ',') `
        'verify-composite-pre,build-candidate,verify-composite-post,publish-native,verify-package' `
        'Production workflow order drifted.'
    Assert-Equal ($result.PSObject.Properties.Name -join ',') `
        'exit_code,package_root,receipt_path,receipt_sha256,manifest_path,manifest_sha256,entries' `
        'Closed build result shape drifted.'
    Assert-Equal $result.exit_code ([long]0) 'Closed build exit code drifted.'
    Assert-Equal $result.receipt_sha256 ('9' * 64) 'Closed build receipt hash drifted.'
    Assert-Equal (@($result.entries.relative_path) -join ',') `
        'app/m0_qt_shell.exe,evidence/qt-shell-composite-package-manifest.v2.json,evidence/qt-shell-composite-package-receipt.v2.json' `
        'Closed build inventory drifted.'
}

Test-Case 'workflow failures cannot publish or return early success' {
    foreach ($failurePoint in @('verify-composite-pre', 'build-candidate',
            'verify-composite-post', 'publish-native', 'verify-package')) {
        $fixture = New-WorkflowFixture
        $original = $fixture.operations
        foreach ($property in @($original.PSObject.Properties)) {
            $operationName = $property.Name
            $operation = $property.Value
            $original.$operationName = {
                param($Context, $Second)
                $event = switch ($operationName) {
                    'verify_composite' { "verify-composite-$Second" }
                    'build_candidate' { 'build-candidate' }
                    'publish' { 'publish-native' }
                    'verify_package' { 'verify-package' }
                }
                if ($event -ceq $failurePoint) {
                    $fixture.events.Add($event)
                    throw "intentional-$failurePoint"
                }
                & $operation $Context $Second
            }.GetNewClosure()
        }
        Assert-Rejected {
            Invoke-Private 'Invoke-M0PackageV2ProductionWorkflow' @{
                Context = $fixture.context
                Operations = $fixture.operations
            }
        } ([regex]::Escape("intentional-$failurePoint"))
        $publishCount = @($fixture.events | Where-Object { $_ -ceq 'publish-native' }).Count
        if ($failurePoint -cin @('verify-composite-pre', 'build-candidate', 'verify-composite-post') -and
            $publishCount -ne 0) {
            throw "Publication ran after '$failurePoint'."
        }
        if ($failurePoint -cne 'verify-package' -and
            @($fixture.events | Where-Object { $_ -ceq 'verify-package' }).Count -ne 0) {
            throw "Final verification ran after '$failurePoint'."
        }
    }
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ([IO.Directory]::Exists($testRoot)) { [IO.Directory]::Delete($testRoot, $true) }
if ($state.Failed -ne 0) { exit 1 }
