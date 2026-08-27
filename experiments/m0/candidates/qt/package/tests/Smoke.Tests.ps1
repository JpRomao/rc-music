#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$modulePath = Join-Path $packageSource 'QtShellPackageSmoke.psm1'
$wrapperPath = Join-Path $packageSource 'Test-QtShellPackageSmoke.ps1'
$script:passed = 0
$script:failed = 0
$script:smokeModule = $null

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) { throw $Message }
}
function Assert-Equal($Actual, $Expected, [string] $Message) {
    if (-not [object]::Equals($Actual, $Expected)) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}
function Assert-Throws([scriptblock] $Action, [string] $Pattern) {
    try { $null = & $Action }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Expected '$Pattern', got '$($_.Exception.Message)'."
        }
        return
    }
    throw "Expected an error matching '$Pattern'."
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try {
        & $Body
        $script:passed++
        Write-Output "[PASS] $Name"
    }
    catch {
        $script:failed++
        Write-Output "[FAIL] $Name`n$($_.Exception.Message)"
    }
}
function Invoke-SmokeInternal([string] $Name, [hashtable] $Arguments) {
    & $script:smokeModule { param($FunctionName, $Parameters) & $FunctionName @Parameters } $Name $Arguments
}
function New-ManifestFixture {
    $files = foreach ($relative in @(
        'm0_qt_shell.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll',
        'platforms/qoffscreen.dll', 'qml/QtQuick/qtquick2plugin.dll', 'imageformats/qsvg.dll'
    )) {
        [pscustomobject]@{ relative_path = $relative; size_bytes = 1L; sha256 = 'a' * 64 }
    }
    [pscustomobject]@{ schema_version = 1; metadata = [pscustomobject]@{}; files = @($files) }
}
function New-ModulePaths {
    @(
        'C:\package stage\m0_qt_shell.exe', 'C:\package stage\Qt6Core.dll',
        'C:\package stage\Qt6Gui.dll', 'C:\package stage\Qt6Widgets.dll',
        'C:\package stage\Qt6Quick.dll', 'C:\package stage\platforms\qoffscreen.dll',
        'C:\Windows\System32\kernel32.dll', 'C:\Windows\System32\vcruntime140.dll',
        'C:\Windows\System32\msvcp140.dll', 'C:\Windows\System32\ucrtbase.dll'
    )
}
function New-Observation([double] $Elapsed, [bool] $Exited = $false, [string[]] $Paths = @(),
                         [AllowNull()] [string] $CaptureError = $null) {
    [pscustomobject]@{
        elapsed_ms = $Elapsed; has_exited = $Exited; module_paths = @($Paths); capture_error = $CaptureError
    }
}
function New-FakeProcessOperations {
    param(
        [object[]] $Observations = @(
            (New-Observation 40 $false (New-ModulePaths)), (New-Observation 150 $true)
        ),
        [int] $ExitCode = 0,
        [string] $StandardOutput = '',
        [string] $StandardError = ''
    )
    $state = [pscustomobject]@{
        index = 0; alive = $false; starts = [Collections.Generic.List[object]]::new()
        events = [Collections.Generic.List[string]]::new(); kills = 0; disposed = 0
        current = $null; finish_calls = 0
    }
    $operations = @{
        Start = {
            param($StartInfo)
            $state.events.Add('start')
            $state.starts.Add($StartInfo)
            $state.index = 0
            $state.alive = $true
            $state.current = [pscustomobject]@{ process_id = 4200 + $state.starts.Count }
            return $state.current
        }.GetNewClosure()
        Observe = {
            param($Session)
            if (-not [object]::ReferenceEquals($Session, $state.current)) { throw 'Lost retained process identity.' }
            $state.events.Add('observe')
            if ($state.index -ge $Observations.Count) { throw 'Fake observation sequence exhausted.' }
            $observation = $Observations[$state.index++]
            $state.alive = -not $observation.has_exited
            return $observation
        }.GetNewClosure()
        Wait = {
            param($Session, $Milliseconds)
            if (-not [object]::ReferenceEquals($Session, $state.current)) { throw 'Wait changed the process identity.' }
            if ($Milliseconds -le 0 -or $Milliseconds -gt 10) { throw 'Unbounded polling wait.' }
            $state.events.Add('wait')
        }.GetNewClosure()
        Finish = {
            param($Session, $RemainingMilliseconds)
            if ($state.alive -or $RemainingMilliseconds -le 0) { throw 'Unbounded output drain or premature finish.' }
            $state.finish_calls++
            return [pscustomobject]@{ exit_code = $ExitCode; stdout = $StandardOutput; stderr = $StandardError }
        }.GetNewClosure()
        IsAlive = { param($Session) return $state.alive }.GetNewClosure()
        Terminate = {
            param($Session)
            if (-not [object]::ReferenceEquals($Session, $state.current)) { throw 'Kill changed the retained identity.' }
            $state.kills++
            $state.alive = $false
            $state.events.Add('terminate')
        }.GetNewClosure()
        Dispose = {
            param($Session)
            if (-not [object]::ReferenceEquals($Session, $state.current)) { throw 'Dispose changed identity.' }
            $state.disposed++
            $state.events.Add('dispose')
        }.GetNewClosure()
    }
    [pscustomobject]@{ State = $state; Operations = $operations }
}
function New-TestPolicy {
    Invoke-SmokeInternal 'New-QtShellSmokeModulePolicy' @{
        PackageRoot = 'C:\package stage'; WindowsRoot = 'C:\Windows'; Manifest = (New-ManifestFixture)
    }
}
function Invoke-FakeJourney($Fake, [int] $TimeoutMs = 1000, [int] $CaptureTimeoutMs = 500) {
    $environment = Invoke-SmokeInternal 'New-QtShellSmokeEnvironment' @{
        WindowsRoot = 'C:\Windows'; WorkingDirectory = 'C:\neutral smoke\tray-lifecycle'
    }
    Invoke-SmokeInternal 'Invoke-QtShellSmokeJourney' @{
        Journey = [pscustomobject]@{ id = 'tray-lifecycle'; arguments = @('--m0-smoke=tray-lifecycle') }
        PackageRoot = 'C:\package stage'; WorkingDirectory = 'C:\neutral smoke\tray-lifecycle'
        Environment = $environment; ModulePolicy = (New-TestPolicy); Operations = $Fake.Operations
        TimeoutMs = $TimeoutMs; CaptureTimeoutMs = $CaptureTimeoutMs
    }
}

Test-Case 'public wrapper and module are present and syntactically valid' {
    Assert-True (Test-Path -LiteralPath $wrapperPath -PathType Leaf) 'The smoke wrapper is missing.'
    Assert-True (Test-Path -LiteralPath $modulePath -PathType Leaf) 'The smoke module is missing.'
    foreach ($path in @($wrapperPath, $modulePath)) {
        $tokens = $null
        $errors = $null
        $null = [Management.Automation.Language.Parser]::ParseFile($path, [ref] $tokens, [ref] $errors)
        Assert-Equal @($errors).Count 0 "PowerShell syntax errors in $path."
    }
    $script:smokeModule = Import-Module -Name $modulePath -Force -PassThru
    Assert-Equal $script:smokeModule.ExportedCommands.Count 1 'Unexpected public seam.'
    Assert-True $script:smokeModule.ExportedCommands.ContainsKey('Test-QtShellPackageSmoke') 'Missing public smoke API.'
    $wrapper = Get-Command -Name $wrapperPath
    foreach ($name in @('PackageRoot', 'ManifestPath', 'ExpectedManifestSha256')) {
        Assert-True $wrapper.Parameters.ContainsKey($name) "Missing wrapper parameter: $name."
        $attributes = @($wrapper.Parameters[$name].Attributes | Where-Object { $_ -is [Management.Automation.ParameterAttribute] })
        Assert-True (@($attributes | Where-Object Mandatory).Count -gt 0) "$name is not mandatory."
    }
}

Test-Case 'environment is an isolated offscreen allowlist with no parent mutations' {
    $before = [Environment]::GetEnvironmentVariables('Process')
    $environment = Invoke-SmokeInternal 'New-QtShellSmokeEnvironment' @{
        WindowsRoot = 'C:\Windows'; WorkingDirectory = 'C:\neutral smoke'
    }
    Assert-Equal $environment['PATH'] 'C:\Windows\System32;C:\Windows' 'SDK/build paths leaked.'
    Assert-Equal $environment['QT_QPA_PLATFORM'] 'offscreen' 'Native GUI was not excluded.'
    Assert-Equal $environment['QT_QUICK_BACKEND'] 'software' 'The renderer is not explicitly software.'
    Assert-Equal $environment['QT_QUICK_CONTROLS_STYLE'] 'Basic' 'Wrong controls style.'
    Assert-Equal $environment['QT_FATAL_WARNINGS'] '1' 'Warnings are not fatal.'
    Assert-Equal $environment['QT_FORCE_STDERR_LOGGING'] '1' 'Missing stderr logging.'
    Assert-Equal $environment['QT_QPA_FONTDIR'] 'C:\Windows\Fonts' 'Fonts depend on the SDK.'
    Assert-Equal $environment['QML_DISABLE_DISK_CACHE'] '1' 'The smoke may leave a user QML cache.'
    Assert-Equal $environment['TEMP'] 'C:\neutral smoke\temp' 'TEMP is not isolated.'
    Assert-Equal $environment['APPDATA'] 'C:\neutral smoke\profile\AppData\Roaming' 'APPDATA is not isolated.'
    foreach ($forbidden in @('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QMLSCENE_DEVICE',
                              'QSG_INFO', 'QSG_RHI_BACKEND', 'QT_LOGGING_RULES', 'QT_DEBUG_PLUGINS', 'HTTP_PROXY')) {
        Assert-True (-not $environment.ContainsKey($forbidden)) "Inherited $forbidden survived."
    }
    $after = [Environment]::GetEnvironmentVariables('Process')
    Assert-Equal $before.Count $after.Count 'Parent environment count changed.'
    foreach ($name in $before.Keys) { Assert-Equal $after[$name] $before[$name] "Parent variable changed: $name." }
}

Test-Case 'start info uses only an absolute executable and separate arguments with hidden redirected pipes' {
    $info = Invoke-SmokeInternal 'New-QtShellSmokeStartInfo' @{
        ExecutablePath = 'C:\package stage\m0_qt_shell.exe'
        Arguments = @('--m0-scenario=unavailable', '--m0-smoke=error-recovery')
        WorkingDirectory = 'C:\neutral smoke'; Environment = @{ PATH = 'C:\Windows\System32;C:\Windows'; QT_QPA_PLATFORM = 'offscreen' }
    }
    Assert-Equal $info.FileName 'C:\package stage\m0_qt_shell.exe' 'Wrong executable.'
    Assert-Equal $info.WorkingDirectory 'C:\neutral smoke' 'Wrong cwd.'
    Assert-True (-not $info.UseShellExecute -and $info.CreateNoWindow) 'Shell or visible console execution enabled.'
    Assert-Equal $info.WindowStyle ([Diagnostics.ProcessWindowStyle]::Hidden) 'Window style is not hidden.'
    Assert-True ($info.RedirectStandardOutput -and $info.RedirectStandardError) 'Output pipes are missing.'
    Assert-Equal $info.ArgumentList.Count 2 'Arguments were concatenated.'
    Assert-Equal $info.ArgumentList[0] '--m0-scenario=unavailable' 'Scenario argument changed.'
    Assert-Equal $info.Environment.Count 2 'Ambient variables leaked into the child.'
}

Test-Case 'module snapshot proves staged Qt and reports external System32 CRT' {
    $snapshot = Invoke-SmokeInternal 'Read-QtShellSmokeModuleSnapshot' @{
        Policy = (New-TestPolicy); Paths = (New-ModulePaths)
    }
    Assert-True $snapshot.complete 'Required Qt modules were not proved.'
    Assert-Equal $snapshot.modules.Count 10 'Module paths were lost.'
    Assert-Equal $snapshot.external_crt.Count 3 'External CRT was not reported.'
    foreach ($crt in $snapshot.external_crt) { Assert-Equal $crt.origin 'system32' 'Wrong external CRT origin.' }
}

foreach ($missing in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll', 'qoffscreen.dll')) {
    Test-Case "missing $missing cannot prove a complete module capture" {
        $paths = @(New-ModulePaths | Where-Object { [IO.Path]::GetFileName($_) -ine $missing })
        $snapshot = Invoke-SmokeInternal 'Read-QtShellSmokeModuleSnapshot' @{ Policy = (New-TestPolicy); Paths = $paths }
        Assert-True (-not $snapshot.complete) "Missing $missing was accepted."
    }
}

foreach ($external in @('C:\sdk\bin\Qt6Qml.dll', 'C:\sdk\qml\QtQuick\qtquick2plugin.dll',
                        'C:\package stage-evil\platforms\qoffscreen.dll', 'C:\Windows\System32\qoffscreen.dll',
                        'C:\other\injected.dll', 'C:\package stage\not-in-manifest.dll', 'Qt6Core.dll')) {
    Test-Case "reject invalid module location $external" {
        Assert-Throws {
            Invoke-SmokeInternal 'Read-QtShellSmokeModuleSnapshot' @{
                Policy = (New-TestPolicy); Paths = @((New-ModulePaths) + $external)
            }
        } 'module_'
    }
}

Test-Case 'success records an early live capture and never terminates the retained process' {
    $fake = New-FakeProcessOperations
    $result = Invoke-FakeJourney $fake
    Assert-Equal $result.exit_code 0 'Wrong exit code.'
    Assert-Equal $result.stdout '' 'Unexpected stdout.'
    Assert-Equal $result.stderr '' 'Unexpected stderr.'
    Assert-Equal $result.module_capture.first_complete_elapsed_ms 40.0 'No early live module evidence.'
    Assert-Equal $fake.State.kills 0 'A successful process was killed.'
    Assert-Equal $fake.State.disposed 1 'Retained process was not disposed.'
    Assert-True (-not $result.terminated_by_runner) 'Success was attributed to forced termination.'
}

foreach ($failure in @(
    @{ name = 'nonzero exit'; exit = 7; stdout = ''; stderr = ''; pattern = 'exit_code' },
    @{ name = 'stdout text'; exit = 0; stdout = 'warning'; stderr = ''; pattern = 'output_not_empty' },
    @{ name = 'stderr whitespace'; exit = 0; stdout = ''; stderr = "`r`n"; pattern = 'output_not_empty' }
)) {
    Test-Case "reject $($failure.name) without killing an already exited process" {
        $fake = New-FakeProcessOperations -ExitCode $failure.exit -StandardOutput $failure.stdout -StandardError $failure.stderr
        Assert-Throws { Invoke-FakeJourney $fake } $failure.pattern
        Assert-Equal $fake.State.kills 0 'An exited process was killed.'
        Assert-Equal $fake.State.disposed 1 'Failure leaked the process object.'
    }
}

Test-Case 'timeout stops only the retained live process' {
    $fake = New-FakeProcessOperations -Observations @(
        (New-Observation 10 $false (New-ModulePaths)), (New-Observation 1000 $false (New-ModulePaths))
    )
    Assert-Throws { Invoke-FakeJourney $fake } 'process_timeout'
    Assert-Equal $fake.State.kills 1 'Timeout did not terminate the owned live process once.'
    Assert-Equal $fake.State.disposed 1 'Timeout leaked the retained object.'
}

Test-Case 'exit zero without a live module snapshot is not success' {
    $fake = New-FakeProcessOperations -Observations @((New-Observation 10 $false), (New-Observation 100 $true))
    Assert-Throws { Invoke-FakeJourney $fake } 'module_capture_missing'
    Assert-Equal $fake.State.kills 0 'A completed process was killed after missing the capture.'
}

Test-Case 'capture at the 500ms boundary is rejected and cleans up' {
    $fake = New-FakeProcessOperations -Observations @((New-Observation 500 $false (New-ModulePaths)))
    Assert-Throws { Invoke-FakeJourney $fake } 'module_capture_timeout'
    Assert-Equal $fake.State.kills 1 'Late capture did not terminate the retained process.'
}

Test-Case 'transient startup capture errors can recover before the deadline' {
    $fake = New-FakeProcessOperations -Observations @(
        (New-Observation 0 $false @() 'not-ready'), (New-Observation 25 $false (New-ModulePaths)),
        (New-Observation 100 $true)
    )
    $result = Invoke-FakeJourney $fake
    Assert-Equal $result.module_capture.transient_error_count 1 'Lost startup capture diagnostic.'
    Assert-Equal $result.module_capture.first_complete_elapsed_ms 25.0 'Capture recovery was not observed.'
    Assert-Equal $fake.State.kills 0 'Recovered capture was killed.'
}

Test-Case 'late external Qt plugin is rejected after the required capture' {
    $fake = New-FakeProcessOperations -Observations @(
        (New-Observation 10 $false (New-ModulePaths)),
        (New-Observation 20 $false @((New-ModulePaths) + 'C:\sdk\imageformats\qsvg.dll'))
    )
    Assert-Throws { Invoke-FakeJourney $fake } 'module_'
    Assert-Equal $fake.State.kills 1 'The late invalid dependency did not trigger cleanup.'
}

Test-Case 'nonmonotonic observation time is rejected' {
    $fake = New-FakeProcessOperations -Observations @(
        (New-Observation 30 $false (New-ModulePaths)), (New-Observation 20 $true)
    )
    Assert-Throws { Invoke-FakeJourney $fake } 'observation_invalid'
    Assert-Equal $fake.State.disposed 1 'Invalid observation leaked the process object.'
}

Test-Case 'manifest rejection precedes workspace creation and every launch' {
    $fake = New-FakeProcessOperations
    $state = [pscustomobject]@{ workspaces = 0; validates = 0 }
    $workspace = { $state.workspaces++; throw 'workspace should not be reached' }.GetNewClosure()
    $validator = { $state.validates++; throw 'manifest-rejected' }.GetNewClosure()
    Assert-Throws {
        Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
            PackageRoot = 'C:\package stage'; ManifestPath = 'C:\manifest.json'; ExpectedManifestSha256 = ('a' * 64)
            WindowsRoot = 'C:\Windows'; ManifestValidator = $validator
            WorkspaceFactory = $workspace; ProcessOperations = $fake.Operations
        }
    } 'manifest-rejected'
    Assert-Equal $state.validates 1 'An initial manifest failure was validated again.'
    Assert-Equal $state.workspaces 0 'A failed manifest created a workspace.'
    Assert-Equal $fake.State.starts.Count 0 'A failed manifest was executed.'
}

Test-Case 'authenticated core executes exactly the two fixed journeys from distinct neutral directories' {
    $fake = New-FakeProcessOperations
    $manifest = New-ManifestFixture
    $state = [pscustomobject]@{ validates = 0; workspaces = 0 }
    $validator = {
        param($PackageRoot, $ManifestPath, $ExpectedManifestSha256)
        Assert-Equal $PackageRoot 'C:\package stage' 'Different package was validated.'
        Assert-Equal $ManifestPath 'C:\manifest.json' 'Wrong manifest was validated.'
        Assert-Equal $ExpectedManifestSha256 ('a' * 64) 'External trust anchor was lost.'
        $state.validates++
        return $manifest
    }.GetNewClosure()
    $workspace = {
        if ($state.validates -ne 1) { throw 'Workspace preceded authentication.' }
        $state.workspaces++
        return 'C:\neutral smoke'
    }.GetNewClosure()
    $report = Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
        PackageRoot = 'C:\package stage'; ManifestPath = 'C:\manifest.json'; ExpectedManifestSha256 = ('a' * 64)
        WindowsRoot = 'C:\Windows'; ManifestValidator = $validator; WorkspaceFactory = $workspace
        ProcessOperations = $fake.Operations
    }
    Assert-Equal $report.status 'passed' 'Smoke report did not succeed.'
    Assert-Equal $state.validates 2 'The manifest was not checked before and after execution.'
    Assert-Equal $fake.State.starts.Count 2 'Unexpected subprocess count.'
    Assert-Equal $fake.State.starts[0].FileName 'C:\package stage\m0_qt_shell.exe' 'Wrong first executable.'
    Assert-Equal $fake.State.starts[1].FileName 'C:\package stage\m0_qt_shell.exe' 'Wrong second executable.'
    Assert-Equal ($fake.State.starts[0].ArgumentList -join '|') '--m0-smoke=tray-lifecycle' 'Tray journey changed.'
    Assert-Equal ($fake.State.starts[1].ArgumentList -join '|') '--m0-scenario=unavailable|--m0-smoke=error-recovery' 'Recovery journey changed.'
    Assert-Equal $fake.State.starts[0].WorkingDirectory 'C:\neutral smoke\tray-lifecycle' 'First cwd is not neutral.'
    Assert-Equal $fake.State.starts[1].WorkingDirectory 'C:\neutral smoke\error-recovery' 'Second cwd is not distinct.'
    Assert-Equal $fake.State.kills 0 'The successful journeys were killed.'
    Assert-Equal $fake.State.disposed 2 'One retained process was not disposed.'
    Assert-Equal @($report.journeys).Count 2 'Report lost a journey.'
    Assert-True (@($report.limitations).Count -ge 2) 'The report omits snapshot or quiescent-tree limitations.'
}

Test-Case 'post-execution manifest failure rejects success after both processes have exited' {
    $fake = New-FakeProcessOperations
    $manifest = New-ManifestFixture
    $state = [pscustomobject]@{ validates = 0; workspaces = 0 }
    $validator = {
        param($PackageRoot, $ManifestPath, $ExpectedManifestSha256)
        Assert-Equal $PackageRoot 'C:/package stage' 'Validation did not receive the original package path.'
        Assert-Equal $ManifestPath 'C:/manifest.json' 'Validation did not receive the original manifest path.'
        Assert-Equal $ExpectedManifestSha256 ('A' * 64) 'Validation changed the required hash.'
        $state.validates++
        if ($state.validates -eq 2) {
            Assert-Equal $fake.State.starts.Count 2 'The final manifest check preceded a journey.'
            Assert-Equal $fake.State.finish_calls 2 'A process was not finished before the final check.'
            Assert-Equal $fake.State.disposed 2 'A process object was retained at the final check.'
            Assert-True (-not $fake.State.alive) 'A subprocess was still alive at the final check.'
            throw 'post-execution-manifest-changed'
        }
        return $manifest
    }.GetNewClosure()
    $workspace = {
        Assert-Equal $state.validates 1 'Workspace creation did not follow the initial check.'
        $state.workspaces++
        return 'C:\neutral smoke'
    }.GetNewClosure()
    $emitted = [Collections.Generic.List[object]]::new()
    Assert-Throws {
        Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
            PackageRoot = 'C:/package stage'; ManifestPath = 'C:/manifest.json'; ExpectedManifestSha256 = ('A' * 64)
            WindowsRoot = 'C:\Windows'; ManifestValidator = $validator; WorkspaceFactory = $workspace
            ProcessOperations = $fake.Operations
        } | ForEach-Object { $emitted.Add($_) }
    } 'post-execution-manifest-changed'
    Assert-Equal $state.validates 2 'The post-execution failure was skipped or retried.'
    Assert-Equal $state.workspaces 1 'A post-execution failure created a new workspace.'
    Assert-Equal $fake.State.starts.Count 2 'A post-execution failure started another subprocess.'
    Assert-Equal $fake.State.kills 0 'An already finished process was terminated.'
    Assert-Equal $fake.State.disposed 2 'Post-execution failure leaked a process object.'
    Assert-True (-not $fake.State.alive) 'Post-execution failure left a subprocess alive.'
    Assert-Equal $emitted.Count 0 'Success output escaped before the final manifest validation.'
}

Test-Case 'partial capture accompanied by an enumeration error is never accepted as evidence' {
    $fake = New-FakeProcessOperations -Observations @(
        (New-Observation 20 $false (New-ModulePaths) 'enumeration-interrupted'),
        (New-Observation 100 $true)
    )
    Assert-Throws { Invoke-FakeJourney $fake } 'module_capture_ambiguous'
    Assert-Equal $fake.State.kills 1 'Ambiguous live capture did not trigger cleanup.'
    Assert-Equal $fake.State.disposed 1 'Ambiguous capture leaked its retained object.'
}

foreach ($required in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll', 'qoffscreen.dll')) {
    Test-Case "manifest missing $required fails before workspace creation or launch" {
        $manifest = New-ManifestFixture
        $manifest.files = @($manifest.files | Where-Object { [IO.Path]::GetFileName($_.relative_path) -ine $required })
        $fake = New-FakeProcessOperations
        $state = [pscustomobject]@{ workspaces = 0; validates = 0 }
        $workspace = { $state.workspaces++; throw 'workspace should not be reached' }.GetNewClosure()
        $validator = { param($Root, $Path, $Hash) $state.validates++; return $manifest }.GetNewClosure()
        Assert-Throws {
            Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
                PackageRoot = 'C:\package stage'; ManifestPath = 'C:\manifest.json'; ExpectedManifestSha256 = ('a' * 64)
                WindowsRoot = 'C:\Windows'; ManifestValidator = $validator
                WorkspaceFactory = $workspace; ProcessOperations = $fake.Operations
            }
        } 'module_manifest_missing_required'
        Assert-Equal $state.validates 1 'A pre-staging failure repeated the manifest validation.'
        Assert-Equal $state.workspaces 0 'Incomplete required runtime created a workspace.'
        Assert-Equal $fake.State.starts.Count 0 'Incomplete required runtime was executed.'
    }
}

Test-Case 'manifest validator sees original paths before any normalization' {
    $state = [pscustomobject]@{ validates = 0 }
    $validator = {
        param($Root, $Path, $Hash)
        Assert-Equal $Root 'C:\package stage\..\other' 'Canonicalization bypassed manifest lexical validation.'
        Assert-Equal $Path 'C:\evidence\..\manifest.json' 'Manifest input was normalized before validation.'
        $state.validates++
        throw 'manifest-rejected-original-path'
    }.GetNewClosure()
    Assert-Throws {
        Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
            PackageRoot = 'C:\package stage\..\other'; ManifestPath = 'C:\evidence\..\manifest.json'
            ExpectedManifestSha256 = ('a' * 64); WindowsRoot = 'C:\Windows'; ManifestValidator = $validator
            WorkspaceFactory = { throw 'unexpected workspace' }; ProcessOperations = (New-FakeProcessOperations).Operations
        }
    } 'manifest-rejected-original-path'
    Assert-Equal $state.validates 1 'Original paths were not checked.'
}

Test-Case 'relative paths use the PowerShell filesystem location validated by the manifest' {
    Push-Location -LiteralPath $PSScriptRoot
    try {
        $expectedRoot = Join-Path $PSScriptRoot 'package stage'
        $expectedManifest = Join-Path $PSScriptRoot 'manifest.json'
        $paths = @(New-ModulePaths | ForEach-Object { $_.Replace('C:\package stage', $expectedRoot) })
        $fake = New-FakeProcessOperations -Observations @(
            (New-Observation 10 $false $paths), (New-Observation 100 $true)
        )
        $manifest = New-ManifestFixture
        $validator = {
            param($Root, $Path, $Hash)
            Assert-Equal $Root 'package stage' 'Relative root input was changed before validation.'
            Assert-Equal $Path 'manifest.json' 'Relative manifest input was changed before validation.'
            return $manifest
        }.GetNewClosure()
        $report = Invoke-SmokeInternal 'Invoke-QtShellPackageSmokeCore' @{
            PackageRoot = 'package stage'; ManifestPath = 'manifest.json'; ExpectedManifestSha256 = ('a' * 64)
            WindowsRoot = 'C:\Windows'; ManifestValidator = $validator; WorkspaceFactory = { 'C:\neutral smoke' }
            ProcessOperations = $fake.Operations
        }
        Assert-Equal $report.package_root $expectedRoot 'Execution used a different directory from PowerShell.'
        Assert-Equal $report.manifest_path $expectedManifest 'Evidence path used the .NET process cwd.'
        Assert-Equal $fake.State.kills 0 'Successful relative-path journeys were killed.'
    }
    finally { Pop-Location }
}

Test-Case 'Windows loader dialog suppression preserves existing flags and restores them after success or failure' {
    Invoke-SmokeInternal 'Initialize-QtShellSmokeNativeStart' @{}
    $before = [MusicPlayer.M0.PackageSmoke.NativeStart]::GetErrorMode()
    $expected = $before -bor 0x8003
    $success = [Func[bool]] {
        Assert-Equal ([MusicPlayer.M0.PackageSmoke.NativeStart]::GetErrorMode()) $expected 'Dialog suppression is not active.'
        return $true
    }.GetNewClosure()
    Assert-True ([MusicPlayer.M0.PackageSmoke.NativeStart]::Run($success)) 'The guarded start result was lost.'
    Assert-Equal ([MusicPlayer.M0.PackageSmoke.NativeStart]::GetErrorMode()) $before 'Successful start changed parent error mode.'
    $failure = [Func[bool]] {
        Assert-Equal ([MusicPlayer.M0.PackageSmoke.NativeStart]::GetErrorMode()) $expected 'Failure callback was not protected.'
        throw 'synthetic-start-failed'
    }.GetNewClosure()
    Assert-Throws { [MusicPlayer.M0.PackageSmoke.NativeStart]::Run($failure) } 'synthetic-start-failed'
    Assert-Equal ([MusicPlayer.M0.PackageSmoke.NativeStart]::GetErrorMode()) $before 'Failed start changed parent error mode.'
}

Write-Output "Tests: $script:passed passed, $script:failed failed."
if ($script:failed -ne 0) { exit 1 }
