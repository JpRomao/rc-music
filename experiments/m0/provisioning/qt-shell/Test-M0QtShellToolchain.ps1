[CmdletBinding(DefaultParameterSetName = 'Probe')]
param(
    [Parameter(Mandatory)]
    [string] $RepositoryRoot,

    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedContractSha256,

    [Parameter(ParameterSetName = 'Probe')]
    [Parameter(Mandatory, ParameterSetName = 'VerifyReceipt')]
    [string] $ReceiptPath,

    [Parameter(Mandatory, ParameterSetName = 'VerifyReceipt')]
    [ValidatePattern('^[0-9a-f]{64}$')]
    [string] $ExpectedReceiptSha256
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0QT001 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$preloaded = @(Get-Module -Name 'M0.QtShellProvisioning')
if ($preloaded.Count -ne 0) {
    throw [IO.InvalidDataException]::new('M0QT040 Test-M0QtShellToolchain requires a clean PowerShell process; a preloaded M0.QtShellProvisioning module was preserved and no module was removed or reloaded.')
}
if ('M0QtShellNativeV1.FileIdentity' -as [type]) {
    throw [IO.InvalidDataException]::new('M0QT040 Test-M0QtShellToolchain requires a clean PowerShell process; the preloaded native trust type is forbidden.')
}
if ('M0QtShellBootstrapNativeV1.FileIdentity' -as [type]) {
    throw [IO.InvalidDataException]::new('M0QT040 Test-M0QtShellToolchain requires a clean PowerShell process; the bootstrap native trust type is preloaded.')
}
$foreignNativeAssembly = $false
foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
    if ($assembly.GetName().Name -cin @('M0QtShellNativeV1', 'M0QtShellBootstrapNativeV1')) {
        $foreignNativeAssembly = $true
        break
    }
    try {
        if (@($assembly.GetTypes() | Where-Object {
                    $namespace = [string]$_.Namespace
                    $namespace -ceq 'M0QtShellNativeV1' -or
                    $namespace.StartsWith('M0QtShellNativeV1.', [StringComparison]::Ordinal) -or
                    $namespace -ceq 'M0QtShellBootstrapNativeV1' -or
                    $namespace.StartsWith('M0QtShellBootstrapNativeV1.', [StringComparison]::Ordinal)
                }).Count -gt 0) {
            $foreignNativeAssembly = $true
            break
        }
    }
    catch [Reflection.ReflectionTypeLoadException] {
        if (@($_.Exception.Types | Where-Object {
                    if ($null -eq $_) { return $false }
                    $namespace = [string]$_.Namespace
                    $namespace -ceq 'M0QtShellNativeV1' -or
                    $namespace.StartsWith('M0QtShellNativeV1.', [StringComparison]::Ordinal) -or
                    $namespace -ceq 'M0QtShellBootstrapNativeV1' -or
                    $namespace.StartsWith('M0QtShellBootstrapNativeV1.', [StringComparison]::Ordinal)
                }).Count -gt 0) {
            $foreignNativeAssembly = $true
            break
        }
    }
}
if ($foreignNativeAssembly) {
    throw [IO.InvalidDataException]::new('M0QT040 Test-M0QtShellToolchain requires a clean PowerShell process; a preloaded native trust namespace or assembly is forbidden.')
}

if ($IsWindows) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Text;

namespace M0QtShellBootstrapNativeV1 {
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
        private const uint FILE_LIST_DIRECTORY = 0x0001;
        private const uint FILE_READ_ATTRIBUTES = 0x0080;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(IntPtr handle, out BY_HANDLE_FILE_INFORMATION info);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(IntPtr handle, StringBuilder path, uint pathLength, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(
            string fileName,
            uint desiredAccess,
            uint shareMode,
            IntPtr securityAttributes,
            uint creationDisposition,
            uint flagsAndAttributes,
            IntPtr templateFile);

        private static BY_HANDLE_FILE_INFORMATION GetInfo(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out info)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return info;
        }
        public static SafeFileHandle OpenDirectoryWithoutDeleteSharing(string path) {
            var handle = CreateFileW(
                path,
                FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                IntPtr.Zero,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                IntPtr.Zero);
            if (handle.IsInvalid) {
                int error = Marshal.GetLastWin32Error();
                handle.Dispose();
                throw new Win32Exception(error);
            }
            return handle;
        }
        public static uint GetAttributes(SafeFileHandle handle) { return GetInfo(handle).FileAttributes; }
        public static uint GetLinkCount(SafeFileHandle handle) { return GetInfo(handle).NumberOfLinks; }
        public static string GetIdentity(SafeFileHandle handle) {
            var info = GetInfo(handle);
            return info.VolumeSerialNumber.ToString("X8") + ":" + info.FileIndexHigh.ToString("X8") + info.FileIndexLow.ToString("X8");
        }
        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle.DangerousGetHandle(), buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return buffer.ToString();
        }
    }
}
'@
}

$literalRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$requestedRepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
if (-not $requestedRepositoryRoot.Equals($literalRepositoryRoot, $comparison)) {
    throw [IO.InvalidDataException]::new('M0QT011 RepositoryRoot differs from the repository that owns this validator.')
}

function Get-M0QtShellBootstrapHash {
    param([Parameter(Mandatory)][IO.Stream] $Stream)

    $position = $Stream.Position
    try {
        $Stream.Position = 0
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { return ([Convert]::ToHexString($algorithm.ComputeHash($Stream))).ToLowerInvariant() }
        finally { $algorithm.Dispose() }
    }
    finally { $Stream.Position = $position }
}

function Assert-M0QtShellBootstrapPath {
    param([Parameter(Mandatory)][string] $LiteralPath)

    if (-not [IO.Path]::IsPathFullyQualified($LiteralPath)) {
        throw [IO.InvalidDataException]::new('M0QT020 bootstrap paths must be absolute and literal.')
    }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $current = $full
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw [IO.InvalidDataException]::new('M0QT020 bootstrap trust rejects reparse/link paths before module import.')
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent.Equals($current, $comparison)) { break }
        $current = $parent
    }
    return $full
}

function Open-M0QtShellBootstrapFile {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedSha256,
        [Nullable[long]] $ExpectedSizeBytes
    )

    $full = Assert-M0QtShellBootstrapPath -LiteralPath $LiteralPath
    $stream = $null
    try {
        $stream = [IO.FileStream]::new($full, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $identity = $null
        if ($IsWindows) {
            if ([M0QtShellBootstrapNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1) {
                throw [IO.InvalidDataException]::new('M0QT011 bootstrap trust requires single-link leaf identities.')
            }
            $final = [M0QtShellBootstrapNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle)
            if ($final.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) { $final = '\\' + $final.Substring(8) }
            elseif ($final.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { $final = $final.Substring(4) }
            $final = [IO.Path]::GetFullPath($final)
            if (-not $final.Equals($full, $comparison)) {
                throw [IO.InvalidDataException]::new('M0QT011 bootstrap leaf final path differs from its literal slot.')
            }
            $identity = [M0QtShellBootstrapNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle)
        }
        if ($null -ne $ExpectedSizeBytes -and ($ExpectedSizeBytes -lt 0 -or $stream.Length -ne $ExpectedSizeBytes)) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract leaf size drifted before module import.')
        }
        $sha256 = Get-M0QtShellBootstrapHash -Stream $stream
        if ($sha256 -cne $ExpectedSha256) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract leaf hash drifted before module import.')
        }
        return [pscustomobject]@{
            stream = $stream; final_path = $full; sha256 = $sha256; size_bytes = [long]$stream.Length; identity = $identity
        }
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        throw
    }
}

function ConvertFrom-M0QtShellBootstrapFinalPath {
    param([Parameter(Mandatory)][string] $FinalPath)

    if ($FinalPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return [IO.Path]::GetFullPath('\\' + $FinalPath.Substring(8))
    }
    if ($FinalPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return [IO.Path]::GetFullPath($FinalPath.Substring(4))
    }
    return [IO.Path]::GetFullPath($FinalPath)
}

function Open-M0QtShellBootstrapDirectory {
    param([Parameter(Mandatory)][string] $LiteralPath)

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0QT040 directory identity pinning requires Windows.')
    }

    $full = Assert-M0QtShellBootstrapPath -LiteralPath $LiteralPath
    $handle = $null
    try {
        $handle = [M0QtShellBootstrapNativeV1.FileIdentity]::OpenDirectoryWithoutDeleteSharing($full)
        $attributes = [M0QtShellBootstrapNativeV1.FileIdentity]::GetAttributes($handle)
        if (($attributes -band [uint32][IO.FileAttributes]::Directory) -eq 0 -or
            ($attributes -band [uint32][IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new('M0QT020 bootstrap trust requires a physical directory identity.')
        }
        $final = ConvertFrom-M0QtShellBootstrapFinalPath -FinalPath `
            ([M0QtShellBootstrapNativeV1.FileIdentity]::GetFinalPath($handle))
        if (-not $final.Equals($full, $comparison)) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap directory final path differs from its literal slot.')
        }
        return [pscustomobject]@{
            safe_handle = $handle
            final_path = $full
            identity = [M0QtShellBootstrapNativeV1.FileIdentity]::GetIdentity($handle)
        }
    }
    catch {
        if ($null -ne $handle) { $handle.Dispose() }
        throw
    }
}

function Assert-M0QtShellBootstrapDirectoryIdentity {
    param([Parameter(Mandatory)][object] $TrustedDirectory)

    $attributes = [M0QtShellBootstrapNativeV1.FileIdentity]::GetAttributes($TrustedDirectory.safe_handle)
    if (($attributes -band [uint32][IO.FileAttributes]::Directory) -eq 0 -or
        ($attributes -band [uint32][IO.FileAttributes]::ReparsePoint) -ne 0 -or
        [M0QtShellBootstrapNativeV1.FileIdentity]::GetIdentity($TrustedDirectory.safe_handle) -cne [string]$TrustedDirectory.identity) {
        throw [IO.InvalidDataException]::new('M0QT011 a retained bootstrap directory identity changed during validation.')
    }
    $final = ConvertFrom-M0QtShellBootstrapFinalPath -FinalPath `
        ([M0QtShellBootstrapNativeV1.FileIdentity]::GetFinalPath($TrustedDirectory.safe_handle))
    if (-not $final.Equals([string]$TrustedDirectory.final_path, $comparison)) {
        throw [IO.InvalidDataException]::new('M0QT011 a retained bootstrap directory final path changed during validation.')
    }
}

function Assert-M0QtShellBootstrapFileIdentity {
    param([Parameter(Mandatory)][object] $TrustedFile)

    if ($TrustedFile.stream.Length -ne [long]$TrustedFile.size_bytes -or
        (Get-M0QtShellBootstrapHash -Stream $TrustedFile.stream) -cne [string]$TrustedFile.sha256) {
        throw [IO.InvalidDataException]::new('M0QT011 a retained bootstrap leaf changed during validation.')
    }
    if ($IsWindows) {
        if ([M0QtShellBootstrapNativeV1.FileIdentity]::GetLinkCount($TrustedFile.stream.SafeFileHandle) -ne 1 -or
            [M0QtShellBootstrapNativeV1.FileIdentity]::GetIdentity($TrustedFile.stream.SafeFileHandle) -cne [string]$TrustedFile.identity) {
            throw [IO.InvalidDataException]::new('M0QT011 a retained bootstrap leaf identity changed during validation.')
        }
        $final = ConvertFrom-M0QtShellBootstrapFinalPath -FinalPath `
            ([M0QtShellBootstrapNativeV1.FileIdentity]::GetFinalPath($TrustedFile.stream.SafeFileHandle))
        if (-not $final.Equals([string]$TrustedFile.final_path, $comparison)) {
            throw [IO.InvalidDataException]::new('M0QT011 a retained bootstrap leaf final path changed during validation.')
        }
    }
}

function Assert-M0QtShellBootstrapContractShape {
    param(
        [Parameter(Mandatory)][object] $Contract,
        [Parameter(Mandatory)][string[]] $ExpectedPaths
    )

    if ($Contract -isnot [pscustomobject] -or
        (@($Contract.PSObject.Properties.Name) | ConvertTo-Json -Compress) -cne (@('files') | ConvertTo-Json -Compress)) {
        throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract root must contain exactly files.')
    }
    if ($Contract.files -isnot [array] -or $Contract.files.Count -ne $ExpectedPaths.Count) {
        throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract must contain exactly eight file entries.')
    }

    $pathComparer = if ($IsWindows) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal }
    $seenPaths = [Collections.Generic.HashSet[string]]::new($pathComparer)
    for ($index = 0; $index -lt $Contract.files.Count; $index++) {
        $entry = $Contract.files[$index]
        if ($entry -isnot [pscustomobject] -or
            (@($entry.PSObject.Properties.Name) | ConvertTo-Json -Compress) -cne `
                (@('path', 'sha256', 'size_bytes') | ConvertTo-Json -Compress)) {
            throw [IO.InvalidDataException]::new('M0QT011 each bootstrap contract entry must contain exactly path, sha256, and size_bytes.')
        }
        if ($entry.path -isnot [string] -or $entry.sha256 -isnot [string] -or $entry.size_bytes -isnot [long]) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract entry types are invalid.')
        }

        $relative = [string]$entry.path
        $segments = @($relative -split '/', -1)
        if ($relative -notmatch '^[A-Za-z0-9._/-]+$' -or
            $relative.StartsWith('/', [StringComparison]::Ordinal) -or
            $relative.Contains('\') -or
            @($segments | Where-Object { $_ -ceq '' -or $_ -ceq '.' -or $_ -ceq '..' }).Count -ne 0) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract contains an unsafe relative path.')
        }
        if (-not $seenPaths.Add($relative)) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract contains duplicate physical path slots.')
        }
        if ($entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $entry.size_bytes -lt 0) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract hash or size metadata is invalid.')
        }
        if ($relative -cne $ExpectedPaths[$index]) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract inventory is not exact and ordered.')
        }
    }
}

function Read-M0QtShellBootstrapText {
    param([Parameter(Mandatory)][object] $TrustedFile)

    if ($TrustedFile.size_bytes -gt [int]::MaxValue) {
        throw [IO.InvalidDataException]::new('M0QT011 bootstrap document exceeds the bounded size.')
    }
    $position = $TrustedFile.stream.Position
    try {
        $TrustedFile.stream.Position = 0
        $bytes = [byte[]]::new([int]$TrustedFile.size_bytes)
        $TrustedFile.stream.ReadExactly($bytes)
        return [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    }
    finally { $TrustedFile.stream.Position = $position }
}

$retainedBootstrapFiles = [Collections.Generic.List[object]]::new()
$retainedBootstrapDirectories = [Collections.Generic.List[object]]::new()
try {
    $contractRoot = Assert-M0QtShellBootstrapPath -LiteralPath (Join-Path $literalRepositoryRoot 'experiments\m0\provisioning\qt-shell')
    if ($IsWindows) {
        $ancestorPaths = [Collections.Generic.List[string]]::new()
        $currentDirectory = $contractRoot
        while (-not [string]::IsNullOrWhiteSpace($currentDirectory)) {
            $ancestorPaths.Add($currentDirectory)
            $parentDirectory = [IO.Path]::GetDirectoryName($currentDirectory)
            if ([string]::IsNullOrEmpty($parentDirectory) -or $parentDirectory.Equals($currentDirectory, $comparison)) {
                break
            }
            $currentDirectory = $parentDirectory
        }
        for ($index = $ancestorPaths.Count - 1; $index -ge 0; $index--) {
            $retainedBootstrapDirectories.Add(
                (Open-M0QtShellBootstrapDirectory -LiteralPath $ancestorPaths[$index]))
        }
    }

    $contractPath = Assert-M0QtShellBootstrapPath -LiteralPath (Join-Path $contractRoot 'qt-shell-toolchain-contract.v1.json')
    $sidecarPath = Assert-M0QtShellBootstrapPath -LiteralPath "$contractPath.sha256"
    if (-not [IO.File]::Exists($contractPath) -or -not [IO.File]::Exists($sidecarPath)) {
        throw [IO.InvalidDataException]::new('M0QT011 provisioning contract or its sidecar is missing before trusted module import.')
    }
    $contractFile = Open-M0QtShellBootstrapFile -LiteralPath $contractPath -ExpectedSha256 $ExpectedContractSha256
    $retainedBootstrapFiles.Add($contractFile)
    $expectedSidecarBytes = [Text.UTF8Encoding]::new($false).GetBytes($ExpectedContractSha256 + "`n")
    $expectedSidecarSha256 = ([Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($expectedSidecarBytes))).ToLowerInvariant()
    $sidecarFile = Open-M0QtShellBootstrapFile -LiteralPath $sidecarPath `
        -ExpectedSha256 $expectedSidecarSha256 -ExpectedSizeBytes 65
    $retainedBootstrapFiles.Add($sidecarFile)
    $sidecarText = Read-M0QtShellBootstrapText -TrustedFile $sidecarFile
    if ($sidecarText -cne ($ExpectedContractSha256 + "`n")) {
        throw [IO.InvalidDataException]::new('M0QT011 provisioning contract sidecar differs from the external expected anchor.')
    }

    $expectedPaths = @(
        'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.schema.json',
        'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-evidence.schema.json',
        'experiments/m0/provisioning/qt-shell/qt-shell-stage-manifest.schema.json',
        'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.v1.json',
        'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-golden.v1.json',
        'experiments/m0/provisioning/qt-shell/qt-shell-stage-manifest.v1.json',
        'experiments/m0/provisioning/qt-shell/M0.QtShellProvisioning.psm1',
        'experiments/m0/provisioning/qt-shell/Test-M0QtShellToolchain.ps1'
    )

    $contractText = Read-M0QtShellBootstrapText -TrustedFile $contractFile
    try {
        $contract = $contractText | ConvertFrom-Json -Depth 100 -DateKind String
    }
    catch {
        throw [IO.InvalidDataException]::new('M0QT011 provisioning contract is not valid JSON.', $_.Exception)
    }
    Assert-M0QtShellBootstrapContractShape -Contract $contract -ExpectedPaths $expectedPaths
    if ($contractText -cne (($contract | ConvertTo-Json -Compress -Depth 100) + "`n")) {
        throw [IO.InvalidDataException]::new('M0QT011 provisioning contract bytes are not canonical compact JSON.')
    }

    $validatedPhysicalLeaves = [Collections.Generic.List[object]]::new()
    foreach ($entry in @($contract.files)) {
        $relative = [string]$entry.path
        $physical = Assert-M0QtShellBootstrapPath -LiteralPath ([IO.Path]::GetFullPath((Join-Path $literalRepositoryRoot ($relative -replace '/', '\'))))
        if (-not $physical.StartsWith($literalRepositoryRoot + [IO.Path]::DirectorySeparatorChar, $comparison)) {
            throw [IO.InvalidDataException]::new('M0QT011 bootstrap contract leaf escaped RepositoryRoot.')
        }
        $validatedPhysicalLeaves.Add([pscustomobject]@{ entry = $entry; physical = $physical })
    }
    foreach ($validatedLeaf in $validatedPhysicalLeaves) {
        $leaf = Open-M0QtShellBootstrapFile -LiteralPath $validatedLeaf.physical `
            -ExpectedSha256 ([string]$validatedLeaf.entry.sha256) `
            -ExpectedSizeBytes ([long]$validatedLeaf.entry.size_bytes)
        $retainedBootstrapFiles.Add($leaf)
    }

    $modulePath = [IO.Path]::GetFullPath((Join-Path $contractRoot 'M0.QtShellProvisioning.psm1'))
    foreach ($directory in $retainedBootstrapDirectories) {
        Assert-M0QtShellBootstrapDirectoryIdentity -TrustedDirectory $directory
    }
    foreach ($file in $retainedBootstrapFiles) {
        Assert-M0QtShellBootstrapFileIdentity -TrustedFile $file
    }
    Import-Module -Name $modulePath -ErrorAction Stop
    foreach ($directory in $retainedBootstrapDirectories) {
        Assert-M0QtShellBootstrapDirectoryIdentity -TrustedDirectory $directory
    }
    foreach ($file in $retainedBootstrapFiles) {
        Assert-M0QtShellBootstrapFileIdentity -TrustedFile $file
    }
    $loaded = @(Get-Module -Name 'M0.QtShellProvisioning')
    if ($loaded.Count -ne 1 -or -not [IO.Path]::GetFullPath($loaded[0].Path).Equals($modulePath, $comparison)) {
        throw [IO.InvalidDataException]::new('M0QT040 the provisioning module did not bind to the exact trusted repository path.')
    }
    if ((@($loaded[0].ExportedCommands.Keys | Sort-Object) | ConvertTo-Json -Compress) -cne `
        (@('Assert-M0QtShellReceipt', 'Test-M0QtShellToolchain') | ConvertTo-Json -Compress)) {
        throw [IO.InvalidDataException]::new('M0QT040 the provisioning module export surface differs from the closed public API.')
    }

    if ($PSCmdlet.ParameterSetName -ceq 'VerifyReceipt') {
        $receipt = M0.QtShellProvisioning\Assert-M0QtShellReceipt `
            -RepositoryRoot $literalRepositoryRoot `
            -ExpectedContractSha256 $ExpectedContractSha256 `
            -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
            -ReceiptPath $ReceiptPath
    }
    else {
        $arguments = @{
            RepositoryRoot = $literalRepositoryRoot
            ExpectedContractSha256 = $ExpectedContractSha256
        }
        if ($PSBoundParameters.ContainsKey('ReceiptPath')) { $arguments.ReceiptPath = $ReceiptPath }
        $receipt = M0.QtShellProvisioning\Test-M0QtShellToolchain @arguments
    }
    foreach ($directory in $retainedBootstrapDirectories) {
        Assert-M0QtShellBootstrapDirectoryIdentity -TrustedDirectory $directory
    }
    foreach ($file in $retainedBootstrapFiles) {
        Assert-M0QtShellBootstrapFileIdentity -TrustedFile $file
    }
    $receipt | ConvertTo-Json -Compress -Depth 100
}
finally {
    foreach ($file in $retainedBootstrapFiles) {
        try { $file.stream.Dispose() } catch { }
    }
    for ($index = $retainedBootstrapDirectories.Count - 1; $index -ge 0; $index--) {
        try { $retainedBootstrapDirectories[$index].safe_handle.Dispose() } catch { }
    }
}
