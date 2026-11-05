#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Two fixed source-only candidates. A supplied public key can bind an identity;
# native registration, installation and binary qualification remain separate.
$script:modulePath = [IO.Path]::GetFullPath($PSCommandPath)
$script:families = @{
    Main = [pscustomobject]@{
        channel = 'Main'; component = 'main-extension-source'; native_host_name = 'com.rcmusic.media'
        extension_path = 'extensions/chromium/main'
        assets = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs',
            'popup.css', 'popup.html', 'popup.mjs', 'detector.js', 'media-collector.js')
        recipes = @('packaging/windows/New-RcMusicChromiumExtensionPackage.ps1',
            'packaging/windows/RcMusicYoutubeExtensionPackage.psm1', 'packaging/windows/RcMusicChromiumIdentity.psm1')
    }
    Youtube = [pscustomobject]@{
        channel = 'Youtube'; component = 'youtube-extension-source'; native_host_name = 'com.rcmusic.youtube'
        extension_path = 'extensions/chromium/youtube'
        assets = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs',
            'popup.css', 'popup.html', 'popup.mjs')
        recipes = @('packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1',
            'packaging/windows/New-RcMusicChromiumExtensionPackage.ps1',
            'packaging/windows/RcMusicYoutubeExtensionPackage.psm1', 'packaging/windows/RcMusicChromiumIdentity.psm1')
    }
}

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

function Get-YoutubePackageFile([string] $Root, [string] $RelativePath, [IO.Stream] $Stream) {
    $source = Join-Path $Root $RelativePath
    $Stream.Position = 0
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    return [pscustomobject]@{
        path = $RelativePath
        source = $source
        size_bytes = $Stream.Length
        sha256 = $hash
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

function Get-YoutubePackagePlan([string] $RepositoryRoot, [string] $OutputDirectory, [switch] $AllowExistingOutput,
        [ValidateSet('Main', 'Youtube')][string] $Channel = 'Youtube') {
    $family = $script:families[$Channel]
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
    $criticalAssets = @($family.assets | ForEach-Object { $family.extension_path + '/' + $_ })
    $streams = @{}
    try {
        # The manifest and identity implementation are the same held inputs
        # whose hashes are recorded. The planner never rewrites a source asset.
        foreach ($relative in @($criticalAssets) + @($family.recipes)) {
            $source = Join-Path $root $relative
            Assert-YoutubePackagePlainPath $source
            if (-not [IO.File]::Exists($source)) { throw 'A required Chromium package source file is missing' }
            $streams[$relative] = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        }
        $files = @(foreach ($name in $family.assets) {
            $relative = $family.extension_path + '/' + $name
            $inputFile = Get-YoutubePackageFile $root $relative $streams[$relative]
            [pscustomobject]@{ source = $inputFile.source; relative_path = ('extension/' + $name)
                size_bytes = $inputFile.size_bytes; sha256 = $inputFile.sha256 }
        })
        $recipeFiles = @(foreach ($relative in $family.recipes) {
            $inputFile = Get-YoutubePackageFile $root $relative $streams[$relative]
            [pscustomobject]@{ path = $relative; size_bytes = $inputFile.size_bytes; sha256 = $inputFile.sha256 }
        })
        $manifest = $streams[$family.extension_path + '/manifest.json']
        if ($manifest.Length -lt 1 -or $manifest.Length -gt 65536) {
            throw 'Chromium manifest must contain between 1 and 65536 bytes'
        }
        $manifest.Position = 0
        $manifestBytes = [byte[]]::new([int] $manifest.Length)
        $offset = 0
        while ($offset -lt $manifestBytes.Length) {
            $read = $manifest.Read($manifestBytes, $offset, $manifestBytes.Length - $offset)
            if ($read -eq 0) { throw 'Chromium manifest ended before its pinned size' }
            $offset += $read
        }
        $manifestHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($manifestBytes)).ToLowerInvariant()
        $manifestEntry = @($files | Where-Object relative_path -CEQ 'extension/manifest.json')[0]
        if ($manifestEntry.sha256 -cne $manifestHash -or $manifestEntry.size_bytes -ne $manifestBytes.Length) {
            throw 'Chromium manifest identity does not match its source snapshot'
        }
        $identityModule = Import-Module (Join-Path $root 'packaging/windows/RcMusicChromiumIdentity.psm1') -Force -PassThru
        $identityCommand = $identityModule.ExportedFunctions['Get-RcMusicChromiumManifestIdentity']
        $identity = & $identityCommand -ManifestBytes $manifestBytes
        $extensionDirty = Test-YoutubePackageDirty $root @($family.extension_path) $criticalAssets
        $recipeDirty = Test-YoutubePackageDirty $root $family.recipes $family.recipes
        return [pscustomobject]@{
            schema_version = 1
            component = $family.component
            channel = $family.channel
            extension_identity = $identity
            identity_state = if ($null -eq $identity) { 'identity_pending' } else { 'public_key_bound' }
            native_host_name = $family.native_host_name
            native_manifest = $null
            repository_root = $root
            output = $output
            source_commit = [string]$head.lines[0]
            extension_dirty = [bool]$extensionDirty
            recipe_dirty = [bool]$recipeDirty
            source_dirty = [bool]($extensionDirty -or $recipeDirty)
            source_inputs = @($family.extension_path) + @($family.recipes)
            recipe_files = $recipeFiles
            files = $files
        }
    } finally {
        foreach ($stream in $streams.Values) { $stream.Dispose() }
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
    return Get-YoutubePackagePlan $RepositoryRoot $OutputDirectory -Channel Youtube
}

function Copy-RcMusicYoutubeExtensionPackagePlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Plan)
    if ($Plan.channel -cne 'Youtube') { throw 'The legacy YouTube copier requires a YouTube plan' }
    return Copy-RcMusicChromiumExtensionPackagePlan -Plan $Plan
}

function Copy-RcMusicChromiumExtensionPackagePlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Plan)
    if ($Plan.channel -isnot [string] -or $Plan.channel -cnotin @('Main', 'Youtube')) {
        throw 'A canonical Main or Youtube package channel is required'
    }
    $current = Get-YoutubePackagePlan $Plan.repository_root $Plan.output -Channel $Plan.channel
    Assert-YoutubePackagePlanMatches $Plan $current
    $streams = @{}
    try {
        # Keep every critical input open without write/delete sharing until
        # the receipt is closed. The candidate never replaces a source or output.
        $sourcePaths = @($current.files.source) + @($current.recipe_files | ForEach-Object {
            Join-Path $current.repository_root $_.path
        })
        foreach ($source in $sourcePaths) {
            $streams[$source] = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        }
        $current = Get-YoutubePackagePlan $Plan.repository_root $Plan.output -Channel $Plan.channel
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
        $after = Get-YoutubePackagePlan $current.repository_root $current.output -AllowExistingOutput -Channel $current.channel
        Assert-YoutubePackagePlanMatches $current $after
        $receipt = [ordered]@{
            schema_version = 1
            component = $current.component
            channel = $current.channel
            extension_identity = $current.extension_identity
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

function New-RcMusicChromiumExtensionPackagePlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $OutputDirectory,
        [Parameter(Mandatory)][ValidateSet('Main', 'Youtube')][string] $Channel
    )
    return Get-YoutubePackagePlan $RepositoryRoot $OutputDirectory -Channel $Channel
}

Export-ModuleMember -Function New-RcMusicYoutubeExtensionPackagePlan, Copy-RcMusicYoutubeExtensionPackagePlan,
    New-RcMusicChromiumExtensionPackagePlan, Copy-RcMusicChromiumExtensionPackagePlan
