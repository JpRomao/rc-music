#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:LoadedModulePath = [IO.Path]::GetFullPath($PSCommandPath)
$script:Utf8NoBom = [Text.UTF8Encoding]::new($false)
$script:OwnerIds = @('qtbase-refresh-v2', 'qtdeclarative', 'qtsvg', 'd3d')
$script:RecipeIds = @('orchestrator', 'runner', 'entrypoint')
$script:RecipePaths = @(
    'experiments/m0/provisioning/qt-shell/M0.QtShellComposite.psm1',
    'experiments/m0/provisioning/qt-shell/M0.QtShellCompositeNative.psm1',
    'experiments/m0/provisioning/qt-shell/Invoke-M0QtShellComposite.ps1'
)
$script:Limits = @(
    'diagnostic M0 composite SDK only',
    'does not prove general Qt ABI compatibility',
    'does not approve a Qt toolkit decision',
    'does not approve redistribution or publication',
    'does not close CHK-SEC or M0.2',
    'does not replace the official SDK or historical packages'
)
$script:ExpectedArchiveSpecs = @(
    [pscustomobject]@{
        owner_id = 'qtdeclarative'; archive_id = 'qtdeclarative'
        relative_path = 'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017qtdeclarative-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
        sha256 = '5591ca564c1a9299a45a15b6b1d324c86e0aaed7cf6c47b7e6fe464aecae2587'
        size_bytes = [long]172484222; file_count = [long]5470; total_size_bytes = [long]1865393737
    },
    [pscustomobject]@{
        owner_id = 'qtsvg'; archive_id = 'qtsvg'
        relative_path = 'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017qtsvg-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
        sha256 = '417f44499c835b2303f3ff78043179bb23442f33d5d2816fbf7e8dbf275b1c89'
        size_bytes = [long]684353; file_count = [long]120; total_size_bytes = [long]4556378
    },
    [pscustomobject]@{
        owner_id = 'd3d'; archive_id = 'qt-d3dcompiler-47-x64'
        relative_path = 'out/m0/downloads/qt-6.11.2-msvc2022_64/6.11.2-0-202608131017d3dcompiler_47-x64.7z'
        sha256 = 'b1c379f739fa59a495a388c0878757e3d90fb72e0df771237a1ee4a2c1675d15'
        size_bytes = [long]1380117; file_count = [long]1; total_size_bytes = [long]4173928
    }
)
$script:ExpectedRequiredArtifactOwners = [ordered]@{
    'bin/Qt6Core.dll' = 'qtbase-refresh-v2'
    'bin/Qt6Gui.dll' = 'qtbase-refresh-v2'
    'bin/Qt6Widgets.dll' = 'qtbase-refresh-v2'
    'bin/Qt6Test.dll' = 'qtbase-refresh-v2'
    'bin/qtpaths.exe' = 'qtbase-refresh-v2'
    'bin/windeployqt.exe' = 'qtbase-refresh-v2'
    'bin/moc.exe' = 'qtbase-refresh-v2'
    'bin/rcc.exe' = 'qtbase-refresh-v2'
    'bin/uic.exe' = 'qtbase-refresh-v2'
    'plugins/platforms/qwindows.dll' = 'qtbase-refresh-v2'
    'plugins/platforms/qoffscreen.dll' = 'qtbase-refresh-v2'
    'bin/Qt6Qml.dll' = 'qtdeclarative'
    'bin/Qt6Quick.dll' = 'qtdeclarative'
    'bin/Qt6QuickControls2.dll' = 'qtdeclarative'
    'bin/Qt6QuickLayouts.dll' = 'qtdeclarative'
    'bin/Qt6QuickTest.dll' = 'qtdeclarative'
    'bin/qmlformat.exe' = 'qtdeclarative'
    'bin/qmllint.exe' = 'qtdeclarative'
    'bin/qmltestrunner.exe' = 'qtdeclarative'
    'bin/qmlimportscanner.exe' = 'qtdeclarative'
    'bin/qmlcachegen.exe' = 'qtdeclarative'
    'bin/qmltyperegistrar.exe' = 'qtdeclarative'
    'bin/Qt6Svg.dll' = 'qtsvg'
    'plugins/imageformats/qsvg.dll' = 'qtsvg'
    'plugins/iconengines/qsvgicon.dll' = 'qtsvg'
    'd3dcompiler_47.dll' = 'd3d'
}

if ($IsWindows) {
    $nativeNamespace = 'M0QtShellComposite_' + [Guid]::NewGuid().ToString('N')
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

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_STATUS_BLOCK {
        public IntPtr Status;
        public IntPtr Information;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct UNICODE_STRING {
        public ushort Length;
        public ushort MaximumLength;
        public IntPtr Buffer;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct OBJECT_ATTRIBUTES {
        public int Length;
        public IntPtr RootDirectory;
        public IntPtr ObjectName;
        public uint Attributes;
        public IntPtr SecurityDescriptor;
        public IntPtr SecurityQualityOfService;
    }

    public static class PhysicalFile {
        private const uint GENERIC_READ = 0x80000000;
        private const uint GENERIC_WRITE = 0x40000000;
        private const uint DELETE_ACCESS = 0x00010000;
        private const uint FILE_LIST_DIRECTORY = 0x00000001;
        private const uint FILE_READ_ATTRIBUTES = 0x00000080;
        private const uint SYNCHRONIZE = 0x00100000;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint FILE_SHARE_DELETE = 0x00000004;
        private const uint CREATE_NEW = 1;
        private const uint OPEN_EXISTING = 3;
        private const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;
        private const uint FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000;
        private const int FileDispositionInfo = 4;
        private const int ERROR_HANDLE_EOF = 38;
        private const uint FILE_CREATE = 2;
        private const uint FILE_DIRECTORY_FILE = 0x00000001;
        private const uint FILE_SYNCHRONOUS_IO_NONALERT = 0x00000020;
        private const uint FILE_OPEN_REPARSE_POINT = 0x00200000;
        private const uint OBJ_CASE_INSENSITIVE = 0x00000040;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr FindFirstStreamW(string path, int infoLevel,
            out WIN32_FIND_STREAM_DATA data, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool FindNextStreamW(IntPtr findHandle, out WIN32_FIND_STREAM_DATA data);

        [DllImport("kernel32.dll")]
        private static extern bool FindClose(IntPtr findHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            IntPtr handle, out BY_HANDLE_FILE_INFORMATION information);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle handle,
            int fileInformationClass, IntPtr fileInformation, uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtSetInformationFile(SafeFileHandle handle,
            out IO_STATUS_BLOCK ioStatusBlock, IntPtr information, uint bufferSize,
            int fileInformationClass);

        [DllImport("ntdll.dll")]
        private static extern int NtCreateFile(out IntPtr fileHandle, uint desiredAccess,
            ref OBJECT_ATTRIBUTES objectAttributes, out IO_STATUS_BLOCK ioStatusBlock,
            IntPtr allocationSize, uint fileAttributes, uint shareAccess,
            uint createDisposition, uint createOptions, IntPtr eaBuffer, uint eaLength);

        [DllImport("ntdll.dll")]
        private static extern uint RtlNtStatusToDosError(int status);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            IntPtr handle, StringBuilder path, uint pathLength, uint flags);

        public static SafeFileHandle OpenFileReadOnly(string path) {
            var handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle CreateDirectoryRelative(SafeFileHandle parent, string leaf) {
            if (String.IsNullOrEmpty(leaf) || leaf.IndexOfAny(new[] { '\\', '/', ':', '\0' }) >= 0) {
                throw new ArgumentException("Directory name must be one relative leaf.", "leaf");
            }
            IntPtr nameBuffer = Marshal.StringToHGlobalUni(leaf);
            IntPtr unicodeBuffer = IntPtr.Zero;
            try {
                var unicode = new UNICODE_STRING {
                    Length = checked((ushort)(leaf.Length * sizeof(char))),
                    MaximumLength = checked((ushort)((leaf.Length + 1) * sizeof(char))),
                    Buffer = nameBuffer
                };
                unicodeBuffer = Marshal.AllocHGlobal(Marshal.SizeOf<UNICODE_STRING>());
                Marshal.StructureToPtr(unicode, unicodeBuffer, false);
                var attributes = new OBJECT_ATTRIBUTES {
                    Length = Marshal.SizeOf<OBJECT_ATTRIBUTES>(),
                    RootDirectory = parent.DangerousGetHandle(),
                    ObjectName = unicodeBuffer,
                    Attributes = OBJ_CASE_INSENSITIVE
                };
                IO_STATUS_BLOCK iosb;
                IntPtr raw;
                int status = NtCreateFile(out raw,
                    FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE_ACCESS | SYNCHRONIZE,
                    ref attributes, out iosb, IntPtr.Zero, 0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
                    FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT,
                    IntPtr.Zero, 0);
                if (status < 0) { throw new Win32Exception((int)RtlNtStatusToDosError(status)); }
                return new SafeFileHandle(raw, true);
            }
            finally {
                if (unicodeBuffer != IntPtr.Zero) Marshal.FreeHGlobal(unicodeBuffer);
                Marshal.FreeHGlobal(nameBuffer);
            }
        }

        public static SafeFileHandle OpenDirectoryReadOnly(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryForOwnership(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
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

        public static SafeFileHandle OpenDirectoryImmutableAnchor(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, IntPtr.Zero, OPEN_EXISTING,
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
            var handle = CreateFileW(path, FILE_READ_ATTRIBUTES | DELETE_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle CreateNewReadWriteDelete(string path) {
            var handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS,
                FILE_SHARE_READ, IntPtr.Zero, CREATE_NEW, 0, IntPtr.Zero);
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
                for (int index = 0; index < total; index++) { Marshal.WriteByte(buffer, index, 0); }
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

        public static void CreateDirectoryNew(string path) {
            if (!CreateDirectoryW(path, IntPtr.Zero)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
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
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out information)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return information;
        }

        public static uint GetLinkCount(SafeFileHandle handle) {
            return GetInformation(handle).NumberOfLinks;
        }

        public static uint GetAttributes(SafeFileHandle handle) {
            return GetInformation(handle).FileAttributes;
        }

        public static string GetIdentity(SafeFileHandle handle) {
            var information = GetInformation(handle);
            return information.VolumeSerialNumber.ToString("X8") + ":" +
                information.FileIndexHigh.ToString("X8") + information.FileIndexLow.ToString("X8");
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(
                handle.DangerousGetHandle(), buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return buffer.ToString();
        }
    }
}
'@
    $types = @(Add-Type -TypeDefinition $nativeSource.Replace('__NAMESPACE__', $nativeNamespace) -PassThru)
    $physicalTypes = @($types | Where-Object Name -CEQ 'PhysicalFile')
    if ($physicalTypes.Count -ne 1) {
        throw 'Composite native security helper did not compile to exactly one PhysicalFile type.'
    }
    $script:PhysicalFile = $physicalTypes[0]
}

function Assert-M0ExactProperties {
    param($Value, [Parameter(Mandatory)] [string[]] $Expected, [Parameter(Mandatory)] [string] $Context)

    if ($null -eq $Value -or $Value -isnot [psobject]) { throw "$Context must be a JSON object." }
    $actual = @($Value.PSObject.Properties.Name)
    if ($actual.Count -ne $Expected.Count) { throw "$Context has an open or incomplete property set." }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($actual[$index] -cne $Expected[$index]) {
            throw "$Context property order or field set drifted."
        }
    }
}

function Assert-M0String {
    param($Value, [Parameter(Mandatory)] [string] $Context, [switch] $AllowEmpty)
    if ($Value -isnot [string] -or (-not $AllowEmpty -and [string]::IsNullOrWhiteSpace($Value))) {
        throw "$Context must be a non-empty string."
    }
}

function Assert-M0Digest {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    if ($Value -isnot [string] -or $Value -cnotmatch '^[0-9a-f]{64}$') {
        throw "$Context must be a lowercase SHA-256 digest."
    }
}

function ConvertTo-M0Int64 {
    param($Value, [Parameter(Mandatory)] [string] $Context, [switch] $AllowZero)
    if ($Value -is [bool] -or $Value -isnot [ValueType] -or
        $Value.GetTypeCode() -notin @(
            [TypeCode]::Byte, [TypeCode]::SByte, [TypeCode]::Int16, [TypeCode]::UInt16,
            [TypeCode]::Int32, [TypeCode]::UInt32, [TypeCode]::Int64)) {
        throw "$Context must be an integer."
    }
    $converted = [long]$Value
    if (($AllowZero -and $converted -lt 0) -or (-not $AllowZero -and $converted -le 0)) {
        throw "$Context is outside its accepted range."
    }
    return $converted
}

function Assert-M0CanonicalUtcTimestamp {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    Assert-M0String $Value $Context
    $parsed = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParseExact(
            $Value, 'O', [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed) -or
        $parsed.Offset -ne [TimeSpan]::Zero -or
        $parsed.ToString('O', [Globalization.CultureInfo]::InvariantCulture) -cne $Value) {
        throw "$Context must be a canonical round-trip UTC timestamp."
    }
}

function Assert-M0Sequence {
    param($Actual, [Parameter(Mandatory)] [object[]] $Expected, [Parameter(Mandatory)] [string] $Context)
    if ($Actual -isnot [array]) { throw "$Context must be a JSON array." }
    $values = @($Actual)
    if ($values.Count -ne $Expected.Count) { throw "$Context sequence length drifted." }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($values[$index] -cne $Expected[$index]) { throw "$Context drifted at index $index." }
    }
}

function Assert-M0NoDuplicateJsonProperties {
    param([Parameter(Mandatory)] [Text.Json.JsonElement] $Element, [string] $Context = '$')
    switch ($Element.ValueKind) {
        ([Text.Json.JsonValueKind]::Object) {
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            foreach ($property in $Element.EnumerateObject()) {
                if (-not $seen.Add($property.Name)) {
                    throw "Duplicate JSON property '$($property.Name)' at $Context."
                }
                Assert-M0NoDuplicateJsonProperties $property.Value "$Context.$($property.Name)"
            }
        }
        ([Text.Json.JsonValueKind]::Array) {
            $index = 0
            foreach ($item in $Element.EnumerateArray()) {
                Assert-M0NoDuplicateJsonProperties $item "$Context[$index]"
                $index++
            }
        }
    }
}

function Read-M0ClosedJson {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    try {
        $bytes = [IO.File]::ReadAllBytes($LiteralPath)
        $strictUtf8 = [Text.UTF8Encoding]::new($false, $true)
        $text = $strictUtf8.GetString($bytes)
        if ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) { throw 'UTF-8 BOM is forbidden.' }
        $document = [Text.Json.JsonDocument]::Parse($text)
        try { Assert-M0NoDuplicateJsonProperties $document.RootElement }
        finally { $document.Dispose() }
        return $text | ConvertFrom-Json -Depth 100 -DateKind String
    }
    catch {
        throw [IO.InvalidDataException]::new("$Context is not valid closed UTF-8 JSON: $($_.Exception.Message)", $_.Exception)
    }
}

function Get-M0Sha256 {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-M0Sha256FromStream {
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

function Assert-M0SafeRelativePath {
    param($Value, [Parameter(Mandatory)] [string] $Context)
    Assert-M0String $Value $Context
    if ($Value.IndexOf([char]0) -ge 0 -or $Value -match '^[\\/]' -or
        $Value -match '^[A-Za-z]:' -or $Value.Contains(':') -or $Value.Contains('\\')) {
        throw "$Context has an unsafe relative path."
    }
    $segments = @($Value -split '/')
    if ($segments.Count -eq 0 -or @($segments | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
        throw "$Context has an unsafe empty, current, or parent segment."
    }
}

function ConvertTo-M0NormalizedDirectoryPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $root = [IO.Path]::GetPathRoot($full)
    if ($full -ceq $root) { return $root }
    return [IO.Path]::TrimEndingDirectorySeparator($full)
}

function ConvertTo-M0ExtendedPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($full.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) { return $full }
    if ($full.StartsWith('\\', [StringComparison]::Ordinal)) { return '\\?\UNC\' + $full.Substring(2) }
    return '\\?\' + $full
}

function ConvertFrom-M0FinalPath {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if ($LiteralPath.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $LiteralPath.Substring(8)
    }
    if ($LiteralPath.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $LiteralPath.Substring(4)
    }
    return $LiteralPath
}

function Test-M0PathWithin {
    param([Parameter(Mandatory)] [string] $Candidate, [Parameter(Mandatory)] [string] $Boundary)
    $candidateFull = [IO.Path]::GetFullPath($Candidate)
    $boundaryFull = ConvertTo-M0NormalizedDirectoryPath $Boundary
    if ([string]::Equals($candidateFull, $boundaryFull, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    return $candidateFull.StartsWith(
        $boundaryFull + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-M0ContainedPath {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0SafeRelativePath $RelativePath $Context
    $rootFull = ConvertTo-M0NormalizedDirectoryPath $Root
    $candidate = [IO.Path]::GetFullPath((Join-Path $rootFull $RelativePath))
    if (-not (Test-M0PathWithin $candidate $rootFull)) { throw "$Context escaped its boundary root." }
    return $candidate
}

function Assert-M0NoReparseChain {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $boundary = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
    $current = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-M0PathWithin $current $boundary)) { throw "$Context is outside its physical boundary." }
    while ($true) {
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Context contains a reparse point: $current"
        }
        $normalized = if ($item.PSIsContainer) {
            ConvertTo-M0NormalizedDirectoryPath $item.FullName
        } else { [IO.Path]::GetFullPath($item.FullName) }
        if ([string]::Equals($normalized, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($normalized)
        if ([string]::IsNullOrEmpty($parent) -or -not (Test-M0PathWithin $parent $boundary)) {
            throw "$Context physical ancestry escaped its boundary."
        }
        $current = $parent
    }
}

function Assert-M0NoAlternateDataStreams {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    if (-not $IsWindows) { throw 'Composite physical validation is supported only on Windows.' }
    $streams = @($script:PhysicalFile::GetStreamNames((ConvertTo-M0ExtendedPath $LiteralPath)))
    $isDirectory = [IO.Directory]::Exists([IO.Path]::GetFullPath($LiteralPath))
    $unexpected = @($streams | Where-Object { $_ -cne '::$DATA' })
    if ($unexpected.Count -ne 0 -or $streams.Count -gt 1 -or
        (-not $isDirectory -and ($streams.Count -ne 1 -or $streams[0] -cne '::$DATA'))) {
        throw "$Context contains an alternate data stream (ADS) or an invalid unnamed stream."
    }
}

function Open-M0PhysicalFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [long] $ExpectedSizeBytes = -1,
        [string] $ExpectedSha256,
        [switch] $RequireSingleLink
    )
    if (-not $IsWindows) { throw 'Composite physical validation is supported only on Windows.' }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not [IO.File]::Exists($full)) { throw "$Context is missing." }
    Assert-M0NoReparseChain $full $BoundaryRoot $Context
    Assert-M0NoAlternateDataStreams $full $Context
    $safe = $script:PhysicalFile::OpenFileReadOnly((ConvertTo-M0ExtendedPath $full))
    try {
        $stream = [IO.FileStream]::new($safe, [IO.FileAccess]::Read)
    }
    catch { $safe.Dispose(); throw }
    try {
        $final = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath $script:PhysicalFile::GetFinalPath($stream.SafeFileHandle)))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolved through a physical alias."
        }
        $linkCount = [long]$script:PhysicalFile::GetLinkCount($stream.SafeFileHandle)
        if ($RequireSingleLink -and $linkCount -ne 1) { throw "$Context is not a single-link file; hardlinks are forbidden." }
        if ($ExpectedSizeBytes -ge 0 -and $stream.Length -ne $ExpectedSizeBytes) { throw "$Context size drifted." }
        $hash = Get-M0Sha256FromStream $stream
        if (-not [string]::IsNullOrEmpty($ExpectedSha256) -and $hash -cne $ExpectedSha256) {
            throw "$Context SHA-256 drifted."
        }
        return [pscustomobject][ordered]@{
            path = $full; stream = $stream; size_bytes = [long]$stream.Length; sha256 = $hash
            identity = $script:PhysicalFile::GetIdentity($stream.SafeFileHandle); link_count = $linkCount
        }
    }
    catch { $stream.Dispose(); throw }
}

function Close-M0Leases {
    param([Parameter(Mandatory)] [Collections.IEnumerable] $Leases)
    foreach ($lease in @($Leases)) {
        if ($null -eq $lease) { continue }
        if ($null -ne $lease.PSObject.Properties['lease_kind'] -and $lease.lease_kind -ceq 'run_root') {
            foreach ($anchor in $lease.children.Values) {
                try { $anchor.handle.Dispose() } catch { }
            }
            try { $lease.root_anchor.handle.Dispose() } catch { }
            continue
        }
        if ($null -ne $lease.PSObject.Properties['stream'] -and $null -ne $lease.stream) {
            try { $lease.stream.Dispose() } catch { }
        }
        if ($null -ne $lease.PSObject.Properties['handle'] -and $null -ne $lease.handle) {
            try { $lease.handle.Dispose() } catch { }
        }
    }
}

function Assert-M0LeasesUnchanged {
    param([Parameter(Mandatory)] [Collections.IEnumerable] $Leases, [Parameter(Mandatory)] [string] $Context)
    foreach ($lease in @($Leases)) {
        if ($null -ne $lease.PSObject.Properties['lease_kind'] -and $lease.lease_kind -ceq 'tree') {
            Assert-M0TreeEnvelopeUnchanged $lease $Context
            continue
        }
        if ($null -ne $lease.PSObject.Properties['lease_kind'] -and $lease.lease_kind -ceq 'directory') {
            Assert-M0DirectoryAnchorUnchanged $lease $Context
            continue
        }
        if ($null -ne $lease.PSObject.Properties['lease_kind'] -and $lease.lease_kind -ceq 'run_root') {
            Assert-M0RunRootEnvelopeUnchanged $lease $Context
            continue
        }
        if ($lease.stream.Length -ne $lease.size_bytes -or (Get-M0Sha256FromStream $lease.stream) -cne $lease.sha256) {
            throw "$Context detected a changed authenticated file: $($lease.path)"
        }
        $final = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                    $script:PhysicalFile::GetFinalPath($lease.stream.SafeFileHandle))))
        if ([string]$script:PhysicalFile::GetIdentity($lease.stream.SafeFileHandle) -cne [string]$lease.identity -or
            -not [string]::Equals($final, [string]$lease.path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context detected physical identity or final-path drift: $($lease.path)"
        }
        if ([long]$script:PhysicalFile::GetLinkCount($lease.stream.SafeFileHandle) -ne $lease.link_count) {
            throw "$Context detected physical link-count drift: $($lease.path)"
        }
        Assert-M0NoAlternateDataStreams $lease.path "$Context authenticated file"
    }
}

function New-M0ExclusiveDirectory {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if (-not $IsWindows) { throw 'Composite directory creation is supported only on Windows.' }
    $script:PhysicalFile::CreateDirectoryNew((ConvertTo-M0ExtendedPath ([IO.Path]::GetFullPath($LiteralPath))))
}

function New-M0AtomicDirectoryAnchor {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [scriptblock] $AfterCreate
    )
    $full = ConvertTo-M0NormalizedDirectoryPath $LiteralPath
    $parent = ConvertTo-M0NormalizedDirectoryPath ([IO.Path]::GetDirectoryName($full))
    if (-not (Test-M0PathWithin $full $BoundaryRoot) -or
        -not [IO.Directory]::Exists($parent)) {
        throw "$Context requires one new contained directory leaf."
    }
    $parentAnchor = Open-M0DirectoryAnchor $parent $BoundaryRoot "$Context parent"
    $handle = $null
    $createdIdentity = $null
    try {
        $handle = $script:PhysicalFile::CreateDirectoryRelative(
            $parentAnchor.handle, [IO.Path]::GetFileName($full))
        $createdIdentity = [string]$script:PhysicalFile::GetIdentity($handle)
        $anchor = [pscustomobject][ordered]@{
            lease_kind = 'directory'; path = $full
            identity = $createdIdentity
            handle = $handle
        }
        if ($null -ne $AfterCreate) { $null = & $AfterCreate $full $anchor }
        Assert-M0DirectoryAnchorUnchanged $anchor "$Context atomic creation"
        $handle = $null
        return $anchor
    }
    catch {
        $failure = $_
        if ($null -ne $handle) {
            try {
                if ([string]$script:PhysicalFile::GetIdentity($handle) -cne $createdIdentity) {
                    throw "$Context refused cleanup of a changed created-directory identity."
                }
                $script:PhysicalFile::MarkDeleteOnClose($handle)
            }
            catch {
                $failure = [Management.Automation.ErrorRecord]::new(
                    [AggregateException]::new(
                        "$Context failed and authenticated created-directory cleanup also failed.",
                        [Exception[]]@($failure.Exception, $_.Exception)),
                    'M0CompositeCreatedDirectoryCleanupFailure',
                    [Management.Automation.ErrorCategory]::InvalidData,
                    $full)
            }
        }
        throw $failure
    }
    finally {
        if ($null -ne $handle) { $handle.Dispose() }
        $parentAnchor.handle.Dispose()
    }
}

function New-M0AtomicOwnedTreeState {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [scriptblock] $AfterCreate
    )
    $full = ConvertTo-M0NormalizedDirectoryPath $Root
    $anchor = New-M0AtomicDirectoryAnchor $full $BoundaryRoot $Context -AfterCreate $AfterCreate
    $directories = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $files = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    Add-M0OwnedIdentity $directories $full $anchor.identity $Context
    return [pscustomobject][ordered]@{
        root = $full; boundary_root = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
        root_anchor = $anchor; directory_ledger = $directories; file_ledger = $files
    }
}

function Get-M0DirectoryIdentity {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = ConvertTo-M0NormalizedDirectoryPath $LiteralPath
    if (-not [IO.Directory]::Exists($full)) { throw "$Context directory is missing." }
    Assert-M0NoReparseChain $full $BoundaryRoot $Context
    Assert-M0NoAlternateDataStreams $full $Context
    $handle = $script:PhysicalFile::OpenDirectoryForOwnership((ConvertTo-M0ExtendedPath $full))
    try {
        $final = ConvertTo-M0NormalizedDirectoryPath (
            ConvertFrom-M0FinalPath $script:PhysicalFile::GetFinalPath($handle))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolves through a physical alias."
        }
        return [string]$script:PhysicalFile::GetIdentity($handle)
    }
    finally { $handle.Dispose() }
}

function Open-M0DirectoryAnchor {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [switch] $RequireDeleteAccess,
        [switch] $Immutable
    )
    $full = ConvertTo-M0NormalizedDirectoryPath $LiteralPath
    if (-not [IO.Directory]::Exists($full)) { throw "$Context directory is missing." }
    Assert-M0NoReparseChain $full $BoundaryRoot $Context
    Assert-M0NoAlternateDataStreams $full $Context
    $handle = $null
    try {
        $handle = if ($RequireDeleteAccess) {
            $script:PhysicalFile::OpenDirectoryReadDeleteAnchor((ConvertTo-M0ExtendedPath $full))
        }
        elseif ($Immutable) {
            $script:PhysicalFile::OpenDirectoryImmutableAnchor((ConvertTo-M0ExtendedPath $full))
        }
        else { $script:PhysicalFile::OpenDirectoryAnchor((ConvertTo-M0ExtendedPath $full)) }
        $attributes = [IO.FileAttributes]$script:PhysicalFile::GetAttributes($handle)
        if (-not ($attributes -band [IO.FileAttributes]::Directory) -or
            ($attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "$Context is not an ordinary anchored directory."
        }
        $final = ConvertTo-M0NormalizedDirectoryPath (
            ConvertFrom-M0FinalPath $script:PhysicalFile::GetFinalPath($handle))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolves through a physical alias."
        }
        $anchor = [pscustomobject][ordered]@{
            lease_kind = 'directory'; path = $full
            identity = [string]$script:PhysicalFile::GetIdentity($handle)
            handle = $handle
        }
        $handle = $null
        return $anchor
    }
    finally { if ($null -ne $handle) { $handle.Dispose() } }
}

function Assert-M0DirectoryAnchorUnchanged {
    param([Parameter(Mandatory)] $Anchor, [Parameter(Mandatory)] [string] $Context)
    if ($Anchor.handle.IsClosed -or $Anchor.handle.IsInvalid) {
        throw "$Context detected a closed directory anchor: $($Anchor.path)"
    }
    $attributes = [IO.FileAttributes]$script:PhysicalFile::GetAttributes($Anchor.handle)
    $final = ConvertTo-M0NormalizedDirectoryPath (
        ConvertFrom-M0FinalPath $script:PhysicalFile::GetFinalPath($Anchor.handle))
    if (-not ($attributes -band [IO.FileAttributes]::Directory) -or
        ($attributes -band [IO.FileAttributes]::ReparsePoint) -or
        [string]$script:PhysicalFile::GetIdentity($Anchor.handle) -cne [string]$Anchor.identity -or
        -not [string]::Equals($final, [string]$Anchor.path, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context detected directory identity or final-path drift: $($Anchor.path)"
    }
    Assert-M0NoAlternateDataStreams $Anchor.path "$Context directory '$($Anchor.path)'"
}

function Add-M0OwnedIdentity {
    param(
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $Ledger,
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Identity,
        [Parameter(Mandatory)] [string] $Context
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if ($Ledger.ContainsKey($full)) {
        if ([string]$Ledger[$full] -cne $Identity) {
            throw "$Context physical identity changed after ownership was recorded."
        }
        return
    }
    $Ledger.Add($full, $Identity)
}

function New-M0OwnedTreeState {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [string] $ExpectedIdentity
    )
    $full = ConvertTo-M0NormalizedDirectoryPath $Root
    if (-not (Test-M0PathWithin $full $BoundaryRoot) -or
        [string]::Equals($full, (ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context owned root is outside its narrow boundary."
    }
    $anchor = Open-M0DirectoryAnchor $full $BoundaryRoot $Context -RequireDeleteAccess
    if (-not [string]::IsNullOrEmpty($ExpectedIdentity) -and
        [string]$anchor.identity -cne $ExpectedIdentity) {
        $anchor.handle.Dispose()
        throw "$Context physical identity differs from the authenticated publisher handoff."
    }
    $directories = [Collections.Generic.Dictionary[string,string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $files = [Collections.Generic.Dictionary[string,string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    Add-M0OwnedIdentity $directories $full $anchor.identity $Context
    return [pscustomobject][ordered]@{
        root = $full; boundary_root = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
        root_anchor = $anchor; directory_ledger = $directories; file_ledger = $files
    }
}

function Open-M0CleanupAnchor {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [string] $Context)
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $handle = $null
    try {
        $handle = $script:PhysicalFile::OpenCleanupAnchor((ConvertTo-M0ExtendedPath $full))
        $attributes = [IO.FileAttributes]$script:PhysicalFile::GetAttributes($handle)
        $final = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                    $script:PhysicalFile::GetFinalPath($handle))))
        if (-not [string]::Equals($final, $full, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context resolves through a replaced physical path."
        }
        $anchor = [pscustomobject][ordered]@{
            path = $full; identity = [string]$script:PhysicalFile::GetIdentity($handle)
            directory = [bool]($attributes -band [IO.FileAttributes]::Directory)
            reparse = [bool]($attributes -band [IO.FileAttributes]::ReparsePoint)
            handle = $handle; depth = [long]0
        }
        $handle = $null
        return $anchor
    }
    finally { if ($null -ne $handle) { $handle.Dispose() } }
}

function Remove-M0OwnedTree {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedExactPath,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] $RootAnchor,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Parameter(Mandatory)] [Collections.Generic.Dictionary[string,string]] $FileLedger,
        [scriptblock] $BeforeDelete
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $expected = [IO.Path]::GetFullPath($ExpectedExactPath)
    if (-not [string]::Equals($full, $expected, [StringComparison]::Ordinal) -or
        -not (Test-M0PathWithin $full $BoundaryRoot) -or
        [string]::Equals($full, (ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refused cleanup outside the exact owned composite root.'
    }
    if (-not $DirectoryLedger.ContainsKey($full)) {
        throw 'Refused cleanup of an unowned composite root ledger.'
    }
    Assert-M0DirectoryAnchorUnchanged $RootAnchor 'Owned composite cleanup root'
    if ($RootAnchor.path -ine $full -or $RootAnchor.identity -cne [string]$DirectoryLedger[$full]) {
        throw 'Refused cleanup because the retained root anchor differs from its exact ledger.'
    }

    $allAnchors = [Collections.Generic.List[object]]::new()
    $leafAnchors = [Collections.Generic.List[object]]::new()
    $directoryAnchors = [Collections.Generic.List[object]]::new()
    $seenDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$seenDirectories.Add($full)
    $pending = [Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ path = $full; depth = [long]0 })
    $committed = $false
    try {
        while ($pending.Count -ne 0) {
            $current = $pending.Pop()
            foreach ($child in [IO.DirectoryInfo]::new($current.path).GetFileSystemInfos()) {
                $childPath = [IO.Path]::GetFullPath($child.FullName)
                if (-not (Test-M0PathWithin $childPath $full) -or
                    -not [string]::Equals([IO.Path]::GetDirectoryName($childPath), $current.path,
                        [StringComparison]::OrdinalIgnoreCase)) {
                    throw 'Composite cleanup found an entry outside its authenticated parent.'
                }
                $anchor = Open-M0CleanupAnchor $childPath 'Composite cleanup entry audit'
                $anchor.depth = [long]$current.depth + 1
                $allAnchors.Add($anchor)
                if ($anchor.reparse) {
                    throw "Composite cleanup exact ledger rejected a reparse entry: $childPath"
                }
                Assert-M0NoAlternateDataStreams $childPath 'Composite cleanup audited entry'
                if ($anchor.directory) {
                    if (-not $DirectoryLedger.ContainsKey($childPath) -or
                        $anchor.identity -cne [string]$DirectoryLedger[$childPath] -or
                        -not $seenDirectories.Add($childPath)) {
                        throw "Composite cleanup rejected an unowned or replaced directory: $childPath"
                    }
                    $directoryAnchors.Add($anchor)
                    $pending.Push([pscustomobject]@{ path = $childPath; depth = $anchor.depth })
                }
                else {
                    if (-not $FileLedger.ContainsKey($childPath) -or
                        $anchor.identity -cne [string]$FileLedger[$childPath] -or
                        -not $seenFiles.Add($childPath)) {
                        throw "Composite cleanup rejected an unowned or replaced file: $childPath"
                    }
                    $leafAnchors.Add($anchor)
                }
            }
        }
        if ($seenDirectories.Count -ne $DirectoryLedger.Count -or
            $seenFiles.Count -ne $FileLedger.Count) {
            throw 'Composite cleanup exact ledger contains a missing or extra entry.'
        }
        foreach ($path in $DirectoryLedger.Keys) {
            if (-not $seenDirectories.Contains($path)) { throw 'Composite cleanup directory ledger is incomplete.' }
        }
        foreach ($path in $FileLedger.Keys) {
            if (-not $seenFiles.Contains($path)) { throw 'Composite cleanup file ledger is incomplete.' }
        }
        if ($null -ne $BeforeDelete) {
            $null = & $BeforeDelete ([pscustomobject]@{ anchors = $allAnchors; root_anchor = $RootAnchor })
        }
        Assert-M0DirectoryAnchorUnchanged $RootAnchor 'Owned composite cleanup commit'
        foreach ($anchor in $allAnchors) {
            $final = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                        $script:PhysicalFile::GetFinalPath($anchor.handle))))
            if (-not [string]::Equals($final, $anchor.path, [StringComparison]::OrdinalIgnoreCase) -or
                [string]$script:PhysicalFile::GetIdentity($anchor.handle) -cne $anchor.identity) {
                throw 'Composite cleanup entry changed after the retained-handle audit.'
            }
            Assert-M0NoAlternateDataStreams $anchor.path 'Composite cleanup commit entry'
        }

        foreach ($anchor in $leafAnchors) {
            $script:PhysicalFile::MarkDeleteOnClose($anchor.handle)
            $anchor.handle.Dispose()
        }
        foreach ($anchor in @($directoryAnchors | Sort-Object depth -Descending)) {
            $script:PhysicalFile::MarkDeleteOnClose($anchor.handle)
            $anchor.handle.Dispose()
        }
        $script:PhysicalFile::MarkDeleteOnClose($RootAnchor.handle)
        $RootAnchor.handle.Dispose()
        $committed = $true
    }
    finally {
        foreach ($anchor in $allAnchors) { try { $anchor.handle.Dispose() } catch { } }
        if (-not $committed) {
            # The retained root remains anchored for the caller; no path-based fallback is safe.
        }
    }
}

function Assert-M0QtShellCompositeContractDocument {
    param([Parameter(Mandatory)] $Contract)

    Assert-M0ExactProperties $Contract @(
        'schema_version', 'composite_id', 'product', 'provenance', 'recipe', 'inputs',
        'ownership', 'required_artifacts', 'isolation', 'limits'
    ) 'composite contract'
    if ($Contract.schema_version -isnot [long] -or $Contract.schema_version -ne 2 -or
        $Contract.composite_id -isnot [string] -or
        $Contract.composite_id -cne 'm0.2-qt-shell-composite-sdk-v2') {
        throw 'Composite contract schema or identity drifted.'
    }

    Assert-M0ExactProperties $Contract.product @(
        'qt_version', 'edition', 'architecture', 'compiler_abi', 'windows_sdk_version'
    ) 'contract.product'
    foreach ($binding in @(
            @('qt_version', '6.11.2'), @('edition', 'Desktop'), @('architecture', 'x86_64'),
            @('compiler_abi', 'msvc2022_64'), @('windows_sdk_version', '10.0.26100.0'))) {
        $property = [string]$binding[0]
        if ($Contract.product.$property -isnot [string] -or $Contract.product.$property -cne $binding[1]) {
            throw "Composite product '$property' drifted."
        }
    }

    Assert-M0ExactProperties $Contract.provenance @('qt_shell_v1_contract', 'qtbase_refresh') `
        'contract.provenance'
    $v1 = $Contract.provenance.qt_shell_v1_contract
    Assert-M0ExactProperties $v1 @('relative_path', 'size_bytes', 'sha256', 'file_count') `
        'contract.provenance.qt_shell_v1_contract'
    if ($v1.relative_path -cne 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-contract.v1.json' -or
        $v1.size_bytes -isnot [long] -or $v1.size_bytes -ne 1412 -or
        $v1.sha256 -cne 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952' -or
        $v1.file_count -isnot [long] -or $v1.file_count -ne 8) {
        throw 'Historical Qt shell v1 provenance drifted.'
    }

    $qtbase = $Contract.provenance.qtbase_refresh
    Assert-M0ExactProperties $qtbase @(
        'contract_relative_path', 'contract_size_bytes', 'contract_sha256',
        'module_relative_path', 'module_size_bytes', 'module_sha256',
        'receipt_relative_path', 'receipt_size_bytes', 'receipt_sha256',
        'manifest_relative_path', 'manifest_size_bytes', 'manifest_sha256',
        'install_root_relative_path', 'install_file_count', 'install_total_size_bytes',
        'install_inventory_relative_path', 'install_inventory_size_bytes', 'install_inventory_sha256'
    ) 'contract.provenance.qtbase_refresh'
    $qtbaseExpected = [ordered]@{
        contract_relative_path = 'experiments/m0/provisioning/qtbase-refresh/qtbase-refresh-contract.v2.json'
        contract_size_bytes = [long]20464
        contract_sha256 = 'fd2cc91ae7824c117a93f69e78d792c500f30fd2d112c3ecb2f0404de3b0d818'
        module_relative_path = 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1'
        module_size_bytes = [long]150065
        module_sha256 = '95245cb3cf812e0174840a73b94eb86c8217b4ee90131cbbae92f45469d54f0d'
        receipt_relative_path = 'q/96bvah3m0t0t71m990olv5gpvs/evidence/qtbase-refresh-receipt.v2.json'
        receipt_size_bytes = [long]4369
        receipt_sha256 = '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619'
        manifest_relative_path = 'q/96bvah3m0t0t71m990olv5gpvs/evidence/qtbase-refresh-manifest.v2.json'
        manifest_size_bytes = [long]13866
        manifest_sha256 = 'dc26c0962e927145466b879e352a4c99d9fc7da54d335567373c353739afbf6b'
        install_root_relative_path = 'q/96bvah3m0t0t71m990olv5gpvs/install'
        install_file_count = [long]4289
        install_total_size_bytes = [long]526177624
        install_inventory_relative_path = 'q/96bvah3m0t0t71m990olv5gpvs/evidence/inventories/install.tsv'
        install_inventory_size_bytes = [long]478362
        install_inventory_sha256 = 'b489401f06a97de9c8017a5dca6df626a3b12605a4974ff680d5fd25856e9e09'
    }
    foreach ($entry in $qtbaseExpected.GetEnumerator()) {
        if ($qtbase.($entry.Key) -cne $entry.Value) { throw "Qtbase provenance '$($entry.Key)' drifted." }
    }

    Assert-M0ExactProperties $Contract.recipe @('files', 'runner_export') 'contract.recipe'
    if ($Contract.recipe.files -isnot [array] -or @($Contract.recipe.files).Count -ne 3 -or
        $Contract.recipe.runner_export -isnot [string] -or
        $Contract.recipe.runner_export -cne 'Invoke-M0QtShellCompositeNative') {
        throw 'Composite recipe shape or native runner binding drifted.'
    }
    for ($index = 0; $index -lt 3; $index++) {
        $file = $Contract.recipe.files[$index]
        Assert-M0ExactProperties $file @('id', 'relative_path', 'size_bytes', 'sha256') `
            "contract.recipe.files[$index]"
        if ($file.id -cne $script:RecipeIds[$index] -or $file.relative_path -cne $script:RecipePaths[$index]) {
            throw "Composite recipe member $index identity or path drifted."
        }
        $null = ConvertTo-M0Int64 $file.size_bytes "Recipe '$($file.id)' size_bytes"
        Assert-M0Digest $file.sha256 "Recipe '$($file.id)' sha256"
    }

    Assert-M0ExactProperties $Contract.inputs @('extractor', 'archives') 'contract.inputs'
    $extractor = $Contract.inputs.extractor
    Assert-M0ExactProperties $extractor @('id', 'version', 'relative_path', 'size_bytes', 'sha256') `
        'contract.inputs.extractor'
    if ($extractor.id -cne '7zip' -or $extractor.version -cne '26.02' -or
        $extractor.relative_path -cne 'out/m0/toolchains/7zip-26.02/x64/7za.exe' -or
        $extractor.size_bytes -isnot [long] -or $extractor.size_bytes -ne 1334784 -or
        $extractor.sha256 -cne '35d4d69d7cd6cb44558f208c3b1334268013f9daf82d2dda848893a1c30c59c2') {
        throw 'Composite extractor pin drifted.'
    }
    if ($Contract.inputs.archives -isnot [array] -or @($Contract.inputs.archives).Count -ne 3) {
        throw 'Composite archive input sequence drifted.'
    }
    for ($index = 0; $index -lt 3; $index++) {
        $archive = $Contract.inputs.archives[$index]
        $expected = $script:ExpectedArchiveSpecs[$index]
        Assert-M0ExactProperties $archive @(
            'owner_id', 'archive_id', 'relative_path', 'size_bytes', 'sha256',
            'expected_file_count', 'expected_total_size_bytes'
        ) "contract.inputs.archives[$index]"
        Assert-M0SafeRelativePath $archive.relative_path "Archive '$($archive.owner_id)' relative_path"
        if ($archive.owner_id -cne $expected.owner_id -or $archive.archive_id -cne $expected.archive_id -or
            $archive.relative_path -cne $expected.relative_path -or
            $archive.size_bytes -cne $expected.size_bytes -or $archive.sha256 -cne $expected.sha256 -or
            $archive.expected_file_count -cne $expected.file_count -or
            $archive.expected_total_size_bytes -cne $expected.total_size_bytes) {
            throw "Composite archive pin '$($expected.owner_id)' drifted."
        }
    }

    Assert-M0ExactProperties $Contract.ownership @(
        'owners', 'path_comparer', 'leaf_collision_policy', 'directory_overlap_allowed',
        'allow_hardlinks', 'allow_reparse_points', 'allow_ads', 'official_sdk_fallback'
    ) 'contract.ownership'
    Assert-M0Sequence $Contract.ownership.owners $script:OwnerIds 'contract.ownership.owners'
    if ($Contract.ownership.path_comparer -cne 'OrdinalIgnoreCase' -or
        $Contract.ownership.leaf_collision_policy -cne 'reject-even-identical' -or
        $Contract.ownership.directory_overlap_allowed -isnot [bool] -or
        -not $Contract.ownership.directory_overlap_allowed) {
        throw 'Composite ownership comparison or collision policy drifted.'
    }
    foreach ($flag in @('allow_hardlinks', 'allow_reparse_points', 'allow_ads', 'official_sdk_fallback')) {
        if ($Contract.ownership.$flag -isnot [bool] -or $Contract.ownership.$flag) {
            throw "Unsafe composite ownership flag '$flag' drifted."
        }
    }

    if ($Contract.required_artifacts -isnot [array] -or
        @($Contract.required_artifacts).Count -ne $script:ExpectedRequiredArtifactOwners.Count) {
        throw 'Composite required-artifact closure drifted.'
    }
    $requiredSeen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for ($index = 0; $index -lt @($Contract.required_artifacts).Count; $index++) {
        $artifact = $Contract.required_artifacts[$index]
        Assert-M0ExactProperties $artifact @('owner_id', 'relative_path', 'size_bytes', 'sha256') `
            "contract.required_artifacts[$index]"
        Assert-M0SafeRelativePath $artifact.relative_path "Required artifact $index relative_path"
        if (-not $requiredSeen.Add([string]$artifact.relative_path)) {
            throw 'Composite required artifacts contain a case-insensitive duplicate path.'
        }
        if (-not $script:ExpectedRequiredArtifactOwners.Contains($artifact.relative_path) -or
            $script:ExpectedRequiredArtifactOwners[$artifact.relative_path] -cne $artifact.owner_id) {
            throw "Required artifact owner or path drifted: $($artifact.relative_path)"
        }
        $null = ConvertTo-M0Int64 $artifact.size_bytes "Required artifact '$($artifact.relative_path)' size_bytes"
        Assert-M0Digest $artifact.sha256 "Required artifact '$($artifact.relative_path)' sha256"
    }

    Assert-M0ExactProperties $Contract.isolation @(
        'run_parent_relative_path', 'require_new_direct_child', 'sdk_relative_path',
        'global_inventory_relative_path', 'owner_inventory_directory_relative_path',
        'manifest_relative_path', 'receipt_relative_path'
    ) 'contract.isolation'
    $isolationExpected = [ordered]@{
        run_parent_relative_path = 'q'; require_new_direct_child = $true; sdk_relative_path = 'sdk'
        global_inventory_relative_path = 'evidence/composite-inventory.tsv'
        owner_inventory_directory_relative_path = 'evidence/inventories'
        manifest_relative_path = 'evidence/qt-shell-composite-manifest.v2.json'
        receipt_relative_path = 'evidence/qt-shell-composite-receipt.v2.json'
    }
    foreach ($entry in $isolationExpected.GetEnumerator()) {
        if ($Contract.isolation.($entry.Key) -cne $entry.Value) {
            throw "Composite isolation '$($entry.Key)' drifted."
        }
    }
    foreach ($pathProperty in @(
            'run_parent_relative_path', 'sdk_relative_path', 'global_inventory_relative_path',
            'owner_inventory_directory_relative_path', 'manifest_relative_path', 'receipt_relative_path')) {
        Assert-M0SafeRelativePath $Contract.isolation.$pathProperty "Isolation $pathProperty"
    }
    Assert-M0Sequence $Contract.limits $script:Limits 'contract.limits'
    return $Contract
}

function Assert-M0QtShellCompositeContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedSha256
    )

    Assert-M0Digest $ExpectedSha256 'Expected composite contract SHA-256'
    if (-not [IO.File]::Exists($LiteralPath)) { throw 'Composite contract is missing.' }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $lease = $null
    try {
        $lease = Open-M0PhysicalFile `
            -LiteralPath $full `
            -BoundaryRoot ([IO.Path]::GetDirectoryName($full)) `
            -Context 'Composite contract' `
            -ExpectedSha256 $ExpectedSha256 `
            -RequireSingleLink
        $contract = Read-M0ClosedJson $lease.path 'Composite contract'
        return Assert-M0QtShellCompositeContractDocument $contract
    }
    finally { if ($null -ne $lease) { $lease.stream.Dispose() } }
}

function Assert-M0QtShellCompositeArchiveMembers {
    param(
        [Parameter(Mandatory)] [object[]] $Members,
        [Collections.IDictionary] $Metrics
    )

    if ($Members.Count -eq 0) { throw 'Composite archive must contain at least one member.' }
    if ($null -ne $Metrics) {
        if (-not $Metrics.Contains('path_probes') -or $Metrics['path_probes'] -isnot [long] -or
            [long]$Metrics['path_probes'] -lt 0) {
            throw 'Composite archive metrics must contain a nonnegative Int64 path_probes counter.'
        }
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $leaves = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $ancestorPrefixes = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($member in $Members) {
        if ($null -eq $member) { throw 'Composite archive member must not be null.' }
        Assert-M0ExactProperties $member @('path', 'kind', 'size_bytes') 'composite archive member'
        Assert-M0SafeRelativePath $member.path 'Composite archive member path'
        foreach ($segment in @([string]$member.path -split '/')) {
            if ($segment.EndsWith(' ', [StringComparison]::Ordinal) -or
                $segment.EndsWith('.', [StringComparison]::Ordinal) -or
                $segment.IndexOfAny([char[]]'<>"|?*') -ge 0 -or
                [IO.Path]::GetFileNameWithoutExtension($segment) -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') {
                throw 'Composite archive member uses an unsafe Windows namespace alias.'
            }
        }
        if ($member.kind -isnot [string] -or $member.kind -cnotin @('file', 'directory')) {
            throw 'Composite archive links and non-regular members are forbidden.'
        }
        $size = ConvertTo-M0Int64 $member.size_bytes 'Composite archive member size_bytes' -AllowZero
        if ($member.kind -ceq 'directory' -and $size -ne 0) {
            throw 'Composite archive directory member has an invalid nonzero size.'
        }
        $path = [string]$member.path
        if ($null -ne $Metrics) { $Metrics['path_probes'] = [long]$Metrics['path_probes'] + 1 }
        if (-not $seen.Add($path)) {
            throw 'Composite archive contains a duplicate or case-aliased member path.'
        }

        $segments = @($path -split '/')
        $prefixBuilder = [Text.StringBuilder]::new()
        for ($index = 0; $index -lt ($segments.Count - 1); $index++) {
            if ($index -gt 0) { [void]$prefixBuilder.Append('/') }
            [void]$prefixBuilder.Append($segments[$index])
            $prefix = $prefixBuilder.ToString()
            if ($null -ne $Metrics) { $Metrics['path_probes'] = [long]$Metrics['path_probes'] + 1 }
            if ($leaves.Contains($prefix)) {
                throw 'Composite archive contains a file/directory namespace alias.'
            }
        }
        if ($member.kind -ceq 'file') {
            if ($null -ne $Metrics) { $Metrics['path_probes'] = [long]$Metrics['path_probes'] + 1 }
            if ($ancestorPrefixes.Contains($path)) {
                throw 'Composite archive contains a file/directory namespace alias.'
            }
            [void]$leaves.Add($path)
        }
        $prefixBuilder.Clear() | Out-Null
        for ($index = 0; $index -lt ($segments.Count - 1); $index++) {
            if ($index -gt 0) { [void]$prefixBuilder.Append('/') }
            [void]$prefixBuilder.Append($segments[$index])
            [void]$ancestorPrefixes.Add($prefixBuilder.ToString())
        }
    }
    return $true
}

function Test-M0RelativePathOverlap {
    param([Parameter(Mandatory)] [string] $First, [Parameter(Mandatory)] [string] $Second)
    if ([string]::Equals($First, $Second, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    return $First.StartsWith($Second + '/', [StringComparison]::OrdinalIgnoreCase) -or
        $Second.StartsWith($First + '/', [StringComparison]::OrdinalIgnoreCase)
}

function Add-M0LeafPathToCollisionIndex {
    param(
        [Parameter(Mandatory)] [string] $Path,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.HashSet[string]] $Leaves,
        [Parameter(Mandatory)] [AllowEmptyCollection()] [Collections.Generic.HashSet[string]] $AncestorPrefixes,
        [Parameter(Mandatory)] [string] $CollisionMessage,
        [Collections.IDictionary] $Metrics
    )
    $segments = @($Path -split '/')
    $prefixBuilder = [Text.StringBuilder]::new()
    for ($index = 0; $index -lt ($segments.Count - 1); $index++) {
        if ($index -gt 0) { [void]$prefixBuilder.Append('/') }
        [void]$prefixBuilder.Append($segments[$index])
        if ($null -ne $Metrics) { $Metrics['path_probes'] = [long]$Metrics['path_probes'] + 1 }
        if ($Leaves.Contains($prefixBuilder.ToString())) { throw $CollisionMessage }
    }
    if ($null -ne $Metrics) { $Metrics['path_probes'] = [long]$Metrics['path_probes'] + 2 }
    if ($Leaves.Contains($Path) -or $AncestorPrefixes.Contains($Path)) { throw $CollisionMessage }
    [void]$Leaves.Add($Path)
    $prefixBuilder.Clear() | Out-Null
    for ($index = 0; $index -lt ($segments.Count - 1); $index++) {
        if ($index -gt 0) { [void]$prefixBuilder.Append('/') }
        [void]$prefixBuilder.Append($segments[$index])
        [void]$AncestorPrefixes.Add($prefixBuilder.ToString())
    }
}

function New-M0QtShellCompositeOwnershipPlan {
    param(
        [Parameter(Mandatory)] [Collections.IDictionary] $OwnerEntries,
        [Collections.IDictionary] $Metrics
    )

    if ($OwnerEntries.Count -ne $script:OwnerIds.Count) {
        throw 'Composite ownership entries must contain exactly four owners.'
    }
    $keys = @($OwnerEntries.Keys)
    Assert-M0Sequence $keys $script:OwnerIds 'Composite ownership owner order'
    if ($null -ne $Metrics) {
        if (-not $Metrics.Contains('path_probes') -or $Metrics['path_probes'] -isnot [long] -or
            [long]$Metrics['path_probes'] -lt 0) {
            throw 'Composite ownership metrics must contain a nonnegative Int64 path_probes counter.'
        }
    }
    $result = [Collections.Generic.List[object]]::new()
    $destinationLeaves = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $destinationAncestors = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($ownerId in $script:OwnerIds) {
        $sourceLeaves = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $sourceAncestors = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $entries = @($OwnerEntries[$ownerId])
        if ($entries.Count -eq 0) { throw "Composite owner '$ownerId' has no leaves." }
        foreach ($entry in $entries) {
            Assert-M0ExactProperties $entry @('source_relative_path', 'relative_path', 'size_bytes', 'sha256') `
                "Composite owner '$ownerId' entry"
            Assert-M0SafeRelativePath $entry.source_relative_path "Composite owner '$ownerId' source path"
            Assert-M0SafeRelativePath $entry.relative_path "Composite owner '$ownerId' destination path"
            $size = ConvertTo-M0Int64 $entry.size_bytes "Composite owner '$ownerId' size_bytes" -AllowZero
            Assert-M0Digest $entry.sha256 "Composite owner '$ownerId' sha256"
            Add-M0LeafPathToCollisionIndex `
                -Path ([string]$entry.source_relative_path) `
                -Leaves $sourceLeaves `
                -AncestorPrefixes $sourceAncestors `
                -CollisionMessage "Composite owner '$ownerId' has a duplicate or case-insensitive source collision." `
                -Metrics $Metrics
            Add-M0LeafPathToCollisionIndex `
                -Path ([string]$entry.relative_path) `
                -Leaves $destinationLeaves `
                -AncestorPrefixes $destinationAncestors `
                -CollisionMessage 'Composite ownership has a case-insensitive destination collision.' `
                -Metrics $Metrics
            $result.Add([pscustomobject][ordered]@{
                    owner_id = $ownerId
                    source_relative_path = [string]$entry.source_relative_path
                    relative_path = [string]$entry.relative_path
                    size_bytes = [long]$size
                    sha256 = [string]$entry.sha256
                })
        }
    }
    return $result.ToArray()
}

function Get-M0TreeSnapshot {
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $Context,
        [Collections.Generic.List[object]] $LeaseSink,
        [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Collections.Generic.Dictionary[string,string]] $FileLedger,
        [scriptblock] $AfterSweep,
        $RootAnchor
    )
    $root = ConvertTo-M0NormalizedDirectoryPath $TreeRoot
    if (-not [IO.Directory]::Exists($root)) { throw "$Context root is missing." }
    $ownedLeases = [Collections.Generic.List[object]]::new()
    $directoryAnchors = [Collections.Generic.List[object]]::new()
    try {
        $ownsRootAnchor = $null -eq $RootAnchor
        $rootAnchor = if ($null -ne $RootAnchor) {
            Assert-M0DirectoryAnchorUnchanged $RootAnchor "$Context retained root"
            if (-not [string]::Equals($RootAnchor.path, $root, [StringComparison]::OrdinalIgnoreCase)) {
                throw "$Context retained root anchor belongs to a different path."
            }
            $RootAnchor
        }
        else { Open-M0DirectoryAnchor $root $root "$Context root" -Immutable }
        $directoryAnchors.Add($rootAnchor)
        if ($ownsRootAnchor) {
            if ($null -ne $LeaseSink) { $LeaseSink.Add($rootAnchor) } else { $ownedLeases.Add($rootAnchor) }
        }
        if ($null -ne $DirectoryLedger) {
            Add-M0OwnedIdentity $DirectoryLedger $root $rootAnchor.identity $Context
        }

        $files = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
        $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $queue = [Collections.Generic.Queue[object]]::new()
        $queue.Enqueue([pscustomobject]@{ path = $root; anchor = $rootAnchor })
        while ($queue.Count -gt 0) {
            $current = $queue.Dequeue()
            Assert-M0DirectoryAnchorUnchanged $current.anchor "$Context sweep"
            foreach ($entry in [IO.DirectoryInfo]::new($current.path).EnumerateFileSystemInfos()) {
                if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    throw "$Context contains a reparse point: $($entry.FullName)"
                }
                $relative = [IO.Path]::GetRelativePath($root, $entry.FullName).Replace('\', '/')
                Assert-M0SafeRelativePath $relative "$Context relative path"
                if ($entry -is [IO.DirectoryInfo]) {
                    if (-not $directories.Add($relative)) { throw "$Context contains a case-aliased directory." }
                    $anchor = Open-M0DirectoryAnchor $entry.FullName $root "$Context directory '$relative'" -Immutable
                    $directoryAnchors.Add($anchor)
                    if ($null -ne $LeaseSink) { $LeaseSink.Add($anchor) } else { $ownedLeases.Add($anchor) }
                    if ($null -ne $DirectoryLedger) {
                        Add-M0OwnedIdentity $DirectoryLedger $entry.FullName $anchor.identity $Context
                    }
                    $queue.Enqueue([pscustomobject]@{ path = $entry.FullName; anchor = $anchor })
                }
                else {
                    if ($directories.Contains($relative) -or $files.ContainsKey($relative)) {
                        throw "$Context contains a case-insensitive path collision."
                    }
                    $lease = Open-M0PhysicalFile `
                        -LiteralPath $entry.FullName `
                        -BoundaryRoot $root `
                        -Context "$Context leaf '$relative'" `
                        -RequireSingleLink
                    $files.Add($relative, [pscustomobject][ordered]@{
                            source_relative_path = $relative
                            relative_path = $relative
                            size_bytes = [long]$lease.size_bytes
                            sha256 = [string]$lease.sha256
                        })
                    if ($null -ne $FileLedger) {
                        Add-M0OwnedIdentity $FileLedger $entry.FullName $lease.identity $Context
                    }
                    if ($null -ne $LeaseSink) { $LeaseSink.Add($lease) } else { $ownedLeases.Add($lease) }
                }
            }
        }
        $paths = [string[]]@($files.Keys)
        [Array]::Sort($paths, [StringComparer]::OrdinalIgnoreCase)
        $rows = [Collections.Generic.List[object]]::new()
        foreach ($path in $paths) { $rows.Add($files[$path]) }
        $snapshot = [pscustomobject][ordered]@{
            root = $root
            entries = $rows.ToArray()
            directories = $directories
            file_count = [long]$rows.Count
            total_size_bytes = [long](($rows | Measure-Object -Property size_bytes -Sum).Sum)
        }
        $envelope = [pscustomobject][ordered]@{
            lease_kind = 'tree'; root = $root; context = $Context
            files = $files; directories = $directories; directory_anchors = $directoryAnchors
        }
        if ($null -ne $AfterSweep) { $null = & $AfterSweep $snapshot }
        Assert-M0TreeEnvelopeUnchanged $envelope "$Context final sweep"
        if ($null -ne $LeaseSink) { $LeaseSink.Add($envelope) }
        return $snapshot
    }
    finally {
        if ($null -eq $LeaseSink) { Close-M0Leases $ownedLeases }
    }
}

function Assert-M0TreeEnvelopeUnchanged {
    param([Parameter(Mandatory)] $Envelope, [Parameter(Mandatory)] [string] $Context)
    foreach ($anchor in @($Envelope.directory_anchors)) {
        Assert-M0DirectoryAnchorUnchanged $anchor $Context
    }
    $seenFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $seenDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $queue = [Collections.Generic.Queue[IO.DirectoryInfo]]::new()
    $queue.Enqueue([IO.DirectoryInfo]::new([string]$Envelope.root))
    while ($queue.Count -gt 0) {
        $directory = $queue.Dequeue()
        Assert-M0NoAlternateDataStreams $directory.FullName "$Context directory"
        foreach ($entry in $directory.EnumerateFileSystemInfos()) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Context tree envelope contains a reparse point."
            }
            $relative = [IO.Path]::GetRelativePath($Envelope.root, $entry.FullName).Replace('\', '/')
            if ($entry -is [IO.DirectoryInfo]) {
                if (-not $seenDirectories.Add($relative) -or
                    -not $Envelope.directories.Contains($relative)) {
                    throw "$Context tree envelope contains an extra or case-aliased directory."
                }
                $queue.Enqueue([IO.DirectoryInfo]$entry)
            }
            else {
                if (-not $seenFiles.Add($relative) -or -not $Envelope.files.ContainsKey($relative)) {
                    throw "$Context tree envelope contains an extra or case-aliased file."
                }
                $expected = $Envelope.files[$relative]
                if ($expected.relative_path -cne $relative) {
                    throw "$Context tree envelope leaf casing drifted."
                }
                $lease = Open-M0PhysicalFile $entry.FullName $Envelope.root `
                    "$Context tree envelope leaf '$relative'" `
                    ([long]$expected.size_bytes) ([string]$expected.sha256) -RequireSingleLink
                $lease.stream.Dispose()
            }
        }
    }
    if ($seenFiles.Count -ne $Envelope.files.Count -or
        $seenDirectories.Count -ne $Envelope.directories.Count) {
        throw "$Context tree envelope contains an extra or missing entry."
    }
}

function Assert-M0ExactSdkTree {
    param(
        [Parameter(Mandatory)] [string] $SdkRoot,
        [Parameter(Mandatory)] [object[]] $Plan,
        [Collections.Generic.List[object]] $LeaseSink,
        [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Collections.Generic.Dictionary[string,string]] $FileLedger,
        [scriptblock] $AfterSweep
    )

    $snapshot = Get-M0TreeSnapshot -TreeRoot $SdkRoot -Context 'Composite physical SDK' `
        -LeaseSink $LeaseSink -DirectoryLedger $DirectoryLedger -FileLedger $FileLedger `
        -AfterSweep $AfterSweep
    $expectedTotal = [long](($Plan | Measure-Object -Property size_bytes -Sum).Sum)
    if ($snapshot.file_count -ne $Plan.Count -or $snapshot.total_size_bytes -ne $expectedTotal) {
        throw 'Composite physical SDK contains an extra or missing leaf or byte.'
    }

    $actualByPath = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $snapshot.entries) { $actualByPath.Add([string]$entry.relative_path, $entry) }
    $expectedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Plan) {
        $relativePath = [string]$row.relative_path
        if (-not $actualByPath.ContainsKey($relativePath)) {
            throw "Composite physical SDK is missing owned leaf '$relativePath'."
        }
        $actual = $actualByPath[$relativePath]
        if ($actual.relative_path -cne $relativePath -or
            $actual.size_bytes -ne [long]$row.size_bytes -or $actual.sha256 -cne [string]$row.sha256) {
            throw "Composite physical SDK leaf path, hash, or size drifted: $relativePath"
        }
        $parent = [IO.Path]::GetDirectoryName($relativePath.Replace('/', '\'))
        while (-not [string]::IsNullOrEmpty($parent)) {
            [void]$expectedDirectories.Add($parent.Replace('\', '/'))
            $parent = [IO.Path]::GetDirectoryName($parent)
        }
    }
    if ($snapshot.directories.Count -ne $expectedDirectories.Count) {
        throw 'Composite physical SDK contains an extra or missing directory.'
    }
    foreach ($directory in $snapshot.directories) {
        if (-not $expectedDirectories.Contains($directory)) {
            throw "Composite physical SDK contains an unexpected directory: $directory"
        }
    }
    return $snapshot
}

function ConvertFrom-M0CanonicalInventory {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [ValidateSet(3, 5)] [int] $ColumnCount,
        [string] $ExpectedOwnerId,
        [Parameter(Mandatory)] [string] $Context
    )
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    if ($text.Contains("`r") -or ($text.Length -gt 0 -and $text[0] -eq [char]0xFEFF) -or
        -not $text.EndsWith("`n", [StringComparison]::Ordinal)) {
        throw "$Context is not canonical BOM-free LF text."
    }
    $rows = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in @($text.Substring(0, $text.Length - 1).Split("`n"))) {
        if ([string]::IsNullOrEmpty($line)) { throw "$Context contains an empty inventory row." }
        $columns = @($line.Split("`t"))
        if ($columns.Count -ne $ColumnCount) { throw "$Context row shape drifted." }
        if ($ColumnCount -eq 3) {
            $sourcePath = $columns[0]; $destinationPath = $columns[0]
            $ownerId = $ExpectedOwnerId; $sizeText = $columns[1]; $digest = $columns[2]
        }
        else {
            $destinationPath = $columns[0]; $ownerId = $columns[1]
            $sourcePath = $columns[2]; $sizeText = $columns[3]; $digest = $columns[4]
            if (-not [string]::IsNullOrEmpty($ExpectedOwnerId) -and $ownerId -cne $ExpectedOwnerId) {
                throw "$Context contains the wrong owner ID."
            }
        }
        Assert-M0SafeRelativePath $sourcePath "$Context source path"
        Assert-M0SafeRelativePath $destinationPath "$Context destination path"
        if (-not $seen.Add($destinationPath)) { throw "$Context contains a duplicate case-insensitive path." }
        $size = [long]0
        if (-not [long]::TryParse($sizeText, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$size) -or $size -lt 0 -or
            $size.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $sizeText) {
            throw "$Context contains a noncanonical size."
        }
        Assert-M0Digest $digest "$Context SHA-256"
        $rows.Add([pscustomobject][ordered]@{
                owner_id = $ownerId; source_relative_path = $sourcePath; relative_path = $destinationPath
                size_bytes = [long]$size; sha256 = $digest
            })
    }
    return [pscustomobject][ordered]@{ text = $text; bytes = $bytes; rows = $rows.ToArray() }
}

function ConvertTo-M0InventoryText {
    param([Parameter(Mandatory)] [object[]] $Rows)
    $builder = [Text.StringBuilder]::new()
    foreach ($row in $Rows) {
        [void]$builder.Append($row.relative_path).Append("`t").Append($row.owner_id).Append("`t").Append(
            $row.source_relative_path).Append("`t").Append(
            ([long]$row.size_bytes).ToString([Globalization.CultureInfo]::InvariantCulture)).Append("`t").Append(
            $row.sha256).Append("`n")
    }
    return $builder.ToString()
}

function Assert-M0FileReference {
    param($Reference, [Parameter(Mandatory)] [string] $Context)
    Assert-M0ExactProperties $Reference @('relative_path', 'size_bytes', 'sha256') $Context
    Assert-M0SafeRelativePath $Reference.relative_path "$Context relative_path"
    $null = ConvertTo-M0Int64 $Reference.size_bytes "$Context size_bytes"
    Assert-M0Digest $Reference.sha256 "$Context sha256"
}

function Assert-M0QtShellCompositeReceiptDocument {
    param(
        [Parameter(Mandatory)] $Receipt,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256
    )
    Assert-M0Digest $ExpectedContractSha256 'Receipt contract anchor'
    Assert-M0ExactProperties $Receipt @(
        'schema_version', 'receipt_id', 'composite_id', 'captured_utc', 'contract_sha256',
        'execution', 'qtbase_receipt', 'sdk', 'evidence', 'limits'
    ) 'composite receipt'
    if ($Receipt.schema_version -isnot [long] -or $Receipt.schema_version -ne 2 -or
        $Receipt.receipt_id -isnot [string] -or
        $Receipt.receipt_id -cne 'm0.2-qt-shell-composite-sdk-v2-receipt' -or
        $Receipt.composite_id -isnot [string] -or $Receipt.composite_id -cne $Contract.composite_id -or
        $Receipt.contract_sha256 -isnot [string] -or
        $Receipt.contract_sha256 -cne $ExpectedContractSha256) {
        throw 'Composite receipt identity or external contract SHA-256 anchor drifted.'
    }
    Assert-M0CanonicalUtcTimestamp $Receipt.captured_utc 'Receipt captured_utc'
    Assert-M0ExactProperties $Receipt.execution @('exit_code', 'stdout', 'stderr') 'receipt.execution'
    if ($Receipt.execution.exit_code -isnot [long] -or $Receipt.execution.exit_code -ne 0 -or
        $Receipt.execution.stdout -isnot [string] -or $Receipt.execution.stderr -isnot [string]) {
        throw 'Composite receipt execution must be a successful closed observation.'
    }
    Assert-M0FileReference $Receipt.qtbase_receipt 'receipt.qtbase_receipt'
    $parent = $Contract.provenance.qtbase_refresh
    if ($Receipt.qtbase_receipt.relative_path -cne $parent.receipt_relative_path -or
        $Receipt.qtbase_receipt.size_bytes -cne $parent.receipt_size_bytes -or
        $Receipt.qtbase_receipt.sha256 -cne $parent.receipt_sha256) {
        throw 'Composite receipt parent Qtbase receipt binding drifted.'
    }
    Assert-M0ExactProperties $Receipt.sdk @(
        'root_relative_path', 'file_count', 'total_size_bytes',
        'inventory_relative_path', 'inventory_size_bytes', 'inventory_sha256'
    ) 'receipt.sdk'
    if ($Receipt.sdk.root_relative_path -isnot [string] -or
        $Receipt.sdk.root_relative_path -cne $Contract.isolation.sdk_relative_path -or
        $Receipt.sdk.inventory_relative_path -isnot [string] -or
        $Receipt.sdk.inventory_relative_path -cne $Contract.isolation.global_inventory_relative_path) {
        throw 'Composite receipt SDK or inventory path drifted.'
    }
    $null = ConvertTo-M0Int64 $Receipt.sdk.file_count 'receipt.sdk.file_count'
    $null = ConvertTo-M0Int64 $Receipt.sdk.total_size_bytes 'receipt.sdk.total_size_bytes'
    $null = ConvertTo-M0Int64 $Receipt.sdk.inventory_size_bytes 'receipt.sdk.inventory_size_bytes'
    Assert-M0Digest $Receipt.sdk.inventory_sha256 'receipt.sdk.inventory_sha256'
    Assert-M0FileReference $Receipt.evidence 'receipt.evidence'
    if ($Receipt.evidence.relative_path -cne $Contract.isolation.manifest_relative_path) {
        throw 'Composite receipt evidence manifest path drifted.'
    }
    Assert-M0Sequence $Receipt.limits $script:Limits 'receipt.limits'
    return $true
}

function Invoke-M0BoundedProcess {
    param(
        [Parameter(Mandatory)] [string] $FilePath,
        [Parameter(Mandatory)] [string[]] $ArgumentList,
        [Parameter(Mandatory)] [string] $WorkingDirectory,
        [int] $TimeoutSeconds = 900,
        [long] $MaximumCapturedBytes = 67108864
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    $start.WorkingDirectory = $WorkingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($argument in $ArgumentList) { [void]$start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Authenticated extractor process did not start.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            try { $process.Kill($true) } catch { }
            $process.WaitForExit()
            throw 'Authenticated extractor process exceeded its bounded timeout.'
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        if ($script:Utf8NoBom.GetByteCount($stdout) -gt $MaximumCapturedBytes -or
            $script:Utf8NoBom.GetByteCount($stderr) -gt $MaximumCapturedBytes) {
            throw 'Authenticated extractor output exceeded its bounded capture limit.'
        }
        return [pscustomobject][ordered]@{
            exit_code = [long]$process.ExitCode; stdout = $stdout; stderr = $stderr
        }
    }
    finally { $process.Dispose() }
}

function ConvertFrom-M0SevenZipListing {
    param([Parameter(Mandatory)] [string] $Text)

    $members = [Collections.Generic.List[object]]::new()
    $record = [ordered]@{}
    $inside = $false
    $flush = {
        if (-not $inside -or -not $record.Contains('Path')) { $record.Clear(); return }
        $kind = if ($record.Contains('Symbolic Link') -or $record.Contains('Hard Link')) {
            'link'
        }
        elseif (($record.Contains('Folder') -and [string]$record['Folder'] -ceq '+') -or
            ($record.Contains('Attributes') -and [string]$record['Attributes'] -match '^D')) {
            'directory'
        }
        else { 'file' }
        $size = [long]0
        if ($record.Contains('Size') -and -not [string]::IsNullOrEmpty([string]$record['Size'])) {
            if (-not [long]::TryParse([string]$record['Size'], [Globalization.NumberStyles]::None,
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$size) -or $size -lt 0) {
                throw 'Composite archive listing contains an invalid member size.'
            }
        }
        $members.Add([pscustomobject][ordered]@{
                path = ([string]$record['Path']).Replace('\', '/').TrimEnd('/')
                kind = $kind
                size_bytes = [long]$size
            })
        $record.Clear()
    }
    foreach ($line in @($Text.Replace("`r`n", "`n").Replace("`r", "`n") -split "`n")) {
        if ($line -match '^-{5,}\s*$') { & $flush; $inside = $true; continue }
        if ([string]::IsNullOrWhiteSpace($line)) { & $flush; continue }
        if ($line -match '^([^=]+?)\s*=\s*(.*)$') { $record[$matches[1].Trim()] = $matches[2] }
    }
    & $flush
    if ($members.Count -eq 0) { throw 'Composite archive listing did not contain member records.' }
    $null = Assert-M0QtShellCompositeArchiveMembers $members.ToArray()
    return $members.ToArray()
}

function Assert-M0ExtractedArchive {
    param(
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [object[]] $Members,
        [Parameter(Mandatory)] $ArchiveSpec,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $LeaseSink,
        [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Collections.Generic.Dictionary[string,string]] $FileLedger
    )
    $expectedFiles = [Collections.Generic.Dictionary[string,long]]::new([StringComparer]::OrdinalIgnoreCase)
    $expectedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($member in $Members) {
        if ($member.kind -ceq 'directory') { [void]$expectedDirectories.Add([string]$member.path); continue }
        $expectedFiles.Add([string]$member.path, [long]$member.size_bytes)
        $parent = [IO.Path]::GetDirectoryName(([string]$member.path).Replace('/', '\'))
        while (-not [string]::IsNullOrEmpty($parent)) {
            [void]$expectedDirectories.Add($parent.Replace('\', '/'))
            $parent = [IO.Path]::GetDirectoryName($parent)
        }
    }
    if ($expectedFiles.Count -ne [long]$ArchiveSpec.expected_file_count -or
        [long](($expectedFiles.Values | Measure-Object -Sum).Sum) -ne [long]$ArchiveSpec.expected_total_size_bytes) {
        throw "Archive '$($ArchiveSpec.owner_id)' listing count or total size drifted from the contract."
    }
    $snapshot = Get-M0TreeSnapshot -TreeRoot $Root `
        -Context "Extracted archive '$($ArchiveSpec.owner_id)'" -LeaseSink $LeaseSink `
        -DirectoryLedger $DirectoryLedger -FileLedger $FileLedger
    if ($snapshot.file_count -ne $expectedFiles.Count -or
        $snapshot.total_size_bytes -ne [long]$ArchiveSpec.expected_total_size_bytes -or
        $snapshot.directories.Count -ne $expectedDirectories.Count) {
        throw "Extracted archive '$($ArchiveSpec.owner_id)' has extra or missing physical entries."
    }
    foreach ($directory in $snapshot.directories) {
        if (-not $expectedDirectories.Contains($directory)) {
            throw "Extracted archive '$($ArchiveSpec.owner_id)' has an unexpected directory."
        }
    }
    foreach ($entry in $snapshot.entries) {
        if (-not $expectedFiles.ContainsKey($entry.relative_path) -or
            $expectedFiles[$entry.relative_path] -ne $entry.size_bytes) {
            throw "Extracted archive '$($ArchiveSpec.owner_id)' differs from its safe listing."
        }
    }
    return $snapshot
}

function Write-M0CreateNewFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $BoundaryRoot,
        [Parameter(Mandatory)] [string] $Context,
        [scriptblock] $AfterFlush,
        [scriptblock] $BeforeCommit,
        [scriptblock] $ValidateCommitEnvelope,
        [Collections.Generic.Dictionary[string,string]] $DirectoryLedger,
        [Collections.Generic.Dictionary[string,string]] $FileLedger
    )
    $full = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-M0PathWithin $full $BoundaryRoot)) { throw "$Context escaped its publication boundary." }
    if ([IO.File]::Exists($full) -or [IO.Directory]::Exists($full)) {
        throw "$Context destination already exists; publication will not replace it."
    }
    $parent = [IO.Path]::GetDirectoryName($full)
    $createdParentAnchor = $null
    if (-not [IO.Directory]::Exists($parent)) {
        $createdParentAnchor = New-M0AtomicDirectoryAnchor $parent $BoundaryRoot "$Context new parent"
    }
    Assert-M0NoReparseChain $parent $BoundaryRoot "$Context parent"
    Assert-M0NoAlternateDataStreams $parent "$Context parent"

    $pending = Join-Path $parent (
        '.' + [IO.Path]::GetFileName($full) + '.' + [Guid]::NewGuid().ToString('N') + '.pending')
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
    $stream = $null
    $parentAnchor = $null
    $pendingIdentity = $null
    $published = $false
    try {
        if ($null -ne $createdParentAnchor) {
            $parentAnchor = $createdParentAnchor
            $createdParentAnchor = $null
        }
        else {
            $parentAnchor = Open-M0DirectoryAnchor $parent $BoundaryRoot "$Context parent anchor"
        }
        if ($null -ne $DirectoryLedger) {
            $current = $parent
            $boundary = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
            while ($true) {
                $identity = if ([string]::Equals($current, $parent, [StringComparison]::OrdinalIgnoreCase)) {
                    $parentAnchor.identity
                }
                else { Get-M0DirectoryIdentity $current $boundary "$Context owned directory" }
                Add-M0OwnedIdentity $DirectoryLedger $current $identity $Context
                if ([string]::Equals($current, $boundary, [StringComparison]::OrdinalIgnoreCase)) { break }
                $current = [IO.Path]::GetDirectoryName($current)
            }
        }
        $safe = $script:PhysicalFile::CreateNewReadWriteDelete((ConvertTo-M0ExtendedPath $pending))
        try {
            $stream = [IO.FileStream]::new($safe, [IO.FileAccess]::ReadWrite, 4096, $false)
            $safe = $null
        }
        finally { if ($null -ne $safe) { $safe.Dispose() } }
        $pendingIdentity = [string]$script:PhysicalFile::GetIdentity($stream.SafeFileHandle)
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
        if ($null -ne $AfterFlush) { $null = & $AfterFlush $pending $full $stream }

        $pendingFinal = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                    $script:PhysicalFile::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($pendingFinal, $pending, [StringComparison]::OrdinalIgnoreCase) -or
            [string]$script:PhysicalFile::GetIdentity($stream.SafeFileHandle) -cne $pendingIdentity -or
            [long]$script:PhysicalFile::GetLinkCount($stream.SafeFileHandle) -ne 1 -or
            $stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $hash) {
            throw "$Context pending publication bytes or physical identity drifted."
        }
        Assert-M0NoAlternateDataStreams $pending "$Context pending publication"
        Assert-M0DirectoryAnchorUnchanged $parentAnchor "$Context before commit"
        if ($null -ne $BeforeCommit) { $null = & $BeforeCommit $pending $full }
        if ($null -ne $ValidateCommitEnvelope) { $null = & $ValidateCommitEnvelope }
        Assert-M0DirectoryAnchorUnchanged $parentAnchor "$Context commit hook"
        $pendingFinal = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                    $script:PhysicalFile::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($pendingFinal, $pending, [StringComparison]::OrdinalIgnoreCase) -or
            [string]$script:PhysicalFile::GetIdentity($stream.SafeFileHandle) -cne $pendingIdentity -or
            [long]$script:PhysicalFile::GetLinkCount($stream.SafeFileHandle) -ne 1 -or
            $stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $hash) {
            throw "$Context pending publication changed at the commit seam."
        }
        Assert-M0NoAlternateDataStreams $pending "$Context pending commit leaf"

        $script:PhysicalFile::RenameNoReplaceRelative(
            $stream.SafeFileHandle, $parentAnchor.handle, [IO.Path]::GetFileName($full))
        $publishedFinal = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath (
                    $script:PhysicalFile::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($publishedFinal, $full, [StringComparison]::OrdinalIgnoreCase) -or
            [string]$script:PhysicalFile::GetIdentity($stream.SafeFileHandle) -cne $pendingIdentity -or
            [long]$script:PhysicalFile::GetLinkCount($stream.SafeFileHandle) -ne 1 -or
            $stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $hash) {
            throw "$Context changed across atomic publication."
        }
        Assert-M0DirectoryAnchorUnchanged $parentAnchor "$Context after commit"
        Assert-M0NoAlternateDataStreams $full "$Context published leaf"
        $published = $true
        $stream.Dispose()
        $stream = $null
        $lease = Open-M0PhysicalFile `
            $full $BoundaryRoot $Context $Bytes.Length $hash -RequireSingleLink
        if ($lease.identity -cne $pendingIdentity) {
            $lease.stream.Dispose()
            throw "$Context identity drifted while acquiring its published read lease."
        }
        if ($null -ne $FileLedger) {
            Add-M0OwnedIdentity $FileLedger $full $lease.identity $Context
        }
        return [pscustomobject][ordered]@{
            relative_path = [IO.Path]::GetRelativePath($BoundaryRoot, $full).Replace('\', '/')
            size_bytes = [long]$Bytes.Length
            sha256 = $hash
            lease = $lease
        }
    }
    catch {
        $failure = $_
        if ($null -ne $stream) {
            try {
                if ([string]$script:PhysicalFile::GetIdentity($stream.SafeFileHandle) -cne $pendingIdentity) {
                    throw "$Context refused cleanup of a changed pending identity."
                }
                $script:PhysicalFile::MarkDeleteOnClose($stream.SafeFileHandle)
            }
            catch {
                $failure = [Management.Automation.ErrorRecord]::new(
                    [AggregateException]::new(
                        "$Context failed and its authenticated pending cleanup also failed.",
                        [Exception[]]@($failure.Exception, $_.Exception)),
                    'M0CompositePendingCleanupFailure',
                    [Management.Automation.ErrorCategory]::InvalidData,
                    $full)
            }
            finally { $stream.Dispose() }
        }
        throw $failure
    }
    finally {
        if ($null -ne $parentAnchor) { $parentAnchor.handle.Dispose() }
        if ($null -ne $createdParentAnchor) { $createdParentAnchor.handle.Dispose() }
    }
}

function Assert-M0RequiredArtifacts {
    param([Parameter(Mandatory)] [object[]] $Plan, [Parameter(Mandatory)] $Contract)
    $byPath = [Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($row in $Plan) { $byPath.Add([string]$row.relative_path, $row) }
    foreach ($artifact in @($Contract.required_artifacts)) {
        if (-not $byPath.ContainsKey([string]$artifact.relative_path)) {
            throw "Composite required artifact is missing from ownership: $($artifact.relative_path)"
        }
        $row = $byPath[[string]$artifact.relative_path]
        if ($row.owner_id -cne $artifact.owner_id -or $row.size_bytes -cne $artifact.size_bytes -or
            $row.sha256 -cne $artifact.sha256) {
            throw "Composite required artifact owner, size, or SHA-256 drifted: $($artifact.relative_path)"
        }
    }
}

function Assert-M0RecipeFiles {
    param(
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Leases
    )
    $paths = [ordered]@{}
    foreach ($file in @($Contract.recipe.files)) {
        $path = Resolve-M0ContainedPath $RepositoryRoot $file.relative_path "Recipe '$($file.id)'"
        $lease = Open-M0PhysicalFile $path $RepositoryRoot "Recipe '$($file.id)'" `
            ([long]$file.size_bytes) ([string]$file.sha256) -RequireSingleLink
        $Leases.Add($lease)
        $paths[$file.id] = $lease.path
    }
    return $paths
}

function Assert-M0HistoricalV1 {
    param(
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Leases
    )
    $v1 = $Contract.provenance.qt_shell_v1_contract
    $path = Resolve-M0ContainedPath $RepositoryRoot $v1.relative_path 'Historical Qt shell v1 contract'
    $lease = Open-M0PhysicalFile $path $RepositoryRoot 'Historical Qt shell v1 contract' `
        ([long]$v1.size_bytes) ([string]$v1.sha256) -RequireSingleLink
    $Leases.Add($lease)
    $document = Read-M0ClosedJson $lease.path 'Historical Qt shell v1 contract'
    Assert-M0ExactProperties $document @('files') 'Historical Qt shell v1 contract'
    if ($document.files -isnot [array] -or @($document.files).Count -ne [long]$v1.file_count) {
        throw 'Historical Qt shell v1 contract member count drifted.'
    }
    foreach ($member in @($document.files)) {
        Assert-M0ExactProperties $member @('path', 'sha256', 'size_bytes') 'Historical v1 recipe member'
        Assert-M0SafeRelativePath $member.path 'Historical v1 recipe path'
        Assert-M0Digest $member.sha256 'Historical v1 recipe SHA-256'
        $memberPath = Resolve-M0ContainedPath $RepositoryRoot $member.path 'Historical v1 recipe member'
        $memberLease = Open-M0PhysicalFile $memberPath $RepositoryRoot 'Historical v1 recipe member' `
            ([long]$member.size_bytes) ([string]$member.sha256) -RequireSingleLink
        $Leases.Add($memberLease)
    }
}

function Assert-M0QtBaseParent {
    param(
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $Leases
    )
    Assert-M0Digest $ExpectedQtBaseReceiptSha256 'Expected parent Qtbase receipt SHA-256'
    $parent = $Contract.provenance.qtbase_refresh
    if ($ExpectedQtBaseReceiptSha256 -cne $parent.receipt_sha256) {
        throw 'External Qtbase receipt SHA-256 anchor differs from the composite contract.'
    }
    $contractPath = Resolve-M0ContainedPath $RepositoryRoot $parent.contract_relative_path 'Qtbase parent contract'
    $modulePath = Resolve-M0ContainedPath $RepositoryRoot $parent.module_relative_path 'Qtbase parent verifier module'
    $receiptPath = Resolve-M0ContainedPath $RepositoryRoot $parent.receipt_relative_path 'Qtbase parent receipt'
    $manifestPath = Resolve-M0ContainedPath $RepositoryRoot $parent.manifest_relative_path 'Qtbase parent manifest'
    $inventoryPath = Resolve-M0ContainedPath $RepositoryRoot $parent.install_inventory_relative_path `
        'Qtbase parent install inventory'
    foreach ($binding in @(
            @($contractPath, $parent.contract_size_bytes, $parent.contract_sha256, 'Qtbase parent contract'),
            @($modulePath, $parent.module_size_bytes, $parent.module_sha256, 'Qtbase parent verifier module'),
            @($receiptPath, $parent.receipt_size_bytes, $parent.receipt_sha256, 'Qtbase parent receipt'),
            @($manifestPath, $parent.manifest_size_bytes, $parent.manifest_sha256, 'Qtbase parent manifest'),
            @($inventoryPath, $parent.install_inventory_size_bytes, $parent.install_inventory_sha256, 'Qtbase parent install inventory'))) {
        $lease = Open-M0PhysicalFile ([string]$binding[0]) $RepositoryRoot ([string]$binding[3]) `
            ([long]$binding[1]) ([string]$binding[2]) -RequireSingleLink
        $Leases.Add($lease)
    }
    if (Get-Module -Name 'M0.QtBaseRefresh' -ErrorAction SilentlyContinue) {
        throw 'Qtbase parent verifier module was preloaded before authentication.'
    }
    $module = Import-Module -Name $modulePath -Force -PassThru
    try {
        $exports = @($module.ExportedFunctions.Keys | Sort-Object)
        Assert-M0Sequence $exports @(
            'Assert-M0QtBaseRefreshContract', 'Assert-M0QtBaseRefreshReceipt', 'Invoke-M0QtBaseRefresh'
        ) 'Qtbase parent verifier export surface'
        $null = & $module.ExportedFunctions['Assert-M0QtBaseRefreshReceipt'] `
            -LiteralPath $receiptPath `
            -ExpectedReceiptSha256 $ExpectedQtBaseReceiptSha256 `
            -ContractPath $contractPath `
            -ExpectedContractSha256 ([string]$parent.contract_sha256) `
            -RepositoryRoot $RepositoryRoot
    }
    finally { Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue }
    return [pscustomobject][ordered]@{
        receipt_path = $receiptPath
        install_root = Resolve-M0ContainedPath $RepositoryRoot $parent.install_root_relative_path `
            'Qtbase parent install root'
        inventory_path = $inventoryPath
    }
}

function Invoke-M0QtShellComposite {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $RunRoot
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new(
            'Qt shell composite SDK production requires Windows physical-file semantics.')
    }
    $contract = Assert-M0QtShellCompositeContract $ContractPath $ExpectedContractSha256
    $repository = ConvertTo-M0NormalizedDirectoryPath $RepositoryRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'Composite RepositoryRoot does not exist.' }
    Assert-M0NoReparseChain $repository $repository 'Composite RepositoryRoot'
    $contractFull = [IO.Path]::GetFullPath($ContractPath)
    if (-not (Test-M0PathWithin $contractFull $repository)) {
        throw 'Composite contract must be inside RepositoryRoot.'
    }

    $runParent = Resolve-M0ContainedPath $repository $contract.isolation.run_parent_relative_path `
        'Composite RunRoot parent'
    if (-not [IO.Directory]::Exists($runParent)) { throw 'Composite RunRoot parent is missing.' }
    Assert-M0NoReparseChain $runParent $repository 'Composite RunRoot parent'
    $runRootFull = ConvertTo-M0NormalizedDirectoryPath $RunRoot
    if ([string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($runRootFull)) -or
        -not [string]::Equals([IO.Path]::GetDirectoryName($runRootFull), $runParent,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Composite RunRoot must be a new direct child of the pinned run parent.'
    }
    if ([IO.Directory]::Exists($runRootFull) -or [IO.File]::Exists($runRootFull)) {
        throw 'Composite RunRoot must be new and must not already exist.'
    }
    $runId = [IO.Path]::GetFileName($runRootFull)
    if ($runId.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
        $runId.EndsWith(' ', [StringComparison]::Ordinal) -or
        $runId.EndsWith('.', [StringComparison]::Ordinal)) {
        throw 'Composite RunRoot leaf is unsafe.'
    }
    $archiveScratch = Join-Path $runParent ('.' + $runId + '.composite-archives')
    $nativeScratch = Join-Path $runParent ('.' + $runId + '.composite-native')
    foreach ($scratch in @($archiveScratch, $nativeScratch)) {
        if ([IO.Directory]::Exists($scratch) -or [IO.File]::Exists($scratch)) {
            throw 'Composite private scratch path already exists.'
        }
    }

    $leases = [Collections.Generic.List[object]]::new()
    $sourceLeases = [Collections.Generic.List[object]]::new()
    $publicationLeases = [Collections.Generic.List[object]]::new()
    $runnerModule = $null
    $ownsArchiveScratch = $false
    $archiveScratchState = $null
    $runRootState = $null
    $publicationCommitted = $false
    $failure = $null
    try {
        $contractLease = Open-M0PhysicalFile $contractFull $repository 'Composite contract' `
            (Get-Item -LiteralPath $contractFull).Length $ExpectedContractSha256 -RequireSingleLink
        $leases.Add($contractLease)
        $recipePaths = Assert-M0RecipeFiles $contract $repository $leases
        if (-not [string]::Equals($recipePaths.orchestrator, $script:LoadedModulePath,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Loaded composite orchestrator is not its authenticated recipe leaf.'
        }
        if (-not [string]::Equals([IO.Path]::GetFullPath($MyInvocation.PSCommandPath),
                [IO.Path]::GetFullPath($recipePaths.entrypoint), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Composite production invocation did not originate from the authenticated entrypoint.'
        }
        Assert-M0HistoricalV1 $contract $repository $leases
        $parent = Assert-M0QtBaseParent $contract $ExpectedQtBaseReceiptSha256 $repository $leases

        $extractorSpec = $contract.inputs.extractor
        $extractorPath = Resolve-M0ContainedPath $repository $extractorSpec.relative_path `
            'Composite authenticated extractor'
        $extractorLease = Open-M0PhysicalFile $extractorPath $repository 'Composite authenticated extractor' `
            ([long]$extractorSpec.size_bytes) ([string]$extractorSpec.sha256) -RequireSingleLink
        $leases.Add($extractorLease)
        $archivePaths = [ordered]@{}
        foreach ($archive in @($contract.inputs.archives)) {
            $archivePath = Resolve-M0ContainedPath $repository $archive.relative_path `
                "Composite archive '$($archive.owner_id)'"
            $archiveLease = Open-M0PhysicalFile $archivePath $repository `
                "Composite archive '$($archive.owner_id)'" `
                ([long]$archive.size_bytes) ([string]$archive.sha256) -RequireSingleLink
            $leases.Add($archiveLease)
            $archivePaths[$archive.owner_id] = $archiveLease.path
        }

        $archiveScratchState = New-M0AtomicOwnedTreeState `
            $archiveScratch $runParent 'Composite archive scratch root'
        $ownsArchiveScratch = $true
        $sourceRoots = [ordered]@{ 'qtbase-refresh-v2' = $parent.install_root }
        $ownerEntries = [ordered]@{}
        $parentInventory = ConvertFrom-M0CanonicalInventory `
            -LiteralPath $parent.inventory_path `
            -ColumnCount 3 `
            -ExpectedOwnerId 'qtbase-refresh-v2' `
            -Context 'Qtbase parent install inventory'
        if (@($parentInventory.rows).Count -ne [long]$contract.provenance.qtbase_refresh.install_file_count -or
            [long](($parentInventory.rows | Measure-Object -Property size_bytes -Sum).Sum) -ne
                [long]$contract.provenance.qtbase_refresh.install_total_size_bytes) {
            throw 'Qtbase parent install inventory count or total size drifted.'
        }
        $ownerEntries['qtbase-refresh-v2'] = @($parentInventory.rows | ForEach-Object {
                [pscustomobject][ordered]@{
                    source_relative_path = $_.source_relative_path; relative_path = $_.relative_path
                    size_bytes = [long]$_.size_bytes; sha256 = $_.sha256
                }
            })

        foreach ($archive in @($contract.inputs.archives)) {
            $ownerId = [string]$archive.owner_id
            $extractRoot = Join-Path $archiveScratch $ownerId
            $extractAnchor = New-M0AtomicDirectoryAnchor $extractRoot $archiveScratch `
                "Archive '$ownerId' extraction root"
            $sourceLeases.Add($extractAnchor)
            Add-M0OwnedIdentity $archiveScratchState.directory_ledger $extractRoot `
                $extractAnchor.identity `
                "Archive '$ownerId' extraction root"
            $listing = Invoke-M0BoundedProcess `
                -FilePath $extractorPath `
                -ArgumentList @('l', '-slt', [string]$archivePaths[$ownerId]) `
                -WorkingDirectory $archiveScratch
            if ($listing.exit_code -ne 0 -or -not [string]::IsNullOrEmpty($listing.stderr)) {
                throw "Authenticated archive '$ownerId' listing failed closed."
            }
            Assert-M0LeasesUnchanged $leases "After archive '$ownerId' listing"
            $members = @(ConvertFrom-M0SevenZipListing $listing.stdout)
            $extraction = Invoke-M0BoundedProcess `
                -FilePath $extractorPath `
                -ArgumentList @('x', '-y', '-bb0', ("-o$extractRoot"), [string]$archivePaths[$ownerId]) `
                -WorkingDirectory $archiveScratch
            if ($extraction.exit_code -ne 0 -or -not [string]::IsNullOrEmpty($extraction.stderr)) {
                throw "Authenticated archive '$ownerId' extraction failed closed."
            }
            Assert-M0LeasesUnchanged $leases "After archive '$ownerId' extraction"
            $snapshot = Assert-M0ExtractedArchive $extractRoot $members $archive $sourceLeases `
                $archiveScratchState.directory_ledger $archiveScratchState.file_ledger
            $sourceRoots[$ownerId] = $snapshot.root
            $ownerEntries[$ownerId] = @($snapshot.entries)
        }

        $plan = @(New-M0QtShellCompositeOwnershipPlan $ownerEntries)
        Assert-M0RequiredArtifacts $plan $contract
        $runnerModule = Import-Module -Name $recipePaths.runner -Force -PassThru
        $runnerExports = @($runnerModule.ExportedFunctions.Keys)
        if ($runnerExports.Count -ne 1 -or $runnerExports[0] -cne $contract.recipe.runner_export -or
            @($runnerModule.ExportedAliases.Keys).Count -ne 0 -or
            @($runnerModule.ExportedCmdlets.Keys).Count -ne 0 -or
            @($runnerModule.ExportedVariables.Keys).Count -ne 0) {
            throw 'Authenticated composite native runner export surface drifted.'
        }
        $nativeResult = @(& $runnerModule.ExportedFunctions[$contract.recipe.runner_export] `
            -SourceRoots $sourceRoots `
            -OutputRoot $runRootFull `
            -ScratchRoot $nativeScratch `
            -Ownership $plan)
        if ($nativeResult.Count -ne 1 -or $null -eq $nativeResult[0]) {
            throw 'Authenticated composite native runner returned an invalid result cardinality.'
        }
        $nativeResult = $nativeResult[0]
        Assert-M0ExactProperties $nativeResult @(
            'exit_code', 'sdk_root', 'inventory_relative_path', 'entries', 'published_root_identity'
        ) 'native composite result'
        if ($nativeResult.exit_code -isnot [long] -or $nativeResult.exit_code -ne 0 -or
            $nativeResult.sdk_root -cne $contract.isolation.sdk_relative_path -or
            $nativeResult.inventory_relative_path -cne $contract.isolation.global_inventory_relative_path -or
            @($nativeResult.entries).Count -ne $plan.Count -or
            [string]::IsNullOrWhiteSpace([string]$nativeResult.published_root_identity)) {
            throw 'Authenticated composite native runner result drifted.'
        }
        foreach ($entry in @($nativeResult.entries)) {
            Assert-M0ExactProperties $entry @(
                'owner_id', 'source_relative_path', 'relative_path', 'size_bytes', 'sha256'
            ) 'native composite ownership row'
        }
        if ((ConvertTo-M0InventoryText @($nativeResult.entries)) -cne
            (ConvertTo-M0InventoryText $plan)) {
            throw 'Authenticated composite native runner entries differ from the exact ownership plan.'
        }
        if (-not [IO.Directory]::Exists($runRootFull)) {
            throw 'Authenticated composite native runner did not atomically publish RunRoot.'
        }
        $runRootState = New-M0OwnedTreeState `
            $runRootFull $runParent 'Composite published RunRoot' `
            -ExpectedIdentity ([string]$nativeResult.published_root_identity)
        if ([IO.Directory]::Exists($nativeScratch) -or [IO.File]::Exists($nativeScratch)) {
            throw 'Authenticated composite native runner left a private scratch path after publication.'
        }
        $sdkRoot = Resolve-M0ContainedPath $runRootFull `
            $contract.isolation.sdk_relative_path 'Composite physical SDK'
        $null = Assert-M0ExactSdkTree -SdkRoot $sdkRoot -Plan $plan `
            -LeaseSink $publicationLeases `
            -DirectoryLedger $runRootState.directory_ledger -FileLedger $runRootState.file_ledger

        Close-M0Leases $sourceLeases
        $sourceLeases.Clear()
        Remove-M0OwnedTree -LiteralPath $archiveScratch -ExpectedExactPath $archiveScratch `
            -BoundaryRoot $runParent -RootAnchor $archiveScratchState.root_anchor `
            -DirectoryLedger $archiveScratchState.directory_ledger `
            -FileLedger $archiveScratchState.file_ledger
        $ownsArchiveScratch = $false
        $archiveScratchState = $null
        Assert-M0LeasesUnchanged $leases 'After composite native publication'
        $contract = Assert-M0QtShellCompositeContract $contractFull $ExpectedContractSha256

        $globalInventoryPath = Resolve-M0ContainedPath $runRootFull `
            $contract.isolation.global_inventory_relative_path 'Composite global inventory'
        $expectedGlobalText = ConvertTo-M0InventoryText $plan
        $observedGlobalText = [IO.File]::ReadAllText($globalInventoryPath, [Text.UTF8Encoding]::new($false, $true))
        if ($observedGlobalText -cne $expectedGlobalText) {
            throw 'Native composite global inventory is not the canonical ownership plan.'
        }
        $globalBytes = $script:Utf8NoBom.GetBytes($expectedGlobalText)
        $globalHash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($globalBytes)).ToLowerInvariant()
        $globalLease = Open-M0PhysicalFile $globalInventoryPath $runRootFull 'Composite global inventory' `
            $globalBytes.Length $globalHash -RequireSingleLink
        $publicationLeases.Add($globalLease)
        Add-M0OwnedIdentity $runRootState.file_ledger $globalInventoryPath $globalLease.identity `
            'Composite global inventory'
        $evidenceDirectory = [IO.Path]::GetDirectoryName($globalInventoryPath)
        Add-M0OwnedIdentity $runRootState.directory_ledger $evidenceDirectory `
            (Get-M0DirectoryIdentity $evidenceDirectory $runRootFull 'Composite evidence directory') `
            'Composite evidence directory'

        $ownerManifestRows = [Collections.Generic.List[object]]::new()
        foreach ($ownerId in $script:OwnerIds) {
            $rows = @($plan | Where-Object owner_id -CEQ $ownerId)
            $text = ConvertTo-M0InventoryText $rows
            $bytes = $script:Utf8NoBom.GetBytes($text)
            $relativePath = "$($contract.isolation.owner_inventory_directory_relative_path)/$ownerId.tsv"
            $path = Resolve-M0ContainedPath $runRootFull $relativePath "Owner '$ownerId' inventory"
            $published = Write-M0CreateNewFile -LiteralPath $path -Bytes $bytes `
                -BoundaryRoot $runRootFull -Context "Owner '$ownerId' inventory" `
                -DirectoryLedger $runRootState.directory_ledger -FileLedger $runRootState.file_ledger
            $publicationLeases.Add($published.lease)
            $ownerManifestRows.Add([pscustomobject][ordered]@{
                    owner_id = $ownerId
                    file_count = [long]$rows.Count
                    total_size_bytes = [long](($rows | Measure-Object -Property size_bytes -Sum).Sum)
                    inventory_relative_path = $relativePath
                    inventory_size_bytes = [long]$published.size_bytes
                    inventory_sha256 = $published.sha256
                })
        }
        $sdkReference = [pscustomobject][ordered]@{
            root_relative_path = $contract.isolation.sdk_relative_path
            file_count = [long]$plan.Count
            total_size_bytes = [long](($plan | Measure-Object -Property size_bytes -Sum).Sum)
            inventory_relative_path = $contract.isolation.global_inventory_relative_path
            inventory_size_bytes = [long]$globalBytes.Length
            inventory_sha256 = $globalHash
        }
        $capturedUtc = [DateTimeOffset]::UtcNow.ToString('O')
        $manifest = [pscustomobject][ordered]@{
            schema_version = [long]2
            manifest_id = "$($contract.composite_id)-manifest"
            composite_id = $contract.composite_id
            run_id = $runId
            captured_utc = $capturedUtc
            contract_sha256 = $ExpectedContractSha256
            recipe = $contract.recipe
            provenance = $contract.provenance
            inputs = $contract.inputs
            ownership_policy = $contract.ownership
            owner_inventories = $ownerManifestRows.ToArray()
            sdk = $sdkReference
            required_artifacts = $contract.required_artifacts
            limits = $script:Limits
        }
        $manifestBytes = $script:Utf8NoBom.GetBytes(($manifest | ConvertTo-Json -Depth 100) + "`n")
        $manifestPath = Resolve-M0ContainedPath $runRootFull $contract.isolation.manifest_relative_path `
            'Composite evidence manifest'
        $validateSdkCommitEnvelope = {
            $null = Assert-M0ExactSdkTree $sdkRoot $plan
        }.GetNewClosure()
        $null = Assert-M0ExactSdkTree $sdkRoot $plan
        Assert-M0LeasesUnchanged (@($leases) + @($publicationLeases)) `
            'Before composite manifest publication'
        $manifestPublication = Write-M0CreateNewFile -LiteralPath $manifestPath `
            -Bytes $manifestBytes -BoundaryRoot $runRootFull -Context 'Composite evidence manifest' `
            -ValidateCommitEnvelope $validateSdkCommitEnvelope `
            -DirectoryLedger $runRootState.directory_ledger -FileLedger $runRootState.file_ledger
        $publicationLeases.Add($manifestPublication.lease)
        Assert-M0LeasesUnchanged (@($leases) + @($publicationLeases)) `
            'After composite manifest publication'

        $receipt = [pscustomobject][ordered]@{
            schema_version = [long]2
            receipt_id = "$($contract.composite_id)-receipt"
            composite_id = $contract.composite_id
            captured_utc = $capturedUtc
            contract_sha256 = $ExpectedContractSha256
            execution = [pscustomobject][ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
            qtbase_receipt = [pscustomobject][ordered]@{
                relative_path = $contract.provenance.qtbase_refresh.receipt_relative_path
                size_bytes = [long]$contract.provenance.qtbase_refresh.receipt_size_bytes
                sha256 = $contract.provenance.qtbase_refresh.receipt_sha256
            }
            sdk = $sdkReference
            evidence = [pscustomobject][ordered]@{
                relative_path = $contract.isolation.manifest_relative_path
                size_bytes = [long]$manifestPublication.size_bytes
                sha256 = $manifestPublication.sha256
            }
            limits = $script:Limits
        }
        $null = Assert-M0QtShellCompositeReceiptDocument $receipt $contract $ExpectedContractSha256
        Assert-M0LeasesUnchanged (@($leases) + @($publicationLeases)) 'Before final composite receipt publication'
        $receiptBytes = $script:Utf8NoBom.GetBytes(($receipt | ConvertTo-Json -Depth 100) + "`n")
        $receiptPath = Resolve-M0ContainedPath $runRootFull $contract.isolation.receipt_relative_path `
            'Composite final receipt'
        $null = Assert-M0ExactSdkTree $sdkRoot $plan
        Assert-M0LeasesUnchanged (@($leases) + @($publicationLeases)) `
            'At final composite receipt commit seam'
        $receiptPublication = Write-M0CreateNewFile -LiteralPath $receiptPath `
            -Bytes $receiptBytes -BoundaryRoot $runRootFull -Context 'Composite final receipt' `
            -ValidateCommitEnvelope $validateSdkCommitEnvelope `
            -DirectoryLedger $runRootState.directory_ledger -FileLedger $runRootState.file_ledger
        $publicationLeases.Add($receiptPublication.lease)
        Assert-M0LeasesUnchanged (@($leases) + @($publicationLeases)) 'After final composite receipt publication'
        $publicationCommitted = $true
        return [pscustomobject][ordered]@{
            composite_id = $contract.composite_id
            run_id = $runId
            contract_sha256 = $ExpectedContractSha256
            manifest_path = $manifestPath
            manifest_sha256 = $manifestPublication.sha256
            receipt_path = $receiptPath
            receipt_sha256 = $receiptPublication.sha256
        }
    }
    catch { $failure = $_ }
    finally {
        if ($null -ne $runnerModule) {
            Remove-Module -ModuleInfo $runnerModule -Force -ErrorAction SilentlyContinue
        }
        Close-M0Leases $sourceLeases
        Close-M0Leases $publicationLeases
        Close-M0Leases $leases
        if (-not $publicationCommitted) {
            try {
                if ($ownsArchiveScratch -and $null -ne $archiveScratchState -and
                    [IO.Directory]::Exists($archiveScratch)) {
                    Remove-M0OwnedTree `
                        -LiteralPath $archiveScratch -ExpectedExactPath $archiveScratch `
                        -BoundaryRoot $runParent -RootAnchor $archiveScratchState.root_anchor `
                        -DirectoryLedger $archiveScratchState.directory_ledger `
                        -FileLedger $archiveScratchState.file_ledger
                }
                if ($null -ne $runRootState -and [IO.Directory]::Exists($runRootFull)) {
                    Remove-M0OwnedTree -LiteralPath $runRootFull -ExpectedExactPath $runRootFull `
                        -BoundaryRoot $runParent -RootAnchor $runRootState.root_anchor `
                        -DirectoryLedger $runRootState.directory_ledger `
                        -FileLedger $runRootState.file_ledger
                }
            }
            catch {
                if ($null -ne $failure) {
                    $failure = [Management.Automation.ErrorRecord]::new(
                        [AggregateException]::new('Composite failed and owned-root cleanup also failed.',
                            [Exception[]]@($failure.Exception, $_.Exception)),
                        'M0CompositeCleanupFailure', [Management.Automation.ErrorCategory]::InvalidData, $runRootFull)
                }
                else { $failure = $_ }
            }
        }
        foreach ($state in @($archiveScratchState, $runRootState)) {
            if ($null -ne $state -and $null -ne $state.root_anchor -and
                -not $state.root_anchor.handle.IsClosed) {
                try { $state.root_anchor.handle.Dispose() } catch { }
            }
        }
    }
    if ($null -ne $failure) { throw $failure }
}

function Test-M0JsonEquivalent {
    param($First, $Second)
    return (($First | ConvertTo-Json -Depth 100 -Compress) -ceq
        ($Second | ConvertTo-Json -Depth 100 -Compress))
}

function Assert-M0ManifestDocument {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedRunId
    )
    Assert-M0ExactProperties $Manifest @(
        'schema_version', 'manifest_id', 'composite_id', 'run_id', 'captured_utc',
        'contract_sha256', 'recipe', 'provenance', 'inputs', 'ownership_policy',
        'owner_inventories', 'sdk', 'required_artifacts', 'limits'
    ) 'composite evidence manifest'
    if ($Manifest.schema_version -isnot [long] -or $Manifest.schema_version -ne 2 -or
        $Manifest.manifest_id -cne "$($Contract.composite_id)-manifest" -or
        $Manifest.composite_id -cne $Contract.composite_id -or
        $Manifest.run_id -cne $ExpectedRunId -or
        $Manifest.contract_sha256 -cne $ExpectedContractSha256) {
        throw 'Composite evidence manifest identity, RunRoot, or contract binding drifted.'
    }
    Assert-M0CanonicalUtcTimestamp $Manifest.captured_utc 'Manifest captured_utc'
    foreach ($binding in @(
            @($Manifest.recipe, $Contract.recipe, 'recipe'),
            @($Manifest.provenance, $Contract.provenance, 'provenance'),
            @($Manifest.inputs, $Contract.inputs, 'inputs'),
            @($Manifest.ownership_policy, $Contract.ownership, 'ownership policy'),
            @($Manifest.required_artifacts, $Contract.required_artifacts, 'required artifacts'))) {
        if (-not (Test-M0JsonEquivalent $binding[0] $binding[1])) {
            throw "Composite evidence manifest $($binding[2]) drifted from the authenticated contract."
        }
    }
    if ($Manifest.owner_inventories -isnot [array] -or @($Manifest.owner_inventories).Count -ne 4) {
        throw 'Composite evidence manifest owner inventory sequence drifted.'
    }
    for ($index = 0; $index -lt 4; $index++) {
        $owner = $Manifest.owner_inventories[$index]
        Assert-M0ExactProperties $owner @(
            'owner_id', 'file_count', 'total_size_bytes', 'inventory_relative_path',
            'inventory_size_bytes', 'inventory_sha256'
        ) "manifest.owner_inventories[$index]"
        if ($owner.owner_id -cne $script:OwnerIds[$index] -or
            $owner.inventory_relative_path -cne
                "$($Contract.isolation.owner_inventory_directory_relative_path)/$($script:OwnerIds[$index]).tsv") {
            throw 'Composite evidence manifest owner identity or inventory path drifted.'
        }
        $null = ConvertTo-M0Int64 $owner.file_count 'Manifest owner file_count'
        $null = ConvertTo-M0Int64 $owner.total_size_bytes 'Manifest owner total_size_bytes'
        $null = ConvertTo-M0Int64 $owner.inventory_size_bytes 'Manifest owner inventory_size_bytes'
        Assert-M0Digest $owner.inventory_sha256 'Manifest owner inventory_sha256'
    }
    Assert-M0ExactProperties $Manifest.sdk @(
        'root_relative_path', 'file_count', 'total_size_bytes', 'inventory_relative_path',
        'inventory_size_bytes', 'inventory_sha256'
    ) 'manifest.sdk'
    if ($Manifest.sdk.root_relative_path -cne $Contract.isolation.sdk_relative_path -or
        $Manifest.sdk.inventory_relative_path -cne $Contract.isolation.global_inventory_relative_path) {
        throw 'Composite evidence manifest SDK paths drifted.'
    }
    $null = ConvertTo-M0Int64 $Manifest.sdk.file_count 'Manifest SDK file_count'
    $null = ConvertTo-M0Int64 $Manifest.sdk.total_size_bytes 'Manifest SDK total_size_bytes'
    $null = ConvertTo-M0Int64 $Manifest.sdk.inventory_size_bytes 'Manifest SDK inventory_size_bytes'
    Assert-M0Digest $Manifest.sdk.inventory_sha256 'Manifest SDK inventory_sha256'
    Assert-M0Sequence $Manifest.limits $script:Limits 'manifest.limits'
}

function Get-M0RootForRelativeFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $RelativePath,
        [Parameter(Mandatory)] [string] $Context
    )
    Assert-M0SafeRelativePath $RelativePath "$Context relative path"
    $current = [IO.Path]::GetFullPath($LiteralPath)
    foreach ($null in @($RelativePath -split '/')) { $current = [IO.Path]::GetDirectoryName($current) }
    if ([string]::IsNullOrWhiteSpace($current)) { throw "$Context cannot derive a canonical root." }
    $root = ConvertTo-M0NormalizedDirectoryPath $current
    $expected = Resolve-M0ContainedPath $root $RelativePath "$Context canonical path"
    if (-not [string]::Equals($expected, [IO.Path]::GetFullPath($LiteralPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context is not at its canonical relative path."
    }
    return $root
}

function Open-M0Reference {
    param(
        [Parameter(Mandatory)] $Reference,
        [Parameter(Mandatory)] [string] $Root,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $LeaseSink
    )
    Assert-M0FileReference $Reference $Context
    $path = Resolve-M0ContainedPath $Root $Reference.relative_path $Context
    $lease = Open-M0PhysicalFile $path $Root $Context `
        ([long]$Reference.size_bytes) ([string]$Reference.sha256) -RequireSingleLink
    $LeaseSink.Add($lease)
    return $lease.path
}

function Assert-M0EvidenceTree {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)] [object[]] $OwnerManifestRows,
        [Parameter(Mandatory)] [Collections.Generic.List[object]] $LeaseSink
    )
    $evidenceRoot = Resolve-M0ContainedPath $RunRoot 'evidence' 'Composite evidence root'
    $snapshot = Get-M0TreeSnapshot $evidenceRoot 'Composite evidence tree' $LeaseSink
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$expected.Add('composite-inventory.tsv')
    [void]$expected.Add([IO.Path]::GetRelativePath('evidence', $Contract.isolation.manifest_relative_path).Replace('\', '/'))
    [void]$expected.Add([IO.Path]::GetRelativePath('evidence', $Contract.isolation.receipt_relative_path).Replace('\', '/'))
    foreach ($owner in $OwnerManifestRows) {
        [void]$expected.Add([IO.Path]::GetRelativePath('evidence', $owner.inventory_relative_path).Replace('\', '/'))
    }
    if ($snapshot.file_count -ne $expected.Count) {
        throw 'Composite evidence tree contains an extra or missing file.'
    }
    foreach ($entry in $snapshot.entries) {
        if (-not $expected.Contains($entry.relative_path)) {
            throw "Composite evidence tree contains an unexpected file: $($entry.relative_path)"
        }
    }
    $expectedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$expectedDirectories.Add('inventories')
    if ($snapshot.directories.Count -ne $expectedDirectories.Count) {
        throw 'Composite evidence tree contains an extra or missing directory.'
    }
    foreach ($directory in $snapshot.directories) {
        if (-not $expectedDirectories.Contains($directory)) {
            throw "Composite evidence tree contains an unexpected directory: $directory"
        }
    }
}

function Assert-M0RunRootEnvelope {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Collections.Generic.List[object]] $LeaseSink,
        [scriptblock] $AfterSweep
    )
    $root = ConvertTo-M0NormalizedDirectoryPath $RunRoot
    $rootAnchor = Open-M0DirectoryAnchor $root ([IO.Path]::GetDirectoryName($root)) `
        'Composite verification RunRoot anchor' -Immutable
    $children = [Collections.Generic.Dictionary[string,object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $transferred = $false
    try {
        foreach ($entry in [IO.DirectoryInfo]::new($root).EnumerateFileSystemInfos()) {
            if ($entry -isnot [IO.DirectoryInfo] -or
                ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                ($entry.Name -cne 'sdk' -and $entry.Name -cne 'evidence') -or
                $children.ContainsKey($entry.Name)) {
                throw 'Composite verification RunRoot contains an extra child.'
            }
            $anchor = Open-M0DirectoryAnchor $entry.FullName $root `
                "Composite verification RunRoot child '$($entry.Name)'" -Immutable
            $children.Add($entry.Name, $anchor)
        }
        if ($children.Count -ne 2 -or -not $children.ContainsKey('sdk') -or
            -not $children.ContainsKey('evidence')) {
            throw 'Composite verification RunRoot contains an extra or missing child.'
        }
        if ($null -ne $AfterSweep) { $null = & $AfterSweep $children }
        Assert-M0DirectoryAnchorUnchanged $rootAnchor 'Composite verification RunRoot final resweep'
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in [IO.DirectoryInfo]::new($root).EnumerateFileSystemInfos()) {
            if ($entry -isnot [IO.DirectoryInfo] -or
                ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                -not $children.ContainsKey($entry.Name) -or -not $seen.Add($entry.Name)) {
                throw 'Composite verification RunRoot final resweep detected an extra child.'
            }
            Assert-M0DirectoryAnchorUnchanged $children[$entry.Name] `
                'Composite verification RunRoot final resweep'
        }
        if ($seen.Count -ne 2) {
            throw 'Composite verification RunRoot final resweep detected an extra or missing child.'
        }
        if ($null -ne $LeaseSink) {
            $LeaseSink.Add([pscustomobject][ordered]@{
                    lease_kind = 'run_root'; root = $root
                    root_anchor = $rootAnchor; children = $children
                })
            $transferred = $true
        }
    }
    finally {
        if (-not $transferred) {
            foreach ($anchor in $children.Values) { $anchor.handle.Dispose() }
            $rootAnchor.handle.Dispose()
        }
    }
}

function Assert-M0RunRootEnvelopeUnchanged {
    param([Parameter(Mandatory)] $Envelope, [Parameter(Mandatory)] [string] $Context)
    Assert-M0DirectoryAnchorUnchanged $Envelope.root_anchor "$Context RunRoot"
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [IO.DirectoryInfo]::new([string]$Envelope.root).EnumerateFileSystemInfos()) {
        if ($entry -isnot [IO.DirectoryInfo] -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
            -not $Envelope.children.ContainsKey($entry.Name) -or -not $seen.Add($entry.Name)) {
            throw "$Context RunRoot envelope contains an extra or replaced child."
        }
        Assert-M0DirectoryAnchorUnchanged $Envelope.children[$entry.Name] "$Context RunRoot child"
    }
    if ($seen.Count -ne 2) { throw "$Context RunRoot envelope contains an extra or missing child." }
}

function Assert-M0QtShellCompositeReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $ExpectedQtBaseReceiptSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new(
            'Qt shell composite receipt verification requires Windows physical-file semantics.')
    }
    Assert-M0Digest $ExpectedReceiptSha256 'Expected composite receipt SHA-256'
    $contract = Assert-M0QtShellCompositeContract $ContractPath $ExpectedContractSha256
    $repository = ConvertTo-M0NormalizedDirectoryPath $RepositoryRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'Composite RepositoryRoot does not exist.' }
    Assert-M0NoReparseChain $repository $repository 'Composite RepositoryRoot'
    $runParent = Resolve-M0ContainedPath $repository $contract.isolation.run_parent_relative_path `
        'Composite receipt RunRoot parent'
    if (-not [IO.Directory]::Exists($runParent)) { throw 'Composite receipt RunRoot parent is missing.' }
    Assert-M0NoReparseChain $runParent $repository 'Composite receipt RunRoot parent'
    $receiptFull = [IO.Path]::GetFullPath($LiteralPath)
    if (-not [IO.File]::Exists($receiptFull)) { throw 'Composite receipt is missing.' }
    $runRoot = Get-M0RootForRelativeFile $receiptFull $contract.isolation.receipt_relative_path `
        'Composite receipt'
    if (-not [string]::Equals([IO.Path]::GetDirectoryName($runRoot), $runParent,
            [StringComparison]::OrdinalIgnoreCase) -or
        [string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($runRoot))) {
        throw 'Composite receipt RunRoot must be a canonical direct child of its pinned parent.'
    }
    Assert-M0NoReparseChain $runRoot $runParent 'Composite receipt RunRoot'
    $runId = [IO.Path]::GetFileName($runRoot)
    $leases = [Collections.Generic.List[object]]::new()
    try {
        $contractFull = [IO.Path]::GetFullPath($ContractPath)
        if (-not (Test-M0PathWithin $contractFull $repository)) {
            throw 'Composite contract must be inside RepositoryRoot.'
        }
        $contractLease = Open-M0PhysicalFile $contractFull $repository 'Composite contract' `
            (Get-Item -LiteralPath $contractFull).Length $ExpectedContractSha256 -RequireSingleLink
        $leases.Add($contractLease)
        $null = Assert-M0RecipeFiles $contract $repository $leases
        Assert-M0HistoricalV1 $contract $repository $leases
        $null = Assert-M0QtBaseParent $contract $ExpectedQtBaseReceiptSha256 $repository $leases
        $extractor = $contract.inputs.extractor
        $extractorPath = Resolve-M0ContainedPath $repository $extractor.relative_path `
            'Composite authenticated extractor'
        $leases.Add((Open-M0PhysicalFile $extractorPath $repository 'Composite authenticated extractor' `
                ([long]$extractor.size_bytes) ([string]$extractor.sha256) -RequireSingleLink))
        foreach ($archive in @($contract.inputs.archives)) {
            $archivePath = Resolve-M0ContainedPath $repository $archive.relative_path `
                "Composite archive '$($archive.owner_id)'"
            $leases.Add((Open-M0PhysicalFile $archivePath $repository `
                    "Composite archive '$($archive.owner_id)'" `
                    ([long]$archive.size_bytes) ([string]$archive.sha256) -RequireSingleLink))
        }

        $receiptLease = Open-M0PhysicalFile $receiptFull $runRoot 'Composite receipt' `
            (Get-Item -LiteralPath $receiptFull).Length $ExpectedReceiptSha256 -RequireSingleLink
        $leases.Add($receiptLease)
        $receipt = Read-M0ClosedJson $receiptLease.path 'Composite receipt'
        $null = Assert-M0QtShellCompositeReceiptDocument $receipt $contract $ExpectedContractSha256
        $manifestPath = Open-M0Reference $receipt.evidence $runRoot 'Composite evidence manifest' $leases
        $manifest = Read-M0ClosedJson $manifestPath 'Composite evidence manifest'
        Assert-M0ManifestDocument $manifest $contract $ExpectedContractSha256 $runId
        if ($receipt.captured_utc -cne $manifest.captured_utc -or
            -not (Test-M0JsonEquivalent $receipt.sdk $manifest.sdk)) {
            throw 'Composite receipt and evidence manifest observations drifted.'
        }

        $globalReference = [pscustomobject][ordered]@{
            relative_path = $receipt.sdk.inventory_relative_path
            size_bytes = [long]$receipt.sdk.inventory_size_bytes
            sha256 = $receipt.sdk.inventory_sha256
        }
        $globalPath = Open-M0Reference $globalReference $runRoot 'Composite global inventory' $leases
        $global = ConvertFrom-M0CanonicalInventory $globalPath 5 '' 'Composite global inventory'
        $ownerEntries = [ordered]@{}
        foreach ($ownerManifest in @($manifest.owner_inventories)) {
            $reference = [pscustomobject][ordered]@{
                relative_path = $ownerManifest.inventory_relative_path
                size_bytes = [long]$ownerManifest.inventory_size_bytes
                sha256 = $ownerManifest.inventory_sha256
            }
            $ownerPath = Open-M0Reference $reference $runRoot `
                "Owner '$($ownerManifest.owner_id)' inventory" $leases
            $ownerInventory = ConvertFrom-M0CanonicalInventory $ownerPath 5 $ownerManifest.owner_id `
                "Owner '$($ownerManifest.owner_id)' inventory"
            $ownerRows = @($ownerInventory.rows)
            if ($ownerRows.Count -ne [long]$ownerManifest.file_count -or
                [long](($ownerRows | Measure-Object -Property size_bytes -Sum).Sum) -ne
                    [long]$ownerManifest.total_size_bytes) {
                throw "Owner '$($ownerManifest.owner_id)' inventory count or total size drifted."
            }
            $ownerEntries[$ownerManifest.owner_id] = @($ownerRows | ForEach-Object {
                    [pscustomobject][ordered]@{
                        source_relative_path = $_.source_relative_path; relative_path = $_.relative_path
                        size_bytes = [long]$_.size_bytes; sha256 = $_.sha256
                    }
                })
        }
        $plan = @(New-M0QtShellCompositeOwnershipPlan $ownerEntries)
        $canonicalGlobal = ConvertTo-M0InventoryText $plan
        if ($global.text -cne $canonicalGlobal -or @($global.rows).Count -ne $plan.Count) {
            throw 'Composite global inventory differs from the exact per-owner closure.'
        }
        if ($plan.Count -ne [long]$receipt.sdk.file_count -or
            [long](($plan | Measure-Object -Property size_bytes -Sum).Sum) -ne
                [long]$receipt.sdk.total_size_bytes) {
            throw 'Composite receipt SDK count or total size differs from ownership.'
        }
        Assert-M0RequiredArtifacts $plan $contract

        $sdkRoot = Resolve-M0ContainedPath $runRoot $receipt.sdk.root_relative_path 'Composite physical SDK'
        $null = Assert-M0ExactSdkTree $sdkRoot $plan $leases
        Assert-M0EvidenceTree $runRoot $contract $manifest.owner_inventories $leases
        Assert-M0RunRootEnvelope $runRoot -LeaseSink $leases
        Assert-M0LeasesUnchanged $leases 'Composite receipt verification'
        return $receipt
    }
    finally { Close-M0Leases $leases }
}

Export-ModuleMember -Function @(
    'Assert-M0QtShellCompositeContract',
    'Assert-M0QtShellCompositeReceipt',
    'Invoke-M0QtShellComposite'
)
