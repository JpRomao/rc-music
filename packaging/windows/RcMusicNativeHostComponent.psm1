#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicYoutubeNativeManifest.psm1') -Force

function Get-HostComponentLocalPath([string] $Value) {
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value.Length -lt 4 -or $Value.Length -gt 32767 -or
        $Value -cnotmatch '\A[A-Za-z]:[\\/]') {
        throw 'Native host component inputs must be ordinary absolute local paths.'
    }
    $null = [Text.UTF8Encoding]::new($false, $true).GetByteCount($Value)
    foreach ($part in $Value.Substring(3).Replace('\', '/').Split('/')) {
        if ($part.Length -eq 0 -or $part -in @('.', '..') -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '[\x00-\x1f\x7f:"<>|?*]' -or
            $part.Split('.')[0] -imatch '\A(?:CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|COM[1-9¹²³]|LPT[1-9¹²³])\z') {
            throw 'Unsafe native host component path segment.'
        }
    }
    return [IO.Path]::GetFullPath($Value)
}

function Assert-HostComponentPlainPath([string] $Path) {
    $current = $Path
    while ($current) {
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'Native host component inputs cannot traverse reparse points.'
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Open-HostComponentInput([string] $Path) {
    Assert-HostComponentPlainPath $Path
    return [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
}

function Get-HostComponentStreamHash([IO.Stream] $Stream) {
    $Stream.Position = 0
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    $Stream.Position = 0
    return $hash
}

function Assert-HostComponentSamePath([string] $Actual, [string] $Expected) {
    $path = Get-HostComponentLocalPath $Actual
    if (-not [string]::Equals($path, $Expected, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Native host component paths must match their fixed build locations.'
    }
}

# Build receipts are consistency evidence from a trusted producer. This reader
# does not attest a publisher, execute hosts, discover dependencies or register
# anything. The caller separately audits the complete copied PE closure.
function Get-RcMusicNativeHostComponentPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $BuildDirectory,
        [Parameter(Mandatory)] [Collections.IDictionary] $Cache,
        [Parameter(Mandatory)] [string] $MainBuildReceipt,
        [Parameter(Mandatory)] [string] $MainBuildReceiptSha256,
        [Parameter(Mandatory)] [string] $YoutubeBuildReceipt,
        [Parameter(Mandatory)] [string] $YoutubeBuildReceiptSha256
    )
    $build = Get-HostComponentLocalPath $BuildDirectory
    Assert-HostComponentPlainPath $build
    if (-not [IO.Directory]::Exists($build)) { throw 'Native host build directory is absent.' }
    $families = @(
        [pscustomobject]@{
            name = 'Main'; protocol = 'direct.media.v1'; host_name = 'com.rcmusic.media'
            description = 'RC Music media extension host'; basename = 'rc_music_native_host.exe'
            receipt = $MainBuildReceipt; pin = $MainBuildReceiptSha256
            receipt_relative = 'native-host/rc_music_native_host.build-profile.json'
            config_relative = 'native-host/NativeHostBuildConfiguration.hpp'
            cache_receipt = 'RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE'
            cache_origin = 'RC_MUSIC_NATIVE_MAIN_ORIGIN'; cache_endpoint = 'RC_MUSIC_NATIVE_ENDPOINT'
        },
        [pscustomobject]@{
            name = 'Youtube'; protocol = 'youtube.audio.v1'; host_name = 'com.rcmusic.youtube'
            description = 'RC Music YouTube audio extension host'; basename = 'rc_music_youtube_native_host.exe'
            receipt = $YoutubeBuildReceipt; pin = $YoutubeBuildReceiptSha256
            receipt_relative = 'youtube-native-host/rc_music_youtube_native_host.build-profile.json'
            config_relative = 'youtube-native-host/NativeHostBuildConfiguration.hpp'
            cache_receipt = 'RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE'
            cache_origin = 'RC_MUSIC_NATIVE_YOUTUBE_ORIGIN'; cache_endpoint = 'RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT'
        }
    )
    $sharedPath = Get-HostComponentLocalPath (Join-Path $build 'native-host/NativeHostBuildProfiles.hpp')
    if (-not $Cache.Contains('RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER') -or
        $Cache['RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER'] -isnot [string]) {
        throw 'The compiled shared native profiles header must be explicitly present in the cache.'
    }
    Assert-HostComponentSamePath $Cache['RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER'] $sharedPath
    foreach ($family in $families) {
        if ($family.pin -cnotmatch '\A[0-9a-f]{64}\z') { throw 'Explicit lowercase receipt SHA256 pins are required.' }
        $expected = Get-HostComponentLocalPath (Join-Path $build $family.receipt_relative)
        Assert-HostComponentSamePath $family.receipt $expected
        if (-not $Cache.Contains($family.cache_receipt) -or $Cache[$family.cache_receipt] -isnot [string]) {
            throw 'The compiled native host receipt cache alias is missing.'
        }
        Assert-HostComponentSamePath $Cache[$family.cache_receipt] $expected
    }

    $streams = [Collections.Generic.List[IO.Stream]]::new()
    $receipts = [Collections.Generic.List[object]]::new()
    $profiles = [Collections.Generic.List[object]]::new()
    $files = [Collections.Generic.List[object]]::new()
    $manifests = [Collections.Generic.List[object]]::new()
    try {
        # Hold all receipt snapshots while the public strict reader reopens
        # them compatibly. No input is writable/replaceable through these handles.
        foreach ($family in $families) {
            $path = Get-HostComponentLocalPath (Join-Path $build $family.receipt_relative)
            $stream = Open-HostComponentInput $path
            $streams.Add($stream)
            if ($stream.Length -lt 1 -or $stream.Length -gt 65536) { throw 'Native host build receipt exceeds its bounded size.' }
        }
        $sharedStream = Open-HostComponentInput $sharedPath
        $streams.Add($sharedStream)
        $sharedHash = Get-HostComponentStreamHash $sharedStream
        foreach ($family in $families) {
            $receiptPath = Get-HostComponentLocalPath (Join-Path $build $family.receipt_relative)
            $profile = Read-RcMusicNativeHostBuildProfile -ReceiptPath $receiptPath -ReceiptSha256 $family.pin -Family $family.name
            if ($profile.protocol -cne $family.protocol -or $profile.host_name -cne $family.host_name) {
                throw 'The native host receipt belongs to the wrong protocol family.'
            }
            foreach ($field in @('origin', 'endpoint')) {
                $cacheKey = if ($field -ceq 'origin') { $family.cache_origin } else { $family.cache_endpoint }
                if (-not $Cache.Contains($cacheKey) -or $Cache[$cacheKey] -isnot [string] -or
                    $Cache[$cacheKey] -cne $profile.$field) {
                    throw 'Native host origin and endpoint must exactly match the compiled cache.'
                }
            }
            if (@($profile.owner_images).Count -ne 1 -or
                -not [string]::Equals($profile.owner_images[0], 'RCMusic.exe', [StringComparison]::OrdinalIgnoreCase) -or
                @($profile.host_images).Count -ne 1 -or
                -not [string]::Equals($profile.host_images[0], $family.basename, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Native host peers must be the exact product image singletons.'
            }
            $exePath = Get-HostComponentLocalPath (Join-Path $build $family.basename)
            $configPath = Get-HostComponentLocalPath (Join-Path $build $family.config_relative)
            Assert-HostComponentSamePath $profile.executable.path $exePath
            Assert-HostComponentSamePath $profile.configuration.path $configPath
            $exeStream = Open-HostComponentInput $exePath
            $streams.Add($exeStream)
            $configStream = Open-HostComponentInput $configPath
            $streams.Add($configStream)
            if ($exeStream.Length -ne $profile.executable.size_bytes -or
                (Get-HostComponentStreamHash $exeStream) -cne $profile.executable.sha256 -or
                (Get-HostComponentStreamHash $configStream) -cne $profile.configuration.sha256) {
                throw 'Current native host executable or configuration bytes differ from the pinned receipt.'
            }
            $receipts.Add([pscustomobject]@{ family = $family.name; path = $receiptPath; sha256 = $family.pin })
            $profiles.Add($profile)
            $files.Add([pscustomobject]@{
                source = $exePath; relative_path = $family.basename
                size_bytes = $exeStream.Length; sha256 = $profile.executable.sha256
            })
            $content = (ConvertTo-Json -InputObject ([ordered]@{
                name = $family.host_name; description = $family.description; path = $family.basename
                type = 'stdio'; allowed_origins = @($profile.origin)
            }) -Depth 4) + "`n"
            $bytes = [Text.Encoding]::UTF8.GetBytes($content)
            $manifests.Add([pscustomobject]@{
                relative_path = $family.host_name + '.json'; content = $content; size_bytes = $bytes.Length
                sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
            })
        }
        if ([string]::Equals($profiles[0].origin, $profiles[1].origin, [StringComparison]::OrdinalIgnoreCase) -or
            [string]::Equals($profiles[0].endpoint, $profiles[1].endpoint, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The two native host origin and endpoint identities must remain distinct.'
        }
        return [pscustomobject]@{
            receipts = $receipts.ToArray(); shared_header = [pscustomobject]@{ path = $sharedPath; sha256 = $sharedHash }
            profiles = $profiles.ToArray(); files = $files.ToArray(); manifests = $manifests.ToArray()
        }
    } finally {
        foreach ($stream in $streams) { $stream.Dispose() }
    }
}

Export-ModuleMember -Function Get-RcMusicNativeHostComponentPlan
