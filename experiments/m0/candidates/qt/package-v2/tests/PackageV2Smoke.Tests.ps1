#Requires -Version 7.2

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path $PSScriptRoot '../M0.QtShellCompositePackageSmoke.psm1'
$script:SmokeModule = Import-Module $modulePath -Force -PassThru
$script:Passed = 0
$script:Failed = 0

function Invoke-TestCase {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [scriptblock]$Body
    )

    try {
        & $Body
        $script:Passed++
        "PASS: $Name"
    }
    catch {
        $script:Failed++
        "FAIL: $Name :: $($_.Exception.Message)"
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory)]
        [bool]$Condition,

        [Parameter(Mandatory)]
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Remove-TestDirectory {
    param(
        [Parameter(Mandatory)]
        [string]$LiteralPath
    )

    if ([IO.Directory]::Exists($LiteralPath)) {
        [IO.Directory]::Delete($LiteralPath, $true)
    }
}

function New-SmokeFixture {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-smoke-' + [Guid]::NewGuid().ToString('N'))
    $packageRoot = Join-Path $root 'package'
    $appRoot = Join-Path $packageRoot 'app'
    $evidenceRoot = Join-Path $packageRoot 'evidence'
    [IO.Directory]::CreateDirectory($appRoot) | Out-Null
    [IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null

    $executablePath = Join-Path $appRoot 'm0_qt_shell.exe'
    $receiptPath = Join-Path $evidenceRoot 'qt-shell-composite-package-receipt.v2.json'
    $manifestPath = Join-Path $evidenceRoot 'manifest.json'
    [IO.File]::WriteAllText($executablePath, 'x')
    [IO.File]::WriteAllText($receiptPath, '{}')
    [IO.File]::WriteAllText($manifestPath, '{}')

    return [pscustomobject][ordered]@{
        root = $root
        package_root = $packageRoot
        app_root = $appRoot
        receipt_path = $receiptPath
        manifest_path = $manifestPath
        events = [Collections.Generic.List[string]]::new()
    }
}

function New-SmokeParameters {
    param(
        [Parameter(Mandatory)]
        $Fixture
    )

    $requiredNames = @(
        'm0_qt_shell.exe',
        'Qt6Core.dll',
        'Qt6Gui.dll',
        'Qt6Widgets.dll',
        'Qt6Quick.dll',
        'qoffscreen.dll'
    )
    $payload = @($requiredNames | ForEach-Object { "app/$_" })
    $verifyReceipt = {
        param($phase)
        $Fixture.events.Add("verify:$phase")
        return [pscustomobject][ordered]@{
            verified = $true
            package_id = 'pkg'
            contract_sha256 = 'b' * 64
            composite_receipt_sha256 = 'c' * 64
            manifest_sha256 = 'd' * 64
            receipt_sha256 = 'a' * 64
            manifest_path = 'evidence/manifest.json'
            payload = $payload
            evidence = @()
        }
    }.GetNewClosure()
    $runJourney = {
        param($executable, $arguments, $workingDirectory, $environment, $timeoutMilliseconds)
        $Fixture.events.Add("run:$($arguments[-1])")
        $modulePaths = @($requiredNames | ForEach-Object { Join-Path $Fixture.app_root $_ })
        $modulePaths += Join-Path (Join-Path $environment.SystemRoot 'System32') 'vcruntime140.dll'
        return [pscustomobject][ordered]@{
            exit_code = [long]0
            stdout = ''
            stderr = ''
            timed_out = $false
            capture_elapsed_ms = [double]20
            module_paths = $modulePaths
            survivor_count = [long]0
        }
    }.GetNewClosure()

    return @{
        PackageRoot = $Fixture.package_root
        ReceiptPath = $Fixture.receipt_path
        ExpectedReceiptSha256 = 'a' * 64
        ContractPath = 'contract.json'
        ExpectedContractSha256 = 'b' * 64
        CompositeReceiptPath = 'composite-receipt.json'
        ExpectedCompositeReceiptSha256 = 'c' * 64
        RepositoryRoot = $Fixture.root
        VerifyReceiptAction = $verifyReceipt
        ProcessOperations = @{
            RunJourney = $runJourney
            Terminate = {
                param($session)
                $Fixture.events.Add('terminate')
            }.GetNewClosure()
            Dispose = {
                param($session)
                $Fixture.events.Add('dispose')
            }.GetNewClosure()
        }
        WorkspaceFactory = {
            Join-Path $Fixture.root ('workspace-' + [Guid]::NewGuid().ToString('N'))
        }.GetNewClosure()
    }
}

function Assert-SmokeRejected {
    param(
        [Parameter(Mandatory)]
        [scriptblock]$Mutate
    )

    $fixture = New-SmokeFixture
    try {
        $arguments = New-SmokeParameters -Fixture $fixture
        & $Mutate $fixture $arguments
        $rejected = $false
        try {
            Test-M0QtShellCompositePackageSmoke @arguments | Out-Null
        }
        catch {
            $rejected = $true
        }
        Assert-True $rejected 'Expected the smoke contract to reject the fixture.'
    }
    finally {
        Remove-TestDirectory -LiteralPath $fixture.root
    }
}

function Get-NativeFixtureToolchain {
    $packageDirectory = Split-Path $PSScriptRoot -Parent
    $contractPath = Join-Path $packageDirectory 'qt-shell-composite-package-contract.v2.json'
    $contract = [IO.File]::ReadAllText($contractPath) | ConvertFrom-Json
    $compilerPath = [string]@($contract.inputs.tools | Where-Object id -CEQ 'cl')[0].path
    $resourceCompilerPath = [string]@($contract.inputs.tools | Where-Object id -CEQ 'rc')[0].path
    $windowsSdkVersion = [string]$contract.product.windows_sdk_version

    $vcRoot = $compilerPath
    foreach ($unused in 1..4) {
        $vcRoot = Split-Path $vcRoot -Parent
    }
    $windowsSdkRoot = $resourceCompilerPath
    foreach ($unused in 1..4) {
        $windowsSdkRoot = Split-Path $windowsSdkRoot -Parent
    }
    return [pscustomobject][ordered]@{
        compiler_path = $compilerPath
        compiler_directory = Split-Path $compilerPath -Parent
        vc_root = $vcRoot
        windows_sdk_root = $windowsSdkRoot
        windows_sdk_version = $windowsSdkVersion
    }
}

function New-DelayedModuleHost {
    param(
        [Parameter(Mandatory)]
        [string]$Root
    )

    $toolchain = Get-NativeFixtureToolchain
    foreach ($requiredPath in @(
            $toolchain.compiler_path,
            (Join-Path $toolchain.vc_root 'include/excpt.h'),
            (Join-Path $toolchain.windows_sdk_root "Lib/$($toolchain.windows_sdk_version)/um/x64/kernel32.lib"))) {
        Assert-True ([IO.File]::Exists($requiredPath)) "Native fixture prerequisite is absent: $requiredPath"
    }

    $sourcePath = Join-Path $Root 'DelayedModuleHost.cpp'
    $objectPath = Join-Path $Root 'DelayedModuleHost.obj'
    $executablePath = Join-Path $Root 'DelayedModuleHost.exe'
    $source = @'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void WINAPI SmokeEntry()
{
    wchar_t modulePath[512];
    DWORD length = GetEnvironmentVariableW(L"DELAYED_SMOKE_MODULE", modulePath, 512);
    if (length == 0 || length >= 512)
    {
        ExitProcess(2);
    }

    Sleep(75);
    HMODULE module = LoadLibraryW(modulePath);
    DWORD result = module == NULL ? GetLastError() : 0;
    Sleep(600);
    ExitProcess(result);
}
'@
    [IO.File]::WriteAllText($sourcePath, $source, [Text.UTF8Encoding]::new($false))

    $sdkIncludeRoot = Join-Path $toolchain.windows_sdk_root "include/$($toolchain.windows_sdk_version)"
    $compilerArguments = @(
        '/nologo',
        '/O2',
        '/GS-',
        '/EHsc-',
        '/GR-',
        '/DUNICODE',
        '/D_UNICODE',
        ('/I' + (Join-Path $toolchain.vc_root 'include')),
        ('/I' + (Join-Path $sdkIncludeRoot 'um')),
        ('/I' + (Join-Path $sdkIncludeRoot 'shared')),
        ('/I' + (Join-Path $sdkIncludeRoot 'ucrt')),
        ('/Fo:' + $objectPath),
        ('/Fe:' + $executablePath),
        $sourcePath,
        '/link',
        '/SUBSYSTEM:CONSOLE',
        '/ENTRY:SmokeEntry',
        '/NODEFAULTLIB',
        ('/LIBPATH:' + (Join-Path $toolchain.windows_sdk_root "Lib/$($toolchain.windows_sdk_version)/um/x64")),
        'kernel32.lib'
    )

    $originalPath = $env:PATH
    try {
        $env:PATH = $toolchain.compiler_directory + ';' + (Join-Path $env:SystemRoot 'System32')
        $compilerOutput = @(& $toolchain.compiler_path @compilerArguments 2>&1)
        $compilerExitCode = $LASTEXITCODE
    }
    finally {
        $env:PATH = $originalPath
    }
    Assert-True ($compilerExitCode -eq 0) `
        "Native delayed-module fixture failed to compile: $($compilerOutput -join [Environment]::NewLine)"
    Assert-True ([IO.File]::Exists($executablePath)) 'Native delayed-module fixture produced no executable.'
    return $executablePath
}

function New-RealEnvironment {
    param(
        [Parameter(Mandatory)]
        [string]$Root
    )

    return @{
        SystemRoot = $env:SystemRoot
        WINDIR = $env:SystemRoot
        PATH = Join-Path $env:SystemRoot 'System32'
        TEMP = $Root
        TMP = $Root
    }
}

Invoke-TestCase 'API and verification order are exact' {
    $fixture = New-SmokeFixture
    try {
        $arguments = New-SmokeParameters -Fixture $fixture
        $result = Test-M0QtShellCompositePackageSmoke @arguments
        Assert-True (($script:SmokeModule.ExportedFunctions.Keys -join ',') -ceq
            'Test-M0QtShellCompositePackageSmoke') 'The public API drifted.'
        $expectedEvents = @(
            'verify:tray-lifecycle-pre',
            'run:--m0-smoke=tray-lifecycle',
            'dispose',
            'verify:tray-lifecycle-post',
            'verify:error-recovery-pre',
            'run:--m0-smoke=error-recovery',
            'dispose',
            'verify:error-recovery-post',
            'verify:final'
        ) -join ','
        Assert-True (($fixture.events -join ',') -ceq $expectedEvents) `
            "Verification order drifted: $($fixture.events -join ',')"
        Assert-True ($result.journeys.Count -eq 2) 'Smoke journey count drifted.'
        Assert-True ((@($result.journeys.journey) -join ',') -ceq 'tray-lifecycle,error-recovery') `
            'Smoke journey identities drifted.'
        $expectedResultFields = @(
            'verified', 'package_id', 'package_root', 'contract_sha256',
            'composite_receipt_sha256', 'manifest_sha256', 'receipt_sha256',
            'journeys', 'workspace', 'limits'
        ) -join ','
        Assert-True (($result.PSObject.Properties.Name -join ',') -ceq $expectedResultFields) `
            'Top-level smoke result is open or reordered.'
        foreach ($journey in $result.journeys) {
            $expectedJourneyFields = @(
                'journey', 'working_directory', 'exit_code', 'stdout', 'stderr',
                'timed_out', 'module_capture_ms', 'last_module_sample_ms',
                'module_paths', 'survivor_count'
            ) -join ','
            Assert-True (($journey.PSObject.Properties.Name -join ',') -ceq $expectedJourneyFields) `
                "Journey result shape drifted for $($journey.journey)."
            Assert-True ($journey.exit_code -eq 0 -and $journey.stdout -ceq '' -and
                $journey.stderr -ceq '' -and -not $journey.timed_out -and
                $journey.survivor_count -eq 0) "Journey evidence is not successful for $($journey.journey)."
            Assert-True (@($journey.module_paths | Where-Object { -not [IO.Path]::IsPathRooted($_) }).Count -eq 0) `
                "Journey module evidence contains a non-canonical path for $($journey.journey)."
        }
    }
    finally {
        Remove-TestDirectory -LiteralPath $fixture.root
    }
}

Invoke-TestCase 'wrong receipt path is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $arguments.ReceiptPath = $fixture.manifest_path
    }
}

Invoke-TestCase 'open verifier result is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $arguments.VerifyReceiptAction = {
            param($phase)
            [pscustomobject]@{ verified = $true }
        }
    }
}

Invoke-TestCase 'package-relative verifier manifest is required' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.VerifyReceiptAction
        $arguments.VerifyReceiptAction = {
            param($phase)
            $result = & $original $phase
            $result.manifest_path = $fixture.manifest_path
            return $result
        }.GetNewClosure()
    }
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.VerifyReceiptAction
        $arguments.VerifyReceiptAction = {
            param($phase)
            $result = & $original $phase
            $result.manifest_path = 'evidence/../outside.json'
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'verifier binding drift is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $state = [pscustomobject]@{ call_count = 0 }
        $original = $arguments.VerifyReceiptAction
        $arguments.VerifyReceiptAction = {
            param($phase)
            $state.call_count++
            $result = & $original $phase
            if ($state.call_count -gt 1) {
                $result.package_id = 'other'
            }
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'module absent from authenticated payload is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.VerifyReceiptAction
        $arguments.VerifyReceiptAction = {
            param($phase)
            $result = & $original $phase
            $result.payload = @($result.payload | Where-Object { $_ -notlike '*Qt6Core*' })
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'external Qt module is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            [pscustomobject]@{
                exit_code = 0
                stdout = ''
                stderr = ''
                timed_out = $false
                capture_elapsed_ms = 1
                module_paths = @($executable, 'C:\evil\Qt6Core.dll')
                survivor_count = 0
            }
        }
    }
}

Invoke-TestCase 'external non-Windows module is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            [pscustomobject]@{
                exit_code = 0
                stdout = ''
                stderr = ''
                timed_out = $false
                capture_elapsed_ms = 1
                module_paths = @($executable, 'C:\evil\other.dll')
                survivor_count = 0
            }
        }
    }
}

Invoke-TestCase 'module in writable Windows subtree is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.ProcessOperations.RunJourney
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            $result = & $original $executable $journeyArguments $workingDirectory $environment $timeoutMilliseconds
            $result.module_paths += Join-Path $environment.SystemRoot 'Temp/evil.dll'
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase '500ms module-capture boundary is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.ProcessOperations.RunJourney
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            $result = & $original $executable $journeyArguments $workingDirectory $environment $timeoutMilliseconds
            $result.capture_elapsed_ms = 500
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'post-verification failure emits no result' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.VerifyReceiptAction
        $arguments.VerifyReceiptAction = {
            param($phase)
            $result = & $original $phase
            if ($phase -like '*-post') {
                $result.verified = $false
            }
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'reported survivor is rejected, terminated, and disposed' {
    $fixture = New-SmokeFixture
    try {
        $arguments = New-SmokeParameters -Fixture $fixture
        $original = $arguments.ProcessOperations.RunJourney
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            $result = & $original $executable $journeyArguments $workingDirectory $environment $timeoutMilliseconds
            $result.survivor_count = 1
            return $result
        }.GetNewClosure()

        $rejected = $false
        try {
            Test-M0QtShellCompositePackageSmoke @arguments | Out-Null
        }
        catch {
            $rejected = $true
        }
        Assert-True $rejected 'A nonzero survivor count was accepted.'
        Assert-True ($fixture.events.Contains('terminate')) 'The failed journey was not terminated.'
        Assert-True ($fixture.events.Contains('dispose')) 'The failed journey was not disposed.'
    }
    finally {
        Remove-TestDirectory -LiteralPath $fixture.root
    }
}

Invoke-TestCase 'workspace inside package is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $arguments.WorkspaceFactory = { $fixture.package_root }.GetNewClosure()
    }
}

Invoke-TestCase 'workspace collision is rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $existing = Join-Path $fixture.root 'existing'
        [IO.Directory]::CreateDirectory($existing) | Out-Null
        $arguments.WorkspaceFactory = { $existing }.GetNewClosure()
    }
}

Invoke-TestCase 'timeout and process output are rejected' {
    Assert-SmokeRejected {
        param($fixture, $arguments)
        $original = $arguments.ProcessOperations.RunJourney
        $arguments.ProcessOperations.RunJourney = {
            param($executable, $journeyArguments, $workingDirectory, $environment, $timeoutMilliseconds)
            $result = & $original $executable $journeyArguments $workingDirectory $environment $timeoutMilliseconds
            $result.timed_out = $true
            $result.stdout = 'bad'
            return $result
        }.GetNewClosure()
    }
}

Invoke-TestCase 'real adapter assigns suspended tree to kill-on-close job before execution' {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-smoke-real-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    try {
        $sentinel = Join-Path $root 'escaped.txt'
        $escapedSentinel = $sentinel.Replace("'", "''")
        $childCommand = "Start-Sleep -Milliseconds 800; [IO.File]::WriteAllText('$escapedSentinel','escaped')"
        $escapedPowerShell = $PSHOME.Replace("'", "''")
        $escapedChildCommand = $childCommand.Replace("'", "''")
        $rootCommand = "Start-Process -FilePath '$escapedPowerShell\pwsh.exe' " +
            "-ArgumentList @('-NoProfile','-Command','$escapedChildCommand'); Start-Sleep -Seconds 30"
        $operations = & $script:SmokeModule { New-M0RealProcessOperations }
        $session = & $operations.RunJourney `
            (Join-Path $PSHOME 'pwsh.exe') `
            @('-NoProfile', '-Command', $rootCommand) `
            $root `
            (New-RealEnvironment -Root $root) `
            150
        try {
            Assert-True $session.timed_out 'Real adapter did not exercise timeout cleanup.'
            Assert-True ($session.survivor_count -eq 0) 'Real adapter reported survivors.'
            Assert-True ($session.capture_elapsed_ms -lt 500) 'Real adapter crossed the capture boundary.'
        }
        finally {
            & $operations.Dispose $session
        }
        Start-Sleep -Milliseconds 1200
        Assert-True (-not [IO.File]::Exists($sentinel)) 'A descendant escaped Job Object cleanup.'
    }
    finally {
        Remove-TestDirectory -LiteralPath $root
    }
}

Invoke-TestCase 'real adapter accumulates a native module loaded after its first sample' {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-smoke-modules-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    try {
        $hostPath = New-DelayedModuleHost -Root $root
        $delayedModulePath = Join-Path $root 'DelayedSmokeModule.dll'
        [IO.File]::Copy((Join-Path $env:SystemRoot 'System32/version.dll'), $delayedModulePath)
        $environment = New-RealEnvironment -Root $root
        $environment.DELAYED_SMOKE_MODULE = $delayedModulePath
        $operations = & $script:SmokeModule { New-M0RealProcessOperations }
        $session = & $operations.RunJourney $hostPath @('ignored') $root $environment 1200
        try {
            Assert-True ($session.capture_elapsed_ms -lt 500) `
                'Module capture crossed the strict 500ms boundary.'
            Assert-True ($session.last_sample_elapsed_ms -le $session.capture_elapsed_ms) `
                'Last successful sample timestamp exceeds total sampler duration.'
            $delayedCount = @($session.module_paths | Where-Object {
                    [IO.Path]::GetFileName($_) -ceq 'DelayedSmokeModule.dll'
                }).Count
            Assert-True ($delayedCount -eq 1) `
                "The module loaded after 75ms was omitted (capture=$($session.capture_elapsed_ms)ms)."
            $firstSampleDelayedCount = @($session.first_sample_module_paths | Where-Object {
                    [IO.Path]::GetFileName($_) -ceq 'DelayedSmokeModule.dll'
                }).Count
            Assert-True ($firstSampleDelayedCount -eq 0) `
                'The delayed module was already present in the suspended-process baseline sample.'
            Assert-True ($session.sample_count -gt 1) 'The adapter did not take a later module sample.'
            Assert-True ($session.exit_code -eq 0) 'The native delayed-module host failed.'
            Assert-True (-not $session.timed_out) 'The native delayed-module host timed out.'
            Assert-True ($session.survivor_count -eq 0) 'The native delayed-module host left survivors.'
        }
        finally {
            & $operations.Dispose $session
        }
    }
    finally {
        Remove-TestDirectory -LiteralPath $root
    }
}

Invoke-TestCase 'assignment failure terminates the suspended primary outside the job' {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-smoke-assign-failure-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    try {
        $nativeType = & $script:SmokeModule { $script:NativeSession }
        $bindingFlags = [Reflection.BindingFlags]::NonPublic -bor [Reflection.BindingFlags]::Static
        $startCore = $nativeType.GetMethod('StartCore', $bindingFlags)
        Assert-True ($null -ne $startCore) 'The native start core has no injectable assignment boundary.'

        $rejectAssignment = [Func[IntPtr, IntPtr, bool]] {
            param([IntPtr]$job, [IntPtr]$process)
            return $false
        }
        $invokeArguments = [object[]]::new(7)
        $invokeArguments[0] = [string](Join-Path $PSHOME 'pwsh.exe')
        $invokeArguments[1] = [string[]]@('-NoProfile', '-Command', 'Start-Sleep -Seconds 30')
        $invokeArguments[2] = [string]$root
        $invokeArguments[3] = [Collections.IDictionary](New-RealEnvironment -Root $root)
        $invokeArguments[4] = [string](Join-Path $root 'stdout.bin')
        $invokeArguments[5] = [string](Join-Path $root 'stderr.bin')
        $invokeArguments[6] = $rejectAssignment

        $failure = $null
        try {
            $startCore.Invoke($null, $invokeArguments) | Out-Null
        }
        catch {
            $failure = $_.Exception.GetBaseException()
        }
        Assert-True ($null -ne $failure) 'The forced assignment failure unexpectedly succeeded.'
        Assert-True ($failure.Message -match 'PID (?<pid>[0-9]+)') `
            "The assignment failure did not identify its primary PID: $($failure.Message)"

        $primaryPid = [int]$Matches.pid
        $primaryExited = $false
        try {
            $primary = [Diagnostics.Process]::GetProcessById($primaryPid)
            try {
                $primaryExited = $primary.WaitForExit(2000)
            }
            finally {
                $primary.Dispose()
            }
        }
        catch [ArgumentException] {
            $primaryExited = $true
        }
        Assert-True $primaryExited 'The unassigned suspended primary process survived cleanup.'
    }
    finally {
        Remove-TestDirectory -LiteralPath $root
    }
}

Invoke-TestCase 'native job reports live tree and proves zero after termination' {
    $root = Join-Path ([IO.Path]::GetTempPath()) ('m0-smoke-job-count-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $native = $null
    try {
        $childCommand = 'Start-Sleep -Seconds 30'
        $escapedPowerShell = $PSHOME.Replace("'", "''")
        $escapedChildCommand = $childCommand.Replace("'", "''")
        $rootCommand = "Start-Process -FilePath '$escapedPowerShell\pwsh.exe' " +
            "-ArgumentList @('-NoProfile','-Command','$escapedChildCommand'); Start-Sleep -Seconds 30"
        $nativeType = & $script:SmokeModule { $script:NativeSession }
        $native = $nativeType::Start(
            (Join-Path $PSHOME 'pwsh.exe'),
            [string[]]@('-NoProfile', '-Command', $rootCommand),
            $root,
            [Collections.IDictionary](New-RealEnvironment -Root $root),
            (Join-Path $root 'stdout.bin'),
            (Join-Path $root 'stderr.bin'))
        $native.Resume()

        $activeProcesses = [uint32]0
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while ($clock.ElapsedMilliseconds -lt 3000 -and $activeProcesses -lt 2) {
            $activeProcesses = $native.GetActiveProcessCount()
            if ($activeProcesses -lt 2) {
                Start-Sleep -Milliseconds 10
            }
        }
        Assert-True ($activeProcesses -ge 2) 'The Job Object did not report root plus descendant.'

        $survivorCount = $native.TerminateTreeAndCountSurvivors(5000)
        Assert-True ($survivorCount -eq 0) 'The Job Object still reports active processes after termination.'
    }
    finally {
        if ($null -ne $native) {
            $native.Dispose()
        }
        Remove-TestDirectory -LiteralPath $root
    }
}

"Tests: $($script:Passed) passed, $($script:Failed) failed."
if ($script:Failed -ne 0) {
    exit 1
}
