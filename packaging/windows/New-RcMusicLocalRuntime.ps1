#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $OutputDirectory,
    [Parameter(Mandatory)] [ValidateNotNullOrEmpty()] [string] $BuildDirectory,
    [ValidateSet('Lite', 'CompleteExperimental')] [string] $Edition = 'Lite',
    [AllowEmptyString()] [string] $DownloadPackageReceipt,
    [AllowEmptyString()] [string] $DownloadPackageReceiptSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$BuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory, $repository)
if (-not $OutputDirectory.StartsWith((Join-Path $repository 'out/product/'), [StringComparison]::OrdinalIgnoreCase) -and
    -not $OutputDirectory.StartsWith((Join-Path $repository 'out/m0/'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Local runtime output must be a fresh directory below repository out/product (or historical out/m0)'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Output directory already exists; choose a new name' }
$componentEnabled = $PSBoundParameters.ContainsKey('DownloadPackageReceipt') -or
    $PSBoundParameters.ContainsKey('DownloadPackageReceiptSha256')
if ($componentEnabled -and (-not $PSBoundParameters.ContainsKey('DownloadPackageReceipt') -or
        -not $PSBoundParameters.ContainsKey('DownloadPackageReceiptSha256') -or
        [string]::IsNullOrWhiteSpace($DownloadPackageReceipt) -or
        $DownloadPackageReceiptSha256 -cnotmatch '\A[0-9a-f]{64}\z')) {
    throw 'Download package requires an explicit receipt path and its lowercase SHA256 pin'
}
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'PeDependencyReport.psm1') -Force
$downloadComponent = $null
if ($componentEnabled) {
    Import-Module (Join-Path $PSScriptRoot 'RcMusicDownloadComponent.psm1') -Force
    Import-Module (Join-Path $PSScriptRoot 'RcMusicDownloadComponentPe.psm1') -Force
    $downloadComponent = Get-RcMusicDownloadComponentPlan -ReceiptPath $DownloadPackageReceipt `
        -ReceiptSha256 $DownloadPackageReceiptSha256
    $componentSnapshot = $downloadComponent | ConvertTo-Json -Depth 15 -Compress
}

# Record the actual implementation and data files, never just a legacy wrapper.
function Get-RuntimeRecipeFiles {
    param([ValidateSet('Lite', 'CompleteExperimental')][string] $Edition = 'Lite',
        [switch] $DownloadComponent)
    $names = @('New-RcMusicLocalRuntime.ps1', 'RcMusicRuntimePlan.psm1', 'PeDependencyReport.psm1',
        'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($Edition -ieq 'CompleteExperimental') { $names += 'WEBVIEW2-DEVELOPMENT.txt' }
    if ($DownloadComponent) { $names += 'RcMusicDownloadComponent.psm1', 'RcMusicDownloadComponentPe.psm1' }
    $names | ForEach-Object {
        [pscustomobject]@{ path = ('packaging/windows/' + $_)
            sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
}
$recipeFiles = @(Get-RuntimeRecipeFiles -Edition $Edition -DownloadComponent:$componentEnabled)
$recipeBefore = ConvertTo-Json -InputObject $recipeFiles -Compress

function Invoke-RuntimeTool([string] $Program, [string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new($Program)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $repository
    $start.Environment['VSLANG'] = '1033'
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Could not start $Program" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) { $process.Kill($true); $process.WaitForExit(); throw "Tool timed out: $Program" }
        $output = $stdout.GetAwaiter().GetResult()
        $errorText = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($errorText)) {
            throw "Tool failed or reported a diagnostic: $Program`n$output`n$errorText"
        }
        return $output
    } finally { $process.Dispose() }
}

function Read-RuntimeCache {
    $result = @{}
    foreach ($line in (Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
        if ($line -match '^([^#/][^:]*):[^=]+=(.*)$') { $result[$Matches[1]] = $Matches[2] }
    }
    return $result
}
function Assert-DownloadPackageUnchanged {
    if (-not $componentEnabled) { return }
    $current = Get-RcMusicDownloadComponentPlan -ReceiptPath $DownloadPackageReceipt `
        -ReceiptSha256 $DownloadPackageReceiptSha256
    if (($current | ConvertTo-Json -Depth 15 -Compress) -cne $componentSnapshot) {
        throw 'Download package changed while collecting the runtime'
    }
}
$cache = Read-RuntimeCache
$configurationArguments = @{ RepositoryRoot = $repository; BuildTesting = 'OFF'; Edition = $Edition }
if ($componentEnabled) {
    $configurationArguments.DownloadProfileHeader = $downloadComponent.header_path
    $configurationArguments.DownloadProfileHeaderSha256 = $downloadComponent.header_sha256
}
$configuration = Get-RcMusicDesktopBuildConfiguration -Cache $cache @configurationArguments
$qt = [IO.Path]::GetFullPath((Join-Path $cache.Qt6_DIR '../../..'))
$multimedia = [IO.Path]::GetFullPath((Join-Path $cache.Qt6Multimedia_DIR '../../..'))
$taglib = [IO.Path]::GetFullPath((Join-Path $cache.TagLib_DIR '../../..'))
$dumpbin = Join-Path ([IO.Path]::GetDirectoryName($cache.CMAKE_CXX_COMPILER)) 'dumpbin.exe'
$sourceSnapshot = Get-RcMusicSourceSnapshot -RepositoryRoot $repository -EntryPoint $configuration.entrypoint
# This is an incremental application build, never a Qt SDK rebuild or benchmark.
$null = Invoke-RuntimeTool $cache.CMAKE_COMMAND @('--build', $BuildDirectory, '--target', $configuration.target)
$afterBuild = Get-RcMusicSourceSnapshot -RepositoryRoot $repository -EntryPoint $configuration.entrypoint
if ($sourceSnapshot.commit -cne $afterBuild.commit) { throw 'Source commit changed during the application build' }
if ($componentEnabled) {
    $null = Get-RcMusicDesktopBuildConfiguration -Cache (Read-RuntimeCache) @configurationArguments
    Assert-DownloadPackageUnchanged
}
$scannerText = Invoke-RuntimeTool (Join-Path $qt 'bin/qmlimportscanner.exe') @(
    '-rootPath', $configuration.qml_source, '-importPath', (Join-Path $BuildDirectory 'qml'), '-importPath', (Join-Path $qt 'qml'))
$modules = @($scannerText | ConvertFrom-Json -Depth 20)
$files = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
$peQueue = [Collections.Generic.Queue[object]]::new()
function Add-RuntimeFile([string] $SourceFile, [string] $RelativePath) {
    $SourceFile = [IO.Path]::GetFullPath($SourceFile)
    $RelativePath = $RelativePath.Replace('\', '/')
    if ($files.ContainsKey($RelativePath)) {
        if ($files[$RelativePath].source -ine $SourceFile) { throw "Conflicting runtime destination: $RelativePath" }
        return
    }
    if (-not [IO.File]::Exists($SourceFile)) { throw "Runtime input is missing: $SourceFile" }
    $entry = [pscustomobject]@{ source = $SourceFile; relative_path = $RelativePath }
    $files.Add($RelativePath, $entry)
    if ([IO.Path]::GetExtension($SourceFile) -in @('.exe', '.dll')) { $peQueue.Enqueue($entry) }
}
Add-RuntimeFile (Join-Path $BuildDirectory $configuration.executable) 'RCMusic.exe'
Add-RuntimeFile (Join-Path $PSScriptRoot 'qt.conf') 'qt.conf'
Add-RuntimeFile (Join-Path $PSScriptRoot 'LOCAL-DEVELOPMENT.txt') 'LOCAL-DEVELOPMENT.txt'
if ($configuration.edition -ceq 'CompleteExperimental') {
    Add-RuntimeFile (Join-Path $PSScriptRoot 'WEBVIEW2-DEVELOPMENT.txt') 'WEBVIEW2-DEVELOPMENT.txt'
    Add-RuntimeFile (Join-Path $configuration.browser_sdk_root 'LICENSE.txt') 'notices/WebView2-SDK-LICENSE.txt'
    Add-RuntimeFile (Join-Path $configuration.browser_sdk_root 'NOTICE.txt') 'notices/WebView2-SDK-NOTICE.txt'
}
foreach ($plugin in @('platforms/qwindows.dll', 'platforms/qoffscreen.dll', 'imageformats/qjpeg.dll',
        'imageformats/qico.dll', 'imageformats/qsvg.dll', 'iconengines/qsvgicon.dll')) {
    Add-RuntimeFile (Join-Path $qt ('plugins/' + $plugin)) ('plugins/' + $plugin)
}
Add-RuntimeFile (Join-Path $multimedia 'plugins/multimedia/ffmpegmediaplugin.dll') 'plugins/multimedia/ffmpegmediaplugin.dll'
# Direct3D shader compiler is supplied by the already provisioned Qt toolchain,
# never copied from the host Windows SDK/System32. It is loaded dynamically.
Add-RuntimeFile (Join-Path $qt 'd3dcompiler_47.dll') 'd3dcompiler_47.dll'
foreach ($module in $modules) {
    if ($module.type -cne 'module' -or -not $module.PSObject.Properties['path']) { throw 'Unresolved QML import' }
    # The application module is linked statically and already embedded in the exe.
    if ($module.name -ceq 'MusicPlayer.M0') { continue }
    $modulePath = [IO.Path]::GetFullPath($module.path)
    $qmlRoot = Join-Path $qt 'qml/'
    if (-not $modulePath.StartsWith($qmlRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected QML import source' }
    $relative = 'qml/' + [IO.Path]::GetRelativePath($qmlRoot, $modulePath).Replace('\', '/')
    Add-RuntimeFile (Join-Path $modulePath 'qmldir') ($relative + '/qmldir')
    if ($module.PSObject.Properties['plugin']) {
        Add-RuntimeFile (Join-Path $modulePath ($module.plugin + '.dll')) ($relative + '/' + $module.plugin + '.dll')
    }
    foreach ($field in @('components', 'scripts')) {
        if ($module.PSObject.Properties[$field]) {
            foreach ($component in $module.$field) {
                Add-RuntimeFile $component ('qml/' + [IO.Path]::GetRelativePath($qmlRoot, $component).Replace('\', '/'))
            }
        }
    }
}
$external = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$reports = [Collections.Generic.List[object]]::new()
$dllRoots = @((Join-Path $qt 'bin'), (Join-Path $multimedia 'bin'), (Join-Path $taglib 'bin'))
while ($peQueue.Count -gt 0) {
    $entry = $peQueue.Dequeue()
    $report = ConvertFrom-QtShellPeReport (Invoke-RuntimeTool $dumpbin @('/headers', '/dependents', $entry.source))
    $reports.Add([pscustomobject]@{ relative_path = $entry.relative_path; imports = $report.imports })
    foreach ($dependency in $report.imports) {
        if ($files.ContainsKey($dependency)) { continue }
        $candidates = @($dllRoots | ForEach-Object { Join-Path $_ $dependency } | Where-Object { [IO.File]::Exists($_) })
        if ($candidates.Count -eq 1) { Add-RuntimeFile $candidates[0] $dependency; continue }
        if ($candidates.Count -gt 1) { throw "Ambiguous DLL origin: $dependency" }
        if ($dependency -match '^(?i:api-ms-win-|ext-ms-win-|msvcp140|vcruntime140|concrt140)') {
            $null = $external.Add($dependency)
            continue
        }
        if ($dependency -match '^(?i:Qt6|tag\.|avcodec-|avformat-|avutil-|swresample-|swscale-)') {
            throw "Required application dependency is absent from its configured origin: $dependency"
        }
        $systemLibrary = Join-Path ([Environment]::SystemDirectory) $dependency
        if (-not [IO.File]::Exists($systemLibrary)) { throw "Unresolved runtime DLL: $dependency" }
        $null = $external.Add($dependency)
    }
}
$componentClosure = $null
if ($componentEnabled) {
    $componentReports = [Collections.Generic.List[object]]::new()
    foreach ($entry in $downloadComponent.files) {
        if ([IO.Path]::GetExtension($entry.relative_path) -in @('.exe', '.dll', '.pyd')) {
            $report = ConvertFrom-QtShellPeReport (Invoke-RuntimeTool $dumpbin @('/headers', '/dependents', $entry.source))
            $componentReports.Add([pscustomobject]@{
                relative_path = $entry.relative_path; machine = $report.machine; imports = $report.imports
            })
        }
    }
    $componentClosure = Get-RcMusicDownloadComponentPeClosure -Files $downloadComponent.files `
        -Reports $componentReports.ToArray()
    foreach ($entry in $downloadComponent.files) {
        if ($files.ContainsKey($entry.relative_path)) { throw 'Download component collides with the player payload' }
        # Keep the authenticated private tree out of the player's flattening
        # resolver. The separate closure includes PYDs and never discovers DLLs.
        $files.Add($entry.relative_path, $entry)
    }
    foreach ($report in $componentClosure.reports) { $reports.Add($report) }
    foreach ($import in $componentClosure.windows_external) { $null = $external.Add($import.name) }
    Assert-DownloadPackageUnchanged
}
$payload = Join-Path $OutputDirectory 'app'
$allowedRoots = @($qt, $multimedia, $taglib, $BuildDirectory, $PSScriptRoot)
if ($configuration.browser_sdk_root) { $allowedRoots += $configuration.browser_sdk_root }
if ($componentEnabled) { $allowedRoots += $downloadComponent.package_root }
$plan = New-RcMusicRuntimePlan -Files @($files.Values) -AllowedSourceRoots $allowedRoots -Destination $payload
if ($componentEnabled) {
    foreach ($expected in $downloadComponent.files) {
        $copied = @($plan.files | Where-Object relative_path -CEQ $expected.relative_path)
        if ($copied.Count -ne 1 -or $copied[0].size_bytes -cne $expected.size_bytes -or
            $copied[0].sha256 -cne $expected.sha256) { throw 'Download component mapping differs from its pinned plan' }
    }
}
$null = Copy-RcMusicRuntimePlan -Plan $plan
Assert-DownloadPackageUnchanged
$afterCopy = Get-RcMusicSourceSnapshot -RepositoryRoot $repository -EntryPoint $configuration.entrypoint
if ($sourceSnapshot.commit -cne $afterCopy.commit) { throw 'Source commit changed while collecting the runtime' }
if ((ConvertTo-Json -InputObject @(Get-RuntimeRecipeFiles -Edition $Edition -DownloadComponent:$componentEnabled) -Compress) -cne $recipeBefore) {
    throw 'Runtime recipe changed while collecting the runtime'
}
$recipeStatus = Invoke-RuntimeTool 'git' @('status', '--porcelain', '--', 'packaging/windows')
$manifestPath = Join-Path $OutputDirectory 'runtime-manifest.json'
$manifest = [ordered]@{
    schema_version = 1; name = 'RC Music'; architecture = 'x64'; build_testing = $false
    edition = $configuration.edition; browser_runtime_requirement = $configuration.browser_runtime_requirement
    source_commit = $sourceSnapshot.commit; source_inputs = $sourceSnapshot.input_paths
    recipe_dirty = -not [string]::IsNullOrWhiteSpace($recipeStatus)
    recipe_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
    planner_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1') -Algorithm SHA256).Hash.ToLowerInvariant()
    parser_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'PeDependencyReport.psm1') -Algorithm SHA256).Hash.ToLowerInvariant()
    recipe_path_base = 'repository'; recipe_files = $recipeFiles
    distribution = 'private-local-development-only'
    format_matrix = 'experimental; packaged codec checks pending; no registered associations'
    files = $plan.files; pe_imports = @($reports); external_prerequisites = @($external | Sort-Object)
}
if ($componentEnabled) {
    $manifest.download_component = [ordered]@{
        schema_version = 1; selection_default = $false
        receipt_sha256 = $downloadComponent.receipt_sha256; header_sha256 = $downloadComponent.header_sha256
        activation_marker = $downloadComponent.metadata.activation_marker
        files = @($downloadComponent.files | Select-Object relative_path, size_bytes, sha256)
        pe_closure = $componentClosure
    }
    # The installer will create this marker only after the user selects the
    # component and its files are verified. Packaging alone grants no activation.
    if (Test-Path -LiteralPath (Join-Path $payload 'download-tools/installed-v1')) {
        throw 'Runtime packaging must not activate the optional download component'
    }
}
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 12) + "`n", [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'qml-imports.json'), $scannerText, [Text.UTF8Encoding]::new($false))
[pscustomobject]@{ payload = $payload; manifest = $manifestPath }
