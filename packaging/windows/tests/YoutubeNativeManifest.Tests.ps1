#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testParent = Join-Path $repository 'out/product/test-runs/youtube-native-manifest'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
$recipeNames = @('RcMusicYoutubeNativeManifest.psm1', 'New-RcMusicYoutubeNativeManifest.ps1')
$extensionId = 'abcdefghijklmnopabcdefghijklmnop'
$origin = 'chrome-extension://' + $extensionId + '/'

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected, [string] $Message = 'Values differ') {
    if ($Actual -cne $Expected) { throw $Message }
}
function Assert-Throws([scriptblock] $Body, [string] $Message) {
    $thrown = $false
    try { & $Body | Out-Null } catch { $thrown = $true }
    if (-not $thrown) { throw $Message }
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Utf8([string] $Path, [string] $Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Assert-SafeTestRoot {
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe owned fixture root'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe fixture ancestor' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Save-Receipt($Fixture) {
    Write-Utf8 $Fixture.Receipt (ConvertTo-Json -InputObject $Fixture.Data -Depth 12 -Compress)
    $Fixture.Pin = Hash $Fixture.Receipt
}
function New-Fixture {
    $state.Fixtures++
    $root = Join-Path $testRoot ('fixture 日本 ' + $state.Fixtures)
    $packaging = Join-Path $root 'packaging/windows'
    $build = Join-Path $root 'build with spaces'
    $payload = Join-Path $root 'payload 日本'
    $main = Join-Path $root 'principal'
    $null = New-Item -ItemType Directory -Path $packaging, $build, $payload, $main
    foreach ($name in $recipeNames) {
        Copy-Item -LiteralPath (Join-Path $repository ('packaging/windows/' + $name)) -Destination $packaging
    }
    $exe = Join-Path $build 'rc_music_youtube_native_host.exe'
    # Deliberately not a PE executable. The planner must inspect bytes, never run it.
    Write-Utf8 $exe 'RC Music owned synthetic non-executable host bytes.'
    $copy = Join-Path $payload 'rc_music_youtube_native_host.exe'
    Copy-Item -LiteralPath $exe -Destination $copy
    $configuration = Join-Path $build 'NativeHostBuildConfiguration.hpp'
    Write-Utf8 $configuration '// Owned receipt fixture. Not compiled or loaded.'
    $sentinel = Join-Path $main 'preserve.keep'
    Write-Utf8 $sentinel 'Principal files must remain unchanged.'
    $data = [ordered]@{
        schema_version = 1
        component = 'rcmusic-native-host-build-profile'
        protocol = 'youtube.audio.v1'
        host_name = 'com.rcmusic.youtube'
        origin = $origin
        endpoint = 'rc-music-youtube-fixture'
        enabled = $true
        owner_images = @('RCMusic.exe')
        host_images = @('rc_music_youtube_native_host.exe')
        executable = [ordered]@{ path = $exe.Replace('\', '/'); sha256 = (Hash $exe); size_bytes = [IO.FileInfo]::new($exe).Length }
        configuration = [ordered]@{ path = $configuration.Replace('\', '/'); sha256 = (Hash $configuration) }
    }
    $fixture = [pscustomobject]@{
        Root = $root; Packaging = $packaging; Exe = $exe; Host = $copy; Config = $configuration
        Main = $sentinel; MainHash = (Hash $sentinel); Data = $data; Pin = ''
        Receipt = (Join-Path $build 'host-build-profile.json')
        Output = (Join-Path $root 'out/product/native manifest candidate')
        Entry = (Join-Path $packaging 'New-RcMusicYoutubeNativeManifest.ps1')
    }
    Save-Receipt $fixture
    Remove-Module RcMusicYoutubeNativeManifest -Force -ErrorAction SilentlyContinue
    Import-Module (Join-Path $packaging 'RcMusicYoutubeNativeManifest.psm1') -Global -Force
    return $fixture
}
function New-Plan($Fixture) {
    return New-RcMusicYoutubeNativeManifestPlan -RepositoryRoot $Fixture.Root -BuildReceipt $Fixture.Receipt `
        -BuildReceiptSha256 $Fixture.Pin -ExtensionId $extensionId -HostExecutable $Fixture.Host -OutputDirectory $Fixture.Output
}
function Set-FixtureFamily($Fixture, [string] $Family) {
    if ($Family -ceq 'Main') {
        $Fixture.Data.protocol = 'direct.media.v1'
        $Fixture.Data.host_name = 'com.rcmusic.media'
        $Fixture.Data.host_images = @('rc_music_native_host.exe')
        $Fixture.Data.executable.path = (Join-Path ([IO.Path]::GetDirectoryName($Fixture.Exe)) 'rc_music_native_host.exe').Replace('\', '/')
        Save-Receipt $Fixture
    }
}
function Assert-Rejected-Data([scriptblock] $Change) {
    $f = New-Fixture
    & $Change $f.Data
    Save-Receipt $f
    Assert-Throws { New-Plan $f } 'Invalid build profile was accepted'
    Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Invalid profile created output'
    Assert-Equal (Hash $f.Main) $f.MainHash
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    foreach ($family in @('Main', 'Youtube')) {
        Test-Case "the public $family reader returns a pinned profile without requiring host or configuration bytes" {
            $f = New-Fixture
            Set-FixtureFamily $f $family
            $leaf = if ($family -ceq 'Main') { 'rc_music_native_host.exe' } else { 'rc_music_youtube_native_host.exe' }
            $f.Data.executable.path = (Join-Path $f.Root ('absent originals/' + $leaf)).Replace('\', '/')
            $f.Data.configuration.path = (Join-Path $f.Root 'absent originals/configuration.hpp').Replace('\', '/')
            Save-Receipt $f
            $before = Hash $f.Receipt
            $profile = Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family $family
            Assert-True ($null -ne $profile) 'Reader returned no profile'
            Assert-Equal $profile.protocol $f.Data.protocol
            Assert-Equal $profile.host_name $f.Data.host_name
            Assert-Equal $profile.origin $origin
            Assert-Equal $profile.endpoint $f.Data.endpoint
            Assert-Equal ($profile.owner_images -join '|') 'RCMusic.exe'
            Assert-Equal ($profile.host_images -join '|') $leaf
            Assert-Equal $profile.executable.path ([IO.Path]::GetFullPath($f.Data.executable.path))
            Assert-Equal $profile.executable.sha256 $f.Data.executable.sha256
            Assert-Equal $profile.executable.size_bytes $f.Data.executable.size_bytes
            Assert-Equal $profile.configuration.path ([IO.Path]::GetFullPath($f.Data.configuration.path))
            Assert-Equal $profile.configuration.sha256 $f.Data.configuration.sha256
            Assert-Equal (Hash $f.Receipt) $before
            Assert-Equal (Hash $f.Main) $f.MainHash
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Reader created output'
        }
    }
    Test-Case 'the public reader rejects cross-family profiles and mismatched logical host names' {
        foreach ($family in @('Main', 'Youtube')) {
            $f = New-Fixture
            Set-FixtureFamily $f $family
            $other = if ($family -ceq 'Main') { 'Youtube' } else { 'Main' }
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family $other } 'Cross-family receipt was accepted'
            $f.Data.host_name = if ($family -ceq 'Main') { 'com.rcmusic.youtube' } else { 'com.rcmusic.media' }
            Save-Receipt $f
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family $family } 'Family protocol accepted a different host name'
        }
        Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family 'Other' } 'Unknown family was accepted'
    }
    Test-Case 'the public reader preserves strict enabled schema and scalar validation for both families' {
        foreach ($family in @('Main', 'Youtube')) {
            foreach ($change in @(
                { param($d) $d.enabled = $false },
                { param($d) $d.enabled = 'true' },
                { param($d) $d.schema_version = $true },
                { param($d) $d.component = $false },
                { param($d) $d.endpoint = "endpoint`n" },
                { param($d) $d.unexpected = 1 }
            )) {
                $f = New-Fixture
                Set-FixtureFamily $f $family
                & $change $f.Data
                Save-Receipt $f
                Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family $family } 'Disabled, coerced or open-schema profile was accepted'
                Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Reader created output for a rejected profile'
            }
        }
    }
    Test-Case 'the public reader rejects duplicate fields invalid UTF8 and trailing JSON through the shared parser' {
        foreach ($kind in @('duplicate-root', 'duplicate-nested', 'utf8', 'trailing')) {
            $f = New-Fixture
            $json = [IO.File]::ReadAllText($f.Receipt)
            switch ($kind) {
                'duplicate-root' { Write-Utf8 $f.Receipt $json.Replace('"enabled":true', '"enabled":true,"enabled":true') }
                'duplicate-nested' { Write-Utf8 $f.Receipt $json.Replace('"configuration":{', '"configuration":{"sha256":"' + ('0' * 64) + '",') }
                'utf8' { [IO.File]::WriteAllBytes($f.Receipt, [byte[]]@(0x7b, 0xff, 0x7d)) }
                'trailing' { Write-Utf8 $f.Receipt ($json + '{}') }
            }
            $f.Pin = Hash $f.Receipt
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family Youtube } 'Malformed receipt bytes were accepted'
        }
    }
    Test-Case 'the public reader requires an exact pin and verifies it before parsing receipt JSON' {
        $f = New-Fixture
        foreach ($bad in @(('0' * 63), ('A' * 64), ($f.Pin + "`n"), ('0' * 64))) {
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $bad -Family Youtube } 'Invalid receipt pin was accepted'
        }
        Write-Utf8 $f.Receipt '{'
        $errorText = ''
        try { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 ('0' * 64) -Family Youtube | Out-Null }
        catch { $errorText = $_.Exception.Message }
        Assert-True ($errorText -match 'SHA256') 'Malformed JSON was parsed before checking its mismatched pin'
    }
    Test-Case 'the public reader uses a held read snapshot and releases it after returning' {
        $f = New-Fixture
        $writer = [IO.File]::Open($f.Receipt, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::ReadWrite)
        try {
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family Youtube } 'Reader shared an active writer'
        } finally { $writer.Dispose() }
        $profile = Read-RcMusicNativeHostBuildProfile -ReceiptPath $f.Receipt -ReceiptSha256 $f.Pin -Family Youtube
        Assert-True ($null -ne $profile) 'Reader did not recover after the writer closed'
        $exclusive = [IO.File]::Open($f.Receipt, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $exclusive.Dispose()
        foreach ($path in @('relative-receipt.json', ('\\?\' + $f.Receipt), [IO.Path]::GetDirectoryName($f.Receipt))) {
            Assert-Throws { Read-RcMusicNativeHostBuildProfile -ReceiptPath $path -ReceiptSha256 $f.Pin -Family Youtube } 'Reader accepted a non-ordinary receipt path'
        }
    }
    Test-Case 'planning binds a copied host and explicit extension ID to a pinned build receipt without writing' {
        $f = New-Fixture
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'Planner returned no candidate'
        Assert-Equal $plan.schema_version 1
        Assert-Equal $plan.component 'youtube-native-manifest-candidate'
        Assert-Equal $plan.installation_state 'installation_pending'
        Assert-Equal $plan.build_receipt.sha256 $f.Pin
        Assert-Equal $plan.host_executable.path $f.Host
        Assert-Equal $plan.host_executable.sha256 (Hash $f.Host)
        Assert-Equal $plan.native_manifest.name 'com.rcmusic.youtube'
        Assert-Equal $plan.native_manifest.type 'stdio'
        Assert-Equal $plan.native_manifest.path $f.Host
        Assert-Equal $plan.native_manifest.allowed_origins.Count 1
        Assert-Equal $plan.native_manifest.allowed_origins[0] $origin
        Assert-Equal $plan.endpoint 'rc-music-youtube-fixture'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Planning wrote an output'
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
    Test-Case 'materialization writes only the five-field Chrome candidate and an installation-pending receipt' {
        $f = New-Fixture
        $before = Hash $f.Host
        $result = Copy-RcMusicYoutubeNativeManifestPlan -Plan (New-Plan $f)
        Assert-True ($null -ne $result) 'Materializer returned no candidate'
        Assert-Equal $result.package_directory $f.Output
        Assert-Equal $result.manifest_path (Join-Path $f.Output 'com.rcmusic.youtube.json')
        Assert-Equal $result.receipt_path (Join-Path $f.Output 'native-manifest-receipt.json')
        $manifest = Get-Content -LiteralPath $result.manifest_path -Raw | ConvertFrom-Json
        Assert-Equal (@($manifest.PSObject.Properties.Name | Sort-Object) -join '|') 'allowed_origins|description|name|path|type'
        Assert-Equal $manifest.name 'com.rcmusic.youtube'
        Assert-True (-not [string]::IsNullOrWhiteSpace($manifest.description)) 'Description was empty'
        Assert-Equal $manifest.type 'stdio'
        Assert-Equal $manifest.path $f.Host
        Assert-Equal $manifest.allowed_origins.Count 1
        Assert-Equal $manifest.allowed_origins[0] $origin
        $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json
        Assert-Equal $receipt.installation_state 'installation_pending'
        Assert-Equal $receipt.build_receipt.sha256 $f.Pin
        Assert-Equal $receipt.host_executable.sha256 $before
        Assert-Equal $receipt.manifest.sha256 (Hash $result.manifest_path)
        Assert-Equal (@(Get-ChildItem -LiteralPath $f.Output -File -Recurse | Sort-Object Name | ForEach-Object Name) -join '|') `
            'com.rcmusic.youtube.json|native-manifest-receipt.json'
        Assert-Equal (Hash $f.Host) $before 'Host was changed'
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
    Test-Case 'an identical copied host can outlive its original build file and configuration path' {
        $f = New-Fixture
        Remove-Item -LiteralPath $f.Exe, $f.Config
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'A retained receipt could not bind the identical copied host'
        Assert-True (-not $plan.configuration_present) 'Missing original configuration was claimed present'
        Assert-Equal $plan.host_executable.sha256 $f.Data.executable.sha256
    }
    Test-Case 'explicit IDs and origin agreement are mandatory' {
        foreach ($bad in @(('a' * 31), ('a' * 33), ('A' * 32), ('q' * 32), ('*' * 32), 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb')) {
            $f = New-Fixture
            Assert-Throws {
                New-RcMusicYoutubeNativeManifestPlan -RepositoryRoot $f.Root -BuildReceipt $f.Receipt -BuildReceiptSha256 $f.Pin `
                    -ExtensionId $bad -HostExecutable $f.Host -OutputDirectory $f.Output
            } 'Invalid or mismatched extension ID was accepted'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Invalid ID created output'
        }
    }
    Test-Case 'receipt pins are exact lowercase SHA256 values and bind the bytes before parsing' {
        foreach ($bad in @(('0' * 63), ('A' * 64), ('0' * 64))) {
            $f = New-Fixture
            $f.Pin = $bad
            Assert-Throws { New-Plan $f } 'Invalid or mismatched receipt pin was accepted'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Invalid pin created output'
        }
    }
    Test-Case 'disabled principal wildcard and malformed profiles cannot produce a YouTube manifest' {
        foreach ($change in @(
            { param($d) $d.enabled = $false; $d.origin = ''; $d.endpoint = '' },
            { param($d) $d.protocol = 'direct.media.v1'; $d.host_name = 'com.rcmusic.media' },
            { param($d) $d.host_name = 'com.rcmusic.media' },
            { param($d) $d.origin = 'chrome-extension://*/' },
            { param($d) $d.endpoint = '../pipe' },
            { param($d) $d.endpoint = 'a' * 97 },
            { param($d) $d.endpoint = "rc-music-youtube-fixture`n" },
            { param($d) $d.endpoint = "rc-music-youtube-fixture`r`n" },
            { param($d) $d.origin = '' }
        )) { Assert-Rejected-Data $change }
    }
    Test-Case 'closed receipt fields and scalar types are checked rather than coerced' {
        foreach ($change in @(
            { param($d) $d.schema_version = '1' },
            { param($d) $d.schema_version = 2 },
            { param($d) $d.enabled = 'true' },
            { param($d) $d['extra'] = $true },
            { param($d) $d.Remove('configuration') },
            { param($d) $d.executable['extra'] = 1 },
            { param($d) $d.executable.size_bytes = '51' },
            { param($d) $d.executable.size_bytes = 0 },
            { param($d) $d.executable.sha256 = 'A' * 64 },
            { param($d) $d.configuration.path = 'relative.hpp' },
            { param($d) $d.configuration.sha256 = 'x' * 64 }
        )) { Assert-Rejected-Data $change }
    }
    Test-Case 'peer image lists use bounded unique executable basenames' {
        foreach ($change in @(
            { param($d) $d.owner_images = 'RCMusic.exe' },
            { param($d) $d.owner_images = @() },
            { param($d) $d.owner_images = @('RCMusic.exe', 'rcmusic.EXE') },
            { param($d) $d.host_images = @('one.exe', 'two.exe', 'three.exe', 'four.exe', 'five.exe') },
            { param($d) $d.owner_images = @('../RCMusic.exe') },
            { param($d) $d.owner_images = @('RC Music.exe') },
            { param($d) $d.owner_images = @("RCMusic.exe`n") },
            { param($d) $d.owner_images = @("RCMusic.exe`r`n") },
            { param($d) $d.host_images = @('rc_music_native_host.exe') }
        )) { Assert-Rejected-Data $change }
    }
    Test-Case 'Windows device paths and reserved peer names are rejected without opening a device' {
        foreach ($change in @(
            { param($d) $d.owner_images = @('NUL.exe') },
            { param($d) $d.host_images = @('COM1.exe') },
            { param($d) $d.executable.path = 'C:/NUL.exe' },
            { param($d) $d.executable.path = '//server/share/host.exe' },
            { param($d) $d.configuration.path = '//?/C:/fixture/config.hpp' },
            { param($d) $d.configuration.path = 'C:/fixture/CON/config.hpp' }
        )) { Assert-Rejected-Data $change }
        foreach ($bad in @('\\.\NUL', 'C:\fixture\AUX.exe', '\\server\share\host.exe')) {
            $f = New-Fixture
            $f.Host = $bad
            Assert-Throws { New-Plan $f } 'Device or UNC host path was accepted'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected device input created output'
        }
    }
    Test-Case 'strict UTF8 JSON rejects duplicates trailing data excessive depth and oversized inputs' {
        foreach ($kind in @('duplicate-root', 'duplicate-nested', 'fractional-schema', 'trailing', 'depth', 'oversize', 'utf8')) {
            $f = New-Fixture
            $raw = Get-Content -LiteralPath $f.Receipt -Raw
            switch ($kind) {
                'duplicate-root' { $raw = $raw.Replace('"schema_version":1', '"schema_version":1,"schema_version":1') }
                'duplicate-nested' { $raw = $raw.Replace('"executable":{', '"executable":{"path":"duplicate",') }
                'fractional-schema' { $raw = $raw.Replace('"schema_version":1', '"schema_version":1.0') }
                'trailing' { $raw += '{}' }
                'depth' { $raw = ('[' * 10) + '0' + (']' * 10) }
                'oversize' { $raw += ' ' * 65536 }
            }
            if ($kind -eq 'utf8') { [IO.File]::WriteAllBytes($f.Receipt, [byte[]](0x7B, 0xFF, 0x7D)) }
            else { Write-Utf8 $f.Receipt $raw }
            $f.Pin = Hash $f.Receipt
            Assert-Throws { New-Plan $f } 'Malformed receipt JSON was accepted'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Malformed JSON created output'
        }
    }
    Test-Case 'host basename size and bytes and any existing configuration must match the receipt' {
        foreach ($kind in @('basename', 'bytes', 'size', 'configuration', 'missing-host', 'missing-receipt')) {
            $f = New-Fixture
            switch ($kind) {
                'basename' {
                    $renamed = Join-Path ([IO.Path]::GetDirectoryName($f.Host)) 'different.exe'
                    Copy-Item -LiteralPath $f.Host -Destination $renamed
                    $f.Host = $renamed
                }
                'bytes' {
                    $bytes = [IO.File]::ReadAllBytes($f.Host)
                    $bytes[0] = $bytes[0] -bxor 1
                    [IO.File]::WriteAllBytes($f.Host, $bytes)
                }
                'size' { $f.Data.executable.size_bytes++; Save-Receipt $f }
                'configuration' { [IO.File]::AppendAllText($f.Config, ' changed') }
                'missing-host' { Remove-Item -LiteralPath $f.Host }
                'missing-receipt' { Remove-Item -LiteralPath $f.Receipt }
            }
            Assert-Throws { New-Plan $f } 'Unbound host/configuration was accepted'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Unbound input created output'
        }
    }
    Test-Case 'stale receipt host and configuration snapshots fail before output creation' {
        foreach ($kind in @('receipt', 'host', 'configuration')) {
            $f = New-Fixture
            $plan = New-Plan $f
            Assert-True ($null -ne $plan) 'No plan'
            switch ($kind) {
                'receipt' { [IO.File]::AppendAllText($f.Receipt, ' ') }
                'host' { [IO.File]::AppendAllText($f.Host, ' changed') }
                'configuration' { [IO.File]::AppendAllText($f.Config, ' changed') }
            }
            Assert-Throws { Copy-RcMusicYoutubeNativeManifestPlan -Plan $plan } 'A stale source snapshot was materialized'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Drift was discovered after output creation'
        }
    }
    Test-Case 'existing outputs unsafe destinations and tampered plans preserve prior files' {
        $f = New-Fixture
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        $null = New-Item -ItemType Directory -Path $f.Output
        $sentinel = Join-Path $f.Output 'preserve.keep'
        Write-Utf8 $sentinel 'Existing candidate remains intact.'
        $before = Hash $sentinel
        Assert-Throws { New-Plan $f } 'Existing output was accepted'
        Assert-Throws { Copy-RcMusicYoutubeNativeManifestPlan -Plan $plan } 'Existing output was overwritten'
        Assert-Equal (Hash $sentinel) $before
        $g = New-Fixture
        foreach ($bad in @($g.Root, (Join-Path $g.Root 'principal/candidate'), 'relative-candidate')) {
            Assert-Throws {
                New-RcMusicYoutubeNativeManifestPlan -RepositoryRoot $g.Root -BuildReceipt $g.Receipt -BuildReceiptSha256 $g.Pin `
                    -ExtensionId $extensionId -HostExecutable $g.Host -OutputDirectory $bad
            } 'Unsafe output was accepted'
        }
        $plan = New-Plan $g
        Assert-True ($null -ne $plan) 'No plan'
        $plan.native_manifest.allowed_origins = @('chrome-extension://*/')
        Assert-Throws { Copy-RcMusicYoutubeNativeManifestPlan -Plan $plan } 'Forged native manifest was materialized'
        Assert-True (-not (Test-Path -LiteralPath $g.Output)) 'Forged plan created output'
        Assert-Equal (Hash $g.Main) $g.MainHash
    }
    Test-Case 'the CLI resolves its fresh output from its own repository without executing the synthetic host' {
        $f = New-Fixture
        $before = Hash $f.Host
        Push-Location $testRoot
        try {
            $result = & $f.Entry -BuildReceipt $f.Receipt -BuildReceiptSha256 $f.Pin -ExtensionId $extensionId `
                -HostExecutable $f.Host -OutputDirectory 'out/product/cli-manifest'
        } finally { Pop-Location }
        Assert-True ($null -ne $result) 'CLI returned no candidate'
        Assert-Equal $result.package_directory (Join-Path $f.Root 'out/product/cli-manifest')
        Assert-Equal (Hash $f.Host) $before
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
} finally {
    Remove-Module RcMusicYoutubeNativeManifest -Force -ErrorAction SilentlyContinue
    if ($state.Failed -eq 0) {
        Assert-SafeTestRoot
        if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    } else { Write-Host "[INFO] Preserved owned failed fixtures: $testRoot" }
}
Write-Host "YouTube native manifest candidate: $($state.Passed) passed / $($state.Failed) failed"
if ($state.Failed -gt 0) { exit 1 }
