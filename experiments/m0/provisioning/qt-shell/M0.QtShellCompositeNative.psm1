#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0CompositeUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0CompositeOwnerIds = @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')
$script:M0CompositeOwnershipProperties = @(
    'owner_id', 'source_relative_path', 'relative_path', 'size_bytes', 'sha256'
)

if ($IsWindows) {
    $nativeNamespace = 'M0QtShellCompositeNative_' + [Guid]::NewGuid().ToString('N')
    $nativeDefinition = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace __M0_COMPOSITE_NAMESPACE__ {
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

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_STATUS_BLOCK {
        public IntPtr Status;
        public UIntPtr Information;
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
        private const uint DELETE_ACCESS = 0x00010000;
        private const uint FILE_LIST_DIRECTORY = 0x00000001;
        private const uint FILE_READ_ATTRIBUTES = 0x00000080;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint FILE_SHARE_DELETE = 0x00000004;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        private const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
        private const int FileDispositionInfo = 4;
        private const int ERROR_HANDLE_EOF = 38;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes,
            IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle, out BY_HANDLE_FILE_INFORMATION information);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle handle,
            int fileInformationClass, IntPtr fileInformation, uint bufferSize);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("ntdll.dll")]
        private static extern int NtSetInformationFile(SafeFileHandle handle,
            out IO_STATUS_BLOCK ioStatusBlock, IntPtr information, uint bufferSize,
            int fileInformationClass);

        [DllImport("ntdll.dll")]
        private static extern uint RtlNtStatusToDosError(int status);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFindHandle FindFirstStreamW(string fileName, int infoLevel,
            out WIN32_FIND_STREAM_DATA findStreamData, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextStreamW(SafeFindHandle findHandle,
            out WIN32_FIND_STREAM_DATA findStreamData);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool FindClose(IntPtr findHandle);

        public static SafeFileHandle OpenFileReadOnly(string path) {
            var handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, IntPtr.Zero,
                OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryReadOnly(string path) {
            var handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, IntPtr.Zero,
                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryAnchor(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryReadDeleteAnchor(string path) {
            var handle = CreateFileW(path,
                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenCleanupAnchor(string path) {
            uint flags = FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS;
            var handle = CreateFileW(path, FILE_READ_ATTRIBUTES | DELETE_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING, flags,
                IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenIdentityShareAll(string path, bool directory) {
            var flags = directory ? FILE_FLAG_BACKUP_SEMANTICS : 0;
            var handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                IntPtr.Zero, OPEN_EXISTING, flags, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static void RenameNoReplaceRelative(SafeFileHandle source,
            SafeFileHandle destinationDirectory, string destinationName) {
            if (String.IsNullOrEmpty(destinationName) ||
                destinationName.IndexOfAny(new[] { '\\', '/', ':', '\0' }) >= 0) {
                throw new ArgumentException(
                    "The destination must be one relative leaf name.", "destinationName");
            }
            var name = Encoding.Unicode.GetBytes(destinationName);
            int rootOffset = IntPtr.Size;
            int lengthOffset = rootOffset + IntPtr.Size;
            int nameOffset = lengthOffset + sizeof(uint);
            int unalignedTotal = nameOffset + name.Length + sizeof(char);
            int total = ((unalignedTotal + IntPtr.Size - 1) / IntPtr.Size) * IntPtr.Size;
            IntPtr buffer = Marshal.AllocHGlobal(total);
            try {
                for (int index = 0; index < total; index++) {
                    Marshal.WriteByte(buffer, index, 0);
                }
                Marshal.WriteIntPtr(buffer, rootOffset,
                    destinationDirectory.DangerousGetHandle());
                Marshal.WriteInt32(buffer, lengthOffset, name.Length);
                Marshal.Copy(name, 0, IntPtr.Add(buffer, nameOffset), name.Length);
                IO_STATUS_BLOCK ioStatusBlock;
                int status = NtSetInformationFile(source, out ioStatusBlock, buffer,
                    (uint)total, 10);
                if (status < 0) {
                    throw new Win32Exception((int)RtlNtStatusToDosError(status));
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        public static void MarkDeleteOnClose(SafeFileHandle handle) {
            IntPtr buffer = Marshal.AllocHGlobal(1);
            try {
                Marshal.WriteByte(buffer, 0, 1);
                if (!SetFileInformationByHandle(handle, FileDispositionInfo, buffer, 1)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        private static BY_HANDLE_FILE_INFORMATION GetInformation(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION information;
            if (!GetFileInformationByHandle(handle, out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return information;
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

        public static uint GetAttributes(SafeFileHandle handle) {
            return GetInformation(handle).FileAttributes;
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
}
'@.Replace('__M0_COMPOSITE_NAMESPACE__', $nativeNamespace)
    $nativeTypes = @(Add-Type -TypeDefinition $nativeDefinition -PassThru)
    $nativeFileTypes = @($nativeTypes | Where-Object FullName -CEQ "$nativeNamespace.NativeFile")
    if ($nativeFileTypes.Count -ne 1) {
        throw 'Native composite file-identity type compilation returned an unexpected type set.'
    }
    $script:M0CompositeNativeFileType = [type]$nativeFileTypes[0]
}
else {
    $script:M0CompositeNativeFileType = $null
}

function ConvertTo-M0CompositeFullPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if ([string]::IsNullOrWhiteSpace($LiteralPath)) { throw 'A filesystem path must not be empty.' }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $root = [IO.Path]::GetPathRoot($full)
    if (-not [string]::Equals($full, $root, [StringComparison]::OrdinalIgnoreCase)) {
        $full = $full.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    }
    return $full
}

function ConvertTo-M0CompositeExtendedPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $full.Substring(2) }
    return '\\?\' + $full
}

function ConvertFrom-M0CompositeFinalPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if ($LiteralPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $LiteralPath.Substring(8)
    }
    if ($LiteralPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $LiteralPath.Substring(4)
    }
    return $LiteralPath
}

function Test-M0CompositePathWithin {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Boundary,
        [switch] $AllowEqual
    )

    $candidateFull = ConvertTo-M0CompositeFullPath $Candidate
    $boundaryFull = ConvertTo-M0CompositeFullPath $Boundary
    if ($AllowEqual -and
        [string]::Equals($candidateFull, $boundaryFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    $prefix = $boundaryFull + [IO.Path]::DirectorySeparatorChar
    return $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Test-M0CompositePathsOverlap {
    param(
        [Parameter(Mandatory)] [string] $First,
        [Parameter(Mandatory)] [string] $Second
    )

    return (Test-M0CompositePathWithin $First $Second -AllowEqual) -or
        (Test-M0CompositePathWithin $Second $First -AllowEqual)
}

function Get-M0CompositeExistingItem {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    return Get-Item -LiteralPath $LiteralPath -Force -ErrorAction SilentlyContinue
}

function Assert-M0CompositeNoReparseExistingChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )

    $current = [IO.Path]::GetFullPath($LiteralPath)
    while ($null -eq (Get-M0CompositeExistingItem $current)) {
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $current) { break }
        $current = $parent
    }
    while (-not [string]::IsNullOrEmpty($current)) {
        $item = Get-Item -LiteralPath $current -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse point in its physical ancestry: $current"
        }
        $trimmed = $current.TrimEnd([IO.Path]::DirectorySeparatorChar)
        $parent = [IO.Path]::GetDirectoryName($trimmed)
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $current) { break }
        $current = $parent
    }
}

function Assert-M0CompositeSafeRelativePath {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) {
        throw "$Context must be a non-empty relative path."
    }
    if ([IO.Path]::IsPathRooted($Value) -or
        $Value.StartsWith('/', [StringComparison]::Ordinal) -or
        $Value.StartsWith('\\', [StringComparison]::Ordinal) -or
        $Value.Contains('\\') -or $Value.Contains(':') -or $Value.Contains([char]0)) {
        throw "$Context is absolute or contains an unsafe separator, ADS colon, or NUL."
    }
    $segments = @($Value -split '/', -1)
    foreach ($segment in $segments) {
        if ($segment -in @('', '.', '..')) {
            throw "$Context contains an empty, current, or parent traversal segment."
        }
        if ($segment.EndsWith(' ', [StringComparison]::Ordinal) -or
            $segment.EndsWith('.', [StringComparison]::Ordinal)) {
            throw "$Context contains a Windows-ambiguous trailing dot or space."
        }
        if ($segment.IndexOfAny([char[]]'<>"|?*') -ge 0 -or
            @($segment.ToCharArray() | Where-Object { [int]$_ -lt 32 }).Count -ne 0) {
            throw "$Context contains Windows-invalid path characters."
        }
        $deviceStem = ($segment -split '\.', 2)[0]
        if ($deviceStem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            throw "$Context contains a reserved Windows device name."
        }
    }
}

function Assert-M0CompositeDigest {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context must be a lowercase SHA-256 digest."
    }
}

function ConvertTo-M0CompositeInt64 {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Value -isnot [sbyte] -and $Value -isnot [byte] -and
        $Value -isnot [int16] -and $Value -isnot [uint16] -and
        $Value -isnot [int32] -and $Value -isnot [uint32] -and
        $Value -isnot [int64]) {
        throw "$Context must be an Int64-compatible integer."
    }
    $converted = [long]$Value
    if ($converted -lt 0) { throw "$Context must not be negative." }
    return $converted
}

function Assert-M0CompositeNoAlternateDataStreams {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )

    $streams = @($script:M0CompositeNativeFileType::GetStreamNames(
            (ConvertTo-M0CompositeExtendedPath $LiteralPath)))
    $unexpected = @($streams | Where-Object { $_ -cne '::$DATA' })
    if ($unexpected.Count -ne 0) {
        throw "$Context contains an alternate data stream: $($unexpected[0])"
    }
}

function Get-M0CompositeSha256FromStream {
    param([Parameter(Mandatory)] [IO.Stream] $Stream)

    $position = $Stream.Position
    try {
        $Stream.Position = 0
        return [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    }
    finally { $Stream.Position = $position }
}

function Open-M0CompositePhysicalFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Nullable[long]] $ExpectedSizeBytes,
        [string] $ExpectedSha256
    )

    if (-not $IsWindows) { throw "$Context requires Windows physical-file semantics." }
    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    $boundary = ConvertTo-M0CompositeFullPath $BoundaryRoot
    if (-not (Test-M0CompositePathWithin $full $boundary)) {
        throw "$Context is outside its authenticated source or candidate boundary."
    }
    Assert-M0CompositeNoReparseExistingChain $full $Context
    $item = Get-Item -LiteralPath $full -Force
    if ($item -isnot [IO.FileInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "$Context must be a regular non-reparse file."
    }

    $handle = $null
    $stream = $null
    try {
        $handle = $script:M0CompositeNativeFileType::OpenFileReadOnly(
            (ConvertTo-M0CompositeExtendedPath $full))
        Assert-M0CompositeNoAlternateDataStreams $full $Context
        $final = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
                $script:M0CompositeNativeFileType::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context final physical path differs from its authenticated path."
        }
        $linkCount = [long]$script:M0CompositeNativeFileType::GetLinkCount($handle)
        if ($linkCount -ne 1) {
            throw "$Context has hardlink count $linkCount; exactly one single-link leaf is required."
        }
        $identity = $script:M0CompositeNativeFileType::GetIdentity($handle)
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::Read, 65536, $false)
        $handle = $null
        if ($null -ne $ExpectedSizeBytes -and $stream.Length -ne [long]$ExpectedSizeBytes) {
            throw "$Context size differs from its ownership anchor."
        }
        $sha256 = Get-M0CompositeSha256FromStream $stream
        if (-not [string]::IsNullOrEmpty($ExpectedSha256)) {
            Assert-M0CompositeDigest $ExpectedSha256 "$Context expected SHA-256"
            if ($sha256 -cne $ExpectedSha256) {
                throw "$Context SHA-256 differs from its ownership anchor."
            }
        }
        return [pscustomobject][ordered]@{
            path = $full
            size_bytes = [long]$stream.Length
            sha256 = $sha256
            identity = $identity
            stream = $stream
        }
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        elseif ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Assert-M0CompositePhysicalDirectory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    Assert-M0CompositeNoReparseExistingChain $full $Context
    $item = Get-Item -LiteralPath $full -Force
    if ($item -isnot [IO.DirectoryInfo] -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "$Context must be a real directory, not a reparse point."
    }
    Assert-M0CompositeNoAlternateDataStreams $full $Context

    $handle = $script:M0CompositeNativeFileType::OpenDirectoryReadOnly(
        (ConvertTo-M0CompositeExtendedPath $full))
    try {
        $final = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
                $script:M0CompositeNativeFileType::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context final physical path differs from its authenticated path."
        }
        return [pscustomobject][ordered]@{
            path = $full
            identity = $script:M0CompositeNativeFileType::GetIdentity($handle)
        }
    }
    finally { $handle.Dispose() }
}

function Open-M0CompositeDirectoryAnchor {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [string] $ExpectedIdentity,
        [switch] $RequireDeleteAccess
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    Assert-M0CompositeNoReparseExistingChain $full $Context
    $item = Get-Item -LiteralPath $full -Force
    if ($item -isnot [IO.DirectoryInfo] -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "$Context must be a real directory anchor."
    }
    Assert-M0CompositeNoAlternateDataStreams $full $Context
    $handle = $null
    try {
        $handle = if ($RequireDeleteAccess) {
            $script:M0CompositeNativeFileType::OpenDirectoryReadDeleteAnchor(
                (ConvertTo-M0CompositeExtendedPath $full))
        }
        else {
            $script:M0CompositeNativeFileType::OpenDirectoryAnchor(
                (ConvertTo-M0CompositeExtendedPath $full))
        }
        $final = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
                $script:M0CompositeNativeFileType::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context final physical path differs from its authenticated path."
        }
        $identity = $script:M0CompositeNativeFileType::GetIdentity($handle)
        if (-not [string]::IsNullOrEmpty($ExpectedIdentity) -and
            $identity -cne $ExpectedIdentity) {
            throw "$Context physical identity was replaced before its anchor handle opened."
        }
        return [pscustomobject][ordered]@{
            path = $full
            identity = $identity
            handle = $handle
        }
    }
    catch {
        if ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Get-M0CompositeCurrentIdentity {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [bool] $Directory,
        [Parameter(Mandatory)] [string] $Context
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    $handle = $script:M0CompositeNativeFileType::OpenIdentityShareAll(
        (ConvertTo-M0CompositeExtendedPath $full), $Directory)
    try {
        $final = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
                $script:M0CompositeNativeFileType::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolves through a replaced physical path."
        }
        return [string]$script:M0CompositeNativeFileType::GetIdentity($handle)
    }
    finally { $handle.Dispose() }
}

function Get-M0CompositeTreeSnapshot {
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($null -eq (Get-M0CompositeExistingItem $TreeRoot)) { throw "$Context is missing: $TreeRoot" }
    $rootLease = Assert-M0CompositePhysicalDirectory $TreeRoot "$Context root"
    $root = $rootLease.path
    $records = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $caseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $directories = [Collections.Generic.List[string]]::new()
    $pending = [Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ path = $root; relative_path = '' })

    while ($pending.Count -ne 0) {
        $current = $pending.Pop()
        Assert-M0CompositePhysicalDirectory $current.path "$Context directory '$($current.relative_path)'" | Out-Null
        # Materialize the directory batch so a rejection cannot strand a lazy native
        # enumeration handle that denies cleanup of an upstream test/source tree.
        foreach ($item in [IO.DirectoryInfo]::new($current.path).GetFileSystemInfos()) {
            $relative = if ([string]::IsNullOrEmpty($current.relative_path)) {
                $item.Name
            }
            else { $current.relative_path + '/' + $item.Name }
            Assert-M0CompositeSafeRelativePath $relative "$Context entry path"
            if (-not $caseFolded.Add($relative)) {
                throw "$Context contains a case-insensitive source alias or collision: $relative"
            }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "$Context contains a reparse-point, junction, or symbolic-link entry: $relative"
            }
            if ($item -is [IO.DirectoryInfo]) {
                Assert-M0CompositeNoAlternateDataStreams $item.FullName "$Context directory '$relative'"
                $directories.Add($relative)
                $pending.Push([pscustomobject]@{ path = $item.FullName; relative_path = $relative })
                continue
            }
            if ($item -isnot [IO.FileInfo]) {
                throw "$Context contains an unsupported filesystem entry: $relative"
            }
            $lease = Open-M0CompositePhysicalFile `
                -LiteralPath $item.FullName `
                -BoundaryRoot $root `
                -Context "$Context file '$relative'"
            try {
                $records.Add($relative, [pscustomobject][ordered]@{
                        relative_path = $relative
                        size_bytes = [long]$lease.size_bytes
                        sha256 = $lease.sha256
                        identity = $lease.identity
                        physical_path = $lease.path
                    })
            }
            finally { $lease.stream.Dispose() }
        }
    }

    return [pscustomobject][ordered]@{
        root = $root
        identity = $rootLease.identity
        records = $records
        directories = $directories.ToArray()
    }
}

function New-M0CompositeRelativeLeafIndex {
    return [pscustomobject][ordered]@{
        leaves = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        ancestor_prefixes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    }
}

function Add-M0CompositeRelativeLeaf {
    param(
        [Parameter(Mandatory)] $Index,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Index.leaves.Contains($RelativePath) -or
        $Index.ancestor_prefixes.Contains($RelativePath)) {
        throw "$Context has a case-insensitive duplicate or file/directory alias at '$RelativePath'."
    }
    $segments = @($RelativePath.Split('/'))
    $prefix = [Text.StringBuilder]::new()
    $prefixes = [Collections.Generic.List[string]]::new()
    for ($segmentIndex = 0; $segmentIndex -lt ($segments.Count - 1); $segmentIndex++) {
        if ($segmentIndex -ne 0) { [void]$prefix.Append('/') }
        [void]$prefix.Append($segments[$segmentIndex])
        $value = $prefix.ToString()
        if ($Index.leaves.Contains($value)) {
            throw "$Context has a case-insensitive file/directory alias at '$RelativePath'."
        }
        $prefixes.Add($value)
    }
    if (-not $Index.leaves.Add($RelativePath)) {
        throw "$Context has a case-insensitive duplicate at '$RelativePath'."
    }
    foreach ($value in $prefixes) { [void]$Index.ancestor_prefixes.Add($value) }
}

function Assert-M0CompositeClosedOwnership {
    param([Parameter(Mandatory)] [object[]] $Ownership)

    if ($Ownership.Count -eq 0) { throw 'Composite ownership must not be empty.' }
    $normalized = [Collections.Generic.List[object]]::new()
    $destinations = New-M0CompositeRelativeLeafIndex
    $sourcesByOwner = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $ownersSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        $sourcesByOwner.Add(
            $ownerId,
            (New-M0CompositeRelativeLeafIndex))
    }

    for ($index = 0; $index -lt $Ownership.Count; $index++) {
        $row = $Ownership[$index]
        if ($null -eq $row) { throw "Composite ownership row $index must not be null." }
        $actualProperties = @($row.PSObject.Properties.Name)
        if ($actualProperties.Count -ne $script:M0CompositeOwnershipProperties.Count) {
            throw "Composite ownership row $index has an open or incomplete shape."
        }
        for ($propertyIndex = 0; $propertyIndex -lt $script:M0CompositeOwnershipProperties.Count; $propertyIndex++) {
            if ($actualProperties[$propertyIndex] -cne $script:M0CompositeOwnershipProperties[$propertyIndex]) {
                throw "Composite ownership row $index property shape or order differs from the closed contract."
            }
        }

        if ($row.owner_id -isnot [string] -or
            $script:M0CompositeOwnerIds -cnotcontains $row.owner_id) {
            throw "Composite ownership row $index has an unknown owner ID."
        }
        [void]$ownersSeen.Add([string]$row.owner_id)
        Assert-M0CompositeSafeRelativePath $row.source_relative_path `
            "Composite ownership row $index source_relative_path"
        Assert-M0CompositeSafeRelativePath $row.relative_path `
            "Composite ownership row $index relative_path"
        $sizeBytes = ConvertTo-M0CompositeInt64 $row.size_bytes `
            "Composite ownership row $index size_bytes"
        Assert-M0CompositeDigest $row.sha256 "Composite ownership row $index sha256"

        Add-M0CompositeRelativeLeaf `
            -Index $destinations `
            -RelativePath ([string]$row.relative_path) `
            -Context 'Composite ownership destination'
        Add-M0CompositeRelativeLeaf `
            -Index $sourcesByOwner[[string]$row.owner_id] `
            -RelativePath ([string]$row.source_relative_path) `
            -Context "Composite ownership source owner '$($row.owner_id)'"
        $normalized.Add([pscustomobject][ordered]@{
                owner_id = [string]$row.owner_id
                source_relative_path = [string]$row.source_relative_path
                relative_path = [string]$row.relative_path
                size_bytes = [long]$sizeBytes
                sha256 = [string]$row.sha256
            })
    }

    if ($ownersSeen.Count -ne $script:M0CompositeOwnerIds.Count) {
        throw 'Composite ownership must contain the exact four owner IDs.'
    }
    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        if (-not $ownersSeen.Contains($ownerId)) {
            throw "Composite ownership is missing required owner ID '$ownerId'."
        }
    }
    return $normalized.ToArray()
}

function Assert-M0CompositeSourceRootMap {
    param([Parameter(Mandatory)] [Collections.IDictionary] $SourceRoots)

    if ($SourceRoots.Count -ne $script:M0CompositeOwnerIds.Count) {
        throw 'SourceRoots must contain exactly the four closed owner IDs.'
    }
    $keysSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($key in $SourceRoots.Keys) {
        if ($key -isnot [string] -or $script:M0CompositeOwnerIds -cnotcontains $key -or
            -not $keysSeen.Add([string]$key)) {
            throw 'SourceRoots contains an unknown, duplicate, or non-string owner ID.'
        }
    }
    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        if (-not $keysSeen.Contains($ownerId)) { throw "SourceRoots is missing required owner ID '$ownerId'." }
        if ($SourceRoots[$ownerId] -isnot [string] -or
            [string]::IsNullOrWhiteSpace([string]$SourceRoots[$ownerId])) {
            throw "SourceRoots owner '$ownerId' must map to one non-empty filesystem path."
        }
    }
}

function Assert-M0CompositeSafeRootLayout {
    param(
        [Parameter(Mandatory)] [Collections.IDictionary] $SourceRoots,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $ScratchRoot
    )

    $output = ConvertTo-M0CompositeFullPath $OutputRoot
    $scratch = ConvertTo-M0CompositeFullPath $ScratchRoot
    foreach ($entry in @(
            [pscustomobject]@{ path = $output; context = 'Composite output root' },
            [pscustomobject]@{ path = $scratch; context = 'Composite private scratch root' })) {
        $volumeRoot = [IO.Path]::GetPathRoot($entry.path)
        if ([string]::Equals($entry.path, $volumeRoot, [StringComparison]::OrdinalIgnoreCase) -or
            [string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($entry.path))) {
            throw "$($entry.context) must not be a broad volume root."
        }
        Assert-M0CompositeNoReparseExistingChain $entry.path $entry.context
        if ($null -ne (Get-M0CompositeExistingItem $entry.path)) {
            throw "$($entry.context) already exists; composition refuses an output collision."
        }
    }
    if (Test-M0CompositePathsOverlap $output $scratch) {
        throw 'Composite output and scratch roots collide or alias each other case-insensitively.'
    }
    if (-not [string]::Equals(
            [IO.Path]::GetPathRoot($output),
            [IO.Path]::GetPathRoot($scratch),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Composite output and scratch roots must share one volume for atomic publication.'
    }

    $outputParent = ConvertTo-M0CompositeFullPath ([IO.Path]::GetDirectoryName($output))
    $scratchParent = ConvertTo-M0CompositeFullPath ([IO.Path]::GetDirectoryName($scratch))
    if (-not [IO.Directory]::Exists($outputParent) -or
        -not [IO.Directory]::Exists($scratchParent)) {
        throw 'Composite output and scratch parents must already exist for anchored publication.'
    }
    $outputParentObservation = Assert-M0CompositePhysicalDirectory `
        $outputParent 'Composite output parent'
    $scratchParentObservation = Assert-M0CompositePhysicalDirectory `
        $scratchParent 'Composite scratch parent'

    $sourcePaths = [Collections.Generic.List[string]]::new()
    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        $source = ConvertTo-M0CompositeFullPath ([string]$SourceRoots[$ownerId])
        foreach ($existing in $sourcePaths) {
            if (Test-M0CompositePathsOverlap $source $existing) {
                throw 'Composite source roots collide, overlap, or alias each other case-insensitively.'
            }
        }
        if ((Test-M0CompositePathsOverlap $source $output) -or
            (Test-M0CompositePathsOverlap $source $scratch)) {
            throw "Composite destination or scratch aliases source owner '$ownerId'."
        }
        $sourcePaths.Add($source)
    }

    return [pscustomobject][ordered]@{
        output_root = $output
        scratch_root = $scratch
        output_parent = $outputParent
        output_parent_identity = $outputParentObservation.identity
        output_leaf = [IO.Path]::GetFileName($output)
        scratch_parent = $scratchParent
        scratch_parent_identity = $scratchParentObservation.identity
    }
}

function Assert-M0CompositeOwnershipCoverage {
    param(
        [Parameter(Mandatory)] [object[]] $Ownership,
        [Parameter(Mandatory)] [Collections.IDictionary] $SourceSnapshots
    )

    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        $expected = @($Ownership | Where-Object owner_id -CEQ $ownerId)
        $snapshot = $SourceSnapshots[$ownerId]
        if ($snapshot.records.Count -ne $expected.Count) {
            throw "Source owner '$ownerId' has extra or missing ownership leaves."
        }
        foreach ($row in $expected) {
            if (-not $snapshot.records.ContainsKey($row.source_relative_path)) {
                throw "Source owner '$ownerId' is missing exact ownership leaf '$($row.source_relative_path)'."
            }
            $record = $snapshot.records[$row.source_relative_path]
            if ($record.size_bytes -ne $row.size_bytes) {
                throw "Source ownership leaf '$ownerId/$($row.source_relative_path)' has a size mismatch."
            }
            if ($record.sha256 -cne $row.sha256) {
                throw "Source ownership leaf '$ownerId/$($row.source_relative_path)' has a SHA-256 mismatch."
            }
        }
    }
}

function Add-M0CompositeOwnedIdentity {
    param(
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $Ledger,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Identity,
        [Parameter(Mandatory)] [string] $Context
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    if ($Ledger.ContainsKey($full)) {
        if ([string]$Ledger[$full] -cne $Identity) {
            throw "$Context physical identity changed after ownership was recorded."
        }
        return
    }
    $Ledger.Add($full, $Identity)
}

function New-M0CompositeOwnedDirectory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Collections.Generic.Dictionary[string,string]] $DirectoryLedger
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    if (-not (Test-M0CompositePathWithin $full $BoundaryRoot -AllowEqual)) {
        throw "$Context is outside private scratch."
    }
    Assert-M0CompositeNoReparseExistingChain $full $Context
    [IO.Directory]::CreateDirectory($full) | Out-Null
    Assert-M0CompositePhysicalDirectory $full $Context | Out-Null
    if ($null -ne $DirectoryLedger) {
        $current = $full
        while ($true) {
            $observation = Assert-M0CompositePhysicalDirectory $current $Context
            Add-M0CompositeOwnedIdentity `
                -Ledger $DirectoryLedger `
                -LiteralPath $current `
                -Identity $observation.identity `
                -Context $Context
            if ([string]::Equals(
                    $current,
                    (ConvertTo-M0CompositeFullPath $BoundaryRoot),
                    [StringComparison]::OrdinalIgnoreCase)) { break }
            $current = [IO.Path]::GetDirectoryName($current)
        }
    }
}

function Open-M0CompositeCleanupAnchor {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    $handle = $null
    try {
        # OPEN_REPARSE_POINT makes a link itself, rather than its target, the deletion subject.
        $handle = $script:M0CompositeNativeFileType::OpenCleanupAnchor(
            (ConvertTo-M0CompositeExtendedPath $full))
        $attributes = [IO.FileAttributes]$script:M0CompositeNativeFileType::GetAttributes($handle)
        $reparse = [bool]($attributes -band [IO.FileAttributes]::ReparsePoint)
        $directory = [bool]($attributes -band [IO.FileAttributes]::Directory)
        $final = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
                $script:M0CompositeNativeFileType::GetFinalPath($handle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolves through a replaced physical path."
        }
        return [pscustomobject][ordered]@{
            path = $full
            identity = [string]$script:M0CompositeNativeFileType::GetIdentity($handle)
            directory = $directory
            reparse = $reparse
            handle = $handle
        }
    }
    catch {
        if ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Remove-M0CompositeAuthenticatedTreeByHandle {
    param(
        [Parameter(Mandatory)] [string] $ScratchRoot,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $FileLedger,
        [Parameter(Mandatory)] $RootHandle
    )

    $root = ConvertTo-M0CompositeFullPath $ScratchRoot
    if ([string]::Equals(
            $root,
            [IO.Path]::GetPathRoot($root),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Composite cleanup refused a broad filesystem target.'
    }
    if (-not $DirectoryLedger.ContainsKey($root)) {
        throw 'Composite cleanup refused an unowned scratch root.'
    }

    $rootAttributes = [IO.FileAttributes]$script:M0CompositeNativeFileType::GetAttributes(
        $RootHandle)
    if (-not ($rootAttributes -band [IO.FileAttributes]::Directory) -or
        ($rootAttributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Composite cleanup refused a replaced or reparse retained scratch root.'
    }
    $rootFinal = ConvertTo-M0CompositeFullPath (ConvertFrom-M0CompositeFinalPath (
            $script:M0CompositeNativeFileType::GetFinalPath($RootHandle)))
    if (-not [string]::Equals($rootFinal, $root, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Composite cleanup refused a retained scratch root at a different physical path.'
    }
    $rootIdentity = [string]$script:M0CompositeNativeFileType::GetIdentity($RootHandle)
    if ($rootIdentity -cne [string]$DirectoryLedger[$root]) {
        throw 'Composite cleanup refused a replaced retained scratch-root identity.'
    }

    # Audit first and retain every entry handle without FILE_SHARE_DELETE. Once an entry
    # is authenticated, it cannot be renamed or substituted before handle-based deletion.
    $allChildAnchors = [Collections.Generic.List[object]]::new()
    $leafAnchors = [Collections.Generic.List[object]]::new()
    $directoryAnchors = [Collections.Generic.List[object]]::new()
    $pending = [Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ path = $root; handle = $RootHandle })
    try {
        while ($pending.Count -ne 0) {
            $current = $pending.Pop()
            foreach ($child in [IO.DirectoryInfo]::new($current.path).GetFileSystemInfos()) {
                $childPath = ConvertTo-M0CompositeFullPath $child.FullName
                if (-not (Test-M0CompositePathWithin $childPath $root) -or
                    -not [string]::Equals(
                        [IO.Path]::GetDirectoryName($childPath),
                        $current.path,
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Composite cleanup refused an entry outside its authenticated parent: $childPath"
                }
                $anchor = Open-M0CompositeCleanupAnchor `
                    -LiteralPath $childPath `
                    -Context 'Composite cleanup entry authentication'
                $allChildAnchors.Add($anchor)
                if ($anchor.reparse) {
                    $leafAnchors.Add($anchor)
                    continue
                }
                if ($anchor.directory) {
                    if (-not $DirectoryLedger.ContainsKey($childPath)) {
                        throw "Composite cleanup refused an unowned ordinary directory: $childPath"
                    }
                    if ($anchor.identity -cne [string]$DirectoryLedger[$childPath]) {
                        throw "Composite cleanup refused a replaced ordinary directory: $childPath"
                    }
                    $directoryAnchors.Add($anchor)
                    $pending.Push($anchor)
                    continue
                }
                if (-not $FileLedger.ContainsKey($childPath)) {
                    throw "Composite cleanup refused an unowned ordinary file: $childPath"
                }
                if ($anchor.identity -cne [string]$FileLedger[$childPath]) {
                    throw "Composite cleanup refused a replaced ordinary file: $childPath"
                }
                $leafAnchors.Add($anchor)
            }
        }

        foreach ($anchor in $leafAnchors) {
            $script:M0CompositeNativeFileType::MarkDeleteOnClose($anchor.handle)
            $anchor.handle.Dispose()
        }
        for ($index = $directoryAnchors.Count - 1; $index -ge 0; $index--) {
            $anchor = $directoryAnchors[$index]
            $script:M0CompositeNativeFileType::MarkDeleteOnClose($anchor.handle)
            $anchor.handle.Dispose()
        }
        $script:M0CompositeNativeFileType::MarkDeleteOnClose($RootHandle)
        $RootHandle.Dispose()
    }
    finally {
        foreach ($anchor in $allChildAnchors) { $anchor.handle.Dispose() }
    }
}

function Copy-M0CompositeOwnershipLeaf {
    param(
        [Parameter(Mandatory)] $Row,
        [Parameter(Mandatory)] $SourceSnapshot,
        [Parameter(Mandatory)] [string] $CandidateSdkRoot,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $FileLedger
    )

    $sourceRecord = $SourceSnapshot.records[$Row.source_relative_path]
    $source = Open-M0CompositePhysicalFile `
        -LiteralPath $sourceRecord.physical_path `
        -BoundaryRoot $SourceSnapshot.root `
        -Context "Composite source '$($Row.owner_id)/$($Row.source_relative_path)'" `
        -ExpectedSizeBytes $Row.size_bytes `
        -ExpectedSha256 $Row.sha256
    try {
        $destination = Join-Path $CandidateSdkRoot (
            $Row.relative_path.Replace('/', [IO.Path]::DirectorySeparatorChar))
        New-M0CompositeOwnedDirectory `
            -LiteralPath ([IO.Path]::GetDirectoryName($destination)) `
            -BoundaryRoot $CandidateSdkRoot `
            -Context "Composite destination parent '$($Row.relative_path)'" `
            -DirectoryLedger $DirectoryLedger
        if ($null -ne (Get-M0CompositeExistingItem $destination)) {
            throw "Composite destination collision occurred before create-new copy: $($Row.relative_path)"
        }
        $output = [IO.FileStream]::new(
            $destination,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::Read,
            65536,
            [IO.FileOptions]::SequentialScan)
        try {
            $source.stream.Position = 0
            $source.stream.CopyTo($output, 65536)
            $output.Flush($true)
            if ($output.Length -ne $Row.size_bytes) {
                throw "Composite create-new copy changed size: $($Row.relative_path)"
            }
        }
        finally { $output.Dispose() }

        $candidate = Open-M0CompositePhysicalFile `
            -LiteralPath $destination `
            -BoundaryRoot $CandidateSdkRoot `
            -Context "Composite candidate '$($Row.relative_path)'" `
            -ExpectedSizeBytes $Row.size_bytes `
            -ExpectedSha256 $Row.sha256
        try {
            if ($candidate.identity -ceq $source.identity) {
                throw "Composite candidate aliases source physical storage: $($Row.relative_path)"
            }
            Add-M0CompositeOwnedIdentity `
                -Ledger $FileLedger `
                -LiteralPath $destination `
                -Identity $candidate.identity `
                -Context "Composite candidate '$($Row.relative_path)'"
            return [string]$source.identity
        }
        finally { $candidate.stream.Dispose() }
    }
    finally { $source.stream.Dispose() }
}

function Get-M0CompositeExpectedDirectories {
    param([Parameter(Mandatory)] [object[]] $Ownership)

    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Ownership) {
        $current = [IO.Path]::GetDirectoryName(
            $row.relative_path.Replace('/', [IO.Path]::DirectorySeparatorChar))
        while (-not [string]::IsNullOrEmpty($current)) {
            [void]$directories.Add(
                $current.Replace([IO.Path]::DirectorySeparatorChar, '/'))
            $current = [IO.Path]::GetDirectoryName($current)
        }
    }
    Write-Output -NoEnumerate $directories
}

function Assert-M0CompositeCandidate {
    param(
        [Parameter(Mandatory)] [string] $CandidateSdkRoot,
        [Parameter(Mandatory)] [object[]] $Ownership,
        [Parameter(Mandatory)] [Collections.IDictionary] $SourceIdentities
    )

    $snapshot = Get-M0CompositeTreeSnapshot $CandidateSdkRoot 'Composite candidate SDK'
    if ($snapshot.records.Count -ne $Ownership.Count) {
        throw 'Composite candidate SDK has extra or missing ownership leaves.'
    }
    $expectedDirectories = Get-M0CompositeExpectedDirectories $Ownership
    if ($snapshot.directories.Count -ne $expectedDirectories.Count) {
        throw 'Composite candidate SDK has extra or missing directories.'
    }
    foreach ($directory in $snapshot.directories) {
        if (-not $expectedDirectories.Contains($directory)) {
            throw "Composite candidate SDK contains an unexpected directory: $directory"
        }
    }
    foreach ($row in $Ownership) {
        if (-not $snapshot.records.ContainsKey($row.relative_path)) {
            throw "Composite candidate SDK is missing exact destination '$($row.relative_path)'."
        }
        $record = $snapshot.records[$row.relative_path]
        if ($record.size_bytes -ne $row.size_bytes -or $record.sha256 -cne $row.sha256) {
            throw "Composite candidate SDK leaf differs from ownership: $($row.relative_path)"
        }
        if ($record.identity -ceq $SourceIdentities[$row.relative_path]) {
            throw "Composite candidate SDK leaf aliases upstream storage: $($row.relative_path)"
        }
    }
}

function Assert-M0CompositeCandidateEnvelope {
    param(
        [Parameter(Mandatory)] [string] $ScratchRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedInventorySizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256
    )

    $scratch = (Assert-M0CompositePhysicalDirectory `
            $ScratchRoot 'Composite candidate envelope root').path
    $expectedRootEntries = [ordered]@{ sdk = 'directory'; evidence = 'directory' }
    $rootEntries = @([IO.DirectoryInfo]::new($scratch).GetFileSystemInfos())
    if ($rootEntries.Count -ne $expectedRootEntries.Count) {
        throw 'Composite candidate envelope has extra or missing root entries.'
    }
    foreach ($entry in $rootEntries) {
        if (-not $expectedRootEntries.Contains([string]$entry.Name) -or
            $entry.Name -cne @($expectedRootEntries.Keys | Where-Object { $_ -ieq $entry.Name })[0]) {
            throw "Composite candidate envelope contains an unexpected or case-aliased root entry: $($entry.Name)"
        }
        if ($entry -isnot [IO.DirectoryInfo] -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Composite candidate envelope root entry is not a real directory: $($entry.Name)"
        }
        Assert-M0CompositePhysicalDirectory `
            $entry.FullName "Composite candidate envelope '$($entry.Name)'" | Out-Null
    }

    $evidenceRoot = Join-Path $scratch 'evidence'
    $evidenceEntries = @([IO.DirectoryInfo]::new($evidenceRoot).GetFileSystemInfos())
    if ($evidenceEntries.Count -ne 1 -or
        $evidenceEntries[0] -isnot [IO.FileInfo] -or
        $evidenceEntries[0].Name -cne 'composite-inventory.tsv' -or
        ($evidenceEntries[0].Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Composite candidate evidence directory differs from its closed inventory-only shape.'
    }
    $inventory = Open-M0CompositePhysicalFile `
        -LiteralPath $InventoryPath `
        -BoundaryRoot $scratch `
        -Context 'Composite canonical inventory' `
        -ExpectedSizeBytes $ExpectedInventorySizeBytes `
        -ExpectedSha256 $ExpectedInventorySha256
    $inventory.stream.Dispose()
}

function Write-M0CompositeCreateNewBytes {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $FileLedger
    )

    $full = ConvertTo-M0CompositeFullPath $LiteralPath
    if (-not (Test-M0CompositePathWithin $full $BoundaryRoot)) {
        throw "$Context is outside private scratch."
    }
    New-M0CompositeOwnedDirectory `
        -LiteralPath ([IO.Path]::GetDirectoryName($full)) `
        -BoundaryRoot $BoundaryRoot `
        -Context "$Context parent" `
        -DirectoryLedger $DirectoryLedger
    if ($null -ne (Get-M0CompositeExistingItem $full)) { throw "$Context already exists." }
    $stream = [IO.FileStream]::new(
        $full, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    }
    finally { $stream.Dispose() }
    $identity = Get-M0CompositeCurrentIdentity $full $false $Context
    Add-M0CompositeOwnedIdentity `
        -Ledger $FileLedger `
        -LiteralPath $full `
        -Identity $identity `
        -Context $Context
}

function Invoke-M0QtShellCompositeNative {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [Collections.IDictionary] $SourceRoots,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $ScratchRoot,
        [Parameter(Mandatory)] [object[]] $Ownership
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new(
            'Native Qt shell composition requires Windows and NTFS physical-file semantics.')
    }
    Assert-M0CompositeSourceRootMap $SourceRoots
    $rows = @(Assert-M0CompositeClosedOwnership $Ownership)
    $layout = Assert-M0CompositeSafeRootLayout `
        -SourceRoots $SourceRoots `
        -OutputRoot $OutputRoot `
        -ScratchRoot $ScratchRoot

    $sourceSnapshots = [ordered]@{}
    $sourceRootIdentities = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($ownerId in $script:M0CompositeOwnerIds) {
        $snapshot = Get-M0CompositeTreeSnapshot `
            -TreeRoot ([string]$SourceRoots[$ownerId]) `
            -Context "Composite source owner '$ownerId'"
        if (-not $sourceRootIdentities.Add([string]$snapshot.identity)) {
            throw 'Composite source roots are physical aliases of the same directory.'
        }
        $sourceSnapshots[$ownerId] = $snapshot
    }
    Assert-M0CompositeOwnershipCoverage -Ownership $rows -SourceSnapshots $sourceSnapshots

    # Collision rejection and complete source authentication happen before this module creates scratch/output.
    if ($null -ne (Get-M0CompositeExistingItem $layout.output_root) -or
        $null -ne (Get-M0CompositeExistingItem $layout.scratch_root)) {
        throw 'Composite output or scratch collision appeared before copy.'
    }

    $directoryLedger = [Collections.Generic.Dictionary[string,string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $fileLedger = [Collections.Generic.Dictionary[string,string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $outputParentAnchor = $null
    $scratchParentAnchor = $null
    $scratchAnchor = $null
    $ownsScratch = $false
    try {
        $outputParentAnchor = Open-M0CompositeDirectoryAnchor `
            -LiteralPath $layout.output_parent `
            -Context 'Composite retained output-parent anchor' `
            -ExpectedIdentity $layout.output_parent_identity
        $scratchParentAnchor = Open-M0CompositeDirectoryAnchor `
            -LiteralPath $layout.scratch_parent `
            -Context 'Composite retained scratch-parent anchor' `
            -ExpectedIdentity $layout.scratch_parent_identity
        New-M0CompositeOwnedDirectory `
            -LiteralPath $layout.scratch_root `
            -BoundaryRoot $layout.scratch_root `
            -Context 'Composite private scratch root' `
            -DirectoryLedger $directoryLedger
        $ownsScratch = $true
        $scratchAnchor = Open-M0CompositeDirectoryAnchor `
            -LiteralPath $layout.scratch_root `
            -Context 'Composite retained scratch-root anchor' `
            -ExpectedIdentity $directoryLedger[$layout.scratch_root] `
            -RequireDeleteAccess
        $candidateSdkRoot = Join-Path $layout.scratch_root 'sdk'
        New-M0CompositeOwnedDirectory `
            -LiteralPath $candidateSdkRoot `
            -BoundaryRoot $layout.scratch_root `
            -Context 'Composite candidate SDK root' `
            -DirectoryLedger $directoryLedger

        $sourceIdentities = [Collections.Generic.Dictionary[string,string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        foreach ($row in $rows) {
            $identity = Copy-M0CompositeOwnershipLeaf `
                -Row $row `
                -SourceSnapshot $sourceSnapshots[$row.owner_id] `
                -CandidateSdkRoot $candidateSdkRoot `
                -DirectoryLedger $directoryLedger `
                -FileLedger $fileLedger
            $sourceIdentities.Add($row.relative_path, $identity)
        }

        $inventoryBuilder = [Text.StringBuilder]::new()
        foreach ($row in $rows) {
            [void]$inventoryBuilder.Append($row.relative_path).Append("`t").Append(
                $row.owner_id).Append("`t").Append($row.source_relative_path).Append("`t").Append(
                $row.size_bytes.ToString([Globalization.CultureInfo]::InvariantCulture)).Append("`t").Append(
                $row.sha256).Append("`n")
        }
        $inventoryRelativePath = 'evidence/composite-inventory.tsv'
        $inventoryPath = Join-Path $layout.scratch_root (
            $inventoryRelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
        $inventoryBytes = $script:M0CompositeUtf8.GetBytes($inventoryBuilder.ToString())
        Write-M0CompositeCreateNewBytes `
            -LiteralPath $inventoryPath `
            -Bytes $inventoryBytes `
            -BoundaryRoot $layout.scratch_root `
            -Context 'Composite canonical inventory' `
            -DirectoryLedger $directoryLedger `
            -FileLedger $fileLedger
        $inventoryHash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($inventoryBytes)).ToLowerInvariant()
        Assert-M0CompositeCandidate `
            -CandidateSdkRoot $candidateSdkRoot `
            -Ownership $rows `
            -SourceIdentities $sourceIdentities
        Assert-M0CompositeCandidateEnvelope `
            -ScratchRoot $layout.scratch_root `
            -InventoryPath $inventoryPath `
            -ExpectedInventorySizeBytes ([long]$inventoryBytes.Length) `
            -ExpectedInventorySha256 $inventoryHash

        $Context = [pscustomobject][ordered]@{ candidate_sdk_root = $candidateSdkRoot }
    # TEST-FIXTURE-INSERTION-POINT: COMPOSITE_PREPUBLICATION

        Assert-M0CompositeCandidate `
            -CandidateSdkRoot $candidateSdkRoot `
            -Ownership $rows `
            -SourceIdentities $sourceIdentities
        Assert-M0CompositeCandidateEnvelope `
            -ScratchRoot $layout.scratch_root `
            -InventoryPath $inventoryPath `
            -ExpectedInventorySizeBytes ([long]$inventoryBytes.Length) `
            -ExpectedInventorySha256 $inventoryHash

        $result = [pscustomobject][ordered]@{
            exit_code = [long]0
            sdk_root = 'sdk'
            inventory_relative_path = $inventoryRelativePath
            entries = $rows
            published_root_identity = [string]$script:M0CompositeNativeFileType::GetIdentity(
                $scratchAnchor.handle)
        }
        $script:M0CompositeNativeFileType::RenameNoReplaceRelative(
            $scratchAnchor.handle,
            $outputParentAnchor.handle,
            $layout.output_leaf)
        $ownsScratch = $false
        return $result
    }
    catch {
        $failure = $_
        if ($ownsScratch) {
            try {
                if ($null -ne $scratchAnchor) {
                    $anchoredIdentity = $script:M0CompositeNativeFileType::GetIdentity(
                        $scratchAnchor.handle)
                    if ($anchoredIdentity -cne [string]$directoryLedger[$layout.scratch_root]) {
                        throw 'Composite cleanup refused a replaced retained scratch-root identity.'
                    }
                }
                else {
                    throw 'Composite cleanup refused to re-resolve scratch without its retained handle.'
                }
                Remove-M0CompositeAuthenticatedTreeByHandle `
                    -ScratchRoot $layout.scratch_root `
                    -DirectoryLedger $directoryLedger `
                    -FileLedger $fileLedger `
                    -RootHandle $scratchAnchor.handle
            }
            catch {
                throw [AggregateException]::new(
                    'Composite composition failed and exact private-scratch cleanup also failed.',
                    [Exception[]]@($failure.Exception, $_.Exception))
            }
        }
        throw $failure
    }
    finally {
        if ($null -ne $scratchAnchor) { $scratchAnchor.handle.Dispose() }
        if ($null -ne $scratchParentAnchor) { $scratchParentAnchor.handle.Dispose() }
        if ($null -ne $outputParentAnchor) { $outputParentAnchor.handle.Dispose() }
    }
}

Export-ModuleMember -Function Invoke-M0QtShellCompositeNative
