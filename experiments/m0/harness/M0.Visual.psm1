Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:M0VisualUtf8 = [Text.UTF8Encoding]::new($false, $true)

function Get-M0VisualSha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Read-M0VisualTextFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $path = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0V001 required $Name is missing: '$path'.")
    }
    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
        throw [IO.InvalidDataException]::new("M0V001 links are forbidden for trusted $Name '$path'.")
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new("M0V001 $Name must be non-empty strict UTF-8 without BOM.")
    }
    try {
        $text = $script:M0VisualUtf8.GetString($bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0V001 $Name is not strict UTF-8.", $_.Exception)
    }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.EndsWith("`n`n", [StringComparison]::Ordinal) -or $text -match '(?m)[\t ]+$') {
        throw [IO.InvalidDataException]::new("M0V001 $Name must use LF, exactly one final newline and no trailing whitespace.")
    }
    return [pscustomobject][ordered]@{
        path = $path
        bytes = $bytes
        text = $text
        sha256 = Get-M0VisualSha256Bytes -Bytes $bytes
    }
}

function ConvertFrom-M0VisualJsonFile {
    param(
        [Parameter(Mandatory)] $File,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Name
    )

    if (-not (Test-Json -Json $File.text -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0V001 $Name failed its closed JSON schema.")
    }
    try {
        return ($File.text | ConvertFrom-Json -Depth 100 -DateKind String)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0V001 $Name is not valid JSON.", $_.Exception)
    }
}

function Assert-M0VisualGoldenSemantics {
    param(
        [Parameter(Mandatory)] $Golden,
        [Parameter(Mandatory)] $Descriptor,
        [Parameter(Mandatory)] $Fixture
    )

    if ($Golden.visual_id -cne $Descriptor.visual_id) {
        throw [IO.InvalidDataException]::new('M0V002 golden visual identity differs from the descriptor.')
    }
    $caseIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($case in @($Golden.roi_vectors) + @($Golden.detection_vectors) + @($Golden.uia_vectors)) {
        if (-not $caseIds.Add([string]$case.case_id)) {
            throw [IO.InvalidDataException]::new("M0V002 duplicate visual golden case '$($case.case_id)'.")
        }
    }
    foreach ($vector in @($Golden.detection_vectors)) {
        $minimumStride = [Numerics.BigInteger]::Parse([string]$vector.width) * 4
        $stride = [Numerics.BigInteger]::Parse([string]$vector.stride_bytes)
        $height = [Numerics.BigInteger]::Parse([string]$vector.height)
        if ($stride -lt $minimumStride) {
            throw [IO.InvalidDataException]::new("M0V002 golden '$($vector.case_id)' stride is shorter than width*4.")
        }
        $expectedHexLength = $stride * $height * 2
        $rootFrequency = [Numerics.BigInteger]::Parse([string]$vector.qpc_frequency)
        $inputTicks = [Numerics.BigInteger]::Parse([string]$vector.input_qpc_ticks)
        $previousTicks = $null
        $previousOrdinal = $null
        $allFrames = @($vector.baseline) + @($vector.observed)
        for ($index = 0; $index -lt $allFrames.Count; $index++) {
            $frame = $allFrames[$index]
            if ([Numerics.BigInteger]::Parse([string]$frame.qpc_frequency) -ne $rootFrequency -or
                [Numerics.BigInteger]::Parse([string]$frame.bgra8_hex.Length) -ne $expectedHexLength) {
                throw [IO.InvalidDataException]::new("M0V002 golden '$($vector.case_id)' frame frequency or BGRA8 length differs.")
            }
            $ticks = [Numerics.BigInteger]::Parse([string]$frame.qpc_ticks)
            $ordinal = [Numerics.BigInteger]::Parse([string]$frame.frame_ordinal)
            if (($null -ne $previousTicks -and $ticks -le $previousTicks) -or
                ($null -ne $previousOrdinal -and $ordinal -le $previousOrdinal)) {
                throw [IO.InvalidDataException]::new("M0V002 golden '$($vector.case_id)' frame QPC/ordinal is not unique and strictly increasing.")
            }
            if (($index -lt 3 -and $ticks -ge $inputTicks) -or
                ($index -ge 3 -and $ticks -le $inputTicks)) {
                throw [IO.InvalidDataException]::new("M0V002 golden '$($vector.case_id)' violates baseline < input < observed causality.")
            }
            $previousTicks = $ticks
            $previousOrdinal = $ordinal
        }
    }
    if ($Fixture.strings.action_pause -cne 'Pausar' -or $Fixture.strings.action_play -cne 'Reproduzir') {
        throw [IO.InvalidDataException]::new('M0V002 trusted pt-BR play/pause fixture strings differ.')
    }
    $playPause = @($Fixture.controls | Where-Object control_id -CEQ 'play-pause')
    if ($playPause.Count -ne 1 -or $playPause[0].role -cne 'button' -or $playPause[0].name_key -cne 'action_pause') {
        throw [IO.InvalidDataException]::new('M0V002 trusted play-pause fixture control differs.')
    }
    $playingState = @($Fixture.states | Where-Object state_id -CEQ 'playing')
    if ($playingState.Count -ne 1 -or
        $playingState[0].playback_state -cne 'playing' -or
        -not [bool]$playingState[0].queue_loaded -or
        [string]::IsNullOrEmpty([string]$playingState[0].current_occurrence_id) -or
        $playingState[0].primary_action_id -cne 'play-pause') {
        throw [IO.InvalidDataException]::new('M0V002 trusted playing seed state differs from the UIA contract.')
    }
    foreach ($vector in @($Golden.roi_vectors)) {
        $actual = Resolve-M0VisualRoi -ControlRect $vector.control_rect -ClientRect $vector.client_rect
        if ($actual.left -ne $vector.expected.left -or
            $actual.top -ne $vector.expected.top -or
            $actual.right -ne $vector.expected.right -or
            $actual.bottom -ne $vector.expected.bottom) {
            throw [IO.InvalidDataException]::new("M0V002 ROI golden '$($vector.case_id)' expected result is semantically false.")
        }
    }
    foreach ($vector in @($Golden.detection_vectors)) {
        $baseline = @($vector.baseline | ForEach-Object {
            [pscustomobject][ordered]@{
                frame_ordinal = $_.frame_ordinal
                qpc_ticks = [string]$_.qpc_ticks
                qpc_frequency = [string]$_.qpc_frequency
                accumulated_frames = $_.accumulated_frames
                pixels = [Convert]::FromHexString([string]$_.bgra8_hex)
            }
        })
        $observed = @($vector.observed | ForEach-Object {
            [pscustomobject][ordered]@{
                frame_ordinal = $_.frame_ordinal
                qpc_ticks = [string]$_.qpc_ticks
                qpc_frequency = [string]$_.qpc_frequency
                accumulated_frames = $_.accumulated_frames
                pixels = [Convert]::FromHexString([string]$_.bgra8_hex)
            }
        })
        $actual = Resolve-M0VisualDetection `
            -Width $vector.width -Height $vector.height -StrideBytes $vector.stride_bytes `
            -BaselineFrames $baseline -ObservedFrames $observed `
            -InputQpcTicks ([string]$vector.input_qpc_ticks) -QpcFrequency ([string]$vector.qpc_frequency)
        if ($actual.detected -ne $vector.expected.detected -or
            $actual.t1_frame_ordinal -ne $vector.expected.t1_frame_ordinal -or
            $actual.t1_qpc_ticks -cne $vector.expected.t1_qpc_ticks -or
            $actual.value_ns -cne $vector.expected.value_ns -or
            $actual.changed_pixel_count -ne $vector.expected.changed_pixel_count) {
            throw [IO.InvalidDataException]::new("M0V002 detection golden '$($vector.case_id)' expected result is semantically false.")
        }
    }
    foreach ($vector in @($Golden.uia_vectors)) {
        if ($vector.initial_state_id -cne 'playing' -or
            $vector.before.name -cne $Fixture.strings.action_pause -or
            @($vector.after | Where-Object name -CEQ $Fixture.strings.action_play).Count -eq 0) {
            throw [IO.InvalidDataException]::new("M0V002 UIA golden '$($vector.case_id)' does not resolve fixture Name keys ordinally.")
        }
        $actual = Assert-M0VisualUiaTransition `
            -BeforeObservation $vector.before -AfterObservations @($vector.after) `
            -InputQpcTicks ([string]$vector.input_qpc_ticks) `
            -QpcFrequency ([string]$vector.qpc_frequency) -Fixture $Fixture
        if (-not $actual.confirmed -or
            $actual.confirmation_qpc_ticks -cne $vector.expected_confirmation_qpc_ticks) {
            throw [IO.InvalidDataException]::new("M0V002 UIA golden '$($vector.case_id)' expected result is semantically false.")
        }
    }
}

function Assert-M0VisualArtifacts {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $ProtocolRoot)

    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    $descriptorFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'visual.v1.json') -Name 'visual descriptor'
    $sidecarFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'visual.v1.sha256') -Name 'visual descriptor sidecar'
    $descriptorSchemaFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'schemas\visual.schema.json') -Name 'visual descriptor schema'
    $goldenSchemaFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'schemas\visual-golden.schema.json') -Name 'visual golden schema'
    $protocolFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'protocol.v1.json') -Name 'protocol descriptor'
    $markerFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'markers.v1.json') -Name 'marker catalog'

    if ($sidecarFile.text -cnotmatch '^([a-f0-9]{64})  visual\.v1\.json\n$' -or $Matches[1] -cne $descriptorFile.sha256) {
        throw [IO.InvalidDataException]::new('M0V001 visual descriptor sidecar is noncanonical or its hash differs.')
    }
    $descriptor = ConvertFrom-M0VisualJsonFile -File $descriptorFile -SchemaPath $descriptorSchemaFile.path -Name 'visual descriptor'
    if ($descriptor.golden_relative_path -cne 'goldens/visual.v1.json') {
        throw [IO.InvalidDataException]::new('M0V001 visual golden relative path differs from the closed contract.')
    }
    $goldenFile = Read-M0VisualTextFile -LiteralPath (Join-Path $root 'goldens\visual.v1.json') -Name 'visual golden'
    if ($goldenFile.sha256 -cne $descriptor.golden_sha256) {
        throw [IO.InvalidDataException]::new('M0V001 visual golden SHA-256 differs from the descriptor anchor.')
    }
    $golden = ConvertFrom-M0VisualJsonFile -File $goldenFile -SchemaPath $goldenSchemaFile.path -Name 'visual golden'
    $protocol = $protocolFile.text | ConvertFrom-Json -Depth 100 -DateKind String
    $markers = $markerFile.text | ConvertFrom-Json -Depth 100 -DateKind String
    if ($protocol.visual_descriptor_sha256 -cne $descriptorFile.sha256.ToUpperInvariant() -or
        $descriptor.protocol_id -cne $protocol.protocol_id -or
        $descriptor.scenario_id -cne 'visual-response-diagnostic') {
        throw [IO.InvalidDataException]::new('M0V001 protocol visual descriptor anchor or identity differs.')
    }
    $visual = $protocol.visual_detection
    if ($visual.pixel_format -cne $descriptor.frame_contract.pixel_format -or
        $visual.row_order -cne $descriptor.frame_contract.row_order -or
        -not [bool]$visual.alpha_ignored -or -not [bool]$visual.row_padding_ignored -or
        [int]$visual.roi_expand_px -ne [int]$descriptor.roi_contract.expand_px -or
        [int]$visual.baseline_frames -ne [int]$descriptor.baseline_contract.frame_count -or
        [int]$visual.rgb_absolute_sum_min -ne [int]$descriptor.change_contract.rgb_absolute_sum_min -or
        [int]$visual.changed_pixels_numerator -ne [int]$descriptor.change_contract.changed_pixels_numerator -or
        [int]$visual.changed_pixels_denominator -ne [int]$descriptor.change_contract.changed_pixels_denominator -or
        [int]$visual.persistent_frames -ne [int]$descriptor.change_contract.persistent_frames -or
        $visual.t1_policy -cne $descriptor.change_contract.t1_policy) {
        throw [IO.InvalidDataException]::new('M0V002 protocol/visual detector semantics differ.')
    }
    if ($markers.visual_diagnostic_policy.decision_role -cne $descriptor.diagnostic_contract.m0_shell_decision_role -or
        [bool]$markers.visual_diagnostic_policy.budget_exceeded_forbidden -ne [bool]$descriptor.diagnostic_contract.budget_exceeded_forbidden -or
        [int]$markers.visual_diagnostic_policy.baseline_frames -ne [int]$descriptor.baseline_contract.frame_count -or
        [long]$markers.visual_diagnostic_policy.uia_confirmation_timeout_ns -ne [long]$descriptor.uia_contract.confirmation_timeout_ns) {
        throw [IO.InvalidDataException]::new('M0V002 marker/visual diagnostic semantics differ.')
    }
    $m0Root = Split-Path $root -Parent
    $fixtureFile = Read-M0VisualTextFile -LiteralPath (Join-Path $m0Root 'shared\shell-fixture\fixture.v1.json') -Name 'shell fixture'
    $fixture = $fixtureFile.text | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-M0VisualGoldenSemantics -Golden $golden -Descriptor $descriptor -Fixture $fixture

    return [pscustomobject][ordered]@{
        visual_id = [string]$descriptor.visual_id
        golden_id = [string]$golden.golden_id
        descriptor_sha256 = [string]$descriptorFile.sha256
        golden_sha256 = [string]$goldenFile.sha256
        descriptor = $descriptor
        golden = $golden
    }
}

function Assert-M0VisualClosedObject {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string[]] $Properties,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($null -eq $Value) {
        throw [ArgumentNullException]::new($Name)
    }
    $actual = @($Value.PSObject.Properties | Where-Object MemberType -In @('NoteProperty', 'Property') | ForEach-Object Name)
    if ($actual.Count -ne $Properties.Count) {
        throw [ArgumentException]::new("M0V010 $Name must contain exactly: $($Properties -join ', ').")
    }
    $expectedSet = [Collections.Generic.HashSet[string]]::new($Properties, [StringComparer]::Ordinal)
    foreach ($property in $actual) {
        if (-not $expectedSet.Contains([string]$property)) {
            throw [ArgumentException]::new("M0V010 $Name contains unknown property '$property'.")
        }
    }
}

function ConvertTo-M0VisualClientInteger {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Name
    )

    $isInteger = $Value -is [Numerics.BigInteger] -or
        $Value -is [sbyte] -or $Value -is [byte] -or
        $Value -is [int16] -or $Value -is [uint16] -or
        $Value -is [int32] -or $Value -is [uint32] -or
        $Value -is [int64] -or $Value -is [uint64]
    if (-not $isInteger) {
        throw [ArgumentException]::new("M0V011 $Name must be an integer physical pixel coordinate.")
    }
    return [Numerics.BigInteger]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
}

function ConvertTo-M0VisualRational {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Name
    )

    Assert-M0VisualClosedObject -Value $Value -Properties @('numerator', 'denominator') -Name $Name
    if ($Value.numerator -isnot [string] -or
        [string]$Value.numerator -cnotmatch '^-?(?:0|[1-9][0-9]{0,30})$') {
        throw [ArgumentException]::new("M0V012 $Name numerator must be a signed rational decimal string.")
    }
    if ($Value.denominator -isnot [string] -or
        [string]$Value.denominator -cnotmatch '^[1-9][0-9]{0,30}$') {
        throw [ArgumentException]::new("M0V012 $Name denominator must be a positive rational decimal string.")
    }
    return [pscustomobject][ordered]@{
        numerator = [Numerics.BigInteger]::Parse([string]$Value.numerator, [Globalization.CultureInfo]::InvariantCulture)
        denominator = [Numerics.BigInteger]::Parse([string]$Value.denominator, [Globalization.CultureInfo]::InvariantCulture)
    }
}

function Compare-M0VisualRational {
    param(
        [Parameter(Mandatory)] $Left,
        [Parameter(Mandatory)] $Right
    )

    return ($Left.numerator * $Right.denominator).CompareTo($Right.numerator * $Left.denominator)
}

function ConvertTo-M0VisualRationalFloor {
    param([Parameter(Mandatory)] $Value)

    $quotient = [Numerics.BigInteger]::Divide($Value.numerator, $Value.denominator)
    $remainder = [Numerics.BigInteger]::Remainder($Value.numerator, $Value.denominator)
    if ($Value.numerator.Sign -lt 0 -and -not $remainder.IsZero) {
        $quotient -= [Numerics.BigInteger]::One
    }
    return $quotient
}

function ConvertTo-M0VisualRationalCeiling {
    param([Parameter(Mandatory)] $Value)

    $quotient = [Numerics.BigInteger]::Divide($Value.numerator, $Value.denominator)
    $remainder = [Numerics.BigInteger]::Remainder($Value.numerator, $Value.denominator)
    if ($Value.numerator.Sign -gt 0 -and -not $remainder.IsZero) {
        $quotient += [Numerics.BigInteger]::One
    }
    return $quotient
}

function Resolve-M0VisualRoi {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $ControlRect,
        [Parameter(Mandatory)] $ClientRect
    )

    $names = @('left', 'top', 'right', 'bottom')
    Assert-M0VisualClosedObject -Value $ControlRect -Properties $names -Name 'control rectangle'
    Assert-M0VisualClosedObject -Value $ClientRect -Properties $names -Name 'client rectangle'

    $control = [ordered]@{}
    $client = [ordered]@{}
    foreach ($name in $names) {
        $control[$name] = ConvertTo-M0VisualRational -Value $ControlRect.$name -Name "control rectangle $name"
        $client[$name] = ConvertTo-M0VisualClientInteger -Value $ClientRect.$name -Name "client rectangle $name"
    }
    if ((Compare-M0VisualRational -Left $control.left -Right $control.right) -ge 0 -or
        (Compare-M0VisualRational -Left $control.top -Right $control.bottom) -ge 0) {
        throw [ArgumentException]::new('M0V013 control rectangle is empty or has invalid half-open order.')
    }
    if ($client.left -ge $client.right -or $client.top -ge $client.bottom) {
        throw [ArgumentException]::new('M0V013 client rectangle must be a non-empty half-open rectangle.')
    }

    $clientLeft = [pscustomobject]@{ numerator = $client.left; denominator = [Numerics.BigInteger]::One }
    $clientTop = [pscustomobject]@{ numerator = $client.top; denominator = [Numerics.BigInteger]::One }
    $clientRight = [pscustomobject]@{ numerator = $client.right; denominator = [Numerics.BigInteger]::One }
    $clientBottom = [pscustomobject]@{ numerator = $client.bottom; denominator = [Numerics.BigInteger]::One }
    if ((Compare-M0VisualRational -Left $control.right -Right $clientLeft) -le 0 -or
        (Compare-M0VisualRational -Left $control.left -Right $clientRight) -ge 0 -or
        (Compare-M0VisualRational -Left $control.bottom -Right $clientTop) -le 0 -or
        (Compare-M0VisualRational -Left $control.top -Right $clientBottom) -ge 0) {
        throw [ArgumentException]::new('M0V013 original control rectangle does not intersect the client rectangle.')
    }

    $expand = [Numerics.BigInteger]::new(4)
    $left = [Numerics.BigInteger]::Max((ConvertTo-M0VisualRationalFloor $control.left) - $expand, $client.left)
    $top = [Numerics.BigInteger]::Max((ConvertTo-M0VisualRationalFloor $control.top) - $expand, $client.top)
    $right = [Numerics.BigInteger]::Min((ConvertTo-M0VisualRationalCeiling $control.right) + $expand, $client.right)
    $bottom = [Numerics.BigInteger]::Min((ConvertTo-M0VisualRationalCeiling $control.bottom) + $expand, $client.bottom)
    if ($left -ge $right -or $top -ge $bottom) {
        throw [InvalidOperationException]::new('M0V013 rounded expanded ROI is empty after client clamp.')
    }
    return [pscustomobject][ordered]@{
        left = $left
        top = $top
        right = $right
        bottom = $bottom
    }
}

function ConvertTo-M0VisualUnsignedDecimal {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Name,
        [switch] $Positive
    )

    if ($Value -isnot [string] -or [string]$Value -cnotmatch '^(?:0|[1-9][0-9]*)$') {
        throw [ArgumentException]::new("M0V020 $Name must be a canonical unsigned decimal string.")
    }
    $number = [Numerics.BigInteger]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture)
    if ($Positive -and $number.IsZero) {
        throw [ArgumentOutOfRangeException]::new($Name, 'M0V020 value must be positive.')
    }
    return $number
}

function ConvertTo-M0VisualPositiveInt32 {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Name
    )

    $number = ConvertTo-M0VisualClientInteger -Value $Value -Name $Name
    if ($number -le [Numerics.BigInteger]::Zero -or $number -gt [int]::MaxValue) {
        throw [ArgumentOutOfRangeException]::new($Name, "M0V021 $Name must be a positive Int32 value.")
    }
    return [int]$number
}

function ConvertTo-M0VisualFrameMetadata {
    param(
        [Parameter(Mandatory)] $Frame,
        [Parameter(Mandatory)] [int] $ExpectedBufferLength,
        [Parameter(Mandatory)] [Numerics.BigInteger] $RootFrequency,
        [Parameter(Mandatory)] [string] $Name
    )

    Assert-M0VisualClosedObject -Value $Frame -Properties @(
        'frame_ordinal', 'qpc_ticks', 'qpc_frequency', 'accumulated_frames', 'pixels'
    ) -Name $Name
    $ordinal = ConvertTo-M0VisualClientInteger -Value $Frame.frame_ordinal -Name "$Name frame ordinal"
    $ticks = ConvertTo-M0VisualUnsignedDecimal -Value $Frame.qpc_ticks -Name "$Name QPC ticks"
    $frequency = ConvertTo-M0VisualUnsignedDecimal -Value $Frame.qpc_frequency -Name "$Name QPC frequency" -Positive
    if ($frequency -ne $RootFrequency) {
        throw [ArgumentException]::new("M0V022 $Name QPC frequency differs from the root frequency.")
    }
    $accumulated = ConvertTo-M0VisualClientInteger -Value $Frame.accumulated_frames -Name "$Name accumulated frames"
    if ($accumulated -le [Numerics.BigInteger]::Zero) {
        throw [ArgumentOutOfRangeException]::new("$Name accumulated frames", 'M0V022 accumulated frames must be positive.')
    }
    if ($Frame.pixels -isnot [byte[]]) {
        throw [ArgumentException]::new("M0V022 $Name pixels must be one BGRA8 byte array.")
    }
    if ($Frame.pixels.Length -ne $ExpectedBufferLength) {
        throw [ArgumentException]::new("M0V022 $Name BGRA8 buffer length differs from stride_bytes*height.")
    }
    return [pscustomobject][ordered]@{
        ordinal = $ordinal
        ticks = $ticks
        frequency = $frequency
        accumulated_frames = $accumulated
        pixels = [byte[]]$Frame.pixels
    }
}

function Get-M0VisualMedianByte {
    param(
        [Parameter(Mandatory)] [byte] $First,
        [Parameter(Mandatory)] [byte] $Second,
        [Parameter(Mandatory)] [byte] $Third
    )

    $minimum = [Math]::Min([int]$First, [Math]::Min([int]$Second, [int]$Third))
    $maximum = [Math]::Max([int]$First, [Math]::Max([int]$Second, [int]$Third))
    return [byte](([int]$First + [int]$Second + [int]$Third) - $minimum - $maximum)
}

function Resolve-M0VisualDetection {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Width,
        [Parameter(Mandatory)] $Height,
        [Parameter(Mandatory)] $StrideBytes,
        [Parameter(Mandatory)] [object[]] $BaselineFrames,
        [Parameter(Mandatory)] [object[]] $ObservedFrames,
        [Parameter(Mandatory)] $InputQpcTicks,
        [Parameter(Mandatory)] $QpcFrequency
    )

    $widthValue = ConvertTo-M0VisualPositiveInt32 -Value $Width -Name 'width'
    $heightValue = ConvertTo-M0VisualPositiveInt32 -Value $Height -Name 'height'
    $strideValue = ConvertTo-M0VisualPositiveInt32 -Value $StrideBytes -Name 'stride_bytes'
    $minimumStride = [Numerics.BigInteger]$widthValue * 4
    if ([Numerics.BigInteger]$strideValue -lt $minimumStride) {
        throw [ArgumentOutOfRangeException]::new('StrideBytes', 'M0V023 stride_bytes must be at least width*4.')
    }
    $expectedLength = [Numerics.BigInteger]$strideValue * $heightValue
    $pixelCount = [Numerics.BigInteger]$widthValue * $heightValue
    if ($expectedLength -gt [int]::MaxValue -or $pixelCount * 3 -gt [int]::MaxValue) {
        throw [ArgumentOutOfRangeException]::new('StrideBytes', 'M0V023 visual geometry exceeds the synthetic detector memory bound.')
    }
    if ($BaselineFrames.Count -ne 3) {
        throw [ArgumentException]::new('M0V024 baseline must contain exactly three frames preceding input.')
    }
    if ($ObservedFrames.Count -eq 0) {
        throw [ArgumentException]::new('M0V024 observed frames must contain at least one frame after input.')
    }

    $inputTicks = ConvertTo-M0VisualUnsignedDecimal -Value $InputQpcTicks -Name 'input QPC ticks'
    $rootFrequency = ConvertTo-M0VisualUnsignedDecimal -Value $QpcFrequency -Name 'root QPC frequency' -Positive
    $frames = [Collections.Generic.List[object]]::new()
    $previousTicks = $null
    $previousOrdinal = $null
    $allFrames = @($BaselineFrames) + @($ObservedFrames)
    for ($index = 0; $index -lt $allFrames.Count; $index++) {
        $phase = if ($index -lt 3) { 'baseline' } else { 'observed' }
        $metadata = ConvertTo-M0VisualFrameMetadata -Frame $allFrames[$index] `
            -ExpectedBufferLength ([int]$expectedLength) -RootFrequency $rootFrequency -Name "$phase frame $index"
        if (($null -ne $previousTicks -and $metadata.ticks -le $previousTicks) -or
            ($null -ne $previousOrdinal -and $metadata.ordinal -le $previousOrdinal)) {
            throw [ArgumentException]::new('M0V025 combined frame QPC ticks and ordinals must be unique and strictly increasing.')
        }
        if (($index -lt 3 -and $metadata.ticks -ge $inputTicks) -or
            ($index -ge 3 -and $metadata.ticks -le $inputTicks)) {
            throw [ArgumentException]::new('M0V025 frame sequence violates baseline < input < observed causality.')
        }
        $frames.Add($metadata)
        $previousTicks = $metadata.ticks
        $previousOrdinal = $metadata.ordinal
    }

    $medianBgr = [byte[]]::new([int]($pixelCount * 3))
    $baseline = @($frames[0], $frames[1], $frames[2])
    $medianIndex = 0
    for ($row = 0; $row -lt $heightValue; $row++) {
        $rowOffset = $row * $strideValue
        for ($column = 0; $column -lt $widthValue; $column++) {
            $pixelOffset = $rowOffset + ($column * 4)
            for ($channel = 0; $channel -lt 3; $channel++) {
                $medianBgr[$medianIndex] = Get-M0VisualMedianByte `
                    -First $baseline[0].pixels[$pixelOffset + $channel] `
                    -Second $baseline[1].pixels[$pixelOffset + $channel] `
                    -Third $baseline[2].pixels[$pixelOffset + $channel]
                $medianIndex++
            }
        }
    }

    $previousQualified = $false
    $previousObservedOrdinal = $null
    $candidate = $null
    for ($frameIndex = 3; $frameIndex -lt $frames.Count; $frameIndex++) {
        $frame = $frames[$frameIndex]
        $changedCount = 0
        $medianIndex = 0
        for ($row = 0; $row -lt $heightValue; $row++) {
            $rowOffset = $row * $strideValue
            for ($column = 0; $column -lt $widthValue; $column++) {
                $pixelOffset = $rowOffset + ($column * 4)
                $difference = 0
                for ($channel = 0; $channel -lt 3; $channel++) {
                    $difference += [Math]::Abs([int]$frame.pixels[$pixelOffset + $channel] - [int]$medianBgr[$medianIndex])
                    $medianIndex++
                }
                if ($difference -ge 15) {
                    $changedCount++
                }
            }
        }
        $qualified = ([Numerics.BigInteger]$changedCount * 100) -ge ($pixelCount * 2)
        $singleCapture = $frame.accumulated_frames -eq [Numerics.BigInteger]::One
        $ordinalAdjacent = $null -ne $previousObservedOrdinal -and
            $frame.ordinal -eq ($previousObservedOrdinal + [Numerics.BigInteger]::One)

        if (-not $qualified -or -not $singleCapture) {
            $previousQualified = $false
            $candidate = $null
        }
        elseif ($previousQualified -and $ordinalAdjacent) {
            $deltaTicks = $candidate.ticks - $inputTicks
            $valueNanoseconds = [Numerics.BigInteger]::Divide(
                $deltaTicks * [Numerics.BigInteger]::Parse('1000000000'),
                $rootFrequency
            )
            return [pscustomobject][ordered]@{
                detected = $true
                t1_frame_ordinal = $candidate.ordinal
                t1_qpc_ticks = $candidate.ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
                value_ns = $valueNanoseconds.ToString([Globalization.CultureInfo]::InvariantCulture)
                changed_pixel_count = [int]$candidate.changed_pixel_count
            }
        }
        else {
            $candidate = [pscustomobject][ordered]@{
                ordinal = $frame.ordinal
                ticks = $frame.ticks
                changed_pixel_count = $changedCount
            }
            $previousQualified = $true
        }
        $previousObservedOrdinal = $frame.ordinal
    }

    return [pscustomobject][ordered]@{
        detected = $false
        t1_frame_ordinal = $null
        t1_qpc_ticks = $null
        value_ns = $null
        changed_pixel_count = $null
    }
}

function Get-M0VisualUiaFixtureContract {
    param([Parameter(Mandatory)] $Fixture)

    if ($Fixture.locale -cne 'pt-BR' -or
        $Fixture.strings.action_pause -isnot [string] -or
        $Fixture.strings.action_play -isnot [string] -or
        $Fixture.strings.action_pause -cne 'Pausar' -or
        $Fixture.strings.action_play -cne 'Reproduzir') {
        throw [ArgumentException]::new('M0V030 UIA fixture must resolve the frozen pt-BR action_pause/action_play strings.')
    }
    $control = @($Fixture.controls | Where-Object control_id -CEQ 'play-pause')
    if ($control.Count -ne 1 -or $control[0].role -cne 'button' -or $control[0].name_key -cne 'action_pause') {
        throw [ArgumentException]::new('M0V030 UIA fixture play-pause button identity or role differs.')
    }
    $playing = @($Fixture.states | Where-Object state_id -CEQ 'playing')
    if ($playing.Count -ne 1 -or
        $playing[0].playback_state -cne 'playing' -or
        -not [bool]$playing[0].queue_loaded -or
        [string]::IsNullOrEmpty([string]$playing[0].current_occurrence_id) -or
        $playing[0].primary_action_id -cne 'play-pause') {
        throw [ArgumentException]::new('M0V030 UIA fixture playing seed state differs.')
    }
    return [pscustomobject][ordered]@{
        before_name = [string]$Fixture.strings.action_pause
        after_name = [string]$Fixture.strings.action_play
    }
}

function ConvertTo-M0VisualUiaObservation {
    param(
        [Parameter(Mandatory)] $Observation,
        [Parameter(Mandatory)] [Numerics.BigInteger] $RootFrequency,
        [Parameter(Mandatory)] [string] $Name
    )

    Assert-M0VisualClosedObject -Value $Observation -Properties @(
        'automation_id', 'role', 'name', 'has_keyboard_focus',
        'toggle_pattern_queried', 'toggle_pattern_used', 'qpc_ticks', 'qpc_frequency'
    ) -Name $Name
    if ($Observation.automation_id -isnot [string] -or $Observation.automation_id -cne 'play-pause') {
        throw [ArgumentException]::new("M0V031 $Name AutomationId identity must equal play-pause ordinally.")
    }
    if ($Observation.role -isnot [string] -or $Observation.role -cne 'button') {
        throw [ArgumentException]::new("M0V031 $Name role must equal button ordinally.")
    }
    if ($Observation.name -isnot [string]) {
        throw [ArgumentException]::new("M0V031 $Name UIA Name must be a string.")
    }
    if ($Observation.has_keyboard_focus -isnot [bool] -or -not $Observation.has_keyboard_focus) {
        throw [ArgumentException]::new("M0V031 $Name must externally confirm HasKeyboardFocus=true.")
    }
    if ($Observation.toggle_pattern_queried -isnot [bool] -or
        $Observation.toggle_pattern_used -isnot [bool] -or
        $Observation.toggle_pattern_queried -or $Observation.toggle_pattern_used) {
        throw [ArgumentException]::new("M0V031 $Name must never query or use TogglePattern.")
    }
    $frequency = ConvertTo-M0VisualUnsignedDecimal -Value $Observation.qpc_frequency -Name "$Name QPC frequency" -Positive
    if ($frequency -ne $RootFrequency) {
        throw [ArgumentException]::new("M0V031 $Name QPC frequency differs from the root frequency.")
    }
    return [pscustomobject][ordered]@{
        name = [string]$Observation.name
        ticks = ConvertTo-M0VisualUnsignedDecimal -Value $Observation.qpc_ticks -Name "$Name QPC ticks"
    }
}

function Assert-M0VisualUiaTransition {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $BeforeObservation,
        [Parameter(Mandatory)] [object[]] $AfterObservations,
        [Parameter(Mandatory)] $InputQpcTicks,
        [Parameter(Mandatory)] $QpcFrequency,
        [Parameter(Mandatory)] $Fixture
    )

    if ($AfterObservations.Count -eq 0) {
        throw [ArgumentException]::new('M0V032 at least one post-input UIA observation is required.')
    }
    $fixtureContract = Get-M0VisualUiaFixtureContract -Fixture $Fixture
    $inputTicks = ConvertTo-M0VisualUnsignedDecimal -Value $InputQpcTicks -Name 'input QPC ticks'
    $rootFrequency = ConvertTo-M0VisualUnsignedDecimal -Value $QpcFrequency -Name 'root QPC frequency' -Positive
    $before = ConvertTo-M0VisualUiaObservation -Observation $BeforeObservation `
        -RootFrequency $rootFrequency -Name 'before-input UIA observation'
    if ($before.ticks -ge $inputTicks) {
        throw [ArgumentException]::new('M0V032 before-input UIA observation must strictly precede input QPC.')
    }
    if ($before.name -cne $fixtureContract.before_name) {
        throw [ArgumentException]::new('M0V032 before-input UIA Name must resolve action_pause ordinally.')
    }

    $validatedAfter = [Collections.Generic.List[object]]::new()
    $previousTicks = $null
    for ($index = 0; $index -lt $AfterObservations.Count; $index++) {
        $observation = ConvertTo-M0VisualUiaObservation -Observation $AfterObservations[$index] `
            -RootFrequency $rootFrequency -Name "after-input UIA observation $index"
        if ($observation.ticks -le $inputTicks) {
            throw [ArgumentException]::new('M0V032 every after-input UIA observation must strictly follow input QPC causally.')
        }
        if ($null -ne $previousTicks -and $observation.ticks -le $previousTicks) {
            throw [ArgumentException]::new('M0V032 after-input UIA QPC ticks must be unique and strictly increasing.')
        }
        if ($observation.name -cne $fixtureContract.before_name -and
            $observation.name -cne $fixtureContract.after_name) {
            throw [ArgumentException]::new('M0V032 after-input UIA Name is not the ordinal action_pause/action_play transition.')
        }
        $validatedAfter.Add($observation)
        $previousTicks = $observation.ticks
    }

    $confirmation = @($validatedAfter | Where-Object name -CEQ $fixtureContract.after_name | Select-Object -First 1)
    if ($confirmation.Count -ne 1) {
        throw [InvalidOperationException]::new('M0V033 UIA Name transition from action_pause to action_play was not confirmed.')
    }
    $deltaTicks = $confirmation[0].ticks - $inputTicks
    $nanosecondsPerSecond = [Numerics.BigInteger]::Parse('1000000000')
    $timeoutNanoseconds = [Numerics.BigInteger]::Parse('2000000000')
    if ($deltaTicks * $nanosecondsPerSecond -gt $rootFrequency * $timeoutNanoseconds) {
        throw [TimeoutException]::new('M0V033 UIA Name transition exceeds the inclusive 2 s timeout.')
    }
    $valueNanoseconds = [Numerics.BigInteger]::Divide($deltaTicks * $nanosecondsPerSecond, $rootFrequency)
    return [pscustomobject][ordered]@{
        confirmed = $true
        confirmation_qpc_ticks = $confirmation[0].ticks.ToString([Globalization.CultureInfo]::InvariantCulture)
        value_ns = $valueNanoseconds.ToString([Globalization.CultureInfo]::InvariantCulture)
    }
}

Export-ModuleMember -Function @(
    'Assert-M0VisualArtifacts',
    'Assert-M0VisualUiaTransition',
    'Resolve-M0VisualDetection',
    'Resolve-M0VisualRoi'
)
