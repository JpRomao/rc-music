#requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-QtShellSmokeEnvironment {
    param([string] $WindowsRoot, [string] $WorkingDirectory)
    # This is a new child-only allowlist, not a mutation or copy of the caller's environment.
    $environment = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    $environment['SystemRoot'] = $WindowsRoot
    $environment['WINDIR'] = $WindowsRoot
    $environment['SystemDrive'] = [IO.Path]::GetPathRoot($WindowsRoot).TrimEnd('\')
    $environment['PATH'] = (Join-Path $WindowsRoot 'System32') + ';' + $WindowsRoot
    $environment['TEMP'] = Join-Path $WorkingDirectory 'temp'
    $environment['TMP'] = $environment['TEMP']
    $environment['USERPROFILE'] = Join-Path $WorkingDirectory 'profile'
    $environment['APPDATA'] = Join-Path $WorkingDirectory 'profile\AppData\Roaming'
    $environment['LOCALAPPDATA'] = Join-Path $WorkingDirectory 'profile\AppData\Local'
    $environment['QT_QPA_PLATFORM'] = 'offscreen'
    # QT_QUICK_BACKEND selects the software adaptation; QSG_RHI_BACKEND selects a hardware RHI API.
    $environment['QT_QUICK_BACKEND'] = 'software'
    $environment['QT_QUICK_CONTROLS_STYLE'] = 'Basic'
    $environment['QT_QPA_FONTDIR'] = Join-Path $WindowsRoot 'Fonts'
    $environment['QT_LOGGING_TO_CONSOLE'] = '1'
    $environment['QT_FORCE_STDERR_LOGGING'] = '1'
    $environment['QT_FATAL_WARNINGS'] = '1'
    $environment['QML_DISABLE_DISK_CACHE'] = '1'
    return $environment
}

function New-QtShellSmokeStartInfo {
    param([string] $ExecutablePath, [string[]] $Arguments, [string] $WorkingDirectory,
          [Collections.IDictionary] $Environment)
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $ExecutablePath
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.Environment.Clear()
    foreach ($name in $Environment.Keys) { $info.Environment[$name] = $Environment[$name] }
    return $info
}

function Test-QtShellSmokePathWithin([string] $Path, [string] $Root) {
    return $Path.StartsWith($Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
                            [StringComparison]::OrdinalIgnoreCase)
}

function Test-QtShellSmokeCrtName([string] $Name) {
    return $Name -match '^(?:(?:vcruntime|msvcp|msvcr|concrt)\d+[^\\/]*|msvcrt|ucrtbase|api-ms-win-crt-[^\\/]+)\.dll$'
}

function New-QtShellSmokeModulePolicy {
    param([string] $PackageRoot, [string] $WindowsRoot, $Manifest)
    $files = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $dllNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $Manifest.files) {
        $null = $files.Add($file.relative_path.Replace('\', '/'))
        $name = [IO.Path]::GetFileName($file.relative_path)
        if ([IO.Path]::GetExtension($name) -ieq '.dll' -and -not (Test-QtShellSmokeCrtName $name)) {
            $null = $dllNames.Add($name)
        }
    }
    if (-not $files.Contains('m0_qt_shell.exe')) { throw 'module_manifest_missing_executable' }
    foreach ($name in @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll')) {
        if (-not $files.Contains($name)) { throw "module_manifest_missing_required: $name" }
    }
    if (-not $dllNames.Contains('qoffscreen.dll')) { throw 'module_manifest_missing_required: qoffscreen.dll' }
    [pscustomobject]@{
        package_root = [IO.Path]::GetFullPath($PackageRoot).TrimEnd('\', '/')
        windows_root = [IO.Path]::GetFullPath($WindowsRoot).TrimEnd('\', '/')
        system32_root = [IO.Path]::GetFullPath((Join-Path $WindowsRoot 'System32'))
        files = $files
        packaged_dll_names = $dllNames
        required_names = @('m0_qt_shell.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Quick.dll', 'qoffscreen.dll')
    }
}

function Read-QtShellSmokeModuleSnapshot {
    param($Policy, [AllowEmptyCollection()] [string[]] $Paths)
    $modules = [Collections.Generic.List[object]]::new()
    $crt = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $stagedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $Paths) {
        if ([string]::IsNullOrWhiteSpace($path) -or -not [IO.Path]::IsPathFullyQualified($path)) {
            throw 'module_path_not_absolute'
        }
        $absolute = [IO.Path]::GetFullPath($path)
        if (-not $seen.Add($absolute)) { throw "module_duplicate_path: $absolute" }
        $name = [IO.Path]::GetFileName($absolute)
        $origin = 'windows'
        if (Test-QtShellSmokePathWithin $absolute $Policy.package_root) {
            $relative = [IO.Path]::GetRelativePath($Policy.package_root, $absolute).Replace('\', '/')
            if (-not $Policy.files.Contains($relative)) { throw "module_not_in_manifest: $relative" }
            $origin = 'package'
            $null = $stagedNames.Add($name)
        }
        else {
            if ($name -match '^Qt\d.*\.dll$' -or $Policy.packaged_dll_names.Contains($name) -or
                $absolute -match '[\\/](qml|plugins)[\\/]') {
                throw "module_qt_outside_package: $absolute"
            }
            if (-not (Test-QtShellSmokePathWithin $absolute $Policy.windows_root)) {
                throw "module_outside_package_or_windows: $absolute"
            }
            if (Test-QtShellSmokePathWithin $absolute $Policy.system32_root) { $origin = 'system32' }
            if (Test-QtShellSmokeCrtName $name) {
                if ($origin -ne 'system32') { throw "module_crt_outside_system32: $absolute" }
                $crt.Add([pscustomobject]@{ name = $name; path = $absolute; origin = $origin })
            }
        }
        $modules.Add([pscustomobject]@{ name = $name; path = $absolute; origin = $origin })
    }
    $complete = $true
    foreach ($name in $Policy.required_names) {
        if (-not $stagedNames.Contains($name)) { $complete = $false }
    }
    [pscustomobject]@{ complete = $complete; modules = $modules.ToArray(); external_crt = $crt.ToArray() }
}

function Initialize-QtShellSmokeNativeStart {
    if ('MusicPlayer.M0.PackageSmoke.NativeStart' -as [type]) { return }
    # A child inherits SetErrorMode, unlike a thread-specific error mode. This process-local
    # guard restores the caller's flags immediately after creation; no registry/OS setting is changed.
    # https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace MusicPlayer.M0.PackageSmoke
{
    public static class NativeStart
    {
        private static readonly object Gate = new object();
        [DllImport("kernel32.dll", ExactSpelling = true)]
        public static extern uint GetErrorMode();
        [DllImport("kernel32.dll", ExactSpelling = true)]
        private static extern uint SetErrorMode(uint mode);
        public static bool Run(Func<bool> start)
        {
            lock (Gate)
            {
                uint previous = GetErrorMode();
                try
                {
                    // SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX.
                    SetErrorMode(previous | 0x8003u);
                    return start();
                }
                finally { SetErrorMode(previous); }
            }
        }
    }
}
'@
}

function Get-QtShellSmokeProcessOperations {
    @{
        Start = {
            param($StartInfo)
            Initialize-QtShellSmokeNativeStart
            $clock = [Diagnostics.Stopwatch]::StartNew()
            $process = [Diagnostics.Process]::new()
            $process.StartInfo = $StartInfo
            $started = $false
            try {
                $started = [MusicPlayer.M0.PackageSmoke.NativeStart]::Run([Func[bool]] { return $process.Start() })
                if (-not $started) { throw 'process_start_failed' }
                # Drain both pipes asynchronously before polling, so output cannot deadlock the child.
                $stdout = $process.StandardOutput.ReadToEndAsync()
                $stderr = $process.StandardError.ReadToEndAsync()
                return [pscustomobject]@{
                    process_id = $process.Id; process = $process; clock = $clock
                    stdout_task = $stdout; stderr_task = $stderr
                }
            }
            catch {
                try {
                    if ($started -and -not $process.HasExited) {
                        $process.Kill()
                        if (-not $process.WaitForExit(1000)) { throw 'process_start_cleanup_timeout' }
                    }
                }
                finally { $process.Dispose() }
                throw
            }
        }
        Observe = {
            param($Session)
            $process = $Session.process
            $process.Refresh()
            $exited = $process.HasExited
            $paths = @()
            $captureError = $null
            if (-not $exited) {
                try {
                    $paths = @($process.Modules | ForEach-Object { $_.FileName })
                    $exited = $process.HasExited
                    # Only keep samples with a live process on both sides of the enumeration.
                    if ($exited) { $paths = @() }
                }
                catch {
                    $paths = @()
                    $process.Refresh()
                    $exited = $process.HasExited
                    if (-not $exited) { $captureError = $_.Exception.GetType().FullName }
                }
            }
            [pscustomobject]@{
                has_exited = $exited; elapsed_ms = $Session.clock.Elapsed.TotalMilliseconds
                module_paths = @($paths); capture_error = $captureError
            }
        }
        Wait = {
            param($Session, $Milliseconds)
            $null = $Session.process.WaitForExit($Milliseconds)
        }
        Finish = {
            param($Session, $RemainingMilliseconds)
            $tasks = [Threading.Tasks.Task[]]@($Session.stdout_task, $Session.stderr_task)
            if (-not [Threading.Tasks.Task]::WaitAll($tasks, $RemainingMilliseconds)) {
                throw 'output_capture_timeout'
            }
            [pscustomobject]@{
                exit_code = $Session.process.ExitCode
                stdout = $Session.stdout_task.GetAwaiter().GetResult()
                stderr = $Session.stderr_task.GetAwaiter().GetResult()
            }
        }
        IsAlive = { param($Session) return -not $Session.process.HasExited }
        Terminate = {
            param($Session)
            # Kill the retained object only; never look up a name or recycle a numeric PID.
            $Session.process.Kill()
            if (-not $Session.process.WaitForExit(1000)) { throw 'process_cleanup_timeout' }
        }
        Dispose = { param($Session) $Session.process.Dispose() }
    }
}

function Invoke-QtShellSmokeJourney {
    param($Journey, [string] $PackageRoot, [string] $WorkingDirectory,
          [Collections.IDictionary] $Environment, $ModulePolicy, [hashtable] $Operations,
          [ValidateRange(1, 30000)] [int] $TimeoutMs = 5000,
          [ValidateRange(1, 500)] [int] $CaptureTimeoutMs = 500)
    $info = New-QtShellSmokeStartInfo -ExecutablePath (Join-Path $PackageRoot 'm0_qt_shell.exe') `
        -Arguments $Journey.arguments -WorkingDirectory $WorkingDirectory -Environment $Environment
    $session = $null
    $passed = $false
    $lastElapsed = -1.0
    $firstComplete = $null
    $samples = 0
    $captureErrors = 0
    $observed = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $crt = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    try {
        $session = & $Operations.Start $info
        while ($true) {
            $observation = & $Operations.Observe $session
            $elapsed = [double] $observation.elapsed_ms
            if ([double]::IsNaN($elapsed) -or [double]::IsInfinity($elapsed) -or $elapsed -lt 0 -or
                $elapsed -lt $lastElapsed -or $observation.has_exited -isnot [bool]) {
                throw 'observation_invalid'
            }
            $lastElapsed = $elapsed
            if (-not [string]::IsNullOrEmpty($observation.capture_error)) {
                $captureErrors++
                if (@($observation.module_paths).Count -gt 0) { throw 'module_capture_ambiguous' }
            }
            if (@($observation.module_paths).Count -gt 0) {
                if ($observation.has_exited) { throw 'module_capture_not_live' }
                $snapshot = Read-QtShellSmokeModuleSnapshot -Policy $ModulePolicy -Paths $observation.module_paths
                $samples++
                foreach ($module in $snapshot.modules) { $observed[$module.path] = $module }
                foreach ($entry in $snapshot.external_crt) { $crt[$entry.path] = $entry }
                if ($null -eq $firstComplete -and $snapshot.complete -and $elapsed -lt $CaptureTimeoutMs) {
                    $firstComplete = $elapsed
                }
            }
            if ($elapsed -ge $TimeoutMs) { throw 'process_timeout' }
            if ($observation.has_exited) {
                $result = & $Operations.Finish $session ([Math]::Max(1, [int] ($TimeoutMs - $elapsed)))
                if ($result.exit_code -ne 0) { throw "process_exit_code: $($result.exit_code)" }
                if ($result.stdout.Length -ne 0 -or $result.stderr.Length -ne 0) { throw 'process_output_not_empty' }
                if ($null -eq $firstComplete) { throw 'module_capture_missing' }
                $passed = $true
                return [pscustomobject][ordered]@{
                    id = $Journey.id; arguments = @($Journey.arguments); process_id = $session.process_id
                    working_directory = $WorkingDirectory; timeout_ms = $TimeoutMs; elapsed_ms = $elapsed
                    exit_code = $result.exit_code; stdout = $result.stdout; stderr = $result.stderr
                    terminated_by_runner = $false
                    module_capture = [pscustomobject][ordered]@{
                        deadline_ms = $CaptureTimeoutMs; first_complete_elapsed_ms = $firstComplete
                        live_sample_count = $samples; transient_error_count = $captureErrors
                        required_names = @($ModulePolicy.required_names)
                        observed_modules = @($observed.Values | Sort-Object path)
                        external_crt = @($crt.Values | Sort-Object path)
                    }
                }
            }
            if ($null -eq $firstComplete -and $elapsed -ge $CaptureTimeoutMs) { throw 'module_capture_timeout' }
            $null = & $Operations.Wait $session ([Math]::Min(5, [Math]::Max(1, [int] ($TimeoutMs - $elapsed))))
        }
    }
    finally {
        if ($null -ne $session) {
            try {
                if (-not $passed -and (& $Operations.IsAlive $session)) { $null = & $Operations.Terminate $session }
            }
            finally { $null = & $Operations.Dispose $session }
        }
    }
}

function New-QtShellSmokeWorkspace {
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\..'))
    $outRoot = Join-Path $repositoryRoot 'out'
    $m0Root = Join-Path $outRoot 'm0'
    foreach ($parentPath in @($outRoot, $m0Root)) {
        if (Test-Path -LiteralPath $parentPath) {
            $item = Get-Item -LiteralPath $parentPath -Force
            if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw 'workspace_parent_not_regular_directory'
            }
        }
        else { $null = New-Item -ItemType Directory -Path $parentPath -ErrorAction Stop }
    }
    $workspace = Join-Path $m0Root ('qt-package-smoke-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $workspace -ErrorAction Stop
    foreach ($journey in @('tray-lifecycle', 'error-recovery')) {
        $cwd = Join-Path $workspace $journey
        foreach ($relative in @('', 'temp', 'profile', 'profile\AppData', 'profile\AppData\Local', 'profile\AppData\Roaming')) {
            $directory = if ($relative -eq '') { $cwd } else { Join-Path $cwd $relative }
            $null = New-Item -ItemType Directory -Path $directory -ErrorAction Stop
        }
    }
    # Keep the neutral directories as evidence. The runner never removes user or package data.
    return $workspace
}

function Invoke-QtShellSmokeManifestValidation {
    param([string] $PackageRoot, [string] $ManifestPath, [string] $ExpectedManifestSha256)
    $module = Import-Module -Name (Join-Path $PSScriptRoot 'QtShellPackageManifest.psm1') -Force -PassThru
    & $module.ExportedCommands['Test-QtShellPackageManifest'] -PackageRoot $PackageRoot `
        -ManifestPath $ManifestPath -ExpectedManifestSha256 $ExpectedManifestSha256
}

function Invoke-QtShellPackageSmokeCore {
    param([string] $PackageRoot, [string] $ManifestPath, [string] $ExpectedManifestSha256,
          [string] $WindowsRoot, [scriptblock] $ManifestValidator, [scriptblock] $WorkspaceFactory,
          [hashtable] $ProcessOperations)
    # Preserve lexical input checks (including '..') and relative-path semantics owned by the manifest.
    $manifest = & $ManifestValidator $PackageRoot $ManifestPath $ExpectedManifestSha256
    $root = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($PackageRoot).TrimEnd('\', '/')
    $manifestFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ManifestPath)
    $policy = New-QtShellSmokeModulePolicy -PackageRoot $root -WindowsRoot $WindowsRoot -Manifest $manifest
    $workspace = & $WorkspaceFactory
    $journeys = @(
        [pscustomobject]@{ id = 'tray-lifecycle'; arguments = @('--m0-smoke=tray-lifecycle') },
        [pscustomobject]@{ id = 'error-recovery'; arguments = @('--m0-scenario=unavailable', '--m0-smoke=error-recovery') }
    )
    $results = foreach ($journey in $journeys) {
        $cwd = Join-Path $workspace $journey.id
        $environment = New-QtShellSmokeEnvironment -WindowsRoot $WindowsRoot -WorkingDirectory $cwd
        Invoke-QtShellSmokeJourney -Journey $journey -PackageRoot $root -WorkingDirectory $cwd `
            -Environment $environment -ModulePolicy $policy -Operations $ProcessOperations
    }
    # A clean exit is not success if either journey changed the authenticated payload.
    $null = & $ManifestValidator $PackageRoot $ManifestPath $ExpectedManifestSha256
    [pscustomobject][ordered]@{
        schema_version = 1; status = 'passed'; scope = 'local-release-package-offscreen-only'
        package_root = $root; manifest_path = $manifestFile; manifest_sha256 = $ExpectedManifestSha256.ToLowerInvariant()
        working_root = $workspace; environment_policy = 'child-only-windows-allowlist-offscreen-software-basic'
        journeys = @($results)
        limitations = @(
            'Manifest validation assumes a quiescent tree; files are not locked against concurrent local replacement.',
            'Live module snapshots are sampled, not a complete loader trace; transient loads between samples are not proved.',
            'System32 CRT remains an external host prerequisite; this is not clean-machine or redistributable-runtime proof.',
            'No native desktop, tray, audio, network isolation, Linux, installer or public-distribution validation.'
        )
    }
}

function Test-QtShellPackageSmoke {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $PackageRoot,
        [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $ManifestPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-fA-F]{64}$')] [string] $ExpectedManifestSha256
    )
    if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required.' }
    $windowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    if ([string]::IsNullOrEmpty($windowsRoot) -or -not (Test-Path -LiteralPath (Join-Path $windowsRoot 'Fonts') -PathType Container)) {
        throw 'Windows system root or fonts are unavailable.'
    }
    Invoke-QtShellPackageSmokeCore -PackageRoot $PackageRoot -ManifestPath $ManifestPath `
        -ExpectedManifestSha256 $ExpectedManifestSha256 -WindowsRoot $windowsRoot `
        -ManifestValidator ${function:Invoke-QtShellSmokeManifestValidation} `
        -WorkspaceFactory ${function:New-QtShellSmokeWorkspace} -ProcessOperations (Get-QtShellSmokeProcessOperations)
}

Export-ModuleMember -Function Test-QtShellPackageSmoke
