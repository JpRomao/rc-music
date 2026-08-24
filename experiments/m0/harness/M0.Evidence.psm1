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
$script:M0EvidenceMarkersModulePath = Join-Path $PSScriptRoot 'M0.Markers.psm1'
Import-Module $script:M0EvidenceMarkersModulePath -Force -ErrorAction Stop
$script:M0EvidenceMarkerCatalogCache = @{}
$script:M0EvidenceModuleInstanceId = [Guid]::NewGuid().ToString('N')
$script:M0EvidenceWriterRegistry = @{}
$script:M0EvidenceClosedWriterTokens = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$script:M0EvidenceLegacyWriters = @{}
$script:M0EvidenceIdentityTestHook = $null

if ($IsWindows -and -not ('M0EvidenceNativeV1.WindowsFileIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace M0EvidenceNativeV1
{
    public sealed class WindowsFileFingerprint
    {
        public string VolumeSerialHex { get; }
        public string FileIdHex { get; }
        public string FileIndexHex { get; }
        public uint LinkCount { get; }
        public string FinalPath { get; }

        internal WindowsFileFingerprint(string volumeSerialHex, string fileIdHex,
            string fileIndexHex, uint linkCount, string finalPath)
        {
            VolumeSerialHex = volumeSerialHex;
            FileIdHex = fileIdHex;
            FileIndexHex = fileIndexHex;
            LinkCount = linkCount;
            FinalPath = finalPath;
        }
    }

    public static class WindowsFileIdentity
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct FILETIME { public uint Low; public uint High; }

        [StructLayout(LayoutKind.Sequential)]
        private struct BY_HANDLE_FILE_INFORMATION
        {
            public uint Attributes;
            public FILETIME CreationTime;
            public FILETIME LastAccessTime;
            public FILETIME LastWriteTime;
            public uint VolumeSerialNumber;
            public uint FileSizeHigh;
            public uint FileSizeLow;
            public uint NumberOfLinks;
            public uint FileIndexHigh;
            public uint FileIndexLow;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct FILE_ID_128
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
            public byte[] Identifier;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct FILE_ID_INFO
        {
            public ulong VolumeSerialNumber;
            public FILE_ID_128 FileId;
        }

        private enum FILE_INFO_BY_HANDLE_CLASS { FileIdInfo = 18 }

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle, out BY_HANDLE_FILE_INFORMATION information);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandleEx(
            SafeFileHandle handle, FILE_INFO_BY_HANDLE_CLASS informationClass,
            out FILE_ID_INFO information, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle, StringBuilder path, uint characterCount, uint flags);

        public static WindowsFileFingerprint Capture(SafeFileHandle handle)
        {
            if (handle == null || handle.IsInvalid || handle.IsClosed)
                throw new ArgumentException("An open SafeFileHandle is required.", nameof(handle));
            if (!GetFileInformationByHandle(handle, out var legacy))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if (!GetFileInformationByHandleEx(handle, FILE_INFO_BY_HANDLE_CLASS.FileIdInfo,
                    out var modern, (uint)Marshal.SizeOf<FILE_ID_INFO>()))
                throw new Win32Exception(Marshal.GetLastWin32Error());

            var index = ((ulong)legacy.FileIndexHigh << 32) | legacy.FileIndexLow;
            return new WindowsFileFingerprint(
                modern.VolumeSerialNumber.ToString("x16"),
                Convert.ToHexString(modern.FileId.Identifier).ToLowerInvariant(),
                index.ToString("x16"), legacy.NumberOfLinks, GetFinalPath(handle));
        }

        private static string GetFinalPath(SafeFileHandle handle)
        {
            uint capacity = 512;
            while (true)
            {
                var buffer = new StringBuilder((int)capacity);
                uint length = GetFinalPathNameByHandleW(handle, buffer, capacity, 0);
                if (length == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (length < capacity) return NormalizeFinalPath(buffer.ToString());
                capacity = checked(length + 1);
                if (capacity > 32768) throw new IOException("Final path exceeds the Windows safety bound.");
            }
        }

        private static string NormalizeFinalPath(string value)
        {
            if (value.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
                value = @"\\" + value.Substring(8);
            else if (value.StartsWith(@"\\?\", StringComparison.OrdinalIgnoreCase))
                value = value.Substring(4);
            return Path.GetFullPath(value);
        }
    }
}
'@
}

$ExecutionContext.SessionState.Module.OnRemove = {
    foreach ($entry in @($script:M0EvidenceWriterRegistry.Values)) {
        try { if ($null -ne $entry.raw_stream) { $entry.raw_stream.Dispose() } } catch {}
        try { if ($null -ne $entry.root_lock) { $entry.root_lock.Dispose() } } catch {}
        try {
            if ($entry.created_raw -and (Test-Path -LiteralPath $entry.paths.raw_path -PathType Leaf) -and
                (Get-Item -LiteralPath $entry.paths.raw_path -Force).Length -eq 0) {
                [IO.File]::Delete($entry.paths.raw_path)
            }
        }
        catch {}
        try { $entry.public_context.state = 'closed' } catch {}
    }
    $script:M0EvidenceWriterRegistry.Clear()
    $script:M0EvidenceLegacyWriters.Clear()
}

function Get-M0EvidenceMarkerCatalog {
    param([Parameter(Mandatory)] [string] $SchemaRoot)

    $protocolRoot = [IO.Path]::GetFullPath((Split-Path $SchemaRoot -Parent))
    if (-not $script:M0EvidenceMarkerCatalogCache.ContainsKey($protocolRoot)) {
        $script:M0EvidenceMarkerCatalogCache[$protocolRoot] = (Assert-M0MarkerArtifacts -ProtocolRoot $protocolRoot).catalog
    }
    return $script:M0EvidenceMarkerCatalogCache[$protocolRoot]
}

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
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrWhiteSpace([string]$item.LinkType))) {
                throw [IO.InvalidDataException]::new("M0E005 links are forbidden in evidence paths: '$path'.")
            }
        }
    }
    if (-not $MayNotExist -and -not (Test-Path -LiteralPath $full -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0E005 evidence file is missing: '$full'.")
    }
    return $full
}

function Get-M0EvidenceHandleFingerprint {
    param(
        [Parameter(Mandatory)] [IO.FileStream] $Stream,
        [Parameter(Mandatory)] [string] $ExpectedPath
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0E026 durable Evidence file identity is fail-closed outside Windows until an openat/fstat adapter is validated.')
    }
    try {
        $fingerprint = [M0EvidenceNativeV1.WindowsFileIdentity]::Capture($Stream.SafeFileHandle)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0E005 cannot prove opened file identity for '$ExpectedPath'.", $_.Exception)
    }
    $comparison = [StringComparison]::OrdinalIgnoreCase
    $expected = [IO.Path]::GetFullPath($ExpectedPath)
    if ([uint32]$fingerprint.LinkCount -ne 1 -or
        -not [IO.Path]::GetFullPath($fingerprint.FinalPath).Equals($expected, $comparison) -or
        $fingerprint.VolumeSerialHex -notmatch '^[a-f0-9]{16}$' -or
        $fingerprint.FileIdHex -notmatch '^[a-f0-9]{32}$' -or
        $fingerprint.FileIndexHex -notmatch '^[a-f0-9]{16}$' -or
        $fingerprint.FileIdHex -eq ('0' * 32)) {
        throw [IO.InvalidDataException]::new("M0E005 opened file identity, final path or link count is invalid for '$expected'.")
    }
    return $fingerprint
}

function Assert-M0EvidenceHandleIdentity {
    param(
        [Parameter(Mandatory)] [IO.FileStream] $Stream,
        [Parameter(Mandatory)] [string] $ExpectedPath,
        $InitialFingerprint
    )

    $current = Get-M0EvidenceHandleFingerprint -Stream $Stream -ExpectedPath $ExpectedPath
    if ($null -ne $InitialFingerprint -and
        ($current.VolumeSerialHex -cne $InitialFingerprint.VolumeSerialHex -or
         $current.FileIdHex -cne $InitialFingerprint.FileIdHex -or
         $current.FileIndexHex -cne $InitialFingerprint.FileIndexHex)) {
        throw [IO.InvalidDataException]::new("M0E005 opened file identity changed for '$ExpectedPath'.")
    }
    $null = Assert-M0EvidenceSafePath -LiteralPath $ExpectedPath
    return $current
}

function Invoke-M0EvidenceIdentityTestHook {
    param(
        [Parameter(Mandatory)] [string] $Stage,
        [Parameter(Mandatory)] [string] $LiteralPath
    )

    $hook = $script:M0EvidenceIdentityTestHook
    if ($null -eq $hook -or [string]$hook.stage -cne $Stage) { return }
    $script:M0EvidenceIdentityTestHook = $null
    if ($hook.mode -ceq 'replace_same_bytes') {
        $bytes = [IO.File]::ReadAllBytes($LiteralPath)
        [IO.File]::Delete($LiteralPath)
        [IO.File]::WriteAllBytes($LiteralPath, $bytes)
        return
    }
    if ($hook.mode -ceq 'create_hardlink') {
        $null = New-Item -ItemType HardLink -Path ([string]$hook.link_path) -Target $LiteralPath
        return
    }
    throw [InvalidOperationException]::new("M0E005 unknown private identity test-hook mode '$($hook.mode)'.")
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
    $value = $json | ConvertFrom-Json -Depth 100 -DateKind String
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
        $event = $line | ConvertFrom-Json -Depth 100 -DateKind String
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

    $observedActions = @($Events | Where-Object event -ceq 'action_observation' | ForEach-Object { $_.payload })
    if ((ConvertTo-M0EvidenceJsonLine -Value @($Run.actions)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value $observedActions)) {
        throw [IO.InvalidDataException]::new('M0E015 run action summary differs from raw action_observation events.')
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
    if ($Run.candidate_code -ceq 'P') {
        $processTrees = @($Run.artifacts | Where-Object artifact_id -ceq 'process-tree')
        if ($processTrees.Count -ne 1 -or $null -eq $Run.process_tree_sha256 -or
            $processTrees[0].sha256.ToLowerInvariant() -cne $Run.process_tree_sha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0E015 synthetic pilot requires exactly one process-tree artifact linked to process_tree_sha256.')
        }
    }
    else {
        $candidatePackages = @($Run.artifacts | Where-Object artifact_id -ceq 'candidate-package')
        if ($candidatePackages.Count -ne 1 -or
            $candidatePackages[0].sha256.ToLowerInvariant() -cne $Run.candidate_input.artifact_sha256.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new('M0E015 exactly one recorded candidate-package artifact must match candidate input artifact_sha256.')
        }
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

function Assert-M0SyntheticPilotSequence {
    param(
        [Parameter(Mandatory)] [object[]] $Events,
        [switch] $RequireSeal
    )

    if ($Events.Count -lt 3 -or $Events[0].event -cne 'run_started') {
        throw [IO.InvalidDataException]::new('M0E015 synthetic pilot must begin with run_started and include artifact/check evidence.')
    }
    $allowed = @('run_started', 'artifact_recorded', 'check_recorded')
    if ($RequireSeal) { $allowed += 'run_sealed' }
    foreach ($event in $Events) {
        if ($event.candidate_code -cne 'P' -or $event.scenario_id -cne 'synthetic-process-tree' -or
            [int]$event.attempt -ne 1 -or $event.event -cnotin $allowed) {
            throw [IO.InvalidDataException]::new('M0E015 synthetic pilot event identity or event type is outside the closed pilot branch.')
        }
    }
    if (@($Events | Where-Object event -ceq 'artifact_recorded').Count -ne 1 -or
        @($Events | Where-Object event -ceq 'check_recorded').Count -lt 1) {
        throw [IO.InvalidDataException]::new('M0E015 synthetic pilot requires exactly one artifact and at least one check event.')
    }
    if ($RequireSeal -and $Events[-1].event -cne 'run_sealed') {
        throw [IO.InvalidDataException]::new('M0E015 synthetic pilot must end with run_sealed.')
    }
}

function Get-M0EvidenceTransactionPaths {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId,
        [string] $RawPath,
        [string] $RunPath
    )

    if ($RunId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
        throw [ArgumentException]::new("M0E018 invalid run identifier '$RunId'.")
    }
    $root = [IO.Path]::GetFullPath($EvidenceRoot)
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0E018 EvidenceRoot is missing: '$root'.")
    }
    $null = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root '.m0-evidence.lock') -MayNotExist

    $expectedRaw = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.jsonl") -MayNotExist
    $expectedRun = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.run.json") -MayNotExist
    if (-not [string]::IsNullOrWhiteSpace($RawPath) -and
        -not [IO.Path]::GetFullPath($RawPath).Equals($expectedRaw, $(if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }))) {
        throw [ArgumentException]::new('M0E018 RawPath must be the RunId-derived leaf inside EvidenceRoot.')
    }
    if (-not [string]::IsNullOrWhiteSpace($RunPath) -and
        -not [IO.Path]::GetFullPath($RunPath).Equals($expectedRun, $(if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }))) {
        throw [ArgumentException]::new('M0E018 RunPath must be the RunId-derived leaf inside EvidenceRoot.')
    }

    $paths = [ordered]@{
        run_id = $RunId
        root = $root
        raw_path = $expectedRaw
        run_path = $expectedRun
        intent_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.intent.json") -MayNotExist
        snapshot_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.snapshot.json") -MayNotExist
        run_temp_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.run.pending") -MayNotExist
        snapshot_temp_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$RunId.snapshot.pending") -MayNotExist
        lock_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root '.m0-evidence.lock') -MayNotExist
    }
    return [pscustomobject]$paths
}

function Open-M0EvidenceRootLock {
    param(
        [Parameter(Mandatory)] $Paths,
        [switch] $AllowCreateInEmptyRoot
    )

    $stream = $null
    $createdSentinel = $false
    try {
        if (Test-Path -LiteralPath $Paths.lock_path -PathType Leaf) {
            $stream = [IO.FileStream]::new(
                $Paths.lock_path,
                [IO.FileMode]::Open,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None
            )
        }
        else {
            if (-not $AllowCreateInEmptyRoot -or @(Get-ChildItem -LiteralPath $Paths.root -Force).Count -ne 0) {
                throw [IO.InvalidDataException]::new("M0E025 EvidenceRoot lock sentinel is missing from a nonempty or recovery root: '$($Paths.root)'.")
            }
            $stream = [IO.FileStream]::new(
                $Paths.lock_path,
                [IO.FileMode]::CreateNew,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None
            )
            $createdSentinel = $true
        }
    }
    catch [IO.IOException] {
        throw [IO.IOException]::new("M0E020 EvidenceRoot writer lock is already held for '$($Paths.root)'.", $_.Exception)
    }
    try {
        if ($createdSentinel) {
            $unexpectedItems = @(Get-ChildItem -LiteralPath $Paths.root -Force | Where-Object FullName -cne $Paths.lock_path)
            if ($unexpectedItems.Count -ne 0) {
                throw [IO.InvalidDataException]::new("M0E025 EvidenceRoot changed while its initial lock sentinel was created: '$($Paths.root)'.")
            }
        }
        $fingerprint = Assert-M0EvidenceHandleIdentity -Stream $stream -ExpectedPath $Paths.lock_path
        $stream | Add-Member -NotePropertyName M0EvidenceFingerprint -NotePropertyValue $fingerprint -Force
        return $stream
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        if ($createdSentinel -and (Test-Path -LiteralPath $Paths.lock_path -PathType Leaf)) {
            [IO.File]::Delete($Paths.lock_path)
        }
        throw
    }
}

function Open-M0EvidenceRootReadLock {
    param([Parameter(Mandatory)] $Paths)

    if (-not (Test-Path -LiteralPath $Paths.lock_path -PathType Leaf)) {
        throw [IO.InvalidDataException]::new("M0E025 EvidenceRoot lock sentinel is missing for read-only evidence validation: '$($Paths.root)'.")
    }
    $stream = $null
    try {
        $stream = [IO.FileStream]::new(
            $Paths.lock_path,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::None
        )
    }
    catch [IO.IOException] {
        throw [IO.IOException]::new("M0E020 EvidenceRoot writer lock is held for '$($Paths.root)'.", $_.Exception)
    }
    try {
        $fingerprint = Assert-M0EvidenceHandleIdentity -Stream $stream -ExpectedPath $Paths.lock_path
        $stream | Add-Member -NotePropertyName M0EvidenceFingerprint -NotePropertyValue $fingerprint -Force
        return $stream
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        throw
    }
}

function Read-M0EvidenceStreamBytes {
    param([Parameter(Mandatory)] [IO.FileStream] $Stream)

    if ($Stream.Length -gt 1073741824L) {
        throw [IO.InvalidDataException]::new('M0E021 evidence transaction file exceeds the 1 GiB safety bound.')
    }
    $bytes = [byte[]]::new([int]$Stream.Length)
    $Stream.Position = 0
    if ($bytes.Length -gt 0) {
        $Stream.ReadExactly($bytes, 0, $bytes.Length)
    }
    return ,$bytes
}

function Write-M0EvidenceCreateNewFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes
    )

    $path = Assert-M0EvidenceSafePath -LiteralPath $LiteralPath -MayNotExist
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    }
    finally {
        $stream.Dispose()
    }
    $null = Assert-M0EvidenceSafePath -LiteralPath $path
}

function Get-M0EvidenceBlob {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    return [ordered]@{
        size_bytes = [long]$Bytes.Length
        sha256 = Get-M0EvidenceSha256Bytes -Bytes $Bytes
        bytes_hex = [Convert]::ToHexString($Bytes).ToLowerInvariant()
    }
}

function Get-M0EvidenceBlobBytes {
    param(
        [Parameter(Mandatory)] $Blob,
        [Parameter(Mandatory)] [string] $Name
    )

    try {
        $bytes = [Convert]::FromHexString([string]$Blob.bytes_hex)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0E021 $Name bytes_hex is invalid.", $_.Exception)
    }
    if ([long]$bytes.Length -ne [long]$Blob.size_bytes -or
        (Get-M0EvidenceSha256Bytes -Bytes $bytes) -cne [string]$Blob.sha256 -or
        [Convert]::ToHexString($bytes).ToLowerInvariant() -cne [string]$Blob.bytes_hex) {
        throw [IO.InvalidDataException]::new("M0E021 $Name bytes, size or SHA-256 differ.")
    }
    return $bytes
}

function ConvertFrom-M0EvidenceCanonicalDocumentBytes {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Bytes.Length -eq 0 -or
        ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new("M0E021 $Name bytes are empty or contain a BOM.")
    }
    try {
        $text = $script:M0EvidenceUtf8.GetString($Bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0E021 $Name bytes are not strict UTF-8.", $_.Exception)
    }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.Substring(0, $text.Length - 1).Contains("`n")) {
        throw [IO.InvalidDataException]::new("M0E021 $Name must be one canonical JSON document with one final LF.")
    }
    $json = $text.Substring(0, $text.Length - 1)
    if (-not (Test-Json -Json $json -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0E021 $Name failed schema validation.")
    }
    $value = $json | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-M0EvidenceRedactedValue -Value $value
    if ((ConvertTo-M0EvidenceJsonLine -Value $value) -cne $json) {
        throw [IO.InvalidDataException]::new("M0E021 $Name is not canonical compact JSON.")
    }
    return $value
}

function Get-M0EvidenceRawState {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] $Raw
    )

    return [ordered]@{
        size_bytes = [long]$Bytes.Length
        sha256 = Get-M0EvidenceSha256Bytes -Bytes $Bytes
        event_count = [int]$Raw.event_count
        final_line_sha256 = [string]$Raw.final_line_sha256
    }
}

function Get-M0EvidenceTransactionId {
    param(
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] $PathContract,
        [Parameter(Mandatory)] $RawPrecondition,
        [Parameter(Mandatory)] $SealedRaw,
        [Parameter(Mandatory)] $SealBlob,
        [Parameter(Mandatory)] $RunBlob
    )

    $seed = [ordered]@{
        schema_id = 'm0.evidence-transaction-seed.v1'
        run_id = $RunId
        paths = $PathContract
        raw_precondition = $RawPrecondition
        sealed_raw = $SealedRaw
        seal = [ordered]@{ size_bytes = $SealBlob.size_bytes; sha256 = $SealBlob.sha256 }
        run = [ordered]@{ size_bytes = $RunBlob.size_bytes; sha256 = $RunBlob.sha256 }
    }
    return Get-M0EvidenceSha256Text -Value (ConvertTo-M0EvidenceJsonLine -Value $seed)
}

function New-M0EvidenceTransactionPlan {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [byte[]] $RawBytes,
        [Parameter(Mandatory)] $SealEvent,
        [Parameter(Mandatory)] $RunRecord,
        [Parameter(Mandatory)] [ValidateSet('candidate_run', 'synthetic_pilot')] [string] $EvidenceRole,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $eventSchema = Join-Path $SchemaRoot 'event.schema.json'
    $runSchema = Join-Path $SchemaRoot 'run.schema.json'
    $intentSchema = Join-Path $SchemaRoot 'evidence-intent.schema.json'
    $snapshotSchema = Join-Path $SchemaRoot 'evidence-snapshot.schema.json'
    $candidateCode = [string]$RunRecord.candidate_code
    if ([string]$RunRecord.run_id -cne [string]$Paths.run_id -or
        [string]$SealEvent.run_id -cne [string]$Paths.run_id) {
        throw [IO.InvalidDataException]::new('M0E018 transaction run_id differs from the RunId-derived evidence leaves.')
    }
    if (($EvidenceRole -ceq 'candidate_run' -and $candidateCode -cnotin @('A', 'B')) -or
        ($EvidenceRole -ceq 'synthetic_pilot' -and $candidateCode -cne 'P')) {
        throw [IO.InvalidDataException]::new("M0E022 evidence role '$EvidenceRole' is incompatible with candidate_code '$candidateCode'.")
    }
    $rawBeforeSeal = Read-M0EvidenceRaw -LiteralPath $Paths.raw_path -EventSchemaPath $eventSchema -Bytes $RawBytes
    if ($rawBeforeSeal.last_event.event -ceq 'run_sealed') {
        throw [IO.InvalidDataException]::new('M0E019 raw precondition is already sealed without a matching transaction intent.')
    }
    if ($SealEvent.event -cne 'run_sealed' -or $SealEvent.payload.status -cne $RunRecord.status -or
        [string]$SealEvent.payload.status_reason -cne [string]$RunRecord.status_reason -or
        [string]$SealEvent.payload.error_code -cne [string]$RunRecord.error_code -or
        [int]$SealEvent.event_seq -ne ($rawBeforeSeal.event_count + 1) -or
        $SealEvent.previous_line_sha256 -cne $rawBeforeSeal.final_line_sha256) {
        throw [IO.InvalidDataException]::new('M0E007 seal identity, status or raw precondition differs from the run record.')
    }
    if ($rawBeforeSeal.first_event.run_id -cne $RunRecord.run_id) {
        throw [IO.InvalidDataException]::new('M0E007 RunId differs between raw evidence and run record.')
    }
    $sealLine = Assert-M0EvidenceJsonSchema -Value $SealEvent -SchemaPath $eventSchema -Code 'M0E007'
    Assert-M0EvidenceDerivedStatus -Events @($rawBeforeSeal.events) -Status $SealEvent.payload.status -StatusReason $SealEvent.payload.status_reason -ErrorCode $SealEvent.payload.error_code
    if ($candidateCode -ceq 'P') {
        Assert-M0SyntheticPilotSequence -Events @($rawBeforeSeal.events)
    }
    else {
        $markerCatalog = Get-M0EvidenceMarkerCatalog -SchemaRoot $SchemaRoot
        $null = Assert-M0MarkerSequence -Catalog $markerCatalog -ScenarioId $rawBeforeSeal.first_event.scenario_id -Events @($rawBeforeSeal.events) -Status $SealEvent.payload.status
    }

    $record = $RunRecord | ConvertTo-Json -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
    $record.raw = [ordered]@{
        relative_path = Split-Path $Paths.raw_path -Leaf
        event_count = [int]$SealEvent.event_seq
        final_line_sha256 = Get-M0EvidenceSha256Text -Value $sealLine
        sealed_utc = $SealEvent.utc
    }
    Assert-M0EvidenceRedactedValue -Value $record
    $runJson = Assert-M0EvidenceJsonSchema -Value $record -SchemaPath $runSchema -Code 'M0E007'
    $first = $rawBeforeSeal.first_event
    foreach ($field in @('experiment_id', 'run_id', 'candidate_code', 'scenario_id', 'attempt')) {
        if ([string]$record.$field -cne [string]$first.$field) {
            throw [IO.InvalidDataException]::new("M0E007 run identity '$field' differs before intent publication.")
        }
    }
    foreach ($field in @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')) {
        if ($record.common_inputs.$field.ToLowerInvariant() -cne $first.$field.ToLowerInvariant()) {
            throw [IO.InvalidDataException]::new("M0E007 common input '$field' differs before intent publication.")
        }
    }
    $observedMarkers = @(Get-M0MarkerInventory -Events @($rawBeforeSeal.events))
    if ((ConvertTo-M0EvidenceJsonLine -Value @($record.markers)) -cne (ConvertTo-M0EvidenceJsonLine -Value $observedMarkers)) {
        throw [IO.InvalidDataException]::new('M0E007 marker inventory differs before intent publication.')
    }
    Assert-M0EvidenceRunProjection -Run $record -Events @($rawBeforeSeal.events)
    if ((ConvertTo-M0EvidenceJsonLine -Value @($SealEvent.payload.checks_skipped)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value @($record.checks_omitted))) {
        throw [IO.InvalidDataException]::new('M0E015 seal skipped-check inventory differs from the run/raw projection.')
    }

    $sealBytes = $script:M0EvidenceUtf8.GetBytes($sealLine + "`n")
    $runBytes = $script:M0EvidenceUtf8.GetBytes($runJson + "`n")
    $sealedRawBytes = [byte[]]::new($RawBytes.Length + $sealBytes.Length)
    [Buffer]::BlockCopy($RawBytes, 0, $sealedRawBytes, 0, $RawBytes.Length)
    [Buffer]::BlockCopy($sealBytes, 0, $sealedRawBytes, $RawBytes.Length, $sealBytes.Length)
    $rawPrecondition = Get-M0EvidenceRawState -Bytes $RawBytes -Raw $rawBeforeSeal
    $sealedRaw = [ordered]@{
        size_bytes = [long]$sealedRawBytes.Length
        sha256 = Get-M0EvidenceSha256Bytes -Bytes $sealedRawBytes
        event_count = [int]$SealEvent.event_seq
        final_line_sha256 = Get-M0EvidenceSha256Text -Value $sealLine
    }
    $sealBlob = Get-M0EvidenceBlob -Bytes $sealBytes
    $runBlob = Get-M0EvidenceBlob -Bytes $runBytes
    $pathContract = [ordered]@{
        raw_relative_path = Split-Path $Paths.raw_path -Leaf
        run_relative_path = Split-Path $Paths.run_path -Leaf
        intent_relative_path = Split-Path $Paths.intent_path -Leaf
        snapshot_relative_path = Split-Path $Paths.snapshot_path -Leaf
        run_temp_relative_path = Split-Path $Paths.run_temp_path -Leaf
        snapshot_temp_relative_path = Split-Path $Paths.snapshot_temp_path -Leaf
    }
    $transactionId = Get-M0EvidenceTransactionId -RunId $record.run_id -PathContract $pathContract -RawPrecondition $rawPrecondition -SealedRaw $sealedRaw -SealBlob $sealBlob -RunBlob $runBlob
    $snapshot = [ordered]@{
        schema_id = 'm0.evidence-snapshot.v1'
        schema_version = 1
        transaction_id = $transactionId
        run_id = $record.run_id
        state = 'committed'
        evidence_role = $EvidenceRole
        decision_eligible = ($EvidenceRole -ceq 'candidate_run')
        raw_relative_path = $pathContract.raw_relative_path
        run_relative_path = $pathContract.run_relative_path
        intent_relative_path = $pathContract.intent_relative_path
        raw = $sealedRaw
        seal = [ordered]@{ size_bytes = $sealBlob.size_bytes; sha256 = $sealBlob.sha256 }
        run = [ordered]@{ size_bytes = $runBlob.size_bytes; sha256 = $runBlob.sha256 }
    }
    Assert-M0EvidenceRedactedValue -Value $snapshot
    $snapshotJson = Assert-M0EvidenceJsonSchema -Value $snapshot -SchemaPath $snapshotSchema -Code 'M0E022'
    $snapshotBytes = $script:M0EvidenceUtf8.GetBytes($snapshotJson + "`n")
    $snapshotBlob = Get-M0EvidenceBlob -Bytes $snapshotBytes
    $intent = [ordered]@{
        schema_id = 'm0.evidence-intent.v1'
        schema_version = 1
        transaction_id = $transactionId
        run_id = $record.run_id
        paths = $pathContract
        raw_precondition = $rawPrecondition
        expected = [ordered]@{
            sealed_raw = $sealedRaw
            seal = $sealBlob
            run = $runBlob
            snapshot = $snapshotBlob
        }
    }
    Assert-M0EvidenceRedactedValue -Value $intent
    $intentJson = Assert-M0EvidenceJsonSchema -Value $intent -SchemaPath $intentSchema -Code 'M0E021'
    $intentBytes = $script:M0EvidenceUtf8.GetBytes($intentJson + "`n")
    return [pscustomobject][ordered]@{
        transaction_id = $transactionId
        raw_before_seal = $rawBeforeSeal
        record = $record
        seal_event = $SealEvent
        snapshot = $snapshot
        intent = $intent
        raw_precondition_bytes = $RawBytes
        sealed_raw_bytes = $sealedRawBytes
        seal_bytes = $sealBytes
        run_bytes = $runBytes
        snapshot_bytes = $snapshotBytes
        intent_bytes = $intentBytes
    }
}

function Open-M0EvidenceReadHandle {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $path = Assert-M0EvidenceSafePath -LiteralPath $LiteralPath
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $fingerprint = Assert-M0EvidenceHandleIdentity -Stream $stream -ExpectedPath $path
        $stream | Add-Member -NotePropertyName M0EvidenceFingerprint -NotePropertyValue $fingerprint -Force
        return $stream
    }
    catch {
        $stream.Dispose()
        throw
    }
}

function Read-M0EvidenceLockedFileBytes {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $stream = Open-M0EvidenceReadHandle -LiteralPath $LiteralPath
    try {
        return Read-M0EvidenceStreamBytes -Stream $stream
    }
    finally {
        $stream.Dispose()
    }
}

function Test-M0EvidenceByteEquality {
    param(
        [Parameter(Mandatory)] [byte[]] $Left,
        [Parameter(Mandatory)] [byte[]] $Right
    )

    return [Linq.Enumerable]::SequenceEqual[byte]($Left, $Right)
}

function Get-M0EvidenceValidatedIntentPlan {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [byte[]] $RawBytes,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [byte[]] $IntentBytes
    )

    try {
        if (-not $PSBoundParameters.ContainsKey('IntentBytes')) {
            $IntentBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.intent_path
        }
        $intent = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $IntentBytes -SchemaPath (Join-Path $SchemaRoot 'evidence-intent.schema.json') -Name 'transaction intent'
        $expectedPaths = [ordered]@{
            raw_relative_path = Split-Path $Paths.raw_path -Leaf
            run_relative_path = Split-Path $Paths.run_path -Leaf
            intent_relative_path = Split-Path $Paths.intent_path -Leaf
            snapshot_relative_path = Split-Path $Paths.snapshot_path -Leaf
            run_temp_relative_path = Split-Path $Paths.run_temp_path -Leaf
            snapshot_temp_relative_path = Split-Path $Paths.snapshot_temp_path -Leaf
        }
        if ([string]$intent.run_id -cne [string]$Paths.run_id -or
            (ConvertTo-M0EvidenceJsonLine -Value $intent.paths) -cne (ConvertTo-M0EvidenceJsonLine -Value $expectedPaths)) {
            throw [IO.InvalidDataException]::new('M0E021 transaction intent paths differ from EvidenceRoot/RunId-derived leaves.')
        }
        $prefixLength = [long]$intent.raw_precondition.size_bytes
        if ($prefixLength -le 0 -or $RawBytes.Length -lt $prefixLength) {
            throw [IO.InvalidDataException]::new('M0E019 raw evidence is shorter than the immutable intent precondition.')
        }
        $prefix = [byte[]]::new([int]$prefixLength)
        [Buffer]::BlockCopy($RawBytes, 0, $prefix, 0, $prefix.Length)
        if ((Get-M0EvidenceSha256Bytes -Bytes $prefix) -cne [string]$intent.raw_precondition.sha256) {
            throw [IO.InvalidDataException]::new('M0E019 stale transaction intent raw precondition differs from current bytes.')
        }
        $sealBytes = Get-M0EvidenceBlobBytes -Blob $intent.expected.seal -Name 'seal'
        $runBytes = Get-M0EvidenceBlobBytes -Blob $intent.expected.run -Name 'run'
        $snapshotBytes = Get-M0EvidenceBlobBytes -Blob $intent.expected.snapshot -Name 'snapshot'
        $seal = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $sealBytes -SchemaPath (Join-Path $SchemaRoot 'event.schema.json') -Name 'intent seal'
        $run = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $runBytes -SchemaPath (Join-Path $SchemaRoot 'run.schema.json') -Name 'intent run'
        $snapshot = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $snapshotBytes -SchemaPath (Join-Path $SchemaRoot 'evidence-snapshot.schema.json') -Name 'intent snapshot'
        $plan = New-M0EvidenceTransactionPlan -Paths $Paths -RawBytes $prefix -SealEvent $seal -RunRecord $run -EvidenceRole $snapshot.evidence_role -SchemaRoot $SchemaRoot
        if (-not (Test-M0EvidenceByteEquality -Left $IntentBytes -Right $plan.intent_bytes) -or
            -not (Test-M0EvidenceByteEquality -Left $snapshotBytes -Right $plan.snapshot_bytes) -or
            $intent.transaction_id -cne $plan.transaction_id -or $snapshot.transaction_id -cne $plan.transaction_id) {
            throw [IO.InvalidDataException]::new('M0E021 immutable transaction intent or embedded expected bytes differ from the deterministic plan.')
        }
        return $plan
    }
    catch {
        if ($_.Exception.Message -match '^M0E019') { throw }
        if ($_.Exception.Message -match '^M0E021') { throw }
        throw [IO.InvalidDataException]::new("M0E021 transaction intent is corrupt: $($_.Exception.Message)", $_.Exception)
    }
}

function New-M0EvidenceRecoveryStateResult {
    param(
        [Parameter(Mandatory)] [string] $State,
        [Parameter(Mandatory)] $Paths,
        [string] $RunId,
        [string] $TransactionId,
        [string] $ReasonCode,
        [bool] $RunTempPresent = $false,
        [bool] $SnapshotTempPresent = $false
    )

    return [pscustomobject][ordered]@{
        state = $State
        reason_code = if ([string]::IsNullOrWhiteSpace($ReasonCode)) { $null } else { $ReasonCode }
        run_id = if ([string]::IsNullOrWhiteSpace($RunId)) { $null } else { $RunId }
        transaction_id = if ([string]::IsNullOrWhiteSpace($TransactionId)) { $null } else { $TransactionId }
        run_temp_present = $RunTempPresent
        snapshot_temp_present = $SnapshotTempPresent
        raw_path = $Paths.raw_path
        run_path = $Paths.run_path
        intent_path = $Paths.intent_path
        snapshot_path = $Paths.snapshot_path
    }
}

function Get-M0EvidenceRecoveryStateNoLock {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $hasIntent = Test-Path -LiteralPath $Paths.intent_path -PathType Leaf
    $hasRun = Test-Path -LiteralPath $Paths.run_path -PathType Leaf
    $hasSnapshot = Test-Path -LiteralPath $Paths.snapshot_path -PathType Leaf
    $hasRunTemp = Test-Path -LiteralPath $Paths.run_temp_path -PathType Leaf
    $hasSnapshotTemp = Test-Path -LiteralPath $Paths.snapshot_temp_path -PathType Leaf
    if (-not $hasIntent) {
        if ($hasRun -or $hasSnapshot -or $hasRunTemp -or $hasSnapshotTemp) {
            return New-M0EvidenceRecoveryStateResult -State conflict -Paths $Paths -RunId $RunId -ReasonCode orphan_publication
        }
        if (-not (Test-Path -LiteralPath $Paths.raw_path -PathType Leaf)) {
            return New-M0EvidenceRecoveryStateResult -State unstarted -Paths $Paths -RunId $RunId -ReasonCode raw_missing
        }
        try {
            $rawBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.raw_path
            $raw = Read-M0EvidenceRaw -LiteralPath $Paths.raw_path -EventSchemaPath (Join-Path $SchemaRoot 'event.schema.json') -Bytes $rawBytes
            if ($raw.first_event.run_id -cne $RunId) {
                return New-M0EvidenceRecoveryStateResult -State conflict -Paths $Paths -RunId $RunId -ReasonCode raw_identity_mismatch
            }
            if ($raw.last_event.event -ceq 'run_sealed') {
                return New-M0EvidenceRecoveryStateResult -State conflict -Paths $Paths -RunId $RunId -ReasonCode unjournaled_seal
            }
            return New-M0EvidenceRecoveryStateResult -State unstarted -Paths $Paths -RunId $RunId
        }
        catch {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -ReasonCode raw_corrupt
        }
    }

    if (-not (Test-Path -LiteralPath $Paths.raw_path -PathType Leaf)) {
        return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -ReasonCode raw_missing
    }
    try {
        $rawBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.raw_path
        $plan = Get-M0EvidenceValidatedIntentPlan -Paths $Paths -RawBytes $rawBytes -SchemaRoot $SchemaRoot
    }
    catch {
        $state = if ($_.Exception.Message -match '^M0E019') { 'conflict' } else { 'corrupt' }
        $reason = if ($state -ceq 'conflict') { 'stale_precondition' } else { 'intent_corrupt' }
        return New-M0EvidenceRecoveryStateResult -State $state -Paths $Paths -RunId $RunId -ReasonCode $reason -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
    }

    $isPrecondition = $rawBytes.Length -eq $plan.raw_precondition_bytes.Length -and
        (Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.raw_precondition_bytes)
    $isSealed = $rawBytes.Length -eq $plan.sealed_raw_bytes.Length -and
        (Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.sealed_raw_bytes)
    if (-not $isPrecondition -and -not $isSealed) {
        return New-M0EvidenceRecoveryStateResult -State conflict -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode unjournaled_raw_tail -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
    }

    if ($hasRunTemp) {
        try {
            if (-not (Test-M0EvidenceByteEquality -Left (Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.run_temp_path) -Right $plan.run_bytes)) { throw 'different' }
        }
        catch {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode run_temp_corrupt -RunTempPresent $true -SnapshotTempPresent $hasSnapshotTemp
        }
    }
    if ($hasSnapshotTemp) {
        try {
            if (-not (Test-M0EvidenceByteEquality -Left (Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.snapshot_temp_path) -Right $plan.snapshot_bytes)) { throw 'different' }
        }
        catch {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode snapshot_temp_corrupt -RunTempPresent $hasRunTemp -SnapshotTempPresent $true
        }
    }
    if ($hasRun) {
        try {
            if (-not (Test-M0EvidenceByteEquality -Left (Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.run_path) -Right $plan.run_bytes)) { throw 'different' }
        }
        catch {
            return New-M0EvidenceRecoveryStateResult -State conflict -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode run_divergent -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
        }
    }
    if ($hasSnapshot) {
        try {
            if (-not (Test-M0EvidenceByteEquality -Left (Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.snapshot_path) -Right $plan.snapshot_bytes)) { throw 'different' }
        }
        catch {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode snapshot_corrupt -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
        }
        if (-not $isSealed -or -not $hasRun) {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode premature_snapshot -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
        }
        return New-M0EvidenceRecoveryStateResult -State committed -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
    }
    if ($hasRun) {
        if (-not $isSealed) {
            return New-M0EvidenceRecoveryStateResult -State corrupt -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -ReasonCode premature_run -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
        }
        return New-M0EvidenceRecoveryStateResult -State run_published -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
    }
    if ($isSealed) {
        return New-M0EvidenceRecoveryStateResult -State raw_sealed -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
    }
    return New-M0EvidenceRecoveryStateResult -State intent_only -Paths $Paths -RunId $RunId -TransactionId $plan.transaction_id -RunTempPresent $hasRunTemp -SnapshotTempPresent $hasSnapshotTemp
}

function Invoke-M0EvidenceCrashInjection {
    param(
        [AllowEmptyString()] [string] $RequestedBoundary,
        [Parameter(Mandatory)] [string] $ActualBoundary
    )

    if (-not [string]::IsNullOrWhiteSpace($RequestedBoundary) -and $RequestedBoundary -ceq $ActualBoundary) {
        throw [InvalidOperationException]::new("M0E023 injected process-crash boundary '$ActualBoundary'.")
    }
}

function Move-M0EvidenceNoOverwrite {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Destination,
        [Parameter(Mandatory)] [string] $Name
    )

    $sourceStream = Open-M0EvidenceReadHandle -LiteralPath $Source
    try {
        $sourceFingerprint = $sourceStream.M0EvidenceFingerprint
    }
    finally {
        $sourceStream.Dispose()
    }
    $hookStage = if ($Name -ceq 'run record') { 'run_temp_after_flush_before_move' } else { 'snapshot_temp_after_flush_before_move' }
    Invoke-M0EvidenceIdentityTestHook -Stage $hookStage -LiteralPath $Source
    try {
        [IO.File]::Move($Source, $Destination, $false)
    }
    catch [IO.IOException] {
        throw [IO.IOException]::new("M0E019 $Name destination already exists or atomic no-overwrite rename failed.", $_.Exception)
    }
    $destinationStream = Open-M0EvidenceReadHandle -LiteralPath $Destination
    try {
        $destinationFingerprint = Assert-M0EvidenceHandleIdentity -Stream $destinationStream -ExpectedPath $Destination -InitialFingerprint $sourceFingerprint
        if ($destinationFingerprint.VolumeSerialHex -cne $sourceFingerprint.VolumeSerialHex -or
            $destinationFingerprint.FileIdHex -cne $sourceFingerprint.FileIdHex -or
            $destinationFingerprint.FileIndexHex -cne $sourceFingerprint.FileIndexHex) {
            throw [IO.InvalidDataException]::new("M0E005 $Name atomic rename changed physical file identity.")
        }
    }
    finally {
        $destinationStream.Dispose()
    }
}

function Get-M0EvidenceLegacyWriterKey {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId
    )

    $root = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/')
    if ($IsWindows) { $root = $root.ToUpperInvariant() }
    return "$root|$RunId"
}

function Get-M0EvidenceWriterEntry {
    param(
        [Parameter(Mandatory)] $Writer,
        [switch] $AllowClosed
    )

    $tokenProperty = $Writer.PSObject.Properties['writer_token']
    $instanceProperty = $Writer.PSObject.Properties['module_instance_id']
    if ($null -eq $tokenProperty -or $null -eq $instanceProperty -or
        [string]$instanceProperty.Value -cne $script:M0EvidenceModuleInstanceId -or
        [string]$tokenProperty.Value -notmatch '^[a-f0-9]{64}$') {
        throw [IO.InvalidDataException]::new('M0E024 forged writer context or context from another module instance.')
    }
    $token = [string]$tokenProperty.Value
    if (-not $script:M0EvidenceWriterRegistry.ContainsKey($token)) {
        if ($AllowClosed -and $script:M0EvidenceClosedWriterTokens.Contains($token)) { return $null }
        $state = if ($script:M0EvidenceClosedWriterTokens.Contains($token)) { 'closed' } else { 'unknown' }
        throw [IO.InvalidDataException]::new("M0E024 $state writer context is unavailable.")
    }
    $entry = $script:M0EvidenceWriterRegistry[$token]
    if (-not [object]::ReferenceEquals($Writer, $entry.public_context)) {
        throw [IO.InvalidDataException]::new('M0E024 forged writer context does not match the registered opaque token owner.')
    }
    return $entry
}

function Close-M0EvidenceWriterEntry {
    param([Parameter(Mandatory)] $Entry)

    if ($Entry.state -ceq 'closed') { return }
    $Entry.state = 'closed'
    try {
        if ($null -ne $Entry.raw_stream) { $Entry.raw_stream.Dispose() }
    }
    finally {
        if ($null -ne $Entry.root_lock) { $Entry.root_lock.Dispose() }
    }
    if ($Entry.created_raw -and (Test-Path -LiteralPath $Entry.paths.raw_path -PathType Leaf)) {
        $rawItem = Get-Item -LiteralPath $Entry.paths.raw_path -Force
        if ($rawItem.Length -eq 0) { [IO.File]::Delete($Entry.paths.raw_path) }
    }
    $Entry.public_context.state = 'closed'
    $null = $script:M0EvidenceWriterRegistry.Remove($Entry.token)
    $null = $script:M0EvidenceClosedWriterTokens.Add($Entry.token)
    if (-not [string]::IsNullOrWhiteSpace([string]$Entry.legacy_key)) {
        $null = $script:M0EvidenceLegacyWriters.Remove($Entry.legacy_key)
    }
}

function Open-M0EvidenceWriter {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $RunId
    $rootLock = Open-M0EvidenceRootLock -Paths $paths -AllowCreateInEmptyRoot
    $rawStream = $null
    $createdRaw = $false
    try {
        if (Test-Path -LiteralPath $paths.intent_path -PathType Leaf) {
            throw [IO.InvalidDataException]::new('M0E024 writer cannot open a run after immutable intent publication; use recovery APIs.')
        }
        try {
            $rawStream = [IO.FileStream]::new($paths.raw_path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
            $createdRaw = $true
        }
        catch [IO.IOException] {
            $rawStream = [IO.FileStream]::new($paths.raw_path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
        }
        $rawFingerprint = Assert-M0EvidenceHandleIdentity -Stream $rawStream -ExpectedPath $paths.raw_path
        if (-not $createdRaw) {
            $existingBytes = Read-M0EvidenceStreamBytes -Stream $rawStream
            if ($existingBytes.Length -gt 0) {
                $existing = Read-M0EvidenceRaw -LiteralPath $paths.raw_path -EventSchemaPath (Join-Path $SchemaRoot 'event.schema.json') -Bytes $existingBytes
                if ($existing.first_event.run_id -cne $RunId -or $existing.last_event.event -ceq 'run_sealed') {
                    throw [IO.InvalidDataException]::new('M0E024 existing raw identity is foreign or already sealed.')
                }
            }
        }

        $token = [Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)).ToLowerInvariant()
        $publicContext = [pscustomobject][ordered]@{
            writer_token = $token
            module_instance_id = $script:M0EvidenceModuleInstanceId
            run_id = $RunId
            state = 'open'
        }
        $entry = [pscustomobject]@{
            token = $token
            public_context = $publicContext
            state = 'open'
            paths = $paths
            schema_root = [IO.Path]::GetFullPath($SchemaRoot)
            root_lock = $rootLock
            raw_stream = $rawStream
            raw_fingerprint = $rawFingerprint
            created_raw = $createdRaw
            legacy_key = $null
        }
        $script:M0EvidenceWriterRegistry[$token] = $entry
        return $publicContext
    }
    catch {
        if ($null -ne $rawStream) { $rawStream.Dispose() }
        $rootLock.Dispose()
        if ($createdRaw -and (Test-Path -LiteralPath $paths.raw_path -PathType Leaf) -and
            (Get-Item -LiteralPath $paths.raw_path -Force).Length -eq 0) {
            [IO.File]::Delete($paths.raw_path)
        }
        throw
    }
}

function Close-M0EvidenceWriter {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Writer)

    $entry = Get-M0EvidenceWriterEntry -Writer $Writer -AllowClosed
    if ($null -ne $entry) { Close-M0EvidenceWriterEntry -Entry $entry }
    return [pscustomobject][ordered]@{ run_id = [string]$Writer.run_id; state = 'closed' }
}

function Add-M0EvidenceEventWithEntry {
    param(
        [Parameter(Mandatory)] $Entry,
        [Parameter(Mandatory)] $Event
    )

    if ($Event.event -ceq 'run_sealed') {
        throw [InvalidOperationException]::new('M0E018 public Add-M0EvidenceEvent cannot append run_sealed; use Complete-M0EvidenceRun.')
    }
    if ($Entry.state -cne 'open') {
        throw [IO.InvalidDataException]::new("M0E024 writer context is '$($Entry.state)' and cannot append events.")
    }
    if ([string]$Event.run_id -cne [string]$Entry.paths.run_id) {
        throw [IO.InvalidDataException]::new('M0E024 cross-run event RunId differs from the stateful writer context.')
    }
    if (Test-Path -LiteralPath $Entry.paths.intent_path -PathType Leaf) {
        throw [IO.InvalidDataException]::new('M0E019 immutable transaction intent already exists; ordinary raw append is forbidden.')
    }
    $schemaPath = Join-Path $Entry.schema_root 'event.schema.json'
    Assert-M0EvidenceRedactedValue -Value $Event
    $line = Assert-M0EvidenceJsonSchema -Value $Event -SchemaPath $schemaPath -Code 'M0E002'
    $stream = $Entry.raw_stream
    $path = $Entry.paths.raw_path
    if ($stream.Length -eq 0) {
        if ([int]$Event.event_seq -ne 1 -or $null -ne $Event.previous_line_sha256 -or $Event.event -cne 'run_started') {
            throw [IO.InvalidDataException]::new('M0E003 first event must be run_started with sequence 1 and no previous hash.')
        }
    }
    else {
        $existingBytes = Read-M0EvidenceStreamBytes -Stream $stream
        $existing = Read-M0EvidenceRaw -LiteralPath $path -EventSchemaPath $schemaPath -Bytes $existingBytes
        if ($existing.last_event.event -ceq 'run_sealed') {
            throw [IO.InvalidDataException]::new('M0E004 raw evidence is sealed and append-only.')
        }
        if ([int]$Event.event_seq -eq $existing.event_count) {
            $headLine = ConvertTo-M0EvidenceJsonLine -Value $existing.last_event
            if ($line -ceq $headLine) {
                return [pscustomobject][ordered]@{
                    event_seq = [int]$existing.last_event.event_seq
                    event_count = [int]$existing.event_count
                    line_sha256 = [string]$existing.final_line_sha256
                    raw_path = $path
                    replay = $true
                }
            }
            throw [IO.InvalidDataException]::new('M0E003 divergent replay uses the current head sequence with different canonical bytes.')
        }
        if ([int]$Event.event_seq -lt $existing.event_count) {
            throw [IO.InvalidDataException]::new('M0E003 older event replay is forbidden after a later event exists.')
        }
        if ([int]$Event.event_seq -ne ($existing.event_count + 1) -or
            $Event.previous_line_sha256 -cne $existing.final_line_sha256) {
            throw [IO.InvalidDataException]::new('M0E003 next event sequence or previous hash is stale or differs.')
        }
        foreach ($field in $script:M0EvidenceInvariantFields) {
            if ([string]$Event.$field -cne [string]$existing.first_event.$field) {
                throw [IO.InvalidDataException]::new("M0E003 run invariant '$field' differs on append.")
            }
        }

        if ($Event.event -cin @('marker', 'action_observation')) {
            $catalog = Get-M0EvidenceMarkerCatalog -SchemaRoot $Entry.schema_root
            $candidateEvents = @($existing.events) + @($Event)
            $null = Assert-M0MarkerSequence -Catalog $catalog -ScenarioId $Event.scenario_id -Events $candidateEvents
        }
    }

    $null = $stream.Seek(0, [IO.SeekOrigin]::End)
    $lineBytes = $script:M0EvidenceUtf8.GetBytes($line + "`n")
    $stream.Write($lineBytes, 0, $lineBytes.Length)
    $stream.Flush($true)
    return [pscustomobject][ordered]@{
        event_seq = [int]$Event.event_seq
        event_count = [int]$Event.event_seq
        line_sha256 = Get-M0EvidenceSha256Text -Value $line
        raw_path = $path
        replay = $false
    }
}

function Add-M0EvidenceEvent {
    [CmdletBinding(DefaultParameterSetName = 'Writer')]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Writer')] $Writer,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $EvidenceRoot,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $LiteralPath,
        [Parameter(Mandatory)] $Event,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $SchemaPath
    )

    if ($PSCmdlet.ParameterSetName -ceq 'Writer') {
        $entry = Get-M0EvidenceWriterEntry -Writer $Writer
        return Add-M0EvidenceEventWithEntry -Entry $entry -Event $Event
    }

    $schemaRoot = Split-Path -Parent ([IO.Path]::GetFullPath($SchemaPath))
    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId ([string]$Event.run_id) -RawPath $LiteralPath
    $legacyKey = Get-M0EvidenceLegacyWriterKey -EvidenceRoot $EvidenceRoot -RunId ([string]$Event.run_id)
    $entry = $null
    if ($script:M0EvidenceLegacyWriters.ContainsKey($legacyKey)) {
        $entry = Get-M0EvidenceWriterEntry -Writer $script:M0EvidenceLegacyWriters[$legacyKey]
    }
    else {
        $context = Open-M0EvidenceWriter -EvidenceRoot $EvidenceRoot -RunId ([string]$Event.run_id) -SchemaRoot $schemaRoot
        $entry = Get-M0EvidenceWriterEntry -Writer $context
        $entry.legacy_key = $legacyKey
        $script:M0EvidenceLegacyWriters[$legacyKey] = $context
    }
    try {
        return Add-M0EvidenceEventWithEntry -Entry $entry -Event $Event
    }
    catch {
        Close-M0EvidenceWriterEntry -Entry $entry
        throw
    }
}

function Invoke-M0EvidenceResumeNoLock {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [IO.FileStream] $RawStream,
        [AllowEmptyString()] [ValidateSet('', 'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published', 'snapshot_temp_flushed', 'committed')] [string] $CrashAfterBoundary = ''
    )

    if (-not (Test-Path -LiteralPath $Paths.intent_path -PathType Leaf)) {
        throw [IO.InvalidDataException]::new('M0E019 Resume requires an immutable transaction intent.')
    }
    if (-not (Test-Path -LiteralPath $Paths.raw_path -PathType Leaf)) {
        throw [IO.InvalidDataException]::new('M0E021 Resume cannot recover a missing raw evidence file.')
    }

    $ownsRawStream = $null -eq $RawStream
    $rawStream = if ($ownsRawStream) {
        [IO.FileStream]::new($Paths.raw_path, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
    }
    else { $RawStream }
    $runStream = $null
    $snapshotStream = $null
    try {
        $rawInitialFingerprint = Assert-M0EvidenceHandleIdentity -Stream $rawStream -ExpectedPath $Paths.raw_path
        $rawBytes = Read-M0EvidenceStreamBytes -Stream $rawStream
        $plan = Get-M0EvidenceValidatedIntentPlan -Paths $Paths -RawBytes $rawBytes -SchemaRoot $SchemaRoot
        $isPrecondition = Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.raw_precondition_bytes
        $isSealed = Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.sealed_raw_bytes
        if (-not $isPrecondition -and -not $isSealed) {
            throw [IO.InvalidDataException]::new('M0E019 raw evidence has a non-journaled partial or complete tail; recovery never truncates it.')
        }
        if ($isPrecondition) {
            $rawStream.Position = $rawStream.Length
            $rawStream.Write($plan.seal_bytes, 0, $plan.seal_bytes.Length)
            $rawStream.Flush($true)
            $rawBytes = Read-M0EvidenceStreamBytes -Stream $rawStream
            if (-not (Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.sealed_raw_bytes)) {
                throw [IO.InvalidDataException]::new('M0E021 flushed raw seal differs from immutable intent bytes.')
            }
            Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'raw_sealed'
        }

        if (Test-Path -LiteralPath $Paths.run_temp_path -PathType Leaf) {
            $tempBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.run_temp_path
            if (-not (Test-M0EvidenceByteEquality -Left $tempBytes -Right $plan.run_bytes)) {
                throw [IO.InvalidDataException]::new('M0E021 run temp differs from immutable intent bytes.')
            }
        }
        if (Test-Path -LiteralPath $Paths.run_path -PathType Leaf) {
            $existingRunBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.run_path
            if (-not (Test-M0EvidenceByteEquality -Left $existingRunBytes -Right $plan.run_bytes)) {
                throw [IO.InvalidDataException]::new('M0E019 existing final run record diverges from immutable intent bytes.')
            }
        }
        else {
            if (-not (Test-Path -LiteralPath $Paths.run_temp_path -PathType Leaf)) {
                Write-M0EvidenceCreateNewFile -LiteralPath $Paths.run_temp_path -Bytes $plan.run_bytes
                Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'run_temp_flushed'
            }
            Move-M0EvidenceNoOverwrite -Source $Paths.run_temp_path -Destination $Paths.run_path -Name 'run record'
            Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'run_published'
        }

        $runStream = Open-M0EvidenceReadHandle -LiteralPath $Paths.run_path
        $runInitialFingerprint = $runStream.M0EvidenceFingerprint
        $runBytes = Read-M0EvidenceStreamBytes -Stream $runStream
        $null = Assert-M0EvidenceHandleIdentity -Stream $runStream -ExpectedPath $Paths.run_path -InitialFingerprint $runInitialFingerprint
        if (-not (Test-M0EvidenceByteEquality -Left $runBytes -Right $plan.run_bytes)) {
            throw [IO.InvalidDataException]::new('M0E019 final run record changed before snapshot publication.')
        }

        Invoke-M0EvidenceIdentityTestHook -Stage 'before_snapshot_identity_recheck' -LiteralPath $Paths.raw_path
        $rawBytes = Read-M0EvidenceStreamBytes -Stream $rawStream
        $null = Assert-M0EvidenceHandleIdentity -Stream $rawStream -ExpectedPath $Paths.raw_path -InitialFingerprint $rawInitialFingerprint
        if (-not (Test-M0EvidenceByteEquality -Left $rawBytes -Right $plan.sealed_raw_bytes)) {
            throw [IO.InvalidDataException]::new('M0E019 raw file identity or bytes changed before snapshot publication.')
        }

        if (Test-Path -LiteralPath $Paths.snapshot_temp_path -PathType Leaf) {
            $tempSnapshotBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.snapshot_temp_path
            if (-not (Test-M0EvidenceByteEquality -Left $tempSnapshotBytes -Right $plan.snapshot_bytes)) {
                throw [IO.InvalidDataException]::new('M0E021 snapshot temp differs from immutable intent bytes.')
            }
        }
        $snapshotPublished = $false
        if (Test-Path -LiteralPath $Paths.snapshot_path -PathType Leaf) {
            $existingSnapshotBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.snapshot_path
            if (-not (Test-M0EvidenceByteEquality -Left $existingSnapshotBytes -Right $plan.snapshot_bytes)) {
                throw [IO.InvalidDataException]::new('M0E022 existing commit snapshot diverges from immutable intent bytes.')
            }
        }
        else {
            if (-not (Test-Path -LiteralPath $Paths.snapshot_temp_path -PathType Leaf)) {
                Write-M0EvidenceCreateNewFile -LiteralPath $Paths.snapshot_temp_path -Bytes $plan.snapshot_bytes
                Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'snapshot_temp_flushed'
            }
            Move-M0EvidenceNoOverwrite -Source $Paths.snapshot_temp_path -Destination $Paths.snapshot_path -Name 'commit snapshot'
            $snapshotPublished = $true
        }

        $snapshotStream = Open-M0EvidenceReadHandle -LiteralPath $Paths.snapshot_path
        $snapshotInitialFingerprint = $snapshotStream.M0EvidenceFingerprint
        $snapshotBytes = Read-M0EvidenceStreamBytes -Stream $snapshotStream
        $null = Assert-M0EvidenceHandleIdentity -Stream $snapshotStream -ExpectedPath $Paths.snapshot_path -InitialFingerprint $snapshotInitialFingerprint
        if (-not (Test-M0EvidenceByteEquality -Left $snapshotBytes -Right $plan.snapshot_bytes)) {
            throw [IO.InvalidDataException]::new('M0E022 published commit snapshot differs from immutable intent bytes.')
        }
        if ($snapshotPublished) {
            Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'committed'
        }

        $validated = Assert-M0EvidenceRunCore -RawPath $Paths.raw_path -RunPath $Paths.run_path -SchemaRoot $SchemaRoot -RawBytes $rawBytes -RunBytes $runBytes
        return [pscustomobject][ordered]@{
            run_id = $validated.run_id
            status = $validated.status
            event_count = $validated.event_count
            final_line_sha256 = $validated.final_line_sha256
            run_record_sha256 = $validated.run_record_sha256
            recovery_state = 'committed'
            transaction_id = $plan.transaction_id
            snapshot_sha256 = Get-M0EvidenceSha256Bytes -Bytes $snapshotBytes
            raw_path = $Paths.raw_path
            run_path = $Paths.run_path
            intent_path = $Paths.intent_path
            snapshot_path = $Paths.snapshot_path
        }
    }
    finally {
        if ($null -ne $snapshotStream) { $snapshotStream.Dispose() }
        if ($null -ne $runStream) { $runStream.Dispose() }
        if ($ownsRawStream) { $rawStream.Dispose() }
    }
}

function Complete-M0EvidenceRunWithEntry {
    param(
        [Parameter(Mandatory)] $Entry,
        [Parameter(Mandatory)] $SealEvent,
        [Parameter(Mandatory)] $RunRecord,
        [Parameter(Mandatory)] [ValidateSet('candidate_run', 'synthetic_pilot')] [string] $EvidenceRole,
        [AllowEmptyString()] [ValidateSet('', 'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published', 'snapshot_temp_flushed', 'committed')] [string] $CrashAfterBoundary = ''
    )

    $runId = [string]$RunRecord.run_id
    if ($Entry.state -cnotin @('open', 'completed') -or $runId -cne $Entry.paths.run_id -or
        [string]$SealEvent.run_id -cne $Entry.paths.run_id) {
        throw [IO.InvalidDataException]::new('M0E024 cross-run RunId differs from the stateful writer context.')
    }
    $paths = $Entry.paths
    $schemaRoot = $Entry.schema_root
    $null = Assert-M0EvidenceHandleIdentity -Stream $Entry.raw_stream -ExpectedPath $paths.raw_path -InitialFingerprint $Entry.raw_fingerprint
    if (-not (Test-Path -LiteralPath $paths.intent_path -PathType Leaf)) {
        if ((Test-Path -LiteralPath $paths.run_path) -or (Test-Path -LiteralPath $paths.snapshot_path) -or
            (Test-Path -LiteralPath $paths.run_temp_path) -or (Test-Path -LiteralPath $paths.snapshot_temp_path)) {
            throw [IO.InvalidDataException]::new('M0E019 cannot start transaction with orphan publication files.')
        }
        $rawBytes = Read-M0EvidenceStreamBytes -Stream $Entry.raw_stream
        $plan = New-M0EvidenceTransactionPlan -Paths $paths -RawBytes $rawBytes -SealEvent $SealEvent -RunRecord $RunRecord -EvidenceRole $EvidenceRole -SchemaRoot $schemaRoot
        Write-M0EvidenceCreateNewFile -LiteralPath $paths.intent_path -Bytes $plan.intent_bytes
        Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'intent_published'
    }
    else {
        $rawBytes = Read-M0EvidenceStreamBytes -Stream $Entry.raw_stream
        $persistedPlan = Get-M0EvidenceValidatedIntentPlan -Paths $paths -RawBytes $rawBytes -SchemaRoot $schemaRoot
        $proposedPlan = New-M0EvidenceTransactionPlan -Paths $paths -RawBytes $persistedPlan.raw_precondition_bytes -SealEvent $SealEvent -RunRecord $RunRecord -EvidenceRole $EvidenceRole -SchemaRoot $schemaRoot
        if (-not (Test-M0EvidenceByteEquality -Left $persistedPlan.intent_bytes -Right $proposedPlan.intent_bytes)) {
            throw [IO.InvalidDataException]::new('M0E019 replayed completion inputs diverge from the immutable transaction intent.')
        }
    }
    $result = Invoke-M0EvidenceResumeNoLock -Paths $paths -RunId $runId -SchemaRoot $schemaRoot -RawStream $Entry.raw_stream -CrashAfterBoundary $CrashAfterBoundary
    $Entry.state = 'completed'
    $Entry.public_context.state = 'completed'
    return $result
}

function Complete-M0EvidenceRun {
    [CmdletBinding(DefaultParameterSetName = 'Writer')]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Writer')] $Writer,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $EvidenceRoot,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $RawPath,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $RunPath,
        [Parameter(Mandatory)] $SealEvent,
        [Parameter(Mandatory)] $RunRecord,
        [Parameter(Mandatory, ParameterSetName = 'Legacy')] [string] $SchemaRoot,
        [ValidateSet('candidate_run', 'synthetic_pilot')] [string] $EvidenceRole = 'candidate_run',
        [AllowEmptyString()] [ValidateSet('', 'intent_published', 'raw_sealed', 'run_temp_flushed', 'run_published', 'snapshot_temp_flushed', 'committed')] [string] $CrashAfterBoundary = ''
    )

    if ($PSCmdlet.ParameterSetName -ceq 'Writer') {
        $entry = Get-M0EvidenceWriterEntry -Writer $Writer
        try {
            return Complete-M0EvidenceRunWithEntry -Entry $entry -SealEvent $SealEvent -RunRecord $RunRecord -EvidenceRole $EvidenceRole -CrashAfterBoundary $CrashAfterBoundary
        }
        catch {
            if ($_.Exception.Message -match '^M0E023') { Close-M0EvidenceWriterEntry -Entry $entry }
            throw
        }
    }

    $runId = [string]$RunRecord.run_id
    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $runId -RawPath $RawPath -RunPath $RunPath
    $legacyKey = Get-M0EvidenceLegacyWriterKey -EvidenceRoot $EvidenceRoot -RunId $runId
    if ($script:M0EvidenceLegacyWriters.ContainsKey($legacyKey)) {
        $context = $script:M0EvidenceLegacyWriters[$legacyKey]
        $entry = Get-M0EvidenceWriterEntry -Writer $context
    }
    else {
        $context = Open-M0EvidenceWriter -EvidenceRoot $EvidenceRoot -RunId $runId -SchemaRoot $SchemaRoot
        $entry = Get-M0EvidenceWriterEntry -Writer $context
        $entry.legacy_key = $legacyKey
        $script:M0EvidenceLegacyWriters[$legacyKey] = $context
    }
    try {
        return Complete-M0EvidenceRunWithEntry -Entry $entry -SealEvent $SealEvent -RunRecord $RunRecord -EvidenceRole $EvidenceRole -CrashAfterBoundary $CrashAfterBoundary
    }
    finally {
        Close-M0EvidenceWriterEntry -Entry $entry
    }
}

function Assert-M0EvidenceRunCore {
    param(
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [byte[]] $RawBytes,
        [byte[]] $RunBytes
    )

    if (-not $PSBoundParameters.ContainsKey('RunBytes')) {
        $RunBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $RunPath
    }
    if (-not $PSBoundParameters.ContainsKey('RawBytes')) {
        $RawBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $RawPath
    }
    $run = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $RunBytes -SchemaPath (Join-Path $SchemaRoot 'run.schema.json') -Name 'run record'
    $raw = Read-M0EvidenceRaw -LiteralPath $RawPath -EventSchemaPath (Join-Path $SchemaRoot 'event.schema.json') -Bytes $RawBytes -RequireSeal
    $first = $raw.first_event
    $seal = $raw.last_event
    if ((Split-Path $RawPath -Leaf) -cne "$($run.run_id).jsonl" -or
        (Split-Path $RunPath -Leaf) -cne "$($run.run_id).run.json") {
        throw [IO.InvalidDataException]::new('M0E018 raw/run leaves are not derived from the canonical run_id.')
    }
    Assert-M0EvidenceDerivedStatus -Events @($raw.events) -Status $run.status -StatusReason $run.status_reason -ErrorCode $run.error_code
    if ($run.candidate_code -ceq 'P') {
        Assert-M0SyntheticPilotSequence -Events @($raw.events) -RequireSeal
    }
    else {
        $markerCatalog = Get-M0EvidenceMarkerCatalog -SchemaRoot $SchemaRoot
        $null = Assert-M0MarkerSequence -Catalog $markerCatalog -ScenarioId $first.scenario_id -Events @($raw.events) -Status $run.status
    }
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
    $observedMarkers = @(Get-M0MarkerInventory -Events @($raw.events))
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
        run_record_sha256 = Get-M0EvidenceSha256Bytes -Bytes $RunBytes
        started_utc = $first.utc
        started_qpc_ticks = [long]$first.qpc_ticks
        sealed_utc = $seal.utc
        sealed_qpc_ticks = [long]$seal.qpc_ticks
        qpc_frequency = [long]$first.qpc_frequency
        run_record = $run
    }
}

function Assert-M0EvidenceSnapshotForBytes {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [byte[]] $RawBytes,
        [Parameter(Mandatory)] [byte[]] $RunBytes,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [byte[]] $IntentBytes,
        [byte[]] $SnapshotBytes
    )

    if (-not (Test-Path -LiteralPath $Paths.intent_path -PathType Leaf)) {
        throw [IO.InvalidDataException]::new('M0E022 committed snapshot requires its immutable transaction intent.')
    }
    if (-not (Test-Path -LiteralPath $Paths.snapshot_path -PathType Leaf)) {
        throw [IO.InvalidDataException]::new('M0E022 run is not committed: final snapshot is missing.')
    }
    $intentArguments = @{ Paths = $Paths; RawBytes = $RawBytes; SchemaRoot = $SchemaRoot }
    if ($PSBoundParameters.ContainsKey('IntentBytes')) { $intentArguments.IntentBytes = $IntentBytes }
    $plan = Get-M0EvidenceValidatedIntentPlan @intentArguments
    if (-not (Test-M0EvidenceByteEquality -Left $RawBytes -Right $plan.sealed_raw_bytes) -or
        -not (Test-M0EvidenceByteEquality -Left $RunBytes -Right $plan.run_bytes)) {
        throw [IO.InvalidDataException]::new('M0E022 committed snapshot raw/run bytes differ from the immutable intent.')
    }
    if (-not $PSBoundParameters.ContainsKey('SnapshotBytes')) {
        $SnapshotBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $Paths.snapshot_path
    }
    if (-not (Test-M0EvidenceByteEquality -Left $SnapshotBytes -Right $plan.snapshot_bytes)) {
        throw [IO.InvalidDataException]::new('M0E022 commit snapshot bytes differ from the immutable intent.')
    }
    $snapshot = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $SnapshotBytes -SchemaPath (Join-Path $SchemaRoot 'evidence-snapshot.schema.json') -Name 'commit snapshot'
    if ([string]$snapshot.run_id -cne [string]$Paths.run_id) {
        throw [IO.InvalidDataException]::new('M0E022 commit snapshot run_id differs from its RunId-derived leaf.')
    }
    return [pscustomobject][ordered]@{
        state = 'committed'
        evidence_role = $snapshot.evidence_role
        decision_eligible = [bool]$snapshot.decision_eligible
        run_id = $snapshot.run_id
        transaction_id = $snapshot.transaction_id
        snapshot_sha256 = Get-M0EvidenceSha256Bytes -Bytes $SnapshotBytes
        raw_sha256 = $snapshot.raw.sha256
        run_sha256 = $snapshot.run.sha256
    }
}

function Read-M0EvidenceCommittedRunNoLock {
    param(
        [Parameter(Mandatory)] $Paths,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    foreach ($requiredPath in @($Paths.raw_path, $Paths.run_path, $Paths.intent_path, $Paths.snapshot_path)) {
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw [IO.InvalidDataException]::new("M0E022 committed run is missing '$requiredPath'.")
        }
    }
    $streams = [ordered]@{}
    try {
        $streams.raw = Open-M0EvidenceReadHandle -LiteralPath $Paths.raw_path
        $streams.run = Open-M0EvidenceReadHandle -LiteralPath $Paths.run_path
        $streams.intent = Open-M0EvidenceReadHandle -LiteralPath $Paths.intent_path
        $streams.snapshot = Open-M0EvidenceReadHandle -LiteralPath $Paths.snapshot_path
        $rawBytes = Read-M0EvidenceStreamBytes -Stream $streams.raw
        $runBytes = Read-M0EvidenceStreamBytes -Stream $streams.run
        $intentBytes = Read-M0EvidenceStreamBytes -Stream $streams.intent
        $snapshotBytes = Read-M0EvidenceStreamBytes -Stream $streams.snapshot

        foreach ($name in @('raw', 'run', 'intent', 'snapshot')) {
            $expectedPath = $Paths."${name}_path"
            $null = Assert-M0EvidenceHandleIdentity -Stream $streams[$name] -ExpectedPath $expectedPath -InitialFingerprint $streams[$name].M0EvidenceFingerprint
        }
        $validated = Assert-M0EvidenceRunCore -RawPath $Paths.raw_path -RunPath $Paths.run_path -SchemaRoot $SchemaRoot -RawBytes $rawBytes -RunBytes $runBytes
        $snapshot = Assert-M0EvidenceSnapshotForBytes -Paths $Paths -RawBytes $rawBytes -RunBytes $runBytes -SchemaRoot $SchemaRoot -IntentBytes $intentBytes -SnapshotBytes $snapshotBytes
        $validated | Add-Member -NotePropertyName recovery_state -NotePropertyValue $snapshot.state
        $validated | Add-Member -NotePropertyName evidence_role -NotePropertyValue $snapshot.evidence_role
        $validated | Add-Member -NotePropertyName decision_eligible -NotePropertyValue $snapshot.decision_eligible
        $validated | Add-Member -NotePropertyName transaction_id -NotePropertyValue $snapshot.transaction_id
        $validated | Add-Member -NotePropertyName snapshot_sha256 -NotePropertyValue $snapshot.snapshot_sha256
        $validated | Add-Member -NotePropertyName snapshot_result -NotePropertyValue $snapshot
        return $validated
    }
    finally {
        foreach ($stream in @($streams.Values)) {
            if ($null -ne $stream) { $stream.Dispose() }
        }
    }
}

function Assert-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RawPath,
        [Parameter(Mandatory)] [string] $RunPath,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $runLeaf = Split-Path ([IO.Path]::GetFullPath($RunPath)) -Leaf
    if ($runLeaf -cnotmatch '^([A-Za-z0-9][A-Za-z0-9._-]{0,127})\.run\.json$') {
        throw [ArgumentException]::new('M0E018 RunPath leaf must be RunId.run.json.')
    }
    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $Matches[1] -RawPath $RawPath -RunPath $RunPath
    $rootLock = Open-M0EvidenceRootReadLock -Paths $paths
    try {
        return Read-M0EvidenceCommittedRunNoLock -Paths $paths -SchemaRoot $SchemaRoot
    }
    finally {
        $rootLock.Dispose()
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
    $schedule = $json | ConvertFrom-Json -Depth 100 -DateKind String
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

function Get-M0EvidenceSnapshotSetPaths {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetId
    )

    if ($SnapshotSetId -notmatch '^[a-z0-9][a-z0-9._-]{0,127}$') {
        throw [ArgumentException]::new("M0E026 snapshot-set identifier is not canonical lowercase: '$SnapshotSetId'.")
    }
    $root = [IO.Path]::GetFullPath($EvidenceRoot)
    return [pscustomobject][ordered]@{
        set_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$SnapshotSetId.snapshot-set.json") -MayNotExist
        sidecar_path = Assert-M0EvidenceSafePath -LiteralPath (Join-Path $root "$SnapshotSetId.snapshot-set.sha256") -MayNotExist
    }
}

function ConvertTo-M0EvidenceNormalizedCommonInputs {
    param(
        [Parameter(Mandatory)] $CommonInputs,
        [string] $Code = 'M0E016',
        [string] $Name = 'common inputs'
    )

    $required = @('protocol_sha256', 'environment_sha256', 'fixture_sha256', 'harness_sha256', 'scenario_sha256')
    $names = if ($CommonInputs -is [Collections.IDictionary]) {
        @($CommonInputs.Keys | ForEach-Object { [string]$_ })
    }
    else {
        @($CommonInputs.PSObject.Properties.Name)
    }
    if ((ConvertTo-M0EvidenceJsonLine -Value @($names | Sort-Object)) -cne
        (ConvertTo-M0EvidenceJsonLine -Value @($required | Sort-Object))) {
        throw [IO.InvalidDataException]::new("$Code $Name must contain exactly the five common SHA-256 fields.")
    }
    $normalized = [ordered]@{}
    foreach ($field in $required) {
        $value = if ($CommonInputs -is [Collections.IDictionary]) {
            [string]$CommonInputs[$field]
        }
        else {
            [string]$CommonInputs.$field
        }
        if ($value -notmatch '^[A-Fa-f0-9]{64}$') {
            throw [IO.InvalidDataException]::new("$Code $Name '$field' is not a SHA-256 value.")
        }
        $normalized[$field] = $value.ToLowerInvariant()
    }
    return $normalized
}

function Assert-M0EvidenceSnapshotSetInventoryNoLock {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string[]] $RunIds,
        [Parameter(Mandatory)] $Schedule,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    if ($RunIds.Count -ne 44) {
        throw [IO.InvalidDataException]::new("M0E026 snapshot set requires exactly 44 RunIds; got $($RunIds.Count).")
    }
    $seenRunIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $records = [Collections.Generic.List[object]]::new()
    $firstRun = $null
    $firstCommonInputs = $null
    for ($index = 0; $index -lt $RunIds.Count; $index++) {
        $runId = [string]$RunIds[$index]
        if ($runId -notmatch '^run-[AB]-[a-z0-9][a-z0-9._-]{0,121}$' -or -not $seenRunIds.Add($runId)) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot-set RunId '$runId' is duplicate, case-spoofed, path-like or not an A/B run identifier.")
        }
        $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $runId
        $validated = Read-M0EvidenceCommittedRunNoLock -Paths $paths -SchemaRoot $SchemaRoot
        if ($validated.evidence_role -cne 'candidate_run' -or -not [bool]$validated.decision_eligible -or
            $validated.run_record.candidate_code -cnotin @('A', 'B')) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot set rejects non-decisive or synthetic run '$runId'.")
        }
        $run = $validated.run_record
        if ($run.run_id -cne $runId) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot-set RunId '$runId' differs from committed evidence identity.")
        }
        $normalizedCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $run.common_inputs -Code 'M0E026' -Name 'snapshot-set common inputs'
        if ($null -eq $firstRun) {
            $firstRun = $run
            $firstCommonInputs = $normalizedCommonInputs
        }
        if ($run.experiment_id -cne $firstRun.experiment_id -or $run.scenario_id -cne $firstRun.scenario_id -or
            (ConvertTo-M0EvidenceJsonLine -Value $normalizedCommonInputs) -cne (ConvertTo-M0EvidenceJsonLine -Value $firstCommonInputs)) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot set mixes experiment, scenario or common-input identity at '$runId'.")
        }
        $ordinal = $index + 1
        $blockIndex = [int][Math]::Floor($index / 2)
        $positionIndex = $index % 2
        $block = $Schedule.blocks[$blockIndex]
        $expectedCandidate = [string]$block.candidate_order[$positionIndex]
        if ($run.candidate_code -cne $expectedCandidate -or [int]$run.attempt -ne [int]$block.block -or
            [int]$run.schedule.seed -ne [int]$Schedule.seed -or [int]$run.schedule.block -ne [int]$block.block -or
            $run.schedule.block_order -cne $block.order -or [int]$run.schedule.position_in_block -ne ($positionIndex + 1)) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot-set ordinal $ordinal differs from paired schedule candidate/block/order/position.")
        }
        $records.Add([pscustomobject][ordered]@{
            paths = $paths
            validated = $validated
            run = $run
            snapshot_sha256 = [string]$validated.snapshot_sha256
        })
    }
    return @($records)
}

function Assert-M0EvidenceNoUnlistedDecisiveSnapshotsNoLock {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [Collections.Generic.HashSet[string]] $ListedRunIds,
        [Parameter(Mandatory)] $ReferenceRun,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $referenceCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $ReferenceRun.common_inputs -Code 'M0E026' -Name 'reference common inputs'
    foreach ($snapshotItem in @(Get-ChildItem -LiteralPath $EvidenceRoot -File -Filter '*.snapshot.json' | Sort-Object Name)) {
        $suffix = '.snapshot.json'
        $runId = $snapshotItem.Name.Substring(0, $snapshotItem.Name.Length - $suffix.Length)
        if ($ListedRunIds.Contains($runId)) { continue }
        if ($runId -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
            throw [IO.InvalidDataException]::new("M0E026 unlisted snapshot leaf '$($snapshotItem.Name)' has an unsafe RunId.")
        }
        $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $runId
        $validated = Read-M0EvidenceCommittedRunNoLock -Paths $paths -SchemaRoot $SchemaRoot
        $run = $validated.run_record
        $runCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $run.common_inputs -Code 'M0E026' -Name 'unlisted run common inputs'
        $sameDecisionDomain = $run.experiment_id -ceq $ReferenceRun.experiment_id -and
            $run.scenario_id -ceq $ReferenceRun.scenario_id -and
            (ConvertTo-M0EvidenceJsonLine -Value $runCommonInputs) -ceq (ConvertTo-M0EvidenceJsonLine -Value $referenceCommonInputs)
        if ($sameDecisionDomain -and $validated.evidence_role -ceq 'candidate_run' -and [bool]$validated.decision_eligible) {
            throw [IO.InvalidDataException]::new("M0E026 unlisted decision-eligible snapshot '$runId' exists in the same evidence domain.")
        }
    }
}

function New-M0EvidenceSnapshotSet {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetId,
        [Parameter(Mandatory)] [string[]] $RunIds,
        [Parameter(Mandatory)] [string] $SchedulePath,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [AllowEmptyString()] [ValidateSet('', 'snapshot_set_published')] [string] $CrashAfterBoundary = ''
    )

    $outputPaths = Get-M0EvidenceSnapshotSetPaths -EvidenceRoot $EvidenceRoot -SnapshotSetId $SnapshotSetId
    $lockPaths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId 'snapshot-set-lock'
    $rootLock = Open-M0EvidenceRootLock -Paths $lockPaths
    try {
        $schedule = Read-M0EvidenceSchedule -SchedulePath $SchedulePath -SchemaRoot $SchemaRoot
        $statistics = Assert-M0StatisticsArtifacts -ProtocolRoot ([IO.Path]::GetFullPath((Split-Path $SchemaRoot -Parent)))
        $records = @(Assert-M0EvidenceSnapshotSetInventoryNoLock -EvidenceRoot $lockPaths.root -RunIds $RunIds -Schedule $schedule -SchemaRoot $SchemaRoot)
        $listed = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($runId in $RunIds) { $null = $listed.Add($runId) }
        Assert-M0EvidenceNoUnlistedDecisiveSnapshotsNoLock -EvidenceRoot $lockPaths.root -ListedRunIds $listed -ReferenceRun $records[0].run -SchemaRoot $SchemaRoot

        $entries = for ($index = 0; $index -lt $records.Count; $index++) {
            [ordered]@{
                ordinal = $index + 1
                run_id = [string]$RunIds[$index]
                snapshot_relative_path = Split-Path $records[$index].paths.snapshot_path -Leaf
                snapshot_sha256 = [string]$records[$index].snapshot_sha256
            }
        }
        $document = [ordered]@{
            schema_id = 'm0.evidence-snapshot-set.v1'
            schema_version = 1
            snapshot_set_id = $SnapshotSetId
            hash_algorithm = 'sha256'
            experiment_id = [string]$records[0].run.experiment_id
            scenario_id = [string]$records[0].run.scenario_id
            common_inputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $records[0].run.common_inputs -Code 'M0E026' -Name 'snapshot-set common inputs'
            schedule_sha256 = [string]$statistics.schedule_sha256
            entry_count = 44
            entries = @($entries)
        }
        Assert-M0EvidenceRedactedValue -Value $document
        $json = Assert-M0EvidenceJsonSchema -Value $document -SchemaPath (Join-Path $SchemaRoot 'evidence-snapshot-set.schema.json') -Code 'M0E026'
        $setBytes = $script:M0EvidenceUtf8.GetBytes($json + "`n")
        $setHash = Get-M0EvidenceSha256Bytes -Bytes $setBytes
        $sidecarBytes = $script:M0EvidenceUtf8.GetBytes("$setHash  $(Split-Path $outputPaths.set_path -Leaf)`n")

        if (Test-Path -LiteralPath $outputPaths.sidecar_path) {
            throw [IO.IOException]::new('M0E026 snapshot-set commit sidecar already exists; overwrite is forbidden.')
        }
        if (Test-Path -LiteralPath $outputPaths.set_path) {
            $existingSetBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $outputPaths.set_path
            if (-not (Test-M0EvidenceByteEquality -Left $existingSetBytes -Right $setBytes)) {
                throw [IO.InvalidDataException]::new('M0E026 existing uncommitted snapshot set diverges; overwrite is forbidden.')
            }
        }
        else {
            $null = Write-M0EvidenceCreateNewFile -LiteralPath $outputPaths.set_path -Bytes $setBytes
        }
        Invoke-M0EvidenceCrashInjection -RequestedBoundary $CrashAfterBoundary -ActualBoundary 'snapshot_set_published'
        $null = Write-M0EvidenceCreateNewFile -LiteralPath $outputPaths.sidecar_path -Bytes $sidecarBytes
        return [pscustomobject][ordered]@{
            snapshot_set_path = $outputPaths.set_path
            sidecar_path = $outputPaths.sidecar_path
            snapshot_set_sha256 = $setHash
            entry_count = 44
        }
    }
    finally {
        $rootLock.Dispose()
    }
}

function ConvertTo-M0EvidenceExpectedCommonInputs {
    param([Parameter(Mandatory)] $ExpectedCommonInputs)

    return ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $ExpectedCommonInputs -Code 'M0E016' -Name 'ExpectedCommonInputs'
}

function Get-M0EvidenceCanonicalDocumentHash {
    param([Parameter(Mandatory)] $Value)

    $json = ConvertTo-M0EvidenceJsonLine -Value $Value
    return Get-M0EvidenceSha256Text -Value ($json + "`n")
}

function Get-M0EvidenceCandidateDigestsFromRecords {
    param(
        [Parameter(Mandatory)] [object[]] $Records,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [switch] $RequireClean
    )

    if ($Records.Count -eq 0) {
        throw [IO.InvalidDataException]::new("M0E016 candidate '$CandidateCode' has no canonical run records.")
    }
    $seenAttempts = [Collections.Generic.HashSet[int]]::new()
    foreach ($record in $Records) {
        $run = $record.run
        if ($run.candidate_code -cne $CandidateCode -or -not $seenAttempts.Add([int]$run.attempt)) {
            throw [IO.InvalidDataException]::new("M0E016 candidate '$CandidateCode' digest received a foreign or duplicate attempt.")
        }
    }
    $orderedRecords = @($Records | Sort-Object { [int]$_.run.attempt })
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
                run_record_sha256 = $_.run_record_sha256
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

function Get-M0EvidenceCandidateDigests {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetPath,
        [Parameter(Mandatory)] [ValidateSet('A', 'B')] [string] $CandidateCode,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [switch] $RequireClean
    )

    $root = [IO.Path]::GetFullPath($EvidenceRoot)
    $lockPaths = Get-M0EvidenceTransactionPaths -EvidenceRoot $root -RunId 'snapshot-set-lock'
    $rootLock = Open-M0EvidenceRootReadLock -Paths $lockPaths
    try {
        $setResult = Read-M0EvidenceSnapshotSetNoLock -EvidenceRoot $root -SnapshotSetPath $SnapshotSetPath -SchemaRoot $SchemaRoot
        $schedule = Read-M0EvidenceSchedule -SchedulePath (Join-Path (Split-Path $SchemaRoot -Parent) 'schedule.v1.json') -SchemaRoot $SchemaRoot
        $runIds = @($setResult.document.entries | ForEach-Object { [string]$_.run_id })
        $allRecords = @(Assert-M0EvidenceSnapshotSetInventoryNoLock -EvidenceRoot $root -RunIds $runIds -Schedule $schedule -SchemaRoot $SchemaRoot)
        for ($index = 0; $index -lt $allRecords.Count; $index++) {
            if ($allRecords[$index].snapshot_sha256 -cne [string]$setResult.document.entries[$index].snapshot_sha256) {
                throw [IO.InvalidDataException]::new("M0E026 snapshot-set hash differs from committed run '$($runIds[$index])'.")
            }
        }
        $listed = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($runId in $runIds) { $null = $listed.Add($runId) }
        Assert-M0EvidenceNoUnlistedDecisiveSnapshotsNoLock -EvidenceRoot $root -ListedRunIds $listed -ReferenceRun $allRecords[0].run -SchemaRoot $SchemaRoot
        $records = @($allRecords | Where-Object { $_.run.candidate_code -ceq $CandidateCode } | ForEach-Object {
            [pscustomobject]@{
                path = $_.paths.run_path
                run = $_.run
                run_record_sha256 = $_.validated.run_record_sha256
            }
        })
        return Get-M0EvidenceCandidateDigestsFromRecords -Records $records -CandidateCode $CandidateCode -RequireClean:$RequireClean
    }
    finally {
        $rootLock.Dispose()
    }
}

function Read-M0EvidenceSnapshotSetNoLock {
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $SnapshotSetPath,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $root = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/')
    $setPath = Assert-M0EvidenceSafePath -LiteralPath $SnapshotSetPath
    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not (Split-Path $setPath -Parent).Equals($root, $comparison) -or
        (Split-Path $setPath -Leaf) -cnotmatch '^[a-z0-9][a-z0-9._-]{0,127}\.snapshot-set\.json$') {
        throw [IO.InvalidDataException]::new('M0E026 SnapshotSetPath must be a canonical lowercase snapshot-set leaf directly inside EvidenceRoot.')
    }
    $sidecarPath = Assert-M0EvidenceSafePath -LiteralPath ($setPath.Substring(0, $setPath.Length - '.json'.Length) + '.sha256')
    $setStream = $null
    $sidecarStream = $null
    try {
        $setStream = Open-M0EvidenceReadHandle -LiteralPath $setPath
        $sidecarStream = Open-M0EvidenceReadHandle -LiteralPath $sidecarPath
        $setBytes = Read-M0EvidenceStreamBytes -Stream $setStream
        $sidecarBytes = Read-M0EvidenceStreamBytes -Stream $sidecarStream
        $null = Assert-M0EvidenceHandleIdentity -Stream $setStream -ExpectedPath $setPath -InitialFingerprint $setStream.M0EvidenceFingerprint
        $null = Assert-M0EvidenceHandleIdentity -Stream $sidecarStream -ExpectedPath $sidecarPath -InitialFingerprint $sidecarStream.M0EvidenceFingerprint
    }
    finally {
        if ($null -ne $sidecarStream) { $sidecarStream.Dispose() }
        if ($null -ne $setStream) { $setStream.Dispose() }
    }
    $setHash = Get-M0EvidenceSha256Bytes -Bytes $setBytes
    $expectedSidecar = $script:M0EvidenceUtf8.GetBytes("$setHash  $(Split-Path $setPath -Leaf)`n")
    if (-not (Test-M0EvidenceByteEquality -Left $sidecarBytes -Right $expectedSidecar)) {
        throw [IO.InvalidDataException]::new('M0E026 snapshot-set sidecar is missing, noncanonical or differs from the exact set bytes.')
    }
    $set = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $setBytes -SchemaPath (Join-Path $SchemaRoot 'evidence-snapshot-set.schema.json') -Name 'snapshot set'
    $seenIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    for ($index = 0; $index -lt $set.entries.Count; $index++) {
        $entry = $set.entries[$index]
        if ([int]$entry.ordinal -ne ($index + 1) -or -not $seenIds.Add([string]$entry.run_id) -or
            -not $seenPaths.Add([string]$entry.snapshot_relative_path) -or
            [string]$entry.snapshot_relative_path -cne "$($entry.run_id).snapshot.json") {
            throw [IO.InvalidDataException]::new("M0E026 snapshot-set entry ordinal/RunId/path identity differs at position $($index + 1).")
        }
    }
    return [pscustomobject][ordered]@{
        path = $setPath
        sidecar_path = $sidecarPath
        bytes = $setBytes
        sha256 = $setHash
        document = $set
    }
}

function Assert-M0EvidenceBatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $BatchPath,
        [Parameter(Mandatory)] [string] $SnapshotSetPath,
        [Parameter(Mandatory)] [string] $SchedulePath,
        [Parameter(Mandatory)] $ExpectedCommonInputs,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $root = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd('\', '/')
    $batchFullPath = Assert-M0EvidenceSafePath -LiteralPath $BatchPath
    $pathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    if (-not (Split-Path $batchFullPath -Parent).Equals($root, $pathComparison)) {
        throw [IO.InvalidDataException]::new('M0E018 BatchPath must be a direct EvidenceRoot leaf.')
    }
    $lockPaths = Get-M0EvidenceTransactionPaths -EvidenceRoot $root -RunId 'snapshot-set-lock'
    $rootLock = Open-M0EvidenceRootReadLock -Paths $lockPaths
    try {
    $schedule = Read-M0EvidenceSchedule -SchedulePath $SchedulePath -SchemaRoot $SchemaRoot
    $expectedInputs = ConvertTo-M0EvidenceExpectedCommonInputs -ExpectedCommonInputs $ExpectedCommonInputs
    $snapshotSetResult = Read-M0EvidenceSnapshotSetNoLock -EvidenceRoot $root -SnapshotSetPath $SnapshotSetPath -SchemaRoot $SchemaRoot
    $snapshotSet = $snapshotSetResult.document
    $statistics = Assert-M0StatisticsArtifacts -ProtocolRoot ([IO.Path]::GetFullPath((Split-Path $SchemaRoot -Parent)))
    if ($snapshotSet.schedule_sha256 -cne $statistics.schedule_sha256) {
        throw [IO.InvalidDataException]::new('M0E026 snapshot-set schedule_sha256 differs from the anchored schedule sidecar.')
    }
    $batchBytes = Read-M0EvidenceLockedFileBytes -LiteralPath $batchFullPath
    $batch = ConvertFrom-M0EvidenceCanonicalDocumentBytes -Bytes $batchBytes -SchemaPath (Join-Path $SchemaRoot 'batch.schema.json') -Name 'batch record'
    $batchCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $batch.common_inputs -Code 'M0E016' -Name 'batch common inputs'
    $snapshotSetCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $snapshotSet.common_inputs -Code 'M0E026' -Name 'snapshot-set common inputs'
    if ($batch.snapshot_set_sha256.ToLowerInvariant() -cne $snapshotSetResult.sha256 -or
        $batch.experiment_id -cne $snapshotSet.experiment_id -or $batch.scenario_id -cne $snapshotSet.scenario_id -or
        (ConvertTo-M0EvidenceJsonLine -Value $batchCommonInputs) -cne (ConvertTo-M0EvidenceJsonLine -Value $snapshotSetCommonInputs)) {
        throw [IO.InvalidDataException]::new('M0E026 batch identity or snapshot_set_sha256 differs from the committed snapshot set.')
    }
    foreach ($field in $expectedInputs.Keys) {
        if ($batchCommonInputs[$field] -cne $expectedInputs[$field]) {
            throw [IO.InvalidDataException]::new("M0E016 batch common input '$field' differs from ExpectedCommonInputs.")
        }
    }
    if ($batch.candidate_results.Count -ne 2 -or $batch.candidate_results[0].candidate_code -cne 'A' -or
        $batch.candidate_results[1].candidate_code -cne 'B' -or
        $batch.candidate_results[0].artifact_sha256.ToLowerInvariant() -ceq $batch.candidate_results[1].artifact_sha256.ToLowerInvariant()) {
        throw [IO.InvalidDataException]::new('M0E010 batch requires A/B with distinct candidate artifacts.')
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
    $candidateRunRecords = @{
        A = [Collections.Generic.List[object]]::new()
        B = [Collections.Generic.List[object]]::new()
    }
    $observedStarts = @{ A = @{}; B = @{} }
    $batchQpcFrequency = $null
    foreach ($snapshotEntry in $snapshotSet.entries) {
        $runId = [string]$snapshotEntry.run_id
        $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $root -RunId $runId
        $validatedRun = Read-M0EvidenceCommittedRunNoLock -Paths $paths -SchemaRoot $SchemaRoot
        if ($validatedRun.snapshot_sha256 -cne [string]$snapshotEntry.snapshot_sha256) {
            throw [IO.InvalidDataException]::new("M0E026 snapshot-set hash differs from committed snapshot '$runId'.")
        }
        if (-not [bool]$validatedRun.decision_eligible -or $validatedRun.evidence_role -cne 'candidate_run') {
            throw [IO.InvalidDataException]::new("M0E022 batch rejects non-decisive evidence role '$($validatedRun.evidence_role)'.")
        }
        $run = $validatedRun.run_record
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
        $runCommonInputs = ConvertTo-M0EvidenceNormalizedCommonInputs -CommonInputs $run.common_inputs -Code 'M0E016' -Name "run '$runId' common inputs"
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
            if ($runCommonInputs[$field] -cne $expectedInputs[$field]) {
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
        $candidateRunRecords[$run.candidate_code].Add([pscustomobject]@{
            path = $paths.run_path
            run = $run
            run_record_sha256 = $validatedRun.run_record_sha256
        })
        $observedStatuses[$run.candidate_code][$run.status] = [int]$observedStatuses[$run.candidate_code][$run.status] + 1
    }
    $referenceRun = if ($candidateRunRecords.A.Count -gt 0) { $candidateRunRecords.A[0].run } else { $candidateRunRecords.B[0].run }
    Assert-M0EvidenceNoUnlistedDecisiveSnapshotsNoLock -EvidenceRoot $root -ListedRunIds $seenRunIds -ReferenceRun $referenceRun -SchemaRoot $SchemaRoot
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
        $digests = Get-M0EvidenceCandidateDigestsFromRecords -Records @($candidateRunRecords[$candidateCode]) -CandidateCode $candidateCode -RequireClean
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
        snapshot_set_sha256 = $snapshotSetResult.sha256
    }
    }
    finally {
        $rootLock.Dispose()
    }
}

function Get-M0EvidenceRecoveryState {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $RunId
    if (-not (Test-Path -LiteralPath $paths.lock_path -PathType Leaf)) {
        $rootItems = @(Get-ChildItem -LiteralPath $paths.root -Force)
        if ($rootItems.Count -eq 0) {
            return New-M0EvidenceRecoveryStateResult -State unstarted -Paths $paths -RunId $RunId
        }
        return New-M0EvidenceRecoveryStateResult -State conflict -Paths $paths -RunId $RunId -ReasonCode lock_missing
    }
    try {
        $rootLock = Open-M0EvidenceRootReadLock -Paths $paths
    }
    catch [IO.IOException] {
        return New-M0EvidenceRecoveryStateResult -State conflict -Paths $paths -RunId $RunId -ReasonCode writer_locked
    }
    try {
        return Get-M0EvidenceRecoveryStateNoLock -Paths $paths -RunId $RunId -SchemaRoot $SchemaRoot
    }
    finally {
        $rootLock.Dispose()
    }
}

function Resume-M0EvidenceRun {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot,
        [AllowEmptyString()] [ValidateSet('', 'raw_sealed', 'run_temp_flushed', 'run_published', 'snapshot_temp_flushed', 'committed')] [string] $CrashAfterBoundary = ''
    )

    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $RunId
    $rootLock = Open-M0EvidenceRootLock -Paths $paths
    try {
        return Invoke-M0EvidenceResumeNoLock -Paths $paths -RunId $RunId -SchemaRoot $SchemaRoot -CrashAfterBoundary $CrashAfterBoundary
    }
    finally {
        $rootLock.Dispose()
    }
}

function Assert-M0EvidenceSnapshot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $EvidenceRoot,
        [Parameter(Mandatory)] [string] $RunId,
        [Parameter(Mandatory)] [string] $SchemaRoot
    )

    $paths = Get-M0EvidenceTransactionPaths -EvidenceRoot $EvidenceRoot -RunId $RunId
    $rootLock = Open-M0EvidenceRootReadLock -Paths $paths
    try {
        $validated = Read-M0EvidenceCommittedRunNoLock -Paths $paths -SchemaRoot $SchemaRoot
        return $validated.snapshot_result
    }
    finally {
        $rootLock.Dispose()
    }
}

Export-ModuleMember -Function @(
    'Open-M0EvidenceWriter',
    'Close-M0EvidenceWriter',
    'Add-M0EvidenceEvent',
    'Complete-M0EvidenceRun',
    'Assert-M0EvidenceRun',
    'New-M0EvidenceSnapshotSet',
    'Assert-M0EvidenceBatch',
    'Get-M0EvidenceCandidateDigests',
    'ConvertTo-M0EvidenceRedactedText',
    'Get-M0EvidenceRecoveryState',
    'Resume-M0EvidenceRun',
    'Assert-M0EvidenceSnapshot'
)
