#Requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $repository 'packaging/windows/RcMusicExtensionComponent.psm1') -Force
$testParent = Join-Path $repository 'out/product/test-runs/extension-component'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
# Public-only fixed inputs, with independently pinned expected identities.
$mainPublic = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqX9/7jyIe6knu4ZGOK+J3L7DrO16UrFM7AjYtt3tBD7ROepgbUEnrkg1RFLKiUcuAZIIF3qfvLdwZiY3ZebHReloePi/ZhmwKNoey4JeBnlZpZhrFdzI+WBFWTr92Ams1x6LwU6ugdEwfr5uZxUX/hEYUqqcdblF06YntRwu8rPr8tmsGl9s+rZw6L5arTz3Mn1Pw4Y1Wt4q+NRlWYso14doQZ8s/OHuJFRY7kc8MhpIN05ayPlcv2VQIe+xWdm67nR8nWlbsr08c0bQNrgAqCmmnCrWUPW3IXvbcb+4rnL4xiyYnXnxLRPJMpz3fNkpIu7aziR1869t9Hiz2hJ5cQIDAQAB'
$youtubePublic = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsJmJ6eCY4G5chs/I8sD1xnEmue2mffsMr7Ya5gymCGYNpufwaECbeRMh3cAz0xjy7VB0+m25bVWrWAll9xJdPqPahx7aJ0on4T2666fSrzrhGm2Rq5RxVUFcHAmV+GZF1W+1evt1TO3EuNT8WW9+74Ysph6cbo2nm7JEwurhsZ4uBb8vtEQyg1AvE/osIL6qUEW0SRwXabhVL1H911jDHM+tocePeX4BsFfoZMRTaAO94MlWFPniUskPmSQ0o80q+9HP9h5ovZidDfEkB2Ifq+sMFKPJOhJUAHPZhD7HmTFymwfJlQg6XOIQNYUOe6Xm1Vl7gdRfjl4W9XM08yu5tQIDAQAB'
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) { if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." } }
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Rejected([scriptblock] $Body) {
    try { & $Body | Out-Null } catch { return }
    throw 'Expected rejection.'
}
function Json($Value) { return ConvertTo-Json -InputObject $Value -Depth 20 -Compress }
function Write-Utf8([string] $Path, [string] $Text) { [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-OwnedRoot {
    if ($testId -cnotmatch '\A[0-9a-f]{32}\z' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) { throw 'Unsafe owned fixture root.' }
    $current = $testRoot
    while ($current) {
        try { $attributes = [IO.File]::GetAttributes($current) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe fixture ancestor.' }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Save-Receipt($Fixture) { Write-Utf8 $Fixture.Receipt (Json $Fixture.Data); $Fixture.Pin = Hash $Fixture.Receipt }
function Refresh-Asset($Fixture, [string] $Name) {
    $row = @($Fixture.Data.files | Where-Object relative_path -CEQ ('extension/' + $Name))[0]
    $path = Join-Path $Fixture.Extension $Name
    $row.size_bytes = [IO.FileInfo]::new($path).Length; $row.sha256 = Hash $path
    Save-Receipt $Fixture
}
function New-Fixture([ValidateSet('Main', 'Youtube')][string] $Channel = 'Main') {
    $state.Fixtures++
    $root = Join-Path $testRoot ('fixture 日本 ' + $state.Fixtures)
    $package = Join-Path $root 'exported package'
    $extension = Join-Path $package 'extension'
    $null = [IO.Directory]::CreateDirectory($extension)
    $names = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs', 'popup.css', 'popup.html', 'popup.mjs')
    if ($Channel -ceq 'Main') { $names += @('detector.js', 'media-collector.js') }
    else { $names += 'page-video.mjs' }
    $isMain = $Channel -ceq 'Main'
    $key = if ($isMain) { $mainPublic } else { $youtubePublic }
    $id = if ($isMain) { 'kggjfjbjhklkbpelddffdjelbiomoolc' } else { 'lhcdmfamadilnahlckpalijljfohjgkc' }
    $keyHash = if ($isMain) { 'a66959197aba1f4b3355394b18eceeb25dd21767a74a5d2fd6e8dc8a711dd953' } else { 'b723c50c038bd07b2af0b89b95e796a22a30d1fc00d790f98b218ae24a676724' }
    $hostName = if ($isMain) { 'com.rcmusic.media' } else { 'com.rcmusic.youtube' }
    $protocol = if ($isMain) { 'direct.media.v1' } else { 'youtube.audio.v1' }
    $assets = @(foreach ($name in $names) {
        $path = Join-Path $extension $name
        if ($name -ceq 'manifest.json') { Write-Utf8 $path (Json ([ordered]@{ manifest_version = 3; name = 'Owned fixture 日本'; version = '1.0.0'; key = $key })) }
        else { Write-Utf8 $path "// Owned synthetic $Channel asset $name`n" }
        [pscustomobject]@{ relative_path = 'extension/' + $name; size_bytes = [IO.FileInfo]::new($path).Length; sha256 = Hash $path }
    })
    $recipes = @('packaging/windows/New-RcMusicChromiumExtensionPackage.ps1', 'packaging/windows/RcMusicYoutubeExtensionPackage.psm1', 'packaging/windows/RcMusicChromiumIdentity.psm1')
    if (-not $isMain) { $recipes = @('packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1') + $recipes }
    $data = [pscustomobject]@{
        schema_version = 1; component = if ($isMain) { 'main-extension-source' } else { 'youtube-extension-source' }
        channel = $Channel; extension_identity = [pscustomobject]@{ extension_id = $id; origin = 'chrome-extension://' + $id + '/'; public_key_sha256 = $keyHash }
        identity_state = 'public_key_bound'; native_host_name = $hostName; native_manifest = $null
        source_commit = '1' * 40; extension_dirty = $false; recipe_dirty = $false; source_dirty = $false
        source_inputs = @('extensions/chromium/' + $Channel.ToLowerInvariant()) + $recipes
        recipe_files = @($recipes | ForEach-Object { [pscustomobject]@{ path = $_; size_bytes = 17; sha256 = 'e' * 64 } })
        files = $assets
    }
    $fixture = [pscustomobject]@{
        Root = $root; Package = $package; Extension = $extension; Names = $names; Channel = $Channel
        Receipt = Join-Path $package 'package-receipt.json'; Pin = ''; Data = $data
        Native = [pscustomobject]@{ protocol = $protocol; host_name = $hostName; origin = $data.extension_identity.origin
            endpoint = 'rc-music-owned-fixture'; receipt_sha256 = 'a' * 64; configuration_sha256 = 'b' * 64
            executable = if ($isMain) { 'rc_music_native_host.exe' } else { 'rc_music_youtube_native_host.exe' }
            manifest = $hostName + '.json' }
    }
    Save-Receipt $fixture
    return $fixture
}
function Plan($Fixture, [hashtable] $Overrides = @{}) {
    $arguments = @{ ReceiptPath = $Fixture.Receipt; ReceiptSha256 = $Fixture.Pin; Channel = $Fixture.Channel; NativeChannel = $Fixture.Native }
    foreach ($key in $Overrides.Keys) { $arguments[$key] = $Overrides[$key] }
    return Get-RcMusicExtensionComponentPlan @arguments
}
function Snapshot($Fixture) {
    return Json @([IO.Directory]::EnumerateFiles($Fixture.Package, '*', [IO.SearchOption]::AllDirectories) |
        Sort-Object -CaseSensitive | ForEach-Object { [ordered]@{ path = $_; sha256 = Hash $_ } })
}
Assert-OwnedRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Fixture root must be fresh.' }
$null = [IO.Directory]::CreateDirectory($testRoot)
try {
    Test-Case 'both clean fixed families map only their assets to separate browser-extension paths' {
        foreach ($channel in @('Main', 'Youtube')) {
            $f = New-Fixture $channel; $p = Plan $f
            Assert-True ($null -ne $p) 'No component plan.'
            Assert-Equal $p.channel $channel
            Assert-Equal $p.extension_id $f.Data.extension_identity.extension_id
            Assert-Equal $p.origin $f.Native.origin
            Assert-Equal $p.native_host_name $f.Native.host_name
            Assert-Equal $p.receipt_path $f.Receipt
            Assert-Equal $p.receipt_sha256 $f.Pin
            Assert-Equal $p.package_root $f.Package
            Assert-Equal @($p.files).Count $f.Names.Count
            foreach ($name in $f.Names) {
                $row = @($p.files | Where-Object relative_path -CEQ ('browser-extensions/' + $channel.ToLowerInvariant() + '/' + $name))
                Assert-Equal $row.Count 1
                Assert-Equal $row[0].source (Join-Path $f.Extension $name)
                Assert-Equal $row[0].sha256 (Hash $row[0].source)
            }
            Assert-Equal (Json $p) (Json (Plan $f))
        }
    }
    Test-Case 'YouTube collector bytes and receipt membership cannot drift independently' {
        foreach ($variant in @('receipt-missing', 'file-missing', 'same-length')) {
            $f = New-Fixture Youtube
            Assert-True ($null -ne (Plan $f)) 'Valid YouTube collector package was refused.'
            $path = Join-Path $f.Extension 'page-video.mjs'
            switch ($variant) {
                receipt-missing { $f.Data.files = @($f.Data.files | Where-Object relative_path -CNE 'extension/page-video.mjs'); Save-Receipt $f }
                file-missing { Remove-Item -LiteralPath $path }
                same-length { $bytes = [IO.File]::ReadAllBytes($path); $bytes[0] = 88; [IO.File]::WriteAllBytes($path, $bytes) }
            }
            Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'planning preserves sources native metadata and closes every input stream' {
        $f = New-Fixture; $before = Snapshot $f; $native = Json $f.Native
        Assert-True ($null -ne (Plan $f)) 'No component plan.'
        Assert-Equal (Snapshot $f) $before
        Assert-Equal (Json $f.Native) $native
        foreach ($path in [IO.Directory]::EnumerateFiles($f.Package, '*', [IO.SearchOption]::AllDirectories)) {
            $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None); $stream.Dispose()
        }
    }
    Test-Case 'receipt pins bind bytes before parsing and never refresh after drift' {
        foreach ($pin in @(('0' * 64), ('A' * 64), ('a' * 63), (('a' * 64) + "`n"))) {
            $f = New-Fixture; Assert-Rejected { Plan $f @{ ReceiptSha256 = $pin } }
        }
        $f = New-Fixture; [IO.File]::AppendAllText($f.Receipt, "`n"); Assert-Rejected { Plan $f }
    }
    Test-Case 'receipt fields and dirty flags must be closed exact schema and clean booleans' {
        foreach ($field in @('schema_version', 'component', 'channel', 'extension_identity', 'identity_state', 'native_host_name', 'native_manifest', 'source_commit', 'extension_dirty', 'recipe_dirty', 'source_dirty', 'source_inputs', 'recipe_files', 'files')) {
            $f = New-Fixture; $f.Data.PSObject.Properties.Remove($field); Save-Receipt $f; Assert-Rejected { Plan $f }
        }
        foreach ($field in @('extension_dirty', 'recipe_dirty', 'source_dirty')) {
            foreach ($value in @($true, 'false', 0)) { $f = New-Fixture; $f.Data.$field = $value; Save-Receipt $f; Assert-Rejected { Plan $f } }
        }
        $f = New-Fixture; $f.Data | Add-Member -NotePropertyName unknown -NotePropertyValue 1; Save-Receipt $f; Assert-Rejected { Plan $f }
        $f = New-Fixture; $f.Data.schema_version = '1'; Save-Receipt $f; Assert-Rejected { Plan $f }
        $f = New-Fixture; $f.Data.native_manifest = @{}; Save-Receipt $f; Assert-Rejected { Plan $f }
    }
    Test-Case 'receipt JSON rejects duplicates malformed UTF8 excessive size depth and trailing syntax' {
        foreach ($variant in @('duplicate', 'nested-duplicate', 'utf8', 'large', 'depth', 'trailing', 'comment')) {
            $f = New-Fixture
            switch ($variant) {
                duplicate { Write-Utf8 $f.Receipt ((Json $f.Data).Replace('"schema_version":1', '"schema_version":1,"schema_version":1')) }
                nested-duplicate { Write-Utf8 $f.Receipt ((Json $f.Data).Replace('"public_key_sha256":', '"extension_id":"x","public_key_sha256":')) }
                utf8 { [IO.File]::WriteAllBytes($f.Receipt, [byte[]]@(0xc0, 0xaf)) }
                large { Write-Utf8 $f.Receipt (' ' * 65537) }
                depth { Write-Utf8 $f.Receipt (('[' * 17) + '0' + (']' * 17)) }
                trailing { Write-Utf8 $f.Receipt ((Json $f.Data) + '{}') }
                comment { Write-Utf8 $f.Receipt ('/* unsupported */' + (Json $f.Data)) }
            }
            $f.Pin = Hash $f.Receipt; Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'the package family and Native origin host name and protocol must agree' {
        foreach ($field in @('protocol', 'host_name', 'origin')) { $f = New-Fixture; $f.Native.$field = 'different'; Assert-Rejected { Plan $f } }
        $f = New-Fixture; Assert-Rejected { Plan $f @{ Channel = 'Youtube' } }
        $f = New-Fixture; $f.Data.component = 'youtube-extension-source'; Save-Receipt $f; Assert-Rejected { Plan $f }
        $f = New-Fixture; $f.Data.native_host_name = 'com.rcmusic.youtube'; Save-Receipt $f; Assert-Rejected { Plan $f }
    }
    Test-Case 'the manifest-derived identity must match every claimed public identity field' {
        foreach ($field in @('extension_id', 'origin', 'public_key_sha256')) {
            $f = New-Fixture; $f.Data.extension_identity.$field = if ($field -ceq 'public_key_sha256') { '0' * 64 } else { 'wrong' }
            Save-Receipt $f; Assert-Rejected { Plan $f }
        }
        $f = New-Fixture; $f.Data.identity_state = 'identity_pending'; Save-Receipt $f; Assert-Rejected { Plan $f }
        $f = New-Fixture
        Write-Utf8 (Join-Path $f.Extension 'manifest.json') (Json @{ manifest_version = 3; key = $youtubePublic })
        Refresh-Asset $f 'manifest.json'; Assert-Rejected { Plan $f }
    }
    Test-Case 'keyless malformed and duplicated-key manifests cannot become installable components' {
        foreach ($text in @('{"manifest_version":3}', '{"key":false}', '{"key":"invalid"}', ('{"key":"' + $mainPublic + '","key":"' + $mainPublic + '"}'))) {
            $f = New-Fixture; Write-Utf8 (Join-Path $f.Extension 'manifest.json') $text; Refresh-Asset $f 'manifest.json'
            Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'provenance is validated but its original recipe paths are never opened' {
        $f = New-Fixture; Assert-True ($null -ne (Plan $f)) 'Valid inert provenance required unavailable source files.'
        foreach ($variant in @('commit', 'recipe-hash', 'recipe-size', 'recipe-field', 'recipe-path', 'source-path', 'duplicate-source')) {
            $f = New-Fixture
            switch ($variant) {
                commit { $f.Data.source_commit = ('a' * 40) + "`n" }
                recipe-hash { $f.Data.recipe_files[0].sha256 = 'A' * 64 }
                recipe-size { $f.Data.recipe_files[0].size_bytes = '17' }
                recipe-field { $f.Data.recipe_files[0] | Add-Member -NotePropertyName unknown -NotePropertyValue 1 }
                recipe-path { $f.Data.recipe_files[0].path = '../outside.ps1' }
                source-path { $f.Data.source_inputs[0] = 'C:/original/private/source' }
                duplicate-source { $f.Data.source_inputs += $f.Data.source_inputs[0] }
            }
            Save-Receipt $f; Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'asset inventory is the fixed whitelist without aliases private files or changed descriptors' {
        foreach ($variant in @('missing', 'extra', 'alias', 'private', 'path', 'size', 'hash', 'field')) {
            $f = New-Fixture
            switch ($variant) {
                missing { $f.Data.files = @($f.Data.files | Select-Object -Skip 1) }
                extra { $f.Data.files += $f.Data.files[0] }
                alias { $f.Data.files[1].relative_path = 'EXTENSION/MANIFEST.JSON' }
                private { $f.Data.files[1].relative_path = 'extension/key.pem' }
                path { $f.Data.files[1].relative_path = '../native-client.mjs' }
                size { $f.Data.files[1].size_bytes = 0 }
                hash { $f.Data.files[1].sha256 = 'a' * 64 }
                field { $f.Data.files[0] | Add-Member -NotePropertyName source -NotePropertyValue 'external' }
            }
            Save-Receipt $f; Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'the physical package tree is closed and extra files and directories are preserved' {
        foreach ($variant in @('root-file', 'asset-file', 'empty-directory')) {
            $f = New-Fixture
            $extra = switch ($variant) {
                root-file { Join-Path $f.Package 'unknown.txt' }
                asset-file { Join-Path $f.Extension 'private-never-copy.pem' }
                empty-directory { Join-Path $f.Extension 'unknown-directory' }
            }
            if ($variant -ceq 'empty-directory') { $null = [IO.Directory]::CreateDirectory($extra) } else { Write-Utf8 $extra 'Preserve this synthetic extra.' }
            Assert-Rejected { Plan $f }
            Assert-True (Test-Path -LiteralPath $extra) 'Rejected input was removed.'
        }
    }
    Test-Case 'missing or changed asset bytes cannot silently refresh a previously pinned package' {
        foreach ($variant in @('missing', 'length', 'same-length')) {
            $f = New-Fixture; $path = Join-Path $f.Extension 'popup.mjs'
            switch ($variant) {
                missing { Remove-Item -LiteralPath $path }
                length { [IO.File]::AppendAllText($path, 'drift') }
                same-length { $bytes = [IO.File]::ReadAllBytes($path); $bytes[0] = 88; [IO.File]::WriteAllBytes($path, $bytes) }
            }
            Assert-Rejected { Plan $f }
        }
    }
    Test-Case 'receipt basename and local paths are fixed before any payload is accepted' {
        $f = New-Fixture; $renamed = Join-Path $f.Package 'renamed.json'
        Copy-Item -LiteralPath $f.Receipt -Destination $renamed
        Assert-Rejected { Plan $f @{ ReceiptPath = $renamed } }
        foreach ($path in @('C:/NUL/package-receipt.json', '\\?\C:\package-receipt.json', '\\server\share\package-receipt.json', 'relative/package-receipt.json')) {
            Assert-Rejected { Plan $f @{ ReceiptPath = $path } }
        }
    }
    Test-Case 'an owned package ancestor junction is rejected without changing its real target' {
        $f = New-Fixture; $before = Snapshot $f
        $alias = Join-Path $f.Root 'owned-package-alias'
        $null = New-Item -ItemType Junction -Path $alias -Target $f.Package
        Assert-Rejected { Plan $f @{ ReceiptPath = Join-Path $alias 'package-receipt.json' } }
        Assert-Equal (Snapshot $f) $before
        Assert-True (Test-Path -LiteralPath (Join-Path $f.Package 'extension/manifest.json')) 'Junction target was changed.'
    }
} finally { Write-Host "Passed: $($state.Passed); Failed: $($state.Failed); Fixtures: $testRoot" }
if ($state.Failed -gt 0) { exit 1 }
# Keep these small owned fixtures as evidence; this test never deletes packages.
