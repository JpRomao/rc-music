#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ExpectedToolIds = @(
    'cmd', 'vsdevcmd', 'cmake', 'cmcldeps', 'ninja', 'cl', 'link', 'lib', 'dumpbin', 'rc', 'mt', 'midl',
    'windows-h', 'dwrite-h', 'dwrite-2-h', 'dwrite-3-h', 'd2d1-h', 'd2d1-1-h', 'd3d11-h', 'dxgi1-6-h',
    'sdkddkver-h', 'sal-h', 'corecrt-h', 'ucrt-lib', 'kernel32-lib', 'user32-lib', 'gdi32-lib', 'dwrite-lib',
    'd2d1-lib', 'd3d11-lib', 'dxgi-lib', 'windowscodecs-lib', 'advapi32-lib', 'ole32-lib', 'shell32-lib', 'uuid-lib'
)
$script:ForbiddenEnvironment = @(
    'CL', '_CL_', 'LINK', '_LINK_',
    'CFLAGS', 'CXXFLAGS', 'LDFLAGS',
    'CMAKE_PREFIX_PATH', 'CMAKE_TOOLCHAIN_FILE',
    'QMAKESPEC', 'QT_HOST_PATH', 'VULKAN_SDK',
    'PYTHONHOME', 'PYTHONPATH', 'PERL5LIB'
)
$script:ExpectedFeatureNames = @(
    'shared',
    'debug_and_release',
    'force_debug_info',
    'bundled_freetype',
    'bundled_harfbuzz',
    'bundled_pcre2',
    'bundled_zlib',
    'bundled_png',
    'bundled_jpeg',
    'opengl',
    'vulkan',
    'directwrite',
    'directwrite3',
    'directwrite_colrv1',
    'direct2d',
    'direct2d1_1',
    'accessibility',
    'systemtrayicon',
    'openssl_runtime',
    'schannel',
    'intelcet',
    'msvc_obj_debug_info'
)
$script:ExpectedFeatures = [ordered]@{
    shared = $true
    debug_and_release = $true
    force_debug_info = $true
    bundled_freetype = $true
    bundled_harfbuzz = $true
    bundled_pcre2 = $true
    bundled_zlib = $true
    bundled_png = $true
    bundled_jpeg = $true
    opengl = 'dynamic'
    vulkan = $true
    directwrite = $true
    directwrite3 = $true
    directwrite_colrv1 = $true
    direct2d = $true
    direct2d1_1 = $true
    accessibility = $true
    systemtrayicon = $true
    openssl_runtime = $false
    schannel = $true
    intelcet = $true
    msvc_obj_debug_info = $true
}
$script:ExpectedConfigureArguments = @(
    '-prefix', '${INSTALL_ROOT}',
    '-shared',
    '-debug-and-release',
    '-force-debug-info',
    '-headersclean',
    '-nomake', 'examples',
    '-nomake', 'tests',
    '-no-ltcg',
    '-intelcet',
    '-qt-zlib',
    '-qt-pcre',
    '-qt-freetype',
    '-qt-harfbuzz',
    '-qt-libpng',
    '-qt-libjpeg',
    '-opengl', 'dynamic',
    '-vulkan',
    '-no-openssl',
    '-schannel',
    '-no-sql-psql',
    '-cmake-generator', 'Ninja Multi-Config',
    '--',
    '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
    '-DVulkan_INCLUDE_DIR=${VULKAN_INCLUDE}',
    '-DFEATURE_vulkan=ON',
    '-DFEATURE_directwrite=ON',
    '-DFEATURE_directwrite3=ON',
    '-DFEATURE_directwritecolrv1=ON',
    '-DFEATURE_direct2d=ON',
    '-DFEATURE_direct2d1_1=ON',
    '-DFEATURE_accessibility=ON',
    '-DFEATURE_systemtrayicon=ON',
    '-DFEATURE_schannel=ON',
    '-DFEATURE_openssl=OFF',
    '-DFEATURE_intelcet=ON',
    '-DFEATURE_msvc_obj_debug_info=ON',
    '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF',
    '-DBUILD_WITH_PCH=OFF',
    '-DQT_BUILD_TESTS=OFF',
    '-DQT_INSTALL_CONFIG_INFO_FILES=ON',
    '-DQT_GENERATE_SBOM=OFF',
    '-DQT_GENERATE_SOURCE_SBOM=OFF',
    '-DCMAKE_MAKE_PROGRAM=${NINJA}',
    '-DCMAKE_C_COMPILER=${CL}',
    '-DCMAKE_CXX_COMPILER=${CL}',
    '-DCMAKE_RC_COMPILER=${RC}',
    '-DCMAKE_MT=${MT}'
)
$script:ExpectedBuildArguments = @('--build', '${BUILD_ROOT}', '--config', 'RelWithDebInfo', '--parallel', '8')
$script:ExpectedInstallArguments = @('--install', '${BUILD_ROOT}', '--config', 'RelWithDebInfo')
$script:ExpectedArtifacts = @(
    'bin/Qt6Core.dll',
    'bin/Qt6Gui.dll',
    'bin/Qt6Network.dll',
    'bin/Qt6OpenGL.dll',
    'bin/Qt6Widgets.dll',
    'plugins/generic/qtuiotouchplugin.dll',
    'plugins/imageformats/qgif.dll',
    'plugins/imageformats/qico.dll',
    'plugins/imageformats/qjpeg.dll',
    'plugins/networkinformation/qnetworklistmanager.dll',
    'plugins/platforms/qoffscreen.dll',
    'plugins/platforms/qwindows.dll',
    'plugins/styles/qmodernwindowsstyle.dll',
    'plugins/tls/qcertonlybackend.dll',
    'plugins/tls/qschannelbackend.dll'
)
$script:ReceiptLimits = @(
    'diagnostic M0 rebuild only',
    'does not approve a Qt toolkit decision',
    'does not approve redistribution or publication',
    'does not replace the official SDK or historical packages'
)

if ($IsWindows) {
    if ($null -ne ('M0QtBaseRefreshNativeV1.FileIdentity' -as [type])) {
        throw 'QtBase refresh native helper was preloaded before module import.'
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace M0QtBaseRefreshNativeV1 {
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

    public static class FileIdentity {
        private const uint GENERIC_READ = 0x80000000;
        private const uint GENERIC_WRITE = 0x40000000;
        private const uint DELETE_ACCESS = 0x00010000;
        private const uint FILE_LIST_DIRECTORY = 0x00000001;
        private const uint FILE_READ_ATTRIBUTES = 0x00000080;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint CREATE_NEW = 1;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        private const int FileDispositionInfo = 4;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle handle, int infoClass,
            IntPtr information, uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtSetInformationFile(SafeFileHandle handle, out IO_STATUS_BLOCK ioStatusBlock,
            IntPtr information, uint bufferSize, int fileInformationClass);

        [DllImport("ntdll.dll")]
        private static extern uint RtlNtStatusToDosError(int status);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(IntPtr handle, out BY_HANDLE_FILE_INFORMATION info);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(IntPtr handle, StringBuilder path,
            uint pathLength, uint flags);

        public static SafeFileHandle OpenDirectoryWithoutDeleteSharing(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle CreateNewReadWriteDelete(string path) {
            var handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS,
                FILE_SHARE_READ, IntPtr.Zero, CREATE_NEW, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static string GetIdentity(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out info)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return info.VolumeSerialNumber.ToString("X8") + ":" +
                info.FileIndexHigh.ToString("X8") + info.FileIndexLow.ToString("X8");
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

        public static void RenameNoReplaceRelative(SafeFileHandle handle, SafeFileHandle directory,
            string destinationName) {
            if (String.IsNullOrEmpty(destinationName) ||
                destinationName.IndexOfAny(new[] { '\\', '/', ':', '\0' }) >= 0) {
                throw new ArgumentException("Destination must be one relative leaf.", "destinationName");
            }
            byte[] name = Encoding.Unicode.GetBytes(destinationName);
            int rootOffset = IntPtr.Size;
            int lengthOffset = rootOffset + IntPtr.Size;
            int nameOffset = lengthOffset + sizeof(uint);
            int unalignedTotal = nameOffset + name.Length + sizeof(char);
            int total = ((unalignedTotal + IntPtr.Size - 1) / IntPtr.Size) * IntPtr.Size;
            IntPtr buffer = Marshal.AllocHGlobal(total);
            try {
                for (int i = 0; i < total; i++) { Marshal.WriteByte(buffer, i, 0); }
                Marshal.WriteByte(buffer, 0, 0);
                Marshal.WriteIntPtr(buffer, rootOffset, directory.DangerousGetHandle());
                Marshal.WriteInt32(buffer, lengthOffset, name.Length);
                Marshal.Copy(name, 0, IntPtr.Add(buffer, nameOffset), name.Length);
                IO_STATUS_BLOCK ioStatusBlock;
                int status = NtSetInformationFile(handle, out ioStatusBlock, buffer,
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
    }
}
'@
}

function Assert-M0ExactProperties {
    param(
        [Parameter(Mandatory)] $Value,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $Context
    )

    if ($null -eq $Value -or $Value -isnot [psobject]) {
        throw "$Context must be a JSON object."
    }
    $actual = @($Value.PSObject.Properties.Name)
    if ($actual.Count -ne $Expected.Count) {
        throw "$Context must contain the exact property set '$($Expected -join ',')'."
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($actual[$index] -cne $Expected[$index]) {
            throw "$Context property order or field set is not exact. Expected '$($Expected -join ',')'."
        }
    }
}

function Assert-M0String {
    param($Value, [string] $Context, [switch] $AllowEmpty)
    if ($Value -isnot [string] -or (-not $AllowEmpty -and [string]::IsNullOrWhiteSpace($Value))) {
        throw "$Context must be a non-empty string."
    }
}

function Assert-M0Digest {
    param($Value, [string] $Context)
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context must be a lowercase SHA-256 digest."
    }
}

function Assert-M0Commit {
    param($Value, [string] $Context)
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{40}$') {
        throw "$Context must be a full lowercase Git commit SHA."
    }
}

function Assert-M0PositiveSize {
    param($Value, [string] $Context)
    if ($Value -isnot [long] -or $Value -le 0) {
        throw "$Context must be a positive byte count."
    }
}

function Assert-M0SafeRelativePath {
    param($Value, [string] $Context)
    Assert-M0String $Value $Context
    if ([IO.Path]::IsPathRooted($Value) -or $Value.Contains(':') -or $Value.Contains([char]0)) {
        throw "$Context must be a safe relative path."
    }
    $segments = @($Value -split '[\\/]')
    if ($segments.Count -eq 0 -or @($segments | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
        throw "$Context must not contain empty, current, or parent segments."
    }
}

function Assert-M0StringSequence {
    param(
        [Parameter(Mandatory)] $Actual,
        [Parameter(Mandatory)] [string[]] $Expected,
        [Parameter(Mandatory)] [string] $Context
    )
    $values = @($Actual)
    if ($values.Count -ne $Expected.Count) {
        throw "$Context must match the exact expected sequence."
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($values[$index] -isnot [string] -or $values[$index] -cne $Expected[$index]) {
            throw "$Context drifted at index $index."
        }
    }
}

function Assert-M0NoDuplicateJsonProperties {
    param([Parameter(Mandatory)] [Text.Json.JsonElement] $Element, [string] $Context = '$')
    switch ($Element.ValueKind) {
        ([Text.Json.JsonValueKind]::Object) {
            $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($property in $Element.EnumerateObject()) {
                if (-not $names.Add($property.Name)) {
                    throw "JSON contract contains duplicate property '$($property.Name)' at $Context."
                }
                Assert-M0NoDuplicateJsonProperties -Element $property.Value -Context "$Context.$($property.Name)"
            }
        }
        ([Text.Json.JsonValueKind]::Array) {
            $index = 0
            foreach ($item in $Element.EnumerateArray()) {
                Assert-M0NoDuplicateJsonProperties -Element $item -Context "$Context[$index]"
                $index++
            }
        }
    }
}

function Read-M0ClosedJson {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    try {
        $text = [IO.File]::ReadAllText($LiteralPath, [Text.Encoding]::UTF8)
        $json = [Text.Json.JsonDocument]::Parse($text)
        try { Assert-M0NoDuplicateJsonProperties -Element $json.RootElement }
        finally { $json.Dispose() }
        return $text | ConvertFrom-Json -Depth 100 -DateKind String
    }
    catch {
        throw [InvalidDataException]::new("$Context is not valid closed JSON: $($_.Exception.Message)", $_.Exception)
    }
}

function Get-M0Sha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-M0Sha256FromStream {
    param([Parameter(Mandatory)] [IO.FileStream] $Stream)
    $position = $Stream.Position
    try {
        $Stream.Position = 0
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try { return [Convert]::ToHexString($algorithm.ComputeHash($Stream)).ToLowerInvariant() }
        finally { $algorithm.Dispose() }
    }
    finally { $Stream.Position = $position }
}

function ConvertFrom-M0FinalPath {
    param([Parameter(Mandatory)] [string] $Path)
    if ($Path.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $Path.Substring(8)
    }
    if ($Path.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring(4)
    }
    return $Path
}

function ConvertTo-M0ExtendedPath {
    param([Parameter(Mandatory)] [string] $Path)
    $full = [IO.Path]::GetFullPath($Path)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) {
        return '\\?\UNC\' + $full.Substring(2)
    }
    return '\\?\' + $full
}

function ConvertTo-M0NormalizedDirectoryPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $volumeRoot = [IO.Path]::GetPathRoot($full)
    if ($full -ceq $volumeRoot) { return $volumeRoot }
    return $full.TrimEnd([IO.Path]::DirectorySeparatorChar)
}

function Assert-M0NoReparseChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $boundary = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
    $currentPath = [IO.Path]::GetFullPath($LiteralPath)
    $prefix = if ([string]::Equals($boundary, [IO.Path]::GetPathRoot($boundary), [StringComparison]::OrdinalIgnoreCase)) { $boundary } else { $boundary + [IO.Path]::DirectorySeparatorChar }
    if (-not [string]::Equals($currentPath, $boundary, [StringComparison]::OrdinalIgnoreCase) -and
        -not $currentPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context is outside its physical boundary root."
    }
    while ($true) {
        $item = Get-Item -LiteralPath $currentPath -Force
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "$Context contains a reparse-point file or ancestor: $currentPath"
        }
        $normalized = if ($item.PSIsContainer) {
            ConvertTo-M0NormalizedDirectoryPath $item.FullName
        }
        else {
            [IO.Path]::GetFullPath($item.FullName)
        }
        if ([string]::Equals($normalized, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($normalized)
        if ([string]::IsNullOrEmpty($parent) -or $parent.Length -lt $boundary.Length) {
            throw "$Context physical ancestry escaped its boundary root."
        }
        $currentPath = $parent
    }
}

function Publish-M0QtBaseRefreshReceipt {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $RunRoot
    )
    if (-not $IsWindows) { throw 'QtBase refresh receipt publication is proven only on Windows.' }
    $final = [IO.Path]::GetFullPath($LiteralPath)
    $parent = ConvertTo-M0NormalizedDirectoryPath ([IO.Path]::GetDirectoryName($final))
    if (-not [IO.Directory]::Exists($parent)) { throw 'Receipt parent must exist before retained publication.' }
    Assert-M0NoReparseChain -LiteralPath $parent -BoundaryRoot $RunRoot -Context 'Receipt evidence parent'
    if ([IO.File]::Exists($final) -or [IO.Directory]::Exists($final)) {
        throw 'Receipt destination already exists; overwrite is forbidden.'
    }

    $temp = Join-Path $parent ('.qtbase-refresh-receipt-' + [Guid]::NewGuid().ToString('N') + '.pending')
    $parentHandle = $null
    $stream = $null
    $published = $false
    try {
        $parentHandle = [M0QtBaseRefreshNativeV1.FileIdentity]::OpenDirectoryWithoutDeleteSharing((ConvertTo-M0ExtendedPath $parent))
        $parentIdentity = [M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($parentHandle)
        $parentFinal = ConvertTo-M0NormalizedDirectoryPath (ConvertFrom-M0FinalPath `
            ([M0QtBaseRefreshNativeV1.FileIdentity]::GetFinalPath($parentHandle)))
        if (-not [string]::Equals($parentFinal, $parent, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Receipt evidence parent handle resolved through a reparse point or different identity.'
        }

        $tempHandle = [M0QtBaseRefreshNativeV1.FileIdentity]::CreateNewReadWriteDelete((ConvertTo-M0ExtendedPath $temp))
        $stream = [IO.FileStream]::new($tempHandle, [IO.FileAccess]::ReadWrite, 4096, $false)
        if ([M0QtBaseRefreshNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1) {
            throw 'Receipt temporary file must have exactly one link.'
        }
        $tempIdentity = [M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle)
        $tempFinal = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ([M0QtBaseRefreshNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($tempFinal, $temp, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Receipt temporary handle resolved to a different path.'
        }

        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
        $expectedSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
        if ($stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $expectedSha256) {
            throw 'Receipt temporary bytes differ after durable flush.'
        }
        Assert-M0NoReparseChain -LiteralPath $parent -BoundaryRoot $RunRoot -Context 'Receipt evidence parent'
        if ([M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($parentHandle) -cne $parentIdentity -or
            [M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle) -cne $tempIdentity) {
            throw 'Receipt retained parent or temporary file identity changed before publication.'
        }

        [M0QtBaseRefreshNativeV1.FileIdentity]::RenameNoReplaceRelative(
            $stream.SafeFileHandle,
            $parentHandle,
            [IO.Path]::GetFileName($final)
        )
        if ([M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($parentHandle) -cne $parentIdentity -or
            [M0QtBaseRefreshNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle) -cne $tempIdentity -or
            [M0QtBaseRefreshNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1) {
            throw 'Receipt retained identities changed after publication.'
        }
        $finalFromHandle = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ([M0QtBaseRefreshNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($finalFromHandle, $final, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Receipt final handle resolved to a different path after publication.'
        }
        Assert-M0NoReparseChain -LiteralPath $final -BoundaryRoot $RunRoot -Context 'Published receipt'
        if ($stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $expectedSha256) {
            throw 'Receipt final bytes changed after publication.'
        }
        $published = $true
        return [pscustomobject]@{ sha256 = $expectedSha256; size_bytes = [long]$Bytes.Length }
    }
    finally {
        $cleanupFailure = $null
        if (-not $published -and $null -ne $stream) {
            try { [M0QtBaseRefreshNativeV1.FileIdentity]::MarkDeleteOnClose($stream.SafeFileHandle) }
            catch { $cleanupFailure = $_ }
        }
        if ($null -ne $stream) { try { $stream.Dispose() } catch { } }
        if ($null -ne $parentHandle) { try { $parentHandle.Dispose() } catch { } }
        if ($null -ne $cleanupFailure) {
            throw [IO.InvalidDataException]::new('Receipt failure cleanup could not retain delete-on-close semantics.', $cleanupFailure.Exception)
        }
    }
}

function Assert-M0PhysicalFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $Context,
        [string] $BoundaryRoot
    )
    if (-not [IO.File]::Exists($LiteralPath)) { throw "$Context input file is missing: $LiteralPath" }
    $item = Get-Item -LiteralPath $LiteralPath -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$Context input is a reparse point." }
    if (-not [string]::IsNullOrEmpty($BoundaryRoot)) {
        Assert-M0NoReparseChain -LiteralPath $item.FullName -BoundaryRoot $BoundaryRoot -Context $Context
    }
    if ([long]$item.Length -ne $ExpectedSizeBytes) { throw "$Context input size pin mismatch." }
    $actualSha256 = Get-M0Sha256 $item.FullName
    if ($actualSha256 -cne $ExpectedSha256) { throw "$Context input SHA-256 pin mismatch." }
    return $item.FullName
}

function Resolve-M0ContainedPath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0SafeRelativePath $RelativePath $Context
    $fullRoot = ConvertTo-M0NormalizedDirectoryPath $Root
    $candidate = [IO.Path]::GetFullPath((Join-Path $fullRoot $RelativePath))
    $prefix = $fullRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context escapes its pinned root."
    }
    return $candidate
}

function Assert-M0FeatureObject {
    param([Parameter(Mandatory)] $Features, [Parameter(Mandatory)] [string] $Context)
    Assert-M0ExactProperties $Features $script:ExpectedFeatureNames $Context
    foreach ($name in $script:ExpectedFeatureNames) {
        $actual = $Features.PSObject.Properties[$name].Value
        $expected = $script:ExpectedFeatures[$name]
        if (-not [object]::Equals($actual, $expected)) {
            throw "$Context feature '$name' drifted from the required value '$expected'."
        }
    }
}

function Assert-M0ConfigurationObject {
    param([Parameter(Mandatory)] $Configuration, [Parameter(Mandatory)] [string] $Context, [switch] $Observation)
    $properties = if ($Observation) {
        @('generator', 'arguments', 'build_arguments', 'install_arguments', 'installed_configuration', 'artifacts', 'features')
    }
    else {
        @('generator', 'arguments', 'build_arguments', 'install_arguments', 'installed_configuration', 'expected_artifacts', 'expected_features')
    }
    Assert-M0ExactProperties $Configuration $properties $Context
    if ($Configuration.generator -cne 'Ninja Multi-Config') { throw "$Context generator must be Ninja Multi-Config." }
    Assert-M0StringSequence $Configuration.arguments $script:ExpectedConfigureArguments "$Context arguments"
    Assert-M0StringSequence $Configuration.build_arguments $script:ExpectedBuildArguments "$Context build arguments"
    Assert-M0StringSequence $Configuration.install_arguments $script:ExpectedInstallArguments "$Context install arguments"
    if ($Configuration.installed_configuration -cne 'RelWithDebInfo') {
        throw "$Context installed configuration must be RelWithDebInfo."
    }
    if ($Observation) {
        Assert-M0StringSequence $Configuration.artifacts $script:ExpectedArtifacts "$Context artifacts"
        Assert-M0FeatureObject $Configuration.features "$Context features"
    }
    else {
        Assert-M0StringSequence $Configuration.expected_artifacts $script:ExpectedArtifacts "$Context expected artifacts"
        Assert-M0FeatureObject $Configuration.expected_features "$Context expected features"
    }
}

function Assert-M0QtBaseRefreshContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedSha256
    )

    Assert-M0Digest $ExpectedSha256 'Expected contract SHA-256'
    if (-not [IO.File]::Exists($LiteralPath)) { throw "QtBase refresh contract is missing: $LiteralPath" }
    $contractItem = Get-Item -LiteralPath $LiteralPath -Force
    if ($contractItem.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'QtBase refresh contract cannot be a reparse point.' }
    $actualSha256 = Get-M0Sha256 $contractItem.FullName
    if ($actualSha256 -cne $ExpectedSha256) { throw 'QtBase refresh contract SHA-256 mismatch.' }

    $contract = Read-M0ClosedJson -LiteralPath $contractItem.FullName -Context 'QtBase refresh contract'
    Assert-M0ExactProperties $contract @('schema_version', 'refresh_id', 'product', 'inputs', 'tools', 'environment', 'isolation', 'configuration') 'contract'
    if ($contract.schema_version -isnot [long] -or $contract.schema_version -ne 1) {
        throw 'Contract schema_version must be the integer 1.'
    }
    if ($contract.refresh_id -cne 'm0.2-qtbase-freetype-backport-v1') { throw 'Contract refresh_id is not accepted.' }

    Assert-M0ExactProperties $contract.product @('qt_version', 'architecture', 'compiler_abi') 'contract.product'
    if ($contract.product.qt_version -cne '6.11.2' -or
        $contract.product.architecture -cne 'x86_64' -or
        $contract.product.compiler_abi -cne 'msvc2022_64') {
        throw 'Contract product identity drifted.'
    }

    Assert-M0ExactProperties $contract.inputs @('qtbase_source', 'freetype_patch', 'vulkan_headers') 'contract.inputs'
    Assert-M0ExactProperties $contract.inputs.qtbase_source @('relative_path', 'size_bytes', 'sha256', 'commit_sha') 'contract.inputs.qtbase_source'
    Assert-M0SafeRelativePath $contract.inputs.qtbase_source.relative_path 'QtBase source path'
    Assert-M0PositiveSize $contract.inputs.qtbase_source.size_bytes 'QtBase source size'
    Assert-M0Digest $contract.inputs.qtbase_source.sha256 'QtBase source SHA-256'
    Assert-M0Commit $contract.inputs.qtbase_source.commit_sha 'QtBase source commit'
    if ($contract.inputs.qtbase_source.commit_sha -cne 'ef55f427f2c8b410d34f8a7681020a3000cf6866') {
        throw 'QtBase source commit is not the 6.11.2 supplier revision.'
    }

    Assert-M0ExactProperties $contract.inputs.freetype_patch @(
        'relative_path', 'size_bytes', 'sha256', 'target_relative_path', 'target_before_sha256', 'target_after_sha256'
    ) 'contract.inputs.freetype_patch'
    Assert-M0SafeRelativePath $contract.inputs.freetype_patch.relative_path 'FreeType patch path'
    Assert-M0PositiveSize $contract.inputs.freetype_patch.size_bytes 'FreeType patch size'
    Assert-M0Digest $contract.inputs.freetype_patch.sha256 'FreeType patch SHA-256'
    Assert-M0SafeRelativePath $contract.inputs.freetype_patch.target_relative_path 'FreeType patch target'
    if ($contract.inputs.freetype_patch.target_relative_path -cne 'src/3rdparty/freetype/src/truetype/ttgxvar.c') {
        throw 'FreeType patch must have the single accepted target.'
    }
    Assert-M0Digest $contract.inputs.freetype_patch.target_before_sha256 'FreeType target before SHA-256'
    Assert-M0Digest $contract.inputs.freetype_patch.target_after_sha256 'FreeType target after SHA-256'
    if ($contract.inputs.freetype_patch.target_before_sha256 -ceq $contract.inputs.freetype_patch.target_after_sha256) {
        throw 'FreeType target before and after identities must differ.'
    }

    Assert-M0ExactProperties $contract.inputs.vulkan_headers @(
        'relative_path', 'size_bytes', 'sha256', 'commit_sha', 'header_relative_path', 'header_sha256', 'license_relative_path', 'license_sha256'
    ) 'contract.inputs.vulkan_headers'
    Assert-M0SafeRelativePath $contract.inputs.vulkan_headers.relative_path 'Vulkan archive path'
    Assert-M0PositiveSize $contract.inputs.vulkan_headers.size_bytes 'Vulkan archive size'
    Assert-M0Digest $contract.inputs.vulkan_headers.sha256 'Vulkan archive SHA-256'
    Assert-M0Commit $contract.inputs.vulkan_headers.commit_sha 'Vulkan commit'
    if ($contract.inputs.vulkan_headers.commit_sha -cne '37164a5726f7e6113810f9557903a117498421cf') {
        throw 'Vulkan-Headers commit must be the SDK 1.2.182.0 revision.'
    }
    Assert-M0SafeRelativePath $contract.inputs.vulkan_headers.header_relative_path 'Vulkan header path'
    Assert-M0Digest $contract.inputs.vulkan_headers.header_sha256 'Vulkan header SHA-256'
    Assert-M0SafeRelativePath $contract.inputs.vulkan_headers.license_relative_path 'Vulkan license path'
    Assert-M0Digest $contract.inputs.vulkan_headers.license_sha256 'Vulkan license SHA-256'

    $tools = @($contract.tools)
    if ($tools.Count -ne $script:ExpectedToolIds.Count) { throw 'Contract tool inventory is incomplete or contains an extra entry.' }
    $toolIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($tool in $tools) {
        Assert-M0ExactProperties $tool @('id', 'relative_path', 'size_bytes', 'sha256', 'version') 'contract.tools[]'
        Assert-M0String $tool.id 'Tool id'
        if ($tool.id -cnotmatch '^[a-z][a-z0-9-]*$' -or -not $toolIds.Add($tool.id)) {
            throw "Contract tool id '$($tool.id)' is invalid or duplicated."
        }
        Assert-M0SafeRelativePath $tool.relative_path "Tool '$($tool.id)' path"
        Assert-M0PositiveSize $tool.size_bytes "Tool '$($tool.id)' size"
        Assert-M0Digest $tool.sha256 "Tool '$($tool.id)' SHA-256"
        Assert-M0String $tool.version "Tool '$($tool.id)' version"
    }
    Assert-M0StringSequence @($tools | ForEach-Object { $_.id }) $script:ExpectedToolIds 'Contract tool ids'

    Assert-M0ExactProperties $contract.environment @('forbidden_variables') 'contract.environment'
    Assert-M0StringSequence $contract.environment.forbidden_variables $script:ForbiddenEnvironment 'Contract forbidden environment variables'

    Assert-M0ExactProperties $contract.isolation @('run_parent_relative_path', 'require_new_direct_child', 'receipt_relative_path') 'contract.isolation'
    Assert-M0SafeRelativePath $contract.isolation.run_parent_relative_path 'Contract run parent'
    if ($contract.isolation.run_parent_relative_path -cne 'out/m0/qtbase-refresh-runs') { throw 'Contract run parent is not isolated.' }
    if ($contract.isolation.require_new_direct_child -isnot [bool] -or -not $contract.isolation.require_new_direct_child) {
        throw 'Contract must require a new direct-child RunRoot.'
    }
    Assert-M0SafeRelativePath $contract.isolation.receipt_relative_path 'Contract receipt path'
    if ($contract.isolation.receipt_relative_path -cne 'evidence/qtbase-refresh-receipt.v1.json') {
        throw 'Contract receipt path is not accepted.'
    }

    Assert-M0ConfigurationObject $contract.configuration 'contract.configuration'
    return $contract
}

function Assert-M0PatchObservation {
    param([Parameter(Mandatory)] $Patch, [Parameter(Mandatory)] $ContractPatch, [string] $Context = 'observation.patch')
    Assert-M0ExactProperties $Patch @('changed_relative_paths', 'target_relative_path', 'target_before_sha256', 'target_after_sha256') $Context
    $changed = @($Patch.changed_relative_paths)
    if ($changed.Count -ne 1 -or $changed[0] -cne $ContractPatch.target_relative_path) {
        throw "$Context must report exactly the single accepted patch target."
    }
    if ($Patch.target_relative_path -cne $ContractPatch.target_relative_path -or
        $Patch.target_before_sha256 -cne $ContractPatch.target_before_sha256 -or
        $Patch.target_after_sha256 -cne $ContractPatch.target_after_sha256) {
        throw "$Context target identity does not match the contract before/after hashes."
    }
}

function Assert-M0ExecutionObservation {
    param([Parameter(Mandatory)] $Execution, [string] $Context = 'observation.execution')
    Assert-M0ExactProperties $Execution @('exit_code', 'stdout', 'stderr') $Context
    if ($Execution.exit_code -isnot [int] -and $Execution.exit_code -isnot [long]) {
        throw "$Context exit_code must be an integer."
    }
    Assert-M0String $Execution.stdout "$Context stdout" -AllowEmpty
    Assert-M0String $Execution.stderr "$Context stderr" -AllowEmpty
}

function Invoke-M0QtBaseRefresh {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $ToolRoot,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [scriptblock] $Execute
    )

    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    $refreshId = [string]$contract.refresh_id
    $receiptRelativePath = [string]$contract.isolation.receipt_relative_path
    $repository = ConvertTo-M0NormalizedDirectoryPath $RepositoryRoot
    $toolRootFull = ConvertTo-M0NormalizedDirectoryPath $ToolRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'RepositoryRoot does not exist.' }
    if (-not [IO.Directory]::Exists($toolRootFull)) { throw 'ToolRoot does not exist.' }
    Assert-M0NoReparseChain -LiteralPath $repository -BoundaryRoot $repository -Context 'RepositoryRoot'
    Assert-M0NoReparseChain -LiteralPath $toolRootFull -BoundaryRoot $toolRootFull -Context 'ToolRoot'

    $inputPaths = [ordered]@{}
    foreach ($binding in @(
            @('qtbase_source', $contract.inputs.qtbase_source),
            @('freetype_patch', $contract.inputs.freetype_patch),
            @('vulkan_headers', $contract.inputs.vulkan_headers)
        )) {
        $id = [string]$binding[0]
        $spec = $binding[1]
        $candidate = Resolve-M0ContainedPath $repository $spec.relative_path "Input '$id'"
        $inputPaths[$id] = Assert-M0PhysicalFile $candidate ([long]$spec.size_bytes) $spec.sha256 "Input '$id'" -BoundaryRoot $repository
    }

    $toolPaths = [ordered]@{}
    foreach ($tool in @($contract.tools)) {
        $candidate = Resolve-M0ContainedPath $toolRootFull $tool.relative_path "Tool '$($tool.id)'"
        $toolPaths[$tool.id] = Assert-M0PhysicalFile $candidate ([long]$tool.size_bytes) $tool.sha256 "Tool '$($tool.id)'" -BoundaryRoot $toolRootFull
    }

    foreach ($name in @($contract.environment.forbidden_variables)) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if (-not [string]::IsNullOrEmpty($value)) { throw "Forbidden build environment injection '$name' is present." }
    }

    $runParent = Resolve-M0ContainedPath $repository $contract.isolation.run_parent_relative_path 'Run parent'
    if (-not [IO.Directory]::Exists($runParent)) { throw "Pinned isolated RunRoot parent does not exist: $runParent" }
    Assert-M0NoReparseChain -LiteralPath $runParent -BoundaryRoot $repository -Context 'RunRoot parent'
    $runRootFull = [IO.Path]::GetFullPath($RunRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
    if ([string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($runRootFull)) -or
        [IO.Path]::GetDirectoryName($runRootFull) -cne $runParent) {
        throw 'RunRoot must be a new direct child of the pinned isolated parent.'
    }
    if ([IO.Directory]::Exists($runRootFull) -or [IO.File]::Exists($runRootFull)) {
        throw 'RunRoot must be new and must not already exist.'
    }

    [IO.Directory]::CreateDirectory($runRootFull) | Out-Null
    Assert-M0NoReparseChain -LiteralPath $runRootFull -BoundaryRoot $runParent -Context 'RunRoot'
    $contractCopy = $contract | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
    $context = [pscustomobject][ordered]@{
        contract_sha256 = $ExpectedContractSha256
        contract = $contractCopy
        run_root = $runRootFull
        paths = [pscustomobject]$inputPaths
        tool_paths = [pscustomobject]$toolPaths
    }

    $observations = @(& $Execute $context)
    if ($observations.Count -ne 1 -or $null -eq $observations[0]) {
        throw 'QtBase refresh callback must return exactly one observation object.'
    }
    $observation = $observations[0]
    Assert-M0ExactProperties $observation @('execution', 'patch', 'configuration') 'refresh observation'
    Assert-M0ExecutionObservation $observation.execution
    if ([int]$observation.execution.exit_code -ne 0) {
        $message = "QtBase native execution failed with exit code $($observation.execution.exit_code).`nSTDOUT:`n$($observation.execution.stdout)`nSTDERR:`n$($observation.execution.stderr)"
        throw [InvalidOperationException]::new($message)
    }

    $null = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    foreach ($binding in @(
            @('qtbase_source', $contract.inputs.qtbase_source),
            @('freetype_patch', $contract.inputs.freetype_patch),
            @('vulkan_headers', $contract.inputs.vulkan_headers)
        )) {
        $id = [string]$binding[0]
        $spec = $binding[1]
        $null = Assert-M0PhysicalFile $inputPaths[$id] ([long]$spec.size_bytes) $spec.sha256 "Post-execution input '$id'" -BoundaryRoot $repository
    }
    foreach ($tool in @($contract.tools)) {
        $null = Assert-M0PhysicalFile $toolPaths[$tool.id] ([long]$tool.size_bytes) $tool.sha256 "Post-execution tool '$($tool.id)'" -BoundaryRoot $toolRootFull
    }
    foreach ($name in @($contract.environment.forbidden_variables)) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if (-not [string]::IsNullOrEmpty($value)) { throw "Post-execution forbidden environment injection '$name' is present." }
    }

    Assert-M0PatchObservation $observation.patch $contract.inputs.freetype_patch
    Assert-M0ConfigurationObject $observation.configuration 'observation.configuration' -Observation

    $receipt = [ordered]@{
        schema_version = 1
        receipt_id = "${refreshId}-receipt"
        contract_sha256 = $ExpectedContractSha256
        refresh_id = $refreshId
        captured_utc = [DateTimeOffset]::UtcNow.ToString('O')
        execution = $observation.execution
        patch = $observation.patch
        configuration = $observation.configuration
        limits = $script:ReceiptLimits
    }
    Assert-M0NoReparseChain -LiteralPath $runRootFull -BoundaryRoot $runParent -Context 'Post-execution RunRoot'
    $receiptPath = Resolve-M0ContainedPath $runRootFull $receiptRelativePath 'Receipt path'
    $receiptParent = [IO.Path]::GetDirectoryName($receiptPath)
    if (-not [IO.Directory]::Exists($receiptParent)) {
        [IO.Directory]::CreateDirectory($receiptParent) | Out-Null
    }
    Assert-M0NoReparseChain -LiteralPath $receiptParent -BoundaryRoot $runRootFull -Context 'Receipt evidence parent'
    $jsonText = ($receipt | ConvertTo-Json -Depth 100) + "`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($jsonText)
    $publication = Publish-M0QtBaseRefreshReceipt -LiteralPath $receiptPath -Bytes $bytes -RunRoot $runRootFull
    return [pscustomobject][ordered]@{
        contract_sha256 = $ExpectedContractSha256
        receipt_path = $receiptPath
        receipt_sha256 = $publication.sha256
    }
}

function Assert-M0QtBaseRefreshReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256
    )

    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    Assert-M0Digest $ExpectedReceiptSha256 'Expected receipt SHA-256'
    if (-not [IO.File]::Exists($LiteralPath)) { throw 'QtBase refresh receipt is missing.' }
    $item = Get-Item -LiteralPath $LiteralPath -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'QtBase refresh receipt cannot be a reparse point.' }
    if ((Get-M0Sha256 $item.FullName) -cne $ExpectedReceiptSha256) { throw 'QtBase refresh receipt SHA-256 mismatch or tamper.' }
    $receipt = Read-M0ClosedJson -LiteralPath $item.FullName -Context 'QtBase refresh receipt'
    Assert-M0ExactProperties $receipt @(
        'schema_version', 'receipt_id', 'contract_sha256', 'refresh_id', 'captured_utc', 'execution', 'patch', 'configuration', 'limits'
    ) 'receipt'
    if ($receipt.schema_version -isnot [long] -or $receipt.schema_version -ne 1 -or
        $receipt.receipt_id -cne "$($contract.refresh_id)-receipt" -or
        $receipt.contract_sha256 -cne $ExpectedContractSha256 -or
        $receipt.refresh_id -cne $contract.refresh_id) {
        throw 'Receipt identity or contract binding drifted.'
    }
    $captured = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParse($receipt.captured_utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$captured)) {
        throw 'Receipt captured_utc is invalid.'
    }
    Assert-M0ExecutionObservation $receipt.execution 'receipt.execution'
    if ([int]$receipt.execution.exit_code -ne 0) { throw 'Receipt cannot authenticate a failed execution.' }
    Assert-M0PatchObservation $receipt.patch $contract.inputs.freetype_patch 'receipt.patch'
    Assert-M0ConfigurationObject $receipt.configuration 'receipt.configuration' -Observation
    Assert-M0StringSequence $receipt.limits $script:ReceiptLimits 'Receipt limits'
    return $receipt
}

Export-ModuleMember -Function @(
    'Assert-M0QtBaseRefreshContract',
    'Assert-M0QtBaseRefreshReceipt',
    'Invoke-M0QtBaseRefresh'
)
