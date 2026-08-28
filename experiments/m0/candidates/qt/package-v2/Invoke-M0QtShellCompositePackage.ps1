#Requires -Version 7.2

[CmdletBinding(DefaultParameterSetName = 'Build')]
param(
    [Parameter(Mandatory)]
    [string] $ContractPath,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedContractSha256,

    [Parameter(Mandatory)]
    [string] $CompositeReceiptPath,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedCompositeReceiptSha256,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedQtBaseReceiptSha256,

    [Parameter(Mandatory)]
    [string] $RepositoryRoot,

    [Parameter(Mandatory, ParameterSetName = 'Build')]
    [string] $BuildRoot,

    [Parameter(Mandatory, ParameterSetName = 'Build')]
    [Parameter(Mandatory, ParameterSetName = 'Relocate')]
    [string] $OutputRoot,

    [Parameter(Mandatory, ParameterSetName = 'Build')]
    [Parameter(Mandatory, ParameterSetName = 'Relocate')]
    [string] $ScratchRoot,

    [Parameter(Mandatory, ParameterSetName = 'Smoke')]
    [string] $PackageRoot,

    [Parameter(Mandatory, ParameterSetName = 'Relocate')]
    [string] $SourcePackageRoot,

    [Parameter(Mandatory, ParameterSetName = 'Smoke')]
    [Parameter(Mandatory, ParameterSetName = 'Relocate')]
    [string] $ReceiptPath,

    [Parameter(Mandatory, ParameterSetName = 'Smoke')]
    [Parameter(Mandatory, ParameterSetName = 'Relocate')]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedReceiptSha256,

    [Parameter(Mandatory, ParameterSetName = 'Smoke')]
    [string] $SmokeWorkspaceRoot,

    [Parameter(Mandatory, ParameterSetName = 'Smoke')]
    [string] $ReportPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $IsWindows -or -not [Environment]::Is64BitProcess) {
    throw 'The authenticated package-v2 launcher requires Windows x64 PowerShell.'
}

$script:PackageId = 'm0.2-qt-shell-composite-package-v2'
$script:ContractRelativePath = 'experiments/m0/candidates/qt/package-v2/qt-shell-composite-package-contract.v2.json'
$script:ExpectedRecipePaths = @(
    'experiments/m0/candidates/qt/package-v2/Invoke-M0QtShellCompositePackage.ps1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackage.psm1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageNative.psm1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageSmoke.psm1',
    'experiments/m0/candidates/qt/package-v2/Test-M0QtShellCompositePackageSmoke.ps1',
    'experiments/m0/candidates/qt/package-v2/qt.conf',
    'experiments/m0/candidates/qt/package-v2/PACKAGE-LIMITATIONS.txt'
)
$script:CoreRelativePath = $script:ExpectedRecipePaths[1]
$script:NativeRelativePath = $script:ExpectedRecipePaths[2]
$script:SmokeRelativePath = $script:ExpectedRecipePaths[3]
$script:CoreExports = @(
    'Assert-M0QtShellCompositePackageReceipt',
    'Invoke-M0QtShellCompositePackage'
)
$script:CoreInvokeParameters = @(
    'ContractPath',
    'ExpectedContractSha256',
    'CompositeReceiptPath',
    'ExpectedCompositeReceiptSha256',
    'ExpectedQtBaseReceiptSha256',
    'RepositoryRoot',
    'BuildRoot',
    'OutputRoot',
    'ScratchRoot'
)
$script:BuildResultFields = @(
    'exit_code', 'package_root', 'receipt_path', 'receipt_sha256',
    'manifest_path', 'manifest_sha256', 'entries'
)
$script:NativeResultFields = @(
    'exit_code', 'package_root', 'payload_root', 'evidence_root',
    'entries', 'published_root_identity'
)
$script:VerifierResultFields = @(
    'verified', 'package_id', 'contract_sha256', 'composite_receipt_sha256',
    'manifest_sha256', 'receipt_sha256', 'manifest_path', 'payload', 'evidence'
)
$script:SmokeResultFields = @(
    'verified', 'package_id', 'package_root', 'contract_sha256',
    'composite_receipt_sha256', 'manifest_sha256', 'receipt_sha256',
    'journeys', 'workspace', 'limits'
)
$script:JourneyResultFields = @(
    'journey', 'working_directory', 'exit_code', 'stdout', 'stderr',
    'timed_out', 'module_capture_ms', 'module_paths', 'survivor_count'
)
$script:SmokeLimits = @(
    'offscreen diagnostic only',
    'sampling is not a complete loader trace',
    'no accessibility ABI or redistribution proof'
)
$script:CommonParameters = @(
    'Verbose', 'Debug', 'ErrorAction', 'WarningAction', 'InformationAction',
    'ProgressAction', 'ErrorVariable', 'WarningVariable', 'InformationVariable',
    'OutVariable', 'OutBuffer', 'PipelineVariable', 'WhatIf', 'Confirm'
)

$nativeNamespace = 'M0PackageV2Launcher_' + [Guid]::NewGuid().ToString('N')
$nativeSource = @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace __NAMESPACE__ {
    [StructLayout(LayoutKind.Sequential)]
    internal struct ByHandleFileInformation {
        internal uint FileAttributes;
        internal System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        internal uint VolumeSerialNumber;
        internal uint FileSizeHigh;
        internal uint FileSizeLow;
        internal uint NumberOfLinks;
        internal uint FileIndexHigh;
        internal uint FileIndexLow;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FileDispositionInformation {
        [MarshalAs(UnmanagedType.Bool)]
        internal bool DeleteFile;
    }

    public sealed class Identity {
        public uint FileAttributes { get; set; }
        public uint VolumeSerialNumber { get; set; }
        public ulong FileIndex { get; set; }
        public uint NumberOfLinks { get; set; }
        public ulong FileSize { get; set; }
    }

    public static class Native {
        private const uint GenericRead = 0x80000000;
        private const uint GenericWrite = 0x40000000;
        private const uint Delete = 0x00010000;
        private const uint FileShareRead = 0x00000001;
        private const uint FileShareWrite = 0x00000002;
        private const uint CreateNew = 1;
        private const uint OpenExisting = 3;
        private const uint FileAttributeNormal = 0x00000080;
        private const uint FileFlagBackupSemantics = 0x02000000;
        private const uint FileFlagOpenReparsePoint = 0x00200000;
        private const int FileDispositionInfo = 4;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(
            string fileName,
            uint desiredAccess,
            uint shareMode,
            IntPtr securityAttributes,
            uint creationDisposition,
            uint flagsAndAttributes,
            IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle,
            out ByHandleFileInformation information);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle,
            StringBuilder path,
            uint pathLength,
            uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(
            SafeFileHandle handle,
            int informationClass,
            ref FileDispositionInformation information,
            uint bufferSize);

        public static SafeFileHandle OpenFile(string path) {
            SafeFileHandle handle = CreateFileW(
                path,
                GenericRead,
                FileShareRead,
                IntPtr.Zero,
                OpenExisting,
                FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle.IsInvalid) {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "Could not retain file identity: " + path);
            }
            return handle;
        }

        public static SafeFileHandle OpenDirectory(string path) {
            SafeFileHandle handle = CreateFileW(
                path,
                0,
                FileShareRead | FileShareWrite,
                IntPtr.Zero,
                OpenExisting,
                FileFlagBackupSemantics | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle.IsInvalid) {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "Could not retain directory identity: " + path);
            }
            return handle;
        }

        public static SafeFileHandle CreateNewFile(string path) {
            SafeFileHandle handle = CreateFileW(
                path,
                GenericRead | GenericWrite | Delete,
                FileShareRead,
                IntPtr.Zero,
                CreateNew,
                FileAttributeNormal | FileFlagOpenReparsePoint,
                IntPtr.Zero);
            if (handle.IsInvalid) {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error, "Could not create authenticated file: " + path);
            }
            return handle;
        }

        public static void MarkDeleteOnClose(SafeFileHandle handle) {
            var information = new FileDispositionInformation { DeleteFile = true };
            if (!SetFileInformationByHandle(
                    handle,
                    FileDispositionInfo,
                    ref information,
                    (uint)Marshal.SizeOf<FileDispositionInformation>())) {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Could not roll back the exact created file handle.");
            }
        }

        public static Identity Inspect(SafeFileHandle handle) {
            ByHandleFileInformation information;
            if (!GetFileInformationByHandle(handle, out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return new Identity {
                FileAttributes = information.FileAttributes,
                VolumeSerialNumber = information.VolumeSerialNumber,
                FileIndex = ((ulong)information.FileIndexHigh << 32) | information.FileIndexLow,
                NumberOfLinks = information.NumberOfLinks,
                FileSize = ((ulong)information.FileSizeHigh << 32) | information.FileSizeLow
            };
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(
                handle,
                buffer,
                (uint)buffer.Capacity,
                0);
            if (length == 0 || length >= buffer.Capacity) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return buffer.ToString();
        }
    }
}
'@.Replace('__NAMESPACE__', $nativeNamespace)
$nativeTypes = @(Add-Type -TypeDefinition $nativeSource -PassThru)
$script:LauncherNative = @($nativeTypes | Where-Object { $_.Name -ceq 'Native' })[0]

function ConvertTo-M0PackageLauncherFullPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if ([string]::IsNullOrWhiteSpace($LiteralPath)) { throw 'A required path is empty.' }
    return [IO.Path]::GetFullPath($LiteralPath)
}

function ConvertFrom-M0PackageLauncherFinalPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if ($LiteralPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $LiteralPath.Substring(8)
    }
    if ($LiteralPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $LiteralPath.Substring(4)
    }
    return $LiteralPath
}

function Test-M0PackageLauncherPathWithin {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Root,
        [switch] $AllowEqual
    )
    $candidate = (ConvertTo-M0PackageLauncherFullPath $LiteralPath).TrimEnd('\')
    $boundary = (ConvertTo-M0PackageLauncherFullPath $Root).TrimEnd('\')
    if ($AllowEqual -and $candidate.Equals($boundary, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $candidate.StartsWith($boundary + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-M0PackageLauncherSafeRelativePath {
    param([Parameter(Mandatory)] [string] $RelativePath, [Parameter(Mandatory)] [string] $Context)
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains('\', [StringComparison]::Ordinal) -or
        $RelativePath.Contains(':', [StringComparison]::Ordinal) -or
        $RelativePath.StartsWith('/', [StringComparison]::Ordinal) -or
        $RelativePath.EndsWith('/', [StringComparison]::Ordinal)) {
        throw "$Context has an unsafe relative path."
    }
    foreach ($segment in $RelativePath.Split('/')) {
        if ([string]::IsNullOrWhiteSpace($segment) -or $segment -ceq '.' -or $segment -ceq '..') {
            throw "$Context has an unsafe relative path segment."
        }
    }
}

function Resolve-M0PackageLauncherContainedPath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0PackageLauncherSafeRelativePath $RelativePath $Context
    $full = ConvertTo-M0PackageLauncherFullPath (
        Join-Path $Root $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    if (-not (Test-M0PackageLauncherPathWithin $full $Root)) {
        throw "$Context escaped the repository root."
    }
    return $full
}

function Assert-M0PackageLauncherNoReparseChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = ConvertTo-M0PackageLauncherFullPath $LiteralPath
    $boundary = ConvertTo-M0PackageLauncherFullPath $Root
    if (-not (Test-M0PackageLauncherPathWithin $full $boundary -AllowEqual)) {
        throw "$Context escaped its physical boundary."
    }
    $cursor = $full
    while ($true) {
        if ([IO.Directory]::Exists($cursor) -or [IO.File]::Exists($cursor)) {
            $item = Get-Item -LiteralPath $cursor -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Context contains a reparse point: $cursor"
            }
        }
        if ($cursor.Equals($boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -ceq $cursor) {
            throw "$Context did not reach its physical boundary."
        }
        $cursor = $parent
    }
}

function Assert-M0PackageLauncherNoAds {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    $streams = @(Get-Item -LiteralPath $LiteralPath -Stream * -Force -ErrorAction Stop)
    $extra = @($streams | Where-Object { $_.Stream -cne ':$DATA' })
    if ($extra.Count -ne 0) { throw "$Context contains an alternate data stream." }
}

function Get-M0PackageLauncherSha256FromStream {
    param([Parameter(Mandatory)] [IO.Stream] $Stream)
    $Stream.Position = 0
    try {
        return [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($Stream)).ToLowerInvariant()
    }
    finally { $Stream.Position = 0 }
}

function Test-M0PackageLauncherIdentityEqual {
    param($Left, $Right)
    return $Left.VolumeSerialNumber -eq $Right.VolumeSerialNumber -and
        $Left.FileIndex -eq $Right.FileIndex
}

function Open-M0PackageLauncherDirectoryLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $path = ConvertTo-M0PackageLauncherFullPath $LiteralPath
    if (-not [IO.Directory]::Exists($path)) { throw "$Context directory is missing." }
    Assert-M0PackageLauncherNoReparseChain $path $RepositoryRoot $Context
    Assert-M0PackageLauncherNoAds $path $Context
    $handle = $script:LauncherNative::OpenDirectory($path)
    try {
        $identity = $script:LauncherNative::Inspect($handle)
        $attributes = [IO.FileAttributes]$identity.FileAttributes
        $finalPath = ConvertTo-M0PackageLauncherFullPath (
            ConvertFrom-M0PackageLauncherFinalPath ($script:LauncherNative::GetFinalPath($handle)))
        if (-not ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [string]::Equals($finalPath, $path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context is not the exact ordinary directory requested by pathname."
        }
        return [pscustomobject][ordered]@{
            kind = 'directory'
            path = $path
            handle = $handle
            identity = $identity
            context = $Context
        }
    }
    catch { $handle.Dispose(); throw }
}

function Open-M0PackageLauncherFileLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $Context
    )
    if ($ExpectedSizeBytes -lt 0 -or $ExpectedSha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context has an invalid size or SHA-256 pin."
    }
    $path = ConvertTo-M0PackageLauncherFullPath $LiteralPath
    if (-not [IO.File]::Exists($path)) { throw "$Context file is missing." }
    Assert-M0PackageLauncherNoReparseChain $path $RepositoryRoot $Context
    Assert-M0PackageLauncherNoAds $path $Context
    $pathToOpen = $path
    # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_FILE_OPEN
    $handle = $null
    $stream = $null
    try {
        $handle = $script:LauncherNative::OpenFile($pathToOpen)
        $identity = $script:LauncherNative::Inspect($handle)
        $attributes = [IO.FileAttributes]$identity.FileAttributes
        $finalPath = ConvertTo-M0PackageLauncherFullPath (
            ConvertFrom-M0PackageLauncherFinalPath ($script:LauncherNative::GetFinalPath($handle)))
        if (($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [string]::Equals($finalPath, $path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context is not the exact ordinary file requested by pathname."
        }
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::Read, 4096, $false)
        $handle = $null
        if ([long]$stream.Length -ne $ExpectedSizeBytes) {
            throw "$Context size differs from its authenticated recipe pin."
        }
        if ([long]$identity.NumberOfLinks -ne 1) {
            throw "$Context is hardlinked."
        }
        $sha256 = Get-M0PackageLauncherSha256FromStream $stream
        if ($sha256 -cne $ExpectedSha256) {
            throw "$Context SHA-256 differs from its authenticated recipe pin."
        }
        return [pscustomobject][ordered]@{
            kind = 'file'
            path = $path
            stream = $stream
            identity = $identity
            size_bytes = [long]$stream.Length
            sha256 = $sha256
            context = $Context
        }
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        elseif ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Get-M0PackageLauncherDirectoryAncestry {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $path = ConvertTo-M0PackageLauncherFullPath $LiteralPath
    $boundary = ConvertTo-M0PackageLauncherFullPath $RepositoryRoot
    $cursor = if ([IO.Directory]::Exists($path)) { $path } else { [IO.Path]::GetDirectoryName($path) }
    while (-not [IO.Directory]::Exists($cursor)) {
        $parent = [IO.Path]::GetDirectoryName($cursor)
        if ([string]::IsNullOrWhiteSpace($parent) -or $parent -ceq $cursor) {
            throw "$Context has no existing ancestor inside the repository."
        }
        $cursor = $parent
    }
    if (-not (Test-M0PackageLauncherPathWithin $cursor $boundary -AllowEqual)) {
        throw "$Context escaped the repository root."
    }
    $result = [Collections.Generic.List[string]]::new()
    while ($true) {
        $result.Add($cursor)
        if ($cursor.Equals($boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $cursor = [IO.Path]::GetDirectoryName($cursor)
    }
    $values = $result.ToArray()
    [Array]::Reverse($values)
    return $values
}

function Add-M0PackageLauncherDirectoryLeases {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.HashSet[string]] $Seen,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $Leases,
        [Parameter(Mandatory)] [string] $Context
    )
    foreach ($directory in @(Get-M0PackageLauncherDirectoryAncestry $LiteralPath $RepositoryRoot $Context)) {
        if ($Seen.Add($directory)) {
            $Leases.Add((Open-M0PackageLauncherDirectoryLease $directory $RepositoryRoot `
                        "Bootstrap directory '$directory'"))
        }
    }
}

function Assert-M0PackageLauncherLeasesUnchanged {
    param(
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $DirectoryLeases,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.List[object]] $FileLeases,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    foreach ($lease in $DirectoryLeases) {
        Assert-M0PackageLauncherNoReparseChain $lease.path $RepositoryRoot $lease.context
        $identity = $script:LauncherNative::Inspect($lease.handle)
        $attributes = [IO.FileAttributes]$identity.FileAttributes
        $finalPath = ConvertTo-M0PackageLauncherFullPath (
            ConvertFrom-M0PackageLauncherFinalPath ($script:LauncherNative::GetFinalPath($lease.handle)))
        if (-not (Test-M0PackageLauncherIdentityEqual $identity $lease.identity) -or
            -not ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [string]::Equals($finalPath, $lease.path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context detected directory identity drift: $($lease.path)"
        }
    }
    foreach ($lease in $FileLeases) {
        Assert-M0PackageLauncherNoReparseChain $lease.path $RepositoryRoot $lease.context
        Assert-M0PackageLauncherNoAds $lease.path $lease.context
        $identity = $script:LauncherNative::Inspect($lease.stream.SafeFileHandle)
        $attributes = [IO.FileAttributes]$identity.FileAttributes
        $finalPath = ConvertTo-M0PackageLauncherFullPath (
            ConvertFrom-M0PackageLauncherFinalPath (
                $script:LauncherNative::GetFinalPath($lease.stream.SafeFileHandle)))
        if (-not (Test-M0PackageLauncherIdentityEqual $identity $lease.identity) -or
            ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [string]::Equals($finalPath, $lease.path, [StringComparison]::OrdinalIgnoreCase) -or
            [long]$identity.NumberOfLinks -ne 1 -or
            [long]$lease.stream.Length -ne [long]$lease.size_bytes -or
            (Get-M0PackageLauncherSha256FromStream $lease.stream) -cne $lease.sha256) {
            throw "$Context detected authenticated file drift: $($lease.path)"
        }
    }
}

function Close-M0PackageLauncherLeases {
    param([Collections.Generic.List[object]] $FileLeases, [Collections.Generic.List[object]] $DirectoryLeases)
    for ($index = $FileLeases.Count - 1; $index -ge 0; $index--) {
        try { $FileLeases[$index].stream.Dispose() } catch { }
    }
    for ($index = $DirectoryLeases.Count - 1; $index -ge 0; $index--) {
        try { $DirectoryLeases[$index].handle.Dispose() } catch { }
    }
}

function Remove-M0PackageLauncherImportedModules {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[Management.Automation.PSModuleInfo]] $Modules
    )
    $diagnostics = [Collections.Generic.List[string]]::new()
    for ($index = $Modules.Count - 1; $index -ge 0; $index--) {
        $module = $Modules[$index]
        try {
            # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_BEFORE_MODULE_REMOVE
            Remove-Module -ModuleInfo $module -Force -ErrorAction Stop
        }
        catch {
            $diagnostics.Add(
                "Remove-Module failed for '$($module.Name)' at '$($module.Path)': " +
                $_.Exception.Message)
            $stillLoaded = @(Get-Module -Name $module.Name -All -ErrorAction SilentlyContinue |
                Where-Object {
                    [string]::Equals(
                        $_.Path, $module.Path, [StringComparison]::OrdinalIgnoreCase)
                }).Count -ne 0
            if ($stillLoaded) {
                try { Remove-Module -ModuleInfo $module -Force -ErrorAction Stop }
                catch {
                    $diagnostics.Add(
                        "Remove-Module retry failed for '$($module.Name)' at '$($module.Path)': " +
                        $_.Exception.Message)
                }
            }
        }
        $remaining = @(Get-Module -Name $module.Name -All -ErrorAction SilentlyContinue |
            Where-Object {
                [string]::Equals(
                    $_.Path, $module.Path, [StringComparison]::OrdinalIgnoreCase)
            }).Count
        if ($remaining -ne 0) {
            $diagnostics.Add(
                "Authenticated recipe module '$($module.Name)' remains preloaded after cleanup.")
        }
    }
    return [string[]]$diagnostics.ToArray()
}

function Assert-M0PackageLauncherExactProperties {
    param($Value, [string[]] $Expected, [string] $Context)
    if ($Value -isnot [pscustomobject]) { throw "$Context must be a JSON object." }
    [string[]] $actual = @($Value.PSObject.Properties.Name)
    [string[]] $wanted = @($Expected)
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [Array]::Sort($wanted, [StringComparer]::Ordinal)
    if (($actual -join "`n") -cne ($wanted -join "`n")) {
        throw "$Context has an open, incomplete, or reordered property set."
    }
}

function Assert-M0PackageLauncherBuildResult {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $OutputRoot,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]] $SeenDirectories,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[object]] $DirectoryLeases,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[object]] $FileLeases
    )
    Assert-M0PackageLauncherExactProperties `
        $Value $script:BuildResultFields 'Authenticated package-v2 Build result'
    if ($Value.exit_code -isnot [long] -or $Value.exit_code -ne 0 -or
        $Value.package_root -isnot [string] -or
        $Value.receipt_path -isnot [string] -or
        $Value.manifest_path -isnot [string] -or
        $Value.receipt_sha256 -isnot [string] -or
        $Value.receipt_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Value.manifest_sha256 -isnot [string] -or
        $Value.manifest_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Value.entries -isnot [array]) {
        throw 'Authenticated package-v2 Build result has an invalid closed type or hash.'
    }
    $output = ConvertTo-M0PackageLauncherFullPath $OutputRoot
    if (-not [IO.Directory]::Exists($output) -or
        -not [string]::Equals(
            (ConvertTo-M0PackageLauncherFullPath $Value.package_root),
            $output,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Authenticated package-v2 Build result changed OutputRoot.'
    }
    $receipt = ConvertTo-M0PackageLauncherFullPath $Value.receipt_path
    $canonicalReceipt = ConvertTo-M0PackageLauncherFullPath (
        Join-Path $output 'evidence/qt-shell-composite-package-receipt.v2.json')
    if (-not [string]::Equals(
            $receipt, $canonicalReceipt, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Authenticated package-v2 Build result changed the canonical receipt path.'
    }
    $manifest = ConvertTo-M0PackageLauncherFullPath $Value.manifest_path
    if (-not (Test-M0PackageLauncherPathWithin $manifest $output)) {
        throw 'Authenticated package-v2 Build manifest escaped OutputRoot.'
    }
    Add-M0PackageLauncherDirectoryLeases `
        $output $RepositoryRoot $SeenDirectories $DirectoryLeases 'Build OutputRoot'
    $entries = [Collections.Generic.List[object]]::new()
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Value.entries)) {
        $pin = Assert-M0PackageLauncherPin $entry 'Authenticated package-v2 Build entry'
        if (-not $paths.Add([string]$pin.relative_path)) {
            throw 'Authenticated package-v2 Build result contains a duplicate or case-aliased path.'
        }
        $entryPath = Resolve-M0PackageLauncherContainedPath `
            $output $pin.relative_path 'Authenticated package-v2 Build entry'
        Add-M0PackageLauncherDirectoryLeases `
            $entryPath $RepositoryRoot $SeenDirectories $DirectoryLeases `
            'Authenticated package-v2 Build entry'
        $lease = Open-M0PackageLauncherFileLease `
            $entryPath $RepositoryRoot ([long]$pin.size_bytes) ([string]$pin.sha256) `
            'Authenticated package-v2 Build entry'
        $FileLeases.Add($lease)
        $entries.Add([pscustomobject][ordered]@{
                relative_path = [string]$pin.relative_path
                size_bytes = [long]$pin.size_bytes
                sha256 = [string]$pin.sha256
            })
    }
    if ($entries.Count -eq 0) {
        throw 'Authenticated package-v2 Build inventory is empty.'
    }
    $receiptRelative = [IO.Path]::GetRelativePath($output, $receipt).Replace('\', '/')
    if ($entries[-1].relative_path -cne $receiptRelative -or
        $entries[-1].sha256 -cne [string]$Value.receipt_sha256) {
        throw 'Authenticated package-v2 Build receipt is not its final inventory entry.'
    }
    $manifestRelative = [IO.Path]::GetRelativePath($output, $manifest).Replace('\', '/')
    $manifestMatches = @($entries | Where-Object {
            $_.relative_path -ceq $manifestRelative -and
            $_.sha256 -ceq [string]$Value.manifest_sha256
        })
    if ($manifestMatches.Count -ne 1) {
        throw 'Authenticated package-v2 Build manifest is absent from its inventory.'
    }
    return [pscustomobject][ordered]@{
        exit_code = [long]0
        package_root = $output
        receipt_path = $receipt
        receipt_sha256 = [string]$Value.receipt_sha256
        manifest_path = $manifest
        manifest_sha256 = [string]$Value.manifest_sha256
        entries = [object[]]$entries.ToArray()
    }
}

function Add-M0PackageLauncherInventoryLeases {
    param(
        [Parameter(Mandatory)] [string] $PackageRoot,
        [Parameter(Mandatory)] [object[]] $Entries,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.HashSet[string]] $SeenDirectories,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[object]] $DirectoryLeases,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[object]] $FileLeases,
        [Parameter(Mandatory)] [string] $Context
    )
    foreach ($pin in @($Entries)) {
        $entryPath = Resolve-M0PackageLauncherContainedPath `
            $PackageRoot $pin.relative_path "$Context entry"
        Add-M0PackageLauncherDirectoryLeases `
            $entryPath $RepositoryRoot $SeenDirectories $DirectoryLeases "$Context entry"
        $lease = Open-M0PackageLauncherFileLease `
            $entryPath $RepositoryRoot ([long]$pin.size_bytes) ([string]$pin.sha256) "$Context entry"
        $FileLeases.Add($lease)
    }
}

function Assert-M0PackageLauncherNativeResult {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [object[]] $ExpectedEntries
    )
    Assert-M0PackageLauncherExactProperties `
        $Value $script:NativeResultFields 'Authenticated native relocation result'
    if ($Value.exit_code -isnot [long] -or $Value.exit_code -ne 0 -or
        $Value.package_root -isnot [string] -or $Value.package_root -cne '.' -or
        $Value.payload_root -isnot [string] -or $Value.payload_root -cne 'app' -or
        $Value.evidence_root -isnot [string] -or $Value.evidence_root -cne 'evidence' -or
        $Value.entries -isnot [array] -or
        $Value.published_root_identity -isnot [string] -or
        $Value.published_root_identity -cnotmatch '^[0-9A-F]{8}:[0-9A-F]{16}$') {
        throw 'Authenticated native relocation result has an open or invalid shape.'
    }
    $closedEntries = [Collections.Generic.List[object]]::new()
    foreach ($entry in @($Value.entries)) {
        $pin = Assert-M0PackageLauncherPin $entry 'Authenticated native relocation entry'
        $closedEntries.Add([pscustomobject][ordered]@{
                relative_path = [string]$pin.relative_path
                size_bytes = [long]$pin.size_bytes
                sha256 = [string]$pin.sha256
            })
    }
    $expectedJson = @($ExpectedEntries) | ConvertTo-Json -Depth 10 -Compress
    $actualJson = @($closedEntries) | ConvertTo-Json -Depth 10 -Compress
    if ($actualJson -cne $expectedJson) {
        throw 'Authenticated native relocation result changed its closed source inventory.'
    }
    return [pscustomobject][ordered]@{
        exit_code = [long]0
        package_root = '.'
        payload_root = 'app'
        evidence_root = 'evidence'
        entries = [object[]]$closedEntries.ToArray()
        published_root_identity = [string]$Value.published_root_identity
    }
}

function Assert-M0PackageLauncherVerifierResult {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $PackageRoot,
        [Parameter(Mandatory)] [string] $ReceiptPath,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedCompositeReceiptSha256,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0PackageLauncherExactProperties $Value $script:VerifierResultFields $Context
    if ($Value.verified -isnot [bool] -or -not $Value.verified -or
        $Value.package_id -isnot [string] -or $Value.package_id -cne $script:PackageId -or
        $Value.contract_sha256 -isnot [string] -or
        $Value.contract_sha256 -cne $ExpectedContractSha256 -or
        $Value.composite_receipt_sha256 -isnot [string] -or
        $Value.composite_receipt_sha256 -cne $ExpectedCompositeReceiptSha256 -or
        $Value.manifest_sha256 -isnot [string] -or
        $Value.manifest_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Value.receipt_sha256 -isnot [string] -or
        $Value.manifest_path -isnot [string] -or
        $Value.receipt_sha256 -cne $ExpectedReceiptSha256) {
        throw "$Context changed a closed verification identity anchor."
    }
    $package = ConvertTo-M0PackageLauncherFullPath $PackageRoot
    $receipt = ConvertTo-M0PackageLauncherFullPath $ReceiptPath
    if (-not (Test-M0PackageLauncherPathWithin $receipt $package)) {
        throw "$Context receipt is outside its package root."
    }
    Assert-M0PackageLauncherSafeRelativePath $Value.manifest_path "$Context manifest_path"
    $manifest = Resolve-M0PackageLauncherContainedPath $package $Value.manifest_path `
        "$Context manifest_path"
    if (-not (Test-M0PackageLauncherPathWithin $manifest $package)) {
        throw "$Context manifest is outside its package root."
    }
    if ($Value.payload -isnot [array] -or $Value.evidence -isnot [array]) {
        throw "$Context payload and evidence must be closed arrays."
    }
    $entries = [Collections.Generic.List[object]]::new()
    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Value.payload) + @($Value.evidence)) {
        $pin = Assert-M0PackageLauncherPin $entry "$Context inventory entry"
        if (-not $paths.Add([string]$pin.relative_path)) {
            throw "$Context inventory contains a duplicate or case-aliased path."
        }
        $entries.Add([pscustomobject][ordered]@{
                relative_path = [string]$pin.relative_path
                size_bytes = [long]$pin.size_bytes
                sha256 = [string]$pin.sha256
            })
    }
    if ($entries.Count -eq 0) { throw "$Context inventory is empty." }
    $receiptRelative = [IO.Path]::GetRelativePath($package, $receipt).Replace('\', '/')
    if ($entries[-1].relative_path -cne $receiptRelative -or
        $entries[-1].sha256 -cne $ExpectedReceiptSha256) {
        throw "$Context receipt is not the final authenticated inventory entry."
    }
    $manifestMatches = @($entries | Where-Object {
            $_.relative_path -ceq [string]$Value.manifest_path -and
            $_.sha256 -ceq [string]$Value.manifest_sha256
        })
    if ($manifestMatches.Count -ne 1) {
        throw "$Context manifest is absent from the authenticated inventory."
    }
    return [pscustomobject][ordered]@{
        result = $Value
        manifest_path = $manifest
        entries = $entries.ToArray()
    }
}

function Assert-M0PackageLauncherSmokeResult {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $PackageRoot,
        [Parameter(Mandatory)] [string] $WorkspaceRoot,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedCompositeReceiptSha256
    )
    Assert-M0PackageLauncherExactProperties $Value $script:SmokeResultFields `
        'Authenticated package-v2 smoke result'
    $package = ConvertTo-M0PackageLauncherFullPath $PackageRoot
    $workspace = ConvertTo-M0PackageLauncherFullPath $WorkspaceRoot
    if ($Value.verified -isnot [bool] -or -not $Value.verified -or
        $Value.package_id -isnot [string] -or
        $Value.package_id -cne $script:PackageId -or
        $Value.contract_sha256 -isnot [string] -or
        $Value.contract_sha256 -cne $ExpectedContractSha256 -or
        $Value.composite_receipt_sha256 -isnot [string] -or
        $Value.composite_receipt_sha256 -cne $ExpectedCompositeReceiptSha256 -or
        $Value.manifest_sha256 -isnot [string] -or
        $Value.manifest_sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        $Value.receipt_sha256 -isnot [string] -or
        $Value.receipt_sha256 -cne $ExpectedReceiptSha256 -or
        $Value.package_root -isnot [string] -or
        $Value.workspace -isnot [string] -or
        -not [string]::Equals(
            (ConvertTo-M0PackageLauncherFullPath $Value.package_root),
            $package,
            [StringComparison]::OrdinalIgnoreCase) -or
        -not [string]::Equals(
            (ConvertTo-M0PackageLauncherFullPath $Value.workspace),
            $workspace,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Authenticated package-v2 smoke result changed a closed identity or path.'
    }
    if ($Value.journeys -isnot [array] -or @($Value.journeys).Count -ne 2) {
        throw 'Authenticated package-v2 smoke result must contain exactly two journeys.'
    }
    $expectedJourneys = @('tray-lifecycle', 'error-recovery')
    for ($index = 0; $index -lt $expectedJourneys.Count; $index++) {
        $journey = @($Value.journeys)[$index]
        Assert-M0PackageLauncherExactProperties $journey $script:JourneyResultFields `
            "Authenticated package-v2 smoke journey $index"
        $expectedWorking = ConvertTo-M0PackageLauncherFullPath (
            Join-Path $workspace $expectedJourneys[$index])
        $capture = [double]$journey.module_capture_ms
        if ($journey.journey -isnot [string] -or
            $journey.journey -cne $expectedJourneys[$index] -or
            $journey.working_directory -isnot [string] -or
            -not [string]::Equals(
                (ConvertTo-M0PackageLauncherFullPath $journey.working_directory),
                $expectedWorking,
                [StringComparison]::OrdinalIgnoreCase) -or
            $journey.exit_code -isnot [long] -or $journey.exit_code -ne 0 -or
            $journey.stdout -isnot [string] -or $journey.stdout -cne '' -or
            $journey.stderr -isnot [string] -or $journey.stderr -cne '' -or
            $journey.timed_out -isnot [bool] -or $journey.timed_out -or
            $journey.module_capture_ms -isnot [double] -or
            [double]::IsNaN($capture) -or [double]::IsInfinity($capture) -or
            $capture -lt 0 -or $capture -ge 500 -or
            $journey.survivor_count -isnot [long] -or $journey.survivor_count -ne 0) {
            throw "Authenticated package-v2 smoke journey $index is open or invalid."
        }
        if ($journey.module_paths -isnot [array] -or @($journey.module_paths).Count -eq 0) {
            throw "Authenticated package-v2 smoke journey $index has no closed module evidence."
        }
        $modulePaths = [Collections.Generic.HashSet[string]]::new(
            [StringComparer]::OrdinalIgnoreCase)
        [string[]] $canonicalModules = @()
        foreach ($modulePath in @($journey.module_paths)) {
            if ($modulePath -isnot [string] -or [string]::IsNullOrWhiteSpace($modulePath) -or
                -not [IO.Path]::IsPathFullyQualified($modulePath)) {
                throw "Authenticated package-v2 smoke journey $index has a noncanonical module path."
            }
            $canonical = ConvertTo-M0PackageLauncherFullPath $modulePath
            if (-not [string]::Equals(
                    $canonical, $modulePath, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Authenticated package-v2 smoke journey $index has a noncanonical module path."
            }
            if (-not $modulePaths.Add($canonical)) {
                throw "Authenticated package-v2 smoke journey $index repeats a module path."
            }
            $canonicalModules += $canonical
        }
        [string[]] $sortedModules = @($canonicalModules)
        [Array]::Sort($sortedModules, [StringComparer]::OrdinalIgnoreCase)
        if (($canonicalModules -join "`n") -cne ($sortedModules -join "`n")) {
            throw "Authenticated package-v2 smoke journey $index module paths are not canonical-sorted."
        }
    }
    if ($Value.limits -isnot [array] -or
        @($Value.limits | Where-Object { $_ -isnot [string] }).Count -ne 0 -or
        (@($Value.limits) -join "`n") -cne ($script:SmokeLimits -join "`n")) {
        throw 'Authenticated package-v2 smoke limitations are open or changed.'
    }
    return $Value
}

function Write-M0PackageLauncherSmokeReport {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [Collections.Generic.List[object]] $FileLeases
    )
    $path = ConvertTo-M0PackageLauncherFullPath $LiteralPath
    $repository = ConvertTo-M0PackageLauncherFullPath $RepositoryRoot
    $relativePath = [IO.Path]::GetRelativePath($repository, $path).Replace('\', '/')
    Assert-M0PackageLauncherSafeRelativePath $relativePath 'Smoke ReportPath'
    $json = $Value | ConvertTo-Json -Depth 100
    $json = $json.Replace("`r`n", "`n").Replace("`r", "`n") + "`n"
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($json)
    $sha256 = [Convert]::ToHexString(
        [Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    $handle = $null
    $stream = $null
    try {
        $handle = $script:LauncherNative::CreateNewFile($path)
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::ReadWrite, 4096, $false)
        $handle = $null
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
        # TEST-FIXTURE-INSERTION-POINT: PACKAGE_LAUNCHER_AFTER_REPORT_WRITE
        $identity = $script:LauncherNative::Inspect($stream.SafeFileHandle)
        $attributes = [IO.FileAttributes]$identity.FileAttributes
        $finalPath = ConvertTo-M0PackageLauncherFullPath (
            ConvertFrom-M0PackageLauncherFinalPath (
                $script:LauncherNative::GetFinalPath($stream.SafeFileHandle)))
        if (($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not [string]::Equals($finalPath, $path, [StringComparison]::OrdinalIgnoreCase) -or
            [long]$identity.NumberOfLinks -ne 1 -or
            [long]$stream.Length -ne [long]$bytes.Length -or
            (Get-M0PackageLauncherSha256FromStream $stream) -cne $sha256) {
            throw 'Smoke report authentication failed after writing the exact created file.'
        }
        $FileLeases.Add([pscustomobject][ordered]@{
                kind = 'file'
                path = $path
                stream = $stream
                identity = $identity
                size_bytes = [long]$bytes.Length
                sha256 = $sha256
                context = 'Smoke report'
            })
        $stream = $null
    }
    catch {
        $failure = $_
        $rollbackFailure = $null
        try {
            if ($null -ne $stream) {
                $script:LauncherNative::MarkDeleteOnClose($stream.SafeFileHandle)
            }
            elseif ($null -ne $handle) {
                $script:LauncherNative::MarkDeleteOnClose($handle)
            }
        }
        catch { $rollbackFailure = $_.Exception.Message }
        finally {
            if ($null -ne $stream) { $stream.Dispose() }
            elseif ($null -ne $handle) { $handle.Dispose() }
        }
        if ($null -ne $rollbackFailure) {
            $failure.Exception.Data['PackageV2ReportRollbackDiagnostics'] = $rollbackFailure
        }
        $PSCmdlet.ThrowTerminatingError($failure)
    }
    return [pscustomobject][ordered]@{
        relative_path = $relativePath
        size_bytes = [long]$bytes.Length
        sha256 = $sha256
    }
}

function Assert-M0PackageLauncherNoDuplicateJsonProperties {
    param([Text.Json.JsonElement] $Element, [string] $Context)
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $seen.Add($property.Name)) {
                throw "$Context contains duplicate JSON property '$($property.Name)'."
            }
            Assert-M0PackageLauncherNoDuplicateJsonProperties $property.Value "$Context.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($item in $Element.EnumerateArray()) {
            Assert-M0PackageLauncherNoDuplicateJsonProperties $item "$Context[$index]"
            $index++
        }
    }
}

function Read-M0PackageLauncherJsonLease {
    param([Parameter(Mandatory)] $Lease, [Parameter(Mandatory)] [string] $Context)
    $Lease.stream.Position = 0
    $buffer = [byte[]]::new([int]$Lease.stream.Length)
    $offset = 0
    while ($offset -lt $buffer.Length) {
        $read = $Lease.stream.Read($buffer, $offset, $buffer.Length - $offset)
        if ($read -eq 0) { throw "$Context ended before its retained size." }
        $offset += $read
    }
    $Lease.stream.Position = 0
    if ($buffer.Length -ge 3 -and $buffer[0] -eq 0xef -and $buffer[1] -eq 0xbb -and $buffer[2] -eq 0xbf) {
        throw "$Context contains a UTF-8 BOM."
    }
    $utf8 = [Text.UTF8Encoding]::new($false, $true)
    $text = $utf8.GetString($buffer)
    $document = [Text.Json.JsonDocument]::Parse($text)
    try { Assert-M0PackageLauncherNoDuplicateJsonProperties $document.RootElement '$' }
    finally { $document.Dispose() }
    try { return $text | ConvertFrom-Json -Depth 100 -DateKind String }
    catch { throw "$Context is not valid strict JSON: $($_.Exception.Message)" }
}

function Assert-M0PackageLauncherPin {
    param($Pin, [Parameter(Mandatory)] [string] $Context)
    Assert-M0PackageLauncherExactProperties $Pin @('relative_path', 'size_bytes', 'sha256') $Context
    if ($Pin.relative_path -isnot [string] -or
        $Pin.size_bytes -isnot [long] -or $Pin.size_bytes -lt 0 -or
        $Pin.sha256 -isnot [string] -or $Pin.sha256 -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context contains an invalid path, size, or SHA-256 pin."
    }
    Assert-M0PackageLauncherSafeRelativePath $Pin.relative_path "$Context relative_path"
    return $Pin
}

function Assert-M0PackageLauncherContract {
    param([Parameter(Mandatory)] $Contract)
    foreach ($name in @('schema_version', 'package_id', 'recipe', 'inputs')) {
        if ($null -eq $Contract.PSObject.Properties[$name]) {
            throw "Package-v2 contract lacks '$name'."
        }
    }
    if ($Contract.schema_version -isnot [long] -or $Contract.schema_version -ne 2 -or
        $Contract.package_id -isnot [string] -or $Contract.package_id -cne $script:PackageId) {
        throw 'Package-v2 contract schema or identity drifted.'
    }
    Assert-M0PackageLauncherExactProperties $Contract.recipe `
        @('files', 'core_exports', 'native_export', 'smoke_export') 'Package-v2 recipe'
    if ($Contract.recipe.files -isnot [array] -or
        @($Contract.recipe.files).Count -ne $script:ExpectedRecipePaths.Count) {
        throw 'Package-v2 recipe does not close exactly seven members.'
    }
    $pins = @{}
    foreach ($pinValue in @($Contract.recipe.files)) {
        $pin = Assert-M0PackageLauncherPin $pinValue 'Package-v2 recipe member'
        if ($pins.ContainsKey([string]$pin.relative_path)) {
            throw 'Package-v2 recipe contains a case-insensitive duplicate path.'
        }
        $pins[[string]$pin.relative_path] = $pin
    }
    foreach ($expected in $script:ExpectedRecipePaths) {
        if (-not $pins.ContainsKey($expected)) { throw "Package-v2 recipe omits '$expected'." }
    }
    [string[]] $coreExports = @($Contract.recipe.core_exports | ForEach-Object { [string] $_ })
    [string[]] $expectedCoreExports = @($script:CoreExports)
    [Array]::Sort($coreExports, [StringComparer]::Ordinal)
    [Array]::Sort($expectedCoreExports, [StringComparer]::Ordinal)
    if (($coreExports -join "`n") -cne ($expectedCoreExports -join "`n") -or
        $Contract.recipe.native_export -cne 'Invoke-M0QtShellCompositePackageNative' -or
        $Contract.recipe.smoke_export -cne 'Test-M0QtShellCompositePackageSmoke') {
        throw 'Package-v2 recipe export surface drifted.'
    }
    foreach ($name in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        if ($null -eq $Contract.inputs.PSObject.Properties[$name]) {
            throw "Package-v2 contract lacks input '$name'."
        }
        $null = Assert-M0PackageLauncherPin $Contract.inputs.$name "Package-v2 input '$name'"
    }
    return [pscustomobject][ordered]@{ document = $Contract; recipe_pins = $pins }
}

function Assert-M0PackageLauncherExports {
    param(
        [Parameter(Mandatory)] [Management.Automation.PSModuleInfo] $Module,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $ExpectedPath,
        [Parameter(Mandatory)] [string] $Context
    )
    if (-not [string]::Equals(
            (ConvertTo-M0PackageLauncherFullPath $Module.Path),
            (ConvertTo-M0PackageLauncherFullPath $ExpectedPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context was imported from an unauthenticated path."
    }
    [string[]] $actual = @($Module.ExportedFunctions.Keys)
    [string[]] $wanted = @($Expected)
    [Array]::Sort($actual, [StringComparer]::Ordinal)
    [Array]::Sort($wanted, [StringComparer]::Ordinal)
    if (($actual -join "`n") -cne ($wanted -join "`n")) {
        throw "$Context export surface drifted."
    }
}

$repository = ConvertTo-M0PackageLauncherFullPath (
    (Resolve-Path -LiteralPath $RepositoryRoot -ErrorAction Stop).Path)
if (-not [IO.Directory]::Exists($repository)) { throw 'RepositoryRoot is not a directory.' }
Assert-M0PackageLauncherNoReparseChain $repository $repository 'RepositoryRoot'

$contractFull = ConvertTo-M0PackageLauncherFullPath $ContractPath
$expectedContractPath = Resolve-M0PackageLauncherContainedPath $repository `
    $script:ContractRelativePath 'Package-v2 contract path'
if (-not [string]::Equals($contractFull, $expectedContractPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'ContractPath is not the canonical package-v2 contract path.'
}

$directoryLeases = [Collections.Generic.List[object]]::new()
$fileLeases = [Collections.Generic.List[object]]::new()
$seenDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$modules = [Collections.Generic.List[Management.Automation.PSModuleInfo]]::new()
$moduleCleanupDiagnostics = [Collections.Generic.List[string]]::new()
$launcherResult = $null
$pendingSmokeResult = $null
$primaryError = $null
try {
    Add-M0PackageLauncherDirectoryLeases $contractFull $repository $seenDirectories $directoryLeases `
        'Package-v2 contract'
    $contractSize = [long](Get-Item -LiteralPath $contractFull -Force -ErrorAction Stop).Length
    $contractLease = Open-M0PackageLauncherFileLease $contractFull $repository $contractSize `
        $ExpectedContractSha256 'Package-v2 contract'
    $fileLeases.Add($contractLease)
    $contractDocument = Read-M0PackageLauncherJsonLease $contractLease 'Package-v2 contract'
    $closed = Assert-M0PackageLauncherContract $contractDocument

    $recipePaths = @{}
    foreach ($relativePath in $script:ExpectedRecipePaths) {
        $pin = $closed.recipe_pins[$relativePath]
        $path = Resolve-M0PackageLauncherContainedPath $repository $relativePath `
            "Package-v2 recipe '$relativePath'"
        Add-M0PackageLauncherDirectoryLeases $path $repository $seenDirectories $directoryLeases `
            "Package-v2 recipe '$relativePath'"
        $lease = Open-M0PackageLauncherFileLease $path $repository ([long]$pin.size_bytes) `
            ([string]$pin.sha256) "Package-v2 recipe '$relativePath'"
        $fileLeases.Add($lease)
        $recipePaths[$relativePath] = $lease.path
    }

    $launcherRecipePath = [string]$recipePaths[$script:ExpectedRecipePaths[0]]
    if (-not [string]::Equals(
            $launcherRecipePath,
            (ConvertTo-M0PackageLauncherFullPath $PSCommandPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The running launcher is not its authenticated recipe member.'
    }

    $externalInputLeases = @{}
    foreach ($name in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        $pin = $closed.document.inputs.$name
        $path = Resolve-M0PackageLauncherContainedPath $repository $pin.relative_path `
            "Package-v2 input '$name'"
        Add-M0PackageLauncherDirectoryLeases $path $repository $seenDirectories $directoryLeases `
            "Package-v2 input '$name'"
        $lease = Open-M0PackageLauncherFileLease $path $repository ([long]$pin.size_bytes) `
            ([string]$pin.sha256) "Package-v2 input '$name'"
        $fileLeases.Add($lease)
        $externalInputLeases[$name] = $lease
    }
    if ($closed.document.inputs.composite_receipt.sha256 -cne $ExpectedCompositeReceiptSha256 -or
        $closed.document.inputs.qtbase_receipt.sha256 -cne $ExpectedQtBaseReceiptSha256) {
        throw 'Package-v2 external receipt anchor differs from the authenticated contract.'
    }
    if (-not [string]::Equals(
            (ConvertTo-M0PackageLauncherFullPath $CompositeReceiptPath),
            [string]$externalInputLeases.composite_receipt.path,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'CompositeReceiptPath differs from the authenticated contract input.'
    }

    $physicalRoots = [ordered]@{}
    switch ($PSCmdlet.ParameterSetName) {
        'Build' {
            $physicalRoots.BuildRoot = ConvertTo-M0PackageLauncherFullPath $BuildRoot
            $physicalRoots.OutputRoot = ConvertTo-M0PackageLauncherFullPath $OutputRoot
            $physicalRoots.ScratchRoot = ConvertTo-M0PackageLauncherFullPath $ScratchRoot
        }
        'Smoke' {
            $physicalRoots.PackageRoot = ConvertTo-M0PackageLauncherFullPath $PackageRoot
            $physicalRoots.SmokeWorkspaceRoot = ConvertTo-M0PackageLauncherFullPath $SmokeWorkspaceRoot
            $reportFull = ConvertTo-M0PackageLauncherFullPath $ReportPath
            $receiptFull = ConvertTo-M0PackageLauncherFullPath $ReceiptPath
            $canonicalReceipt = ConvertTo-M0PackageLauncherFullPath (
                Join-Path $physicalRoots.PackageRoot `
                    'evidence/qt-shell-composite-package-receipt.v2.json')
            if (-not [string]::Equals(
                    $receiptFull, $canonicalReceipt, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Smoke ReceiptPath is not the canonical package-local receipt.'
            }
            if (-not [IO.Directory]::Exists($physicalRoots.PackageRoot) -or
                -not [IO.File]::Exists($receiptFull)) {
                throw 'Smoke package root or canonical receipt is missing.'
            }
            if ((Test-M0PackageLauncherPathWithin `
                    $physicalRoots.SmokeWorkspaceRoot $physicalRoots.PackageRoot -AllowEqual) -or
                (Test-M0PackageLauncherPathWithin `
                    $physicalRoots.PackageRoot $physicalRoots.SmokeWorkspaceRoot -AllowEqual)) {
                throw 'Smoke workspace and package root must not overlap.'
            }
            if (-not (Test-M0PackageLauncherPathWithin $reportFull $repository) -or
                (Test-M0PackageLauncherPathWithin `
                    $reportFull $physicalRoots.PackageRoot -AllowEqual)) {
                throw 'Smoke ReportPath must be inside RepositoryRoot and outside PackageRoot.'
            }
            $reportRelative = [IO.Path]::GetRelativePath($repository, $reportFull).Replace('\', '/')
            Assert-M0PackageLauncherSafeRelativePath $reportRelative 'Smoke ReportPath'
            if ([IO.File]::Exists($reportFull) -or [IO.Directory]::Exists($reportFull)) {
                throw 'Smoke ReportPath target must be absent.'
            }
            $reportParent = [IO.Path]::GetDirectoryName($reportFull)
            if (-not [IO.Directory]::Exists($reportParent)) {
                throw 'Smoke ReportPath parent directory must already exist.'
            }
            Add-M0PackageLauncherDirectoryLeases `
                $reportParent $repository $seenDirectories $directoryLeases 'Smoke ReportPath parent'
        }
        'Relocate' {
            $physicalRoots.SourcePackageRoot = ConvertTo-M0PackageLauncherFullPath $SourcePackageRoot
            $physicalRoots.OutputRoot = ConvertTo-M0PackageLauncherFullPath $OutputRoot
            $physicalRoots.ScratchRoot = ConvertTo-M0PackageLauncherFullPath $ScratchRoot
            $receiptFull = ConvertTo-M0PackageLauncherFullPath $ReceiptPath
            $canonicalReceipt = ConvertTo-M0PackageLauncherFullPath (
                Join-Path $physicalRoots.SourcePackageRoot `
                    'evidence/qt-shell-composite-package-receipt.v2.json')
            if (-not [string]::Equals(
                    $receiptFull, $canonicalReceipt, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Relocate ReceiptPath is not the canonical source-package receipt.'
            }
            if (-not [IO.Directory]::Exists($physicalRoots.SourcePackageRoot) -or
                -not [IO.File]::Exists($receiptFull)) {
                throw 'Relocate source package root or canonical receipt is missing.'
            }
        }
        default { throw 'Unknown package-v2 launcher parameter set.' }
    }
    foreach ($entry in $physicalRoots.GetEnumerator()) {
        if (-not (Test-M0PackageLauncherPathWithin $entry.Value $repository)) {
            throw "$($entry.Key) must be a strict descendant of RepositoryRoot."
        }
        Add-M0PackageLauncherDirectoryLeases $entry.Value $repository $seenDirectories $directoryLeases `
            $entry.Key
    }
    $rootValues = @($physicalRoots.Values)
    for ($left = 0; $left -lt $rootValues.Count; $left++) {
        for ($right = $left + 1; $right -lt $rootValues.Count; $right++) {
            $leftOverlaps = Test-M0PackageLauncherPathWithin `
                $rootValues[$left] $rootValues[$right] -AllowEqual
            $rightOverlaps = Test-M0PackageLauncherPathWithin `
                $rootValues[$right] $rootValues[$left] -AllowEqual
            if ($leftOverlaps -or $rightOverlaps) {
                throw 'Package-v2 physical roots must be distinct non-overlapping paths.'
            }
        }
    }
    if ($PSCmdlet.ParameterSetName -in @('Smoke', 'Relocate')) {
        Add-M0PackageLauncherDirectoryLeases $receiptFull $repository $seenDirectories $directoryLeases `
            "$($PSCmdlet.ParameterSetName) receipt"
    }

    Assert-M0PackageLauncherLeasesUnchanged $directoryLeases $fileLeases $repository `
        'Package-v2 pre-import authentication'
    foreach ($name in @(
            'M0.QtShellCompositePackage',
            'M0.QtShellCompositePackageNative',
            'M0.QtShellCompositePackageSmoke')) {
        if (Get-Module -Name $name -All -ErrorAction SilentlyContinue) {
            throw "A package-v2 recipe module was preloaded: $name"
        }
    }

    $corePath = [string]$recipePaths[$script:CoreRelativePath]
    $nativePath = [string]$recipePaths[$script:NativeRelativePath]
    $smokePath = [string]$recipePaths[$script:SmokeRelativePath]
    $coreImported = @(Import-Module -Name $corePath -Force -PassThru)
    if ($coreImported.Count -ne 1) { throw 'Authenticated package-v2 core import was not singular.' }
    $coreModule = $coreImported[0]
    $modules.Add($coreModule)
    Assert-M0PackageLauncherExports $coreModule $script:CoreExports $corePath 'Package-v2 core'

    $nativeImported = @(Import-Module -Name $nativePath -Force -PassThru)
    if ($nativeImported.Count -ne 1) { throw 'Authenticated package-v2 native import was not singular.' }
    $nativeModule = $nativeImported[0]
    $modules.Add($nativeModule)
    Assert-M0PackageLauncherExports $nativeModule `
        @([string]$closed.document.recipe.native_export) $nativePath 'Package-v2 native module'

    $smokeImported = @(Import-Module -Name $smokePath -Force -PassThru)
    if ($smokeImported.Count -ne 1) { throw 'Authenticated package-v2 smoke import was not singular.' }
    $smokeModule = $smokeImported[0]
    $modules.Add($smokeModule)
    Assert-M0PackageLauncherExports $smokeModule `
        @([string]$closed.document.recipe.smoke_export) $smokePath 'Package-v2 smoke module'

    Assert-M0PackageLauncherLeasesUnchanged $directoryLeases $fileLeases $repository `
        'Package-v2 post-import authentication'
    $commonArguments = [ordered]@{
        ContractPath = $contractLease.path
        ExpectedContractSha256 = $ExpectedContractSha256
        CompositeReceiptPath = [string]$externalInputLeases.composite_receipt.path
        ExpectedCompositeReceiptSha256 = $ExpectedCompositeReceiptSha256
        ExpectedQtBaseReceiptSha256 = $ExpectedQtBaseReceiptSha256
        RepositoryRoot = $repository
    }
    $verifyReceiptCommand = $coreModule.ExportedFunctions['Assert-M0QtShellCompositePackageReceipt']
    if ($null -eq $verifyReceiptCommand) {
        throw 'Authenticated package-v2 receipt verifier export is missing.'
    }

    switch ($PSCmdlet.ParameterSetName) {
        'Build' {
            $invokeCommand = $coreModule.ExportedFunctions['Invoke-M0QtShellCompositePackage']
            if ($null -eq $invokeCommand) {
                throw 'Authenticated package-v2 core invocation export is missing.'
            }
            [string[]] $actualInvokeParameters = @($invokeCommand.Parameters.Keys | Where-Object {
                    $_ -notin $script:CommonParameters
                })
            [string[]] $expectedInvokeParameters = @($script:CoreInvokeParameters)
            [Array]::Sort($actualInvokeParameters, [StringComparer]::Ordinal)
            [Array]::Sort($expectedInvokeParameters, [StringComparer]::Ordinal)
            if (($actualInvokeParameters -join "`n") -cne ($expectedInvokeParameters -join "`n")) {
                throw 'Authenticated package-v2 core physical boundary drifted.'
            }
            $invokeArguments = [ordered]@{}
            foreach ($entry in $commonArguments.GetEnumerator()) {
                $invokeArguments[$entry.Key] = $entry.Value
            }
            $invokeArguments.BuildRoot = [string]$physicalRoots.BuildRoot
            $invokeArguments.OutputRoot = [string]$physicalRoots.OutputRoot
            $invokeArguments.ScratchRoot = [string]$physicalRoots.ScratchRoot
            $coreResults = @(& $invokeCommand @invokeArguments)
            if ($coreResults.Count -ne 1 -or $null -eq $coreResults[0]) {
                throw 'Authenticated package-v2 core returned an empty or open result stream.'
            }
            $launcherResult = Assert-M0PackageLauncherBuildResult `
                -Value $coreResults[0] `
                -OutputRoot $physicalRoots.OutputRoot `
                -RepositoryRoot $repository `
                -SeenDirectories $seenDirectories `
                -DirectoryLeases $directoryLeases `
                -FileLeases $fileLeases
        }
        'Smoke' {
            $smokeCommand = $smokeModule.ExportedFunctions['Test-M0QtShellCompositePackageSmoke']
            if ($null -eq $smokeCommand) {
                throw 'Authenticated package-v2 smoke invocation export is missing.'
            }
            $assertVerifierResultCommand = Get-Command `
                -Name Assert-M0PackageLauncherVerifierResult -CommandType Function
            $addInventoryLeasesCommand = Get-Command `
                -Name Add-M0PackageLauncherInventoryLeases -CommandType Function
            $smokeInventoryBinding = $null
            $verifyAction = {
                param($Phase)
                $arguments = [ordered]@{
                    LiteralPath = $receiptFull
                    ExpectedReceiptSha256 = $ExpectedReceiptSha256
                }
                foreach ($entry in $commonArguments.GetEnumerator()) {
                    $arguments[$entry.Key] = $entry.Value
                }
                $verifyResults = @(& $verifyReceiptCommand @arguments)
                if ($verifyResults.Count -ne 1 -or $null -eq $verifyResults[0]) {
                    throw "Authenticated smoke $Phase verifier returned an open result stream."
                }
                $closed = & $assertVerifierResultCommand `
                    -Value $verifyResults[0] `
                    -PackageRoot $physicalRoots.PackageRoot `
                    -ReceiptPath $receiptFull `
                    -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
                    -ExpectedContractSha256 $ExpectedContractSha256 `
                    -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
                    -Context "Authenticated smoke $Phase package"
                $binding = [pscustomobject][ordered]@{
                    manifest_sha256 = [string]$closed.result.manifest_sha256
                    entries = [object[]]$closed.entries
                } | ConvertTo-Json -Depth 10 -Compress
                if ($null -eq $smokeInventoryBinding) {
                    & $addInventoryLeasesCommand `
                        -PackageRoot $physicalRoots.PackageRoot `
                        -Entries ([object[]]$closed.entries) `
                        -RepositoryRoot $repository `
                        -SeenDirectories $seenDirectories `
                        -DirectoryLeases $directoryLeases `
                        -FileLeases $fileLeases `
                        -Context 'Authenticated smoke inventory'
                    $smokeInventoryBinding = $binding
                }
                elseif ($binding -cne $smokeInventoryBinding) {
                    throw "Authenticated smoke $Phase inventory drifted from pre-verification."
                }
                return $closed.result
            }.GetNewClosure()
            $workspaceFactory = { [string]$physicalRoots.SmokeWorkspaceRoot }.GetNewClosure()
            $smokeArguments = [ordered]@{
                PackageRoot = [string]$physicalRoots.PackageRoot
                ReceiptPath = $receiptFull
                ExpectedReceiptSha256 = $ExpectedReceiptSha256
                ContractPath = $contractLease.path
                ExpectedContractSha256 = $ExpectedContractSha256
                CompositeReceiptPath = [string]$externalInputLeases.composite_receipt.path
                ExpectedCompositeReceiptSha256 = $ExpectedCompositeReceiptSha256
                RepositoryRoot = $repository
                ExpectedQtBaseReceiptSha256 = $ExpectedQtBaseReceiptSha256
                VerifyReceiptAction = $verifyAction
                WorkspaceFactory = $workspaceFactory
            }
            $smokeResults = @(& $smokeCommand @smokeArguments)
            if ($smokeResults.Count -ne 1 -or $null -eq $smokeResults[0]) {
                throw 'Authenticated package-v2 smoke returned an empty or open result stream.'
            }
            $closedSmokeResult = Assert-M0PackageLauncherSmokeResult `
                -Value $smokeResults[0] `
                -PackageRoot $physicalRoots.PackageRoot `
                -WorkspaceRoot $physicalRoots.SmokeWorkspaceRoot `
                -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
                -ExpectedContractSha256 $ExpectedContractSha256 `
                -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256
            $pendingSmokeResult = $closedSmokeResult
        }
        'Relocate' {
            $sourceVerifyArguments = [ordered]@{
                LiteralPath = $receiptFull
                ExpectedReceiptSha256 = $ExpectedReceiptSha256
            }
            foreach ($entry in $commonArguments.GetEnumerator()) {
                $sourceVerifyArguments[$entry.Key] = $entry.Value
            }
            $sourceResults = @(& $verifyReceiptCommand @sourceVerifyArguments)
            if ($sourceResults.Count -ne 1 -or $null -eq $sourceResults[0]) {
                throw 'Authenticated source-package verifier returned an open result stream.'
            }
            $sourceClosed = Assert-M0PackageLauncherVerifierResult `
                -Value $sourceResults[0] `
                -PackageRoot $physicalRoots.SourcePackageRoot `
                -ReceiptPath $receiptFull `
                -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
                -ExpectedContractSha256 $ExpectedContractSha256 `
                -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
                -Context 'Authenticated source package'
            $nativeCommand = $nativeModule.ExportedFunctions['Invoke-M0QtShellCompositePackageNative']
            if ($null -eq $nativeCommand) {
                throw 'Authenticated package-v2 native publisher export is missing.'
            }
            $nativeResults = @(& $nativeCommand `
                    -SourceRoot $physicalRoots.SourcePackageRoot `
                    -OutputRoot $physicalRoots.OutputRoot `
                    -ScratchRoot $physicalRoots.ScratchRoot `
                    -Entries $sourceClosed.entries)
            if ($nativeResults.Count -ne 1 -or $null -eq $nativeResults[0]) {
                throw 'Authenticated package-v2 native relocation did not return one successful result.'
            }
            $null = Assert-M0PackageLauncherNativeResult `
                -Value $nativeResults[0] `
                -ExpectedEntries ([object[]]$sourceClosed.entries)
            $destinationReceipt = ConvertTo-M0PackageLauncherFullPath (
                Join-Path $physicalRoots.OutputRoot `
                    'evidence/qt-shell-composite-package-receipt.v2.json')
            $destinationVerifyArguments = [ordered]@{
                LiteralPath = $destinationReceipt
                ExpectedReceiptSha256 = $ExpectedReceiptSha256
            }
            foreach ($entry in $commonArguments.GetEnumerator()) {
                $destinationVerifyArguments[$entry.Key] = $entry.Value
            }
            $destinationResults = @(& $verifyReceiptCommand @destinationVerifyArguments)
            if ($destinationResults.Count -ne 1 -or $null -eq $destinationResults[0]) {
                throw 'Authenticated relocated-package verifier returned an open result stream.'
            }
            $destinationClosed = Assert-M0PackageLauncherVerifierResult `
                -Value $destinationResults[0] `
                -PackageRoot $physicalRoots.OutputRoot `
                -ReceiptPath $destinationReceipt `
                -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
                -ExpectedContractSha256 $ExpectedContractSha256 `
                -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
                -Context 'Authenticated relocated package'
            $sourceBinding = [pscustomobject][ordered]@{
                manifest_sha256 = [string]$sourceClosed.result.manifest_sha256
                entries = $sourceClosed.entries
            } | ConvertTo-Json -Depth 10 -Compress
            $destinationBinding = [pscustomobject][ordered]@{
                manifest_sha256 = [string]$destinationClosed.result.manifest_sha256
                entries = $destinationClosed.entries
            } | ConvertTo-Json -Depth 10 -Compress
            if ($sourceBinding -cne $destinationBinding) {
                throw 'Relocated package differs from its authenticated source package.'
            }
            $launcherResult = [pscustomobject][ordered]@{
                exit_code = [long]0
                package_root = [string]$physicalRoots.OutputRoot
                receipt_path = $destinationReceipt
                receipt_sha256 = $ExpectedReceiptSha256
                manifest_path = [string]$destinationClosed.manifest_path
                manifest_sha256 = [string]$destinationClosed.result.manifest_sha256
                entries = [object[]]$destinationClosed.entries
            }
        }
        default { throw 'Unknown authenticated package-v2 invocation mode.' }
    }
    Assert-M0PackageLauncherLeasesUnchanged $directoryLeases $fileLeases $repository `
        'Package-v2 post-invocation authentication'
}
catch { $primaryError = $_ }
finally {
    foreach ($diagnostic in @(Remove-M0PackageLauncherImportedModules $modules)) {
        $moduleCleanupDiagnostics.Add($diagnostic)
    }
    try {
        if ($fileLeases.Count -gt 0 -or $directoryLeases.Count -gt 0) {
            Assert-M0PackageLauncherLeasesUnchanged $directoryLeases $fileLeases $repository `
                'Package-v2 launcher final authentication'
        }
    }
    catch {
        if ($null -eq $primaryError) { $primaryError = $_ }
        else {
            $primaryError.Exception.Data['PackageV2FinalAuthenticationDiagnostics'] = `
                $_.Exception.Message
        }
    }
    try {
        if ($null -eq $primaryError -and $moduleCleanupDiagnostics.Count -eq 0 -and
            $null -ne $pendingSmokeResult) {
            $reportPin = Write-M0PackageLauncherSmokeReport `
                -Value $pendingSmokeResult `
                -LiteralPath $reportFull `
                -RepositoryRoot $repository `
                -FileLeases $fileLeases
            $launcherResult = [pscustomobject][ordered]@{
                result = $pendingSmokeResult
                report = $reportPin
            }
        }
    }
    catch {
        if ($null -eq $primaryError) { $primaryError = $_ }
        else {
            $primaryError.Exception.Data['PackageV2ReportPublicationDiagnostics'] = `
                $_.Exception.Message
        }
    }
    finally { Close-M0PackageLauncherLeases $fileLeases $directoryLeases }
}

if ($moduleCleanupDiagnostics.Count -ne 0) {
    $diagnosticText = $moduleCleanupDiagnostics -join ' | '
    if ($null -eq $primaryError) {
        $exception = [InvalidOperationException]::new(
            'Authenticated package-v2 module cleanup failed: ' + $diagnosticText)
        $primaryError = [Management.Automation.ErrorRecord]::new(
            $exception,
            'PackageV2ModuleCleanupFailure',
            [Management.Automation.ErrorCategory]::ResourceBusy,
            $null)
    }
    $primaryError.Exception.Data['PackageV2ModuleCleanupDiagnostics'] = $diagnosticText
}
if ($null -ne $primaryError) {
    $PSCmdlet.ThrowTerminatingError($primaryError)
}

$launcherResult
