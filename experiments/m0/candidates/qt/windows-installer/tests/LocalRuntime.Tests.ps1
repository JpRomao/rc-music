#Requires -Version 7.0
[CmdletBinding()]
param([string] $OutputDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository ('out/m0/rc-music-local/' + [Guid]::NewGuid().ToString('N'))
}
$result = & (Join-Path $PSScriptRoot '../New-RcMusicLocalRuntime.ps1') -OutputDirectory $OutputDirectory
foreach ($relative in @('RCMusic.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Quick.dll',
        'Qt6Multimedia.dll', 'tag.dll', 'avcodec-61.dll', 'avformat-61.dll',
        'plugins/platforms/qwindows.dll', 'plugins/multimedia/ffmpegmediaplugin.dll',
        'qml/QtQuick/Controls/Basic/qmldir', 'LOCAL-DEVELOPMENT.txt')) {
    if (-not [IO.File]::Exists((Join-Path $result.payload $relative))) { throw "Missing runtime file: $relative" }
}
$manifest = Get-Content -LiteralPath $result.manifest -Raw | ConvertFrom-Json
if ($manifest.build_testing -ne $false -or $manifest.architecture -cne 'x64') { throw 'Not a production-mode x64 payload' }
foreach ($entry in $manifest.files) {
    $file = Join-Path $result.payload $entry.relative_path
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
        throw "Runtime hash mismatch: $($entry.relative_path)"
    }
    if ($entry.relative_path -match '(?i)(?:Qt6Test|Qt6QuickTest|qmltooling|\.pdb$|\.lib$|\.mp3$|\.wav$)') {
        throw "Development/test/media file in payload: $($entry.relative_path)"
    }
}
Write-Host "[PASS] Real x64 runtime assembled with Multimedia, TagLib, QML and verified file hashes: $($result.payload)"
$result
