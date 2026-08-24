Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-M0FixtureSha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
}

function Resolve-M0FixtureContainedPath {
    param(
        [Parameter(Mandatory)] [string] $FixtureRoot,
        [Parameter(Mandatory)] [string] $RelativePath
    )

    if ([IO.Path]::IsPathRooted($RelativePath) -or $RelativePath.Contains('\') -or $RelativePath.Contains(':')) {
        throw [IO.InvalidDataException]::new("M0F020 unsafe fixture path '$RelativePath'.")
    }
    $segments = $RelativePath.Split('/')
    if ($segments.Count -eq 0 -or $segments -contains '..' -or $segments -contains '.') {
        throw [IO.InvalidDataException]::new("M0F020 unsafe fixture path '$RelativePath'.")
    }

    $base = [IO.Path]::GetFullPath($FixtureRoot).TrimEnd('\', '/')
    if (Test-Path -LiteralPath $base) {
        $baseItem = Get-Item -LiteralPath $base -Force
        if (($baseItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0F020 links are forbidden as fixture roots: '$base'.")
        }
    }
    $candidate = [IO.Path]::GetFullPath((Join-Path $base ($RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $candidate.StartsWith($base + [IO.Path]::DirectorySeparatorChar, $comparison)) {
        throw [IO.InvalidDataException]::new("M0F020 fixture path escapes its root: '$RelativePath'.")
    }

    $current = $base
    foreach ($segment in $segments) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0F020 links are forbidden in fixture path '$RelativePath'.")
        }
    }
    return $candidate
}

function Assert-M0FixtureCanonicalText {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw [IO.InvalidDataException]::new("M0F021 UTF-8 BOM is forbidden in '$(Split-Path $LiteralPath -Leaf)'.")
    }
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal)) {
        throw [IO.InvalidDataException]::new("M0F021 fixture text must use LF and a final newline: '$(Split-Path $LiteralPath -Leaf)'.")
    }
}

function Read-M0FixtureJson {
    param(
        [Parameter(Mandatory)] [string] $DocumentPath,
        [Parameter(Mandatory)] [string] $SchemaPath
    )

    Assert-M0FixtureCanonicalText -LiteralPath $DocumentPath
    $json = Get-Content -LiteralPath $DocumentPath -Raw -Encoding UTF8
    if (-not (Test-Json -Json $json -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0F002 JSON schema rejected '$(Split-Path $DocumentPath -Leaf)'.")
    }
    return ($json | ConvertFrom-Json -Depth 100)
}

function Get-M0RuneCount {
    param([Parameter(Mandatory)] [string] $Value)

    return @($Value.EnumerateRunes()).Count
}

function Test-M0JsonValueEqual {
    param(
        [AllowNull()] $Actual,
        [AllowNull()] $Expected
    )

    return (($Actual | ConvertTo-Json -Compress -Depth 100) -ceq
        ($Expected | ConvertTo-Json -Compress -Depth 100))
}

function Assert-M0ExactSequence {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [IO.InvalidDataException]::new("M0F003 $Name count differs: expected $($Expected.Count), got $($Actual.Count).")
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ([string]$Actual[$index] -cne [string]$Expected[$index]) {
            throw [IO.InvalidDataException]::new("M0F003 $Name differs at $index.")
        }
    }
}

function Assert-M0ShellFixtureContract {
    param(
        [Parameter(Mandatory)] [psobject] $Descriptor,
        [Parameter(Mandatory)] [psobject] $Queue
    )

    if ($Descriptor.fixture_id -ne 'M0-SHELL-F1' -or $Queue.queue_id -ne 'M0-SHELL-Q1000' -or
        $Descriptor.queue.queue_id -ne $Queue.queue_id -or [int]$Descriptor.queue.expected_item_count -ne 1000) {
        throw [IO.InvalidDataException]::new('M0F004 fixture and queue identities differ.')
    }

    $defaults = $Descriptor.preferences.defaults
    $expectedDefaults = [ordered]@{
        theme = 'system'; motion = 'system'; volume_percent = 80; muted = $false
        skip_backward_seconds = 5; skip_forward_seconds = 5; close_behavior = 'minimize_to_tray'
        restore_last_session = $true; start_minimized = $false; queue_end_behavior = 'stop_keep_selection'
    }
    foreach ($name in $expectedDefaults.Keys) {
        if ($defaults.$name -cne $expectedDefaults[$name]) {
            throw [IO.InvalidDataException]::new("M0F005 preference '$name' differs from D0.9.")
        }
    }
    if ([int]$Descriptor.preferences.constraints.volume_percent.minimum -ne 0 -or
        [int]$Descriptor.preferences.constraints.volume_percent.maximum -ne 100 -or
        [int]$Descriptor.preferences.constraints.skip_seconds.minimum -ne 1 -or
        [int]$Descriptor.preferences.constraints.skip_seconds.maximum -ne 300) {
        throw [IO.InvalidDataException]::new('M0F005 preference ranges differ from D0.9.')
    }

    Assert-M0ExactSequence -Actual @($Descriptor.states.state_id) -Expected @('empty', 'playing', 'error') -Name 'state ids'
    Assert-M0ExactSequence -Actual @($Descriptor.themes.available_modes) -Expected @('system', 'light', 'dark') -Name 'theme modes'
    Assert-M0ExactSequence -Actual @($Descriptor.motion.available_modes) -Expected @('system', 'reduced') -Name 'motion modes'
    if ([int]$Descriptor.fake_backend.snapshots.frequency_hz -ne 5 -or
        [int]$Descriptor.fake_backend.snapshots.interval_ms -ne 200 -or
        [int]$Descriptor.fake_backend.seek.target_ms -ne 124000 -or
        [int]$Descriptor.fake_backend.seek.ack_after_ticks -ne 1 -or
        [int]$Descriptor.fake_backend.seek.ack_position_ms -ne 124000) {
        throw [IO.InvalidDataException]::new('M0F006 fake snapshot/seek contract differs.')
    }

    $requiredTokens = @('accent', 'border', 'canvas', 'error', 'focus', 'on-accent', 'success', 'surface', 'surface-raised', 'text-primary', 'text-secondary', 'warning')
    $expectedPalettes = [ordered]@{
        light = [ordered]@{
            canvas = '#F6F7F9'; surface = '#FFFFFF'; 'surface-raised' = '#FFFFFF'; 'text-primary' = '#15171A'
            'text-secondary' = '#4F5660'; accent = '#4F46E5'; 'on-accent' = '#FFFFFF'; border = '#8A929E'
            focus = '#2457D6'; error = '#B42318'; warning = '#8A4B08'; success = '#167344'
        }
        dark = [ordered]@{
            canvas = '#111318'; surface = '#181B21'; 'surface-raised' = '#21252D'; 'text-primary' = '#F5F7FA'
            'text-secondary' = '#BBC2CC'; accent = '#8B83FF'; 'on-accent' = '#0B0C10'; border = '#69717D'
            focus = '#A9C2FF'; error = '#FF8A80'; warning = '#FFC46B'; success = '#6DDA9B'
        }
    }
    foreach ($mode in @('light', 'dark')) {
        $palette = $Descriptor.themes.palettes.$mode
        Assert-M0ExactSequence -Actual @($palette.PSObject.Properties.Name | Sort-Object) -Expected $requiredTokens -Name "$mode palette tokens"
        foreach ($token in $requiredTokens) {
            if ($palette.$token -cne $expectedPalettes[$mode][$token]) {
                throw [IO.InvalidDataException]::new("M0F007 $mode token '$token' differs.")
            }
        }
    }

    $expectedStrings = [ordered]@{
        action_open_audio = 'Abrir áudio'
        empty_title = 'Abra um áudio para começar'
        action_play = 'Reproduzir'
        action_pause = 'Pausar'
        preferences_title = 'Preferências'
        error_body = 'A faixa de teste está indisponível.'
    }
    foreach ($name in $expectedStrings.Keys) {
        if ($Descriptor.strings.$name -cne $expectedStrings[$name]) {
            throw [IO.InvalidDataException]::new("M0F008 pt-BR string '$name' differs.")
        }
    }
    foreach ($property in $Descriptor.strings.PSObject.Properties) {
        if (-not $property.Value.IsNormalized([Text.NormalizationForm]::FormC)) {
            throw [IO.InvalidDataException]::new("M0F008 catalog string '$($property.Name)' is not NFC.")
        }
    }

    $expectedControls = @(
        [ordered]@{ control_id = 'open-audio'; role = 'button'; name_key = 'action_open_audio' },
        [ordered]@{ control_id = 'play-pause'; role = 'button'; name_key = 'action_pause' },
        [ordered]@{ control_id = 'previous'; role = 'button'; name_key = 'action_previous' },
        [ordered]@{ control_id = 'next'; role = 'button'; name_key = 'action_next' },
        [ordered]@{ control_id = 'skip-backward'; role = 'button'; name_key = 'skip_backward_name' },
        [ordered]@{ control_id = 'skip-forward'; role = 'button'; name_key = 'skip_forward_name' },
        [ordered]@{ control_id = 'progress'; role = 'slider'; name_key = 'progress_name' },
        [ordered]@{ control_id = 'volume'; role = 'slider'; name_key = 'volume_name' },
        [ordered]@{ control_id = 'mute'; role = 'button'; name_key = 'action_mute' },
        [ordered]@{ control_id = 'queue'; role = 'button'; name_key = 'queue_title' },
        [ordered]@{ control_id = 'preferences'; role = 'button'; name_key = 'preferences_title' }
    )
    if (-not (Test-M0JsonValueEqual -Actual @($Descriptor.controls) -Expected $expectedControls)) {
        throw [IO.InvalidDataException]::new('M0F025 accessible control contract differs.')
    }

    $expectedStates = @(
        [ordered]@{
            state_id = 'empty'; playback_state = 'unloaded'; queue_loaded = $false
            current_occurrence_id = $null; context_occurrence_id = $null
            position_ms = $null; duration_ms = $null; seekable = $false
            asset_id = 'cover-placeholder'; message_key = 'empty_title'
            primary_action_id = 'open-audio'; error_code = $null
        },
        [ordered]@{
            state_id = 'playing'; playback_state = 'playing'; queue_loaded = $true
            current_occurrence_id = 'queue-0017'; context_occurrence_id = $null
            position_ms = 84000; duration_ms = 248000; seekable = $true
            asset_id = 'cover-track'; message_key = $null
            primary_action_id = 'play-pause'; error_code = $null
        },
        [ordered]@{
            state_id = 'error'; playback_state = 'error'; queue_loaded = $true
            current_occurrence_id = 'queue-0999'; context_occurrence_id = 'queue-0017'
            position_ms = $null; duration_ms = $null; seekable = $false
            asset_id = 'cover-track'; message_key = 'error_body'
            primary_action_id = 'skip'; error_code = 'fixture_source_unavailable'
        }
    )
    if (-not (Test-M0JsonValueEqual -Actual @($Descriptor.states) -Expected $expectedStates)) {
        throw [IO.InvalidDataException]::new('M0F026 shell state contract differs.')
    }

    $expectedFixedCases = @(
        [ordered]@{ case_id = 'duplicate-exact'; occurrence_ids = @('queue-0007', 'queue-0707'); expectation = 'distinct_occurrences_same_source' },
        [ordered]@{ case_id = 'unicode-normalization'; occurrence_ids = @('queue-0017', 'queue-0018'); expectation = 'distinct_presentations_same_nfc_source' },
        [ordered]@{ case_id = 'unicode-non-bmp'; occurrence_ids = @('queue-0023'); expectation = 'preserve_unicode_scalars' },
        [ordered]@{ case_id = 'literal-markup'; occurrence_ids = @('queue-0042'); expectation = 'render_as_plain_text' },
        [ordered]@{ case_id = 'long-text'; occurrence_ids = @('queue-0512'); expectation = '512_unicode_scalars' },
        [ordered]@{ case_id = 'unavailable'; occurrence_ids = @('queue-0999'); expectation = 'recoverable_error_preserves_queue' }
    )
    if (-not (Test-M0JsonValueEqual -Actual @($Descriptor.fixed_cases) -Expected $expectedFixedCases)) {
        throw [IO.InvalidDataException]::new('M0F027 fixed-case mapping differs.')
    }

    $expectedAssets = @(
        [ordered]@{ asset_id = 'cover-placeholder'; relative_path = 'assets/cover-placeholder.svg'; alt_key = 'empty_title' },
        [ordered]@{ asset_id = 'cover-track'; relative_path = 'assets/cover-track.svg'; alt_key = 'details_title' }
    )
    if (-not (Test-M0JsonValueEqual -Actual @($Descriptor.assets) -Expected $expectedAssets)) {
        throw [IO.InvalidDataException]::new('M0F028 fixture asset mapping differs.')
    }

    if ([int]$Queue.item_count -ne 1000 -or $Queue.items.Count -ne 1000) {
        throw [IO.InvalidDataException]::new("M0F009 queue must contain exactly 1000 items.")
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt 1000; $index++) {
        $item = $Queue.items[$index]
        $expectedId = 'queue-{0:D4}' -f ($index + 1)
        if ([int]$item.order_index -ne $index -or $item.occurrence_id -cne $expectedId -or -not $seen.Add($item.occurrence_id)) {
            throw [IO.InvalidDataException]::new("M0F009 queue identity/order differs at index $index.")
        }
        if (($item.availability -ceq 'available' -and
                ($null -eq $item.duration_ms -or [long]$item.duration_ms -le 0 -or $null -ne $item.error_code)) -or
            ($item.availability -ceq 'unavailable' -and
                ($null -ne $item.duration_ms -or [string]::IsNullOrWhiteSpace([string]$item.error_code)))) {
            throw [IO.InvalidDataException]::new("M0F029 availability fields differ at '$expectedId'.")
        }
        if ($item.source.synthetic_path_segments[-1] -cne $item.source.file_name) {
            throw [IO.InvalidDataException]::new("M0F010 synthetic file name differs from its final segment at '$expectedId'.")
        }
        foreach ($segment in @($item.source.synthetic_path_segments) + @($item.source.file_name)) {
            if ([IO.Path]::IsPathRooted($segment) -or $segment.Contains('/') -or $segment.Contains('\') -or
                $segment.Contains(':') -or $segment -in @('.', '..', '~') -or $segment -match '^[A-Za-z][A-Za-z0-9+.-]*:') {
                throw [IO.InvalidDataException]::new("M0F010 synthetic source contains an executable path at '$expectedId'.")
            }
        }
    }

    $q7 = $Queue.items[6]
    $q707 = $Queue.items[706]
    if (($q7.source | ConvertTo-Json -Compress -Depth 10) -cne ($q707.source | ConvertTo-Json -Compress -Depth 10) -or
        ($q7.metadata | ConvertTo-Json -Compress -Depth 10) -cne ($q707.metadata | ConvertTo-Json -Compress -Depth 10) -or
        $q7.duration_ms -ne $q707.duration_ms -or $q7.availability -cne $q707.availability -or
        $q7.error_code -cne $q707.error_code) {
        throw [IO.InvalidDataException]::new('M0F011 queue-0007/0707 are not exact semantic duplicates.')
    }

    $q17 = $Queue.items[16]
    $q18 = $Queue.items[17]
    if ($q17.availability -cne 'available' -or $null -ne $q17.error_code -or
        [long]$q17.duration_ms -ne [long]$Descriptor.states[1].duration_ms) {
        throw [IO.InvalidDataException]::new('M0F026 playing state and queue-0017 differ.')
    }
    $q17Fields = @(
        [string]$q17.metadata.title,
        [string]$q17.metadata.artist,
        [string]$q17.metadata.album,
        [string]$q17.source.synthetic_path_segments[0],
        [string]$q17.source.synthetic_path_segments[1],
        [string]$q17.source.file_name
    )
    $q18Fields = @(
        [string]$q18.metadata.title,
        [string]$q18.metadata.artist,
        [string]$q18.metadata.album,
        [string]$q18.source.synthetic_path_segments[0],
        [string]$q18.source.synthetic_path_segments[1],
        [string]$q18.source.file_name
    )
    $normalizationDiffers = $false
    for ($fieldIndex = 0; $fieldIndex -lt $q17Fields.Count; $fieldIndex++) {
        if (-not $q17Fields[$fieldIndex].IsNormalized([Text.NormalizationForm]::FormC) -or
            -not $q18Fields[$fieldIndex].IsNormalized([Text.NormalizationForm]::FormD) -or
            [string]::Equals($q17Fields[$fieldIndex], $q18Fields[$fieldIndex], [StringComparison]::Ordinal) -or
            -not [string]::Equals($q17Fields[$fieldIndex].Normalize([Text.NormalizationForm]::FormC), $q18Fields[$fieldIndex].Normalize([Text.NormalizationForm]::FormC), [StringComparison]::Ordinal)) {
            $normalizationDiffers = $true
            break
        }
    }
    if ($normalizationDiffers -or $q17.source.semantic_key -cne $q18.source.semantic_key -or
        $q17.duration_ms -ne $q18.duration_ms -or $q17.availability -cne $q18.availability -or
        $q17.error_code -cne $q18.error_code -or $q17.source.presentation_form -cne 'nfc' -or
        $q18.source.presentation_form -cne 'nfd') {
        throw [IO.InvalidDataException]::new('M0F012 NFC/NFD fixture pair differs.')
    }
    if ($Queue.items[22].metadata.title -cne 'Olá, mundo — 東京・مرحبا・música 🎧') {
        throw [IO.InvalidDataException]::new('M0F013 non-BMP Unicode fixture differs.')
    }
    if ($Queue.items[41].metadata.title -cne '<b>Não interpretar</b> & "aspas" ''simples''') {
        throw [IO.InvalidDataException]::new('M0F014 literal markup fixture differs.')
    }
    if ((Get-M0RuneCount -Value $Queue.items[511].metadata.title) -ne 512) {
        throw [IO.InvalidDataException]::new('M0F015 long title must contain exactly 512 Unicode scalars.')
    }
    if ($Queue.items[998].availability -cne 'unavailable' -or
        $Queue.items[998].error_code -cne 'fixture_source_unavailable' -or $null -ne $Queue.items[998].duration_ms) {
        throw [IO.InvalidDataException]::new('M0F016 unavailable fixture differs.')
    }
    if (@($Queue.items | Where-Object availability -ceq 'unavailable').Count -ne 1) {
        throw [IO.InvalidDataException]::new('M0F029 queue must contain only the frozen unavailable item.')
    }
}

function Assert-M0SvgAsset {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $text = Get-Content -LiteralPath $LiteralPath -Raw -Encoding UTF8
    if ($text -notmatch '<svg\b' -or $text -match '(?i)<script\b|foreignObject|\bhref\s*=|url\s*\(|<!DOCTYPE|<!ENTITY|xlink:') {
        throw [IO.InvalidDataException]::new("M0F022 unsafe or non-SVG asset '$(Split-Path $LiteralPath -Leaf)'.")
    }
    $document = [xml]$text
    if ($document.DocumentElement.LocalName -ne 'svg' -or $document.DocumentElement.GetAttribute('viewBox') -ne '0 0 256 256') {
        throw [IO.InvalidDataException]::new("M0F022 SVG dimensions differ in '$(Split-Path $LiteralPath -Leaf)'.")
    }

    $allowedAttributes = @{
        svg = @('xmlns', 'viewBox', 'width', 'height')
        rect = @('width', 'height', 'rx', 'fill')
        circle = @('cx', 'cy', 'r', 'fill')
        path = @('d', 'fill')
    }
    foreach ($element in @($document.SelectNodes('//*'))) {
        if ($element.NamespaceURI -ne 'http://www.w3.org/2000/svg' -or -not $allowedAttributes.ContainsKey($element.LocalName)) {
            throw [IO.InvalidDataException]::new("M0F022 SVG element '$($element.Name)' is not allowed.")
        }
        foreach ($attribute in @($element.Attributes)) {
            if ($attribute.Name -notin $allowedAttributes[$element.LocalName]) {
                throw [IO.InvalidDataException]::new("M0F022 SVG attribute '$($attribute.Name)' is not allowed on '$($element.LocalName)'.")
            }
        }
        foreach ($child in @($element.ChildNodes)) {
            if ($child.NodeType -in @([Xml.XmlNodeType]::Text, [Xml.XmlNodeType]::CDATA) -and -not [string]::IsNullOrWhiteSpace($child.Value)) {
                throw [IO.InvalidDataException]::new("M0F022 SVG text content is forbidden in '$($element.LocalName)'.")
            }
        }
    }
}

function Assert-M0FixtureSet {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $FixtureRoot,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $root = [IO.Path]::GetFullPath($FixtureRoot)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0F001 fixture root is missing: '$root'.")
    }
    $rootItem = Get-Item -LiteralPath $root -Force
    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw [IO.InvalidDataException]::new("M0F020 links and reparse points are forbidden as fixture roots: '$root'.")
    }
    $manifestPath = Join-Path $root 'manifest.v1.json'
    $sidecarPath = Join-Path $root 'manifest.v1.sha256'
    foreach ($path in @($manifestPath, $sidecarPath)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("M0F001 required fixture file is missing: '$(Split-Path $path -Leaf)'.")
        }
        Assert-M0FixtureCanonicalText -LiteralPath $path
    }

    $manifestSchema = Join-Path $SchemaRoot 'fixture-manifest.schema.json'
    $descriptorSchema = Join-Path $SchemaRoot 'shell-fixture.schema.json'
    $queueSchema = Join-Path $SchemaRoot 'shell-queue.schema.json'
    $manifest = Read-M0FixtureJson -DocumentPath $manifestPath -SchemaPath $manifestSchema

    $sidecar = (Get-Content -LiteralPath $sidecarPath -Raw -Encoding UTF8).TrimEnd("`n")
    if ($sidecar -notmatch '^(?<hash>[a-f0-9]{64})  manifest\.v1\.json$') {
        throw [IO.InvalidDataException]::new('M0F017 manifest sidecar format differs.')
    }
    $manifestHash = Get-M0FixtureSha256 -LiteralPath $manifestPath
    if ($manifestHash -cne $Matches.hash) {
        throw [IO.InvalidDataException]::new('M0F017 manifest sidecar hash mismatch.')
    }

    $expectedPaths = @('LICENSE.assets.txt', 'assets/cover-placeholder.svg', 'assets/cover-track.svg', 'fixture.v1.json', 'queue.v1.json')
    Assert-M0ExactSequence -Actual @($manifest.files.relative_path) -Expected $expectedPaths -Name 'manifest file order'
    $expectedMetadata = [ordered]@{
        'LICENSE.assets.txt' = @('license', 'text/plain', 'project-authored-synthetic')
        'assets/cover-placeholder.svg' = @('asset', 'image/svg+xml', 'project-authored-synthetic')
        'assets/cover-track.svg' = @('asset', 'image/svg+xml', 'project-authored-synthetic')
        'fixture.v1.json' = @('descriptor', 'application/json', 'generated-from-repository-script')
        'queue.v1.json' = @('queue', 'application/json', 'generated-from-repository-script')
    }
    foreach ($entry in $manifest.files) {
        $metadata = $expectedMetadata[$entry.relative_path]
        if ($null -eq $metadata -or $entry.role -cne $metadata[0] -or $entry.media_type -cne $metadata[1] -or $entry.origin -cne $metadata[2]) {
            throw [IO.InvalidDataException]::new("M0F024 manifest metadata differs for '$($entry.relative_path)'.")
        }
    }
    [string[]]$actualPaths = @(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
        [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
    } | Where-Object { $_ -notin @('manifest.v1.json', 'manifest.v1.sha256') })
    [Array]::Sort($actualPaths, [StringComparer]::Ordinal)
    Assert-M0ExactSequence -Actual $actualPaths -Expected $expectedPaths -Name 'manifest coverage'

    foreach ($entry in $manifest.files) {
        $path = Resolve-M0FixtureContainedPath -FixtureRoot $root -RelativePath $entry.relative_path
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("M0F018 manifest file is missing: '$($entry.relative_path)'.")
        }
        Assert-M0FixtureCanonicalText -LiteralPath $path
        if ((Get-Item -LiteralPath $path).Length -ne [long]$entry.size_bytes) {
            throw [IO.InvalidDataException]::new("M0F018 size mismatch for '$($entry.relative_path)'.")
        }
        if ((Get-M0FixtureSha256 -LiteralPath $path) -cne $entry.sha256) {
            throw [IO.InvalidDataException]::new("M0F019 hash mismatch for '$($entry.relative_path)'.")
        }
        if ($entry.license_id -cne 'LicenseRef-MusicPlayer-M0-Private') {
            throw [IO.InvalidDataException]::new("M0F023 unknown fixture license for '$($entry.relative_path)'.")
        }
    }

    $licensePath = Join-Path $root 'LICENSE.assets.txt'
    $licenseText = Get-Content -LiteralPath $licensePath -Raw -Encoding UTF8
    if ($licenseText -notmatch '(?m)^LicenseRef-MusicPlayer-M0-Private$' -or
        $licenseText -notmatch 'No third-party media') {
        throw [IO.InvalidDataException]::new('M0F023 private license/provenance statement differs.')
    }
    Assert-M0SvgAsset -LiteralPath (Join-Path $root 'assets\cover-placeholder.svg')
    Assert-M0SvgAsset -LiteralPath (Join-Path $root 'assets\cover-track.svg')

    $descriptor = Read-M0FixtureJson -DocumentPath (Join-Path $root 'fixture.v1.json') -SchemaPath $descriptorSchema
    $queue = Read-M0FixtureJson -DocumentPath (Join-Path $root 'queue.v1.json') -SchemaPath $queueSchema
    Assert-M0ShellFixtureContract -Descriptor $descriptor -Queue $queue

    return [pscustomobject]@{
        fixture_id = $descriptor.fixture_id
        queue_id = $queue.queue_id
        item_count = $queue.items.Count
        fixture_sha256 = $manifestHash
        manifest_file_count = $manifest.files.Count
    }
}

Export-ModuleMember -Function @('Assert-M0FixtureSet')
