#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Utf8 = [Text.UTF8Encoding]::new($false, $true)
$script:NativeTokenNames = @('INSTALL_ROOT', 'BUILD_ROOT', 'VULKAN_INCLUDE', 'NINJA', 'CL', 'RC', 'MT')
$script:NativeTreeIds = @('qtbase-before', 'qtbase-patched', 'vulkan', 'install')
$script:NativeTreeRoots = @('source/qtbase-before', 'source/qtbase-patched', 'source/vulkan', 'install')
$script:NativeCommandIds = @('configure', 'build', 'install')
$script:NativeReportIds = @(
    'configure-summary', 'cmake-cache', 'qtgui-freetype-dependency',
    'qt6gui-headers', 'qt6gui-imports', 'qt6gui-exports', 'qminimal-imports'
)

if ($IsWindows) {
    $nativeNamespace = 'M0QtBaseRefreshTreeNative_' + [Guid]::NewGuid().ToString('N')
    $nativeTypeDefinition = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace __M0_NATIVE_NAMESPACE__ {
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

    public static class FileIdentity {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WIN32_FIND_STREAM_DATA {
            public long StreamSize;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)]
            public string StreamName;
        }

        private sealed class SafeFindHandle : SafeHandleZeroOrMinusOneIsInvalid {
            private SafeFindHandle() : base(true) { }
            protected override bool ReleaseHandle() { return FindClose(handle); }
        }

        private const uint GENERIC_READ = 0x80000000;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint OPEN_EXISTING = 3;
        private const int ERROR_HANDLE_EOF = 38;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(IntPtr handle, out BY_HANDLE_FILE_INFORMATION info);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(IntPtr handle, StringBuilder path,
            uint pathLength, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFindHandle FindFirstStreamW(string fileName, int infoLevel,
            out WIN32_FIND_STREAM_DATA findStreamData, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextStreamW(SafeFindHandle findHandle,
            out WIN32_FIND_STREAM_DATA findStreamData);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FindClose(IntPtr findHandle);

        public static SafeFileHandle OpenReadOnlyWithoutWriteDeleteSharing(string path) {
            var handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, IntPtr.Zero,
                OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static uint GetLinkCount(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out info)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return info.NumberOfLinks;
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle.DangerousGetHandle(), buffer,
                (uint)buffer.Capacity, 0);
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
'@.Replace('__M0_NATIVE_NAMESPACE__', $nativeNamespace)
    $compiledNativeTypes = @(Add-Type -TypeDefinition $nativeTypeDefinition -PassThru)
    $fileIdentityTypes = @($compiledNativeTypes | Where-Object FullName -CEQ "$nativeNamespace.FileIdentity")
    if ($fileIdentityTypes.Count -ne 1) { throw 'Native file identity type compilation returned an unexpected type set.' }
    $script:NativeFileIdentityType = [type]$fileIdentityTypes[0]
}
else {
    $script:NativeFileIdentityType = $null
}

function Assert-M0NativeDigest {
    param([Parameter(Mandatory)] $Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context must be a lowercase SHA-256 digest."
    }
}

function Assert-M0NativeNonNegativeInt64 {
    param([Parameter(Mandatory)] $Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [long] -and $Value -isnot [int]) {
        throw "$Context must be an Int64-compatible integer."
    }
    if ([long]$Value -lt 0) { throw "$Context must not be negative." }
}

function ConvertTo-M0NativeExtendedPath {
    param([Parameter(Mandatory)] [string] $Path)
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $full.Substring(2) }
    return '\\?\' + $full
}

function ConvertFrom-M0NativeFinalPath {
    param([Parameter(Mandatory)] [string] $Path)
    if ($Path.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $Path.Substring(8)
    }
    if ($Path.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $Path.Substring(4) }
    return $Path
}

function ConvertTo-M0NativeFullPath {
    param([Parameter(Mandatory)] [string] $Path)
    return [IO.Path]::GetFullPath($Path).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
}

function Test-M0NativePathWithin {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Boundary,
        [switch] $AllowEqual
    )
    $candidateFull = ConvertTo-M0NativeFullPath $Candidate
    $boundaryFull = ConvertTo-M0NativeFullPath $Boundary
    if ($AllowEqual -and [string]::Equals($candidateFull, $boundaryFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    $prefix = $boundaryFull + [IO.Path]::DirectorySeparatorChar
    return $candidateFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-M0NativeNoReparseExistingChain {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    $current = [IO.Path]::GetFullPath($LiteralPath)
    while (-not (Test-Path -LiteralPath $current)) {
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $current) { break }
        $current = $parent
    }
    while (-not [string]::IsNullOrEmpty($current)) {
        $item = Get-Item -LiteralPath $current -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse point in its physical ancestry: $current"
        }
        $parent = [IO.Path]::GetDirectoryName($current.TrimEnd([IO.Path]::DirectorySeparatorChar))
        if ([string]::IsNullOrEmpty($parent) -or $parent -ceq $current) { break }
        $current = $parent
    }
}

function Assert-M0NativeSafeRelativePath {
    param([Parameter(Mandatory)] $Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or [string]::IsNullOrWhiteSpace($Value)) {
        throw "$Context must be a non-empty relative path."
    }
    if ([IO.Path]::IsPathRooted($Value) -or $Value.StartsWith('/', [StringComparison]::Ordinal) -or
        $Value.StartsWith('\\', [StringComparison]::Ordinal) -or $Value.Contains('\\') -or
        $Value.Contains(':') -or $Value.Contains([char]0)) {
        throw "$Context is absolute or contains an unsafe separator, ADS colon, or NUL."
    }
    $segments = @($Value -split '/', -1)
    if ($segments.Count -eq 0) { throw "$Context has no path segments." }
    foreach ($segment in $segments) {
        if ($segment -in @('', '.', '..')) { throw "$Context contains an empty, current, or parent traversal segment." }
        if ($segment.EndsWith(' ', [StringComparison]::Ordinal) -or $segment.EndsWith('.', [StringComparison]::Ordinal)) {
            throw "$Context contains a Windows-ambiguous trailing dot or space."
        }
        if ($segment.IndexOfAny([char[]]'<>"|?*') -ge 0 -or @($segment.ToCharArray() | Where-Object { [int]$_ -lt 32 }).Count -ne 0) {
            throw "$Context contains Windows-invalid path characters."
        }
        $deviceStem = ($segment -split '\.', 2)[0]
        if ($deviceStem -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
            throw "$Context contains a reserved Windows device name."
        }
    }
}

function Get-M0NativeSha256FromStream {
    param([Parameter(Mandatory)] [IO.FileStream] $Stream)
    $position = $Stream.Position
    try {
        $Stream.Position = 0
        return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    }
    finally { $Stream.Position = $position }
}

function Get-M0NativeSha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Assert-M0NativeNoAlternateDataStreams {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    if (-not $IsWindows) { return }
    $streams = @($script:NativeFileIdentityType::GetStreamNames((ConvertTo-M0NativeExtendedPath $LiteralPath)))
    $unexpected = @($streams | Where-Object { $_ -cne '::$DATA' })
    if ($unexpected.Count -ne 0) {
        throw "$Context contains an alternate data stream: $($unexpected[0])"
    }
}

function Open-M0NativePhysicalFileCore {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [string] $BoundaryRoot,
        [Nullable[long]] $ExpectedSizeBytes,
        [string] $ExpectedSha256,
        [switch] $SkipReparseChain,
        [Parameter(Mandatory)] [bool] $RequireSingleLink
    )
    if (-not $IsWindows) { throw "$Context requires the Windows physical-file identity gate." }
    if (-not [IO.File]::Exists($LiteralPath)) { throw "$Context is missing: $LiteralPath" }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($BoundaryRoot -and -not (Test-M0NativePathWithin $full $BoundaryRoot -AllowEqual)) {
        throw "$Context is outside its physical boundary."
    }
    if (-not $SkipReparseChain) { Assert-M0NativeNoReparseExistingChain $full $Context }

    $handle = $null
    $stream = $null
    try {
        $handle = $script:NativeFileIdentityType::OpenReadOnlyWithoutWriteDeleteSharing(
            (ConvertTo-M0NativeExtendedPath $full))
        Assert-M0NativeNoAlternateDataStreams $full $Context
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::Read, 65536, $false)
        $handle = $null
        $finalPath = [IO.Path]::GetFullPath((ConvertFrom-M0NativeFinalPath (
            $script:NativeFileIdentityType::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($finalPath, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context final physical path differs from the authenticated path."
        }
        $linkCount = [long]$script:NativeFileIdentityType::GetLinkCount($stream.SafeFileHandle)
        if ($RequireSingleLink -and $linkCount -ne 1) {
            throw "$Context has physical hardlink count $linkCount; exactly one is required."
        }
        if ($null -ne $ExpectedSizeBytes -and $stream.Length -ne [long]$ExpectedSizeBytes) {
            throw "$Context size differs from the expected anchor."
        }
        $sha256 = Get-M0NativeSha256FromStream $stream
        if ($ExpectedSha256) {
            Assert-M0NativeDigest $ExpectedSha256 "$Context expected SHA-256"
            if ($sha256 -cne $ExpectedSha256) { throw "$Context SHA-256 differs from the expected anchor." }
        }
        return [pscustomobject]@{
            path = $full
            size_bytes = [long]$stream.Length
            sha256 = $sha256
            link_count = $linkCount
            stream = $stream
        }
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        elseif ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Open-M0NativePhysicalFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [string] $BoundaryRoot,
        [Nullable[long]] $ExpectedSizeBytes,
        [string] $ExpectedSha256,
        [switch] $SkipReparseChain
    )
    return Open-M0NativePhysicalFileCore `
        -LiteralPath $LiteralPath `
        -Context $Context `
        -BoundaryRoot $BoundaryRoot `
        -ExpectedSizeBytes $ExpectedSizeBytes `
        -ExpectedSha256 $ExpectedSha256 `
        -SkipReparseChain:$SkipReparseChain `
        -RequireSingleLink $true
}

function Open-M0NativeInstallHardlinkSource {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [switch] $SkipReparseChain
    )
    return Open-M0NativePhysicalFileCore `
        -LiteralPath $LiteralPath `
        -Context $Context `
        -BoundaryRoot $BoundaryRoot `
        -SkipReparseChain:$SkipReparseChain `
        -RequireSingleLink $false
}

function Get-M0NativeTreeData {
    param([Parameter(Mandatory)] [string] $TreeRoot)
    if (-not [IO.Directory]::Exists($TreeRoot)) { throw "Physical tree root is missing: $TreeRoot" }
    $root = ConvertTo-M0NativeFullPath $TreeRoot
    Assert-M0NativeNoReparseExistingChain $root 'Physical tree root'
    $rootItem = Get-Item -LiteralPath $root -Force
    if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Physical tree root must be a real directory, not a reparse point.'
    }

    $seenOrdinal = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenCaseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $recordByPath = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
    $directories = [Collections.Generic.List[string]]::new()
    $pending = [Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ path = $root; relative_path = '' })

    while ($pending.Count -ne 0) {
        $current = $pending.Pop()
        foreach ($item in [IO.DirectoryInfo]::new($current.path).EnumerateFileSystemInfos()) {
            $relative = if ([string]::IsNullOrEmpty($current.relative_path)) {
                $item.Name
            }
            else { $current.relative_path + '/' + $item.Name }
            Assert-M0NativeSafeRelativePath $relative 'Physical tree entry path'
            if (-not $seenOrdinal.Add($relative)) { throw "Physical tree has duplicate path '$relative'." }
            if (-not $seenCaseFolded.Add($relative)) { throw "Physical tree has a case-folded path collision at '$relative'." }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Physical tree contains a reparse-point entry: $relative"
            }
            if ($item -is [IO.DirectoryInfo]) {
                $directories.Add($relative)
                $pending.Push([pscustomobject]@{ path = $item.FullName; relative_path = $relative })
                continue
            }
            if ($item -isnot [IO.FileInfo]) { throw "Physical tree contains an unsupported entry type: $relative" }
            $lease = Open-M0NativePhysicalFile `
                -LiteralPath $item.FullName `
                -Context "Physical tree file '$relative'" `
                -BoundaryRoot $root `
                -SkipReparseChain
            try {
                $recordByPath.Add($relative, [pscustomobject]@{
                    relative_path = $relative
                    size_bytes = [long]$lease.size_bytes
                    sha256 = $lease.sha256
                    physical_path = $lease.path
                })
            }
            finally { $lease.stream.Dispose() }
        }
    }

    $paths = [string[]]@($recordByPath.Keys)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    $directoryPaths = [string[]]@($directories)
    [Array]::Sort($directoryPaths, [StringComparer]::Ordinal)
    $orderedRecords = [Collections.Generic.List[object]]::new()
    $totalBytes = [long]0
    $builder = [Text.StringBuilder]::new()
    foreach ($path in $paths) {
        $record = $recordByPath[$path]
        if ($record.size_bytes -gt ([long]::MaxValue - $totalBytes)) { throw 'Physical tree byte total overflowed Int64.' }
        $totalBytes += $record.size_bytes
        $orderedRecords.Add($record)
        [void]$builder.Append($record.relative_path).Append("`t").Append(
            $record.size_bytes.ToString([Globalization.CultureInfo]::InvariantCulture)).Append("`t").Append(
            $record.sha256).Append("`n")
    }
    if ($orderedRecords.Count -eq 0) { throw 'Physical tree must contain at least one regular file.' }
    $bytes = $script:Utf8.GetBytes($builder.ToString())
    return [pscustomobject]@{
        root = $root
        directories = $directoryPaths
        records = $orderedRecords.ToArray()
        file_count = [long]$orderedRecords.Count
        total_bytes = $totalBytes
        bytes = $bytes
        sha256 = Get-M0NativeSha256Bytes $bytes
    }
}

function Write-M0NativeCreateNewFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ([IO.File]::Exists($full) -or [IO.Directory]::Exists($full)) { throw "$Context already exists: $full" }
    $parent = [IO.Path]::GetDirectoryName($full)
    Assert-M0NativeNoReparseExistingChain $parent "$Context parent"
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    Assert-M0NativeNoReparseExistingChain $parent "$Context parent"
    $stream = [IO.FileStream]::new($full, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    }
    finally { $stream.Dispose() }
}

function Remove-M0NativeNewOutput {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $volumeRoot = [IO.Path]::GetPathRoot($full)
    if ([string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($full)) -or
        [string]::Equals($full, $volumeRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context cleanup refused a broad filesystem target."
    }
    if ([IO.File]::Exists($full)) {
        [IO.File]::Delete($full)
        return
    }
    if ([IO.Directory]::Exists($full)) {
        $item = Get-Item -LiteralPath $full -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            [IO.Directory]::Delete($full, $false)
        }
        else {
            [IO.Directory]::Delete($full, $true)
        }
    }
}

function Get-M0NativePhysicalLinkCount {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [switch] $SkipReparseChain
    )
    if (-not $IsWindows) { throw "$Context requires the Windows physical-file identity gate." }
    if (-not [IO.File]::Exists($LiteralPath)) { throw "$Context is missing: $LiteralPath" }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-M0NativePathWithin $full $BoundaryRoot -AllowEqual)) {
        throw "$Context is outside its physical boundary."
    }
    if (-not $SkipReparseChain) { Assert-M0NativeNoReparseExistingChain $full $Context }

    $handle = $script:NativeFileIdentityType::OpenReadOnlyWithoutWriteDeleteSharing(
        (ConvertTo-M0NativeExtendedPath $full))
    try {
        Assert-M0NativeNoAlternateDataStreams $full $Context
        $finalPath = [IO.Path]::GetFullPath((ConvertFrom-M0NativeFinalPath (
                    $script:NativeFileIdentityType::GetFinalPath($handle))))
        if (-not [string]::Equals($finalPath, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context final physical path differs from the expected path."
        }
        return [long]$script:NativeFileIdentityType::GetLinkCount($handle)
    }
    finally { $handle.Dispose() }
}

function Convert-M0NativeInstallHardlinksToCopies {
    param(
        [Parameter(Mandatory)] [string] $InstallRoot,
        [Parameter(Mandatory)] [string] $RunRoot
    )
    $runRootFull = ConvertTo-M0NativeFullPath $RunRoot
    $installRootFull = ConvertTo-M0NativeFullPath $InstallRoot
    if (-not (Test-M0NativePathWithin $installRootFull $runRootFull)) {
        throw 'Install hardlink materialization root is outside RunRoot.'
    }
    if (-not [IO.Directory]::Exists($installRootFull)) { throw 'Install hardlink materialization root is missing.' }
    Assert-M0NativeNoReparseExistingChain $installRootFull 'Install hardlink materialization root'

    $seenOrdinal = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenCaseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $fileByPath = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $pending = [Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ path = $installRootFull; relative_path = '' })
    while ($pending.Count -ne 0) {
        $current = $pending.Pop()
        foreach ($item in [IO.DirectoryInfo]::new($current.path).EnumerateFileSystemInfos()) {
            $relative = if ([string]::IsNullOrEmpty($current.relative_path)) {
                $item.Name
            }
            else { $current.relative_path + '/' + $item.Name }
            Assert-M0NativeSafeRelativePath $relative 'Install hardlink materialization entry path'
            if (-not $seenOrdinal.Add($relative)) { throw "Install tree has duplicate path '$relative'." }
            if (-not $seenCaseFolded.Add($relative)) { throw "Install tree has a case-folded path collision at '$relative'." }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Install tree contains a reparse-point entry: $relative"
            }
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Push([pscustomobject]@{ path = $item.FullName; relative_path = $relative })
            }
            elseif ($item -is [IO.FileInfo]) { $fileByPath.Add($relative, $item.FullName) }
            else { throw "Install tree contains an unsupported entry type: $relative" }
        }
    }

    $paths = [string[]]@($fileByPath.Keys)
    [Array]::Sort($paths, [StringComparer]::Ordinal)
    $hardlinkedPaths = [Collections.Generic.List[string]]::new()
    foreach ($relative in $paths) {
        $linkCount = Get-M0NativePhysicalLinkCount `
            -LiteralPath $fileByPath[$relative] `
            -Context "Installed file '$relative'" `
            -BoundaryRoot $installRootFull `
            -SkipReparseChain
        if ($linkCount -gt 1) { $hardlinkedPaths.Add($relative) }
    }
    if ($hardlinkedPaths.Count -eq 0) { return }

    $scratchRoot = Resolve-M0NativeRunPath $runRootFull 'n' 'Install hardlink materialization scratch root'
    if ([IO.Directory]::Exists($scratchRoot) -or [IO.File]::Exists($scratchRoot)) {
        throw 'Install hardlink materialization scratch root must be CreateNew.'
    }
    [IO.Directory]::CreateDirectory($scratchRoot) | Out-Null
    Assert-M0NativeNoReparseExistingChain $scratchRoot 'Install hardlink materialization scratch root'
    try {
        $index = 0
        foreach ($relative in $hardlinkedPaths) {
            $sourcePath = $fileByPath[$relative]
            $source = Open-M0NativeInstallHardlinkSource `
                -LiteralPath $sourcePath `
                -Context "Installed hardlink '$relative'" `
                -BoundaryRoot $installRootFull `
                -SkipReparseChain
            try {
                if ($source.link_count -eq 1) { continue }
                $tempPath = Join-Path $scratchRoot $index.ToString('D8', [Globalization.CultureInfo]::InvariantCulture)
                $index++
                $temp = [IO.FileStream]::new($tempPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
                try {
                    $source.stream.Position = 0
                    $source.stream.CopyTo($temp, 65536)
                    $temp.Flush($true)
                }
                finally { $temp.Dispose() }
                $expectedSize = $source.size_bytes
                $expectedSha256 = $source.sha256
            }
            finally { $source.stream.Dispose() }

            [IO.File]::Move($tempPath, $sourcePath, $true)
            $verified = Open-M0NativePhysicalFile `
                -LiteralPath $sourcePath `
                -Context "Materialized installed file '$relative'" `
                -BoundaryRoot $installRootFull `
                -ExpectedSizeBytes $expectedSize `
                -ExpectedSha256 $expectedSha256
            $verified.stream.Dispose()
        }
    }
    finally {
        if ([IO.Directory]::Exists($scratchRoot) -or [IO.File]::Exists($scratchRoot)) {
            Remove-M0NativeNewOutput -LiteralPath $scratchRoot -Context 'Install hardlink materialization scratch root'
        }
    }
}

function Read-M0NativeCanonicalInventory {
    param([Parameter(Mandatory)] [string] $InventoryPath)
    $lease = Open-M0NativePhysicalFile -LiteralPath $InventoryPath -Context 'Canonical inventory'
    try {
        if ($lease.size_bytes -gt [int]::MaxValue) { throw 'Canonical inventory is unexpectedly large.' }
        $bytes = [byte[]]::new([int]$lease.size_bytes)
        $lease.stream.Position = 0
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $lease.stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'Canonical inventory ended unexpectedly.' }
            $offset += $read
        }
    }
    finally { $lease.stream.Dispose() }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw 'Canonical inventory must not contain a UTF-8 BOM.'
    }
    $text = $script:Utf8.GetString($bytes)
    if ($text.Contains("`r")) { throw 'Canonical inventory must use LF and must not contain CR.' }
    if (-not $text.EndsWith("`n", [StringComparison]::Ordinal)) { throw 'Canonical inventory must end in exactly one LF.' }
    if ($text.EndsWith("`n`n", [StringComparison]::Ordinal)) { throw 'Canonical inventory must not contain a blank final record.' }
    $lines = $text.Substring(0, $text.Length - 1).Split("`n", [StringSplitOptions]::None)
    if ($lines.Count -eq 0) { throw 'Canonical inventory must contain at least one record.' }
    $seenCaseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $records = [Collections.Generic.List[object]]::new()
    $previous = $null
    $totalBytes = [long]0
    foreach ($line in $lines) {
        $fields = $line.Split("`t", [StringSplitOptions]::None)
        if ($fields.Count -ne 3) { throw 'Canonical inventory record must contain exactly three TSV fields.' }
        $path = $fields[0]
        Assert-M0NativeSafeRelativePath $path 'Canonical inventory path'
        if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous, $path) -ge 0) {
            throw 'Canonical inventory paths must be strictly ordinal-sorted and unique.'
        }
        if (-not $seenCaseFolded.Add($path)) { throw "Canonical inventory contains a case-folded duplicate path '$path'." }
        if ($fields[1] -cnotmatch '^(0|[1-9][0-9]*)$') { throw 'Canonical inventory size is not a canonical non-negative integer.' }
        $size = [long]0
        if (-not [long]::TryParse($fields[1], [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$size)) {
            throw 'Canonical inventory size exceeds Int64.'
        }
        Assert-M0NativeDigest $fields[2] 'Canonical inventory file SHA-256'
        if ($size -gt ([long]::MaxValue - $totalBytes)) { throw 'Canonical inventory total exceeds Int64.' }
        $totalBytes += $size
        $records.Add([pscustomobject]@{ relative_path = $path; size_bytes = $size; sha256 = $fields[2] })
        $previous = $path
    }
    return [pscustomobject]@{
        records = $records.ToArray()
        file_count = [long]$records.Count
        total_bytes = $totalBytes
        bytes = $bytes
        sha256 = Get-M0NativeSha256Bytes $bytes
    }
}

function Assert-M0NativeByteArraysEqual {
    param([Parameter(Mandatory)] [byte[]] $Actual, [Parameter(Mandatory)] [byte[]] $Expected, [string] $Context)
    if ($Actual.Length -ne $Expected.Length) { throw "$Context byte content differs." }
    for ($index = 0; $index -lt $Actual.Length; $index++) {
        if ($Actual[$index] -ne $Expected[$index]) { throw "$Context byte content differs." }
    }
}

function New-M0CanonicalTreeInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $InventoryPath
    )
    $treeRootFull = ConvertTo-M0NativeFullPath $TreeRoot
    $inventoryFull = [IO.Path]::GetFullPath($InventoryPath)
    if (Test-M0NativePathWithin $inventoryFull $treeRootFull -AllowEqual) {
        throw 'Canonical inventory must be stored outside the tree it describes.'
    }
    $data = Get-M0NativeTreeData $treeRootFull
    Write-M0NativeCreateNewFile -LiteralPath $inventoryFull -Bytes $data.bytes -Context 'Canonical inventory'
    return [pscustomobject]@{
        inventory_path = $inventoryFull
        file_count = $data.file_count
        total_bytes = $data.total_bytes
        sha256 = $data.sha256
    }
}

function Assert-M0TreeMatchesInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedFileCount,
        [Parameter(Mandatory)] [long] $ExpectedTotalBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256
    )
    Assert-M0NativeNonNegativeInt64 $ExpectedFileCount 'Expected inventory file count'
    Assert-M0NativeNonNegativeInt64 $ExpectedTotalBytes 'Expected inventory total bytes'
    Assert-M0NativeDigest $ExpectedInventorySha256 'Expected inventory SHA-256'
    $inventory = Read-M0NativeCanonicalInventory $InventoryPath
    if ($inventory.file_count -ne $ExpectedFileCount -or $inventory.total_bytes -ne $ExpectedTotalBytes -or
        $inventory.sha256 -cne $ExpectedInventorySha256) {
        throw 'Canonical inventory count, total bytes, or SHA-256 differs from its expected anchor.'
    }
    $tree = Get-M0NativeTreeData $TreeRoot
    if ($tree.file_count -ne $inventory.file_count -or $tree.total_bytes -ne $inventory.total_bytes -or
        $tree.sha256 -cne $inventory.sha256) {
        throw 'Physical tree does not match its canonical inventory summary.'
    }
    Assert-M0NativeByteArraysEqual $tree.bytes $inventory.bytes 'Physical tree inventory'
    return [pscustomobject]@{
        inventory_path = [IO.Path]::GetFullPath($InventoryPath)
        file_count = $inventory.file_count
        total_bytes = $inventory.total_bytes
        sha256 = $inventory.sha256
    }
}

function Assert-M0NativeGlobalPaxAttributes {
    param(
        [Parameter(Mandatory)] $Entry,
        [Parameter(Mandatory)] [Collections.IDictionary] $Expected
    )
    if ($Entry -isnot [System.Formats.Tar.PaxGlobalExtendedAttributesTarEntry] -or
        $Entry.Format -ne [System.Formats.Tar.TarEntryFormat]::Pax -or
        $Entry.EntryType -ne [System.Formats.Tar.TarEntryType]::GlobalExtendedAttributes) {
        throw 'Tar archive global PAX header has an unexpected type or format.'
    }
    $actual = $Entry.GlobalExtendedAttributes
    if ($actual.Count -ne $Expected.Count) { throw 'Tar archive global PAX attribute set drifted.' }
    foreach ($key in $Expected.Keys) {
        if ($key -isnot [string] -or $Expected[$key] -isnot [string] -or
            -not $actual.ContainsKey([string]$key) -or $actual[[string]$key] -cne [string]$Expected[$key]) {
            throw "Tar archive global PAX attribute '$key' drifted."
        }
    }
}

function ConvertFrom-M0NativeTarPath {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [bool] $IsDirectory,
        [Parameter(Mandatory)] [string] $ExpectedTopLevelDirectory
    )
    if ($Name.Contains('\\')) { throw 'Tar archive path contains a backslash separator.' }
    $normalized = $Name
    if ($IsDirectory) {
        if (-not $normalized.EndsWith('/', [StringComparison]::Ordinal)) {
            throw 'Tar archive directory path must end with one slash.'
        }
        $normalized = $normalized.Substring(0, $normalized.Length - 1)
    }
    elseif ($normalized.EndsWith('/', [StringComparison]::Ordinal)) {
        throw 'Tar archive file path must not end with a slash.'
    }
    Assert-M0NativeSafeRelativePath $normalized 'Tar archive entry path'
    if ($normalized -cne $ExpectedTopLevelDirectory -and
        -not $normalized.StartsWith($ExpectedTopLevelDirectory + '/', [StringComparison]::Ordinal)) {
        throw 'Tar archive entry escaped or drifted from the expected top-level directory.'
    }
    return $normalized
}

function Expand-M0AuthenticatedTarGz {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $DestinationRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedArchiveSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedArchiveSha256,
        [Parameter(Mandatory)] [string] $ExpectedTopLevelDirectory,
        [Parameter(Mandatory)] [Collections.IDictionary] $ExpectedGlobalPaxAttributes,
        [Parameter(Mandatory)] [long] $ExpectedArchiveEntryCount,
        [Parameter(Mandatory)] [long] $ExpectedDirectoryCount,
        [Parameter(Mandatory)] [long] $ExpectedFileCount,
        [Parameter(Mandatory)] [long] $ExpectedTotalFileBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256
    )
    Assert-M0NativeNonNegativeInt64 $ExpectedArchiveSizeBytes 'Expected archive size'
    Assert-M0NativeDigest $ExpectedArchiveSha256 'Expected archive SHA-256'
    Assert-M0NativeSafeRelativePath $ExpectedTopLevelDirectory 'Expected tar top-level directory'
    if ($ExpectedTopLevelDirectory.Contains('/')) { throw 'Expected tar top-level directory must be exactly one segment.' }
    foreach ($anchor in @(
        @($ExpectedArchiveEntryCount, 'Expected archive entry count'),
        @($ExpectedDirectoryCount, 'Expected archive directory count'),
        @($ExpectedFileCount, 'Expected archive file count'),
        @($ExpectedTotalFileBytes, 'Expected archive total file bytes')
    )) { Assert-M0NativeNonNegativeInt64 $anchor[0] $anchor[1] }
    Assert-M0NativeDigest $ExpectedInventorySha256 'Expected extracted inventory SHA-256'

    $destination = [IO.Path]::GetFullPath($DestinationRoot)
    $inventory = [IO.Path]::GetFullPath($InventoryPath)
    if ([IO.File]::Exists($destination) -or [IO.Directory]::Exists($destination)) {
        throw 'Tar extraction destination must not already exist.'
    }
    if (Test-M0NativePathWithin $inventory $destination -AllowEqual) {
        throw 'Extracted inventory must be stored outside the extracted tree.'
    }
    if ([IO.File]::Exists($inventory) -or [IO.Directory]::Exists($inventory)) {
        throw 'Extracted inventory destination must not already exist.'
    }
    Assert-M0NativeNoReparseExistingChain ([IO.Path]::GetDirectoryName($destination)) 'Tar extraction parent'

    $archive = Open-M0NativePhysicalFile `
        -LiteralPath $ArchivePath `
        -Context 'Authenticated tar.gz archive' `
        -ExpectedSizeBytes $ExpectedArchiveSizeBytes `
        -ExpectedSha256 $ExpectedArchiveSha256
    try {
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        Assert-M0NativeNoReparseExistingChain $destination 'Tar extraction destination'
        $entryCount = [long]0
        $directoryCount = [long]0
        $fileCount = [long]0
        $totalBytes = [long]0
        $globalPaxCount = 0
        $filesystemEntryCount = 0
        $declaredDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $seenOrdinal = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        $seenCaseFolded = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

        $archive.stream.Position = 0
        $gzip = [IO.Compression.GZipStream]::new($archive.stream, [IO.Compression.CompressionMode]::Decompress, $true)
        try {
            $reader = [System.Formats.Tar.TarReader]::new($gzip, $true)
            try {
                while ($null -ne ($entry = $reader.GetNextEntry($false))) {
                    $entryCount++
                    if ($entryCount -gt $ExpectedArchiveEntryCount) { throw 'Tar archive entry-count anchor was exceeded.' }
                    if ($entry.EntryType -eq [System.Formats.Tar.TarEntryType]::GlobalExtendedAttributes) {
                        $globalPaxCount++
                        if ($entryCount -ne 1 -or $globalPaxCount -ne 1) {
                            throw 'Tar archive global PAX header must be the sole first entry.'
                        }
                        Assert-M0NativeGlobalPaxAttributes $entry $ExpectedGlobalPaxAttributes
                        continue
                    }
                    if ($entry.Format -eq [System.Formats.Tar.TarEntryFormat]::Pax -or
                        $entry.EntryType -in @(
                            [System.Formats.Tar.TarEntryType]::ExtendedAttributes,
                            [System.Formats.Tar.TarEntryType]::SymbolicLink,
                            [System.Formats.Tar.TarEntryType]::HardLink
                        )) {
                        throw "Tar archive entry '$($entry.Name)' uses forbidden PAX metadata or link semantics."
                    }
                    if ($entry.Format -ne [System.Formats.Tar.TarEntryFormat]::Ustar) {
                        throw "Tar archive entry '$($entry.Name)' uses an unexpected tar format."
                    }
                    $isDirectory = $entry.EntryType -eq [System.Formats.Tar.TarEntryType]::Directory
                    $isFile = $entry.EntryType -in @(
                        [System.Formats.Tar.TarEntryType]::RegularFile,
                        [System.Formats.Tar.TarEntryType]::V7RegularFile
                    )
                    if (-not $isDirectory -and -not $isFile) {
                        throw "Tar archive entry '$($entry.Name)' has forbidden type '$($entry.EntryType)'."
                    }
                    $canonical = ConvertFrom-M0NativeTarPath $entry.Name $isDirectory $ExpectedTopLevelDirectory
                    if (-not $seenOrdinal.Add($canonical)) { throw "Tar archive contains duplicate path '$canonical'." }
                    if (-not $seenCaseFolded.Add($canonical)) { throw "Tar archive contains a case-folded path collision at '$canonical'." }
                    $filesystemEntryCount++
                    if ($filesystemEntryCount -eq 1 -and (-not $isDirectory -or $canonical -cne $ExpectedTopLevelDirectory)) {
                        throw 'Tar archive first filesystem entry must declare the exact top-level directory.'
                    }
                    $parentCanonical = [IO.Path]::GetDirectoryName($canonical.Replace('/', [IO.Path]::DirectorySeparatorChar))
                    if ($parentCanonical) { $parentCanonical = $parentCanonical.Replace([IO.Path]::DirectorySeparatorChar, '/') }
                    if ($canonical -cne $ExpectedTopLevelDirectory -and
                        ([string]::IsNullOrEmpty($parentCanonical) -or -not $declaredDirectories.Contains($parentCanonical))) {
                        throw "Tar archive entry '$canonical' appeared before its declared parent directory."
                    }
                    $relative = if ($canonical -ceq $ExpectedTopLevelDirectory) {
                        ''
                    }
                    else { $canonical.Substring($ExpectedTopLevelDirectory.Length + 1) }
                    $outputPath = if ([string]::IsNullOrEmpty($relative)) {
                        $destination
                    }
                    else { Join-Path $destination ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar)) }
                    if (-not (Test-M0NativePathWithin $outputPath $destination -AllowEqual)) {
                        throw "Tar archive entry '$canonical' escaped the extraction boundary."
                    }
                    if ($isDirectory) {
                        $directoryCount++
                        if ($directoryCount -gt $ExpectedDirectoryCount) { throw 'Tar archive directory-count anchor was exceeded.' }
                        $declaredDirectories.Add($canonical) | Out-Null
                        if (-not [string]::IsNullOrEmpty($relative)) {
                            [IO.Directory]::CreateDirectory($outputPath) | Out-Null
                            $created = Get-Item -LiteralPath $outputPath -Force
                            if ($created.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                                throw "Tar archive created a reparse-point directory at '$canonical'."
                            }
                        }
                        continue
                    }

                    $fileCount++
                    if ($fileCount -gt $ExpectedFileCount) { throw 'Tar archive file-count anchor was exceeded.' }
                    if ($entry.Length -lt 0 -or $entry.Length -gt ([long]::MaxValue - $totalBytes)) {
                        throw "Tar archive file '$canonical' has an invalid or overflowing length."
                    }
                    $totalBytes += $entry.Length
                    if ($totalBytes -gt $ExpectedTotalFileBytes) { throw 'Tar archive total-file-bytes anchor was exceeded.' }
                    if ($null -eq $entry.DataStream -and $entry.Length -ne 0) {
                        throw "Tar archive non-empty file '$canonical' has no data stream."
                    }
                    $parent = [IO.Path]::GetDirectoryName($outputPath)
                    if (-not [IO.Directory]::Exists($parent)) { throw "Tar archive file '$canonical' has no physical parent directory." }
                    $output = [IO.FileStream]::new($outputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
                    try {
                        if ($null -ne $entry.DataStream) { $entry.DataStream.CopyTo($output) }
                        $output.Flush($true)
                        if ($output.Length -ne $entry.Length) { throw "Tar archive file '$canonical' length drifted during extraction." }
                    }
                    finally { $output.Dispose() }
                }
            }
            finally { $reader.Dispose() }
        }
        finally { $gzip.Dispose() }

        if ($globalPaxCount -ne 1 -or $entryCount -ne $ExpectedArchiveEntryCount -or
            $directoryCount -ne $ExpectedDirectoryCount -or $fileCount -ne $ExpectedFileCount -or
            $totalBytes -ne $ExpectedTotalFileBytes) {
            throw 'Tar archive entry, directory, file, byte, or PAX anchor did not match exactly.'
        }
        $inventoryResult = New-M0CanonicalTreeInventory -TreeRoot $destination -InventoryPath $inventory
        if ($inventoryResult.file_count -ne $ExpectedFileCount -or
            $inventoryResult.total_bytes -ne $ExpectedTotalFileBytes -or
            $inventoryResult.sha256 -cne $ExpectedInventorySha256) {
            throw 'Extracted physical tree inventory differs from its expected anchors.'
        }
        return [pscustomobject]@{
            destination_root = $destination
            inventory_path = $inventory
            archive_entry_count = $entryCount
            directory_count = $directoryCount
            file_count = $inventoryResult.file_count
            total_bytes = $inventoryResult.total_bytes
            sha256 = $inventoryResult.sha256
        }
    }
    catch {
        $failure = $_
        try { Remove-M0NativeNewOutput -LiteralPath $inventory -Context 'Rejected extracted inventory' }
        finally { Remove-M0NativeNewOutput -LiteralPath $destination -Context 'Rejected extracted tree' }
        throw $failure
    }
    finally { $archive.stream.Dispose() }
}

function ConvertFrom-M0NativeStrictUtf8Lines {
    param([Parameter(Mandatory)] [byte[]] $Bytes, [Parameter(Mandatory)] [string] $Context)
    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        throw "$Context must not contain a UTF-8 BOM."
    }
    $text = $script:Utf8.GetString($Bytes)
    $normalized = $text.Replace("`r`n", "`n")
    if ($normalized.Contains("`r")) { throw "$Context contains a bare CR line ending." }
    if (-not $normalized.EndsWith("`n", [StringComparison]::Ordinal)) { throw "$Context must end with a newline." }
    return [string[]]$normalized.Substring(0, $normalized.Length - 1).Split("`n", [StringSplitOptions]::None)
}

function ConvertFrom-M0NativeUnifiedPatch {
    param(
        [Parameter(Mandatory)] [byte[]] $PatchBytes,
        [Parameter(Mandatory)] [byte[]] $SourceBytes,
        [Parameter(Mandatory)] [string] $ExpectedPatchTargetRelativePath
    )
    Assert-M0NativeSafeRelativePath $ExpectedPatchTargetRelativePath 'Expected patch target path'
    $patchLines = ConvertFrom-M0NativeStrictUtf8Lines $PatchBytes 'Patch file'
    $sourceLines = ConvertFrom-M0NativeStrictUtf8Lines $SourceBytes 'Patch target source'
    $diffIndices = @()
    for ($index = 0; $index -lt $patchLines.Count; $index++) {
        if ($patchLines[$index].StartsWith('diff --git ', [StringComparison]::Ordinal)) { $diffIndices += $index }
    }
    if ($diffIndices.Count -ne 1) { throw 'Patch must contain exactly one diff target.' }
    $cursor = $diffIndices[0]
    $match = [regex]::Match($patchLines[$cursor], '^diff --git a/(.+) b/(.+)$', [Text.RegularExpressions.RegexOptions]::CultureInvariant)
    if (-not $match.Success -or $match.Groups[1].Value -cne $ExpectedPatchTargetRelativePath -or
        $match.Groups[2].Value -cne $ExpectedPatchTargetRelativePath) {
        throw 'Patch diff target does not match the expected single target.'
    }
    $cursor++
    while ($cursor -lt $patchLines.Count -and -not $patchLines[$cursor].StartsWith('--- ', [StringComparison]::Ordinal)) {
        if ($patchLines[$cursor] -cnotmatch '^index [0-9a-f]+\.\.[0-9a-f]+( [0-7]{6})?$') {
            throw "Patch contains unsupported metadata before its target header: '$($patchLines[$cursor])'"
        }
        $cursor++
    }
    if ($cursor + 1 -ge $patchLines.Count -or
        $patchLines[$cursor] -cne "--- a/$ExpectedPatchTargetRelativePath" -or
        $patchLines[$cursor + 1] -cne "+++ b/$ExpectedPatchTargetRelativePath") {
        throw 'Patch old/new target headers are not exact.'
    }
    $cursor += 2
    $sourceIndex = 0
    $output = [Collections.Generic.List[string]]::new()
    $hunkCount = 0
    $removedCount = 0
    $addedCount = 0
    while ($cursor -lt $patchLines.Count) {
        $hunk = [regex]::Match($patchLines[$cursor], '^@@ -([0-9]+)(?:,([0-9]+))? \+([0-9]+)(?:,([0-9]+))? @@(?: .*)?$',
            [Text.RegularExpressions.RegexOptions]::CultureInvariant)
        if (-not $hunk.Success) { throw "Patch contains malformed or unsupported hunk metadata: '$($patchLines[$cursor])'" }
        $hunkCount++
        $oldStart = [int]::Parse($hunk.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
        $oldCount = if ($hunk.Groups[2].Success) { [int]::Parse($hunk.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture) } else { 1 }
        $newStart = [int]::Parse($hunk.Groups[3].Value, [Globalization.CultureInfo]::InvariantCulture)
        $newCount = if ($hunk.Groups[4].Success) { [int]::Parse($hunk.Groups[4].Value, [Globalization.CultureInfo]::InvariantCulture) } else { 1 }
        if ($oldStart -lt 1 -or $newStart -lt 1 -or $oldCount -lt 0 -or $newCount -lt 0) {
            throw 'Patch hunk range is invalid.'
        }
        $hunkSourceIndex = $oldStart - 1
        if ($hunkSourceIndex -lt $sourceIndex -or $hunkSourceIndex -gt $sourceLines.Count) {
            throw 'Patch hunk range overlaps or lies outside the source target.'
        }
        while ($sourceIndex -lt $hunkSourceIndex) { $output.Add($sourceLines[$sourceIndex]); $sourceIndex++ }
        if ($output.Count -ne ($newStart - 1)) {
            throw 'Patch hunk new-file coordinate does not match the produced target position.'
        }
        $cursor++
        $oldConsumed = 0
        $newProduced = 0
        while ($cursor -lt $patchLines.Count -and -not $patchLines[$cursor].StartsWith('@@ ', [StringComparison]::Ordinal)) {
            $line = $patchLines[$cursor]
            if ($line.Length -eq 0 -or $line[0] -notin @(' ', '-', '+')) {
                throw "Patch hunk contains an unsupported line: '$line'"
            }
            $content = $line.Substring(1)
            switch ($line[0]) {
                ' ' {
                    if ($sourceIndex -ge $sourceLines.Count -or $sourceLines[$sourceIndex] -cne $content) {
                        throw 'Patch hunk context does not match the source target.'
                    }
                    $output.Add($content)
                    $sourceIndex++
                    $oldConsumed++
                    $newProduced++
                }
                '-' {
                    if ($sourceIndex -ge $sourceLines.Count -or $sourceLines[$sourceIndex] -cne $content) {
                        throw 'Patch hunk removal does not match the source target.'
                    }
                    $sourceIndex++
                    $oldConsumed++
                    $removedCount++
                }
                '+' {
                    $output.Add($content)
                    $newProduced++
                    $addedCount++
                }
            }
            $cursor++
        }
        if ($oldConsumed -ne $oldCount -or $newProduced -ne $newCount) {
            throw 'Patch hunk line counts do not match its declared ranges.'
        }
    }
    if ($hunkCount -eq 0 -or $removedCount -eq 0 -or $addedCount -eq 0) {
        throw 'Patch must contain at least one changing hunk with removals and additions.'
    }
    while ($sourceIndex -lt $sourceLines.Count) { $output.Add($sourceLines[$sourceIndex]); $sourceIndex++ }
    $serialized = [string]::Join("`r`n", $output) + "`r`n"
    return $script:Utf8.GetBytes($serialized)
}

function Copy-M0NativeTreeFromData {
    param(
        [Parameter(Mandatory)] $SourceData,
        [Parameter(Mandatory)] [string] $DestinationTreeRoot
    )
    $destination = [IO.Path]::GetFullPath($DestinationTreeRoot)
    if ([IO.File]::Exists($destination) -or [IO.Directory]::Exists($destination)) {
        throw 'Patched tree destination must not already exist.'
    }
    Assert-M0NativeNoReparseExistingChain ([IO.Path]::GetDirectoryName($destination)) 'Patched tree parent'
    $ownsDestination = $false
    try {
        [IO.Directory]::CreateDirectory($destination) | Out-Null
        $ownsDestination = $true
        foreach ($relative in $SourceData.directories) {
            [IO.Directory]::CreateDirectory((Join-Path $destination ($relative.Replace('/', [IO.Path]::DirectorySeparatorChar)))) | Out-Null
        }
        foreach ($record in $SourceData.records) {
            $source = Open-M0NativePhysicalFile `
                -LiteralPath $record.physical_path `
                -Context "Source tree copy '$($record.relative_path)'" `
                -BoundaryRoot $SourceData.root `
                -ExpectedSizeBytes $record.size_bytes `
                -ExpectedSha256 $record.sha256 `
                -SkipReparseChain
            try {
                $outputPath = Join-Path $destination ($record.relative_path.Replace('/', [IO.Path]::DirectorySeparatorChar))
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null
                $output = [IO.FileStream]::new($outputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
                try {
                    $source.stream.Position = 0
                    $source.stream.CopyTo($output)
                    $output.Flush($true)
                    if ($output.Length -ne $record.size_bytes) { throw "Source tree copy '$($record.relative_path)' changed size." }
                }
                finally { $output.Dispose() }
            }
            finally { $source.stream.Dispose() }
        }
        return $destination
    }
    catch {
        $failure = $_
        if ($ownsDestination) {
            Remove-M0NativeNewOutput -LiteralPath $destination -Context 'Incomplete patched-tree copy'
        }
        throw $failure
    }
}

function Invoke-M0CopyAndApplySingleTargetPatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $SourceTreeRoot,
        [Parameter(Mandatory)] [string] $DestinationTreeRoot,
        [Parameter(Mandatory)] [string] $PatchPath,
        [Parameter(Mandatory)] [long] $ExpectedPatchSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedPatchSha256,
        [Parameter(Mandatory)] [string] $TargetRelativePath,
        [Parameter(Mandatory)] [string] $ExpectedPatchTargetRelativePath,
        [Parameter(Mandatory)] [string] $ExpectedBeforeSha256,
        [Parameter(Mandatory)] [string] $ExpectedAfterSha256
    )
    Assert-M0NativeNonNegativeInt64 $ExpectedPatchSizeBytes 'Expected patch size'
    Assert-M0NativeDigest $ExpectedPatchSha256 'Expected patch SHA-256'
    Assert-M0NativeSafeRelativePath $TargetRelativePath 'Patched tree target path'
    Assert-M0NativeSafeRelativePath $ExpectedPatchTargetRelativePath 'Patch-declared target path'
    Assert-M0NativeDigest $ExpectedBeforeSha256 'Expected patch target before SHA-256'
    Assert-M0NativeDigest $ExpectedAfterSha256 'Expected patch target after SHA-256'
    if ($ExpectedBeforeSha256 -ceq $ExpectedAfterSha256) { throw 'Patch before and after hashes must differ.' }

    $sourceData = Get-M0NativeTreeData $SourceTreeRoot
    $targetRecord = @($sourceData.records | Where-Object { $_.relative_path -ceq $TargetRelativePath })
    if ($targetRecord.Count -ne 1) { throw 'Patch target must identify exactly one physical source-tree file.' }
    if ($targetRecord[0].sha256 -cne $ExpectedBeforeSha256) { throw 'Patch target before SHA-256 differs from its expected identity.' }

    $patch = Open-M0NativePhysicalFile `
        -LiteralPath $PatchPath `
        -Context 'Authenticated single-target patch' `
        -ExpectedSizeBytes $ExpectedPatchSizeBytes `
        -ExpectedSha256 $ExpectedPatchSha256
    try {
        if ($patch.size_bytes -gt [int]::MaxValue) { throw 'Single-target patch is unexpectedly large.' }
        $patchBytes = [byte[]]::new([int]$patch.size_bytes)
        $patch.stream.Position = 0
        $offset = 0
        while ($offset -lt $patchBytes.Length) {
            $read = $patch.stream.Read($patchBytes, $offset, $patchBytes.Length - $offset)
            if ($read -eq 0) { throw 'Single-target patch ended unexpectedly.' }
            $offset += $read
        }
    }
    finally { $patch.stream.Dispose() }

    $target = Open-M0NativePhysicalFile `
        -LiteralPath $targetRecord[0].physical_path `
        -Context 'Patch source target' `
        -BoundaryRoot $sourceData.root `
        -ExpectedSizeBytes $targetRecord[0].size_bytes `
        -ExpectedSha256 $ExpectedBeforeSha256 `
        -SkipReparseChain
    try {
        if ($target.size_bytes -gt [int]::MaxValue) { throw 'Patch target is unexpectedly large.' }
        $sourceBytes = [byte[]]::new([int]$target.size_bytes)
        $target.stream.Position = 0
        $offset = 0
        while ($offset -lt $sourceBytes.Length) {
            $read = $target.stream.Read($sourceBytes, $offset, $sourceBytes.Length - $offset)
            if ($read -eq 0) { throw 'Patch source target ended unexpectedly.' }
            $offset += $read
        }
    }
    finally { $target.stream.Dispose() }

    $patchedBytes = ConvertFrom-M0NativeUnifiedPatch `
        -PatchBytes $patchBytes `
        -SourceBytes $sourceBytes `
        -ExpectedPatchTargetRelativePath $ExpectedPatchTargetRelativePath
    $actualAfterSha256 = Get-M0NativeSha256Bytes $patchedBytes
    if ($actualAfterSha256 -cne $ExpectedAfterSha256) {
        throw 'Patch target after SHA-256 differs after deliberate whole-file CRLF serialization.'
    }

    $destination = [IO.Path]::GetFullPath($DestinationTreeRoot)
    $ownsDestination = $false
    try {
        $destination = Copy-M0NativeTreeFromData $sourceData $destination
        $ownsDestination = $true
        $destinationTarget = Join-Path $destination ($TargetRelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
        $output = [IO.FileStream]::new($destinationTarget, [IO.FileMode]::Truncate, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        try {
            $output.Write($patchedBytes, 0, $patchedBytes.Length)
            $output.Flush($true)
        }
        finally { $output.Dispose() }

        $sourceAfterCopy = Get-M0NativeTreeData $SourceTreeRoot
        Assert-M0NativeByteArraysEqual $sourceAfterCopy.bytes $sourceData.bytes 'Source tree after isolated copy'
        $destinationData = Get-M0NativeTreeData $destination
        $sourceByPath = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::Ordinal)
        foreach ($record in $sourceData.records) { $sourceByPath.Add($record.relative_path, $record) }
        $changed = [Collections.Generic.List[string]]::new()
        if ($destinationData.file_count -ne $sourceData.file_count) { throw 'Patched tree file count differs from the source tree.' }
        foreach ($record in $destinationData.records) {
            if (-not $sourceByPath.ContainsKey($record.relative_path)) { throw "Patched tree added unexpected path '$($record.relative_path)'." }
            $before = $sourceByPath[$record.relative_path]
            if ($before.size_bytes -ne $record.size_bytes -or $before.sha256 -cne $record.sha256) { $changed.Add($record.relative_path) }
        }
        if ($changed.Count -ne 1 -or $changed[0] -cne $TargetRelativePath) {
            throw 'Patch must change exactly its declared single target in the copied tree.'
        }
        $patchedRecord = @($destinationData.records | Where-Object { $_.relative_path -ceq $TargetRelativePath })
        if ($patchedRecord.Count -ne 1 -or $patchedRecord[0].sha256 -cne $ExpectedAfterSha256) {
            throw 'Patched tree target does not retain its expected after identity.'
        }
        return [pscustomobject]@{
            changed_relative_paths = [string[]]@($changed)
            target_relative_path = $TargetRelativePath
            target_before_sha256 = $ExpectedBeforeSha256
            target_after_sha256 = $ExpectedAfterSha256
            line_endings = 'CRLF'
        }
    }
    catch {
        $failure = $_
        if ($ownsDestination) {
            Remove-M0NativeNewOutput -LiteralPath $destination -Context 'Rejected patched tree'
        }
        throw $failure
    }
}

function Assert-M0NativeExactProperties {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $Context
    )
    $actual = @($Value.PSObject.Properties.Name)
    if ($actual.Count -ne $Expected.Count) { throw "$Context has an unexpected property set." }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($actual[$index] -cne $Expected[$index]) { throw "$Context property order or field set drifted." }
    }
}

function Resolve-M0NativeRunPath {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0NativeSafeRelativePath $RelativePath $Context
    $root = ConvertTo-M0NativeFullPath $RunRoot
    $candidate = [IO.Path]::GetFullPath((Join-Path $root ($RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))))
    if (-not (Test-M0NativePathWithin $candidate $root)) { throw "$Context escaped the isolated run root." }
    return $candidate
}

function New-M0NativeFileReference {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-M0NativePathWithin $full $RunRoot)) { throw "$Context is outside the isolated run root." }
    $lease = Open-M0NativePhysicalFile -LiteralPath $full -Context $Context -BoundaryRoot $RunRoot
    try {
        return [pscustomobject][ordered]@{
            relative_path = [IO.Path]::GetRelativePath($RunRoot, $full).Replace('\', '/')
            size_bytes = [long]$lease.size_bytes
            sha256 = $lease.sha256
        }
    }
    finally { $lease.stream.Dispose() }
}

function New-M0NativeTreeObservation {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $RootRelativePath,
        [Parameter(Mandatory)] [string] $InventoryRelativePath,
        [Parameter(Mandatory)] $Summary
    )
    $inventoryPath = Resolve-M0NativeRunPath $RunRoot $InventoryRelativePath "Tree '$Id' inventory"
    return [pscustomobject][ordered]@{
        id = $Id
        root_relative_path = $RootRelativePath
        file_count = [long]$Summary.file_count
        total_size_bytes = [long]$Summary.total_bytes
        inventory = New-M0NativeFileReference $RunRoot $inventoryPath "Tree '$Id' inventory"
    }
}

function Get-M0NativeProcessEnvironment {
    $result = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
        $result[[string]$entry.Key] = [string]$entry.Value
    }
    return $result
}

function Copy-M0NativeEnvironment {
    param([Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Environment)
    $copy = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Environment.GetEnumerator()) { $copy[$entry.Key] = $entry.Value }
    return $copy
}

function Resolve-M0NativeArguments {
    param(
        [Parameter(Mandatory)] $Arguments,
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Bindings,
        [Parameter(Mandatory)] [string] $Context
    )
    $resolved = [Collections.Generic.List[string]]::new()
    foreach ($argument in @($Arguments)) {
        if ($argument -isnot [string]) { throw "$Context contains a non-string argument." }
        $matches = [regex]::Matches($argument, '\$\{([A-Z_]+)\}', [Text.RegularExpressions.RegexOptions]::CultureInvariant)
        $value = $argument
        foreach ($match in $matches) {
            $name = $match.Groups[1].Value
            if (-not $Bindings.ContainsKey($name) -or $name -cnotin $script:NativeTokenNames) {
                throw "$Context contains unknown closed token '$name'."
            }
            $value = $value.Replace($match.Value, $Bindings[$name])
        }
        if ($value.Contains('${', [StringComparison]::Ordinal)) { throw "$Context contains a malformed or unresolved token." }
        $resolved.Add($value)
    }
    return [string[]]$resolved.ToArray()
}

function ConvertTo-M0NativeCmdArgument {
    param([Parameter(Mandatory)] [string] $Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -match '["%!^&|<>\r\n]' -or $Value.Contains([char]0)) {
        throw "$Context contains a command-shell metacharacter that is not accepted."
    }
    return '"' + $Value + '"'
}

function Wait-M0NativeTaskWithin {
    param(
        [Parameter(Mandatory)] [Threading.Tasks.Task] $Task,
        [Parameter(Mandatory)] [int] $TimeoutMilliseconds
    )
    if ($TimeoutMilliseconds -le 0) { throw 'Native task deadline must be a positive number of milliseconds.' }
    if ($Task.IsCompleted) { return $true }
    $deadline = [Threading.Tasks.Task]::Delay($TimeoutMilliseconds)
    $completed = [Threading.Tasks.Task]::WhenAny($Task, $deadline).GetAwaiter().GetResult()
    return [object]::ReferenceEquals($completed, $Task)
}

function Stop-M0NativeProcessTree {
    param(
        [Parameter(Mandatory)] $Process,
        [Parameter(Mandatory)] [Threading.Tasks.Task] $WaitTask,
        [Parameter(Mandatory)] [int] $TimeoutMilliseconds
    )
    if ($WaitTask.IsCompleted) {
        $null = $WaitTask.GetAwaiter().GetResult()
        return
    }
    try { $Process.Kill($true) }
    catch {
        if ($WaitTask.IsCompleted) {
            $null = $WaitTask.GetAwaiter().GetResult()
            return
        }
        throw "Native process tree termination failed: $($_.Exception.Message)"
    }
    if (-not (Wait-M0NativeTaskWithin -Task $WaitTask -TimeoutMilliseconds $TimeoutMilliseconds)) {
        throw "Native process tree did not terminate within the $TimeoutMilliseconds ms teardown deadline."
    }
    $null = $WaitTask.GetAwaiter().GetResult()
}

function Invoke-M0NativeBoundedProcess {
    param([Parameter(Mandatory)] $Request)
    Assert-M0NativeExactProperties $Request @(
        'id', 'file_path', 'arguments', 'logical_arguments', 'resolved_arguments', 'working_directory', 'environment',
        'stdout_path', 'stderr_path', 'timeout_seconds'
    ) 'Native process request'
    # TEST-FIXTURE-INSERTION-POINT: PROCESS

    if (-not [IO.File]::Exists($Request.file_path)) { throw "Native process executable is missing: $($Request.file_path)" }
    if (-not [IO.Directory]::Exists($Request.working_directory)) { throw "Native process working directory is missing: $($Request.working_directory)" }
    if (($Request.timeout_seconds -isnot [int] -and $Request.timeout_seconds -isnot [long]) -or
        [long]$Request.timeout_seconds -le 0) {
        throw 'Native process timeout must be a positive integer number of seconds.'
    }
    foreach ($path in @($Request.stdout_path, $Request.stderr_path)) {
        if ([IO.File]::Exists($path) -or [IO.Directory]::Exists($path)) { throw "Native process output must be CreateNew: $path" }
        $parent = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($path))
        Assert-M0NativeNoReparseExistingChain $parent 'Native process output parent'
        [IO.Directory]::CreateDirectory($parent) | Out-Null
        Assert-M0NativeNoReparseExistingChain $parent 'Native process output parent'
    }

    $stdout = $null
    $stderr = $null
    $process = $null
    try {
        $stdout = [IO.FileStream]::new($Request.stdout_path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $stderr = [IO.FileStream]::new($Request.stderr_path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $process = [Diagnostics.Process]::new()
        $startInfo = [Diagnostics.ProcessStartInfo]::new()
        $startInfo.FileName = [IO.Path]::GetFullPath($Request.file_path)
        $startInfo.WorkingDirectory = [IO.Path]::GetFullPath($Request.working_directory)
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $processArguments = @($Request.arguments)
        $isCmdCommand = [IO.Path]::GetFileName($startInfo.FileName) -ieq 'cmd.exe' -and
            $processArguments.Count -eq 4 -and $processArguments[0] -ceq '/d' -and
            $processArguments[1] -ceq '/s' -and $processArguments[2] -ceq '/c'
        if ($isCmdCommand) {
            $commandLine = [string]$processArguments[3]
            if ($commandLine.Contains("`r") -or $commandLine.Contains("`n") -or $commandLine.Contains([char]0)) {
                throw 'Native cmd command contains an unsafe line break or NUL.'
            }
            # cmd.exe does not implement CommandLineToArgvW quote escaping. Supplying its command
            # through ArgumentList turns embedded quotes into literal backslashes on Windows.
            $startInfo.Arguments = '/d /s /c "' + $commandLine + '"'
        }
        else {
            foreach ($argument in $processArguments) { $startInfo.ArgumentList.Add([string]$argument) }
        }
        $startInfo.Environment.Clear()
        foreach ($entry in $Request.environment.GetEnumerator()) { $startInfo.Environment[$entry.Key] = $entry.Value }
        $process.StartInfo = $startInfo
        if (-not $process.Start()) { throw "Native process '$($Request.id)' did not start." }
        $stdoutTask = $process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $stderrTask = $process.StandardError.BaseStream.CopyToAsync($stderr)
        $waitTask = $process.WaitForExitAsync()
        $delayTask = [Threading.Tasks.Task]::Delay([TimeSpan]::FromSeconds([long]$Request.timeout_seconds))
        $completed = [Threading.Tasks.Task]::WhenAny($waitTask, $delayTask).GetAwaiter().GetResult()
        $timedOut = [object]::ReferenceEquals($completed, $delayTask)
        if ($timedOut) {
            Stop-M0NativeProcessTree -Process $process -WaitTask $waitTask -TimeoutMilliseconds 5000
        }
        else { $null = $waitTask.GetAwaiter().GetResult() }
        $pipeTask = [Threading.Tasks.Task]::WhenAll([Threading.Tasks.Task[]]@($stdoutTask, $stderrTask))
        if (-not (Wait-M0NativeTaskWithin -Task $pipeTask -TimeoutMilliseconds 30000)) {
            throw 'Native process output streams did not close within the 30000 ms teardown deadline.'
        }
        $null = $pipeTask.GetAwaiter().GetResult()
        $stdout.Flush($true)
        $stderr.Flush($true)
        $exitCode = if ($timedOut) { [long]-1 } else { [long]$process.ExitCode }
        return [pscustomobject]@{ exit_code = $exitCode; timed_out = $timedOut }
    }
    finally {
        if ($null -ne $process) { $process.Dispose() }
        if ($null -ne $stdout) { $stdout.Dispose() }
        if ($null -ne $stderr) { $stderr.Dispose() }
    }
}

function Read-M0NativeLogTail {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if (-not [IO.File]::Exists($LiteralPath)) { return '' }
    $stream = [IO.FileStream]::new(
        $LiteralPath,
        [IO.FileMode]::Open,
        [IO.FileAccess]::Read,
        [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $byteCount = [int][Math]::Min([long]8192, $stream.Length)
        $bytes = [byte[]]::new($byteCount)
        $stream.Position = $stream.Length - $byteCount
        $offset = 0
        while ($offset -lt $byteCount) {
            $read = $stream.Read($bytes, $offset, $byteCount - $offset)
            if ($read -eq 0) { break }
            $offset += $read
        }
        return [Text.Encoding]::UTF8.GetString($bytes, 0, $offset)
    }
    finally { $stream.Dispose() }
}

function Get-M0NativeResolvedExecutable {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Environment
    )
    if (-not $Environment.ContainsKey('PATH')) { return $null }
    foreach ($entry in $Environment['PATH'].Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $directory = $entry.Trim().Trim('"')
        if ([string]::IsNullOrWhiteSpace($directory)) { continue }
        $candidate = Join-Path $directory "$Name.exe"
        if ([IO.File]::Exists($candidate)) { return [IO.Path]::GetFullPath($candidate) }
    }
    return $null
}

function Assert-M0NativeBuildEnvironment {
    param(
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Environment,
        [Parameter(Mandatory)] $Context
    )
    foreach ($binding in @(
            @('VSCMD_ARG_HOST_ARCH', 'x64'),
            @('VSCMD_ARG_TGT_ARCH', 'x64')
        )) {
        if (-not $Environment.ContainsKey($binding[0]) -or $Environment[$binding[0]] -cne $binding[1]) {
            throw "Captured toolchain environment '$($binding[0])' must be '$($binding[1])'."
        }
    }
    if (-not $Environment.ContainsKey('VCToolsVersion') -or $Environment['VCToolsVersion'] -cnotmatch '^14\.44\.[0-9]+$') {
        throw 'Captured toolchain environment VCToolsVersion must be the pinned 14.44 family.'
    }
    if (-not $Environment.ContainsKey('WindowsSDKVersion') -or
        $Environment['WindowsSDKVersion'].TrimEnd('\') -cne '10.0.26100.0') {
        throw 'Captured toolchain environment WindowsSDKVersion must be 10.0.26100.0.'
    }
    foreach ($name in @($Context.contract.environment.forbidden_variables)) {
        if ($Environment.ContainsKey([string]$name) -and -not [string]::IsNullOrEmpty($Environment[[string]$name])) {
            throw "Captured toolchain environment contains forbidden variable '$name'."
        }
    }

    if (-not $Environment.ContainsKey('PATH') -or [string]::IsNullOrWhiteSpace($Environment['PATH'])) {
        throw 'Captured toolchain environment did not contain PATH.'
    }
    $cmakeDirectory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Context.tool_paths.cmake))
    $pathEntries = [Collections.Generic.List[string]]::new()
    $pathEntries.Add($cmakeDirectory)
    foreach ($entry in $Environment['PATH'].Split(';', [StringSplitOptions]::RemoveEmptyEntries)) {
        $normalized = $entry.Trim().Trim('"')
        if (-not [string]::IsNullOrWhiteSpace($normalized) -and
            -not [string]::Equals([IO.Path]::GetFullPath($normalized), $cmakeDirectory, [StringComparison]::OrdinalIgnoreCase)) {
            $pathEntries.Add($normalized)
        }
    }
    $Environment['PATH'] = [string]::Join(';', $pathEntries)
    if (-not [string]::Equals($Environment['PATH'].Split(';')[0], $cmakeDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Captured toolchain environment did not place the pinned CMake bin first in PATH.'
    }

    foreach ($toolName in @('cl', 'link', 'rc', 'mt')) {
        $actual = Get-M0NativeResolvedExecutable $toolName $Environment
        $expected = [IO.Path]::GetFullPath([string]$Context.tool_paths.PSObject.Properties[$toolName].Value)
        if ($null -eq $actual -or -not [string]::Equals($actual, $expected, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Captured toolchain environment resolves '$toolName' outside its pinned physical path."
        }
    }
    $clPath = [IO.Path]::GetFullPath([string]$Context.tool_paths.cl)
    $linkPath = [IO.Path]::GetFullPath([string]$Context.tool_paths.link)
    if ($clPath -notmatch '(?i)[\\/]Hostx64[\\/]x64[\\/]cl\.exe$' -or
        $linkPath -notmatch '(?i)[\\/]Hostx64[\\/]x64[\\/]link\.exe$') {
        throw 'Pinned MSVC compiler/linker do not use the x64 host and x64 target layout.'
    }
    $expectedVCToolsRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($clPath)) '../../..')).TrimEnd('\')
    if (-not $Environment.ContainsKey('VCToolsInstallDir') -or
        -not [string]::Equals($Environment['VCToolsInstallDir'].TrimEnd('\'), $expectedVCToolsRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Captured toolchain environment VCToolsInstallDir differs from the pinned 14.44 tool root.'
    }
    foreach ($toolName in @('rc', 'mt')) {
        $path = [IO.Path]::GetFullPath([string]$Context.tool_paths.PSObject.Properties[$toolName].Value)
        if ($path -notmatch '(?i)[\\/]10\.0\.26100\.0[\\/]x64[\\/][^\\/]+\.exe$') {
            throw "Pinned Windows SDK tool '$toolName' is outside SDK 10.0.26100.0 x64."
        }
    }
    $expectedSdkRoot = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName(
                [IO.Path]::GetFullPath([string]$Context.tool_paths.rc))) '../../..')).TrimEnd('\')
    if (-not $Environment.ContainsKey('WindowsSdkDir') -or
        -not [string]::Equals($Environment['WindowsSdkDir'].TrimEnd('\'), $expectedSdkRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Captured toolchain environment WindowsSdkDir differs from the pinned 10.0.26100.0 SDK root.'
    }
}

function Invoke-M0NativeCaptureBuildEnvironment {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $CallerEnvironment
    )
    $childBase = Copy-M0NativeEnvironment $CallerEnvironment
    foreach ($name in @($Context.contract.environment.forbidden_variables)) { $childBase.Remove([string]$name) | Out-Null }
    foreach ($name in @($childBase.Keys)) {
        if ($name -match '^(?i:VSCMD_|VCTools|VSINSTALLDIR$|VCINSTALLDIR$|WindowsSDK|WindowsSdkDir$|INCLUDE$|LIB$|LIBPATH$|UCRTVersion$|UniversalCRTSdkDir$)') {
            $childBase.Remove($name) | Out-Null
        }
    }
    $childBase['VSCMD_SKIP_SENDTELEMETRY'] = '1'
    $toolchainDirectory = Resolve-M0NativeRunPath $Context.run_root 'evidence/toolchain' 'Toolchain evidence directory'
    [IO.Directory]::CreateDirectory($toolchainDirectory) | Out-Null
    $stdoutPath = Join-Path $toolchainDirectory 'vsdevcmd.environment.stdout.tmp'
    $stderrPath = Join-Path $toolchainDirectory 'vsdevcmd.environment.stderr.tmp'
    $vsdevcmd = ConvertTo-M0NativeCmdArgument ([IO.Path]::GetFullPath($Context.tool_paths.vsdevcmd)) 'VsDevCmd path'
    $commandLine = "call $vsdevcmd -arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.26100.0 >nul && set"
    $request = [pscustomobject][ordered]@{
        id = 'vsdevcmd-environment'
        file_path = [IO.Path]::GetFullPath($Context.tool_paths.cmd)
        arguments = @('/d', '/s', '/c', $commandLine)
        logical_arguments = @('-arch=x64', '-host_arch=x64', '-vcvars_ver=14.44', '-winsdk=10.0.26100.0')
        resolved_arguments = @('-arch=x64', '-host_arch=x64', '-vcvars_ver=14.44', '-winsdk=10.0.26100.0')
        working_directory = [IO.Path]::GetFullPath($Context.run_root)
        environment = $childBase
        stdout_path = $stdoutPath
        stderr_path = $stderrPath
        timeout_seconds = [long]120
    }
    $result = Invoke-M0NativeBoundedProcess $request
    if ($result.timed_out) { throw 'VsDevCmd toolchain environment capture timed out.' }
    if ($result.exit_code -ne 0) {
        throw "VsDevCmd toolchain environment capture failed with exit code $($result.exit_code): $(Read-M0NativeLogTail $stderrPath)"
    }
    $environment = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    try {
        $oemEncoding = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
        foreach ($line in [IO.File]::ReadAllLines($stdoutPath, $oemEncoding)) {
            if ([string]::IsNullOrEmpty($line) -or $line.StartsWith('=', [StringComparison]::Ordinal)) { continue }
            $separator = $line.IndexOf('=')
            if ($separator -le 0) { throw "VsDevCmd emitted a non-environment line: $line" }
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            if ($environment.ContainsKey($name)) { throw "VsDevCmd emitted duplicate environment variable '$name'." }
            $environment.Add($name, $value)
        }
        Assert-M0NativeBuildEnvironment $environment $Context
        return $environment
    }
    finally {
        if ([IO.File]::Exists($stdoutPath)) { [IO.File]::Delete($stdoutPath) }
        if ([IO.File]::Exists($stderrPath)) { [IO.File]::Delete($stderrPath) }
    }
}

function Assert-M0NativePinnedTreeSummary {
    param([Parameter(Mandatory)] $Actual, [Parameter(Mandatory)] $Expected, [Parameter(Mandatory)] [string] $Context)
    if ([long]$Actual.file_count -ne [long]$Expected.file_count -or
        [long]$Actual.total_bytes -ne [long]$Expected.total_size_bytes -or
        $Actual.sha256 -cne $Expected.inventory_sha256) {
        throw "$Context fingerprint differs from the schema-v2 contract anchor."
    }
}

function Invoke-M0NativePrepareSources {
    param([Parameter(Mandatory)] $Context)
    # TEST-FIXTURE-INSERTION-POINT: PREPARATION
    $contract = $Context.contract
    $runRoot = [IO.Path]::GetFullPath($Context.run_root)
    $qtBeforeSpec = $contract.trees[0]
    $qtPatchedSpec = $contract.trees[1]
    $vulkanSpec = $contract.trees[2]

    $qtBeforeRoot = Resolve-M0NativeRunPath $runRoot $qtBeforeSpec.root_relative_path 'Qtbase before root'
    $qtBeforeInventory = Resolve-M0NativeRunPath $runRoot $qtBeforeSpec.inventory_relative_path 'Qtbase before inventory'
    $qtSummary = Expand-M0AuthenticatedTarGz `
        -ArchivePath $Context.paths.qtbase_source `
        -DestinationRoot $qtBeforeRoot `
        -InventoryPath $qtBeforeInventory `
        -ExpectedArchiveSizeBytes ([long]$contract.inputs.qtbase_source.size_bytes) `
        -ExpectedArchiveSha256 $contract.inputs.qtbase_source.sha256 `
        -ExpectedTopLevelDirectory "qtbase-$($contract.inputs.qtbase_source.commit_sha)" `
        -ExpectedGlobalPaxAttributes ([ordered]@{ comment = $contract.inputs.qtbase_source.commit_sha }) `
        -ExpectedArchiveEntryCount 26851 `
        -ExpectedDirectoryCount 3994 `
        -ExpectedFileCount ([long]$qtBeforeSpec.expected.file_count) `
        -ExpectedTotalFileBytes ([long]$qtBeforeSpec.expected.total_size_bytes) `
        -ExpectedInventorySha256 $qtBeforeSpec.expected.inventory_sha256
    Assert-M0NativePinnedTreeSummary $qtSummary $qtBeforeSpec.expected 'Qtbase before tree'

    $patchTargetPrefix = 'src/3rdparty/freetype/'
    $targetRelativePath = [string]$contract.inputs.freetype_patch.target_relative_path
    if (-not $targetRelativePath.StartsWith($patchTargetPrefix, [StringComparison]::Ordinal)) {
        throw 'FreeType patch target is outside the expected vendored FreeType subtree.'
    }
    $qtPatchedRoot = Resolve-M0NativeRunPath $runRoot $qtPatchedSpec.root_relative_path 'Qtbase patched root'
    $patch = Invoke-M0CopyAndApplySingleTargetPatch `
        -SourceTreeRoot $qtBeforeRoot `
        -DestinationTreeRoot $qtPatchedRoot `
        -PatchPath $Context.paths.freetype_patch `
        -ExpectedPatchSizeBytes ([long]$contract.inputs.freetype_patch.size_bytes) `
        -ExpectedPatchSha256 $contract.inputs.freetype_patch.sha256 `
        -TargetRelativePath $targetRelativePath `
        -ExpectedPatchTargetRelativePath $targetRelativePath.Substring($patchTargetPrefix.Length) `
        -ExpectedBeforeSha256 $contract.inputs.freetype_patch.target_before_sha256 `
        -ExpectedAfterSha256 $contract.inputs.freetype_patch.target_after_sha256
    $qtPatchedInventory = Resolve-M0NativeRunPath $runRoot $qtPatchedSpec.inventory_relative_path 'Qtbase patched inventory'
    $qtPatchedSummary = New-M0CanonicalTreeInventory -TreeRoot $qtPatchedRoot -InventoryPath $qtPatchedInventory
    Assert-M0NativePinnedTreeSummary $qtPatchedSummary $qtPatchedSpec.expected 'Qtbase patched tree'

    $vulkanRoot = Resolve-M0NativeRunPath $runRoot $vulkanSpec.root_relative_path 'Vulkan root'
    $vulkanInventory = Resolve-M0NativeRunPath $runRoot $vulkanSpec.inventory_relative_path 'Vulkan inventory'
    $vulkanSummary = Expand-M0AuthenticatedTarGz `
        -ArchivePath $Context.paths.vulkan_headers `
        -DestinationRoot $vulkanRoot `
        -InventoryPath $vulkanInventory `
        -ExpectedArchiveSizeBytes ([long]$contract.inputs.vulkan_headers.size_bytes) `
        -ExpectedArchiveSha256 $contract.inputs.vulkan_headers.sha256 `
        -ExpectedTopLevelDirectory "Vulkan-Headers-$($contract.inputs.vulkan_headers.commit_sha)" `
        -ExpectedGlobalPaxAttributes ([ordered]@{ comment = $contract.inputs.vulkan_headers.commit_sha }) `
        -ExpectedArchiveEntryCount 61 `
        -ExpectedDirectoryCount 7 `
        -ExpectedFileCount ([long]$vulkanSpec.expected.file_count) `
        -ExpectedTotalFileBytes ([long]$vulkanSpec.expected.total_size_bytes) `
        -ExpectedInventorySha256 $vulkanSpec.expected.inventory_sha256
    Assert-M0NativePinnedTreeSummary $vulkanSummary $vulkanSpec.expected 'Vulkan tree'
    foreach ($pin in @(
            @($contract.inputs.vulkan_headers.header_relative_path, $contract.inputs.vulkan_headers.header_sha256),
            @($contract.inputs.vulkan_headers.license_relative_path, $contract.inputs.vulkan_headers.license_sha256)
        )) {
        $path = Resolve-M0NativeRunPath $vulkanRoot $pin[0] 'Pinned Vulkan extracted leaf'
        $lease = Open-M0NativePhysicalFile -LiteralPath $path -Context 'Pinned Vulkan extracted leaf' -BoundaryRoot $vulkanRoot -ExpectedSha256 $pin[1]
        $lease.stream.Dispose()
    }

    return [pscustomobject]@{
        patch = [pscustomobject][ordered]@{
            changed_relative_paths = @($patch.changed_relative_paths)
            target_relative_path = $patch.target_relative_path
            target_before_sha256 = $patch.target_before_sha256
            target_after_sha256 = $patch.target_after_sha256
        }
        trees = @(
            (New-M0NativeTreeObservation $runRoot 'qtbase-before' $qtBeforeSpec.root_relative_path $qtBeforeSpec.inventory_relative_path $qtSummary),
            (New-M0NativeTreeObservation $runRoot 'qtbase-patched' $qtPatchedSpec.root_relative_path $qtPatchedSpec.inventory_relative_path $qtPatchedSummary),
            (New-M0NativeTreeObservation $runRoot 'vulkan' $vulkanSpec.root_relative_path $vulkanSpec.inventory_relative_path $vulkanSummary)
        )
    }
}

function Assert-M0NativeStringSequence {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $Context
    )
    $items = @($Actual)
    if ($items.Count -ne $Expected.Count) { throw "$Context sequence length drifted." }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($items[$index] -isnot [string] -or $items[$index] -cne $Expected[$index]) {
            throw "$Context sequence drifted at index $index."
        }
    }
}

function Assert-M0NativeRequiredToolPath {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] [string] $ToolId
    )
    $property = $Context.tool_paths.PSObject.Properties[$ToolId]
    if ($null -eq $property -or $property.Value -isnot [string] -or [string]::IsNullOrWhiteSpace($property.Value)) {
        throw "Native context is missing pinned tool path '$ToolId'."
    }
    $path = [IO.Path]::GetFullPath($property.Value)
    if (-not [IO.File]::Exists($path)) { throw "Pinned tool '$ToolId' is missing: $path" }
    Assert-M0NativeNoReparseExistingChain $path "Pinned tool '$ToolId'"
    return $path
}

function Assert-M0NativeContextAndResolveArguments {
    param([Parameter(Mandatory)] $Context)
    Assert-M0NativeExactProperties $Context @(
        'contract_sha256', 'contract', 'run_root', 'paths', 'tool_paths', 'recipe_paths'
    ) 'Native runner context'
    Assert-M0NativeDigest $Context.contract_sha256 'Native runner contract SHA-256'
    if ($Context.contract.schema_version -isnot [long] -or $Context.contract.schema_version -ne 2) {
        throw 'Native runner requires schema-v2 contract input.'
    }
    if ($Context.contract.recipe.runner_export -cne 'Invoke-M0QtBaseRefreshNative') {
        throw 'Native runner export binding drifted from the schema-v2 contract.'
    }
    $runRoot = ConvertTo-M0NativeFullPath $Context.run_root
    if (-not [IO.Directory]::Exists($runRoot)) { throw 'Native runner RunRoot must already exist.' }
    Assert-M0NativeNoReparseExistingChain $runRoot 'Native runner RunRoot'

    Assert-M0NativeStringSequence @($Context.contract.trees | ForEach-Object id) $script:NativeTreeIds 'Contract tree ids'
    Assert-M0NativeStringSequence @($Context.contract.trees | ForEach-Object root_relative_path) $script:NativeTreeRoots 'Contract tree roots'
    Assert-M0NativeStringSequence @($Context.contract.commands | ForEach-Object id) $script:NativeCommandIds 'Contract command ids'
    Assert-M0NativeStringSequence @($Context.contract.reports | ForEach-Object id) $script:NativeReportIds 'Contract report ids'

    $expectedCommands = @(
        @('cmd', 'source/qtbase-patched/configure.bat', 'configuration.arguments', 'b', 'evidence/logs/configure.stdout.log', 'evidence/logs/configure.stderr.log'),
        @('cmake', $null, 'configuration.build_arguments', 'b', 'evidence/logs/build.stdout.log', 'evidence/logs/build.stderr.log'),
        @('cmake', $null, 'configuration.install_arguments', 'b', 'evidence/logs/install.stdout.log', 'evidence/logs/install.stderr.log')
    )
    for ($index = 0; $index -lt $expectedCommands.Count; $index++) {
        $actual = $Context.contract.commands[$index]
        $expected = $expectedCommands[$index]
        if ($actual.tool_id -cne $expected[0] -or
            -not [object]::Equals($actual.script_relative_path, $expected[1]) -or
            $actual.arguments_binding -cne $expected[2] -or
            $actual.working_directory_relative_path -cne $expected[3] -or
            $actual.stdout_relative_path -cne $expected[4] -or
            $actual.stderr_relative_path -cne $expected[5]) {
            throw "Contract command '$($script:NativeCommandIds[$index])' drifted from the closed native recipe."
        }
    }

    $expectedReports = @(
        @('runner', 'copy-verbatim', 'b/config.summary', 'evidence/reports/configure-summary.txt'),
        @('runner', 'copy-verbatim', 'b/CMakeCache.txt', 'evidence/reports/cmake-cache.txt'),
        @('runner', 'analyze-freetype-link-graph', 'b/CMakeFiles/impl-RelWithDebInfo.ninja', 'evidence/reports/qtgui-freetype-dependency.txt'),
        @('dumpbin', '/nologo|/headers', 'install/bin/Qt6Gui.dll', 'evidence/reports/qt6gui-headers.txt'),
        @('dumpbin', '/nologo|/imports', 'install/bin/Qt6Gui.dll', 'evidence/reports/qt6gui-imports.txt'),
        @('dumpbin', '/nologo|/exports', 'install/bin/Qt6Gui.dll', 'evidence/reports/qt6gui-exports.txt'),
        @('dumpbin', '/nologo|/imports', 'install/plugins/platforms/qminimal.dll', 'evidence/reports/qminimal-imports.txt')
    )
    for ($index = 0; $index -lt $expectedReports.Count; $index++) {
        $actual = $Context.contract.reports[$index]
        $expected = $expectedReports[$index]
        if ($actual.producer_id -cne $expected[0] -or
            (@($actual.arguments) -join '|') -cne $expected[1] -or
            $actual.subject_relative_path -cne $expected[2] -or
            $actual.relative_path -cne $expected[3]) {
            throw "Contract report '$($script:NativeReportIds[$index])' drifted from the closed native recipe."
        }
    }
    if ($Context.contract.configuration.generator -cne 'Ninja Multi-Config' -or
        $Context.contract.configuration.installed_configuration -cne 'RelWithDebInfo') {
        throw 'Native runner requires the pinned Ninja Multi-Config RelWithDebInfo recipe.'
    }

    $resolvedTools = [ordered]@{}
    foreach ($toolId in @('cmd', 'vsdevcmd', 'cmake', 'ninja', 'cl', 'link', 'rc', 'mt', 'dumpbin')) {
        $resolvedTools[$toolId] = Assert-M0NativeRequiredToolPath $Context $toolId
    }
    if ($resolvedTools.dumpbin -notmatch '(?i)[\\/]Hostx64[\\/]x64[\\/]dumpbin\.exe$') {
        throw 'Pinned dumpbin does not use the x64 host and x64 target layout.'
    }

    $bindings = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $bindings.Add('INSTALL_ROOT', (Resolve-M0NativeRunPath $runRoot 'install' 'Install token root').Replace('\', '/'))
    $bindings.Add('BUILD_ROOT', (Resolve-M0NativeRunPath $runRoot 'b' 'Build token root').Replace('\', '/'))
    $bindings.Add('VULKAN_INCLUDE', (Resolve-M0NativeRunPath $runRoot 'source/vulkan/include' 'Vulkan include token root').Replace('\', '/'))
    $bindings.Add('NINJA', $resolvedTools.ninja.Replace('\', '/'))
    $bindings.Add('CL', $resolvedTools.cl.Replace('\', '/'))
    $bindings.Add('RC', $resolvedTools.rc.Replace('\', '/'))
    $bindings.Add('MT', $resolvedTools.mt.Replace('\', '/'))
    $configureArguments = Resolve-M0NativeArguments $Context.contract.configuration.arguments $bindings 'Configure arguments'
    $buildArguments = Resolve-M0NativeArguments $Context.contract.configuration.build_arguments $bindings 'Build arguments'
    $installArguments = Resolve-M0NativeArguments $Context.contract.configuration.install_arguments $bindings 'Install arguments'

    return [pscustomobject]@{
        run_root = $runRoot
        tools = [pscustomobject]$resolvedTools
        bindings = $bindings
        arguments = [pscustomobject]@{
            configure = $configureArguments
            build = $buildArguments
            install = $installArguments
        }
    }
}

function Assert-M0NativePreparedObservation {
    param([Parameter(Mandatory)] $Prepared, [Parameter(Mandatory)] $Contract)
    Assert-M0NativeExactProperties $Prepared @('patch', 'trees') 'Native source preparation result'
    Assert-M0NativeExactProperties $Prepared.patch @(
        'changed_relative_paths', 'target_relative_path', 'target_before_sha256', 'target_after_sha256'
    ) 'Native source preparation patch'
    Assert-M0NativeStringSequence $Prepared.patch.changed_relative_paths @(
        [string]$Contract.inputs.freetype_patch.target_relative_path
    ) 'Native source preparation changed paths'
    if ($Prepared.patch.target_relative_path -cne $Contract.inputs.freetype_patch.target_relative_path -or
        $Prepared.patch.target_before_sha256 -cne $Contract.inputs.freetype_patch.target_before_sha256 -or
        $Prepared.patch.target_after_sha256 -cne $Contract.inputs.freetype_patch.target_after_sha256) {
        throw 'Native source preparation patch observation drifted from the contract.'
    }
    $trees = @($Prepared.trees)
    if ($trees.Count -ne 3) { throw 'Native source preparation must return exactly three source trees.' }
    Assert-M0NativeStringSequence @($trees | ForEach-Object id) @('qtbase-before', 'qtbase-patched', 'vulkan') 'Prepared source tree ids'
}

function Get-M0NativeCommandArguments {
    param([Parameter(Mandatory)] [string] $Binding, [Parameter(Mandatory)] $Contract)
    switch ($Binding) {
        'configuration.arguments' { return [string[]]@($Contract.configuration.arguments) }
        'configuration.build_arguments' { return [string[]]@($Contract.configuration.build_arguments) }
        'configuration.install_arguments' { return [string[]]@($Contract.configuration.install_arguments) }
        default { throw "Unknown native command argument binding '$Binding'." }
    }
}

function Test-M0NativeStreamContainsAscii {
    param(
        [Parameter(Mandatory)] [IO.Stream] $Stream,
        [Parameter(Mandatory)] [string] $Marker
    )

    $needle = [Text.Encoding]::ASCII.GetBytes($Marker)
    if ($needle.Length -eq 0) { throw 'Native log marker must not be empty.' }
    $buffer = [byte[]]::new(65536 + $needle.Length - 1)
    $carry = 0
    $Stream.Position = 0
    while ($true) {
        $read = $Stream.Read($buffer, $carry, 65536)
        $total = $carry + $read
        for ($offset = 0; $offset -le $total - $needle.Length; $offset++) {
            $matched = $true
            for ($index = 0; $index -lt $needle.Length; $index++) {
                if ($buffer[$offset + $index] -ne $needle[$index]) {
                    $matched = $false
                    break
                }
            }
            if ($matched) { return $true }
        }
        if ($read -eq 0) { return $false }
        $carry = [Math]::Min($needle.Length - 1, $total)
        if ($carry -gt 0) {
            [Buffer]::BlockCopy($buffer, $total - $carry, $buffer, 0, $carry)
        }
    }
}

function Assert-M0NativeConfigurePathBudget {
    param(
        [Parameter(Mandatory)] [string] $StdoutPath,
        [Parameter(Mandatory)] [string] $StderrPath,
        [Parameter(Mandatory)] [string] $RunRoot
    )

    foreach ($path in @($StdoutPath, $StderrPath)) {
        $lease = Open-M0NativePhysicalFile -LiteralPath $path -Context 'Configure path-budget log' -BoundaryRoot $RunRoot
        try {
            foreach ($marker in @('CMAKE_OBJECT_PATH_MAX', 'cannot be safely placed under this directory')) {
                if (Test-M0NativeStreamContainsAscii -Stream $lease.stream -Marker $marker) {
                    throw 'Configure reported an unsafe native object path (CMAKE_OBJECT_PATH_MAX); build is forbidden.'
                }
            }
        }
        finally { $lease.stream.Dispose() }
    }
}

function Invoke-M0NativeContractCommands {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Resolved,
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Environment
    )
    $runRoot = $Resolved.run_root
    $buildRoot = Resolve-M0NativeRunPath $runRoot 'b' 'Native build root'
    if ([IO.Directory]::Exists($buildRoot) -or [IO.File]::Exists($buildRoot)) {
        throw 'Native build root must be CreateNew.'
    }
    [IO.Directory]::CreateDirectory($buildRoot) | Out-Null
    Assert-M0NativeNoReparseExistingChain $buildRoot 'Native build root'

    $configureScript = Resolve-M0NativeRunPath $runRoot 'source/qtbase-patched/configure.bat' 'Configure script'
    $scriptLease = Open-M0NativePhysicalFile -LiteralPath $configureScript -Context 'Configure script' -BoundaryRoot $runRoot
    $scriptLease.stream.Dispose()
    $observations = [Collections.Generic.List[object]]::new()
    foreach ($spec in @($Context.contract.commands)) {
        $logicalArguments = Get-M0NativeCommandArguments $spec.arguments_binding $Context.contract
        $resolvedArguments = [string[]]$Resolved.arguments.PSObject.Properties[$spec.id].Value
        $workingDirectory = Resolve-M0NativeRunPath $runRoot $spec.working_directory_relative_path "Command '$($spec.id)' working directory"
        $stdoutPath = Resolve-M0NativeRunPath $runRoot $spec.stdout_relative_path "Command '$($spec.id)' stdout"
        $stderrPath = Resolve-M0NativeRunPath $runRoot $spec.stderr_relative_path "Command '$($spec.id)' stderr"
        if ($spec.id -ceq 'configure') {
            $parts = [Collections.Generic.List[string]]::new()
            $parts.Add('call')
            $parts.Add((ConvertTo-M0NativeCmdArgument $configureScript 'Configure script'))
            foreach ($argument in $resolvedArguments) {
                $parts.Add((ConvertTo-M0NativeCmdArgument $argument 'Resolved configure argument'))
            }
            $filePath = $Resolved.tools.cmd
            $processArguments = @('/d', '/s', '/c', ([string]::Join(' ', $parts)))
        }
        else {
            $filePath = $Resolved.tools.cmake
            $processArguments = $resolvedArguments
        }
        $timeout = switch ($spec.id) {
            'configure' { [long]1800; break }
            'build' { [long]14400; break }
            'install' { [long]1800; break }
            default { throw "Unexpected native command '$($spec.id)'." }
        }
        $request = [pscustomobject][ordered]@{
            id = $spec.id
            file_path = $filePath
            arguments = [string[]]$processArguments
            logical_arguments = [string[]]$logicalArguments
            resolved_arguments = [string[]]$resolvedArguments
            working_directory = $workingDirectory
            environment = Copy-M0NativeEnvironment $Environment
            stdout_path = $stdoutPath
            stderr_path = $stderrPath
            timeout_seconds = $timeout
        }
        $result = Invoke-M0NativeBoundedProcess $request
        if ($result.timed_out) { throw "Native command '$($spec.id)' timed out." }
        if ($result.exit_code -ne 0) {
            throw "Native command '$($spec.id)' failed with exit code $($result.exit_code): $(Read-M0NativeLogTail $stderrPath)"
        }
        if ($spec.id -ceq 'configure') {
            Assert-M0NativeConfigurePathBudget -StdoutPath $stdoutPath -StderrPath $stderrPath -RunRoot $runRoot
        }
        $observations.Add([pscustomobject][ordered]@{
            id = $spec.id
            tool_id = $spec.tool_id
            script_relative_path = $spec.script_relative_path
            arguments = [string[]]$logicalArguments
            working_directory_relative_path = $spec.working_directory_relative_path
            exit_code = [long]0
            stdout = New-M0NativeFileReference $runRoot $stdoutPath "Command '$($spec.id)' stdout"
            stderr = New-M0NativeFileReference $runRoot $stderrPath "Command '$($spec.id)' stderr"
        })
    }
    return $observations.ToArray()
}

function Copy-M0NativeVerbatimReport {
    param(
        [Parameter(Mandatory)] [string] $SubjectPath,
        [Parameter(Mandatory)] [string] $ReportPath,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $lease = Open-M0NativePhysicalFile -LiteralPath $SubjectPath -Context "$Context subject" -BoundaryRoot $RunRoot
    try {
        if ($lease.size_bytes -gt [int]::MaxValue) { throw "$Context subject is unexpectedly large." }
        $bytes = [byte[]]::new([int]$lease.size_bytes)
        $lease.stream.Position = 0
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $lease.stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw "$Context subject ended unexpectedly." }
            $offset += $read
        }
        Write-M0NativeCreateNewFile -LiteralPath $ReportPath -Bytes $bytes -Context $Context
    }
    finally { $lease.stream.Dispose() }
}

function Remove-M0NativeNinjaComment {
    param([Parameter(Mandatory)] [AllowEmptyString()] [string] $Text)
    $builder = [Text.StringBuilder]::new()
    for ($index = 0; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($character -eq '$' -and $index + 1 -lt $Text.Length) {
            $null = $builder.Append($character).Append($Text[$index + 1])
            $index++
            continue
        }
        if ($character -eq '#') { break }
        $null = $builder.Append($character)
    }
    return $builder.ToString().TrimEnd()
}

function Split-M0NativeNinjaTokens {
    param([Parameter(Mandatory)] [AllowEmptyString()] [string] $Text)
    $tokens = [Collections.Generic.List[string]]::new()
    $current = [Text.StringBuilder]::new()
    for ($index = 0; $index -lt $Text.Length; $index++) {
        $character = $Text[$index]
        if ($character -eq '$' -and $index + 1 -lt $Text.Length) {
            $null = $current.Append($character).Append($Text[$index + 1])
            $index++
            continue
        }
        if ([char]::IsWhiteSpace($character)) {
            if ($current.Length -gt 0) {
                $tokens.Add($current.ToString())
                $null = $current.Clear()
            }
            continue
        }
        $null = $current.Append($character)
    }
    if ($current.Length -gt 0) { $tokens.Add($current.ToString()) }
    return $tokens.ToArray()
}

function ConvertFrom-M0NativeNinjaToken {
    param([Parameter(Mandatory)] [string] $Token)
    return $Token.Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$').Replace('\', '/')
}

function Test-M0NativeNinjaDependencyEdge {
    param(
        [Parameter(Mandatory)] [string] $Text,
        [Parameter(Mandatory)] [string] $Target,
        [Parameter(Mandatory)] [string] $Rule,
        [Parameter(Mandatory)] [string] $Dependency
    )
    $normalized = $Text.Replace("`r`n", "`n")
    if ($normalized.Contains("`r")) { return $false }
    $logicalText = [Text.RegularExpressions.Regex]::Replace($normalized, '\$\n[ \t]*', '')
    $lines = @($logicalText.Split("`n", [StringSplitOptions]::RemoveEmptyEntries))
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        $line = Remove-M0NativeNinjaComment $lines[$lineIndex]
        if (-not $line.StartsWith('build ', [StringComparison]::Ordinal)) { continue }
        $separator = -1
        for ($index = 6; $index -lt $line.Length; $index++) {
            if ($line[$index] -eq ':' -and ($index -eq 0 -or $line[$index - 1] -ne '$')) {
                $separator = $index
                break
            }
        }
        if ($separator -lt 0) { continue }
        $outputs = @(Split-M0NativeNinjaTokens $line.Substring(6, $separator - 6) |
                ForEach-Object { ConvertFrom-M0NativeNinjaToken $_ })
        $edgeTokens = @(Split-M0NativeNinjaTokens $line.Substring($separator + 1))
        if ($edgeTokens.Count -lt 1) { continue }
        $actualRule = ConvertFrom-M0NativeNinjaToken $edgeTokens[0]
        $dependencies = @($edgeTokens | Select-Object -Skip 1 |
                ForEach-Object { ConvertFrom-M0NativeNinjaToken $_ })
        if (@($outputs | Where-Object { $_ -ceq $Target }).Count -eq 0 -or $actualRule -cne $Rule -or
            @($dependencies | Where-Object { $_ -ceq $Dependency }).Count -eq 0) {
            continue
        }

        for ($bindingIndex = $lineIndex + 1; $bindingIndex -lt $lines.Count; $bindingIndex++) {
            $bindingLine = $lines[$bindingIndex]
            if ($bindingLine.StartsWith('build ', [StringComparison]::Ordinal)) { break }
            if (-not $bindingLine.StartsWith(' ', [StringComparison]::Ordinal) -and
                -not $bindingLine.StartsWith("`t", [StringComparison]::Ordinal)) {
                break
            }
            $trimmed = (Remove-M0NativeNinjaComment $bindingLine).TrimStart()
            if (-not $trimmed.StartsWith('LINK_LIBRARIES =', [StringComparison]::Ordinal)) { continue }
            $linkTokens = @(Split-M0NativeNinjaTokens $trimmed.Substring('LINK_LIBRARIES ='.Length) |
                    ForEach-Object { ConvertFrom-M0NativeNinjaToken $_ })
            return @($linkTokens | Where-Object { $_ -ceq $Dependency }).Count -gt 0
        }
    }
    return $false
}

function Write-M0NativeFreetypeDependencyReport {
    param(
        [Parameter(Mandatory)] [string] $SubjectPath,
        [Parameter(Mandatory)] [string] $ReportPath,
        [Parameter(Mandatory)] [string] $RunRoot
    )
    $lease = Open-M0NativePhysicalFile -LiteralPath $SubjectPath -Context 'FreeType Ninja graph subject' -BoundaryRoot $RunRoot
    try {
        if ($lease.size_bytes -gt [int]::MaxValue) { throw 'FreeType Ninja graph subject is unexpectedly large.' }
        $bytes = [byte[]]::new([int]$lease.size_bytes)
        $lease.stream.Position = 0
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $lease.stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'FreeType Ninja graph subject ended unexpectedly.' }
            $offset += $read
        }
    }
    finally { $lease.stream.Dispose() }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf) {
        throw 'FreeType Ninja graph subject must not contain a UTF-8 BOM.'
    }
    $text = $script:Utf8.GetString($bytes)
    $requiredEdges = @(
        [pscustomobject]@{
            target = 'bin/Qt6Gui.dll'
            rule = 'CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo'
        },
        [pscustomobject]@{
            target = 'plugins/platforms/qminimal.dll'
            rule = 'CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo'
        }
    )
    foreach ($edge in $requiredEdges) {
        if (-not (Test-M0NativeNinjaDependencyEdge `
                    -Text $text `
                    -Target $edge.target `
                    -Rule $edge.rule `
                    -Dependency 'lib/Qt6BundledFreetype.lib')) {
            throw "FreeType Ninja graph does not prove the exact edge '$($edge.target)' with rule '$($edge.rule)' and associated LINK_LIBRARIES token 'lib/Qt6BundledFreetype.lib'."
        }
    }
    $reportBytes = $script:Utf8.GetBytes(
        "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`n" +
        "plugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n")
    Write-M0NativeCreateNewFile -LiteralPath $ReportPath -Bytes $reportBytes -Context 'FreeType dependency report'
}

function ConvertTo-M0NativeCanonicalProcessText {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -eq 0) { throw "$Context produced empty output." }
    $encoding = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
    $text = $encoding.GetString($bytes).Replace("`r`n", "`n").Replace("`r", "`n")
    $text = $text.TrimEnd([char[]]@("`n"))
    if ([string]::IsNullOrWhiteSpace($text)) { throw "$Context produced no semantic text." }
    return $script:Utf8.GetBytes($text + "`n")
}

function New-M0NativeReportObservation {
    param(
        [Parameter(Mandatory)] $Spec,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $SubjectPath,
        [Parameter(Mandatory)] [string] $ReportPath
    )
    return [pscustomobject][ordered]@{
        id = $Spec.id
        producer_id = $Spec.producer_id
        arguments = [string[]]@($Spec.arguments)
        subject = New-M0NativeFileReference $RunRoot $SubjectPath "Report '$($Spec.id)' subject"
        exit_code = [long]0
        file = New-M0NativeFileReference $RunRoot $ReportPath "Report '$($Spec.id)' file"
    }
}

function Invoke-M0NativeContractReports {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $Resolved,
        [Parameter(Mandatory)] [Collections.Generic.IDictionary[string,string]] $Environment
    )
    $runRoot = $Resolved.run_root
    $observations = [Collections.Generic.List[object]]::new()
    foreach ($spec in @($Context.contract.reports)) {
        $subjectPath = Resolve-M0NativeRunPath $runRoot $spec.subject_relative_path "Report '$($spec.id)' subject"
        $reportPath = Resolve-M0NativeRunPath $runRoot $spec.relative_path "Report '$($spec.id)' file"
        if ([IO.File]::Exists($reportPath) -or [IO.Directory]::Exists($reportPath)) {
            throw "Report '$($spec.id)' file must be CreateNew: $reportPath"
        }
        switch ($spec.id) {
            'configure-summary' {
                Copy-M0NativeVerbatimReport $subjectPath $reportPath $runRoot "Report '$($spec.id)'"
                break
            }
            'cmake-cache' {
                Copy-M0NativeVerbatimReport $subjectPath $reportPath $runRoot "Report '$($spec.id)'"
                break
            }
            'qtgui-freetype-dependency' {
                Write-M0NativeFreetypeDependencyReport $subjectPath $reportPath $runRoot
                break
            }
            default {
                if ($spec.producer_id -cne 'dumpbin') { throw "Unexpected report producer for '$($spec.id)'." }
                $rawStdout = $reportPath + '.raw.stdout.tmp'
                $rawStderr = $reportPath + '.raw.stderr.tmp'
                foreach ($rawPath in @($rawStdout, $rawStderr)) {
                    if ([IO.File]::Exists($rawPath) -or [IO.Directory]::Exists($rawPath)) {
                        throw "Report '$($spec.id)' temporary output must be CreateNew: $rawPath"
                    }
                }
                try {
                    $actualArguments = [Collections.Generic.List[string]]::new()
                    foreach ($argument in @($spec.arguments)) { $actualArguments.Add([string]$argument) }
                    $actualArguments.Add($subjectPath)
                    $request = [pscustomobject][ordered]@{
                        id = "report:$($spec.id)"
                        file_path = $Resolved.tools.dumpbin
                        arguments = [string[]]$actualArguments.ToArray()
                        logical_arguments = [string[]]@($spec.arguments)
                        resolved_arguments = [string[]]$actualArguments.ToArray()
                        working_directory = Resolve-M0NativeRunPath $runRoot 'b' "Report '$($spec.id)' working directory"
                        environment = Copy-M0NativeEnvironment $Environment
                        stdout_path = $rawStdout
                        stderr_path = $rawStderr
                        timeout_seconds = [long]120
                    }
                    $result = Invoke-M0NativeBoundedProcess $request
                    if ($result.timed_out) { throw "Report '$($spec.id)' timed out." }
                    if ($result.exit_code -ne 0) {
                        throw "Report '$($spec.id)' failed with exit code $($result.exit_code): $(Read-M0NativeLogTail $rawStderr)"
                    }
                    $canonicalBytes = ConvertTo-M0NativeCanonicalProcessText $rawStdout "Report '$($spec.id)'"
                    Write-M0NativeCreateNewFile -LiteralPath $reportPath -Bytes $canonicalBytes -Context "Report '$($spec.id)'"
                }
                finally {
                    if ([IO.File]::Exists($rawStdout)) { [IO.File]::Delete($rawStdout) }
                    if ([IO.File]::Exists($rawStderr)) { [IO.File]::Delete($rawStderr) }
                }
                break
            }
        }
        $observations.Add((New-M0NativeReportObservation $spec $runRoot $subjectPath $reportPath))
    }
    return $observations.ToArray()
}

function Assert-M0NativePeX64 {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $RunRoot)
    $lease = Open-M0NativePhysicalFile -LiteralPath $LiteralPath -Context 'Installed PE artifact' -BoundaryRoot $RunRoot
    try {
        if ($lease.size_bytes -lt 134) { throw 'Installed PE artifact is too small.' }
        $header = [byte[]]::new([int][Math]::Min([long]4096, $lease.size_bytes))
        $lease.stream.Position = 0
        $read = $lease.stream.Read($header, 0, $header.Length)
        if ($read -ne $header.Length -or $header[0] -ne 0x4d -or $header[1] -ne 0x5a) {
            throw 'Installed artifact is not an MZ executable.'
        }
        $peOffset = [BitConverter]::ToInt32($header, 0x3c)
        if ($peOffset -lt 0 -or $peOffset + 6 -gt $header.Length -or
            $header[$peOffset] -ne 0x50 -or $header[$peOffset + 1] -ne 0x45 -or
            $header[$peOffset + 2] -ne 0 -or $header[$peOffset + 3] -ne 0 -or
            [BitConverter]::ToUInt16($header, $peOffset + 4) -ne 0x8664) {
            throw 'Installed artifact is not a PE x64 binary.'
        }
    }
    finally { $lease.stream.Dispose() }
}

function New-M0NativeInstallEvidence {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)] [string] $RunRoot)
    $treeSpec = $Context.contract.trees[3]
    $installRoot = Resolve-M0NativeRunPath $RunRoot $treeSpec.root_relative_path 'Install tree root'
    if (-not [IO.Directory]::Exists($installRoot)) { throw 'Install command did not create the install tree.' }
    $inventoryPath = Resolve-M0NativeRunPath $RunRoot $treeSpec.inventory_relative_path 'Install inventory'
    $summary = New-M0CanonicalTreeInventory -TreeRoot $installRoot -InventoryPath $inventoryPath
    $artifacts = [Collections.Generic.List[object]]::new()
    foreach ($relativePath in @($Context.contract.configuration.expected_artifacts)) {
        Assert-M0NativeSafeRelativePath ([string]$relativePath) 'Expected installed artifact path'
        $path = Resolve-M0NativeRunPath $installRoot ([string]$relativePath) 'Expected installed artifact'
        Assert-M0NativePeX64 $path $RunRoot
        $artifacts.Add((New-M0NativeFileReference $RunRoot $path 'Expected installed artifact'))
    }
    $consumerPath = Resolve-M0NativeRunPath $installRoot 'plugins/platforms/qminimal.dll' 'Direct qminimal consumer'
    Assert-M0NativePeX64 $consumerPath $RunRoot
    return [pscustomobject]@{
        tree = New-M0NativeTreeObservation $RunRoot 'install' $treeSpec.root_relative_path $treeSpec.inventory_relative_path $summary
        artifacts = $artifacts.ToArray()
        direct_consumers = @((New-M0NativeFileReference $RunRoot $consumerPath 'Direct qminimal consumer'))
    }
}

function Invoke-M0QtBaseRefreshNative {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Context)
    if ($null -eq $Context) { throw 'Native Qtbase refresh context is required.' }
    $callerEnvironment = Get-M0NativeProcessEnvironment
    foreach ($name in @($Context.contract.environment.forbidden_variables)) {
        if ($callerEnvironment.ContainsKey([string]$name) -and
            -not [string]::IsNullOrEmpty($callerEnvironment[[string]$name])) {
            throw "Forbidden build environment injection '$name' is present."
        }
    }
    $resolved = Assert-M0NativeContextAndResolveArguments $Context
    $prepared = Invoke-M0NativePrepareSources $Context
    Assert-M0NativePreparedObservation $prepared $Context.contract
    $environment = Invoke-M0NativeCaptureBuildEnvironment $Context $callerEnvironment
    $commands = Invoke-M0NativeContractCommands $Context $resolved $environment
    Convert-M0NativeInstallHardlinksToCopies `
        -InstallRoot (Resolve-M0NativeRunPath $resolved.run_root 'install' 'Installed tree') `
        -RunRoot $resolved.run_root
    $reports = Invoke-M0NativeContractReports $Context $resolved $environment
    $install = New-M0NativeInstallEvidence $Context $resolved.run_root
    return [pscustomobject][ordered]@{
        execution = [pscustomobject][ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
        patch = $prepared.patch
        configuration = [pscustomobject][ordered]@{
            generator = $Context.contract.configuration.generator
            arguments = [string[]]@($Context.contract.configuration.arguments)
            build_arguments = [string[]]@($Context.contract.configuration.build_arguments)
            install_arguments = [string[]]@($Context.contract.configuration.install_arguments)
            installed_configuration = $Context.contract.configuration.installed_configuration
            artifacts = [string[]]@($Context.contract.configuration.expected_artifacts)
            features = $Context.contract.configuration.expected_features
        }
        evidence = [pscustomobject][ordered]@{
            trees = @($prepared.trees) + @($install.tree)
            commands = [object[]]$commands
            reports = [object[]]$reports
            artifacts = [object[]]$install.artifacts
            direct_consumers = [object[]]$install.direct_consumers
        }
    }
}

Export-ModuleMember -Function 'Invoke-M0QtBaseRefreshNative'
