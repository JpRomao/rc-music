#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:LoadedModulePath = [IO.Path]::GetFullPath($PSCommandPath)

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
$script:EvidenceManifestRelativePath = 'evidence/qtbase-refresh-manifest.v1.json'
$script:EvidenceManifestV2RelativePath = 'evidence/qtbase-refresh-manifest.v2.json'
$script:ExpectedRecipeFiles = @(
    'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1',
    'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1',
    'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1'
)
$script:ExpectedRecipeIds = @('orchestrator', 'runner', 'entrypoint')
$script:ExpectedTreeIds = @('qtbase-before', 'qtbase-patched', 'vulkan', 'install')
$script:ExpectedTreeRoots = @('source/qtbase-before', 'source/qtbase-patched', 'source/vulkan', 'install')
$script:ExpectedInventoryPaths = @(
    'evidence/inventories/qtbase-before.tsv',
    'evidence/inventories/qtbase-patched.tsv',
    'evidence/inventories/vulkan.tsv',
    'evidence/inventories/install.tsv'
)
$script:ExpectedCommandIds = @('configure', 'build', 'install')
$script:ExpectedCommandSpecs = @(
    [pscustomobject]@{
        id = 'configure'; tool_id = 'cmd'; script_relative_path = 'source/qtbase-patched/configure.bat'
        arguments_binding = 'configuration.arguments'; working_directory_relative_path = 'build'
        stdout_relative_path = 'evidence/logs/configure.stdout.log'; stderr_relative_path = 'evidence/logs/configure.stderr.log'
    },
    [pscustomobject]@{
        id = 'build'; tool_id = 'cmake'; script_relative_path = $null
        arguments_binding = 'configuration.build_arguments'; working_directory_relative_path = 'build'
        stdout_relative_path = 'evidence/logs/build.stdout.log'; stderr_relative_path = 'evidence/logs/build.stderr.log'
    },
    [pscustomobject]@{
        id = 'install'; tool_id = 'cmake'; script_relative_path = $null
        arguments_binding = 'configuration.install_arguments'; working_directory_relative_path = 'build'
        stdout_relative_path = 'evidence/logs/install.stdout.log'; stderr_relative_path = 'evidence/logs/install.stderr.log'
    }
)
$script:ExpectedReportIds = @(
    'configure-summary',
    'cmake-cache',
    'qtgui-freetype-dependency',
    'qt6gui-headers',
    'qt6gui-imports',
    'qt6gui-exports',
    'qminimal-imports'
)
$script:ExpectedReportSpecs = @(
    [pscustomobject]@{ id = 'configure-summary'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'build/config.summary'; relative_path = 'evidence/reports/configure-summary.txt' },
    [pscustomobject]@{ id = 'cmake-cache'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'build/CMakeCache.txt'; relative_path = 'evidence/reports/cmake-cache.txt' },
    [pscustomobject]@{ id = 'qtgui-freetype-dependency'; producer_id = 'runner'; arguments = @('analyze-freetype-link-graph'); subject_relative_path = 'build/CMakeFiles/impl-RelWithDebInfo.ninja'; relative_path = 'evidence/reports/qtgui-freetype-dependency.txt' },
    [pscustomobject]@{ id = 'qt6gui-headers'; producer_id = 'dumpbin'; arguments = @('/nologo', '/headers'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-headers.txt' },
    [pscustomobject]@{ id = 'qt6gui-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-imports.txt' },
    [pscustomobject]@{ id = 'qt6gui-exports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/exports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-exports.txt' },
    [pscustomobject]@{ id = 'qminimal-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/plugins/platforms/qminimal.dll'; relative_path = 'evidence/reports/qminimal-imports.txt' }
)
$script:ExpectedCMakeCacheEntries = [ordered]@{
    CMAKE_CONFIGURATION_TYPES = [pscustomobject]@{ type = 'UNINITIALIZED'; value = 'RelWithDebInfo;Debug' }
    CMAKE_GENERATOR = [pscustomobject]@{ type = 'INTERNAL'; value = 'Ninja Multi-Config' }
    BUILD_SHARED_LIBS = [pscustomobject]@{ type = 'BOOL'; value = 'ON' }
    BUILD_WITH_PCH = [pscustomobject]@{ type = 'BOOL'; value = 'OFF' }
    CMAKE_INTERPROCEDURAL_OPTIMIZATION = [pscustomobject]@{ type = 'UNINITIALIZED'; value = 'OFF' }
    QT_FEATURE_accessibility = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_direct2d = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_direct2d1_1 = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_directwrite = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_directwrite3 = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_directwritecolrv1 = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_dynamicgl = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_freetype = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_harfbuzz = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_intelcet = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_jpeg = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_msvc_obj_debug_info = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_opengl = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_openssl = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_pcre2 = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_png = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_schannel = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_system_freetype = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_system_harfbuzz = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_system_jpeg = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_system_pcre2 = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_system_png = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_system_zlib = [pscustomobject]@{ type = 'INTERNAL'; value = 'OFF' }
    QT_FEATURE_systemtrayicon = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
    QT_FEATURE_vulkan = [pscustomobject]@{ type = 'INTERNAL'; value = 'ON' }
}
$script:ExpectedConfigSummaryLines = @(
    '  Mode ................................... debug and release (with debug info)',
    '  Building shared libraries (alias of  BUILD_SHARED_LIBS)  yes',
    '  Using Link Time Optimization (LTCG) .... no',
    '  Using Intel Control-flow Enforcement Technology (CET)  yes',
    '  OpenSSL ................................ no',
    '    Qt directly linked to OpenSSL ........ no',
    '  OpenSSL 1.1 ............................ no',
    '  OpenSSL 3.0 ............................ no',
    '  Using system zlib ...................... no',
    '  PCRE2 .................................. yes',
    '    Using system PCRE2 ................... no',
    '  Schannel ............................... yes',
    '  Accessibility .......................... yes',
    '  FreeType ............................... yes',
    '    Using system FreeType ................ no',
    '  HarfBuzz ............................... yes',
    '    Using system HarfBuzz ................ no',
    '    JPEG ................................. yes',
    '    PNG .................................. yes',
    '  OpenGL:',
    '    Desktop OpenGL ....................... no',
    '    Dynamic OpenGL ....................... yes',
    '    OpenGL ES 2.0 ........................ no',
    '    OpenGL ES 3.0 ........................ no',
    '    OpenGL ES 3.1 ........................ no',
    '    OpenGL ES 3.2 ........................ no',
    '  Vulkan ................................. yes',
    '    DirectWrite .......................... yes',
    '    DirectWrite 3 ........................ yes',
    '    DirectWrite COLRv1 Support ........... yes'
)
$script:ExpectedDirectConsumers = @('install/plugins/platforms/qminimal.dll')
$script:ReceiptLimits = @(
    'diagnostic M0 rebuild only',
    'does not approve a Qt toolkit decision',
    'does not approve redistribution or publication',
    'does not replace the official SDK or historical packages'
)

if ($IsWindows) {
    $nativeNamespace = 'M0QtBaseRefreshNative_' + [Guid]::NewGuid().ToString('N')
    $nativeSource = @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
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

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_STATUS_BLOCK {
        public IntPtr Status;
        public UIntPtr Information;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct WIN32_FIND_STREAM_DATA {
        public long StreamSize;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)]
        public string StreamName;
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
        private const int ERROR_HANDLE_EOF = 38;

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

        public static SafeFileHandle OpenDirectoryWithoutWriteDeleteSharing(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenFileReadOnlyWithoutWriteDeleteSharing(string path) {
            var handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle CreateNewReadWriteDelete(string path) {
            var handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS,
                FILE_SHARE_READ, IntPtr.Zero, CREATE_NEW, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
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
    $nativeTypes = @(Add-Type -TypeDefinition $nativeSource.Replace('__M0_NATIVE_NAMESPACE__', $nativeNamespace) -PassThru)
    $nativeFileIdentityTypes = @($nativeTypes | Where-Object Name -CEQ 'FileIdentity')
    if ($nativeFileIdentityTypes.Count -ne 1) { throw 'QtBase refresh native helper compilation did not yield one isolated FileIdentity type.' }
    $script:NativeFileIdentity = $nativeFileIdentityTypes[0]
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

function Assert-M0CanonicalUtcTimestamp {
    param($Value, [Parameter(Mandatory)][string] $Context)
    Assert-M0String $Value $Context
    $parsed = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParseExact(
            $Value,
            'O',
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind,
            [ref]$parsed
        ) -or
        $parsed.Offset -ne [TimeSpan]::Zero -or
        $parsed.ToString('O', [Globalization.CultureInfo]::InvariantCulture) -cne $Value) {
        throw "$Context must be a canonical round-trip UTC timestamp."
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
    if ($Actual -isnot [Array]) {
        throw "$Context must be a JSON array."
    }
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

function New-M0ExclusiveDirectory {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    if (-not $IsWindows) { throw 'Exclusive QtBase refresh directory creation is proven only on Windows.' }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $script:NativeFileIdentity::CreateDirectoryNew((ConvertTo-M0ExtendedPath $full))
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
        [Parameter(Mandatory)] [string] $RunRoot,
        [switch] $RetainPublishedLease
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
    $tempHandle = $null
    $stream = $null
    $published = $false
    try {
        $parentHandle = $script:NativeFileIdentity::OpenDirectoryWithoutDeleteSharing((ConvertTo-M0ExtendedPath $parent))
        $parentIdentity = $script:NativeFileIdentity::GetIdentity($parentHandle)
        $parentFinal = ConvertTo-M0NormalizedDirectoryPath (ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($parentHandle)))
        if (-not [string]::Equals($parentFinal, $parent, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Receipt evidence parent handle resolved through a reparse point or different identity.'
        }

        $tempHandle = $script:NativeFileIdentity::CreateNewReadWriteDelete((ConvertTo-M0ExtendedPath $temp))
        $stream = [IO.FileStream]::new($tempHandle, [IO.FileAccess]::ReadWrite, 4096, $false)
        $tempHandle = $null
        if ($script:NativeFileIdentity::GetLinkCount($stream.SafeFileHandle) -ne 1) {
            throw 'Receipt temporary file must have exactly one link.'
        }
        $tempIdentity = $script:NativeFileIdentity::GetIdentity($stream.SafeFileHandle)
        $tempFinal = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($stream.SafeFileHandle))))
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
        if ($script:NativeFileIdentity::GetIdentity($parentHandle) -cne $parentIdentity -or
            $script:NativeFileIdentity::GetIdentity($stream.SafeFileHandle) -cne $tempIdentity) {
            throw 'Receipt retained parent or temporary file identity changed before publication.'
        }

        $script:NativeFileIdentity::RenameNoReplaceRelative(
            $stream.SafeFileHandle,
            $parentHandle,
            [IO.Path]::GetFileName($final)
        )
        if ($script:NativeFileIdentity::GetIdentity($parentHandle) -cne $parentIdentity -or
            $script:NativeFileIdentity::GetIdentity($stream.SafeFileHandle) -cne $tempIdentity -or
            $script:NativeFileIdentity::GetLinkCount($stream.SafeFileHandle) -ne 1) {
            throw 'Receipt retained identities changed after publication.'
        }
        $finalFromHandle = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($finalFromHandle, $final, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Receipt final handle resolved to a different path after publication.'
        }
        Assert-M0NoReparseChain -LiteralPath $final -BoundaryRoot $RunRoot -Context 'Published receipt'
        if ($stream.Length -ne $Bytes.Length -or (Get-M0Sha256FromStream $stream) -cne $expectedSha256) {
            throw 'Receipt final bytes changed after publication.'
        }
        $published = $true
        $result = [ordered]@{ sha256 = $expectedSha256; size_bytes = [long]$Bytes.Length }
        if ($RetainPublishedLease) {
            $result.lease = [pscustomobject]@{
                path = $final
                stream = $stream
                identity = $tempIdentity
                link_count = [long]1
                expected_size_bytes = [long]$Bytes.Length
                expected_sha256 = $expectedSha256
                require_single_link = $true
                boundary_root = (ConvertTo-M0NormalizedDirectoryPath $RunRoot)
                context = 'Published evidence file'
            }
            $stream = $null
        }
        return [pscustomobject]$result
    }
    finally {
        $cleanupFailure = $null
        if (-not $published -and $null -ne $stream) {
            try { $script:NativeFileIdentity::MarkDeleteOnClose($stream.SafeFileHandle) }
            catch { $cleanupFailure = $_ }
        }
        if (-not $published -and $null -ne $tempHandle) {
            try { $script:NativeFileIdentity::MarkDeleteOnClose($tempHandle) }
            catch { $cleanupFailure = $_ }
        }
        if ($null -ne $stream) { try { $stream.Dispose() } catch { } }
        if ($null -ne $tempHandle) { try { $tempHandle.Dispose() } catch { } }
        if ($null -ne $parentHandle) { try { $parentHandle.Dispose() } catch { } }
        if ($null -ne $cleanupFailure) {
            throw [IO.InvalidDataException]::new('Receipt failure cleanup could not retain delete-on-close semantics.', $cleanupFailure.Exception)
        }
    }
}

function Open-M0PhysicalFileLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $Context,
        [string] $BoundaryRoot,
        [switch] $RequireSingleLink
    )
    if (-not $IsWindows) { throw "$Context retained physical authentication is proven only on Windows." }
    if (-not [IO.File]::Exists($LiteralPath)) { throw "$Context input file is missing: $LiteralPath" }
    $item = Get-Item -LiteralPath $LiteralPath -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "$Context input is a reparse point." }
    if (-not [string]::IsNullOrEmpty($BoundaryRoot)) {
        Assert-M0NoReparseChain -LiteralPath $item.FullName -BoundaryRoot $BoundaryRoot -Context $Context
    }
    $handle = $null
    $stream = $null
    try {
        $handle = $script:NativeFileIdentity::OpenFileReadOnlyWithoutWriteDeleteSharing(
            (ConvertTo-M0ExtendedPath $item.FullName)
        )
        $stream = [IO.FileStream]::new($handle, [IO.FileAccess]::Read, 65536, $false)
        $handle = $null
        $finalPath = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($stream.SafeFileHandle))))
        if (-not [string]::Equals($finalPath, [IO.Path]::GetFullPath($item.FullName), [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context retained handle resolved to a different physical path."
        }
        $linkCount = $script:NativeFileIdentity::GetLinkCount($stream.SafeFileHandle)
        if ($RequireSingleLink -and $linkCount -ne 1) { throw "$Context physical leaf must have exactly one hardlink." }
        if ([long]$stream.Length -ne $ExpectedSizeBytes) { throw "$Context input size pin mismatch." }
        $actualSha256 = Get-M0Sha256FromStream $stream
        if ($actualSha256 -cne $ExpectedSha256) { throw "$Context input SHA-256 pin mismatch." }
        return [pscustomobject]@{
            path = $finalPath
            stream = $stream
            identity = $script:NativeFileIdentity::GetIdentity($stream.SafeFileHandle)
            link_count = [long]$linkCount
            expected_size_bytes = $ExpectedSizeBytes
            expected_sha256 = $ExpectedSha256
            require_single_link = [bool]$RequireSingleLink
            boundary_root = if ([string]::IsNullOrEmpty($BoundaryRoot)) { $null } else { ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot }
            context = $Context
        }
    }
    catch {
        if ($null -ne $stream) { try { $stream.Dispose() } catch { } }
        if ($null -ne $handle) { try { $handle.Dispose() } catch { } }
        throw
    }
}

function Open-M0PhysicalDirectoryLease {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Context,
        [Parameter(Mandatory)] [string] $BoundaryRoot
    )
    if (-not $IsWindows) { throw "$Context retained physical authentication is proven only on Windows." }
    if (-not [IO.Directory]::Exists($LiteralPath)) { throw "$Context directory is missing: $LiteralPath" }
    $item = Get-Item -LiteralPath $LiteralPath -Force -ErrorAction Stop
    if ($item -isnot [IO.DirectoryInfo] -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "$Context is not an ordinary physical directory."
    }
    Assert-M0NoReparseChain -LiteralPath $item.FullName -BoundaryRoot $BoundaryRoot -Context $Context
    $handle = $null
    try {
        $handle = $script:NativeFileIdentity::OpenDirectoryWithoutWriteDeleteSharing(
            (ConvertTo-M0ExtendedPath $item.FullName)
        )
        $finalPath = ConvertTo-M0NormalizedDirectoryPath (ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($handle)))
        if (-not [string]::Equals($finalPath, (ConvertTo-M0NormalizedDirectoryPath $item.FullName), [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context retained handle resolved to a different physical directory."
        }
        $result = [pscustomobject]@{
            path = $finalPath
            handle = $handle
            identity = $script:NativeFileIdentity::GetIdentity($handle)
            boundary_root = ConvertTo-M0NormalizedDirectoryPath $BoundaryRoot
            context = $Context
        }
        $handle = $null
        return $result
    }
    finally {
        if ($null -ne $handle) { try { $handle.Dispose() } catch { } }
    }
}

function Assert-M0PhysicalFileLeasesUnchanged {
    param(
        [Parameter(Mandatory)] $Leases,
        [Parameter(Mandatory)] [string] $Context
    )
    foreach ($lease in @($Leases)) {
        $kindProperty = if ($null -eq $lease) { $null } else { $lease.PSObject.Properties['kind'] }
        if ($null -ne $kindProperty -and $kindProperty.Value -ceq 'tree_snapshot') {
            $null = Assert-M0PhysicalTreeInventory `
                -TreeRoot $lease.tree_root `
                -InventoryPath $lease.inventory_path `
                -ExpectedFileCount ([long]$lease.expected_file_count) `
                -ExpectedTotalSizeBytes ([long]$lease.expected_total_size_bytes) `
                -ExpectedInventorySha256 $lease.expected_inventory_sha256 `
                -Context "$Context retained $($lease.context)"
            continue
        }
        $directoryHandleProperty = if ($null -eq $lease) { $null } else { $lease.PSObject.Properties['handle'] }
        if ($null -ne $directoryHandleProperty) {
            $handle = $directoryHandleProperty.Value
            if ($null -eq $handle -or $handle.IsClosed -or $handle.IsInvalid) {
                throw "$Context retained physical directory lease is unavailable."
            }
            $actualIdentity = $script:NativeFileIdentity::GetIdentity($handle)
            if ($actualIdentity -cne $lease.identity) {
                throw "$Context retained physical directory identity changed after authentication for '$($lease.context)'."
            }
            $actualPath = ConvertTo-M0NormalizedDirectoryPath (ConvertFrom-M0FinalPath `
                ($script:NativeFileIdentity::GetFinalPath($handle)))
            if (-not [string]::Equals($actualPath, $lease.path, [StringComparison]::OrdinalIgnoreCase)) {
                throw "$Context retained physical directory path changed after authentication for '$($lease.context)'."
            }
            Assert-M0NoReparseChain -LiteralPath $actualPath -BoundaryRoot $lease.boundary_root -Context "$Context retained '$($lease.context)'"
            continue
        }
        if ($null -eq $lease -or $null -eq $lease.stream -or
            $lease.stream.SafeFileHandle.IsClosed -or $lease.stream.SafeFileHandle.IsInvalid) {
            throw "$Context retained physical lease is unavailable."
        }
        $actualIdentity = $script:NativeFileIdentity::GetIdentity($lease.stream.SafeFileHandle)
        if ($actualIdentity -cne $lease.identity) {
            throw "$Context retained physical identity changed after authentication for '$($lease.context)'."
        }
        $actualPath = [IO.Path]::GetFullPath((ConvertFrom-M0FinalPath `
            ($script:NativeFileIdentity::GetFinalPath($lease.stream.SafeFileHandle))))
        if (-not [string]::Equals($actualPath, $lease.path, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$Context retained physical path changed after authentication for '$($lease.context)'."
        }
        if (-not [string]::IsNullOrEmpty($lease.boundary_root)) {
            Assert-M0NoReparseChain -LiteralPath $actualPath -BoundaryRoot $lease.boundary_root -Context "$Context retained '$($lease.context)'"
        }
        $actualLinkCount = [long]$script:NativeFileIdentity::GetLinkCount($lease.stream.SafeFileHandle)
        if ($actualLinkCount -ne [long]$lease.link_count -or
            ([bool]$lease.require_single_link -and $actualLinkCount -ne 1)) {
            throw "$Context hardlink count changed after authentication for '$($lease.context)'."
        }
        if ([long]$lease.stream.Length -ne [long]$lease.expected_size_bytes) {
            throw "$Context retained physical size changed after authentication for '$($lease.context)'."
        }
        if ((Get-M0Sha256FromStream $lease.stream) -cne $lease.expected_sha256) {
            throw "$Context retained physical SHA-256 changed after authentication for '$($lease.context)'."
        }
    }
}

function Assert-M0PhysicalFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [long] $ExpectedSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedSha256,
        [Parameter(Mandatory)] [string] $Context,
        [string] $BoundaryRoot,
        [switch] $RequireSingleLink
    )
    $lease = Open-M0PhysicalFileLease @PSBoundParameters
    try { return $lease.path }
    finally { $lease.stream.Dispose() }
}

function Close-M0PhysicalFileLeases {
    param([Parameter(Mandatory)] $Leases)
    foreach ($lease in @($Leases)) {
        if ($null -ne $lease -and $null -ne $lease.PSObject.Properties['kind'] -and $lease.kind -ceq 'tree_snapshot') {
            continue
        }
        if ($null -ne $lease -and $null -ne $lease.PSObject.Properties['handle']) {
            try { $lease.handle.Dispose() } catch { }
            continue
        }
        if ($null -ne $lease -and $null -ne $lease.stream) {
            try { $lease.stream.Dispose() } catch { }
        }
    }
}

function Set-M0UncommittedPublicationsDeleteOnClose {
    param([Parameter(Mandatory)] $PublicationLeases)
    foreach ($lease in @($PublicationLeases)) {
        if ($null -eq $lease -or $null -eq $lease.stream -or
            $lease.stream.SafeFileHandle.IsClosed -or $lease.stream.SafeFileHandle.IsInvalid) {
            throw 'Uncommitted publication lease is unavailable for failure cleanup.'
        }
        $script:NativeFileIdentity::MarkDeleteOnClose($lease.stream.SafeFileHandle)
    }
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
    $prefix = if ([string]::Equals($fullRoot, [IO.Path]::GetPathRoot($fullRoot), [StringComparison]::OrdinalIgnoreCase)) {
        $fullRoot
    }
    else {
        $fullRoot + [IO.Path]::DirectorySeparatorChar
    }
    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Context escapes its pinned root."
    }
    return $candidate
}

function Assert-M0NonNegativeInteger {
    param($Value, [Parameter(Mandatory)][string] $Context)
    if ($Value -isnot [long] -or $Value -lt 0) {
        throw "$Context must be a non-negative integer."
    }
}

function Assert-M0CanonicalEvidencePath {
    param($Value, [Parameter(Mandatory)][string] $Context)
    Assert-M0SafeRelativePath $Value $Context
    if ($Value.Contains('\\') -or $Value -cmatch '[\x00-\x1f]') {
        throw "$Context must use canonical forward-slash path syntax."
    }
    foreach ($segment in @($Value -split '/')) {
        if ($segment.EndsWith(' ', [StringComparison]::Ordinal) -or
            $segment.EndsWith('.', [StringComparison]::Ordinal) -or
            $segment -cmatch '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$') {
            throw "$Context contains a Windows-reserved or ambiguous segment."
        }
    }
}

function Assert-M0FileReference {
    param([Parameter(Mandatory)] $Reference, [Parameter(Mandatory)][string] $Context)
    Assert-M0ExactProperties $Reference @('relative_path', 'size_bytes', 'sha256') $Context
    Assert-M0CanonicalEvidencePath $Reference.relative_path "$Context relative_path"
    Assert-M0NonNegativeInteger $Reference.size_bytes "$Context size_bytes"
    Assert-M0Digest $Reference.sha256 "$Context sha256"
}

function Assert-M0PhysicalReference {
    param(
        [Parameter(Mandatory)] $Reference,
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][string] $BoundaryRoot,
        [Parameter(Mandatory)][string] $Context,
        $LeaseSink
    )
    Assert-M0FileReference $Reference $Context
    $path = Resolve-M0ContainedPath $Root $Reference.relative_path $Context
    $lease = Open-M0PhysicalFileLease `
        -LiteralPath $path `
        -ExpectedSizeBytes ([long]$Reference.size_bytes) `
        -ExpectedSha256 $Reference.sha256 `
        -Context $Context `
        -BoundaryRoot $BoundaryRoot `
        -RequireSingleLink
    if ($null -ne $LeaseSink) {
        $LeaseSink.Add($lease)
        return $lease.path
    }
    try { return $lease.path }
    finally { $lease.stream.Dispose() }
}

function Get-M0PeFileOffset {
    param(
        [Parameter(Mandatory)][uint32] $Rva,
        [Parameter(Mandatory)][long] $RequiredLength,
        [Parameter(Mandatory)] $Sections,
        [Parameter(Mandatory)][uint32] $SizeOfHeaders,
        [Parameter(Mandatory)][long] $FileLength,
        [Parameter(Mandatory)][string] $Context
    )
    if ($RequiredLength -lt 0) { throw "$Context PE range length is invalid." }
    if ([long]$Rva -lt [long]$SizeOfHeaders) {
        if ([long]$Rva + $RequiredLength -le [long]$SizeOfHeaders -and
            [long]$Rva + $RequiredLength -le $FileLength) {
            return [long]$Rva
        }
        throw "$Context PE header RVA exceeds the physical headers."
    }
    foreach ($section in @($Sections)) {
        $start = [long]$section.virtual_address
        $span = [Math]::Max([long]$section.virtual_size, [long]$section.raw_size)
        $rvaValue = [long]$Rva
        if ($rvaValue -lt $start -or $rvaValue -ge $start + $span) { continue }
        $delta = $rvaValue - $start
        if ($delta + $RequiredLength -gt [long]$section.raw_size) {
            throw "$Context PE RVA is not backed by physical section data."
        }
        $offset = [long]$section.raw_pointer + $delta
        if ($offset -lt 0 -or $offset + $RequiredLength -gt $FileLength) {
            throw "$Context PE RVA resolves beyond the physical file."
        }
        return $offset
    }
    throw "$Context PE RVA does not resolve to a section."
}

function Read-M0PeAsciiString {
    param(
        [Parameter(Mandatory)][IO.FileStream] $Stream,
        [Parameter(Mandatory)][long] $Offset,
        [Parameter(Mandatory)][string] $Context
    )
    if ($Offset -lt 0 -or $Offset -ge $Stream.Length) { throw "$Context PE string offset is invalid." }
    $Stream.Position = $Offset
    $builder = [Text.StringBuilder]::new()
    for ($index = 0; $index -lt 4096 -and $Stream.Position -lt $Stream.Length; $index++) {
        $value = $Stream.ReadByte()
        if ($value -eq 0) {
            if ($builder.Length -eq 0) { throw "$Context PE string is empty." }
            return $builder.ToString()
        }
        if ($value -lt 0x20 -or $value -gt 0x7e) { throw "$Context PE string is not printable ASCII." }
        $null = $builder.Append([char]$value)
    }
    throw "$Context PE string is unterminated or exceeds the accepted limit."
}

function Get-M0PeX64Metadata {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][string] $Context)
    $stream = [IO.FileStream]::new($LiteralPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 4096) { throw "$Context is not a complete PE image." }
        $reader = [IO.BinaryReader]::new($stream, [Text.Encoding]::UTF8, $true)
        try {
            if ($reader.ReadUInt16() -ne 0x5a4d) { throw "$Context lacks the DOS MZ signature." }
            $stream.Position = 0x3c
            $peOffset = $reader.ReadInt32()
            if ($peOffset -lt 0x40 -or [long]$peOffset + 6 -gt $stream.Length) { throw "$Context has an invalid PE header offset." }
            $stream.Position = $peOffset
            if ($reader.ReadUInt32() -ne 0x00004550) { throw "$Context lacks the PE signature." }
            if ($reader.ReadUInt16() -ne 0x8664) { throw "$Context is not an x86-64 PE image." }
            $sectionCount = $reader.ReadUInt16()
            if ($sectionCount -lt 2 -or $sectionCount -gt 96) { throw "$Context has an invalid PE section count." }
            $stream.Position = $peOffset + 20
            $optionalHeaderSize = $reader.ReadUInt16()
            $characteristics = $reader.ReadUInt16()
            if ($optionalHeaderSize -lt 0xf0 -or ($characteristics -band 0x2000) -eq 0) {
                throw "$Context is not a complete x86-64 DLL image."
            }
            $optionalHeader = $peOffset + 24
            $sectionTable = $optionalHeader + $optionalHeaderSize
            $sectionTableEnd = [long]$sectionTable + ([long]$sectionCount * 40)
            if ($sectionTableEnd -gt $stream.Length) { throw "$Context PE section table exceeds the file." }
            $stream.Position = $optionalHeader
            if ($reader.ReadUInt16() -ne 0x20b) { throw "$Context optional header is not PE32+." }
            $stream.Position = $optionalHeader + 32
            $sectionAlignment = $reader.ReadUInt32()
            $fileAlignment = $reader.ReadUInt32()
            $stream.Position = $optionalHeader + 56
            $sizeOfImage = $reader.ReadUInt32()
            $sizeOfHeaders = $reader.ReadUInt32()
            $stream.Position = $optionalHeader + 108
            $directoryCount = $reader.ReadUInt32()
            if ($sectionAlignment -lt 0x1000 -or $fileAlignment -lt 0x200 -or
                ($fileAlignment -band ($fileAlignment - 1)) -ne 0 -or
                $sizeOfImage -lt $sectionAlignment -or $sizeOfHeaders -lt $sectionTableEnd -or
                $sizeOfHeaders -gt $stream.Length -or $directoryCount -lt 16) {
                throw "$Context optional header has invalid image, alignment, header, or directory values."
            }
            $stream.Position = $optionalHeader + 112
            $exportRva = $reader.ReadUInt32()
            $exportSize = $reader.ReadUInt32()
            $importRva = $reader.ReadUInt32()
            $importSize = $reader.ReadUInt32()
            if ($exportRva -eq 0 -or $exportSize -eq 0 -or $importRva -eq 0 -or $importSize -eq 0) {
                throw "$Context must contain non-empty PE export and import directories."
            }
            $hasPhysicalSection = $false
            $sections = [Collections.Generic.List[object]]::new()
            for ($index = 0; $index -lt $sectionCount; $index++) {
                $sectionOffset = [long]$sectionTable + ([long]$index * 40)
                $stream.Position = $sectionOffset
                $nameBytes = $reader.ReadBytes(8)
                $nameLength = [Array]::IndexOf($nameBytes, [byte]0)
                if ($nameLength -lt 0) { $nameLength = 8 }
                if ($nameLength -eq 0) { throw "$Context contains an unnamed PE section." }
                $stream.Position = $sectionOffset + 8
                $virtualSize = $reader.ReadUInt32()
                $virtualAddress = $reader.ReadUInt32()
                $stream.Position = $sectionOffset + 16
                $rawSize = $reader.ReadUInt32()
                $rawPointer = $reader.ReadUInt32()
                if ($rawSize -gt 0) {
                    if ($rawPointer -lt $sizeOfHeaders -or [long]$rawPointer + [long]$rawSize -gt $stream.Length) {
                        throw "$Context PE section raw data exceeds the file."
                    }
                    $hasPhysicalSection = $true
                }
                $sections.Add([pscustomobject]@{
                        virtual_size = $virtualSize
                        virtual_address = $virtualAddress
                        raw_size = $rawSize
                        raw_pointer = $rawPointer
                    })
            }
            if (-not $hasPhysicalSection) { throw "$Context does not contain a physical PE section." }

            if ($importSize -lt 20 -or $exportSize -lt 40) {
                throw "$Context PE import or export directory is structurally incomplete."
            }
            $imports = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            $importOffset = Get-M0PeFileOffset $importRva 20 $sections $sizeOfHeaders $stream.Length $Context
            $maximumDescriptors = [Math]::Min([long]4096, [Math]::Max([long]1, [long][Math]::Ceiling([double]$importSize / 20.0) + 1))
            $terminatedImports = $false
            for ($index = 0; $index -lt $maximumDescriptors; $index++) {
                $descriptorOffset = $importOffset + ([long]$index * 20)
                if ($descriptorOffset + 20 -gt $stream.Length) { throw "$Context PE import descriptor exceeds the file." }
                $stream.Position = $descriptorOffset
                $originalFirstThunk = $reader.ReadUInt32()
                $timeDateStamp = $reader.ReadUInt32()
                $forwarderChain = $reader.ReadUInt32()
                $nameRva = $reader.ReadUInt32()
                $firstThunk = $reader.ReadUInt32()
                if ($originalFirstThunk -eq 0 -and $timeDateStamp -eq 0 -and $forwarderChain -eq 0 -and
                    $nameRva -eq 0 -and $firstThunk -eq 0) {
                    $terminatedImports = $true
                    break
                }
                if ($nameRva -eq 0) { throw "$Context PE import descriptor has no module name." }
                $nameOffset = Get-M0PeFileOffset $nameRva 1 $sections $sizeOfHeaders $stream.Length $Context
                $null = $imports.Add((Read-M0PeAsciiString $stream $nameOffset "$Context import module"))
            }
            if (-not $terminatedImports -or $imports.Count -eq 0) {
                throw "$Context PE import directory is unterminated or empty."
            }

            $exportOffset = Get-M0PeFileOffset $exportRva 40 $sections $sizeOfHeaders $stream.Length $Context
            $stream.Position = $exportOffset + 20
            $numberOfFunctions = $reader.ReadUInt32()
            $numberOfNames = $reader.ReadUInt32()
            $addressOfFunctions = $reader.ReadUInt32()
            $addressOfNames = $reader.ReadUInt32()
            $addressOfNameOrdinals = $reader.ReadUInt32()
            if ($numberOfFunctions -eq 0 -or $numberOfNames -eq 0 -or
                $numberOfFunctions -gt 1000000 -or $numberOfNames -gt 1000000 -or
                $addressOfFunctions -eq 0 -or $addressOfNames -eq 0 -or $addressOfNameOrdinals -eq 0) {
                throw "$Context PE export directory has invalid function or name tables."
            }
            $null = Get-M0PeFileOffset $addressOfFunctions ([long]$numberOfFunctions * 4) $sections $sizeOfHeaders $stream.Length $Context
            $nameTableOffset = Get-M0PeFileOffset $addressOfNames ([long]$numberOfNames * 4) $sections $sizeOfHeaders $stream.Length $Context
            $null = Get-M0PeFileOffset $addressOfNameOrdinals ([long]$numberOfNames * 2) $sections $sizeOfHeaders $stream.Length $Context
            $exports = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
            for ($index = 0; $index -lt [long]$numberOfNames; $index++) {
                $stream.Position = $nameTableOffset + ([long]$index * 4)
                $nameRva = $reader.ReadUInt32()
                $nameOffset = Get-M0PeFileOffset $nameRva 1 $sections $sizeOfHeaders $stream.Length $Context
                $null = $exports.Add((Read-M0PeAsciiString $stream $nameOffset "$Context export name"))
            }
            return [pscustomobject]@{
                imports = @($imports | Sort-Object)
                exports = @($exports | Sort-Object)
            }
        }
        finally { $reader.Dispose() }
    }
    finally { $stream.Dispose() }
}

function Assert-M0PeX64 {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][string] $Context,
        [string[]] $RequiredImports = @(),
        [string[]] $RequiredExports = @()
    )
    $metadata = Get-M0PeX64Metadata $LiteralPath $Context
    foreach ($requiredImport in @($RequiredImports)) {
        if (@($metadata.imports | Where-Object { $_ -ieq $requiredImport }).Count -ne 1) {
            throw "$Context PE import table does not contain required module '$requiredImport'."
        }
    }
    foreach ($requiredExport in @($RequiredExports)) {
        if (@($metadata.exports | Where-Object { $_ -ceq $requiredExport }).Count -ne 1) {
            throw "$Context PE export table does not contain required symbol '$requiredExport'."
        }
    }
}

function Assert-M0CanonicalInventory {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][long] $ExpectedFileCount,
        [Parameter(Mandatory)][long] $ExpectedTotalSizeBytes,
        [Parameter(Mandatory)][string] $Context
    )
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf)) {
        throw "$Context inventory must be non-empty UTF-8 without BOM."
    }
    try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
    catch { throw [InvalidDataException]::new("$Context inventory is not strict UTF-8.", $_.Exception) }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($text.Length -ge 2 -and $text[$text.Length - 2] -eq "`n")) {
        throw "$Context inventory must use LF with exactly one final newline."
    }
    $lines = @($text.Substring(0, $text.Length - 1).Split("`n", [StringSplitOptions]::None))
    if ([long]$lines.Count -ne $ExpectedFileCount) { throw "$Context inventory file count differs." }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $previous = $null
    $total = [long]0
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $parts = @($lines[$index].Split("`t", [StringSplitOptions]::None))
        if ($parts.Count -ne 3) { throw "$Context inventory line $index must have exactly three TSV fields." }
        Assert-M0CanonicalEvidencePath $parts[0] "$Context inventory line $index path"
        if ($null -ne $previous -and [StringComparer]::Ordinal.Compare($previous, $parts[0]) -ge 0) {
            throw "$Context inventory is not in strict ordinal path order."
        }
        if (-not $seen.Add($parts[0])) { throw "$Context inventory contains a duplicate case-insensitive path." }
        $size = [long]0
        if (-not [long]::TryParse($parts[1], [Globalization.NumberStyles]::None, [Globalization.CultureInfo]::InvariantCulture, [ref]$size) -or
            $size -lt 0 -or $size.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $parts[1]) {
            throw "$Context inventory line $index size is not a canonical non-negative integer."
        }
        Assert-M0Digest $parts[2] "$Context inventory line $index sha256"
        if ($total -gt [long]::MaxValue - $size) { throw "$Context inventory total size overflows Int64." }
        $total += $size
        $previous = $parts[0]
    }
    if ($total -ne $ExpectedTotalSizeBytes) { throw "$Context inventory total size differs." }
}

function Assert-M0OrdinaryTreeItem {
    param([Parameter(Mandatory)] [IO.FileSystemInfo] $Item, [Parameter(Mandatory)] [string] $Context)
    if ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "$Context contains a reparse point."
    }
    $linkTypeProperty = $Item.PSObject.Properties['LinkType']
    if ($null -ne $linkTypeProperty -and $linkTypeProperty.Value -eq 'HardLink') {
        throw "$Context contains a hardlink."
    }
    foreach ($streamName in @($script:NativeFileIdentity::GetStreamNames((ConvertTo-M0ExtendedPath $Item.FullName)))) {
        if ($streamName -cne '::$DATA') { throw "$Context contains an alternate data stream." }
    }
}

function Assert-M0PhysicalTreeInventory {
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedFileCount,
        [Parameter(Mandatory)] [long] $ExpectedTotalSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256,
        [Parameter(Mandatory)] [string] $Context,
        $LeaseSink
    )
    Assert-M0Digest $ExpectedInventorySha256 "$Context expected inventory SHA-256"
    if (-not [IO.Directory]::Exists($TreeRoot)) { throw "$Context physical tree is missing." }
    $root = ConvertTo-M0NormalizedDirectoryPath $TreeRoot
    Assert-M0NoReparseChain -LiteralPath $root -BoundaryRoot $root -Context $Context
    $rootItem = Get-Item -LiteralPath $root -Force -ErrorAction Stop
    Assert-M0OrdinaryTreeItem $rootItem $Context

    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($root)
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $rows = [Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
    $total = [long]0
    while ($pending.Count -gt 0) {
        $directory = Get-Item -LiteralPath $pending.Pop() -Force -ErrorAction Stop
        Assert-M0OrdinaryTreeItem $directory $Context
        $directoryLease = Open-M0PhysicalDirectoryLease $directory.FullName "$Context directory" $root
        if ($null -ne $LeaseSink) { $LeaseSink.Add($directoryLease) }
        try {
        foreach ($item in @(Get-ChildItem -LiteralPath $directoryLease.path -Force -ErrorAction Stop)) {
            Assert-M0CanonicalEvidencePath $item.Name "$Context physical leaf name"
            $relativePath = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
            Assert-M0CanonicalEvidencePath $relativePath "$Context physical relative path"
            if (-not $seen.Add($relativePath)) { throw "$Context contains a duplicate case-insensitive physical path." }
            Assert-M0OrdinaryTreeItem $item "$Context '$relativePath'"
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Push($item.FullName)
                continue
            }
            if ($item -isnot [IO.FileInfo]) { throw "$Context contains a nonregular filesystem item." }
            $initialSha256 = Get-M0Sha256 $item.FullName
            $lease = Open-M0PhysicalFileLease `
                -LiteralPath $item.FullName `
                -ExpectedSizeBytes ([long]$item.Length) `
                -ExpectedSha256 $initialSha256 `
                -Context "$Context '$relativePath'" `
                -BoundaryRoot $root `
                -RequireSingleLink
            try {
                $size = [long]$lease.expected_size_bytes
                if ($total -gt [long]::MaxValue - $size) { throw "$Context physical total size overflows Int64." }
                $total += $size
                $rows.Add($relativePath, [pscustomobject][ordered]@{
                    relative_path = $relativePath
                    size_bytes = $size
                    sha256 = $lease.expected_sha256
                })
                if ($null -ne $LeaseSink) {
                    $LeaseSink.Add($lease)
                    $lease = $null
                }
            }
            finally { if ($null -ne $lease) { $lease.stream.Dispose() } }
        }
        }
        finally { if ($null -eq $LeaseSink) { $directoryLease.handle.Dispose() } }
    }
    if ([long]$rows.Count -ne $ExpectedFileCount -or $total -ne $ExpectedTotalSizeBytes) {
        throw "$Context physical tree count or total differs from its declared inventory."
    }
    $builder = [Text.StringBuilder]::new()
    foreach ($row in $rows.Values) {
        $null = $builder.Append($row.relative_path).Append("`t")
        $null = $builder.Append($row.size_bytes.ToString([Globalization.CultureInfo]::InvariantCulture)).Append("`t")
        $null = $builder.Append($row.sha256).Append("`n")
    }
    $canonicalBytes = [Text.UTF8Encoding]::new($false).GetBytes($builder.ToString())
    $canonicalSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($canonicalBytes)).ToLowerInvariant()

    if (-not [IO.File]::Exists($InventoryPath)) { throw "$Context inventory file is missing." }
    if ((Get-M0Sha256 $InventoryPath) -cne $ExpectedInventorySha256) {
        throw "$Context inventory SHA-256 differs from its authenticated reference."
    }
    Assert-M0CanonicalInventory $InventoryPath $ExpectedFileCount $ExpectedTotalSizeBytes $Context
    if ($canonicalSha256 -cne $ExpectedInventorySha256 -or
        [long]$canonicalBytes.Length -ne [long](Get-Item -LiteralPath $InventoryPath).Length) {
        throw "$Context inventory does not match the physical tree."
    }
    if ($null -ne $LeaseSink) {
        $LeaseSink.Add([pscustomobject]@{
            kind = 'tree_snapshot'
            tree_root = $root
            inventory_path = [IO.Path]::GetFullPath($InventoryPath)
            expected_file_count = $ExpectedFileCount
            expected_total_size_bytes = $ExpectedTotalSizeBytes
            expected_inventory_sha256 = $ExpectedInventorySha256
            context = $Context
        })
    }
    return [pscustomobject][ordered]@{
        file_count = [long]$rows.Count
        total_size_bytes = $total
        inventory_sha256 = $canonicalSha256
        rows = @($rows.Values)
    }
}

function Assert-M0ReferenceSequence {
    param(
        [Parameter(Mandatory)] $References,
        [Parameter(Mandatory)][string[]] $ExpectedPaths,
        [Parameter(Mandatory)][string] $Context
    )
    if ($References -isnot [Array]) {
        throw "$Context must be a JSON array."
    }
    $items = @($References)
    if ($items.Count -ne $ExpectedPaths.Count) {
        throw "$Context must contain the exact physical reference set."
    }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for ($index = 0; $index -lt $items.Count; $index++) {
        Assert-M0FileReference $items[$index] "$Context[$index]"
        if (-not $seen.Add([string]$items[$index].relative_path)) {
            throw "$Context contains a duplicate case-insensitive physical path."
        }
        if ($items[$index].relative_path -cne $ExpectedPaths[$index]) {
            throw "$Context path sequence drifted at index $index."
        }
    }
}

function Read-M0StrictLfText {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][string] $Context)
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf)) {
        throw "$Context semantic content is invalid: expected non-empty UTF-8 without BOM."
    }
    try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
    catch { throw [InvalidDataException]::new("$Context semantic content is invalid: expected strict UTF-8.", $_.Exception) }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($text.Length -ge 2 -and $text[$text.Length - 2] -eq "`n")) {
        throw "$Context semantic content is invalid: expected LF and exactly one final newline."
    }
    return $text
}

function Read-M0VerbatimUtf8Text {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][string] $Context)
    $bytes = [IO.File]::ReadAllBytes($LiteralPath)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf)) {
        throw "$Context semantic content is invalid: expected non-empty UTF-8 without BOM."
    }
    try { $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
    catch { throw [InvalidDataException]::new("$Context semantic content is invalid: expected strict UTF-8.", $_.Exception) }
    $normalized = $text.Replace("`r`n", "`n")
    if ($normalized.Contains("`r") -or -not $normalized.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($normalized.Length -ge 2 -and $normalized[$normalized.Length - 2] -eq "`n")) {
        throw "$Context semantic content is invalid: expected consistent LF or CRLF and exactly one final newline."
    }
    return $normalized
}

function Get-M0CMakeCacheEntries {
    param(
        [Parameter(Mandatory)][string] $Text,
        [Parameter(Mandatory)][string] $Context
    )
    $entries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($line in $Text.Split("`n", [StringSplitOptions]::None)) {
        if ([string]::IsNullOrEmpty($line) -or $line.StartsWith('#', [StringComparison]::Ordinal) -or
            $line.StartsWith('//', [StringComparison]::Ordinal)) {
            continue
        }
        $match = [Text.RegularExpressions.Regex]::Match(
            $line,
            '\A(?<key>[^:=\s]+):(?<type>[^=\s]+)=(?<value>.*)\z',
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
        )
        if (-not $match.Success) { throw "$Context contains a noncanonical CMake cache line." }
        $key = $match.Groups['key'].Value
        if ($entries.ContainsKey($key)) { throw "$Context contains duplicate CMake cache key '$key'." }
        $entries.Add($key, [pscustomobject]@{
                type = $match.Groups['type'].Value
                value = $match.Groups['value'].Value
            })
    }
    return ,$entries
}

function Assert-M0ExpectedConfigSummaryLines {
    param([Parameter(Mandatory)][string] $Text, [Parameter(Mandatory)][string] $Context)
    $lines = @($Text.Split("`n", [StringSplitOptions]::None))
    foreach ($expectedLine in $script:ExpectedConfigSummaryLines) {
        $labelPattern = if ($expectedLine -ceq '  OpenGL:') {
            '^  OpenGL:.*$'
        }
        elseif ($expectedLine.StartsWith('  Building shared libraries ', [StringComparison]::Ordinal)) {
            '^' + [Text.RegularExpressions.Regex]::Escape('  Building shared libraries (alias of  BUILD_SHARED_LIBS)') + '[ \t]{2,}.*$'
        }
        else {
            $separator = [Text.RegularExpressions.Regex]::Match($expectedLine, '\.{2,}')
            if ($separator.Success) {
                $label = $expectedLine.Substring(0, $separator.Index).TrimEnd()
                '^' + [Text.RegularExpressions.Regex]::Escape($label) + '[ \t]+\.{2,}[ \t]+.*$'
            }
            else {
                $valueSeparator = [Text.RegularExpressions.Regex]::Match($expectedLine, '\A(?<label>.*\S)[ \t]{2,}(?:yes|no)\z')
                if (-not $valueSeparator.Success) { throw "$Context expected configuration label cannot be derived." }
                '^' + [Text.RegularExpressions.Regex]::Escape($valueSeparator.Groups['label'].Value) + '[ \t]{2,}.*$'
            }
        }
        $labelLines = @($lines | Where-Object {
                [Text.RegularExpressions.Regex]::IsMatch($_, $labelPattern, [Text.RegularExpressions.RegexOptions]::CultureInvariant)
            })
        if ($labelLines.Count -ne 1 -or $labelLines[0] -cne $expectedLine) {
            throw "$Context does not contain exactly one expected configuration line '$expectedLine'."
        }
    }
}

function Remove-M0NinjaComment {
    param([Parameter(Mandatory)][AllowEmptyString()][string] $Text)
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

function Split-M0NinjaTokens {
    param([Parameter(Mandatory)][AllowEmptyString()][string] $Text)
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

function ConvertFrom-M0NinjaToken {
    param([Parameter(Mandatory)][string] $Token)
    return $Token.Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$').Replace('\', '/')
}

function Test-M0NinjaDependencyEdge {
    param(
        [Parameter(Mandatory)][string] $Text,
        [Parameter(Mandatory)][string] $Target,
        [Parameter(Mandatory)][string] $Rule,
        [Parameter(Mandatory)][string] $Dependency
    )
    $normalized = $Text.Replace("`r`n", "`n")
    if ($normalized.Contains("`r")) { return $false }
    $logicalText = [Text.RegularExpressions.Regex]::Replace($normalized, '\$\n[ \t]*', '')
    $lines = @($logicalText.Split("`n", [StringSplitOptions]::RemoveEmptyEntries))
    for ($lineIndex = 0; $lineIndex -lt $lines.Count; $lineIndex++) {
        $line = Remove-M0NinjaComment $lines[$lineIndex]
        if (-not $line.StartsWith('build ', [StringComparison]::Ordinal)) { continue }
        $separator = -1
        for ($index = 6; $index -lt $line.Length; $index++) {
            if ($line[$index] -eq ':' -and ($index -eq 0 -or $line[$index - 1] -ne '$')) {
                $separator = $index
                break
            }
        }
        if ($separator -lt 0) { continue }
        $outputs = @(Split-M0NinjaTokens $line.Substring(6, $separator - 6) | ForEach-Object { ConvertFrom-M0NinjaToken $_ })
        $edgeTokens = @(Split-M0NinjaTokens $line.Substring($separator + 1))
        if ($edgeTokens.Count -lt 1) { continue }
        $actualRule = ConvertFrom-M0NinjaToken $edgeTokens[0]
        $dependencies = @($edgeTokens | Select-Object -Skip 1 | ForEach-Object { ConvertFrom-M0NinjaToken $_ })
        $hasTarget = @($outputs | Where-Object { $_ -ceq $Target }).Count -gt 0
        if (-not $hasTarget) { continue }
        if ($actualRule -cne $Rule) { continue }
        $hasDependency = @($dependencies | Where-Object { $_ -ceq $Dependency }).Count -gt 0
        if (-not $hasDependency) { continue }

        for ($bindingIndex = $lineIndex + 1; $bindingIndex -lt $lines.Count; $bindingIndex++) {
            $bindingLine = $lines[$bindingIndex]
            if ($bindingLine.StartsWith('build ', [StringComparison]::Ordinal)) { break }
            if (-not $bindingLine.StartsWith(' ', [StringComparison]::Ordinal) -and
                -not $bindingLine.StartsWith("`t", [StringComparison]::Ordinal)) { break }
            $trimmed = (Remove-M0NinjaComment $bindingLine).TrimStart()
            if (-not $trimmed.StartsWith('LINK_LIBRARIES =', [StringComparison]::Ordinal)) { continue }
            $linkTokens = @(Split-M0NinjaTokens $trimmed.Substring('LINK_LIBRARIES ='.Length) |
                    ForEach-Object { ConvertFrom-M0NinjaToken $_ })
            return @($linkTokens | Where-Object { $_ -ceq $Dependency }).Count -gt 0
        }
    }
    return $false
}

function Test-M0DumpbinSubjectLine {
    param(
        [Parameter(Mandatory)][string] $Text,
        [Parameter(Mandatory)][string] $SubjectPath
    )
    $leaf = [Text.RegularExpressions.Regex]::Escape([IO.Path]::GetFileName($SubjectPath))
    return [Text.RegularExpressions.Regex]::IsMatch(
        $Text,
        "^[ \t]*Dump of file[ \t]+(?:.*[\\/])?$leaf[ \t]*$",
        [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
            [Text.RegularExpressions.RegexOptions]::Multiline -bor
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
}

function Test-M0DumpbinModuleLine {
    param(
        [Parameter(Mandatory)][string] $Text,
        [Parameter(Mandatory)][string] $ModuleName
    )
    $module = [Text.RegularExpressions.Regex]::Escape($ModuleName)
    return [Text.RegularExpressions.Regex]::IsMatch(
        $Text,
        "^[ \t]*$module[ \t]*$",
        [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
            [Text.RegularExpressions.RegexOptions]::Multiline -bor
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
}

function Test-M0DumpbinQColorExportRow {
    param([Parameter(Mandatory)][string] $Text)
    return [Text.RegularExpressions.Regex]::IsMatch(
        $Text,
        '^\s*[0-9]+[ \t]+[0-9A-F]+[ \t]+[0-9A-F]{4,}[ \t]+\?\?0QColor@@QEAA@HHHH@Z[ \t]*$',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
            [Text.RegularExpressions.RegexOptions]::Multiline -bor
            [Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
}

function Assert-M0V2SemanticReport {
    param(
        [Parameter(Mandatory)] $Spec,
        [Parameter(Mandatory)][string] $ReportPath,
        [Parameter(Mandatory)][string] $SubjectPath,
        [Parameter(Mandatory)][string] $Context
    )
    $text = if ($Spec.id -in @('configure-summary', 'cmake-cache')) {
        Read-M0VerbatimUtf8Text $ReportPath $Context
    }
    else {
        Read-M0StrictLfText $ReportPath $Context
    }
    switch ($Spec.id) {
        'configure-summary' {
            if ((Get-M0Sha256 $ReportPath) -cne (Get-M0Sha256 $SubjectPath) -or
                [long](Get-Item -LiteralPath $ReportPath).Length -ne [long](Get-Item -LiteralPath $SubjectPath).Length -or
                -not $text.Contains('Qt configuration summary', [StringComparison]::Ordinal)) {
                throw "$Context semantic content is invalid: configure summary is not the bound source copy."
            }
            Assert-M0ExpectedConfigSummaryLines $text $Context
        }
        'cmake-cache' {
            if ((Get-M0Sha256 $ReportPath) -cne (Get-M0Sha256 $SubjectPath) -or
                [long](Get-Item -LiteralPath $ReportPath).Length -ne [long](Get-Item -LiteralPath $SubjectPath).Length) {
                throw "$Context semantic content is invalid: CMake cache is not the bound source copy."
            }
            $cache = Get-M0CMakeCacheEntries $text $Context
            foreach ($key in $script:ExpectedCMakeCacheEntries.Keys) {
                if (-not $cache.ContainsKey($key)) {
                    throw "$Context semantic content is invalid: CMake cache key '$key' is absent."
                }
                $actual = $cache[$key]
                $expected = $script:ExpectedCMakeCacheEntries[$key]
                if ($actual.type -cne $expected.type -or $actual.value -cne $expected.value) {
                    throw "$Context semantic content is invalid: CMake cache key '$key' drifted."
                }
            }
        }
        'qtgui-freetype-dependency' {
            $expected = "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`nplugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n"
            $subjectText = Read-M0VerbatimUtf8Text $SubjectPath "$Context subject"
            if ($text -cne $expected -or
                -not (Test-M0NinjaDependencyEdge $subjectText 'bin/Qt6Gui.dll' 'CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo' 'lib/Qt6BundledFreetype.lib') -or
                -not (Test-M0NinjaDependencyEdge $subjectText 'plugins/platforms/qminimal.dll' 'CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo' 'lib/Qt6BundledFreetype.lib')) {
                throw "$Context semantic content is invalid: bundled FreeType link edges are not proven."
            }
        }
        'qt6gui-headers' {
            Assert-M0PeX64 $SubjectPath "$Context subject"
            if (-not (Test-M0DumpbinSubjectLine $text $SubjectPath)) {
                throw "$Context semantic content is invalid: dumpbin does not identify the bound Qt6Gui subject."
            }
            foreach ($marker in @('Dump of file', 'PE signature found', 'File Type: DLL', 'FILE HEADER VALUES', '8664 machine (x64)', '20B magic # (PE32+)')) {
                if (-not $text.Contains($marker, [StringComparison]::Ordinal)) {
                    throw "$Context semantic content is invalid: dumpbin headers marker '$marker' is absent."
                }
            }
        }
        'qt6gui-imports' {
            Assert-M0PeX64 $SubjectPath "$Context subject" -RequiredImports @('Qt6Core.dll', 'DWrite.dll')
            if (-not (Test-M0DumpbinSubjectLine $text $SubjectPath) -or
                -not (Test-M0DumpbinModuleLine $text 'Qt6Core.dll') -or
                -not (Test-M0DumpbinModuleLine $text 'DWrite.dll')) {
                throw "$Context semantic content is invalid: dumpbin does not bind the Qt6Gui subject and required Qt6Core/DWrite imports."
            }
            foreach ($marker in @('Dump of file', 'File Type: DLL', 'Section contains the following imports:')) {
                if (-not $text.Contains($marker, [StringComparison]::Ordinal)) {
                    throw "$Context semantic content is invalid: dumpbin imports marker '$marker' is absent."
                }
            }
        }
        'qt6gui-exports' {
            Assert-M0PeX64 $SubjectPath "$Context subject" -RequiredExports @('??0QColor@@QEAA@HHHH@Z')
            if (-not (Test-M0DumpbinSubjectLine $text $SubjectPath) -or
                -not (Test-M0DumpbinQColorExportRow $text)) {
                throw "$Context semantic content is invalid: dumpbin does not bind the Qt6Gui subject and a real QColor export row."
            }
            foreach ($marker in @('Dump of file', 'File Type: DLL', 'Section contains the following exports for', 'ordinal hint RVA')) {
                if (-not $text.Contains($marker, [StringComparison]::Ordinal)) {
                    throw "$Context semantic content is invalid: dumpbin exports marker '$marker' is absent."
                }
            }
        }
        'qminimal-imports' {
            Assert-M0PeX64 $SubjectPath "$Context subject" -RequiredImports @('Qt6Gui.dll', 'Qt6Core.dll')
            if (-not (Test-M0DumpbinSubjectLine $text $SubjectPath) -or
                -not (Test-M0DumpbinModuleLine $text 'Qt6Gui.dll') -or
                -not (Test-M0DumpbinModuleLine $text 'Qt6Core.dll')) {
                throw "$Context semantic content is invalid: dumpbin does not bind qminimal and its required Qt6Gui/Qt6Core imports."
            }
            foreach ($marker in @('Dump of file', 'File Type: DLL', 'Section contains the following imports:')) {
                if (-not $text.Contains($marker, [StringComparison]::Ordinal)) {
                    throw "$Context semantic content is invalid: qminimal dumpbin imports marker '$marker' is absent."
                }
            }
        }
        default { throw "$Context semantic content is invalid: unknown report id." }
    }
}

function Assert-M0EvidenceObservation {
    param(
        [Parameter(Mandatory)] $Evidence,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)][string] $RunRoot,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        $LeaseSink,
        [string] $Context = 'observation.evidence'
    )
    $isV2 = $Contract.schema_version -eq 2
    $evidenceProperties = if ($isV2) {
        @('trees', 'commands', 'reports', 'artifacts', 'direct_consumers')
    }
    else {
        @('recipe', 'trees', 'commands', 'reports', 'artifacts', 'direct_consumers')
    }
    Assert-M0ExactProperties $Evidence $evidenceProperties $Context
    foreach ($collectionName in @('trees', 'commands', 'reports', 'artifacts', 'direct_consumers')) {
        if ($Evidence.PSObject.Properties[$collectionName].Value -isnot [Array]) {
            throw "$Context.$collectionName must be a JSON array."
        }
    }

    if (-not $isV2) {
        Assert-M0ExactProperties $Evidence.recipe @('files') "$Context.recipe"
        Assert-M0ReferenceSequence $Evidence.recipe.files $script:ExpectedRecipeFiles "$Context.recipe.files"
        for ($index = 0; $index -lt $script:ExpectedRecipeFiles.Count; $index++) {
            $null = Assert-M0PhysicalReference `
                -Reference @($Evidence.recipe.files)[$index] `
                -Root $RepositoryRoot `
                -BoundaryRoot $RepositoryRoot `
                -Context "$Context.recipe.files[$index]" `
                -LeaseSink $LeaseSink
        }
    }

    $trees = @($Evidence.trees)
    if ($trees.Count -ne $script:ExpectedTreeIds.Count) { throw "$Context.trees must contain the exact tree set." }
    $treeSnapshots = [ordered]@{}
    for ($index = 0; $index -lt $trees.Count; $index++) {
        $tree = $trees[$index]
        $treeProperties = if ($isV2) { @('id', 'root_relative_path', 'file_count', 'total_size_bytes', 'inventory') } else { @('id', 'file_count', 'total_size_bytes', 'inventory') }
        Assert-M0ExactProperties $tree $treeProperties "$Context.trees[$index]"
        Assert-M0String $tree.id "$Context.trees[$index].id"
        if ($tree.id -cne $script:ExpectedTreeIds[$index]) { throw "$Context.trees id sequence drifted." }
        Assert-M0PositiveSize $tree.file_count "$Context.trees[$index].file_count"
        Assert-M0PositiveSize $tree.total_size_bytes "$Context.trees[$index].total_size_bytes"
        $expectedInventoryPath = if ($isV2) { $Contract.trees[$index].inventory_relative_path } else { "evidence/inventories/$($tree.id).tsv" }
        if ($tree.inventory.relative_path -cne $expectedInventoryPath) { throw "$Context.trees[$index] inventory path drifted." }
        $inventoryPath = Assert-M0PhysicalReference $tree.inventory $RunRoot $RunRoot "$Context.trees[$index].inventory" $LeaseSink
        Assert-M0CanonicalInventory $inventoryPath ([long]$tree.file_count) ([long]$tree.total_size_bytes) "$Context.trees[$index]"
        if ($isV2) {
            if ($tree.root_relative_path -cne $Contract.trees[$index].root_relative_path) {
                throw "$Context.trees[$index] physical root path drifted from the contract."
            }
            $treeRoot = Resolve-M0ContainedPath $RunRoot $tree.root_relative_path "$Context.trees[$index] physical root"
            $snapshot = Assert-M0PhysicalTreeInventory `
                -TreeRoot $treeRoot `
                -InventoryPath $inventoryPath `
                -ExpectedFileCount ([long]$tree.file_count) `
                -ExpectedTotalSizeBytes ([long]$tree.total_size_bytes) `
                -ExpectedInventorySha256 $tree.inventory.sha256 `
                -Context "$Context.trees[$index]" `
                -LeaseSink $LeaseSink
            $treeSnapshots[$tree.id] = $snapshot
            $pinned = $Contract.trees[$index].expected
            if ($null -ne $pinned -and
                ([long]$tree.file_count -ne [long]$pinned.file_count -or
                 [long]$tree.total_size_bytes -ne [long]$pinned.total_size_bytes -or
                 $tree.inventory.sha256 -cne $pinned.inventory_sha256)) {
                throw "$Context.trees[$index] fingerprint drifted from the contract."
            }
        }
    }

    if ($isV2) {
        $beforeRows = @($treeSnapshots['qtbase-before'].rows)
        $patchedRows = @($treeSnapshots['qtbase-patched'].rows)
        if ($beforeRows.Count -ne $patchedRows.Count) { throw "$Context Qtbase original and patched trees differ in file set." }
        $changedPaths = [Collections.Generic.List[string]]::new()
        for ($index = 0; $index -lt $beforeRows.Count; $index++) {
            if ($beforeRows[$index].relative_path -cne $patchedRows[$index].relative_path) {
                throw "$Context Qtbase original and patched trees differ in path set."
            }
            if ($beforeRows[$index].size_bytes -ne $patchedRows[$index].size_bytes -or
                $beforeRows[$index].sha256 -cne $patchedRows[$index].sha256) {
                $changedPaths.Add($beforeRows[$index].relative_path)
            }
        }
        $target = $Contract.inputs.freetype_patch.target_relative_path
        if ($changedPaths.Count -ne 1 -or $changedPaths[0] -cne $target) {
            throw "$Context Qtbase patch must change exactly the contract-pinned target."
        }
        $beforeTarget = @($beforeRows | Where-Object relative_path -CEQ $target)
        $patchedTarget = @($patchedRows | Where-Object relative_path -CEQ $target)
        if ($beforeTarget.Count -ne 1 -or $patchedTarget.Count -ne 1 -or
            $beforeTarget[0].sha256 -cne $Contract.inputs.freetype_patch.target_before_sha256 -or
            $patchedTarget[0].sha256 -cne $Contract.inputs.freetype_patch.target_after_sha256) {
            throw "$Context Qtbase patch target bytes drifted from the contract."
        }
        $vulkanRows = @($treeSnapshots['vulkan'].rows)
        foreach ($vulkanPin in @(
                @($Contract.inputs.vulkan_headers.header_relative_path, $Contract.inputs.vulkan_headers.header_sha256),
                @($Contract.inputs.vulkan_headers.license_relative_path, $Contract.inputs.vulkan_headers.license_sha256)
            )) {
            $row = @($vulkanRows | Where-Object relative_path -CEQ $vulkanPin[0])
            if ($row.Count -ne 1 -or $row[0].sha256 -cne $vulkanPin[1]) {
                throw "$Context Vulkan physical tree does not contain the contract-pinned leaf."
            }
        }
    }

    $commands = @($Evidence.commands)
    if ($commands.Count -ne $script:ExpectedCommandIds.Count) { throw "$Context.commands must contain the exact command set." }
    for ($index = 0; $index -lt $commands.Count; $index++) {
        $command = $commands[$index]
        $commandProperties = if ($isV2) {
            @('id', 'tool_id', 'script_relative_path', 'arguments', 'working_directory_relative_path', 'exit_code', 'stdout', 'stderr')
        }
        else {
            @('id', 'exit_code', 'stdout', 'stderr')
        }
        Assert-M0ExactProperties $command $commandProperties "$Context.commands[$index]"
        Assert-M0String $command.id "$Context.commands[$index].id"
        if ($command.id -cne $script:ExpectedCommandIds[$index]) { throw "$Context.commands id sequence drifted." }
        if ($isV2) {
            $pinnedCommand = $Contract.commands[$index]
            Assert-M0String $command.tool_id "$Context.commands[$index].tool_id"
            if ($null -ne $command.script_relative_path) {
                Assert-M0String $command.script_relative_path "$Context.commands[$index].script_relative_path"
            }
            Assert-M0String $command.working_directory_relative_path "$Context.commands[$index].working_directory_relative_path"
            if ($command.tool_id -cne $pinnedCommand.tool_id -or
                $command.working_directory_relative_path -cne $pinnedCommand.working_directory_relative_path -or
                -not [object]::Equals($command.script_relative_path, $pinnedCommand.script_relative_path)) {
                throw "$Context.commands[$index] tool, script or working directory drifted from the contract."
            }
            $pinnedArguments = switch ($pinnedCommand.arguments_binding) {
                'configuration.arguments' { @($Contract.configuration.arguments); break }
                'configuration.build_arguments' { @($Contract.configuration.build_arguments); break }
                'configuration.install_arguments' { @($Contract.configuration.install_arguments); break }
                default { throw "$Context.commands[$index] has an unknown contract arguments binding." }
            }
            Assert-M0StringSequence $command.arguments $pinnedArguments "$Context.commands[$index] arguments"
        }
        if ($command.exit_code -isnot [long] -or $command.exit_code -ne 0) {
            throw "$Context.commands[$index] must record integer exit code zero."
        }
        foreach ($streamName in @('stdout', 'stderr')) {
            $reference = $command.PSObject.Properties[$streamName].Value
            $expectedLogPath = if ($isV2) { $Contract.commands[$index].PSObject.Properties["${streamName}_relative_path"].Value } else { "evidence/logs/$($command.id).$streamName.log" }
            if ($reference.relative_path -cne $expectedLogPath) { throw "$Context.commands[$index].$streamName path drifted." }
            $null = Assert-M0PhysicalReference $reference $RunRoot $RunRoot "$Context.commands[$index].$streamName" $LeaseSink
        }
    }

    $reports = @($Evidence.reports)
    if ($reports.Count -ne $script:ExpectedReportIds.Count) { throw "$Context.reports must contain the exact report set." }
    for ($index = 0; $index -lt $reports.Count; $index++) {
        $report = $reports[$index]
        $reportProperties = if ($isV2) { @('id', 'producer_id', 'arguments', 'subject', 'exit_code', 'file') } else { @('id', 'file') }
        Assert-M0ExactProperties $report $reportProperties "$Context.reports[$index]"
        Assert-M0String $report.id "$Context.reports[$index].id"
        if ($report.id -cne $script:ExpectedReportIds[$index]) { throw "$Context.reports id sequence drifted." }
        $expectedReportPath = if ($isV2) { $Contract.reports[$index].relative_path } else { "evidence/reports/$($report.id).txt" }
        if ($report.file.relative_path -cne $expectedReportPath) { throw "$Context.reports[$index] path drifted." }
        $reportPath = Assert-M0PhysicalReference $report.file $RunRoot $RunRoot "$Context.reports[$index].file" $LeaseSink
        if ($isV2) {
            $pinnedReport = $Contract.reports[$index]
            Assert-M0String $report.producer_id "$Context.reports[$index].producer_id"
            if ($report.producer_id -cne $pinnedReport.producer_id) { throw "$Context.reports[$index] producer drifted from the contract." }
            Assert-M0StringSequence $report.arguments $pinnedReport.arguments "$Context.reports[$index] arguments"
            if ($report.subject.relative_path -cne $pinnedReport.subject_relative_path) { throw "$Context.reports[$index] subject path drifted." }
            if ($report.exit_code -isnot [long] -or $report.exit_code -ne 0) { throw "$Context.reports[$index] producer exit code must be integer zero." }
            $subjectPath = Assert-M0PhysicalReference $report.subject $RunRoot $RunRoot "$Context.reports[$index].subject" $LeaseSink
            Assert-M0V2SemanticReport $pinnedReport $reportPath $subjectPath "$Context.reports[$index]"
        }
    }

    $expectedArtifactPaths = @($script:ExpectedArtifacts | ForEach-Object { "install/$_" })
    Assert-M0ReferenceSequence $Evidence.artifacts $expectedArtifactPaths "$Context.artifacts"
    for ($index = 0; $index -lt $expectedArtifactPaths.Count; $index++) {
        if ([long]@($Evidence.artifacts)[$index].size_bytes -le 0) { throw "$Context.artifacts[$index] must not be empty." }
        $artifactPath = Assert-M0PhysicalReference @($Evidence.artifacts)[$index] $RunRoot $RunRoot "$Context.artifacts[$index]" $LeaseSink
        Assert-M0PeX64 $artifactPath "$Context.artifacts[$index]"
    }

    Assert-M0ReferenceSequence $Evidence.direct_consumers $script:ExpectedDirectConsumers "$Context.direct_consumers"
    if ([long]@($Evidence.direct_consumers)[0].size_bytes -le 0) { throw "$Context.direct_consumers[0] must not be empty." }
    $consumerPath = Assert-M0PhysicalReference @($Evidence.direct_consumers)[0] $RunRoot $RunRoot "$Context.direct_consumers[0]" $LeaseSink
    Assert-M0PeX64 $consumerPath "$Context.direct_consumers[0]"

    if ($isV2) {
        $installRows = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
        foreach ($row in @($treeSnapshots['install'].rows)) { $installRows.Add($row.relative_path, $row) }
        foreach ($reference in @($Evidence.artifacts) + @($Evidence.direct_consumers)) {
            $installRelativePath = $reference.relative_path.Substring('install/'.Length)
            if (-not $installRows.ContainsKey($installRelativePath)) {
                throw "$Context artifact is absent from the recomputed install inventory: $installRelativePath"
            }
            $row = $installRows[$installRelativePath]
            if ([long]$row.size_bytes -ne [long]$reference.size_bytes -or $row.sha256 -cne $reference.sha256) {
                throw "$Context artifact differs from the recomputed install inventory: $installRelativePath"
            }
        }
    }
}

function Assert-M0EvidenceManifest {
    param(
        [Parameter(Mandatory)] $Manifest,
        [Parameter(Mandatory)] $Contract,
        [Parameter(Mandatory)][string] $ExpectedContractSha256,
        [Parameter(Mandatory)][string] $RunRoot,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        $LeaseSink
    )
    Assert-M0ExactProperties $Manifest @(
        'schema_version', 'manifest_id', 'contract_sha256', 'refresh_id', 'run_id', 'captured_utc',
        'recipe', 'trees', 'commands', 'reports', 'artifacts', 'direct_consumers', 'limits'
    ) 'evidence manifest'
    Assert-M0String $Manifest.manifest_id 'Evidence manifest id'
    Assert-M0Digest $Manifest.contract_sha256 'Evidence manifest contract SHA-256'
    Assert-M0String $Manifest.refresh_id 'Evidence manifest refresh id'
    Assert-M0String $Manifest.run_id 'Evidence manifest run id'
    Assert-M0String $Manifest.captured_utc 'Evidence manifest captured_utc'
    if ($Manifest.schema_version -isnot [long] -or $Manifest.schema_version -ne $Contract.schema_version -or
        $Manifest.manifest_id -cne "$($Contract.refresh_id)-evidence" -or
        $Manifest.contract_sha256 -cne $ExpectedContractSha256 -or
        $Manifest.refresh_id -cne $Contract.refresh_id -or
        $Manifest.run_id -cne [IO.Path]::GetFileName($RunRoot)) {
        throw 'Evidence manifest identity or contract binding drifted.'
    }
    Assert-M0CanonicalUtcTimestamp $Manifest.captured_utc 'Evidence manifest captured_utc'
    if ($Contract.schema_version -eq 2) {
        Assert-M0V2RecipeContract $Manifest.recipe
        if ($Manifest.recipe.runner_export -cne $Contract.recipe.runner_export) {
            throw 'Evidence manifest recipe runner export drifted from the contract.'
        }
        for ($index = 0; $index -lt $script:ExpectedRecipeIds.Count; $index++) {
            $actual = $Manifest.recipe.files[$index]
            $pinned = $Contract.recipe.files[$index]
            if ($actual.id -cne $pinned.id -or $actual.relative_path -cne $pinned.relative_path -or
                [long]$actual.size_bytes -ne [long]$pinned.size_bytes -or $actual.sha256 -cne $pinned.sha256) {
                throw "Evidence manifest recipe file '$($pinned.id)' drifted from the contract."
            }
            $reference = [pscustomobject][ordered]@{
                relative_path = $actual.relative_path
                size_bytes = [long]$actual.size_bytes
                sha256 = $actual.sha256
            }
            $null = Assert-M0PhysicalReference `
                -Reference $reference `
                -Root $RepositoryRoot `
                -BoundaryRoot $RepositoryRoot `
                -Context "Evidence manifest recipe file '$($actual.id)'" `
                -LeaseSink $LeaseSink
        }
        $body = [pscustomobject][ordered]@{
            trees = $Manifest.trees
            commands = $Manifest.commands
            reports = $Manifest.reports
            artifacts = $Manifest.artifacts
            direct_consumers = $Manifest.direct_consumers
        }
    }
    else {
        $body = [pscustomobject][ordered]@{
            recipe = $Manifest.recipe
            trees = $Manifest.trees
            commands = $Manifest.commands
            reports = $Manifest.reports
            artifacts = $Manifest.artifacts
            direct_consumers = $Manifest.direct_consumers
        }
    }
    Assert-M0EvidenceObservation $body $Contract $RunRoot $RepositoryRoot $LeaseSink 'evidence manifest'
    Assert-M0StringSequence $Manifest.limits $script:ReceiptLimits 'Evidence manifest limits'
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
    Assert-M0String $Configuration.generator "$Context generator"
    if ($Configuration.generator -cne 'Ninja Multi-Config') { throw "$Context generator must be Ninja Multi-Config." }
    Assert-M0StringSequence $Configuration.arguments $script:ExpectedConfigureArguments "$Context arguments"
    Assert-M0StringSequence $Configuration.build_arguments $script:ExpectedBuildArguments "$Context build arguments"
    Assert-M0StringSequence $Configuration.install_arguments $script:ExpectedInstallArguments "$Context install arguments"
    Assert-M0String $Configuration.installed_configuration "$Context installed configuration"
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

function Assert-M0V2RecipeContract {
    param([Parameter(Mandatory)] $Recipe)
    Assert-M0ExactProperties $Recipe @('files', 'runner_export') 'contract.recipe'
    Assert-M0String $Recipe.runner_export 'Contract recipe runner export'
    if ($Recipe.runner_export -cne 'Invoke-M0QtBaseRefreshNative') {
        throw 'Contract recipe runner export is not accepted.'
    }
    if ($Recipe.files -isnot [Array]) { throw 'Contract recipe files must be a JSON array.' }
    $files = @($Recipe.files)
    if ($files.Count -ne $script:ExpectedRecipeIds.Count) {
        throw 'Contract recipe must pin the exact production file set.'
    }
    for ($index = 0; $index -lt $files.Count; $index++) {
        $file = $files[$index]
        Assert-M0ExactProperties $file @('id', 'relative_path', 'size_bytes', 'sha256') "contract.recipe.files[$index]"
        Assert-M0String $file.id "Contract recipe id at index $index"
        if ($file.id -cne $script:ExpectedRecipeIds[$index]) {
            throw "Contract recipe id sequence drifted at index $index."
        }
        if ($file.relative_path -cne $script:ExpectedRecipeFiles[$index]) {
            throw "Contract recipe path sequence drifted at index $index."
        }
        Assert-M0SafeRelativePath $file.relative_path "Contract recipe file '$($file.id)' path"
        Assert-M0PositiveSize $file.size_bytes "Contract recipe file '$($file.id)' size"
        Assert-M0Digest $file.sha256 "Contract recipe file '$($file.id)' SHA-256"
    }
}

function Assert-M0V2TreeContract {
    param([Parameter(Mandatory)] $Trees)
    if ($Trees -isnot [Array]) { throw 'Contract trees must be a JSON array.' }
    $items = @($Trees)
    if ($items.Count -ne $script:ExpectedTreeIds.Count) {
        throw 'Contract tree layout must contain the exact source and install tree set.'
    }
    for ($index = 0; $index -lt $items.Count; $index++) {
        $tree = $items[$index]
        Assert-M0ExactProperties $tree @('id', 'root_relative_path', 'inventory_relative_path', 'expected') "contract.trees[$index]"
        Assert-M0String $tree.id "Contract tree id at index $index"
        if ($tree.id -cne $script:ExpectedTreeIds[$index]) { throw "Contract tree id sequence drifted at index $index." }
        if ($tree.root_relative_path -cne $script:ExpectedTreeRoots[$index]) { throw "Contract tree root path drifted at index $index." }
        if ($tree.inventory_relative_path -cne $script:ExpectedInventoryPaths[$index]) { throw "Contract tree inventory path drifted at index $index." }
        Assert-M0CanonicalEvidencePath $tree.root_relative_path "Contract tree '$($tree.id)' root"
        Assert-M0CanonicalEvidencePath $tree.inventory_relative_path "Contract tree '$($tree.id)' inventory"
        if ($tree.id -ceq 'install') {
            if ($null -ne $tree.expected) { throw 'Contract install tree fingerprint must remain derived rather than predeclared.' }
            continue
        }
        Assert-M0ExactProperties $tree.expected @('file_count', 'total_size_bytes', 'inventory_sha256') "contract.trees[$index].expected"
        Assert-M0PositiveSize $tree.expected.file_count "Contract tree '$($tree.id)' expected file count"
        Assert-M0PositiveSize $tree.expected.total_size_bytes "Contract tree '$($tree.id)' expected total size"
        Assert-M0Digest $tree.expected.inventory_sha256 "Contract tree '$($tree.id)' expected inventory SHA-256"
    }
    if ($items[0].expected.inventory_sha256 -ceq $items[1].expected.inventory_sha256) {
        throw 'Contract original and patched Qtbase tree fingerprints must differ.'
    }
}

function Assert-M0V2CommandContract {
    param([Parameter(Mandatory)] $Commands)
    if ($Commands -isnot [Array]) { throw 'Contract commands must be a JSON array.' }
    $items = @($Commands)
    if ($items.Count -ne $script:ExpectedCommandSpecs.Count) {
        throw 'Contract commands must contain the exact configure, build and install set.'
    }
    for ($index = 0; $index -lt $items.Count; $index++) {
        $command = $items[$index]
        $expected = $script:ExpectedCommandSpecs[$index]
        Assert-M0ExactProperties $command @(
            'id', 'tool_id', 'script_relative_path', 'arguments_binding', 'working_directory_relative_path',
            'stdout_relative_path', 'stderr_relative_path'
        ) "contract.commands[$index]"
        foreach ($property in @('id', 'tool_id', 'arguments_binding', 'working_directory_relative_path', 'stdout_relative_path', 'stderr_relative_path')) {
            Assert-M0String $command.PSObject.Properties[$property].Value "Contract command '$($expected.id)' $property"
        }
        foreach ($property in @(
                'id', 'tool_id', 'script_relative_path', 'arguments_binding', 'working_directory_relative_path',
                'stdout_relative_path', 'stderr_relative_path'
            )) {
            $actual = $command.PSObject.Properties[$property].Value
            $pinned = $expected.PSObject.Properties[$property].Value
            if ($null -eq $pinned) {
                if ($null -ne $actual) { throw "Contract command '$($expected.id)' $property must be null." }
            }
            elseif ($actual -cne $pinned) {
                throw "Contract command '$($expected.id)' $property drifted."
            }
        }
        if ($null -ne $command.script_relative_path) {
            Assert-M0CanonicalEvidencePath $command.script_relative_path "Contract command '$($command.id)' script"
        }
        Assert-M0CanonicalEvidencePath $command.working_directory_relative_path "Contract command '$($command.id)' working directory"
        Assert-M0CanonicalEvidencePath $command.stdout_relative_path "Contract command '$($command.id)' stdout log"
        Assert-M0CanonicalEvidencePath $command.stderr_relative_path "Contract command '$($command.id)' stderr log"
    }
}

function Assert-M0V2ReportContract {
    param([Parameter(Mandatory)] $Reports)
    if ($Reports -isnot [Array]) { throw 'Contract reports must be a JSON array.' }
    $items = @($Reports)
    if ($items.Count -ne $script:ExpectedReportIds.Count) {
        throw 'Contract reports must contain the exact semantic report set.'
    }
    for ($index = 0; $index -lt $items.Count; $index++) {
        $report = $items[$index]
        $expected = $script:ExpectedReportSpecs[$index]
        Assert-M0ExactProperties $report @('id', 'producer_id', 'arguments', 'subject_relative_path', 'relative_path') "contract.reports[$index]"
        foreach ($property in @('id', 'producer_id', 'subject_relative_path', 'relative_path')) {
            Assert-M0String $report.PSObject.Properties[$property].Value "Contract report '$($expected.id)' $property"
        }
        if ($report.id -cne $expected.id -or $report.producer_id -cne $expected.producer_id -or
            $report.subject_relative_path -cne $expected.subject_relative_path -or
            $report.relative_path -cne $expected.relative_path) {
            throw "Contract report '$($expected.id)' identity, producer, subject or path drifted."
        }
        Assert-M0StringSequence $report.arguments $expected.arguments "Contract report '$($report.id)' arguments"
        Assert-M0CanonicalEvidencePath $report.subject_relative_path "Contract report '$($report.id)' subject path"
        Assert-M0CanonicalEvidencePath $report.relative_path "Contract report '$($report.id)' path"
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
    $contractLease = Open-M0PhysicalFileLease `
        -LiteralPath $contractItem.FullName `
        -ExpectedSizeBytes ([long]$contractItem.Length) `
        -ExpectedSha256 $ExpectedSha256 `
        -Context 'QtBase refresh contract' `
        -RequireSingleLink
    try {
    $contract = Read-M0ClosedJson -LiteralPath $contractLease.path -Context 'QtBase refresh contract'
    if ($contract.schema_version -isnot [long]) { throw 'Contract schema_version must be an integer.' }
    if ($contract.schema_version -eq 1) {
        Assert-M0ExactProperties $contract @('schema_version', 'refresh_id', 'product', 'inputs', 'tools', 'environment', 'isolation', 'configuration') 'contract'
        Assert-M0String $contract.refresh_id 'Contract refresh_id'
        if ($contract.refresh_id -cne 'm0.2-qtbase-freetype-backport-v1') { throw 'Contract refresh_id is not accepted for schema v1.' }
    }
    elseif ($contract.schema_version -eq 2) {
        Assert-M0ExactProperties $contract @(
            'schema_version', 'refresh_id', 'product', 'recipe', 'inputs', 'tools', 'environment', 'isolation',
            'trees', 'commands', 'reports', 'configuration'
        ) 'contract'
        Assert-M0String $contract.refresh_id 'Contract refresh_id'
        if ($contract.refresh_id -cne 'm0.2-qtbase-freetype-backport-v2') { throw 'Contract refresh_id is not accepted for schema v2.' }
        Assert-M0V2RecipeContract $contract.recipe
        Assert-M0V2TreeContract $contract.trees
        Assert-M0V2CommandContract $contract.commands
        Assert-M0V2ReportContract $contract.reports
    }
    else {
        throw 'Contract schema_version must be integer 1 or 2.'
    }

    Assert-M0ExactProperties $contract.product @('qt_version', 'architecture', 'compiler_abi') 'contract.product'
    Assert-M0String $contract.product.qt_version 'Contract Qt version'
    Assert-M0String $contract.product.architecture 'Contract architecture'
    Assert-M0String $contract.product.compiler_abi 'Contract compiler ABI'
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

    if ($contract.tools -isnot [Array]) { throw 'Contract tools must be a JSON array.' }
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
    $expectedReceiptPath = if ($contract.schema_version -eq 2) {
        'evidence/qtbase-refresh-receipt.v2.json'
    }
    else {
        'evidence/qtbase-refresh-receipt.v1.json'
    }
    if ($contract.isolation.receipt_relative_path -cne $expectedReceiptPath) {
        throw 'Contract receipt path is not accepted.'
    }

    Assert-M0ConfigurationObject $contract.configuration 'contract.configuration'
    return $contract
    }
    finally {
        $contractLease.stream.Dispose()
    }
}

function Assert-M0PatchObservation {
    param([Parameter(Mandatory)] $Patch, [Parameter(Mandatory)] $ContractPatch, [string] $Context = 'observation.patch')
    Assert-M0ExactProperties $Patch @('changed_relative_paths', 'target_relative_path', 'target_before_sha256', 'target_after_sha256') $Context
    if ($Patch.changed_relative_paths -isnot [Array]) { throw "$Context changed paths must be a JSON array." }
    Assert-M0StringSequence $Patch.changed_relative_paths @([string]$ContractPatch.target_relative_path) "$Context changed paths"
    Assert-M0SafeRelativePath $Patch.target_relative_path "$Context target path"
    Assert-M0Digest $Patch.target_before_sha256 "$Context target before SHA-256"
    Assert-M0Digest $Patch.target_after_sha256 "$Context target after SHA-256"
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

function Invoke-M0QtBaseRefreshCore {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $ToolRoot,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $CallerPath
    )

    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    if ($contract.schema_version -ne 2) {
        throw 'Schema v1 is historical verification-only; new QtBase refresh executions require schema v2.'
    }
    $refreshId = [string]$contract.refresh_id
    $receiptRelativePath = [string]$contract.isolation.receipt_relative_path
    $repository = ConvertTo-M0NormalizedDirectoryPath $RepositoryRoot
    $toolRootFull = ConvertTo-M0NormalizedDirectoryPath $ToolRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'RepositoryRoot does not exist.' }
    if (-not [IO.Directory]::Exists($toolRootFull)) { throw 'ToolRoot does not exist.' }
    Assert-M0NoReparseChain -LiteralPath $repository -BoundaryRoot $repository -Context 'RepositoryRoot'
    Assert-M0NoReparseChain -LiteralPath $toolRootFull -BoundaryRoot $toolRootFull -Context 'ToolRoot'

    $leases = [Collections.Generic.List[object]]::new()
    $publicationLeases = [Collections.Generic.List[object]]::new()
    $publicationCommitted = $false
    $runnerModule = $null
    try {
    $contractItem = Get-Item -LiteralPath $ContractPath -Force
    $contractLease = Open-M0PhysicalFileLease `
        -LiteralPath $contractItem.FullName `
        -ExpectedSizeBytes ([long]$contractItem.Length) `
        -ExpectedSha256 $ExpectedContractSha256 `
        -Context 'QtBase refresh contract' `
        -BoundaryRoot $repository `
        -RequireSingleLink
    $leases.Add($contractLease)
    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    $refreshId = [string]$contract.refresh_id
    $receiptRelativePath = [string]$contract.isolation.receipt_relative_path

    $recipePaths = [ordered]@{}
    $recipeLeases = [ordered]@{}
    if ($contract.schema_version -eq 2) {
        foreach ($recipeFile in @($contract.recipe.files)) {
            $candidate = Resolve-M0ContainedPath $repository $recipeFile.relative_path "Recipe '$($recipeFile.id)'"
            $lease = Open-M0PhysicalFileLease `
                -LiteralPath $candidate `
                -ExpectedSizeBytes ([long]$recipeFile.size_bytes) `
                -ExpectedSha256 $recipeFile.sha256 `
                -Context "Recipe '$($recipeFile.id)'" `
                -BoundaryRoot $repository `
                -RequireSingleLink
            $leases.Add($lease)
            $recipePaths[$recipeFile.id] = $lease.path
            $recipeLeases[$recipeFile.id] = $lease
        }
    }
    $orchestratorSpec = @($contract.recipe.files | Where-Object id -CEQ 'orchestrator')
    if ($orchestratorSpec.Count -ne 1) { throw 'Contract recipe does not contain exactly one orchestrator pin.' }
    $loadedModuleItem = Get-Item -LiteralPath $script:LoadedModulePath -Force -ErrorAction Stop
    $loadedModuleLease = Open-M0PhysicalFileLease `
        -LiteralPath $loadedModuleItem.FullName `
        -ExpectedSizeBytes ([long]$orchestratorSpec[0].size_bytes) `
        -ExpectedSha256 $orchestratorSpec[0].sha256 `
        -Context 'Loaded QtBase refresh orchestrator bytes' `
        -BoundaryRoot $repository `
        -RequireSingleLink
    $leases.Add($loadedModuleLease)
    $pinnedOrchestratorLease = $recipeLeases['orchestrator']
    if ($null -eq $pinnedOrchestratorLease -or
        -not [string]::Equals($loadedModuleLease.path, $pinnedOrchestratorLease.path, [StringComparison]::OrdinalIgnoreCase) -or
        $loadedModuleLease.identity -cne $pinnedOrchestratorLease.identity) {
        throw 'Loaded QtBase refresh orchestrator is not the exact authenticated physical recipe leaf.'
    }
    $entrypointPath = $recipePaths['entrypoint']
    if (-not [string]::Equals([IO.Path]::GetFullPath($CallerPath), $entrypointPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'QtBase refresh production invocation did not originate from the authenticated entrypoint.'
    }

    $inputPaths = [ordered]@{}
    foreach ($binding in @(
            @('qtbase_source', $contract.inputs.qtbase_source),
            @('freetype_patch', $contract.inputs.freetype_patch),
            @('vulkan_headers', $contract.inputs.vulkan_headers)
        )) {
        $id = [string]$binding[0]
        $spec = $binding[1]
        $candidate = Resolve-M0ContainedPath $repository $spec.relative_path "Input '$id'"
        $lease = Open-M0PhysicalFileLease $candidate ([long]$spec.size_bytes) $spec.sha256 "Input '$id'" -BoundaryRoot $repository -RequireSingleLink
        $leases.Add($lease)
        $inputPaths[$id] = $lease.path
    }

    $toolPaths = [ordered]@{}
    foreach ($tool in @($contract.tools)) {
        $candidate = Resolve-M0ContainedPath $toolRootFull $tool.relative_path "Tool '$($tool.id)'"
        $lease = Open-M0PhysicalFileLease $candidate ([long]$tool.size_bytes) $tool.sha256 "Tool '$($tool.id)'" -BoundaryRoot $toolRootFull
        $leases.Add($lease)
        $toolPaths[$tool.id] = $lease.path
    }

    foreach ($name in @($contract.environment.forbidden_variables)) {
        $value = [Environment]::GetEnvironmentVariable($name, 'Process')
        if (-not [string]::IsNullOrEmpty($value)) { throw "Forbidden build environment injection '$name' is present." }
    }

    $runnerSpec = @($contract.recipe.files | Where-Object id -CEQ 'runner')
    if ($runnerSpec.Count -ne 1) { throw 'Contract recipe does not contain exactly one runner pin.' }
    $imported = @(Import-Module -Name $recipePaths['runner'] -Force -PassThru)
    if ($imported.Count -ne 1) { throw 'Pinned native runner import did not yield exactly one module.' }
    $runnerModule = $imported[0]
    $runnerExports = @($runnerModule.ExportedFunctions.Keys)
    if ($runnerExports.Count -ne 1 -or $runnerExports[0] -cne $contract.recipe.runner_export -or
        @($runnerModule.ExportedAliases.Keys).Count -ne 0 -or
        @($runnerModule.ExportedCmdlets.Keys).Count -ne 0 -or
        @($runnerModule.ExportedVariables.Keys).Count -ne 0) {
        throw 'Pinned native runner module export surface drifted from the contract.'
    }
    $runnerCommand = $runnerModule.ExportedFunctions[$contract.recipe.runner_export]
    if ($null -eq $runnerCommand) { throw 'Pinned native runner export is unavailable after import.' }

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

    New-M0ExclusiveDirectory -LiteralPath $runRootFull
    Assert-M0NoReparseChain -LiteralPath $runRootFull -BoundaryRoot $runParent -Context 'RunRoot'
    $contractCopy = $contract | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
    $context = [pscustomobject][ordered]@{
        contract_sha256 = $ExpectedContractSha256
        contract = $contractCopy
        run_root = $runRootFull
        paths = [pscustomobject]$inputPaths
        tool_paths = [pscustomobject]$toolPaths
        recipe_paths = [pscustomobject]$recipePaths
    }

    $observations = @(& $runnerCommand -Context $context)
    if ($observations.Count -ne 1 -or $null -eq $observations[0]) {
        throw 'Pinned QtBase refresh runner must return exactly one observation object.'
    }
    $observation = $observations[0]
    Assert-M0ExactProperties $observation @('execution', 'patch', 'configuration', 'evidence') 'refresh observation'
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

    Assert-M0EvidenceObservation `
        -Evidence $observation.evidence `
        -Contract $contract `
        -RunRoot $runRootFull `
        -RepositoryRoot $repository `
        -LeaseSink $leases

    $manifestRecipe = if ($contract.schema_version -eq 2) { $contract.recipe } else { $observation.evidence.recipe }
    $manifestRelativePath = if ($contract.schema_version -eq 2) { $script:EvidenceManifestV2RelativePath } else { $script:EvidenceManifestRelativePath }
    $manifest = [pscustomobject][ordered]@{
        schema_version = [long]$contract.schema_version
        manifest_id = "${refreshId}-evidence"
        contract_sha256 = $ExpectedContractSha256
        refresh_id = $refreshId
        run_id = [IO.Path]::GetFileName($runRootFull)
        captured_utc = [DateTimeOffset]::UtcNow.ToString('O')
        recipe = $manifestRecipe
        trees = $observation.evidence.trees
        commands = $observation.evidence.commands
        reports = $observation.evidence.reports
        artifacts = $observation.evidence.artifacts
        direct_consumers = $observation.evidence.direct_consumers
        limits = $script:ReceiptLimits
    }
    Assert-M0EvidenceManifest $manifest $contract $ExpectedContractSha256 $runRootFull $repository
    $manifestPath = Resolve-M0ContainedPath $runRootFull $manifestRelativePath 'Evidence manifest path'
    $manifestParent = [IO.Path]::GetDirectoryName($manifestPath)
    if (-not [IO.Directory]::Exists($manifestParent)) {
        [IO.Directory]::CreateDirectory($manifestParent) | Out-Null
    }
    Assert-M0NoReparseChain -LiteralPath $manifestParent -BoundaryRoot $runRootFull -Context 'Evidence manifest parent'
    $manifestText = ($manifest | ConvertTo-Json -Depth 100) + "`n"
    $manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes($manifestText)
    $manifestPublication = Publish-M0QtBaseRefreshReceipt `
        -LiteralPath $manifestPath `
        -Bytes $manifestBytes `
        -RunRoot $runRootFull `
        -RetainPublishedLease
    $leases.Add($manifestPublication.lease)
    $publicationLeases.Add($manifestPublication.lease)
    $manifestReference = [pscustomobject][ordered]@{
        relative_path = $manifestRelativePath
        size_bytes = [long]$manifestPublication.size_bytes
        sha256 = $manifestPublication.sha256
    }

    $receipt = [ordered]@{
        schema_version = [long]$contract.schema_version
        receipt_id = "${refreshId}-receipt"
        contract_sha256 = $ExpectedContractSha256
        refresh_id = $refreshId
        captured_utc = [DateTimeOffset]::UtcNow.ToString('O')
        execution = $observation.execution
        patch = $observation.patch
        configuration = $observation.configuration
        evidence = $manifestReference
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
    Assert-M0PhysicalFileLeasesUnchanged $leases 'Before final receipt publication'
    $publication = Publish-M0QtBaseRefreshReceipt `
        -LiteralPath $receiptPath `
        -Bytes $bytes `
        -RunRoot $runRootFull `
        -RetainPublishedLease
    $leases.Add($publication.lease)
    $publicationLeases.Add($publication.lease)
    Assert-M0PhysicalFileLeasesUnchanged $leases 'After final receipt publication'
    $publicationCommitted = $true
    return [pscustomobject][ordered]@{
        contract_sha256 = $ExpectedContractSha256
        evidence_manifest_path = $manifestPath
        evidence_manifest_sha256 = $manifestPublication.sha256
        receipt_path = $receiptPath
        receipt_sha256 = $publication.sha256
    }
    }
    finally {
        $publicationCleanupFailure = $null
        if (-not $publicationCommitted -and $publicationLeases.Count -gt 0) {
            try { Set-M0UncommittedPublicationsDeleteOnClose $publicationLeases }
            catch { $publicationCleanupFailure = $_ }
        }
        if ($null -ne $runnerModule) { try { Remove-Module -ModuleInfo $runnerModule -Force } catch { } }
        Close-M0PhysicalFileLeases $leases
        if ($null -ne $publicationCleanupFailure) {
            throw [IO.InvalidDataException]::new('Failed QtBase refresh publication could not be removed safely.', $publicationCleanupFailure.Exception)
        }
    }
}

function Invoke-M0QtBaseRefresh {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $ToolRoot,
        [Parameter(Mandatory)] [string] $RunRoot
    )

    return Invoke-M0QtBaseRefreshCore `
        -ContractPath $ContractPath `
        -ExpectedContractSha256 $ExpectedContractSha256 `
        -RepositoryRoot $RepositoryRoot `
        -ToolRoot $ToolRoot `
        -RunRoot $RunRoot `
        -CallerPath $MyInvocation.PSCommandPath
}

function Assert-M0QtBaseRefreshReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [string] $RepositoryRoot
    )

    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    Assert-M0Digest $ExpectedReceiptSha256 'Expected receipt SHA-256'
    if (-not [IO.File]::Exists($LiteralPath)) { throw 'QtBase refresh receipt is missing.' }
    $item = Get-Item -LiteralPath $LiteralPath -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'QtBase refresh receipt cannot be a reparse point.' }
    if ($contract.schema_version -eq 1) {
        $historicalLease = $null
        try {
            $historicalLease = Open-M0PhysicalFileLease `
                -LiteralPath $item.FullName `
                -ExpectedSizeBytes ([long]$item.Length) `
                -ExpectedSha256 $ExpectedReceiptSha256 `
                -Context 'Historical QtBase refresh receipt'
            $receipt = Read-M0ClosedJson -LiteralPath $historicalLease.path -Context 'Historical QtBase refresh receipt'
            Assert-M0ExactProperties $receipt @(
                'schema_version', 'receipt_id', 'contract_sha256', 'refresh_id', 'captured_utc',
                'execution', 'patch', 'configuration', 'limits'
            ) 'receipt'
            if ($receipt.schema_version -isnot [long] -or $receipt.schema_version -ne 1 -or
                $receipt.receipt_id -isnot [string] -or $receipt.receipt_id -cne "$($contract.refresh_id)-receipt" -or
                $receipt.contract_sha256 -isnot [string] -or $receipt.contract_sha256 -cne $ExpectedContractSha256 -or
                $receipt.refresh_id -isnot [string] -or $receipt.refresh_id -cne $contract.refresh_id) {
                throw 'Historical receipt identity or contract binding drifted.'
            }
            $captured = [DateTimeOffset]::MinValue
            if ($receipt.captured_utc -isnot [string] -or
                -not [DateTimeOffset]::TryParse($receipt.captured_utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind, [ref]$captured)) {
                throw 'Historical receipt captured_utc is invalid.'
            }
            Assert-M0ExecutionObservation $receipt.execution 'receipt.execution'
            if ([int]$receipt.execution.exit_code -ne 0) { throw 'Receipt cannot authenticate a failed execution.' }
            Assert-M0PatchObservation $receipt.patch $contract.inputs.freetype_patch 'receipt.patch'
            Assert-M0ConfigurationObject $receipt.configuration 'receipt.configuration' -Observation
            Assert-M0StringSequence $receipt.limits $script:ReceiptLimits 'Receipt limits'
            return $receipt
        }
        finally {
            if ($null -ne $historicalLease) { $historicalLease.stream.Dispose() }
        }
    }

    if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
        throw 'RepositoryRoot is mandatory when verifying a schema v2 receipt.'
    }
    $repository = ConvertTo-M0NormalizedDirectoryPath $RepositoryRoot
    if (-not [IO.Directory]::Exists($repository)) { throw 'RepositoryRoot does not exist.' }
    $leases = [Collections.Generic.List[object]]::new()
    try {
    $contractItem = Get-Item -LiteralPath $ContractPath -Force
    $contractLease = Open-M0PhysicalFileLease `
        -LiteralPath $contractItem.FullName `
        -ExpectedSizeBytes ([long]$contractItem.Length) `
        -ExpectedSha256 $ExpectedContractSha256 `
        -Context 'QtBase refresh contract' `
        -BoundaryRoot $repository `
        -RequireSingleLink
    $leases.Add($contractLease)
    $contract = Assert-M0QtBaseRefreshContract -LiteralPath $ContractPath -ExpectedSha256 $ExpectedContractSha256
    $runParent = Resolve-M0ContainedPath $repository $contract.isolation.run_parent_relative_path 'Receipt run parent'
    if (-not [IO.Directory]::Exists($runParent)) { throw 'Receipt run parent does not exist.' }
    Assert-M0NoReparseChain -LiteralPath $runParent -BoundaryRoot $repository -Context 'Receipt run parent'
    $runRoot = ConvertTo-M0NormalizedDirectoryPath ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($item.FullName)))
    if ([string]::IsNullOrWhiteSpace([IO.Path]::GetFileName($runRoot)) -or
        -not [string]::Equals([IO.Path]::GetDirectoryName($runRoot), $runParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Receipt RunRoot must be a direct child of the contract-pinned run parent.'
    }
    Assert-M0NoReparseChain -LiteralPath $runRoot -BoundaryRoot $runParent -Context 'Receipt RunRoot'
    $receiptLease = Open-M0PhysicalFileLease `
        -LiteralPath $item.FullName `
        -ExpectedSizeBytes ([long]$item.Length) `
        -ExpectedSha256 $ExpectedReceiptSha256 `
        -Context 'QtBase refresh receipt' `
        -BoundaryRoot $runRoot `
        -RequireSingleLink
    $leases.Add($receiptLease)
    $expectedReceiptPath = Resolve-M0ContainedPath $runRoot $contract.isolation.receipt_relative_path 'Receipt verification path'
    if (-not [string]::Equals($receiptLease.path, $expectedReceiptPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Receipt is not located at its canonical RunRoot path.'
    }
    Assert-M0NoReparseChain -LiteralPath $receiptLease.path -BoundaryRoot $runRoot -Context 'Receipt verification'
    $receipt = Read-M0ClosedJson -LiteralPath $receiptLease.path -Context 'QtBase refresh receipt'
    Assert-M0ExactProperties $receipt @(
        'schema_version', 'receipt_id', 'contract_sha256', 'refresh_id', 'captured_utc', 'execution', 'patch', 'configuration', 'evidence', 'limits'
    ) 'receipt'
    Assert-M0String $receipt.receipt_id 'Receipt id'
    Assert-M0Digest $receipt.contract_sha256 'Receipt contract SHA-256'
    Assert-M0String $receipt.refresh_id 'Receipt refresh id'
    Assert-M0String $receipt.captured_utc 'Receipt captured_utc'
    if ($receipt.schema_version -isnot [long] -or $receipt.schema_version -ne $contract.schema_version -or
        $receipt.receipt_id -cne "$($contract.refresh_id)-receipt" -or
        $receipt.contract_sha256 -cne $ExpectedContractSha256 -or
        $receipt.refresh_id -cne $contract.refresh_id) {
        throw 'Receipt identity or contract binding drifted.'
    }
    Assert-M0CanonicalUtcTimestamp $receipt.captured_utc 'Receipt captured_utc'
    Assert-M0ExecutionObservation $receipt.execution 'receipt.execution'
    if ([int]$receipt.execution.exit_code -ne 0) { throw 'Receipt cannot authenticate a failed execution.' }
    Assert-M0PatchObservation $receipt.patch $contract.inputs.freetype_patch 'receipt.patch'
    Assert-M0ConfigurationObject $receipt.configuration 'receipt.configuration' -Observation
    Assert-M0FileReference $receipt.evidence 'receipt.evidence'
    $expectedManifestPath = if ($contract.schema_version -eq 2) { $script:EvidenceManifestV2RelativePath } else { $script:EvidenceManifestRelativePath }
    if ($receipt.evidence.relative_path -cne $expectedManifestPath) {
        throw 'Receipt evidence manifest path drifted.'
    }
    $manifestPath = Assert-M0PhysicalReference `
        -Reference $receipt.evidence `
        -Root $runRoot `
        -BoundaryRoot $runRoot `
        -Context 'Receipt evidence manifest' `
        -LeaseSink $leases
    $manifest = Read-M0ClosedJson -LiteralPath $manifestPath -Context 'QtBase refresh evidence manifest'
    Assert-M0EvidenceManifest $manifest $contract $ExpectedContractSha256 $runRoot $repository $leases
    Assert-M0StringSequence $receipt.limits $script:ReceiptLimits 'Receipt limits'
    Assert-M0PhysicalFileLeasesUnchanged $leases 'Receipt verification'
    return $receipt
    }
    finally {
        Close-M0PhysicalFileLeases $leases
    }
}

Export-ModuleMember -Function @(
    'Assert-M0QtBaseRefreshContract',
    'Assert-M0QtBaseRefreshReceipt',
    'Invoke-M0QtBaseRefresh'
)
