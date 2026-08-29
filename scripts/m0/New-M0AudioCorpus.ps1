[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $DestinationRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')).TrimEnd('\', '/')
$destination = [IO.Path]::GetFullPath($DestinationRoot).TrimEnd('\', '/')
$outputRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out\m0')).TrimEnd('\', '/')
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$isOutputChild = $destination.StartsWith($outputRoot + [IO.Path]::DirectorySeparatorChar, $comparison)

if (-not $isOutputChild) {
    throw [ArgumentException]::new('Audio corpus generation is limited to fresh staging directories under out/m0.')
}

$relativeDestination = [IO.Path]::GetRelativePath($repositoryRoot, $destination)
$currentPath = $repositoryRoot
foreach ($segment in $relativeDestination.Split(
        [char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar),
        [StringSplitOptions]::RemoveEmptyEntries)) {
    $currentPath = Join-Path $currentPath $segment
    if (-not (Test-Path -LiteralPath $currentPath)) { break }
    $item = Get-Item -LiteralPath $currentPath -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw [IO.InvalidDataException]::new("M0ACG002 links and reparse points are forbidden in the destination path: '$currentPath'.")
    }
}
if (Test-Path -LiteralPath $destination) {
    if (-not (Test-Path -LiteralPath $destination -PathType Container) -or
        @(Get-ChildItem -LiteralPath $destination -Force).Count -ne 0) {
        throw [IO.IOException]::new('M0ACG001 destination must not exist or must be an empty directory.')
    }
}

function Write-M0AudioText {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Content
    )

    $canonical = $Content.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($LiteralPath, $canonical, $utf8NoBom)
}

function Write-M0AudioJson {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Value
    )

    Write-M0AudioText -LiteralPath $LiteralPath -Content ($Value | ConvertTo-Json -Depth 100)
}

function Get-M0AudioSha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function New-M0AbsentTags {
    return [ordered]@{
        state = 'absent'
        title = $null
        artist = $null
        album = $null
        track = $null
        year = $null
    }
}

function New-M0AbsentArtwork {
    return [ordered]@{
        state = 'absent'
        encoded_size_bytes = 0
        width_px = $null
        height_px = $null
        pixel_count = $null
        expected_action = 'none'
    }
}

function New-M0Capabilities {
    param([Parameter(Mandatory)] [bool] $Valid)

    return [ordered]@{
        decodable = $Valid
        duration_known = $Valid
        seekable = $Valid
        tags_present = $false
        artwork_present = $false
        play_to_end = $Valid
    }
}

function New-M0Case {
    param(
        [Parameter(Mandatory)] [string] $CaseId,
        [Parameter(Mandatory)] [string] $Behavior,
        [string[]] $FixtureIds = @(),
        [string[]] $RequirementIds = @('FR-002')
    )

    return [ordered]@{
        case_id = $CaseId
        behavior = $Behavior
        status = if ($FixtureIds.Count -gt 0) { 'fixture_bound' } else { 'catalogued' }
        fixture_ids = @($FixtureIds)
        requirement_ids = @($RequirementIds)
    }
}

function New-M0WaveBytes {
    $sampleRate = 48000
    $sampleCount = 48000
    $bitsPerSample = 16
    $channels = 1
    $blockAlign = [int]($channels * ($bitsPerSample / 8))
    $byteRate = $sampleRate * $blockAlign
    $dataSize = $sampleCount * $blockAlign
    $stream = [IO.MemoryStream]::new(44 + $dataSize)
    $writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::ASCII, $true)
    try {
        $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([uint32](36 + $dataSize))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
        $writer.Write([Text.Encoding]::ASCII.GetBytes('fmt '))
        $writer.Write([uint32]16)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$channels)
        $writer.Write([uint32]$sampleRate)
        $writer.Write([uint32]$byteRate)
        $writer.Write([uint16]$blockAlign)
        $writer.Write([uint16]$bitsPerSample)
        $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([uint32]$dataSize)
        for ($index = 0; $index -lt $sampleCount; $index++) {
            [int16] $sample = if (($index % 96) -lt 48) { 4096 } else { -4096 }
            $writer.Write($sample)
        }
        $writer.Flush()
        return $stream.ToArray()
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function New-M0ArtifactEntry {
    param(
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Role,
        [Parameter(Mandatory)] [string] $MediaType,
        [Parameter(Mandatory)] [string] $Origin
    )

    $path = Join-Path $destination $RelativePath
    return [ordered]@{
        relative_path = $RelativePath.Replace('\', '/')
        role = $Role
        media_type = $MediaType
        size_bytes = (Get-Item -LiteralPath $path).Length
        sha256 = Get-M0AudioSha256 -LiteralPath $path
        origin = $Origin
        license = 'LicenseRef-MusicPlayer-M0-Private'
    }
}

function New-M0FixtureEntry {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $ActionId,
        [Parameter(Mandatory)] [bool] $Valid,
        [AllowNull()] $Container,
        [AllowNull()] $CodecAndProfile,
        [AllowNull()] [Nullable[int]] $DurationMs,
        [AllowNull()] [Nullable[int]] $SampleRateHz,
        [AllowNull()] [Nullable[int]] $Channels,
        [AllowNull()] [Nullable[int]] $BitDepth,
        [Parameter(Mandatory)] [string] $BitrateMode,
        [AllowNull()] $ErrorCode,
        [Parameter(Mandatory)] [string[]] $RequirementIds
    )

    $path = Join-Path $destination $RelativePath
    return [ordered]@{
        id = $Id
        relative_path = $RelativePath.Replace('\', '/')
        sha256 = Get-M0AudioSha256 -LiteralPath $path
        size_bytes = (Get-Item -LiteralPath $path).Length
        source_or_generation_recipe = "M0-AUDIO-RECIPE-1:$ActionId"
        license = 'LicenseRef-MusicPlayer-M0-Private'
        container = $Container
        codec_and_profile = $CodecAndProfile
        duration_ms = $DurationMs
        duration_tolerance_ms = if ($Valid) { 0 } else { 0 }
        sample_rate_hz = $SampleRateHz
        channels = $Channels
        bit_depth_if_known = $BitDepth
        bitrate_mode = $BitrateMode
        expected_tags = New-M0AbsentTags
        expected_artwork = New-M0AbsentArtwork
        expected_capabilities = New-M0Capabilities -Valid $Valid
        expected_outcome = if ($Valid) { 'valid' } else { 'controlled_error' }
        expected_error_code = $ErrorCode
        requirement_ids = @($RequirementIds)
    }
}

$validRoot = Join-Path $destination 'fixtures\valid'
$edgeRoot = Join-Path $destination 'fixtures\edge'
$hostileRoot = Join-Path $destination 'fixtures\hostile'
foreach ($path in @($validRoot, $edgeRoot, $hostileRoot)) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

$wavBytes = New-M0WaveBytes
$wavPath = Join-Path $validRoot 'wav-pcm-mono.wav'
[IO.File]::WriteAllBytes($wavPath, $wavBytes)
[IO.File]::WriteAllBytes((Join-Path $edgeRoot 'wrong-extension-wav-as-mp3.mp3'), $wavBytes)
[IO.File]::WriteAllBytes((Join-Path $hostileRoot 'truncated-wav-pcm-mono.wav'), $wavBytes[0..39])
[IO.File]::WriteAllBytes((Join-Path $hostileRoot 'empty.bin'), [byte[]]::new(0))

$license = @'
LicenseRef-MusicPlayer-M0-Private

Copyright (c) 2026, the Music Player project owner.

This corpus foundation contains only synthetic signal bytes generated by the
repository script. It contains no third-party media, commercial recording, or
personal audio. Private local use, copying, and modification are permitted for
development and testing of this repository. Public redistribution is not
granted until the project owner selects an explicit public license.
'@
Write-M0AudioText -LiteralPath (Join-Path $destination 'LICENSE.assets.txt') -Content $license

$metadataCases = @(
    New-M0Case 'MR-001' 'Read present tags without changing their values.' @() @('FR-021')
    New-M0Case 'MR-002' 'Represent absent tags explicitly.' @('wav-pcm-mono') @('FR-021')
    New-M0Case 'MR-003' 'Use the file name as title fallback when title is absent.' @('wav-pcm-mono') @('FR-021')
    New-M0Case 'MR-004' 'Sanitize invalid text encoding without crashing.' @() @('FR-023')
    New-M0Case 'MR-005' 'Discard corrupt embedded artwork.' @() @('FR-023', 'NFR-LIMIT-001')
    New-M0Case 'MR-006' 'Accept artwork immediately below the encoded-byte limit.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-007' 'Accept artwork exactly at the encoded-byte limit.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-008' 'Replace artwork above the encoded-byte limit with a placeholder.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-009' 'Apply artwork dimension and pixel limits below their boundaries.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-010' 'Apply artwork dimension and pixel limits exactly at their boundaries.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-011' 'Reject artwork beyond dimension or pixel boundaries.' @() @('NFR-LIMIT-001')
    New-M0Case 'MR-012' 'Accept one metadata field exactly at 16 KiB.' @() @('NFR-LIMIT-002')
    New-M0Case 'MR-013' 'Diagnose one metadata field above 16 KiB.' @() @('NFR-LIMIT-002')
    New-M0Case 'MR-014' 'Accept aggregate metadata exactly at 256 KiB.' @() @('NFR-LIMIT-002')
    New-M0Case 'MR-015' 'Diagnose aggregate metadata above 256 KiB.' @() @('NFR-LIMIT-002')
    New-M0Case 'MR-016' 'Discard a result after cancellation.' @() @('FR-024', 'NFR-LIMIT-003')
    New-M0Case 'MR-017' 'Discard a stale generation after a newer load.' @() @('FR-024', 'NFR-LIMIT-003')
    New-M0Case 'MR-018' 'Report technical properties without false zero values.' @('wav-pcm-mono', 'wrong-extension-wav-as-mp3') @('FR-020', 'FR-022')
    New-M0Case 'MR-019' 'Identify media by content when the extension is misleading.' @('wrong-extension-wav-as-mp3') @('FR-005', 'FR-022')
    New-M0Case 'MR-020' 'Respect the two-second job budget without accumulating work.' @() @('FR-024', 'NFR-LIMIT-003')
    New-M0Case 'MR-101' 'Read WAV PCM properties and absent tags.' @('wav-pcm-mono') @('FR-020', 'FR-021', 'FR-022')
    New-M0Case 'MR-102' 'Read WAV content stored with an MP3 extension.' @('wrong-extension-wav-as-mp3') @('FR-005', 'FR-022')
    New-M0Case 'MR-103' 'Return controlled errors for truncated and empty input.' @('empty', 'truncated-wav-pcm-mono') @('FR-005', 'NFR-REL-001')
)

$playbackCases = @(
    New-M0Case 'PB-001' 'A valid load publishes ready and observed capabilities.' @('wav-pcm-mono') @('FR-002', 'FR-010')
    New-M0Case 'PB-002' 'Play is idempotent.' @() @('FR-010')
    New-M0Case 'PB-003' 'Pause is idempotent.' @() @('FR-010')
    New-M0Case 'PB-004' 'A supported seek confirms its destination.' @() @('FR-012')
    New-M0Case 'PB-005' 'A rejected seek returns a typed error without a false snapshot.' @() @('FR-012', 'FR-014')
    New-M0Case 'PB-006' 'End of media is emitted exactly once.' @() @('FR-010')
    New-M0Case 'PB-007' 'Unload is idempotent.' @() @('FR-010')
    New-M0Case 'PB-008' 'Shutdown is idempotent.' @() @('FR-010')
    New-M0Case 'PB-009' 'A failed load does not contaminate the next valid load.' @('empty', 'wav-pcm-mono') @('FR-005', 'FR-010')
    New-M0Case 'PB-010' 'Callbacks preserve the documented event order.' @() @('FR-010')
    New-M0Case 'PB-011' 'Calls after shutdown return a typed failure.' @() @('FR-010')
    New-M0Case 'PB-012' 'Events from an old generation are discarded.' @() @('FR-024', 'NFR-LIMIT-003')
    New-M0Case 'PB-101' 'Load, probe, seek, and finish a valid WAV fixture.' @('wav-pcm-mono') @('FR-002', 'FR-010', 'FR-012')
    New-M0Case 'PB-102' 'Detect playable WAV content behind an MP3 extension.' @('wrong-extension-wav-as-mp3') @('FR-005')
    New-M0Case 'PB-103' 'Keep the process alive after hostile input returns a controlled error.' @('empty', 'truncated-wav-pcm-mono') @('FR-005', 'NFR-REL-001')
)

$cases = [ordered]@{
    schema_id = 'm0.audio-contract-cases.v1'
    schema_version = 1
    catalog_id = 'M0-AUDIO-CONTRACT-CASES-1'
    catalog_status = 'partial'
    ports = @(
        [ordered]@{ port = 'MetadataReader'; cases = $metadataCases }
        [ordered]@{ port = 'PlaybackBackend'; cases = $playbackCases }
    )
}
Write-M0AudioJson -LiteralPath (Join-Path $destination 'cases.v1.json') -Value $cases

$generatorPath = [IO.Path]::GetFullPath($PSCommandPath)
$recipe = [ordered]@{
    schema_id = 'm0.audio-corpus-recipe.v1'
    schema_version = 1
    recipe_id = 'M0-AUDIO-RECIPE-1'
    generator = [ordered]@{
        relative_path = 'scripts/m0/New-M0AudioCorpus.ps1'
        sha256 = Get-M0AudioSha256 -LiteralPath $generatorPath
        size_bytes = (Get-Item -LiteralPath $generatorPath).Length
        runtime = 'PowerShell 7.6.5 with .NET integer and binary APIs'
    }
    network_allowed = $false
    ambient_tool_lookup_allowed = $false
    source_signal = [ordered]@{
        kind = 'integer_square_wave'
        frequency_hz = 500
        amplitude = 4096
        sample_rate_hz = 48000
        channels = 1
        bits_per_sample = 16
        sample_count = 48000
        duration_ms = 1000
    }
    actions = @(
        [ordered]@{ action_id = 'empty'; operation = 'write_empty_file'; output_relative_path = 'fixtures/hostile/empty.bin' }
        [ordered]@{ action_id = 'truncated-wav-pcm-mono'; operation = 'take_prefix_bytes'; output_relative_path = 'fixtures/hostile/truncated-wav-pcm-mono.wav'; input_action_id = 'wav-pcm-mono'; prefix_bytes = 40 }
        [ordered]@{ action_id = 'wav-pcm-mono'; operation = 'write_pcm_wave'; output_relative_path = 'fixtures/valid/wav-pcm-mono.wav' }
        [ordered]@{ action_id = 'wrong-extension-wav-as-mp3'; operation = 'copy_exact_bytes'; output_relative_path = 'fixtures/edge/wrong-extension-wav-as-mp3.mp3'; input_action_id = 'wav-pcm-mono' }
    )
}
Write-M0AudioJson -LiteralPath (Join-Path $destination 'recipe.v1.json') -Value $recipe

$artifacts = @(
    New-M0ArtifactEntry 'LICENSE.assets.txt' 'license' 'text/plain' 'project-authored-synthetic'
    New-M0ArtifactEntry 'cases.v1.json' 'contract_cases' 'application/json' 'generated-from-repository-script'
    New-M0ArtifactEntry 'recipe.v1.json' 'generation_recipe' 'application/json' 'generated-from-repository-script'
)
$fixtures = @(
    New-M0FixtureEntry 'empty' 'fixtures/hostile/empty.bin' 'empty' $false $null $null $null $null $null $null 'not_applicable' 'empty_input' @('FR-005', 'NFR-REL-001')
    New-M0FixtureEntry 'truncated-wav-pcm-mono' 'fixtures/hostile/truncated-wav-pcm-mono.wav' 'truncated-wav-pcm-mono' $false 'WAV' 'PCM signed 16-bit little-endian' $null $null $null $null 'unknown' 'truncated_input' @('FR-005', 'NFR-REL-001')
    New-M0FixtureEntry 'wav-pcm-mono' 'fixtures/valid/wav-pcm-mono.wav' 'wav-pcm-mono' $true 'WAV' 'PCM signed 16-bit little-endian' 1000 48000 1 16 'uncompressed' $null @('FR-002', 'FR-020', 'FR-021', 'FR-022')
    New-M0FixtureEntry 'wrong-extension-wav-as-mp3' 'fixtures/edge/wrong-extension-wav-as-mp3.mp3' 'wrong-extension-wav-as-mp3' $true 'WAV' 'PCM signed 16-bit little-endian' 1000 48000 1 16 'uncompressed' $null @('FR-005', 'FR-020', 'FR-022')
)

$manifest = [ordered]@{
    schema_id = 'm0.audio-corpus-manifest.v1'
    schema_version = 1
    manifest_id = 'M0-AUDIO-CORPUS-1-MANIFEST'
    corpus_id = 'M0-AUDIO-CORPUS-1'
    completion = [ordered]@{
        m0_5 = 'in_progress'
        catalog_status = 'partial'
        published_format_support = $false
    }
    hash_algorithm = 'sha256'
    canonical_text = [ordered]@{
        encoding = 'utf-8'
        bom = $false
        newline = 'lf'
        final_newline_count = 1
    }
    artifacts = $artifacts
    fixtures = $fixtures
}
$manifestPath = Join-Path $destination 'manifest.v1.json'
Write-M0AudioJson -LiteralPath $manifestPath -Value $manifest
$manifestHash = Get-M0AudioSha256 -LiteralPath $manifestPath
Write-M0AudioText -LiteralPath (Join-Path $destination 'manifest.v1.sha256') -Content "$manifestHash  manifest.v1.json"

[pscustomobject]@{
    corpus_root = $destination
    corpus_id = 'M0-AUDIO-CORPUS-1'
    fixture_count = 4
    manifest_sha256 = $manifestHash
}
