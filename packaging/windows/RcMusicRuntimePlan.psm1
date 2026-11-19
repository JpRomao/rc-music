#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-RcMusicRuntimeRelativePath([object] $Path) {
    if ($Path -isnot [string] -or [string]::IsNullOrWhiteSpace($Path) -or
        [IO.Path]::IsPathRooted($Path) -or $Path.Contains(':')) {
        throw 'Runtime relative path must not be empty, rooted, or an alternate stream'
    }
    $relative = $Path.Replace('\', '/')
    foreach ($segment in $relative.Split('/')) {
        if ($segment -in @('', '.', '..') -or $segment -match '[<>:"|?*\x00-\x1f]' -or
            $segment -match '[ .]$' -or $segment -match '^(?i:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|$)') {
            throw 'Runtime path contains an unsafe segment or Windows name'
        }
    }
    return $relative
}

function Assert-RcMusicRuntimeNoReparse([string] $Path) {
    $current = $Path
    while (-not [string]::IsNullOrEmpty($current)) {
        try {
            $attributes = [IO.File]::GetAttributes($current)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Runtime path contains a reparse point'
            }
        } catch {
            $cause = $_.Exception.GetBaseException()
            if ($cause -isnot [IO.FileNotFoundException] -and $cause -isnot [IO.DirectoryNotFoundException]) { throw }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Get-RcMusicRuntimeAbsolutePath([object] $Path) {
    if ($Path -isnot [string] -or [string]::IsNullOrWhiteSpace($Path) -or
        -not [IO.Path]::IsPathFullyQualified($Path) -or $Path -match '^\\\\[?.]\\') {
        throw 'Runtime source/root/destination path must be an ordinary absolute path'
    }
    $normalized = $Path.Replace('/', '\')
    $prefix = [IO.Path]::GetPathRoot($normalized)
    $tail = $normalized.Substring($prefix.Length).TrimEnd('\')
    if ($tail.Length -gt 0) { $null = Get-RcMusicRuntimeRelativePath $tail }
    $absolute = [IO.Path]::GetFullPath($normalized)
    if ($absolute.Length -gt $prefix.Length) { $absolute = $absolute.TrimEnd('\') }
    Assert-RcMusicRuntimeNoReparse $absolute
    return $absolute
}

function New-RcMusicRuntimePlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][object[]] $Files,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]] $AllowedSourceRoots,
        [Parameter(Mandatory)][string] $Destination
    )
    if ($Files.Count -eq 0 -or $AllowedSourceRoots.Count -eq 0) { throw 'Runtime files and allowed roots must not be empty' }
    $destinationRoot = Get-RcMusicRuntimeAbsolutePath $Destination
    if (Test-Path -LiteralPath $destinationRoot) { throw 'Runtime destination must not already exist' }

    $roots = [Collections.Generic.List[string]]::new()
    foreach ($root in $AllowedSourceRoots) {
        $resolvedRoot = Get-RcMusicRuntimeAbsolutePath $root
        if (-not [IO.Directory]::Exists($resolvedRoot)) { throw 'Runtime source root must be an existing directory' }
        $roots.Add($resolvedRoot)
    }
    $destinations = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $entries = [Collections.Generic.List[object]]::new()
    foreach ($file in $Files) {
        if ($null -eq $file) { throw 'Runtime mapping must contain source and relative_path' }
        foreach ($field in @('source', 'relative_path')) {
            $present = if ($file -is [Collections.IDictionary]) { $file.Contains($field) }
                else { $null -ne $file.PSObject.Properties[$field] }
            if (-not $present) { throw "Runtime mapping is missing $field" }
        }
        $source = Get-RcMusicRuntimeAbsolutePath $file.source
        $relative = Get-RcMusicRuntimeRelativePath $file.relative_path
        if (-not [IO.File]::Exists($source)) { throw 'Runtime source must be an existing file, not a directory' }
        $contained = $false
        foreach ($root in $roots) {
            if ($source.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
                $contained = $true
                break
            }
        }
        if (-not $contained) { throw 'Runtime source is outside its allowed source roots' }
        if (-not $destinations.Add($relative)) { throw 'Runtime destination has a duplicate case-insensitive path' }
        $entries.Add([pscustomobject]@{
            source = $source
            relative_path = $relative
            size_bytes = (Get-Item -LiteralPath $source).Length
            sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    foreach ($entry in $entries) {
        $segments = $entry.relative_path.Split('/')
        for ($index = 1; $index -lt $segments.Length; $index++) {
            if ($destinations.Contains(($segments[0..($index - 1)] -join '/'))) {
                throw 'Runtime destination has a file/directory collision'
            }
        }
    }
    return [pscustomobject]@{
        destination = $destinationRoot
        allowed_source_roots = $roots.ToArray()
        files = $entries.ToArray()
    }
}

function Copy-RcMusicRuntimePlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Plan)

    # Revalidate the complete plan before creating any payload directory.
    $verified = New-RcMusicRuntimePlan -Files $Plan.files -AllowedSourceRoots $Plan.allowed_source_roots `
        -Destination $Plan.destination
    for ($index = 0; $index -lt $verified.files.Count; $index++) {
        $expected = $Plan.files[$index]
        $actual = $verified.files[$index]
        if ($expected.sha256 -cne $actual.sha256 -or $expected.size_bytes -cne $actual.size_bytes) {
            throw 'Runtime source hash or size changed since planning'
        }
    }

    $null = [IO.Directory]::CreateDirectory($verified.destination)
    foreach ($entry in $verified.files) {
        $target = Join-Path $verified.destination $entry.relative_path
        Assert-RcMusicRuntimeNoReparse $entry.source
        Assert-RcMusicRuntimeNoReparse $target
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
        $inputStream = [IO.File]::Open($entry.source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        try {
            $outputStream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try { $inputStream.CopyTo($outputStream) }
            finally { $outputStream.Dispose() }
        } finally { $inputStream.Dispose() }
        if ((Get-Item -LiteralPath $target).Length -cne $entry.size_bytes -or
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
            throw 'Runtime copied bytes do not match the planned hash and size'
        }
    }
    return $verified
}

function Get-RcMusicDesktopBuildConfiguration {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][Collections.IDictionary] $Cache,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [ValidateSet('ON', 'OFF')][string] $BuildTesting = 'OFF',
        [ValidateSet('Lite', 'CompleteExperimental')][string] $Edition = 'Lite',
        [object] $DownloadProfileHeader,
        [object] $DownloadProfileHeaderSha256,
        [switch] $NativeHostComponent,
        [switch] $TestProfileRoot
    )
    $hasDownloadHeader = $PSBoundParameters.ContainsKey('DownloadProfileHeader')
    $hasDownloadPin = $PSBoundParameters.ContainsKey('DownloadProfileHeaderSha256')
    $downloadEnabled = $hasDownloadHeader -or $hasDownloadPin
    $nativeEnabled = [bool] $NativeHostComponent
    $nativeMode = if ($nativeEnabled) { 'ON' } else { 'OFF' }
    $downloadHeader = $null
    $downloadHeaderHash = $null
    if ($downloadEnabled) {
        if (-not $hasDownloadHeader -or -not $hasDownloadPin -or
            $DownloadProfileHeader -isnot [string] -or [string]::IsNullOrWhiteSpace($DownloadProfileHeader) -or
            $DownloadProfileHeaderSha256 -isnot [string] -or $DownloadProfileHeaderSha256 -cnotmatch '\A[0-9a-f]{64}\z') {
            throw 'Download profile header and lowercase SHA256 pin must be supplied together'
        }
        $downloadHeader = Get-RcMusicRuntimeAbsolutePath $DownloadProfileHeader
        if (-not [IO.File]::Exists($downloadHeader)) { throw 'Download profile header must be an existing file' }
        $cachedHeader = $Cache['RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER']
        if ($cachedHeader -isnot [string] -or [string]::IsNullOrWhiteSpace($cachedHeader)) {
            throw 'Download profile header must match its explicit build cache entry'
        }
        $cachedHeader = Get-RcMusicRuntimeAbsolutePath $cachedHeader
        if (-not [string]::Equals($cachedHeader, $downloadHeader, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Download profile header does not match the compiled build cache path'
        }
        $downloadHeaderHash = (Get-FileHash -LiteralPath $downloadHeader -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($downloadHeaderHash -cne $DownloadProfileHeaderSha256) { throw 'Download profile header SHA256 changed' }
    } elseif ($Cache.Contains('RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER') -and
        $null -ne $Cache['RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER'] -and
        ($Cache['RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER'] -isnot [string] -or
            $Cache['RC_MUSIC_PLAYER_DOWNLOAD_PROFILE_HEADER'].Length -ne 0)) {
        throw 'A configured download profile header requires an explicit pinned opt-in'
    }
    if ($nativeEnabled -and -not $downloadEnabled) {
        throw 'Native host packaging requires an explicit pinned download profile header'
    }
    if (-not $nativeEnabled -and $Cache.Contains('RC_MUSIC_BUILD_NATIVE_MESSAGING') -and
        ($Cache['RC_MUSIC_BUILD_NATIVE_MESSAGING'] -isnot [string] -or
            $Cache['RC_MUSIC_BUILD_NATIVE_MESSAGING'] -cne 'OFF')) {
        throw 'Unsupported desktop build configuration: RC_MUSIC_BUILD_NATIVE_MESSAGING requires an explicit native-host opt-in'
    }
    $root = Get-RcMusicRuntimeAbsolutePath $RepositoryRoot
    foreach ($required in @{ CMAKE_BUILD_TYPE = 'Release'; BUILD_TESTING = $BuildTesting }.GetEnumerator()) {
        if ($Cache[$required.Key] -cne $required.Value) {
            throw "Unsupported desktop build configuration: $($required.Key)"
        }
    }
    $complete = $Edition -ieq 'CompleteExperimental'
    # Preserve the default Lite contract, including historical caches without
    # an embedded-browser key. Only the explicit edition opts into the engine.
    if (-not $complete -and $Cache.Contains('RC_MUSIC_BUILD_EMBEDDED_BROWSER') -and
        ($Cache['RC_MUSIC_BUILD_EMBEDDED_BROWSER'] -isnot [string] -or
            $Cache['RC_MUSIC_BUILD_EMBEDDED_BROWSER'] -cne 'OFF')) {
        throw 'Unsupported desktop build configuration: RC_MUSIC_BUILD_EMBEDDED_BROWSER (local/Lite runtime requires OFF)'
    }
    $sourceHome = $Cache['CMAKE_HOME_DIRECTORY']
    if ($sourceHome -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($sourceHome)) {
        throw 'Unsupported desktop build entrypoint path'
    }
    $sourceHome = [IO.Path]::GetFullPath($sourceHome).TrimEnd('\', '/')
    $legacy = Join-Path $root 'experiments/m0/candidates/qt/shell'
    if ($sourceHome -ieq $root) {
        if ($Cache['RC_MUSIC_BUILD_DESKTOP'] -cne 'ON') {
            throw 'Unsupported desktop build configuration: RC_MUSIC_BUILD_DESKTOP'
        }
        $entrypoint = 'product'
    } elseif ($sourceHome -ieq $legacy) {
        if ($Cache['M0_QT_AUDIO_SUPPORT'] -cne 'ON' -or $Cache['M0_QT_SHELL_SUPPORT'] -cne 'ON') {
            throw 'Unsupported desktop build configuration: legacy audio and shell must be enabled'
        }
        $entrypoint = 'legacy'
    } else { throw 'Unsupported desktop build entrypoint' }
    if ($downloadEnabled) {
        if ($entrypoint -cne 'product') { throw 'A download profile requires the product desktop entrypoint' }
        $downloadOptions = [ordered]@{
            RC_MUSIC_BUILD_BROWSER_INTEGRATION = 'ON'
            RC_MUSIC_BUILD_DOWNLOADS = 'ON'
            RC_MUSIC_BUILD_DOWNLOAD_NETWORK = 'ON'
            RC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE = 'ON'
            RC_MUSIC_BUILD_NATIVE_MESSAGING = $nativeMode
        }
        foreach ($option in $downloadOptions.GetEnumerator()) {
            if (-not $Cache.Contains($option.Key) -or $Cache[$option.Key] -isnot [string] -or
                $Cache[$option.Key] -cne $option.Value) {
                throw "Unsupported desktop build configuration: $($option.Key) (download profile requires $($option.Value))"
            }
        }
    }
    if ($nativeEnabled) {
        foreach ($key in @('RC_MUSIC_NATIVE_MAIN_ORIGIN', 'RC_MUSIC_NATIVE_YOUTUBE_ORIGIN')) {
            if (-not $Cache.Contains($key) -or $Cache[$key] -isnot [string] -or
                $Cache[$key] -cnotmatch '\Achrome-extension://[a-p]{32}/\z') {
                throw "Invalid trusted native origin identity: $key"
            }
        }
        foreach ($key in @('RC_MUSIC_NATIVE_ENDPOINT', 'RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT')) {
            if (-not $Cache.Contains($key) -or $Cache[$key] -isnot [string] -or
                $Cache[$key] -cnotmatch '\A[A-Za-z0-9_-]{1,96}\z') {
                throw "Invalid trusted native pipe endpoint identity: $key"
            }
        }
        if ([string]::Equals($Cache['RC_MUSIC_NATIVE_MAIN_ORIGIN'], $Cache['RC_MUSIC_NATIVE_YOUTUBE_ORIGIN'],
                [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($Cache['RC_MUSIC_NATIVE_ENDPOINT'], $Cache['RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT'],
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Principal and YouTube native origin and pipe identities must be distinct'
        }
        foreach ($key in @('RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE', 'RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE',
                'RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER')) {
            $alias = $Cache[$key]
            if (-not $Cache.Contains($key) -or $alias -isnot [string] -or
                $alias -notmatch '\A[A-Za-z]:[\\/]' -or $alias -match '[\x00-\x1f\x7f]') {
                throw "Native build alias must be an ordinary absolute local path: $key"
            }
            try {
                # Lexical validation only. The component reader owns file
                # existence, fixed build locations, pins and receipt matching.
                $tail = $alias.Replace('\', '/').Substring(3)
                $null = Get-RcMusicRuntimeRelativePath $tail
            } catch {
                throw "Invalid native build alias path: $key"
            }
        }
    }
    # S13.1: `--profile-root <dir>` is a test build option. A runtime carries it only on an explicit
    # opt-in that matches the compiled cache; the installer refuses such a runtime.
    $testProfileRootEnabled = [bool] $TestProfileRoot
    $cachedTestProfileRoot = if ($Cache.Contains('RC_MUSIC_TEST_PROFILE_ROOT')) { $Cache['RC_MUSIC_TEST_PROFILE_ROOT'] } else { 'OFF' }
    $expectedTestProfileRoot = if ($testProfileRootEnabled) { 'ON' } else { 'OFF' }
    if ($cachedTestProfileRoot -isnot [string] -or $cachedTestProfileRoot -cne $expectedTestProfileRoot) {
        throw "Unsupported desktop build configuration: RC_MUSIC_TEST_PROFILE_ROOT (runtime requires $expectedTestProfileRoot)"
    }
    $browserSdkRoot = $null
    if ($complete) {
        if ($entrypoint -cne 'product') {
            throw 'CompleteExperimental requires the product desktop entrypoint'
        }
        $downloadMode = if ($downloadEnabled) { 'ON' } else { 'OFF' }
        $options = [ordered]@{
            RC_MUSIC_BUILD_EMBEDDED_BROWSER = 'ON'
            RC_MUSIC_BUILD_BROWSER_INTEGRATION = $downloadMode
            RC_MUSIC_BUILD_DOWNLOADS = $downloadMode
            RC_MUSIC_BUILD_DOWNLOAD_NETWORK = $downloadMode
            RC_MUSIC_BUILD_DOWNLOAD_PERSISTENCE = $downloadMode
            RC_MUSIC_BUILD_NATIVE_MESSAGING = $nativeMode
        }
        foreach ($option in $options.GetEnumerator()) {
            if (-not $Cache.Contains($option.Key) -or $Cache[$option.Key] -isnot [string] -or
                $Cache[$option.Key] -cne $option.Value) {
                throw "Unsupported desktop build configuration: $($option.Key) (CompleteExperimental requires $($option.Value))"
            }
        }
        try {
            $browserSdkRoot = Get-RcMusicRuntimeAbsolutePath $Cache['RC_MUSIC_WEBVIEW2_SDK_ROOT']
            if (-not [IO.Directory]::Exists($browserSdkRoot)) { throw 'SDK root must be an existing directory' }
            foreach ($relative in @('build/native/include/WebView2.h', 'build/native/include/WebView2EnvironmentOptions.h',
                    'build/native/x64/WebView2LoaderStatic.lib', 'LICENSE.txt', 'NOTICE.txt')) {
                $artifact = Get-RcMusicRuntimeAbsolutePath (Join-Path $browserSdkRoot $relative)
                if (-not [IO.File]::Exists($artifact)) { throw "SDK artifact must be an existing file: $relative" }
            }
        } catch {
            throw "Unsupported desktop build configuration: RC_MUSIC_WEBVIEW2_SDK_ROOT (SDK: $($_.Exception.Message))"
        }
    }
    # Explicit ON remains available to inspection/smoke callers. Runtime
    # packaging requests OFF and cannot silently accept an instrumented cache.
    return [pscustomobject]@{
        edition = if ($complete) { 'CompleteExperimental' } else { 'Lite' }
        download_component_enabled = $downloadEnabled
        native_hosts_enabled = $nativeEnabled
        test_profile_root = $testProfileRootEnabled
        download_profile_header = $downloadHeader
        download_profile_header_sha256 = $downloadHeaderHash
        browser_sdk_root = $browserSdkRoot
        browser_runtime_requirement = if ($complete) { 'existing-webview2-evergreen-x64' } else { 'none' }
        entrypoint = $entrypoint
        target = 'm0_qt_shell'
        executable = 'm0_qt_shell.exe'
        qml_source = Join-Path $root 'src/ui/qml'
        instrumented_executable = if ($BuildTesting -ceq 'OFF') { $null }
            elseif ($entrypoint -ceq 'product') { 'tests/process/rc_music_test_shell.exe' }
            else { 'm0_qt_shell.exe' }
        audio_integration_executable = if ($BuildTesting -ceq 'OFF') { $null }
            elseif ($entrypoint -ceq 'product') { 'rc_music_local_audio_integration_tests.exe' }
            else { 'm0_qt_local_audio_integration_tests.exe' }
    }
}

function Get-RcMusicSourceSnapshot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [ValidateSet('product', 'legacy')][string] $EntryPoint = 'legacy'
    )

    $root = Get-RcMusicRuntimeAbsolutePath $RepositoryRoot
    $inputPaths = @(
        'CMakeLists.txt',
        'cmake',
        'include',
        'src',
        'tests',
        'tools',
        'assets',
        'packaging/windows'
    )
    if ($EntryPoint -ieq 'legacy') {
        $inputPaths = @('experiments/m0/candidates/qt/shell', 'experiments/m0/protocol',
            'experiments/m0/shared/shell-fixture') + $inputPaths
    }
    $topLevel = @(& git --no-optional-locks -C $root rev-parse --show-toplevel 2>&1)
    if ($LASTEXITCODE -ne 0 -or $topLevel.Count -ne 1 -or
        -not [string]::Equals([IO.Path]::GetFullPath([string]$topLevel[0]), $root, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Runtime source repository root must identify the Git worktree root'
    }
    $head = @(& git --no-optional-locks -C $root rev-parse --verify HEAD 2>&1)
    if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1) {
        throw 'Runtime source repository must have a committed HEAD'
    }
    $changes = @(& git --no-optional-locks -C $root status --porcelain=v1 --untracked-files=all -- @inputPaths 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Runtime source input status could not be read' }
    if ($changes.Count -ne 0) { throw 'Runtime source inputs must be clean before recording a snapshot' }
    return [pscustomobject]@{ commit = [string]$head[0]; input_paths = $inputPaths }
}

Export-ModuleMember -Function New-RcMusicRuntimePlan, Copy-RcMusicRuntimePlan, Get-RcMusicSourceSnapshot, Get-RcMusicDesktopBuildConfiguration
