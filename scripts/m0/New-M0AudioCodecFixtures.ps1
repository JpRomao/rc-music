[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $FfmpegPath,
    [Parameter(Mandatory)] [string] $FfprobePath,
    [Parameter(Mandatory)] [string] $DestinationRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0 codec fixture generation requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')).TrimEnd('\', '/')
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out\m0')).TrimEnd('\', '/')
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }

function Get-M0Sha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-M0Text {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Content
    )

    $canonical = $Content.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($LiteralPath, $canonical, $utf8NoBom)
}

function Write-M0Json {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Value
    )

    Write-M0Text -LiteralPath $LiteralPath -Content ($Value | ConvertTo-Json -Depth 100)
}

function Assert-M0LiteralAbsoluteFile {
    param(
        [Parameter(Mandatory)] [string] $ParameterName,
        [Parameter(Mandatory)] [string] $LiteralPath
    )

    if ([WildcardPattern]::ContainsWildcardCharacters($LiteralPath) -or
        -not [IO.Path]::IsPathFullyQualified($LiteralPath)) {
        throw [ArgumentException]::new("$ParameterName must be a fully qualified literal path without wildcard characters.")
    }

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("$ParameterName does not name an existing file.", $fullPath)
    }

    $item = Get-Item -LiteralPath $fullPath -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw [IO.InvalidDataException]::new("$ParameterName must not be a symbolic link or reparse point.")
    }

    return $fullPath
}

function Assert-M0DestinationPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if ([WildcardPattern]::ContainsWildcardCharacters($LiteralPath) -or
        -not [IO.Path]::IsPathFullyQualified($LiteralPath)) {
        throw [ArgumentException]::new('DestinationRoot must be a fully qualified literal path without wildcard characters.')
    }

    $destination = [IO.Path]::GetFullPath($LiteralPath).TrimEnd('\', '/')
    if (-not $destination.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, $comparison)) {
        throw [ArgumentException]::new('Codec fixture generation is limited to a fresh staging directory strictly below out/m0.')
    }

    $relativeDestination = [IO.Path]::GetRelativePath($repositoryRoot, $destination)
    $currentPath = $repositoryRoot
    if (Test-Path -LiteralPath $currentPath) {
        $repositoryItem = Get-Item -LiteralPath $currentPath -Force
        if (($repositoryItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0ACF002 links and reparse points are forbidden in the destination path: '$currentPath'.")
        }
    }

    foreach ($segment in $relativeDestination.Split(
            [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar),
            [StringSplitOptions]::RemoveEmptyEntries)) {
        $currentPath = Join-Path $currentPath $segment
        if (-not (Test-Path -LiteralPath $currentPath)) { break }
        $item = Get-Item -LiteralPath $currentPath -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0ACF002 links and reparse points are forbidden in the destination path: '$currentPath'.")
        }
    }

    if (Test-Path -LiteralPath $destination) {
        if (-not (Test-Path -LiteralPath $destination -PathType Container) -or
            @(Get-ChildItem -LiteralPath $destination -Force).Count -ne 0) {
            throw [IO.IOException]::new('M0ACF001 DestinationRoot must not exist or must be an empty directory.')
        }
    }

    return $destination
}

function Invoke-M0NativeCapture {
    param(
        [Parameter(Mandatory)] [string] $LiteralExecutable,
        [Parameter(Mandatory)] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $Operation
    )

    [string[]] $lines = @(& $LiteralExecutable @Arguments 2>&1 | ForEach-Object { $_.ToString() })
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $diagnostic = ($lines | Select-Object -Last 20) -join "`n"
        throw [InvalidOperationException]::new("$Operation failed with exit code $exitCode.`n$diagnostic")
    }

    return $lines
}

function Assert-M0PinnedTool {
    param(
        [Parameter(Mandatory)] [string] $ToolId,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $ExpectedVersionLine
    )

    $item = Get-Item -LiteralPath $LiteralPath -Force
    if ($item.Length -ne $ExpectedSizeBytes) {
        throw [IO.InvalidDataException]::new("$ToolId size mismatch: expected $ExpectedSizeBytes, found $($item.Length).")
    }

    $actualSha256 = Get-M0Sha256 -LiteralPath $LiteralPath
    if ($actualSha256 -cne $ExpectedSha256) {
        throw [IO.InvalidDataException]::new("$ToolId SHA-256 mismatch: expected $ExpectedSha256, found $actualSha256.")
    }

    [string[]] $versionLines = @(Invoke-M0NativeCapture -LiteralExecutable $LiteralPath -Arguments @('-hide_banner', '-version') -Operation "$ToolId version check")
    if ($versionLines.Count -eq 0 -or $versionLines[0] -cne $ExpectedVersionLine) {
        $actualVersion = if ($versionLines.Count -gt 0) { $versionLines[0] } else { '<no output>' }
        throw [IO.InvalidDataException]::new("$ToolId version mismatch: expected '$ExpectedVersionLine', found '$actualVersion'.")
    }

    return [ordered]@{
        tool_id = $ToolId
        version_line = $ExpectedVersionLine
        size_bytes = $ExpectedSizeBytes
        sha256 = $ExpectedSha256
        usage_scope = 'fixture-generation-only; never runtime or packaged'
    }
}

function Get-M0PropertyValue {
    param(
        [AllowNull()] $Object,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-M0NullableInt {
    param([AllowNull()] $Value)

    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $null }
    return [int]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Get-M0NullableLong {
    param([AllowNull()] $Value)

    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return $null }
    return [long]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

function Test-M0ExpectedTags {
    param(
        [AllowNull()] $Tags,
        [Parameter(Mandatory)] $ExpectedTags
    )

    if ($null -eq $Tags) { return $false }
    foreach ($name in @('title', 'artist', 'album', 'comment', 'date')) {
        if ([string](Get-M0PropertyValue -Object $Tags -Name $name) -cne [string]$ExpectedTags[$name]) {
            return $false
        }
    }
    return $true
}

$ffmpeg = Assert-M0LiteralAbsoluteFile -ParameterName 'FfmpegPath' -LiteralPath $FfmpegPath
$ffprobe = Assert-M0LiteralAbsoluteFile -ParameterName 'FfprobePath' -LiteralPath $FfprobePath
$destination = Assert-M0DestinationPath -LiteralPath $DestinationRoot

$ffmpegTool = Assert-M0PinnedTool `
    -ToolId 'ffmpeg' `
    -LiteralPath $ffmpeg `
    -ExpectedSizeBytes 227398656 `
    -ExpectedSha256 '09948d4cdd0650da6ff5a87577469f2a218dc2615ae379f8f734d24c49de0f73' `
    -ExpectedVersionLine 'ffmpeg version 8.1.1-full_build-www.gyan.dev Copyright (c) 2000-2026 the FFmpeg developers'
$ffprobeTool = Assert-M0PinnedTool `
    -ToolId 'ffprobe' `
    -LiteralPath $ffprobe `
    -ExpectedSizeBytes 227193344 `
    -ExpectedSha256 'a6618e99bb58869ded3c6f37b53aa1a8d701c3591dbb7b5b317d47369c112be2' `
    -ExpectedVersionLine 'ffprobe version 8.1.1-full_build-www.gyan.dev Copyright (c) 2007-2026 the FFmpeg developers'

[IO.Directory]::CreateDirectory($destination) | Out-Null
$validRoot = Join-Path $destination 'fixtures\valid'
$candidateRoot = Join-Path $destination 'fixtures\candidate'
$workingRoot = Join-Path $destination '.work'
foreach ($path in @($validRoot, $candidateRoot, $workingRoot)) {
    [IO.Directory]::CreateDirectory($path) | Out-Null
}

$signal = 'aevalsrc=0.125*sin(2*PI*440*t)|0.125*sin(2*PI*660*t):s=48000:d=2:c=stereo'
$sourcePath = Join-Path $workingRoot 'deterministic-stereo-2s.wav'
[string[]] $sourceArguments = @(
    '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
    '-f', 'lavfi', '-i', $signal,
    '-map_metadata', '-1', '-vn', '-sn', '-dn',
    '-fflags', '+bitexact', '-flags:a', '+bitexact', '-threads:a', '1',
    '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2',
    '-write_bext', '0', '-write_peak', 'off', '-rf64', 'never', '-f', 'wav',
    $sourcePath
)
[string[]] $portableSourceArguments = @($sourceArguments[0..($sourceArguments.Count - 2)]) + @('{source}')

$expectedTags = [ordered]@{
    title = 'Canção determinística 日本語 🎵'
    artist = 'João – Codex'
    album = 'Álbum de teste Ω'
    comment = 'fixture local • 2026-08-29'
    date = '2026-08-29'
}

[string[]] $commonEncodingArguments = @(
    '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
    '-i', $sourcePath,
    '-map', '0:a:0', '-map_metadata', '-1', '-vn', '-sn', '-dn',
    '-fflags', '+bitexact', '-flags:a', '+bitexact', '-threads:a', '1',
    '-metadata', "title=$($expectedTags.title)",
    '-metadata', "artist=$($expectedTags.artist)",
    '-metadata', "album=$($expectedTags.album)",
    '-metadata', "comment=$($expectedTags.comment)",
    '-metadata', "date=$($expectedTags.date)",
    '-metadata', 'creation_time='
)
[string[]] $portableCommonEncodingArguments = @($commonEncodingArguments)
$portableCommonEncodingArguments[6] = '{source}'

$actions = @(
    [ordered]@{
        fixture_id = 'mp3-cbr-stereo'
        status = 'valid'
        output_relative_path = 'fixtures/valid/mp3-cbr-stereo.mp3'
        codec_and_profile = 'MP3 Layer III CBR 128 kb/s'
        bitrate_mode = 'constant'
        codec_arguments = @('-c:a', 'libmp3lame', '-b:a', '128k', '-compression_level', '2', '-id3v2_version', '3', '-write_id3v1', '0', '-write_xing', '1', '-f', 'mp3')
    }
    [ordered]@{
        fixture_id = 'mp3-vbr-stereo'
        status = 'valid'
        output_relative_path = 'fixtures/valid/mp3-vbr-stereo.mp3'
        codec_and_profile = 'MP3 Layer III VBR quality 4'
        bitrate_mode = 'variable'
        codec_arguments = @('-c:a', 'libmp3lame', '-q:a', '4', '-compression_level', '2', '-id3v2_version', '3', '-write_id3v1', '0', '-write_xing', '1', '-f', 'mp3')
    }
    [ordered]@{
        fixture_id = 'mp3-unicode-tags'
        status = 'valid'
        output_relative_path = 'fixtures/valid/mp3-unicode-tags.mp3'
        codec_and_profile = 'MP3 Layer III CBR 96 kb/s with ID3v2.4 Unicode tags'
        bitrate_mode = 'constant'
        codec_arguments = @('-c:a', 'libmp3lame', '-b:a', '96k', '-compression_level', '2', '-id3v2_version', '4', '-write_id3v1', '0', '-write_xing', '1', '-f', 'mp3')
    }
    [ordered]@{
        fixture_id = 'm4a-aac-lc'
        status = 'valid'
        output_relative_path = 'fixtures/valid/m4a-aac-lc.m4a'
        codec_and_profile = 'AAC Low Complexity 128 kb/s in ISO BMFF/M4A'
        bitrate_mode = 'constant'
        codec_arguments = @('-c:a', 'aac', '-profile:a', 'aac_low', '-b:a', '128k', '-movflags', '+faststart', '-write_prft', '0', '-f', 'ipod')
    }
    [ordered]@{
        fixture_id = 'flac-16-stereo'
        status = 'valid'
        output_relative_path = 'fixtures/valid/flac-16-stereo.flac'
        codec_and_profile = 'FLAC 16-bit compression level 5'
        bitrate_mode = 'lossless'
        codec_arguments = @('-c:a', 'flac', '-compression_level', '5', '-f', 'flac')
    }
    [ordered]@{
        fixture_id = 'ogg-vorbis'
        status = 'valid'
        output_relative_path = 'fixtures/valid/ogg-vorbis.ogg'
        codec_and_profile = 'Vorbis quality 4 in Ogg'
        bitrate_mode = 'variable'
        codec_arguments = @('-c:a', 'libvorbis', '-q:a', '4', '-serial_offset', '0', '-page_duration', '1000000', '-f', 'ogg')
    }
    [ordered]@{
        fixture_id = 'opus-candidate'
        status = 'candidate'
        output_relative_path = 'fixtures/candidate/opus-candidate.opus'
        codec_and_profile = 'Opus VBR 96 kb/s in Ogg'
        bitrate_mode = 'variable'
        codec_arguments = @('-c:a', 'libopus', '-b:a', '96k', '-vbr', 'on', '-compression_level', '10', '-application', 'audio', '-frame_duration', '20', '-mapping_family', '0', '-serial_offset', '0', '-page_duration', '1000000', '-f', 'ogg')
    }
    [ordered]@{
        fixture_id = 'm4a-alac-candidate'
        status = 'candidate'
        output_relative_path = 'fixtures/candidate/m4a-alac-candidate.m4a'
        codec_and_profile = 'ALAC 16-bit in ISO BMFF/M4A'
        bitrate_mode = 'lossless'
        codec_arguments = @('-c:a', 'alac', '-sample_fmt', 's16p', '-movflags', '+faststart', '-write_prft', '0', '-f', 'ipod')
    }
)

$probeEntries = 'format=format_name,duration,size,bit_rate:format_tags:stream=index,codec_name,codec_long_name,profile,codec_type,sample_fmt,sample_rate,channels,channel_layout,bits_per_sample,bits_per_raw_sample,start_time,duration,bit_rate:stream_tags'
[string[]] $portableProbeArguments = @('-v', 'error', '-show_entries', $probeEntries, '-of', 'json', '{input}')
[string[]] $portableDecodeHashArguments = @(
    '-hide_banner', '-loglevel', 'error', '-nostdin', '-i', '{input}',
    '-map', '0:a:0', '-vn', '-sn', '-dn', '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2',
    '-f', 'hash', '-hash', 'sha256', '-'
)

$generatorPath = [IO.Path]::GetFullPath($PSCommandPath)
$recipe = [ordered]@{
    schema_id = 'm0.audio-codec-recipe.v1'
    schema_version = 1
    recipe_id = 'M0-AUDIO-CODEC-RECIPE-1'
    generator = [ordered]@{
        relative_path = 'scripts/m0/New-M0AudioCodecFixtures.ps1'
        sha256 = Get-M0Sha256 -LiteralPath $generatorPath
        size_bytes = (Get-Item -LiteralPath $generatorPath).Length
        runtime = 'PowerShell 7.6.5'
    }
    network_allowed = $false
    ambient_tool_lookup_allowed = $false
    tools = @($ffmpegTool, $ffprobeTool)
    source_signal = [ordered]@{
        kind = 'lavfi_aevalsrc_sine_pair'
        expression = $signal
        left_frequency_hz = 440
        right_frequency_hz = 660
        amplitude = 0.125
        sample_rate_hz = 48000
        channels = 2
        bits_per_sample = 16
        sample_count_per_channel = 96000
        duration_ms = 2000
        expected_size_bytes = 384044
        expected_sha256 = '249a7a355a3c84b5a8d2de3737b637a27b5e3be59a3ff0b6e5bb6ff7e43753a8'
        generation_arguments = $portableSourceArguments
    }
    metadata = $expectedTags
    common_encoding_arguments = $portableCommonEncodingArguments
    probe_arguments = $portableProbeArguments
    decode_hash_arguments = $portableDecodeHashArguments
    actions = $actions
}
$recipePath = Join-Path $destination 'codec-recipe.v1.json'
Write-M0Json -LiteralPath $recipePath -Value $recipe

[string[]] $sourceOutput = @(Invoke-M0NativeCapture -LiteralExecutable $ffmpeg -Arguments $sourceArguments -Operation 'deterministic source generation')
if ($sourceOutput.Count -ne 0) {
    throw [IO.InvalidDataException]::new('FFmpeg emitted unexpected output while generating the deterministic source.')
}
$sourceItem = Get-Item -LiteralPath $sourcePath -Force
$sourceSha256 = Get-M0Sha256 -LiteralPath $sourcePath
if ($sourceItem.Length -ne 384044 -or $sourceSha256 -cne '249a7a355a3c84b5a8d2de3737b637a27b5e3be59a3ff0b6e5bb6ff7e43753a8') {
    throw [IO.InvalidDataException]::new("Deterministic source mismatch: size $($sourceItem.Length), SHA-256 $sourceSha256.")
}

$fixtureProbeEntries = [Collections.Generic.List[object]]::new()
foreach ($action in $actions) {
    $outputPath = Join-Path $destination $action.output_relative_path
    $outputParent = Split-Path -Parent $outputPath
    if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("Fixture output directory does not exist: '$outputParent'.")
    }

    [string[]] $arguments = @($commonEncodingArguments) + @($action.codec_arguments) + @($outputPath)
    [string[]] $generationOutput = @(Invoke-M0NativeCapture -LiteralExecutable $ffmpeg -Arguments $arguments -Operation "generation of $($action.fixture_id)")
    if ($generationOutput.Count -ne 0) {
        throw [IO.InvalidDataException]::new("FFmpeg emitted unexpected output while generating '$($action.fixture_id)'.")
    }

    $outputItem = Get-Item -LiteralPath $outputPath -Force
    if (($outputItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or $outputItem.Length -le 0) {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' is empty, linked, or otherwise invalid.")
    }

    [string[]] $probeLines = @(Invoke-M0NativeCapture -LiteralExecutable $ffprobe -Arguments @(
            '-v', 'error', '-show_entries', $probeEntries, '-of', 'json', $outputPath
        ) -Operation "probe of $($action.fixture_id)")
    $probe = ($probeLines -join "`n") | ConvertFrom-Json -Depth 30
    $streams = @(Get-M0PropertyValue -Object $probe -Name 'streams')
    if ($streams.Count -ne 1 -or [string](Get-M0PropertyValue -Object $streams[0] -Name 'codec_type') -cne 'audio') {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' must contain exactly one audio stream.")
    }
    $stream = $streams[0]
    $format = Get-M0PropertyValue -Object $probe -Name 'format'
    if ($null -eq $format) {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' has no ffprobe format record.")
    }

    $formatTags = Get-M0PropertyValue -Object $format -Name 'tags'
    $streamTags = Get-M0PropertyValue -Object $stream -Name 'tags'
    $tagLocation = if (Test-M0ExpectedTags -Tags $formatTags -ExpectedTags $expectedTags) {
        'format'
    }
    elseif (Test-M0ExpectedTags -Tags $streamTags -ExpectedTags $expectedTags) {
        'stream'
    }
    else {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' did not retain the exact Unicode tags.")
    }

    [string[]] $decodeLines = @(Invoke-M0NativeCapture -LiteralExecutable $ffmpeg -Arguments @(
            '-hide_banner', '-loglevel', 'error', '-nostdin', '-i', $outputPath,
            '-map', '0:a:0', '-vn', '-sn', '-dn', '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2',
            '-f', 'hash', '-hash', 'sha256', '-'
        ) -Operation "decode hash of $($action.fixture_id)")
    [string[]] $decodeHashes = @($decodeLines | Where-Object { $_ -cmatch '^SHA256=[a-f0-9]{64}$' })
    if ($decodeHashes.Count -ne 1) {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' did not produce one SHA-256 decode hash.")
    }

    $durationText = [string](Get-M0PropertyValue -Object $format -Name 'duration')
    $containerDurationMs = [double]::Parse($durationText, [Globalization.CultureInfo]::InvariantCulture) * 1000.0
    if ($containerDurationMs -lt 1900.0 -or $containerDurationMs -gt 2100.0) {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' has unexpected container duration $containerDurationMs ms.")
    }

    $sampleRateHz = Get-M0NullableInt (Get-M0PropertyValue -Object $stream -Name 'sample_rate')
    $channels = Get-M0NullableInt (Get-M0PropertyValue -Object $stream -Name 'channels')
    if ($sampleRateHz -ne 48000 -or $channels -ne 2) {
        throw [IO.InvalidDataException]::new("Fixture '$($action.fixture_id)' must probe as stereo at 48 kHz.")
    }

    $fixtureProbeEntries.Add([ordered]@{
        fixture_id = $action.fixture_id
        status = $action.status
        relative_path = $action.output_relative_path
        size_bytes = $outputItem.Length
        sha256 = Get-M0Sha256 -LiteralPath $outputPath
        decoded_pcm_sha256 = $decodeHashes[0].Substring(7)
        format_name = [string](Get-M0PropertyValue -Object $format -Name 'format_name')
        container_duration_ms = $containerDurationMs
        codec_name = [string](Get-M0PropertyValue -Object $stream -Name 'codec_name')
        profile = Get-M0PropertyValue -Object $stream -Name 'profile'
        sample_format = [string](Get-M0PropertyValue -Object $stream -Name 'sample_fmt')
        sample_rate_hz = $sampleRateHz
        channels = $channels
        channel_layout = Get-M0PropertyValue -Object $stream -Name 'channel_layout'
        bits_per_sample = Get-M0NullableInt (Get-M0PropertyValue -Object $stream -Name 'bits_per_sample')
        bits_per_raw_sample = Get-M0NullableInt (Get-M0PropertyValue -Object $stream -Name 'bits_per_raw_sample')
        bit_rate_bps = Get-M0NullableLong (Get-M0PropertyValue -Object $format -Name 'bit_rate')
        tag_location = $tagLocation
        tags = $expectedTags
    })
}

$recipeItem = Get-Item -LiteralPath $recipePath
$probeReceipt = [ordered]@{
    schema_id = 'm0.audio-codec-probe.v1'
    schema_version = 1
    probe_id = 'M0-AUDIO-CODEC-PROBE-1'
    recipe = [ordered]@{
        relative_path = 'codec-recipe.v1.json'
        size_bytes = $recipeItem.Length
        sha256 = Get-M0Sha256 -LiteralPath $recipePath
    }
    tools = @($ffmpegTool, $ffprobeTool)
    source = [ordered]@{
        kind = 'lavfi_aevalsrc_sine_pair'
        size_bytes = $sourceItem.Length
        sha256 = $sourceSha256
        sample_rate_hz = 48000
        channels = 2
        decoded_sample_count_per_channel = 96000
        duration_ms = 2000
    }
    expected_tags = $expectedTags
    fixtures = @($fixtureProbeEntries)
}
$probePath = Join-Path $destination 'codec-probe.v1.json'
Write-M0Json -LiteralPath $probePath -Value $probeReceipt

if (Test-Path -LiteralPath $sourcePath -PathType Leaf) {
    [IO.File]::Delete($sourcePath)
}
if ((Test-Path -LiteralPath $workingRoot -PathType Container) -and
    @(Get-ChildItem -LiteralPath $workingRoot -Force).Count -eq 0) {
    [IO.Directory]::Delete($workingRoot, $false)
}

[pscustomobject]@{
    output_root = $destination
    recipe_relative_path = 'codec-recipe.v1.json'
    recipe_sha256 = Get-M0Sha256 -LiteralPath $recipePath
    probe_relative_path = 'codec-probe.v1.json'
    probe_sha256 = Get-M0Sha256 -LiteralPath $probePath
    fixture_count = $fixtureProbeEntries.Count
}
