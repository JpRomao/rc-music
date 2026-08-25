Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0PilotUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0PilotModuleSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
$script:M0PilotRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')).TrimEnd('\', '/')
$script:M0PilotProtocolRoot = Join-Path $script:M0PilotRepositoryRoot 'experiments\m0\protocol'
$script:M0PilotInputSchemaPath = Join-Path $script:M0PilotProtocolRoot 'schemas\pilot-summary-input.schema.json'
$script:M0PilotSummarySchemaPath = Join-Path $script:M0PilotProtocolRoot 'schemas\pilot-summary.schema.json'
$script:M0PilotIntentSchemaPath = Join-Path $script:M0PilotProtocolRoot 'schemas\pilot-intent.schema.json'
$script:M0PilotProcessPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'M0.Process.psm1'))
$script:M0PilotEvidencePath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'M0.Evidence.psm1'))
$script:M0PilotArtifactsTestHook = $null
$script:M0PilotPublicationTestHook = $null
$script:M0PilotLiveTestHook = $null
$script:M0PilotRecoveryTestHook = $null

function Get-M0PilotPhysicalTrustModule {
    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new(
            'M0I020 physical Pilot file identity is not proven outside Windows; pure Pilot APIs remain available.'
        )
    }
    $comparison = [StringComparison]::OrdinalIgnoreCase
    $modules = @(Get-Module -All -Name 'M0.Process')
    if ($modules.Count -gt 1 -or
        ($modules.Count -eq 1 -and -not [IO.Path]::GetFullPath($modules[0].Path).Equals($script:M0PilotProcessPath, $comparison))) {
        throw [IO.InvalidDataException]::new('M0I020 physical Pilot trust refuses a foreign or ambiguous M0.Process module.')
    }
    if ($modules.Count -eq 0) {
        foreach ($nativeTypeName in @(
            'M0ProcessNativeV1.PilotSession',
            'M0ProcessNativeV1.ProcessCapability',
            'M0ProcessNativeV1.TrustedFile'
        )) {
            if ($nativeTypeName -as [type]) {
                throw [IO.InvalidDataException]::new(
                    "M0I020 physical Pilot trust refuses preloaded native type '$nativeTypeName' without M0.Process."
                )
            }
        }
        Import-Module -Name $script:M0PilotProcessPath -ErrorAction Stop
        $modules = @(Get-Module -All -Name 'M0.Process')
    }
    if ($modules.Count -ne 1 -or
        -not [IO.Path]::GetFullPath($modules[0].Path).Equals($script:M0PilotProcessPath, $comparison) -or
        -not ('M0ProcessNativeV1.TrustedFile' -as [type])) {
        throw [IO.InvalidDataException]::new('M0I020 physical Pilot trust could not bind exact M0.Process native identity.')
    }
    $contract = & $modules[0] { Assert-M0ProcessNativeContract }
    if ([string]$contract.contract_id -cne 'm0-process-native-v1' -or [int]$contract.type_count -ne 3 -or
        [string]$contract.module_instance_id -cne (& $modules[0] { $script:M0ProcessModuleInstanceId }) -or
        [string]$contract.module_sha256 -notmatch '^[a-f0-9]{64}$') {
        throw [IO.InvalidDataException]::new('M0I020 physical Pilot trust rejected M0.Process native contract identity.')
    }
    return $modules[0]
}

function Read-M0PilotTrustedFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name,
        [switch] $KeepOpen,
        [switch] $AllowEmpty,
        [long] $MaximumBytes = 33554432
    )

    $null = Get-M0PilotPhysicalTrustModule
    $path = [IO.Path]::GetFullPath($LiteralPath)
    $comparison = [StringComparison]::OrdinalIgnoreCase
    if (-not $path.StartsWith($script:M0PilotRepositoryRoot + [IO.Path]::DirectorySeparatorChar, $comparison)) {
        throw [UnauthorizedAccessException]::new("M0I020 trusted $Name escaped the module repository.")
    }
    $relative = [IO.Path]::GetRelativePath($script:M0PilotRepositoryRoot, $path)
    $segments = $relative.Split([char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar),
        [StringSplitOptions]::RemoveEmptyEntries)
    $ancestor = $script:M0PilotRepositoryRoot
    for ($index = 0; $index -lt $segments.Count - 1; $index++) {
        $ancestor = Join-Path $ancestor $segments[$index]
        if (-not (Test-Path -LiteralPath $ancestor -PathType Container)) {
            throw [IO.DirectoryNotFoundException]::new("M0I020 trusted $Name ancestor is missing: '$ancestor'.")
        }
        $item = Get-Item -LiteralPath $ancestor -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            throw [IO.InvalidDataException]::new("M0I020 links are forbidden in trusted $Name ancestors: '$ancestor'.")
        }
    }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0I020 required $Name is missing: '$path'.")
    }
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $path)
        $item = Get-Item -LiteralPath $path -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            throw [IO.InvalidDataException]::new("M0I020 links are forbidden for trusted $Name '$path'.")
        }
        if ($stream.Length -gt $MaximumBytes) {
            throw [IO.InvalidDataException]::new("M0I020 trusted $Name exceeds its closed size bound.")
        }
        $bytes = [byte[]]::new($stream.Length)
        if ($bytes.Length -gt 0) { $stream.ReadExactly($bytes, 0, $bytes.Length) }
        [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $path)
        if ((-not $AllowEmpty -and $bytes.Length -eq 0) -or
            ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
            throw [IO.InvalidDataException]::new("M0I020 trusted $Name must be nonempty UTF-8 without BOM.")
        }
        $text = if ($bytes.Length -eq 0) { '' } else {
            try { $script:M0PilotUtf8.GetString($bytes) }
            catch { throw [IO.InvalidDataException]::new("M0I020 trusted $Name is not strict UTF-8.", $_.Exception) }
        }
        if ($bytes.Length -gt 0 -and
            ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
                $text.EndsWith("`n`n", [StringComparison]::Ordinal) -or $text -match '(?m)[\t ]+$')) {
            throw [IO.InvalidDataException]::new(
                "M0I020 trusted $Name must use LF, exactly one final newline and no trailing whitespace."
            )
        }
        $result = [pscustomobject][ordered]@{
            path = $path
            bytes = $bytes
            text = $text
            sha256 = Get-M0PilotSha256Bytes -Bytes $bytes
            stream = if ($KeepOpen) { $stream } else { $null }
        }
        if ($KeepOpen) { $stream = $null }
        return $result
    }
    finally {
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

function Assert-M0PilotHeldFileStable {
    param(
        [Parameter(Mandatory)] $File,
        [Parameter(Mandatory)] [string] $Name
    )
    if ($null -eq $File.stream) {
        throw [IO.InvalidDataException]::new("M0I020 trusted $Name no longer has its retained handle.")
    }
    [M0ProcessNativeV1.TrustedFile]::Validate($File.stream.SafeFileHandle, [string]$File.path)
    if ($File.stream.Length -ne $File.bytes.Length) {
        throw [IO.InvalidDataException]::new("M0I020 trusted $Name length changed while retained.")
    }
    $File.stream.Position = 0
    $current = [byte[]]::new($File.bytes.Length)
    if ($current.Length -gt 0) { $File.stream.ReadExactly($current, 0, $current.Length) }
    if (-not [Linq.Enumerable]::SequenceEqual[byte]($current, [byte[]]$File.bytes) -or
        (Get-M0PilotSha256Bytes -Bytes $current) -cne [string]$File.sha256) {
        throw [IO.InvalidDataException]::new("M0I020 trusted $Name bytes changed while retained.")
    }
}

function Get-M0PilotSha256Bytes {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [byte[]] $Bytes)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function ConvertTo-M0PilotCanonicalText {
    param([Parameter(Mandatory)] $Value)
    return (($Value | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n")
}

function Copy-M0PilotValue {
    param([Parameter(Mandatory)] $Value)
    return ($Value | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String)
}

function Assert-M0PilotSchema {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Name
    )
    $json = $Value | ConvertTo-Json -Compress -Depth 100
    if (-not (Test-Json -Json $json -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0I010 $Name failed its closed JSON schema.")
    }
}

function Assert-M0PilotLowerSha256 {
    param([AllowNull()] $Value, [Parameter(Mandatory)] [string] $Name)
    if ($Value -isnot [string] -or [string]$Value -cnotmatch '^[a-f0-9]{64}$') {
        throw [IO.InvalidDataException]::new("M0I010 $Name must be a lowercase SHA-256 value.")
    }
}

function Assert-M0PilotExactProjectionObject {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] $Expected,
        [Parameter(Mandatory)] [string[]] $PropertyNames,
        [Parameter(Mandatory)] [string] $Name
    )
    foreach ($propertyName in $PropertyNames) {
        $actualValue = $Actual.$propertyName
        $expectedValue = $Expected.$propertyName
        if ($null -eq $actualValue -and $null -eq $expectedValue) { continue }
        if ($null -eq $actualValue -or $null -eq $expectedValue -or
            [string]$actualValue -cne [string]$expectedValue) {
            throw [IO.InvalidDataException]::new("M0I010 $Name property '$propertyName' differs from its exact projection.")
        }
    }
}

function ConvertTo-M0PilotInt64 {
    param(
        [AllowNull()] $Value,
        [Parameter(Mandatory)] [string] $Name,
        [long] $Minimum = 0
    )
    $isInteger = $false
    foreach ($type in @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64], [uint64])) {
        if ($Value -is $type) { $isInteger = $true; break }
    }
    [long]$parsed = 0
    if (-not $isInteger -or -not [long]::TryParse([string]$Value, [ref]$parsed) -or $parsed -lt $Minimum) {
        throw [IO.InvalidDataException]::new("M0I010 $Name must be a canonical Int64 integer at least $Minimum.")
    }
    return $parsed
}

function ConvertTo-M0PilotCanonicalUtc {
    param([AllowNull()] $Value, [Parameter(Mandatory)] [string] $Name)
    if ($Value -isnot [string] -or [string]$Value -cnotmatch '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{7}Z$') {
        throw [IO.InvalidDataException]::new("M0I010 $Name must use canonical UTC with seven fractional digits and Z.")
    }
    [DateTimeOffset]$parsed = [DateTimeOffset]::MinValue
    $styles = [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
    if (-not [DateTimeOffset]::TryParseExact(
            [string]$Value, 'yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture,
            $styles, [ref]$parsed
        ) -or $parsed.ToString('yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture) -cne [string]$Value) {
        throw [IO.InvalidDataException]::new("M0I010 $Name is not a real canonical UTC instant.")
    }
    return $parsed
}

function Assert-M0PilotCommonInputs {
    param([Parameter(Mandatory)] $CommonInputs)
    $expectedNames = @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')
    if (@($CommonInputs.PSObject.Properties).Count -ne $expectedNames.Count) {
        throw [IO.InvalidDataException]::new('M0I010 common_inputs has missing or extra properties.')
    }
    foreach ($name in $expectedNames) {
        if ($CommonInputs.PSObject.Properties.Name -cnotcontains $name) {
            throw [IO.InvalidDataException]::new("M0I010 common_inputs is missing '$name'.")
        }
        Assert-M0PilotLowerSha256 -Value $CommonInputs.$name -Name "common_inputs.$name"
    }
}

function Get-M0PilotExpectedPaths {
    param([Parameter(Mandatory)] [string] $RunId)
    return [pscustomobject][ordered]@{
        raw = "$RunId.jsonl"
        pilot_intent = "$RunId.pilot-intent.json"
        evidence_intent = "$RunId.intent.json"
        run = "$RunId.run.json"
        snapshot = "$RunId.snapshot.json"
        process_tree = "artifacts/$RunId.process-tree.json"
        pilot_summary = "$RunId.pilot-summary.json"
        pilot_summary_sidecar = "$RunId.pilot-summary.sha256"
    }
}

function New-M0SyntheticPilotRunStartedAnchor {
    param([Parameter(Mandatory)] $Event)

    Assert-M0PilotSchema -Value $Event -SchemaPath (Join-Path $script:M0PilotProtocolRoot 'schemas\event.schema.json') `
        -Name 'pilot run_started event'
    $copy = Copy-M0PilotValue $Event
    if ($copy.schema_id -cne 'm0.event.v1' -or [int]$copy.schema_version -ne 1 -or
        $copy.candidate_code -cne 'P' -or $copy.scenario_id -cne 'synthetic-process-tree' -or
        [int]$copy.attempt -ne 1 -or [int]$copy.event_seq -ne 1 -or $null -ne $copy.previous_line_sha256 -or
        $copy.event -cne 'run_started' -or $copy.payload.phase -cne 'started' -or
        @($copy.payload.PSObject.Properties).Count -ne 1) {
        throw [IO.InvalidDataException]::new('M0I010 pilot intent run_started event differs from the exact first-event contract.')
    }
    foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
        Assert-M0PilotLowerSha256 -Value $copy.$name -Name "run_started.$name"
    }
    $null = ConvertTo-M0PilotCanonicalUtc -Value $copy.utc -Name 'run_started.utc'
    $null = ConvertTo-M0PilotInt64 -Value $copy.qpc_ticks -Name 'run_started.qpc_ticks'
    $null = ConvertTo-M0PilotInt64 -Value $copy.qpc_frequency -Name 'run_started.qpc_frequency' -Minimum 1
    $canonicalEvent = [pscustomobject][ordered]@{
        schema_id = 'm0.event.v1'
        schema_version = 1
        protocol_sha256 = ([string]$copy.protocol_sha256).ToLowerInvariant()
        environment_sha256 = ([string]$copy.environment_sha256).ToLowerInvariant()
        fixture_sha256 = ([string]$copy.fixture_sha256).ToLowerInvariant()
        harness_sha256 = ([string]$copy.harness_sha256).ToLowerInvariant()
        scenario_sha256 = ([string]$copy.scenario_sha256).ToLowerInvariant()
        experiment_id = [string]$copy.experiment_id
        run_id = [string]$copy.run_id
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        event_seq = 1
        previous_line_sha256 = $null
        utc = [string]$copy.utc
        qpc_ticks = [long]$copy.qpc_ticks
        qpc_frequency = [long]$copy.qpc_frequency
        event = 'run_started'
        payload = [pscustomobject][ordered]@{ phase = 'started' }
    }
    $line = $canonicalEvent | ConvertTo-Json -Compress -Depth 100
    if ($line.Contains("`r") -or $line.Contains("`n")) {
        throw [IO.InvalidDataException]::new('M0I010 run_started canonical event must serialize to one JSON line.')
    }
    $lineBytes = $script:M0PilotUtf8.GetBytes($line + "`n")
    return [pscustomobject][ordered]@{
        event = $canonicalEvent
        canonical_line_sha256 = Get-M0PilotSha256Bytes -Bytes $script:M0PilotUtf8.GetBytes($line)
        size_bytes = [long]$lineBytes.Length
    }
}

function New-M0SyntheticPilotIntent {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Intent)

    Assert-M0PilotSchema -Value $Intent -SchemaPath $script:M0PilotIntentSchemaPath -Name 'pilot intent'
    $copy = Copy-M0PilotValue $Intent
    Assert-M0PilotCommonInputs -CommonInputs $copy.common_inputs
    $expectedPaths = Get-M0PilotExpectedPaths -RunId ([string]$copy.run_id)
    foreach ($name in @($expectedPaths.PSObject.Properties.Name)) {
        if ([string]$copy.expected_paths.$name -cne [string]$expectedPaths.$name) {
            throw [IO.InvalidDataException]::new("M0I010 pilot intent path '$name' is not derived from run_id.")
        }
    }
    if (@($copy.expected_paths.PSObject.Properties).Count -ne 8 -or
        @($copy.expected_paths.PSObject.Properties.Value | Select-Object -Unique).Count -ne 8) {
        throw [IO.InvalidDataException]::new('M0I010 pilot intent expected paths must be eight distinct closed slots.')
    }
    $runStarted = New-M0SyntheticPilotRunStartedAnchor -Event $copy.run_started.event
    if ([string]$copy.run_started.canonical_line_sha256 -cne [string]$runStarted.canonical_line_sha256 -or
        [long]$copy.run_started.size_bytes -ne [long]$runStarted.size_bytes -or
        [string]$runStarted.event.experiment_id -cne [string]$copy.experiment_id -or
        [string]$runStarted.event.run_id -cne [string]$copy.run_id) {
        throw [IO.InvalidDataException]::new('M0I010 pilot intent run_started receipt differs from its canonical event or run identity.')
    }
    foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
        if ([string]$runStarted.event.$name -cne [string]$copy.common_inputs.$name) {
            throw [IO.InvalidDataException]::new("M0I010 pilot intent run_started '$name' differs from common_inputs.")
        }
    }

    $document = [pscustomobject][ordered]@{
        '$schema' = 'schemas/pilot-intent.schema.json'
        schema_id = 'm0.synthetic-pilot-intent.v1'
        schema_version = 1
        state = 'launch-authorized-once'
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        experiment_id = [string]$copy.experiment_id
        run_id = [string]$copy.run_id
        common_inputs = [pscustomobject][ordered]@{
            protocol_sha256 = ([string]$copy.common_inputs.protocol_sha256).ToLowerInvariant()
            environment_sha256 = ([string]$copy.common_inputs.environment_sha256).ToLowerInvariant()
            fixture_sha256 = ([string]$copy.common_inputs.fixture_sha256).ToLowerInvariant()
            harness_sha256 = ([string]$copy.common_inputs.harness_sha256).ToLowerInvariant()
            scenario_sha256 = ([string]$copy.common_inputs.scenario_sha256).ToLowerInvariant()
        }
        evidence_writer_state = 'open-before-live'
        live_launch_limit = 1
        run_started = $runStarted
        expected_paths = $expectedPaths
    }
    Assert-M0PilotSchema -Value $document -SchemaPath $script:M0PilotIntentSchemaPath -Name 'canonical pilot intent'
    $canonicalJson = ConvertTo-M0PilotCanonicalText -Value $document
    $bytes = $script:M0PilotUtf8.GetBytes($canonicalJson)
    return [pscustomobject][ordered]@{
        canonical_json = $canonicalJson
        sha256 = Get-M0PilotSha256Bytes -Bytes $bytes
        size_bytes = [long]$bytes.Length
        document = Copy-M0PilotValue $document
    }
}

function Assert-M0SyntheticPilotFixtureBindings {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $PilotIntent,
        [Parameter(Mandatory)] $SummaryInput
    )

    $intentBuilt = New-M0SyntheticPilotIntent -Intent $PilotIntent
    $summaryBuilt = New-M0SyntheticPilotSummary -SummaryInput $SummaryInput
    $intent = $intentBuilt.document
    $receipt = $summaryBuilt.document.receipt
    $runStarted = $intent.run_started

    if ([string]$receipt.experiment_id -cne [string]$intent.experiment_id -or
        [string]$receipt.run_id -cne [string]$intent.run_id -or
        [string]$receipt.pilot_intent.relative_path -cne [string]$intent.expected_paths.pilot_intent -or
        [string]$receipt.pilot_intent.sha256 -cne [string]$intentBuilt.sha256 -or
        [long]$receipt.pilot_intent.size_bytes -ne [long]$intentBuilt.size_bytes -or
        [string]$receipt.raws[0].relative_path -cne [string]$intent.expected_paths.raw -or
        [string]$receipt.raws[0].first_line_sha256 -cne [string]$runStarted.canonical_line_sha256 -or
        [long]$receipt.raws[0].first_line_size_bytes -ne [long]$runStarted.size_bytes -or
        [string]$receipt.intent.relative_path -cne [string]$intent.expected_paths.evidence_intent -or
        [string]$receipt.run.relative_path -cne [string]$intent.expected_paths.run -or
        [string]$receipt.snapshot.relative_path -cne [string]$intent.expected_paths.snapshot -or
        [string]$receipt.artifacts[0].relative_path -cne [string]$intent.expected_paths.process_tree -or
        [string]$receipt.started_utc -cne [string]$runStarted.event.utc -or
        [long]$receipt.started_qpc_ticks -ne [long]$runStarted.event.qpc_ticks -or
        [long]$receipt.qpc_frequency -ne [long]$runStarted.event.qpc_frequency) {
        throw [IO.InvalidDataException]::new('M0I011 Pilot fixture intent/summary receipt binding differs.')
    }
    foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
        if ([string]$receipt.common_inputs.$name -cne [string]$intent.common_inputs.$name) {
            throw [IO.InvalidDataException]::new("M0I011 Pilot fixture common input '$name' differs between intent and summary receipt.")
        }
    }
    return [pscustomobject][ordered]@{
        run_id = [string]$intent.run_id
        intent_sha256 = [string]$intentBuilt.sha256
        summary_sha256 = [string]$summaryBuilt.sha256
    }
}

function Get-M0PilotCanonicalMissingCapabilities {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $MissingCapabilities)
    $order = [ordered]@{
        'etw-process-start-stop' = 1
        'job-completion-port' = 2
        'root-handle' = 3
        'external-attribution-proof' = 4
        'safe-process-identity' = 5
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $previous = 0
    $result = [Collections.Generic.List[string]]::new()
    foreach ($item in $MissingCapabilities) {
        if ($item -isnot [string] -or -not $order.Contains([string]$item) -or
            -not $seen.Add([string]$item) -or [int]$order[[string]$item] -le $previous) {
            throw [IO.InvalidDataException]::new('M0I010 process missing_capabilities must be unique and in canonical causal order.')
        }
        $previous = [int]$order[[string]$item]
        $result.Add([string]$item)
    }
    return @($result)
}

function Get-M0PilotCanonicalCleanupFailureCodes {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $FailureCodes)
    $order = [ordered]@{
        snapshot_validation_failed = 0
        identity_revalidation_failed = 1
        retained_handle_wait_failed = 2
        helper_survived_shutdown = 3
        job_close_failed = 4
        unsafe_run_root_contents = 5
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $previous = -1
    $result = [Collections.Generic.List[string]]::new()
    foreach ($item in $FailureCodes) {
        if ($item -isnot [string] -or -not $order.Contains([string]$item) -or
            -not $seen.Add([string]$item) -or [int]$order[[string]$item] -le $previous) {
            throw [IO.InvalidDataException]::new(
                'M0I010 cleanup failure_codes must be unique and use canonical Process causal order.'
            )
        }
        $previous = [int]$order[[string]$item]
        $result.Add([string]$item)
    }
    return @($result)
}

function Resolve-M0PilotOutcomeProjection {
    param(
        [Parameter(Mandatory)] $Process,
        [Parameter(Mandatory)] $Cleanup
    )
    $failureCodes = @(Get-M0PilotCanonicalCleanupFailureCodes -FailureCodes @($Cleanup.failure_codes))
    $failureSet = [Collections.Generic.HashSet[string]]::new([string[]]$failureCodes, [StringComparer]::Ordinal)
    $hasSurvivorFailure = $failureSet.Contains('retained_handle_wait_failed') -or
        $failureSet.Contains('helper_survived_shutdown')
    if ($hasSurvivorFailure) {
        return [pscustomobject][ordered]@{
            check = [pscustomobject][ordered]@{
                check_id = 'CHK-PROCESS'; status = 'failed'; reason_code = 'helper_survived_shutdown'
                status_reason = 'helper_survived_shutdown'; error_code = 'helper_survived_shutdown'
            }
            run = [pscustomobject][ordered]@{
                status = 'failed'; status_reason = 'helper_survived_shutdown'; error_code = 'helper_survived_shutdown'
            }
        }
    }
    if (-not [bool]$Cleanup.completed) {
        return [pscustomobject][ordered]@{
            check = [pscustomobject][ordered]@{
                check_id = 'CHK-PROCESS'; status = 'skipped'; reason_code = [string]$Cleanup.failure_code
                status_reason = 'incomplete_evidence'; error_code = $null
            }
            run = [pscustomobject][ordered]@{
                status = 'inconclusive'; status_reason = 'incomplete_evidence'; error_code = $null
            }
        }
    }
    if ([string]$Process.state -ceq 'complete') {
        return [pscustomobject][ordered]@{
            check = [pscustomobject][ordered]@{
                check_id = 'CHK-PROCESS'; status = 'passed'; reason_code = $null; status_reason = $null; error_code = $null
            }
            run = [pscustomobject][ordered]@{ status = 'valid'; status_reason = $null; error_code = $null }
        }
    }
    return [pscustomobject][ordered]@{
        check = [pscustomobject][ordered]@{
            check_id = 'CHK-PROCESS'; status = 'skipped'; reason_code = "process_tree_$([string]$Process.state)"
            status_reason = 'incomplete_evidence'; error_code = $null
        }
        run = [pscustomobject][ordered]@{
            status = 'inconclusive'; status_reason = 'incomplete_evidence'; error_code = $null
        }
    }
}

function Assert-M0PilotProcessSemantics {
    param(
        [Parameter(Mandatory)] $Process,
        [Parameter(Mandatory)] $Cleanup,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Identities
    )
    $missing = @(Get-M0PilotCanonicalMissingCapabilities -MissingCapabilities @($Process.missing_capabilities))
    $missingSet = [Collections.Generic.HashSet[string]]::new([string[]]$missing, [StringComparer]::Ordinal)
    $state = [string]$Process.state
    $errorCode = if ($null -eq $Process.error_code) { $null } else { [string]$Process.error_code }
    $origin = [string]$Process.snapshot_origin

    if ($state -ceq 'complete') {
        if ($origin -cne 'observed' -or $null -ne $errorCode -or $missing.Count -ne 0) {
            throw [IO.InvalidDataException]::new('M0I010 complete process semantics contradict origin, error or missing capabilities.')
        }
    }
    elseif ($state -ceq 'partial') {
        if ($origin -cne 'observed') {
            throw [IO.InvalidDataException]::new('M0I010 partial process semantics require an observed snapshot origin.')
        }
        switch ($errorCode) {
            'etw_live_not_implemented' {
                if (-not $missingSet.Contains('etw-process-start-stop') -or $missingSet.Contains('root-handle') -or
                    $missingSet.Contains('external-attribution-proof') -or $missingSet.Contains('safe-process-identity')) {
                    throw [IO.InvalidDataException]::new('M0I010 ETW-live partial process missing capabilities contradict its cause.')
                }
            }
            'etw_trace_incomplete' {
                if (-not $missingSet.Contains('etw-process-start-stop') -or $missingSet.Contains('root-handle') -or
                    $missingSet.Contains('external-attribution-proof') -or $missingSet.Contains('safe-process-identity')) {
                    throw [IO.InvalidDataException]::new('M0I010 ETW-trace partial process missing capabilities contradict its cause.')
                }
            }
            'root_identity_missing' {
                if (-not $missingSet.Contains('root-handle') -or $missingSet.Contains('safe-process-identity')) {
                    throw [IO.InvalidDataException]::new('M0I010 root-missing process does not carry its canonical cause.')
                }
            }
            'unresolved_external_attribution' {
                if (-not $missingSet.Contains('external-attribution-proof') -or $missingSet.Contains('root-handle') -or
                    $missingSet.Contains('safe-process-identity')) {
                    throw [IO.InvalidDataException]::new('M0I010 unresolved attribution process does not carry its canonical cause.')
                }
            }
            default { throw [IO.InvalidDataException]::new('M0I010 partial process uses an unsupported causal error.') }
        }
    }
    elseif ($state -ceq 'unavailable') {
        $expectedVariantJson = if ($errorCode -ceq 'safe_identity_unavailable') {
            @(
                '["etw-process-start-stop","safe-process-identity"]',
                '["etw-process-start-stop","job-completion-port","safe-process-identity"]'
            )
        }
        elseif ($errorCode -ceq 'etw_trace_unavailable') {
            @('["etw-process-start-stop"]', '["etw-process-start-stop","job-completion-port"]')
        }
        else { throw [IO.InvalidDataException]::new('M0I010 unavailable process uses an unsupported causal error.') }
        $actualMissingJson = ConvertTo-Json -InputObject @($missing) -Compress
        if ($expectedVariantJson -cnotcontains $actualMissingJson) {
            throw [IO.InvalidDataException]::new('M0I010 unavailable process missing capabilities differ from its exact cause.')
        }
    }
    else { throw [IO.InvalidDataException]::new('M0I010 unsupported process state.') }

    if ($Cleanup.attempted -isnot [bool] -or $Cleanup.completed -isnot [bool]) {
        throw [IO.InvalidDataException]::new('M0I010 cleanup attempted/completed values must be booleans.')
    }
    $survivors = ConvertTo-M0PilotInt64 -Value $Cleanup.survivor_count -Name 'cleanup.survivor_count'
    $failureCode = if ($null -eq $Cleanup.failure_code) { $null } else { [string]$Cleanup.failure_code }
    $failureCodes = @(Get-M0PilotCanonicalCleanupFailureCodes -FailureCodes @($Cleanup.failure_codes))
    $failureSet = [Collections.Generic.HashSet[string]]::new([string[]]$failureCodes, [StringComparer]::Ordinal)
    if ($origin -ceq 'observed') {
        $hasPrivateAuthority = -not ($errorCode -ceq 'root_identity_missing' -or $state -ceq 'unavailable')
        if ([bool]$Cleanup.attempted -ne $hasPrivateAuthority) {
            throw [IO.InvalidDataException]::new('M0I010 observed process cleanup attempt contradicts its mutation authority.')
        }
    }
    if (-not [bool]$Cleanup.attempted) {
        if (-not [bool]$Cleanup.completed -or $survivors -ne 0 -or $null -ne $failureCode -or
            $failureCodes.Count -ne 0 -or
            -not (($state -ceq 'unavailable') -or ($state -ceq 'partial' -and $errorCode -ceq 'root_identity_missing'))) {
            throw [IO.InvalidDataException]::new('M0I010 no-authority cleanup contradicts process or completion semantics.')
        }
    }
    elseif ([bool]$Cleanup.completed) {
        if ($survivors -ne 0 -or $null -ne $failureCode -or $failureCodes.Count -ne 0) {
            throw [IO.InvalidDataException]::new('M0I010 completed cleanup cannot carry survivors or failure.')
        }
    }
    else {
        if ($null -eq $failureCode -or $failureCodes.Count -eq 0 -or $failureCode -cne $failureCodes[0]) {
            throw [IO.InvalidDataException]::new(
                'M0I010 incomplete cleanup requires its canonical primary failure first in failure_codes.'
            )
        }
        $hasSurvivorFailure = $failureSet.Contains('retained_handle_wait_failed') -or
            $failureSet.Contains('helper_survived_shutdown')
        if (($survivors -gt 0) -ne $hasSurvivorFailure) {
            throw [IO.InvalidDataException]::new('M0I010 cleanup survivor count contradicts its ordered failure causes.')
        }
    }

    $identityCount = @($Identities).Count
    if ($state -ceq 'complete' -or $errorCode -cin @('etw_live_not_implemented', 'etw_trace_incomplete')) {
        if ($identityCount -ne 3) {
            throw [IO.InvalidDataException]::new('M0I010 observed live topology requires root, child and grandchild identities.')
        }
    }
    elseif ($state -ceq 'unavailable' -or $errorCode -ceq 'root_identity_missing') {
        if ($identityCount -ne 0) {
            throw [IO.InvalidDataException]::new('M0I010 unavailable/root-missing topology must not publish process identities.')
        }
    }
    elseif ($identityCount -lt 1 -or $identityCount -gt 3) {
        throw [IO.InvalidDataException]::new('M0I010 unresolved topology must publish a safe nonempty role prefix.')
    }
}

function Assert-M0PilotReceiptSemantics {
    param(
        [Parameter(Mandatory)] $Receipt,
        [Parameter(Mandatory)] $Process,
        [Parameter(Mandatory)] $Cleanup
    )
    Assert-M0PilotCommonInputs -CommonInputs $Receipt.common_inputs
    $runId = [string]$Receipt.run_id
    $paths = Get-M0PilotExpectedPaths -RunId $runId
    $actualPaths = [ordered]@{
        raw = [string]$Receipt.raws[0].relative_path
        pilot_intent = [string]$Receipt.pilot_intent.relative_path
        evidence_intent = [string]$Receipt.intent.relative_path
        run = [string]$Receipt.run.relative_path
        snapshot = [string]$Receipt.snapshot.relative_path
        process_tree = [string]$Receipt.artifacts[0].relative_path
    }
    foreach ($name in $actualPaths.Keys) {
        if ([string]$actualPaths[$name] -cne [string]$paths.$name) {
            throw [IO.InvalidDataException]::new("M0I010 receipt path '$name' is not derived from run_id.")
        }
    }
    if (@($actualPaths.Values | Select-Object -Unique).Count -ne $actualPaths.Count) {
        throw [IO.InvalidDataException]::new('M0I010 receipt file slots must have distinct derived paths.')
    }
    foreach ($file in @(
        $Receipt.pilot_intent, $Receipt.raws[0], $Receipt.intent, $Receipt.run, $Receipt.snapshot, $Receipt.artifacts[0]
    )) {
        Assert-M0PilotLowerSha256 -Value $file.sha256 -Name 'receipt file sha256'
        $null = ConvertTo-M0PilotInt64 -Value $file.size_bytes -Name 'receipt file size' -Minimum 1
    }
    if ([int]$Receipt.raws[0].event_count -ne 4) {
        throw [IO.InvalidDataException]::new('M0I010 synthetic pilot raw receipt must contain exactly four events.')
    }
    Assert-M0PilotLowerSha256 -Value $Receipt.raws[0].final_line_sha256 -Name 'raw final_line_sha256'
    Assert-M0PilotLowerSha256 -Value $Receipt.raws[0].first_line_sha256 -Name 'raw first_line_sha256'
    $null = ConvertTo-M0PilotInt64 -Value $Receipt.raws[0].first_line_size_bytes `
        -Name 'raw first_line_size_bytes' -Minimum 1
    if ([long]$Receipt.raws[0].size_bytes -le [long]$Receipt.raws[0].first_line_size_bytes) {
        throw [IO.InvalidDataException]::new('M0I010 sealed four-event raw size must exceed its first canonical line size.')
    }
    Assert-M0PilotLowerSha256 -Value $Receipt.transaction_id -Name 'transaction_id'

    $started = ConvertTo-M0PilotCanonicalUtc -Value $Receipt.started_utc -Name 'receipt.started_utc'
    $sealed = ConvertTo-M0PilotCanonicalUtc -Value $Receipt.sealed_utc -Name 'receipt.sealed_utc'
    if ($sealed -lt $started) {
        throw [IO.InvalidDataException]::new('M0I010 receipt UTC clock regressed.')
    }
    $startedQpc = ConvertTo-M0PilotInt64 -Value $Receipt.started_qpc_ticks -Name 'receipt.started_qpc_ticks'
    $sealedQpc = ConvertTo-M0PilotInt64 -Value $Receipt.sealed_qpc_ticks -Name 'receipt.sealed_qpc_ticks'
    $null = ConvertTo-M0PilotInt64 -Value $Receipt.qpc_frequency -Name 'receipt.qpc_frequency' -Minimum 1
    if ($sealedQpc -lt $startedQpc) {
        throw [IO.InvalidDataException]::new('M0I010 receipt QPC clock regressed.')
    }

    $roleOrder = @('root', 'child', 'grandchild')
    $seenPid = [Collections.Generic.HashSet[uint32]]::new()
    $seenIdentity = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt @($Receipt.process_identities).Count; $index++) {
        $identity = $Receipt.process_identities[$index]
        if ($index -ge $roleOrder.Count -or [string]$identity.role -cne $roleOrder[$index]) {
            throw [IO.InvalidDataException]::new('M0I010 process identities must be a canonical root/child/grandchild prefix.')
        }
        [long]$pid = ConvertTo-M0PilotInt64 -Value $identity.pid -Name 'identity.pid' -Minimum 1
        if ($pid -gt [uint32]::MaxValue -or -not $seenPid.Add([uint32]$pid)) {
            throw [IO.InvalidDataException]::new('M0I010 process identity PID must be unique and within UInt32.')
        }
        $birth = [string]$identity.creation_time_filetime_utc
        [uint64]$parsedBirth = 0
        if ($birth -cnotmatch '^[1-9][0-9]{0,19}$' -or
            -not [uint64]::TryParse($birth, [Globalization.NumberStyles]::None, [Globalization.CultureInfo]::InvariantCulture, [ref]$parsedBirth) -or
            -not $seenIdentity.Add("$pid|$birth")) {
            throw [IO.InvalidDataException]::new('M0I010 process identity must be a unique PID/FILETIME pair with canonical UInt64 birth.')
        }
    }
    Assert-M0PilotProcessSemantics -Process $Process -Cleanup $Cleanup -Identities @($Receipt.process_identities)
}

function Assert-M0SyntheticPilotInputSemantics {
    param([Parameter(Mandatory)] $SummaryInput)
    Assert-M0PilotReceiptSemantics -Receipt $SummaryInput.receipt -Process $SummaryInput.process -Cleanup $SummaryInput.cleanup
    $projection = Resolve-M0PilotOutcomeProjection -Process $SummaryInput.process -Cleanup $SummaryInput.cleanup
    Assert-M0PilotExactProjectionObject -Actual $SummaryInput.check -Expected $projection.check `
        -PropertyNames @('check_id', 'status', 'reason_code', 'status_reason', 'error_code') -Name 'pilot check outcome'
    Assert-M0PilotExactProjectionObject -Actual $SummaryInput.run -Expected $projection.run `
        -PropertyNames @('status', 'status_reason', 'error_code') -Name 'pilot run outcome'
}

function ConvertTo-M0PilotCanonicalFileReceipt {
    param([Parameter(Mandatory)] $File)
    return [pscustomobject][ordered]@{
        relative_path = [string]$File.relative_path
        sha256 = ([string]$File.sha256).ToLowerInvariant()
        size_bytes = [long]$File.size_bytes
    }
}

function ConvertTo-M0PilotCanonicalReceipt {
    param([Parameter(Mandatory)] $Receipt)
    $raw = $Receipt.raws[0]
    return [pscustomobject][ordered]@{
        experiment_id = [string]$Receipt.experiment_id
        run_id = [string]$Receipt.run_id
        transaction_id = ([string]$Receipt.transaction_id).ToLowerInvariant()
        common_inputs = [pscustomobject][ordered]@{
            protocol_sha256 = ([string]$Receipt.common_inputs.protocol_sha256).ToLowerInvariant()
            environment_sha256 = ([string]$Receipt.common_inputs.environment_sha256).ToLowerInvariant()
            fixture_sha256 = ([string]$Receipt.common_inputs.fixture_sha256).ToLowerInvariant()
            harness_sha256 = ([string]$Receipt.common_inputs.harness_sha256).ToLowerInvariant()
            scenario_sha256 = ([string]$Receipt.common_inputs.scenario_sha256).ToLowerInvariant()
        }
        pilot_intent = ConvertTo-M0PilotCanonicalFileReceipt -File $Receipt.pilot_intent
        raws = @([pscustomobject][ordered]@{
                relative_path = [string]$raw.relative_path
                sha256 = ([string]$raw.sha256).ToLowerInvariant()
                size_bytes = [long]$raw.size_bytes
                event_count = 4
                first_line_sha256 = ([string]$raw.first_line_sha256).ToLowerInvariant()
                first_line_size_bytes = [long]$raw.first_line_size_bytes
                final_line_sha256 = ([string]$raw.final_line_sha256).ToLowerInvariant()
            })
        traces = @()
        intent = ConvertTo-M0PilotCanonicalFileReceipt -File $Receipt.intent
        run = ConvertTo-M0PilotCanonicalFileReceipt -File $Receipt.run
        snapshot = ConvertTo-M0PilotCanonicalFileReceipt -File $Receipt.snapshot
        artifacts = @([pscustomobject][ordered]@{
                artifact_id = 'process-tree'
                relative_path = [string]$Receipt.artifacts[0].relative_path
                sha256 = ([string]$Receipt.artifacts[0].sha256).ToLowerInvariant()
                size_bytes = [long]$Receipt.artifacts[0].size_bytes
            })
        started_utc = [string]$Receipt.started_utc
        sealed_utc = [string]$Receipt.sealed_utc
        started_qpc_ticks = [long]$Receipt.started_qpc_ticks
        sealed_qpc_ticks = [long]$Receipt.sealed_qpc_ticks
        qpc_frequency = [long]$Receipt.qpc_frequency
        process_identities = @($Receipt.process_identities | ForEach-Object {
                [pscustomobject][ordered]@{
                    role = [string]$_.role
                    pid = [long]$_.pid
                    creation_time_filetime_utc = [string]$_.creation_time_filetime_utc
                }
            })
    }
}

function New-M0SyntheticPilotSummary {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [Alias('Input')] $SummaryInput)

    Assert-M0PilotSchema -Value $SummaryInput -SchemaPath $script:M0PilotInputSchemaPath -Name 'pilot summary input'
    $copy = Copy-M0PilotValue $SummaryInput
    Assert-M0SyntheticPilotInputSemantics -SummaryInput $copy
    $semantic = [pscustomobject][ordered]@{
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        publication_state = 'committed'
        run = [pscustomobject][ordered]@{
            status = [string]$copy.run.status
            status_reason = if ($null -eq $copy.run.status_reason) { $null } else { [string]$copy.run.status_reason }
            error_code = if ($null -eq $copy.run.error_code) { $null } else { [string]$copy.run.error_code }
        }
        process = [pscustomobject][ordered]@{
            snapshot_origin = [string]$copy.process.snapshot_origin
            state = [string]$copy.process.state
            error_code = if ($null -eq $copy.process.error_code) { $null } else { [string]$copy.process.error_code }
            missing_capabilities = @(Get-M0PilotCanonicalMissingCapabilities -MissingCapabilities @($copy.process.missing_capabilities))
        }
        cleanup = [pscustomobject][ordered]@{
            attempted = [bool]$copy.cleanup.attempted
            completed = [bool]$copy.cleanup.completed
            survivor_count = [long]$copy.cleanup.survivor_count
            failure_code = if ($null -eq $copy.cleanup.failure_code) { $null } else { [string]$copy.cleanup.failure_code }
            failure_codes = @(Get-M0PilotCanonicalCleanupFailureCodes -FailureCodes @($copy.cleanup.failure_codes))
        }
        check = [pscustomobject][ordered]@{
            check_id = 'CHK-PROCESS'
            status = [string]$copy.check.status
            reason_code = if ($null -eq $copy.check.reason_code) { $null } else { [string]$copy.check.reason_code }
            status_reason = if ($null -eq $copy.check.status_reason) { $null } else { [string]$copy.check.status_reason }
            error_code = if ($null -eq $copy.check.error_code) { $null } else { [string]$copy.check.error_code }
        }
        artifact_count = 1
    }
    $document = [pscustomobject][ordered]@{
        '$schema' = 'schemas/pilot-summary.schema.json'
        schema_id = 'm0.synthetic-pilot-summary.v1'
        schema_version = 1
        semantic = $semantic
        receipt = ConvertTo-M0PilotCanonicalReceipt -Receipt $copy.receipt
    }
    Assert-M0PilotSchema -Value $document -SchemaPath $script:M0PilotSummarySchemaPath -Name 'canonical pilot summary'
    $canonicalJson = ConvertTo-M0PilotCanonicalText -Value $document
    $semanticJson = ConvertTo-M0PilotCanonicalText -Value $semantic
    $bytes = $script:M0PilotUtf8.GetBytes($canonicalJson)
    $semanticBytes = $script:M0PilotUtf8.GetBytes($semanticJson)
    return [pscustomobject][ordered]@{
        canonical_json = $canonicalJson
        sha256 = Get-M0PilotSha256Bytes -Bytes $bytes
        size_bytes = [long]$bytes.Length
        semantic_json = $semanticJson
        semantic_sha256 = Get-M0PilotSha256Bytes -Bytes $semanticBytes
        document = Copy-M0PilotValue $document
    }
}

function Assert-M0SyntheticPilotSummary {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Summary)

    Assert-M0PilotSchema -Value $Summary -SchemaPath $script:M0PilotSummarySchemaPath -Name 'pilot summary'
    $copy = Copy-M0PilotValue $Summary
    $rebuildInput = [pscustomobject][ordered]@{
        '$schema' = 'schemas/pilot-summary-input.schema.json'
        schema_id = 'm0.synthetic-pilot-summary-input.v1'
        schema_version = 1
        candidate_code = $copy.semantic.candidate_code
        scenario_id = $copy.semantic.scenario_id
        attempt = $copy.semantic.attempt
        evidence_role = $copy.semantic.evidence_role
        decision_eligible = $copy.semantic.decision_eligible
        publication_state = $copy.semantic.publication_state
        run = $copy.semantic.run
        process = $copy.semantic.process
        cleanup = $copy.semantic.cleanup
        check = $copy.semantic.check
        artifact_count = $copy.semantic.artifact_count
        receipt = $copy.receipt
    }
    $rebuilt = New-M0SyntheticPilotSummary -SummaryInput $rebuildInput
    return $rebuilt
}

function Get-M0PilotManifestSchemaEntry {
    param(
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string, object]] $Entries,
        [Parameter(Mandatory)] [string] $RelativePath
    )
    if (-not $Entries.ContainsKey($RelativePath)) {
        throw [IO.InvalidDataException]::new("M0I021 harness manifest does not anchor '$RelativePath'.")
    }
    return $Entries[$RelativePath]
}

function Assert-M0PilotManifestFileBinding {
    param(
        [Parameter(Mandatory)] $File,
        [Parameter(Mandatory)] $Entry,
        [Parameter(Mandatory)] [string] $Name
    )
    if ([string]$File.sha256 -cne [string]$Entry.sha256 -or
        [long]$File.bytes.Length -ne [long]$Entry.size_bytes) {
        throw [IO.InvalidDataException]::new("M0I021 trusted $Name differs from its protocol-anchored harness manifest entry.")
    }
}

function Open-M0PilotManifestBoundFile {
    param(
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string, object]] $Entries,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $HeldFiles,
        [Parameter(Mandatory)] [string] $Name
    )

    $file = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
            $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)) -Name $Name -KeepOpen
    $HeldFiles.Add($file)
    Assert-M0PilotManifestFileBinding -File $file `
        -Entry (Get-M0PilotManifestSchemaEntry -Entries $Entries -RelativePath $RelativePath) -Name $Name
    return $file
}

function Close-M0PilotTrustSet {
    param([AllowNull()] $TrustSet)
    if ($null -eq $TrustSet -or $null -eq $TrustSet.held_files) { return }
    for ($index = $TrustSet.held_files.Count - 1; $index -ge 0; $index--) {
        if ($null -ne $TrustSet.held_files[$index].stream) {
            $TrustSet.held_files[$index].stream.Dispose()
        }
    }
}

function Get-M0PilotEvidenceModule {
    param([Parameter(Mandatory)] $TrustSet)

    $comparison = [StringComparison]::OrdinalIgnoreCase
    $expected = [IO.Path]::GetFullPath([string]$TrustSet.runtime_sources.evidence.path)
    $loaded = @(Get-Module -All -Name 'M0.Evidence')
    if ($loaded.Count -gt 1 -or
        ($loaded.Count -eq 1 -and -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison))) {
        throw [IO.InvalidDataException]::new('M0I022 physical Pilot refuses a foreign or ambiguous M0.Evidence module.')
    }
    if ($loaded.Count -eq 0) {
        foreach ($nativeTypeName in @('M0EvidenceNativeV1.WindowsFileIdentity', 'M0EvidenceNativeV1.WindowsFileFingerprint')) {
            if ($nativeTypeName -as [type]) {
                throw [IO.InvalidDataException]::new(
                    "M0I022 physical Pilot refuses preloaded native Evidence type '$nativeTypeName' without M0.Evidence."
                )
            }
        }
        Import-Module -Name $expected -ErrorAction Stop
        $loaded = @(Get-Module -All -Name 'M0.Evidence')
    }
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($expected, $comparison)) {
        throw [IO.InvalidDataException]::new('M0I022 physical Pilot could not bind exact M0.Evidence runtime identity.')
    }
    $contract = & $loaded[0] { Assert-M0EvidenceRuntimeContract }
    if ([string]$contract.contract_id -cne 'm0-evidence-runtime-v1' -or
        [string]$contract.module_sha256 -cne [string]$TrustSet.runtime_sources.evidence.sha256 -or
        [string]$contract.markers_module_sha256 -cne [string]$TrustSet.runtime_sources.markers.sha256 -or
        [string]$contract.statistics_module_sha256 -cne [string]$TrustSet.runtime_sources.statistics.sha256 -or
        [string]$contract.module_instance_id -notmatch '^[a-f0-9]{32}$' -or
        [string]$contract.markers_module_instance_id -notmatch '^[a-f0-9]{32}$' -or
        [string]$contract.statistics_module_instance_id -notmatch '^[a-f0-9]{32}$') {
        throw [IO.InvalidDataException]::new('M0I022 loaded Evidence dependency graph differs from retained manifest bytes.')
    }
    foreach ($dependency in @(
        [pscustomobject]@{ name = 'M0.Markers'; source = $TrustSet.runtime_sources.markers },
        [pscustomobject]@{ name = 'M0.Statistics'; source = $TrustSet.runtime_sources.statistics }
    )) {
        $instances = @(Get-Module -All -Name $dependency.name)
        if ($instances.Count -ne 1 -or
            -not [IO.Path]::GetFullPath($instances[0].Path).Equals([IO.Path]::GetFullPath([string]$dependency.source.path), $comparison)) {
            throw [IO.InvalidDataException]::new("M0I022 Evidence dependency '$($dependency.name)' is foreign or ambiguous.")
        }
    }
    return $loaded[0]
}

function Open-M0PilotContractTrustSet {
    param(
        [Parameter(Mandatory)] [string] $ProtocolRoot,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedProtocolSha256
    )

    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $root.Equals($script:M0PilotProtocolRoot, $comparison)) {
        throw [UnauthorizedAccessException]::new(
            'M0I021 physical Pilot ProtocolRoot differs from the trusted module repository.'
        )
    }
    $held = [Collections.Generic.List[object]]::new()
    $trustSet = $null
    try {
        $protocolFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root 'protocol.v1.json') `
            -Name 'Pilot protocol' -KeepOpen
        $held.Add($protocolFile)
        if ($protocolFile.sha256 -cne $ExpectedProtocolSha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new(
                'M0I021 Pilot protocol hash differs from the independently captured expected protocol.'
            )
        }
        try { $protocol = $protocolFile.text | ConvertFrom-Json -Depth 100 -DateKind String }
        catch { throw [IO.InvalidDataException]::new('M0I021 Pilot protocol JSON is invalid.', $_.Exception) }
        if ([string]$protocol.protocol_id -cne 'M0-P1' -or
            [string]$protocol.harness_manifest_sha256 -notmatch '^[A-F0-9]{64}$' -or
            [string]$protocol.synthetic_process_tree_scenario_sha256 -notmatch '^[A-F0-9]{64}$') {
            throw [IO.InvalidDataException]::new('M0I021 Pilot protocol lacks its closed manifest/scenario anchors.')
        }

        $manifestFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
                'experiments\m0\harness\manifest.v1.json') -Name 'Pilot harness manifest' -KeepOpen
        $held.Add($manifestFile)
        if ($manifestFile.sha256 -cne ([string]$protocol.harness_manifest_sha256).ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0I021 harness manifest differs from the captured Pilot protocol anchor.')
        }
        $manifestSidecarFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
                'experiments\m0\harness\manifest.v1.sha256') -Name 'Pilot harness manifest sidecar' -KeepOpen
        $held.Add($manifestSidecarFile)
        if ($manifestSidecarFile.text -cne "$($manifestFile.sha256)  manifest.v1.json`n") {
            throw [IO.InvalidDataException]::new('M0I021 harness manifest sidecar differs from exact manifest bytes.')
        }
        try { $manifest = $manifestFile.text | ConvertFrom-Json -Depth 100 -DateKind String }
        catch { throw [IO.InvalidDataException]::new('M0I021 harness manifest JSON is invalid.', $_.Exception) }
        if ((ConvertTo-M0PilotCanonicalText -Value $manifest) -cne $manifestFile.text) {
            throw [IO.InvalidDataException]::new('M0I021 harness manifest is not canonical JSON.')
        }
        $manifestEntries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
        foreach ($entry in @($manifest.runtime_files) + @($manifest.schema_files)) {
            if (-not $manifestEntries.TryAdd([string]$entry.relative_path, $entry)) {
                throw [IO.InvalidDataException]::new('M0I021 harness manifest contains duplicate trusted paths.')
            }
        }
        $pilotModuleFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/harness/M0.Pilot.psm1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'loaded Pilot module source'
        if ($pilotModuleFile.sha256 -cne $script:M0PilotModuleSha256) {
            throw [IO.InvalidDataException]::new('M0I021 loaded M0.Pilot instance differs from its manifest-bound source bytes.')
        }
        $processModuleFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/harness/M0.Process.psm1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'loaded Process module source'
        $processModule = Get-M0PilotPhysicalTrustModule
        $processNativeContract = & $processModule { Assert-M0ProcessNativeContract }
        if ([string]$processNativeContract.module_sha256 -cne [string]$processModuleFile.sha256) {
            throw [IO.InvalidDataException]::new('M0I021 loaded M0.Process instance differs from its manifest-bound source bytes.')
        }
        $manifestSchemaPath = 'experiments/m0/protocol/schemas/harness-manifest.schema.json'
        $manifestSchemaFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
                $manifestSchemaPath.Replace('/', [IO.Path]::DirectorySeparatorChar)) `
            -Name 'Pilot harness manifest schema' -KeepOpen
        $held.Add($manifestSchemaFile)
        Assert-M0PilotManifestFileBinding -File $manifestSchemaFile `
            -Entry (Get-M0PilotManifestSchemaEntry -Entries $manifestEntries -RelativePath $manifestSchemaPath) `
            -Name 'harness manifest schema'
        if (-not (Test-Json -Json $manifestFile.text -SchemaFile $manifestSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 harness manifest failed its anchored closed schema.')
        }

        $protocolSchemaRelative = 'experiments/m0/protocol/schemas/protocol.schema.json'
        $protocolSchemaFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
                $protocolSchemaRelative.Replace('/', [IO.Path]::DirectorySeparatorChar)) `
            -Name 'Pilot protocol schema' -KeepOpen
        $held.Add($protocolSchemaFile)
        Assert-M0PilotManifestFileBinding -File $protocolSchemaFile `
            -Entry (Get-M0PilotManifestSchemaEntry -Entries $manifestEntries -RelativePath $protocolSchemaRelative) `
            -Name 'protocol schema'
        if (-not (Test-Json -Json $protocolFile.text -SchemaFile $protocolSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 Pilot protocol failed its anchored closed schema.')
        }

        $processSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/process.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Process descriptor schema'
        $processDescriptorFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root 'process.v1.json') `
            -Name 'Process descriptor' -KeepOpen
        $held.Add($processDescriptorFile)
        if ($processDescriptorFile.sha256 -cne ([string]$protocol.process_descriptor_sha256).ToLowerInvariant() -or
            -not (Test-Json -Json $processDescriptorFile.text -SchemaFile $processSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 Process descriptor differs from the protocol anchor or closed schema.')
        }
        $processDescriptorSidecar = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root 'process.v1.sha256') `
            -Name 'Process descriptor sidecar' -KeepOpen
        $held.Add($processDescriptorSidecar)
        if ($processDescriptorSidecar.text -cne "$($processDescriptorFile.sha256)  process.v1.json`n") {
            throw [IO.InvalidDataException]::new('M0I021 Process descriptor sidecar differs from exact descriptor bytes.')
        }
        $processDescriptor = $processDescriptorFile.text | ConvertFrom-Json -Depth 100 -DateKind String
        if ([string]$processDescriptor.process_id -cne 'M0-PROCESS-1' -or
            [string]$processDescriptor.protocol_id -cne 'M0-P1' -or
            [string]$processDescriptor.scenario_id -cne 'synthetic-process-tree') {
            throw [IO.InvalidDataException]::new('M0I021 Process descriptor identity differs from the synthetic Pilot contract.')
        }

        $processGoldenSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/process-golden.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Process golden schema'
        $processGoldenPath = [string]$processDescriptor.golden_relative_path
        if ($processGoldenPath -cne 'goldens/process.v1.json') {
            throw [IO.InvalidDataException]::new('M0I021 Process golden path differs from its literal slot.')
        }
        $processGoldenFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root `
                $processGoldenPath.Replace('/', [IO.Path]::DirectorySeparatorChar)) -Name 'Process golden' -KeepOpen
        $held.Add($processGoldenFile)
        if ($processGoldenFile.sha256 -cne ([string]$processDescriptor.golden_sha256).ToLowerInvariant() -or
            -not (Test-Json -Json $processGoldenFile.text -SchemaFile $processGoldenSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 Process golden differs from its descriptor anchor or closed schema.')
        }

        $processTreeSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/process-tree.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Process tree schema'
        $processArtifactSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/process-tree-artifact.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Process tree artifact schema'
        if ([string]$processDescriptor.pilot_contract.snapshot_schema_relative_path -cne 'schemas/process-tree.schema.json' -or
            [string]$processDescriptor.pilot_contract.artifact_schema_relative_path -cne 'schemas/process-tree-artifact.schema.json' -or
            $processTreeSchemaFile.sha256 -cne ([string]$processDescriptor.pilot_contract.snapshot_schema_sha256).ToLowerInvariant() -or
            $processArtifactSchemaFile.sha256 -cne ([string]$processDescriptor.pilot_contract.artifact_schema_sha256).ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0I021 Process tree schema slots differ from their descriptor anchors.')
        }
        $processHelperFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'scripts/m0/Invoke-M0SyntheticProcessNode.ps1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Process synthetic helper'
        if ([string]$processDescriptor.pilot_contract.helper_relative_path -cne 'scripts/m0/Invoke-M0SyntheticProcessNode.ps1' -or
            $processHelperFile.sha256 -cne ([string]$processDescriptor.pilot_contract.helper_sha256).ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0I021 Process helper differs from its descriptor anchor.')
        }

        $evidenceModuleFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/harness/M0.Evidence.psm1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Evidence module source'
        $markersModuleFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/harness/M0.Markers.psm1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Markers module source'
        $statisticsModuleFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/harness/M0.Statistics.psm1' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Statistics module source'
        $runSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/run.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Evidence run schema'
        $evidenceIntentSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/evidence-intent.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Evidence transaction intent schema'
        $evidenceSnapshotSchemaFile = Open-M0PilotManifestBoundFile `
            -RelativePath 'experiments/m0/protocol/schemas/evidence-snapshot.schema.json' -Entries $manifestEntries `
            -HeldFiles $held -Name 'Evidence snapshot schema'

        $scenarioSchemaRelative = 'experiments/m0/protocol/schemas/synthetic-process-tree-scenario.schema.json'
        $scenarioSchemaFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $script:M0PilotRepositoryRoot `
                $scenarioSchemaRelative.Replace('/', [IO.Path]::DirectorySeparatorChar)) `
            -Name 'Pilot scenario schema' -KeepOpen
        $held.Add($scenarioSchemaFile)
        Assert-M0PilotManifestFileBinding -File $scenarioSchemaFile `
            -Entry (Get-M0PilotManifestSchemaEntry -Entries $manifestEntries -RelativePath $scenarioSchemaRelative) `
            -Name 'scenario schema'
        $scenarioFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root `
                'scenarios\synthetic-process-tree.v1.json') -Name 'Pilot scenario' -KeepOpen
        $held.Add($scenarioFile)
        if ($scenarioFile.sha256 -cne ([string]$protocol.synthetic_process_tree_scenario_sha256).ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0I021 synthetic Pilot scenario differs from the protocol anchor.')
        }
        $scenarioSidecarFile = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root `
                'scenarios\synthetic-process-tree.v1.sha256') -Name 'Pilot scenario sidecar' -KeepOpen
        $held.Add($scenarioSidecarFile)
        if ($scenarioSidecarFile.text -cne "$($scenarioFile.sha256)  synthetic-process-tree.v1.json`n" -or
            -not (Test-Json -Json $scenarioFile.text -SchemaFile $scenarioSchemaFile.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 synthetic Pilot scenario schema/sidecar binding differs.')
        }
        $scenario = $scenarioFile.text | ConvertFrom-Json -Depth 100 -DateKind String
        if ([string]$scenario.protocol_id -cne 'M0-P1' -or [string]$scenario.scenario_id -cne 'synthetic-process-tree' -or
            [string]$scenario.candidate_code -cne 'P' -or [int]$scenario.attempt -ne 1 -or
            [string]$scenario.evidence_role -cne 'synthetic_pilot' -or [bool]$scenario.decision_eligible -or
            (@($scenario.event_sequence) -join '|') -cne 'run_started|artifact_recorded|check_recorded|run_sealed') {
            throw [IO.InvalidDataException]::new('M0I021 synthetic Pilot scenario identity/sequence differs.')
        }

        $slotDefinitions = @(
            [pscustomobject]@{ name = 'intent_schema'; relative = 'schemas/pilot-intent.schema.json'; hash = [string]$scenario.pilot_intent_contract.schema_sha256; manifest = $true },
            [pscustomobject]@{ name = 'event_schema'; relative = 'schemas/event.schema.json'; hash = [string]$scenario.pilot_intent_contract.event_schema_sha256; manifest = $true },
            [pscustomobject]@{ name = 'intent_golden'; relative = 'goldens/pilot-intent.v1.json'; hash = [string]$scenario.pilot_intent_contract.golden_sha256; manifest = $false },
            [pscustomobject]@{ name = 'summary_input_schema'; relative = 'schemas/pilot-summary-input.schema.json'; hash = [string]$scenario.summary_contract.input_schema_sha256; manifest = $true },
            [pscustomobject]@{ name = 'summary_input'; relative = 'goldens/pilot-summary-input.v1.json'; hash = [string]$scenario.summary_contract.input_sha256; manifest = $false },
            [pscustomobject]@{ name = 'summary_schema'; relative = 'schemas/pilot-summary.schema.json'; hash = [string]$scenario.summary_contract.schema_sha256; manifest = $true },
            [pscustomobject]@{ name = 'summary_golden'; relative = 'goldens/pilot-summary.v1.json'; hash = [string]$scenario.summary_contract.golden_sha256; manifest = $false }
        )
        $scenarioPaths = [ordered]@{
            intent_schema = [string]$scenario.pilot_intent_contract.schema_relative_path
            event_schema = [string]$scenario.pilot_intent_contract.event_schema_relative_path
            intent_golden = [string]$scenario.pilot_intent_contract.golden_relative_path
            summary_input_schema = [string]$scenario.summary_contract.input_schema_relative_path
            summary_input = [string]$scenario.summary_contract.input_relative_path
            summary_schema = [string]$scenario.summary_contract.schema_relative_path
            summary_golden = [string]$scenario.summary_contract.golden_relative_path
        }
        $slotFiles = [ordered]@{}
        foreach ($slot in $slotDefinitions) {
            if ([string]$scenarioPaths[$slot.name] -cne [string]$slot.relative) {
                throw [IO.InvalidDataException]::new("M0I021 Pilot slot '$($slot.name)' path differs from its literal contract.")
            }
            $file = Read-M0PilotTrustedFile -LiteralPath (Join-Path $root $slot.relative.Replace('/', `
                        [IO.Path]::DirectorySeparatorChar)) -Name "Pilot $($slot.name)" -KeepOpen
            $held.Add($file)
            if ($file.sha256 -cne ([string]$slot.hash).ToLowerInvariant()) {
                throw [IO.InvalidDataException]::new("M0I021 Pilot slot '$($slot.name)' differs from its scenario anchor.")
            }
            if ([bool]$slot.manifest) {
                $manifestRelative = "experiments/m0/protocol/$($slot.relative)"
                Assert-M0PilotManifestFileBinding -File $file `
                    -Entry (Get-M0PilotManifestSchemaEntry -Entries $manifestEntries -RelativePath $manifestRelative) `
                    -Name $slot.name
            }
            $slotFiles[$slot.name] = $file
        }

        if (-not (Test-Json -Json $slotFiles.intent_golden.text -SchemaFile $slotFiles.intent_schema.path -ErrorAction SilentlyContinue) -or
            -not (Test-Json -Json $slotFiles.summary_input.text -SchemaFile $slotFiles.summary_input_schema.path -ErrorAction SilentlyContinue) -or
            -not (Test-Json -Json $slotFiles.summary_golden.text -SchemaFile $slotFiles.summary_schema.path -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new('M0I021 Pilot golden failed its retained anchored schema.')
        }
        $intent = $slotFiles.intent_golden.text | ConvertFrom-Json -Depth 100 -DateKind String
        $summaryInput = $slotFiles.summary_input.text | ConvertFrom-Json -Depth 100 -DateKind String
        $intentBuilt = New-M0SyntheticPilotIntent -Intent $intent
        $summaryBuilt = New-M0SyntheticPilotSummary -SummaryInput $summaryInput
        $null = Assert-M0SyntheticPilotFixtureBindings -PilotIntent $intent -SummaryInput $summaryInput
        if (-not [Linq.Enumerable]::SequenceEqual[byte]($script:M0PilotUtf8.GetBytes($intentBuilt.canonical_json),
                [byte[]]$slotFiles.intent_golden.bytes) -or
            -not [Linq.Enumerable]::SequenceEqual[byte]($script:M0PilotUtf8.GetBytes($summaryBuilt.canonical_json),
                [byte[]]$slotFiles.summary_golden.bytes)) {
            throw [IO.InvalidDataException]::new('M0I021 Pilot canonical builders differ from retained goldens.')
        }

        if ($null -ne $script:M0PilotArtifactsTestHook) {
            $hook = $script:M0PilotArtifactsTestHook
            $script:M0PilotArtifactsTestHook = $null
            switch -CaseSensitive ([string]$hook.stage) {
                'after-all-open-hardlink' {
                    $null = New-Item -ItemType HardLink -Path ([string]$hook.link_path) -Target ([string]$hook.target_path)
                }
                'after-all-open-overwrite' {
                    $writer = $null
                    try {
                        $writer = [IO.FileStream]::new([string]$hook.target_path, [IO.FileMode]::Open,
                            [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
                    }
                    catch [IO.IOException] { }
                    finally { if ($null -ne $writer) { $writer.Dispose() } }
                    if ($null -ne $writer) {
                        throw [IO.InvalidDataException]::new('M0I021 retained Pilot contract unexpectedly allowed a writer.')
                    }
                }
                'after-all-open-rename' {
                    $renamed = $false
                    try { [IO.File]::Move([string]$hook.target_path, [string]$hook.destination_path); $renamed = $true }
                    catch [IO.IOException] { }
                    if ($renamed) {
                        [IO.File]::Move([string]$hook.destination_path, [string]$hook.target_path)
                        throw [IO.InvalidDataException]::new('M0I021 retained Pilot contract unexpectedly allowed rename.')
                    }
                }
                default { throw [IO.InvalidDataException]::new('M0I021 unknown Pilot artifact trust test hook.') }
            }
        }
        foreach ($file in $held) {
            Assert-M0PilotHeldFileStable -File $file -Name ([IO.Path]::GetFileName([string]$file.path))
        }
        $trustSet = [pscustomobject][ordered]@{
            protocol_root = $root
            protocol_sha256 = $protocolFile.sha256
            scenario_sha256 = $scenarioFile.sha256
            manifest_sha256 = $manifestFile.sha256
            protocol = $protocol
            scenario = $scenario
            process_descriptor = $processDescriptor
            process_module = $processModule
            process_artifact_schema_path = $processArtifactSchemaFile.path
            process_tree_schema_path = $processTreeSchemaFile.path
            evidence_module_path = $evidenceModuleFile.path
            evidence_schema_root = Join-Path $root 'schemas'
            runtime_sources = [pscustomobject][ordered]@{
                evidence = $evidenceModuleFile
                markers = $markersModuleFile
                statistics = $statisticsModuleFile
            }
            held_files = $held
        }
        return $trustSet
    }
    catch {
        Close-M0PilotTrustSet -TrustSet ([pscustomobject]@{ held_files = $held })
        throw
    }
}

function Assert-M0PilotPhysicalScope {
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $EvidenceRoot
    )

    $comparison = [StringComparison]::OrdinalIgnoreCase
    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    if (-not $repository.Equals($script:M0PilotRepositoryRoot, $comparison)) {
        throw [UnauthorizedAccessException]::new('M0I023 physical Pilot RepositoryRoot differs from the loaded module repository.')
    }
    $root = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/')
    $outRoot = [IO.Path]::GetFullPath((Join-Path $repository 'out\m0')).TrimEnd('\', '/')
    if (-not $root.StartsWith($outRoot + [IO.Path]::DirectorySeparatorChar, $comparison) -or
        -not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [UnauthorizedAccessException]::new('M0I023 EvidenceRoot must be an existing child of the trusted repository out/m0 root.')
    }
    $relative = [IO.Path]::GetRelativePath($repository, $root)
    $ancestor = $repository
    foreach ($segment in $relative.Split([char[]]@('\', '/'), [StringSplitOptions]::RemoveEmptyEntries)) {
        $ancestor = Join-Path $ancestor $segment
        $item = Get-Item -LiteralPath $ancestor -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            throw [IO.InvalidDataException]::new("M0I023 links are forbidden in physical Pilot root '$ancestor'.")
        }
    }
    return $root
}

function Get-M0PilotPublicationPaths {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId
    )
    if ($RunId -cnotmatch '^run-P-[A-Za-z0-9._-]{1,122}$') {
        throw [ArgumentException]::new('M0I023 physical Pilot RunId differs from the closed P identity.')
    }
    return [pscustomobject][ordered]@{
        root = $EvidenceRoot
        lock = Join-Path $EvidenceRoot '.m0-evidence.lock'
        raw = Join-Path $EvidenceRoot "$RunId.jsonl"
        pilot_intent = Join-Path $EvidenceRoot "$RunId.pilot-intent.json"
        evidence_intent = Join-Path $EvidenceRoot "$RunId.intent.json"
        run = Join-Path $EvidenceRoot "$RunId.run.json"
        snapshot = Join-Path $EvidenceRoot "$RunId.snapshot.json"
        artifacts = Join-Path $EvidenceRoot 'artifacts'
        process_tree = Join-Path $EvidenceRoot "artifacts\$RunId.process-tree.json"
        summary_pending = Join-Path $EvidenceRoot "$RunId.pilot-summary.pending"
        summary = Join-Path $EvidenceRoot "$RunId.pilot-summary.json"
        summary_sidecar = Join-Path $EvidenceRoot "$RunId.pilot-summary.sha256"
    }
}

function Write-M0PilotCreateNewBytes {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Name
    )
    $stream = $null
    try {
        $fullPath = [IO.Path]::GetFullPath($LiteralPath)
        $stream = [IO.FileStream]::new($fullPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
        [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $fullPath)
        if ($Bytes.Length -gt 0) { $stream.Write($Bytes, 0, $Bytes.Length) }
        $stream.Flush($true)
        [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $fullPath)
        $stream.Position = 0
        $readBack = [byte[]]::new($Bytes.Length)
        if ($readBack.Length -gt 0) { $stream.ReadExactly($readBack, 0, $readBack.Length) }
        if (-not [Linq.Enumerable]::SequenceEqual[byte]($readBack, $Bytes)) {
            throw [IO.InvalidDataException]::new("M0I024 $Name differs on its retained CreateNew handle after durable flush.")
        }
        $result = [pscustomobject][ordered]@{
            path = $fullPath
            bytes = $readBack
            text = if ($readBack.Length -eq 0) { '' } else { $script:M0PilotUtf8.GetString($readBack) }
            sha256 = Get-M0PilotSha256Bytes -Bytes $readBack
            stream = $stream
        }
        $stream = $null
        return $result
    }
    catch [IO.IOException] {
        throw [IO.IOException]::new("M0I024 $Name CreateNew publication refused an existing or unsafe leaf.", $_.Exception)
    }
    finally { if ($null -ne $stream) { $stream.Dispose() } }
}

function New-M0PilotEvidenceEvent {
    param(
        [Parameter(Mandatory)] $CommonInputs,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [int] $Sequence,
        [AllowNull()] $PreviousLineSha256,
        [Parameter(Mandatory)] [string] $Utc,
        [Parameter(Mandatory)] [long] $QpcTicks,
        [Parameter(Mandatory)] [long] $QpcFrequency,
        [Parameter(Mandatory)] [string] $Event,
        [Parameter(Mandatory)] $Payload
    )
    $value = [pscustomobject][ordered]@{
        schema_id = 'm0.event.v1'
        schema_version = 1
        protocol_sha256 = [string]$CommonInputs.protocol_sha256
        environment_sha256 = [string]$CommonInputs.environment_sha256
        fixture_sha256 = [string]$CommonInputs.fixture_sha256
        harness_sha256 = [string]$CommonInputs.harness_sha256
        scenario_sha256 = [string]$CommonInputs.scenario_sha256
        experiment_id = $ExperimentId
        run_id = $RunId
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        event_seq = $Sequence
        previous_line_sha256 = $PreviousLineSha256
        utc = $Utc
        qpc_ticks = $QpcTicks
        qpc_frequency = $QpcFrequency
        event = $Event
        payload = $Payload
    }
    Assert-M0PilotSchema -Value $value -SchemaPath (Join-Path $script:M0PilotProtocolRoot 'schemas\event.schema.json') `
        -Name "physical Pilot event $Sequence"
    return $value
}

function Get-M0PilotProcessArtifactProjection {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $ProcessArtifact,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId
    )
    $document = if ($null -ne $ProcessArtifact.PSObject.Properties['document']) {
        Copy-M0PilotValue $ProcessArtifact.document
    }
    else { Copy-M0PilotValue $ProcessArtifact }
    $inputJson = (& $TrustSet.process_module { param($value) ConvertTo-M0ProcessPublicJson -Value $value } $document) + "`n"
    try {
        $canonical = & $TrustSet.process_module {
            param($value, $schemaPath)
            ConvertTo-M0CanonicalProcessTreeArtifact -Artifact $value -ArtifactSchemaPath $schemaPath
        } $document $TrustSet.process_artifact_schema_path
    }
    catch {
        throw [IO.InvalidDataException]::new(
            'M0I025 prevalidated Process artifact is not a closed canonical physical Pilot artifact.', $_.Exception
        )
    }
    if (-not [Linq.Enumerable]::SequenceEqual[byte](
            $script:M0PilotUtf8.GetBytes($inputJson), [byte[]]$canonical.bytes
        ) -or [string]$canonical.document.experiment_id -cne $ExperimentId -or
        [string]$canonical.document.run_id -cne $RunId -or
        [string]$canonical.document.artifact_id -cne 'process-tree' -or
        [string]$canonical.document.candidate_code -cne 'P' -or
        [string]$canonical.document.scenario_id -cne 'synthetic-process-tree' -or
        [int]$canonical.document.attempt -ne 1 -or
        [string]$canonical.document.evidence_role -cne 'synthetic_pilot' -or
        [bool]$canonical.document.decision_eligible) {
        throw [IO.InvalidDataException]::new(
            'M0I025 prevalidated Process artifact differs from its exact canonical property order or physical Pilot identity.'
        )
    }
    $outcome = & $TrustSet.process_module {
        param($value) Resolve-M0ProcessPilotOutcome -ProcessArtifact $value
    } $canonical.document
    return [pscustomobject][ordered]@{
        document = $canonical.document
        canonical_json = [string]$canonical.canonical_json
        bytes = [byte[]]$canonical.bytes
        sha256 = [string]$canonical.sha256
        size_bytes = [long]$canonical.size_bytes
        outcome = $outcome
    }
}

function Get-M0PilotTrustedCommonInputsFromSet {
    param([Parameter(Mandatory)] $TrustSet)
    return [pscustomobject][ordered]@{
        protocol_sha256 = ([string]$TrustSet.protocol_sha256).ToLowerInvariant()
        environment_sha256 = ([string]$TrustSet.protocol.environment_bundle_sha256).ToLowerInvariant()
        fixture_sha256 = ([string]$TrustSet.protocol.fixture_manifest_sha256).ToLowerInvariant()
        harness_sha256 = ([string]$TrustSet.manifest_sha256).ToLowerInvariant()
        scenario_sha256 = ([string]$TrustSet.scenario_sha256).ToLowerInvariant()
    }
}

function Get-M0PilotPhysicalInventory {
    param([Parameter(Mandatory)] [string] $EvidenceRoot)
    return @(Get-ChildItem -LiteralPath $EvidenceRoot -File -Recurse -Force | ForEach-Object {
        [IO.Path]::GetRelativePath($EvidenceRoot, $_.FullName).Replace('\', '/')
    } | Sort-Object)
}

function Assert-M0PilotPhysicalInventory {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string[]] $Expected
    )
    $actual = @(Get-M0PilotPhysicalInventory -EvidenceRoot $EvidenceRoot)
    $expectedSorted = @($Expected | Sort-Object)
    if (($actual | ConvertTo-Json -Compress) -cne ($expectedSorted | ConvertTo-Json -Compress)) {
        throw [IO.InvalidDataException]::new('M0I026 physical Pilot inventory contains a missing, foreign, temporary or decisive A/B leaf.')
    }
    $directories = @(Get-ChildItem -LiteralPath $EvidenceRoot -Directory -Recurse -Force)
    if ($directories.Count -ne 1 -or
        [IO.Path]::GetRelativePath($EvidenceRoot, $directories[0].FullName).Replace('\', '/') -cne 'artifacts') {
        throw [IO.InvalidDataException]::new('M0I026 physical Pilot inventory must contain only its literal artifacts directory.')
    }
    foreach ($directory in @((Get-Item -LiteralPath $EvidenceRoot -Force), $directories[0])) {
        if (($directory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($directory.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$directory.LinkType))) {
            throw [IO.InvalidDataException]::new('M0I026 physical Pilot directories cannot be links or reparse points.')
        }
    }
}

function Close-M0PilotPublicationSet {
    param([AllowNull()] $PublicationSet)
    if ($null -eq $PublicationSet) { return }
    if ($null -ne $PublicationSet.held_files) {
        for ($index = $PublicationSet.held_files.Count - 1; $index -ge 0; $index--) {
            if ($null -ne $PublicationSet.held_files[$index].stream) {
                $PublicationSet.held_files[$index].stream.Dispose()
            }
        }
    }
    if ($null -ne $PublicationSet.root_lock) { $PublicationSet.root_lock.Dispose() }
}

function Open-M0PilotPublicationSet {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $EvidenceModule,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId,
        [AllowNull()] $CreatorFiles,
        [switch] $RequireCommittedSummary
    )

    $root = Assert-M0PilotPhysicalScope -RepositoryRoot $RepositoryRoot -EvidenceRoot $EvidenceRoot
    $paths = Get-M0PilotPublicationPaths -EvidenceRoot $root -RunId $RunId
    $rootLock = $null
    $held = [Collections.Generic.List[object]]::new()
    try {
        if (-not (Test-Path -LiteralPath $paths.lock -PathType Leaf)) {
            throw [IO.InvalidDataException]::new('M0I026 physical Pilot publication is missing the Evidence root sentinel.')
        }
        $rootLock = [IO.FileStream]::new($paths.lock, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        [M0ProcessNativeV1.TrustedFile]::Validate($rootLock.SafeFileHandle, [IO.Path]::GetFullPath($paths.lock))
        if ($rootLock.Length -ne 0) {
            throw [IO.InvalidDataException]::new('M0I026 Evidence root sentinel must remain the exact empty lock leaf.')
        }

        $baseRelative = @(
            '.m0-evidence.lock',
            "artifacts/$RunId.process-tree.json",
            "$RunId.intent.json",
            "$RunId.jsonl",
            "$RunId.pilot-intent.json",
            "$RunId.run.json",
            "$RunId.snapshot.json"
        )
        $hasSummary = Test-Path -LiteralPath $paths.summary -PathType Leaf
        $hasSidecar = Test-Path -LiteralPath $paths.summary_sidecar -PathType Leaf
        $hasPending = Test-Path -LiteralPath $paths.summary_pending -PathType Leaf
        if (($hasSidecar -and -not $hasSummary) -or ($hasPending -and ($hasSummary -or $hasSidecar)) -or
            ($RequireCommittedSummary -and (-not $hasSummary -or -not $hasSidecar -or $hasPending))) {
            throw [IO.InvalidDataException]::new('M0I026 Pilot summary final/pending/sidecar publication state is conflicting or incomplete.')
        }
        $expectedRelative = [Collections.Generic.List[string]]::new()
        foreach ($relative in $baseRelative) { $expectedRelative.Add($relative) }
        if ($hasPending) { $expectedRelative.Add("$RunId.pilot-summary.pending") }
        if ($hasSummary) { $expectedRelative.Add("$RunId.pilot-summary.json") }
        if ($hasSidecar) { $expectedRelative.Add("$RunId.pilot-summary.sha256") }
        Assert-M0PilotPhysicalInventory -EvidenceRoot $root -Expected @($expectedRelative)

        $files = [ordered]@{}
        foreach ($definition in @(
            [pscustomobject]@{ key = 'pilot_intent'; path = $paths.pilot_intent; name = 'physical Pilot intent' },
            [pscustomobject]@{ key = 'raw'; path = $paths.raw; name = 'physical Pilot raw' },
            [pscustomobject]@{ key = 'evidence_intent'; path = $paths.evidence_intent; name = 'physical Evidence intent' },
            [pscustomobject]@{ key = 'run'; path = $paths.run; name = 'physical Pilot run' },
            [pscustomobject]@{ key = 'snapshot'; path = $paths.snapshot; name = 'physical Evidence snapshot' },
            [pscustomobject]@{ key = 'artifact'; path = $paths.process_tree; name = 'physical Process artifact' }
        )) {
            $file = if ($null -ne $CreatorFiles -and $null -ne $CreatorFiles.PSObject.Properties[$definition.key]) {
                $CreatorFiles.($definition.key)
            }
            else {
                Read-M0PilotTrustedFile -LiteralPath $definition.path -Name $definition.name -KeepOpen
            }
            $held.Add($file)
            $files[$definition.key] = $file
        }

        $pilotIntent = $files.pilot_intent.text | ConvertFrom-Json -Depth 100 -DateKind String
        $intentBuilt = New-M0SyntheticPilotIntent -Intent $pilotIntent
        if (-not [Linq.Enumerable]::SequenceEqual[byte]($script:M0PilotUtf8.GetBytes($intentBuilt.canonical_json),
                [byte[]]$files.pilot_intent.bytes) -or [string]$pilotIntent.experiment_id -cne $ExperimentId -or
            [string]$pilotIntent.run_id -cne $RunId) {
            throw [IO.InvalidDataException]::new('M0I027 physical Pilot intent differs from its canonical identity bytes.')
        }
        $commonInputs = Get-M0PilotTrustedCommonInputsFromSet -TrustSet $TrustSet
        foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
            if ([string]$pilotIntent.common_inputs.$name -cne [string]$commonInputs.$name) {
                throw [IO.InvalidDataException]::new("M0I027 Pilot intent common input '$name' differs from retained trust.")
            }
        }

        $rawLines = $files.raw.text.Split("`n", [StringSplitOptions]::None)
        if ($rawLines.Count -ne 5 -or $rawLines[4] -cne '') {
            throw [IO.InvalidDataException]::new('M0I027 synthetic Pilot raw must contain exactly four canonical JSON lines.')
        }
        $events = [Collections.Generic.List[object]]::new()
        for ($index = 0; $index -lt 4; $index++) {
            if (-not (Test-Json -Json $rawLines[$index] -SchemaFile (Join-Path $TrustSet.protocol_root 'schemas\event.schema.json') `
                    -ErrorAction SilentlyContinue)) {
                throw [IO.InvalidDataException]::new("M0I027 raw event $($index + 1) failed the retained event schema.")
            }
            $event = $rawLines[$index] | ConvertFrom-Json -Depth 100 -DateKind String
            if (($event | ConvertTo-Json -Compress -Depth 100) -cne $rawLines[$index]) {
                throw [IO.InvalidDataException]::new("M0I027 raw event $($index + 1) is not canonical JSON.")
            }
            $events.Add($event)
        }
        $firstLineBytes = $script:M0PilotUtf8.GetBytes($rawLines[0] + "`n")
        if ($rawLines[0] -cne ($pilotIntent.run_started.event | ConvertTo-Json -Compress -Depth 100) -or
            (Get-M0PilotSha256Bytes -Bytes $script:M0PilotUtf8.GetBytes($rawLines[0])) -cne
                [string]$pilotIntent.run_started.canonical_line_sha256 -or
            [long]$firstLineBytes.Length -ne [long]$pilotIntent.run_started.size_bytes) {
            throw [IO.InvalidDataException]::new('M0I027 raw first line differs byte-for-byte from the launch intent run_started anchor.')
        }

        $artifactDocument = $files.artifact.text | ConvertFrom-Json -Depth 100 -DateKind String
        $artifactProjection = Get-M0PilotProcessArtifactProjection -TrustSet $TrustSet -ProcessArtifact $artifactDocument `
            -ExperimentId $ExperimentId -RunId $RunId
        if (-not [Linq.Enumerable]::SequenceEqual[byte]([byte[]]$artifactProjection.bytes, [byte[]]$files.artifact.bytes)) {
            throw [IO.InvalidDataException]::new('M0I027 Process artifact bytes are not the canonical mapper input.')
        }

        $evidence = & $EvidenceModule {
            param($evidenceRoot, $runId, $schemaRoot)
            $transactionPaths = Get-M0EvidenceTransactionPaths -EvidenceRoot $evidenceRoot -RunId $runId
            Read-M0EvidenceCommittedRunNoLock -Paths $transactionPaths -SchemaRoot $schemaRoot
        } $root $RunId $TrustSet.evidence_schema_root
        $runDocument = $files.run.text | ConvertFrom-Json -Depth 100 -DateKind String
        $snapshotDocument = $files.snapshot.text | ConvertFrom-Json -Depth 100 -DateKind String
        if ([string]$runDocument.experiment_id -cne $ExperimentId -or [string]$runDocument.run_id -cne $RunId -or
            [string]$snapshotDocument.run_id -cne $RunId -or [string]$evidence.transaction_id -cne [string]$snapshotDocument.transaction_id) {
            throw [IO.InvalidDataException]::new('M0I027 committed Evidence identity differs from the requested physical Pilot run.')
        }

        $artifactEvent = $events[1]
        $expectedArtifactPayload = [pscustomobject][ordered]@{
            artifact_id = 'process-tree'
            relative_path = "artifacts/$RunId.process-tree.json"
            sha256 = [string]$artifactProjection.sha256
            size_bytes = [long]$artifactProjection.size_bytes
            transient = $false
            redacted = $true
        }
        if (($artifactEvent.payload | ConvertTo-Json -Compress -Depth 20) -cne
            ($expectedArtifactPayload | ConvertTo-Json -Compress -Depth 20) -or
            ($events[2].payload | ConvertTo-Json -Compress -Depth 20) -cne
            ($artifactProjection.outcome.check_payload | ConvertTo-Json -Compress -Depth 20) -or
            [string]$runDocument.process_tree_sha256 -cne [string]$artifactProjection.sha256 -or
            [string]$runDocument.status -cne [string]$artifactProjection.outcome.run_status -or
            [string]$runDocument.status_reason -cne [string]$artifactProjection.outcome.run_status_reason -or
            [string]$runDocument.error_code -cne [string]$artifactProjection.outcome.run_error_code) {
            throw [IO.InvalidDataException]::new('M0I027 raw/check/run projection differs from the single post-Close Process artifact.')
        }

        $frequency = [long]$events[0].qpc_frequency
        for ($index = 1; $index -lt $events.Count; $index++) {
            if ([long]$events[$index].qpc_frequency -ne $frequency -or
                [long]$events[$index].qpc_ticks -lt [long]$events[$index - 1].qpc_ticks -or
                [DateTimeOffset]::ParseExact([string]$events[$index].utc, 'yyyy-MM-ddTHH:mm:ss.fffffffZ',
                    [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal) -lt
                [DateTimeOffset]::ParseExact([string]$events[$index - 1].utc, 'yyyy-MM-ddTHH:mm:ss.fffffffZ',
                    [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal)) {
                throw [IO.InvalidDataException]::new('M0I027 raw physical clocks regress or change QPC frequency.')
            }
        }
        if ($null -ne $artifactDocument.snapshot.qpc_before_create_ticks) {
            [long]$beforeCreate = [long]::Parse([string]$artifactDocument.snapshot.qpc_before_create_ticks,
                [Globalization.CultureInfo]::InvariantCulture)
            [long]$afterResume = [long]::Parse([string]$artifactDocument.snapshot.qpc_after_resume_ticks,
                [Globalization.CultureInfo]::InvariantCulture)
            if ([long]$events[0].qpc_ticks -gt $beforeCreate -or $beforeCreate -ge $afterResume -or
                $afterResume -gt [long]$events[1].qpc_ticks -or
                [long]$artifactDocument.snapshot.qpc_frequency -ne $frequency) {
                throw [IO.InvalidDataException]::new('M0I027 run_started/live/artifact QPC boundaries are not causally ordered.')
            }
        }

        $identities = @($artifactDocument.snapshot.nodes | ForEach-Object {
            [pscustomobject][ordered]@{
                role = [string]$_.role
                pid = [uint32]$_.identity.pid
                creation_time_filetime_utc = [string]$_.identity.creation_time_filetime_utc
            }
        })
        $summaryInput = [pscustomobject][ordered]@{
            '$schema' = 'schemas/pilot-summary-input.schema.json'
            schema_id = 'm0.synthetic-pilot-summary-input.v1'
            schema_version = 1
            candidate_code = 'P'
            scenario_id = 'synthetic-process-tree'
            attempt = 1
            evidence_role = 'synthetic_pilot'
            decision_eligible = $false
            publication_state = 'committed'
            run = [pscustomobject][ordered]@{
                status = [string]$artifactProjection.outcome.run_status
                status_reason = $artifactProjection.outcome.run_status_reason
                error_code = $artifactProjection.outcome.run_error_code
            }
            process = [pscustomobject][ordered]@{
                snapshot_origin = [string]$artifactDocument.snapshot_origin
                state = [string]$artifactDocument.snapshot.state
                error_code = $artifactDocument.snapshot.error_code
                missing_capabilities = @($artifactDocument.snapshot.missing_capabilities)
            }
            cleanup = [pscustomobject][ordered]@{
                attempted = [bool]$artifactDocument.cleanup.attempted
                completed = [bool]$artifactDocument.cleanup.completed
                survivor_count = [long]$artifactDocument.cleanup.survivor_count
                failure_code = $artifactDocument.cleanup.failure_code
                failure_codes = @($artifactDocument.cleanup.failure_codes)
            }
            check = Copy-M0PilotValue $artifactProjection.outcome.check_payload
            artifact_count = 1
            receipt = [pscustomobject][ordered]@{
                experiment_id = $ExperimentId
                run_id = $RunId
                transaction_id = [string]$snapshotDocument.transaction_id
                common_inputs = $commonInputs
                pilot_intent = [pscustomobject][ordered]@{
                    relative_path = "$RunId.pilot-intent.json"; sha256 = [string]$files.pilot_intent.sha256
                    size_bytes = [long]$files.pilot_intent.bytes.Length
                }
                raws = @([pscustomobject][ordered]@{
                    relative_path = "$RunId.jsonl"; sha256 = [string]$files.raw.sha256
                    size_bytes = [long]$files.raw.bytes.Length; event_count = 4
                    first_line_sha256 = Get-M0PilotSha256Bytes -Bytes $script:M0PilotUtf8.GetBytes($rawLines[0])
                    first_line_size_bytes = [long]$firstLineBytes.Length
                    final_line_sha256 = Get-M0PilotSha256Bytes -Bytes $script:M0PilotUtf8.GetBytes($rawLines[3])
                })
                traces = @()
                intent = [pscustomobject][ordered]@{
                    relative_path = "$RunId.intent.json"; sha256 = [string]$files.evidence_intent.sha256
                    size_bytes = [long]$files.evidence_intent.bytes.Length
                }
                run = [pscustomobject][ordered]@{
                    relative_path = "$RunId.run.json"; sha256 = [string]$files.run.sha256
                    size_bytes = [long]$files.run.bytes.Length
                }
                snapshot = [pscustomobject][ordered]@{
                    relative_path = "$RunId.snapshot.json"; sha256 = [string]$files.snapshot.sha256
                    size_bytes = [long]$files.snapshot.bytes.Length
                }
                artifacts = @([pscustomobject][ordered]@{
                    artifact_id = 'process-tree'; relative_path = "artifacts/$RunId.process-tree.json"
                    sha256 = [string]$files.artifact.sha256; size_bytes = [long]$files.artifact.bytes.Length
                })
                started_utc = [string]$events[0].utc
                sealed_utc = [string]$events[3].utc
                started_qpc_ticks = [long]$events[0].qpc_ticks
                sealed_qpc_ticks = [long]$events[3].qpc_ticks
                qpc_frequency = $frequency
                process_identities = $identities
            }
        }
        $summaryBuilt = New-M0SyntheticPilotSummary -SummaryInput $summaryInput
        $null = Assert-M0SyntheticPilotFixtureBindings -PilotIntent $pilotIntent -SummaryInput $summaryInput

        if ($hasSummary) {
            $summaryFile = Read-M0PilotTrustedFile -LiteralPath $paths.summary -Name 'physical Pilot summary' -KeepOpen
            $held.Add($summaryFile)
            $files.summary = $summaryFile
            if (-not [Linq.Enumerable]::SequenceEqual[byte]($script:M0PilotUtf8.GetBytes($summaryBuilt.canonical_json),
                    [byte[]]$summaryFile.bytes)) {
                throw [IO.InvalidDataException]::new('M0I028 persisted Pilot summary differs from artifact-derived canonical bytes.')
            }
            $null = Assert-M0SyntheticPilotSummary -Summary ($summaryFile.text | ConvertFrom-Json -Depth 100 -DateKind String)
        }
        if ($hasPending) {
            $pendingFile = Read-M0PilotTrustedFile -LiteralPath $paths.summary_pending -Name 'physical Pilot summary pending' -KeepOpen
            $held.Add($pendingFile)
            $files.summary_pending = $pendingFile
            if (-not [Linq.Enumerable]::SequenceEqual[byte]($script:M0PilotUtf8.GetBytes($summaryBuilt.canonical_json),
                    [byte[]]$pendingFile.bytes)) {
                throw [IO.InvalidDataException]::new('M0I028 pending Pilot summary diverges from artifact-derived canonical bytes.')
            }
        }
        if ($hasSidecar) {
            $sidecarFile = Read-M0PilotTrustedFile -LiteralPath $paths.summary_sidecar -Name 'physical Pilot summary sidecar' -KeepOpen
            $held.Add($sidecarFile)
            $files.summary_sidecar = $sidecarFile
            if ($sidecarFile.text -cne "$($summaryBuilt.sha256)  $RunId.pilot-summary.json`n") {
                throw [IO.InvalidDataException]::new('M0I028 Pilot summary sidecar differs from exact artifact-derived summary bytes.')
            }
        }
        foreach ($file in $held) {
            Assert-M0PilotHeldFileStable -File $file -Name ([IO.Path]::GetFileName([string]$file.path))
        }
        [M0ProcessNativeV1.TrustedFile]::Validate($rootLock.SafeFileHandle, [IO.Path]::GetFullPath($paths.lock))
        Assert-M0PilotPhysicalInventory -EvidenceRoot $root -Expected @($expectedRelative)
        return [pscustomobject][ordered]@{
            root = $root
            paths = $paths
            root_lock = $rootLock
            held_files = $held
            files = $files
            summary_input = $summaryInput
            summary = $summaryBuilt
            artifact = $artifactProjection
            evidence = $evidence
            has_summary = $hasSummary
            has_sidecar = $hasSidecar
            has_pending = $hasPending
            expected_inventory = @($expectedRelative)
        }
    }
    catch {
        Close-M0PilotPublicationSet -PublicationSet ([pscustomobject]@{ held_files = $held; root_lock = $rootLock })
        throw
    }
}

function Complete-M0PilotSummaryPublication {
    param(
        [Parameter(Mandatory)] $PublicationSet,
        [Parameter(Mandatory)] $EvidenceModule,
        [AllowEmptyString()] [string] $CrashAfterBoundary = '',
        [AllowNull()] $LiveHook
    )

    $summaryBytes = $script:M0PilotUtf8.GetBytes([string]$PublicationSet.summary.canonical_json)
    if (-not $PublicationSet.has_summary) {
        if (-not $PublicationSet.has_pending) {
            $pending = Write-M0PilotCreateNewBytes -LiteralPath $PublicationSet.paths.summary_pending `
                -Bytes $summaryBytes -Name 'Pilot summary pending'
            $PublicationSet.held_files.Add($pending)
            $PublicationSet.files.summary_pending = $pending
            $PublicationSet.has_pending = $true
            Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'summary_pending'
            Invoke-M0PilotCrashBoundary -RequestedBoundary $CrashAfterBoundary `
                -ActualBoundary 'summary_pending' -AbruptCrash $false
        }
        if ($null -ne $PublicationSet.files.summary_pending.stream) {
            Assert-M0PilotHeldFileStable -File $PublicationSet.files.summary_pending -Name 'Pilot summary pending'
            $PublicationSet.files.summary_pending.stream.Dispose()
            $PublicationSet.files.summary_pending.stream = $null
        }
        & $EvidenceModule {
            param($source, $destination)
            Move-M0EvidenceNoOverwrite -Source $source -Destination $destination -Name 'Pilot summary'
        } $PublicationSet.paths.summary_pending $PublicationSet.paths.summary
        $PublicationSet.has_pending = $false
        $PublicationSet.has_summary = $true
        $summaryFile = Read-M0PilotTrustedFile -LiteralPath $PublicationSet.paths.summary `
            -Name 'physical Pilot summary' -KeepOpen
        $PublicationSet.held_files.Add($summaryFile)
        $PublicationSet.files.summary = $summaryFile
        if (-not [Linq.Enumerable]::SequenceEqual[byte]([byte[]]$summaryFile.bytes, $summaryBytes)) {
            throw [IO.InvalidDataException]::new('M0I028 atomically published Pilot summary differs from derived bytes.')
        }
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'summary_published'
        if ($CrashAfterBoundary -ceq 'summary_published') {
            throw [IO.IOException]::new('M0I029 crash injection after summary_published and before the commit sidecar.')
        }
    }
    elseif (-not [Linq.Enumerable]::SequenceEqual[byte]([byte[]]$PublicationSet.files.summary.bytes, $summaryBytes)) {
        throw [IO.InvalidDataException]::new('M0I028 existing Pilot summary diverges from derived bytes and cannot be overwritten.')
    }

    if (-not $PublicationSet.has_sidecar) {
        $sidecarBytes = $script:M0PilotUtf8.GetBytes(
            "$($PublicationSet.summary.sha256)  $([IO.Path]::GetFileName($PublicationSet.paths.summary))`n"
        )
        $sidecar = Write-M0PilotCreateNewBytes -LiteralPath $PublicationSet.paths.summary_sidecar `
            -Bytes $sidecarBytes -Name 'Pilot summary commit sidecar'
        $PublicationSet.held_files.Add($sidecar)
        $PublicationSet.files.summary_sidecar = $sidecar
        $PublicationSet.has_sidecar = $true
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'sidecar_published'
    }
    foreach ($file in $PublicationSet.held_files) {
        if ($null -ne $file.stream) {
            Assert-M0PilotHeldFileStable -File $file -Name ([IO.Path]::GetFileName([string]$file.path))
        }
    }
    $expected = @(
        '.m0-evidence.lock',
        [IO.Path]::GetRelativePath($PublicationSet.root, $PublicationSet.paths.process_tree).Replace('\', '/'),
        [IO.Path]::GetFileName($PublicationSet.paths.evidence_intent),
        [IO.Path]::GetFileName($PublicationSet.paths.raw),
        [IO.Path]::GetFileName($PublicationSet.paths.pilot_intent),
        [IO.Path]::GetFileName($PublicationSet.paths.summary),
        [IO.Path]::GetFileName($PublicationSet.paths.summary_sidecar),
        [IO.Path]::GetFileName($PublicationSet.paths.run),
        [IO.Path]::GetFileName($PublicationSet.paths.snapshot)
    )
    Assert-M0PilotPhysicalInventory -EvidenceRoot $PublicationSet.root -Expected $expected
}

function New-M0PilotPhysicalResult {
    param(
        [Parameter(Mandatory)] $PublicationSet,
        [bool] $LiveLaunchPerformed = $false
    )
    return [pscustomobject][ordered]@{
        publication_state = 'committed'
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        run_status = [string]$PublicationSet.summary.document.semantic.run.status
        run_status_reason = $PublicationSet.summary.document.semantic.run.status_reason
        run_error_code = $PublicationSet.summary.document.semantic.run.error_code
        cleanup_complete = [bool]$PublicationSet.summary.document.semantic.cleanup.completed
        survivor_count = [long]$PublicationSet.summary.document.semantic.cleanup.survivor_count
        summary_sha256 = [string]$PublicationSet.summary.sha256
        process_tree_sha256 = [string]$PublicationSet.artifact.sha256
        common_inputs = Copy-M0PilotValue $PublicationSet.summary.document.receipt.common_inputs
        live_launch_performed = $LiveLaunchPerformed
    }
}

function Publish-M0SyntheticPilotFixture {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Request)

    $trust = $null
    $publication = $null
    $writer = $null
    $createdFiles = [Collections.Generic.List[object]]::new()
    try {
        $trust = Open-M0PilotContractTrustSet -ProtocolRoot ([string]$Request.protocol_root) `
            -ExpectedProtocolSha256 ([string]$Request.expected_protocol_sha256)
        $root = Assert-M0PilotPhysicalScope -RepositoryRoot ([string]$Request.repository_root) `
            -EvidenceRoot ([string]$Request.evidence_root)
        $paths = Get-M0PilotPublicationPaths -EvidenceRoot $root -RunId ([string]$Request.run_id)
        $evidenceModule = Get-M0PilotEvidenceModule -TrustSet $trust
        $existing = @(Get-ChildItem -LiteralPath $root -Force)
        if ($existing.Count -gt 0) {
            if (-not (Test-Path -LiteralPath $paths.evidence_intent -PathType Leaf)) {
                throw [IO.InvalidDataException]::new(
                    'M0I030 pre-Evidence-intent Pilot state is incomplete; require a new EvidenceRoot and RunId without relaunch or overwrite.'
                )
            }
            $recovery = & $evidenceModule {
                param($evidenceRoot, $runId, $schemaRoot)
                Get-M0EvidenceRecoveryState -EvidenceRoot $evidenceRoot -RunId $runId -SchemaRoot $schemaRoot
            } $root ([string]$Request.run_id) $trust.evidence_schema_root
            if ([string]$recovery.state -cne 'committed') {
                if ([string]$recovery.state -cnotin @('intent_only', 'raw_sealed', 'run_published')) {
                    throw [IO.InvalidDataException]::new("M0I030 Evidence recovery state '$($recovery.state)' is conflicting and cannot be overwritten.")
                }
                $null = & $evidenceModule {
                    param($evidenceRoot, $runId, $schemaRoot)
                    Resume-M0EvidenceRun -EvidenceRoot $evidenceRoot -RunId $runId -SchemaRoot $schemaRoot
                } $root ([string]$Request.run_id) $trust.evidence_schema_root
            }
            $publication = Open-M0PilotPublicationSet -TrustSet $trust -EvidenceModule $evidenceModule `
                -RepositoryRoot ([string]$Request.repository_root) -EvidenceRoot $root `
                -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
            $requestedArtifact = Get-M0PilotProcessArtifactProjection -TrustSet $trust `
                -ProcessArtifact $Request.process_artifact -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
            if ([string]$requestedArtifact.sha256 -cne [string]$publication.artifact.sha256) {
                throw [IO.InvalidDataException]::new('M0I030 fixture replay Process artifact diverges from immutable physical bytes.')
            }
            Complete-M0PilotSummaryPublication -PublicationSet $publication -EvidenceModule $evidenceModule `
                -CrashAfterBoundary ([string]$Request.crash_after_boundary)
            return New-M0PilotPhysicalResult -PublicationSet $publication
        }

        $artifact = Get-M0PilotProcessArtifactProjection -TrustSet $trust -ProcessArtifact $Request.process_artifact `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
        $commonInputs = Get-M0PilotTrustedCommonInputsFromSet -TrustSet $trust
        $clocks = $Request.clocks
        $runStarted = New-M0PilotEvidenceEvent -CommonInputs $commonInputs -ExperimentId ([string]$Request.experiment_id) `
            -RunId ([string]$Request.run_id) -Sequence 1 -PreviousLineSha256 $null `
            -Utc ([string]$clocks.started_utc) -QpcTicks ([long]$clocks.started_qpc_ticks) `
            -QpcFrequency ([long]$clocks.qpc_frequency) -Event 'run_started' `
            -Payload ([pscustomobject][ordered]@{ phase = 'started' })
        $runStartedAnchor = New-M0SyntheticPilotRunStartedAnchor -Event $runStarted
        $intentInput = [pscustomobject][ordered]@{
            '$schema' = 'schemas/pilot-intent.schema.json'
            schema_id = 'm0.synthetic-pilot-intent.v1'
            schema_version = 1
            state = 'launch-authorized-once'
            candidate_code = 'P'
            scenario_id = 'synthetic-process-tree'
            attempt = 1
            evidence_role = 'synthetic_pilot'
            decision_eligible = $false
            experiment_id = [string]$Request.experiment_id
            run_id = [string]$Request.run_id
            common_inputs = $commonInputs
            evidence_writer_state = 'open-before-live'
            live_launch_limit = 1
            run_started = $runStartedAnchor
            expected_paths = Get-M0PilotExpectedPaths -RunId ([string]$Request.run_id)
        }
        $intent = New-M0SyntheticPilotIntent -Intent $intentInput
        $writer = & $evidenceModule {
            param($evidenceRoot, $runId, $schemaRoot)
            Open-M0EvidenceWriter -EvidenceRoot $evidenceRoot -RunId $runId -SchemaRoot $schemaRoot
        } $root ([string]$Request.run_id) $trust.evidence_schema_root
        $pilotIntentFile = Write-M0PilotCreateNewBytes -LiteralPath $paths.pilot_intent `
            -Bytes $script:M0PilotUtf8.GetBytes($intent.canonical_json) -Name 'Pilot launch intent'
        $createdFiles.Add($pilotIntentFile)
        if ([string]$Request.crash_after_boundary -ceq 'pilot_intent_published') {
            throw [IO.IOException]::new('M0I029 crash injection after pilot_intent_published.')
        }
        $firstAppend = & $evidenceModule {
            param($context, $event) Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $runStarted
        if ([string]$Request.crash_after_boundary -ceq 'raw_started') {
            throw [IO.IOException]::new('M0I029 crash injection after raw_started.')
        }
        New-Item -ItemType Directory -Path $paths.artifacts -ErrorAction Stop | Out-Null
        $artifactDirectory = Get-Item -LiteralPath $paths.artifacts -Force
        if (($artifactDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new('M0I024 Process artifact directory became a reparse point.')
        }
        $artifactFile = Write-M0PilotCreateNewBytes -LiteralPath $paths.process_tree -Bytes $artifact.bytes `
            -Name 'Process tree artifact'
        $createdFiles.Add($artifactFile)
        if ([string]$Request.crash_after_boundary -ceq 'artifact_published') {
            throw [IO.IOException]::new('M0I029 crash injection after artifact_published.')
        }
        $artifactEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs -ExperimentId ([string]$Request.experiment_id) `
            -RunId ([string]$Request.run_id) -Sequence 2 -PreviousLineSha256 ([string]$firstAppend.line_sha256) `
            -Utc ([string]$clocks.artifact_utc) -QpcTicks ([long]$clocks.artifact_qpc_ticks) `
            -QpcFrequency ([long]$clocks.qpc_frequency) -Event 'artifact_recorded' -Payload ([pscustomobject][ordered]@{
                artifact_id = 'process-tree'; relative_path = "artifacts/$($Request.run_id).process-tree.json"
                sha256 = [string]$artifact.sha256; size_bytes = [long]$artifact.size_bytes
                transient = $false; redacted = $true
            })
        $artifactAppend = & $evidenceModule {
            param($context, $event) Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $artifactEvent
        $checkEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs -ExperimentId ([string]$Request.experiment_id) `
            -RunId ([string]$Request.run_id) -Sequence 3 -PreviousLineSha256 ([string]$artifactAppend.line_sha256) `
            -Utc ([string]$clocks.check_utc) -QpcTicks ([long]$clocks.check_qpc_ticks) `
            -QpcFrequency ([long]$clocks.qpc_frequency) -Event 'check_recorded' -Payload $artifact.outcome.check_payload
        $checkAppend = & $evidenceModule {
            param($context, $event) Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $checkEvent
        $sealEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs -ExperimentId ([string]$Request.experiment_id) `
            -RunId ([string]$Request.run_id) -Sequence 4 -PreviousLineSha256 ([string]$checkAppend.line_sha256) `
            -Utc ([string]$clocks.sealed_utc) -QpcTicks ([long]$clocks.sealed_qpc_ticks) `
            -QpcFrequency ([long]$clocks.qpc_frequency) -Event 'run_sealed' -Payload ([pscustomobject][ordered]@{
                status = [string]$artifact.outcome.run_status
                status_reason = $artifact.outcome.run_status_reason
                error_code = $artifact.outcome.run_error_code
                checks_skipped = @($artifact.outcome.checks_omitted)
            })
        $runRecord = [pscustomobject][ordered]@{
            schema_id = 'm0.run.v1'; schema_version = 1
            experiment_id = [string]$Request.experiment_id; run_id = [string]$Request.run_id
            candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            common_inputs = $commonInputs; candidate_input = $null; schedule = $null
            status = [string]$artifact.outcome.run_status
            status_reason = $artifact.outcome.run_status_reason
            error_code = $artifact.outcome.run_error_code
            raw = $null
            markers = @('run-started')
            process_tree_sha256 = [string]$artifact.sha256
            metrics = @(); actions = @(); external_noise = @()
            artifacts = @([pscustomobject][ordered]@{
                artifact_id = 'process-tree'; relative_path = "artifacts/$($Request.run_id).process-tree.json"
                sha256 = [string]$artifact.sha256; size_bytes = [long]$artifact.size_bytes
            })
            checks_omitted = @($artifact.outcome.checks_omitted)
        }
        $evidenceCrash = if ([string]$Request.crash_after_boundary -cin @(
                'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published',
                'snapshot_temp_flushed', 'committed'
            )) { [string]$Request.crash_after_boundary } else { '' }
        $null = & $evidenceModule {
            param($context, $seal, $run, $boundary)
            Complete-M0EvidenceRun -Writer $context -SealEvent $seal -RunRecord $run `
                -EvidenceRole synthetic_pilot -CrashAfterBoundary $boundary
        } $writer $sealEvent $runRecord $evidenceCrash
        $null = & $evidenceModule { param($context) Close-M0EvidenceWriter -Writer $context } $writer
        $writer = $null
        $publication = Open-M0PilotPublicationSet -TrustSet $trust -EvidenceModule $evidenceModule `
            -RepositoryRoot ([string]$Request.repository_root) -EvidenceRoot $root `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
            -CreatorFiles ([pscustomobject]@{ pilot_intent = $pilotIntentFile; artifact = $artifactFile })
        Complete-M0PilotSummaryPublication -PublicationSet $publication -EvidenceModule $evidenceModule `
            -CrashAfterBoundary ([string]$Request.crash_after_boundary)
        return New-M0PilotPhysicalResult -PublicationSet $publication
    }
    finally {
        if ($null -ne $writer) {
            try {
                $evidenceModules = @(Get-Module -All -Name M0.Evidence)
                if ($evidenceModules.Count -eq 1) {
                    $null = & $evidenceModules[0] { param($context) Close-M0EvidenceWriter -Writer $context } $writer
                }
            }
            catch { }
        }
        Close-M0PilotPublicationSet -PublicationSet $publication
        for ($index = $createdFiles.Count - 1; $index -ge 0; $index--) {
            if ($null -ne $createdFiles[$index].stream) { $createdFiles[$index].stream.Dispose() }
        }
        Close-M0PilotTrustSet -TrustSet $trust
    }
}

function Assert-M0PilotArtifacts {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ProtocolRoot,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedProtocolSha256
    )
    $trust = $null
    try {
        $trust = Open-M0PilotContractTrustSet -ProtocolRoot $ProtocolRoot `
            -ExpectedProtocolSha256 $ExpectedProtocolSha256
        return [pscustomobject][ordered]@{
            protocol_sha256 = [string]$trust.protocol_sha256
            manifest_sha256 = [string]$trust.manifest_sha256
            scenario_sha256 = [string]$trust.scenario_sha256
        }
    }
    finally { Close-M0PilotTrustSet -TrustSet $trust }
}

function Assert-M0SyntheticPilotPublication {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $ExpectedProtocolSha256
    )
    $trust = $null
    $publication = $null
    try {
        $trust = Open-M0PilotContractTrustSet -ProtocolRoot $script:M0PilotProtocolRoot `
            -ExpectedProtocolSha256 $ExpectedProtocolSha256
        $evidenceModule = Get-M0PilotEvidenceModule -TrustSet $trust
        $publication = Open-M0PilotPublicationSet -TrustSet $trust -EvidenceModule $evidenceModule `
            -RepositoryRoot $RepositoryRoot -EvidenceRoot $EvidenceRoot -ExperimentId $ExperimentId -RunId $RunId `
            -RequireCommittedSummary
        return New-M0PilotPhysicalResult -PublicationSet $publication
    }
    finally {
        Close-M0PilotPublicationSet -PublicationSet $publication
        Close-M0PilotTrustSet -TrustSet $trust
    }
}

function Assert-M0PilotLivePreflight {
    param(
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId,
        [AllowEmptyString()] [string] $CrashAfterBoundary,
        [bool] $AbruptCrash
    )

    if ($ExperimentId -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
        throw [ArgumentException]::new('M0I041 ExperimentId is outside the ordinal closed Evidence identifier contract.')
    }
    if ($RunId -cnotmatch '^run-P-[A-Za-z0-9][A-Za-z0-9._-]{0,121}$') {
        throw [ArgumentException]::new('M0I041 RunId must be the ordinal synthetic Pilot run-P identifier.')
    }
    $boundaries = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($boundary in @(
        '', 'pilot_intent_published', 'raw_started', 'live_opened', 'artifact_published',
        'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published',
        'snapshot_temp_flushed', 'committed', 'summary_pending', 'summary_published'
    )) { $null = $boundaries.Add($boundary) }
    if (-not $boundaries.Contains($CrashAfterBoundary)) {
        throw [ArgumentException]::new('M0I041 CrashAfterBoundary is outside the ordinal closed Pilot boundary set.')
    }
    if ($AbruptCrash -and $CrashAfterBoundary -cnotin @('live_opened', 'artifact_published')) {
        throw [ArgumentException]::new('M0I041 AbruptCrash requires the closed live_opened or artifact_published boundary.')
    }
}

function Invoke-M0PilotLiveStage {
    param(
        [AllowNull()] $Hook,
        [Parameter(Mandatory)] [string] $Stage
    )
    if ($null -eq $Hook) { return }
    if ($null -eq $Hook.PSObject.Properties['events'] -or
        $Hook.events -isnot [Collections.Generic.List[string]]) {
        throw [IO.InvalidDataException]::new('M0I042 invalid private Pilot live test hook ledger.')
    }
    $Hook.events.Add($Stage)
}

function Invoke-M0PilotCrashBoundary {
    param(
        [AllowEmptyString()] [string] $RequestedBoundary,
        [Parameter(Mandatory)] [string] $ActualBoundary,
        [bool] $AbruptCrash
    )
    if ($RequestedBoundary -cne $ActualBoundary) { return }
    if ($AbruptCrash) {
        [Environment]::Exit(197)
    }
    throw [IO.IOException]::new("M0I029 crash injection after $ActualBoundary.")
}

function Get-M0PilotCanonicalUtcNow {
    return [DateTime]::UtcNow.ToString(
        'yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture
    )
}

function New-M0PilotLiveProcessArtifact {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $Request,
        [AllowNull()] $LiveHook
    )

    $pilot = $null
    $snapshot = $null
    $cleanup = $null
    $snapshotFailed = $false
    try {
        $pilot = & $TrustSet.process_module {
            param($repositoryRoot)
            Open-M0SyntheticProcessPilot -RepositoryRoot $repositoryRoot
        } ([string]$Request.repository_root)
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'process_opened'
        Invoke-M0PilotCrashBoundary -RequestedBoundary ([string]$Request.crash_after_boundary) `
            -ActualBoundary 'live_opened' -AbruptCrash ([bool]$Request.abrupt_crash)
        try {
            if ($null -ne $LiveHook -and $null -ne $LiveHook.PSObject.Properties['fail_snapshot'] -and
                [bool]$LiveHook.fail_snapshot) {
                throw [IO.InvalidDataException]::new('M0I042 injected pre-snapshot failure after live authority was opened.')
            }
            $snapshot = & $TrustSet.process_module {
                param($capability)
                Get-M0SyntheticProcessPilotSnapshot -Pilot $capability
            } $pilot
            Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'snapshot_captured'
        }
        catch {
            $snapshotFailed = $true
            Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'snapshot_failed'
        }
    }
    finally {
        if ($null -ne $pilot) {
            $cleanup = & $TrustSet.process_module {
                param($capability)
                Close-M0SyntheticProcessPilot -Pilot $capability
            } $pilot
            Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'process_closed'
        }
    }
    if ($null -eq $cleanup) {
        throw [IO.InvalidDataException]::new('M0I042 live Process authority did not return a canonical Close result.')
    }
    $artifact = if ($snapshotFailed) {
        & $TrustSet.process_module {
            param($closed, $experimentId, $runId)
            New-M0ProcessTreeArtifact -UnavailableErrorCode safe_identity_unavailable `
                -CleanupResult $closed -ExperimentId $experimentId -RunId $runId
        } $cleanup ([string]$Request.experiment_id) ([string]$Request.run_id)
    }
    else {
        & $TrustSet.process_module {
            param($tree, $closed, $experimentId, $runId)
            New-M0ProcessTreeArtifact -Snapshot $tree -CleanupResult $closed `
                -ExperimentId $experimentId -RunId $runId
        } $snapshot $cleanup ([string]$Request.experiment_id) ([string]$Request.run_id)
    }
    if ($snapshotFailed) {
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'fallback_artifact_built'
    }
    return Get-M0PilotProcessArtifactProjection -TrustSet $TrustSet -ProcessArtifact $artifact `
        -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
}

function Close-M0PilotRecoverySet {
    param([AllowNull()] $RecoverySet)
    if ($null -eq $RecoverySet) { return }
    Close-M0PilotPublicationSet -PublicationSet $RecoverySet
}

function Invoke-M0PilotRecoveryTestHook {
    if ($null -eq $script:M0PilotRecoveryTestHook) { return }
    $hook = $script:M0PilotRecoveryTestHook
    $script:M0PilotRecoveryTestHook = $null
    switch -CaseSensitive ([string]$hook.operation) {
        'overwrite' {
            $writer = $null
            try {
                $writer = [IO.FileStream]::new([string]$hook.target_path, [IO.FileMode]::Open,
                    [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
            }
            catch [IO.IOException] {
                throw [IO.InvalidDataException]::new(
                    'M0I042 recovery handoff overwrite was blocked by the retained intent handle before Resume.', $_.Exception)
            }
            finally { if ($null -ne $writer) { $writer.Dispose() } }
            throw [IO.InvalidDataException]::new('M0I042 recovery handoff unexpectedly allowed an overwrite handle.')
        }
        'rename' {
            try {
                [IO.File]::Move([string]$hook.target_path, [string]$hook.destination_path)
                [IO.File]::Move([string]$hook.destination_path, [string]$hook.target_path)
            }
            catch [IO.IOException] {
                throw [IO.InvalidDataException]::new(
                    'M0I042 recovery handoff rename was blocked by the retained Evidence intent before Resume.', $_.Exception)
            }
            throw [IO.InvalidDataException]::new('M0I042 recovery handoff unexpectedly allowed a retained-file rename.')
        }
        'hardlink' {
            $null = New-Item -ItemType HardLink -Path ([string]$hook.destination_path) `
                -Target ([string]$hook.target_path)
        }
        'extra-leaf' {
            [IO.File]::WriteAllText([string]$hook.destination_path, "{}`n", $script:M0PilotUtf8)
        }
        default { throw [IO.InvalidDataException]::new('M0I042 unknown recovery handoff test hook.') }
    }
}

function Open-M0PilotRecoverySet {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $EvidenceModule,
        [Parameter(Mandatory)] $Request
    )

    $root = Assert-M0PilotPhysicalScope -RepositoryRoot ([string]$Request.repository_root) `
        -EvidenceRoot ([string]$Request.evidence_root)
    $paths = Get-M0PilotPublicationPaths -EvidenceRoot $root -RunId ([string]$Request.run_id)
    $rootLock = $null
    $held = [Collections.Generic.List[object]]::new()
    try {
        if (-not (Test-Path -LiteralPath $paths.lock -PathType Leaf)) {
            throw [IO.InvalidDataException]::new('M0I030 Pilot recovery is missing the Evidence root sentinel.')
        }
        $rootLock = [IO.FileStream]::new($paths.lock, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None)
        [M0ProcessNativeV1.TrustedFile]::Validate($rootLock.SafeFileHandle, [IO.Path]::GetFullPath($paths.lock))
        if ($rootLock.Length -ne 0) {
            throw [IO.InvalidDataException]::new('M0I030 Pilot recovery sentinel differs from the exact empty lock leaf.')
        }

        $evidenceContext = & $EvidenceModule {
            param($evidenceRoot, $runId, $schemaRoot)
            $transactionPaths = Get-M0EvidenceTransactionPaths -EvidenceRoot $evidenceRoot -RunId $runId
            [pscustomobject][ordered]@{
                paths = $transactionPaths
                recovery = Get-M0EvidenceRecoveryStateNoLock -Paths $transactionPaths -RunId $runId `
                    -SchemaRoot $schemaRoot
            }
        } $root ([string]$Request.run_id) $TrustSet.evidence_schema_root
        $recovery = $evidenceContext.recovery
        if ([string]$recovery.state -cnotin @('intent_only', 'raw_sealed', 'run_published')) {
            throw [IO.InvalidDataException]::new(
                "M0I030 Evidence recovery state '$($recovery.state)' cannot advance under the pre-publication Pilot contract."
            )
        }

        $required = @($paths.pilot_intent, $paths.raw, $paths.evidence_intent, $paths.process_tree)
        foreach ($requiredPath in $required) {
            if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
                throw [IO.InvalidDataException]::new(
                    "M0I030 Pilot recovery precondition is missing required leaf '$([IO.Path]::GetFileName($requiredPath))'."
                )
            }
        }
        $expected = [Collections.Generic.List[string]]::new()
        foreach ($relative in @(
            '.m0-evidence.lock',
            "artifacts/$($Request.run_id).process-tree.json",
            "$($Request.run_id).intent.json",
            "$($Request.run_id).jsonl",
            "$($Request.run_id).pilot-intent.json"
        )) { $expected.Add($relative) }
        switch -CaseSensitive ([string]$recovery.state) {
            'intent_only' {
                if ([bool]$recovery.run_temp_present -or [bool]$recovery.snapshot_temp_present) {
                    throw [IO.InvalidDataException]::new('M0I030 intent_only recovery cannot contain Evidence temp publications.')
                }
            }
            'raw_sealed' {
                if ([bool]$recovery.snapshot_temp_present) {
                    throw [IO.InvalidDataException]::new('M0I030 raw_sealed recovery cannot contain a snapshot temp publication.')
                }
                if ([bool]$recovery.run_temp_present) { $expected.Add("$($Request.run_id).run.pending") }
            }
            'run_published' {
                if ([bool]$recovery.run_temp_present) {
                    throw [IO.InvalidDataException]::new('M0I030 run_published recovery cannot retain a run temp publication.')
                }
                $expected.Add("$($Request.run_id).run.json")
                if ([bool]$recovery.snapshot_temp_present) { $expected.Add("$($Request.run_id).snapshot.pending") }
            }
        }
        Assert-M0PilotPhysicalInventory -EvidenceRoot $root -Expected @($expected)

        $files = [ordered]@{}
        foreach ($definition in @(
            [pscustomobject]@{ key = 'pilot_intent'; path = $paths.pilot_intent; name = 'recovery Pilot intent' },
            [pscustomobject]@{ key = 'evidence_intent'; path = $paths.evidence_intent; name = 'recovery Evidence intent' },
            [pscustomobject]@{ key = 'artifact'; path = $paths.process_tree; name = 'recovery Process artifact' }
        )) {
            $file = Read-M0PilotTrustedFile -LiteralPath $definition.path -Name $definition.name -KeepOpen
            $held.Add($file)
            $files[$definition.key] = $file
        }
        $rawFile = Read-M0PilotTrustedFile -LiteralPath $paths.raw -Name 'recovery Pilot raw'
        $pilotIntent = $files.pilot_intent.text | ConvertFrom-Json -Depth 100 -DateKind String
        $intentBuilt = New-M0SyntheticPilotIntent -Intent $pilotIntent
        if (-not [Linq.Enumerable]::SequenceEqual[byte](
                $script:M0PilotUtf8.GetBytes($intentBuilt.canonical_json), [byte[]]$files.pilot_intent.bytes) -or
            [string]$pilotIntent.experiment_id -cne [string]$Request.experiment_id -or
            [string]$pilotIntent.run_id -cne [string]$Request.run_id) {
            throw [IO.InvalidDataException]::new('M0I030 recovery Pilot intent differs from its canonical requested identity.')
        }
        $commonInputs = Get-M0PilotTrustedCommonInputsFromSet -TrustSet $TrustSet
        foreach ($name in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
            if ([string]$pilotIntent.common_inputs.$name -cne [string]$commonInputs.$name) {
                throw [IO.InvalidDataException]::new("M0I030 recovery Pilot intent common input '$name' differs from retained trust.")
            }
        }

        $artifactDocument = $files.artifact.text | ConvertFrom-Json -Depth 100 -DateKind String
        $artifact = Get-M0PilotProcessArtifactProjection -TrustSet $TrustSet -ProcessArtifact $artifactDocument `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
        if (-not [Linq.Enumerable]::SequenceEqual[byte]([byte[]]$artifact.bytes, [byte[]]$files.artifact.bytes)) {
            throw [IO.InvalidDataException]::new('M0I030 recovery Process artifact is not its exact canonical byte projection.')
        }

        $rawLines = $rawFile.text.Split("`n", [StringSplitOptions]::None)
        $eventCount = if ([string]$recovery.state -ceq 'intent_only') { 3 } else { 4 }
        if ($rawLines.Count -ne ($eventCount + 1) -or $rawLines[$eventCount] -cne '') {
            throw [IO.InvalidDataException]::new('M0I030 recovery raw differs from its exact preseal or sealed P event count.')
        }
        $events = [Collections.Generic.List[object]]::new()
        for ($index = 0; $index -lt $eventCount; $index++) {
            if (-not (Test-Json -Json $rawLines[$index] -SchemaFile (Join-Path $TrustSet.protocol_root `
                        'schemas\event.schema.json') -ErrorAction SilentlyContinue)) {
                throw [IO.InvalidDataException]::new("M0I030 recovery raw event $($index + 1) failed the retained schema.")
            }
            $event = $rawLines[$index] | ConvertFrom-Json -Depth 100 -DateKind String
            if (($event | ConvertTo-Json -Compress -Depth 100) -cne $rawLines[$index]) {
                throw [IO.InvalidDataException]::new("M0I030 recovery raw event $($index + 1) is not canonical JSON.")
            }
            $events.Add($event)
        }
        $null = & $EvidenceModule {
            param([object[]]$rawEvents, [bool]$sealed)
            if ($sealed) { Assert-M0SyntheticPilotSequence -Events $rawEvents -RequireSeal }
            else { Assert-M0SyntheticPilotSequence -Events $rawEvents }
        } ([object[]]@($events)) ($eventCount -eq 4)
        $firstLineBytes = $script:M0PilotUtf8.GetBytes($rawLines[0] + "`n")
        if ($rawLines[0] -cne ($pilotIntent.run_started.event | ConvertTo-Json -Compress -Depth 100) -or
            (Get-M0PilotSha256Bytes -Bytes $script:M0PilotUtf8.GetBytes($rawLines[0])) -cne
                [string]$pilotIntent.run_started.canonical_line_sha256 -or
            [long]$firstLineBytes.Length -ne [long]$pilotIntent.run_started.size_bytes) {
            throw [IO.InvalidDataException]::new('M0I030 recovery raw first line differs from the retained Pilot intent.')
        }
        $expectedArtifactPayload = [pscustomobject][ordered]@{
            artifact_id = 'process-tree'; relative_path = "artifacts/$($Request.run_id).process-tree.json"
            sha256 = [string]$artifact.sha256; size_bytes = [long]$artifact.size_bytes
            transient = $false; redacted = $true
        }
        if (($events[1].payload | ConvertTo-Json -Compress -Depth 20) -cne
                ($expectedArtifactPayload | ConvertTo-Json -Compress -Depth 20) -or
            ($events[2].payload | ConvertTo-Json -Compress -Depth 20) -cne
                ($artifact.outcome.check_payload | ConvertTo-Json -Compress -Depth 20)) {
            throw [IO.InvalidDataException]::new('M0I030 recovery raw artifact/check events differ from the retained artifact.')
        }
        $confirmedRecovery = & $EvidenceModule {
            param($transactionPaths, $runId, $schemaRoot)
            Get-M0EvidenceRecoveryStateNoLock -Paths $transactionPaths -RunId $runId -SchemaRoot $schemaRoot
        } $evidenceContext.paths ([string]$Request.run_id) $TrustSet.evidence_schema_root
        if ([string]$confirmedRecovery.state -cne [string]$recovery.state -or
            [bool]$confirmedRecovery.run_temp_present -ne [bool]$recovery.run_temp_present -or
            [bool]$confirmedRecovery.snapshot_temp_present -ne [bool]$recovery.snapshot_temp_present) {
            throw [IO.InvalidDataException]::new(
                'M0I030 recovery state changed before the retained Evidence intent established the Resume handoff.'
            )
        }
        Invoke-M0PilotRecoveryTestHook
        foreach ($file in $held) {
            Assert-M0PilotHeldFileStable -File $file -Name ([IO.Path]::GetFileName([string]$file.path))
        }
        [M0ProcessNativeV1.TrustedFile]::Validate($rootLock.SafeFileHandle, [IO.Path]::GetFullPath($paths.lock))
        Assert-M0PilotPhysicalInventory -EvidenceRoot $root -Expected @($expected)
        return [pscustomobject][ordered]@{
            root = $root
            paths = $paths
            evidence_paths = $evidenceContext.paths
            recovery = $confirmedRecovery
            root_lock = $rootLock
            held_files = $held
        }
    }
    catch {
        Close-M0PilotRecoverySet -RecoverySet ([pscustomobject]@{ root_lock = $rootLock; held_files = $held })
        throw
    }
}

function Resume-M0PilotLivePublication {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $EvidenceModule,
        [Parameter(Mandatory)] $Request,
        [AllowNull()] $LiveHook
    )

    $root = Assert-M0PilotPhysicalScope -RepositoryRoot ([string]$Request.repository_root) `
        -EvidenceRoot ([string]$Request.evidence_root)
    $paths = Get-M0PilotPublicationPaths -EvidenceRoot $root -RunId ([string]$Request.run_id)
    if (-not (Test-Path -LiteralPath $paths.evidence_intent -PathType Leaf)) {
        throw [IO.InvalidDataException]::new(
            'M0I030 pre-Evidence-intent Pilot state is incomplete; require a new EvidenceRoot and RunId without relaunch or overwrite.'
        )
    }
    $recovery = & $EvidenceModule {
        param($evidenceRoot, $runId, $schemaRoot)
        Get-M0EvidenceRecoveryState -EvidenceRoot $evidenceRoot -RunId $runId -SchemaRoot $schemaRoot
    } $root ([string]$Request.run_id) $TrustSet.evidence_schema_root
    if ([string]$recovery.state -cne 'committed') {
        $recoverySet = $null
        try {
            $recoverySet = Open-M0PilotRecoverySet -TrustSet $TrustSet -EvidenceModule $EvidenceModule `
                -Request $Request
            $null = & $EvidenceModule {
                param($transactionPaths, $runId, $schemaRoot)
                Invoke-M0EvidenceResumeNoLock -Paths $transactionPaths -RunId $runId -SchemaRoot $schemaRoot
            } $recoverySet.evidence_paths ([string]$Request.run_id) $TrustSet.evidence_schema_root
            foreach ($file in $recoverySet.held_files) {
                Assert-M0PilotHeldFileStable -File $file -Name ([IO.Path]::GetFileName([string]$file.path))
            }
            [M0ProcessNativeV1.TrustedFile]::Validate(
                $recoverySet.root_lock.SafeFileHandle, [IO.Path]::GetFullPath($recoverySet.paths.lock))
            Assert-M0PilotPhysicalInventory -EvidenceRoot $root -Expected @(
                '.m0-evidence.lock',
                "artifacts/$($Request.run_id).process-tree.json",
                "$($Request.run_id).intent.json",
                "$($Request.run_id).jsonl",
                "$($Request.run_id).pilot-intent.json",
                "$($Request.run_id).run.json",
                "$($Request.run_id).snapshot.json"
            )
        }
        finally { Close-M0PilotRecoverySet -RecoverySet $recoverySet }
    }
    Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'evidence_recovered'
    $publication = $null
    try {
        $publication = Open-M0PilotPublicationSet -TrustSet $TrustSet -EvidenceModule $EvidenceModule `
            -RepositoryRoot ([string]$Request.repository_root) -EvidenceRoot $root `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id)
        Complete-M0PilotSummaryPublication -PublicationSet $publication -EvidenceModule $EvidenceModule `
            -CrashAfterBoundary ([string]$Request.crash_after_boundary) -LiveHook $LiveHook
        return New-M0PilotPhysicalResult -PublicationSet $publication
    }
    finally { Close-M0PilotPublicationSet -PublicationSet $publication }
}

function Publish-M0PilotFreshSession {
    param(
        [Parameter(Mandatory)] $TrustSet,
        [Parameter(Mandatory)] $EvidenceModule,
        [Parameter(Mandatory)] $Request,
        [Parameter(Mandatory)] [scriptblock] $ArtifactFactory,
        [AllowNull()] $LiveHook
    )

    $root = Assert-M0PilotPhysicalScope -RepositoryRoot ([string]$Request.repository_root) `
        -EvidenceRoot ([string]$Request.evidence_root)
    if (@(Get-ChildItem -LiteralPath $root -Force).Count -ne 0) {
        throw [IO.InvalidDataException]::new('M0I030 fresh live Pilot publication requires an exact empty EvidenceRoot.')
    }
    $paths = Get-M0PilotPublicationPaths -EvidenceRoot $root -RunId ([string]$Request.run_id)
    $commonInputs = Get-M0PilotTrustedCommonInputsFromSet -TrustSet $TrustSet
    $frequency = [long][Diagnostics.Stopwatch]::Frequency
    $startedUtc = Get-M0PilotCanonicalUtcNow
    $startedQpc = [long][Diagnostics.Stopwatch]::GetTimestamp()
    $runStarted = New-M0PilotEvidenceEvent -CommonInputs $commonInputs `
        -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
        -Sequence 1 -PreviousLineSha256 $null -Utc $startedUtc -QpcTicks $startedQpc `
        -QpcFrequency $frequency -Event 'run_started' -Payload ([pscustomobject][ordered]@{ phase = 'started' })
    $runStartedAnchor = New-M0SyntheticPilotRunStartedAnchor -Event $runStarted
    $intent = New-M0SyntheticPilotIntent -Intent ([pscustomobject][ordered]@{
        '$schema' = 'schemas/pilot-intent.schema.json'
        schema_id = 'm0.synthetic-pilot-intent.v1'; schema_version = 1
        state = 'launch-authorized-once'; candidate_code = 'P'; scenario_id = 'synthetic-process-tree'
        attempt = 1; evidence_role = 'synthetic_pilot'; decision_eligible = $false
        experiment_id = [string]$Request.experiment_id; run_id = [string]$Request.run_id
        common_inputs = $commonInputs; evidence_writer_state = 'open-before-live'; live_launch_limit = 1
        run_started = $runStartedAnchor; expected_paths = Get-M0PilotExpectedPaths -RunId ([string]$Request.run_id)
    })

    $writer = $null
    $publication = $null
    $createdFiles = [Collections.Generic.List[object]]::new()
    try {
        $writer = & $EvidenceModule {
            param($evidenceRoot, $runId, $schemaRoot)
            Open-M0EvidenceWriter -EvidenceRoot $evidenceRoot -RunId $runId -SchemaRoot $schemaRoot
        } $root ([string]$Request.run_id) $TrustSet.evidence_schema_root
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'writer_opened'
        $pilotIntentFile = Write-M0PilotCreateNewBytes -LiteralPath $paths.pilot_intent `
            -Bytes $script:M0PilotUtf8.GetBytes($intent.canonical_json) -Name 'Pilot launch intent'
        $createdFiles.Add($pilotIntentFile)
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'pilot_intent_published'
        Invoke-M0PilotCrashBoundary -RequestedBoundary ([string]$Request.crash_after_boundary) `
            -ActualBoundary 'pilot_intent_published' -AbruptCrash ([bool]$Request.abrupt_crash)
        $firstAppend = & $EvidenceModule {
            param($context, $event)
            Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $runStarted
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'raw_started_flushed'
        Invoke-M0PilotCrashBoundary -RequestedBoundary ([string]$Request.crash_after_boundary) `
            -ActualBoundary 'raw_started' -AbruptCrash ([bool]$Request.abrupt_crash)

        $artifact = & $ArtifactFactory $TrustSet $Request $LiveHook
        $artifactUtc = Get-M0PilotCanonicalUtcNow
        $artifactQpc = [long][Diagnostics.Stopwatch]::GetTimestamp()
        New-Item -ItemType Directory -Path $paths.artifacts -ErrorAction Stop | Out-Null
        $artifactDirectory = Get-Item -LiteralPath $paths.artifacts -Force
        if (($artifactDirectory.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new('M0I024 Process artifact directory became a reparse point.')
        }
        $artifactFile = Write-M0PilotCreateNewBytes -LiteralPath $paths.process_tree `
            -Bytes $artifact.bytes -Name 'Process tree artifact'
        $createdFiles.Add($artifactFile)
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'artifact_published'
        Invoke-M0PilotCrashBoundary -RequestedBoundary ([string]$Request.crash_after_boundary) `
            -ActualBoundary 'artifact_published' -AbruptCrash ([bool]$Request.abrupt_crash)

        $artifactEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
            -Sequence 2 -PreviousLineSha256 ([string]$firstAppend.line_sha256) `
            -Utc $artifactUtc -QpcTicks $artifactQpc -QpcFrequency $frequency `
            -Event 'artifact_recorded' -Payload ([pscustomobject][ordered]@{
                artifact_id = 'process-tree'; relative_path = "artifacts/$($Request.run_id).process-tree.json"
                sha256 = [string]$artifact.sha256; size_bytes = [long]$artifact.size_bytes
                transient = $false; redacted = $true
            })
        $artifactAppend = & $EvidenceModule {
            param($context, $event)
            Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $artifactEvent
        $checkUtc = Get-M0PilotCanonicalUtcNow
        $checkQpc = [long][Diagnostics.Stopwatch]::GetTimestamp()
        $checkEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
            -Sequence 3 -PreviousLineSha256 ([string]$artifactAppend.line_sha256) `
            -Utc $checkUtc -QpcTicks $checkQpc -QpcFrequency $frequency `
            -Event 'check_recorded' -Payload $artifact.outcome.check_payload
        $checkAppend = & $EvidenceModule {
            param($context, $event)
            Add-M0EvidenceEvent -Writer $context -Event $event
        } $writer $checkEvent
        $sealedUtc = Get-M0PilotCanonicalUtcNow
        $sealedQpc = [long][Diagnostics.Stopwatch]::GetTimestamp()
        $sealEvent = New-M0PilotEvidenceEvent -CommonInputs $commonInputs `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
            -Sequence 4 -PreviousLineSha256 ([string]$checkAppend.line_sha256) `
            -Utc $sealedUtc -QpcTicks $sealedQpc -QpcFrequency $frequency `
            -Event 'run_sealed' -Payload ([pscustomobject][ordered]@{
                status = [string]$artifact.outcome.run_status
                status_reason = $artifact.outcome.run_status_reason
                error_code = $artifact.outcome.run_error_code
                checks_skipped = @($artifact.outcome.checks_omitted)
            })
        $runRecord = [pscustomobject][ordered]@{
            schema_id = 'm0.run.v1'; schema_version = 1
            experiment_id = [string]$Request.experiment_id; run_id = [string]$Request.run_id
            candidate_code = 'P'; scenario_id = 'synthetic-process-tree'; attempt = 1
            common_inputs = $commonInputs; candidate_input = $null; schedule = $null
            status = [string]$artifact.outcome.run_status
            status_reason = $artifact.outcome.run_status_reason
            error_code = $artifact.outcome.run_error_code
            raw = $null; markers = @('run-started'); process_tree_sha256 = [string]$artifact.sha256
            metrics = @(); actions = @(); external_noise = @()
            artifacts = @([pscustomobject][ordered]@{
                artifact_id = 'process-tree'; relative_path = "artifacts/$($Request.run_id).process-tree.json"
                sha256 = [string]$artifact.sha256; size_bytes = [long]$artifact.size_bytes
            })
            checks_omitted = @($artifact.outcome.checks_omitted)
        }
        $evidenceCrash = if ([string]$Request.crash_after_boundary -cin @(
                'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published',
                'snapshot_temp_flushed', 'committed'
            )) { [string]$Request.crash_after_boundary } else { '' }
        $null = & $EvidenceModule {
            param($context, $seal, $run, $boundary)
            Complete-M0EvidenceRun -Writer $context -SealEvent $seal -RunRecord $run `
                -EvidenceRole synthetic_pilot -CrashAfterBoundary $boundary
        } $writer $sealEvent $runRecord $evidenceCrash
        Invoke-M0PilotLiveStage -Hook $LiveHook -Stage 'evidence_committed'
        $null = & $EvidenceModule { param($context) Close-M0EvidenceWriter -Writer $context } $writer
        $writer = $null
        $publication = Open-M0PilotPublicationSet -TrustSet $TrustSet -EvidenceModule $EvidenceModule `
            -RepositoryRoot ([string]$Request.repository_root) -EvidenceRoot $root `
            -ExperimentId ([string]$Request.experiment_id) -RunId ([string]$Request.run_id) `
            -CreatorFiles ([pscustomobject]@{ pilot_intent = $pilotIntentFile; artifact = $artifactFile })
        Complete-M0PilotSummaryPublication -PublicationSet $publication -EvidenceModule $EvidenceModule `
            -CrashAfterBoundary ([string]$Request.crash_after_boundary) -LiveHook $LiveHook
        return New-M0PilotPhysicalResult -PublicationSet $publication -LiveLaunchPerformed $true
    }
    finally {
        if ($null -ne $writer) {
            try { $null = & $EvidenceModule { param($context) Close-M0EvidenceWriter -Writer $context } $writer }
            catch { }
        }
        Close-M0PilotPublicationSet -PublicationSet $publication
        for ($index = $createdFiles.Count - 1; $index -ge 0; $index--) {
            if ($null -ne $createdFiles[$index].stream) { $createdFiles[$index].stream.Dispose() }
        }
    }
}

function Invoke-M0SyntheticPilot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedProtocolSha256,
        [AllowEmptyString()] [string] $CrashAfterBoundary = '',
        [switch] $AbruptCrash
    )
    $trust = $null
    $liveHook = $script:M0PilotLiveTestHook
    $script:M0PilotLiveTestHook = $null
    try {
        $trust = Open-M0PilotContractTrustSet -ProtocolRoot $script:M0PilotProtocolRoot `
            -ExpectedProtocolSha256 $ExpectedProtocolSha256
        Invoke-M0PilotLiveStage -Hook $liveHook -Stage 'trust_opened'
        $actualRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
        $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
        if (-not $actualRepositoryRoot.Equals($script:M0PilotRepositoryRoot, $comparison)) {
            throw [IO.InvalidDataException]::new(
                'M0I023 RepositoryRoot differs from the exact trusted repository bound to M0.Pilot.'
            )
        }
        Assert-M0PilotLivePreflight -ExperimentId $ExperimentId -RunId $RunId `
            -CrashAfterBoundary $CrashAfterBoundary -AbruptCrash ([bool]$AbruptCrash)
        Invoke-M0PilotLiveStage -Hook $liveHook -Stage 'preflight_validated'
        if (-not $IsWindows) {
            throw [PlatformNotSupportedException]::new(
                'M0I040 live synthetic Pilot publication is not proven outside Windows and fails closed before mutation.'
            )
        }

        $root = Assert-M0PilotPhysicalScope -RepositoryRoot $RepositoryRoot -EvidenceRoot $EvidenceRoot
        $evidenceModule = Get-M0PilotEvidenceModule -TrustSet $trust
        $request = [pscustomobject][ordered]@{
            repository_root = $actualRepositoryRoot
            evidence_root = $root
            experiment_id = $ExperimentId
            run_id = $RunId
            crash_after_boundary = $CrashAfterBoundary
            abrupt_crash = [bool]$AbruptCrash
        }
        if (@(Get-ChildItem -LiteralPath $root -Force).Count -ne 0) {
            return Resume-M0PilotLivePublication -TrustSet $trust -EvidenceModule $evidenceModule `
                -Request $request -LiveHook $liveHook
        }

        $artifactFactory = {
            param($set, $liveRequest, $hook)
            New-M0PilotLiveProcessArtifact -TrustSet $set -Request $liveRequest -LiveHook $hook
        }
        return Publish-M0PilotFreshSession -TrustSet $trust -EvidenceModule $evidenceModule `
            -Request $request -ArtifactFactory $artifactFactory -LiveHook $liveHook
    }
    finally { Close-M0PilotTrustSet -TrustSet $trust }
}

Export-ModuleMember -Function @(
    'Assert-M0PilotArtifacts',
    'Assert-M0SyntheticPilotPublication',
    'Assert-M0SyntheticPilotSummary',
    'Invoke-M0SyntheticPilot',
    'New-M0SyntheticPilotSummary'
)
