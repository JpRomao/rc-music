Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0EvidenceUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0EvidenceInvariantFields = @(
    'protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256',
    'scenario_sha256', 'experiment_id', 'run_id', 'candidate_code', 'scenario_id',
    'attempt', 'qpc_frequency'
)
$script:M0EvidenceStatisticsModulePath = Join-Path $PSScriptRoot 'M0.Statistics.psm1'
Import-Module $script:M0EvidenceStatisticsModulePath -Force -ErrorAction Stop

function Get-M0EvidenceSha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    $hash = [Security.Cryptography.SHA256]::HashData($Bytes)
    return [Convert]::ToHexString($hash).ToLowerInvariant()
}

function Get-M0EvidenceSha256Text {
    param([Parameter(Mandatory)] [string] $Value)

    return Get-M0EvidenceSha256Bytes -Bytes $script:M0EvidenceUtf8.GetBytes($Value)
}

function ConvertTo-M0EvidenceJsonLine {
    param([Parameter(Mandatory)] $Value)

    $json = ConvertTo-Json -InputObject $Value -Compress -Depth 100
    if ($json.Contains("`r") -or $json.Contains("`n")) {
        throw [IO.InvalidDataException]::new('M0E002 evidence JSON must serialize to one line.')
    }
    return $json
}

function Assert-M0EvidenceSafePath {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [switch] $MayNotExist
    )

    $full = [IO.Path]::GetFullPath($LiteralPath)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full.Substring($root.Length).Contains(':')) {
        throw [ArgumentException]::new("M0E005 alternate data streams are forbidden: '$LiteralPath'.")
    }
    $parent = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0E005 evidence parent is missing: '$parent'.")
    }
    $pathsToInspect = [Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath $full) {
        $pathsToInspect.Add($full)
    }
    $ancestor = $parent
    while (-not [string]::IsNullOrWhiteSpace($ancestor)) {
        $pathsToInspect.Add($ancestor)
        $ancestorInfo = [IO.Directory]::GetParent($ancestor)
        $ancestor = if ($null -eq $ancestorInfo) { $null } else { $ancestorInfo.FullName }
    }
    foreach ($path in $pathsToInspect) {
        if (Test-Path -LiteralPath $path) {
            $item = Get-Item -LiteralPath $path -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw [IO.InvalidDataException]::new("M0E005 links are forbidden in evidence paths: '$path'.")
            }
        }
    }
    if (-not $MayNotExist -and -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0E005 evidence file is missing: '$full'.")
    }
    return $full
}

function Test-M0EvidenceSensitiveString {
    param([Parameter(Mandatory)] [AllowEmptyString()] [string] $Value)

    if ($Value -match '(?i)\b[A-Za-z][A-Za-z0-9+.-]*://[^\s''"<>]*' -or
        $Value -match '(?i)\b(?:file|mailto):[^\s''"<>]+' -or
        $Value -match '(?i)\b(?:serial|endpoint)\s*[:=]\s*[^\s''"<>]+' -or
        $Value -match '(?i)(?<![A-Z0-9])SN-[A-Z0-9][A-Z0-9._-]{2,}(?![A-Z0-9])' -or
        $Value -match '(?i)\bSWD\\MMDEVAPI\\\{[^}\r\n]+\}' -or
        $Value -match '(?i)\bdevice\s+[^\r\n]{1,240}' -or
        $Value -match '(?i)(?<![A-Z0-9._%+-])[A-Z0-9._%+-]+@[A-Z0-9-]+(?:\.[A-Z0-9-]+)*(?![A-Z0-9.-])' -or
        $Value -match '(?<!\d)(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?!\d)' -or
        $Value -match '(?i)(?<![A-F0-9:])(?:[A-F0-9]{0,4}(?::[A-F0-9]{0,4})*::[A-F0-9]{0,4}(?::[A-F0-9]{0,4})*|(?:[A-F0-9]{1,4}:){7}[A-F0-9]{1,4})(?![A-F0-9:])' -or
        $Value -match '(?i)(?<![A-F0-9])(?:[A-F0-9]{2}[:-]){5}[A-F0-9]{2}(?![A-F0-9])' -or
        $Value -match '(?i)(?<![A-Za-z0-9])(?:[A-Z]:[\\/]|\\\\[^\\/\s]+[\\/])' -or
        $Value -match '(?i)(?<![A-Za-z0-9._-])/(?!/)(?:[^\s''"<>]+|$)') {
        return $true
    }

    $identities = @(
        [Environment]::MachineName,
        [Environment]::UserName,
        $env:COMPUTERNAME,
        $env:USERNAME,
        $env:USER
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and $_.Length -ge 3 } | Select-Object -Unique
    foreach ($identity in $identities) {
        $identityPattern = '(?i)(?<![A-Za-z0-9]){0}(?![A-Za-z0-9])' -f [regex]::Escape($identity)
        if ($Value -match $identityPattern) {
            return $true
        }
    }
    foreach ($homePath in @($env:USERPROFILE, $env:HOME)) {
        if (-not [string]::IsNullOrWhiteSpace($homePath) -and
            $Value.IndexOf($homePath, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            return $true
        }
    }
    return $false
}

function Assert-M0EvidenceRedactedValue {
    param(
        [AllowNull()] $Value,
        [string] $Location = '$'
    )

    if ($null -eq $Value -or $Value -is [bool] -or $Value -is [ValueType]) {
        return
    }
    if ($Value -is [string]) {
        if (Test-M0EvidenceSensitiveString -Value $Value) {
            throw [IO.InvalidDataException]::new("M0E006 sensitive identity or absolute path at '$Location'.")
        }
        return
    }
    if ($Value -is [Collections.IDictionary]) {
        foreach ($key in $Value.Keys) {
            if ([string]$key -match '^(?i:hostname|computer_name|machine_name|username|user_name|serial|endpoint|absolute_path)$') {
                throw [IO.InvalidDataException]::new("M0E006 forbidden evidence property '$key'.")
            }
            Assert-M0EvidenceRedactedValue -Value $Value[$key] -Location "$Location.$key"
        }
        return
    }
    if ($Value -is [Collections.IEnumerable]) {
        $index = 0
        foreach ($item in $Value) {
            Assert-M0EvidenceRedactedValue -Value $item -Location "$Location[$index]"
            $index++
        }
        return
    }
    foreach ($property in $Value.PSObject.Properties) {
        if ($property.Name -match '^(?i:hostname|computer_name|machine_name|username|user_name|serial|endpoint|absolute_path)$') {
            throw [IO.InvalidDataException]::new("M0E006 forbidden evidence property '$($property.Name)'.")
        }
        Assert-M0EvidenceRedactedValue -Value $property.Value -Location "$Location.$($property.Name)"
    }
}

function ConvertTo-M0EvidenceRedactedText {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $Value)

    $result = $Value
    $result = [regex]::Replace($result, '(?i)\b[A-Za-z][A-Za-z0-9+.-]*://[^\s''"<>]+', '[redacted-url]')
    $result = [regex]::Replace($result, '(?i)\bfile:(?:(?:[A-Z]:[\\/])|/)[^\s''"<>]+', '[redacted-path]')
    $result = [regex]::Replace($result, '(?i)\bmailto:[^\s''"<>]+', '[redacted-email]')
    $result = [regex]::Replace($result, '(?i)\b(?:serial|endpoint)\s*[:=]\s*[^\r\n]{1,240}', '[redacted-identifier]')
    $result = [regex]::Replace($result, '(?i)(?<![A-Z0-9])SN-[A-Z0-9][A-Z0-9._-]{2,}(?![A-Z0-9])', '[redacted-serial]')
    $result = [regex]::Replace($result, '(?i)\bSWD\\MMDEVAPI\\\{[^}\r\n]+\}', '[redacted-endpoint]')
    $result = [regex]::Replace($result, '(?i)\bdevice\s+[^\r\n]{1,240}', '[redacted-device]')
    $result = [regex]::Replace($result, '(?i)(?<![A-Z0-9._%+-])[A-Z0-9._%+-]+@[A-Z0-9-]+(?:\.[A-Z0-9-]+)*(?![A-Z0-9.-])', '[redacted-email]')
    $result = [regex]::Replace($result, '(?<!\d)(?:\d{1,3}\.){3}\d{1,3}(?::\d{1,5})?(?!\d)', '[redacted-endpoint]')
    $result = [regex]::Replace($result, '(?i)(?<![A-F0-9:])(?:[A-F0-9]{0,4}(?::[A-F0-9]{0,4})*::[A-F0-9]{0,4}(?::[A-F0-9]{0,4})*|(?:[A-F0-9]{1,4}:){7}[A-F0-9]{1,4})(?![A-F0-9:])', '[redacted-endpoint]')
    $result = [regex]::Replace($result, '(?i)(?<![A-F0-9])(?:[A-F0-9]{2}[:-]){5}[A-F0-9]{2}(?![A-F0-9])', '[redacted-endpoint]')
    $result = [regex]::Replace($result, '(?i)(?<![A-Za-z0-9])(?:[A-Z]:[\\/]|\\\\)[^\s''"<>]+', '[redacted-path]')
    $result = [regex]::Replace($result, '(?i)(?<![A-Za-z0-9._-])/(?!/)(?:[^\s''"<>]+|$)', '[redacted-path]')
    $identities = @(
        [Environment]::MachineName,
        [Environment]::UserName,
        $env:COMPUTERNAME,
        $env:USERNAME,
        $env:USER
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and $_.Length -ge 3 } | Select-Object -Unique
    foreach ($identity in $identities) {
        $identityPattern = '(?i)(?<![A-Za-z0-9]){0}(?![A-Za-z0-9])' -f [regex]::Escape($identity)
        $result = [regex]::Replace($result, $identityPattern, '[redacted-identity]')
    }
    return $result
}

function Assert-M0EvidenceJsonSchema {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Code
    )

    $schema = Assert-M0EvidenceSafePath -LiteralPath $SchemaPath
    $json = ConvertTo-M0EvidenceJsonLine -Value $Value
    if (-not (Test-Json -Json $json -SchemaFile $schema -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("$Code evidence schema rejected the document.")
    }
    return $json
}

function Read-M0EvidenceJsonDocument {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Code
    )

    $path = Assert-M0EvidenceSafePath -LiteralPath $LiteralPath
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new("$Code evidence JSON is empty or has a BOM.")
    }
    $text = $script:M0EvidenceUtf8.GetString($bytes)
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.Substring(0, $text.Length - 1).Contains("`n")) {
        throw [IO.InvalidDataException]::new("$Code evidence JSON must be one UTF-8/LF document with final newline.")
    }
    $json = $text.TrimEnd("`n")
    if (-not (Test-Json -Json $json -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("$Code evidence JSON failed schema validation.")
    }
    $value = $json | ConvertFrom-Json -Depth 100
    Assert-M0EvidenceRedactedValue -Value $value
    if ((ConvertTo-M0EvidenceJsonLine -Value $value) -cne $json) {
        throw [IO.InvalidDataException]::new("$Code evidence JSON is not canonical compact JSON.")
    }
    return $value
}

function Read-M0EvidenceRaw {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $EventSchemaPath,
        [AllowEmptyCollection()] [byte[]] $Bytes,
        [switch] $RequireSeal
    )

    if ($PSBoundParameters.ContainsKey('Bytes')) {
        $bytes = $Bytes
    }
    else {
        $path = Assert-M0EvidenceSafePath -LiteralPath $LiteralPath
        $bytes = [IO.File]::ReadAllBytes($path)
    }
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new('M0E002 raw evidence is empty or has a BOM.')
    }
    $text = $script:M0EvidenceUtf8.GetString($bytes)
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($text.Length -ge 2 -and $text[$text.Length - 2] -eq "`n")) {
        throw [IO.InvalidDataException]::new('M0E002 raw evidence must be UTF-8 JSONL with LF and final newline.')
    }
    $lines = @($text.TrimEnd("`n").Split("`n"))
    if ($lines.Count -eq 0 -or $lines -contains '') {
        throw [IO.InvalidDataException]::new('M0E002 raw evidence contains an empty line.')
    }

    $events = [Collections.Generic.List[object]]::new()
    $previousLine = $null
    $first = $null
    $previousEvent = $null
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($script:M0EvidenceUtf8.GetByteCount($line) -gt 1048576 -or
            -not (Test-Json -Json $line -SchemaFile $EventSchemaPath -ErrorAction SilentlyContinue)) {
            throw [IO.InvalidDataException]::new("M0E002 event schema rejected line $($index + 1).")
        }
        $event = $line | ConvertFrom-Json -Depth 100
        Assert-M0EvidenceRedactedValue -Value $event -Location "line[$($index + 1)]"
        if ((ConvertTo-M0EvidenceJsonLine -Value $event) -cne $line) {
            throw [IO.InvalidDataException]::new("M0E002 event line $($index + 1) is not canonical compact JSON.")
        }
        $expectedSequence = $index + 1
        $expectedPreviousHash = if ($null -eq $previousLine) { $null } else { Get-M0EvidenceSha256Text -Value $previousLine }
        if ([int]$event.event_seq -ne $expectedSequence -or
            (($null -eq $expectedPreviousHash -and $null -ne $event.previous_line_sha256) -or
             ($null -ne $expectedPreviousHash -and $event.previous_line_sha256 -cne $expectedPreviousHash))) {
            throw [IO.InvalidDataException]::new("M0E003 event sequence/hash chain differs at line $expectedSequence.")
        }
        if ($index -eq 0) {
            if ($event.event -cne 'run_started') {
                throw [IO.InvalidDataException]::new('M0E003 first evidence event must be run_started.')
            }
            $first = $event
        }
        else {
            foreach ($field in $script:M0EvidenceInvariantFields) {
                if ([string]$event.$field -cne [string]$first.$field) {
                    throw [IO.InvalidDataException]::new("M0E003 run invariant '$field' changed at line $expectedSequence.")
                }
            }
            if ([long]$event.qpc_ticks -lt [long]$previousEvent.qpc_ticks -or
                [DateTimeOffset]::Parse($event.utc) -lt [DateTimeOffset]::Parse($previousEvent.utc)) {
                throw [IO.InvalidDataException]::new("M0E003 event clocks regressed at line $expectedSequence.")
            }
        }
        if ($previousEvent -and $previousEvent.event -ceq 'run_sealed') {
            throw [IO.InvalidDataException]::new('M0E004 raw evidence contains data after its seal.')
        }
        $events.Add($event)
        $previousLine = $line
        $previousEvent = $event
    }
    if ($RequireSeal -and $previousEvent.event -cne 'run_sealed') {
        throw [IO.InvalidDataException]::new('M0E004 raw evidence is not sealed.')
    }
    return [pscustomobject]@{
        events = @($events)
        event_count = $events.Count
        first_event = $first
        last_event = $previousEvent
        final_line_sha256 = Get-M0EvidenceSha256Text -Value $previousLine
    }
}

function Assert-M0EvidenceDerivedStatus {
    param(
        [Parameter(Mandatory)] [object[]] $Events,
        [Parameter(Mandatory)] [string] $Status,
        [AllowNull()] $StatusReason,
        [AllowNull()] $ErrorCode
    )

    $failurePolicies = @(
        [pscustomobject]@{ outcome = 'crash'; reason = 'candidate_crash' },
        [pscustomobject]@{ outcome = 'hang'; reason = 'candidate_hang' },
        [pscustomobject]@{ outcome = 'timeout'; reason = 'scenario_timeout' },
        [pscustomobject]@{ outcome = 'control_unavailable'; reason = 'window_not_actionable' },
        [pscustomobject]@{ outcome = 'budget_exceeded'; reason = 'budget_violated' }
    )
    $failure = @()
    $expectedReason = $null
    foreach ($policy in $failurePolicies) {
        $failure = @($Events | Where-Object {
            $_.event -ceq 'action_observation' -and $_.payload.outcome -ceq $policy.outcome
        } | Sort-Object { [int]$_.event_seq } | Select-Object -First 1)
        if ($failure.Count -eq 1) {
            $expectedReason = $policy.reason
            break
        }
    }
    if ($failure.Count -eq 1) {
        if ($Status -cne 'failed' -or [string]$StatusReason -cne $expectedReason -or
            [string]$ErrorCode -cne [string]$failure[0].payload.error_code) {
            throw [IO.InvalidDataException]::new("M0E013 action outcome '$($failure[0].payload.outcome)' must preserve failed/$expectedReason and its error code, never discarded.")
        }
        return
    }

    $failedChecks = @($Events | Where-Object {
        $_.event -ceq 'check_recorded' -and $_.payload.status -ceq 'failed'
    } | Sort-Object { [int]$_.event_seq })
    if ($failedChecks.Count -gt 0) {
        $failedCheck = $failedChecks[0]
        foreach ($otherCheck in @($failedChecks | Select-Object -Skip 1)) {
            if ($otherCheck.payload.status_reason -cne $failedCheck.payload.status_reason -or
                $otherCheck.payload.error_code -cne $failedCheck.payload.error_code) {
                throw [IO.InvalidDataException]::new('M0E013 conflicting failed checks do not define one deterministic terminal cause.')
            }
        }
        if ($Status -cne 'failed' -or
            [string]$StatusReason -cne [string]$failedCheck.payload.status_reason -or
            [string]$ErrorCode -cne [string]$failedCheck.payload.error_code) {
            throw [IO.InvalidDataException]::new("M0E013 failed check '$($failedCheck.payload.check_id)' must preserve its closed status reason and error code.")
        }
        return
    }

    $observedNoise = @($Events | Where-Object {
        $_.event -ceq 'noise_observation' -and [bool]$_.payload.observed
    } | Sort-Object { [int]$_.event_seq })
    if ($observedNoise.Count -gt 0) {
        $classifiedCause = $observedNoise[0].payload
        foreach ($otherNoise in @($observedNoise | Select-Object -Skip 1)) {
            if ($otherNoise.payload.discard_reason -cne $classifiedCause.discard_reason -or
                [string]$otherNoise.payload.error_code -cne [string]$classifiedCause.error_code) {
                throw [IO.InvalidDataException]::new('M0E013 conflicting observed noise does not define one deterministic discard cause.')
            }
        }
        if ($Status -cne 'discarded' -or
            [string]$StatusReason -cne [string]$classifiedCause.discard_reason -or
            [string]$ErrorCode -cne [string]$classifiedCause.error_code) {
            throw [IO.InvalidDataException]::new("M0E013 observed cause '$($classifiedCause.noise_code)' must preserve its closed discard reason and error code.")
        }
        return
    }

    $skippedCheck = @($Events | Where-Object {
        $_.event -ceq 'check_recorded' -and $_.payload.status -ceq 'skipped'
    } | Select-Object -First 1)
    if ($skippedCheck.Count -eq 1) {
        if ($Status -cne 'inconclusive' -or
            [string]$StatusReason -cne [string]$skippedCheck[0].payload.status_reason -or
            [string]$ErrorCode -cne [string]$skippedCheck[0].payload.error_code) {
            throw [IO.InvalidDataException]::new("M0E013 skipped check '$($skippedCheck[0].payload.check_id)' requires its closed inconclusive/incomplete_evidence cause.")
        }
        return
    }

    if ($Status -cne 'valid' -or $null -ne $StatusReason -or $null -ne $ErrorCode) {
        throw [IO.InvalidDataException]::new('M0E013 unsupported non-valid terminal status has no deterministic adverse raw event.')
    }
}

function Assert-M0EvidenceRunProjection {
    param(
        [Parameter(Mandatory)] $Run,
        [Parameter(Mandatory)] [object[]] $Events
    )

    $observedMetrics = @($Events | Where-Object event -ceq 'resource_sample' | ForEach-Object { $_.payload })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($Run.metrics)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $observedMetrics)) {
        throw [IO.InvalidDataException]::new('M0E015 run metric summary differs from raw resource_sample events.')
    }

    $observedNoise = @($Events | Where-Object event -ceq 'noise_observation' | ForEach-Object { $_.payload })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($Run.external_noise)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $observedNoise)) {
        throw [IO.InvalidDataException]::new('M0E015 run external-noise summary differs from raw noise_observation events.')
    }

    $observedArtifacts = @($Events | Where-Object event -ceq 'artifact_recorded' | ForEach-Object {
        [ordered]@{
            artifact_id = $_.payload.artifact_id
            relative_path = $_.payload.relative_path
            sha256 = $_.payload.sha256
            size_bytes = [long]$_.payload.size_bytes
        }
    })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($Run.artifacts)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $observedArtifacts)) {
        throw [IO.InvalidDataException]::new('M0E015 run artifact summary differs from raw artifact_recorded events.')
    }
    $candidatePackages = @($Run.artifacts | Where-Object artifact_id -ceq 'candidate-package')
    if ($candidatePackages.Count -ne 1 -or
        $candidatePackages[0].sha256.ToLowerInvariant() -cne $Run.candidate_input.artifact_sha256.ToLowerInvariant()) {
        throw [IO.InvalidDataException]::new('M0E015 exactly one recorded candidate-package artifact must match candidate input artifact_sha256.')
    }

    $observedOmittedChecks = @($Events | Where-Object {
        $_.event -ceq 'check_recorded' -and $_.payload.status -ceq 'skipped'
    } | ForEach-Object {
        [ordered]@{ check_id = $_.payload.check_id; reason_code = $_.payload.reason_code }
    })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($Run.checks_omitted)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $observedOmittedChecks)) {
        throw [IO.InvalidDataException]::new('M0E015 run omitted-check summary differs from raw skipped check_recorded events.')
    }
}

function Add-M0EvidenceEvent {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Event,
        [Parameter(Mandatory)] [string] $SchemaPath
    )

    Assert-M0EvidenceRedactedValue -Value $Event
    $line = Assert-M0EvidenceJsonSchema -Value $Event -SchemaPath $SchemaPath -Code 'M0E002'
    $path = Assert-M0EvidenceSafePath -LiteralPath $LiteralPath -MayNotExist
    $stream = $null
    $created = $false
    $appendSucceeded = $false
    try {
        try {
            $stream = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
            $created = $true
        }
        catch [IO.IOException] {
            $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
        }

        if ($created) {
            if ([int]$Event.event_seq -ne 1 -or $null -ne $Event.previous_line_sha256 -or $Event.event -cne 'run_started') {
                throw [IO.InvalidDataException]::new('M0E003 first event must be run_started with sequence 1 and no previous hash.')
            }
        }
        else {
            $existingBytes = [byte[]]::new($stream.Length)
            $stream.Position = 0
            $stream.ReadExactly($existingBytes, 0, $existingBytes.Length)
            $existing = Read-M0EvidenceRaw -LiteralPath $path -EventSchemaPath $SchemaPath -Bytes $existingBytes
            if ($existing.last_event.event -ceq 'run_sealed') {
                throw [IO.InvalidDataException]::new('M0E004 raw evidence is sealed and append-only.')
            }
            if ([int]$Event.event_seq -ne ($existing.event_count + 1) -or
                $Event.previous_line_sha256 -cne $existing.final_line_sha256) {
                throw [IO.InvalidDataException]::new('M0E003 next event sequence or previous hash differs.')
            }
            foreach ($field in $script:M0EvidenceInvariantFields) {
                if ([string]$Event.$field -cne [string]$existing.first_event.$field) {
                    throw [IO.InvalidDataException]::new("M0E003 run invariant '$field' differs on append.")
                }
            }
        }

        $null = $stream.Seek(0, [IO.SeekOrigin]::End)
        $lineBytes = $script:M0EvidenceUtf8.GetBytes($line + "`n")
        $stream.Write($lineBytes, 0, $lineBytes.Length)
        $stream.Flush($true)
        $appendSucceeded = $true
    }
    finally {
        if ($null -ne $stream) {
            $stream.Dispose()
        }
        if ($created -and -not $appendSucceeded -and (Test-Path -LiteralPath $path -PathType Leaf) -and
            (Get-Item -LiteralPath $path -Force).Length -eq 0) {
            [IO.File]::Delete($path)
        }
    }
    return [pscustomobject]@{
        event_seq = [int]$Event.event_seq
        event_count = [int]$Event.event_seq
        line_sha256 = Get-M0EvidenceSha256Text -Value $line
        raw_path = $path
    }
}

function Complete-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] $SealEvent,
        [Parameter(Mandatory)] $RunRecord,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $runPathFull = Assert-M0EvidenceSafePath -LiteralPath $RunPath -MayNotExist
    if (Test-Path -LiteralPath $runPathFull) {
        throw [IO.IOException]::new('M0E007 sealed run record already exists and cannot be overwritten.')
    }
    if ($SealEvent.event -cne 'run_sealed' -or $SealEvent.payload.status -cne $RunRecord.status -or
        [string]$SealEvent.payload.status_reason -cne [string]$RunRecord.status_reason -or
        [string]$SealEvent.payload.error_code -cne [string]$RunRecord.error_code) {
        throw [IO.InvalidDataException]::new('M0E007 seal status differs from the run record.')
    }
    $eventSchema = Join-Path $SchemaRoot 'event.schema.json'
    $rawBeforeSeal = Read-M0EvidenceRaw -LiteralPath $RawPath -EventSchemaPath $eventSchema
    if ($rawBeforeSeal.last_event.event -ceq 'run_sealed' -or
        [int]$SealEvent.event_seq -ne ($rawBeforeSeal.event_count + 1) -or
        $SealEvent.previous_line_sha256 -cne $rawBeforeSeal.final_line_sha256) {
        throw [IO.InvalidDataException]::new('M0E007 raw evidence cannot accept the requested seal.')
    }
    $sealLine = Assert-M0EvidenceJsonSchema -Value $SealEvent -SchemaPath $eventSchema -Code 'M0E007'
    Assert-M0EvidenceDerivedStatus -Events @($rawBeforeSeal.events) -Status $SealEvent.payload.status -StatusReason $SealEvent.payload.status_reason -ErrorCode $SealEvent.payload.error_code
    $record = $RunRecord | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100
    $record.raw = [ordered]@{
        relative_path = Split-Path $RawPath -Leaf
        event_count = [int]$SealEvent.event_seq
        final_line_sha256 = Get-M0EvidenceSha256Text -Value $sealLine
        sealed_utc = $SealEvent.utc
    }
    Assert-M0EvidenceRedactedValue -Value $record
    $runSchema = Join-Path $SchemaRoot 'run.schema.json'
    $runJson = Assert-M0EvidenceJsonSchema -Value $record -SchemaPath $runSchema -Code 'M0E007'
    $first = $rawBeforeSeal.first_event
    foreach ($field in @('experiment_id', 'run_id', 'candidate_code', 'scenario_id', 'attempt')) {
        if ([string]$record.$field -cne [string]$first.$field) {
            throw [IO.InvalidDataException]::new("M0E007 run identity '$field' differs before seal.")
        }
    }
    foreach ($field in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
        if ($record.common_inputs.$field.ToLowerInvariant() -cne $first.$field.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E007 common input '$field' differs before seal.")
        }
    }
    $observedMarkers = @('run-started') + @($rawBeforeSeal.events | Where-Object event -ceq 'marker' | ForEach-Object { $_.payload.marker_id })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($record.markers)) -cne (ConvertTo-M0EvidenceJsonLine -Value $observedMarkers)) {
        throw [IO.InvalidDataException]::new('M0E007 marker inventory differs before seal.')
    }
    Assert-M0EvidenceRunProjection -Run $record -Events @($rawBeforeSeal.events)
    if ((ConvertTo-M0EvidenceJsonLine -Value @($SealEvent.payload.checks_skipped)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value @($record.checks_omitted))) {
        throw [IO.InvalidDataException]::new('M0E015 seal skipped-check inventory differs from the run/raw projection.')
    }
    $sealResult = Add-M0EvidenceEvent -LiteralPath $RawPath -Event $SealEvent -SchemaPath $eventSchema
    if ($sealResult.event_count -ne [int]$record.raw.event_count -or $sealResult.line_sha256 -cne $record.raw.final_line_sha256) {
        throw [IO.InvalidDataException]::new('M0E007 appended seal differs from its prevalidated hash.')
    }
    $bytes = $script:M0EvidenceUtf8.GetBytes($runJson + "`n")
    $stream = [IO.FileStream]::new($runPathFull, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    }
    finally {
        $stream.Dispose()
    }
    return Assert-M0EvidenceRun -RawPath $RawPath -RunPath $runPathFull -SchemaRoot $SchemaRoot
}

function Assert-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $run = Read-M0EvidenceJsonDocument -LiteralPath $RunPath -SchemaPath (Join-Path $SchemaRoot 'run.schema.json') -Code 'M0E008'
    $raw = Read-M0EvidenceRaw -LiteralPath $RawPath -EventSchemaPath (Join-Path $SchemaRoot 'event.schema.json') -RequireSeal
    $first = $raw.first_event
    $seal = $raw.last_event
    Assert-M0EvidenceDerivedStatus -Events @($raw.events) -Status $run.status -StatusReason $run.status_reason -ErrorCode $run.error_code
    foreach ($field in @('experiment_id', 'run_id', 'candidate_code', 'scenario_id', 'attempt')) {
        if ([string]$run.$field -cne [string]$first.$field) {
            throw [IO.InvalidDataException]::new("M0E008 run identity '$field' differs from raw evidence.")
        }
    }
    $hashFields = [ordered]@{
        protocol_sha256 = 'protocol_sha256'; environment_sha256 = 'environment_sha256'
        fixture_sha256 = 'fixture_sha256'; harness_sha256 = 'harness_sha256'; scenario_sha256 = 'scenario_sha256'
    }
    foreach ($field in $hashFields.Keys) {
        if ($run.common_inputs.$field.ToLowerInvariant() -cne $first.$field.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E008 common input '$field' differs from raw evidence.")
        }
    }
    if ($run.status -cne $seal.payload.status -or
        [string]$run.status_reason -cne [string]$seal.payload.status_reason -or
        [string]$run.error_code -cne [string]$seal.payload.error_code -or
        [int]$run.raw.event_count -ne $raw.event_count -or
        $run.raw.final_line_sha256 -cne $raw.final_line_sha256 -or
        $run.raw.relative_path -cne (Split-Path $RawPath -Leaf) -or
        $run.raw.sealed_utc -cne $seal.utc) {
        throw [IO.InvalidDataException]::new('M0E008 run seal/hash metadata differs from raw evidence.')
    }
    $observedMarkers = @('run-started') + @($raw.events | Where-Object event -ceq 'marker' | ForEach-Object { $_.payload.marker_id })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($run.markers)) -cne (ConvertTo-M0EvidenceJsonLine -Value $observedMarkers)) {
        throw [IO.InvalidDataException]::new('M0E008 run marker inventory differs from raw evidence.')
    }
    Assert-M0EvidenceRunProjection -Run $run -Events @($raw.events)
    if ((ConvertTo-M0EvidenceJsonLine -Value @($seal.payload.checks_skipped)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value @($run.checks_omitted))) {
        throw [IO.InvalidDataException]::new('M0E015 seal skipped-check inventory differs from the run/raw projection.')
    }
    return [pscustomobject]@{
        run_id = $run.run_id
        status = $run.status
        event_count = $raw.event_count
        final_line_sha256 = $raw.final_line_sha256
        run_record_sha256 = (Get-FileHash -LiteralPath $RunPath -Algorithm SHA256).Hash.ToLowerInvariant()
        started_utc = $first.utc
        started_qpc_ticks = [long]$first.qpc_ticks
        sealed_utc = $seal.utc
        sealed_qpc_ticks = [long]$seal.qpc_ticks
        qpc_frequency = [long]$first.qpc_frequency
    }
}

function Get-M0ExpectedCandidateResult {
    param([Parameter(Mandatory)] $Candidate)

    if ([int]$Candidate.failed -gt 0) { return 'fail' }
    if ([int]$Candidate.valid -lt 20 -or [int]$Candidate.discarded -gt 2 -or [int]$Candidate.inconclusive -gt 0) {
        return 'inconclusive'
    }
    return 'pass'
}

function Read-M0EvidenceSchedule {
    param(
        [Parameter(Mandatory)] [string] $SchedulePath,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $path = Assert-M0EvidenceSafePath -LiteralPath $SchedulePath
    $protocolRoot = [IO.Path]::GetFullPath((Split-Path $SchemaRoot -Parent))
    $anchoredSchedulePath = [IO.Path]::GetFullPath((Join-Path $protocolRoot 'schedule.v1.json'))
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not $path.Equals($anchoredSchedulePath, $pathComparison)) {
        throw [IO.InvalidDataException]::new('M0E014 SchedulePath must be the schedule.v1.json anchored beside the supplied schema root.')
    }
    try {
        $null = Assert-M0StatisticsArtifacts -ProtocolRoot $protocolRoot
    }
    catch {
        throw [IO.InvalidDataException]::new("M0E014 anchored schedule/statistics artifacts failed validation: $($_.Exception.Message)", $_.Exception)
    }
    $schemaPath = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $SchemaRoot 'schedule.schema.json')
    $json = [IO.File]::ReadAllText($path, $script:M0EvidenceUtf8)
    if (-not (Test-Json -Json $json -SchemaFile $schemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0E014 paired schedule failed schema validation.')
    }
    $schedule = $json | ConvertFrom-Json -Depth 100
    $generatedSchedule = New-M0PairedSchedule
    if ((ConvertTo-M0EvidenceJsonLine -Value $schedule) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $generatedSchedule)) {
        throw [IO.InvalidDataException]::new('M0E014 anchored schedule differs from the fixed SHA-256 rank algorithm.')
    }
    $seenBlocks = [Collections.Generic.HashSet[int]]::new()
    foreach ($block in $schedule.blocks) {
        if (-not $seenBlocks.Add([int]$block.block) -or
            $block.input -cne ('M0-P1|20260823|block:{0}' -f $block.block) -or
            (($block.order -ceq 'AB') -and ((ConvertTo-M0EvidenceJsonLine -Value @($block.candidate_order)) -cne '["A","B"]')) -or
            (($block.order -ceq 'BA') -and ((ConvertTo-M0EvidenceJsonLine -Value @($block.candidate_order)) -cne '["B","A"]'))) {
            throw [IO.InvalidDataException]::new("M0E014 paired schedule block '$($block.block)' is internally inconsistent.")
        }
    }
    if ($seenBlocks.Count -ne 22) {
        throw [IO.InvalidDataException]::new('M0E014 paired schedule must contain blocks 1 through 22 exactly once.')
    }
    for ($blockNumber = 1; $blockNumber -le 22; $blockNumber++) {
        if (-not $seenBlocks.Contains($blockNumber)) {
            throw [IO.InvalidDataException]::new("M0E014 paired schedule is missing block $blockNumber.")
        }
    }
    return $schedule
}

function ConvertTo-M0EvidenceExpectedCommonInputs {
    param([Parameter(Mandatory)] $ExpectedCommonInputs)

    $required = @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')
    $names = if ($ExpectedCommonInputs -is [Collections.IDictionary]) {
        @($ExpectedCommonInputs.Keys | ForEach-Object { [string]$_ })
    }
    else {
        @($ExpectedCommonInputs.PSObject.Properties.Name)
    }
    if ((ConvertTo-M0EvidenceJsonLine -Value @($names | Sort-Object)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value @($required | Sort-Object))) {
        throw [IO.InvalidDataException]::new('M0E016 ExpectedCommonInputs must contain exactly the five common SHA-256 fields.')
    }
    $normalized = [ordered]@{}
    foreach ($field in $required) {
        $value = [string]$ExpectedCommonInputs.$field
        if ($value -notmatch '^[A-Fa-f0-9]{64}$') {
            throw [IO.InvalidDataException]::new("M0E016 ExpectedCommonInputs '$field' is not a SHA-256 value.")
        }
        $normalized[$field] = $value.ToLowerInvariant()
    }
    return $normalized
}

function Get-M0EvidenceCanonicalDocumentHash {
    param([Parameter(Mandatory)] $Value)

    $json = ConvertTo-M0EvidenceJsonLine -Value $Value
    return Get-M0EvidenceSha256Text -Value ($json + "`n")
}

function Get-M0EvidenceCandidateDigests {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string[]] $RunPaths,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [switch] $RequireClean
    )

    if ($RunPaths.Count -eq 0) {
        throw [IO.InvalidDataException]::new("M0E016 candidate '$CandidateCode' has no canonical run records.")
    }
    $records = [Collections.Generic.List[object]]::new()
    $seenAttempts = [Collections.Generic.HashSet[int]]::new()
    foreach ($runPath in $RunPaths) {
        $path = Assert-M0EvidenceSafePath -LiteralPath $runPath
        $run = Read-M0EvidenceJsonDocument -LiteralPath $path -SchemaPath (Join-Path $SchemaRoot 'run.schema.json') -Code 'M0E016'
        if ($run.candidate_code -cne $CandidateCode -or -not $seenAttempts.Add([int]$run.attempt)) {
            throw [IO.InvalidDataException]::new("M0E016 candidate '$CandidateCode' digest received a foreign or duplicate attempt.")
        }
        $records.Add([pscustomobject]@{ path = $path; run = $run })
    }
    $orderedRecords = @($records | Sort-Object { [int]$_.run.attempt })
    $first = $orderedRecords[0].run
    $toolchainsJson = ConvertTo-M0EvidenceJsonLine -Value @($first.candidate_input.toolchains)
    $lockfilesJson = ConvertTo-M0EvidenceJsonLine -Value @($first.candidate_input.lockfiles)
    foreach ($record in $orderedRecords) {
        if ($record.run.candidate_input.git_commit -cne $first.candidate_input.git_commit -or
            [bool]$record.run.candidate_input.dirty -ne [bool]$first.candidate_input.dirty -or
            $record.run.candidate_input.build_mode -cne $first.candidate_input.build_mode -or
            (ConvertTo-M0EvidenceJsonLine -Value @($record.run.candidate_input.toolchains)) -cne $toolchainsJson -or
            (ConvertTo-M0EvidenceJsonLine -Value @($record.run.candidate_input.lockfiles)) -cne $lockfilesJson -or
            $record.run.candidate_input.artifact_sha256.ToLowerInvariant() -cne $first.candidate_input.artifact_sha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E016 candidate '$CandidateCode' candidate input invariants differ across run records.")
        }
    }
    if ($RequireClean -and [bool]$first.candidate_input.dirty) {
        throw [IO.InvalidDataException]::new("M0E016 decisive candidate '$CandidateCode' requires dirty=false across all run records.")
    }

    $toolchainManifest = [ordered]@{
        schema_id = 'm0.toolchain-bundle.v1'
        candidate_code = $CandidateCode
        toolchains = @($first.candidate_input.toolchains)
    }
    $lockfileManifest = [ordered]@{
        schema_id = 'm0.lockfile-bundle.v1'
        candidate_code = $CandidateCode
        lockfiles = @($first.candidate_input.lockfiles)
    }
    $runsManifest = [ordered]@{
        schema_id = 'm0.runs-manifest.v1'
        candidate_code = $CandidateCode
        runs = @($orderedRecords | ForEach-Object {
            [ordered]@{
                attempt = [int]$_.run.attempt
                run_id = $_.run.run_id
                run_record_sha256 = (Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash.ToLowerInvariant()
                raw_final_line_sha256 = $_.run.raw.final_line_sha256
                status = $_.run.status
            }
        })
    }
    return [pscustomobject]@{
        candidate_code = $CandidateCode
        git_commit = $first.candidate_input.git_commit
        dirty = [bool]$first.candidate_input.dirty
        build_mode = $first.candidate_input.build_mode
        artifact_sha256 = $first.candidate_input.artifact_sha256.ToLowerInvariant()
        toolchain_bundle_sha256 = Get-M0EvidenceCanonicalDocumentHash -Value $toolchainManifest
        lockfile_bundle_sha256 = Get-M0EvidenceCanonicalDocumentHash -Value $lockfileManifest
        runs_manifest_sha256 = Get-M0EvidenceCanonicalDocumentHash -Value $runsManifest
        toolchain_manifest = $toolchainManifest
        lockfile_manifest = $lockfileManifest
        runs_manifest = $runsManifest
    }
}

function Assert-M0EvidenceBatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $BatchPath,
        [Parameter(Mandatory)] [string[]] $RunPaths,
        [Parameter(Mandatory)] [string] $SchedulePath,
        [Parameter(Mandatory)] $ExpectedCommonInputs,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $schedule = Read-M0EvidenceSchedule -SchedulePath $SchedulePath -SchemaRoot $SchemaRoot
    $expectedInputs = ConvertTo-M0EvidenceExpectedCommonInputs -ExpectedCommonInputs $ExpectedCommonInputs
    $batch = Read-M0EvidenceJsonDocument -LiteralPath $BatchPath -SchemaPath (Join-Path $SchemaRoot 'batch.schema.json') -Code 'M0E009'
    foreach ($field in $expectedInputs.Keys) {
        if ($batch.common_inputs.$field.ToLowerInvariant() -cne $expectedInputs[$field]) {
            throw [IO.InvalidDataException]::new("M0E016 batch common input '$field' differs from ExpectedCommonInputs.")
        }
    }
    if ($batch.candidate_results.Count -ne 2 -or $batch.candidate_results[0].candidate_code -cne 'A' -or
        $batch.candidate_results[1].candidate_code -cne 'B' -or
        $batch.candidate_results[0].artifact_sha256.ToLowerInvariant() -ceq $batch.candidate_results[1].artifact_sha256.ToLowerInvariant()) {
        throw [IO.InvalidDataException]::new('M0E010 batch requires A/B with distinct candidate artifacts.')
    }
    if ($RunPaths.Count -ne 44) {
        throw [IO.InvalidDataException]::new("M0E012 decisive batch requires exactly 44 run records; got $($RunPaths.Count).")
    }
    foreach ($candidate in $batch.candidate_results) {
        $total = [int]$candidate.valid + [int]$candidate.discarded + [int]$candidate.failed + [int]$candidate.inconclusive
        $expected = Get-M0ExpectedCandidateResult -Candidate $candidate
        if ($total -ne [int]$candidate.planned -or $candidate.result -cne $expected) {
            throw [IO.InvalidDataException]::new("M0E011 candidate '$($candidate.candidate_code)' counts/status precedence differs.")
        }
    }
    $expectedDecision = if (@($batch.candidate_results | Where-Object result -ceq 'fail').Count -gt 0) {
        'fail'
    }
    elseif (@($batch.candidate_results | Where-Object result -ceq 'inconclusive').Count -gt 0) {
        'inconclusive'
    }
    else { 'pass' }
    if ($batch.decision -cne $expectedDecision) {
        throw [IO.InvalidDataException]::new('M0E011 batch decision precedence differs.')
    }

    $seenCandidates = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenRunIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenAttempts = @{
        A = [Collections.Generic.HashSet[int]]::new()
        B = [Collections.Generic.HashSet[int]]::new()
    }
    $observedStatuses = @{
        A = [ordered]@{ valid = 0; discarded = 0; failed = 0; inconclusive = 0 }
        B = [ordered]@{ valid = 0; discarded = 0; failed = 0; inconclusive = 0 }
    }
    $candidateRunPaths = @{
        A = [Collections.Generic.List[string]]::new()
        B = [Collections.Generic.List[string]]::new()
    }
    $observedStarts = @{ A = @{}; B = @{} }
    $batchQpcFrequency = $null
    foreach ($runPath in $RunPaths) {
        $run = Read-M0EvidenceJsonDocument -LiteralPath $runPath -SchemaPath (Join-Path $SchemaRoot 'run.schema.json') -Code 'M0E009'
        $candidate = @($batch.candidate_results | Where-Object candidate_code -ceq $run.candidate_code)
        if ($candidate.Count -ne 1) {
            throw [IO.InvalidDataException]::new("M0E010 run candidate '$($run.candidate_code)' is absent or duplicated in batch.")
        }
        $null = $seenCandidates.Add($run.candidate_code)
        if (-not $seenRunIds.Add($run.run_id) -or -not $seenAttempts[$run.candidate_code].Add([int]$run.attempt)) {
            throw [IO.InvalidDataException]::new("M0E012 duplicate run or attempt '$($run.run_id)'.")
        }
        if ($run.experiment_id -cne $batch.experiment_id -or $run.scenario_id -cne $batch.scenario_id) {
            throw [IO.InvalidDataException]::new("M0E010 run '$($run.run_id)' belongs to another experiment/scenario.")
        }
        $scheduledBlock = @($schedule.blocks | Where-Object block -eq ([int]$run.attempt))
        $expectedPosition = if ($scheduledBlock.Count -eq 1) {
            [Array]::IndexOf([object[]]@($scheduledBlock[0].candidate_order), [string]$run.candidate_code) + 1
        }
        else { 0 }
        if ($scheduledBlock.Count -ne 1 -or [int]$run.schedule.seed -ne [int]$schedule.seed -or
            [int]$run.schedule.block -ne [int]$scheduledBlock[0].block -or
            $run.schedule.block_order -cne $scheduledBlock[0].order -or
            [int]$run.schedule.position_in_block -ne $expectedPosition) {
            throw [IO.InvalidDataException]::new("M0E014 run '$($run.run_id)' candidate/block/order/position differs from paired schedule.")
        }
        foreach ($field in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
            if ($run.common_inputs.$field.ToLowerInvariant() -cne $expectedInputs[$field]) {
                throw [IO.InvalidDataException]::new("M0E016 run common input '$field' differs from ExpectedCommonInputs.")
            }
        }
        if ($run.candidate_input.artifact_sha256.ToLowerInvariant() -cne $candidate[0].artifact_sha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E010 candidate artifact differs for '$($run.candidate_code)'.")
        }
        $candidatePackages = @($run.artifacts | Where-Object artifact_id -ceq 'candidate-package')
        if ($candidatePackages.Count -ne 1 -or
            $candidatePackages[0].sha256.ToLowerInvariant() -cne $run.candidate_input.artifact_sha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E016 decisive run '$($run.run_id)' requires one matching recorded candidate-package artifact.")
        }
        $rawPath = Join-Path (Split-Path -Parent ([IO.Path]::GetFullPath($runPath))) $run.raw.relative_path
        $validatedRun = Assert-M0EvidenceRun -RawPath $rawPath -RunPath $runPath -SchemaRoot $SchemaRoot
        if ($null -eq $batchQpcFrequency) {
            $batchQpcFrequency = [long]$validatedRun.qpc_frequency
        }
        elseif ([long]$batchQpcFrequency -ne [long]$validatedRun.qpc_frequency) {
            throw [IO.InvalidDataException]::new("M0E014 run '$($run.run_id)' QPC frequency differs inside the paired batch.")
        }
        $observedStarts[$run.candidate_code][[int]$run.attempt] = [pscustomobject]@{
            started_utc = [DateTimeOffset]::Parse($validatedRun.started_utc)
            started_qpc_ticks = [long]$validatedRun.started_qpc_ticks
            sealed_utc = [DateTimeOffset]::Parse($validatedRun.sealed_utc)
            sealed_qpc_ticks = [long]$validatedRun.sealed_qpc_ticks
        }
        $candidateRunPaths[$run.candidate_code].Add([IO.Path]::GetFullPath($runPath))
        $observedStatuses[$run.candidate_code][$run.status] = [int]$observedStatuses[$run.candidate_code][$run.status] + 1
    }
    if (-not ($seenCandidates.Contains('A') -and $seenCandidates.Contains('B'))) {
        throw [IO.InvalidDataException]::new('M0E010 batch validation requires at least one run from A and B.')
    }
    foreach ($candidateCode in @('A', 'B')) {
        if ($seenAttempts[$candidateCode].Count -ne 22) {
            throw [IO.InvalidDataException]::new("M0E012 candidate '$candidateCode' does not contain 22 unique attempts.")
        }
        for ($attempt = 1; $attempt -le 22; $attempt++) {
            if (-not $seenAttempts[$candidateCode].Contains($attempt)) {
                throw [IO.InvalidDataException]::new("M0E012 candidate '$candidateCode' is missing attempt $attempt.")
            }
        }
        $candidate = @($batch.candidate_results | Where-Object candidate_code -ceq $candidateCode)[0]
        foreach ($status in @('valid', 'discarded', 'failed', 'inconclusive')) {
            if ([int]$candidate.$status -ne [int]$observedStatuses[$candidateCode][$status]) {
                throw [IO.InvalidDataException]::new("M0E012 candidate '$candidateCode' $status count differs from sealed runs.")
            }
        }
        $digests = Get-M0EvidenceCandidateDigests -RunPaths @($candidateRunPaths[$candidateCode]) -CandidateCode $candidateCode -SchemaRoot $SchemaRoot -RequireClean
        foreach ($field in @('artifact_sha256', 'toolchain_bundle_sha256', 'lockfile_bundle_sha256', 'runs_manifest_sha256')) {
            if ($candidate.$field.ToLowerInvariant() -cne $digests.$field.ToLowerInvariant()) {
                throw [IO.InvalidDataException]::new("M0E016 candidate '$candidateCode' $field differs from its canonical manifest/records.")
            }
        }
    }
    $previousWindow = $null
    foreach ($block in $schedule.blocks) {
        for ($positionIndex = 0; $positionIndex -lt 2; $positionIndex++) {
            $candidateCode = [string]$block.candidate_order[$positionIndex]
            $currentWindow = $observedStarts[$candidateCode][[int]$block.block]
            if ($null -eq $currentWindow -or
                ($null -ne $previousWindow -and
                 ($previousWindow.sealed_qpc_ticks -ge $currentWindow.started_qpc_ticks -or
                  $previousWindow.sealed_utc -ge $currentWindow.started_utc))) {
                throw [IO.InvalidDataException]::new("M0E014 global raw execution position sequence overlaps or is unordered: prior run_sealed must precede run_started at block '$($block.block)' position '$($positionIndex + 1)' in QPC/UTC.")
            }
            $previousWindow = $currentWindow
        }
    }
    return [pscustomobject]@{
        batch_id = $batch.batch_id
        decision = $batch.decision
        candidate_count = $batch.candidate_results.Count
    }
}

Export-ModuleMember -Function @(
    'Add-M0EvidenceEvent',
    'Complete-M0EvidenceRun',
    'Assert-M0EvidenceRun',
    'Assert-M0EvidenceBatch',
    'Get-M0EvidenceCandidateDigests',
    'ConvertTo-M0EvidenceRedactedText'
)
