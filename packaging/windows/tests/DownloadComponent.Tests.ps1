#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot '../RcMusicDownloadComponent.psm1') -Force
$testParent = Join-Path $repository 'out/product/test-runs/download-component'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
$network = [ordered]@{
    Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
    Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'; Msvcp140_1 = 'msvcp140_1.dll'
    Vcruntime140 = 'vcruntime140.dll'; Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
}
$youtubeRoles = [ordered]@{
    Extractor = 'extractor/yt-dlp.exe'; Node = 'javascript/node.exe'
    EjsCore = 'extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js'
    EjsLibrary = 'extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js'
}
$notices = @('python-LICENSE.txt', 'node-LICENSE.txt', 'yt-dlp-LICENSE.txt', 'ejs-LICENSE.txt', 'pyinstaller-COPYING.txt')

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected, [string] $Message = 'Values differ') {
    if ($Actual -cne $Expected) { throw $Message }
}
function Assert-Throws([scriptblock] $Body, [string] $Message) {
    $thrown = $false
    try { & $Body | Out-Null } catch { $thrown = $true }
    if (-not $thrown) { throw $Message }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Utf8([string] $Path, [string] $Text) {
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Assert-SafeTestRoot {
    if ($testId -cnotmatch '^[0-9a-f]{32}\z' -or
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
function Snapshot([string] $Root) {
    $rows = @(Get-ChildItem -LiteralPath $Root -Recurse -Force | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/')
        if ($_.PSIsContainer) { 'directory|' + $relative }
        else { 'file|' + $relative + '|' + $_.Length + '|' + (Hash $_.FullName) }
    } | Sort-Object -CaseSensitive)
    return $rows -join "`n"
}
function Save-Receipt($Fixture) {
    Write-Utf8 $Fixture.Receipt (ConvertTo-Json -InputObject $Fixture.Data -Depth 10 -Compress)
    $Fixture.Pin = Hash $Fixture.Receipt
}
function Add-Asset([string] $Root, [string] $Component, [string] $Role, [string] $Relative) {
    $path = Join-Path $Root $Relative
    Write-Utf8 $path ('Owned non-PE fixture ' + $Relative)
    return [ordered]@{ component = $Component; role = $Role; relative_path = $Relative
        bytes = [IO.FileInfo]::new($path).Length; sha256 = (Hash $path) }
}
function New-Fixture([switch] $Youtube, [switch] $MissingMedia) {
    $state.Fixtures++
    $root = Join-Path $testRoot ('component espaço 音 ' + $state.Fixtures)
    $null = New-Item -ItemType Directory -Path $root
    $files = @()
    foreach ($entry in $network.GetEnumerator()) {
        $files += Add-Asset $root 'network' $entry.Key ('payload/download-tools/network/' + $entry.Value)
    }
    $media = Add-Asset $root 'media' 'Media' 'payload/download-tools/media/RCMusicMediaWorker.exe'
    if ($MissingMedia) {
        # Only this owned fixture is made incomplete; no real tool is involved.
        Remove-Item -LiteralPath (Join-Path $root $media.relative_path)
    }
    $files += $media
    if ($Youtube) {
        foreach ($entry in $youtubeRoles.GetEnumerator()) {
            $files += Add-Asset $root 'youtube' $entry.Key ('payload/download-tools/youtube/' + $entry.Value)
        }
        foreach ($name in $notices) {
            $files += Add-Asset $root 'youtube' 'Support' ('payload/download-tools/youtube/notices/' + $name)
        }
        $files += Add-Asset $root 'youtube' 'Support' 'payload/download-tools/youtube/extractor/_internal/python313.dll'
    }
    $header = Join-Path $root 'PlayerDownloadPackageProfile.hpp'
    Write-Utf8 $header '#pragma once // Owned synthetic header, never compiled by this test.'
    $data = [ordered]@{
        schemaVersion = 1; purpose = 'player-download-package-profile-v1'; state = 'candidate'; files = $files
        header = [ordered]@{ relative_path = 'PlayerDownloadPackageProfile.hpp'; bytes = [IO.FileInfo]::new($header).Length; sha256 = (Hash $header) }
        recipe_files = @(
            [ordered]@{ path = 'Z:/historical/unavailable/tools/download-runtime/build_profile.py'; bytes = 100; sha256 = ('0' * 64) },
            [ordered]@{ path = 'Z:/historical/unavailable/tools/youtube-resolver/onedir_plan.py'; bytes = 100; sha256 = ('1' * 64) })
        youtube_receipt = $(if ($Youtube) { [ordered]@{ path = 'Z:/historical/unavailable/prod-source-auth.json'; bytes = 100; sha256 = ('2' * 64) } } else { $null })
    }
    $fixture = [pscustomobject]@{ Root = $root; Receipt = (Join-Path $root 'download-profile-receipt.json')
        Header = $header; Data = $data; Pin = '' }
    Save-Receipt $fixture
    return $fixture
}
function Get-Plan($Fixture) {
    return Get-RcMusicDownloadComponentPlan -ReceiptPath $Fixture.Receipt -ReceiptSha256 $Fixture.Pin
}
function Reject-ChangedReceipt([scriptblock] $Change, [switch] $Youtube) {
    $f = New-Fixture -Youtube:$Youtube
    & $Change $f.Data
    Save-Receipt $f
    $before = Snapshot $f.Root
    Assert-Throws { Get-Plan $f } 'Invalid component receipt was accepted'
    Assert-Equal (Snapshot $f.Root) $before 'Rejected component source changed'
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Owned fixture root must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    Test-Case 'a pinned direct component yields ten runtime mappings without changing sources or creating a marker' {
        $f = New-Fixture
        $before = Snapshot $f.Root
        $plan = Get-Plan $f
        Assert-True ($null -ne $plan) 'Validator returned no component plan'
        Assert-Equal $plan.package_root $f.Root
        Assert-Equal $plan.receipt_path $f.Receipt
        Assert-Equal $plan.receipt_sha256 $f.Pin
        Assert-Equal $plan.header_path $f.Header
        Assert-Equal $plan.header_sha256 $f.Data.header.sha256
        Assert-Equal $plan.files.Count 10
        foreach ($entry in $plan.files) {
            Assert-True ($entry.relative_path.StartsWith('download-tools/', [StringComparison]::Ordinal)) 'Runtime mapping kept a payload prefix'
            Assert-Equal $entry.sha256 (Hash $entry.source)
            Assert-Equal $entry.size_bytes ([IO.FileInfo]::new($entry.source).Length)
        }
        Assert-Equal $plan.metadata.schema_version 1
        Assert-True ($plan.metadata.selection_default -is [bool] -and -not $plan.metadata.selection_default) 'Component was selected by default'
        Assert-Equal $plan.metadata.receipt_sha256 $f.Pin
        Assert-Equal $plan.metadata.header_sha256 $f.Data.header.sha256
        Assert-Equal $plan.metadata.activation_marker.relative_path 'download-tools/installed-v1'
        Assert-Equal $plan.metadata.activation_marker.content "RCMusicDownloadTools-v1`n"
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $f.Root 'payload/download-tools/installed-v1'))) 'Reader created an activation marker'
        Assert-Equal (Snapshot $f.Root) $before 'Component inspection changed its inputs'
    }
    Test-Case 'YouTube maps its closed nested inventory and treats absent external provenance as inert data' {
        $f = New-Fixture -Youtube
        $before = Snapshot $f.Root
        $plan = Get-Plan $f
        Assert-True ($null -ne $plan) 'No YouTube component plan'
        Assert-Equal $plan.files.Count 20
        Assert-Equal (@($plan.files | Where-Object component -CEQ 'youtube').Count) 10
        Assert-True ($plan.files.relative_path -ccontains 'download-tools/youtube/extractor/_internal/python313.dll') 'Nested support path was flattened'
        Assert-True ($plan.files.relative_path -ccontains 'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js') 'EJS path was changed'
        Assert-Equal (Snapshot $f.Root) $before 'Reading historical paths changed the candidate'
    }
    Test-Case 'the explicit receipt SHA256 is mandatory exact and rechecked for each call' {
        foreach ($bad in @(('A' * 64), ('0' * 63), ('0' * 64))) {
            $f = New-Fixture
            $f.Pin = $bad
            Assert-Throws { Get-Plan $f } 'Malformed or mismatched pin was accepted'
        }
        $f = New-Fixture
        Assert-True ($null -ne (Get-Plan $f)) 'No first snapshot'
        [IO.File]::AppendAllText($f.Receipt, ' ')
        $before = Snapshot $f.Root
        Assert-Throws { Get-Plan $f } 'A stale receipt pin was accepted'
        Assert-Equal (Snapshot $f.Root) $before
    }
    Test-Case 'closed receipt and nested fields keep JSON types without coercion' {
        foreach ($change in @(
            { param($d) $d['extra'] = $true },
            { param($d) $d.schemaVersion = '1' },
            { param($d) $d.state = 'installed' },
            { param($d) $d.header['extra'] = 1 },
            { param($d) $d.files[0].bytes = $true },
            { param($d) $d.files[0].bytes = 134217729 },
            { param($d) $d.files[0].sha256 = "$(('0' * 64))`n" },
            { param($d) $d.recipe_files[0]['extra'] = 1 }
        )) { Reject-ChangedReceipt $change }
    }
    Test-Case 'strict UTF8 JSON rejects duplicate keys trailing input depth and size before payload copying' {
        foreach ($kind in @('duplicate', 'trailing', 'depth', 'oversize', 'utf8')) {
            $f = New-Fixture
            $raw = Get-Content -LiteralPath $f.Receipt -Raw
            switch ($kind) {
                'duplicate' { $raw = $raw.Replace('"schemaVersion":1', '"schemaVersion":1,"schemaVersion":1') }
                'trailing' { $raw += '{}' }
                'depth' { $raw = ('[' * 10) + '0' + (']' * 10) }
                'oversize' { $raw += ' ' * 8388608 }
            }
            if ($kind -eq 'utf8') { [IO.File]::WriteAllBytes($f.Receipt, [byte[]](0x7B, 0xFF, 0x7D)) }
            else { Write-Utf8 $f.Receipt $raw }
            $f.Pin = Hash $f.Receipt
            $before = Snapshot $f.Root
            Assert-Throws { Get-Plan $f } 'Invalid JSON was accepted'
            Assert-Equal (Snapshot $f.Root) $before
        }
    }
    Test-Case 'network media and YouTube roles and fixed locations cannot be swapped or broadened' {
        foreach ($change in @(
            { param($d) $d.files[0].role = 'Other' },
            { param($d) $d.files[1].role = 'Bootstrap' },
            { param($d) $d.files[0].relative_path = 'payload/download-tools/network/../escape.exe' },
            { param($d) $d.files[0].relative_path = 'payload/download-tools/media/RCMusicDownloadWorker.exe' },
            { param($d) $d.files[-1].component = 'other' }
        )) { Reject-ChangedReceipt $change }
        Reject-ChangedReceipt { param($d) $d.youtube_receipt = $null } -Youtube
        Reject-ChangedReceipt { param($d) $d.files[10].role = 'Support' } -Youtube
    }
    Test-Case 'case aliases and file-directory conflicts in YouTube inventory are rejected' {
        Reject-ChangedReceipt {
            param($d)
            $row = $d.files[-1]
            $d.files += [ordered]@{ component = 'youtube'; role = 'Support'
                relative_path = $row.relative_path.Replace('python313.dll', 'PYTHON313.dll'); bytes = $row.bytes; sha256 = $row.sha256 }
        } -Youtube
        Reject-ChangedReceipt {
            param($d)
            $row = $d.files[-1]
            $d.files += [ordered]@{ component = 'youtube'; role = 'Support'
                relative_path = $row.relative_path + '/child.bin'; bytes = 1; sha256 = ('0' * 64) }
        } -Youtube
    }
    Test-Case 'the physical candidate tree rejects extra files empty directories and an unowned activation marker' {
        foreach ($kind in @('file', 'directory', 'marker')) {
            $f = New-Fixture -Youtube
            if ($kind -eq 'file') { Write-Utf8 (Join-Path $f.Root 'payload/undeclared.bin') 'Preserve unknown file' }
            elseif ($kind -eq 'directory') { $null = New-Item -ItemType Directory -Path (Join-Path $f.Root 'payload/unknown-empty') }
            else { Write-Utf8 (Join-Path $f.Root 'payload/download-tools/installed-v1') "RCMusicDownloadTools-v1`n" }
            $before = Snapshot $f.Root
            Assert-Throws { Get-Plan $f } 'An undeclared component object was accepted'
            Assert-Equal (Snapshot $f.Root) $before 'Unknown object was deleted or changed'
        }
    }
    Test-Case 'header identity bytes and source payload drift are rejected without repairs' {
        Reject-ChangedReceipt { param($d) $d.header.relative_path = '../foreign.hpp' }
        foreach ($kind in @('header', 'payload')) {
            $f = New-Fixture
            Assert-True ($null -ne (Get-Plan $f)) 'No initial plan'
            $path = if ($kind -eq 'header') { $f.Header } else { Join-Path $f.Root $f.Data.files[0].relative_path }
            [IO.File]::AppendAllText($path, ' changed')
            $before = Snapshot $f.Root
            Assert-Throws { Get-Plan $f } 'Header or payload drift was accepted'
            Assert-Equal (Snapshot $f.Root) $before
        }
        $f = New-Fixture -MissingMedia
        $before = Snapshot $f.Root
        Assert-Throws { Get-Plan $f } 'A missing payload file was accepted'
        Assert-Equal (Snapshot $f.Root) $before
    }
    Test-Case 'receipt paths must be local absolute and safe before file access' {
        foreach ($bad in @('relative.json', '\\server.invalid\share\receipt.json', '\\.\NUL', 'C:/NUL/receipt.json', 'C:/fixture/receipt.json:stream')) {
            Assert-Throws { Get-RcMusicDownloadComponentPlan -ReceiptPath $bad -ReceiptSha256 ('0' * 64) } 'Unsafe receipt path was accepted'
        }
    }
} finally {
    Remove-Module RcMusicDownloadComponent -Force -ErrorAction SilentlyContinue
    if ($state.Failed -eq 0) {
        Assert-SafeTestRoot
        if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    } else { Write-Host "[INFO] Preserved owned failed fixtures: $testRoot" }
}
Write-Host "Download component: $($state.Passed) passed / $($state.Failed) failed"
if ($state.Failed -gt 0) { exit 1 }
