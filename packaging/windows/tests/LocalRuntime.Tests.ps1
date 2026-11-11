#Requires -Version 7.0
[CmdletBinding()]
param([string] $OutputDirectory, [Parameter(Mandatory)] [string] $BuildDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $repository ('out/product/rc-music-local/' + [Guid]::NewGuid().ToString('N'))
}
$result = & (Join-Path $PSScriptRoot '../New-RcMusicLocalRuntime.ps1') -OutputDirectory $OutputDirectory -BuildDirectory $BuildDirectory
foreach ($relative in @('RCMusic.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Quick.dll',
        'Qt6Multimedia.dll', 'tag.dll', 'avcodec-61.dll', 'avformat-61.dll',
        'plugins/platforms/qwindows.dll', 'plugins/multimedia/ffmpegmediaplugin.dll',
        'qml/QtQuick/Controls/Basic/qmldir', 'LOCAL-DEVELOPMENT.txt')) {
    if (-not [IO.File]::Exists((Join-Path $result.payload $relative))) { throw "Missing runtime file: $relative" }
}
$manifest = Get-Content -LiteralPath $result.manifest -Raw | ConvertFrom-Json
if ($manifest.build_testing -ne $false -or $manifest.architecture -cne 'x64') { throw 'Not a production-mode x64 payload' }
$networkPin = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../qt-network-runtime.v1.json') -Raw | ConvertFrom-Json
$network = @($manifest.files | Where-Object relative_path -CEQ 'Qt6Network.dll')
if ($network.Count -ne 1 -or $network[0].sha256 -cne $networkPin.sha256 -or
    $manifest.qt_network_runtime.qualified_build_receipt_sha256 -cne $networkPin.qualified_build_receipt_sha256) {
    throw 'The runtime manifest does not bind the qualified Qt6Network.dll'
}
foreach ($relative in @('notices/ffmpeg/NOTICE.txt', 'notices/ffmpeg/FFmpeg-COPYING.LGPLv2.1.txt',
        'notices/ffmpeg/FFmpeg-LICENSE.txt', 'notices/ffmpeg/FFmpeg-CREDITS.txt',
        'notices/ffmpeg/FFmpeg-IJG.txt', 'notices/ffmpeg/Boost-LICENSE.txt', 'notices/ffmpeg/zlib-LICENSE.txt',
        'notices/qt-multimedia/NOTICE.txt', 'notices/qt-multimedia/Signalsmith-Stretch-LICENSE.txt',
        'notices/qt-multimedia/Signalsmith-DSP-LICENSE.txt', 'notices/qt-multimedia/dr-libs-LICENSE.txt',
        'notices/qt-multimedia/dr-wav-NOTICE.txt', 'notices/qt-multimedia/TLSF-LICENSE.txt',
        'notices/taglib/NOTICE.txt', 'notices/taglib/TagLib-AUTHORS.txt',
        'notices/taglib/TagLib-COPYING.LGPL.txt', 'notices/taglib/TagLib-COPYING.MPL.txt',
        'notices/taglib/utfcpp-LICENSE.txt')) {
    $entry = @($manifest.files | Where-Object relative_path -CEQ $relative)
    $recipe = @($manifest.recipe_files | Where-Object path -CEQ ('packaging/windows/' + $relative))
    if ($entry.Count -ne 1 -or $recipe.Count -ne 1 -or $entry[0].sha256 -cne $recipe[0].sha256) {
        throw "Missing or unbound runtime notice: $relative"
    }
}
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
