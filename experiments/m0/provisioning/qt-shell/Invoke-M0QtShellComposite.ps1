#Requires -Version 7.0

[CmdletBinding(DefaultParameterSetName = 'Compose')]
param(
    [Parameter(Mandatory)]
    [string] $ContractPath,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedContractSha256,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedQtBaseReceiptSha256,

    [Parameter(Mandatory)]
    [string] $RepositoryRoot,

    [Parameter(Mandatory, ParameterSetName = 'Compose')]
    [string] $RunRoot,

    [Parameter(Mandatory, ParameterSetName = 'Verify')]
    [string] $ReceiptPath,

    [Parameter(Mandatory, ParameterSetName = 'Verify')]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedReceiptSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $IsWindows) {
    throw 'The Qt shell composite production launcher is proven only on Windows.'
}

$script:ExpectedRecipePaths = [ordered]@{
    orchestrator = 'experiments/m0/provisioning/qt-shell/M0.QtShellComposite.psm1'
    runner = 'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1'
    entrypoint = 'experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1'
}

$nativeNamespace = 'M0QtShellCompositeBootstrap_' + [Guid]::NewGuid().ToString('N')
$nativeSource = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace __NAMESPACE__ {
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
    public struct WIN32_FIND_STREAM_DATA {
        public long StreamSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)]
        public string StreamName;
    }

    public static class BootstrapFile {
        private const uint GENERIC_READ = 0x80000000;
        private const uint FILE_LIST_DIRECTORY = 0x00000001;
        private const uint FILE_READ_ATTRIBUTES = 0x00000080;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        private const uint FILE_FLAG_SEQUENTIAL_SCAN = 0x08000000;
        private const int ERROR_HANDLE_EOF = 38;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes,
            IntPtr templateFile);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr FindFirstStreamW(string path, int infoLevel,
            out WIN32_FIND_STREAM_DATA data, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextStreamW(IntPtr findHandle, out WIN32_FIND_STREAM_DATA data);

        [DllImport("kernel32.dll")]
        private static extern bool FindClose(IntPtr findHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle, out BY_HANDLE_FILE_INFORMATION information);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle, StringBuilder path, uint pathLength, uint flags);

        public static SafeFileHandle OpenFileLease(string path) {
            var handle = CreateFileW(path, GENERIC_READ | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryLease(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static string[] GetStreamNames(string path) {
            var names = new List<string>();
            WIN32_FIND_STREAM_DATA data;
            IntPtr handle = FindFirstStreamW(path, 0, out data, 0);
            if (handle == new IntPtr(-1)) {
                int error = Marshal.GetLastWin32Error();
                if (error == ERROR_HANDLE_EOF) { return names.ToArray(); }
                throw new Win32Exception(error);
            }
            try {
                names.Add(data.StreamName);
                while (FindNextStreamW(handle, out data)) { names.Add(data.StreamName); }
                int error = Marshal.GetLastWin32Error();
                if (error != ERROR_HANDLE_EOF) { throw new Win32Exception(error); }
                return names.ToArray();
            }
            finally { FindClose(handle); }
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
                information.FileIndexHigh.ToString("X8") + information.FileIndexLow.ToString("X8");
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return buffer.ToString();
        }
    }
}
'@
$compiledTypes = @(Add-Type -TypeDefinition $nativeSource.Replace('__NAMESPACE__', $nativeNamespace) -PassThru)
$bootstrapTypes = @($compiledTypes | Where-Object Name -CEQ 'BootstrapFile')
if ($bootstrapTypes.Count -ne 1) {
    throw 'The composite bootstrap native helper did not compile to exactly one BootstrapFile type.'
}
$script:BootstrapFile = $bootstrapTypes[0]

function ConvertTo-NormalizedDirectoryPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $full = [IO.Path]::GetFullPath($LiteralPath)
    $root = [IO.Path]::GetPathRoot($full)
    if ([string]::Equals($full, $root, [StringComparison]::OrdinalIgnoreCase)) { return $root }
    return [IO.Path]::TrimEndingDirectorySeparator($full)
}

function ConvertTo-ExtendedPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $full.Substring(2) }
    return '\\?\' + $full
}

function ConvertFrom-FinalPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)

    if ($LiteralPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $LiteralPath.Substring(8)
    }
    if ($LiteralPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $LiteralPath.Substring(4)
    }
    return $LiteralPath
}

function Test-PathWithin {
    param(
        [Parameter(Mandatory)] [string] $Candidate,
        [Parameter(Mandatory)] [string] $Boundary
    )

    $candidateFull = [IO.Path]::GetFullPath($Candidate)
    $boundaryFull = ConvertTo-NormalizedDirectoryPath $Boundary
    if ([string]::Equals($candidateFull, $boundaryFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $candidateFull.StartsWith(
        $boundaryFull + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparseChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    $boundary = ConvertTo-NormalizedDirectoryPath $BoundaryRoot
    $current = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-PathWithin $current $boundary)) { throw "$Context escaped the repository root." }
    while ($true) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Context contains a reparse point: $current"
        }
        $normalized = if ($item.PSIsContainer) {
            ConvertTo-NormalizedDirectoryPath $item.FullName
        }
        else { [IO.Path]::GetFullPath($item.FullName) }
        if ([string]::Equals($normalized, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($normalized)
        if ([string]::IsNullOrEmpty($parent) -or -not (Test-PathWithin $parent $boundary)) {
            throw "$Context physical ancestry escaped the repository root."
        }
        $current = $parent
    }
}

function Resolve-ContainedFile {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        $RelativePath.IndexOf([char]0) -ge 0 -or
        $RelativePath -match '^[\\/]' -or
        $RelativePath -match '^[A-Za-z]:' -or
        $RelativePath.Contains(':') -or
        @($RelativePath -split '[\\/]' | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
        throw "$Context has an unsafe relative path."
    }

    $rootFull = ConvertTo-NormalizedDirectoryPath $Root
    $candidate = [IO.Path]::GetFullPath((Join-Path $rootFull $RelativePath))
    if (-not (Test-PathWithin $candidate $rootFull) -or
        [string]::Equals($candidate, $rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context escaped the repository root."
    }
    return $candidate
}

function Get-DirectoryAncestry {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    $boundary = ConvertTo-NormalizedDirectoryPath $BoundaryRoot
    $directory = [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($LiteralPath))
    if ([string]::IsNullOrEmpty($directory) -or -not (Test-PathWithin $directory $boundary)) {
        throw "$Context has no contained physical ancestry."
    }
    $stack = [Collections.Generic.Stack[string]]::new()
    while ($true) {
        $stack.Push($directory)
        if ([string]::Equals($directory, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($directory)
        if ([string]::IsNullOrEmpty($parent) -or -not (Test-PathWithin $parent $boundary)) {
            throw "$Context ancestry escaped the repository root."
        }
        $directory = $parent
    }
    return @($stack.ToArray())
}

function Get-FileHashFromStream {
    param([Parameter(Mandatory)] [IO.Stream] $Stream)

    $position = $Stream.Position
    try {
        $Stream.Position = 0
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToHexString($algorithm.ComputeHash($Stream)).ToLowerInvariant() }
        finally { $algorithm.Dispose() }
    }
    finally { $Stream.Position = $position }
}

function Assert-NoAlternateDataStreams {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context
    )

    $streams = @($script:BootstrapFile::GetStreamNames((ConvertTo-ExtendedPath $LiteralPath)))
    if ($streams.Count -ne 1 -or $streams[0] -cne '::$DATA') {
        throw "$Context contains an alternate data stream (ADS) or an invalid unnamed stream."
    }
}

function Open-DirectoryLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $Context
    )

    $full = ConvertTo-NormalizedDirectoryPath $LiteralPath
    Assert-NoReparseChain $full $RepositoryRoot $Context
    $handle = $script:BootstrapFile::OpenDirectoryLease((ConvertTo-ExtendedPath $full))
    try {
        $attributes = [IO.FileAttributes][uint]$script:BootstrapFile::GetAttributes($handle)
        if (($attributes -band [IO.FileAttributes]::Directory) -eq 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Context is not a physical regular directory."
        }
        $final = ConvertTo-NormalizedDirectoryPath (
            ConvertFrom-FinalPath $script:BootstrapFile::GetFinalPath($handle))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolved through a physical directory alias."
        }
        Assert-NoReparseChain $full $RepositoryRoot $Context
        return [pscustomobject][ordered]@{
            path = $full
            handle = $handle
            final_path = $final
            identity = $script:BootstrapFile::GetIdentity($handle)
        }
    }
    catch {
        $handle.Dispose()
        throw
    }
}

function Open-FileLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [long] $ExpectedSizeBytes = -1,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$' -or $ExpectedSizeBytes -lt -1) {
        throw "$Context has an invalid authenticated identity."
    }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-PathWithin $full $RepositoryRoot)) { throw "$Context escaped the repository root." }
    Assert-NoReparseChain $full $RepositoryRoot $Context
    $safeHandle = $script:BootstrapFile::OpenFileLease((ConvertTo-ExtendedPath $full))
    try { $stream = [IO.FileStream]::new($safeHandle, [IO.FileAccess]::Read) }
    catch { $safeHandle.Dispose(); throw }
    try {
        $attributes = [IO.FileAttributes][uint]$script:BootstrapFile::GetAttributes($stream.SafeFileHandle)
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Context is not a physical regular file."
        }
        $final = [IO.Path]::GetFullPath((
                ConvertFrom-FinalPath $script:BootstrapFile::GetFinalPath($stream.SafeFileHandle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolved through a physical file alias."
        }
        Assert-NoReparseChain $full $RepositoryRoot $Context
        Assert-NoAlternateDataStreams $full $Context
        $linkCount = [long]$script:BootstrapFile::GetLinkCount($stream.SafeFileHandle)
        if ($linkCount -ne 1) { throw "$Context is not a single-link file; hardlinks are forbidden." }
        if ($ExpectedSizeBytes -ge 0 -and $stream.Length -ne $ExpectedSizeBytes) {
            throw "$Context size differs from the authenticated contract."
        }
        $sha256 = Get-FileHashFromStream $stream
        if ($sha256 -cne $ExpectedSha256) {
            throw "$Context SHA-256 differs from its authenticated anchor."
        }
        return [pscustomobject][ordered]@{
            path = $full
            stream = $stream
            final_path = $final
            identity = $script:BootstrapFile::GetIdentity($stream.SafeFileHandle)
            link_count = $linkCount
            size_bytes = [long]$stream.Length
            sha256 = $sha256
            context = $Context
        }
    }
    catch {
        $stream.Dispose()
        throw
    }
}

function Assert-LeasesUnchanged {
    param(
        [Parameter(Mandatory)] [Collections.IEnumerable] $DirectoryLeases,
        [Parameter(Mandatory)] [Collections.IEnumerable] $FileLeases,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    foreach ($lease in $DirectoryLeases) {
        Assert-NoReparseChain $lease.path $RepositoryRoot "Bootstrap directory '$($lease.path)'"
        $attributes = [IO.FileAttributes][uint]$script:BootstrapFile::GetAttributes($lease.handle)
        $final = ConvertTo-NormalizedDirectoryPath (
            ConvertFrom-FinalPath $script:BootstrapFile::GetFinalPath($lease.handle))
        if (($attributes -band [IO.FileAttributes]::Directory) -eq 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not [string]::Equals($final, $lease.final_path, [StringComparison]::OrdinalIgnoreCase) -or
            $script:BootstrapFile::GetIdentity($lease.handle) -cne $lease.identity) {
            throw "A bootstrap ancestry directory changed identity: $($lease.path)"
        }
    }
    foreach ($lease in $FileLeases) {
        Assert-NoReparseChain $lease.path $RepositoryRoot $lease.context
        Assert-NoAlternateDataStreams $lease.path $lease.context
        $attributes = [IO.FileAttributes][uint]$script:BootstrapFile::GetAttributes($lease.stream.SafeFileHandle)
        $final = [IO.Path]::GetFullPath((
                ConvertFrom-FinalPath $script:BootstrapFile::GetFinalPath($lease.stream.SafeFileHandle)))
        if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            -not [string]::Equals($final, $lease.final_path, [StringComparison]::OrdinalIgnoreCase) -or
            $script:BootstrapFile::GetIdentity($lease.stream.SafeFileHandle) -cne $lease.identity) {
            throw "An authenticated bootstrap file changed physical identity: $($lease.path)"
        }
        if ([long]$script:BootstrapFile::GetLinkCount($lease.stream.SafeFileHandle) -ne $lease.link_count -or
            $lease.link_count -ne 1) {
            throw "An authenticated bootstrap file changed hardlink count: $($lease.path)"
        }
        if ($lease.stream.Length -ne $lease.size_bytes -or
            (Get-FileHashFromStream $lease.stream) -cne $lease.sha256) {
            throw "An authenticated bootstrap file changed bytes: $($lease.path)"
        }
    }
}

function Close-Leases {
    param(
        [Parameter(Mandatory)] [Collections.IList] $DirectoryLeases,
        [Parameter(Mandatory)] [Collections.IList] $FileLeases
    )

    for ($index = $FileLeases.Count - 1; $index -ge 0; $index--) {
        try { $FileLeases[$index].stream.Dispose() } catch { }
    }
    for ($index = $DirectoryLeases.Count - 1; $index -ge 0; $index--) {
        try { $DirectoryLeases[$index].handle.Dispose() } catch { }
    }
}

function Assert-NoDuplicateJsonProperties {
    param(
        [Parameter(Mandatory)] [Text.Json.JsonElement] $Element,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw "$Context contains duplicate JSON property '$($property.Name)'."
            }
            Assert-NoDuplicateJsonProperties $property.Value "$Context.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($item in $Element.EnumerateArray()) {
            Assert-NoDuplicateJsonProperties $item "$Context[$index]"
            $index++
        }
    }
}

function Read-ContractFromLease {
    param([Parameter(Mandatory)] $Lease)

    if ($Lease.stream.Length -le 0 -or $Lease.stream.Length -gt [int]::MaxValue) {
        throw 'The authenticated composite contract has an unsupported byte length.'
    }
    $bytes = [byte[]]::new([int]$Lease.stream.Length)
    $position = $Lease.stream.Position
    try {
        $Lease.stream.Position = 0
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $Lease.stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { throw 'The composite contract ended before its authenticated size.' }
            $offset += $read
        }
    }
    finally { $Lease.stream.Position = $position }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf) {
        throw 'The authenticated composite contract must be UTF-8 without BOM.'
    }
    try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
    catch { throw [IO.InvalidDataException]::new('The authenticated composite contract is not strict UTF-8.', $_.Exception) }
    try { $document = [Text.Json.JsonDocument]::Parse($text) }
    catch { throw [IO.InvalidDataException]::new('The authenticated composite contract is not JSON.', $_.Exception) }
    try { Assert-NoDuplicateJsonProperties $document.RootElement 'contract' }
    finally { $document.Dispose() }
    return $text | ConvertFrom-Json -Depth 100 -DateKind String
}

function Assert-ClosedCoreResult {
    param(
        [Parameter(Mandatory)] $Result,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] $Contract
    )

    $expectedProperties = @(
        'composite_id',
        'run_id',
        'contract_sha256',
        'manifest_path',
        'manifest_sha256',
        'receipt_path',
        'receipt_sha256'
    )
    if ($Result -isnot [pscustomobject]) {
        throw 'The authenticated orchestrator result must be exactly one closed object.'
    }
    $properties = @($Result.PSObject.Properties)
    if ($properties.Count -ne $expectedProperties.Count) {
        throw 'The authenticated orchestrator result has an open or incomplete property set.'
    }
    for ($index = 0; $index -lt $expectedProperties.Count; $index++) {
        if ($properties[$index].Name -cne $expectedProperties[$index] -or
            $properties[$index].MemberType -ne [Management.Automation.PSMemberTypes]::NoteProperty) {
            throw 'The authenticated orchestrator result property shape or order drifted.'
        }
    }

    $values = [ordered]@{}
    for ($index = 0; $index -lt $expectedProperties.Count; $index++) {
        $values[$expectedProperties[$index]] = $properties[$index].Value
    }

    foreach ($name in @('composite_id', 'run_id', 'contract_sha256', 'manifest_path',
            'manifest_sha256', 'receipt_path', 'receipt_sha256')) {
        if ($values[$name] -isnot [string] -or [string]::IsNullOrWhiteSpace($values[$name])) {
            throw "The authenticated orchestrator result '$name' must be a non-empty string."
        }
    }
    foreach ($name in @('contract_sha256', 'manifest_sha256', 'receipt_sha256')) {
        if ($values[$name] -cnotmatch '^[0-9a-f]{64}$') {
            throw "The authenticated orchestrator result '$name' must be a lowercase SHA-256 digest."
        }
    }
    if ($values.contract_sha256 -cne $ExpectedContractSha256) {
        throw 'The authenticated orchestrator result changed the explicit contract anchor.'
    }
    if ($values.composite_id -cne $Contract.composite_id) {
        throw 'The authenticated orchestrator result composite_id differs from the authenticated contract.'
    }

    $runRootFull = ConvertTo-NormalizedDirectoryPath $RunRoot
    if ($values.run_id -cne [IO.Path]::GetFileName($runRootFull)) {
        throw 'The authenticated orchestrator result run_id differs from RunRoot.'
    }
    $expectedArtifactPaths = [ordered]@{
        manifest_path = Resolve-ContainedFile -Root $runRootFull `
            -RelativePath $Contract.isolation.manifest_relative_path `
            -Context 'Authenticated contract manifest path'
        receipt_path = Resolve-ContainedFile -Root $runRootFull `
            -RelativePath $Contract.isolation.receipt_relative_path `
            -Context 'Authenticated contract receipt path'
    }
    $artifactPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @('manifest_path', 'receipt_path')) {
        $path = [string]$values[$name]
        if (-not [IO.Path]::IsPathFullyQualified($path)) {
            throw "The authenticated orchestrator result '$name' must be an absolute path."
        }
        $full = [IO.Path]::GetFullPath($path)
        if (-not [string]::Equals($full, $path, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-PathWithin $full $runRootFull) -or
            [string]::Equals($full, $runRootFull, [StringComparison]::OrdinalIgnoreCase)) {
            throw "The authenticated orchestrator result '$name' is not canonical inside RunRoot."
        }
        if (-not [string]::Equals(
                $full, [string]$expectedArtifactPaths[$name], [StringComparison]::OrdinalIgnoreCase)) {
            throw "The authenticated orchestrator result '$name' differs from the exact contract path."
        }
        if (-not $artifactPaths.Add($full)) {
            throw 'The authenticated orchestrator result aliases its manifest and receipt paths.'
        }
    }
    return [pscustomobject]$values
}

function Assert-ExactVerifierObjectShape {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string[]] $ExpectedProperties,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($Value -isnot [pscustomobject]) {
        throw "The authenticated verifier $Context must be exactly one object."
    }
    $properties = @($Value.PSObject.Properties)
    if ($properties.Count -ne $ExpectedProperties.Count) {
        throw "The authenticated verifier $Context has an open or incomplete property set."
    }
    for ($index = 0; $index -lt $ExpectedProperties.Count; $index++) {
        if ($properties[$index].Name -cne $ExpectedProperties[$index] -or
            $properties[$index].MemberType -ne [Management.Automation.PSMemberTypes]::NoteProperty) {
            throw "The authenticated verifier $Context property shape or order drifted."
        }
    }
}

function Assert-ClosedVerifierResult {
    param(
        [Parameter(Mandatory)] $Result,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedQtBaseReceiptSha256
    )

    Assert-ExactVerifierObjectShape $Result @(
        'schema_version', 'receipt_id', 'composite_id', 'captured_utc', 'contract_sha256',
        'execution', 'qtbase_receipt', 'sdk', 'evidence', 'limits'
    ) 'result'
    Assert-ExactVerifierObjectShape $Result.execution @('exit_code', 'stdout', 'stderr') 'execution result'
    Assert-ExactVerifierObjectShape $Result.qtbase_receipt @(
        'relative_path', 'size_bytes', 'sha256'
    ) 'qtbase_receipt result'
    Assert-ExactVerifierObjectShape $Result.sdk @(
        'root_relative_path', 'file_count', 'total_size_bytes', 'inventory_relative_path',
        'inventory_size_bytes', 'inventory_sha256'
    ) 'sdk result'
    Assert-ExactVerifierObjectShape $Result.evidence @(
        'relative_path', 'size_bytes', 'sha256'
    ) 'evidence result'

    if ($Result.schema_version -isnot [long] -or $Result.schema_version -ne 2 -or
        $Result.receipt_id -isnot [string] -or
        $Result.receipt_id -cne "$($Contract.composite_id)-receipt" -or
        $Result.composite_id -isnot [string] -or
        $Result.composite_id -cne $Contract.composite_id -or
        $Result.contract_sha256 -isnot [string] -or
        $Result.contract_sha256 -cne $ExpectedContractSha256) {
        throw 'The authenticated verifier result identity or external contract anchor drifted.'
    }

    $capturedUtc = [DateTimeOffset]::MinValue
    if ($Result.captured_utc -isnot [string] -or
        -not [DateTimeOffset]::TryParseExact(
            $Result.captured_utc, 'O', [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind, [ref]$capturedUtc) -or
        $capturedUtc.Offset -ne [TimeSpan]::Zero -or
        $capturedUtc.ToString('O', [Globalization.CultureInfo]::InvariantCulture) -cne $Result.captured_utc) {
        throw 'The authenticated verifier result captured_utc is not a canonical UTC timestamp.'
    }
    if ($Result.execution.exit_code -isnot [long] -or $Result.execution.exit_code -ne 0 -or
        $Result.execution.stdout -isnot [string] -or $Result.execution.stderr -isnot [string]) {
        throw 'The authenticated verifier execution result has malformed types or values.'
    }

    if ($Result.qtbase_receipt.relative_path -isnot [string] -or
        $Result.qtbase_receipt.relative_path -cne $Contract.provenance.qtbase_refresh.receipt_relative_path -or
        $Result.qtbase_receipt.size_bytes -isnot [long] -or
        $Result.qtbase_receipt.size_bytes -ne $Contract.provenance.qtbase_refresh.receipt_size_bytes -or
        $Result.qtbase_receipt.sha256 -isnot [string] -or
        $Result.qtbase_receipt.sha256 -cne $ExpectedQtBaseReceiptSha256) {
        throw 'The authenticated verifier qtbase_receipt result has malformed types, paths, or anchors.'
    }

    if ($Result.sdk.root_relative_path -isnot [string] -or
        $Result.sdk.root_relative_path -cne $Contract.isolation.sdk_relative_path -or
        $Result.sdk.file_count -isnot [long] -or $Result.sdk.file_count -le 0 -or
        $Result.sdk.total_size_bytes -isnot [long] -or $Result.sdk.total_size_bytes -le 0 -or
        $Result.sdk.inventory_relative_path -isnot [string] -or
        $Result.sdk.inventory_relative_path -cne $Contract.isolation.global_inventory_relative_path -or
        $Result.sdk.inventory_size_bytes -isnot [long] -or $Result.sdk.inventory_size_bytes -le 0 -or
        $Result.sdk.inventory_sha256 -isnot [string] -or
        $Result.sdk.inventory_sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'The authenticated verifier sdk result has malformed types, paths, or hashes.'
    }
    if ($Result.evidence.relative_path -isnot [string] -or
        $Result.evidence.relative_path -cne $Contract.isolation.manifest_relative_path -or
        $Result.evidence.size_bytes -isnot [long] -or $Result.evidence.size_bytes -le 0 -or
        $Result.evidence.sha256 -isnot [string] -or
        $Result.evidence.sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw 'The authenticated verifier evidence result has malformed types, paths, or hashes.'
    }

    if ($Result.limits -isnot [Array] -or $Contract.limits -isnot [Array] -or
        @($Result.limits).Count -ne @($Contract.limits).Count) {
        throw 'The authenticated verifier limits result has malformed type or cardinality.'
    }
    for ($index = 0; $index -lt @($Contract.limits).Count; $index++) {
        if ($Result.limits[$index] -isnot [string] -or
            $Contract.limits[$index] -isnot [string] -or
            $Result.limits[$index] -cne $Contract.limits[$index]) {
            throw 'The authenticated verifier limits result changed type, value, or order.'
        }
    }
}

$repository = ConvertTo-NormalizedDirectoryPath (
    [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path))
if (-not [IO.Directory]::Exists($repository)) { throw 'The repository root is not a directory.' }
$contract = [IO.Path]::GetFullPath($ContractPath)
if (-not (Test-PathWithin $contract $repository) -or
    [string]::Equals($contract, $repository, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The composite contract must be inside the repository root.'
}

$fixedRecipePaths = [ordered]@{}
foreach ($expected in $script:ExpectedRecipePaths.GetEnumerator()) {
    $fixedRecipePaths[$expected.Key] = Resolve-ContainedFile -Root $repository `
        -RelativePath $expected.Value -Context "Recipe '$($expected.Key)'"
}

$directoryLeases = [Collections.Generic.List[object]]::new()
$fileLeases = [Collections.Generic.List[object]]::new()
$module = $null
$launcherResult = $null
try {
    $directorySet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($bootstrapPath in @($contract) + @($fixedRecipePaths.Values)) {
        foreach ($directory in @(Get-DirectoryAncestry $bootstrapPath $repository 'Bootstrap file')) {
            if ($directorySet.Add($directory)) {
                $directoryLeases.Add((Open-DirectoryLease $directory $repository "Bootstrap directory '$directory'"))
            }
        }
    }

    $contractLease = Open-FileLease -LiteralPath $contract -RepositoryRoot $repository `
        -ExpectedSha256 $ExpectedContractSha256 -Context 'Composite contract'
    $fileLeases.Add($contractLease)
    $contractDocument = Read-ContractFromLease $contractLease

    if ($contractDocument -isnot [pscustomobject] -or
        $null -eq $contractDocument.PSObject.Properties['schema_version'] -or
        $contractDocument.schema_version -isnot [long] -or $contractDocument.schema_version -ne 2 -or
        $null -eq $contractDocument.PSObject.Properties['composite_id'] -or
        $contractDocument.composite_id -isnot [string] -or
        [string]::IsNullOrWhiteSpace($contractDocument.composite_id) -or
        $null -eq $contractDocument.PSObject.Properties['provenance'] -or
        $contractDocument.provenance -isnot [pscustomobject] -or
        $null -eq $contractDocument.provenance.PSObject.Properties['qtbase_refresh'] -or
        $contractDocument.provenance.qtbase_refresh -isnot [pscustomobject] -or
        $null -eq $contractDocument.provenance.qtbase_refresh.PSObject.Properties['receipt_sha256'] -or
        $contractDocument.provenance.qtbase_refresh.receipt_sha256 -isnot [string] -or
        $null -eq $contractDocument.PSObject.Properties['recipe'] -or
        $contractDocument.recipe -isnot [pscustomobject] -or
        $null -eq $contractDocument.recipe.PSObject.Properties['runner_export'] -or
        $contractDocument.recipe.runner_export -cne 'Invoke-M0QtShellCompositeNative' -or
        $null -eq $contractDocument.recipe.PSObject.Properties['files'] -or
        $contractDocument.recipe.files -isnot [Array] -or
        @($contractDocument.recipe.files).Count -ne $script:ExpectedRecipePaths.Count -or
        $null -eq $contractDocument.PSObject.Properties['isolation'] -or
        $contractDocument.isolation -isnot [pscustomobject] -or
        $null -eq $contractDocument.isolation.PSObject.Properties['manifest_relative_path'] -or
        $contractDocument.isolation.manifest_relative_path -isnot [string] -or
        $null -eq $contractDocument.isolation.PSObject.Properties['receipt_relative_path'] -or
        $contractDocument.isolation.receipt_relative_path -isnot [string]) {
        throw 'The authenticated composite contract does not contain the closed production recipe.'
    }
    if ($contractDocument.provenance.qtbase_refresh.receipt_sha256 -cne $ExpectedQtBaseReceiptSha256) {
        throw 'The explicit Qtbase receipt anchor differs from the composite contract.'
    }

    $recipePaths = [ordered]@{}
    $recipeSpecs = @($contractDocument.recipe.files)
    $expectedRecipe = @($script:ExpectedRecipePaths.GetEnumerator())
    for ($index = 0; $index -lt $expectedRecipe.Count; $index++) {
        $spec = $recipeSpecs[$index]
        $expected = $expectedRecipe[$index]
        if ($spec -isnot [pscustomobject] -or
            $null -eq $spec.PSObject.Properties['id'] -or $spec.id -isnot [string] -or
            $null -eq $spec.PSObject.Properties['relative_path'] -or $spec.relative_path -isnot [string] -or
            $null -eq $spec.PSObject.Properties['size_bytes'] -or $spec.size_bytes -isnot [long] -or
            [long]$spec.size_bytes -le 0 -or
            $null -eq $spec.PSObject.Properties['sha256'] -or $spec.sha256 -isnot [string] -or
            $spec.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $spec.id -cne $expected.Key -or $spec.relative_path -cne $expected.Value) {
            throw "Recipe entry $index differs from the closed launcher recipe."
        }
        $path = [string]$fixedRecipePaths[$expected.Key]
        $lease = Open-FileLease -LiteralPath $path -RepositoryRoot $repository `
            -ExpectedSizeBytes ([long]$spec.size_bytes) -ExpectedSha256 $spec.sha256 `
            -Context "Recipe '$($spec.id)'"
        $fileLeases.Add($lease)
        $recipePaths[$spec.id] = $lease.path
    }

    if (-not [string]::Equals(
            [IO.Path]::GetFullPath([string]$recipePaths.entrypoint),
            [IO.Path]::GetFullPath($PSCommandPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The running entrypoint is not the authenticated recipe entrypoint.'
    }

    Assert-LeasesUnchanged $directoryLeases $fileLeases $repository
    foreach ($loadedName in @('M0.QtShellComposite', 'M0.QtShellCompositeNative')) {
        if (Get-Module -Name $loadedName -ErrorAction SilentlyContinue) {
            throw "A composite recipe module was preloaded: $loadedName"
        }
    }

    $imported = @(Import-Module -Name ([string]$recipePaths.orchestrator) -Force -PassThru)
    if ($imported.Count -ne 1) { throw 'The authenticated orchestrator import did not yield exactly one module.' }
    $module = $imported[0]
    $functionName = if ($PSCmdlet.ParameterSetName -ceq 'Verify') {
        'Assert-M0QtShellCompositeReceipt'
    }
    else { 'Invoke-M0QtShellComposite' }
    $coreFunction = $module.ExportedFunctions[$functionName]
    if ($null -eq $coreFunction -or $coreFunction.Name -cne $functionName) {
        throw "The authenticated orchestrator does not export $functionName."
    }
    Assert-LeasesUnchanged $directoryLeases $fileLeases $repository

    if ($PSCmdlet.ParameterSetName -ceq 'Verify') {
        $receiptFull = [IO.Path]::GetFullPath($ReceiptPath)
        if (-not (Test-PathWithin $receiptFull $repository) -or
            [string]::Equals($receiptFull, $repository, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The independently verified receipt must be inside the repository root.'
        }
        $verified = & $coreFunction `
            -LiteralPath $receiptFull `
            -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
            -ContractPath $contractLease.path `
            -ExpectedContractSha256 $ExpectedContractSha256 `
            -ExpectedQtBaseReceiptSha256 $ExpectedQtBaseReceiptSha256 `
            -RepositoryRoot $repository
        Assert-LeasesUnchanged $directoryLeases $fileLeases $repository
        Assert-ClosedVerifierResult $verified $contractDocument `
            $ExpectedContractSha256 $ExpectedQtBaseReceiptSha256
        $launcherResult = [pscustomobject][ordered]@{
            composite_id = [string]$contractDocument.composite_id
            contract_sha256 = $ExpectedContractSha256
            qtbase_receipt_sha256 = $ExpectedQtBaseReceiptSha256
            receipt_path = $receiptFull
            receipt_sha256 = $ExpectedReceiptSha256
            verified = $true
        }
    }
    else {
        $result = & $coreFunction `
            -ContractPath $contractLease.path `
            -ExpectedContractSha256 $ExpectedContractSha256 `
            -ExpectedQtBaseReceiptSha256 $ExpectedQtBaseReceiptSha256 `
            -RepositoryRoot $repository `
            -RunRoot $RunRoot
        Assert-LeasesUnchanged $directoryLeases $fileLeases $repository

        $validatedResult = Assert-ClosedCoreResult $result $ExpectedContractSha256 $RunRoot $contractDocument
        $manifestPath = [string]$validatedResult.manifest_path
        $manifestSha256 = [string]$validatedResult.manifest_sha256
        $receiptPath = [string]$validatedResult.receipt_path
        $receiptSha256 = [string]$validatedResult.receipt_sha256
        $manifestLease = Open-FileLease -LiteralPath $manifestPath -RepositoryRoot $repository `
            -ExpectedSha256 $manifestSha256 -Context 'Composite manifest handoff'
        $fileLeases.Add($manifestLease)
        $receiptLease = Open-FileLease -LiteralPath $receiptPath -RepositoryRoot $repository `
            -ExpectedSha256 $receiptSha256 -Context 'Composite receipt handoff'
        $fileLeases.Add($receiptLease)
        Assert-LeasesUnchanged $directoryLeases $fileLeases $repository

        $launcherResult = [pscustomobject][ordered]@{
            composite_id = [string]$validatedResult.composite_id
            run_id = [string]$validatedResult.run_id
            contract_sha256 = $ExpectedContractSha256
            manifest_path = $manifestPath
            manifest_sha256 = $manifestSha256
            receipt_path = $receiptPath
            receipt_sha256 = $receiptSha256
        }
    }
}
finally {
    if ($null -ne $module) {
        try { Remove-Module -ModuleInfo $module -Force -ErrorAction Stop } catch { }
    }
    $revalidationError = $null
    try {
        if ($directoryLeases.Count -ne 0 -or $fileLeases.Count -ne 0) {
            Assert-LeasesUnchanged $directoryLeases $fileLeases $repository
        }
    }
    catch { $revalidationError = $_ }
    finally { Close-Leases $directoryLeases $fileLeases }
    if ($null -ne $revalidationError) { throw $revalidationError.Exception }
}

$launcherResult
