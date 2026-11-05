#Requires -Version 7.0
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $repository 'packaging/windows/RcMusicDownloadComponentPe.psm1') -Force
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection.'
}
function New-Entry([string] $Relative, [string] $Component, [string] $Role) {
    return [pscustomobject] [ordered] @{
        source = 'C:/absent pinned inputs/' + $Relative
        relative_path = $Relative
        component = $Component
        role = $Role
        size_bytes = 23
        sha256 = '0' * 64
    }
}
function New-Report([string] $Relative, [string[]] $Imports = @('KERNEL32.dll')) {
    return [pscustomobject] [ordered] @{ relative_path = $Relative; machine = 'x64'; imports = $Imports }
}
function New-Fixture([switch] $WithoutYoutube) {
    $files = [Collections.Generic.List[object]]::new()
    $reports = [Collections.Generic.List[object]]::new()
    $network = [ordered] @{
        Bootstrap = 'RCMusicDownloadWorker.exe'
        Payload = 'rcmusic-download-network.dll'
        Core = 'Qt6Core.dll'
        Network = 'Qt6Network.dll'
        Msvcp140 = 'msvcp140.dll'
        Msvcp140_1 = 'msvcp140_1.dll'
        Vcruntime140 = 'vcruntime140.dll'
        Vcruntime140_1 = 'vcruntime140_1.dll'
        Plugin = 'qrcmusicschanneloffline.dll'
    }
    foreach ($role in $network.Keys) {
        $relative = 'download-tools/network/' + $network[$role]
        $files.Add((New-Entry $relative 'network' $role))
        $reports.Add((New-Report $relative))
    }
    $media = 'download-tools/media/RCMusicMediaWorker.exe'
    $files.Add((New-Entry $media 'media' 'Media'))
    $reports.Add((New-Report $media @('KERNEL32.dll', 'ole32.dll')))
    if (-not $WithoutYoutube) {
        $extractor = 'download-tools/youtube/extractor/yt-dlp.exe'
        $node = 'download-tools/youtube/javascript/node.exe'
        $files.Add((New-Entry $extractor 'youtube' 'Extractor'))
        $reports.Add((New-Report $extractor))
        $files.Add((New-Entry $node 'youtube' 'Node'))
        $reports.Add((New-Report $node @('ADVAPI32.dll', 'CRYPT32.dll', 'dbghelp.dll', 'KERNEL32.dll',
                    'ole32.dll', 'SHELL32.dll', 'USER32.dll', 'USERENV.dll', 'WINMM.dll', 'WS2_32.dll')))
        $support = @('_asyncio.pyd', '_bz2.pyd', '_ctypes.pyd', '_decimal.pyd', '_elementtree.pyd',
            '_hashlib.pyd', '_lzma.pyd', '_multiprocessing.pyd', '_overlapped.pyd', '_queue.pyd',
            '_socket.pyd', '_sqlite3.pyd', '_ssl.pyd', '_uuid.pyd', '_wmi.pyd', 'pyexpat.pyd',
            'select.pyd', 'unicodedata.pyd', 'libcrypto-3.dll', 'libffi-8.dll', 'libssl-3.dll',
            'python313.dll', 'sqlite3.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll')
        foreach ($name in $support) {
            $relative = 'download-tools/youtube/extractor/_internal/' + $name
            $files.Add((New-Entry $relative 'youtube' 'Support'))
            $reports.Add((New-Report $relative))
        }
        foreach ($bundle in @(@('EjsCore', 'core.min.js'), @('EjsLibrary', 'lib.min.js'))) {
            $files.Add((New-Entry ('download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/' + $bundle[1]) 'youtube' $bundle[0]))
        }
        $files.Add((New-Entry 'download-tools/youtube/extractor/_internal/base_library.zip' 'youtube' 'Support'))
        foreach ($notice in @('ejs', 'node', 'pyinstaller', 'python', 'yt-dlp')) {
            $files.Add((New-Entry ('download-tools/youtube/notices/' + $notice + '-LICENSE.txt') 'youtube' 'Support'))
        }
    }
    $fixture = [pscustomobject] @{ Files = @($files.ToArray()); Reports = @($reports.ToArray()) }
    Set-Imports $fixture 'download-tools/network/rcmusic-download-network.dll' @(
        'Qt6Core.dll', 'Qt6Network.dll', 'MSVCP140.dll', 'VCRUNTIME140.dll', 'KERNEL32.dll')
    if (-not $WithoutYoutube) {
        Set-Imports $fixture 'download-tools/youtube/extractor/_internal/_ssl.pyd' @(
            'python313.dll', 'libcrypto-3.dll', 'libssl-3.dll', 'VCRUNTIME140.dll', 'KERNEL32.dll')
        Set-Imports $fixture 'download-tools/youtube/extractor/_internal/python313.dll' @(
            'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll', 'api-ms-win-core-path-l1-1-0.dll', 'KERNEL32.dll')
    }
    return $fixture
}
function Set-Imports($Fixture, [string] $Relative, [string[]] $Imports) {
    $entry = @($Fixture.Reports | Where-Object relative_path -CEQ $Relative)
    if ($entry.Count -ne 1) { throw 'Fixture report is missing or ambiguous.' }
    $entry[0].imports = $Imports
}
function Closure($Fixture) {
    return Get-RcMusicDownloadComponentPeClosure -Files $Fixture.Files -Reports $Fixture.Reports
}

Test-Case 'all 37 PEs including 18 PYDs are covered without flattening the 45-file tree' {
    $fixture = New-Fixture
    $result = Closure $fixture
    Assert-Equal $fixture.Files.Count 45
    Assert-Equal $result.reports.Count 37
    Assert-Equal @($result.reports | Where-Object relative_path -CLike '*.pyd').Count 18
    Assert-Equal @($result.reports | Where-Object domain -CEQ 'network').Count 9
    Assert-Equal @($result.reports | Where-Object domain -CEQ 'media').Count 1
    Assert-Equal @($result.reports | Where-Object domain -CEQ 'extractor').Count 26
    Assert-Equal @($result.reports | Where-Object domain -CEQ 'node').Count 1
    $edge = @($result.private_imports | Where-Object {
        $_.relative_path -ceq 'download-tools/youtube/extractor/_internal/_ssl.pyd' -and $_.name -ceq 'python313.dll'
    })
    Assert-Equal $edge.Count 1
    Assert-Equal $edge[0].resolved_path 'download-tools/youtube/extractor/_internal/python313.dll'
    Assert-Equal $edge[0].domain 'extractor'
    Assert-Equal @($result.windows_external | Where-Object name -CEQ 'kernel32.dll').Count 37
    Assert-Equal @($result.windows_external | Where-Object name -Match '^(python|vcruntime|msvcp|libssl|libcrypto)').Count 0
}
Test-Case 'network and media alone are a complete optional component without a YouTube domain' {
    $result = Closure (New-Fixture -WithoutYoutube)
    Assert-Equal $result.reports.Count 10
    Assert-Equal @($result.reports | Where-Object domain -In @('extractor', 'node')).Count 0
}
Test-Case 'normalization is deterministic case-insensitive and does not mutate caller data' {
    $fixture = New-Fixture
    $before = $fixture | ConvertTo-Json -Depth 12 -Compress
    $expected = Closure $fixture | ConvertTo-Json -Depth 12 -Compress
    Assert-Equal ($fixture | ConvertTo-Json -Depth 12 -Compress) $before
    [Array]::Reverse($fixture.Files)
    [Array]::Reverse($fixture.Reports)
    foreach ($report in $fixture.Reports) {
        $report.relative_path = $report.relative_path.ToUpperInvariant().Replace('/', '\')
        $report.imports = @($report.imports | ForEach-Object { $_.ToUpperInvariant() }) + @('kernel32.DLL')
    }
    Assert-Equal ((Closure $fixture) | ConvertTo-Json -Depth 12 -Compress) $expected
}
Test-Case 'network cannot drop one of its nine fixed private roles' {
    $fixture = New-Fixture
    $fixture.Files = @($fixture.Files | Where-Object role -CNE 'Network')
    $fixture.Reports = @($fixture.Reports | Where-Object relative_path -CNE 'download-tools/network/Qt6Network.dll')
    Assert-Rejected { Closure $fixture } 'network|inventory'
}
Test-Case 'extractor cannot resolve a missing private Python DLL from the host' {
    $fixture = New-Fixture
    $fixture.Files = @($fixture.Files | Where-Object relative_path -CNE 'download-tools/youtube/extractor/_internal/python313.dll')
    $fixture.Reports = @($fixture.Reports | Where-Object relative_path -CNE 'download-tools/youtube/extractor/_internal/python313.dll')
    Assert-Rejected { Closure $fixture } 'private|unresolved'
}
Test-Case 'network CRT cannot satisfy an extractor import' {
    $fixture = New-Fixture
    $fixture.Files = @($fixture.Files | Where-Object relative_path -CNE 'download-tools/youtube/extractor/_internal/VCRUNTIME140.dll')
    $fixture.Reports = @($fixture.Reports | Where-Object relative_path -CNE 'download-tools/youtube/extractor/_internal/VCRUNTIME140.dll')
    Assert-Rejected { Closure $fixture } 'private|unresolved'
}
Test-Case 'Node cannot borrow CRT from the extractor or network' {
    $fixture = New-Fixture
    Set-Imports $fixture 'download-tools/youtube/javascript/node.exe' @('KERNEL32.dll', 'VCRUNTIME140.dll')
    Assert-Rejected { Closure $fixture } 'private|unresolved'
}
Test-Case 'Media Foundation cannot borrow the player or network Qt runtime' {
    $fixture = New-Fixture
    Set-Imports $fixture 'download-tools/media/RCMusicMediaWorker.exe' @('KERNEL32.dll', 'Qt6Core.dll')
    Assert-Rejected { Closure $fixture } 'private|unresolved'
}
Test-Case 'bundled Windows system DLL shadows are refused' {
    $fixture = New-Fixture
    $path = 'download-tools/youtube/extractor/_internal/KERNEL32.dll'
    $fixture.Files += New-Entry $path 'youtube' 'Support'
    $fixture.Reports += New-Report $path
    Assert-Rejected { Closure $fixture } 'system|Windows|shadow'
}
Test-Case 'private imports cannot be satisfied by flattening a nested support directory' {
    $fixture = New-Fixture
    foreach ($entry in @($fixture.Files) + @($fixture.Reports)) {
        if ($entry.relative_path -ceq 'download-tools/youtube/extractor/_internal/libcrypto-3.dll') {
            $entry.relative_path = 'download-tools/youtube/extractor/_internal/foreign/libcrypto-3.dll'
        }
    }
    Assert-Rejected { Closure $fixture } 'private|unresolved'
}
Test-Case 'a PYD without its own report is rejected' {
    $fixture = New-Fixture
    $fixture.Reports = @($fixture.Reports | Where-Object relative_path -CNE 'download-tools/youtube/extractor/_internal/_ssl.pyd')
    Assert-Rejected { Closure $fixture } 'report'
}
Test-Case 'duplicate reports are refused even when path casing differs' {
    $fixture = New-Fixture
    $fixture.Reports += New-Report 'DOWNLOAD-TOOLS/YOUTUBE/EXTRACTOR/_INTERNAL/_SSL.PYD'
    Assert-Rejected { Closure $fixture } 'duplicate|report'
}
Test-Case 'extra reports and reports for non-PE data are rejected' {
    foreach ($path in @('download-tools/youtube/unknown.dll',
            'download-tools/youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js')) {
        $fixture = New-Fixture
        $fixture.Reports += New-Report $path
        Assert-Rejected { Closure $fixture } 'report|non-PE'
    }
}
Test-Case 'non-x64 or malformed report shapes are rejected' {
    foreach ($machine in @('x86', 'arm64', 'X64', '')) {
        $fixture = New-Fixture
        $fixture.Reports[0].machine = $machine
        Assert-Rejected { Closure $fixture } 'machine|x64|report'
    }
    $fixture = New-Fixture
    $fixture.Reports[0].imports = 'KERNEL32.dll'
    Assert-Rejected { Closure $fixture } 'imports|report'
}
Test-Case 'unknown imports and unreviewed API sets are refused instead of probing System32' {
    foreach ($name in @('foreign.dll', 'api-ms-win-madeup-l1-1-0.dll', 'ext-ms-win-madeup-l1-1-0.dll')) {
        $fixture = New-Fixture
        Set-Imports $fixture 'download-tools/youtube/javascript/node.exe' @('KERNEL32.dll', $name)
        Assert-Rejected { Closure $fixture } 'import|unresolved'
    }
}
Test-Case 'Windows allowlists remain distinct for each domain' {
    $fixture = New-Fixture
    Set-Imports $fixture 'download-tools/media/RCMusicMediaWorker.exe' @('KERNEL32.dll', 'dbghelp.dll')
    Assert-Rejected { Closure $fixture } 'import|unresolved'
    $fixture = New-Fixture
    Set-Imports $fixture 'download-tools/network/Qt6Core.dll' @('KERNEL32.dll', 'api-ms-win-core-path-l1-1-0.dll')
    Assert-Rejected { Closure $fixture } 'import|unresolved'
}
Test-Case 'import names must be bare ASCII DLL basenames without whitespace or path syntax' {
    foreach ($name in @('', ' C:\\foreign.dll', '../foreign.dll', 'foreign.exe', 'bad name.dll',
            '日本.dll', 'KERNEL32.dll ', "KERNEL32.dll`n", 'KERNEL32.dll:stream')) {
        $fixture = New-Fixture
        Set-Imports $fixture 'download-tools/youtube/javascript/node.exe' @($name)
        Assert-Rejected { Closure $fixture } 'import'
    }
}
Test-Case 'inventory paths components and fixed roles are closed before resolving imports' {
    foreach ($path in @('/download-tools/network/Qt6Core.dll', 'payload/download-tools/network/Qt6Core.dll',
            'download-tools/network/../Qt6Core.dll', 'download-tools/network/Qt6Core.dll:stream',
            'download-tools/network/nested/Qt6Core.dll')) {
        $fixture = New-Fixture
        $fixture.Files[2].relative_path = $path
        Assert-Rejected { Closure $fixture } 'inventory|path|network'
    }
    $fixture = New-Fixture
    $fixture.Files[0].component = 'unknown'
    Assert-Rejected { Closure $fixture } 'component|inventory'
    $fixture = New-Fixture
    $fixture.Files[0].role = 'Unknown'
    Assert-Rejected { Closure $fixture } 'role|inventory|network'
}
Test-Case 'inventory path collisions are refused case-insensitively' {
    $fixture = New-Fixture
    $fixture.Files += New-Entry 'DOWNLOAD-TOOLS/NETWORK/QT6CORE.DLL' 'network' 'Core'
    Assert-Rejected { Closure $fixture } 'duplicate|collision|inventory'
}
Test-Case 'inventory metadata must contain explicit positive bytes and lowercase pins' {
    foreach ($bytes in @(0, -1, 134217729, '23', $true)) {
        $fixture = New-Fixture
        $fixture.Files[0].size_bytes = $bytes
        Assert-Rejected { Closure $fixture } 'bytes|inventory'
    }
    foreach ($pin in @('', ('A' * 64), ('0' * 63))) {
        $fixture = New-Fixture
        $fixture.Files[0].sha256 = $pin
        Assert-Rejected { Closure $fixture } 'pin|SHA|inventory'
    }
}

Write-Host "Download component PE: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
