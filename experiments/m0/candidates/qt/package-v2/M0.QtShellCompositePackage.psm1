#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:PackageId = 'm0.2-qt-shell-composite-package-v2'
$script:WindowsSdkVersion = '10.0.26100.0'
$script:MsvcCompilerVersion = '19.44.35228'
$script:QtConfigMsvcVersion = '19.44.35228'

if ($IsWindows) {
    $nativeNamespace = 'M0QtShellPackageVerifier_' + [Guid]::NewGuid().ToString('N')
    $nativeDefinition = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace __M0_PACKAGE_VERIFIER_NAMESPACE__ {
    [StructLayout(LayoutKind.Sequential)]
    public struct BY_HANDLE_FILE_INFORMATION {
        public uint FileAttributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    internal struct WIN32_FIND_STREAM_DATA {
        public long StreamSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)]
        public string StreamName;
    }

    internal sealed class SafeFindHandle : SafeHandleZeroOrMinusOneIsInvalid {
        private SafeFindHandle() : base(true) { }
        protected override bool ReleaseHandle() { return NativeFile.FindClose(handle); }
    }

    public static class NativeFile {
        private const uint GENERIC_READ = 0x80000000;
        private const uint FILE_LIST_DIRECTORY = 0x00000001;
        private const uint FILE_READ_ATTRIBUTES = 0x00000080;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        private const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
        private const int ERROR_HANDLE_EOF = 38;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes,
            IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle, out BY_HANDLE_FILE_INFORMATION information);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFindHandle FindFirstStreamW(string fileName, int infoLevel,
            out WIN32_FIND_STREAM_DATA findStreamData, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextStreamW(SafeFindHandle findHandle,
            out WIN32_FIND_STREAM_DATA findStreamData);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool FindClose(IntPtr findHandle);

        public static SafeFileHandle OpenFileLease(string path) {
            var handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, IntPtr.Zero,
                OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryLease(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        private static BY_HANDLE_FILE_INFORMATION GetInformation(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION information;
            if (!GetFileInformationByHandle(handle, out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return information;
        }

        public static uint GetAttributes(SafeFileHandle handle) {
            return GetInformation(handle).FileAttributes;
        }

        public static uint GetLinkCount(SafeFileHandle handle) {
            return GetInformation(handle).NumberOfLinks;
        }

        public static string GetIdentity(SafeFileHandle handle) {
            var information = GetInformation(handle);
            return information.VolumeSerialNumber.ToString("X8") + ":" +
                information.FileIndexHigh.ToString("X8") +
                information.FileIndexLow.ToString("X8");
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return buffer.ToString();
        }

        public static string[] GetStreamNames(string path) {
            WIN32_FIND_STREAM_DATA data;
            using (var findHandle = FindFirstStreamW(path, 0, out data, 0)) {
                if (findHandle.IsInvalid) {
                    int error = Marshal.GetLastWin32Error();
                    if (error == ERROR_HANDLE_EOF) { return new string[0]; }
                    throw new Win32Exception(error);
                }
                var names = new List<string>();
                names.Add(data.StreamName);
                while (FindNextStreamW(findHandle, out data)) { names.Add(data.StreamName); }
                int finalError = Marshal.GetLastWin32Error();
                if (finalError != ERROR_HANDLE_EOF) { throw new Win32Exception(finalError); }
                return names.ToArray();
            }
        }
    }

    public sealed class BoundedCaptureResult {
        public byte[] Bytes { get; private set; }
        public bool Exceeded { get; private set; }
        public long TotalBytes { get; private set; }

        internal BoundedCaptureResult(byte[] bytes, bool exceeded, long totalBytes) {
            Bytes = bytes;
            Exceeded = exceeded;
            TotalBytes = totalBytes;
        }
    }

    public static class ProcessIo {
        public static async Task<BoundedCaptureResult> ReadBoundedAndDrainAsync(
            Stream source, int maximumBytes) {
            if (source == null) { throw new ArgumentNullException(nameof(source)); }
            if (maximumBytes <= 0) { throw new ArgumentOutOfRangeException(nameof(maximumBytes)); }
            byte[] buffer = new byte[65536];
            long totalBytes = 0;
            bool exceeded = false;
            using (var retained = new MemoryStream(Math.Min(maximumBytes, buffer.Length))) {
                while (true) {
                    int count = await source.ReadAsync(buffer, 0, buffer.Length).ConfigureAwait(false);
                    if (count == 0) { break; }
                    totalBytes = checked(totalBytes + count);
                    int remaining = maximumBytes - checked((int)retained.Length);
                    if (remaining > 0) {
                        retained.Write(buffer, 0, Math.Min(remaining, count));
                    }
                    if (count > remaining) { exceeded = true; }
                }
                return new BoundedCaptureResult(retained.ToArray(), exceeded, totalBytes);
            }
        }
    }
}
'@.Replace('__M0_PACKAGE_VERIFIER_NAMESPACE__', $nativeNamespace)
    $nativeTypes = @(Add-Type -TypeDefinition $nativeDefinition -PassThru)
    $nativeMatches = @($nativeTypes | Where-Object FullName -CEQ "$nativeNamespace.NativeFile")
    $processIoMatches = @($nativeTypes | Where-Object FullName -CEQ "$nativeNamespace.ProcessIo")
    if ($nativeMatches.Count -ne 1 -or $processIoMatches.Count -ne 1) {
        throw 'Package verifier native file type compilation returned an unexpected type set.'
    }
    $script:PackageVerifierNative = [type]$nativeMatches[0]
    $script:PackageProcessIo = [type]$processIoMatches[0]
}
else {
    $script:PackageVerifierNative = $null
    $script:PackageProcessIo = $null
}

function Assert-M0PackageV2Sha256 {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context must be one lowercase SHA-256."
    }
}

function Assert-M0PackageV2Fields {
    param(
        $Value,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $Context
    )
    if ($Value -isnot [pscustomobject]) { throw "$Context must be one JSON object." }
    $actual = [string[]]@($Value.PSObject.Properties.Name)
    $wanted = [string[]]@($Expected)
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [Array]::Sort($wanted, [StringComparer]::Ordinal)
    if (($actual -join "`n") -cne ($wanted -join "`n")) {
        throw "$Context has an open, incomplete, or renamed shape."
    }
}

function Assert-M0PackageV2RequiredFields {
    param($Value, [string[]] $Required, [string] $Context)
    if ($Value -isnot [pscustomobject]) { throw "$Context must be one JSON object." }
    foreach ($name in $Required) {
        if ($null -eq $Value.PSObject.Properties[$name]) { throw "$Context is missing '$name'." }
    }
}

function Assert-M0PackageV2NoDuplicateJsonProperties {
    param([Parameter(Mandatory)] [Text.Json.JsonElement] $Element, [string] $Context = '$')
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw "Duplicate JSON property '$($property.Name)' at $Context."
            }
            Assert-M0PackageV2NoDuplicateJsonProperties $property.Value "$Context.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($item in $Element.EnumerateArray()) {
            Assert-M0PackageV2NoDuplicateJsonProperties $item "$Context[$index]"
            $index++
        }
    }
}

function ConvertTo-M0PackageV2FullPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if ([string]::IsNullOrWhiteSpace($LiteralPath)) { throw 'A filesystem path must not be empty.' }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $root = [IO.Path]::GetPathRoot($full)
    if (-not [string]::Equals($full, $root, [StringComparison]::OrdinalIgnoreCase)) {
        $full = [IO.Path]::TrimEndingDirectorySeparator($full)
    }
    return $full
}

function ConvertTo-M0PackageV2ExtendedPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $full.Substring(2) }
    return '\\?\' + $full
}

function ConvertFrom-M0PackageV2FinalPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if ($LiteralPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $LiteralPath.Substring(8)
    }
    if ($LiteralPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $LiteralPath.Substring(4)
    }
    return $LiteralPath
}

function Test-M0PackageV2PathWithin {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Boundary,
        [switch] $AllowEqual
    )
    $candidateFull = ConvertTo-M0PackageV2FullPath $Candidate
    $boundaryFull = ConvertTo-M0PackageV2FullPath $Boundary
    if ($AllowEqual -and
        [string]::Equals($candidateFull, $boundaryFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    $prefix = if ($boundaryFull.EndsWith([IO.Path]::DirectorySeparatorChar)) {
        $boundaryFull
    }
    else { $boundaryFull + [IO.Path]::DirectorySeparatorChar }
    return $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-M0PackageV2SafeRelativePath {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value) -or
        [IO.Path]::IsPathRooted($Value) -or $Value.StartsWith('/') -or
        $Value.Contains([IO.Path]::DirectorySeparatorChar) -or
        $Value.Contains(':') -or $Value.Contains([char]0)) {
        throw "$Context has an unsafe relative path."
    }
    $segments = @($Value.Split('/'))
    foreach ($segment in $segments) {
        if ($segment -in @('', '.', '..') -or
            $segment.EndsWith(' ', [StringComparison]::Ordinal) -or
            $segment.EndsWith('.', [StringComparison]::Ordinal) -or
            $segment.IndexOfAny([char[]]'<>"|?*') -ge 0 -or
            @($segment.ToCharArray() | Where-Object { [int]$_ -lt 32 }).Count -ne 0) {
            throw "$Context has an unsafe relative path segment."
        }
        $deviceStem = ($segment -split '\.', 2)[0]
        if ($deviceStem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            throw "$Context has an unsafe reserved relative path segment."
        }
    }
}

function Resolve-M0PackageV2ContainedPath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0PackageV2SafeRelativePath $RelativePath $Context
    $rootFull = ConvertTo-M0PackageV2FullPath $Root
    $candidate = [IO.Path]::GetFullPath((Join-Path $rootFull (
                $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
    if (-not (Test-M0PackageV2PathWithin $candidate $rootFull)) {
        throw "$Context escaped its boundary root."
    }
    return $candidate
}

function Assert-M0PackageV2NoReparseChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $boundary = ConvertTo-M0PackageV2FullPath $BoundaryRoot
    $current = ConvertTo-M0PackageV2FullPath $LiteralPath
    if (-not (Test-M0PackageV2PathWithin $current $boundary -AllowEqual)) {
        throw "$Context is outside its physical boundary."
    }
    while ($true) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse point: $current"
        }
        $normalized = ConvertTo-M0PackageV2FullPath $item.FullName
        if ([string]::Equals($normalized, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($normalized)
        if ([string]::IsNullOrEmpty($parent) -or
            -not (Test-M0PackageV2PathWithin $parent $boundary -AllowEqual)) {
            throw "$Context physical ancestry escaped its boundary."
        }
        $current = $parent
    }
}

function Assert-M0PackageV2NoAlternateDataStreams {
    param([string] $LiteralPath, [string] $Context)
    $streams = @($script:PackageVerifierNative::GetStreamNames(
            (ConvertTo-M0PackageV2ExtendedPath $LiteralPath)))
    $isDirectory = [IO.Directory]::Exists([IO.Path]::GetFullPath($LiteralPath))
    $unexpected = @($streams | Where-Object { $_ -cne '::$DATA' })
    if ($unexpected.Count -ne 0 -or $streams.Count -gt 1 -or
        (-not $isDirectory -and ($streams.Count -ne 1 -or $streams[0] -cne '::$DATA'))) {
        throw "$Context contains an alternate data stream (ADS) or invalid unnamed stream."
    }
}

function Get-M0PackageV2Sha256FromStream {
    param([Parameter(Mandatory)] [IO.Stream] $Stream)
    $position = $Stream.Position
    try {
        $Stream.Position = 0
        return [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    }
    finally { $Stream.Position = $position }
}

function Open-M0PackageV2FileLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [long] $ExpectedSizeBytes = -1,
        [string] $ExpectedSha256,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $LeaseSink,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $FileSink
    )
    $full = ConvertTo-M0PackageV2FullPath $LiteralPath
    if (-not [IO.File]::Exists($full)) { throw "$Context file is missing." }
    Assert-M0PackageV2NoReparseChain $full $BoundaryRoot $Context
    $item = Get-Item -LiteralPath $full -Force
    if ($item -isnot [IO.FileInfo] -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "$Context must be an ordinary non-reparse file."
    }
    $handle = $null
    $stream = $null
    try {
        $handle = $script:PackageVerifierNative::OpenFileLease(
            (ConvertTo-M0PackageV2ExtendedPath $full))
        $attributes = [IO.FileAttributes]$script:PackageVerifierNative::GetAttributes($handle)
        if (($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "$Context opened as a directory or reparse point."
        }
        $final = ConvertTo-M0PackageV2FullPath (ConvertFrom-M0PackageV2FinalPath (
                $script:PackageVerifierNative::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolved through a physical alias."
        }
        $linkCount = [long]$script:PackageVerifierNative::GetLinkCount($handle)
        if ($linkCount -ne 1) { throw "$Context hardlink count is $linkCount; exactly one is required." }
        Assert-M0PackageV2NoAlternateDataStreams $full $Context
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::Read, 65536, $false)
        $handle = $null
        $hash = Get-M0PackageV2Sha256FromStream $stream
        if ($ExpectedSizeBytes -ge 0 -and $stream.Length -ne $ExpectedSizeBytes) {
            throw "$Context size drifted."
        }
        if (-not [string]::IsNullOrEmpty($ExpectedSha256)) {
            Assert-M0PackageV2Sha256 $ExpectedSha256 "$Context expected hash"
            if ($hash -cne $ExpectedSha256) { throw "$Context SHA-256 drifted." }
        }
        $lease = [pscustomobject][ordered]@{
            lease_kind = 'file'
            path = $full
            size_bytes = [long]$stream.Length
            sha256 = $hash
            identity = [string]$script:PackageVerifierNative::GetIdentity($stream.SafeFileHandle)
            link_count = $linkCount
            stream = $stream
        }
        $LeaseSink.Add($lease)
        $FileSink.Add($lease)
        return $lease
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        elseif ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Open-M0PackageV2DirectoryLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $LeaseSink,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $DirectorySink
    )
    $full = ConvertTo-M0PackageV2FullPath $LiteralPath
    if (-not [IO.Directory]::Exists($full)) { throw "$Context directory is missing." }
    Assert-M0PackageV2NoReparseChain $full $BoundaryRoot $Context
    Assert-M0PackageV2NoAlternateDataStreams $full $Context
    $handle = $null
    try {
        $handle = $script:PackageVerifierNative::OpenDirectoryLease(
            (ConvertTo-M0PackageV2ExtendedPath $full))
        $attributes = [IO.FileAttributes]$script:PackageVerifierNative::GetAttributes($handle)
        if (-not ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "$Context is not an ordinary anchored directory."
        }
        $final = ConvertTo-M0PackageV2FullPath (ConvertFrom-M0PackageV2FinalPath (
                $script:PackageVerifierNative::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolved through a physical alias."
        }
        $lease = [pscustomobject][ordered]@{
            lease_kind = 'directory'
            path = $full
            identity = [string]$script:PackageVerifierNative::GetIdentity($handle)
            handle = $handle
        }
        $LeaseSink.Add($lease)
        $DirectorySink.Add($lease)
        $handle = $null
        return $lease
    }
    finally { if ($null -ne $handle) { $handle.Dispose() } }
}

function Read-M0PackageV2JsonLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $LeaseSink,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $FileSink
    )
    Assert-M0PackageV2Sha256 $ExpectedSha256 "$Context external hash"
    $record = Open-M0PackageV2FileLease $LiteralPath $BoundaryRoot $Context -ExpectedSha256 $ExpectedSha256 `
        -LeaseSink $LeaseSink -FileSink $FileSink
    $memory = [IO.MemoryStream]::new()
    try {
        $record.stream.Position = 0
        $record.stream.CopyTo($memory)
        $bytes = $memory.ToArray()
    }
    finally { $memory.Dispose() }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf) {
        throw "$Context must be UTF-8 without BOM."
    }
    try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
    catch { throw [IO.InvalidDataException]::new("$Context is not strict UTF-8.", $_.Exception) }
    try { $document = [Text.Json.JsonDocument]::Parse($text) }
    catch { throw [IO.InvalidDataException]::new("$Context is not JSON.", $_.Exception) }
    try {
        if ($document.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
            throw "$Context must contain one JSON object."
        }
        Assert-M0PackageV2NoDuplicateJsonProperties $document.RootElement
    }
    finally { $document.Dispose() }
    return [pscustomobject][ordered]@{
        value = $text | ConvertFrom-Json -Depth 100 -DateKind String
        record = $record
    }
}

function Close-M0PackageV2Leases {
    param([Collections.Generic.List[object]] $Leases)
    for ($index = $Leases.Count - 1; $index -ge 0; $index--) {
        $lease = $Leases[$index]
        if ($lease.lease_kind -ceq 'file') {
            try { $lease.stream.Dispose() } catch { }
        }
        elseif ($lease.lease_kind -ceq 'directory') {
            try { $lease.handle.Dispose() } catch { }
        }
    }
}

function Assert-M0PackageV2FileLeasesUnchanged {
    param([Collections.Generic.List[object]] $Files, [string] $Context)
    foreach ($lease in $Files) {
        if ($lease.stream.SafeFileHandle.IsClosed -or $lease.stream.SafeFileHandle.IsInvalid -or
            $lease.stream.Length -ne $lease.size_bytes -or
            (Get-M0PackageV2Sha256FromStream $lease.stream) -cne $lease.sha256) {
            throw "$Context detected changed authenticated file bytes: $($lease.path)"
        }
        $attributes = [IO.FileAttributes]$script:PackageVerifierNative::GetAttributes(
            $lease.stream.SafeFileHandle)
        $final = ConvertTo-M0PackageV2FullPath (ConvertFrom-M0PackageV2FinalPath (
                $script:PackageVerifierNative::GetFinalPath($lease.stream.SafeFileHandle)))
        if (($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            [string]$script:PackageVerifierNative::GetIdentity($lease.stream.SafeFileHandle) -cne $lease.identity -or
            [long]$script:PackageVerifierNative::GetLinkCount($lease.stream.SafeFileHandle) -ne 1 -or
            -not [string]::Equals($final, $lease.path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context detected identity, reparse, or hardlink drift: $($lease.path)"
        }
        Assert-M0PackageV2NoAlternateDataStreams $lease.path "$Context authenticated file"
    }
}

function Assert-M0PackageV2DirectoryLeasesUnchanged {
    param([Collections.Generic.List[object]] $Directories, [string] $Context)
    foreach ($lease in $Directories) {
        if ($lease.handle.IsClosed -or $lease.handle.IsInvalid) {
            throw "$Context detected a closed directory lease: $($lease.path)"
        }
        $attributes = [IO.FileAttributes]$script:PackageVerifierNative::GetAttributes($lease.handle)
        $final = ConvertTo-M0PackageV2FullPath (ConvertFrom-M0PackageV2FinalPath (
                $script:PackageVerifierNative::GetFinalPath($lease.handle)))
        if (-not ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            [string]$script:PackageVerifierNative::GetIdentity($lease.handle) -cne $lease.identity -or
            -not [string]::Equals($final, $lease.path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context detected directory identity or reparse drift: $($lease.path)"
        }
        Assert-M0PackageV2NoAlternateDataStreams $lease.path "$Context authenticated directory"
    }
}

function Assert-M0PackageV2Pin {
    param($Value, [string] $Context)
    Assert-M0PackageV2Fields $Value @('relative_path', 'size_bytes', 'sha256') "$Context pin shape"
    Assert-M0PackageV2SafeRelativePath $Value.relative_path "$Context relative path"
    if ($Value.size_bytes -isnot [long] -or $Value.size_bytes -lt 0) {
        throw "$Context size_bytes must be one non-negative Int64."
    }
    Assert-M0PackageV2Sha256 $Value.sha256 "$Context hash"
    return [pscustomobject][ordered]@{
        relative_path = [string]$Value.relative_path
        size_bytes = [long]$Value.size_bytes
        sha256 = [string]$Value.sha256
    }
}

function Assert-M0PackageV2HashObject {
    param($Value, [string] $Context)
    Assert-M0PackageV2Fields $Value @('sha256') "$Context shape"
    Assert-M0PackageV2Sha256 $Value.sha256 "$Context hash"
}

function Add-M0PackageV2InventoryPath {
    param(
        [string] $RelativePath,
        [Collections.Generic.HashSet[string]] $Leaves,
        [Collections.Generic.HashSet[string]] $Ancestors,
        [string] $Context
    )
    if ($Leaves.Contains($RelativePath) -or $Ancestors.Contains($RelativePath)) {
        throw "$Context contains a case-insensitive path collision."
    }
    $segments = @($RelativePath.Split('/'))
    $prefix = [Text.StringBuilder]::new()
    $prefixes = [Collections.Generic.List[string]]::new()
    for ($index = 0; $index -lt $segments.Count - 1; $index++) {
        if ($index -ne 0) { [void]$prefix.Append('/') }
        [void]$prefix.Append($segments[$index])
        $value = $prefix.ToString()
        if ($Leaves.Contains($value)) { throw "$Context contains a file/directory path collision." }
        $prefixes.Add($value)
    }
    if (-not $Leaves.Add($RelativePath)) { throw "$Context contains a case-aliased duplicate." }
    foreach ($value in $prefixes) { [void]$Ancestors.Add($value) }
}

function ConvertTo-M0PackageV2Inventory {
    param($Value, [string] $Context)
    if ($Value -isnot [array]) { throw "$Context must be one JSON array." }
    $values = @($Value)
    if ($values.Count -eq 0) { throw "$Context must contain at least one real file." }
    $leaves = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $ancestors = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $rows = [Collections.Generic.List[object]]::new()
    for ($index = 0; $index -lt $values.Count; $index++) {
        $row = $values[$index]
        Assert-M0PackageV2Fields $row @('relative_path', 'size_bytes', 'sha256') "$Context row $index shape"
        Assert-M0PackageV2SafeRelativePath $row.relative_path "$Context row $index relative path"
        if ($row.size_bytes -isnot [long] -or $row.size_bytes -lt 0) {
            throw "$Context row $index size_bytes must be one non-negative Int64."
        }
        Assert-M0PackageV2Sha256 $row.sha256 "$Context row $index hash"
        Add-M0PackageV2InventoryPath ([string]$row.relative_path) $leaves $ancestors $Context
        $rows.Add([pscustomobject][ordered]@{
                relative_path = [string]$row.relative_path
                size_bytes = [long]$row.size_bytes
                sha256 = [string]$row.sha256
            })
    }
    return $rows.ToArray()
}

function Get-M0PackageV2ExpectedDirectories {
    param([object[]] $Rows)
    $result = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Rows) {
        $segments = @(([string]$row.relative_path).Split('/'))
        $prefix = [Text.StringBuilder]::new()
        for ($index = 0; $index -lt $segments.Count - 1; $index++) {
            if ($index -ne 0) { [void]$prefix.Append('/') }
            [void]$prefix.Append($segments[$index])
            $value = $prefix.ToString()
            if ($result.ContainsKey($value)) {
                if ($result[$value] -cne $value) { throw 'Inventory contains a case-aliased directory.' }
            }
            else { $result.Add($value, $value) }
        }
    }
    return ,$result
}

function Assert-M0PackageV2TreeEnvelopeUnchanged {
    param($Envelope, [string] $Context)
    $seenFiles = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenDirectories = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue([string]$Envelope.root)
    while ($queue.Count -ne 0) {
        $current = $queue.Dequeue()
        foreach ($entry in [IO.DirectoryInfo]::new($current).GetFileSystemInfos()) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "$Context tree envelope contains a reparse point."
            }
            $relative = [IO.Path]::GetRelativePath($Envelope.root, $entry.FullName).Replace(
                [IO.Path]::DirectorySeparatorChar, '/')
            if ($entry -is [IO.DirectoryInfo]) {
                if (-not $seenDirectories.TryAdd($relative, $relative) -or
                    -not $Envelope.directories.ContainsKey($relative) -or
                    $Envelope.directories[$relative] -cne $relative) {
                    throw "$Context tree topology contains an extra or case-aliased directory."
                }
                $queue.Enqueue($entry.FullName)
            }
            elseif ($entry -is [IO.FileInfo]) {
                if (-not $seenFiles.TryAdd($relative, $relative) -or
                    -not $Envelope.files.ContainsKey($relative) -or
                    $Envelope.files[$relative].relative_path -cne $relative) {
                    throw "$Context tree envelope contains an extra or case-aliased file."
                }
            }
            else { throw "$Context tree contains an unsupported entry." }
        }
    }
    if ($seenFiles.Count -ne $Envelope.files.Count -or
        $seenDirectories.Count -ne $Envelope.directories.Count) {
        throw "$Context tree topology contains an extra or missing file or directory."
    }
}

function Get-M0PackageV2TreeEnvelope {
    param(
        [string] $Root,
        [object[]] $Rows,
        [string] $Context,
        [Collections.Generic.List[object]] $LeaseSink,
        [Collections.Generic.List[object]] $FileSink,
        [Collections.Generic.List[object]] $DirectorySink
    )
    $rootFull = ConvertTo-M0PackageV2FullPath $Root
    $expectedFiles = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Rows) { $expectedFiles.Add([string]$row.relative_path, $row) }
    $expectedDirectories = Get-M0PackageV2ExpectedDirectories $Rows
    $actualFiles = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $actualDirectories = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $rootLease = Open-M0PackageV2DirectoryLease $rootFull $rootFull "$Context root" `
        $LeaseSink $DirectorySink
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($rootFull)
    while ($queue.Count -ne 0) {
        $current = $queue.Dequeue()
        foreach ($entry in [IO.DirectoryInfo]::new($current).GetFileSystemInfos()) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "$Context contains a reparse point: $($entry.FullName)"
            }
            $relative = [IO.Path]::GetRelativePath($rootFull, $entry.FullName).Replace(
                [IO.Path]::DirectorySeparatorChar, '/')
            Assert-M0PackageV2SafeRelativePath $relative "$Context relative path"
            if ($entry -is [IO.DirectoryInfo]) {
                if (-not $actualDirectories.TryAdd($relative, $relative) -or
                    -not $expectedDirectories.ContainsKey($relative) -or
                    $expectedDirectories[$relative] -cne $relative) {
                    throw "$Context topology contains an extra or case-aliased directory."
                }
                $null = Open-M0PackageV2DirectoryLease $entry.FullName $rootFull `
                    "$Context directory '$relative'" $LeaseSink $DirectorySink
                $queue.Enqueue($entry.FullName)
                continue
            }
            if ($entry -isnot [IO.FileInfo]) { throw "$Context contains an unsupported entry." }
            if (-not $actualFiles.TryAdd($relative, $relative) -or
                -not $expectedFiles.ContainsKey($relative)) {
                throw "$Context contains an extra or case-aliased leaf."
            }
            $expected = $expectedFiles[$relative]
            if ($expected.relative_path -cne $relative) {
                throw "$Context leaf casing differs from its canonical inventory."
            }
            $null = Open-M0PackageV2FileLease $entry.FullName $rootFull `
                "$Context leaf '$relative'" ([long]$expected.size_bytes) ([string]$expected.sha256) `
                $LeaseSink $FileSink
        }
    }
    if ($actualFiles.Count -ne $expectedFiles.Count -or
        $actualDirectories.Count -ne $expectedDirectories.Count) {
        throw "$Context topology contains an extra or missing leaf or directory."
    }
    $envelope = [pscustomobject][ordered]@{
        root = $rootFull
        files = $expectedFiles
        directories = $expectedDirectories
        root_identity = $rootLease.identity
    }
    Assert-M0PackageV2TreeEnvelopeUnchanged $envelope "$Context initial resweep"
    return $envelope
}

function Assert-M0PackageV2TopLevelEnvelope {
    param([string] $RunRoot, [string] $Context)
    $entries = @([IO.DirectoryInfo]::new($RunRoot).GetFileSystemInfos())
    if ($entries.Count -ne 2) { throw "$Context top-level envelope has an extra or missing entry." }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in $entries) {
        if ($entry -isnot [IO.DirectoryInfo] -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            $entry.Name -cnotin @('app', 'evidence') -or -not $seen.Add($entry.Name)) {
            throw "$Context top-level envelope must be exactly app/ and evidence/ with canonical casing."
        }
    }
    if (-not $seen.Contains('app') -or -not $seen.Contains('evidence')) {
        throw "$Context top-level envelope is incomplete."
    }
}

function Get-M0PackageV2RootFromRelativeFile {
    param([string] $LiteralPath, [string] $RelativePath, [string] $Context)
    Assert-M0PackageV2SafeRelativePath $RelativePath "$Context relative path"
    $full = ConvertTo-M0PackageV2FullPath $LiteralPath
    $root = $full
    foreach ($null in @($RelativePath.Split('/'))) {
        $root = [IO.Path]::GetDirectoryName($root)
        if ([string]::IsNullOrEmpty($root)) { throw "$Context has no containing package root." }
    }
    $root = ConvertTo-M0PackageV2FullPath $root
    $resolved = Resolve-M0PackageV2ContainedPath $root $RelativePath $Context
    if (-not [string]::Equals($resolved, $full, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context path differs from the contract envelope."
    }
    return $root
}

function Get-M0PackageV2InventoryTotals {
    param([object[]] $Rows)
    $bytes = [long]0
    foreach ($row in $Rows) {
        if ([long]$row.size_bytes -gt ([long]::MaxValue - $bytes)) {
            throw 'Package inventory total size overflows Int64.'
        }
        $bytes += [long]$row.size_bytes
    }
    return [pscustomobject][ordered]@{ files = [long]$Rows.Count; bytes = $bytes }
}

function Add-M0PackageV2RootPrefix {
    param([string] $RootRelativePath, [object[]] $Rows)
    $result = [Collections.Generic.List[object]]::new()
    foreach ($row in $Rows) {
        $result.Add([pscustomobject][ordered]@{
                relative_path = "$RootRelativePath/$($row.relative_path)"
                size_bytes = [long]$row.size_bytes
                sha256 = [string]$row.sha256
            })
    }
    return $result.ToArray()
}

function Assert-M0PackageV2Contract {
    param($Contract)
    Assert-M0PackageV2Fields $Contract @(
        'schema_version', 'package_id', 'product', 'recipe', 'inputs', 'build',
        'deployment', 'envelope', 'smoke', 'security', 'limits') 'Package contract root shape'
    if ($Contract.schema_version -isnot [long] -or $Contract.schema_version -ne 2 -or
        $Contract.package_id -isnot [string] -or $Contract.package_id -cne $script:PackageId) {
        throw 'Package contract schema or package_id drifted.'
    }
    Assert-M0PackageV2Fields $Contract.product @(
        'qt_version', 'edition', 'architecture', 'compiler_abi', 'windows_sdk_version',
        'msvc_compiler_version', 'msvc_toolset_version', 'qt_config_msvc_version') `
        'Package contract product shape'
    if ($Contract.product.windows_sdk_version -cne $script:WindowsSdkVersion -or
        $Contract.product.msvc_compiler_version -cne $script:MsvcCompilerVersion -or
        $Contract.product.qt_config_msvc_version -cne $script:QtConfigMsvcVersion) {
        throw 'Package contract platform or compiler identity drifted.'
    }
    Assert-M0PackageV2Fields $Contract.inputs @(
        'composite_contract', 'composite_receipt', 'qtbase_receipt', 'sources',
        'catch2', 'tools', 'sbom_files') 'Package contract inputs shape'
    foreach ($name in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        $null = Assert-M0PackageV2Pin $Contract.inputs.$name "Package contract $name anchor"
    }
    Assert-M0PackageV2Fields $Contract.envelope @(
        'top_level', 'payload_relative_path', 'evidence_relative_path', 'manifest_relative_path',
        'receipt_relative_path', 'receipt_is_final_entry', 'create_new', 'allow_reparse_points',
        'allow_ads', 'allow_hardlinks', 'path_comparer') 'Package contract envelope shape'
    if ($Contract.envelope.top_level -isnot [array] -or
        (@($Contract.envelope.top_level) -join ',') -cne 'app,evidence' -or
        $Contract.envelope.payload_relative_path -cne 'app' -or
        $Contract.envelope.evidence_relative_path -cne 'evidence' -or
        $Contract.envelope.receipt_is_final_entry -ne $true -or
        $Contract.envelope.create_new -ne $true -or
        $Contract.envelope.allow_reparse_points -ne $false -or
        $Contract.envelope.allow_ads -ne $false -or
        $Contract.envelope.allow_hardlinks -ne $false -or
        $Contract.envelope.path_comparer -cne 'OrdinalIgnoreCase') {
        throw 'Package contract envelope safety policy drifted.'
    }
    foreach ($name in @('manifest_relative_path', 'receipt_relative_path')) {
        Assert-M0PackageV2SafeRelativePath $Contract.envelope.$name "Package contract $name"
        if (-not ([string]$Contract.envelope.$name).StartsWith('evidence/', [StringComparison]::Ordinal)) {
            throw "Package contract $name must be inside evidence/."
        }
    }
}

function Assert-M0PackageV2ProductionContract {
    param([Parameter(Mandatory)] $Contract)
    Assert-M0PackageV2Contract $Contract
    $product = $Contract.product
    $expectedProduct = [ordered]@{
        qt_version = '6.11.2'
        edition = 'Desktop'
        architecture = 'x86_64'
        compiler_abi = 'msvc2022_64'
        windows_sdk_version = $script:WindowsSdkVersion
        msvc_compiler_version = $script:MsvcCompilerVersion
        msvc_toolset_version = '14.44.35207'
        qt_config_msvc_version = $script:QtConfigMsvcVersion
    }
    foreach ($entry in $expectedProduct.GetEnumerator()) {
        if ($product.($entry.Key) -isnot [string] -or
            $product.($entry.Key) -cne $entry.Value) {
            throw "Package production identity '$($entry.Key)' drifted."
        }
    }

    Assert-M0PackageV2Fields $Contract.recipe @(
        'files', 'core_exports', 'native_export', 'smoke_export') `
        'Package production recipe shape'
    $expectedRecipePaths = @(
        'experiments/m0/candidates/qt/package-v2/Invoke-M0QtShellCompositePackage.ps1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackage.psm1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageNative.psm1',
        'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageSmoke.psm1',
        'experiments/m0/candidates/qt/package-v2/Test-M0QtShellCompositePackageSmoke.ps1',
        'experiments/m0/candidates/qt/package-v2/qt.conf',
        'experiments/m0/candidates/qt/package-v2/PACKAGE-LIMITATIONS.txt'
    )
    if ($Contract.recipe.files -isnot [array] -or
        @($Contract.recipe.files).Count -ne $expectedRecipePaths.Count) {
        throw 'Package production recipe does not contain exactly seven files.'
    }
    $recipePaths = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($pinValue in @($Contract.recipe.files)) {
        $pin = Assert-M0PackageV2Pin $pinValue 'Package production recipe file'
        if (-not $recipePaths.Add($pin.relative_path)) {
            throw 'Package production recipe contains a duplicate path.'
        }
    }
    foreach ($path in $expectedRecipePaths) {
        if (-not $recipePaths.Contains($path)) {
            throw "Package production recipe omits '$path'."
        }
    }
    [string[]] $coreExports = @($Contract.recipe.core_exports)
    [string[]] $expectedCoreExports = @(
        'Assert-M0QtShellCompositePackageReceipt',
        'Invoke-M0QtShellCompositePackage')
    [Array]::Sort($coreExports, [StringComparer]::Ordinal)
    [Array]::Sort($expectedCoreExports, [StringComparer]::Ordinal)
    if (($coreExports -join "`n") -cne ($expectedCoreExports -join "`n") -or
        $Contract.recipe.native_export -cne 'Invoke-M0QtShellCompositePackageNative' -or
        $Contract.recipe.smoke_export -cne 'Test-M0QtShellCompositePackageSmoke') {
        throw 'Package production recipe export surface drifted.'
    }

    Assert-M0PackageV2Fields $Contract.inputs.sources @(
        'shell_tree', 'fixture_tree', 'protocol_tree', 'cmakelists',
        'fixture_manifest', 'protocol') 'Package production source inputs'
    $expectedTrees = [ordered]@{
        shell_tree = 'experiments/m0/candidates/qt/shell'
        fixture_tree = 'experiments/m0/shared/shell-fixture'
        protocol_tree = 'experiments/m0/protocol'
    }
    foreach ($entry in $expectedTrees.GetEnumerator()) {
        $tree = $Contract.inputs.sources.($entry.Key)
        Assert-M0PackageV2Fields $tree @('relative_path', 'git_tree') `
            "Package production source tree '$($entry.Key)'"
        Assert-M0PackageV2SafeRelativePath $tree.relative_path `
            "Package production source tree '$($entry.Key)' path"
        if ($tree.relative_path -cne $entry.Value -or
            $tree.git_tree -isnot [string] -or $tree.git_tree -cnotmatch '^[0-9a-f]{40}$') {
            throw "Package production source tree '$($entry.Key)' drifted."
        }
    }
    $expectedSourceFiles = [ordered]@{
        cmakelists = 'experiments/m0/candidates/qt/shell/CMakeLists.txt'
        fixture_manifest = 'experiments/m0/shared/shell-fixture/manifest.v1.json'
        protocol = 'experiments/m0/protocol/protocol.v1.json'
    }
    foreach ($entry in $expectedSourceFiles.GetEnumerator()) {
        $pin = Assert-M0PackageV2Pin $Contract.inputs.sources.($entry.Key) `
            "Package production source '$($entry.Key)'"
        if ($pin.relative_path -cne $entry.Value) {
            throw "Package production source '$($entry.Key)' path drifted."
        }
    }

    Assert-M0PackageV2Fields $Contract.inputs.catch2 @(
        'version', 'commit', 'source_relative_path', 'archive_relative_path',
        'archive_size_bytes', 'archive_sha256') 'Package production Catch2 input'
    $catch2 = $Contract.inputs.catch2
    if ($catch2.version -cne '3.15.3' -or
        $catch2.commit -cne '8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb' -or
        $catch2.source_relative_path -cne 'out/m0/toolchains/Catch2-3.15.3' -or
        $catch2.archive_relative_path -cne `
            'out/m0/downloads/catch2-3.15.3/Catch2-3.15.3.tar.gz' -or
        $catch2.archive_size_bytes -isnot [long] -or $catch2.archive_size_bytes -le 0) {
        throw 'Package production Catch2 identity drifted.'
    }
    Assert-M0PackageV2Sha256 $catch2.archive_sha256 'Package production Catch2 archive'

    $expectedToolIds = @(
        'cmake', 'ctest', 'ninja', 'cl', 'link', 'dumpbin', 'rc', 'mt',
        'vsdevcmd', 'cmd', 'git', 'windeployqt', 'qtpaths', 'qmllint')
    $expectedToolPathKinds = [ordered]@{
        cmake = 'absolute'
        ctest = 'absolute'
        ninja = 'absolute'
        cl = 'absolute'
        link = 'absolute'
        dumpbin = 'absolute'
        rc = 'absolute'
        mt = 'absolute'
        vsdevcmd = 'absolute'
        cmd = 'absolute'
        git = 'absolute'
        windeployqt = 'composite-sdk-relative'
        qtpaths = 'composite-sdk-relative'
        qmllint = 'composite-sdk-relative'
    }
    $expectedCompositeToolPaths = [ordered]@{
        windeployqt = 'bin/windeployqt.exe'
        qtpaths = 'bin/qtpaths.exe'
        qmllint = 'bin/qmllint.exe'
    }
    if ($Contract.inputs.tools -isnot [array] -or
        @($Contract.inputs.tools).Count -ne $expectedToolIds.Count) {
        throw 'Package production tool inventory is incomplete.'
    }
    $toolIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($tool in @($Contract.inputs.tools)) {
        Assert-M0PackageV2Fields $tool @(
            'id', 'version', 'path_kind', 'path', 'size_bytes', 'sha256') `
            'Package production tool'
        if ($tool.id -isnot [string] -or -not $toolIds.Add($tool.id) -or
            $tool.version -isnot [string] -or [string]::IsNullOrWhiteSpace($tool.version) -or
            $tool.path -isnot [string] -or [string]::IsNullOrWhiteSpace($tool.path) -or
            $tool.size_bytes -isnot [long] -or $tool.size_bytes -le 0 -or
            -not $expectedToolPathKinds.Contains($tool.id) -or
            $tool.path_kind -cne $expectedToolPathKinds[$tool.id]) {
            throw 'Package production tool contains an invalid identity field.'
        }
        Assert-M0PackageV2Sha256 $tool.sha256 "Package production tool '$($tool.id)'"
        if ($tool.path_kind -ceq 'absolute' -and -not [IO.Path]::IsPathFullyQualified($tool.path)) {
            throw "Package production tool '$($tool.id)' must have an absolute path."
        }
        if ($tool.path_kind -ceq 'composite-sdk-relative') {
            Assert-M0PackageV2SafeRelativePath $tool.path `
                "Package production tool '$($tool.id)' SDK path"
            if ($tool.path -cne $expectedCompositeToolPaths[$tool.id]) {
                throw "Package production tool '$($tool.id)' escaped its canonical SDK location."
            }
        }
    }
    foreach ($id in $expectedToolIds) {
        if (-not $toolIds.Contains($id)) { throw "Package production tool '$id' is missing." }
    }

    if ($Contract.inputs.sbom_files -isnot [array] -or
        @($Contract.inputs.sbom_files).Count -ne 2) {
        throw 'Package production evidence requires exactly two binary SPDX inputs.'
    }
    $expectedSbomIds = [ordered]@{
        'qtdeclarative-binary' = 'evidence/sbom/qtdeclarative-6.11.2.spdx'
        'qtsvg-binary' = 'evidence/sbom/qtsvg-6.11.2.spdx'
    }
    $sbomIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($sbom in @($Contract.inputs.sbom_files)) {
        Assert-M0PackageV2Fields $sbom @(
            'id', 'source_relative_path', 'target_relative_path', 'size_bytes', 'sha256') `
            'Package production SPDX input'
        Assert-M0PackageV2SafeRelativePath $sbom.source_relative_path `
            'Package production SPDX source path'
        Assert-M0PackageV2SafeRelativePath $sbom.target_relative_path `
            'Package production SPDX target path'
        if ($sbom.id -isnot [string] -or -not $sbomIds.Add($sbom.id) -or
            -not $expectedSbomIds.Contains($sbom.id) -or
            $sbom.target_relative_path -cne $expectedSbomIds[$sbom.id] -or
            $sbom.size_bytes -isnot [long] -or $sbom.size_bytes -le 0) {
            throw 'Package production SPDX identity drifted.'
        }
        Assert-M0PackageV2Sha256 $sbom.sha256 "Package production SPDX '$($sbom.id)'"
    }

    Assert-M0PackageV2Fields $Contract.build @(
        'generator', 'configuration', 'qt_on', 'core_off', 'qml_lint') `
        'Package production build policy'
    Assert-M0PackageV2Fields $Contract.build.qt_on @(
        'support_enabled', 'expected_ctest_count') 'Package Qt-on build policy'
    Assert-M0PackageV2Fields $Contract.build.core_off @(
        'support_enabled', 'expected_ctest_count') 'Package core-off build policy'
    Assert-M0PackageV2Fields $Contract.build.qml_lint @('required', 'target') `
        'Package QML lint policy'
    if ($Contract.build.generator -cne 'Ninja' -or
        $Contract.build.configuration -cne 'Release' -or
        $Contract.build.qt_on.support_enabled -ne $true -or
        $Contract.build.qt_on.expected_ctest_count -ne 65 -or
        $Contract.build.core_off.support_enabled -ne $false -or
        $Contract.build.core_off.expected_ctest_count -ne 55 -or
        $Contract.build.qml_lint.required -ne $true -or
        $Contract.build.qml_lint.target -cne 'all_qmllint') {
        throw 'Package production build policy drifted.'
    }

    Assert-M0PackageV2Fields $Contract.deployment @(
        'dry_run_equals_actual', 'composite_sdk_only', 'application_qml_only',
        'allow_official_sdk_fallback', 'allow_system32_qt_or_d3d_fallback',
        'composite_qconfig') 'Package production deployment policy'
    $qconfig = Assert-M0PackageV2Pin $Contract.deployment.composite_qconfig `
        'Package production qconfig'
    if ($Contract.deployment.dry_run_equals_actual -ne $true -or
        $Contract.deployment.composite_sdk_only -ne $true -or
        $Contract.deployment.application_qml_only -ne $true -or
        $Contract.deployment.allow_official_sdk_fallback -ne $false -or
        $Contract.deployment.allow_system32_qt_or_d3d_fallback -ne $false -or
        $qconfig.relative_path -cne 'mkspecs/qconfig.pri') {
        throw 'Package production deployment policy drifted.'
    }

    Assert-M0PackageV2Fields $Contract.smoke @(
        'variants', 'journeys', 'module_capture_deadline_ms',
        'verify_before_and_after_each', 'require_zero_survivors') `
        'Package production smoke policy'
    if ((@($Contract.smoke.variants) -join ',') -cne `
            'original,relocated-spaces-accents' -or
        @($Contract.smoke.journeys).Count -ne 2 -or
        $Contract.smoke.module_capture_deadline_ms -ne 500 -or
        $Contract.smoke.verify_before_and_after_each -ne $true -or
        $Contract.smoke.require_zero_survivors -ne $true) {
        throw 'Package production smoke policy drifted.'
    }
    $expectedJourneys = [ordered]@{
        'tray-lifecycle' = @('--m0-smoke=tray-lifecycle')
        'error-recovery' = @('--m0-scenario=unavailable', '--m0-smoke=error-recovery')
    }
    foreach ($journey in @($Contract.smoke.journeys)) {
        Assert-M0PackageV2Fields $journey @('id', 'arguments') `
            'Package production smoke journey'
        if (-not $expectedJourneys.Contains($journey.id) -or
            (@($journey.arguments) -join ',') -cne
                (@($expectedJourneys[$journey.id]) -join ',')) {
            throw 'Package production smoke journey drifted.'
        }
    }

    Assert-M0PackageV2Fields $Contract.security @(
        'redistribution_allowed', 'chk_sec', 'qtbase_sbom_status',
        'notices_status', 'sources_status') 'Package production security status'
    if ($Contract.security.redistribution_allowed -ne $false -or
        $Contract.security.chk_sec -cne 'open' -or
        $Contract.security.qtbase_sbom_status -cne 'absent' -or
        $Contract.security.notices_status -cne 'incomplete' -or
        $Contract.security.sources_status -cne 'incomplete') {
        throw 'Package production security status became misleading.'
    }
    if ($Contract.limits -isnot [array] -or @($Contract.limits).Count -lt 6 -or
        @($Contract.limits | Where-Object { $_ -isnot [string] -or
                [string]::IsNullOrWhiteSpace($_) }).Count -ne 0) {
        throw 'Package production limitations are incomplete.'
    }
}

function Assert-M0QtShellCompositePackageReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $CompositeReceiptPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedQtBaseReceiptSha256
    )
    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new(
            'Authenticated package verification requires Windows physical-file semantics.')
    }
    Assert-M0PackageV2Sha256 $ExpectedQtBaseReceiptSha256 'Expected Qtbase receipt anchor'
    $repository = ConvertTo-M0PackageV2FullPath $RepositoryRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'Repository root is missing.' }
    $leases = [Collections.Generic.List[object]]::new()
    $fileLeases = [Collections.Generic.List[object]]::new()
    $directoryLeases = [Collections.Generic.List[object]]::new()
    $treeEnvelopes = [Collections.Generic.List[object]]::new()
    try {
        $contractDocument = Read-M0PackageV2JsonLease $ContractPath $ExpectedContractSha256 $repository `
            'Package contract' $leases $fileLeases
        $contract = $contractDocument.value
        Assert-M0PackageV2Contract $contract
        $compositeContractPin = Assert-M0PackageV2Pin `
            $contract.inputs.composite_contract 'Package composite contract anchor'
        $compositeReceiptPin = Assert-M0PackageV2Pin `
            $contract.inputs.composite_receipt 'Package composite receipt anchor'
        $qtbaseReceiptPin = Assert-M0PackageV2Pin `
            $contract.inputs.qtbase_receipt 'Package Qtbase receipt anchor'
        if ($compositeReceiptPin.sha256 -cne $ExpectedCompositeReceiptSha256 -or
            $qtbaseReceiptPin.sha256 -cne $ExpectedQtBaseReceiptSha256) {
            throw 'Package contract external receipt anchor drifted.'
        }

        $compositeContractPath = Resolve-M0PackageV2ContainedPath $repository `
            $compositeContractPin.relative_path 'Composite contract anchor path'
        $compositeContractDocument = Read-M0PackageV2JsonLease $compositeContractPath `
            $compositeContractPin.sha256 $repository 'Composite contract anchor' $leases $fileLeases
        if ($compositeContractDocument.record.size_bytes -ne $compositeContractPin.size_bytes) {
            throw 'Composite contract anchor size drifted.'
        }
        $expectedCompositePath = Resolve-M0PackageV2ContainedPath $repository `
            $compositeReceiptPin.relative_path 'Composite receipt anchor path'
        $providedCompositePath = ConvertTo-M0PackageV2FullPath $CompositeReceiptPath
        if (-not [string]::Equals(
                $expectedCompositePath, $providedCompositePath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Composite receipt path differs from its contract anchor.'
        }
        $compositeDocument = Read-M0PackageV2JsonLease $providedCompositePath `
            $ExpectedCompositeReceiptSha256 $repository 'Composite receipt anchor' $leases $fileLeases
        if ($compositeDocument.record.size_bytes -ne $compositeReceiptPin.size_bytes) {
            throw 'Composite receipt anchor size drifted.'
        }
        $composite = $compositeDocument.value
        Assert-M0PackageV2RequiredFields $composite @('schema_version', 'contract_sha256', 'qtbase_receipt') `
            'Composite receipt anchor'
        $compositeQtbasePin = Assert-M0PackageV2Pin $composite.qtbase_receipt `
            'Composite receipt Qtbase anchor'
        if ($composite.schema_version -isnot [long] -or $composite.schema_version -ne 2 -or
            $composite.contract_sha256 -cne $compositeContractPin.sha256 -or
            $compositeQtbasePin.relative_path -cne $qtbaseReceiptPin.relative_path -or
            $compositeQtbasePin.size_bytes -ne $qtbaseReceiptPin.size_bytes -or
            $compositeQtbasePin.sha256 -cne $ExpectedQtBaseReceiptSha256) {
            throw 'Composite receipt contract or Qtbase anchor drifted.'
        }
        $qtbasePath = Resolve-M0PackageV2ContainedPath $repository `
            $qtbaseReceiptPin.relative_path 'Qtbase receipt anchor path'
        $compositeQtbasePath = Resolve-M0PackageV2ContainedPath $repository `
            $compositeQtbasePin.relative_path 'Composite receipt Qtbase anchor path'
        if (-not [string]::Equals(
                $qtbasePath, $compositeQtbasePath, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Composite and package contracts resolve different physical Qtbase receipts.'
        }
        $qtbaseDocument = Read-M0PackageV2JsonLease $qtbasePath $ExpectedQtBaseReceiptSha256 `
            $repository 'Qtbase receipt anchor' $leases $fileLeases
        if ($qtbaseDocument.record.size_bytes -ne $qtbaseReceiptPin.size_bytes) {
            throw 'Qtbase receipt anchor size drifted.'
        }

        $receiptDocument = Read-M0PackageV2JsonLease $LiteralPath $ExpectedReceiptSha256 `
            $repository 'Package receipt' $leases $fileLeases
        $receipt = $receiptDocument.value
        Assert-M0PackageV2Fields $receipt @(
            'schema_version', 'package_id', 'contract', 'composite_receipt', 'qtbase_receipt',
            'manifest', 'totals') 'Package receipt root shape'
        if ($receipt.schema_version -isnot [long] -or $receipt.schema_version -ne 2 -or
            $receipt.package_id -isnot [string] -or $receipt.package_id -cne $script:PackageId) {
            throw 'Package receipt schema or package_id drifted.'
        }
        Assert-M0PackageV2HashObject $receipt.contract 'Package receipt contract'
        Assert-M0PackageV2HashObject $receipt.composite_receipt 'Package receipt composite receipt'
        Assert-M0PackageV2HashObject $receipt.qtbase_receipt 'Package receipt Qtbase receipt'
        if ($receipt.contract.sha256 -cne $ExpectedContractSha256 -or
            $receipt.composite_receipt.sha256 -cne $ExpectedCompositeReceiptSha256 -or
            $receipt.qtbase_receipt.sha256 -cne $ExpectedQtBaseReceiptSha256) {
            throw 'Package receipt changed an external identity anchor.'
        }
        $manifestPin = Assert-M0PackageV2Pin $receipt.manifest 'Package receipt manifest'
        if ($manifestPin.relative_path -cne $contract.envelope.manifest_relative_path) {
            throw 'Package receipt manifest path differs from the contract envelope.'
        }
        Assert-M0PackageV2Fields $receipt.totals @(
            'payload_files', 'payload_bytes', 'evidence_files', 'evidence_bytes') `
            'Package receipt totals shape'
        foreach ($name in @('payload_files', 'payload_bytes', 'evidence_files', 'evidence_bytes')) {
            if ($receipt.totals.$name -isnot [long] -or $receipt.totals.$name -lt 0) {
                throw "Package receipt totals $name must be a non-negative Int64."
            }
        }

        $receiptFull = ConvertTo-M0PackageV2FullPath $LiteralPath
        $runRoot = Get-M0PackageV2RootFromRelativeFile $receiptFull `
            $contract.envelope.receipt_relative_path 'Package receipt'
        if (-not (Test-M0PackageV2PathWithin $runRoot $repository)) {
            throw 'Package root escaped the repository root.'
        }
        $null = Open-M0PackageV2DirectoryLease $runRoot $repository 'Package root' `
            $leases $directoryLeases
        Assert-M0PackageV2TopLevelEnvelope $runRoot 'Package'

        $manifestPath = Resolve-M0PackageV2ContainedPath $runRoot `
            $manifestPin.relative_path 'Package manifest path'
        $manifestDocument = Read-M0PackageV2JsonLease $manifestPath $manifestPin.sha256 `
            $runRoot 'Package manifest' $leases $fileLeases
        if ($manifestDocument.record.size_bytes -ne $manifestPin.size_bytes) {
            throw 'Package manifest size differs from its receipt pin.'
        }
        $manifest = $manifestDocument.value
        Assert-M0PackageV2Fields $manifest @(
            'schema_version', 'package_id', 'contract', 'composite_receipt', 'qtbase_receipt',
            'payload', 'evidence') 'Package manifest root shape'
        if ($manifest.schema_version -isnot [long] -or $manifest.schema_version -ne 2 -or
            $manifest.package_id -isnot [string] -or $manifest.package_id -cne $script:PackageId) {
            throw 'Package manifest schema or package_id drifted.'
        }
        Assert-M0PackageV2HashObject $manifest.contract 'Package manifest contract'
        Assert-M0PackageV2HashObject $manifest.composite_receipt 'Package manifest composite receipt'
        Assert-M0PackageV2HashObject $manifest.qtbase_receipt 'Package manifest Qtbase receipt'
        if ($manifest.contract.sha256 -cne $ExpectedContractSha256 -or
            $manifest.composite_receipt.sha256 -cne $ExpectedCompositeReceiptSha256 -or
            $manifest.qtbase_receipt.sha256 -cne $ExpectedQtBaseReceiptSha256) {
            throw 'Package manifest changed an external identity anchor.'
        }
        Assert-M0PackageV2Fields $manifest.payload @('root_relative_path', 'files') `
            'Package manifest payload shape'
        Assert-M0PackageV2Fields $manifest.evidence @('root_relative_path', 'files') `
            'Package manifest evidence shape'
        if ($manifest.payload.root_relative_path -cne $contract.envelope.payload_relative_path -or
            $manifest.evidence.root_relative_path -cne $contract.envelope.evidence_relative_path) {
            throw 'Package manifest payload or evidence root drifted.'
        }
        $payloadRows = @(ConvertTo-M0PackageV2Inventory $manifest.payload.files `
                'Package payload inventory')
        $evidenceInventoryRows = @(ConvertTo-M0PackageV2Inventory $manifest.evidence.files `
                'Package evidence inventory')
        $payloadTotals = Get-M0PackageV2InventoryTotals $payloadRows
        $evidenceTotals = Get-M0PackageV2InventoryTotals $evidenceInventoryRows
        if ($receipt.totals.payload_files -ne $payloadTotals.files -or
            $receipt.totals.payload_bytes -ne $payloadTotals.bytes -or
            $receipt.totals.evidence_files -ne $evidenceTotals.files -or
            $receipt.totals.evidence_bytes -ne $evidenceTotals.bytes) {
            throw 'Package receipt totals differ from manifest payload or evidence inventory.'
        }

        $payloadRoot = Resolve-M0PackageV2ContainedPath $runRoot `
            $contract.envelope.payload_relative_path 'Package payload root'
        $evidenceRoot = Resolve-M0PackageV2ContainedPath $runRoot `
            $contract.envelope.evidence_relative_path 'Package evidence root'
        $payloadEnvelope = Get-M0PackageV2TreeEnvelope $payloadRoot $payloadRows 'Package payload' `
            $leases $fileLeases $directoryLeases
        $treeEnvelopes.Add($payloadEnvelope)

        $manifestEvidenceRelative = [IO.Path]::GetRelativePath($evidenceRoot, $manifestPath).Replace(
            [IO.Path]::DirectorySeparatorChar, '/')
        $receiptEvidenceRelative = [IO.Path]::GetRelativePath($evidenceRoot, $receiptFull).Replace(
            [IO.Path]::DirectorySeparatorChar, '/')
        $closedEvidenceRows = [Collections.Generic.List[object]]::new()
        foreach ($row in $evidenceInventoryRows) { $closedEvidenceRows.Add($row) }
        $closedEvidenceRows.Add([pscustomobject][ordered]@{
                relative_path = $manifestEvidenceRelative
                size_bytes = [long]$manifestDocument.record.size_bytes
                sha256 = [string]$manifestDocument.record.sha256
            })
        $closedEvidenceRows.Add([pscustomobject][ordered]@{
                relative_path = $receiptEvidenceRelative
                size_bytes = [long]$receiptDocument.record.size_bytes
                sha256 = [string]$receiptDocument.record.sha256
            })
        $closedEvidence = @(ConvertTo-M0PackageV2Inventory $closedEvidenceRows.ToArray() `
                'Package closed evidence envelope')
        if ($closedEvidence[-1].relative_path -cne $receiptEvidenceRelative) {
            throw 'Package receipt is not the final evidence inventory entry.'
        }
        $evidenceEnvelope = Get-M0PackageV2TreeEnvelope $evidenceRoot $closedEvidence `
            'Package evidence' $leases $fileLeases $directoryLeases
        $treeEnvelopes.Add($evidenceEnvelope)

        $publicPayload = @(Add-M0PackageV2RootPrefix `
                $contract.envelope.payload_relative_path $payloadRows)
        $publicEvidence = @(Add-M0PackageV2RootPrefix `
                $contract.envelope.evidence_relative_path $closedEvidence)
        $result = [pscustomobject][ordered]@{
            verified = $true
            package_id = $script:PackageId
            contract_sha256 = $ExpectedContractSha256
            composite_receipt_sha256 = $ExpectedCompositeReceiptSha256
            manifest_sha256 = [string]$manifestDocument.record.sha256
            receipt_sha256 = $ExpectedReceiptSha256
            manifest_path = [string]$contract.envelope.manifest_relative_path
            payload = $publicPayload
            evidence = $publicEvidence
        }

        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_VERIFIER_FINAL_RESWEEP
        Assert-M0PackageV2FileLeasesUnchanged $fileLeases 'Package verifier final resweep'
        Assert-M0PackageV2DirectoryLeasesUnchanged $directoryLeases 'Package verifier final resweep'
        Assert-M0PackageV2TopLevelEnvelope $runRoot 'Package final resweep'
        foreach ($envelope in $treeEnvelopes) {
            Assert-M0PackageV2TreeEnvelopeUnchanged $envelope 'Package verifier final resweep'
        }
        return $result
    }
    finally { Close-M0PackageV2Leases $leases }
}

function Assert-M0PackageV2AbsolutePlanPath {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value) -or
        -not [IO.Path]::IsPathFullyQualified($Value)) {
        throw "$Context must be one absolute path."
    }
    return ConvertTo-M0PackageV2FullPath $Value
}

function New-M0PackageV2BuildCommand {
    param(
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Arguments,
        [Parameter(Mandatory)] [string] $WorkingDirectory,
        [Parameter(Mandatory)] [long] $TimeoutSeconds,
        $ExpectedTestCount
    )
    if ($Id -cnotmatch '^[a-z0-9]+(?:-[a-z0-9]+)*$') {
        throw 'Package-v2 build command id is malformed.'
    }
    $executable = Assert-M0PackageV2AbsolutePlanPath $FilePath "Build command $Id executable"
    $working = Assert-M0PackageV2AbsolutePlanPath $WorkingDirectory "Build command $Id working directory"
    if ($TimeoutSeconds -le 0) { throw "Build command $Id timeout must be positive." }
    $argumentList = [object[]]@($Arguments)
    foreach ($argument in $argumentList) {
        if ($argument -isnot [string] -or [string]::IsNullOrEmpty($argument) -or
            $argument.Contains([char]0)) {
            throw "Build command $Id has an invalid typed argument."
        }
    }
    if ($null -ne $ExpectedTestCount -and
        ($ExpectedTestCount -isnot [long] -or $ExpectedTestCount -le 0)) {
        throw "Build command $Id has an invalid expected test count."
    }
    return [pscustomobject][ordered]@{
        id = $Id
        file_path = $executable
        arguments = $argumentList
        working_directory = $working
        timeout_seconds = [long]$TimeoutSeconds
        expected_test_count = $ExpectedTestCount
    }
}

function New-M0PackageV2BuildPlan {
    param([Parameter(Mandatory)] $Context)
    Assert-M0PackageV2Fields $Context @(
        'repository_root', 'source_root', 'composite_sdk_root', 'catch2_root',
        'qt_on_build_root', 'core_off_build_root', 'tools') 'Package-v2 build context'
    Assert-M0PackageV2Fields $Context.tools @(
        'cmake', 'ctest', 'ninja', 'cl', 'link', 'rc', 'mt', 'dumpbin') `
        'Package-v2 build tools'

    $repository = Assert-M0PackageV2AbsolutePlanPath $Context.repository_root `
        'Package-v2 repository root'
    $source = Assert-M0PackageV2AbsolutePlanPath $Context.source_root `
        'Package-v2 source root'
    $sdk = Assert-M0PackageV2AbsolutePlanPath $Context.composite_sdk_root `
        'Package-v2 composite SDK root'
    $catch2 = Assert-M0PackageV2AbsolutePlanPath $Context.catch2_root `
        'Package-v2 Catch2 root'
    $qtBuild = Assert-M0PackageV2AbsolutePlanPath $Context.qt_on_build_root `
        'Package-v2 Qt-on build root'
    $coreBuild = Assert-M0PackageV2AbsolutePlanPath $Context.core_off_build_root `
        'Package-v2 core-off build root'
    foreach ($root in @($source, $sdk, $catch2, $qtBuild, $coreBuild)) {
        if (-not (Test-M0PackageV2PathWithin $root $repository)) {
            throw 'Package-v2 build context root escaped the repository root.'
        }
    }
    if ([string]::Equals($qtBuild, $coreBuild, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Qt-on and core-off builds require distinct roots.'
    }

    $tools = [ordered]@{}
    foreach ($name in @('cmake', 'ctest', 'ninja', 'cl', 'link', 'rc', 'mt', 'dumpbin')) {
        $tools[$name] = Assert-M0PackageV2AbsolutePlanPath $Context.tools.$name `
            "Package-v2 $name tool"
    }
    $qtConfig = Join-Path $sdk 'lib/cmake/Qt6'
    $coreExecutable = Join-Path $coreBuild 'm0_qt_core_tests.exe'
    $commonConfigure = [object[]]@(
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=ON',
        "-DCMAKE_SYSTEM_VERSION=$script:WindowsSdkVersion",
        "-DM0_CATCH2_SOURCE_DIR=$catch2",
        "-DCMAKE_MAKE_PROGRAM=$($tools.ninja)",
        "-DCMAKE_CXX_COMPILER=$($tools.cl)",
        "-DCMAKE_LINKER=$($tools.link)",
        "-DCMAKE_RC_COMPILER=$($tools.rc)",
        "-DCMAKE_MT=$($tools.mt)",
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
    )

    $commands = [Collections.Generic.List[object]]::new()
    $commands.Add((New-M0PackageV2BuildCommand -Id 'qt-on-configure' `
            -FilePath $tools.cmake -Arguments ([object[]]@(
                    '-S', $source, '-B', $qtBuild,
                    '-DM0_QT_SHELL_SUPPORT=ON',
                    "-DCMAKE_PREFIX_PATH=$sdk",
                    "-DQt6_DIR=$qtConfig") + $commonConfigure) `
            -WorkingDirectory $repository -TimeoutSeconds 300 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'qt-on-build' `
            -FilePath $tools.cmake -Arguments ([object[]]@(
                    '--build', $qtBuild, '--config', 'Release', '--target', 'all')) `
            -WorkingDirectory $repository -TimeoutSeconds 1800 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'qt-on-qmllint' `
            -FilePath $tools.cmake -Arguments ([object[]]@(
                    '--build', $qtBuild, '--config', 'Release', '--target', 'all_qmllint')) `
            -WorkingDirectory $repository -TimeoutSeconds 600 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'qt-on-ctest-list' `
            -FilePath $tools.ctest -Arguments ([object[]]@(
                    '--test-dir', $qtBuild, '-C', 'Release', '--show-only=json-v1')) `
            -WorkingDirectory $repository -TimeoutSeconds 300 -ExpectedTestCount ([long]65)))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'qt-on-ctest' `
            -FilePath $tools.ctest -Arguments ([object[]]@(
                    '--test-dir', $qtBuild, '-C', 'Release', '--output-on-failure',
                    '--no-tests=error', '--output-junit',
                    (Join-Path $qtBuild 'ctest-results.junit.xml'))) `
            -WorkingDirectory $repository -TimeoutSeconds 900 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'core-off-configure' `
            -FilePath $tools.cmake -Arguments ([object[]]@(
                    '-S', $source, '-B', $coreBuild,
                    '-DM0_QT_SHELL_SUPPORT=OFF') + $commonConfigure) `
            -WorkingDirectory $repository -TimeoutSeconds 300 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'core-off-build' `
            -FilePath $tools.cmake -Arguments ([object[]]@(
                    '--build', $coreBuild, '--config', 'Release', '--target', 'm0_qt_core_tests')) `
            -WorkingDirectory $repository -TimeoutSeconds 1800 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'core-off-ctest-list' `
            -FilePath $tools.ctest -Arguments ([object[]]@(
                    '--test-dir', $coreBuild, '-C', 'Release', '--show-only=json-v1')) `
            -WorkingDirectory $repository -TimeoutSeconds 300 -ExpectedTestCount ([long]55)))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'core-off-ctest' `
            -FilePath $tools.ctest -Arguments ([object[]]@(
                    '--test-dir', $coreBuild, '-C', 'Release', '--output-on-failure',
                    '--no-tests=error', '--output-junit',
                    (Join-Path $coreBuild 'ctest-results.junit.xml'))) `
            -WorkingDirectory $repository -TimeoutSeconds 900 -ExpectedTestCount $null))
    $commands.Add((New-M0PackageV2BuildCommand -Id 'core-off-import-audit' `
            -FilePath $tools.dumpbin -Arguments ([object[]]@(
                    '/nologo', '/headers', '/dependents', $coreExecutable)) `
            -WorkingDirectory $coreBuild -TimeoutSeconds 300 -ExpectedTestCount $null))

    return [pscustomobject][ordered]@{
        schema_version = [long]2
        commands = [object[]]$commands.ToArray()
    }
}

function Assert-M0PackageV2CTestInventory {
    param(
        [Parameter(Mandatory)] [string] $Json,
        [Parameter(Mandatory)] [long] $ExpectedCount,
        [Parameter(Mandatory)] [string] $Context
    )
    if ([string]::IsNullOrWhiteSpace($Json)) { throw "$Context inventory JSON is empty." }
    if ($ExpectedCount -le 0) { throw "$Context expected test count must be positive." }
    try { $jsonDocument = [Text.Json.JsonDocument]::Parse($Json) }
    catch { throw "$Context inventory JSON is invalid: $($_.Exception.Message)" }
    try {
        if ($jsonDocument.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
            throw "$Context inventory must be one JSON object."
        }
        Assert-M0PackageV2NoDuplicateJsonProperties $jsonDocument.RootElement $Context
    }
    finally { $jsonDocument.Dispose() }
    try { $document = $Json | ConvertFrom-Json -Depth 100 -DateKind String -ErrorAction Stop }
    catch { throw "$Context inventory JSON is invalid: $($_.Exception.Message)" }
    Assert-M0PackageV2RequiredFields $document @('kind', 'version', 'tests') $Context
    if ($document.kind -cne 'ctestInfo') { throw "$Context inventory kind is invalid." }
    Assert-M0PackageV2RequiredFields $document.version @('major', 'minor') "$Context version"
    if ($document.version.major -isnot [long] -or $document.version.major -ne 1 -or
        $document.version.minor -isnot [long] -or $document.version.minor -lt 0) {
        throw "$Context inventory version is unsupported."
    }
    $tests = @($document.tests)
    if ($tests.Count -ne $ExpectedCount) {
        throw "$Context inventory expected $ExpectedCount tests, found $($tests.Count)."
    }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($test in $tests) {
        Assert-M0PackageV2RequiredFields $test @('name', 'properties') "$Context test"
        if ($test.name -isnot [string] -or [string]::IsNullOrWhiteSpace($test.name) -or
            -not $names.Add($test.name)) {
            throw "$Context contains an empty or duplicate test name."
        }
        foreach ($property in @($test.properties)) {
            Assert-M0PackageV2RequiredFields $property @('name', 'value') `
                "$Context test $($test.name) property"
            if ($property.name -isnot [string]) {
                throw "$Context test $($test.name) has a malformed property name."
            }
            if (($property.name -ieq 'DISABLED' -and $property.value -eq $true) -or
                $property.name -iin @('SKIP_REGULAR_EXPRESSION', 'SKIP_RETURN_CODE')) {
                throw "$Context test $($test.name) is disabled or can be skipped."
            }
        }
    }
    [string[]] $orderedNames = @($names)
    [Array]::Sort($orderedNames, [StringComparer]::Ordinal)
    return [pscustomobject][ordered]@{
        test_count = [long] $tests.Count
        names = [object[]] $orderedNames
    }
}

function Assert-M0PackageV2CTestJUnit {
    param(
        [Parameter(Mandatory)] [string] $Xml,
        [Parameter(Mandatory)] [long] $ExpectedCount,
        [Parameter(Mandatory)] [string] $Context
    )
    if ([string]::IsNullOrWhiteSpace($Xml)) { throw "$Context JUnit XML is empty." }
    $settings = [Xml.XmlReaderSettings]::new()
    $settings.DtdProcessing = [Xml.DtdProcessing]::Prohibit
    $settings.XmlResolver = $null
    $stringReader = [IO.StringReader]::new($Xml)
    $reader = $null
    $document = [Xml.XmlDocument]::new()
    $document.XmlResolver = $null
    try {
        $reader = [Xml.XmlReader]::Create($stringReader, $settings)
        $document.Load($reader)
    }
    catch { throw [IO.InvalidDataException]::new("$Context JUnit XML is invalid.", $_.Exception) }
    finally {
        if ($null -ne $reader) { $reader.Dispose() }
        $stringReader.Dispose()
    }
    if ($null -eq $document.DocumentElement -or
        $document.DocumentElement.LocalName -cne 'testsuite') {
        throw "$Context JUnit root is unsupported."
    }
    $rootSuite = $document.DocumentElement
    foreach ($attributeName in @('tests', 'failures', 'skipped', 'disabled')) {
        if ($null -eq $rootSuite.Attributes[$attributeName]) {
            throw "$Context JUnit root lacks its $attributeName aggregate."
        }
    }
    [long] $aggregateTests = 0
    if (-not [long]::TryParse(
            $rootSuite.Attributes['tests'].Value, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref] $aggregateTests) -or
        $aggregateTests -ne $ExpectedCount) {
        throw "$Context JUnit tests aggregate differs from the expected count."
    }
    $testCases = @($document.SelectNodes("//*[local-name()='testcase']"))
    if ($testCases.Count -ne $ExpectedCount) {
        throw "$Context JUnit expected $ExpectedCount tests, found $($testCases.Count)."
    }
    $junitNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $skippedElements = @($document.SelectNodes("//*[local-name()='skipped']"))
    if ($skippedElements.Count -ne 0) {
        throw "$Context JUnit contains skipped tests."
    }
    $failureElements = @($document.SelectNodes(
            "//*[local-name()='failure' or local-name()='error']"))
    if ($failureElements.Count -ne 0) {
        throw "$Context JUnit contains a failure or error element."
    }
    foreach ($node in @($document.SelectNodes("//*[local-name()='testsuite' or local-name()='testsuites']"))) {
        foreach ($attributeName in @('failures', 'errors', 'skipped', 'disabled')) {
            $attribute = $node.Attributes[$attributeName]
            if ($null -ne $attribute) {
                [long] $count = 0
                if (-not [long]::TryParse(
                        $attribute.Value, [Globalization.NumberStyles]::None,
                        [Globalization.CultureInfo]::InvariantCulture, [ref] $count) -or
                    $count -ne 0) {
                    throw "$Context JUnit reports non-zero $attributeName."
                }
            }
        }
    }
    foreach ($testCase in $testCases) {
        if ($null -eq $testCase.Attributes['name'] -or
            [string]::IsNullOrWhiteSpace($testCase.Attributes['name'].Value)) {
            throw "$Context JUnit contains an unnamed testcase."
        }
        if (-not $junitNames.Add([string] $testCase.Attributes['name'].Value)) {
            throw "$Context JUnit contains a duplicate testcase name."
        }
        $status = $testCase.Attributes['status']
        if ($null -eq $status -or $status.Value -cne 'run') {
            throw "$Context JUnit testcase is not explicitly status=run."
        }
        $disabled = $testCase.Attributes['disabled']
        if ($null -ne $disabled -and $disabled.Value -imatch '^(?:true|1|yes)$') {
            throw "$Context JUnit contains a disabled testcase."
        }
        foreach ($attributeName in @('status', 'result')) {
            $attribute = $testCase.Attributes[$attributeName]
            if ($null -ne $attribute -and
                $attribute.Value -imatch 'skip|disabled|not\s*run|notrun') {
                throw "$Context JUnit contains a skipped or not-run test."
            }
        }
    }
    [string[]] $orderedNames = @($junitNames)
    [Array]::Sort($orderedNames, [StringComparer]::Ordinal)
    return [pscustomobject][ordered]@{
        test_count = [long] $testCases.Count
        failures = [long] 0
        errors = [long] 0
        skipped = [long] 0
        disabled = [long] 0
        names = [object[]] $orderedNames
    }
}

function Assert-M0PackageV2CoreOffImports {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Records)
    if ($Records.Count -eq 0) { throw 'Core-off import audit has no PE records.' }
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $core = $null
    foreach ($record in $Records) {
        Assert-M0PackageV2RequiredFields $record @('relative_path', 'machine', 'imports') `
            'Core-off import record'
        Assert-M0PackageV2SafeRelativePath $record.relative_path 'Core-off import record'
        if (-not $paths.Add($record.relative_path)) {
            throw 'Core-off import audit contains duplicate or case-aliased records.'
        }
        if ($record.machine -isnot [string] -or $record.machine -cne 'x64') {
            throw "Core-off PE is not x64: $($record.relative_path)"
        }
        $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($import in @($record.imports)) {
            if ($import -isnot [string] -or [string]::IsNullOrWhiteSpace($import) -or
                -not $imports.Add($import)) {
                throw "Core-off PE has an empty or duplicate import: $($record.relative_path)"
            }
            if ($import -imatch '^qt[^\\/]*\.dll$') {
                throw "Core-off PE imports Qt: $($record.relative_path) -> $import"
            }
        }
        if ($record.relative_path -ceq 'm0_qt_core_tests.exe') {
            if ($null -ne $core) { throw 'Core-off import audit contains the core test executable twice.' }
            $core = $record.relative_path
        }
    }
    if ($null -eq $core) { throw 'Core-off import audit lacks m0_qt_core_tests.exe.' }
    return [pscustomobject][ordered]@{ core_executable = [string]$core }
}

function ConvertTo-M0PackageV2CanonicalDeploymentPlan {
    param([Parameter(Mandatory)] $Plan, [Parameter(Mandatory)] [string] $Context)
    Assert-M0PackageV2Fields $Plan @('schema_version', 'files') $Context
    if ($Plan.schema_version -isnot [long] -or $Plan.schema_version -ne 2) {
        throw "$Context schema version is invalid."
    }
    $rows = @($Plan.files)
    if ($rows.Count -eq 0) { throw "$Context has no deployment files." }
    $byTarget = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $rows) {
        Assert-M0PackageV2Fields $row @(
            'relative_path', 'source_kind', 'source_relative_path', 'size_bytes', 'sha256') `
            "$Context deployment row"
        Assert-M0PackageV2SafeRelativePath $row.relative_path "$Context target"
        Assert-M0PackageV2SafeRelativePath $row.source_relative_path "$Context source"
        if ($row.source_kind -isnot [string] -or
            $row.source_kind -cnotin @('composite-sdk', 'application-qml')) {
            throw "$Context has an invalid deployment source kind."
        }
        if ($row.size_bytes -isnot [long] -or $row.size_bytes -lt 0) {
            throw "$Context has an invalid deployment file size."
        }
        Assert-M0PackageV2Sha256 $row.sha256 "$Context deployment hash"
        if ($byTarget.ContainsKey($row.relative_path)) {
            throw "$Context contains a duplicate or case-aliased target."
        }
        $byTarget.Add($row.relative_path, [pscustomobject][ordered]@{
                relative_path = [string]$row.relative_path
                source_kind = [string]$row.source_kind
                source_relative_path = [string]$row.source_relative_path
                size_bytes = [long]$row.size_bytes
                sha256 = [string]$row.sha256
            })
    }
    [string[]]$keys = @($byTarget.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return [pscustomobject][ordered]@{
        schema_version = [long]2
        files = [object[]]@($keys | ForEach-Object { $byTarget[$_] })
    }
}

function ConvertTo-M0PackageV2DeploymentPlan {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Files,
        [Parameter(Mandatory)] [string] $CompositeSdkRoot,
        [Parameter(Mandatory)] [string] $ApplicationQmlRoot,
        [Parameter(Mandatory)] [string] $DestinationRoot
    )
    if ($Files.Count -eq 0) { throw 'Deployment source mapping is empty.' }
    $sdk = ConvertTo-M0PackageV2FullPath $CompositeSdkRoot
    $qml = ConvertTo-M0PackageV2FullPath $ApplicationQmlRoot
    $destination = ConvertTo-M0PackageV2FullPath $DestinationRoot
    foreach ($directory in @($sdk, $qml, $destination)) {
        if (-not [IO.Directory]::Exists($directory)) {
            throw "Deployment boundary is missing: $directory"
        }
    }
    if ((Test-M0PackageV2PathWithin $sdk $qml -AllowEqual) -or
        (Test-M0PackageV2PathWithin $qml $sdk -AllowEqual) -or
        (Test-M0PackageV2PathWithin $destination $sdk -AllowEqual) -or
        (Test-M0PackageV2PathWithin $destination $qml -AllowEqual)) {
        throw 'Deployment source and destination boundaries must be distinct and non-overlapping.'
    }

    $leases = [Collections.Generic.List[object]]::new()
    $fileLeases = [Collections.Generic.List[object]]::new()
    try {
        $rows = [Collections.Generic.List[object]]::new()
        foreach ($file in $Files) {
            Assert-M0PackageV2RequiredFields $file @('source', 'target') 'Deployment mapping'
            if ($file.source -isnot [string] -or $file.target -isnot [string]) {
                throw 'Deployment source and target must be paths.'
            }
            $source = ConvertTo-M0PackageV2FullPath $file.source
            $target = ConvertTo-M0PackageV2FullPath $file.target
            $sourceRoot = $null
            $sourceKind = $null
            if (Test-M0PackageV2PathWithin $source $sdk) {
                $sourceRoot = $sdk
                $sourceKind = 'composite-sdk'
            }
            elseif (Test-M0PackageV2PathWithin $source $qml) {
                $sourceRoot = $qml
                $sourceKind = 'application-qml'
            }
            else { throw "Deployment source is outside the authenticated roots: $source" }
            if (-not (Test-M0PackageV2PathWithin $target $destination)) {
                throw "Deployment target escaped its destination root: $target"
            }
            $sourceRelative = [IO.Path]::GetRelativePath($sourceRoot, $source).Replace('\', '/')
            $targetRelative = [IO.Path]::GetRelativePath($destination, $target).Replace('\', '/')
            Assert-M0PackageV2SafeRelativePath $sourceRelative 'Deployment source'
            Assert-M0PackageV2SafeRelativePath $targetRelative 'Deployment target'

            if ($IsWindows) {
                $lease = Open-M0PackageV2FileLease $source $sourceRoot `
                    "Deployment source $sourceRelative" -LeaseSink $leases -FileSink $fileLeases
                $size = [long]$lease.size_bytes
                $sha256 = [string]$lease.sha256
            }
            else {
                Assert-M0PackageV2NoReparseChain $source $sourceRoot `
                    "Deployment source $sourceRelative"
                $item = Get-Item -LiteralPath $source -Force -ErrorAction Stop
                if ($item -isnot [IO.FileInfo]) { throw "Deployment source is not a file: $source" }
                $size = [long]$item.Length
                $sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            $rows.Add([pscustomobject][ordered]@{
                    relative_path = $targetRelative
                    source_kind = $sourceKind
                    source_relative_path = $sourceRelative
                    size_bytes = $size
                    sha256 = $sha256
                })
        }
        $plan = ConvertTo-M0PackageV2CanonicalDeploymentPlan ([pscustomobject][ordered]@{
                schema_version = [long]2
                files = [object[]]$rows.ToArray()
            }) 'Deployment plan'
        if ($IsWindows) {
            Assert-M0PackageV2FileLeasesUnchanged $fileLeases 'Deployment plan final resweep'
        }
        return $plan
    }
    finally { Close-M0PackageV2Leases $leases }
}

function Assert-M0PackageV2DeploymentPlansEqual {
    param([Parameter(Mandatory)] $DryRun, [Parameter(Mandatory)] $Actual)
    $dryCanonical = ConvertTo-M0PackageV2CanonicalDeploymentPlan $DryRun `
        'Dry-run deployment plan'
    $actualCanonical = ConvertTo-M0PackageV2CanonicalDeploymentPlan $Actual `
        'Actual deployment plan'
    $dryBytes = $dryCanonical | ConvertTo-Json -Depth 10 -Compress
    $actualBytes = $actualCanonical | ConvertTo-Json -Depth 10 -Compress
    if (-not [string]::Equals($dryBytes, $actualBytes, [StringComparison]::Ordinal)) {
        throw 'Deployment changed between the dry-run and actual canonical plans.'
    }
}

function New-M0PackageV2ProcessStartInfo {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [string[]] $Arguments,
        [Parameter(Mandatory)] [string] $WorkingDirectory,
        [Parameter(Mandatory)] [Collections.IDictionary] $Environment
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false, $true)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false, $true)
    foreach ($argument in $Arguments) {
        if ($null -eq $argument -or $argument.Contains([char]0)) {
            throw 'A production process argument is null or contains NUL.'
        }
        [void] $start.ArgumentList.Add($argument)
    }
    $start.Environment.Clear()
    foreach ($entry in $Environment.GetEnumerator()) {
        if ($entry.Key -isnot [string] -or [string]::IsNullOrWhiteSpace($entry.Key) -or
            $entry.Value -isnot [string] -or $entry.Key.Contains('=') -or
            $entry.Key.Contains([char]0) -or $entry.Value.Contains([char]0)) {
            throw 'The production process environment contains an invalid entry.'
        }
        $start.Environment.Add([string] $entry.Key, [string] $entry.Value)
    }
    return $start
}

function Invoke-M0PackageV2BoundedProcess {
    param(
        [Parameter(Mandatory)] $Command,
        [Parameter(Mandatory)] [Collections.IDictionary] $Environment,
        [long] $CaptureLimitBytes = 16777216,
        [long] $TerminationTimeoutMilliseconds = 5000
    )
    if ($CaptureLimitBytes -le 0 -or $CaptureLimitBytes -gt [int]::MaxValue) {
        throw 'Production process capture limit is outside the supported Int32 byte boundary.'
    }
    if ($TerminationTimeoutMilliseconds -le 0 -or
        $TerminationTimeoutMilliseconds -gt [int]::MaxValue) {
        throw 'Production process termination timeout is outside the supported Int32 boundary.'
    }
    if ($Command.timeout_seconds -isnot [long] -or $Command.timeout_seconds -le 0 -or
        $Command.timeout_seconds -gt [Math]::Floor([int]::MaxValue / 1000)) {
        throw "Production command '$($Command.id)' timeout exceeds the process runner boundary."
    }
    $timeoutMilliseconds = [int] ([long] $Command.timeout_seconds * 1000)
    $start = New-M0PackageV2ProcessStartInfo -FilePath $Command.file_path `
        -Arguments ([string[]] @($Command.arguments)) `
        -WorkingDirectory $Command.working_directory -Environment $Environment
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    try {
        if (-not $process.Start()) { throw "Could not start production command '$($Command.id)'." }
        $stdoutTask = $script:PackageProcessIo::ReadBoundedAndDrainAsync(
            $process.StandardOutput.BaseStream, [int] $CaptureLimitBytes)
        $stderrTask = $script:PackageProcessIo::ReadBoundedAndDrainAsync(
            $process.StandardError.BaseStream, [int] $CaptureLimitBytes)
        $timedOut = -not $process.WaitForExit($timeoutMilliseconds)
        if ($timedOut) {
            try { $process.Kill($true) }
            catch {
                throw [InvalidOperationException]::new(
                    "Production command '$($Command.id)' timed out and its process tree could not be terminated.",
                    $_.Exception)
            }
            if (-not $process.WaitForExit([int] $TerminationTimeoutMilliseconds)) {
                throw [TimeoutException]::new(
                    "Production command '$($Command.id)' process tree did not terminate inside the bounded grace period.")
            }
        }
        try {
            $captureCompleted = [Threading.Tasks.Task]::WaitAll(
                [Threading.Tasks.Task[]] @($stdoutTask, $stderrTask),
                [int] $TerminationTimeoutMilliseconds)
        }
        catch {
            throw [IO.InvalidDataException]::new(
                "Production command '$($Command.id)' output capture failed.", $_.Exception)
        }
        if (-not $captureCompleted) {
            throw [TimeoutException]::new(
                "Production command '$($Command.id)' output pipes did not close inside the bounded grace period.")
        }
        $stdoutCapture = $stdoutTask.GetAwaiter().GetResult()
        $stderrCapture = $stderrTask.GetAwaiter().GetResult()
        $stopwatch.Stop()
        $utf8 = [Text.UTF8Encoding]::new($false, $true)
        if ($stdoutCapture.Exceeded -or $stderrCapture.Exceeded) {
            throw "Production command '$($Command.id)' exceeded the $CaptureLimitBytes-byte capture boundary."
        }
        try {
            $stdout = $utf8.GetString($stdoutCapture.Bytes)
            $stderr = $utf8.GetString($stderrCapture.Bytes)
        }
        catch {
            throw [IO.InvalidDataException]::new(
                "Production command '$($Command.id)' emitted invalid UTF-8.", $_.Exception)
        }
        return [pscustomobject][ordered]@{
            id = [string] $Command.id
            exit_code = if ($timedOut) { [long] -1 } else { [long] $process.ExitCode }
            stdout = [string] $stdout
            stderr = [string] $stderr
            timed_out = [bool] $timedOut
            elapsed_milliseconds = [long] $stopwatch.ElapsedMilliseconds
        }
    }
    finally {
        $stopwatch.Stop()
        $process.Dispose()
    }
}

function ConvertFrom-M0PackageV2PeReport {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $RelativePath
    )
    Assert-M0PackageV2SafeRelativePath $RelativePath 'PE report subject'
    $machineMatches = @([regex]::Matches(
            $Text, '(?im)^\s*8664\s+machine\s+\(x64\)\s*$'))
    if ($machineMatches.Count -ne 1) {
        throw "PE report is not unambiguously x64: $RelativePath"
    }
    $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($match in [regex]::Matches($Text, '(?im)^\s*([A-Za-z0-9_.-]+\.dll)\s*$')) {
        $name = [string] $match.Groups[1].Value
        if (-not $imports.Add($name)) {
            throw "PE report contains a duplicate import: $RelativePath -> $name"
        }
    }
    if ($imports.Count -eq 0) { throw "PE report has no imports: $RelativePath" }
    [string[]] $ordered = @($imports)
    [Array]::Sort($ordered, [StringComparer]::Ordinal)
    return [pscustomobject][ordered]@{
        relative_path = $RelativePath
        machine = 'x64'
        imports = [object[]] $ordered
    }
}

function Invoke-M0PackageV2BuildCommands {
    param(
        [Parameter(Mandatory)] $Plan,
        [Parameter(Mandatory)] $Environment,
        [Parameter(Mandatory)] [scriptblock] $RunAction
    )
    Assert-M0PackageV2Fields $Plan @('schema_version', 'commands') 'Build execution plan'
    if ($Plan.schema_version -isnot [long] -or $Plan.schema_version -ne 2) {
        throw 'Build execution plan schema drifted.'
    }
    $results = [Collections.Generic.List[object]]::new()
    $inventories = [ordered]@{}
    $junitProofs = [ordered]@{}
    $coreImports = $null
    foreach ($command in @($Plan.commands)) {
        $commandEnvironment = if ($Environment -is [Collections.IDictionary]) {
            $Environment
        }
        elseif ($command.id.StartsWith('qt-on-', [StringComparison]::Ordinal)) {
            $Environment.qt_on
        }
        elseif ($command.id.StartsWith('core-off-', [StringComparison]::Ordinal)) {
            $Environment.core_off
        }
        else {
            throw "Production command '$($command.id)' has no closed environment class."
        }
        $result = & $RunAction $command $commandEnvironment
        if ($null -eq $result) { throw "Production command '$($command.id)' returned no result." }
        Assert-M0PackageV2RequiredFields $result @('exit_code', 'stdout', 'stderr', 'timed_out') `
            "Production command '$($command.id)' result"
        if ($result.timed_out -eq $true) {
            throw "Production command '$($command.id)' timed out."
        }
        if ($result.exit_code -isnot [long] -or $result.exit_code -ne 0) {
            throw "Production command '$($command.id)' failed with exit $($result.exit_code): $($result.stderr)"
        }
        if ($result.stdout -isnot [string] -or $result.stderr -isnot [string]) {
            throw "Production command '$($command.id)' returned non-text output."
        }
        if ($null -ne $command.expected_test_count) {
            $inventories[$command.id] = Assert-M0PackageV2CTestInventory `
                -Json $result.stdout -ExpectedCount $command.expected_test_count `
                -Context $command.id
        }
        if ($command.id -cin @('qt-on-ctest', 'core-off-ctest')) {
            $junitIndex = [Array]::IndexOf([object[]] $command.arguments, '--output-junit')
            if ($junitIndex -lt 0 -or $junitIndex + 1 -ge @($command.arguments).Count) {
                throw "Production command '$($command.id)' lacks --output-junit."
            }
            $junitPath = Assert-M0PackageV2AbsolutePlanPath `
                $command.arguments[$junitIndex + 1] "$($command.id) JUnit path"
            if (-not [IO.File]::Exists($junitPath)) {
                throw "Production command '$($command.id)' did not create JUnit evidence."
            }
            $expectedCount = if ($command.id -ceq 'qt-on-ctest') { [long] 65 } else { [long] 55 }
            $junitProof = Assert-M0PackageV2CTestJUnit `
                -Xml ([IO.File]::ReadAllText($junitPath, [Text.UTF8Encoding]::new($false, $true))) `
                -ExpectedCount $expectedCount -Context $command.id
            $inventoryId = if ($command.id -ceq 'qt-on-ctest') {
                'qt-on-ctest-list'
            }
            else { 'core-off-ctest-list' }
            if (-not $inventories.Contains($inventoryId) -or
                (@($inventories[$inventoryId].names) -join "`n") -cne
                    (@($junitProof.names) -join "`n")) {
                throw "Production command '$($command.id)' JUnit names differ from its authenticated CTest inventory."
            }
            $junitProofs[$command.id] = $junitProof
        }
        if ($command.id -ceq 'core-off-import-audit') {
            $record = ConvertFrom-M0PackageV2PeReport -Text $result.stdout `
                -RelativePath 'm0_qt_core_tests.exe'
            $coreImports = Assert-M0PackageV2CoreOffImports -Records @($record)
        }
        $results.Add($result)
    }
    if (@($Plan.commands | Where-Object id -CEQ 'core-off-import-audit').Count -eq 1 -and
        $null -eq $coreImports) {
        throw 'Core-off import audit did not produce a closed result.'
    }
    return [pscustomobject][ordered]@{
        results = [object[]] $results.ToArray()
        ctest_inventories = [pscustomobject] $inventories
        ctest_junit = [pscustomobject] $junitProofs
        core_off_imports = $coreImports
    }
}

function Assert-M0PackageV2DeployDiagnostics {
    param(
        [Parameter(Mandatory)] [AllowEmptyString()] [string] $StandardError,
        [Parameter(Mandatory)] [string] $CompositeSdkRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    if ([string]::IsNullOrWhiteSpace($StandardError)) {
        return [pscustomobject][ordered]@{ warning_count = [long] 0; warning = '' }
    }
    $catalog = (Join-Path (ConvertTo-M0PackageV2FullPath $CompositeSdkRoot) `
            'translations/catalogs.json').Replace('\', '/')
    $normalized = $StandardError.Trim().Replace('\', '/')
    $escapedCatalog = [regex]::Escape($catalog)
    $allowed = "\AWarning: Translations will not be available due to the following error\.(?:\r?\n)" +
        "Cannot open $escapedCatalog\.?\z"
    if ($normalized -cnotmatch $allowed) {
        throw "$Context produced unexpected windeployqt stderr diagnostic: $StandardError"
    }
    return [pscustomobject][ordered]@{
        warning_count = [long] 1
        warning = $normalized
    }
}

function New-M0PackageV2PublicationEntries {
    param(
        [Parameter(Mandatory)] [string] $SourceRoot,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [object[]] $Rows,
        [Parameter(Mandatory)] [string] $ReceiptRelativePath
    )
    $source = ConvertTo-M0PackageV2FullPath $SourceRoot
    Assert-M0PackageV2SafeRelativePath $ReceiptRelativePath 'Package receipt publication path'
    $byPath = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Rows) {
        Assert-M0PackageV2Fields $row @('relative_path', 'size_bytes', 'sha256') `
            'Package publication row'
        Assert-M0PackageV2SafeRelativePath $row.relative_path 'Package publication row path'
        if ($row.size_bytes -isnot [long] -or $row.size_bytes -lt 0) {
            throw 'Package publication row has an invalid size.'
        }
        Assert-M0PackageV2Sha256 $row.sha256 'Package publication row hash'
        if ($byPath.ContainsKey($row.relative_path)) {
            throw 'Package publication has a duplicate or case-aliased path.'
        }
        $byPath.Add($row.relative_path, $row)
    }
    if (-not $byPath.ContainsKey($ReceiptRelativePath)) {
        throw 'Package publication omits its final receipt.'
    }
    [string[]] $paths = @($byPath.Keys | Where-Object {
            -not [string]::Equals($_, $ReceiptRelativePath, [StringComparison]::OrdinalIgnoreCase)
        })
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    $paths = [string[]] @($paths + $ReceiptRelativePath)
    return [object[]] @($paths | ForEach-Object {
            $row = $byPath[$_]
            [pscustomobject][ordered]@{
                relative_path = [string] $row.relative_path
                size_bytes = [long] $row.size_bytes
                sha256 = [string] $row.sha256
            }
        })
}

function Assert-M0PackageV2ClosedVerifierResult {
    param(
        [Parameter(Mandatory)] $Result,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256
    )
    Assert-M0PackageV2Fields $Result @(
        'verified', 'package_id', 'contract_sha256', 'composite_receipt_sha256',
        'manifest_sha256', 'receipt_sha256', 'manifest_path', 'payload', 'evidence') `
        'Final package verifier result'
    if ($Result.verified -ne $true -or $Result.package_id -cne $script:PackageId -or
        $Result.contract_sha256 -cne $ExpectedContractSha256 -or
        $Result.composite_receipt_sha256 -cne $ExpectedCompositeReceiptSha256 -or
        $Result.receipt_sha256 -cne $ExpectedReceiptSha256) {
        throw 'Final package verifier result changed an authenticated identity.'
    }
    Assert-M0PackageV2Sha256 $Result.manifest_sha256 'Final package manifest hash'
    return $Result
}

function New-M0PackageV2ClosedBuildResult {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Candidate,
        [Parameter(Mandatory)] $Verified
    )
    $closedVerifier = Assert-M0PackageV2ClosedVerifierResult -Result $Verified `
        -ExpectedContractSha256 $Context.expected_contract_sha256 `
        -ExpectedCompositeReceiptSha256 $Context.expected_composite_receipt_sha256 `
        -ExpectedReceiptSha256 $Candidate.receipt_sha256
    $verifiedEntries = [object[]] @(@($closedVerifier.payload) + @($closedVerifier.evidence))
    $candidateJson = @($Candidate.entries) | ConvertTo-Json -Depth 10 -Compress
    $verifiedJson = $verifiedEntries | ConvertTo-Json -Depth 10 -Compress
    if (-not [string]::Equals($candidateJson, $verifiedJson, [StringComparison]::Ordinal)) {
        throw 'Final verifier inventory differs from the published candidate entries.'
    }
    $packageRoot = ConvertTo-M0PackageV2FullPath $Context.output_root
    $receiptPath = Resolve-M0PackageV2ContainedPath $packageRoot `
        $Candidate.receipt_relative_path 'Final package receipt path'
    $manifestPath = Resolve-M0PackageV2ContainedPath $packageRoot `
        $Candidate.manifest_relative_path 'Final package manifest path'
    if (-not [IO.File]::Exists($receiptPath) -or -not [IO.File]::Exists($manifestPath)) {
        throw 'Final package controls are missing after publication.'
    }
    return [pscustomobject][ordered]@{
        exit_code = [long] 0
        package_root = $packageRoot
        receipt_path = $receiptPath
        receipt_sha256 = [string] $Candidate.receipt_sha256
        manifest_path = $manifestPath
        manifest_sha256 = [string] $closedVerifier.manifest_sha256
        entries = $verifiedEntries
    }
}

function Invoke-M0PackageV2ProductionWorkflow {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Operations
    )
    Assert-M0PackageV2Fields $Operations @(
        'verify_composite', 'build_candidate', 'publish', 'verify_package') `
        'Production workflow operations'
    foreach ($name in $Operations.PSObject.Properties.Name) {
        if ($Operations.$name -isnot [scriptblock]) {
            throw "Production workflow operation '$name' is not callable."
        }
    }
    $pre = & $Operations.verify_composite $Context 'pre'
    if ($null -eq $pre -or $pre.verified -ne $true) {
        throw 'Composite pre-verification did not return verified=true.'
    }
    $candidate = & $Operations.build_candidate $Context
    if ($null -eq $candidate) { throw 'Production candidate build returned no result.' }
    Assert-M0PackageV2RequiredFields $candidate @(
        'source_root', 'manifest_relative_path', 'receipt_relative_path',
        'receipt_sha256', 'entries') `
        'Production package candidate'
    Assert-M0PackageV2Sha256 $candidate.receipt_sha256 'Production candidate receipt hash'
    if (@($candidate.entries).Count -eq 0 -or
        @($candidate.entries)[-1].relative_path -cne $candidate.receipt_relative_path) {
        throw 'Production candidate does not end with its receipt entry.'
    }
    $post = & $Operations.verify_composite $Context 'post'
    if ($null -eq $post -or $post.verified -ne $true) {
        throw 'Composite post-verification did not return verified=true.'
    }
    $publication = & $Operations.publish $Context $candidate
    if ($null -eq $publication -or $publication.exit_code -isnot [long] -or
        $publication.exit_code -ne 0) {
        throw 'Native package publication failed closed.'
    }
    $verified = & $Operations.verify_package $Context $candidate
    return New-M0PackageV2ClosedBuildResult -Context $Context `
        -Candidate $candidate -Verified $verified
}

function Open-M0PackageV2PinnedFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Pin,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Leases,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Files
    )
    $closedPin = Assert-M0PackageV2Pin $Pin $Context
    return Open-M0PackageV2FileLease -LiteralPath $LiteralPath `
        -BoundaryRoot $BoundaryRoot -Context $Context `
        -ExpectedSizeBytes $closedPin.size_bytes -ExpectedSha256 $closedPin.sha256 `
        -LeaseSink $Leases -FileSink $Files
}

function Assert-M0PackageV2FreshRoots {
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $BuildRoot,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $ScratchRoot
    )
    $repository = ConvertTo-M0PackageV2FullPath $RepositoryRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'RepositoryRoot is missing.' }
    $mutableWorkspace = ConvertTo-M0PackageV2FullPath (Join-Path $repository 'out/m0')
    $roots = [ordered]@{
        build_root = ConvertTo-M0PackageV2FullPath $BuildRoot
        output_root = ConvertTo-M0PackageV2FullPath $OutputRoot
        scratch_root = ConvertTo-M0PackageV2FullPath $ScratchRoot
    }
    foreach ($entry in $roots.GetEnumerator()) {
        if (-not (Test-M0PackageV2PathWithin $entry.Value $mutableWorkspace)) {
            throw "$($entry.Key) must be a strict descendant of the RepositoryRoot/out/m0 workspace."
        }
        if ([IO.File]::Exists($entry.Value) -or [IO.Directory]::Exists($entry.Value)) {
            throw "$($entry.Key) must be fresh and absent."
        }
        $parent = [IO.Path]::GetDirectoryName($entry.Value)
        if (-not [IO.Directory]::Exists($parent)) {
            throw "$($entry.Key) parent directory is missing."
        }
        Assert-M0PackageV2NoReparseChain $parent $repository "$($entry.Key) parent"
    }
    [string[]] $values = @($roots.Values)
    for ($left = 0; $left -lt $values.Count; $left++) {
        for ($right = $left + 1; $right -lt $values.Count; $right++) {
            if ((Test-M0PackageV2PathWithin $values[$left] $values[$right] -AllowEqual) -or
                (Test-M0PackageV2PathWithin $values[$right] $values[$left] -AllowEqual)) {
                throw 'BuildRoot, OutputRoot, and ScratchRoot must be distinct and non-overlapping.'
            }
        }
    }
    return [pscustomobject][ordered]@{
        build_root = [string] $roots.build_root
        output_root = [string] $roots.output_root
        scratch_root = [string] $roots.scratch_root
    }
}

function Get-M0PackageV2ProductionContext {
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $CompositeReceiptPath,
        [Parameter(Mandatory)] [string] $ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)] [string] $ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $BuildRoot,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $ScratchRoot,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Leases,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Files,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Directories
    )
    foreach ($anchor in @(
            @($ExpectedContractSha256, 'Package contract hash'),
            @($ExpectedCompositeReceiptSha256, 'Composite receipt hash'),
            @($ExpectedQtBaseReceiptSha256, 'Qtbase receipt hash'))) {
        Assert-M0PackageV2Sha256 $anchor[0] $anchor[1]
    }
    $repository = ConvertTo-M0PackageV2FullPath $RepositoryRoot
    $roots = Assert-M0PackageV2FreshRoots -RepositoryRoot $repository `
        -BuildRoot $BuildRoot -OutputRoot $OutputRoot -ScratchRoot $ScratchRoot
    $null = Open-M0PackageV2DirectoryLease -LiteralPath $repository `
        -BoundaryRoot $repository -Context 'Package repository root' `
        -LeaseSink $Leases -DirectorySink $Directories

    $expectedContractPath = Resolve-M0PackageV2ContainedPath $repository `
        'experiments/m0/candidates/qt/package-v2/qt-shell-composite-package-contract.v2.json' `
        'Package production contract path'
    $contractFull = ConvertTo-M0PackageV2FullPath $ContractPath
    if (-not [string]::Equals(
            $contractFull, $expectedContractPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ContractPath is not the canonical package-v2 contract path.'
    }
    $contractDocument = Read-M0PackageV2JsonLease -LiteralPath $contractFull `
        -ExpectedSha256 $ExpectedContractSha256 -BoundaryRoot $repository `
        -Context 'Package production contract' -LeaseSink $Leases -FileSink $Files
    $contract = $contractDocument.value
    Assert-M0PackageV2ProductionContract $contract

    $recipe = [ordered]@{}
    foreach ($pin in @($contract.recipe.files)) {
        $path = Resolve-M0PackageV2ContainedPath $repository $pin.relative_path `
            "Package recipe '$($pin.relative_path)'"
        $lease = Open-M0PackageV2PinnedFile -LiteralPath $path -Pin $pin `
            -BoundaryRoot $repository -Context "Package recipe '$($pin.relative_path)'" `
            -Leases $Leases -Files $Files
        $recipe[$pin.relative_path] = $lease.path
    }

    $compositeContractPin = Assert-M0PackageV2Pin $contract.inputs.composite_contract `
        'Package composite contract input'
    $compositeReceiptPin = Assert-M0PackageV2Pin $contract.inputs.composite_receipt `
        'Package composite receipt input'
    $qtbaseReceiptPin = Assert-M0PackageV2Pin $contract.inputs.qtbase_receipt `
        'Package Qtbase receipt input'
    if ($compositeReceiptPin.sha256 -cne $ExpectedCompositeReceiptSha256 -or
        $qtbaseReceiptPin.sha256 -cne $ExpectedQtBaseReceiptSha256) {
        throw 'Package external receipt anchors differ from the production contract.'
    }
    $expectedCompositeReceiptPath = Resolve-M0PackageV2ContainedPath $repository `
        $compositeReceiptPin.relative_path 'Package composite receipt input path'
    $compositeReceiptFull = ConvertTo-M0PackageV2FullPath $CompositeReceiptPath
    if (-not [string]::Equals(
            $compositeReceiptFull, $expectedCompositeReceiptPath,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CompositeReceiptPath differs from the production contract.'
    }
    $compositeContractPath = Resolve-M0PackageV2ContainedPath $repository `
        $compositeContractPin.relative_path 'Package composite contract input path'
    $compositeContractDocument = Read-M0PackageV2JsonLease `
        -LiteralPath $compositeContractPath -ExpectedSha256 $compositeContractPin.sha256 `
        -BoundaryRoot $repository -Context 'Package composite contract input' `
        -LeaseSink $Leases -FileSink $Files
    if ($compositeContractDocument.record.size_bytes -ne $compositeContractPin.size_bytes) {
        throw 'Package composite contract input size drifted.'
    }
    $compositeReceiptDocument = Read-M0PackageV2JsonLease `
        -LiteralPath $compositeReceiptFull -ExpectedSha256 $ExpectedCompositeReceiptSha256 `
        -BoundaryRoot $repository -Context 'Package composite receipt input' `
        -LeaseSink $Leases -FileSink $Files
    if ($compositeReceiptDocument.record.size_bytes -ne $compositeReceiptPin.size_bytes) {
        throw 'Package composite receipt input size drifted.'
    }
    $qtbaseReceiptPath = Resolve-M0PackageV2ContainedPath $repository `
        $qtbaseReceiptPin.relative_path 'Package Qtbase receipt input path'
    $qtbaseReceiptDocument = Read-M0PackageV2JsonLease `
        -LiteralPath $qtbaseReceiptPath -ExpectedSha256 $ExpectedQtBaseReceiptSha256 `
        -BoundaryRoot $repository -Context 'Package Qtbase receipt input' `
        -LeaseSink $Leases -FileSink $Files
    if ($qtbaseReceiptDocument.record.size_bytes -ne $qtbaseReceiptPin.size_bytes) {
        throw 'Package Qtbase receipt input size drifted.'
    }

    $composite = $compositeReceiptDocument.value
    Assert-M0PackageV2RequiredFields $composite @(
        'schema_version', 'contract_sha256', 'qtbase_receipt', 'sdk') `
        'Package composite receipt input'
    $compositeQtbase = Assert-M0PackageV2Pin $composite.qtbase_receipt `
        'Package composite receipt Qtbase input'
    if ($composite.schema_version -isnot [long] -or $composite.schema_version -ne 2 -or
        $composite.contract_sha256 -cne $compositeContractPin.sha256 -or
        $compositeQtbase.relative_path -cne $qtbaseReceiptPin.relative_path -or
        $compositeQtbase.size_bytes -ne $qtbaseReceiptPin.size_bytes -or
        $compositeQtbase.sha256 -cne $qtbaseReceiptPin.sha256) {
        throw 'Package composite receipt changed its contract or Qtbase identity.'
    }
    Assert-M0PackageV2RequiredFields $composite.sdk @(
        'root_relative_path', 'file_count', 'total_size_bytes') `
        'Package composite SDK receipt'
    Assert-M0PackageV2SafeRelativePath $composite.sdk.root_relative_path `
        'Package composite SDK root'
    if ($composite.sdk.root_relative_path -cne 'sdk' -or
        $composite.sdk.file_count -isnot [long] -or $composite.sdk.file_count -le 0 -or
        $composite.sdk.total_size_bytes -isnot [long] -or
        $composite.sdk.total_size_bytes -le 0) {
        throw 'Package composite SDK receipt is invalid.'
    }
    $compositeRunRoot = [IO.Path]::GetDirectoryName(
        [IO.Path]::GetDirectoryName($compositeReceiptFull))
    $compositeSdkRoot = Resolve-M0PackageV2ContainedPath $compositeRunRoot `
        $composite.sdk.root_relative_path 'Package composite SDK root'
    if (-not [IO.Directory]::Exists($compositeSdkRoot)) {
        throw 'Package composite SDK root is missing.'
    }

    $compositeContract = $compositeContractDocument.value
    Assert-M0PackageV2RequiredFields $compositeContract @('recipe', 'isolation') `
        'Package composite contract input'
    Assert-M0PackageV2RequiredFields $compositeContract.recipe @('files') `
        'Package composite contract recipe'
    $entrypointRows = @($compositeContract.recipe.files | Where-Object id -CEQ 'entrypoint')
    if ($entrypointRows.Count -ne 1) {
        throw 'Package composite contract lacks one entrypoint recipe pin.'
    }
    $entrypoint = $entrypointRows[0]
    Assert-M0PackageV2RequiredFields $entrypoint @(
        'id', 'relative_path', 'size_bytes', 'sha256') `
        'Package composite verifier entrypoint'
    $entrypointPin = [pscustomobject][ordered]@{
        relative_path = [string] $entrypoint.relative_path
        size_bytes = [long] $entrypoint.size_bytes
        sha256 = [string] $entrypoint.sha256
    }
    $compositeVerifierPath = Resolve-M0PackageV2ContainedPath $repository `
        $entrypointPin.relative_path 'Package composite verifier entrypoint path'
    $null = Open-M0PackageV2PinnedFile -LiteralPath $compositeVerifierPath `
        -Pin $entrypointPin -BoundaryRoot $repository `
        -Context 'Package composite verifier entrypoint' -Leases $Leases -Files $Files

    foreach ($sourceName in @('cmakelists', 'fixture_manifest', 'protocol')) {
        $pin = $contract.inputs.sources.$sourceName
        $path = Resolve-M0PackageV2ContainedPath $repository $pin.relative_path `
            "Package source '$sourceName'"
        $null = Open-M0PackageV2PinnedFile -LiteralPath $path -Pin $pin `
            -BoundaryRoot $repository -Context "Package source '$sourceName'" `
            -Leases $Leases -Files $Files
    }
    $catch2ArchivePin = [pscustomobject][ordered]@{
        relative_path = [string] $contract.inputs.catch2.archive_relative_path
        size_bytes = [long] $contract.inputs.catch2.archive_size_bytes
        sha256 = [string] $contract.inputs.catch2.archive_sha256
    }
    $catch2ArchivePath = Resolve-M0PackageV2ContainedPath $repository `
        $catch2ArchivePin.relative_path 'Package Catch2 archive path'
    $null = Open-M0PackageV2PinnedFile -LiteralPath $catch2ArchivePath `
        -Pin $catch2ArchivePin -BoundaryRoot $repository -Context 'Package Catch2 archive' `
        -Leases $Leases -Files $Files

    $tools = [ordered]@{}
    foreach ($tool in @($contract.inputs.tools)) {
        $path = if ($tool.path_kind -ceq 'absolute') {
            ConvertTo-M0PackageV2FullPath $tool.path
        }
        else {
            Resolve-M0PackageV2ContainedPath $compositeSdkRoot $tool.path `
                "Package tool '$($tool.id)' composite SDK path"
        }
        $pin = [pscustomobject][ordered]@{
            relative_path = if ($tool.path_kind -ceq 'absolute') {
                [IO.Path]::GetFileName($path)
            }
            else { [string] $tool.path }
            size_bytes = [long] $tool.size_bytes
            sha256 = [string] $tool.sha256
        }
        $boundary = if ($tool.path_kind -ceq 'absolute') {
            [IO.Path]::GetPathRoot($path)
        }
        else { $compositeSdkRoot }
        $null = Open-M0PackageV2PinnedFile -LiteralPath $path -Pin $pin `
            -BoundaryRoot $boundary -Context "Package tool '$($tool.id)'" `
            -Leases $Leases -Files $Files
        $tools[$tool.id] = $path
    }
    $qconfigPin = $contract.deployment.composite_qconfig
    $qconfigPath = Resolve-M0PackageV2ContainedPath $compositeSdkRoot `
        $qconfigPin.relative_path 'Package composite qconfig path'
    $null = Open-M0PackageV2PinnedFile -LiteralPath $qconfigPath -Pin $qconfigPin `
        -BoundaryRoot $compositeSdkRoot -Context 'Package composite qconfig' `
        -Leases $Leases -Files $Files

    $sbomSources = [ordered]@{}
    foreach ($sbom in @($contract.inputs.sbom_files)) {
        $path = Resolve-M0PackageV2ContainedPath $compositeSdkRoot `
            $sbom.source_relative_path "Package SPDX '$($sbom.id)' source"
        $pin = [pscustomobject][ordered]@{
            relative_path = [string] $sbom.source_relative_path
            size_bytes = [long] $sbom.size_bytes
            sha256 = [string] $sbom.sha256
        }
        $null = Open-M0PackageV2PinnedFile -LiteralPath $path -Pin $pin `
            -BoundaryRoot $compositeSdkRoot -Context "Package SPDX '$($sbom.id)'" `
            -Leases $Leases -Files $Files
        $sbomSources[$sbom.id] = $path
    }

    return [pscustomobject][ordered]@{
        repository_root = $repository
        contract_path = $contractFull
        contract = $contract
        expected_contract_sha256 = $ExpectedContractSha256
        composite_contract_path = $compositeContractPath
        expected_composite_contract_sha256 = $compositeContractPin.sha256
        composite_receipt_path = $compositeReceiptFull
        expected_composite_receipt_sha256 = $ExpectedCompositeReceiptSha256
        qtbase_receipt_path = $qtbaseReceiptPath
        expected_qtbase_receipt_sha256 = $ExpectedQtBaseReceiptSha256
        composite_verifier_path = $compositeVerifierPath
        composite_sdk_root = $compositeSdkRoot
        composite_sdk_file_count = [long] $composite.sdk.file_count
        composite_sdk_total_size_bytes = [long] $composite.sdk.total_size_bytes
        build_root = $roots.build_root
        output_root = $roots.output_root
        scratch_root = $roots.scratch_root
        source_root = Resolve-M0PackageV2ContainedPath $repository `
            $contract.inputs.sources.shell_tree.relative_path 'Package shell source root'
        catch2_archive_path = $catch2ArchivePath
        tools = [pscustomobject] $tools
        recipe = [pscustomobject] $recipe
        native_module_path = [string] $recipe[
            'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageNative.psm1']
        qt_conf_path = [string] $recipe[
            'experiments/m0/candidates/qt/package-v2/qt.conf']
        limitations_path = [string] $recipe[
            'experiments/m0/candidates/qt/package-v2/PACKAGE-LIMITATIONS.txt']
        sbom_sources = [pscustomobject] $sbomSources
        retained_leases = $Leases
        retained_files = $Files
        retained_directories = $Directories
        source_file_leases = [Collections.Generic.Dictionary[string, object]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        source_tree_envelopes = [Collections.Generic.List[object]]::new()
    }
}

function Get-M0PackageV2GitBlobSha1FromLease {
    param([Parameter(Mandatory)] $Lease)
    if ($Lease.lease_kind -cne 'file' -or $Lease.stream -isnot [IO.FileStream] -or
        -not $Lease.stream.CanRead -or -not $Lease.stream.CanSeek) {
        throw 'Git source blob authentication requires one retained seekable file lease.'
    }
    $stream = $Lease.stream
    $position = $stream.Position
    $hash = [Security.Cryptography.IncrementalHash]::CreateHash(
        [Security.Cryptography.HashAlgorithmName]::SHA1)
    try {
        $header = [Text.Encoding]::ASCII.GetBytes("blob $($stream.Length)`0")
        $hash.AppendData($header)
        $stream.Position = 0
        $buffer = [byte[]]::new(65536)
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -ne 0) {
            $hash.AppendData($buffer, 0, $count)
        }
        return [Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
    }
    finally {
        $stream.Position = $position
        $hash.Dispose()
    }
}

function ConvertFrom-M0PackageV2GitTreeListing {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [Collections.IDictionary] $TreeInputs,
        [Parameter(Mandatory)] [string] $Context
    )
    if ([string]::IsNullOrEmpty($Text)) { throw "$Context Git tree listing is empty." }
    $entries = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($Text.Split([char] 0) | Where-Object { $_.Length -ne 0 })) {
        $match = [regex]::Match(
            $record, '^(?<mode>[0-9]{6}) (?<type>[a-z]+) (?<blob>[0-9a-f]{40})\t(?<path>.+)$',
            [Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if (-not $match.Success -or $match.Groups['type'].Value -cne 'blob' -or
            $match.Groups['mode'].Value -cnotin @('100644', '100755')) {
            throw "$Context Git tree contains a link, submodule, or malformed entry."
        }
        $path = [string] $match.Groups['path'].Value
        Assert-M0PackageV2SafeRelativePath $path "$Context Git tree path"
        $owners = @($TreeInputs.Values | Where-Object {
                $path.StartsWith(
                    ([string] $_.relative_path) + '/', [StringComparison]::Ordinal)
            })
        if ($owners.Count -ne 1) {
            throw "$Context Git tree entry escaped or ambiguously crossed its pinned roots."
        }
        if ($entries.ContainsKey($path)) {
            throw "$Context Git tree contains a duplicate or case-aliased path."
        }
        $entries.Add($path, [pscustomobject][ordered]@{
                relative_path = $path
                mode = [string] $match.Groups['mode'].Value
                git_blob = [string] $match.Groups['blob'].Value
            })
    }
    if ($entries.Count -eq 0) { throw "$Context Git tree listing has no source files." }
    return ,$entries
}

function Assert-M0PackageV2GitIndexListing {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] $ExpectedEntries,
        [Parameter(Mandatory)] [string] $Context
    )
    $paths = [Collections.Generic.Dictionary[string, string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($Text.Split([char] 0) | Where-Object { $_.Length -ne 0 })) {
        $match = [regex]::Match($record, '^(?<tag>.?) (?<path>.+)$',
            [Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if (-not $match.Success -or $match.Groups['tag'].Value -cne 'H') {
            throw "$Context Git index contains assume-unchanged, skip-worktree, or another unsafe flag."
        }
        $path = [string] $match.Groups['path'].Value
        Assert-M0PackageV2SafeRelativePath $path "$Context Git index path"
        if (-not $paths.TryAdd($path, $path)) {
            throw "$Context Git index contains a duplicate or case-aliased path."
        }
    }
    if ($paths.Count -ne $ExpectedEntries.Count -or
        -not [Collections.Generic.HashSet[string]]::new(
            [string[]] $paths.Keys, [StringComparer]::OrdinalIgnoreCase).SetEquals(
                [string[]] $ExpectedEntries.Keys)) {
        throw "$Context Git index differs from the authenticated commit tree."
    }
}

function Get-M0PackageV2PhysicalSourceProof {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [Collections.IDictionary] $TreeInputs,
        [Parameter(Mandatory)] $ExpectedEntries,
        [Parameter(Mandatory)] [ValidateSet('pre', 'post')] [string] $Phase
    )
    foreach ($property in @(
            'retained_leases', 'retained_files', 'retained_directories',
            'source_file_leases', 'source_tree_envelopes')) {
        if ($null -eq $Context.PSObject.Properties[$property]) {
            throw "Package physical source context lacks '$property'."
        }
    }
    if ($Phase -ceq 'pre') {
        if ($Context.source_file_leases.Count -ne 0 -or
            $Context.source_tree_envelopes.Count -ne 0) {
            throw 'Package physical source pre-authentication was invoked more than once.'
        }
        foreach ($treeEntry in $TreeInputs.GetEnumerator()) {
            $treeRoot = Resolve-M0PackageV2ContainedPath $Context.repository_root `
                $treeEntry.Value.relative_path "Package physical source '$($treeEntry.Key)' root"
            $prefix = ([string] $treeEntry.Value.relative_path) + '/'
            $rows = [Collections.Generic.List[object]]::new()
            foreach ($expected in @($ExpectedEntries.Values | Where-Object {
                        $_.relative_path.StartsWith($prefix, [StringComparison]::Ordinal)
                    })) {
                $relative = $expected.relative_path.Substring($prefix.Length)
                Assert-M0PackageV2SafeRelativePath $relative `
                    "Package physical source '$($treeEntry.Key)' leaf"
                $path = Resolve-M0PackageV2ContainedPath $treeRoot $relative `
                    "Package physical source '$($treeEntry.Key)' leaf"
                $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
                if ($item -isnot [IO.FileInfo]) {
                    throw "Package physical source '$($treeEntry.Key)' contains a non-file leaf."
                }
                $rows.Add([pscustomobject][ordered]@{
                        relative_path = $relative
                        size_bytes = [long] $item.Length
                        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                    })
            }
            if ($rows.Count -eq 0) {
                throw "Package physical source '$($treeEntry.Key)' is empty."
            }
            $fileStart = $Context.retained_files.Count
            $envelope = Get-M0PackageV2TreeEnvelope -Root $treeRoot `
                -Rows ([object[]] $rows.ToArray()) `
                -Context "Package physical source '$($treeEntry.Key)'" `
                -LeaseSink $Context.retained_leases -FileSink $Context.retained_files `
                -DirectorySink $Context.retained_directories
            $Context.source_tree_envelopes.Add($envelope)
            for ($index = $fileStart; $index -lt $Context.retained_files.Count; $index++) {
                $lease = $Context.retained_files[$index]
                $repositoryRelative = [IO.Path]::GetRelativePath(
                    $Context.repository_root, $lease.path).Replace('\', '/')
                if (-not $ExpectedEntries.ContainsKey($repositoryRelative) -or
                    $Context.source_file_leases.ContainsKey($repositoryRelative)) {
                    throw 'Package physical source lease escaped or duplicated its Git inventory.'
                }
                $expectedBlob = $ExpectedEntries[$repositoryRelative].git_blob
                if ((Get-M0PackageV2GitBlobSha1FromLease $lease) -cne $expectedBlob) {
                    throw "Package worktree bytes drifted from the authenticated Git blob: $repositoryRelative"
                }
                $Context.source_file_leases.Add($repositoryRelative, $lease)
            }
        }
    }
    else {
        if ($Context.source_tree_envelopes.Count -ne $TreeInputs.Count -or
            $Context.source_file_leases.Count -ne $ExpectedEntries.Count) {
            throw 'Package physical source post-authentication lacks its retained pre-state.'
        }
        foreach ($envelope in $Context.source_tree_envelopes) {
            Assert-M0PackageV2TreeEnvelopeUnchanged $envelope `
                'Package physical source post-authentication'
        }
    }
    Assert-M0PackageV2FileLeasesUnchanged $Context.retained_files `
        "Package physical source $Phase file resweep"
    Assert-M0PackageV2DirectoryLeasesUnchanged $Context.retained_directories `
        "Package physical source $Phase directory resweep"
    if ($Context.source_file_leases.Count -ne $ExpectedEntries.Count) {
        throw "Package physical source $Phase inventory is incomplete."
    }
    $proofRows = [Collections.Generic.List[object]]::new()
    [string[]] $paths = @($ExpectedEntries.Keys)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    foreach ($path in $paths) {
        if (-not $Context.source_file_leases.ContainsKey($path)) {
            throw "Package physical source $Phase is missing a retained Git leaf."
        }
        $lease = $Context.source_file_leases[$path]
        $gitBlob = Get-M0PackageV2GitBlobSha1FromLease $lease
        if ($gitBlob -cne $ExpectedEntries[$path].git_blob) {
            throw "Package worktree bytes drifted from the authenticated Git blob: $path"
        }
        $proofRows.Add([pscustomobject][ordered]@{
                relative_path = $path
                size_bytes = [long] $lease.size_bytes
                sha256 = [string] $lease.sha256
                git_blob = $gitBlob
            })
    }
    $proofJson = [object[]] $proofRows.ToArray() | ConvertTo-Json -Depth 10 -Compress
    return [pscustomobject][ordered]@{
        file_count = [long] $proofRows.Count
        physical_inventory_sha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData(
                [Text.Encoding]::UTF8.GetBytes($proofJson))).ToLowerInvariant()
    }
}

function Assert-M0PackageV2GitSources {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [Collections.IDictionary] $Environment,
        [Parameter(Mandatory)] [scriptblock] $RunAction,
        [Parameter(Mandatory)] [ValidateSet('pre', 'post')] [string] $Phase,
        [scriptblock] $PhysicalProofAction
    )
    $treeInputs = [ordered]@{
        shell_tree = $Context.contract.inputs.sources.shell_tree
        fixture_tree = $Context.contract.inputs.sources.fixture_tree
        protocol_tree = $Context.contract.inputs.sources.protocol_tree
    }
    $headCommand = New-M0PackageV2BuildCommand -Id "source-$Phase-head" `
        -FilePath $Context.tools.git -Arguments ([object[]] @(
                '-C', $Context.repository_root, 'rev-parse', 'HEAD')) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
        -ExpectedTestCount $null
    $headResult = & $RunAction $headCommand $Environment
    $head = if ($null -eq $headResult) { '' } else { ([string] $headResult.stdout).Trim() }
    if ($null -eq $headResult -or $headResult.timed_out -eq $true -or
        $headResult.exit_code -ne 0 -or
        -not [string]::IsNullOrWhiteSpace($headResult.stderr) -or
        $head -cnotmatch '^[0-9a-f]{40}$') {
        throw "Package source HEAD could not be authenticated during $Phase."
    }
    $sourcePaths = [string[]] @($treeInputs.Values | ForEach-Object { $_.relative_path })
    $statusCommand = New-M0PackageV2BuildCommand -Id "source-$Phase-status" `
        -FilePath $Context.tools.git -Arguments ([object[]] @(
                '-C', $Context.repository_root, 'status', '--porcelain=v1',
                '--untracked-files=all', '--') + $sourcePaths) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
        -ExpectedTestCount $null
    $status = & $RunAction $statusCommand $Environment
    if ($null -eq $status -or $status.timed_out -eq $true -or
        $status.exit_code -ne 0 -or -not [string]::IsNullOrWhiteSpace($status.stderr)) {
        throw "Package source $Phase Git status failed closed."
    }
    if (-not [string]::IsNullOrWhiteSpace($status.stdout)) {
        throw "Package source paths are dirty during $Phase authentication."
    }
    $trees = [ordered]@{}
    foreach ($entry in $treeInputs.GetEnumerator()) {
        $command = New-M0PackageV2BuildCommand -Id "source-$Phase-$($entry.Key.Replace('_', '-'))" `
            -FilePath $Context.tools.git -Arguments ([object[]] @(
                    '-C', $Context.repository_root, 'rev-parse',
                    "${head}:$($entry.Value.relative_path)")) `
            -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
            -ExpectedTestCount $null
        $result = & $RunAction $command $Environment
        $treeHash = if ($null -eq $result) { '' } else { ([string] $result.stdout).Trim() }
        if ($null -eq $result -or $result.timed_out -eq $true -or
            $result.exit_code -ne 0 -or -not [string]::IsNullOrWhiteSpace($result.stderr) -or
            $treeHash -cne $entry.Value.git_tree) {
            throw "Package source tree '$($entry.Key)' drifted during $Phase authentication."
        }
        $trees[$entry.Key] = $treeHash
    }
    $listingCommand = New-M0PackageV2BuildCommand -Id "source-$Phase-tree-files" `
        -FilePath $Context.tools.git -Arguments ([object[]] @(
                '-C', $Context.repository_root, 'ls-tree', '-r', '-z',
                '--full-tree', $head, '--') + $sourcePaths) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
        -ExpectedTestCount $null
    $listingResult = & $RunAction $listingCommand $Environment
    if ($null -eq $listingResult -or $listingResult.timed_out -eq $true -or
        $listingResult.exit_code -ne 0 -or
        -not [string]::IsNullOrWhiteSpace($listingResult.stderr)) {
        throw "Package source $Phase Git tree listing failed closed."
    }
    $expectedEntries = ConvertFrom-M0PackageV2GitTreeListing `
        -Text $listingResult.stdout -TreeInputs $treeInputs `
        -Context "Package source $Phase"

    $indexCommand = New-M0PackageV2BuildCommand -Id "source-$Phase-index-flags" `
        -FilePath $Context.tools.git -Arguments ([object[]] @(
                '-C', $Context.repository_root, 'ls-files', '-v', '-z', '--') +
            $sourcePaths) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
        -ExpectedTestCount $null
    $indexResult = & $RunAction $indexCommand $Environment
    if ($null -eq $indexResult -or $indexResult.timed_out -eq $true -or
        $indexResult.exit_code -ne 0 -or
        -not [string]::IsNullOrWhiteSpace($indexResult.stderr)) {
        throw "Package source $Phase Git index listing failed closed."
    }
    Assert-M0PackageV2GitIndexListing -Text $indexResult.stdout `
        -ExpectedEntries $expectedEntries -Context "Package source $Phase"
    $physicalProof = if ($null -eq $PhysicalProofAction) {
        Get-M0PackageV2PhysicalSourceProof -Context $Context `
            -TreeInputs $treeInputs -ExpectedEntries $expectedEntries -Phase $Phase
    }
    else {
        & $PhysicalProofAction $Context $treeInputs $expectedEntries $Phase
    }
    Assert-M0PackageV2Fields $physicalProof @(
        'file_count', 'physical_inventory_sha256') "Package source $Phase physical proof"
    if ($physicalProof.file_count -isnot [long] -or $physicalProof.file_count -le 0) {
        throw "Package source $Phase physical proof has an invalid file count."
    }
    Assert-M0PackageV2Sha256 $physicalProof.physical_inventory_sha256 `
        "Package source $Phase physical inventory"

    $headConfirmCommand = New-M0PackageV2BuildCommand -Id "source-$Phase-head-confirm" `
        -FilePath $Context.tools.git -Arguments ([object[]] @(
                '-C', $Context.repository_root, 'rev-parse', 'HEAD')) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
        -ExpectedTestCount $null
    $headConfirmResult = & $RunAction $headConfirmCommand $Environment
    $headConfirm = if ($null -eq $headConfirmResult) {
        ''
    }
    else { ([string] $headConfirmResult.stdout).Trim() }
    if ($null -eq $headConfirmResult -or $headConfirmResult.timed_out -eq $true -or
        $headConfirmResult.exit_code -ne 0 -or
        -not [string]::IsNullOrWhiteSpace($headConfirmResult.stderr) -or
        $headConfirm -cnotmatch '^[0-9a-f]{40}$' -or $headConfirm -cne $head) {
        throw "Package source HEAD changed or could not be authenticated during $Phase."
    }
    return [pscustomobject][ordered]@{
        phase = $Phase
        source_commit = $head
        clean = $true
        trees = [pscustomobject] $trees
        file_count = [long] $physicalProof.file_count
        physical_inventory_sha256 = [string] $physicalProof.physical_inventory_sha256
    }
}

function Get-M0PackageV2ParentPath {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [int] $Levels)
    $current = ConvertTo-M0PackageV2FullPath $LiteralPath
    for ($index = 0; $index -lt $Levels; $index++) {
        $parent = [IO.Directory]::GetParent($current)
        if ($null -eq $parent) { throw 'Could not derive an authenticated toolchain root.' }
        $current = $parent.FullName
    }
    return $current
}

function New-M0PackageV2BuildEnvironment {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $PrivateTempRoot
    )
    $clDirectory = [IO.Path]::GetDirectoryName([string] $Context.tools.cl)
    $vcToolsRoot = Get-M0PackageV2ParentPath $clDirectory 3
    $rcDirectory = [IO.Path]::GetDirectoryName([string] $Context.tools.rc)
    $windowsKitRoot = Get-M0PackageV2ParentPath $rcDirectory 3
    $sdkVersion = $script:WindowsSdkVersion
    $sdkInclude = Join-Path $windowsKitRoot "Include/$sdkVersion"
    $sdkLib = Join-Path $windowsKitRoot "Lib/$sdkVersion"
    $systemRoot = ConvertTo-M0PackageV2FullPath ([Environment]::GetFolderPath(
            [Environment+SpecialFolder]::Windows))
    $system32 = Join-Path $systemRoot 'System32'
    $corePathEntries = @(
        $clDirectory,
        [IO.Path]::GetDirectoryName([string] $Context.tools.rc),
        [IO.Path]::GetDirectoryName([string] $Context.tools.cmake),
        [IO.Path]::GetDirectoryName([string] $Context.tools.ninja),
        [IO.Path]::GetDirectoryName([string] $Context.tools.git),
        $system32
    )
    [string[]] $canonicalCorePathEntries = @($corePathEntries | ForEach-Object {
            ConvertTo-M0PackageV2FullPath $_
        } | Select-Object -Unique)
    [string[]] $canonicalQtPathEntries = @(
        (ConvertTo-M0PackageV2FullPath (Join-Path $Context.composite_sdk_root 'bin'))
        $canonicalCorePathEntries
    )
    $includeEntries = @(
        (Join-Path $vcToolsRoot 'include'),
        (Join-Path $sdkInclude 'ucrt'),
        (Join-Path $sdkInclude 'shared'),
        (Join-Path $sdkInclude 'um'),
        (Join-Path $sdkInclude 'winrt'),
        (Join-Path $sdkInclude 'cppwinrt')
    )
    $libEntries = @(
        (Join-Path $vcToolsRoot 'lib/x64'),
        (Join-Path $sdkLib 'ucrt/x64'),
        (Join-Path $sdkLib 'um/x64')
    )
    foreach ($directory in @($canonicalQtPathEntries + $includeEntries + $libEntries)) {
        if (-not [IO.Directory]::Exists($directory)) {
            throw "Closed MSVC/SDK environment directory is missing: $directory"
        }
    }
    $common = [ordered]@{
        SystemRoot = $systemRoot
        WINDIR = $systemRoot
        COMSPEC = Join-Path $system32 'cmd.exe'
        TEMP = $PrivateTempRoot
        TMP = $PrivateTempRoot
        LOCALAPPDATA = Join-Path $PrivateTempRoot 'local-app-data'
        APPDATA = Join-Path $PrivateTempRoot 'app-data'
        USERPROFILE = Join-Path $PrivateTempRoot 'profile'
        PATH = $canonicalCorePathEntries -join ';'
        INCLUDE = $includeEntries -join ';'
        LIB = $libEntries -join ';'
        LIBPATH = (Join-Path $vcToolsRoot 'lib/x64')
        WindowsSdkDir = "$windowsKitRoot\"
        WindowsSDKVersion = "$sdkVersion\"
        UniversalCRTSdkDir = "$windowsKitRoot\"
        UCRTVersion = $sdkVersion
        VCToolsInstallDir = "$vcToolsRoot\"
        VCToolsVersion = '14.44.35207'
        VSCMD_ARG_HOST_ARCH = 'x64'
        VSCMD_ARG_TGT_ARCH = 'x64'
        PreferredToolArchitecture = 'x64'
        Platform = 'x64'
        VisualStudioVersion = '18.0'
        VSLANG = '1033'
        GIT_CONFIG_NOSYSTEM = '1'
        GIT_CONFIG_GLOBAL = 'NUL'
        GIT_OPTIONAL_LOCKS = '0'
        LC_ALL = 'C'
    }
    $core = [ordered]@{}
    foreach ($entry in $common.GetEnumerator()) { $core[$entry.Key] = $entry.Value }
    $qt = [ordered]@{}
    foreach ($entry in $common.GetEnumerator()) { $qt[$entry.Key] = $entry.Value }
    $qt.PATH = $canonicalQtPathEntries -join ';'
    $qt.QT_QPA_PLATFORM = 'offscreen'
    $qt.QT_QUICK_BACKEND = 'software'
    $qt.QT_QUICK_CONTROLS_STYLE = 'Basic'
    $qt.QT_FATAL_WARNINGS = '1'
    $qt.QT_FORCE_STDERR_LOGGING = '1'
    $qt.QML_DISABLE_DISK_CACHE = '1'
    $qt.QT_HASH_SEED = '0'
    foreach ($entry in $core.GetEnumerator()) {
        if ($entry.Key -match '^(?:QT|QML)' -or
            ([string] $entry.Value).Contains(
                $Context.composite_sdk_root, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Core-off environment contains a Qt key or composite SDK path.'
        }
    }
    return [pscustomobject][ordered]@{
        git = $core
        core_off = $core
        qt_on = $qt
        deployment = $qt
    }
}

function ConvertTo-M0PackageV2RedactedEnvironment {
    param([Parameter(Mandatory)] [Collections.IDictionary] $Environment)
    [string[]] $keys = @($Environment.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return [object[]] @($keys | ForEach-Object {
            $value = [string] $Environment[$_]
            [pscustomobject][ordered]@{
                name = $_
                value_sha256 = [Convert]::ToHexString(
                    [Security.Cryptography.SHA256]::HashData(
                        [Text.Encoding]::UTF8.GetBytes($value))).ToLowerInvariant()
                value = if ($_ -cin @(
                        'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND',
                        'QT_QUICK_CONTROLS_STYLE', 'QT_FATAL_WARNINGS',
                        'QT_FORCE_STDERR_LOGGING', 'QML_DISABLE_DISK_CACHE')) {
                    $value
                }
                else { '<redacted>' }
            }
        })
}

function Expand-M0PackageV2Catch2Archive {
    param(
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $DestinationRoot
    )
    $destination = ConvertTo-M0PackageV2FullPath $DestinationRoot
    if ([IO.File]::Exists($destination) -or [IO.Directory]::Exists($destination)) {
        throw 'Fresh Catch2 extraction destination already exists.'
    }
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    $archiveStream = [IO.File]::Open(
        (ConvertTo-M0PackageV2FullPath $ArchivePath), [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $gzip = $null
    $reader = $null
    $fileCount = [long] 0
    $pathIndex = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $explicitEntries = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    try {
        $gzip = [IO.Compression.GZipStream]::new(
            $archiveStream, [IO.Compression.CompressionMode]::Decompress, $false)
        $reader = [System.Formats.Tar.TarReader]::new($gzip, $false)
        while ($null -ne ($entry = $reader.GetNextEntry())) {
            if ($entry.EntryType -eq [System.Formats.Tar.TarEntryType]::GlobalExtendedAttributes) {
                continue
            }
            $name = [string] $entry.Name
            if (-not $name.StartsWith('Catch2-3.15.3/', [StringComparison]::Ordinal) -or
                $name.Contains('\') -or $name.Contains([char]0)) {
                throw "Catch2 archive has an unexpected entry path: $name"
            }
            $relative = $name.Substring('Catch2-3.15.3/'.Length).TrimEnd('/')
            if ([string]::IsNullOrEmpty($relative)) {
                if ($entry.EntryType -ne [System.Formats.Tar.TarEntryType]::Directory) {
                    throw 'Catch2 archive root is not a directory.'
                }
                continue
            }
            Assert-M0PackageV2SafeRelativePath $relative 'Catch2 archive entry'
            $segments = $relative.Split('/')
            for ($segmentIndex = 0; $segmentIndex -lt $segments.Count; $segmentIndex++) {
                $indexedPath = ($segments[0..$segmentIndex] -join '/')
                $isLeaf = $segmentIndex -eq ($segments.Count - 1)
                $kind = if ($isLeaf -and
                    $entry.EntryType -eq [System.Formats.Tar.TarEntryType]::RegularFile) {
                    'file'
                }
                else { 'directory' }
                if ($pathIndex.ContainsKey($indexedPath)) {
                    $existing = $pathIndex[$indexedPath]
                    if ($existing.path -cne $indexedPath) {
                        throw "Catch2 archive contains a case-aliased path: $relative"
                    }
                    if ($existing.kind -cne $kind -and ($isLeaf -or $existing.kind -ceq 'file')) {
                        throw "Catch2 archive contains a file/directory collision: $relative"
                    }
                }
                else {
                    $pathIndex.Add($indexedPath, [pscustomobject][ordered]@{
                            path = $indexedPath; kind = $kind
                        })
                }
            }
            if (-not $explicitEntries.Add($relative)) {
                throw "Catch2 archive contains a duplicate entry: $relative"
            }
            $target = Resolve-M0PackageV2ContainedPath $destination $relative `
                'Catch2 archive destination'
            switch ($entry.EntryType) {
                ([System.Formats.Tar.TarEntryType]::Directory) {
                    if ([IO.File]::Exists($target)) {
                        throw "Catch2 archive directory collides with a file: $relative"
                    }
                    [IO.Directory]::CreateDirectory($target) | Out-Null
                }
                ([System.Formats.Tar.TarEntryType]::RegularFile) {
                    $parent = [IO.Path]::GetDirectoryName($target)
                    [IO.Directory]::CreateDirectory($parent) | Out-Null
                    $output = [IO.FileStream]::new(
                        $target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
                        [IO.FileShare]::None, 65536, [IO.FileOptions]::SequentialScan)
                    try {
                        if ($null -eq $entry.DataStream) {
                            throw "Catch2 archive file has no data stream: $relative"
                        }
                        $entry.DataStream.CopyTo($output)
                    }
                    finally { $output.Dispose() }
                    $fileCount++
                }
                default {
                    throw "Catch2 archive contains a link or unsupported entry: $relative"
                }
            }
        }
    }
    catch {
        throw
    }
    finally {
        if ($null -ne $reader) { $reader.Dispose() }
        elseif ($null -ne $gzip) { $gzip.Dispose() }
        else { $archiveStream.Dispose() }
    }
    if ($fileCount -le 0 -or
        -not [IO.File]::Exists((Join-Path $destination 'CMakeLists.txt')) -or
        -not [IO.File]::Exists((Join-Path $destination 'LICENSE.txt'))) {
        throw 'Catch2 authenticated extraction is incomplete.'
    }
    return [pscustomobject][ordered]@{
        root = $destination
        file_count = $fileCount
    }
}

function Write-M0PackageV2Utf8File {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [AllowEmptyString()] [string] $Text
    )
    $parent = [IO.Path]::GetDirectoryName((ConvertTo-M0PackageV2FullPath $LiteralPath))
    if (-not [IO.Directory]::Exists($parent)) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($Text)
    $stream = [IO.FileStream]::new(
        (ConvertTo-M0PackageV2FullPath $LiteralPath), [IO.FileMode]::CreateNew,
        [IO.FileAccess]::Write, [IO.FileShare]::None, 65536,
        [IO.FileOptions]::SequentialScan)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    }
    finally { $stream.Dispose() }
}

function Write-M0PackageV2JsonFile {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] $Value)
    $json = ($Value | ConvertTo-Json -Depth 100) + "`n"
    Write-M0PackageV2Utf8File -LiteralPath $LiteralPath -Text $json
}

function Copy-M0PackageV2FileCreateNew {
    param(
        [Parameter(Mandatory)] [string] $SourcePath,
        [Parameter(Mandatory)] [string] $DestinationPath
    )
    $source = ConvertTo-M0PackageV2FullPath $SourcePath
    $destination = ConvertTo-M0PackageV2FullPath $DestinationPath
    if (-not [IO.File]::Exists($source)) { throw "Package copy source is missing: $source" }
    if ([IO.File]::Exists($destination) -or [IO.Directory]::Exists($destination)) {
        throw "Package copy destination is not fresh: $destination"
    }
    $parent = [IO.Path]::GetDirectoryName($destination)
    if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    $input = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $output = $null
    try {
        $output = [IO.FileStream]::new(
            $destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
            [IO.FileShare]::None, 65536, [IO.FileOptions]::SequentialScan)
        $input.CopyTo($output)
        $output.Flush($true)
    }
    finally {
        if ($null -ne $output) { $output.Dispose() }
        $input.Dispose()
    }
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -cne $destinationHash -or
        (Get-Item -LiteralPath $source).Length -ne (Get-Item -LiteralPath $destination).Length) {
        throw "Package create-new copy changed bytes: $destination"
    }
    return [pscustomobject][ordered]@{
        source_path = $source
        destination_path = $destination
        size_bytes = [long] (Get-Item -LiteralPath $destination).Length
        sha256 = $destinationHash
    }
}

function Get-M0PackageV2FileInventory {
    param([Parameter(Mandatory)] [string] $Root, [Parameter(Mandatory)] [string] $Context)
    $rootFull = ConvertTo-M0PackageV2FullPath $Root
    if (-not [IO.Directory]::Exists($rootFull)) { throw "$Context root is missing." }
    Assert-M0PackageV2NoReparseChain $rootFull $rootFull $Context
    $rows = [Collections.Generic.List[object]]::new()
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in Get-ChildItem -LiteralPath $rootFull -Force -Recurse) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse point: $($item.FullName)"
        }
        Assert-M0PackageV2NoAlternateDataStreams $item.FullName $Context
        if ($item -is [IO.DirectoryInfo]) { continue }
        if ($item -isnot [IO.FileInfo]) { throw "$Context contains a non-file leaf." }
        if ($IsWindows) {
            $handle = $script:PackageVerifierNative::OpenFileLease(
                (ConvertTo-M0PackageV2ExtendedPath $item.FullName))
            try { $linkCount = [long] $script:PackageVerifierNative::GetLinkCount($handle) }
            finally { $handle.Dispose() }
            if ($linkCount -ne 1) {
                throw "$Context contains a hardlinked file: $($item.FullName)"
            }
        }
        $relative = [IO.Path]::GetRelativePath($rootFull, $item.FullName).Replace('\', '/')
        Assert-M0PackageV2SafeRelativePath $relative "$Context inventory path"
        if (-not $paths.Add($relative)) {
            throw "$Context contains a duplicate or case-aliased path."
        }
        $rows.Add([pscustomobject][ordered]@{
                relative_path = $relative
                size_bytes = [long] $item.Length
                sha256 = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            })
    }
    $byPath = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($row in $rows) { $byPath.Add($row.relative_path, $row) }
    [string[]] $keys = @($byPath.Keys)
    [Array]::Sort($keys, [StringComparer]::Ordinal)
    return [object[]] @($keys | ForEach-Object { $byPath[$_] })
}

function ConvertFrom-M0PackageV2StrictJson {
    param([Parameter(Mandatory)] [string] $Text, [Parameter(Mandatory)] [string] $Context)
    if ([string]::IsNullOrWhiteSpace($Text)) { throw "$Context JSON is empty." }
    try { $document = [Text.Json.JsonDocument]::Parse($Text) }
    catch { throw [IO.InvalidDataException]::new("$Context JSON is invalid.", $_.Exception) }
    try {
        if ($document.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object) {
            throw "$Context must be one JSON object."
        }
        Assert-M0PackageV2NoDuplicateJsonProperties $document.RootElement $Context
    }
    finally { $document.Dispose() }
    try { return $Text | ConvertFrom-Json -Depth 100 -DateKind String -ErrorAction Stop }
    catch { throw [IO.InvalidDataException]::new("$Context JSON is invalid.", $_.Exception) }
}

function New-M0PackageV2DeployCommand {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $ExecutablePath,
        [Parameter(Mandatory)] [string] $QtOnBuildRoot,
        [Parameter(Mandatory)] [string] $ApplicationRoot,
        [switch] $DryRun
    )
    $arguments = [Collections.Generic.List[object]]::new()
    foreach ($argument in @(
            '--release', '--qtpaths', [string] $Context.tools.qtpaths,
            '--qmldir', (Join-Path $Context.source_root 'qml'),
            '--qmlimport', (Join-Path $QtOnBuildRoot 'qml'),
            '--dir', $ApplicationRoot,
            '--plugindir', (Join-Path $ApplicationRoot 'plugins'),
            '--qml-deploy-dir', (Join-Path $ApplicationRoot 'qml'),
            '--no-patchqt', '--no-compiler-runtime', '--no-translations', '--no-ffmpeg',
            '--no-system-d3d-compiler', '--no-system-dxc-compiler',
            '--skip-plugin-types', 'qmltooling',
            '--include-plugins', 'qsvg,qsvgicon,qoffscreen')) {
        $arguments.Add([string] $argument)
    }
    if ($DryRun) { $arguments.Add('--dry-run') }
    $arguments.Add('--json')
    $arguments.Add($ExecutablePath)
    return New-M0PackageV2BuildCommand -Id $Id -FilePath $Context.tools.windeployqt `
        -Arguments ([object[]] $arguments.ToArray()) `
        -WorkingDirectory $Context.repository_root -TimeoutSeconds 900 `
        -ExpectedTestCount $null
}

function Assert-M0PackageV2CommandSucceeded {
    param([Parameter(Mandatory)] $Command, [Parameter(Mandatory)] $Result)
    if ($null -eq $Result -or $Result.timed_out -eq $true -or
        $Result.exit_code -isnot [long] -or $Result.exit_code -ne 0 -or
        $Result.stdout -isnot [string] -or $Result.stderr -isnot [string]) {
        $exit = if ($null -eq $Result) { 'missing' } else { [string] $Result.exit_code }
        throw "Production command '$($Command.id)' failed closed with exit $exit."
    }
}

function Invoke-M0PackageV2Deployment {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $QtOnBuildRoot,
        [Parameter(Mandatory)] [string] $ApplicationRoot,
        [Parameter(Mandatory)] [Collections.IDictionary] $Environment,
        [Parameter(Mandatory)] [scriptblock] $RunAction
    )
    $executable = Join-Path $QtOnBuildRoot 'm0_qt_shell.exe'
    if (-not [IO.File]::Exists($executable)) {
        throw 'Qt-on Release build did not produce m0_qt_shell.exe.'
    }
    $qmlRoot = Join-Path $QtOnBuildRoot 'qml/MusicPlayer/M0'
    if (-not [IO.Directory]::Exists($qmlRoot)) {
        throw 'Qt-on Release build did not produce application QML output.'
    }
    if (-not [IO.Directory]::Exists($ApplicationRoot)) {
        [IO.Directory]::CreateDirectory($ApplicationRoot) | Out-Null
    }
    $d3dSource = Join-Path $Context.composite_sdk_root 'd3dcompiler_47.dll'
    if (-not [IO.File]::Exists($d3dSource)) {
        throw 'Composite SDK lacks its non-System32 d3dcompiler_47.dll.'
    }
    $d3dMapping = [pscustomobject][ordered]@{
        source = $d3dSource
        target = Join-Path $ApplicationRoot 'd3dcompiler_47.dll'
    }

    $dryCommand = New-M0PackageV2DeployCommand -Context $Context -Id 'deploy-dry-run' `
        -ExecutablePath $executable -QtOnBuildRoot $QtOnBuildRoot `
        -ApplicationRoot $ApplicationRoot -DryRun
    $dryResult = & $RunAction $dryCommand $Environment
    Assert-M0PackageV2CommandSucceeded $dryCommand $dryResult
    $dryDiagnostics = Assert-M0PackageV2DeployDiagnostics `
        -StandardError $dryResult.stderr -CompositeSdkRoot $Context.composite_sdk_root `
        -Context 'windeployqt dry run'
    $dryJson = ConvertFrom-M0PackageV2StrictJson $dryResult.stdout 'windeployqt dry run'
    Assert-M0PackageV2RequiredFields $dryJson @('files') 'windeployqt dry run'
    $dryMappings = [object[]] @(@($dryJson.files) + @($d3dMapping))
    $dryPlan = ConvertTo-M0PackageV2DeploymentPlan -Files $dryMappings `
        -CompositeSdkRoot $Context.composite_sdk_root `
        -ApplicationQmlRoot $qmlRoot -DestinationRoot $ApplicationRoot

    $null = Copy-M0PackageV2FileCreateNew -SourcePath $executable `
        -DestinationPath (Join-Path $ApplicationRoot 'm0_qt_shell.exe')
    $actualCommand = New-M0PackageV2DeployCommand -Context $Context -Id 'deploy-actual' `
        -ExecutablePath $executable -QtOnBuildRoot $QtOnBuildRoot `
        -ApplicationRoot $ApplicationRoot
    $actualResult = & $RunAction $actualCommand $Environment
    Assert-M0PackageV2CommandSucceeded $actualCommand $actualResult
    $actualDiagnostics = Assert-M0PackageV2DeployDiagnostics `
        -StandardError $actualResult.stderr -CompositeSdkRoot $Context.composite_sdk_root `
        -Context 'windeployqt actual deployment'
    $actualJson = ConvertFrom-M0PackageV2StrictJson $actualResult.stdout `
        'windeployqt actual deployment'
    Assert-M0PackageV2RequiredFields $actualJson @('files') 'windeployqt actual deployment'
    $null = Copy-M0PackageV2FileCreateNew -SourcePath $d3dSource `
        -DestinationPath $d3dMapping.target
    $actualMappings = [object[]] @(@($actualJson.files) + @($d3dMapping))
    $actualPlan = ConvertTo-M0PackageV2DeploymentPlan -Files $actualMappings `
        -CompositeSdkRoot $Context.composite_sdk_root `
        -ApplicationQmlRoot $qmlRoot -DestinationRoot $ApplicationRoot
    Assert-M0PackageV2DeploymentPlansEqual -DryRun $dryPlan -Actual $actualPlan

    foreach ($row in @($actualPlan.files)) {
        $target = Resolve-M0PackageV2ContainedPath $ApplicationRoot $row.relative_path `
            'Deployed payload target'
        if (-not [IO.File]::Exists($target)) {
            throw "Deployed payload target is missing: $($row.relative_path)"
        }
        $item = Get-Item -LiteralPath $target -Force
        $hash = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($item.Length -ne $row.size_bytes -or $hash -cne $row.sha256) {
            throw "Deployed payload bytes differ from the authenticated plan: $($row.relative_path)"
        }
    }
    return [pscustomobject][ordered]@{
        executable_path = $executable
        application_qml_root = $qmlRoot
        dry_run = $dryResult
        actual = $actualResult
        dry_diagnostics = $dryDiagnostics
        actual_diagnostics = $actualDiagnostics
        dry_plan = $dryPlan
        actual_plan = $actualPlan
    }
}

function Invoke-M0PackageV2PayloadPeClosure {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $ApplicationRoot,
        [Parameter(Mandatory)] [object[]] $PayloadRows,
        [Parameter(Mandatory)] [Collections.IDictionary] $Environment,
        [Parameter(Mandatory)] [scriptblock] $RunAction
    )
    $payloadByPath = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $PayloadRows) { $payloadByPath.Add($row.relative_path, $row) }
    $peRows = @($PayloadRows | Where-Object { $_.relative_path -imatch '\.(?:dll|exe)$' })
    if (@($peRows | Where-Object relative_path -CEQ 'm0_qt_shell.exe').Count -ne 1) {
        throw 'Payload PE closure lacks exactly one m0_qt_shell.exe.'
    }
    $records = [Collections.Generic.List[object]]::new()
    $external = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $index = 0
    foreach ($row in $peRows) {
        $path = Resolve-M0PackageV2ContainedPath $ApplicationRoot $row.relative_path `
            'Payload PE audit path'
        $command = New-M0PackageV2BuildCommand -Id ('payload-pe-{0:d4}' -f $index) `
            -FilePath $Context.tools.dumpbin `
            -Arguments ([object[]] @('/nologo', '/headers', '/dependents', $path)) `
            -WorkingDirectory $Context.repository_root -TimeoutSeconds 120 `
            -ExpectedTestCount $null
        $result = & $RunAction $command $Environment
        Assert-M0PackageV2CommandSucceeded $command $result
        if (-not [string]::IsNullOrWhiteSpace($result.stderr)) {
            throw "Payload PE audit produced stderr: $($row.relative_path)"
        }
        $parsed = ConvertFrom-M0PackageV2PeReport -Text $result.stdout `
            -RelativePath $row.relative_path
        $imports = [Collections.Generic.List[object]]::new()
        foreach ($dependency in @($parsed.imports)) {
            $sibling = [IO.Path]::GetRelativePath(
                $ApplicationRoot,
                (Join-Path ([IO.Path]::GetDirectoryName($path)) $dependency)).Replace('\', '/')
            if ($payloadByPath.ContainsKey($sibling)) {
                $imports.Add([pscustomobject][ordered]@{
                        name = $dependency; kind = 'payload'; path = $sibling
                    })
            }
            elseif ($payloadByPath.ContainsKey($dependency)) {
                $imports.Add([pscustomobject][ordered]@{
                        name = $dependency; kind = 'payload'; path = $dependency
                    })
            }
            elseif ($dependency -imatch '^(?:api|ext)-ms-win-[a-z0-9-]+\.dll$') {
                $imports.Add([pscustomobject][ordered]@{
                        name = $dependency; kind = 'windows-api-set'; path = $null
                    })
            }
            else {
                if ($dependency -imatch '^Qt6' -or $dependency -ieq 'd3dcompiler_47.dll') {
                    throw "Payload PE requires a forbidden SDK/System32 fallback: $($row.relative_path) -> $dependency"
                }
                $systemPath = Join-Path ([Environment]::SystemDirectory) $dependency
                if (-not [IO.File]::Exists($systemPath)) {
                    throw "Payload PE dependency is unresolved: $($row.relative_path) -> $dependency"
                }
                $kind = if ($dependency -imatch '^(?:vcruntime|msvcp|concrt|vccorlib)') {
                    'host-msvc-runtime'
                }
                else { 'windows-system' }
                if (-not $external.ContainsKey($dependency)) {
                    $item = Get-Item -LiteralPath $systemPath -Force
                    $external.Add($dependency, [pscustomobject][ordered]@{
                            name = $dependency
                            kind = $kind
                            location = "Windows/System32/$dependency"
                            size_bytes = [long] $item.Length
                            file_version = [string] $item.VersionInfo.FileVersion
                            sha256 = (Get-FileHash -LiteralPath $systemPath -Algorithm SHA256).Hash.ToLowerInvariant()
                        })
                }
                $imports.Add([pscustomobject][ordered]@{
                        name = $dependency; kind = $kind; path = "Windows/System32/$dependency"
                    })
            }
        }
        $records.Add([pscustomobject][ordered]@{
                relative_path = $row.relative_path
                machine = 'x64'
                imports = [object[]] $imports.ToArray()
            })
        $index++
    }
    [string[]] $externalNames = @($external.Keys)
    [Array]::Sort($externalNames, [StringComparer]::Ordinal)
    return [pscustomobject][ordered]@{
        schema_version = [long] 2
        files = [object[]] $records.ToArray()
        external_files = [object[]] @($externalNames | ForEach-Object { $external[$_] })
        limitation = 'Static imports only; this is not a complete runtime loader or clean-machine proof.'
    }
}

function Remove-M0PackageV2EmptyCandidateDirectories {
    param([Parameter(Mandatory)] [string] $CandidateRoot)
    $root = ConvertTo-M0PackageV2FullPath $CandidateRoot
    do {
        $removed = $false
        foreach ($directory in @([IO.Directory]::EnumerateDirectories(
                    $root, '*', [IO.SearchOption]::AllDirectories))) {
            $relative = [IO.Path]::GetRelativePath($root, $directory).Replace('\', '/')
            if ($relative -cin @('app', 'evidence')) { continue }
            $entries = @([IO.Directory]::EnumerateFileSystemEntries($directory))
            if ($entries.Count -eq 0) {
                [IO.Directory]::Delete($directory, $false)
                $removed = $true
            }
        }
    } while ($removed)
}

function New-M0PackageV2ManifestAndReceipt {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $CandidateRoot,
        [Parameter(Mandatory)] [object[]] $PayloadRows,
        [Parameter(Mandatory)] [object[]] $EvidenceRows
    )
    $contract = $Context.contract
    $manifest = [pscustomobject][ordered]@{
        schema_version = [long] 2
        package_id = $script:PackageId
        contract = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_contract_sha256
        }
        composite_receipt = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_composite_receipt_sha256
        }
        qtbase_receipt = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_qtbase_receipt_sha256
        }
        payload = [pscustomobject][ordered]@{
            root_relative_path = [string] $contract.envelope.payload_relative_path
            files = [object[]] $PayloadRows
        }
        evidence = [pscustomobject][ordered]@{
            root_relative_path = [string] $contract.envelope.evidence_relative_path
            files = [object[]] $EvidenceRows
        }
    }
    $manifestPath = Resolve-M0PackageV2ContainedPath $CandidateRoot `
        $contract.envelope.manifest_relative_path 'Candidate package manifest path'
    Write-M0PackageV2JsonFile -LiteralPath $manifestPath -Value $manifest
    $manifestItem = Get-Item -LiteralPath $manifestPath -Force
    $manifestPin = [pscustomobject][ordered]@{
        relative_path = [string] $contract.envelope.manifest_relative_path
        size_bytes = [long] $manifestItem.Length
        sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $payloadTotals = Get-M0PackageV2InventoryTotals $PayloadRows
    $evidenceTotals = Get-M0PackageV2InventoryTotals $EvidenceRows
    $receipt = [pscustomobject][ordered]@{
        schema_version = [long] 2
        package_id = $script:PackageId
        contract = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_contract_sha256
        }
        composite_receipt = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_composite_receipt_sha256
        }
        qtbase_receipt = [pscustomobject][ordered]@{
            sha256 = [string] $Context.expected_qtbase_receipt_sha256
        }
        manifest = $manifestPin
        totals = [pscustomobject][ordered]@{
            payload_files = [long] $payloadTotals.files
            payload_bytes = [long] $payloadTotals.bytes
            evidence_files = [long] $evidenceTotals.files
            evidence_bytes = [long] $evidenceTotals.bytes
        }
    }
    $receiptPath = Resolve-M0PackageV2ContainedPath $CandidateRoot `
        $contract.envelope.receipt_relative_path 'Candidate package receipt path'
    Write-M0PackageV2JsonFile -LiteralPath $receiptPath -Value $receipt
    $receiptItem = Get-Item -LiteralPath $receiptPath -Force
    return [pscustomobject][ordered]@{
        manifest_path = $manifestPath
        manifest_relative_path = [string] $contract.envelope.manifest_relative_path
        manifest_sha256 = [string] $manifestPin.sha256
        receipt_path = $receiptPath
        receipt_relative_path = [string] $contract.envelope.receipt_relative_path
        receipt_sha256 = (Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant()
        receipt_size_bytes = [long] $receiptItem.Length
    }
}

function Invoke-M0PackageV2BuildCandidate {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [scriptblock] $RunAction
    )
    if ([IO.File]::Exists($Context.build_root) -or
        [IO.Directory]::Exists($Context.build_root)) {
        throw 'BuildRoot is no longer fresh before candidate construction.'
    }
    [IO.Directory]::CreateDirectory($Context.build_root) | Out-Null
    $qtBuild = Join-Path $Context.build_root 'qt-on'
    $coreBuild = Join-Path $Context.build_root 'core-off'
    $candidateRoot = Join-Path $Context.build_root 'package-source'
    $applicationRoot = Join-Path $candidateRoot 'app'
    $evidenceRoot = Join-Path $candidateRoot 'evidence'
    $privateTemp = Join-Path $Context.build_root 'private-temp'
    $catch2Root = Join-Path $Context.build_root 'third-party/Catch2-3.15.3'
    foreach ($directory in @(
            $applicationRoot, $evidenceRoot, $privateTemp,
            (Join-Path $privateTemp 'local-app-data'),
            (Join-Path $privateTemp 'app-data'),
            (Join-Path $privateTemp 'profile'),
            ([IO.Path]::GetDirectoryName($catch2Root)))) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    $environments = New-M0PackageV2BuildEnvironment -Context $Context `
        -PrivateTempRoot $privateTemp
    $sourcePre = Assert-M0PackageV2GitSources -Context $Context `
        -Environment $environments.git -RunAction $RunAction -Phase 'pre'
    $catch2 = Expand-M0PackageV2Catch2Archive `
        -ArchivePath $Context.catch2_archive_path -DestinationRoot $catch2Root

    $planContext = [pscustomobject][ordered]@{
        repository_root = $Context.repository_root
        source_root = $Context.source_root
        composite_sdk_root = $Context.composite_sdk_root
        catch2_root = $catch2.root
        qt_on_build_root = $qtBuild
        core_off_build_root = $coreBuild
        tools = [pscustomobject][ordered]@{
            cmake = $Context.tools.cmake
            ctest = $Context.tools.ctest
            ninja = $Context.tools.ninja
            cl = $Context.tools.cl
            link = $Context.tools.link
            rc = $Context.tools.rc
            mt = $Context.tools.mt
            dumpbin = $Context.tools.dumpbin
        }
    }
    $plan = New-M0PackageV2BuildPlan -Context $planContext
    $buildExecution = Invoke-M0PackageV2BuildCommands -Plan $plan `
        -Environment $environments -RunAction $RunAction

    $buildEvidenceRoot = Join-Path $evidenceRoot 'build'
    [IO.Directory]::CreateDirectory($buildEvidenceRoot) | Out-Null
    Write-M0PackageV2JsonFile -LiteralPath (Join-Path $buildEvidenceRoot 'command-plan.v2.json') `
        -Value $plan
    $resultSummary = [Collections.Generic.List[object]]::new()
    foreach ($result in @($buildExecution.results)) {
        $stdoutPath = Join-Path $buildEvidenceRoot "$($result.id).stdout.txt"
        $stderrPath = Join-Path $buildEvidenceRoot "$($result.id).stderr.txt"
        Write-M0PackageV2Utf8File -LiteralPath $stdoutPath -Text $result.stdout
        Write-M0PackageV2Utf8File -LiteralPath $stderrPath -Text $result.stderr
        $elapsed = if ($null -ne $result.PSObject.Properties['elapsed_milliseconds']) {
            [long] $result.elapsed_milliseconds
        }
        else { [long] 0 }
        $resultSummary.Add([pscustomobject][ordered]@{
                id = [string] $result.id
                exit_code = [long] $result.exit_code
                timed_out = [bool] $result.timed_out
                elapsed_milliseconds = $elapsed
                stdout_sha256 = (Get-FileHash -LiteralPath $stdoutPath -Algorithm SHA256).Hash.ToLowerInvariant()
                stderr_sha256 = (Get-FileHash -LiteralPath $stderrPath -Algorithm SHA256).Hash.ToLowerInvariant()
            })
    }
    Write-M0PackageV2JsonFile -LiteralPath (Join-Path $buildEvidenceRoot 'command-results.v2.json') `
        -Value ([pscustomobject][ordered]@{
            schema_version = [long] 2
            commands = [object[]] $resultSummary.ToArray()
        })
    Write-M0PackageV2JsonFile -LiteralPath (Join-Path $buildEvidenceRoot 'ctest-proofs.v2.json') `
        -Value ([pscustomobject][ordered]@{
            schema_version = [long] 2
            inventories = $buildExecution.ctest_inventories
            junit = $buildExecution.ctest_junit
            core_off_imports = $buildExecution.core_off_imports
        })
    $null = Copy-M0PackageV2FileCreateNew `
        -SourcePath (Join-Path $qtBuild 'ctest-results.junit.xml') `
        -DestinationPath (Join-Path $buildEvidenceRoot 'qt-on-ctest-results.junit.xml')
    $null = Copy-M0PackageV2FileCreateNew `
        -SourcePath (Join-Path $coreBuild 'ctest-results.junit.xml') `
        -DestinationPath (Join-Path $buildEvidenceRoot 'core-off-ctest-results.junit.xml')

    $deployment = Invoke-M0PackageV2Deployment -Context $Context `
        -QtOnBuildRoot $qtBuild -ApplicationRoot $applicationRoot `
        -Environment $environments.deployment -RunAction $RunAction
    $deploymentEvidenceRoot = Join-Path $evidenceRoot 'deployment'
    [IO.Directory]::CreateDirectory($deploymentEvidenceRoot) | Out-Null
    Write-M0PackageV2Utf8File `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'windeployqt-dry-run.json') `
        -Text $deployment.dry_run.stdout
    Write-M0PackageV2Utf8File `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'windeployqt-actual.json') `
        -Text $deployment.actual.stdout
    Write-M0PackageV2JsonFile `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'deployment-plan.v2.json') `
        -Value $deployment.actual_plan
    Write-M0PackageV2JsonFile `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'diagnostics.v2.json') `
        -Value ([pscustomobject][ordered]@{
            schema_version = [long] 2
            dry_run = $deployment.dry_diagnostics
            actual = $deployment.actual_diagnostics
        })

    $null = Copy-M0PackageV2FileCreateNew -SourcePath $Context.qt_conf_path `
        -DestinationPath (Join-Path $applicationRoot 'qt.conf')
    $null = Copy-M0PackageV2FileCreateNew -SourcePath $Context.limitations_path `
        -DestinationPath (Join-Path $applicationRoot 'PACKAGE-LIMITATIONS.txt')
    foreach ($sbom in @($Context.contract.inputs.sbom_files)) {
        $source = $Context.sbom_sources.($sbom.id)
        $target = Resolve-M0PackageV2ContainedPath $candidateRoot `
            $sbom.target_relative_path "Candidate SPDX '$($sbom.id)' target"
        $copy = Copy-M0PackageV2FileCreateNew -SourcePath $source -DestinationPath $target
        if ($copy.size_bytes -ne $sbom.size_bytes -or $copy.sha256 -cne $sbom.sha256) {
            throw "Candidate SPDX '$($sbom.id)' bytes drifted."
        }
    }

    $payloadRows = @(Get-M0PackageV2FileInventory $applicationRoot 'Candidate payload')
    $expectedPaths = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($path in @($deployment.actual_plan.files.relative_path) + @(
            'm0_qt_shell.exe', 'qt.conf', 'PACKAGE-LIMITATIONS.txt')) {
        if (-not $expectedPaths.Add([string] $path)) {
            throw "Candidate payload plan contains a duplicate path: $path"
        }
    }
    if ($payloadRows.Count -ne $expectedPaths.Count -or
        -not $expectedPaths.SetEquals([string[]] @($payloadRows.relative_path))) {
        throw 'Candidate payload differs from the exact deployment and recipe file set.'
    }
    $builtExecutableHash = (Get-FileHash -LiteralPath $deployment.executable_path `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    $payloadExecutable = @($payloadRows | Where-Object relative_path -CEQ 'm0_qt_shell.exe')
    if ($payloadExecutable.Count -ne 1 -or
        $payloadExecutable[0].sha256 -cne $builtExecutableHash) {
        throw 'Candidate payload executable differs from the Qt-on Release build.'
    }
    Write-M0PackageV2JsonFile `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'payload-inventory.v2.json') `
        -Value ([pscustomobject][ordered]@{
            schema_version = [long] 2
            files = [object[]] $payloadRows
        })
    $peClosure = Invoke-M0PackageV2PayloadPeClosure -Context $Context `
        -ApplicationRoot $applicationRoot -PayloadRows $payloadRows `
        -Environment $environments.core_off -RunAction $RunAction
    Write-M0PackageV2JsonFile `
        -LiteralPath (Join-Path $deploymentEvidenceRoot 'pe-closure.v2.json') `
        -Value $peClosure

    $sourcePost = Assert-M0PackageV2GitSources -Context $Context `
        -Environment $environments.git -RunAction $RunAction -Phase 'post'
    if ($sourcePost.source_commit -cne $sourcePre.source_commit -or
        $sourcePost.file_count -ne $sourcePre.file_count -or
        $sourcePost.physical_inventory_sha256 -cne $sourcePre.physical_inventory_sha256 -or
        ($sourcePost.trees | ConvertTo-Json -Compress) -cne
            ($sourcePre.trees | ConvertTo-Json -Compress)) {
        throw 'Package source identity changed during build or deployment.'
    }
    $provenanceRoot = Join-Path $evidenceRoot 'provenance'
    [IO.Directory]::CreateDirectory($provenanceRoot) | Out-Null
    Write-M0PackageV2JsonFile `
        -LiteralPath (Join-Path $provenanceRoot 'authenticated-inputs.v2.json') `
        -Value ([pscustomobject][ordered]@{
            schema_version = [long] 2
            contract_sha256 = [string] $Context.expected_contract_sha256
            composite_receipt_sha256 = [string] $Context.expected_composite_receipt_sha256
            qtbase_receipt_sha256 = [string] $Context.expected_qtbase_receipt_sha256
            composite_sdk = [pscustomobject][ordered]@{
                file_count = [long] $Context.composite_sdk_file_count
                total_size_bytes = [long] $Context.composite_sdk_total_size_bytes
            }
            catch2 = [pscustomobject][ordered]@{
                version = [string] $Context.contract.inputs.catch2.version
                commit = [string] $Context.contract.inputs.catch2.commit
                archive_sha256 = [string] $Context.contract.inputs.catch2.archive_sha256
                extracted_file_count = [long] $catch2.file_count
            }
            source_pre = $sourcePre
            source_post = $sourcePost
            tools = [object[]] $Context.contract.inputs.tools
            environments = [pscustomobject][ordered]@{
                qt_on = [object[]] (ConvertTo-M0PackageV2RedactedEnvironment `
                    $environments.qt_on)
                core_off = [object[]] (ConvertTo-M0PackageV2RedactedEnvironment `
                    $environments.core_off)
                deployment = [object[]] (ConvertTo-M0PackageV2RedactedEnvironment `
                    $environments.deployment)
            }
        })

    $evidenceRows = @(Get-M0PackageV2FileInventory $evidenceRoot `
            'Candidate pre-control evidence')
    $controls = New-M0PackageV2ManifestAndReceipt -Context $Context `
        -CandidateRoot $candidateRoot -PayloadRows $payloadRows -EvidenceRows $evidenceRows
    Remove-M0PackageV2EmptyCandidateDirectories -CandidateRoot $candidateRoot
    $completeRows = @(Get-M0PackageV2FileInventory $candidateRoot 'Complete package candidate')
    $entries = @(New-M0PackageV2PublicationEntries -SourceRoot $candidateRoot `
            -Rows $completeRows -ReceiptRelativePath $controls.receipt_relative_path)
    if ($entries[-1].relative_path -cne $controls.receipt_relative_path -or
        $entries[-1].sha256 -cne $controls.receipt_sha256) {
        throw 'Complete candidate receipt is not the final authenticated entry.'
    }
    return [pscustomobject][ordered]@{
        source_root = $candidateRoot
        manifest_relative_path = $controls.manifest_relative_path
        manifest_sha256 = $controls.manifest_sha256
        receipt_relative_path = $controls.receipt_relative_path
        receipt_sha256 = $controls.receipt_sha256
        entries = [object[]] $entries
    }
}

function Invoke-M0PackageV2CompositeVerification {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [ValidateSet('pre', 'post')] [string] $Phase
    )
    $results = @(& $Context.composite_verifier_path `
            -ContractPath $Context.composite_contract_path `
            -ExpectedContractSha256 $Context.expected_composite_contract_sha256 `
            -ExpectedQtBaseReceiptSha256 $Context.expected_qtbase_receipt_sha256 `
            -RepositoryRoot $Context.repository_root `
            -ReceiptPath $Context.composite_receipt_path `
            -ExpectedReceiptSha256 $Context.expected_composite_receipt_sha256)
    if ($results.Count -ne 1 -or $null -eq $results[0]) {
        throw "Composite $Phase verification returned an open result stream."
    }
    $result = $results[0]
    Assert-M0PackageV2Fields $result @(
        'composite_id', 'contract_sha256', 'qtbase_receipt_sha256',
        'receipt_path', 'receipt_sha256', 'verified') `
        "Composite $Phase verification result"
    if ($result.verified -ne $true -or
        $result.contract_sha256 -cne $Context.expected_composite_contract_sha256 -or
        $result.qtbase_receipt_sha256 -cne $Context.expected_qtbase_receipt_sha256 -or
        $result.receipt_sha256 -cne $Context.expected_composite_receipt_sha256 -or
        -not [string]::Equals(
            (ConvertTo-M0PackageV2FullPath $result.receipt_path),
            $Context.composite_receipt_path,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Composite $Phase verification changed an authenticated identity."
    }
    Assert-M0PackageV2FileLeasesUnchanged $Context.retained_files `
        "Package retained inputs after composite $Phase verification"
    return [pscustomobject][ordered]@{
        verified = $true
        phase = $Phase
        composite_id = [string] $result.composite_id
        sdk_root = [string] $Context.composite_sdk_root
    }
}

function Invoke-M0PackageV2NativePublication {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Candidate)
    $commands = @(Get-Command -Name 'Invoke-M0QtShellCompositePackageNative' `
            -CommandType Function -All -ErrorAction SilentlyContinue | Where-Object {
            $null -ne $_.Module -and [string]::Equals(
                (ConvertTo-M0PackageV2FullPath $_.Module.Path),
                $Context.native_module_path,
                [StringComparison]::OrdinalIgnoreCase)
        })
    if ($commands.Count -ne 1) {
        throw 'Authenticated native publisher is not imported exactly once.'
    }
    [string[]] $exports = @($commands[0].Module.ExportedFunctions.Keys)
    if ($exports.Count -ne 1 -or
        $exports[0] -cne 'Invoke-M0QtShellCompositePackageNative') {
        throw 'Authenticated native publisher export surface drifted.'
    }
    $results = @(& $commands[0] -SourceRoot $Candidate.source_root `
            -OutputRoot $Context.output_root -ScratchRoot $Context.scratch_root `
            -Entries ([object[]] $Candidate.entries))
    if ($results.Count -ne 1 -or $null -eq $results[0] -or
        $results[0].exit_code -isnot [long] -or $results[0].exit_code -ne 0) {
        throw 'Authenticated native publisher failed closed.'
    }
    Assert-M0PackageV2FileLeasesUnchanged $Context.retained_files `
        'Package retained inputs after native publication'
    return $results[0]
}

function Invoke-M0PackageV2FinalVerification {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] $Candidate)
    $receiptPath = Resolve-M0PackageV2ContainedPath $Context.output_root `
        $Candidate.receipt_relative_path 'Published package receipt path'
    $result = Assert-M0QtShellCompositePackageReceipt `
        -LiteralPath $receiptPath `
        -ExpectedReceiptSha256 $Candidate.receipt_sha256 `
        -ContractPath $Context.contract_path `
        -ExpectedContractSha256 $Context.expected_contract_sha256 `
        -CompositeReceiptPath $Context.composite_receipt_path `
        -ExpectedCompositeReceiptSha256 $Context.expected_composite_receipt_sha256 `
        -RepositoryRoot $Context.repository_root `
        -ExpectedQtBaseReceiptSha256 $Context.expected_qtbase_receipt_sha256
    Assert-M0PackageV2FileLeasesUnchanged $Context.retained_files `
        'Package retained inputs after final package verification'
    return $result
}

function New-M0PackageV2ProductionOperations {
    $runAction = {
        param($Command, $Environment)
        Invoke-M0PackageV2BoundedProcess -Command $Command -Environment $Environment
    }
    return [pscustomobject][ordered]@{
        verify_composite = {
            param($Context, $Phase)
            Invoke-M0PackageV2CompositeVerification -Context $Context -Phase $Phase
        }
        build_candidate = {
            param($Context)
            Invoke-M0PackageV2BuildCandidate -Context $Context -RunAction $runAction
        }.GetNewClosure()
        publish = {
            param($Context, $Candidate)
            Invoke-M0PackageV2NativePublication -Context $Context -Candidate $Candidate
        }
        verify_package = {
            param($Context, $Candidate)
            Invoke-M0PackageV2FinalVerification -Context $Context -Candidate $Candidate
        }
    }
}

function Invoke-M0QtShellCompositePackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $CompositeReceiptPath,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)] [ValidatePattern('^[0-9a-f]{64}$')] [string] $ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $BuildRoot,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $ScratchRoot
    )
    if (-not $IsWindows -or -not [Environment]::Is64BitProcess) {
        throw [PlatformNotSupportedException]::new(
            'Package-v2 production requires Windows x64 PowerShell.')
    }
    $leases = [Collections.Generic.List[object]]::new()
    $files = [Collections.Generic.List[object]]::new()
    $directories = [Collections.Generic.List[object]]::new()
    try {
        $context = Get-M0PackageV2ProductionContext `
            -ContractPath $ContractPath -ExpectedContractSha256 $ExpectedContractSha256 `
            -CompositeReceiptPath $CompositeReceiptPath `
            -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
            -ExpectedQtBaseReceiptSha256 $ExpectedQtBaseReceiptSha256 `
            -RepositoryRoot $RepositoryRoot -BuildRoot $BuildRoot `
            -OutputRoot $OutputRoot -ScratchRoot $ScratchRoot `
            -Leases $leases -Files $files -Directories $directories
        Assert-M0PackageV2FileLeasesUnchanged $files `
            'Package production pre-workflow inputs'
        Assert-M0PackageV2DirectoryLeasesUnchanged $directories `
            'Package production pre-workflow directories'
        $operations = New-M0PackageV2ProductionOperations
        $result = Invoke-M0PackageV2ProductionWorkflow -Context $context `
            -Operations $operations
        Assert-M0PackageV2FileLeasesUnchanged $files `
            'Package production final inputs'
        Assert-M0PackageV2DirectoryLeasesUnchanged $directories `
            'Package production final directories'
        return $result
    }
    finally { Close-M0PackageV2Leases $leases }
}

Export-ModuleMember -Function @(
    'Assert-M0QtShellCompositePackageReceipt',
    'Invoke-M0QtShellCompositePackage'
)
