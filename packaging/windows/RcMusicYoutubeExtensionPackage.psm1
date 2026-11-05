#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Source-only candidate. No extension identity, native host manifest, browser
# registration, installer execution or binary runtime qualification is provided.
$script:modulePath = [IO.Path]::GetFullPath($PSCommandPath)
$script:assetNames = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs',
    'popup.css', 'popup.html', 'popup.mjs')
$script:recipePaths = @('packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1',
    'packaging/windows/RcMusicYoutubeExtensionPackage.psm1')
$script:extensionPath = 'extensions/chromium/youtube'
$script:inputPaths = @($script:extensionPath) + $script:recipePaths

function Get-YoutubePackageAbsolutePath([string] $Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or -not [IO.Path]::IsPathFullyQualified($Path)) {
        throw 'YouTube package paths must be absolute'
    }
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.Length -gt [IO.Path]::GetPathRoot($full).Length) { $full = $full.TrimEnd('\', '/') }
    return $full
}

function Assert-YoutubePackagePlainPath([string] $Path) {
    $ancestor = $Path
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'YouTube package paths must not traverse reparse points'
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

function Invoke-YoutubePackageGit([string] $Root, [string[]] $Arguments) {
    # These are read-only built-ins. No hooks, filters, credentials or optional
    # index refresh locks are needed to identify this source candidate.
    $lines = @(& git --no-optional-locks -c core.fsmonitor=false -C $Root @Arguments 2>&1)
    return [pscustomobject]@{ code = $LASTEXITCODE; lines = $lines }
}

function Get-YoutubePackageFile([string] $Root, [string] $RelativePath) {
    $source = Join-Path $Root $RelativePath
    Assert-YoutubePackagePlainPath $source
    if (-not [IO.File]::Exists($source)) { throw 'A required YouTube package source file is missing' }
    return [pscustomobject]@{
        path = $RelativePath
        source = $source
        size_bytes = [IO.FileInfo]::new($source).Length
        sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Test-YoutubePackageDirty([string] $Root, [string[]] $Paths, [string[]] $CriticalPaths) {
    $status = Invoke-YoutubePackageGit $Root (@('status', '--porcelain=v1', '--untracked-files=all', '--') + $Paths)
    if ($status.code -ne 0) { throw 'YouTube package input status could not be read' }
    if ($status.lines.Count -ne 0) { return $true }
    foreach ($relative in $CriticalPaths) {
        # Porcelain can omit ignored files and assume-unchanged entries. Compare
        # the actual bytes, without Git clean filters, with a committed blob.
        $tracked = Invoke-YoutubePackageGit $Root @('ls-files', '--error-unmatch', '--', $relative)
        if ($tracked.code -ne 0) { return $true }
        $head = Invoke-YoutubePackageGit $Root @('rev-parse', '--verify', ('HEAD:' + $relative))
        if ($head.code -ne 0) { return $true }
        $actual = Invoke-YoutubePackageGit $Root @('hash-object', '--no-filters', '--', $relative)
        if ($actual.code -ne 0 -or $actual.lines.Count -ne 1 -or $head.lines.Count -ne 1 -or
            [string]$actual.lines[0] -cnotmatch '^[0-9a-f]{40}([0-9a-f]{24})?$') {
            throw 'A critical YouTube package source blob could not be inspected'
        }
        if ([string]$actual.lines[0] -cne [string]$head.lines[0]) { return $true }
    }
    return $false
}

function Get-YoutubePackagePlan([string] $RepositoryRoot, [string] $OutputDirectory, [switch] $AllowExistingOutput) {
    $root = Get-YoutubePackageAbsolutePath $RepositoryRoot
    $output = Get-YoutubePackageAbsolutePath $OutputDirectory
    Assert-YoutubePackagePlainPath $root
    Assert-YoutubePackagePlainPath $output
    if (-not [IO.Directory]::Exists($root)) { throw 'YouTube package repository is missing' }
    if (-not [string]::Equals($script:modulePath,
            (Join-Path $root 'packaging/windows/RcMusicYoutubeExtensionPackage.psm1'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The loaded YouTube package recipe must belong to its source repository'
    }
    $outputPrefix = (Join-Path $root 'out/product') + [IO.Path]::DirectorySeparatorChar
    if (-not $output.StartsWith($outputPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'YouTube package output must be a fresh directory below repository out/product'
    }
    if (-not $AllowExistingOutput -and (Test-Path -LiteralPath $output)) {
        throw 'YouTube package output already exists; choose a fresh directory'
    }

    $top = Invoke-YoutubePackageGit $root @('rev-parse', '--show-toplevel')
    if ($top.code -ne 0 -or $top.lines.Count -ne 1 -or
        -not [string]::Equals([IO.Path]::GetFullPath([string]$top.lines[0]), $root, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'YouTube package repository must identify the Git worktree root'
    }
    $head = Invoke-YoutubePackageGit $root @('rev-parse', '--verify', 'HEAD')
    if ($head.code -ne 0 -or $head.lines.Count -ne 1 -or [string]$head.lines[0] -cnotmatch '^[0-9a-f]{40}$') {
        throw 'YouTube package source must have a committed HEAD'
    }
    $criticalAssets = @($script:assetNames | ForEach-Object { $script:extensionPath + '/' + $_ })
    $files = @(foreach ($name in $script:assetNames) {
        $inputFile = Get-YoutubePackageFile $root ($script:extensionPath + '/' + $name)
        [pscustomobject]@{ source = $inputFile.source; relative_path = ('extension/' + $name)
            size_bytes = $inputFile.size_bytes; sha256 = $inputFile.sha256 }
    })
    $recipeFiles = @(foreach ($relative in $script:recipePaths) {
        $inputFile = Get-YoutubePackageFile $root $relative
        [pscustomobject]@{ path = $relative; size_bytes = $inputFile.size_bytes; sha256 = $inputFile.sha256 }
    })
    $extensionDirty = Test-YoutubePackageDirty $root @($script:extensionPath) $criticalAssets
    $recipeDirty = Test-YoutubePackageDirty $root $script:recipePaths $script:recipePaths
    return [pscustomobject]@{
        schema_version = 1
        component = 'youtube-extension-source'
        identity_state = 'identity_pending'
        native_host_name = 'com.rcmusic.youtube'
        native_manifest = $null
        repository_root = $root
        output = $output
        source_commit = [string]$head.lines[0]
        extension_dirty = [bool]$extensionDirty
        recipe_dirty = [bool]$recipeDirty
        source_dirty = [bool]($extensionDirty -or $recipeDirty)
        source_inputs = @($script:inputPaths)
        recipe_files = $recipeFiles
        files = $files
    }
}

function Assert-YoutubePackagePlanMatches([object] $Plan, [object] $Current) {
    if ((ConvertTo-Json -InputObject $Plan -Depth 10 -Compress) -cne
        (ConvertTo-Json -InputObject $Current -Depth 10 -Compress)) {
        throw 'YouTube package plan changed or its source snapshot is stale'
    }
}

function New-RcMusicYoutubeExtensionPackagePlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $OutputDirectory
    )
    return Get-YoutubePackagePlan $RepositoryRoot $OutputDirectory
}

function Copy-RcMusicYoutubeExtensionPackagePlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Plan)
    $current = Get-YoutubePackagePlan $Plan.repository_root $Plan.output
    Assert-YoutubePackagePlanMatches $Plan $current
    $streams = @{}
    try {
        # Keep the nine critical inputs open without write/delete sharing until
        # the receipt is closed. The candidate never replaces a source or output.
        $sourcePaths = @($current.files.source) + @($current.recipe_files | ForEach-Object {
            Join-Path $current.repository_root $_.path
        })
        foreach ($source in $sourcePaths) {
            $streams[$source] = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        }
        $current = Get-YoutubePackagePlan $Plan.repository_root $Plan.output
        Assert-YoutubePackagePlanMatches $Plan $current
        $null = New-Item -ItemType Directory -Path $current.output -ErrorAction Stop
        $extension = Join-Path $current.output 'extension'
        $null = New-Item -ItemType Directory -Path $extension -ErrorAction Stop
        foreach ($entry in $current.files) {
            $destination = Join-Path $current.output $entry.relative_path
            $target = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            try {
                $sourceStream = $streams[$entry.source]
                $sourceStream.Position = 0
                $sourceStream.CopyTo($target)
                $target.Flush($true)
            } finally { $target.Dispose() }
            if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256 -or
                [IO.FileInfo]::new($destination).Length -ne $entry.size_bytes) {
                throw 'Copied YouTube package bytes do not match the source inventory; partial output was preserved'
            }
        }
        $after = Get-YoutubePackagePlan $current.repository_root $current.output -AllowExistingOutput
        Assert-YoutubePackagePlanMatches $current $after
        $receipt = [ordered]@{
            schema_version = 1
            component = $current.component
            identity_state = $current.identity_state
            native_host_name = $current.native_host_name
            native_manifest = $null
            source_commit = $current.source_commit
            extension_dirty = $current.extension_dirty
            recipe_dirty = $current.recipe_dirty
            source_dirty = $current.source_dirty
            source_inputs = $current.source_inputs
            recipe_files = $current.recipe_files
            files = @($current.files | ForEach-Object {
                [pscustomobject]@{ relative_path = $_.relative_path; size_bytes = $_.size_bytes; sha256 = $_.sha256 }
            })
        }
        $receiptPath = Join-Path $current.output 'package-receipt.json'
        $receiptStream = [IO.File]::Open($receiptPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try {
            $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $receipt -Depth 10) + "`n")
            $receiptStream.Write($bytes, 0, $bytes.Length)
            $receiptStream.Flush($true)
        } finally { $receiptStream.Dispose() }
        return [pscustomobject]@{ package_directory = $current.output; extension_directory = $extension; receipt_path = $receiptPath }
    } finally {
        foreach ($stream in $streams.Values) { $stream.Dispose() }
    }
}

Export-ModuleMember -Function New-RcMusicYoutubeExtensionPackagePlan, Copy-RcMusicYoutubeExtensionPackagePlan
