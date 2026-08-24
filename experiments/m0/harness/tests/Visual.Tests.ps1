$visualModulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Visual.psm1'
$protocolRoot = Join-Path $RepositoryRoot 'experiments\m0\protocol'
$visualDescriptorPath = Join-Path $protocolRoot 'visual.v1.json'
$visualSidecarPath = Join-Path $protocolRoot 'visual.v1.sha256'
$visualGoldenPath = Join-Path $protocolRoot 'goldens\visual.v1.json'
$visualSchemaPath = Join-Path $protocolRoot 'schemas\visual.schema.json'
$visualGoldenSchemaPath = Join-Path $protocolRoot 'schemas\visual-golden.schema.json'
$visualRunnerPath = Join-Path $RepositoryRoot 'scripts\m0\Test-M0Visual.ps1'

Test-Case 'The frozen visual contract exists before synthetic observations are accepted' {
    foreach ($path in @(
        $visualModulePath,
        $visualDescriptorPath,
        $visualSidecarPath,
        $visualGoldenPath,
        $visualSchemaPath,
        $visualGoldenSchemaPath,
        $visualRunnerPath
    )) {
        Assert-True (Test-Path -LiteralPath $path -PathType Leaf) "Missing visual contract '$path'."
    }
}

Import-Module $visualModulePath -Force -ErrorAction Stop
$visualModule = Get-Module -Name M0.Visual

Test-Case 'The visual module exports only its approved synthetic API' {
    Assert-SequenceEqual -Actual @($visualModule.ExportedFunctions.Keys | Sort-Object) -Expected @(
        'Assert-M0VisualArtifacts',
        'Assert-M0VisualUiaTransition',
        'Resolve-M0VisualDetection',
        'Resolve-M0VisualRoi'
    )
}

Test-Case 'The visual descriptor, sidecar and golden form one anchored closed contract' {
    $result = Assert-M0VisualArtifacts -ProtocolRoot $protocolRoot
    Assert-Equal -Actual $result.visual_id -Expected 'M0-VISUAL-1'
    Assert-Equal -Actual $result.golden_id -Expected 'M0-VISUAL-GOLDEN-1'
    Assert-True ($result.descriptor_sha256 -match '^[a-f0-9]{64}$')
    Assert-True ($result.golden_sha256 -match '^[a-f0-9]{64}$')
}

Test-Case 'A fully reanchored visual golden with a false expected result is rejected semantically' {
    $isolatedRoot = Join-Path $TestTempRoot 'visual-semantic-tamper'
    $isolatedM0 = Join-Path $isolatedRoot 'experiments\m0'
    New-Item -ItemType Directory -Path $isolatedM0 -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\protocol') -Destination $isolatedM0 -Recurse
    $isolatedShared = Join-Path $isolatedM0 'shared'
    New-Item -ItemType Directory -Path $isolatedShared -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture') -Destination $isolatedShared -Recurse
    $isolatedProtocol = Join-Path $isolatedM0 'protocol'
    $utf8 = [Text.UTF8Encoding]::new($false)

    $goldenPath = Join-Path $isolatedProtocol 'goldens\visual.v1.json'
    $golden = Get-Content -LiteralPath $goldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $golden.roi_vectors[0].expected.left = [int]$golden.roi_vectors[0].expected.left + 1
    $goldenText = ($golden | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($goldenPath, $goldenText, $utf8)
    $goldenHash = (Get-FileHash -LiteralPath $goldenPath -Algorithm SHA256).Hash.ToLowerInvariant()

    $descriptorPath = Join-Path $isolatedProtocol 'visual.v1.json'
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $descriptor.golden_sha256 = $goldenHash
    $descriptorText = ($descriptor | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($descriptorPath, $descriptorText, $utf8)
    $descriptorHash = (Get-FileHash -LiteralPath $descriptorPath -Algorithm SHA256).Hash
    [IO.File]::WriteAllText((Join-Path $isolatedProtocol 'visual.v1.sha256'), "$($descriptorHash.ToLowerInvariant())  visual.v1.json`n", $utf8)

    $protocolPath = Join-Path $isolatedProtocol 'protocol.v1.json'
    $protocol = Get-Content -LiteralPath $protocolPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $protocol.visual_descriptor_sha256 = $descriptorHash
    $protocolText = ($protocol | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($protocolPath, $protocolText, $utf8)

    Assert-Throws { $null = Assert-M0VisualArtifacts -ProtocolRoot $isolatedProtocol } 'golden|ROI|expected|semantic'
}

Test-Case 'Visual ROI vectors use rational floor-ceil, expand four and clamp half-open' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.roi_vectors)) {
        $actual = Resolve-M0VisualRoi -ControlRect $vector.control_rect -ClientRect $vector.client_rect
        Assert-Equal -Actual $actual.left -Expected $vector.expected.left "ROI left differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.top -Expected $vector.expected.top "ROI top differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.right -Expected $vector.expected.right "ROI right differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.bottom -Expected $vector.expected.bottom "ROI bottom differs for '$($vector.case_id)'."
    }
}

Test-Case 'Visual ROI rejects malformed, empty and originally out-of-client rectangles' {
    $unit = [pscustomobject]@{ numerator = '1'; denominator = '1' }
    $two = [pscustomobject]@{ numerator = '2'; denominator = '1' }
    $client = [pscustomobject]@{ left = 0; top = 0; right = 10; bottom = 10 }

    $zeroDenominator = [pscustomobject]@{
        left = [pscustomobject]@{ numerator = '0'; denominator = '0' }
        top = $unit
        right = $two
        bottom = $two
    }
    Assert-Throws { Resolve-M0VisualRoi -ControlRect $zeroDenominator -ClientRect $client } 'denominator'

    $reversed = [pscustomobject]@{ left = $two; top = $unit; right = $unit; bottom = $two }
    Assert-Throws { Resolve-M0VisualRoi -ControlRect $reversed -ClientRect $client } 'empty|order'

    $outside = [pscustomobject]@{
        left = [pscustomobject]@{ numerator = '11'; denominator = '1' }
        top = $unit
        right = [pscustomobject]@{ numerator = '12'; denominator = '1' }
        bottom = $two
    }
    Assert-Throws { Resolve-M0VisualRoi -ControlRect $outside -ClientRect $client } 'intersect'

    $emptyClient = [pscustomobject]@{ left = 0; top = 0; right = 0; bottom = 10 }
    $valid = [pscustomobject]@{ left = $unit; top = $unit; right = $two; bottom = $two }
    Assert-Throws { Resolve-M0VisualRoi -ControlRect $valid -ClientRect $emptyClient } 'client'
}

function ConvertTo-M0VisualTestFrame {
    param([Parameter(Mandatory)] $GoldenFrame)

    return [pscustomobject][ordered]@{
        frame_ordinal = $GoldenFrame.frame_ordinal
        qpc_ticks = [string]$GoldenFrame.qpc_ticks
        qpc_frequency = [string]$GoldenFrame.qpc_frequency
        accumulated_frames = $GoldenFrame.accumulated_frames
        pixels = [Convert]::FromHexString([string]$GoldenFrame.bgra8_hex)
    }
}

function New-M0VisualTestFrame {
    param(
        [Parameter(Mandatory)] [int] $Ordinal,
        [Parameter(Mandatory)] [string] $Ticks,
        [Parameter(Mandatory)] [byte[]] $Pixels,
        [string] $Frequency = '10',
        [int] $AccumulatedFrames = 1
    )

    return [pscustomobject][ordered]@{
        frame_ordinal = $Ordinal
        qpc_ticks = $Ticks
        qpc_frequency = $Frequency
        accumulated_frames = $AccumulatedFrames
        pixels = $Pixels
    }
}

function New-M0VisualZeroBaseline {
    param(
        [Parameter(Mandatory)] [int] $BufferLength,
        [string] $Frequency = '10'
    )

    return @(
        New-M0VisualTestFrame -Ordinal -2 -Ticks '0' -Frequency $Frequency -Pixels ([byte[]]::new($BufferLength))
        New-M0VisualTestFrame -Ordinal -1 -Ticks '1' -Frequency $Frequency -Pixels ([byte[]]::new($BufferLength))
        New-M0VisualTestFrame -Ordinal 0 -Ticks '2' -Frequency $Frequency -Pixels ([byte[]]::new($BufferLength))
    )
}

Test-Case 'Visual detection reproduces the anchored integer and BigInteger golden vectors' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.detection_vectors)) {
        $baseline = @($vector.baseline | ForEach-Object { ConvertTo-M0VisualTestFrame $_ })
        $observed = @($vector.observed | ForEach-Object { ConvertTo-M0VisualTestFrame $_ })
        $actual = Resolve-M0VisualDetection `
            -Width $vector.width -Height $vector.height -StrideBytes $vector.stride_bytes `
            -BaselineFrames $baseline -ObservedFrames $observed `
            -InputQpcTicks ([string]$vector.input_qpc_ticks) -QpcFrequency ([string]$vector.qpc_frequency)
        Assert-Equal -Actual $actual.detected -Expected $vector.expected.detected "Detection differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.t1_frame_ordinal -Expected $vector.expected.t1_frame_ordinal "t1 ordinal differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.t1_qpc_ticks -Expected $vector.expected.t1_qpc_ticks "t1 QPC differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.value_ns -Expected $vector.expected.value_ns "QPC floor differs for '$($vector.case_id)'."
        Assert-Equal -Actual $actual.changed_pixel_count -Expected $vector.expected.changed_pixel_count "Changed count differs for '$($vector.case_id)'."
    }
}

Test-Case 'Visual baseline is the independent B G R median and alpha-only changes are ignored' {
    $medianBaseline = @(
        New-M0VisualTestFrame -Ordinal -2 -Ticks '0' -Pixels ([byte[]](0, 0, 255, 10))
        New-M0VisualTestFrame -Ordinal -1 -Ticks '1' -Pixels ([byte[]](0, 255, 0, 20))
        New-M0VisualTestFrame -Ordinal 0 -Ticks '2' -Pixels ([byte[]](255, 0, 0, 30))
    )
    $atMedianObserved = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels ([byte[]](0, 0, 0, 0))
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels ([byte[]](0, 0, 0, 255))
    )
    $atMedian = Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 `
        -BaselineFrames $medianBaseline -ObservedFrames $atMedianObserved -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $atMedian.detected) 'Independent channel medians must be zero despite different per-frame outliers.'

    $aboveMedianObserved = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels ([byte[]](15, 0, 0, 0))
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels ([byte[]](15, 0, 0, 255))
    )
    $aboveMedian = Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 `
        -BaselineFrames $medianBaseline -ObservedFrames $aboveMedianObserved -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True $aboveMedian.detected 'RGB absolute sum 15 from the independent median must qualify.'

    $alphaBaseline = @(
        New-M0VisualTestFrame -Ordinal -2 -Ticks '0' -Pixels ([byte[]](0, 0, 0, 0))
        New-M0VisualTestFrame -Ordinal -1 -Ticks '1' -Pixels ([byte[]](0, 0, 0, 127))
        New-M0VisualTestFrame -Ordinal 0 -Ticks '2' -Pixels ([byte[]](0, 0, 0, 255))
    )
    $alphaObserved = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels ([byte[]](0, 0, 0, 255))
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels ([byte[]](0, 0, 0, 0))
    )
    $alpha = Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 `
        -BaselineFrames $alphaBaseline -ObservedFrames $alphaObserved -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $alpha.detected) 'Alpha-only changes must be ignored.'
}

Test-Case 'Visual row padding is ignored while geometry and buffer length stay explicit' {
    $baseline = New-M0VisualZeroBaseline -BufferLength 12
    $paddingOne = [byte[]](0, 0, 0, 0, 255, 254, 0, 0, 0, 0, 253, 252)
    $paddingTwo = [byte[]](0, 0, 0, 0, 1, 2, 0, 0, 0, 0, 3, 4)
    $observed = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $paddingOne
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $paddingTwo
    )
    $actual = Resolve-M0VisualDetection -Width 1 -Height 2 -StrideBytes 6 `
        -BaselineFrames $baseline -ObservedFrames $observed -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $actual.detected) 'Row-padding-only changes must be ignored.'

    Assert-Throws {
        Resolve-M0VisualDetection -Width 0 -Height 1 -StrideBytes 4 `
            -BaselineFrames (New-M0VisualZeroBaseline -BufferLength 4) -ObservedFrames $observed `
            -InputQpcTicks '3' -QpcFrequency '10'
    } 'width|positive'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 0 -StrideBytes 4 `
            -BaselineFrames (New-M0VisualZeroBaseline -BufferLength 4) -ObservedFrames $observed `
            -InputQpcTicks '3' -QpcFrequency '10'
    } 'height|positive'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 2 -Height 1 -StrideBytes 7 `
            -BaselineFrames (New-M0VisualZeroBaseline -BufferLength 7) -ObservedFrames $observed `
            -InputQpcTicks '3' -QpcFrequency '10'
    } 'stride'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 2 -StrideBytes 6 `
            -BaselineFrames (New-M0VisualZeroBaseline -BufferLength 11) -ObservedFrames $observed `
            -InputQpcTicks '3' -QpcFrequency '10'
    } 'length'
}

Test-Case 'Visual thresholds use RGB sum 15 and exact two-per-hundred cross multiplication' {
    $baselineOne = New-M0VisualZeroBaseline -BufferLength 4
    $below = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels ([byte[]](14, 0, 0, 0))
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels ([byte[]](14, 0, 0, 0))
    )
    $belowResult = Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 `
        -BaselineFrames $baselineOne -ObservedFrames $below -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $belowResult.detected) 'RGB absolute sum 14 must remain unchanged.'

    $hundredBaseline = New-M0VisualZeroBaseline -BufferLength 400
    $oneChangedFirst = [byte[]]::new(400)
    $oneChangedSecond = [byte[]]::new(400)
    $oneChangedFirst[0] = 15
    $oneChangedSecond[0] = 15
    $onePercent = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $oneChangedFirst
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $oneChangedSecond
    )
    $onePercentResult = Resolve-M0VisualDetection -Width 100 -Height 1 -StrideBytes 400 `
        -BaselineFrames $hundredBaseline -ObservedFrames $onePercent -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $onePercentResult.detected) 'One changed pixel out of one hundred must not qualify.'

    $twoChangedFirst = [byte[]]::new(400)
    $twoChangedSecond = [byte[]]::new(400)
    $twoChangedFirst[0] = 15
    $twoChangedFirst[4] = 15
    $twoChangedSecond[0] = 15
    $twoChangedSecond[4] = 15
    $twoPercent = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $twoChangedFirst
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $twoChangedSecond
    )
    $twoPercentResult = Resolve-M0VisualDetection -Width 100 -Height 1 -StrideBytes 400 `
        -BaselineFrames $hundredBaseline -ObservedFrames $twoPercent -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True $twoPercentResult.detected 'Two changed pixels out of one hundred must qualify exactly.'

    $fiftyOneBaseline = New-M0VisualZeroBaseline -BufferLength 204
    $oneOfFiftyOneFirst = [byte[]]::new(204)
    $oneOfFiftyOneSecond = [byte[]]::new(204)
    $oneOfFiftyOneFirst[0] = 15
    $oneOfFiftyOneSecond[0] = 15
    $oneOfFiftyOne = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $oneOfFiftyOneFirst
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $oneOfFiftyOneSecond
    )
    $underTwoPercent = Resolve-M0VisualDetection -Width 51 -Height 1 -StrideBytes 204 `
        -BaselineFrames $fiftyOneBaseline -ObservedFrames $oneOfFiftyOne -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True (-not $underTwoPercent.detected) 'One of fifty-one must fail the exact integer cross multiplication.'
}

Test-Case 'Visual persistence resets on accumulated frames and ordinal gaps and buffers t1 from the first frame' {
    $baseline = New-M0VisualZeroBaseline -BufferLength 4
    $changed = [byte[]](15, 0, 0, 0)
    $observed = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $changed -AccumulatedFrames 2
        New-M0VisualTestFrame -Ordinal 4 -Ticks '6' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 5 -Ticks '7' -Pixels $changed
    )
    $actual = Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 `
        -BaselineFrames $baseline -ObservedFrames $observed -InputQpcTicks '3' -QpcFrequency '10'
    Assert-True $actual.detected
    Assert-Equal -Actual $actual.t1_frame_ordinal -Expected 4
    Assert-Equal -Actual $actual.t1_qpc_ticks -Expected '6'
    Assert-Equal -Actual $actual.value_ns -Expected '300000000'
}

Test-Case 'Visual frame validation rejects noncausal, duplicate or inconsistent QPC and ordinals' {
    $baseline = New-M0VisualZeroBaseline -BufferLength 4
    $changed = [byte[]](15, 0, 0, 0)
    $validObserved = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $changed
    )

    $duplicateTicks = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 2 -Ticks '4' -Pixels $changed
    )
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $duplicateTicks -InputQpcTicks '3' -QpcFrequency '10'
    } 'QPC|increasing'

    $duplicateOrdinals = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 1 -Ticks '5' -Pixels $changed
    )
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $duplicateOrdinals -InputQpcTicks '3' -QpcFrequency '10'
    } 'ordinal|increasing'

    $equalInput = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '3' -Pixels $changed
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $changed
    )
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $equalInput -InputQpcTicks '3' -QpcFrequency '10'
    } 'input|causal'

    $wrongFrequency = @(
        New-M0VisualTestFrame -Ordinal 1 -Ticks '4' -Pixels $changed -Frequency '9'
        New-M0VisualTestFrame -Ordinal 2 -Ticks '5' -Pixels $changed -Frequency '9'
    )
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $wrongFrequency -InputQpcTicks '3' -QpcFrequency '10'
    } 'frequency'

    $badBaseline = @($baseline)
    $badBaseline[2] = New-M0VisualTestFrame -Ordinal 0 -Ticks '3' -Pixels ([byte[]]::new(4))
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $badBaseline `
            -ObservedFrames $validObserved -InputQpcTicks '3' -QpcFrequency '10'
    } 'baseline|input|causal'

    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames @($baseline[0], $baseline[1]) `
            -ObservedFrames $validObserved -InputQpcTicks '3' -QpcFrequency '10'
    } 'three|baseline'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames @() -InputQpcTicks '3' -QpcFrequency '10'
    } 'observed'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $validObserved -InputQpcTicks '3.0' -QpcFrequency '10'
    } 'canonical|decimal'
    Assert-Throws {
        Resolve-M0VisualDetection -Width 1 -Height 1 -StrideBytes 4 -BaselineFrames $baseline `
            -ObservedFrames $validObserved -InputQpcTicks '3' -QpcFrequency '0'
    } 'positive'
}

function Copy-M0VisualTestObject {
    param([Parameter(Mandatory)] $Value)

    return ($Value | ConvertTo-Json -Depth 100 -Compress | ConvertFrom-Json -Depth 100 -DateKind String)
}

function Invoke-M0VisualTestChildProcess {
    param([Parameter(Mandatory)] [string] $ScriptPath)

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = (Get-Process -Id $PID).Path
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-NonInteractive', '-File', $ScriptPath)) {
        $null = $startInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($startInfo)
    try {
        $output = $child.StandardOutput.ReadToEnd() + $child.StandardError.ReadToEnd()
        $child.WaitForExit()
        return [pscustomobject]@{ exit_code = $child.ExitCode; output = $output }
    }
    finally {
        $child.Dispose()
    }
}

Test-Case 'The public Visual entrypoint rejects homonymous preloaded modules before use' {
    foreach ($moduleName in @('M0.Harness', 'M0.Visual')) {
        $fakeRoot = Join-Path $TestTempRoot "visual-preloaded-$moduleName"
        New-Item -ItemType Directory -Path $fakeRoot | Out-Null
        $fakePath = Join-Path $fakeRoot "$moduleName.psm1"
        $fakeSource = if ($moduleName -ceq 'M0.Harness') {
            "function Assert-M0HarnessManifest { throw 'FAKE-HARNESS-CALLED' }`nExport-ModuleMember -Function Assert-M0HarnessManifest`n"
        }
        else {
            "function Assert-M0VisualArtifacts { throw 'FAKE-VISUAL-CALLED' }`nExport-ModuleMember -Function Assert-M0VisualArtifacts`n"
        }
        [IO.File]::WriteAllText($fakePath, $fakeSource, [Text.UTF8Encoding]::new($false))
        $childPath = Join-Path $TestTempRoot "visual-preloaded-$moduleName.ps1"
        $childSource = @"
`$ErrorActionPreference = 'Stop'
Import-Module -Name '$($fakePath.Replace("'", "''"))' -ErrorAction Stop
& '$($visualRunnerPath.Replace("'", "''"))'
"@
        [IO.File]::WriteAllText($childPath, $childSource, [Text.UTF8Encoding]::new($false))
        $result = Invoke-M0VisualTestChildProcess -ScriptPath $childPath
        Assert-True ($result.exit_code -ne 0) "The Visual child with preloaded '$moduleName' unexpectedly succeeded."
        Assert-True ($result.output -match 'M0V040') "Preloaded '$moduleName' was not rejected: $($result.output)"
        Assert-True ($result.output -notmatch 'FAKE-(HARNESS|VISUAL)-CALLED') "Preloaded '$moduleName' executed: $($result.output)"
    }
}

Test-Case 'The public Visual entrypoint validates all trusted common inputs before the detector' {
    $runnerSource = Get-Content -LiteralPath $visualRunnerPath -Raw -Encoding UTF8
    Assert-True ($runnerSource -match 'M0\.Harness\\Get-M0TrustedCommonInputs') `
        'Test-M0Visual must validate the complete trusted common-input chain.'
    Assert-True ($runnerSource.IndexOf('M0.Harness\Get-M0TrustedCommonInputs', [StringComparison]::Ordinal) -lt `
        $runnerSource.IndexOf('M0.Visual\Assert-M0VisualArtifacts', [StringComparison]::Ordinal)) `
        'Trusted common inputs must be validated before the public detector artifact call.'
}

Test-Case 'Visual UIA golden resolves pt-BR Name and accepts the inclusive two-second boundary' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $fixturePath = Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture\fixture.v1.json'
    $fixture = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    foreach ($vector in @($golden.uia_vectors)) {
        Assert-Equal -Actual $vector.initial_state_id -Expected 'playing'
        $actual = Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($vector.after) `
            -InputQpcTicks ([string]$vector.input_qpc_ticks) -QpcFrequency ([string]$vector.qpc_frequency) -Fixture $fixture
        Assert-True $actual.confirmed
        Assert-Equal -Actual $actual.confirmation_qpc_ticks -Expected $vector.expected_confirmation_qpc_ticks
        Assert-Equal -Actual $actual.value_ns -Expected '2000000000'
    }
}

Test-Case 'Visual UIA never queries or uses TogglePattern' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $fixturePath = Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture\fixture.v1.json'
    $fixture = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $vector = $golden.uia_vectors[0]

    $queriedBefore = Copy-M0VisualTestObject $vector.before
    $queriedBefore.toggle_pattern_queried = $true
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $queriedBefore -AfterObservations @($vector.after) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'TogglePattern|query'

    $usedAfter = Copy-M0VisualTestObject $vector.after
    $usedAfter[1].toggle_pattern_used = $true
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($usedAfter) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'TogglePattern|use'
}

Test-Case 'Visual UIA identity role Name and keyboard focus are exact ordinal external observations' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $fixturePath = Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture\fixture.v1.json'
    $fixture = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $vector = $golden.uia_vectors[0]

    foreach ($mutation in @(
        [pscustomobject]@{ property = 'automation_id'; value = 'Play-Pause' },
        [pscustomobject]@{ property = 'role'; value = 'Button' },
        [pscustomobject]@{ property = 'name'; value = 'reproduzir' },
        [pscustomobject]@{ property = 'has_keyboard_focus'; value = $false }
    )) {
        $after = Copy-M0VisualTestObject $vector.after
        $after[1].($mutation.property) = $mutation.value
        Assert-Throws {
            Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($after) `
                -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
        } 'identity|role|Name|focus|transition'
    }

    foreach ($mutation in @(
        [pscustomobject]@{ property = 'automation_id'; value = 'Play-Pause' },
        [pscustomobject]@{ property = 'role'; value = 'Button' },
        [pscustomobject]@{ property = 'name'; value = 'Reproduzir' },
        [pscustomobject]@{ property = 'has_keyboard_focus'; value = $false }
    )) {
        $before = Copy-M0VisualTestObject $vector.before
        $before.($mutation.property) = $mutation.value
        Assert-Throws {
            Assert-M0VisualUiaTransition -BeforeObservation $before -AfterObservations @($vector.after) `
                -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
        } 'identity|role|Name|focus|before'
    }

    $wrongSeed = Copy-M0VisualTestObject $fixture
    $playing = @($wrongSeed.states | Where-Object state_id -CEQ 'playing')[0]
    $playing.playback_state = 'paused'
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($vector.after) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $wrongSeed
    } 'playing|seed'
}

Test-Case 'Visual UIA validates strict causal QPC order and rejects one tick beyond two seconds' {
    $golden = Get-Content -LiteralPath $visualGoldenPath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $fixturePath = Join-Path $RepositoryRoot 'experiments\m0\shared\shell-fixture\fixture.v1.json'
    $fixture = Get-Content -LiteralPath $fixturePath -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $vector = $golden.uia_vectors[0]

    $late = Copy-M0VisualTestObject $vector.after
    $late[1].qpc_ticks = '20000101'
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($late) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } '2 s|timeout'

    $duplicate = Copy-M0VisualTestObject $vector.after
    $duplicate[1].qpc_ticks = $duplicate[0].qpc_ticks
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($duplicate) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'QPC|increasing'

    $equalInput = Copy-M0VisualTestObject $vector.before
    $equalInput.qpc_ticks = $vector.input_qpc_ticks
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $equalInput -AfterObservations @($vector.after) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'before|input|causal'

    $wrongFrequency = Copy-M0VisualTestObject $vector.after
    $wrongFrequency[0].qpc_frequency = '9999999'
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations @($wrongFrequency) `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'frequency'

    $noTransition = @(Copy-M0VisualTestObject $vector.after[0])
    Assert-Throws {
        Assert-M0VisualUiaTransition -BeforeObservation $vector.before -AfterObservations $noTransition `
            -InputQpcTicks $vector.input_qpc_ticks -QpcFrequency $vector.qpc_frequency -Fixture $fixture
    } 'transition|Name'
}

Test-Case 'Equal-QPC visual-before-UIA ordering remains a frozen collector boundary' {
    $artifacts = Assert-M0VisualArtifacts -ProtocolRoot $protocolRoot
    Assert-Equal -Actual $artifacts.descriptor.diagnostic_contract.observation_buffer_order -Expected 'ascending-qpc-before-append'
    Assert-Equal -Actual $artifacts.descriptor.diagnostic_contract.equal_qpc_order -Expected 'visual-change-before-uia-change'
}
