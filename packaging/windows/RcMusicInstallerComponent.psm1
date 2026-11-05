#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicDownloadComponentPe.psm1') -Force

function Get-InstallerComponentFields($Value) {
    if ($Value -is [Collections.IDictionary]) { return @($Value.Keys) }
    if ($Value -is [pscustomobject]) { return @($Value.PSObject.Properties.Name) }
    throw 'Installer component records must be objects.'
}

function Assert-InstallerComponentFields($Value, [string[]] $Expected) {
    $actual = @(Get-InstallerComponentFields $Value)
    if ($actual.Count -ne $Expected.Count) { throw 'Invalid installer component fields.' }
    foreach ($name in $actual) {
        if ($name -isnot [string] -or $Expected -cnotcontains $name) {
            throw 'Unknown installer component field.'
        }
    }
}

function Test-InstallerComponentInteger($Value) {
    return $Value -is [byte] -or $Value -is [sbyte] -or $Value -is [int16] -or
        $Value -is [uint16] -or $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64]
}

function Assert-InstallerComponentSha($Value) {
    if ($Value -isnot [string] -or $Value -cnotmatch '\A[0-9a-f]{64}\z') {
        throw 'Invalid installer component lowercase SHA256 pin.'
    }
}

function Assert-InstallerComponentSize($Value) {
    if (-not (Test-InstallerComponentInteger $Value) -or $Value -le 0 -or $Value -gt 134217728) {
        throw 'Invalid installer component file size.'
    }
}

function Get-InstallerComponentRelative($Value) {
    if ($Value -isnot [string] -or $Value.Length -eq 0 -or $Value.Length -gt 4096 -or
        $Value -match '\A[\\/]' -or $Value.Contains(':')) {
        throw 'Invalid installer component relative path.'
    }
    $relative = $Value.Replace('\', '/')
    foreach ($part in $relative.Split('/')) {
        if ($part.Length -eq 0 -or $part.Length -gt 255 -or $part -in @('.', '..') -or
            $part -match '[<>:"|?*\x00-\x1f\x7f]' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '\A(?i:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])(?:\.|\z)') {
            throw 'Unsafe installer component relative path.'
        }
    }
    return $relative
}

function Get-InstallerComponentRuntimeRoot([string] $Value) {
    $root = $Value.Replace('\', '/')
    if ($root -cnotmatch '\A[A-Za-z]:/' -or $root -match '[\x00-\x1f\x7f]') {
        throw 'The runtime directory must be an explicit ordinary local absolute path.'
    }
    if ($root.Length -gt 3) {
        $root = $root.TrimEnd('/')
        $null = Get-InstallerComponentRelative $root.Substring(3)
    }
    return $root.TrimEnd('/')
}

function Get-InstallerComponentCanonicalClosure($Closure) {
    Assert-InstallerComponentFields $Closure @('reports', 'private_imports', 'windows_external')
    $normalized = [ordered]@{}
    foreach ($category in @('reports', 'private_imports', 'windows_external')) {
        if ($Closure.$category -isnot [Array]) { throw 'Installer PE closure members must be arrays.' }
        $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in $Closure.$category) {
            if ($category -ceq 'reports') {
                Assert-InstallerComponentFields $entry @('relative_path', 'domain', 'machine', 'imports')
            } elseif ($category -ceq 'private_imports') {
                Assert-InstallerComponentFields $entry @('relative_path', 'domain', 'name', 'resolved_path')
            } else {
                Assert-InstallerComponentFields $entry @('relative_path', 'domain', 'name')
            }
            $relative = (Get-InstallerComponentRelative $entry.relative_path).ToLowerInvariant()
            if ($entry.domain -isnot [string] -or
                @('network', 'media', 'extractor', 'node') -cnotcontains $entry.domain) {
                throw 'Invalid installer PE load domain.'
            }
            $record = [ordered]@{ relative_path = $relative; domain = $entry.domain }
            if ($category -ceq 'reports') {
                if (-not $paths.Add($relative) -or $entry.machine -isnot [string] -or
                    $entry.machine -cne 'x64' -or $entry.imports -isnot [Array]) {
                    throw 'Invalid or duplicate installer PE report.'
                }
                $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
                foreach ($name in $entry.imports) {
                    if ($name -isnot [string] -or $name -inotmatch '\A[A-Za-z0-9_][A-Za-z0-9_.+-]*\.dll\z') {
                        throw 'Invalid installer PE import name.'
                    }
                    $null = $imports.Add($name.ToLowerInvariant())
                }
                [string[]] $names = @($imports)
                [Array]::Sort($names, [StringComparer]::Ordinal)
                $record.machine = 'x64'; $record.imports = $names
            } else {
                if ($entry.name -isnot [string] -or $entry.name -inotmatch '\A[A-Za-z0-9_][A-Za-z0-9_.+-]*\.dll\z') {
                    throw 'Invalid installer PE edge name.'
                }
                $record.name = $entry.name.ToLowerInvariant()
                if ($category -ceq 'private_imports') {
                    $record.resolved_path = (Get-InstallerComponentRelative $entry.resolved_path).ToLowerInvariant()
                }
            }
            $key = ConvertTo-Json -InputObject $record -Depth 4 -Compress
            if (-not $keys.Add($key)) { throw 'Duplicate installer PE closure entry.' }
        }
        [string[]] $sorted = @($keys)
        [Array]::Sort($sorted, [StringComparer]::Ordinal)
        $normalized[$category] = $sorted
    }
    return ConvertTo-Json -InputObject $normalized -Depth 4 -Compress
}

# The installer verifies physical runtime bytes separately through RuntimePlan.
# This helper validates the declared component without following historical
# source paths, opening build receipts, inspecting PEs, or creating its marker.
function Get-RcMusicInstallerComponent {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] [string] $RuntimeDirectory
    )
    $manifestFields = @(Get-InstallerComponentFields $Manifest)
    if ($manifestFields -cnotcontains 'files' -or $Manifest.files -isnot [Array]) {
        throw 'Runtime manifest files must be an array.'
    }
    $byPath = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    $toolFiles = [Collections.Generic.List[object]]::new()
    foreach ($file in $Manifest.files) {
        if (@(Get-InstallerComponentFields $file) -cnotcontains 'relative_path') {
            throw 'Runtime file is missing its relative path.'
        }
        $relative = Get-InstallerComponentRelative $file.relative_path
        if ($byPath.ContainsKey($relative)) { throw 'Duplicate case-insensitive runtime path.' }
        $byPath.Add($relative, $file)
        if ($relative -ieq 'download-tools' -or $relative.StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase)) {
            $toolFiles.Add($file)
        }
    }
    if ($manifestFields -cnotcontains 'download_component') {
        if ($toolFiles.Count -ne 0) { throw 'Undeclared download tools cannot enter the base installation.' }
        return $null
    }

    $component = $Manifest.download_component
    Assert-InstallerComponentFields $component @('schema_version', 'selection_default', 'receipt_sha256',
        'header_sha256', 'activation_marker', 'files', 'pe_closure')
    if (-not (Test-InstallerComponentInteger $component.schema_version) -or $component.schema_version -ne 1 -or
        $component.selection_default -isnot [bool] -or $component.selection_default) {
        throw 'The optional component requires schema 1 and default selection false.'
    }
    Assert-InstallerComponentSha $component.receipt_sha256
    Assert-InstallerComponentSha $component.header_sha256
    Assert-InstallerComponentFields $component.activation_marker @('relative_path', 'content')
    $markerPath = 'download-tools/installed-v1'
    $markerContent = "RCMusicDownloadTools-v1`n"
    if ($component.activation_marker.relative_path -isnot [string] -or
        $component.activation_marker.relative_path -cne $markerPath -or
        $component.activation_marker.content -isnot [string] -or
        $component.activation_marker.content -cne $markerContent) {
        throw 'The component activation marker must contain exactly the fixed LF descriptor.'
    }
    if ($byPath.ContainsKey($markerPath)) { throw 'The runtime must not contain an activation marker.' }
    if ($component.files -isnot [Array] -or $component.files.Count -lt 10 -or
        $component.files.Count -gt 266 -or $component.files.Count -ne $toolFiles.Count) {
        throw 'Invalid component inventory size or mismatch with runtime tools.'
    }
    $declared = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($file in $component.files) {
        Assert-InstallerComponentFields $file @('relative_path', 'size_bytes', 'sha256')
        $relative = Get-InstallerComponentRelative $file.relative_path
        if (-not $relative.StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase) -or
            -not $declared.Add($relative) -or -not $byPath.ContainsKey($relative)) {
            throw 'Invalid, duplicate or unmatched component inventory path.'
        }
        Assert-InstallerComponentSize $file.size_bytes
        Assert-InstallerComponentSha $file.sha256
        $runtimeFile = $byPath[$relative]
        $fields = @(Get-InstallerComponentFields $runtimeFile)
        if ($fields -cnotcontains 'size_bytes' -or $fields -cnotcontains 'sha256') {
            throw 'Runtime component entries require size and hash.'
        }
        Assert-InstallerComponentSize $runtimeFile.size_bytes
        Assert-InstallerComponentSha $runtimeFile.sha256
        if ($runtimeFile.size_bytes -ne $file.size_bytes -or $runtimeFile.sha256 -cne $file.sha256) {
            throw 'Component inventory differs from runtime size or hash.'
        }
    }

    $network = [ordered]@{
        Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
        Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'
        Msvcp140_1 = 'msvcp140_1.dll'; Vcruntime140 = 'vcruntime140.dll'
        Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
    }
    $fixed = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($role in $network.Keys) {
        $fixed.Add(('download-tools/network/' + $network[$role]), @('network', $role))
    }
    $fixed.Add('download-tools/media/RCMusicMediaWorker.exe', @('media', 'Media'))
    $fixed.Add('download-tools/youtube/extractor/yt-dlp.exe', @('youtube', 'Extractor'))
    $fixed.Add('download-tools/youtube/javascript/node.exe', @('youtube', 'Node'))
    $fixed.Add('download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js', @('youtube', 'EjsCore'))
    $fixed.Add('download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js', @('youtube', 'EjsLibrary'))
    $runtimeRoot = Get-InstallerComponentRuntimeRoot $RuntimeDirectory
    $inventory = [Collections.Generic.List[object]]::new()
    $youtubeCount = 0; [int64] $youtubeBytes = 0
    foreach ($file in $toolFiles) {
        $relative = Get-InstallerComponentRelative $file.relative_path
        if ($fixed.ContainsKey($relative)) {
            $role = $fixed[$relative]
        } elseif ($relative.StartsWith('download-tools/youtube/extractor/_internal/', [StringComparison]::OrdinalIgnoreCase) -or
            $relative.StartsWith('download-tools/youtube/notices/', [StringComparison]::OrdinalIgnoreCase)) {
            $role = @('youtube', 'Support')
        } else {
            throw 'Unknown or misplaced installer component artifact.'
        }
        if ($role[0] -ceq 'youtube') { ++$youtubeCount; $youtubeBytes += $file.size_bytes }
        $inventory.Add([pscustomobject]@{
            source = $runtimeRoot + '/' + $relative; relative_path = $relative
            component = $role[0]; role = $role[1]; size_bytes = $file.size_bytes; sha256 = $file.sha256
        })
    }
    if ($youtubeCount -gt 256 -or $youtubeBytes -gt 536870912) {
        throw 'The YouTube inventory exceeds the existing package limits.'
    }
    if ($youtubeCount -gt 0) {
        foreach ($name in @('python-LICENSE.txt', 'node-LICENSE.txt', 'yt-dlp-LICENSE.txt', 'ejs-LICENSE.txt', 'pyinstaller-COPYING.txt')) {
            if (-not $declared.Contains('download-tools/youtube/notices/' + $name)) {
                throw 'A required YouTube runtime notice is missing.'
            }
        }
    }
    $claimed = Get-InstallerComponentCanonicalClosure $component.pe_closure
    if ($component.pe_closure.reports.Count -gt $inventory.Count) { throw 'Too many component PE reports.' }
    $reports = @($component.pe_closure.reports | ForEach-Object {
        [pscustomobject]@{ relative_path = $_.relative_path; machine = $_.machine; imports = $_.imports }
    })
    $recomputed = Get-RcMusicDownloadComponentPeClosure -Files $inventory.ToArray() -Reports $reports
    if ($claimed -cne (Get-InstallerComponentCanonicalClosure $recomputed)) {
        throw 'Claimed component PE reports or edges differ from the recomputed closure.'
    }
    return [pscustomobject]@{
        available = $true; files = $toolFiles.ToArray()
        marker = [pscustomobject]@{
            relative_path = $markerPath; content = $markerContent; size_bytes = 24
            sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
                [Text.Encoding]::ASCII.GetBytes($markerContent))).ToLowerInvariant()
        }
        receipt_sha256 = $component.receipt_sha256; header_sha256 = $component.header_sha256
    }
}

Export-ModuleMember -Function Get-RcMusicInstallerComponent
