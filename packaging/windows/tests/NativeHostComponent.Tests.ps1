#Requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $repository 'packaging/windows/RcMusicNativeHostComponent.psm1') -Force
$testParent = Join-Path $repository 'out/product/test-runs/native-host-component'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Rejected([scriptblock] $Body) {
    try { & $Body | Out-Null } catch { return }
    throw 'Expected rejection.'
}
function Json($Value) { return ConvertTo-Json -InputObject $Value -Depth 12 -Compress }
function Write-Utf8([string] $Path, [string] $Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function TextHash([string] $Text) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text))).ToLowerInvariant()
}
function Assert-OwnedRoot {
    if ($testId -cnotmatch '\A[0-9a-f]{32}\z' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe owned test root.'
    }
    $current = $testRoot
    while ($current) {
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in owned fixture ancestors.' }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Save-Receipt($Fixture, [ValidateSet('Main', 'Youtube')][string] $Family) {
    $data = $Fixture.$Family
    $path = $Fixture.($Family + 'Receipt')
    Write-Utf8 $path (Json $data)
    $Fixture.($Family + 'Pin') = Hash $path
}
function New-Fixture {
    $state.Fixtures++
    $root = Join-Path $testRoot ('fixture 日本 ' + $state.Fixtures)
    $build = Join-Path $root 'build with spaces'
    $mainDirectory = Join-Path $build 'native-host'
    $youtubeDirectory = Join-Path $build 'youtube-native-host'
    $null = [IO.Directory]::CreateDirectory($mainDirectory)
    $null = [IO.Directory]::CreateDirectory($youtubeDirectory)
    $mainExe = Join-Path $build 'rc_music_native_host.exe'
    $youtubeExe = Join-Path $build 'rc_music_youtube_native_host.exe'
    $mainConfig = Join-Path $mainDirectory 'NativeHostBuildConfiguration.hpp'
    $youtubeConfig = Join-Path $youtubeDirectory 'NativeHostBuildConfiguration.hpp'
    $shared = Join-Path $mainDirectory 'NativeHostBuildProfiles.hpp'
    # These are synthetic text, deliberately not executable PE images.
    Write-Utf8 $mainExe 'RC Music synthetic MAIN host, never execute.'
    Write-Utf8 $youtubeExe 'RC Music synthetic YOUTUBE host, never execute.'
    Write-Utf8 $mainConfig '// Owned opaque MAIN configuration bytes.'
    Write-Utf8 $youtubeConfig '// Owned opaque YOUTUBE configuration bytes.'
    Write-Utf8 $shared '// Opaque shared profile header; no content marker is authority.'
    $cache = [ordered]@{
        RC_MUSIC_NATIVE_MAIN_ORIGIN = 'chrome-extension://' + ('a' * 32) + '/'
        RC_MUSIC_NATIVE_ENDPOINT = 'rc-music-fixture-main'
        RC_MUSIC_NATIVE_YOUTUBE_ORIGIN = 'chrome-extension://' + ('b' * 32) + '/'
        RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT = 'rc-music-fixture-youtube'
        RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE = Join-Path $mainDirectory 'rc_music_native_host.build-profile.json'
        RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE = Join-Path $youtubeDirectory 'rc_music_youtube_native_host.build-profile.json'
        RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER = $shared
    }
    $main = [ordered]@{
        schema_version = 1; component = 'rcmusic-native-host-build-profile'; protocol = 'direct.media.v1'
        host_name = 'com.rcmusic.media'; origin = $cache.RC_MUSIC_NATIVE_MAIN_ORIGIN
        endpoint = $cache.RC_MUSIC_NATIVE_ENDPOINT; enabled = $true
        owner_images = @('RCMusic.exe'); host_images = @('rc_music_native_host.exe')
        executable = [ordered]@{ path = $mainExe.Replace('\', '/'); size_bytes = [IO.FileInfo]::new($mainExe).Length; sha256 = Hash $mainExe }
        configuration = [ordered]@{ path = $mainConfig.Replace('\', '/'); sha256 = Hash $mainConfig }
    }
    $youtube = [ordered]@{
        schema_version = 1; component = 'rcmusic-native-host-build-profile'; protocol = 'youtube.audio.v1'
        host_name = 'com.rcmusic.youtube'; origin = $cache.RC_MUSIC_NATIVE_YOUTUBE_ORIGIN
        endpoint = $cache.RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT; enabled = $true
        owner_images = @('RCMusic.exe'); host_images = @('rc_music_youtube_native_host.exe')
        executable = [ordered]@{ path = $youtubeExe.Replace('\', '/'); size_bytes = [IO.FileInfo]::new($youtubeExe).Length; sha256 = Hash $youtubeExe }
        configuration = [ordered]@{ path = $youtubeConfig.Replace('\', '/'); sha256 = Hash $youtubeConfig }
    }
    $fixture = [pscustomobject]@{
        Root = $root; Build = $build; Cache = $cache; Main = $main; Youtube = $youtube
        MainReceipt = $cache.RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE; YoutubeReceipt = $cache.RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE
        MainPin = ''; YoutubePin = ''; SharedHeader = $shared
    }
    Save-Receipt $fixture Main; Save-Receipt $fixture Youtube
    return $fixture
}
function Plan($Fixture, [hashtable] $Overrides = @{}) {
    $parameters = @{
        BuildDirectory = $Fixture.Build; Cache = $Fixture.Cache
        MainBuildReceipt = $Fixture.MainReceipt; MainBuildReceiptSha256 = $Fixture.MainPin
        YoutubeBuildReceipt = $Fixture.YoutubeReceipt; YoutubeBuildReceiptSha256 = $Fixture.YoutubePin
    }
    foreach ($key in $Overrides.Keys) { $parameters[$key] = $Overrides[$key] }
    return Get-RcMusicNativeHostComponentPlan @parameters
}
function Snapshot($Fixture) {
    return Json @([IO.Directory]::EnumerateFiles($Fixture.Root, '*', [IO.SearchOption]::AllDirectories) |
        Sort-Object -CaseSensitive | ForEach-Object { [ordered]@{ path = $_; sha256 = Hash $_ } })
}

Assert-OwnedRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Owned test root must be fresh.' }
$null = [IO.Directory]::CreateDirectory($testRoot)
try {
    Test-Case 'two pinned build profiles produce deterministic files and portable five-field manifests' {
        $f = New-Fixture
        $plan = Plan $f
        Assert-True ($null -ne $plan) 'Planner returned no component.'
        Assert-Equal @($plan.files).Count 2
        Assert-Equal @($plan.receipts).Count 2
        Assert-Equal @($plan.profiles).Count 2
        Assert-Equal @($plan.manifests).Count 2
        Assert-Equal $plan.receipts[0].family 'Main'
        Assert-Equal $plan.receipts[0].sha256 $f.MainPin
        Assert-Equal $plan.receipts[1].family 'Youtube'
        Assert-Equal $plan.receipts[1].sha256 $f.YoutubePin
        Assert-Equal $plan.shared_header.path $f.SharedHeader
        Assert-Equal $plan.shared_header.sha256 (Hash $f.SharedHeader)
        Assert-Equal $plan.files[0].relative_path 'rc_music_native_host.exe'
        Assert-Equal $plan.files[1].relative_path 'rc_music_youtube_native_host.exe'
        Assert-Equal $plan.files[0].sha256 $f.Main.executable.sha256
        Assert-Equal $plan.files[1].size_bytes $f.Youtube.executable.size_bytes
        for ($index = 0; $index -lt 2; $index++) {
            $item = $plan.manifests[$index]
            $manifest = $item.content | ConvertFrom-Json
            Assert-Equal (@($manifest.PSObject.Properties.Name | Sort-Object) -join '|') 'allowed_origins|description|name|path|type'
            Assert-Equal $item.relative_path ($manifest.name + '.json')
            Assert-Equal $manifest.path $plan.files[$index].relative_path
            Assert-Equal $manifest.type 'stdio'
            Assert-Equal @($manifest.allowed_origins).Count 1
            Assert-Equal $manifest.allowed_origins[0] $plan.profiles[$index].origin
            Assert-True (-not [string]::IsNullOrWhiteSpace($manifest.description)) 'Manifest description is empty.'
            Assert-True ($item.content.EndsWith("`n") -and -not $item.content.EndsWith("`r`n")) 'Manifest must end with one LF.'
            Assert-Equal $item.size_bytes ([Text.Encoding]::UTF8.GetByteCount($item.content))
            Assert-Equal $item.sha256 (TextHash $item.content)
        }
        Assert-Equal (Json $plan) (Json (Plan $f))
    }
    Test-Case 'planning preserves every input and releases all held streams before returning' {
        $f = New-Fixture; $before = Snapshot $f; $cache = Json $f.Cache
        Assert-True ($null -ne (Plan $f)) 'No plan.'
        Assert-Equal (Snapshot $f) $before
        Assert-Equal (Json $f.Cache) $cache
        foreach ($path in [IO.Directory]::EnumerateFiles($f.Root, '*', [IO.SearchOption]::AllDirectories)) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
            $stream.Dispose()
        }
    }
    Test-Case 'both receipt inputs require explicit exact lowercase pins without silent refresh' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($pin in @(('0' * 64), ('A' * 64), ('a' * 63), (('a' * 64) + "`n"))) {
                $f = New-Fixture
                Assert-Rejected { Plan $f @{ ($family + 'BuildReceiptSha256') = $pin } }
            }
        }
    }
    Test-Case 'all four compiled identity cache entries must exactly match the pinned profiles' {
        foreach ($key in @('RC_MUSIC_NATIVE_MAIN_ORIGIN', 'RC_MUSIC_NATIVE_ENDPOINT', 'RC_MUSIC_NATIVE_YOUTUBE_ORIGIN', 'RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT')) {
            foreach ($value in @('', 'different-value', $null)) {
                $f = New-Fixture; $f.Cache[$key] = $value
                Assert-Rejected { Plan $f }
            }
        }
        $f = New-Fixture
        $f.Youtube.origin = $f.Main.origin; $f.Cache.RC_MUSIC_NATIVE_YOUTUBE_ORIGIN = $f.Main.origin
        Save-Receipt $f Youtube
        Assert-Rejected { Plan $f }
        $f = New-Fixture
        $f.Youtube.endpoint = $f.Main.endpoint.ToUpperInvariant()
        $f.Cache.RC_MUSIC_NATIVE_YOUTUBE_ENDPOINT = $f.Youtube.endpoint; Save-Receipt $f Youtube
        Assert-Rejected { Plan $f }
    }
    Test-Case 'receipt paths are fixed producer locations and aliases of the matching cache entries' {
        foreach ($family in @('Main', 'Youtube')) {
            $f = New-Fixture
            $copy = Join-Path $f.Build ($family + '-copy.json')
            Copy-Item -LiteralPath $f.($family + 'Receipt') -Destination $copy
            Assert-Rejected { Plan $f @{ ($family + 'BuildReceipt') = $copy } }
            $f.Cache[('RC_MUSIC_NATIVE_' + $family.ToUpperInvariant() + '_BUILD_PROFILE')] = $copy
            Assert-Rejected { Plan $f @{ ($family + 'BuildReceipt') = $copy } }
        }
        $f = New-Fixture
        $f.Cache.RC_MUSIC_NATIVE_MAIN_BUILD_PROFILE = $f.MainReceipt.ToUpperInvariant().Replace('\', '/')
        $f.Cache.RC_MUSIC_NATIVE_YOUTUBE_BUILD_PROFILE = $f.YoutubeReceipt.ToUpperInvariant().Replace('\', '/')
        Assert-True ($null -ne (Plan $f)) 'Canonical Windows cache aliases were rejected.'
    }
    Test-Case 'disabled or wrong-family receipts cannot be used even with refreshed fixture pins' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($variant in @('disabled', 'protocol', 'name')) {
                $f = New-Fixture
                switch ($variant) {
                    disabled { $f.$family.enabled = $false }
                    protocol { $f.$family.protocol = if ($family -ceq 'Main') { 'youtube.audio.v1' } else { 'direct.media.v1' } }
                    name { $f.$family.host_name = if ($family -ceq 'Main') { 'com.rcmusic.youtube' } else { 'com.rcmusic.media' } }
                }
                Save-Receipt $f $family
                Assert-Rejected { Plan $f }
            }
        }
    }
    Test-Case 'owner and host image lists must be the exact product singletons' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($variant in @('owner-other', 'owner-extra', 'host-other', 'host-extra')) {
                $f = New-Fixture
                switch ($variant) {
                    owner-other { $f.$family.owner_images = @('OtherPlayer.exe') }
                    owner-extra { $f.$family.owner_images += 'OtherPlayer.exe' }
                    host-other { $f.$family.host_images = @('OtherHost.exe') }
                    host-extra { $f.$family.host_images += 'OtherHost.exe' }
                }
                Save-Receipt $f $family
                Assert-Rejected { Plan $f }
            }
        }
    }
    Test-Case 'every current receipt executable configuration and shared header must exist' {
        foreach ($which in @('MainReceipt', 'YoutubeReceipt', 'MainExe', 'YoutubeExe', 'MainConfig', 'YoutubeConfig', 'SharedHeader')) {
            $f = New-Fixture
            $path = switch ($which) {
                MainExe { $f.Main.executable.path }; YoutubeExe { $f.Youtube.executable.path }
                MainConfig { $f.Main.configuration.path }; YoutubeConfig { $f.Youtube.configuration.path }
                default { $f.$which }
            }
            Remove-Item -LiteralPath $path
            Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'recorded executable and configuration paths cannot be replaced by identical sibling or outside copies' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($kind in @('executable', 'configuration')) {
                foreach ($location in @('sibling', 'outside')) {
                    $f = New-Fixture
                    $directory = if ($location -ceq 'outside') { $f.Root } else { $f.Build }
                    $copy = Join-Path $directory ($family + '-' + [IO.Path]::GetFileName($f.$family.$kind.path))
                    Copy-Item -LiteralPath $f.$family.$kind.path -Destination $copy
                    $f.$family.$kind.path = $copy.Replace('\', '/')
                    Save-Receipt $f $family
                    Assert-Rejected { Plan $f }
                }
            }
        }
    }
    Test-Case 'current executable bytes sizes and configuration hashes remain bound to their receipts' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($variant in @('executable', 'configuration', 'declared-size')) {
                $f = New-Fixture
                if ($variant -ceq 'declared-size') { $f.$family.executable.size_bytes++; Save-Receipt $f $family }
                else { [IO.File]::AppendAllText($f.$family.$variant.path, 'drift') }
                Assert-Rejected { Plan $f }
            }
        }
    }
    Test-Case 'shared header is fixed and pinned as opaque bytes without content authority' {
        $f = New-Fixture
        Write-Utf8 $f.SharedHeader 'Not parsed C++, no markers or origin strings at all.'
        $plan = Plan $f
        Assert-True ($null -ne $plan) 'Opaque shared header was rejected.'
        Assert-Equal $plan.shared_header.sha256 (Hash $f.SharedHeader)
        $copy = Join-Path $f.Build 'other-shared.hpp'
        Copy-Item -LiteralPath $f.SharedHeader -Destination $copy
        $f.Cache.RC_MUSIC_NATIVE_BUILD_PROFILES_HEADER = $copy
        Assert-Rejected { Plan $f }
    }
    Test-Case 'a saved plan never refreshes the explicit pins after receipt bytes drift' {
        $f = New-Fixture; $plan = Plan $f
        Assert-True ($null -ne $plan) 'No initial plan.'
        $before = Json $plan
        [IO.File]::AppendAllText($f.MainReceipt, "`n")
        Assert-Rejected { Plan $f }
        Assert-Equal (Json $plan) $before
        Assert-Equal $plan.receipts[0].sha256 $f.MainPin
    }
    Test-Case 'unsafe local paths and receipts outside the build cannot enter the component plan' {
        $f = New-Fixture
        foreach ($path in @('C:/NUL/build', '\\server\share\build', '\\?\C:\build', 'relative/build')) {
            Assert-Rejected { Plan $f @{ BuildDirectory = $path } }
        }
        foreach ($family in @('Main', 'Youtube')) {
            $f = New-Fixture
            $outside = Join-Path $f.Root ($family + '-outside.json')
            Copy-Item -LiteralPath $f.($family + 'Receipt') -Destination $outside
            Assert-Rejected { Plan $f @{ ($family + 'BuildReceipt') = $outside } }
        }
    }
} finally {
    Write-Host "Passed: $($state.Passed); Failed: $($state.Failed); Fixtures: $testRoot"
}
if ($state.Failed -gt 0) { exit 1 }
# Retain the small owned fixtures as evidence; this suite never removes a build.
