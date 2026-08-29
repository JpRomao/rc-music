Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0ACUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0ACPrivateLicense = 'LicenseRef-MusicPlayer-M0-Private'
$script:M0ACCodecRecipeSha256 = 'd3ac52181be002560fbe750d999943ceb5b62e25c203be9eb5863a50474bb884'
$script:M0ACKnownRequirements = @(
    'FR-002',
    'FR-005',
    'FR-010',
    'FR-012',
    'FR-014',
    'FR-020',
    'FR-021',
    'FR-022',
    'FR-023',
    'FR-024',
    'NFR-LIMIT-001',
    'NFR-LIMIT-002',
    'NFR-LIMIT-003',
    'NFR-REL-001',
    'NFR-REL-006'
)
$script:M0ACPlaybackCaseIds = @(
    'PB-001', 'PB-002', 'PB-003', 'PB-004', 'PB-005', 'PB-006',
    'PB-007', 'PB-008', 'PB-009', 'PB-010', 'PB-011', 'PB-012',
    'PB-101', 'PB-102', 'PB-103'
)
$script:M0ACMetadataCaseIds = @(
    'MR-001', 'MR-002', 'MR-003', 'MR-004', 'MR-005', 'MR-006',
    'MR-007', 'MR-008', 'MR-009', 'MR-010', 'MR-011', 'MR-012',
    'MR-013', 'MR-014', 'MR-015', 'MR-016', 'MR-017', 'MR-018',
    'MR-019', 'MR-020', 'MR-101', 'MR-102', 'MR-103'
)
$script:M0ACFoundationFixturePaths = [ordered]@{
    'wrong-extension-wav-as-mp3' = 'fixtures/edge/wrong-extension-wav-as-mp3.mp3'
    'empty' = 'fixtures/hostile/empty.bin'
    'truncated-wav-pcm-mono' = 'fixtures/hostile/truncated-wav-pcm-mono.wav'
    'wav-pcm-mono' = 'fixtures/valid/wav-pcm-mono.wav'
}
$script:M0ACCodecFixturePaths = [ordered]@{
    'flac-16-stereo' = 'fixtures/valid/flac-16-stereo.flac'
    'm4a-aac-lc' = 'fixtures/valid/m4a-aac-lc.m4a'
    'm4a-alac-candidate' = 'fixtures/candidate/m4a-alac-candidate.m4a'
    'mp3-cbr-stereo' = 'fixtures/valid/mp3-cbr-stereo.mp3'
    'mp3-unicode-tags' = 'fixtures/valid/mp3-unicode-tags.mp3'
    'mp3-vbr-stereo' = 'fixtures/valid/mp3-vbr-stereo.mp3'
    'ogg-vorbis' = 'fixtures/valid/ogg-vorbis.ogg'
    'opus-candidate' = 'fixtures/candidate/opus-candidate.opus'
}
$script:M0ACFixturePaths = [ordered]@{}
foreach ($entry in $script:M0ACFoundationFixturePaths.GetEnumerator()) {
    $script:M0ACFixturePaths[$entry.Key] = $entry.Value
}
foreach ($entry in $script:M0ACCodecFixturePaths.GetEnumerator()) {
    $script:M0ACFixturePaths[$entry.Key] = $entry.Value
}
$script:M0ACCodecContracts = [ordered]@{
    'flac-16-stereo' = @('FLAC', 'FLAC 16-bit compression level 5', 'lossless', 16, 20)
    'm4a-aac-lc' = @('ISO MP4/M4A', 'AAC Low Complexity 128 kb/s in ISO BMFF/M4A', 'constant', $null, 25)
    'm4a-alac-candidate' = @('ISO MP4/M4A', 'ALAC 16-bit in ISO BMFF/M4A', 'lossless', 16, 20)
    'mp3-cbr-stereo' = @('MP3', 'MP3 Layer III CBR 128 kb/s', 'constant', $null, 50)
    'mp3-unicode-tags' = @('MP3', 'MP3 Layer III CBR 96 kb/s with ID3v2.4 Unicode tags', 'constant', $null, 50)
    'mp3-vbr-stereo' = @('MP3', 'MP3 Layer III VBR quality 4', 'variable', $null, 50)
    'ogg-vorbis' = @('Ogg', 'Vorbis quality 4 in Ogg', 'variable', $null, 20)
    'opus-candidate' = @('Ogg', 'Opus VBR 96 kb/s in Ogg', 'variable', $null, 20)
}
$script:M0ACCodecProbeContracts = [ordered]@{
    'flac-16-stereo' = [pscustomobject]@{
        format_name = 'flac'; codec_name = 'flac'; profile = $null; sample_format = 's16'
        tag_location = 'format'; bits_per_raw_sample = 16
        decoded_pcm_sha256 = '2f1969d5f3caa79047f0a51aca03e99a7da9d9bb7a9feceecb99c625e992cf36'
    }
    'm4a-aac-lc' = [pscustomobject]@{
        format_name = 'mov,mp4,m4a,3gp,3g2,mj2'; codec_name = 'aac'; profile = 'LC'; sample_format = 'fltp'
        tag_location = 'format'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = '14b73e8a79e31e8ceab643a086a5283a9e85d359317cc60fa2761a5f8d62a1e1'
    }
    'm4a-alac-candidate' = [pscustomobject]@{
        format_name = 'mov,mp4,m4a,3gp,3g2,mj2'; codec_name = 'alac'; profile = $null; sample_format = 's16p'
        tag_location = 'format'; bits_per_raw_sample = 16
        decoded_pcm_sha256 = '2f1969d5f3caa79047f0a51aca03e99a7da9d9bb7a9feceecb99c625e992cf36'
    }
    'mp3-cbr-stereo' = [pscustomobject]@{
        format_name = 'mp3'; codec_name = 'mp3'; profile = $null; sample_format = 'fltp'
        tag_location = 'format'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = 'abd62d8a3cb53fa231dd1d8b6f377e838d385bbe775b320dfac841310b48134e'
    }
    'mp3-unicode-tags' = [pscustomobject]@{
        format_name = 'mp3'; codec_name = 'mp3'; profile = $null; sample_format = 'fltp'
        tag_location = 'format'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = 'a46d3cbab2810241e882d4b493d0ba07f53b3d3df805ab41476e2f217b6a0320'
    }
    'mp3-vbr-stereo' = [pscustomobject]@{
        format_name = 'mp3'; codec_name = 'mp3'; profile = $null; sample_format = 'fltp'
        tag_location = 'format'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = 'faf2d38e9cf339d6bfaa320a87a61493c6d36e719b67aed060580ba748999b80'
    }
    'ogg-vorbis' = [pscustomobject]@{
        format_name = 'ogg'; codec_name = 'vorbis'; profile = $null; sample_format = 'fltp'
        tag_location = 'stream'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = '1ad419ef2c32c7914eff0e89bb9de160fba2137618ccbc07d24b1e8f7aadf6b9'
    }
    'opus-candidate' = [pscustomobject]@{
        format_name = 'ogg'; codec_name = 'opus'; profile = $null; sample_format = 'fltp'
        tag_location = 'stream'; bits_per_raw_sample = $null
        decoded_pcm_sha256 = 'afe443a59c56d8a300355d1e6d44d3da23d59e85e09d898965e6f00ad3e47506'
    }
}

function Get-M0ACSha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Assert-M0ACSafeRoot {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $fullPath = [IO.Path]::GetFullPath($LiteralPath).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    if (-not (Test-Path -LiteralPath $fullPath -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0AC001 $Name is missing: '$fullPath'.")
    }
    $item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw [IO.InvalidDataException]::new("M0AC001 $Name cannot be a link or reparse point: '$fullPath'.")
    }
    return $fullPath
}

function Assert-M0ACNoReparseDescendants {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Name
    )

    foreach ($item in @(Get-ChildItem -LiteralPath $Root -Recurse -Force -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new(
                "M0AC001 $Name contains a link or reparse point: '$($item.FullName)'."
            )
        }
    }
}

function Test-M0ACSafeRelativePath {
    param([Parameter(Mandatory)] [string] $RelativePath)

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains('\') -or
        $RelativePath.Contains(':') -or
        $RelativePath.StartsWith('/', [StringComparison]::Ordinal)) {
        return $false
    }
    $segments = $RelativePath.Split('/')
    if ($segments.Count -eq 0 -or $segments -contains '' -or
        $segments -contains '.' -or $segments -contains '..') {
        return $false
    }
    foreach ($segment in $segments) {
        if ($segment -notmatch '^[A-Za-z0-9._-]+$') {
            return $false
        }
    }
    return $true
}

function Resolve-M0ACContainedPath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath
    )

    if (-not (Test-M0ACSafeRelativePath -RelativePath $RelativePath)) {
        throw [IO.InvalidDataException]::new("M0AC005 unsafe path '$RelativePath'.")
    }

    $base = [IO.Path]::GetFullPath($Root).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    )
    $nativeRelative = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $candidate = [IO.Path]::GetFullPath((Join-Path $base $nativeRelative))
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $candidate.StartsWith($base + [IO.Path]::DirectorySeparatorChar, $comparison)) {
        throw [IO.InvalidDataException]::new("M0AC005 unsafe path escapes its root: '$RelativePath'.")
    }

    $current = $base
    foreach ($segment in $RelativePath.Split('/')) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new(
                "M0AC001 unsafe path crosses a link or reparse point: '$RelativePath'."
            )
        }
    }
    return $candidate
}

function Read-M0ACCanonicalText {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -ge 3 -and
        $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw [IO.InvalidDataException]::new("M0AC002 $Name must not contain a UTF-8 BOM.")
    }
    try {
        $text = $script:M0ACUtf8.GetString($bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0AC002 $Name is not strict UTF-8.", $_.Exception)
    }
    if ($text.Contains("`r") -or
        -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($text.Length -ge 2 -and $text[$text.Length - 2] -eq "`n")) {
        throw [IO.InvalidDataException]::new(
            "M0AC002 $Name must use LF and exactly one final newline."
        )
    }
    return $text
}

function Assert-M0ACNoDuplicateJsonProperties {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw [IO.InvalidDataException]::new(
                    "M0AC003 duplicate JSON property '$($property.Name)' at $Context."
                )
            }
            Assert-M0ACNoDuplicateJsonProperties -Element $property.Value `
                -Context "$Context.$($property.Name)"
        }
        return
    }
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($value in $Element.EnumerateArray()) {
            Assert-M0ACNoDuplicateJsonProperties -Element $value -Context "$Context[$index]"
            $index++
        }
    }
}

function Assert-M0ACJsonHasNoDuplicateProperties {
    param(
        [Parameter(Mandatory)] [string] $Json,
        [Parameter(Mandatory)] [string] $Name
    )

    $document = $null
    try {
        $document = [Text.Json.JsonDocument]::Parse($Json)
        Assert-M0ACNoDuplicateJsonProperties -Element $document.RootElement -Context '$'
    }
    catch [IO.InvalidDataException] {
        throw
    }
    catch {
        throw [IO.InvalidDataException]::new("M0AC003 $Name is not valid JSON.", $_.Exception)
    }
    finally {
        if ($null -ne $document) {
            $document.Dispose()
        }
    }
}

function Read-M0ACJsonDocument {
    param(
        [Parameter(Mandatory)] [string] $DocumentPath,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $json = Read-M0ACCanonicalText -LiteralPath $DocumentPath -Name $Name
    Assert-M0ACJsonHasNoDuplicateProperties -Json $json -Name $Name
    $schemaName = "schema for $Name"
    $schema = Read-M0ACCanonicalText -LiteralPath $SchemaPath -Name $schemaName
    Assert-M0ACJsonHasNoDuplicateProperties -Json $schema -Name $schemaName
    try {
        $valid = Test-Json -Json $json -Schema $schema -ErrorAction SilentlyContinue
    }
    catch {
        throw [IO.InvalidDataException]::new("M0AC003 $Name schema validation failed.", $_.Exception)
    }
    if (-not $valid) {
        throw [IO.InvalidDataException]::new("M0AC003 $Name failed its closed JSON schema.")
    }
    try {
        return ($json | ConvertFrom-Json -Depth 100)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0AC003 $Name could not be decoded.", $_.Exception)
    }
}

function Assert-M0ACOrdinalSequence {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Values,
        [Parameter(Mandatory)] [string] $Name
    )

    [string[]] $actual = @($Values | ForEach-Object { [string]$_ })
    [string[]] $sorted = [string[]]$actual.Clone()
    [Array]::Sort($sorted, [StringComparer]::Ordinal)
    if ($actual.Count -ne $sorted.Count) {
        throw [IO.InvalidDataException]::new("M0AC009 $Name count differs while ordering.")
    }
    for ($index = 0; $index -lt $actual.Count; $index++) {
        if ($actual[$index] -cne $sorted[$index]) {
            throw [IO.InvalidDataException]::new("M0AC009 $Name is not in ordinal order at index $index.")
        }
    }
}

function Assert-M0ACExactSequence {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [IO.InvalidDataException]::new(
            "M0AC009 $Name count differs: expected $($Expected.Count), got $($Actual.Count)."
        )
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ([string]$Actual[$index] -cne [string]$Expected[$index]) {
            throw [IO.InvalidDataException]::new("M0AC009 $Name differs at index $index.")
        }
    }
}

function Assert-M0ACUniqueStrings {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Values,
        [Parameter(Mandatory)] [string] $Name,
        [StringComparer] $Comparer = [StringComparer]::Ordinal
    )

    $seen = [Collections.Generic.HashSet[string]]::new($Comparer)
    foreach ($value in $Values) {
        if (-not $seen.Add([string]$value)) {
            throw [IO.InvalidDataException]::new("M0AC009 $Name contains duplicate '$value'.")
        }
    }
}

function Assert-M0ACKnownRequirements {
    param(
        [Parameter(Mandatory)] [object[]] $RequirementIds,
        [Parameter(Mandatory)] [string] $Name
    )

    Assert-M0ACOrdinalSequence -Values $RequirementIds -Name "$Name requirement IDs"
    Assert-M0ACUniqueStrings -Values $RequirementIds -Name "$Name requirement IDs"
    foreach ($requirementId in $RequirementIds) {
        if ([string]$requirementId -cnotin $script:M0ACKnownRequirements) {
            throw [IO.InvalidDataException]::new(
                "M0AC009 $Name references requirement '$requirementId' outside M0.5."
            )
        }
    }
}

function Get-M0ACOptionalProperty {
    param(
        [Parameter(Mandatory)] [psobject] $InputObject,
        [Parameter(Mandatory)] [string] $Name
    )

    $property = $InputObject.PSObject.Properties[$Name]
    if ($null -eq $property) {
        return $null
    }
    return $property.Value
}

function Read-M0ACUInt16LittleEndian {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [int] $Offset
    )

    if ($Offset -lt 0 -or $Offset + 2 -gt $Bytes.Length) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV UInt16 read exceeds the file.')
    }
    return [int](([long]$Bytes[$Offset]) -bor (([long]$Bytes[$Offset + 1]) -shl 8))
}

function Read-M0ACUInt32LittleEndian {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [int] $Offset
    )

    if ($Offset -lt 0 -or $Offset + 4 -gt $Bytes.Length) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV UInt32 read exceeds the file.')
    }
    return [long](
        ([long]$Bytes[$Offset]) -bor
        (([long]$Bytes[$Offset + 1]) -shl 8) -bor
        (([long]$Bytes[$Offset + 2]) -shl 16) -bor
        (([long]$Bytes[$Offset + 3]) -shl 24)
    )
}

function Read-M0WavePcmContract {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0AC012 WAV file is missing: '$fullPath'.")
    }
    $item = Get-Item -LiteralPath $fullPath -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw [IO.InvalidDataException]::new("M0AC012 WAV file cannot be a link or reparse point: '$fullPath'.")
    }

    $bytes = [IO.File]::ReadAllBytes($fullPath)
    if ($bytes.Length -lt 44 -or
        [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -cne 'RIFF' -or
        [Text.Encoding]::ASCII.GetString($bytes, 8, 4) -cne 'WAVE') {
        throw [IO.InvalidDataException]::new('M0AC012 file is not a complete RIFF/WAVE stream.')
    }
    $riffSize = Read-M0ACUInt32LittleEndian -Bytes $bytes -Offset 4
    if ($riffSize -ne $bytes.Length - 8) {
        throw [IO.InvalidDataException]::new('M0AC012 RIFF size does not match the exact file length.')
    }

    $format = $null
    $data = $null
    $offset = 12
    while ($offset -lt $bytes.Length) {
        if ($offset + 8 -gt $bytes.Length) {
            throw [IO.InvalidDataException]::new('M0AC012 WAV ends inside a chunk header.')
        }
        $chunkId = [Text.Encoding]::ASCII.GetString($bytes, $offset, 4)
        $chunkSize = Read-M0ACUInt32LittleEndian -Bytes $bytes -Offset ($offset + 4)
        $chunkDataOffset = $offset + 8
        $chunkEnd = $chunkDataOffset + $chunkSize
        if ($chunkEnd -gt $bytes.Length) {
            throw [IO.InvalidDataException]::new("M0AC012 WAV chunk '$chunkId' exceeds the file.")
        }
        if ($chunkId -ceq 'fmt ') {
            if ($null -ne $format) {
                throw [IO.InvalidDataException]::new('M0AC012 WAV contains duplicate fmt chunks.')
            }
            if ($chunkSize -ne 16) {
                throw [IO.InvalidDataException]::new('M0AC012 WAV PCM fmt chunk must contain exactly 16 bytes.')
            }
            $format = [pscustomobject][ordered]@{
                header_offset = $offset
                audio_format = Read-M0ACUInt16LittleEndian -Bytes $bytes -Offset $chunkDataOffset
                channels = Read-M0ACUInt16LittleEndian -Bytes $bytes -Offset ($chunkDataOffset + 2)
                sample_rate_hz = Read-M0ACUInt32LittleEndian -Bytes $bytes -Offset ($chunkDataOffset + 4)
                byte_rate = Read-M0ACUInt32LittleEndian -Bytes $bytes -Offset ($chunkDataOffset + 8)
                block_align = Read-M0ACUInt16LittleEndian -Bytes $bytes -Offset ($chunkDataOffset + 12)
                bits_per_sample = Read-M0ACUInt16LittleEndian -Bytes $bytes -Offset ($chunkDataOffset + 14)
            }
        }
        elseif ($chunkId -ceq 'data') {
            if ($null -ne $data) {
                throw [IO.InvalidDataException]::new('M0AC012 WAV contains duplicate data chunks.')
            }
            $data = [pscustomobject][ordered]@{
                header_offset = $offset
                data_offset = $chunkDataOffset
                size_bytes = $chunkSize
            }
        }
        $offset = [int]($chunkEnd + ($chunkSize % 2))
    }
    if ($offset -ne $bytes.Length -or $null -eq $format -or $null -eq $data) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV chunk closure is incomplete.')
    }
    if ($format.header_offset -ne 12 -or $data.header_offset -ne 36 -or
        $format.audio_format -ne 1 -or $format.channels -ne 1 -or
        $format.sample_rate_hz -ne 48000 -or $format.bits_per_sample -ne 16 -or
        $format.block_align -ne 2 -or $format.byte_rate -ne 96000 -or
        $data.size_bytes -ne 96000 -or $bytes.Length -ne 96044) {
        throw [IO.InvalidDataException]::new(
            'M0AC012 WAV differs from the canonical mono 48 kHz signed 16-bit PCM contract.'
        )
    }
    if (($data.size_bytes % $format.block_align) -ne 0) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV data size is not block aligned.')
    }
    $sampleCount = [long]($data.size_bytes / $format.block_align)
    if ($sampleCount -ne 48000) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV must contain exactly 48000 mono samples.')
    }
    $durationMs = [long](($sampleCount * 1000) / $format.sample_rate_hz)
    if ($durationMs -ne 1000) {
        throw [IO.InvalidDataException]::new('M0AC012 WAV duration must be exactly 1000 ms.')
    }

    return [pscustomobject][ordered]@{
        audio_format = [int]$format.audio_format
        channels = [int]$format.channels
        sample_rate_hz = [int]$format.sample_rate_hz
        byte_rate = [int]$format.byte_rate
        block_align = [int]$format.block_align
        bits_per_sample = [int]$format.bits_per_sample
        sample_count = $sampleCount
        duration_ms = $durationMs
        data_size_bytes = [long]$data.size_bytes
        file_size_bytes = [long]$bytes.Length
        data_offset = [int]$data.data_offset
    }
}

function Assert-M0ACByteSequenceEqual {
    param(
        [Parameter(Mandatory)] [byte[]] $Actual,
        [Parameter(Mandatory)] [byte[]] $Expected,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Actual.Length -ne $Expected.Length) {
        throw [IO.InvalidDataException]::new("M0AC013 $Name byte length differs.")
    }
    for ($index = 0; $index -lt $Expected.Length; $index++) {
        if ($Actual[$index] -ne $Expected[$index]) {
            throw [IO.InvalidDataException]::new("M0AC013 $Name differs at byte $index.")
        }
    }
}

function Assert-M0ACFixtureMetadata {
    param([Parameter(Mandatory)] [psobject] $Fixture)

    $fixtureId = [string]$Fixture.id
    Assert-M0ACKnownRequirements -RequirementIds @($Fixture.requirement_ids) -Name "fixture '$fixtureId'"
    if ($Fixture.license -cne $script:M0ACPrivateLicense) {
        throw [IO.InvalidDataException]::new("M0AC008 fixture '$fixtureId' is not privately licensed.")
    }

    $isHostile = $fixtureId -in @('empty', 'truncated-wav-pcm-mono')
    $isFoundationWav = $fixtureId -in @('wav-pcm-mono', 'wrong-extension-wav-as-mp3')
    $isCodec = $script:M0ACCodecContracts.Contains($fixtureId)
    if (-not $isHostile -and -not $isFoundationWav -and -not $isCodec) {
        throw [IO.InvalidDataException]::new("M0AC014 fixture '$fixtureId' has no closed metadata contract.")
    }

    $capabilities = $Fixture.expected_capabilities
    if ($isHostile) {
        if ($Fixture.expected_outcome -cne 'controlled_error' -or
            [string]::IsNullOrWhiteSpace([string]$Fixture.expected_error_code) -or
            $null -ne $Fixture.duration_ms -or $null -ne $Fixture.sample_rate_hz -or
            $null -ne $Fixture.channels -or $null -ne $Fixture.bit_depth_if_known -or
            [int]$Fixture.duration_tolerance_ms -ne 0 -or
            $capabilities.decodable -or $capabilities.duration_known -or $capabilities.seekable -or
            $capabilities.tags_present -or $capabilities.artwork_present -or $capabilities.play_to_end) {
            throw [IO.InvalidDataException]::new("M0AC014 hostile fixture '$fixtureId' metadata differs.")
        }
    }
    elseif ($isFoundationWav) {
        if ($Fixture.expected_outcome -cne 'valid' -or $null -ne $Fixture.expected_error_code -or
            $Fixture.container -cne 'WAV' -or
            [string]$Fixture.codec_and_profile -notmatch '(?i)pcm' -or
            [long]$Fixture.duration_ms -ne 1000 -or [int]$Fixture.duration_tolerance_ms -ne 0 -or
            [long]$Fixture.sample_rate_hz -ne 48000 -or [int]$Fixture.channels -ne 1 -or
            [int]$Fixture.bit_depth_if_known -ne 16 -or $Fixture.bitrate_mode -cne 'uncompressed' -or
            -not $capabilities.decodable -or -not $capabilities.duration_known -or
            -not $capabilities.seekable -or $capabilities.tags_present -or
            $capabilities.artwork_present -or -not $capabilities.play_to_end) {
            throw [IO.InvalidDataException]::new("M0AC014 valid WAV fixture '$fixtureId' metadata differs.")
        }
    }
    else {
        $contract = $script:M0ACCodecContracts[$fixtureId]
        $expectedBitDepth = $contract[3]
        $bitDepthMatches = if ($null -eq $expectedBitDepth) {
            $null -eq $Fixture.bit_depth_if_known
        }
        else {
            [int]$Fixture.bit_depth_if_known -eq [int]$expectedBitDepth
        }
        if ($Fixture.expected_outcome -cne 'valid' -or $null -ne $Fixture.expected_error_code -or
            $Fixture.container -cne $contract[0] -or $Fixture.codec_and_profile -cne $contract[1] -or
            [long]$Fixture.duration_ms -ne 2000 -or
            [int]$Fixture.duration_tolerance_ms -ne [int]$contract[4] -or
            [long]$Fixture.sample_rate_hz -ne 48000 -or [int]$Fixture.channels -ne 2 -or
            -not $bitDepthMatches -or $Fixture.bitrate_mode -cne $contract[2] -or
            -not $capabilities.decodable -or -not $capabilities.duration_known -or
            -not $capabilities.seekable -or -not $capabilities.tags_present -or
            $capabilities.artwork_present -or -not $capabilities.play_to_end) {
            throw [IO.InvalidDataException]::new("M0AC014 codec fixture '$fixtureId' metadata differs.")
        }
    }

    $tags = $Fixture.expected_tags
    if ($isCodec) {
        if ($tags.state -cne 'present' -or
            $tags.title -cne 'Canção determinística 日本語 🎵' -or
            $tags.artist -cne 'João – Codex' -or
            $tags.album -cne 'Álbum de teste Ω' -or
            $null -ne $tags.track -or [int]$tags.year -ne 2026) {
            throw [IO.InvalidDataException]::new("M0AC014 codec fixture '$fixtureId' tag expectation differs.")
        }
    }
    elseif ($tags.state -cne 'absent' -or $null -ne $tags.title -or $null -ne $tags.artist -or
        $null -ne $tags.album -or $null -ne $tags.track -or $null -ne $tags.year) {
        throw [IO.InvalidDataException]::new("M0AC014 fixture '$fixtureId' tag expectation differs.")
    }

    $artwork = $Fixture.expected_artwork
    if ($artwork.state -cne 'absent' -or [long]$artwork.encoded_size_bytes -ne 0 -or
        $null -ne $artwork.width_px -or $null -ne $artwork.height_px -or
        $null -ne $artwork.pixel_count -or $artwork.expected_action -cne 'none') {
        throw [IO.InvalidDataException]::new("M0AC014 fixture '$fixtureId' artwork expectation differs.")
    }
}

function Assert-M0ACCaseCatalog {
    param(
        [Parameter(Mandatory)] [psobject] $Catalog,
        [Parameter(Mandatory)] [psobject] $Manifest
    )

    if ($Catalog.catalog_status -cne $Manifest.completion.catalog_status) {
        throw [IO.InvalidDataException]::new('M0AC010 case catalog status differs from the manifest.')
    }
    Assert-M0ACExactSequence -Actual @($Catalog.ports.port) `
        -Expected @('MetadataReader', 'PlaybackBackend') -Name 'case catalog ports'

    $fixtureIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($fixture in $Manifest.fixtures) {
        $null = $fixtureIds.Add([string]$fixture.id)
    }
    $boundFixtureIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $allCaseIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($portEntry in $Catalog.ports) {
        $expectedIds = if ($portEntry.port -ceq 'MetadataReader') {
            $script:M0ACMetadataCaseIds
        }
        else {
            $script:M0ACPlaybackCaseIds
        }
        Assert-M0ACExactSequence -Actual @($portEntry.cases.case_id) -Expected $expectedIds `
            -Name "$($portEntry.port) canonical case IDs"
        foreach ($case in $portEntry.cases) {
            if (-not $allCaseIds.Add([string]$case.case_id)) {
                throw [IO.InvalidDataException]::new("M0AC010 duplicate case ID '$($case.case_id)'.")
            }
            $expectedPrefix = if ($portEntry.port -ceq 'MetadataReader') { 'MR-' } else { 'PB-' }
            if (-not ([string]$case.case_id).StartsWith($expectedPrefix, [StringComparison]::Ordinal)) {
                throw [IO.InvalidDataException]::new(
                    "M0AC010 case '$($case.case_id)' is assigned to the wrong port."
                )
            }
            $caseFixtureIds = @($case.fixture_ids)
            Assert-M0ACOrdinalSequence -Values $caseFixtureIds -Name "case '$($case.case_id)' fixture IDs"
            Assert-M0ACUniqueStrings -Values $caseFixtureIds -Name "case '$($case.case_id)' fixture IDs"
            Assert-M0ACKnownRequirements -RequirementIds @($case.requirement_ids) -Name "case '$($case.case_id)'"
            if (($caseFixtureIds.Count -eq 0 -and $case.status -cne 'catalogued') -or
                ($caseFixtureIds.Count -gt 0 -and $case.status -cne 'fixture_bound')) {
                throw [IO.InvalidDataException]::new(
                    "M0AC010 case '$($case.case_id)' status does not match its fixture bindings."
                )
            }
            foreach ($fixtureId in $caseFixtureIds) {
                if (-not $fixtureIds.Contains([string]$fixtureId)) {
                    throw [IO.InvalidDataException]::new(
                        "M0AC010 case '$($case.case_id)' references unknown fixture '$fixtureId'."
                    )
                }
                $null = $boundFixtureIds.Add([string]$fixtureId)
            }
        }
    }
    foreach ($fixtureId in $fixtureIds) {
        if (-not $boundFixtureIds.Contains($fixtureId)) {
            throw [IO.InvalidDataException]::new("M0AC010 fixture '$fixtureId' has no canonical case binding.")
        }
    }
}

function Get-M0ACRepositoryRootFromSchemaRoot {
    param([Parameter(Mandatory)] [string] $SchemaRoot)

    $candidate = [IO.Path]::GetFullPath((Join-Path $SchemaRoot '..\..\..\..'))
    $expected = [IO.Path]::GetFullPath((Join-Path $candidate 'experiments\m0\audio-corpus\schemas'))
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $expected.Equals([IO.Path]::GetFullPath($SchemaRoot), $comparison)) {
        throw [IO.InvalidDataException]::new('M0AC011 schema root is outside the expected repository layout.')
    }
    return $candidate
}

function Assert-M0ACRecipe {
    param(
        [Parameter(Mandatory)] [psobject] $Recipe,
        [Parameter(Mandatory)] [psobject] $Manifest,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    $actionIds = @($Recipe.actions.action_id)
    Assert-M0ACOrdinalSequence -Values $actionIds -Name 'recipe action IDs'
    Assert-M0ACUniqueStrings -Values $actionIds -Name 'recipe action IDs'
    Assert-M0ACUniqueStrings -Values @($Recipe.actions.output_relative_path) -Name 'recipe output paths' `
        -Comparer $(if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal })

    $actionsById = @{}
    $actionsByOperation = @{}
    foreach ($action in $Recipe.actions) {
        if (-not (Test-M0ACSafeRelativePath -RelativePath $action.output_relative_path)) {
            throw [IO.InvalidDataException]::new(
                "M0AC011 recipe action '$($action.action_id)' has an unsafe path."
            )
        }
        $actionsById[[string]$action.action_id] = $action
        if ($actionsByOperation.ContainsKey([string]$action.operation)) {
            throw [IO.InvalidDataException]::new("M0AC011 duplicate recipe operation '$($action.operation)'.")
        }
        $actionsByOperation[[string]$action.operation] = $action
    }
    foreach ($operation in @('write_pcm_wave', 'copy_exact_bytes', 'take_prefix_bytes', 'write_empty_file')) {
        if (-not $actionsByOperation.ContainsKey($operation)) {
            throw [IO.InvalidDataException]::new("M0AC011 recipe is missing operation '$operation'.")
        }
    }

    $waveAction = $actionsByOperation['write_pcm_wave']
    $copyAction = $actionsByOperation['copy_exact_bytes']
    $prefixAction = $actionsByOperation['take_prefix_bytes']
    $emptyAction = $actionsByOperation['write_empty_file']
    if ($waveAction.output_relative_path -cne $script:M0ACFixturePaths['wav-pcm-mono'] -or
        $copyAction.output_relative_path -cne $script:M0ACFixturePaths['wrong-extension-wav-as-mp3'] -or
        $prefixAction.output_relative_path -cne $script:M0ACFixturePaths['truncated-wav-pcm-mono'] -or
        $emptyAction.output_relative_path -cne $script:M0ACFixturePaths['empty']) {
        throw [IO.InvalidDataException]::new('M0AC011 recipe outputs differ from the closed fixture paths.')
    }
    if ($null -ne (Get-M0ACOptionalProperty $waveAction 'input_action_id') -or
        $null -ne (Get-M0ACOptionalProperty $waveAction 'prefix_bytes') -or
        $null -ne (Get-M0ACOptionalProperty $emptyAction 'input_action_id') -or
        $null -ne (Get-M0ACOptionalProperty $emptyAction 'prefix_bytes') -or
        [string](Get-M0ACOptionalProperty $copyAction 'input_action_id') -cne [string]$waveAction.action_id -or
        $null -ne (Get-M0ACOptionalProperty $copyAction 'prefix_bytes') -or
        [string](Get-M0ACOptionalProperty $prefixAction 'input_action_id') -cne [string]$waveAction.action_id -or
        [int](Get-M0ACOptionalProperty $prefixAction 'prefix_bytes') -ne 40) {
        throw [IO.InvalidDataException]::new('M0AC011 recipe dependency/parameter closure differs.')
    }

    $prefix = "$($Recipe.recipe_id):"
    foreach ($fixture in $Manifest.fixtures) {
        if (-not $script:M0ACFoundationFixturePaths.Contains([string]$fixture.id)) {
            continue
        }
        if (-not ([string]$fixture.source_or_generation_recipe).StartsWith($prefix, [StringComparison]::Ordinal)) {
            throw [IO.InvalidDataException]::new(
                "M0AC011 foundation fixture '$($fixture.id)' references another recipe."
            )
        }
        $actionId = ([string]$fixture.source_or_generation_recipe).Substring($prefix.Length)
        if (-not $actionsById.ContainsKey($actionId) -or
            $actionsById[$actionId].output_relative_path -cne $fixture.relative_path) {
            throw [IO.InvalidDataException]::new(
                "M0AC011 fixture '$($fixture.id)' recipe action does not produce its path."
            )
        }
    }

    $generatorPath = Resolve-M0ACContainedPath -Root $RepositoryRoot `
        -RelativePath ([string]$Recipe.generator.relative_path)
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new('M0AC011 recipe generator is missing.')
    }
    $generator = Get-Item -LiteralPath $generatorPath -Force -ErrorAction Stop
    if ($generator.Length -ne [long]$Recipe.generator.size_bytes) {
        throw [IO.InvalidDataException]::new('M0AC011 recipe generator size mismatch.')
    }
    if ((Get-M0ACSha256 -LiteralPath $generatorPath) -cne [string]$Recipe.generator.sha256) {
        throw [IO.InvalidDataException]::new('M0AC011 recipe generator hash mismatch.')
    }
}

function Assert-M0ACIntegerSquareWave {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    for ($sampleIndex = 0; $sampleIndex -lt 48000; $sampleIndex++) {
        $raw = Read-M0ACUInt16LittleEndian -Bytes $bytes -Offset (44 + ($sampleIndex * 2))
        $actual = if ($raw -ge 32768) { $raw - 65536 } else { $raw }
        $expected = if (($sampleIndex % 96) -lt 48) { 4096 } else { -4096 }
        if ($actual -ne $expected) {
            throw [IO.InvalidDataException]::new(
                "M0AC012 integer square-wave sample differs at index $sampleIndex."
            )
        }
    }
}

function Assert-M0ACCodecRecipeAndProbe {
    param(
        [Parameter(Mandatory)] [psobject] $Recipe,
        [Parameter(Mandatory)] [psobject] $Probe,
        [Parameter(Mandatory)] [psobject] $Manifest,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    $expectedIds = [string[]]@($script:M0ACCodecFixturePaths.Keys)
    [Array]::Sort($expectedIds, [StringComparer]::Ordinal)
    $actionIds = [string[]]@($Recipe.actions.fixture_id)
    [Array]::Sort($actionIds, [StringComparer]::Ordinal)
    Assert-M0ACExactSequence -Actual $actionIds -Expected $expectedIds -Name 'codec recipe fixture IDs'
    Assert-M0ACUniqueStrings -Values @($Recipe.actions.output_relative_path) `
        -Name 'codec recipe output paths' `
        -Comparer $(if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal })

    $probeIds = [string[]]@($Probe.fixtures.fixture_id)
    [Array]::Sort($probeIds, [StringComparer]::Ordinal)
    Assert-M0ACExactSequence -Actual $probeIds -Expected $expectedIds -Name 'codec probe fixture IDs'
    Assert-M0ACUniqueStrings -Values @($Probe.fixtures.relative_path) `
        -Name 'codec probe paths' `
        -Comparer $(if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal })

    $generatorPath = Resolve-M0ACContainedPath -Root $RepositoryRoot `
        -RelativePath ([string]$Recipe.generator.relative_path)
    if (-not (Test-Path -LiteralPath $generatorPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new('M0AC016 codec recipe generator is missing.')
    }
    $generator = Get-Item -LiteralPath $generatorPath -Force -ErrorAction Stop
    if ($generator.Length -ne [long]$Recipe.generator.size_bytes -or
        (Get-M0ACSha256 -LiteralPath $generatorPath) -cne [string]$Recipe.generator.sha256) {
        throw [IO.InvalidDataException]::new('M0AC016 codec recipe generator identity differs.')
    }

    $actionsById = @{}
    foreach ($action in $Recipe.actions) {
        $fixtureId = [string]$action.fixture_id
        if ($actionsById.ContainsKey($fixtureId)) {
            throw [IO.InvalidDataException]::new("M0AC016 duplicate codec recipe fixture '$fixtureId'.")
        }
        if (-not (Test-M0ACSafeRelativePath -RelativePath ([string]$action.output_relative_path))) {
            throw [IO.InvalidDataException]::new("M0AC016 unsafe codec recipe path for '$fixtureId'.")
        }
        $actionsById[$fixtureId] = $action
    }
    $probeById = @{}
    foreach ($entry in $Probe.fixtures) {
        $fixtureId = [string]$entry.fixture_id
        if ($probeById.ContainsKey($fixtureId)) {
            throw [IO.InvalidDataException]::new("M0AC016 duplicate codec probe fixture '$fixtureId'.")
        }
        if (-not (Test-M0ACSafeRelativePath -RelativePath ([string]$entry.relative_path))) {
            throw [IO.InvalidDataException]::new("M0AC016 unsafe codec probe path for '$fixtureId'.")
        }
        $probeById[$fixtureId] = $entry
    }
    $manifestById = @{}
    foreach ($fixture in $Manifest.fixtures) {
        $manifestById[[string]$fixture.id] = $fixture
    }

    foreach ($fixtureId in $expectedIds) {
        $action = $actionsById[$fixtureId]
        $entry = $probeById[$fixtureId]
        $fixture = $manifestById[$fixtureId]
        $expectedStatus = if ($fixtureId -in @('m4a-alac-candidate', 'opus-candidate')) {
            'candidate'
        }
        else {
            'valid'
        }
        $probeContract = $script:M0ACCodecProbeContracts[$fixtureId]
        $profileMatches = if ($null -eq $probeContract.profile) {
            $null -eq $entry.profile
        }
        else {
            $entry.profile -ceq $probeContract.profile
        }
        $rawBitsMatch = if ($null -eq $probeContract.bits_per_raw_sample) {
            $null -eq $entry.bits_per_raw_sample
        }
        else {
            [int]$entry.bits_per_raw_sample -eq [int]$probeContract.bits_per_raw_sample
        }
        $expectedSource = "$($Recipe.recipe_id):$fixtureId"
        if ($action.status -cne $expectedStatus -or $entry.status -cne $expectedStatus -or
            $action.output_relative_path -cne $script:M0ACCodecFixturePaths[$fixtureId] -or
            $entry.relative_path -cne $action.output_relative_path -or
            $fixture.relative_path -cne $action.output_relative_path -or
            $fixture.source_or_generation_recipe -cne $expectedSource -or
            $fixture.codec_and_profile -cne $action.codec_and_profile -or
            $fixture.bitrate_mode -cne $action.bitrate_mode -or
            $entry.format_name -cne $probeContract.format_name -or
            $entry.codec_name -cne $probeContract.codec_name -or
            -not $profileMatches -or $entry.sample_format -cne $probeContract.sample_format -or
            $entry.tag_location -cne $probeContract.tag_location -or -not $rawBitsMatch -or
            $entry.decoded_pcm_sha256 -cne $probeContract.decoded_pcm_sha256 -or
            [long]$fixture.size_bytes -ne [long]$entry.size_bytes -or
            $fixture.sha256 -cne $entry.sha256 -or
            [Math]::Abs([double]$entry.container_duration_ms - [double]$fixture.duration_ms) -gt
                [double]$fixture.duration_tolerance_ms) {
            throw [IO.InvalidDataException]::new("M0AC016 codec recipe/probe/manifest differ for '$fixtureId'.")
        }
    }

    if ($Probe.recipe.relative_path -cne 'codec-recipe.v1.json') {
        throw [IO.InvalidDataException]::new('M0AC016 codec probe recipe path differs.')
    }
    $codecRecipeArtifact = @($Manifest.artifacts | Where-Object role -ceq 'codec_generation_recipe')
    if ($codecRecipeArtifact.Count -ne 1 -or
        [long]$Probe.recipe.size_bytes -ne [long]$codecRecipeArtifact[0].size_bytes -or
        $Probe.recipe.sha256 -cne $codecRecipeArtifact[0].sha256) {
        throw [IO.InvalidDataException]::new('M0AC016 codec probe does not authenticate its recipe artifact.')
    }
    if ($codecRecipeArtifact[0].sha256 -cne $script:M0ACCodecRecipeSha256) {
        throw [IO.InvalidDataException]::new('M0AC016 codec recipe arguments or semantic identity differ.')
    }
}

function Assert-M0ACCodecSignature {
    param(
        [Parameter(Mandatory)] [string] $FixtureId,
        [Parameter(Mandatory)] [string] $LiteralPath
    )

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -lt 16) {
        throw [IO.InvalidDataException]::new("M0AC015 codec fixture '$FixtureId' is too small.")
    }
    $asciiPrefixLength = [Math]::Min($bytes.Length, 128)
    $asciiPrefix = [Text.Encoding]::ASCII.GetString($bytes, 0, $asciiPrefixLength)
    $matches = switch -Regex ($FixtureId) {
        '^mp3-' {
            [Text.Encoding]::ASCII.GetString($bytes, 0, 3) -ceq 'ID3'
            break
        }
        '^m4a-' {
            [Text.Encoding]::ASCII.GetString($bytes, 4, 4) -ceq 'ftyp'
            break
        }
        '^flac-' {
            [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ceq 'fLaC'
            break
        }
        '^ogg-' {
            [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ceq 'OggS' -and
                $asciiPrefix.Contains('vorbis', [StringComparison]::Ordinal)
            break
        }
        '^opus-' {
            [Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ceq 'OggS' -and
                $asciiPrefix.Contains('OpusHead', [StringComparison]::Ordinal)
            break
        }
        default { $false }
    }
    if (-not $matches) {
        throw [IO.InvalidDataException]::new("M0AC015 codec signature differs for '$FixtureId'.")
    }
}

function Assert-M0AudioCorpus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $CorpusRoot,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $root = Assert-M0ACSafeRoot -LiteralPath $CorpusRoot -Name 'corpus root'
    $schemas = Assert-M0ACSafeRoot -LiteralPath $SchemaRoot -Name 'schema root'
    Assert-M0ACNoReparseDescendants -Root $root -Name 'corpus root'
    Assert-M0ACNoReparseDescendants -Root $schemas -Name 'schema root'

    $manifestSchema = Resolve-M0ACContainedPath -Root $schemas `
        -RelativePath 'audio-corpus-manifest.schema.json'
    $casesSchema = Resolve-M0ACContainedPath -Root $schemas `
        -RelativePath 'audio-contract-cases.schema.json'
    $recipeSchema = Resolve-M0ACContainedPath -Root $schemas `
        -RelativePath 'audio-corpus-recipe.schema.json'
    $codecRecipeSchema = Resolve-M0ACContainedPath -Root $schemas `
        -RelativePath 'audio-codec-recipe.schema.json'
    $codecProbeSchema = Resolve-M0ACContainedPath -Root $schemas `
        -RelativePath 'audio-codec-probe.schema.json'
    foreach ($schemaPath in @(
        $manifestSchema,
        $casesSchema,
        $recipeSchema,
        $codecRecipeSchema,
        $codecProbeSchema
    )) {
        if (-not (Test-Path -LiteralPath $schemaPath -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("M0AC003 schema is missing: '$schemaPath'.")
        }
    }

    $manifestPath = Resolve-M0ACContainedPath -Root $root -RelativePath 'manifest.v1.json'
    $sidecarPath = Resolve-M0ACContainedPath -Root $root -RelativePath 'manifest.v1.sha256'
    $manifest = Read-M0ACJsonDocument -DocumentPath $manifestPath `
        -SchemaPath $manifestSchema -Name 'audio corpus manifest'
    $manifestHash = Get-M0ACSha256 -LiteralPath $manifestPath
    $sidecar = Read-M0ACCanonicalText -LiteralPath $sidecarPath -Name 'audio corpus manifest sidecar'
    $expectedSidecar = "$manifestHash  manifest.v1.json`n"
    if ($sidecar -cne $expectedSidecar) {
        throw [IO.InvalidDataException]::new('M0AC004 manifest sidecar format or hash differs.')
    }

    if ($manifest.corpus_id -cne 'M0-AUDIO-CORPUS-1' -or
        $manifest.completion.m0_5 -cne 'in_progress' -or
        $manifest.completion.catalog_status -cne 'partial' -or
        $manifest.completion.published_format_support -ne $false) {
        throw [IO.InvalidDataException]::new('M0AC014 manifest completion boundary differs.')
    }

    Assert-M0ACOrdinalSequence -Values @($manifest.artifacts.relative_path) -Name 'artifact paths'
    Assert-M0ACOrdinalSequence -Values @($manifest.fixtures.id) -Name 'fixture IDs'
    Assert-M0ACUniqueStrings -Values @($manifest.fixtures.id) -Name 'fixture IDs'
    $pathComparer = if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal }
    $declaredPaths = @($manifest.artifacts.relative_path) + @($manifest.fixtures.relative_path)
    Assert-M0ACUniqueStrings -Values $declaredPaths -Name 'manifest paths' -Comparer $pathComparer

    $artifactsByRole = @{}
    foreach ($artifact in $manifest.artifacts) {
        if ($artifactsByRole.ContainsKey([string]$artifact.role)) {
            throw [IO.InvalidDataException]::new("M0AC009 duplicate artifact role '$($artifact.role)'.")
        }
        $artifactsByRole[[string]$artifact.role] = $artifact
        if ($artifact.license -cne $script:M0ACPrivateLicense) {
            throw [IO.InvalidDataException]::new("M0AC008 artifact '$($artifact.relative_path)' is not privately licensed.")
        }
    }
    $expectedArtifactPaths = [ordered]@{
        license = @('LICENSE.assets.txt', 'text/plain')
        contract_cases = @('cases.v1.json', 'application/json')
        codec_probe_receipt = @('codec-probe.v1.json', 'application/json')
        codec_generation_recipe = @('codec-recipe.v1.json', 'application/json')
        generation_recipe = @('recipe.v1.json', 'application/json')
    }
    foreach ($role in $expectedArtifactPaths.Keys) {
        if (-not $artifactsByRole.ContainsKey($role)) {
            throw [IO.InvalidDataException]::new("M0AC009 manifest is missing artifact role '$role'.")
        }
        $artifact = $artifactsByRole[$role]
        if ($artifact.relative_path -cne $expectedArtifactPaths[$role][0] -or
            $artifact.media_type -cne $expectedArtifactPaths[$role][1]) {
            throw [IO.InvalidDataException]::new("M0AC009 artifact role '$role' path/media type differs.")
        }
    }

    $fixturesById = @{}
    foreach ($fixture in $manifest.fixtures) {
        $fixturesById[[string]$fixture.id] = $fixture
    }
    [string[]] $actualFixtureIds = @($fixturesById.Keys)
    [string[]] $expectedFixtureIds = @($script:M0ACFixturePaths.Keys)
    [Array]::Sort($actualFixtureIds, [StringComparer]::Ordinal)
    [Array]::Sort($expectedFixtureIds, [StringComparer]::Ordinal)
    Assert-M0ACExactSequence -Actual $actualFixtureIds -Expected $expectedFixtureIds -Name 'fixture ID set'
    foreach ($fixtureId in $script:M0ACFixturePaths.Keys) {
        $fixture = $fixturesById[$fixtureId]
        if ($fixture.relative_path -cne $script:M0ACFixturePaths[$fixtureId]) {
            throw [IO.InvalidDataException]::new("M0AC009 fixture '$fixtureId' path differs.")
        }
        Assert-M0ACFixtureMetadata -Fixture $fixture
    }

    [string[]] $expectedInventory = @('manifest.v1.json', 'manifest.v1.sha256') + $declaredPaths
    [Array]::Sort($expectedInventory, [StringComparer]::Ordinal)
    [string[]] $actualInventory = @(Get-ChildItem -LiteralPath $root -Recurse -File -Force | ForEach-Object {
        [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
    })
    [Array]::Sort($actualInventory, [StringComparer]::Ordinal)
    Assert-M0ACExactSequence -Actual $actualInventory -Expected $expectedInventory -Name 'corpus inventory'

    foreach ($entry in @($manifest.artifacts) + @($manifest.fixtures)) {
        $path = Resolve-M0ACContainedPath -Root $root -RelativePath ([string]$entry.relative_path)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("M0AC006 inventory file is missing: '$($entry.relative_path)'.")
        }
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if ($item.Length -ne [long]$entry.size_bytes) {
            throw [IO.InvalidDataException]::new("M0AC007 size mismatch for '$($entry.relative_path)'.")
        }
        if ((Get-M0ACSha256 -LiteralPath $path) -cne [string]$entry.sha256) {
            throw [IO.InvalidDataException]::new("M0AC007 hash mismatch for '$($entry.relative_path)'.")
        }
        if ($null -ne $entry.PSObject.Properties['role']) {
            $null = Read-M0ACCanonicalText -LiteralPath $path -Name ([string]$entry.relative_path)
        }
    }

    $licensePath = Resolve-M0ACContainedPath -Root $root -RelativePath 'LICENSE.assets.txt'
    $licenseText = Read-M0ACCanonicalText -LiteralPath $licensePath -Name 'audio corpus license'
    if ($licenseText -notmatch '(?m)^LicenseRef-MusicPlayer-M0-Private$' -or
        $licenseText -notmatch '(?i)private local (?:project|use)' -or
        $licenseText -notmatch '(?is)public\s+redistribution\s+is\s+not\s+granted' -or
        $licenseText -notmatch '(?i)no third-party') {
        throw [IO.InvalidDataException]::new('M0AC008 private corpus license/provenance statement differs.')
    }

    $casesPath = Resolve-M0ACContainedPath -Root $root -RelativePath 'cases.v1.json'
    $catalog = Read-M0ACJsonDocument -DocumentPath $casesPath -SchemaPath $casesSchema `
        -Name 'audio contract case catalog'
    Assert-M0ACCaseCatalog -Catalog $catalog -Manifest $manifest

    $recipePath = Resolve-M0ACContainedPath -Root $root -RelativePath 'recipe.v1.json'
    $recipe = Read-M0ACJsonDocument -DocumentPath $recipePath -SchemaPath $recipeSchema `
        -Name 'audio corpus recipe'
    $repositoryRoot = Get-M0ACRepositoryRootFromSchemaRoot -SchemaRoot $schemas
    Assert-M0ACRecipe -Recipe $recipe -Manifest $manifest -RepositoryRoot $repositoryRoot
    $codecRecipePath = Resolve-M0ACContainedPath -Root $root -RelativePath 'codec-recipe.v1.json'
    $codecRecipe = Read-M0ACJsonDocument -DocumentPath $codecRecipePath `
        -SchemaPath $codecRecipeSchema -Name 'audio codec recipe'
    $codecProbePath = Resolve-M0ACContainedPath -Root $root -RelativePath 'codec-probe.v1.json'
    $codecProbe = Read-M0ACJsonDocument -DocumentPath $codecProbePath `
        -SchemaPath $codecProbeSchema -Name 'audio codec probe receipt'
    Assert-M0ACCodecRecipeAndProbe -Recipe $codecRecipe -Probe $codecProbe `
        -Manifest $manifest -RepositoryRoot $repositoryRoot

    $wavPath = Resolve-M0ACContainedPath -Root $root -RelativePath $script:M0ACFixturePaths['wav-pcm-mono']
    $wrongExtensionPath = Resolve-M0ACContainedPath -Root $root `
        -RelativePath $script:M0ACFixturePaths['wrong-extension-wav-as-mp3']
    $truncatedPath = Resolve-M0ACContainedPath -Root $root `
        -RelativePath $script:M0ACFixturePaths['truncated-wav-pcm-mono']
    $emptyPath = Resolve-M0ACContainedPath -Root $root -RelativePath $script:M0ACFixturePaths['empty']
    $null = Read-M0WavePcmContract -LiteralPath $wavPath
    Assert-M0ACIntegerSquareWave -LiteralPath $wavPath
    $wavBytes = [IO.File]::ReadAllBytes($wavPath)
    $wrongExtensionBytes = [IO.File]::ReadAllBytes($wrongExtensionPath)
    Assert-M0ACByteSequenceEqual -Actual $wrongExtensionBytes -Expected $wavBytes `
        -Name 'wrong-extension WAV derivation'
    $truncatedBytes = [IO.File]::ReadAllBytes($truncatedPath)
    if ($truncatedBytes.Length -ne 40) {
        throw [IO.InvalidDataException]::new('M0AC013 truncated WAV must contain exactly 40 bytes.')
    }
    [byte[]] $expectedPrefix = $wavBytes[0..39]
    Assert-M0ACByteSequenceEqual -Actual $truncatedBytes -Expected $expectedPrefix `
        -Name 'truncated WAV derivation'
    if ((Get-Item -LiteralPath $emptyPath -Force).Length -ne 0) {
        throw [IO.InvalidDataException]::new('M0AC013 empty fixture must contain zero bytes.')
    }
    foreach ($fixtureId in $script:M0ACCodecFixturePaths.Keys) {
        $codecPath = Resolve-M0ACContainedPath -Root $root `
            -RelativePath $script:M0ACCodecFixturePaths[$fixtureId]
        Assert-M0ACCodecSignature -FixtureId $fixtureId -LiteralPath $codecPath
    }

    $validFixtureCount = @($manifest.fixtures | Where-Object expected_outcome -ceq 'valid').Count
    $hostileFixtureCount = @($manifest.fixtures | Where-Object expected_outcome -ceq 'controlled_error').Count
    if ($validFixtureCount -ne 10 -or $hostileFixtureCount -ne 2) {
        throw [IO.InvalidDataException]::new('M0AC014 fixture outcome counts differ.')
    }
    return [pscustomobject][ordered]@{
        corpus_id = [string]$manifest.corpus_id
        fixture_count = [int]$manifest.fixtures.Count
        valid_fixture_count = [int]$validFixtureCount
        hostile_fixture_count = [int]$hostileFixtureCount
        catalog_status = [string]$manifest.completion.catalog_status
        manifest_sha256 = $manifestHash
    }
}

Export-ModuleMember -Function @('Assert-M0AudioCorpus', 'Read-M0WavePcmContract')
