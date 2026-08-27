#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-NormalWindowsPath([string] $Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path.StartsWith('\\')) {
        throw 'A normal local Windows path is required.'
    }
    $tail = $Path.Replace('/', '\') -replace '^[A-Za-z]:\\', ''
    foreach ($part in $tail.Split('\')) {
        $device = $part.Split('.')[0].TrimEnd(' ')
        if ($part -in @('', '.', '..') -or $part -match '[\x00-\x1f<>:"|?*]' -or
            $part -match '[. ]$' -or $device -match '^(?i:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])$') {
            throw "Unsafe path name or traversal: $Path"
        }
    }
}

function Assert-NoReparseAncestor([string] $Path, [Collections.Generic.HashSet[string]] $Checked = $null) {
    $cursor = $Path
    while (-not [string]::IsNullOrEmpty($cursor)) {
        if ($null -ne $Checked -and $Checked.Contains($cursor)) { return }
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "A reparse path is not allowed: $cursor"
            }
        }
        if ($null -ne $Checked) { $null = $Checked.Add($cursor) }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
}

function Resolve-NormalChildPath {
    param([Parameter(Mandatory)] [string] $BasePath, [Parameter(Mandatory)] [string] $CandidatePath)
    Assert-NormalWindowsPath $BasePath
    Assert-NormalWindowsPath $CandidatePath
    if (-not [IO.Path]::IsPathFullyQualified($BasePath)) { throw 'Base path must be absolute.' }
    $base = [IO.Path]::GetFullPath($BasePath)
    $resolved = [IO.Path]::GetFullPath($CandidatePath, $base)
    if (-not $resolved.StartsWith($base.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside its scoped parent: $CandidatePath"
    }
    return $resolved
}

function Resolve-QtShellPackageChildPath {
    param([Parameter(Mandatory)] [string] $BasePath, [Parameter(Mandatory)] [string] $CandidatePath)
    $resolved = Resolve-NormalChildPath $BasePath $CandidatePath
    Assert-NoReparseAncestor $resolved
    return $resolved
}

function New-QtShellPackageDirectory {
    param([Parameter(Mandatory)] [string] $Parent, [Parameter(Mandatory)] [string] $Name)
    if ($Name.Contains('/') -or $Name.Contains('\')) { throw 'Package name must be one path component.' }
    $destination = Resolve-QtShellPackageChildPath $Parent $Name
    if (Test-Path -LiteralPath $destination) { throw "Package destination already exists: $destination" }
    $null = New-Item -ItemType Directory -Path $destination -ErrorAction Stop
    return $destination
}

function ConvertTo-QtShellDeploymentPlan {
    param(
        [Parameter(Mandatory)] $DeploymentJson,
        [Parameter(Mandatory)] [string] $QtRoot,
        [Parameter(Mandatory)] [string] $PackageRoot,
        [string] $ApplicationQmlRoot = ''
    )
    if (@($DeploymentJson.files).Count -eq 0) { throw 'The deployment plan is empty.' }
    $entries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    # A plan is built on quiescent inputs. Avoid re-reading the same parents for
    # every QML asset; this is not a handle-based concurrent-writer guarantee.
    $checkedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($mapping in $DeploymentJson.files) {
        $origin = 'qt-toolchain'
        $sourceRoot = $QtRoot
        if (-not [string]::IsNullOrEmpty($ApplicationQmlRoot) -and
            [IO.Path]::GetFullPath([string] $mapping.source).StartsWith([IO.Path]::GetFullPath($ApplicationQmlRoot).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            $origin = 'application-qml'
            $sourceRoot = $ApplicationQmlRoot
        }
        $source = Resolve-NormalChildPath $sourceRoot ([string] $mapping.source)
        $destination = Resolve-NormalChildPath $PackageRoot (Join-Path ([string] $mapping.target) ([IO.Path]::GetFileName($source)))
        Assert-NoReparseAncestor $source $checkedPaths
        Assert-NoReparseAncestor $destination $checkedPaths
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Deployment source is missing: $source" }
        $name = [IO.Path]::GetFileName($source)
        $extension = [IO.Path]::GetExtension($name).ToLowerInvariant()
        if (($extension -notin @('.dll', '.qml', '.qmltypes', '.png') -and $name -cne 'qmldir') -or
            $name -match '^Qt6(?:Test|QuickTest|QuickControlsTestUtils|QmlDebug)' -or
            $name -match '^Qt6.*d\.dll$' -or ($origin -eq 'application-qml' -and $extension -eq '.dll')) {
            throw "Development or unexpected payload file: $name"
        }
        $relative = [IO.Path]::GetRelativePath($PackageRoot, $destination).Replace('\', '/')
        if ($entries.ContainsKey($relative)) { throw "Duplicate deployment destination: $relative" }
        $file = Get-Item -LiteralPath $source -Force
        $entries.Add($relative, [pscustomobject] [ordered] @{
            relative_path = $relative
            origin = $origin
            source_path = [IO.Path]::GetRelativePath($sourceRoot, $source).Replace('\', '/')
            size_bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        })
    }
    [string[]] $keys = @($entries.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return [pscustomobject] [ordered] @{ schema_version = 1; files = @($keys | ForEach-Object { $entries[$_] }) }
}

function Test-QtShellReleaseCache {
    param(
        [Parameter(Mandatory)] [string] $CacheText,
        [Parameter(Mandatory)] [string] $SourceRoot,
        [Parameter(Mandatory)] [string] $QtRoot
    )
    $cache = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::Ordinal)
    foreach ($line in $CacheText -split '\r?\n') {
        if ($line -match '^(?<name>[^/#][^:=]*):[^=]*=(?<value>.*)$') {
            if (-not $cache.TryAdd($Matches.name, $Matches.value)) { throw "Duplicate cache key: $($Matches.name)" }
        }
    }
    $required = [ordered] @{
        CMAKE_BUILD_TYPE = 'Release'
        BUILD_TESTING = 'ON'
        M0_QT_SHELL_SUPPORT = 'ON'
        CMAKE_GENERATOR = 'Ninja'
    }
    foreach ($key in $required.Keys) {
        if (-not $cache.ContainsKey($key) -or $cache[$key] -cne $required[$key]) {
            throw "Release cache requires $key=$($required[$key])."
        }
    }
    foreach ($pair in @(@('CMAKE_HOME_DIRECTORY', $SourceRoot), @('Qt6_DIR', (Join-Path $QtRoot 'lib/cmake/Qt6')))) {
        if (-not $cache.ContainsKey($pair[0]) -or
            -not [IO.Path]::GetFullPath($cache[$pair[0]]).Equals([IO.Path]::GetFullPath($pair[1]), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Release cache has an unexpected source or Qt path: $($pair[0])"
        }
    }
    return [pscustomobject] [ordered] @{ configuration = 'Release'; test_hooks = $true; generator = 'Ninja' }
}

function Get-QtShellCompilerIdentity {
    param([Parameter(Mandatory)] [string] $CompilerInfoText, [Parameter(Mandatory)] [string] $QtConfigText)
    if ($CompilerInfoText -notmatch '(?m)^set\(CMAKE_CXX_COMPILER_VERSION "(19\.43\.34808\.0)"\)\r?$') {
        throw 'Compiler drift requires review of the pinned package recipe.'
    }
    $shellMsvc = $Matches[1]
    if ($CompilerInfoText -notmatch '(?m)^set\(CMAKE_CXX_COMPILER "([^"]+)"\)\r?$') { throw 'Compiler identity is missing.' }
    $compiler = $Matches[1]
    if ($QtConfigText -notmatch '(?m)^QT_VERSION = 6\.11\.2\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_MAJOR_VERSION = 19\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_MINOR_VERSION = 44\r?$' -or
        $QtConfigText -notmatch '(?m)^QT_MSVC_PATCH_VERSION = 35227\r?$') {
        throw 'Qt build compiler/version drift requires review of the package recipe.'
    }
    return [pscustomobject] [ordered] @{ shell_msvc = $shellMsvc; qt_build_msvc = '19.44.35227'; compiler = $compiler }
}

function ConvertFrom-QtShellPeReport {
    param([Parameter(Mandatory)] [string] $Report)
    if ($Report -notmatch '(?m)^\s*8664 machine \(x64\)\s*$') { throw 'PE report does not identify x64.' }
    $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($match in [regex]::Matches($Report, '(?m)^[\t ]+([A-Za-z0-9_.+-]+\.dll)[\t ]*\r?$')) {
        $null = $imports.Add($match.Groups[1].Value)
    }
    if ($imports.Count -eq 0) { throw 'PE report contains no imports.' }
    [string[]] $orderedImports = @($imports)
    [Array]::Sort($orderedImports, [StringComparer]::Ordinal)
    return [pscustomobject] [ordered] @{ machine = 'x64'; imports = $orderedImports }
}

function Assert-QtShellRuntimeFiles {
    param([Parameter(Mandatory)] [string[]] $RelativePaths)
    $required = @(
        'm0_qt_shell.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
        'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickLayouts.dll', 'Qt6Svg.dll',
        'plugins/platforms/qwindows.dll', 'plugins/platforms/qoffscreen.dll',
        'plugins/iconengines/qsvgicon.dll', 'plugins/imageformats/qsvg.dll',
        'qml/QtQuick/qmldir', 'qml/QtQuick/Controls/qmldir', 'qml/QtQuick/Controls/Basic/qmldir',
        'qml/QtQuick/Templates/qmldir', 'qml/QtQuick/Layouts/qmldir'
    )
    foreach ($file in $required) {
        if ($RelativePaths -cnotcontains $file) { throw "Required runtime file is missing: $file" }
    }
    foreach ($path in $RelativePaths) {
        $name = [IO.Path]::GetFileName($path)
        if (($name -match '\.exe$' -and $path -cne 'm0_qt_shell.exe') -or
            $name -match '^(?:Qt6(?:Test|QuickTest|QuickControlsTestUtils|QmlDebug)|vcruntime|msvcp|concrt|vccorlib)' -or
            $name -match '^Qt6.*d\.dll$' -or $path -match '(?:^|/)qmltooling/' -or
            $name -match '\.(?:mp3|mp4|m4a|aac|flac|wav|ogg|opus|wma|pdb|lib|obj|exp)$') {
            throw "Forbidden local evaluation payload: $path"
        }
    }
}

Export-ModuleMember -Function Resolve-QtShellPackageChildPath, New-QtShellPackageDirectory, ConvertTo-QtShellDeploymentPlan, Test-QtShellReleaseCache, Get-QtShellCompilerIdentity, ConvertFrom-QtShellPeReport, Assert-QtShellRuntimeFiles
