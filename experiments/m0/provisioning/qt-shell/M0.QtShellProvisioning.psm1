Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:M0QtShellModuleRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$script:M0QtShellRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..'))
$script:M0QtShellUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0QtShellPathComparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$script:M0QtShellProvisioningTestHook = $null

if ($IsWindows) {
    $nativePreloaded = $false
    foreach ($assembly in [AppDomain]::CurrentDomain.GetAssemblies()) {
        if ($assembly.GetName().Name -ieq 'M0QtShellNativeV1') {
            $nativePreloaded = $true
            break
        }
        try { $assemblyTypes = @($assembly.GetTypes()) }
        catch [Reflection.ReflectionTypeLoadException] { $assemblyTypes = @($_.Exception.Types | Where-Object { $null -ne $_ }) }
        foreach ($assemblyType in $assemblyTypes) {
            if ($null -ne $assemblyType.Namespace -and
                ([string]$assemblyType.Namespace -ieq 'M0QtShellNativeV1' -or
                 ([string]$assemblyType.Namespace).StartsWith('M0QtShellNativeV1.', [StringComparison]::OrdinalIgnoreCase))) {
                $nativePreloaded = $true
                break
            }
        }
        if ($nativePreloaded) { break }
    }
    if ($nativePreloaded) {
        throw [IO.InvalidDataException]::new('M0QT040 a native provisioning namespace or assembly was preloaded before module import.')
    }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Text;

namespace M0QtShellNativeV1 {
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
        private const int FileRenameInfo = 3;
        private const int FileDispositionInfo = 4;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            IntPtr securityAttributes, uint creationDisposition, uint flagsAndAttributes, IntPtr templateFile);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateDirectoryW(string path, IntPtr securityAttributes);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFileInformationByHandle(SafeFileHandle handle, int infoClass,
            IntPtr information, uint bufferSize);

        [DllImport("ntdll.dll")]
        private static extern int NtSetInformationFile(SafeFileHandle handle, out IO_STATUS_BLOCK ioStatusBlock,
            IntPtr information, uint bufferSize, int fileInformationClass);

        [DllImport("ntdll.dll")]
        private static extern uint RtlNtStatusToDosError(int status);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(IntPtr hFile, out BY_HANDLE_FILE_INFORMATION info);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(IntPtr hFile, StringBuilder path, uint pathLength, uint flags);

        public static uint GetLinkCount(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out info)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return info.NumberOfLinks;
        }

        public static string GetIdentity(SafeFileHandle handle) {
            BY_HANDLE_FILE_INFORMATION info;
            if (!GetFileInformationByHandle(handle.DangerousGetHandle(), out info)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return info.VolumeSerialNumber.ToString("X8") + ":" + info.FileIndexHigh.ToString("X8") + info.FileIndexLow.ToString("X8");
        }

        public static string GetFinalPath(SafeFileHandle handle) {
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle.DangerousGetHandle(), buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return buffer.ToString();
        }

        public static SafeFileHandle CreateNewReadWriteDelete(string path) {
            var handle = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS,
                FILE_SHARE_READ, IntPtr.Zero, CREATE_NEW, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenReadDelete(string path) {
            var handle = CreateFileW(path, GENERIC_READ | DELETE_ACCESS,
                FILE_SHARE_READ, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenReadShareAll(string path) {
            var handle = CreateFileW(path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | 0x00000004,
                IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryWithoutDeleteSharing(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle OpenDirectoryReadDeleteWithoutDeleteSharing(string path) {
            var handle = CreateFileW(path, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | DELETE_ACCESS,
                FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
            if (handle.IsInvalid) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            return handle;
        }

        public static SafeFileHandle CreateNewDirectoryReadDeleteWithoutDeleteSharing(string path) {
            if (!CreateDirectoryW(path, IntPtr.Zero)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return OpenDirectoryReadDeleteWithoutDeleteSharing(path);
        }

        public static void RenameNoReplaceRelative(SafeFileHandle handle, SafeFileHandle directory,
            string destinationName) {
            if (String.IsNullOrEmpty(destinationName) ||
                destinationName.IndexOfAny(new[] { '\\', '/', ':', '\0' }) >= 0) {
                throw new ArgumentException("The destination must be one relative leaf name.", "destinationName");
            }
            var name = Encoding.Unicode.GetBytes(destinationName);
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
                int status = NtSetInformationFile(handle, out ioStatusBlock, buffer, (uint)total, 10);
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

    [StructLayout(LayoutKind.Sequential)]
    public struct WINTRUST_FILE_INFO {
        public uint cbStruct;
        public IntPtr pcwszFilePath;
        public IntPtr hFile;
        public IntPtr pgKnownSubject;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct WINTRUST_DATA {
        public uint cbStruct;
        public IntPtr pPolicyCallbackData;
        public IntPtr pSIPClientData;
        public uint dwUIChoice;
        public uint fdwRevocationChecks;
        public uint dwUnionChoice;
        public IntPtr pFile;
        public uint dwStateAction;
        public IntPtr hWVTStateData;
        public IntPtr pwszURLReference;
        public uint dwProvFlags;
        public uint dwUIContext;
    }

    public static class AuthenticodeVerifier {
        private const uint WTD_UI_NONE = 2;
        private const uint WTD_REVOKE_NONE = 0;
        private const uint WTD_CHOICE_FILE = 1;
        private const uint WTD_STATEACTION_VERIFY = 1;
        private const uint WTD_STATEACTION_CLOSE = 2;
        private const uint WTD_CACHE_ONLY_URL_RETRIEVAL = 0x00001000;
        private static readonly Guid WINTRUST_ACTION_GENERIC_VERIFY_V2 =
            new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE");

        [DllImport("wintrust.dll", ExactSpelling = true)]
        private static extern int WinVerifyTrust(IntPtr hwnd, IntPtr actionId, IntPtr trustData);

        public static void VerifyCacheOnly(string path) {
            IntPtr pathPointer = IntPtr.Zero;
            IntPtr filePointer = IntPtr.Zero;
            IntPtr dataPointer = IntPtr.Zero;
            IntPtr actionPointer = IntPtr.Zero;
            bool verificationAttempted = false;
            try {
                pathPointer = Marshal.StringToHGlobalUni(path);
                var file = new WINTRUST_FILE_INFO {
                    cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_FILE_INFO)),
                    pcwszFilePath = pathPointer,
                    hFile = IntPtr.Zero,
                    pgKnownSubject = IntPtr.Zero
                };
                filePointer = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WINTRUST_FILE_INFO)));
                Marshal.StructureToPtr(file, filePointer, false);
                var data = new WINTRUST_DATA {
                    cbStruct = (uint)Marshal.SizeOf(typeof(WINTRUST_DATA)),
                    pPolicyCallbackData = IntPtr.Zero,
                    pSIPClientData = IntPtr.Zero,
                    dwUIChoice = WTD_UI_NONE,
                    fdwRevocationChecks = WTD_REVOKE_NONE,
                    dwUnionChoice = WTD_CHOICE_FILE,
                    pFile = filePointer,
                    dwStateAction = WTD_STATEACTION_VERIFY,
                    hWVTStateData = IntPtr.Zero,
                    pwszURLReference = IntPtr.Zero,
                    dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL,
                    dwUIContext = 0
                };
                dataPointer = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WINTRUST_DATA)));
                Marshal.StructureToPtr(data, dataPointer, false);
                actionPointer = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(Guid)));
                Marshal.StructureToPtr(WINTRUST_ACTION_GENERIC_VERIFY_V2, actionPointer, false);
                verificationAttempted = true;
                int status = WinVerifyTrust(IntPtr.Zero, actionPointer, dataPointer);
                if (status != 0) {
                    throw new Win32Exception(status, "WinVerifyTrust cache-only verification failed.");
                }
            }
            finally {
                if (verificationAttempted && dataPointer != IntPtr.Zero && actionPointer != IntPtr.Zero) {
                    var closeData = (WINTRUST_DATA)Marshal.PtrToStructure(dataPointer, typeof(WINTRUST_DATA));
                    closeData.dwStateAction = WTD_STATEACTION_CLOSE;
                    Marshal.StructureToPtr(closeData, dataPointer, false);
                    WinVerifyTrust(IntPtr.Zero, actionPointer, dataPointer);
                }
                if (actionPointer != IntPtr.Zero) { Marshal.FreeHGlobal(actionPointer); }
                if (dataPointer != IntPtr.Zero) { Marshal.FreeHGlobal(dataPointer); }
                if (filePointer != IntPtr.Zero) { Marshal.FreeHGlobal(filePointer); }
                if (pathPointer != IntPtr.Zero) { Marshal.FreeHGlobal(pathPointer); }
            }
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_COUNTERS {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {
        public long TotalUserTime;
        public long TotalKernelTime;
        public long ThisPeriodTotalUserTime;
        public long ThisPeriodTotalKernelTime;
        public uint TotalPageFaultCount;
        public uint TotalProcesses;
        public uint ActiveProcesses;
        public uint TotalTerminatedProcesses;
    }

    public sealed class KillOnCloseJob : IDisposable {
        private IntPtr handle;
        private const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        private const int JobObjectExtendedLimitInformation = 9;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int infoClass,
            ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, uint length);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool QueryInformationJobObject(IntPtr job, int infoClass,
            out JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info, uint length, IntPtr returnLength);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        public KillOnCloseJob() {
            handle = CreateJobObject(IntPtr.Zero, null);
            if (handle == IntPtr.Zero) { throw new Win32Exception(Marshal.GetLastWin32Error()); }
            var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(handle, JobObjectExtendedLimitInformation, ref info,
                (uint)Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION)))) {
                var error = Marshal.GetLastWin32Error();
                CloseHandle(handle);
                handle = IntPtr.Zero;
                throw new Win32Exception(error);
            }
        }

        public void Assign(Process process) {
            AssignHandle(process.Handle);
        }

        public void AssignHandle(IntPtr processHandle) {
            if (handle == IntPtr.Zero || !AssignProcessToJobObject(handle, processHandle)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }

        public IntPtr DangerousGetHandle() {
            if (handle == IntPtr.Zero) { throw new ObjectDisposedException("KillOnCloseJob"); }
            return handle;
        }

        public void TerminateAndWait(int timeoutMilliseconds) {
            if (handle == IntPtr.Zero) { throw new ObjectDisposedException("KillOnCloseJob"); }
            long deadline = Environment.TickCount64 + timeoutMilliseconds;
            bool terminationIssued = false;
            while (true) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting;
                if (!QueryInformationJobObject(handle, 1, out accounting,
                    (uint)Marshal.SizeOf(typeof(JOBOBJECT_BASIC_ACCOUNTING_INFORMATION)), IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                if (accounting.ActiveProcesses == 0) { return; }
                if (!terminationIssued) {
                    if (!TerminateJobObject(handle, 197)) {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }
                    terminationIssued = true;
                }
                if (Environment.TickCount64 >= deadline) {
                    throw new TimeoutException("The owned Job did not become empty before the deadline.");
                }
                System.Threading.Thread.Sleep(10);
            }
        }

        public void Dispose() {
            if (handle != IntPtr.Zero) {
                CloseHandle(handle);
                handle = IntPtr.Zero;
            }
            GC.SuppressFinalize(this);
        }

        ~KillOnCloseJob() { Dispose(); }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SECURITY_ATTRIBUTES {
        public uint nLength;
        public IntPtr lpSecurityDescriptor;
        [MarshalAs(UnmanagedType.Bool)] public bool bInheritHandle;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct STARTUPINFO {
        public uint cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public uint dwX;
        public uint dwY;
        public uint dwXSize;
        public uint dwYSize;
        public uint dwXCountChars;
        public uint dwYCountChars;
        public uint dwFillAttribute;
        public uint dwFlags;
        public ushort wShowWindow;
        public ushort cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct STARTUPINFOEX {
        public STARTUPINFO StartupInfo;
        public IntPtr lpAttributeList;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess;
        public IntPtr hThread;
        public uint dwProcessId;
        public uint dwThreadId;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_MITIGATION_IMAGE_LOAD_POLICY {
        public uint Flags;
    }

    public static class SuspendedProcessLauncher {
        private const uint GENERIC_READ = 0x80000000;
        private const uint GENERIC_WRITE = 0x40000000;
        private const uint DELETE_ACCESS = 0x00010000;
        private const uint FILE_SHARE_READ = 0x00000001;
        private const uint FILE_SHARE_WRITE = 0x00000002;
        private const uint CREATE_NEW = 1;
        private const uint OPEN_EXISTING = 3;
        private const uint CREATE_SUSPENDED = 0x00000004;
        private const uint CREATE_UNICODE_ENVIRONMENT = 0x00000400;
        private const uint EXTENDED_STARTUPINFO_PRESENT = 0x00080000;
        private const uint CREATE_NO_WINDOW = 0x08000000;
        private const uint STARTF_USESTDHANDLES = 0x00000100;
        private const int ProcessImageLoadPolicy = 10;
        private const uint REQUIRED_IMAGE_LOAD_POLICY_FLAGS = 0x00000007;
        private const ulong IMAGE_LOAD_NO_REMOTE_ALWAYS_ON = 1UL << 52;
        private const ulong IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON = 1UL << 56;
        private const ulong IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON = 1UL << 60;
        private static readonly IntPtr PROC_THREAD_ATTRIBUTE_HANDLE_LIST = new IntPtr(0x00020002);
        private static readonly IntPtr PROC_THREAD_ATTRIBUTE_JOB_LIST = new IntPtr(0x0002000D);
        private static readonly IntPtr PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY = new IntPtr(0x00020007);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
            ref SECURITY_ATTRIBUTES securityAttributes, uint creationDisposition, uint flags, IntPtr templateFile);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateProcessW(string applicationName, StringBuilder commandLine,
            IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles, uint creationFlags,
            IntPtr environment, string currentDirectory, ref STARTUPINFOEX startupInfo,
            out PROCESS_INFORMATION processInformation);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool InitializeProcThreadAttributeList(IntPtr attributeList, int count,
            uint flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UpdateProcThreadAttribute(IntPtr attributeList, uint flags,
            IntPtr attribute, IntPtr value, IntPtr size, IntPtr previousValue, IntPtr returnSize);
        [DllImport("kernel32.dll")]
        private static extern void DeleteProcThreadAttributeList(IntPtr attributeList);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetProcessMitigationPolicy(IntPtr process, int mitigationPolicy,
            out PROCESS_MITIGATION_IMAGE_LOAD_POLICY buffer, IntPtr length);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileSizeEx(SafeFileHandle file, out long size);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetFilePointerEx(SafeFileHandle file, long distance,
            out long newPosition, uint moveMethod);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ReadFile(SafeFileHandle file, byte[] buffer, int bytesToRead,
            out int bytesRead, IntPtr overlapped);

        public sealed class RetainedCapture : IDisposable {
            private SafeFileHandle handle;
            private readonly string expectedPath;
            private readonly string identity;
            private bool deletePending;
            private bool disposed;

            internal SafeFileHandle Handle { get { return handle; } }

            internal RetainedCapture(SafeFileHandle fileHandle, string literalPath) {
                handle = fileHandle;
                expectedPath = Path.GetFullPath(literalPath);
                if (FileIdentity.GetLinkCount(handle) != 1) {
                    throw new InvalidOperationException("A capture leaf does not have a single-link identity.");
                }
                identity = FileIdentity.GetIdentity(handle);
                AssertIdentity();
                FileIdentity.MarkDeleteOnClose(handle);
                deletePending = true;
            }

            private static string NormalizeFinalPath(string path) {
                if (path.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) {
                    path = @"\\" + path.Substring(8);
                }
                else if (path.StartsWith(@"\\?\", StringComparison.OrdinalIgnoreCase)) {
                    path = path.Substring(4);
                }
                return Path.GetFullPath(path);
            }

            private void AssertIdentity() {
                if (disposed || handle == null || handle.IsInvalid || handle.IsClosed) {
                    throw new ObjectDisposedException("RetainedCapture");
                }
                uint linkCount = FileIdentity.GetLinkCount(handle);
                if ((!deletePending && linkCount != 1) || (deletePending && linkCount > 1) ||
                    !String.Equals(FileIdentity.GetIdentity(handle), identity, StringComparison.Ordinal)) {
                    throw new InvalidOperationException("A capture leaf identity changed while retained.");
                }
                string finalPath = NormalizeFinalPath(FileIdentity.GetFinalPath(handle));
                if (!String.Equals(finalPath, expectedPath, StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidOperationException("A capture leaf final path changed while retained.");
                }
            }

            public long GetLength() {
                AssertIdentity();
                long length;
                if (!GetFileSizeEx(handle, out length)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                return length;
            }

            public byte[] ReadBounded(int maximumBytes) {
                AssertIdentity();
                long length = GetLength();
                if (length < 0 || length > maximumBytes || length > Int32.MaxValue) {
                    throw new InvalidOperationException("Capture bytes exceed the bounded read limit.");
                }
                long ignoredPosition;
                if (!SetFilePointerEx(handle, 0, out ignoredPosition, 0)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                var bytes = new byte[(int)length];
                int offset = 0;
                while (offset < bytes.Length) {
                    int read;
                    var chunk = new byte[bytes.Length - offset];
                    if (!ReadFile(handle, chunk, chunk.Length, out read, IntPtr.Zero)) {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }
                    if (read == 0) { throw new EndOfStreamException("Capture ended before its held length."); }
                    Buffer.BlockCopy(chunk, 0, bytes, offset, read);
                    offset += read;
                }
                AssertIdentity();
                if (GetLength() != length) {
                    throw new InvalidOperationException("Capture length changed during held-handle readback.");
                }
                return bytes;
            }

            public void MarkDeleteOnClose() {
                AssertIdentity();
                if (!deletePending) {
                    FileIdentity.MarkDeleteOnClose(handle);
                    deletePending = true;
                }
            }

            public void Dispose() {
                if (disposed) { return; }
                if (handle != null) { handle.Dispose(); }
                disposed = true;
                GC.SuppressFinalize(this);
            }

            ~RetainedCapture() { Dispose(); }
        }

        private static string Quote(string value) {
            if (value.Length > 0 && value.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) { return value; }
            var result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char character in value) {
                if (character == '\\') { slashes++; continue; }
                if (character == '"') {
                    result.Append('\\', (slashes * 2) + 1).Append('"');
                    slashes = 0;
                    continue;
                }
                if (slashes > 0) { result.Append('\\', slashes); slashes = 0; }
                result.Append(character);
            }
            if (slashes > 0) { result.Append('\\', slashes * 2); }
            return result.Append('"').ToString();
        }

        private static IntPtr BuildEnvironment(System.Collections.IDictionary environment) {
            var names = new System.Collections.Generic.List<string>();
            foreach (System.Collections.DictionaryEntry entry in environment) {
                if (entry.Value != null) { names.Add((string)entry.Key); }
            }
            names.Sort(StringComparer.OrdinalIgnoreCase);
            var block = new StringBuilder();
            foreach (string name in names) {
                block.Append(name).Append('=').Append((string)environment[name]).Append('\0');
            }
            block.Append('\0');
            return Marshal.StringToHGlobalUni(block.ToString());
        }

        public sealed class PendingLaunch : IDisposable {
            private PROCESS_INFORMATION processInformation;
            private bool resumed;
            private bool disposed;
            private RetainedCapture standardOutput;
            private RetainedCapture standardError;

            public Process Process { get; private set; }

            internal PendingLaunch(PROCESS_INFORMATION information, RetainedCapture output,
                RetainedCapture error) {
                processInformation = information;
                standardOutput = output;
                standardError = error;
                Process = Process.GetProcessById((int)information.dwProcessId);
                var retainedManagedHandle = Process.Handle;
            }

            public long GetStandardOutputLength() { return standardOutput.GetLength(); }
            public long GetStandardErrorLength() { return standardError.GetLength(); }
            public byte[] ReadStandardOutput(int maximumBytes) { return standardOutput.ReadBounded(maximumBytes); }
            public byte[] ReadStandardError(int maximumBytes) { return standardError.ReadBounded(maximumBytes); }
            public void DeleteCapturesOnClose() {
                standardOutput.MarkDeleteOnClose();
                standardError.MarkDeleteOnClose();
            }

            private void ReleaseNativeHandles() {
                if (processInformation.hThread != IntPtr.Zero) {
                    CloseHandle(processInformation.hThread);
                    processInformation.hThread = IntPtr.Zero;
                }
                if (processInformation.hProcess != IntPtr.Zero) {
                    CloseHandle(processInformation.hProcess);
                    processInformation.hProcess = IntPtr.Zero;
                }
            }

            public void Resume() {
                if (disposed || resumed || processInformation.hProcess == IntPtr.Zero ||
                    processInformation.hThread == IntPtr.Zero) {
                    throw new InvalidOperationException("The suspended process is no longer resumable.");
                }
                try {
                    if (ResumeThread(processInformation.hThread) == UInt32.MaxValue) {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }
                    resumed = true;
                }
                catch {
                    TerminateProcess(processInformation.hProcess, 197);
                    throw;
                }
                finally {
                    ReleaseNativeHandles();
                }
            }

            public void Dispose() {
                if (disposed) { return; }
                if (!resumed && processInformation.hProcess != IntPtr.Zero) {
                    TerminateProcess(processInformation.hProcess, 197);
                }
                ReleaseNativeHandles();
                if (standardError != null) { standardError.Dispose(); standardError = null; }
                if (standardOutput != null) { standardOutput.Dispose(); standardOutput = null; }
                disposed = true;
                GC.SuppressFinalize(this);
            }

            ~PendingLaunch() { Dispose(); }
        }

        public static PendingLaunch CreateSuspended(KillOnCloseJob job, string executable, string arguments,
            string workingDirectory, System.Collections.IDictionary environment,
            string stdoutPath, string stderrPath) {
            if (job == null) { throw new ArgumentNullException("job"); }
            var security = new SECURITY_ATTRIBUTES {
                nLength = (uint)Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES)),
                lpSecurityDescriptor = IntPtr.Zero,
                bInheritHandle = true
            };
            SafeFileHandle stdout = null;
            SafeFileHandle stderr = null;
            RetainedCapture stdoutCapture = null;
            RetainedCapture stderrCapture = null;
            SafeFileHandle stdin = null;
            IntPtr attributeList = IntPtr.Zero;
            IntPtr handleList = IntPtr.Zero;
            IntPtr jobList = IntPtr.Zero;
            IntPtr mitigationPolicy = IntPtr.Zero;
            IntPtr environmentBlock = IntPtr.Zero;
            var processInfo = new PROCESS_INFORMATION();
            bool created = false;
            try {
                stdout = CreateFileW(stdoutPath, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS, FILE_SHARE_READ,
                    ref security, CREATE_NEW, 0, IntPtr.Zero);
                stderr = CreateFileW(stderrPath, GENERIC_READ | GENERIC_WRITE | DELETE_ACCESS, FILE_SHARE_READ,
                    ref security, CREATE_NEW, 0, IntPtr.Zero);
                stdin = CreateFileW("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    ref security, OPEN_EXISTING, 0, IntPtr.Zero);
                if (stdout.IsInvalid || stderr.IsInvalid || stdin.IsInvalid) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                stdoutCapture = new RetainedCapture(stdout, stdoutPath);
                stderrCapture = new RetainedCapture(stderr, stderrPath);

                IntPtr attributeSize = IntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 3, 0, ref attributeSize);
                attributeList = Marshal.AllocHGlobal(attributeSize);
                if (!InitializeProcThreadAttributeList(attributeList, 3, 0, ref attributeSize)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                handleList = Marshal.AllocHGlobal(IntPtr.Size * 3);
                Marshal.WriteIntPtr(handleList, 0, stdout.DangerousGetHandle());
                Marshal.WriteIntPtr(handleList, IntPtr.Size, stderr.DangerousGetHandle());
                Marshal.WriteIntPtr(handleList, IntPtr.Size * 2, stdin.DangerousGetHandle());
                if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    handleList, new IntPtr(IntPtr.Size * 3), IntPtr.Zero, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                jobList = Marshal.AllocHGlobal(IntPtr.Size);
                Marshal.WriteIntPtr(jobList, job.DangerousGetHandle());
                if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                    jobList, new IntPtr(IntPtr.Size), IntPtr.Zero, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                mitigationPolicy = Marshal.AllocHGlobal(sizeof(ulong));
                ulong imageLoadPolicy = IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
                    IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON | IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
                Marshal.WriteInt64(mitigationPolicy, unchecked((long)imageLoadPolicy));
                if (!UpdateProcThreadAttribute(attributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                    mitigationPolicy, new IntPtr(sizeof(ulong)), IntPtr.Zero, IntPtr.Zero)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                var startup = new STARTUPINFOEX();
                startup.StartupInfo.cb = (uint)Marshal.SizeOf(typeof(STARTUPINFOEX));
                startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
                startup.StartupInfo.hStdInput = stdin.DangerousGetHandle();
                startup.StartupInfo.hStdOutput = stdout.DangerousGetHandle();
                startup.StartupInfo.hStdError = stderr.DangerousGetHandle();
                startup.lpAttributeList = attributeList;
                environmentBlock = BuildEnvironment(environment);
                var commandLine = new StringBuilder(Quote(executable) +
                    (String.IsNullOrEmpty(arguments) ? String.Empty : " " + arguments));
                uint flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                    EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW;
                if (!CreateProcessW(executable, commandLine, IntPtr.Zero, IntPtr.Zero, true, flags,
                    environmentBlock, workingDirectory, ref startup, out processInfo)) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                created = true;
                PROCESS_MITIGATION_IMAGE_LOAD_POLICY observedPolicy;
                if (!GetProcessMitigationPolicy(processInfo.hProcess, ProcessImageLoadPolicy,
                    out observedPolicy, new IntPtr(Marshal.SizeOf(typeof(PROCESS_MITIGATION_IMAGE_LOAD_POLICY))))) {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                if ((observedPolicy.Flags & REQUIRED_IMAGE_LOAD_POLICY_FLAGS) != REQUIRED_IMAGE_LOAD_POLICY_FLAGS) {
                    throw new InvalidOperationException("The suspended child lacks the required image-load mitigation policy.");
                }
                var pending = new PendingLaunch(processInfo, stdoutCapture, stderrCapture);
                processInfo = new PROCESS_INFORMATION();
                stdoutCapture = null;
                stderrCapture = null;
                stdout = null;
                stderr = null;
                created = false;
                return pending;
            }
            catch {
                if (created && processInfo.hProcess != IntPtr.Zero) { TerminateProcess(processInfo.hProcess, 197); }
                throw;
            }
            finally {
                if (processInfo.hThread != IntPtr.Zero) { CloseHandle(processInfo.hThread); }
                if (processInfo.hProcess != IntPtr.Zero) { CloseHandle(processInfo.hProcess); }
                if (environmentBlock != IntPtr.Zero) { Marshal.FreeHGlobal(environmentBlock); }
                if (attributeList != IntPtr.Zero) { DeleteProcThreadAttributeList(attributeList); Marshal.FreeHGlobal(attributeList); }
                if (handleList != IntPtr.Zero) { Marshal.FreeHGlobal(handleList); }
                if (jobList != IntPtr.Zero) { Marshal.FreeHGlobal(jobList); }
                if (mitigationPolicy != IntPtr.Zero) { Marshal.FreeHGlobal(mitigationPolicy); }
                if (stdin != null) { stdin.Dispose(); }
                if (stderrCapture != null) {
                    try { stderrCapture.MarkDeleteOnClose(); } finally { stderrCapture.Dispose(); }
                }
                if (stdoutCapture != null) {
                    try { stdoutCapture.MarkDeleteOnClose(); } finally { stdoutCapture.Dispose(); }
                }
                if (stderr != null) { stderr.Dispose(); }
                if (stdout != null) { stdout.Dispose(); }
            }
        }
    }
}
'@
}

function Throw-M0QtShellInvalidData {
    param([Parameter(Mandatory)][string] $Message)
    throw [IO.InvalidDataException]::new($Message)
}

function Get-M0QtShellSha256FromStream {
    param([Parameter(Mandatory)][IO.Stream] $Stream)

    $position = $Stream.Position
    try {
        $Stream.Position = 0
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            return ([Convert]::ToHexString($algorithm.ComputeHash($Stream))).ToLowerInvariant()
        }
        finally {
            $algorithm.Dispose()
        }
    }
    finally {
        $Stream.Position = $position
    }
}

function Get-M0QtShellSha256Bytes {
    param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]] $Bytes)

    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([Convert]::ToHexString($algorithm.ComputeHash($Bytes))).ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function ConvertTo-M0QtShellCanonicalBytes {
    param([Parameter(Mandatory)][object] $Document)

    $text = ($Document | ConvertTo-Json -Compress -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    return $script:M0QtShellUtf8.GetBytes($text)
}

function Read-M0QtShellTrustedBytes {
    param([Parameter(Mandatory)][object] $TrustedFile)

    if ($TrustedFile.size_bytes -gt [int]::MaxValue) {
        Throw-M0QtShellInvalidData 'M0QT010 a trusted JSON leaf exceeds the supported document size.'
    }
    $position = $TrustedFile.stream.Position
    try {
        $TrustedFile.stream.Position = 0
        $bytes = [byte[]]::new([int]$TrustedFile.size_bytes)
        $TrustedFile.stream.ReadExactly($bytes)
        return ,$bytes
    }
    finally { $TrustedFile.stream.Position = $position }
}

function ConvertFrom-M0QtShellCanonicalJsonBytes {
    param([Parameter(Mandatory)][AllowEmptyCollection()][byte[]] $Bytes)

    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        Throw-M0QtShellInvalidData 'M0QT010 canonical JSON must not contain a BOM.'
    }
    $text = $script:M0QtShellUtf8.GetString($Bytes)
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.EndsWith("`n`n", [StringComparison]::Ordinal)) {
        Throw-M0QtShellInvalidData 'M0QT010 canonical JSON must use LF and exactly one terminal LF.'
    }
    $document = $text | ConvertFrom-Json -Depth 100 -DateKind String
    $expected = ConvertTo-M0QtShellCanonicalBytes -Document $document
    if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$Bytes, [byte[]]$expected)) {
        Throw-M0QtShellInvalidData 'M0QT010 JSON property order/bytes are not canonical.'
    }
    return $document
}

function Assert-M0QtShellSafePathText {
    param([Parameter(Mandatory)][string] $LiteralPath)

    if (-not [IO.Path]::IsPathFullyQualified($LiteralPath)) {
        Throw-M0QtShellInvalidData 'M0QT020 a trusted path must be absolute and literal.'
    }
    if ([string]::IsNullOrWhiteSpace($LiteralPath) -or $LiteralPath.IndexOf([char]0) -ge 0) {
        Throw-M0QtShellInvalidData 'M0QT020 a trusted path is empty or contains NUL.'
    }
    if ($IsWindows) {
        $colon = $LiteralPath.IndexOf(':', 2)
        if ($colon -ge 0) {
            Throw-M0QtShellInvalidData 'M0QT020 alternate data streams and colon path aliases are forbidden.'
        }
    }
}

function Assert-M0QtShellNoReparseAncestors {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [switch] $IncludeLeaf
    )

    $full = [IO.Path]::GetFullPath($LiteralPath)
    $current = if ($IncludeLeaf) { $full } else { [IO.Path]::GetDirectoryName($full) }
    while (-not [string]::IsNullOrWhiteSpace($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Throw-M0QtShellInvalidData "M0QT020 reparse/link path components are forbidden."
            }
        }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or $parent.Equals($current, $script:M0QtShellPathComparison)) {
            break
        }
        $current = $parent
    }
}

function ConvertFrom-M0QtShellFinalPath {
    param([Parameter(Mandatory)][string] $Path)

    if ($Path.StartsWith('\\?\UNC\', [StringComparison]::OrdinalIgnoreCase)) {
        return '\\' + $Path.Substring(8)
    }
    if ($Path.StartsWith('\\?\', [StringComparison]::OrdinalIgnoreCase)) {
        return $Path.Substring(4)
    }
    return $Path
}

function Open-M0QtShellPinnedDirectories {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $LiteralPath)

    if (-not $IsWindows) {
        Throw-M0QtShellInvalidData 'M0QT020 directory identity pinning is not proven outside Windows.'
    }
    $full = [IO.Path]::GetFullPath($LiteralPath)
    $directory = if ([IO.Directory]::Exists($full)) { $full } else { [IO.Path]::GetDirectoryName($full) }
    if ([string]::IsNullOrEmpty($directory) -or -not [IO.Directory]::Exists($directory)) {
        Throw-M0QtShellInvalidData 'M0QT020 directory identity pinning requires an existing directory.'
    }
    $paths = [Collections.Generic.List[string]]::new()
    while (-not [string]::IsNullOrEmpty($directory)) {
        $paths.Add($directory)
        $parent = [IO.Path]::GetDirectoryName($directory.TrimEnd([IO.Path]::DirectorySeparatorChar))
        if ([string]::IsNullOrEmpty($parent) -or $parent.Equals($directory, $script:M0QtShellPathComparison)) { break }
        $directory = $parent
    }
    $paths.Reverse()
    $pinned = [Collections.Generic.List[object]]::new()
    try {
        foreach ($path in $paths) {
            Assert-M0QtShellNoReparseAncestors -LiteralPath $path -IncludeLeaf
            $handle = [M0QtShellNativeV1.FileIdentity]::OpenDirectoryWithoutDeleteSharing($path)
            try {
                $final = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                    ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($handle))))
                if (-not $final.Equals([IO.Path]::GetFullPath($path), $script:M0QtShellPathComparison)) {
                    Throw-M0QtShellInvalidData 'M0QT020 a pinned directory final path differs from its literal path.'
                }
                $pinned.Add([pscustomobject]@{
                        handle = $handle
                        final_path = $final
                        identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($handle)
                    })
                $handle = $null
            }
            finally { if ($null -ne $handle) { $handle.Dispose() } }
        }
        return @($pinned)
    }
    catch {
        foreach ($entry in @($pinned)) { try { $entry.handle.Dispose() } catch { } }
        throw
    }
}

function Assert-M0QtShellPinnedDirectoriesUnchanged {
    param([AllowEmptyCollection()][object[]] $PinnedDirectories)

    foreach ($entry in @($PinnedDirectories)) {
        if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($entry.handle) -cne [string]$entry.identity) {
            Throw-M0QtShellInvalidData 'M0QT020 a pinned directory identity changed.'
        }
        $final = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($entry.handle))))
        if (-not $final.Equals([string]$entry.final_path, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT020 a pinned directory final path changed.'
        }
    }
}

function Close-M0QtShellPinnedDirectories {
    param([AllowEmptyCollection()][object[]] $PinnedDirectories)
    for ($index = @($PinnedDirectories).Count - 1; $index -ge 0; $index--) {
        try { $PinnedDirectories[$index].handle.Dispose() } catch { }
    }
}

function Open-M0QtShellTrustedFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedSha256,
        [long] $ExpectedSizeBytes,
        [switch] $DeleteOnCloseCapable
    )

    $hasExpectedSize = $PSBoundParameters.ContainsKey('ExpectedSizeBytes')
    if ($hasExpectedSize -and $ExpectedSizeBytes -lt 0) {
        Throw-M0QtShellInvalidData 'M0QT021 expected size cannot be negative.'
    }
    Assert-M0QtShellSafePathText -LiteralPath $LiteralPath
    $full = [IO.Path]::GetFullPath($LiteralPath)
    Assert-M0QtShellNoReparseAncestors -LiteralPath $full -IncludeLeaf
    if (-not [IO.File]::Exists($full)) {
        Throw-M0QtShellInvalidData 'M0QT021 required trusted file is missing.'
    }

    $stream = $null
    try {
        $stream = if ($DeleteOnCloseCapable) {
            if (-not $IsWindows) { Throw-M0QtShellInvalidData 'M0QT021 retained identity deletion is not proven outside Windows.' }
            $safeHandle = [M0QtShellNativeV1.FileIdentity]::OpenReadDelete($full)
            try {
                $createdStream = [IO.FileStream]::new($safeHandle, [IO.FileAccess]::Read, 4096, $false)
                $safeHandle = $null
                $createdStream
            }
            finally { if ($null -ne $safeHandle) { $safeHandle.Dispose() } }
        }
        else {
            [IO.FileStream]::new($full, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        }
        $identity = $null
        if ($IsWindows) {
            $linkCount = [M0QtShellNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle)
            if ($linkCount -ne 1) {
                Throw-M0QtShellInvalidData 'M0QT021 trusted files must have a single link identity.'
            }
            $final = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
            if (-not $final.Equals($full, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT021 trusted file final path differs from its literal path.'
            }
            $identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle)
        }

        if ($hasExpectedSize -and $stream.Length -ne $ExpectedSizeBytes) {
            Throw-M0QtShellInvalidData 'M0QT021 trusted file size/bytes drifted.'
        }
        $sha256 = Get-M0QtShellSha256FromStream -Stream $stream
        if ($PSBoundParameters.ContainsKey('ExpectedSha256') -and $sha256 -cne $ExpectedSha256) {
            Throw-M0QtShellInvalidData 'M0QT021 trusted file hash drifted from the expected bytes.'
        }
        return [pscustomobject]@{
            stream = $stream
            sha256 = $sha256
            size_bytes = [long]$stream.Length
            final_path = $full
            identity = $identity
        }
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        throw
    }
}

function Assert-M0QtShellTrustedFileUnchanged {
    param([Parameter(Mandatory)][object] $TrustedFile)

    if ($TrustedFile.stream.Length -ne [long]$TrustedFile.size_bytes -or
        (Get-M0QtShellSha256FromStream -Stream $TrustedFile.stream) -cne [string]$TrustedFile.sha256) {
        Throw-M0QtShellInvalidData 'M0QT021 trusted file bytes changed while its handle was retained.'
    }
    if ($IsWindows) {
        if ([M0QtShellNativeV1.FileIdentity]::GetLinkCount($TrustedFile.stream.SafeFileHandle) -ne 1 -or
            [M0QtShellNativeV1.FileIdentity]::GetIdentity($TrustedFile.stream.SafeFileHandle) -cne [string]$TrustedFile.identity) {
            Throw-M0QtShellInvalidData 'M0QT021 trusted file link/identity changed while its handle was retained.'
        }
        $final = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($TrustedFile.stream.SafeFileHandle))))
        if (-not $final.Equals([string]$TrustedFile.final_path, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT021 trusted file final path changed while its handle was retained.'
        }
    }
}

function Assert-M0QtShellAuthenticode {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][string] $ExpectedSubject,
        [Parameter(Mandatory)][ValidatePattern('^[A-F0-9]{40}$')][string] $ExpectedThumbprint,
        [object] $TrustedFile
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0QT030 Authenticode validation is Windows-only and remains not_proven here.')
    }
    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    $ownsHeld = -not $PSBoundParameters.ContainsKey('TrustedFile')
    $held = if ($ownsHeld) {
        Open-M0QtShellTrustedFile -LiteralPath $fullPath
    }
    else {
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $TrustedFile
        if (-not ([string]$TrustedFile.final_path).Equals($fullPath, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT030 retained Authenticode leaf differs from the requested literal path.'
        }
        $TrustedFile
    }
    $pinnedDirectories = @()
    try {
        $pinnedDirectories = @(Open-M0QtShellPinnedDirectories -LiteralPath $held.final_path)
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pinnedDirectories
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $held
        $canonicalWindowsRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        if (-not [IO.Directory]::Exists($canonicalWindowsRoot)) {
            Throw-M0QtShellInvalidData 'M0QT030 the canonical Windows directory is unavailable for trust verification.'
        }
        $previousSystemRoot = [Environment]::GetEnvironmentVariable('SystemRoot', [EnvironmentVariableTarget]::Process)
        $previousWinDir = [Environment]::GetEnvironmentVariable('WINDIR', [EnvironmentVariableTarget]::Process)
        try {
            [Environment]::SetEnvironmentVariable('SystemRoot', $canonicalWindowsRoot, [EnvironmentVariableTarget]::Process)
            [Environment]::SetEnvironmentVariable('WINDIR', $canonicalWindowsRoot, [EnvironmentVariableTarget]::Process)
            try { [M0QtShellNativeV1.AuthenticodeVerifier]::VerifyCacheOnly($held.final_path) }
            catch { Throw-M0QtShellInvalidData 'M0QT030 the Authenticode signature is not Valid under cache-only trust verification.' }
            $certificate = $null
            try {
                $signedCertificate = [Security.Cryptography.X509Certificates.X509Certificate]::CreateFromSignedFile($held.final_path)
                $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($signedCertificate)
                if ($certificate.Subject -cne $ExpectedSubject) {
                    Throw-M0QtShellInvalidData 'M0QT030 the Authenticode signer subject differs from the expected NV Access identity.'
                }
                if ($certificate.Thumbprint.ToUpperInvariant() -cne $ExpectedThumbprint) {
                    Throw-M0QtShellInvalidData 'M0QT030 the Authenticode signer thumbprint differs from the expected certificate.'
                }
            }
            finally {
                if ($null -ne $certificate) { $certificate.Dispose() }
            }
        }
        finally {
            [Environment]::SetEnvironmentVariable('SystemRoot', $previousSystemRoot, [EnvironmentVariableTarget]::Process)
            [Environment]::SetEnvironmentVariable('WINDIR', $previousWinDir, [EnvironmentVariableTarget]::Process)
        }
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $held
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pinnedDirectories
        return [pscustomobject]@{ valid = $true; subject = $ExpectedSubject; thumbprint = $ExpectedThumbprint }
    }
    finally {
        if (@($pinnedDirectories).Count -gt 0) {
            Close-M0QtShellPinnedDirectories -PinnedDirectories $pinnedDirectories
        }
        if ($ownsHeld) { $held.stream.Dispose() }
    }
}

function Assert-M0QtShellArchiveMembers {
    [CmdletBinding()]
    param([Parameter(Mandatory)][ValidateNotNullOrEmpty()][object[]] $Members)

    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($member in $Members) {
        $path = [string]$member.path
        $kind = [string]$member.kind
        if ([string]::IsNullOrWhiteSpace($path) -or $path.IndexOf([char]0) -ge 0 -or
            $path -match '^[\\/]' -or $path -match '^[A-Za-z]:' -or $path.Contains(':') -or
            @($path -split '[\\/]' | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0) {
            Throw-M0QtShellInvalidData 'M0QT022 unsafe archive member path was rejected.'
        }
        if ($kind -cnotin @('file', 'directory')) {
            Throw-M0QtShellInvalidData 'M0QT022 archive links and non-regular members are forbidden.'
        }
        $normalized = $path.Replace('\', '/').TrimEnd('/')
        if (-not $seen.Add($normalized)) {
            Throw-M0QtShellInvalidData 'M0QT022 duplicate or case-aliased archive member was rejected.'
        }
    }
    return $true
}

function ConvertFrom-M0QtShell7ZipSltListing {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $Text)

    $members = [Collections.Generic.List[object]]::new()
    $record = [ordered]@{}
    $insideMembers = $false
    $flush = {
        if (-not $insideMembers -or -not $record.Contains('Path')) {
            $record.Clear()
            return
        }
        $kind = if ($record.Contains('Symbolic Link') -or $record.Contains('Hard Link')) {
            'symlink'
        }
        elseif ($record.Contains('Attributes') -and [string]$record['Attributes'] -match '^D') {
            'directory'
        }
        else { 'file' }
        $members.Add([pscustomobject]@{ path = [string]$record['Path']; kind = $kind })
        $record.Clear()
    }
    foreach ($line in @($Text.Replace("`r`n", "`n").Replace("`r", "`n") -split "`n")) {
        if ($line -match '^-{5,}\s*$') {
            & $flush
            $insideMembers = $true
            continue
        }
        if ([string]::IsNullOrWhiteSpace($line)) {
            & $flush
            continue
        }
        if ($line -match '^([^=]+?)\s*=\s*(.*)$') {
            $record[$matches[1].Trim()] = $matches[2]
        }
    }
    & $flush
    if ($members.Count -eq 0) {
        Throw-M0QtShellInvalidData 'M0QT022 archive listing did not contain any member records.'
    }
    return @($members)
}

function Assert-M0QtShellExtractedInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string[]] $ExpectedRelativePaths
    )

    Assert-M0QtShellSafePathText -LiteralPath $Root
    $fullRoot = [IO.Path]::GetFullPath($Root)
    Assert-M0QtShellNoReparseAncestors -LiteralPath $fullRoot -IncludeLeaf
    if (-not [IO.Directory]::Exists($fullRoot)) {
        Throw-M0QtShellInvalidData 'M0QT024 extracted inventory root is missing.'
    }
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $ExpectedRelativePaths) {
        if ([string]::IsNullOrWhiteSpace($relative) -or $relative -match '^[\\/]' -or $relative -match '^[A-Za-z]:' -or
            $relative.Contains(':') -or @($relative -split '[\\/]' | Where-Object { $_ -in @('', '.', '..') }).Count -ne 0 -or
            -not $expected.Add($relative.Replace('\', '/'))) {
            Throw-M0QtShellInvalidData 'M0QT024 expected extracted inventory contains an unsafe or duplicate path.'
        }
    }
    $actual = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in @(Get-ChildItem -LiteralPath $fullRoot -Recurse -Force)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-M0QtShellInvalidData 'M0QT024 extracted inventory contains a reparse/link entry.'
        }
        if ($item.PSIsContainer) { continue }
        $relative = [IO.Path]::GetRelativePath($fullRoot, $item.FullName).Replace('\', '/')
        if (-not $actual.Add($relative)) {
            Throw-M0QtShellInvalidData 'M0QT024 extracted inventory contains a duplicate path identity.'
        }
        $held = Open-M0QtShellTrustedFile -LiteralPath $item.FullName
        $held.stream.Dispose()
    }
    if ($actual.Count -ne $expected.Count) {
        Throw-M0QtShellInvalidData 'M0QT024 extracted inventory contains an extra or missing leaf.'
    }
    foreach ($relative in $expected) {
        if (-not $actual.Contains($relative)) {
            Throw-M0QtShellInvalidData 'M0QT024 extracted inventory differs from the closed expected leaves.'
        }
    }
    return $true
}

function Assert-M0QtShellDirectoryTreeNoReparse {
    param([Parameter(Mandatory)][string] $Root)

    $fullRoot = [IO.Path]::GetFullPath($Root)
    Assert-M0QtShellNoReparseAncestors -LiteralPath $fullRoot -IncludeLeaf
    if (-not [IO.Directory]::Exists($fullRoot)) {
        Throw-M0QtShellInvalidData 'M0QT024 required trusted directory root is missing.'
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $fullRoot -Recurse -Force -ErrorAction Stop)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Throw-M0QtShellInvalidData 'M0QT024 trusted directory tree contains a reparse/link entry.'
        }
    }
}

function Stop-M0QtShellProcessTree {
    param([Parameter(Mandatory)][Diagnostics.Process] $Process)

    try {
        if (-not $Process.HasExited) {
            $Process.Kill($true)
        }
    }
    catch {
        try {
            if (-not $Process.HasExited) { $Process.Kill() }
        }
        catch { }
    }
    try { $Process.WaitForExit(5000) | Out-Null } catch { }
}

function ConvertTo-M0QtShellProcessArgument {
    param([AllowEmptyString()][string] $Value)

    if ($Value.IndexOf([char]0) -ge 0) {
        Throw-M0QtShellInvalidData 'M0QT020 child process arguments cannot contain NUL.'
    }
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    $null = $builder.Append('"')
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
            continue
        }
        if ($character -eq '"') {
            $null = $builder.Append(('\' * (($backslashes * 2) + 1)))
            $null = $builder.Append('"')
            $backslashes = 0
            continue
        }
        if ($backslashes -gt 0) {
            $null = $builder.Append(('\' * $backslashes))
            $backslashes = 0
        }
        $null = $builder.Append($character)
    }
    if ($backslashes -gt 0) { $null = $builder.Append(('\' * ($backslashes * 2))) }
    $null = $builder.Append('"')
    return $builder.ToString()
}

function Invoke-M0QtShellBoundedProcess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [AllowEmptyCollection()][string[]] $ArgumentList = @(),
        [ValidateRange(1, 300)][int] $TimeoutSeconds = 30,
        [ValidateRange(1, 16777216)][int] $MaximumCapturedBytes = 1048576,
        [switch] $AllowNonZeroExit,
        [string] $WorkingDirectory,
        [hashtable] $Environment,
        [string] $TempRoot,
        [string] $QtStageRoot,
        [scriptblock] $PinnedStateValidation,
        [object] $TrustedExecutable,
        [AllowEmptyCollection()][object[]] $TrustedDirectories
    )

    $resolvedFilePath = [IO.Path]::GetFullPath($FilePath)
    $ownsTrustedExecutable = -not $PSBoundParameters.ContainsKey('TrustedExecutable')
    $trusted = if ($ownsTrustedExecutable) {
        Open-M0QtShellTrustedFile -LiteralPath $resolvedFilePath
    }
    else {
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $TrustedExecutable
        if (-not ([string]$TrustedExecutable.final_path).Equals(
                $resolvedFilePath, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT020 retained executable differs from the requested literal path.'
        }
        $TrustedExecutable
    }
    $ownedRoot = $null
    $process = $null
    $ownedJob = $null
    $pendingLaunch = $null
    $pinnedDirectories = @()
    $ownsPinnedDirectories = -not $PSBoundParameters.ContainsKey('TrustedDirectories')
    $ownedRootHandle = $null
    $ownedRootIdentity = $null
    $primaryFailure = $null
    try {
        $baseTemp = if ([string]::IsNullOrWhiteSpace($TempRoot)) { [IO.Path]::GetTempPath() } else { [IO.Path]::GetFullPath($TempRoot) }
        if (-not [IO.Directory]::Exists($baseTemp)) {
            Assert-M0QtShellNoReparseAncestors -LiteralPath ([IO.Path]::GetDirectoryName($baseTemp)) -IncludeLeaf
            [IO.Directory]::CreateDirectory($baseTemp) | Out-Null
        }
        Assert-M0QtShellNoReparseAncestors -LiteralPath $baseTemp -IncludeLeaf
        $resolvedWorkingDirectory = if (-not [string]::IsNullOrWhiteSpace($WorkingDirectory)) {
            [IO.Path]::GetFullPath($WorkingDirectory)
        }
        else { $null }
        if ($null -ne $resolvedWorkingDirectory) {
            Assert-M0QtShellNoReparseAncestors -LiteralPath $resolvedWorkingDirectory -IncludeLeaf
            if (-not [IO.Directory]::Exists($resolvedWorkingDirectory)) {
                Throw-M0QtShellInvalidData 'M0QT020 the explicit process working directory is missing.'
            }
        }
        $resolvedQtStageRoot = if (-not [string]::IsNullOrWhiteSpace($QtStageRoot)) {
            [IO.Path]::GetFullPath($QtStageRoot)
        }
        else { $null }
        if ($null -ne $resolvedQtStageRoot) {
            Assert-M0QtShellNoReparseAncestors -LiteralPath $resolvedQtStageRoot -IncludeLeaf
            if (-not [IO.Directory]::Exists($resolvedQtStageRoot) -or
                -not ([string]$trusted.final_path).StartsWith(
                    $resolvedQtStageRoot + [IO.Path]::DirectorySeparatorChar,
                    $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT020 the Qt executable and isolated stage root are inconsistent.'
            }
        }
        $ownedRoot = Join-Path $baseTemp ('m0-qt-probe-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($ownedRoot) | Out-Null
        Assert-M0QtShellNoReparseAncestors -LiteralPath $ownedRoot -IncludeLeaf
        $ownedRootHandle = [M0QtShellNativeV1.FileIdentity]::OpenDirectoryReadDeleteWithoutDeleteSharing($ownedRoot)
        $ownedRootIdentity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($ownedRootHandle)
        $stdoutPath = Join-Path $ownedRoot 'stdout.bin'
        $stderrPath = Join-Path $ownedRoot 'stderr.bin'

        $windowsRoot = if ($IsWindows) { [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows) } else { '/' }
        $systemDrive = if ($IsWindows) { [IO.Path]::GetPathRoot($windowsRoot).TrimEnd([IO.Path]::DirectorySeparatorChar) } else { '/' }
        $programData = if ($IsWindows) { [Environment]::GetFolderPath([Environment+SpecialFolder]::CommonApplicationData) } else { '/' }
        $closedEnvironment = @{}
        foreach ($target in @([EnvironmentVariableTarget]::Process, [EnvironmentVariableTarget]::User,
            [EnvironmentVariableTarget]::Machine)) {
            foreach ($key in [Environment]::GetEnvironmentVariables($target).Keys) {
                $closedEnvironment[[string]$key] = $null
            }
        }
        $closedPathSegments = [Collections.Generic.List[string]]::new()
        $closedPathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($segment in @([IO.Path]::GetDirectoryName($trusted.final_path), (Join-Path $windowsRoot 'System32'))) {
            $normalizedSegment = [IO.Path]::GetFullPath($segment).TrimEnd([IO.Path]::DirectorySeparatorChar)
            if ($closedPathSet.Add($normalizedSegment)) { $closedPathSegments.Add($normalizedSegment) }
        }
        foreach ($entry in @{
            PATH = ($closedPathSegments -join [IO.Path]::PathSeparator)
            SystemRoot = $windowsRoot; WINDIR = $windowsRoot; SystemDrive = $systemDrive; ProgramData = $programData
            TEMP = $ownedRoot; TMP = $ownedRoot
            QML_IMPORT_PATH = ''; QML2_IMPORT_PATH = ''; QT_PLUGIN_PATH = ''; QT_QPA_PLATFORM_PLUGIN_PATH = ''
            QT_QPA_PLATFORM = ''; QT_QUICK_CONTROLS_STYLE = ''; QML_DISABLE_DISK_CACHE = ''; QTDIR = ''; QT_SELECT = ''
            HTTP_PROXY = ''; HTTPS_PROXY = ''; ALL_PROXY = ''; NO_PROXY = '*'
        }.GetEnumerator()) {
            $closedEnvironment[[string]$entry.Key] = [string]$entry.Value
        }
        if ($null -ne $resolvedQtStageRoot) {
            $closedEnvironment['QML_IMPORT_PATH'] = [string](Join-Path $resolvedQtStageRoot 'qml')
            $closedEnvironment['QML2_IMPORT_PATH'] = [string](Join-Path $resolvedQtStageRoot 'qml')
            $closedEnvironment['QT_PLUGIN_PATH'] = [string](Join-Path $resolvedQtStageRoot 'plugins')
            $closedEnvironment['QT_QPA_PLATFORM_PLUGIN_PATH'] = [string](Join-Path $resolvedQtStageRoot 'plugins\platforms')
            $closedEnvironment['QT_QPA_PLATFORM'] = [string]'windows'
            $closedEnvironment['QT_QUICK_CONTROLS_STYLE'] = [string]'Basic'
            $closedEnvironment['QML_DISABLE_DISK_CACHE'] = [string]'1'
        }
        if ($null -ne $Environment) {
            $reserved = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            foreach ($reservedName in @(
                    'PATH', 'PATHEXT', 'PSModulePath', 'SystemRoot', 'WINDIR', 'SystemDrive', 'ProgramData', 'TEMP', 'TMP',
                    'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
                    'QT_QPA_PLATFORM', 'QT_QUICK_CONTROLS_STYLE', 'QML_DISABLE_DISK_CACHE', 'QTDIR', 'QT_SELECT',
                    'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY', 'NO_PROXY')) {
                $null = $reserved.Add($reservedName)
            }
            foreach ($key in $Environment.Keys) {
                $name = [string]$key
                $value = [string]$Environment[$key]
                if ([string]::IsNullOrEmpty($name) -or $name.IndexOf([char]0) -ge 0 -or $name.Contains('=')) {
                    Throw-M0QtShellInvalidData 'M0QT020 child environment names must be nonempty and cannot contain NUL or equals.'
                }
                if ($value.IndexOf([char]0) -ge 0) {
                    Throw-M0QtShellInvalidData "M0QT020 child environment value for '$name' cannot contain NUL."
                }
                if ($reserved.Contains($name)) {
                    Throw-M0QtShellInvalidData "M0QT020 child environment key '$name' is reserved by the closed runner."
                }
                $closedEnvironment[$name] = $value
            }
        }

        if (-not $IsWindows) {
            Throw-M0QtShellInvalidData 'M0QT020 bounded external probes are not proven outside Windows.'
        }
        $argumentText = @($ArgumentList | ForEach-Object { ConvertTo-M0QtShellProcessArgument -Value $_ }) -join ' '
        $processWorkingDirectory = if ($null -ne $resolvedWorkingDirectory) { $resolvedWorkingDirectory } else { $ownedRoot }
        $beforeDirectoryPinsHook = if ($null -ne $script:M0QtShellProvisioningTestHook) {
            $script:M0QtShellProvisioningTestHook.PSObject.Properties['after_trusted_executable_opened_before_directory_pins']
        }
        else { $null }
        if ($null -ne $beforeDirectoryPinsHook -and $null -ne $beforeDirectoryPinsHook.Value) {
            & $beforeDirectoryPinsHook.Value
        }
        if ($ownsPinnedDirectories) {
            $pinnedDirectories = @(Open-M0QtShellPinnedDirectories -LiteralPath $trusted.final_path)
            if ($null -ne $resolvedWorkingDirectory) {
                $pinnedDirectories += @(Open-M0QtShellPinnedDirectories -LiteralPath $resolvedWorkingDirectory)
            }
        }
        else {
            $pinnedDirectories = @($TrustedDirectories)
            $pinnedPathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            foreach ($pinnedDirectory in $pinnedDirectories) {
                $null = $pinnedPathSet.Add([IO.Path]::GetFullPath([string]$pinnedDirectory.final_path))
            }
            foreach ($requiredLeaf in @($trusted.final_path, $resolvedWorkingDirectory) | Where-Object { $null -ne $_ }) {
                $requiredDirectory = if ([IO.Directory]::Exists([string]$requiredLeaf)) {
                    [IO.Path]::GetFullPath([string]$requiredLeaf)
                }
                else { [IO.Path]::GetDirectoryName([IO.Path]::GetFullPath([string]$requiredLeaf)) }
                while (-not [string]::IsNullOrEmpty($requiredDirectory)) {
                    if (-not $pinnedPathSet.Contains($requiredDirectory)) {
                        Throw-M0QtShellInvalidData 'M0QT020 externally retained directory pins do not cover every executable/working-directory ancestor.'
                    }
                    $requiredParent = [IO.Path]::GetDirectoryName(
                        $requiredDirectory.TrimEnd([IO.Path]::DirectorySeparatorChar))
                    if ([string]::IsNullOrEmpty($requiredParent) -or
                        $requiredParent.Equals($requiredDirectory, $script:M0QtShellPathComparison)) { break }
                    $requiredDirectory = $requiredParent
                }
            }
        }
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pinnedDirectories
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted
        if ($null -ne $PinnedStateValidation) { & $PinnedStateValidation | Out-Null }
        $ownedJob = [M0QtShellNativeV1.KillOnCloseJob]::new()
        $pendingLaunch = [M0QtShellNativeV1.SuspendedProcessLauncher]::CreateSuspended(
            $ownedJob,
            $trusted.final_path,
            $argumentText,
            $processWorkingDirectory,
            $closedEnvironment,
            $stdoutPath,
            $stderrPath
        )
        $process = $pendingLaunch.Process
        $beforeResumeHook = if ($null -ne $script:M0QtShellProvisioningTestHook) {
            $script:M0QtShellProvisioningTestHook.PSObject.Properties['after_job_bound_suspended_before_resume']
        }
        else { $null }
        if ($null -ne $beforeResumeHook -and $null -ne $beforeResumeHook.Value) {
            & $beforeResumeHook.Value ([int]$process.Id)
        }
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pinnedDirectories
        if ($null -ne $PinnedStateValidation) { & $PinnedStateValidation | Out-Null }
        $pendingLaunch.Resume()
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while (-not $process.HasExited) {
            if ([DateTime]::UtcNow -ge $deadline) {
                Stop-M0QtShellProcessTree -Process $process
                Throw-M0QtShellInvalidData 'M0QT050 external probe timeout; the owned process tree was terminated.'
            }
            if ($pendingLaunch.GetStandardOutputLength() -gt $MaximumCapturedBytes -or
                $pendingLaunch.GetStandardErrorLength() -gt $MaximumCapturedBytes) {
                Stop-M0QtShellProcessTree -Process $process
                Throw-M0QtShellInvalidData 'M0QT051 captured output exceeded the bounded output limit; the owned process tree was terminated.'
            }
            Start-Sleep -Milliseconds 25
            $process.Refresh()
        }
        $process.WaitForExit()
        $exitCode = [int]$process.ExitCode
        $ownedJob.TerminateAndWait(5000)
        if ($pendingLaunch.GetStandardOutputLength() -gt $MaximumCapturedBytes -or
            $pendingLaunch.GetStandardErrorLength() -gt $MaximumCapturedBytes) {
            Throw-M0QtShellInvalidData 'M0QT051 captured output exceeded the bounded output limit.'
        }
        $stdout = $script:M0QtShellUtf8.GetString($pendingLaunch.ReadStandardOutput($MaximumCapturedBytes))
        $stderr = $script:M0QtShellUtf8.GetString($pendingLaunch.ReadStandardError($MaximumCapturedBytes))
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pinnedDirectories
        Assert-M0QtShellNoReparseAncestors -LiteralPath $baseTemp -IncludeLeaf
        Assert-M0QtShellNoReparseAncestors -LiteralPath $ownedRoot -IncludeLeaf
        if ($null -ne $PinnedStateValidation) { & $PinnedStateValidation | Out-Null }
        if (-not $AllowNonZeroExit -and $exitCode -ne 0) {
            Throw-M0QtShellInvalidData "M0QT052 external probe returned nonzero exit code $exitCode."
        }
        return [pscustomobject]@{ exit_code = $exitCode; stdout = $stdout; stderr = $stderr }
    }
    catch {
        $causalFailure = $_
        $primaryFailure = $_
        if ($null -ne $ownedJob) {
            try {
                $ownedJob.TerminateAndWait(5000)
            }
            catch {
                $cleanupFailure = $_
                throw [IO.InvalidDataException]::new(
                    "M0QT053 the owned Job did not become empty while handling '$($causalFailure.Exception.Message)'.",
                    $cleanupFailure.Exception
                )
            }
        }
        throw $causalFailure
    }
    finally {
        $cleanupFailure = $null
        if ($null -ne $pendingLaunch) {
            try { $pendingLaunch.DeleteCapturesOnClose() }
            catch { $cleanupFailure = $_ }
            try { $pendingLaunch.Dispose() }
            catch { if ($null -eq $cleanupFailure) { $cleanupFailure = $_ } }
        }
        if ($null -ne $process -and -not $process.HasExited) { Stop-M0QtShellProcessTree -Process $process }
        if ($null -ne $ownedJob) { try { $ownedJob.Dispose() } catch { } }
        if ($null -ne $process) { try { $process.Dispose() } catch { } }
        if ($ownsTrustedExecutable -and $null -ne $trusted -and $null -ne $trusted.stream) {
            $trusted.stream.Dispose()
        }
        if ($ownsPinnedDirectories -and @($pinnedDirectories).Count -gt 0) {
            Close-M0QtShellPinnedDirectories -PinnedDirectories $pinnedDirectories
        }
        if ($null -ne $ownedRootHandle) {
            try {
                if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($ownedRootHandle) -cne $ownedRootIdentity) {
                    Throw-M0QtShellInvalidData 'M0QT054 the owned capture directory identity changed before cleanup.'
                }
                [M0QtShellNativeV1.FileIdentity]::MarkDeleteOnClose($ownedRootHandle)
            }
            catch { if ($null -eq $cleanupFailure) { $cleanupFailure = $_ } }
            finally { try { $ownedRootHandle.Dispose() } catch { } }
        }
        if ($null -ne $cleanupFailure) {
            $context = if ($null -ne $primaryFailure) { " while handling '$($primaryFailure.Exception.Message)'" } else { '' }
            throw [IO.InvalidDataException]::new(
                "M0QT054 retained capture cleanup failed$context.",
                $cleanupFailure.Exception
            )
        }
    }
}

function Assert-M0QtShellCanonicalJsonFile {
    param([Parameter(Mandatory)][string] $LiteralPath)

    $trusted = Open-M0QtShellTrustedFile -LiteralPath ([IO.Path]::GetFullPath($LiteralPath))
    try {
        return ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes (Read-M0QtShellTrustedBytes -TrustedFile $trusted)
    }
    finally {
        try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted }
        finally { $trusted.stream.Dispose() }
    }
}

function Assert-M0QtShellContractDocument {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $ContractDocument)

    $rootProperties = @($ContractDocument.PSObject.Properties.Name)
    if (($rootProperties | ConvertTo-Json -Compress) -cne '"files"') {
        Throw-M0QtShellInvalidData 'M0QT011 contract root shape must contain only files.'
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
    $entries = @($ContractDocument.files)
    if ($entries.Count -ne $expectedPaths.Count) {
        Throw-M0QtShellInvalidData 'M0QT011 contract file inventory count is not exact.'
    }
    for ($index = 0; $index -lt $entries.Count; $index++) {
        $entry = $entries[$index]
        $entryProperties = @($entry.PSObject.Properties.Name)
        if (($entryProperties | ConvertTo-Json -Compress) -cne '["path","sha256","size_bytes"]') {
            Throw-M0QtShellInvalidData 'M0QT011 contract entry shape must be exactly path, sha256, size_bytes.'
        }
        $path = [string]$entry.path
        if ($path -cne $expectedPaths[$index] -or $path -cnotmatch '^[a-zA-Z0-9._/-]+$' -or
            $path -cmatch '(^|/)\.\.(/|$)' -or $path.Contains('\')) {
            Throw-M0QtShellInvalidData 'M0QT011 contract file inventory is not exact, ordered, and safe.'
        }
        if ([string]$entry.sha256 -cnotmatch '^[0-9a-f]{64}$') {
            Throw-M0QtShellInvalidData 'M0QT011 contract entry hash is not canonical lowercase SHA-256.'
        }
        $size = $entry.size_bytes
        $integerSize = $size -is [byte] -or $size -is [sbyte] -or $size -is [int16] -or
            $size -is [uint16] -or $size -is [int32] -or $size -is [uint32] -or
            $size -is [int64] -or $size -is [uint64]
        if (-not $integerSize -or [decimal]$size -lt 0 -or [decimal]$size -gt [long]::MaxValue) {
            Throw-M0QtShellInvalidData 'M0QT011 contract entry size must be a nonnegative signed 64-bit integer.'
        }
    }
    return ,$entries
}

function Assert-M0QtShellStageManifestDocument {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object] $ManifestDocument,
        [Parameter(Mandatory)][object] $DescriptorDocument
    )

    $expectedMetadataPaths = @(
        'mkspecs/qconfig.pri',
        'modules/Core.json',
        'modules/Gui.json',
        'modules/Network.json',
        'modules/OpenGL.json',
        'modules/Qml.json',
        'modules/QmlMeta.json',
        'modules/QmlModels.json',
        'modules/QmlWorkerScript.json',
        'modules/Quick.json',
        'modules/QuickTest.json',
        'modules/Svg.json',
        'modules/Test.json',
        'translations/catalogs.json'
    )
    $entries = @($ManifestDocument.entries)
    $canonicalManifestBytes = ConvertTo-M0QtShellCanonicalBytes -Document $ManifestDocument
    if ([string]$ManifestDocument.'$schema' -cne './qt-shell-stage-manifest.schema.json' -or
        [int]$ManifestDocument.schema_version -ne 1 -or
        [string]$ManifestDocument.manifest_id -cne 'M0-QT-SHELL-STAGE-1' -or
        $entries.Count -ne 162 -or
        [long]$DescriptorDocument.qt.stage_manifest.entry_count -ne 162 -or
        [long]$DescriptorDocument.qt.stage_manifest.stage_total_size_bytes -ne 59226130 -or
        [long]$DescriptorDocument.qt.stage_manifest.deployment_metadata_leaf_count -ne 14 -or
        [long]$DescriptorDocument.qt.stage_manifest.deployment_metadata_size_bytes -ne 8282 -or
        $canonicalManifestBytes.Length -ne [long]$DescriptorDocument.qt.stage_manifest.size_bytes -or
        (Get-M0QtShellSha256Bytes -Bytes $canonicalManifestBytes) -cne [string]$DescriptorDocument.qt.stage_manifest.sha256) {
        Throw-M0QtShellInvalidData 'M0QT084 stage manifest identity or closed inventory metadata drifted.'
    }
    if ([string]$ManifestDocument.scope.phase -cne 'M0.2' -or
        [string]$ManifestDocument.scope.purpose -cne 'isolated-preflight-probes' -or
        [string]$ManifestDocument.scope.runtime_root -cne 'ephemeral-owned-stage' -or
        [bool]$ManifestDocument.scope.network_allowed -ne $false -or
        [bool]$ManifestDocument.scope.ambient_qt_lookup_allowed -ne $false -or
        [string]$ManifestDocument.qml_closure.tooling_roots -cne 'embedded-resources-not-staged' -or
        [string]$ManifestDocument.qml_closure.qmllint_plugins.mode -cne 'disabled-by-repeated-name' -or
        (@($ManifestDocument.qml_closure.qmllint_plugins.names) | ConvertTo-Json -Compress) -cne '["QtDesignStudio","Quick"]' -or
        [string]$DescriptorDocument.qt.stage_manifest.qmllint_plugin_disable_mode -cne 'repeated-name-options' -or
        (@($DescriptorDocument.qt.stage_manifest.qmllint_disabled_plugins) | ConvertTo-Json -Compress) -cne '["QtDesignStudio","Quick"]') {
        Throw-M0QtShellInvalidData 'M0QT084 stage qmllint or isolation policy drifted.'
    }
    if ([long]$ManifestDocument.pe_closure.leaf_count -ne 40 -or
        [long]$ManifestDocument.pe_closure.total_size_bytes -ne 57667264 -or
        [long]$ManifestDocument.pe_closure.canonical_tsv_size_bytes -ne 4019 -or
        [string]$ManifestDocument.pe_closure.canonical_tsv_sha256 -cne '4ec58c91c8e7c8a298a5696d24295ed584e21c2625a3ae9bf5b808a999150a8f' -or
        [string]$ManifestDocument.pe_closure.ordering -cne 'ordinal-ignore-case' -or
        [string]$ManifestDocument.pe_closure.derivation -cne 'anchored-dumpbin-import-table-analysis' -or
        [long]$ManifestDocument.pe_closure.internal_import_edge_count -ne 115 -or
        [long]$ManifestDocument.pe_closure.internal_import_edges_tsv_size_bytes -ne 5577 -or
        [string]$ManifestDocument.pe_closure.internal_import_edges_tsv_sha256 -cne '7f6289cc5baea58996f2e3ab6b89ce6dc190d00bbad7571b46979fb91052a916' -or
        [long]$ManifestDocument.pe_closure.missing_internal_qt_import_count -ne 0 -or
        [long]$ManifestDocument.pe_closure.external_import_name_count -ne 54 -or
        [long]$ManifestDocument.pe_closure.external_import_names_tsv_size_bytes -ne 1009 -or
        [string]$ManifestDocument.pe_closure.external_import_names_tsv_sha256 -cne '2c70f3b09a437ddb3409b87a36306f165987556530a6dd34e449cf71d320deee' -or
        [string]$ManifestDocument.pe_closure.external_resolution_policy -cne 'system32-or-api-set' -or
        [long]$ManifestDocument.pe_closure.api_set_alias_count -ne 16 -or
        [long]$ManifestDocument.pe_closure.concrete_external_name_count -ne 38 -or
        [long]$ManifestDocument.pe_closure.concrete_external_system32_missing_count -ne 0 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_leaf_count -ne 40 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_total_size_bytes -ne 57667264 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_closure_tsv_size_bytes -ne 4019 -or
        [string]$DescriptorDocument.qt.stage_manifest.pe_closure_tsv_sha256 -cne [string]$ManifestDocument.pe_closure.canonical_tsv_sha256 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edge_count -ne 115 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edges_tsv_size_bytes -ne 5577 -or
        [string]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edges_tsv_sha256 -cne [string]$ManifestDocument.pe_closure.internal_import_edges_tsv_sha256 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_missing_internal_qt_import_count -ne 0 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_external_import_name_count -ne 54 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_external_import_names_tsv_size_bytes -ne 1009 -or
        [string]$DescriptorDocument.qt.stage_manifest.pe_external_import_names_tsv_sha256 -cne [string]$ManifestDocument.pe_closure.external_import_names_tsv_sha256 -or
        [string]$DescriptorDocument.qt.stage_manifest.pe_external_resolution_policy -cne 'system32-or-api-set' -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_api_set_alias_count -ne 16 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_concrete_external_name_count -ne 38 -or
        [long]$DescriptorDocument.qt.stage_manifest.pe_concrete_external_system32_missing_count -ne 0) {
        Throw-M0QtShellInvalidData 'M0QT084 stage PE import closure identity or resolution policy drifted.'
    }

    $paths = [Collections.Generic.List[string]]::new()
    $pathSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        $path = [string]$entry.stage_relative_path
        if ($path -cnotmatch '^[A-Za-z0-9._/-]+$' -or $path -cmatch '(^|/)\.\.(/|$)' -or
            $path.Contains('\') -or -not $pathSet.Add($path)) {
            Throw-M0QtShellInvalidData 'M0QT084 stage manifest contains an unsafe, duplicate, or case-aliased path.'
        }
        $null = $paths.Add($path)
    }
    $sortedPaths = [Collections.Generic.List[string]]::new()
    foreach ($path in $paths) { $sortedPaths.Add($path) }
    $sortedPaths.Sort([StringComparer]::OrdinalIgnoreCase)
    if ((@($paths) | ConvertTo-Json -Compress) -cne (@($sortedPaths) | ConvertTo-Json -Compress)) {
        Throw-M0QtShellInvalidData 'M0QT084 stage manifest inventory is not OrdinalIgnoreCase ordered.'
    }
    foreach ($forbidden in @('qml/builtins.qmltypes', 'qml/jsroot.qmltypes',
            'plugins/qmllint/qdslintplugin.dll', 'plugins/qmllint/quicklintplugin.dll')) {
        if ($pathSet.Contains($forbidden)) {
            Throw-M0QtShellInvalidData 'M0QT084 stage manifest contains a forbidden ambient or vacuous-lint leaf.'
        }
    }

    $toolById = @{}
    foreach ($tool in @($DescriptorDocument.qt.tools)) { $toolById[[string]$tool.id] = $tool }
    $artifactById = @{}
    foreach ($artifact in @($DescriptorDocument.qt.selected_artifacts)) { $artifactById[[string]$artifact.id] = $artifact }
    $seenToolIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $seenArtifactIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $qmlRows = [Collections.Generic.List[string]]::new()
    $peRows = [Collections.Generic.List[string]]::new()
    $qmlBytes = 0L
    $peBytes = 0L
    $stageBytes = 0L
    $inlineCount = 0
    $qtFileCount = 0
    foreach ($entry in $entries) {
        $source = $null
        switch -CaseSensitive ([string]$entry.source_kind) {
            'qt_tool' {
                $id = [string]$entry.source_id
                if (-not $toolById.ContainsKey($id) -or -not $seenToolIds.Add($id)) {
                    Throw-M0QtShellInvalidData 'M0QT084 stage tool reference is missing or duplicated.'
                }
                $source = $toolById[$id]
            }
            'selected_artifact' {
                $id = [string]$entry.source_id
                if (-not $artifactById.ContainsKey($id) -or -not $seenArtifactIds.Add($id)) {
                    Throw-M0QtShellInvalidData 'M0QT084 stage artifact reference is missing or duplicated.'
                }
                $source = $artifactById[$id]
            }
            'qt_file' {
                $qtFileCount++
                if ([string]$entry.stage_relative_path -cne [string]$entry.source_relative_path -or
                    [string]$entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or [long]$entry.size_bytes -le 0) {
                    Throw-M0QtShellInvalidData 'M0QT084 stage Qt-file identity is invalid.'
                }
                $source = $entry
            }
            'inline_utf8' {
                $inlineCount++
                if ([string]$entry.stage_relative_path -cne 'bin/qt.conf') {
                    Throw-M0QtShellInvalidData 'M0QT084 stage contains an unapproved inline leaf.'
                }
                try { $inlineBytes = [Convert]::FromBase64String([string]$entry.content_base64) }
                catch { Throw-M0QtShellInvalidData 'M0QT084 stage inline qt.conf is not canonical base64.' }
                if ($inlineBytes.Length -ne [long]$entry.size_bytes -or
                    (Get-M0QtShellSha256Bytes -Bytes $inlineBytes) -cne [string]$entry.sha256 -or
                    $script:M0QtShellUtf8.GetString($inlineBytes) -cne "[Paths]`nPrefix=..`nBinaries=bin`nPlugins=plugins`nQmlImports=qml`n") {
                    Throw-M0QtShellInvalidData 'M0QT084 stage inline qt.conf bytes drifted.'
                }
                $source = $entry
            }
            default { Throw-M0QtShellInvalidData 'M0QT084 stage manifest contains an unknown source kind.' }
        }
        if ([string]$entry.source_kind -cin @('qt_tool', 'selected_artifact') -and
            [string]$entry.stage_relative_path -cne [string]$source.relative_path) {
            Throw-M0QtShellInvalidData 'M0QT084 stage descriptor reference changed its physical path.'
        }
        $stageBytes += [long]$source.size_bytes
        if ([string]$entry.stage_relative_path -cmatch '^qml/') {
            $qmlBytes += [long]$source.size_bytes
            $null = $qmlRows.Add("$($entry.stage_relative_path)`t$([long]$source.size_bytes)`t$($source.sha256)`n")
        }
        if ([string]$entry.source_kind -cin @('qt_tool', 'selected_artifact')) {
            $peBytes += [long]$source.size_bytes
            $null = $peRows.Add("$($entry.stage_relative_path)`t$([long]$source.size_bytes)`t$($source.sha256)`n")
        }
    }
    if ($seenToolIds.Count -ne @($DescriptorDocument.qt.tools).Count -or
        $seenArtifactIds.Count -ne @($DescriptorDocument.qt.selected_artifacts).Count -or
        $qtFileCount -ne 121 -or $inlineCount -ne 1 -or $stageBytes -ne 59226130) {
        Throw-M0QtShellInvalidData 'M0QT084 stage manifest source closure is incomplete.'
    }
    $metadataPaths = @($entries | Where-Object { $_.stage_relative_path -in $expectedMetadataPaths } |
            ForEach-Object { [string]$_.stage_relative_path })
    if (($metadataPaths | ConvertTo-Json -Compress) -cne ($expectedMetadataPaths | ConvertTo-Json -Compress) -or
        (@($entries | Where-Object { $_.stage_relative_path -in $expectedMetadataPaths } |
                Measure-Object size_bytes -Sum).Sum) -ne 8282) {
        Throw-M0QtShellInvalidData 'M0QT084 stage deployment metadata closure is incomplete.'
    }
    $qmlTsvBytes = $script:M0QtShellUtf8.GetBytes(($qmlRows -join ''))
    if ($qmlRows.Count -ne 119 -or $qmlBytes -ne 1934682 -or $qmlTsvBytes.Length -ne 13051 -or
        (Get-M0QtShellSha256Bytes -Bytes $qmlTsvBytes) -cne '97b67eab9a67c198583e454c4ed383b8f365262c262aaed445552c1f7edc0c44') {
        Throw-M0QtShellInvalidData 'M0QT084 stage QML closure identity drifted.'
    }
    $peTsvBytes = $script:M0QtShellUtf8.GetBytes(($peRows -join ''))
    if ($peRows.Count -ne 40 -or $peBytes -ne 57667264 -or $peTsvBytes.Length -ne 4019 -or
        (Get-M0QtShellSha256Bytes -Bytes $peTsvBytes) -cne '4ec58c91c8e7c8a298a5696d24295ed584e21c2625a3ae9bf5b808a999150a8f') {
        Throw-M0QtShellInvalidData 'M0QT084 stage PE leaf closure identity drifted.'
    }
    return $ManifestDocument
}

function Assert-M0QtShellProvisioningContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedContractSha256,
        [string] $ContractRoot,
        [switch] $RetainHandles
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    if (-not $root.Equals($script:M0QtShellRepositoryRoot, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT011 RepositoryRoot differs from the literal repository that loaded this module.'
    }
    $expectedRoot = $script:M0QtShellModuleRoot
    $actualContractRoot = if ([string]::IsNullOrWhiteSpace($ContractRoot)) { $expectedRoot } else { [IO.Path]::GetFullPath($ContractRoot) }
    if (-not $actualContractRoot.Equals($expectedRoot, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT011 contract root differs from the literal provisioning root.'
    }

    $contractPath = Join-Path $actualContractRoot 'qt-shell-toolchain-contract.v1.json'
    $sidecarPath = "$contractPath.sha256"
    $contractFile = Open-M0QtShellTrustedFile -LiteralPath $contractPath -ExpectedSha256 $ExpectedContractSha256
    $sidecarFile = $null
    $handles = [Collections.Generic.List[object]]::new()
    $succeeded = $false
    try {
        $sidecarFile = Open-M0QtShellTrustedFile -LiteralPath $sidecarPath -ExpectedSizeBytes 65
        $sidecarFile.stream.Position = 0
        $sidecarText = $script:M0QtShellUtf8.GetString(([IO.BinaryReader]::new($sidecarFile.stream, $script:M0QtShellUtf8, $true)).ReadBytes(65))
        if ($sidecarText -cne ($ExpectedContractSha256 + "`n")) {
            Throw-M0QtShellInvalidData 'M0QT011 contract sidecar differs from the external expected anchor.'
        }
        $contract = ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes (Read-M0QtShellTrustedBytes -TrustedFile $contractFile)
        $contractEntries = Assert-M0QtShellContractDocument -ContractDocument $contract
        try {
            foreach ($entry in $contractEntries) {
                $physical = [IO.Path]::GetFullPath((Join-Path $root ([string]$entry.path -replace '/', '\')))
                if (-not $physical.StartsWith($root + [IO.Path]::DirectorySeparatorChar, $script:M0QtShellPathComparison)) {
                    Throw-M0QtShellInvalidData 'M0QT011 contract leaf escaped RepositoryRoot.'
                }
                $handle = Open-M0QtShellTrustedFile -LiteralPath $physical -ExpectedSha256 ([string]$entry.sha256) -ExpectedSizeBytes ([long]$entry.size_bytes)
                $handles.Add($handle)
            }
        }
        catch { throw }
        $succeeded = $true
        return [pscustomobject]@{
            contract_sha256 = $ExpectedContractSha256
            document = $contract
            retained_files = if ($RetainHandles) { @($contractFile, $sidecarFile) + @($handles) } else { @() }
        }
    }
    finally {
        if (-not $RetainHandles -or -not $succeeded) {
            foreach ($handle in $handles) {
                try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $handle }
                finally { $handle.stream.Dispose() }
            }
            if ($null -ne $sidecarFile) {
                try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $sidecarFile }
                finally { $sidecarFile.stream.Dispose() }
            }
            try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $contractFile }
            finally { $contractFile.stream.Dispose() }
        }
    }
}

function Get-M0QtShellLicenseSize {
    param([Parameter(Mandatory)][object] $License)

    if ($null -ne $License.PSObject.Properties['text_size_bytes']) {
        return [long]$License.text_size_bytes
    }
    return $null
}

function ConvertTo-M0QtShellLicenseBinding {
    param([Parameter(Mandatory)][object] $License)

    $result = [ordered]@{}
    foreach ($property in $License.PSObject.Properties) {
        $result[$property.Name] = $property.Value
    }
    return $result
}

function Get-M0QtShellDescriptorBindings {
    param([Parameter(Mandatory)][object] $DescriptorDocument)

    return [ordered]@{
        qt_archives = @($DescriptorDocument.qt.archives | ForEach-Object {
            [ordered]@{
                id = $_.id
                file_name = $_.file_name
                sha1 = $_.sha1
                sha256 = $_.sha256
                size_bytes = [long]$_.size_bytes
                license = ConvertTo-M0QtShellLicenseBinding -License $_.license
            }
        })
        qt_tools = @($DescriptorDocument.qt.tools | ForEach-Object {
            [ordered]@{
                id = $_.id; version = $_.version; architecture = $_.architecture; provider = $_.provider
                path_base = $_.path_base; relative_path = $_.relative_path; sha256 = $_.sha256; size_bytes = [long]$_.size_bytes
                license = ConvertTo-M0QtShellLicenseBinding -License $_.license
            }
        })
        host_tools = @($DescriptorDocument.host.tools | ForEach-Object {
            [ordered]@{
                id = $_.id; version = $_.version; architecture = $_.architecture; provider = $_.provider
                path_base = $_.path_base; relative_path = $_.relative_path; sha256 = $_.sha256; size_bytes = [long]$_.size_bytes
                license = ConvertTo-M0QtShellLicenseBinding -License $_.license
            }
        })
        qt_artifacts = @($DescriptorDocument.qt.selected_artifacts | ForEach-Object {
            [ordered]@{ id = $_.id; relative_path = $_.relative_path; sha256 = $_.sha256; size_bytes = [long]$_.size_bytes }
        })
        stage_manifest = [ordered]@{
            schema_relative_path = $DescriptorDocument.qt.stage_manifest.schema_relative_path
            schema_sha256 = $DescriptorDocument.qt.stage_manifest.schema_sha256
            schema_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.schema_size_bytes
            relative_path = $DescriptorDocument.qt.stage_manifest.relative_path
            sha256 = $DescriptorDocument.qt.stage_manifest.sha256
            size_bytes = [long]$DescriptorDocument.qt.stage_manifest.size_bytes
            entry_count = [long]$DescriptorDocument.qt.stage_manifest.entry_count
            stage_total_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.stage_total_size_bytes
            deployment_metadata_leaf_count = [long]$DescriptorDocument.qt.stage_manifest.deployment_metadata_leaf_count
            deployment_metadata_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.deployment_metadata_size_bytes
            qml_leaf_count = [long]$DescriptorDocument.qt.stage_manifest.qml_leaf_count
            qml_total_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.qml_total_size_bytes
            qml_closure_tsv_sha256 = $DescriptorDocument.qt.stage_manifest.qml_closure_tsv_sha256
            qml_ordering = $DescriptorDocument.qt.stage_manifest.qml_ordering
            qmllint_plugin_disable_mode = $DescriptorDocument.qt.stage_manifest.qmllint_plugin_disable_mode
            qmllint_disabled_plugins = @($DescriptorDocument.qt.stage_manifest.qmllint_disabled_plugins)
            deployment_qml_scan = $DescriptorDocument.qt.stage_manifest.deployment_qml_scan
            runtime_smoke = $DescriptorDocument.qt.stage_manifest.runtime_smoke
            pe_leaf_count = [long]$DescriptorDocument.qt.stage_manifest.pe_leaf_count
            pe_total_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.pe_total_size_bytes
            pe_closure_tsv_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.pe_closure_tsv_size_bytes
            pe_closure_tsv_sha256 = $DescriptorDocument.qt.stage_manifest.pe_closure_tsv_sha256
            pe_internal_import_edge_count = [long]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edge_count
            pe_internal_import_edges_tsv_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edges_tsv_size_bytes
            pe_internal_import_edges_tsv_sha256 = $DescriptorDocument.qt.stage_manifest.pe_internal_import_edges_tsv_sha256
            pe_missing_internal_qt_import_count = [long]$DescriptorDocument.qt.stage_manifest.pe_missing_internal_qt_import_count
            pe_external_import_name_count = [long]$DescriptorDocument.qt.stage_manifest.pe_external_import_name_count
            pe_external_import_names_tsv_size_bytes = [long]$DescriptorDocument.qt.stage_manifest.pe_external_import_names_tsv_size_bytes
            pe_external_import_names_tsv_sha256 = $DescriptorDocument.qt.stage_manifest.pe_external_import_names_tsv_sha256
            pe_external_resolution_policy = $DescriptorDocument.qt.stage_manifest.pe_external_resolution_policy
            pe_api_set_alias_count = [long]$DescriptorDocument.qt.stage_manifest.pe_api_set_alias_count
            pe_concrete_external_name_count = [long]$DescriptorDocument.qt.stage_manifest.pe_concrete_external_name_count
            pe_concrete_external_system32_missing_count = [long]$DescriptorDocument.qt.stage_manifest.pe_concrete_external_system32_missing_count
        }
        catch2 = [ordered]@{
            id = $DescriptorDocument.catch2.id; version = $DescriptorDocument.catch2.version; commit_sha = $DescriptorDocument.catch2.commit_sha
            archive_sha256 = $DescriptorDocument.catch2.sha256; archive_size_bytes = [long]$DescriptorDocument.catch2.size_bytes
            license = ConvertTo-M0QtShellLicenseBinding -License $DescriptorDocument.catch2.license
        }
        nvda = [ordered]@{
            id = $DescriptorDocument.nvda.id; version = $DescriptorDocument.nvda.version
            installer_sha256 = $DescriptorDocument.nvda.sha256; installer_size_bytes = [long]$DescriptorDocument.nvda.size_bytes
            portable_executable_sha256 = $DescriptorDocument.nvda.portable_executable.sha256
            portable_executable_size_bytes = [long]$DescriptorDocument.nvda.portable_executable.size_bytes
            authenticode_subject = $DescriptorDocument.nvda.authenticode.subject
            authenticode_thumbprint = $DescriptorDocument.nvda.authenticode.thumbprint
            license = ConvertTo-M0QtShellLicenseBinding -License $DescriptorDocument.nvda.license
        }
        extractor = [ordered]@{
            id = $DescriptorDocument.extractor.id; version = $DescriptorDocument.extractor.version
            archive_sha256 = $DescriptorDocument.extractor.archive.sha256; archive_size_bytes = [long]$DescriptorDocument.extractor.archive.size_bytes
            executable_sha256 = $DescriptorDocument.extractor.sha256; executable_size_bytes = [long]$DescriptorDocument.extractor.size_bytes
            selected_members = @($DescriptorDocument.extractor.selected_members)
            excluded_components = @($DescriptorDocument.extractor.excluded_components)
            selected_executable_embedded_components = @($DescriptorDocument.extractor.selected_executable_embedded_components)
            selected_executable_contains_bsd_licensed_code = [bool]$DescriptorDocument.extractor.selected_executable_contains_bsd_licensed_code
            license = ConvertTo-M0QtShellLicenseBinding -License $DescriptorDocument.extractor.license
        }
    }
}

function ConvertTo-M0QtShellSemanticValue {
    param([AllowNull()][object] $Value)

    if ($null -eq $Value -or $Value -is [string] -or $Value -is [ValueType]) { return $Value }
    if ($Value -is [Collections.IDictionary]) {
        $result = [ordered]@{}
        foreach ($key in @($Value.Keys | ForEach-Object { [string]$_ } | Sort-Object -CaseSensitive)) {
            $result[$key] = ConvertTo-M0QtShellSemanticValue -Value $Value[$key]
        }
        return $result
    }
    if ($Value -is [Collections.IEnumerable] -and $Value -isnot [pscustomobject]) {
        return ,@($Value | ForEach-Object { ConvertTo-M0QtShellSemanticValue -Value $_ })
    }
    $objectResult = [ordered]@{}
    foreach ($property in @($Value.PSObject.Properties | Sort-Object Name -CaseSensitive)) {
        $objectResult[$property.Name] = ConvertTo-M0QtShellSemanticValue -Value $property.Value
    }
    return $objectResult
}

function Test-M0QtShellSemanticEqual {
    param([AllowNull()][object] $Actual, [AllowNull()][object] $Expected)

    $actualJson = ConvertTo-M0QtShellSemanticValue -Value $Actual | ConvertTo-Json -Compress -Depth 100
    $expectedJson = ConvertTo-M0QtShellSemanticValue -Value $Expected | ConvertTo-Json -Compress -Depth 100
    return $actualJson -ceq $expectedJson
}

function Assert-M0QtShellReceiptSemantics {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object] $ReceiptDocument,
        [Parameter(Mandatory)][object] $DescriptorDocument,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedContractSha256,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedDescriptorSha256
    )

    if ([string]$ReceiptDocument.artifact_role -cne 'live_receipt' -or -not [bool]$ReceiptDocument.live_evidence -or
        [string]$ReceiptDocument.contract_sha256 -cne $ExpectedContractSha256 -or
        [string]$ReceiptDocument.descriptor_sha256 -cne $ExpectedDescriptorSha256 -or
        [string]$ReceiptDocument.validation_mode -cne 'offline_inputs_only' -or
        [bool]$ReceiptDocument.network_allowed -ne $false -or
        [string]$ReceiptDocument.network_enforcement -cne 'not_enforced' -or
        [string]$ReceiptDocument.network_observation -cne 'not_measured') {
        Throw-M0QtShellInvalidData 'M0QT083 live receipt identity/contract binding is invalid.'
    }
    $utc = [string]$ReceiptDocument.generated_utc
    $parsedUtc = [DateTime]::MinValue
    if ($utc -notmatch '^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{7}Z$' -or
        -not [DateTime]::TryParseExact($utc, 'yyyy-MM-ddTHH:mm:ss.fffffffZ',
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal,
            [ref]$parsedUtc)) {
        Throw-M0QtShellInvalidData 'M0QT083 live receipt UTC is not a real canonical seven-fraction UTC instant.'
    }
    $expectedStates = [ordered]@{
        assets_provisioned = $true
        host_tools_observed = $true
        qml_tool_capabilities_validated = $true
        preflight_scope_satisfied = $true
        host_build_capability_validated = $false
        manual_validated = $false
        candidate_accepted = $false
        candidate_build_validated = $false
        acceptance_ready = $false
        staging_approved = $false
        package_approved = $false
        qt_network_approved = $false
        accessibility_passed = $false
    }
    $expectedDiagnostics = [ordered]@{
        qt_network_staging_probe = 'diagnostic-no-go-final-bundle'
        manual_nvda_journey = 'not-executed'
        candidate_build = 'not-executed'
    }
    $expectedCapabilities = [ordered]@{
        qmlformat = [ordered]@{ check_non_mutating = $true; dirty_fixture_changed = $true; second_pass_byte_stable = $true; canonical_lf_round_trip = $true }
        qmllint = [ordered]@{ valid_fixture_accepted = $true; invalid_fixture_rejected = $true }
        isolated_stage = [ordered]@{
            manifest_sha256 = [string]$DescriptorDocument.qt.stage_manifest.sha256
            entry_count = 162
            exact_inventory_validated = $true
            qml_closure_tsv_sha256 = [string]$DescriptorDocument.qt.stage_manifest.qml_closure_tsv_sha256
            pe_closure_tsv_sha256 = [string]$DescriptorDocument.qt.stage_manifest.pe_closure_tsv_sha256
            pe_internal_import_edges_tsv_sha256 = [string]$DescriptorDocument.qt.stage_manifest.pe_internal_import_edges_tsv_sha256
            pe_external_import_names_tsv_sha256 = [string]$DescriptorDocument.qt.stage_manifest.pe_external_import_names_tsv_sha256
            removed_after_probe = $true
        }
        qmltestrunner = [ordered]@{
            qml_models_loaded = $true; worker_script_loaded = $true; window_loaded = $true
            basic_controls_loaded = $true; shapes_loaded = $true; svg_image_loaded = $true
            qt_test_passed = $true
        }
        deployment = [ordered]@{
            probe_only = $true; staging_approved = $false
            bundle_policy_status = 'not-evaluated-by-tooling-preflight'
            dry_run_mapping_validated = $true; quick_import_scan_disabled = $true
            mapping_entry_count = 14
        }
        nvda = [ordered]@{ authenticode_valid = $true; manual_journey_validated = $false }
    }
    $expectedBindings = Get-M0QtShellDescriptorBindings -DescriptorDocument $DescriptorDocument
    if (-not (Test-M0QtShellSemanticEqual -Actual $ReceiptDocument.states -Expected $expectedStates) -or
        -not (Test-M0QtShellSemanticEqual -Actual $ReceiptDocument.diagnostics -Expected $expectedDiagnostics) -or
        -not (Test-M0QtShellSemanticEqual -Actual $ReceiptDocument.capabilities -Expected $expectedCapabilities) -or
        -not (Test-M0QtShellSemanticEqual -Actual $ReceiptDocument.observations -Expected $expectedBindings)) {
        Throw-M0QtShellInvalidData 'M0QT083 live receipt semantic binding differs from the anchored descriptor or physical success projection.'
    }
    return $true
}

function New-M0QtShellSyntheticGolden {
    [CmdletBinding()]
    param()

    $descriptorPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain.v1.json'
    $descriptorFile = Open-M0QtShellTrustedFile -LiteralPath $descriptorPath
    try {
        $descriptorBytes = Read-M0QtShellTrustedBytes -TrustedFile $descriptorFile
        $descriptor = ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes $descriptorBytes
        $descriptorSha256 = Get-M0QtShellSha256Bytes -Bytes $descriptorBytes
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $descriptorFile
    }
    finally { $descriptorFile.stream.Dispose() }
    $document = [ordered]@{
        '$schema' = 'qt-shell-toolchain-evidence.schema.json'
        schema_id = 'm0.qt-shell-toolchain-evidence.v1'
        schema_version = 1
        artifact_role = 'synthetic_golden'
        live_evidence = $false
        validation_mode = 'offline_inputs_only'
        network_allowed = $false
        network_enforcement = 'not_enforced'
        network_observation = 'not_measured'
        descriptor_sha256 = $descriptorSha256
        expected_states = [ordered]@{
            assets_provisioned = $true
            host_tools_observed = $true
            qml_tool_capabilities_validated = $true
            preflight_scope_satisfied = $true
            host_build_capability_validated = $false
            manual_validated = $false
            candidate_accepted = $false
            candidate_build_validated = $false
            acceptance_ready = $false
            staging_approved = $false
            package_approved = $false
            qt_network_approved = $false
            accessibility_passed = $false
        }
        diagnostics = [ordered]@{
            qt_network_staging_probe = 'diagnostic-no-go-final-bundle'
            manual_nvda_journey = 'not-executed'
            candidate_build = 'not-executed'
        }
        capabilities = [ordered]@{
            qmlformat = [ordered]@{ check_non_mutating = $true; dirty_fixture_changed = $true; second_pass_byte_stable = $true; canonical_lf_round_trip = $true }
            qmllint = [ordered]@{ valid_fixture_accepted = $true; invalid_fixture_rejected = $true }
            isolated_stage = [ordered]@{
                manifest_sha256 = [string]$descriptor.qt.stage_manifest.sha256
                entry_count = 162
                exact_inventory_validated = $true
                qml_closure_tsv_sha256 = [string]$descriptor.qt.stage_manifest.qml_closure_tsv_sha256
                pe_closure_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_closure_tsv_sha256
                pe_internal_import_edges_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_internal_import_edges_tsv_sha256
                pe_external_import_names_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_external_import_names_tsv_sha256
                removed_after_probe = $true
            }
            qmltestrunner = [ordered]@{
                qml_models_loaded = $true; worker_script_loaded = $true; window_loaded = $true
                basic_controls_loaded = $true; shapes_loaded = $true; svg_image_loaded = $true
                qt_test_passed = $true
            }
            deployment = [ordered]@{
                probe_only = $true; staging_approved = $false
                bundle_policy_status = 'not-evaluated-by-tooling-preflight'
                dry_run_mapping_validated = $true; quick_import_scan_disabled = $true
                mapping_entry_count = 14
            }
            nvda = [ordered]@{ authenticode_valid = $true; manual_journey_validated = $false }
        }
        expected_bindings = Get-M0QtShellDescriptorBindings -DescriptorDocument $descriptor
    }
    return ,(ConvertTo-M0QtShellCanonicalBytes -Document $document)
}

function Assert-M0QtShellHeldProbeFileUnchanged {
    param([Parameter(Mandatory)] $HeldFile)

    if ($null -eq $HeldFile.stream -or $null -eq $HeldFile.stream.SafeFileHandle -or
        $HeldFile.stream.SafeFileHandle.IsClosed -or $HeldFile.stream.SafeFileHandle.IsInvalid) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture handle is unavailable.'
    }
    if ([M0QtShellNativeV1.FileIdentity]::GetLinkCount($HeldFile.stream.SafeFileHandle) -ne 1 -or
        [M0QtShellNativeV1.FileIdentity]::GetIdentity($HeldFile.stream.SafeFileHandle) -cne [string]$HeldFile.identity) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture identity changed.'
    }
    $finalPath = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path (
        [M0QtShellNativeV1.FileIdentity]::GetFinalPath($HeldFile.stream.SafeFileHandle))))
    if (-not $finalPath.Equals([string]$HeldFile.final_path, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture final path changed.'
    }
    if ([long]$HeldFile.stream.Length -ne [long]$HeldFile.size_bytes) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture size changed.'
    }
}

function Read-M0QtShellHeldProbeBytes {
    param([Parameter(Mandatory)] $HeldFile)

    Assert-M0QtShellHeldProbeFileUnchanged -HeldFile $HeldFile
    if ([long]$HeldFile.size_bytes -gt [int]::MaxValue) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture exceeds the bounded reader.'
    }
    $HeldFile.stream.Position = 0
    $reader = [IO.BinaryReader]::new($HeldFile.stream, $script:M0QtShellUtf8, $true)
    try { $bytes = $reader.ReadBytes([int]$HeldFile.size_bytes) }
    finally { $reader.Dispose() }
    if ($bytes.Length -ne [int]$HeldFile.size_bytes -or $HeldFile.stream.ReadByte() -ne -1) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture readback was not exact.'
    }
    Assert-M0QtShellHeldProbeFileUnchanged -HeldFile $HeldFile
    if ((Get-M0QtShellSha256Bytes -Bytes $bytes) -cne [string]$HeldFile.sha256) {
        Throw-M0QtShellInvalidData 'M0QT020 a held capability fixture bytes changed.'
    }
    return ,$bytes
}

function New-M0QtShellHeldProbeFile {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]] $Bytes
    )

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    Assert-M0QtShellNoReparseAncestors -LiteralPath ([IO.Path]::GetDirectoryName($fullPath)) -IncludeLeaf
    $stream = $null
    $bridgeHandle = $null
    try {
        if (-not $IsWindows) {
            Throw-M0QtShellInvalidData 'M0QT020 retained capability fixtures are not proven outside Windows.'
        }
        $writerHandle = [M0QtShellNativeV1.FileIdentity]::CreateNewReadWriteDelete($fullPath)
        try {
            $stream = [IO.FileStream]::new($writerHandle, [IO.FileAccess]::ReadWrite, 4096, $false)
            $writerHandle = $null
        }
        finally { if ($null -ne $writerHandle) { $writerHandle.Dispose() } }
        if ($Bytes.Length -gt 0) { $stream.Write($Bytes, 0, $Bytes.Length) }
        $stream.Flush($true)
        $finalPath = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path (
            [M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
        if (-not $finalPath.Equals($fullPath, $script:M0QtShellPathComparison) -or
            [M0QtShellNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1) {
            Throw-M0QtShellInvalidData 'M0QT020 a capability fixture was not created at its literal single-link slot.'
        }
        $identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle)
        $bridgeHandle = [M0QtShellNativeV1.FileIdentity]::OpenReadShareAll($fullPath)
        $bridgeFinalPath = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path (
            [M0QtShellNativeV1.FileIdentity]::GetFinalPath($bridgeHandle))))
        if (-not $bridgeFinalPath.Equals($fullPath, $script:M0QtShellPathComparison) -or
            [M0QtShellNativeV1.FileIdentity]::GetIdentity($bridgeHandle) -cne $identity) {
            Throw-M0QtShellInvalidData 'M0QT020 a capability fixture bridge changed its literal identity.'
        }
        $stream.Dispose()
        $stream = $null
        $stream = [IO.FileStream]::new($fullPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        $readFinalPath = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path (
            [M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
        if (-not $readFinalPath.Equals($fullPath, $script:M0QtShellPathComparison) -or
            [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle) -cne $identity) {
            Throw-M0QtShellInvalidData 'M0QT020 a capability fixture retained reader changed its literal identity.'
        }
        $bridgeHandle.Dispose()
        $bridgeHandle = $null
        $held = [pscustomobject]@{
            stream = $stream
            final_path = $finalPath
            identity = $identity
            size_bytes = [long]$Bytes.Length
            sha256 = Get-M0QtShellSha256Bytes -Bytes $Bytes
        }
        Assert-M0QtShellHeldProbeFileUnchanged -HeldFile $held
        $stream = $null
        return $held
    }
    finally {
        if ($null -ne $bridgeHandle) { $bridgeHandle.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

function Assert-M0QtShellQmlFormatCapability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [AllowEmptyCollection()][string[]] $ArgumentPrefix = @(),
        [ValidateRange(1, 300)][int] $TimeoutSeconds = 30,
        [string] $TempRoot,
        [object] $QtStage,
        [object] $TrustedExecutable
    )

    $baseRoot = if ([string]::IsNullOrWhiteSpace($TempRoot)) { [IO.Path]::GetTempPath() } else { [IO.Path]::GetFullPath($TempRoot) }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $baseRoot -IncludeLeaf
    $probeRoot = Join-Path $baseRoot ('m0-qt-qmlformat-' + [Guid]::NewGuid().ToString('N'))
    $dirtyFile = $null
    $formattedFile = $null
    $probePins = @()
    try {
        [IO.Directory]::CreateDirectory($probeRoot) | Out-Null
        Assert-M0QtShellNoReparseAncestors -LiteralPath $probeRoot -IncludeLeaf
        $probePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $probeRoot)
        $dirtyPath = Join-Path $probeRoot 'dirty.qml'
        $formattedPath = Join-Path $probeRoot 'formatted.qml'
        $dirtyBytes = $script:M0QtShellUtf8.GetBytes("import QtQuick`nItem{property int value:1}`n")
        $dirtyFile = New-M0QtShellHeldProbeFile -LiteralPath $dirtyPath -Bytes $dirtyBytes
        $assertHeldCommand = Get-Command Assert-M0QtShellHeldProbeFileUnchanged -CommandType Function -ErrorAction Stop
        $assertStageCommand = if ($null -ne $QtStage) {
            Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
        }
        else { $null }
        $firstValidation = {
            & $assertHeldCommand -HeldFile $dirtyFile
            if ($null -ne $assertStageCommand) { & $assertStageCommand -Stage $QtStage | Out-Null }
        }.GetNewClosure()
        $firstParameters = @{
            FilePath = $FilePath
            ArgumentList = @($ArgumentPrefix + @($dirtyPath))
            TimeoutSeconds = $TimeoutSeconds
            MaximumCapturedBytes = 1048576
            WorkingDirectory = $probeRoot
            TempRoot = $probeRoot
            PinnedStateValidation = $firstValidation
        }
        if ($PSBoundParameters.ContainsKey('TrustedExecutable')) {
            $firstParameters.TrustedExecutable = $TrustedExecutable
        }
        if ($null -ne $QtStage) {
            $firstParameters.QtStageRoot = [string]$QtStage.root
            $firstParameters.TrustedDirectories = @($QtStage.parent_pins) + @($QtStage.directories) + @($probePins)
        }
        $first = Invoke-M0QtShellBoundedProcess @firstParameters
        $dirtyReadback = Read-M0QtShellHeldProbeBytes -HeldFile $dirtyFile
        if (-not [string]::IsNullOrEmpty($first.stderr)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat emitted unexpected stderr for the valid dirty fixture.'
        }
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$dirtyBytes, [byte[]]$dirtyReadback)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat mutated the dirty check fixture.'
        }
        if ([string]::IsNullOrEmpty($first.stdout)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat emitted no formatted bytes.'
        }
        $formattedText = $first.stdout.Replace("`r`n", "`n").Replace("`r", "`n")
        $formattedBytes = $script:M0QtShellUtf8.GetBytes($formattedText)
        if ([Linq.Enumerable]::SequenceEqual([byte[]]$dirtyBytes, [byte[]]$formattedBytes)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat did not change the dirty fixture.'
        }
        $formattedFile = New-M0QtShellHeldProbeFile -LiteralPath $formattedPath -Bytes $formattedBytes
        $secondValidation = {
            & $assertHeldCommand -HeldFile $formattedFile
            if ($null -ne $assertStageCommand) { & $assertStageCommand -Stage $QtStage | Out-Null }
        }.GetNewClosure()
        $secondParameters = @{}
        foreach ($entry in $firstParameters.GetEnumerator()) { $secondParameters[$entry.Key] = $entry.Value }
        $secondParameters.ArgumentList = @($ArgumentPrefix + @($formattedPath))
        $secondParameters.PinnedStateValidation = $secondValidation
        $second = Invoke-M0QtShellBoundedProcess @secondParameters
        $null = Read-M0QtShellHeldProbeBytes -HeldFile $formattedFile
        if (-not [string]::IsNullOrEmpty($second.stderr)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat emitted unexpected stderr for the canonical fixture.'
        }
        $secondBytes = $script:M0QtShellUtf8.GetBytes($second.stdout.Replace("`r`n", "`n").Replace("`r", "`n"))
        if (-not [Linq.Enumerable]::SequenceEqual([byte[]]$formattedBytes, [byte[]]$secondBytes)) {
            Throw-M0QtShellInvalidData 'M0QT080 qmlformat canonical second pass is not byte-stable.'
        }
        return [pscustomobject]@{ check_non_mutating = $true; dirty_fixture_changed = $true; second_pass_byte_stable = $true; canonical_lf_round_trip = $true }
    }
    finally {
        if ($null -ne $formattedFile -and $null -ne $formattedFile.stream) { $formattedFile.stream.Dispose() }
        if ($null -ne $dirtyFile -and $null -ne $dirtyFile.stream) { $dirtyFile.stream.Dispose() }
        if (@($probePins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $probePins }
        if ([IO.Directory]::Exists($probeRoot)) { Remove-Item -LiteralPath $probeRoot -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

function Assert-M0QtShellQmlLintCapability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [AllowEmptyCollection()][string[]] $ArgumentPrefix = @(),
        [ValidateRange(1, 300)][int] $TimeoutSeconds = 30,
        [string] $TempRoot,
        [object] $QtStage,
        [object] $TrustedExecutable
    )

    $baseRoot = if ([string]::IsNullOrWhiteSpace($TempRoot)) { [IO.Path]::GetTempPath() } else { [IO.Path]::GetFullPath($TempRoot) }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $baseRoot -IncludeLeaf
    $probeRoot = Join-Path $baseRoot ('m0-qt-qmllint-' + [Guid]::NewGuid().ToString('N'))
    $validFile = $null
    $invalidFile = $null
    $probePins = @()
    try {
        [IO.Directory]::CreateDirectory($probeRoot) | Out-Null
        Assert-M0QtShellNoReparseAncestors -LiteralPath $probeRoot -IncludeLeaf
        $probePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $probeRoot)
        $validPath = Join-Path $probeRoot 'valid.qml'
        $invalidPath = Join-Path $probeRoot 'invalid.qml'
        $validFile = New-M0QtShellHeldProbeFile -LiteralPath $validPath `
            -Bytes $script:M0QtShellUtf8.GetBytes("import QtQuick`nItem { property int value: 1 }`n")
        $invalidFile = New-M0QtShellHeldProbeFile -LiteralPath $invalidPath `
            -Bytes $script:M0QtShellUtf8.GetBytes("import QtQuick`nItem { property int value: }`n")
        $assertHeldCommand = Get-Command Assert-M0QtShellHeldProbeFileUnchanged -CommandType Function -ErrorAction Stop
        $assertStageCommand = if ($null -ne $QtStage) {
            Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
        }
        else { $null }
        $validValidation = {
            & $assertHeldCommand -HeldFile $validFile
            if ($null -ne $assertStageCommand) { & $assertStageCommand -Stage $QtStage | Out-Null }
        }.GetNewClosure()
        $validParameters = @{
            FilePath = $FilePath
            ArgumentList = @($ArgumentPrefix + @($validPath))
            TimeoutSeconds = $TimeoutSeconds
            MaximumCapturedBytes = 1048576
            AllowNonZeroExit = $true
            WorkingDirectory = $probeRoot
            TempRoot = $probeRoot
            PinnedStateValidation = $validValidation
        }
        if ($PSBoundParameters.ContainsKey('TrustedExecutable')) {
            $validParameters.TrustedExecutable = $TrustedExecutable
        }
        if ($null -ne $QtStage) {
            $validParameters.QtStageRoot = [string]$QtStage.root
            $validParameters.TrustedDirectories = @($QtStage.parent_pins) + @($QtStage.directories) + @($probePins)
        }
        $valid = Invoke-M0QtShellBoundedProcess @validParameters
        $invalidValidation = {
            & $assertHeldCommand -HeldFile $invalidFile
            if ($null -ne $assertStageCommand) { & $assertStageCommand -Stage $QtStage | Out-Null }
        }.GetNewClosure()
        $invalidParameters = @{}
        foreach ($entry in $validParameters.GetEnumerator()) { $invalidParameters[$entry.Key] = $entry.Value }
        $invalidParameters.ArgumentList = @($ArgumentPrefix + @($invalidPath))
        $invalidParameters.PinnedStateValidation = $invalidValidation
        $invalid = Invoke-M0QtShellBoundedProcess @invalidParameters
        $null = Read-M0QtShellHeldProbeBytes -HeldFile $validFile
        $null = Read-M0QtShellHeldProbeBytes -HeldFile $invalidFile
        if ($valid.exit_code -ne 0 -or -not [string]::IsNullOrEmpty($valid.stderr) -or
            $invalid.exit_code -eq 0 -or ($invalid.stdout + $invalid.stderr) -notmatch 'syntax|Expected token|error') {
            $diagnostic = [ordered]@{
                valid_exit = $valid.exit_code; valid_stdout = $valid.stdout; valid_stderr = $valid.stderr
                invalid_exit = $invalid.exit_code; invalid_stdout = $invalid.stdout; invalid_stderr = $invalid.stderr
            } | ConvertTo-Json -Compress
            Throw-M0QtShellInvalidData "M0QT081 qmllint did not accept the valid fixture and reject the invalid fixture for the expected syntax reason: $diagnostic"
        }
        return [pscustomobject]@{ valid_fixture_accepted = $true; invalid_fixture_rejected = $true }
    }
    finally {
        if ($null -ne $invalidFile -and $null -ne $invalidFile.stream) { $invalidFile.stream.Dispose() }
        if ($null -ne $validFile -and $null -ne $validFile.stream) { $validFile.stream.Dispose() }
        if (@($probePins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $probePins }
        if ([IO.Directory]::Exists($probeRoot)) { Remove-Item -LiteralPath $probeRoot -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

function Get-M0QtShellIsolatedStageTool {
    param(
        [Parameter(Mandatory)][object] $Stage,
        [Parameter(Mandatory)][string] $ToolId
    )

    if (-not $Stage.tool_paths.ContainsKey($ToolId)) {
        Throw-M0QtShellInvalidData "M0QT085 isolated stage tool '$ToolId' is absent."
    }
    $path = [string]$Stage.tool_paths[$ToolId]
    $matches = @($Stage.files | Where-Object {
            ([string]$_.final_path).Equals($path, $script:M0QtShellPathComparison)
        })
    if ($matches.Count -ne 1) {
        Throw-M0QtShellInvalidData "M0QT085 isolated stage tool '$ToolId' handle is missing or ambiguous."
    }
    return $matches[0]
}

function Assert-M0QtShellQmlTestCapability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object] $QtStage,
        [ValidateRange(1, 300)][int] $TimeoutSeconds = 30,
        [string] $TempRoot
    )

    $baseRoot = if ([string]::IsNullOrWhiteSpace($TempRoot)) {
        [IO.Path]::GetDirectoryName([string]$QtStage.root)
    }
    else { [IO.Path]::GetFullPath($TempRoot) }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $baseRoot -IncludeLeaf
    $probeRoot = Join-Path $baseRoot ('m0-qt-qmltest-' + [Guid]::NewGuid().ToString('N'))
    $fixture = $null
    $probePins = @()
    try {
        [IO.Directory]::CreateDirectory($probeRoot) | Out-Null
        Assert-M0QtShellNoReparseAncestors -LiteralPath $probeRoot -IncludeLeaf
        $probePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $probeRoot)
        $svg = '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1" fill="red"/></svg>'
        $svgBase64 = [Convert]::ToBase64String($script:M0QtShellUtf8.GetBytes($svg))
        $qmlText = @(
            'import QtQml.Models'
            'import QtQml.WorkerScript'
            'import QtQuick'
            'import QtQuick.Window'
            'import QtQuick.Controls'
            'import QtQuick.Shapes'
            'import QtTest'
            ''
            'TestCase {'
            '    name: "IsolatedQtClosureSmoke"'
            '    property ListModel listModel: ListModel { ListElement { name: "one" } }'
            '    property WorkerScript worker: WorkerScript {}'
            '    property Window hiddenWindow: Window { visible: false }'
            '    property Button basicButton: Button {}'
            '    property Shape quickShape: Shape {}'
            "    property Image svgImage: Image { asynchronous: false; source: `"data:image/svg+xml;base64,$svgBase64`" }"
            ''
            '    function test_imports() {'
            '        compare(listModel.count, 1)'
            '        verify(worker !== null)'
            '        verify(hiddenWindow !== null)'
            '        verify(basicButton !== null)'
            '        verify(quickShape !== null)'
            '        tryCompare(svgImage, "status", Image.Ready)'
            '    }'
            '}'
            ''
        ) -join "`n"
        $fixturePath = Join-Path $probeRoot 'tst_isolated_qt_closure.qml'
        $fixture = New-M0QtShellHeldProbeFile -LiteralPath $fixturePath `
            -Bytes $script:M0QtShellUtf8.GetBytes($qmlText)
        $trustedTool = Get-M0QtShellIsolatedStageTool -Stage $QtStage -ToolId 'qmltestrunner'
        $assertHeldCommand = Get-Command Assert-M0QtShellHeldProbeFileUnchanged -CommandType Function -ErrorAction Stop
        $assertStageCommand = Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
        $validation = {
            & $assertHeldCommand -HeldFile $fixture
            & $assertStageCommand -Stage $QtStage | Out-Null
        }.GetNewClosure()
        $result = Invoke-M0QtShellBoundedProcess -FilePath $trustedTool.final_path `
            -ArgumentList @('-nocrashhandler', '-input', $fixturePath,
                '-import', (Join-Path ([string]$QtStage.root) 'qml'),
                '-plugins', (Join-Path ([string]$QtStage.root) 'plugins'),
                '-o', '-,tap') `
            -TimeoutSeconds $TimeoutSeconds -MaximumCapturedBytes 1048576 `
            -WorkingDirectory $probeRoot -TempRoot $probeRoot `
            -QtStageRoot ([string]$QtStage.root) -PinnedStateValidation $validation `
            -TrustedExecutable $trustedTool `
            -TrustedDirectories (@($QtStage.parent_pins) + @($QtStage.directories) + @($probePins))
        $null = Read-M0QtShellHeldProbeBytes -HeldFile $fixture
        $tap = $result.stdout.Replace("`r`n", "`n").Replace("`r", "`n")
        $expectedTapLines = @(
            'TAP version 13',
            '# qmltestrunner',
            'ok 1 - IsolatedQtClosureSmoke::initTestCase()',
            'ok 2 - IsolatedQtClosureSmoke::test_imports()',
            'ok 3 - IsolatedQtClosureSmoke::cleanupTestCase()',
            '1..3',
            '# tests 3',
            '# pass 3',
            '# fail 0'
        )
        $actualTapLines = @($tap.TrimEnd("`n") -split "`n")
        if ($result.exit_code -ne 0 -or -not [string]::IsNullOrEmpty($result.stderr) -or
            ($actualTapLines | ConvertTo-Json -Compress) -cne ($expectedTapLines | ConvertTo-Json -Compress)) {
            Throw-M0QtShellInvalidData 'M0QT087 qmltestrunner did not prove the exact isolated QML runtime smoke.'
        }
        return [pscustomobject][ordered]@{
            qml_models_loaded = $true
            worker_script_loaded = $true
            window_loaded = $true
            basic_controls_loaded = $true
            shapes_loaded = $true
            svg_image_loaded = $true
            qt_test_passed = $true
        }
    }
    finally {
        if ($null -ne $fixture -and $null -ne $fixture.stream) { $fixture.stream.Dispose() }
        if (@($probePins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $probePins }
        if ([IO.Directory]::Exists($probeRoot)) {
            Remove-Item -LiteralPath $probeRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Assert-M0QtShellDeploymentCapability {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][object] $QtStage,
        [ValidateRange(1, 300)][int] $TimeoutSeconds = 30,
        [string] $TempRoot
    )

    $stageRoot = [IO.Path]::GetFullPath([string]$QtStage.root)
    $deployTool = Get-M0QtShellIsolatedStageTool -Stage $QtStage -ToolId 'windeployqt'
    $testTool = Get-M0QtShellIsolatedStageTool -Stage $QtStage -ToolId 'qmltestrunner'
    $qtPathsTool = @($QtStage.files | Where-Object {
            [string]$_.stage_relative_path -ceq 'bin/qtpaths.exe'
        })
    if ($qtPathsTool.Count -ne 1) {
        Throw-M0QtShellInvalidData 'M0QT088 staged qtpaths handle is missing or ambiguous.'
    }
    $assertStageCommand = Get-Command Assert-M0QtShellIsolatedStage -CommandType Function -ErrorAction Stop
    $validation = { & $assertStageCommand -Stage $QtStage | Out-Null }.GetNewClosure()
    $result = Invoke-M0QtShellBoundedProcess -FilePath $deployTool.final_path `
        -ArgumentList @('--dry-run', '--list', 'mapping', '--no-quick-import', '--no-translations',
            '--no-compiler-runtime', '--no-system-dxc-compiler', '--no-opengl-sw',
            '--no-system-d3d-compiler', '--qtpaths', $qtPathsTool[0].final_path, $testTool.final_path) `
        -TimeoutSeconds $TimeoutSeconds -MaximumCapturedBytes 1048576 `
        -WorkingDirectory $stageRoot -TempRoot $TempRoot `
        -QtStageRoot $stageRoot -PinnedStateValidation $validation `
        -TrustedExecutable $deployTool `
        -TrustedDirectories (@($QtStage.parent_pins) + @($QtStage.directories))
    $destinations = @(
        'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6OpenGL.dll', 'Qt6Qml.dll',
        'Qt6QmlMeta.dll', 'Qt6QmlModels.dll', 'Qt6QmlWorkerScript.dll', 'Qt6Quick.dll',
        'Qt6QuickTest.dll', 'Qt6Svg.dll', 'Qt6Test.dll', 'imageformats\qsvg.dll',
        'platforms\qwindows.dll'
    )
    $sources = @(
        'bin\Qt6Core.dll', 'bin\Qt6Gui.dll', 'bin\Qt6Network.dll', 'bin\Qt6OpenGL.dll',
        'bin\Qt6Qml.dll', 'bin\Qt6QmlMeta.dll', 'bin\Qt6QmlModels.dll',
        'bin\Qt6QmlWorkerScript.dll', 'bin\Qt6Quick.dll', 'bin\Qt6QuickTest.dll',
        'bin\Qt6Svg.dll', 'bin\Qt6Test.dll', 'plugins\imageformats\qsvg.dll',
        'plugins\platforms\qwindows.dll'
    )
    $expectedLines = @(
        for ($index = 0; $index -lt $sources.Count; $index++) {
            '"' + (Join-Path $stageRoot $sources[$index]) + '" "' + $destinations[$index] + '"'
        }
    )
    $mapping = $result.stdout.Replace("`r`n", "`n").Replace("`r", "`n")
    $actualLines = @($mapping.TrimEnd("`n") -split "`n")
    if ($result.exit_code -ne 0 -or -not [string]::IsNullOrEmpty($result.stderr) -or
        ($actualLines | ConvertTo-Json -Compress) -cne ($expectedLines | ConvertTo-Json -Compress)) {
        Throw-M0QtShellInvalidData 'M0QT088 windeployqt dry-run mapping differs from the exact isolated 14-leaf policy.'
    }
    return [pscustomobject][ordered]@{
        probe_only = $true
        staging_approved = $false
        bundle_policy_status = 'not-evaluated-by-tooling-preflight'
        dry_run_mapping_validated = $true
        quick_import_scan_disabled = $true
        mapping_entry_count = 14
    }
}

function Resolve-M0QtShellPhysicalPath {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $PathBase,
        [Parameter(Mandatory)][string] $RelativePath
    )

    if ($RelativePath -notmatch '^[A-Za-z0-9_. /-]+$' -or $RelativePath -match '(^|/)\.\.(/|$)' -or $RelativePath.Contains('\')) {
        Throw-M0QtShellInvalidData 'M0QT023 a descriptor physical slot contains an unsafe relative path.'
    }
    $base = switch -CaseSensitive ($PathBase) {
        'repository_root' { $RepositoryRoot }
        'qt_toolchain_root' { Join-Path $RepositoryRoot 'out\m0\toolchains\qt-6.11.2-msvc2022_64' }
        'program_files' { [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles) }
        'vs2022_community' { Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)) 'Microsoft Visual Studio\2022\Community' }
        default { Throw-M0QtShellInvalidData "M0QT023 unknown descriptor path base '$PathBase'." }
    }
    $fullBase = [IO.Path]::GetFullPath($base)
    $full = [IO.Path]::GetFullPath((Join-Path $fullBase ($RelativePath -replace '/', '\')))
    if (-not $full.StartsWith($fullBase + [IO.Path]::DirectorySeparatorChar, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT023 physical slot escaped its pinned root.'
    }
    return $full
}

function Test-M0QtShellExpectedFile {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][string] $ExpectedSha256,
        [Parameter(Mandatory)][long] $ExpectedSizeBytes,
        [Parameter(Mandatory)][string] $Id,
        [Collections.Generic.List[object]] $RetainedFiles
    )

    $trusted = Open-M0QtShellTrustedFile -LiteralPath $LiteralPath -ExpectedSha256 $ExpectedSha256 -ExpectedSizeBytes $ExpectedSizeBytes
    if ($null -ne $RetainedFiles) { $RetainedFiles.Add($trusted) }
    try { return [ordered]@{ id = $Id; sha256 = $trusted.sha256; size_bytes = [long]$trusted.size_bytes } }
    finally {
        if ($null -eq $RetainedFiles) {
            try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted }
            finally { $trusted.stream.Dispose() }
        }
    }
}

function Get-M0QtShellRetainedFile {
    param(
        [Parameter(Mandatory)][Collections.Generic.List[object]] $Files,
        [Parameter(Mandatory)][string] $LiteralPath
    )

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    $matches = @($Files | Where-Object {
            ([string]$_.final_path).Equals($fullPath, $script:M0QtShellPathComparison)
        })
    if ($matches.Count -ne 1) {
        Throw-M0QtShellInvalidData 'M0QT021 retained file handle is missing or ambiguous.'
    }
    return $matches[0]
}

function Assert-M0QtShellIsolatedStage {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Stage)

    if (-not $IsWindows) {
        Throw-M0QtShellInvalidData 'M0QT085 isolated stage identity validation is not proven outside Windows.'
    }
    $root = [IO.Path]::GetFullPath([string]$Stage.root)
    Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories @($Stage.parent_pins)
    Assert-M0QtShellDirectoryTreeNoReparse -Root $root

    foreach ($directory in @($Stage.directories)) {
        if ($null -eq $directory.handle -or $directory.handle.IsClosed -or $directory.handle.IsInvalid -or
            [M0QtShellNativeV1.FileIdentity]::GetIdentity($directory.handle) -cne [string]$directory.identity) {
            Throw-M0QtShellInvalidData 'M0QT085 a retained stage directory identity changed.'
        }
        $directoryFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($directory.handle))))
        if (-not $directoryFinal.Equals([string]$directory.final_path, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT085 a retained stage directory final path changed.'
        }
    }
    foreach ($file in @($Stage.files)) {
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $file
    }

    $expectedFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in @($Stage.expected_paths)) { $null = $expectedFiles.Add([string]$relative) }
    $actualFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in @(Get-ChildItem -LiteralPath $root -File -Recurse -Force -ErrorAction Stop)) {
        $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
        if (-not $actualFiles.Add($relative)) {
            Throw-M0QtShellInvalidData 'M0QT085 isolated stage inventory contains a duplicate or case-aliased leaf.'
        }
    }
    if ($actualFiles.Count -ne $expectedFiles.Count) {
        Throw-M0QtShellInvalidData 'M0QT085 isolated stage inventory contains an extra or missing leaf.'
    }
    foreach ($relative in $expectedFiles) {
        if (-not $actualFiles.Contains($relative)) {
            Throw-M0QtShellInvalidData 'M0QT085 isolated stage inventory differs from the exact manifest leaves.'
        }
    }

    $expectedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in @($Stage.expected_directories)) { $null = $expectedDirectories.Add([string]$relative) }
    $actualDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in @(Get-ChildItem -LiteralPath $root -Directory -Recurse -Force -ErrorAction Stop)) {
        $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
        if (-not $actualDirectories.Add($relative)) {
            Throw-M0QtShellInvalidData 'M0QT085 isolated stage inventory contains a duplicate or case-aliased directory.'
        }
    }
    if ($actualDirectories.Count -ne $expectedDirectories.Count) {
        Throw-M0QtShellInvalidData 'M0QT085 isolated stage directory inventory contains an extra or missing entry.'
    }
    foreach ($relative in $expectedDirectories) {
        if (-not $actualDirectories.Contains($relative)) {
            Throw-M0QtShellInvalidData 'M0QT085 isolated stage directory inventory differs from the exact manifest closure.'
        }
    }
    Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories @($Stage.parent_pins)
    foreach ($directory in @($Stage.directories)) {
        if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($directory.handle) -cne [string]$directory.identity) {
            Throw-M0QtShellInvalidData 'M0QT085 a retained stage directory changed during inventory enumeration.'
        }
        $directoryFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($directory.handle))))
        if (-not $directoryFinal.Equals([string]$directory.final_path, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT085 a retained stage directory path changed during inventory enumeration.'
        }
    }
    foreach ($file in @($Stage.files)) { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $file }
    return $true
}

function Close-M0QtShellIsolatedStage {
    [CmdletBinding()]
    param([Parameter(Mandatory)][object] $Stage)

    $cleanupFailure = $null
    foreach ($file in @($Stage.files)) {
        $bridgeHandle = $null
        $deleteHandle = $null
        try {
            Assert-M0QtShellTrustedFileUnchanged -TrustedFile $file
            $bridgeHandle = [M0QtShellNativeV1.FileIdentity]::OpenReadShareAll([string]$file.final_path)
            $bridgeFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($bridgeHandle))))
            if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($bridgeHandle) -cne [string]$file.identity -or
                -not $bridgeFinal.Equals([string]$file.final_path, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT086 a stage cleanup bridge changed leaf identity.'
            }
            $file.stream.Dispose()
            $deleteHandle = [M0QtShellNativeV1.FileIdentity]::OpenReadDelete([string]$file.final_path)
            $deleteFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($deleteHandle))))
            if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($deleteHandle) -cne [string]$file.identity -or
                -not $deleteFinal.Equals([string]$file.final_path, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT086 a stage cleanup delete handle changed leaf identity.'
            }
            [M0QtShellNativeV1.FileIdentity]::MarkDeleteOnClose($deleteHandle)
        }
        catch { if ($null -eq $cleanupFailure) { $cleanupFailure = $_ } }
        finally {
            try { if (-not $file.stream.SafeFileHandle.IsClosed) { $file.stream.Dispose() } } catch { }
            if ($null -ne $deleteHandle) { try { $deleteHandle.Dispose() } catch { } }
            if ($null -ne $bridgeHandle) { try { $bridgeHandle.Dispose() } catch { } }
        }
    }
    $directories = @($Stage.directories)
    for ($index = $directories.Count - 1; $index -ge 0; $index--) {
        $directory = $directories[$index]
        try {
            if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($directory.handle) -cne [string]$directory.identity) {
                Throw-M0QtShellInvalidData 'M0QT086 a retained stage directory identity changed during cleanup.'
            }
            $directoryFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($directory.handle))))
            if (-not $directoryFinal.Equals([string]$directory.final_path, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT086 a retained stage directory final path changed during cleanup.'
            }
            [M0QtShellNativeV1.FileIdentity]::MarkDeleteOnClose($directory.handle)
        }
        catch { if ($null -eq $cleanupFailure) { $cleanupFailure = $_ } }
        finally { try { $directory.handle.Dispose() } catch { } }
    }
    try { Close-M0QtShellPinnedDirectories -PinnedDirectories @($Stage.parent_pins) }
    catch { if ($null -eq $cleanupFailure) { $cleanupFailure = $_ } }
    if ($null -ne $cleanupFailure) {
        throw [IO.InvalidDataException]::new(
            'M0QT086 isolated stage retained-handle cleanup failed.',
            $cleanupFailure.Exception
        )
    }
}

function New-M0QtShellIsolatedStage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][object] $DescriptorDocument,
        [Parameter(Mandatory)][object] $ManifestDocument,
        [Parameter(Mandatory)][string] $StageParent
    )

    if (-not $IsWindows) {
        Throw-M0QtShellInvalidData 'M0QT085 isolated stage materialization is not proven outside Windows.'
    }
    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    $parent = [IO.Path]::GetFullPath($StageParent)
    if (-not [IO.Directory]::Exists($parent)) {
        Throw-M0QtShellInvalidData 'M0QT085 isolated stage parent must already exist.'
    }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $parent -IncludeLeaf
    Assert-M0QtShellStageManifestDocument -ManifestDocument $ManifestDocument `
        -DescriptorDocument $DescriptorDocument | Out-Null

    $stageRoot = Join-Path $parent ('m0-qt-isolated-' + [Guid]::NewGuid().ToString('N'))
    $files = [Collections.Generic.List[object]]::new()
    $directories = [Collections.Generic.List[object]]::new()
    $parentPins = @()
    $sourcePins = @()
    $toolPaths = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::Ordinal)
    $expectedPaths = @($ManifestDocument.entries | ForEach-Object { [string]$_.stage_relative_path })
    $expectedDirectorySet = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($relative in $expectedPaths) {
        $directory = [IO.Path]::GetDirectoryName($relative.Replace('/', '\'))
        while (-not [string]::IsNullOrEmpty($directory)) {
            $normalized = $directory.Replace('\', '/')
            $null = $expectedDirectorySet.Add($normalized)
            $directory = [IO.Path]::GetDirectoryName($directory)
        }
    }
    $expectedDirectories = @($expectedDirectorySet | Sort-Object `
        @{ Expression = { @($_ -split '/').Count } },
        @{ Expression = { $_.ToLowerInvariant() } })
    $stage = [pscustomobject]@{
        root = $stageRoot
        files = $files
        directories = $directories
        parent_pins = $parentPins
        expected_paths = $expectedPaths
        expected_directories = $expectedDirectories
        tool_paths = $toolPaths
    }
    $completed = $false
    try {
        $parentPins = @(Open-M0QtShellPinnedDirectories -LiteralPath $parent)
        $stage.parent_pins = $parentPins
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $parentPins

        $rootHandle = [M0QtShellNativeV1.FileIdentity]::CreateNewDirectoryReadDeleteWithoutDeleteSharing($stageRoot)
        $rootFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($rootHandle))))
        if (-not $rootFinal.Equals($stageRoot, $script:M0QtShellPathComparison)) {
            $rootHandle.Dispose()
            Throw-M0QtShellInvalidData 'M0QT085 isolated stage root handle resolved to a different path.'
        }
        $null = $directories.Add([pscustomobject]@{
                handle = $rootHandle
                final_path = $rootFinal
                identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($rootHandle)
                stage_relative_path = ''
            })
        foreach ($relativeDirectory in $expectedDirectories) {
            $physicalDirectory = [IO.Path]::GetFullPath((Join-Path $stageRoot ($relativeDirectory -replace '/', '\')))
            if (-not $physicalDirectory.StartsWith($stageRoot + [IO.Path]::DirectorySeparatorChar,
                    $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT085 isolated stage directory escaped its owned root.'
            }
            $directoryHandle = [M0QtShellNativeV1.FileIdentity]::CreateNewDirectoryReadDeleteWithoutDeleteSharing($physicalDirectory)
            $directoryFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($directoryHandle))))
            if (-not $directoryFinal.Equals($physicalDirectory, $script:M0QtShellPathComparison)) {
                $directoryHandle.Dispose()
                Throw-M0QtShellInvalidData 'M0QT085 isolated stage child directory resolved to a different path.'
            }
            $null = $directories.Add([pscustomobject]@{
                    handle = $directoryHandle
                    final_path = $directoryFinal
                    identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($directoryHandle)
                    stage_relative_path = $relativeDirectory
                })
        }

        $qtRoot = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase 'repository_root' `
            -RelativePath ([string]$DescriptorDocument.path_roots.qt_toolchain_root)
        $sourcePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $qtRoot)
        $toolById = @{}
        foreach ($tool in @($DescriptorDocument.qt.tools)) { $toolById[[string]$tool.id] = $tool }
        $artifactById = @{}
        foreach ($artifact in @($DescriptorDocument.qt.selected_artifacts)) { $artifactById[[string]$artifact.id] = $artifact }

        foreach ($entry in @($ManifestDocument.entries)) {
            $relativePath = [string]$entry.stage_relative_path
            $destinationPath = [IO.Path]::GetFullPath((Join-Path $stageRoot ($relativePath -replace '/', '\')))
            if (-not $destinationPath.StartsWith($stageRoot + [IO.Path]::DirectorySeparatorChar,
                    $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT085 isolated stage leaf escaped its owned root.'
            }
            $expectedSha256 = $null
            $expectedSizeBytes = 0L
            $sourceRelativePath = $null
            switch -CaseSensitive ([string]$entry.source_kind) {
                'qt_tool' {
                    $source = $toolById[[string]$entry.source_id]
                    $sourceRelativePath = [string]$source.relative_path
                    $expectedSha256 = [string]$source.sha256
                    $expectedSizeBytes = [long]$source.size_bytes
                }
                'selected_artifact' {
                    $source = $artifactById[[string]$entry.source_id]
                    $sourceRelativePath = [string]$source.relative_path
                    $expectedSha256 = [string]$source.sha256
                    $expectedSizeBytes = [long]$source.size_bytes
                }
                'qt_file' {
                    $sourceRelativePath = [string]$entry.source_relative_path
                    $expectedSha256 = [string]$entry.sha256
                    $expectedSizeBytes = [long]$entry.size_bytes
                }
                'inline_utf8' {
                    $expectedSha256 = [string]$entry.sha256
                    $expectedSizeBytes = [long]$entry.size_bytes
                }
                default { Throw-M0QtShellInvalidData 'M0QT085 isolated stage source kind is unsupported.' }
            }

            $destinationStream = $null
            $destinationRetained = $false
            $sourceFile = $null
            try {
                $destinationHandle = [M0QtShellNativeV1.FileIdentity]::CreateNewReadWriteDelete($destinationPath)
                try {
                    $destinationStream = [IO.FileStream]::new($destinationHandle, [IO.FileAccess]::ReadWrite, 4096, $false)
                    $destinationHandle = $null
                }
                finally { if ($null -ne $destinationHandle) { $destinationHandle.Dispose() } }

                if ([string]$entry.source_kind -ceq 'inline_utf8') {
                    $inlineBytes = [Convert]::FromBase64String([string]$entry.content_base64)
                    $destinationStream.Write($inlineBytes, 0, $inlineBytes.Length)
                }
                else {
                    $sourcePath = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root `
                        -PathBase 'qt_toolchain_root' -RelativePath $sourceRelativePath
                    $sourceFile = Open-M0QtShellTrustedFile -LiteralPath $sourcePath `
                        -ExpectedSha256 $expectedSha256 -ExpectedSizeBytes $expectedSizeBytes
                    Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $sourcePins
                    Assert-M0QtShellTrustedFileUnchanged -TrustedFile $sourceFile
                    $sourceFile.stream.Position = 0
                    $sourceFile.stream.CopyTo($destinationStream)
                    Assert-M0QtShellTrustedFileUnchanged -TrustedFile $sourceFile
                }
                $destinationStream.Flush($true)
                if ($destinationStream.Length -ne $expectedSizeBytes -or
                    (Get-M0QtShellSha256FromStream -Stream $destinationStream) -cne $expectedSha256 -or
                    [M0QtShellNativeV1.FileIdentity]::GetLinkCount($destinationStream.SafeFileHandle) -ne 1) {
                    Throw-M0QtShellInvalidData 'M0QT085 isolated stage leaf bytes or link identity drifted during copy.'
                }
                $destinationFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                    ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($destinationStream.SafeFileHandle))))
                if (-not $destinationFinal.Equals($destinationPath, $script:M0QtShellPathComparison)) {
                    Throw-M0QtShellInvalidData 'M0QT085 isolated stage leaf final path differs after copy.'
                }
                $destinationIdentity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($destinationStream.SafeFileHandle)
                $bridgeHandle = [M0QtShellNativeV1.FileIdentity]::OpenReadShareAll($destinationPath)
                try {
                    $bridgeFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                        ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($bridgeHandle))))
                    if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($bridgeHandle) -cne $destinationIdentity -or
                        -not $bridgeFinal.Equals($destinationFinal, $script:M0QtShellPathComparison)) {
                        Throw-M0QtShellInvalidData 'M0QT085 isolated stage bridge handle changed leaf identity.'
                    }
                    $destinationStream.Dispose()
                    $destinationStream = $null
                    $readStream = $null
                    try {
                        $readStream = [IO.FileStream]::new(
                            $destinationPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
                        $readDeleteFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($readStream.SafeFileHandle))))
                        if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($readStream.SafeFileHandle) -cne $destinationIdentity -or
                            -not $readDeleteFinal.Equals($destinationFinal, $script:M0QtShellPathComparison)) {
                            Throw-M0QtShellInvalidData 'M0QT085 isolated stage retained handle changed leaf identity.'
                        }
                        $destinationStream = $readStream
                        $readStream = $null
                    }
                    finally {
                        if ($null -ne $readStream) { $readStream.Dispose() }
                    }
                }
                finally { $bridgeHandle.Dispose() }
                $retainedFile = [pscustomobject]@{
                    stream = $destinationStream
                    sha256 = $expectedSha256
                    size_bytes = $expectedSizeBytes
                    final_path = $destinationFinal
                    identity = $destinationIdentity
                    stage_relative_path = $relativePath
                }
                Assert-M0QtShellTrustedFileUnchanged -TrustedFile $retainedFile
                $null = $files.Add($retainedFile)
                if ([string]$entry.source_kind -ceq 'qt_tool') {
                    $toolPaths[[string]$entry.source_id] = $destinationFinal
                }
                $destinationRetained = $true
                $destinationStream = $null
            }
            finally {
                if ($null -ne $sourceFile) {
                    try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $sourceFile }
                    finally { $sourceFile.stream.Dispose() }
                }
                if (-not $destinationRetained -and $null -ne $destinationStream) {
                    try { [M0QtShellNativeV1.FileIdentity]::MarkDeleteOnClose($destinationStream.SafeFileHandle) }
                    finally { $destinationStream.Dispose() }
                }
            }
        }
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $sourcePins
        Assert-M0QtShellIsolatedStage -Stage $stage | Out-Null
        $completed = $true
        return $stage
    }
    finally {
        if (@($sourcePins).Count -gt 0) {
            Close-M0QtShellPinnedDirectories -PinnedDirectories $sourcePins
        }
        if (-not $completed) {
            try { Close-M0QtShellIsolatedStage -Stage $stage }
            catch {
                throw [IO.InvalidDataException]::new(
                    'M0QT086 isolated stage construction cleanup failed.',
                    $_.Exception
                )
            }
        }
    }
}

function Close-M0QtShellRetainedFiles {
    param([Parameter(Mandatory)][Collections.Generic.List[object]] $Files)

    $failure = $null
    foreach ($file in $Files) {
        try {
            Assert-M0QtShellTrustedFileUnchanged -TrustedFile $file
        }
        catch { if ($null -eq $failure) { $failure = $_ } }
        finally { try { $file.stream.Dispose() } catch { } }
    }
    if ($null -ne $failure) { throw $failure }
}

function Test-M0QtShellVersionInfo {
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][string] $ExpectedVersion,
        [object] $TrustedFile,
        [AllowEmptyCollection()][object[]] $TrustedDirectories
    )

    $fullPath = [IO.Path]::GetFullPath($LiteralPath)
    $ownsTrusted = -not $PSBoundParameters.ContainsKey('TrustedFile')
    $trusted = if ($ownsTrusted) {
        Open-M0QtShellTrustedFile -LiteralPath $fullPath
    }
    else {
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $TrustedFile
        if (-not ([string]$TrustedFile.final_path).Equals($fullPath, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT060 retained version leaf differs from the requested literal path.'
        }
        $TrustedFile
    }
    $pins = @()
    $ownsPins = -not $PSBoundParameters.ContainsKey('TrustedDirectories')
    try {
        $pins = if ($ownsPins) {
            @(Open-M0QtShellPinnedDirectories -LiteralPath $fullPath)
        }
        else { @($TrustedDirectories) }
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pins
        $version = [Diagnostics.FileVersionInfo]::GetVersionInfo($fullPath).FileVersion
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $trusted
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $pins
        if ($version -cne $ExpectedVersion) {
            Throw-M0QtShellInvalidData "M0QT060 executable version drifted from '$ExpectedVersion'."
        }
    }
    finally {
        if ($ownsPins -and @($pins).Count -gt 0) {
            Close-M0QtShellPinnedDirectories -PinnedDirectories $pins
        }
        if ($ownsTrusted) { $trusted.stream.Dispose() }
    }
}

function Publish-M0QtShellReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $LiteralPath,
        [Parameter(Mandatory)][AllowEmptyCollection()][byte[]] $Bytes,
        [scriptblock] $AfterTempFlush,
        [scriptblock] $AfterAtomicMove
    )

    if (-not $IsWindows) {
        Throw-M0QtShellInvalidData 'M0QT070 contained receipt publication is not proven outside Windows.'
    }
    Assert-M0QtShellSafePathText -LiteralPath $LiteralPath
    $final = [IO.Path]::GetFullPath($LiteralPath)
    $parent = [IO.Path]::GetDirectoryName($final)
    if (-not [IO.Directory]::Exists($parent)) { Throw-M0QtShellInvalidData 'M0QT070 receipt parent must already exist.' }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $parent -IncludeLeaf
    if ([IO.File]::Exists($final)) { Throw-M0QtShellInvalidData 'M0QT070 receipt already exists; CreateNew/no overwrite is required.' }
    $temp = Join-Path $parent ('.' + [IO.Path]::GetFileName($final) + '.' + [Guid]::NewGuid().ToString('N') + '.pending')
    $published = $false
    $moved = $false
    $stream = $null
    $parentHandle = $null
    $parentIdentity = $null
    try {
        $parentHandle = [M0QtShellNativeV1.FileIdentity]::OpenDirectoryWithoutDeleteSharing($parent)
        $parentIdentity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($parentHandle)
        $heldParentFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
            ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($parentHandle))))
        if (-not $heldParentFinal.Equals($parent, $script:M0QtShellPathComparison)) {
            Throw-M0QtShellInvalidData 'M0QT070 receipt parent handle resolved to a different directory identity.'
        }
        $safeHandle = [M0QtShellNativeV1.FileIdentity]::CreateNewReadWriteDelete($temp)
        $stream = [IO.FileStream]::new($safeHandle, [IO.FileAccess]::ReadWrite, 4096, $false)
        Assert-M0QtShellNoReparseAncestors -LiteralPath $temp -IncludeLeaf
        $identity = $null
        if ($IsWindows) {
            if ([M0QtShellNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt temp must have a single-link identity.'
            }
            $tempFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
            if (-not $tempFinal.Equals($temp, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt temp final path differs before write.'
            }
            $identity = [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle)
        }
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
        if ($null -ne $AfterTempFlush) { & $AfterTempFlush }
        if ($stream.Length -ne $Bytes.Length -or (Get-M0QtShellSha256FromStream -Stream $stream) -cne (Get-M0QtShellSha256Bytes -Bytes $Bytes)) {
            Throw-M0QtShellInvalidData 'M0QT070 receipt temp bytes differ after durable flush.'
        }
        if ($IsWindows) {
            if ([M0QtShellNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1 -or
                [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle) -cne $identity) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt temp link/identity changed before atomic move.'
            }
            $tempFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
            if (-not $tempFinal.Equals($temp, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt temp final path changed before atomic move.'
            }
        }
        Assert-M0QtShellNoReparseAncestors -LiteralPath $parent -IncludeLeaf
        if ($IsWindows) {
            if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($parentHandle) -cne $parentIdentity) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt parent identity changed before atomic move.'
            }
            $heldParentFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($parentHandle))))
            if (-not $heldParentFinal.Equals($parent, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt parent final path changed before atomic move.'
            }
            [M0QtShellNativeV1.FileIdentity]::RenameNoReplaceRelative(
                $stream.SafeFileHandle,
                $parentHandle,
                [IO.Path]::GetFileName($final)
            )
        }
        $moved = $true
        if ($null -ne $AfterAtomicMove) { & $AfterAtomicMove }
        Assert-M0QtShellNoReparseAncestors -LiteralPath $final -IncludeLeaf
        if ($IsWindows) {
            if ([M0QtShellNativeV1.FileIdentity]::GetIdentity($parentHandle) -cne $parentIdentity) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt parent identity changed after atomic move.'
            }
            $heldParentFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($parentHandle))))
            if (-not $heldParentFinal.Equals($parent, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt parent final path changed after atomic move.'
            }
            if ([M0QtShellNativeV1.FileIdentity]::GetLinkCount($stream.SafeFileHandle) -ne 1 -or
                [M0QtShellNativeV1.FileIdentity]::GetIdentity($stream.SafeFileHandle) -cne $identity) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt final link/identity differs after atomic move.'
            }
            $movedFinal = [IO.Path]::GetFullPath((ConvertFrom-M0QtShellFinalPath -Path `
                ([M0QtShellNativeV1.FileIdentity]::GetFinalPath($stream.SafeFileHandle))))
            if (-not $movedFinal.Equals($final, $script:M0QtShellPathComparison)) {
                Throw-M0QtShellInvalidData 'M0QT070 receipt final path differs after atomic move.'
            }
        }
        $expectedHash = Get-M0QtShellSha256Bytes -Bytes $Bytes
        if ($stream.Length -ne $Bytes.Length -or (Get-M0QtShellSha256FromStream -Stream $stream) -cne $expectedHash) {
            Throw-M0QtShellInvalidData 'M0QT070 receipt final bytes differ after atomic move.'
        }
        $published = $true
        return [pscustomobject]@{ sha256 = $expectedHash; size_bytes = [long]$Bytes.Length }
    }
    finally {
        $cleanupFailure = $null
        if (-not $published -and $null -ne $stream) {
            try { [M0QtShellNativeV1.FileIdentity]::MarkDeleteOnClose($stream.SafeFileHandle) }
            catch { $cleanupFailure = $_ }
        }
        if ($null -ne $stream) { try { $stream.Dispose() } catch { } }
        if ($null -ne $parentHandle) { try { $parentHandle.Dispose() } catch { } }
        if ($null -ne $cleanupFailure) {
            throw [IO.InvalidDataException]::new(
                'M0QT071 receipt failure cleanup could not mark the retained identity for deletion.',
                $cleanupFailure.Exception
            )
        }
    }
}

function Assert-M0QtShellReceipt {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedContractSha256,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedReceiptSha256,
        [Parameter(Mandatory)][string] $ReceiptPath
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    if (-not $root.Equals($script:M0QtShellRepositoryRoot, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT011 RepositoryRoot differs from the literal repository that loaded this module.'
    }
    $resolvedReceiptPath = [IO.Path]::GetFullPath($ReceiptPath)
    Assert-M0QtShellSafePathText -LiteralPath $resolvedReceiptPath
    Assert-M0QtShellNoReparseAncestors -LiteralPath $resolvedReceiptPath -IncludeLeaf

    $retainedFiles = [Collections.Generic.List[object]]::new()
    $receiptFile = $null
    try {
        $contract = Assert-M0QtShellProvisioningContract -RepositoryRoot $root `
            -ExpectedContractSha256 $ExpectedContractSha256 -RetainHandles
        foreach ($file in @($contract.retained_files)) { $retainedFiles.Add($file) }

        $descriptorPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain.v1.json'
        $evidenceSchemaPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain-evidence.schema.json'
        $descriptorFile = @($retainedFiles | Where-Object {
                ([string]$_.final_path).Equals($descriptorPath, $script:M0QtShellPathComparison)
            })
        $evidenceSchemaFile = @($retainedFiles | Where-Object {
                ([string]$_.final_path).Equals($evidenceSchemaPath, $script:M0QtShellPathComparison)
            })
        if ($descriptorFile.Count -ne 1 -or $evidenceSchemaFile.Count -ne 1) {
            Throw-M0QtShellInvalidData 'M0QT011 the retained contract does not contain one descriptor and evidence schema.'
        }

        $descriptor = ConvertFrom-M0QtShellCanonicalJsonBytes `
            -Bytes (Read-M0QtShellTrustedBytes -TrustedFile $descriptorFile[0])
        $evidenceSchemaText = $script:M0QtShellUtf8.GetString(
            (Read-M0QtShellTrustedBytes -TrustedFile $evidenceSchemaFile[0]))
        $receiptFile = Open-M0QtShellTrustedFile -LiteralPath $resolvedReceiptPath `
            -ExpectedSha256 $ExpectedReceiptSha256
        $receiptBytes = Read-M0QtShellTrustedBytes -TrustedFile $receiptFile
        $receiptDocument = ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes $receiptBytes
        $receiptText = $script:M0QtShellUtf8.GetString($receiptBytes)
        if (-not (Test-Json -Json $receiptText -Schema $evidenceSchemaText -ErrorAction SilentlyContinue)) {
            Throw-M0QtShellInvalidData 'M0QT083 persisted receipt failed its closed evidence schema.'
        }
        Assert-M0QtShellReceiptSemantics -ReceiptDocument $receiptDocument -DescriptorDocument $descriptor `
            -ExpectedContractSha256 $ExpectedContractSha256 -ExpectedDescriptorSha256 ([string]$descriptorFile[0].sha256) | Out-Null
        Assert-M0QtShellTrustedFileUnchanged -TrustedFile $receiptFile

        $returnValue = $receiptDocument | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
        return [pscustomobject][ordered]@{
            receipt = $returnValue
            receipt_sha256 = [string]$receiptFile.sha256
        }
    }
    finally {
        try {
            if ($null -ne $receiptFile) {
                try { Assert-M0QtShellTrustedFileUnchanged -TrustedFile $receiptFile }
                finally { $receiptFile.stream.Dispose() }
            }
        }
        finally {
            if ($retainedFiles.Count -gt 0) { Close-M0QtShellRetainedFiles -Files $retainedFiles }
        }
    }
}

function Test-M0QtShellToolchain {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string] $ExpectedContractSha256,
        [string] $ReceiptPath
    )

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0QT001 Qt shell host validation is Windows-only and remains not_proven on this platform.')
    }
    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    $retainedFiles = [Collections.Generic.List[object]]::new()
    $receiptParentPins = @()
    $contract = Assert-M0QtShellProvisioningContract -RepositoryRoot $root -ExpectedContractSha256 $ExpectedContractSha256 -RetainHandles
    foreach ($heldContractFile in @($contract.retained_files)) { $retainedFiles.Add($heldContractFile) }
    try {
    $descriptorPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain.v1.json'
    $schemaPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain.schema.json'
    $evidenceSchemaPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-toolchain-evidence.schema.json'
    $stageSchemaPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-stage-manifest.schema.json'
    $stageManifestPath = Join-Path $script:M0QtShellModuleRoot 'qt-shell-stage-manifest.v1.json'
    $descriptorFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $descriptorPath
    $schemaFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $schemaPath
    $evidenceSchemaFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $evidenceSchemaPath
    $stageSchemaFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $stageSchemaPath
    $stageManifestFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $stageManifestPath
    $descriptorBytes = Read-M0QtShellTrustedBytes -TrustedFile $descriptorFile
    $descriptorText = $script:M0QtShellUtf8.GetString($descriptorBytes)
    $schemaText = $script:M0QtShellUtf8.GetString((Read-M0QtShellTrustedBytes -TrustedFile $schemaFile))
    if (-not (Test-Json -Json $descriptorText -Schema $schemaText -ErrorAction SilentlyContinue)) {
        Throw-M0QtShellInvalidData 'M0QT012 descriptor schema validation failed.'
    }
    $descriptor = ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes $descriptorBytes
    $stageManifestBytes = Read-M0QtShellTrustedBytes -TrustedFile $stageManifestFile
    $stageManifestText = $script:M0QtShellUtf8.GetString($stageManifestBytes)
    $stageSchemaText = $script:M0QtShellUtf8.GetString((Read-M0QtShellTrustedBytes -TrustedFile $stageSchemaFile))
    if (-not (Test-Json -Json $stageManifestText -Schema $stageSchemaText -ErrorAction SilentlyContinue)) {
        Throw-M0QtShellInvalidData 'M0QT084 stage manifest schema validation failed.'
    }
    $stageManifest = ConvertFrom-M0QtShellCanonicalJsonBytes -Bytes $stageManifestBytes
    Assert-M0QtShellStageManifestDocument -ManifestDocument $stageManifest -DescriptorDocument $descriptor | Out-Null
    $evidenceSchemaText = $script:M0QtShellUtf8.GetString(
        (Read-M0QtShellTrustedBytes -TrustedFile $evidenceSchemaFile))
    $resolvedReceiptPath = $null
    if ($PSBoundParameters.ContainsKey('ReceiptPath')) {
        $resolvedReceiptPath = [IO.Path]::GetFullPath($ReceiptPath)
        Assert-M0QtShellSafePathText -LiteralPath $resolvedReceiptPath
        $receiptParent = [IO.Path]::GetDirectoryName($resolvedReceiptPath)
        if (-not [IO.Directory]::Exists($receiptParent)) {
            Throw-M0QtShellInvalidData 'M0QT070 receipt parent must already exist before physical probes.'
        }
        Assert-M0QtShellNoReparseAncestors -LiteralPath $receiptParent -IncludeLeaf
        $receiptParentPins = @(Open-M0QtShellPinnedDirectories -LiteralPath $receiptParent)
        Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $receiptParentPins
        if ([IO.File]::Exists($resolvedReceiptPath) -or [IO.Directory]::Exists($resolvedReceiptPath)) {
            Throw-M0QtShellInvalidData 'M0QT070 receipt already exists; CreateNew/no overwrite is required before physical probes.'
        }
    }
    $beforePhysicalProbeHook = if ($null -ne $script:M0QtShellProvisioningTestHook) {
        $script:M0QtShellProvisioningTestHook.PSObject.Properties['before_physical_probe']
    }
    else { $null }
    if ($null -ne $beforePhysicalProbeHook -and $null -ne $beforePhysicalProbeHook.Value) {
        & $beforePhysicalProbeHook.Value
    }

    $expectedBindings = Get-M0QtShellDescriptorBindings -DescriptorDocument $descriptor
    $qtArchives = @()
    for ($archiveIndex = 0; $archiveIndex -lt @($descriptor.qt.archives).Count; $archiveIndex++) {
        $archive = $descriptor.qt.archives[$archiveIndex]
        $path = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$archive.path_base) -RelativePath ([string]$archive.relative_path)
        $null = Test-M0QtShellExpectedFile -LiteralPath $path -ExpectedSha256 ([string]$archive.sha256) -ExpectedSizeBytes ([long]$archive.size_bytes) -Id ([string]$archive.id) -RetainedFiles $retainedFiles
        if ([string]$archive.license.evidence_status -ceq 'physical_verified') {
            $licensePath = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$archive.license.local_path_base) -RelativePath ([string]$archive.license.local_relative_path)
            $licenseSize = Get-M0QtShellLicenseSize -License $archive.license
            if ($null -eq $licenseSize) { Throw-M0QtShellInvalidData 'M0QT012 Qt archive license size binding is missing.' }
            $null = Test-M0QtShellExpectedFile -LiteralPath $licensePath -ExpectedSha256 ([string]$archive.license.text_sha256) `
                -ExpectedSizeBytes ([long]$licenseSize) -Id (([string]$archive.id) + '-license') -RetainedFiles $retainedFiles
        }
        $qtArchives += $expectedBindings.qt_archives[$archiveIndex]
    }
    $qtTools = @()
    $qtToolPaths = @{}
    for ($toolIndex = 0; $toolIndex -lt @($descriptor.qt.tools).Count; $toolIndex++) {
        $tool = $descriptor.qt.tools[$toolIndex]
        $path = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$tool.path_base) -RelativePath ([string]$tool.relative_path)
        $null = Test-M0QtShellExpectedFile -LiteralPath $path -ExpectedSha256 ([string]$tool.sha256) -ExpectedSizeBytes ([long]$tool.size_bytes) -Id ([string]$tool.id) -RetainedFiles $retainedFiles
        $toolLicensePath = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$tool.license.local_path_base) -RelativePath ([string]$tool.license.local_relative_path)
        $toolLicenseSize = Get-M0QtShellLicenseSize -License $tool.license
        if ($null -eq $toolLicenseSize) { Throw-M0QtShellInvalidData 'M0QT012 Qt tool license size binding is missing.' }
        $null = Test-M0QtShellExpectedFile -LiteralPath $toolLicensePath -ExpectedSha256 ([string]$tool.license.text_sha256) `
            -ExpectedSizeBytes ([long]$toolLicenseSize) -Id (([string]$tool.id) + '-license') -RetainedFiles $retainedFiles
        $qtTools += $expectedBindings.qt_tools[$toolIndex]
        $qtToolPaths[[string]$tool.id] = $path
    }
    $hostTools = @()
    for ($hostIndex = 0; $hostIndex -lt @($descriptor.host.tools).Count; $hostIndex++) {
        $tool = $descriptor.host.tools[$hostIndex]
        $path = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$tool.path_base) -RelativePath ([string]$tool.relative_path)
        $null = Test-M0QtShellExpectedFile -LiteralPath $path -ExpectedSha256 ([string]$tool.sha256) -ExpectedSizeBytes ([long]$tool.size_bytes) -Id ([string]$tool.id) -RetainedFiles $retainedFiles
        $hostTools += $expectedBindings.host_tools[$hostIndex]
        $hostToolFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $path
        if ([string]$tool.id -ceq 'ninja') {
            $ninjaVersion = Invoke-M0QtShellBoundedProcess -FilePath $path `
                -ArgumentList @('--version') -TimeoutSeconds 15 -MaximumCapturedBytes 65536 `
                -WorkingDirectory ([IO.Path]::GetDirectoryName($path)) `
                -TrustedExecutable $hostToolFile
            if ($ninjaVersion.stdout.Trim() -cne [string]$tool.version -or
                -not [string]::IsNullOrEmpty($ninjaVersion.stderr)) {
                Throw-M0QtShellInvalidData "M0QT060 ninja version drifted from '$($tool.version)'."
            }
        }
        else {
            Test-M0QtShellVersionInfo -LiteralPath $path -ExpectedVersion ([string]$tool.version) `
                -TrustedFile $hostToolFile
        }
    }

    $catchArchive = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.catch2.path_base) -RelativePath ([string]$descriptor.catch2.archive_relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $catchArchive -ExpectedSha256 ([string]$descriptor.catch2.sha256) -ExpectedSizeBytes ([long]$descriptor.catch2.size_bytes) -Id 'catch2-source' -RetainedFiles $retainedFiles
    $catchLicense = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.catch2.license.local_path_base) -RelativePath ([string]$descriptor.catch2.license.local_relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $catchLicense -ExpectedSha256 ([string]$descriptor.catch2.license.text_sha256) -ExpectedSizeBytes ([long]$descriptor.catch2.license.text_size_bytes) -Id 'catch2-license' -RetainedFiles $retainedFiles

    $nvdaInstaller = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.nvda.path_base) -RelativePath ([string]$descriptor.nvda.installer_relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $nvdaInstaller -ExpectedSha256 ([string]$descriptor.nvda.sha256) -ExpectedSizeBytes ([long]$descriptor.nvda.size_bytes) -Id 'nvda-installer' -RetainedFiles $retainedFiles
    $nvdaInstallerFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $nvdaInstaller
    $null = Assert-M0QtShellAuthenticode -LiteralPath $nvdaInstaller `
        -ExpectedSubject ([string]$descriptor.nvda.authenticode.subject) `
        -ExpectedThumbprint ([string]$descriptor.nvda.authenticode.thumbprint) `
        -TrustedFile $nvdaInstallerFile
    $nvdaExe = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.nvda.path_base) -RelativePath ([string]$descriptor.nvda.portable_executable.relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $nvdaExe -ExpectedSha256 ([string]$descriptor.nvda.portable_executable.sha256) -ExpectedSizeBytes ([long]$descriptor.nvda.portable_executable.size_bytes) -Id 'nvda-executable' -RetainedFiles $retainedFiles
    $nvdaExeFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $nvdaExe
    $null = Assert-M0QtShellAuthenticode -LiteralPath $nvdaExe `
        -ExpectedSubject ([string]$descriptor.nvda.authenticode.subject) `
        -ExpectedThumbprint ([string]$descriptor.nvda.authenticode.thumbprint) `
        -TrustedFile $nvdaExeFile
    $nvdaLicense = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.nvda.license.local_path_base) -RelativePath ([string]$descriptor.nvda.license.local_relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $nvdaLicense -ExpectedSha256 ([string]$descriptor.nvda.license.text_sha256) -ExpectedSizeBytes ([long]$descriptor.nvda.license.text_size_bytes) -Id 'nvda-license' -RetainedFiles $retainedFiles

    $extractorArchive = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.extractor.archive.path_base) -RelativePath ([string]$descriptor.extractor.archive.relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $extractorArchive -ExpectedSha256 ([string]$descriptor.extractor.archive.sha256) -ExpectedSizeBytes ([long]$descriptor.extractor.archive.size_bytes) -Id '7zip-archive' -RetainedFiles $retainedFiles
    $extractorArchiveFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $extractorArchive
    $extractorExe = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.extractor.path_base) -RelativePath ([string]$descriptor.extractor.relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $extractorExe -ExpectedSha256 ([string]$descriptor.extractor.sha256) -ExpectedSizeBytes ([long]$descriptor.extractor.size_bytes) -Id '7zip-executable' -RetainedFiles $retainedFiles
    $extractorExeFile = Get-M0QtShellRetainedFile -Files $retainedFiles -LiteralPath $extractorExe
    $extractorLicense = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase ([string]$descriptor.extractor.license.local_path_base) -RelativePath ([string]$descriptor.extractor.license.local_relative_path)
    $null = Test-M0QtShellExpectedFile -LiteralPath $extractorLicense -ExpectedSha256 ([string]$descriptor.extractor.license.text_sha256) -ExpectedSizeBytes ([long]$descriptor.extractor.license.text_size_bytes) -Id '7zip-license' -RetainedFiles $retainedFiles
    $extractorRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($extractorExe))
    Assert-M0QtShellExtractedInventory -Root $extractorRoot -ExpectedRelativePaths @($descriptor.extractor.selected_members) | Out-Null

    foreach ($artifact in @($descriptor.qt.selected_artifacts)) {
        $artifactPath = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase 'qt_toolchain_root' -RelativePath ([string]$artifact.relative_path)
        $null = Test-M0QtShellExpectedFile -LiteralPath $artifactPath -ExpectedSha256 ([string]$artifact.sha256) -ExpectedSizeBytes ([long]$artifact.size_bytes) -Id ([string]$artifact.id) -RetainedFiles $retainedFiles
    }

    $probeBase = Resolve-M0QtShellPhysicalPath -RepositoryRoot $root -PathBase 'repository_root' `
        -RelativePath ([string]$descriptor.validation.temporary_root)
    $outM0Root = [IO.Path]::GetFullPath((Join-Path $root 'out\m0'))
    if (-not $probeBase.StartsWith($outM0Root + [IO.Path]::DirectorySeparatorChar, $script:M0QtShellPathComparison)) {
        Throw-M0QtShellInvalidData 'M0QT020 probe temporary root must remain below the repository out/m0 boundary.'
    }
    $probeBaseCreated = -not [IO.Directory]::Exists($probeBase)
    if ($probeBaseCreated) {
        Assert-M0QtShellNoReparseAncestors -LiteralPath ([IO.Path]::GetDirectoryName($probeBase)) -IncludeLeaf
        [IO.Directory]::CreateDirectory($probeBase) | Out-Null
    }
    Assert-M0QtShellNoReparseAncestors -LiteralPath $probeBase -IncludeLeaf
    $isolatedStage = $null
    try {
        $extractorPins = @()
        $archivePins = @()
        $probePins = @()
        try {
            $extractorPins = @(Open-M0QtShellPinnedDirectories -LiteralPath $extractorExe)
            $archivePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $extractorArchive)
            $probePins = @(Open-M0QtShellPinnedDirectories -LiteralPath $probeBase)
            $assertTrustedCommand = Get-Command Assert-M0QtShellTrustedFileUnchanged -CommandType Function -ErrorAction Stop
            $assertPinsCommand = Get-Command Assert-M0QtShellPinnedDirectoriesUnchanged -CommandType Function -ErrorAction Stop
            $archiveValidation = {
                & $assertTrustedCommand -TrustedFile $extractorExeFile
                & $assertTrustedCommand -TrustedFile $extractorArchiveFile
                & $assertPinsCommand -PinnedDirectories (@($extractorPins) + @($archivePins) + @($probePins))
            }.GetNewClosure()
            $archiveListing = Invoke-M0QtShellBoundedProcess -FilePath $extractorExe `
                -ArgumentList @('l', '-slt', $extractorArchive) -TimeoutSeconds 30 `
                -MaximumCapturedBytes 262144 -WorkingDirectory $probeBase -TempRoot $probeBase `
                -TrustedExecutable $extractorExeFile `
                -TrustedDirectories (@($extractorPins) + @($archivePins) + @($probePins)) `
                -PinnedStateValidation $archiveValidation
            if (-not [string]::IsNullOrEmpty($archiveListing.stderr)) {
                Throw-M0QtShellInvalidData 'M0QT022 anchored extractor listing emitted unexpected stderr.'
            }
        }
        finally {
            if (@($probePins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $probePins }
            if (@($archivePins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $archivePins }
            if (@($extractorPins).Count -gt 0) { Close-M0QtShellPinnedDirectories -PinnedDirectories $extractorPins }
        }
        $archiveMembers = ConvertFrom-M0QtShell7ZipSltListing -Text $archiveListing.stdout
        Assert-M0QtShellArchiveMembers -Members $archiveMembers | Out-Null
        $archiveMemberPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($member in $archiveMembers) { $null = $archiveMemberPaths.Add(([string]$member.path).Replace('\', '/').TrimEnd('/')) }
        foreach ($selectedMember in @($descriptor.extractor.selected_members)) {
            if (-not $archiveMemberPaths.Contains([string]$selectedMember)) {
                Throw-M0QtShellInvalidData 'M0QT022 selected extractor member is absent from the anchored archive listing.'
            }
        }

        $isolatedStage = New-M0QtShellIsolatedStage -RepositoryRoot $root `
            -DescriptorDocument $descriptor -ManifestDocument $stageManifest -StageParent $probeBase
        $stagePins = @($isolatedStage.parent_pins) + @($isolatedStage.directories)
        foreach ($tool in @($descriptor.qt.tools)) {
            $stagedTool = Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId ([string]$tool.id)
            Test-M0QtShellVersionInfo -LiteralPath $stagedTool.final_path `
                -ExpectedVersion ([string]$tool.version) -TrustedFile $stagedTool `
                -TrustedDirectories $stagePins
        }
        $formatTool = Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmlformat'
        $lintTool = Get-M0QtShellIsolatedStageTool -Stage $isolatedStage -ToolId 'qmllint'
        $formatCapability = Assert-M0QtShellQmlFormatCapability -FilePath $formatTool.final_path `
            -ArgumentPrefix @('--ignore-settings') -TimeoutSeconds 30 -TempRoot $probeBase `
            -QtStage $isolatedStage -TrustedExecutable $formatTool
        $lintCapability = Assert-M0QtShellQmlLintCapability -FilePath $lintTool.final_path `
            -ArgumentPrefix @('--bare', '--ignore-settings',
                '--disable-plugins', 'QtDesignStudio', '--disable-plugins', 'Quick',
                '-I', (Join-Path ([string]$isolatedStage.root) 'qml')) `
            -TimeoutSeconds 30 -TempRoot $probeBase -QtStage $isolatedStage `
            -TrustedExecutable $lintTool
        $qmlTestCapability = Assert-M0QtShellQmlTestCapability -QtStage $isolatedStage `
            -TimeoutSeconds 30 -TempRoot $probeBase
        $deploymentCapability = Assert-M0QtShellDeploymentCapability -QtStage $isolatedStage `
            -TimeoutSeconds 30 -TempRoot $probeBase
        $isolatedStageCapability = [pscustomobject][ordered]@{
            manifest_sha256 = [string]$descriptor.qt.stage_manifest.sha256
            entry_count = 162
            exact_inventory_validated = $true
            qml_closure_tsv_sha256 = [string]$descriptor.qt.stage_manifest.qml_closure_tsv_sha256
            pe_closure_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_closure_tsv_sha256
            pe_internal_import_edges_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_internal_import_edges_tsv_sha256
            pe_external_import_names_tsv_sha256 = [string]$descriptor.qt.stage_manifest.pe_external_import_names_tsv_sha256
            removed_after_probe = $true
        }
        Close-M0QtShellIsolatedStage -Stage $isolatedStage
        $isolatedStage = $null

        $receiptDocument = [ordered]@{
            '$schema' = 'qt-shell-toolchain-evidence.schema.json'
            schema_id = 'm0.qt-shell-toolchain-evidence.v1'
            schema_version = 1
            artifact_role = 'live_receipt'
            live_evidence = $true
            contract_sha256 = $ExpectedContractSha256
            descriptor_sha256 = $descriptorFile.sha256
            generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffffffZ', [Globalization.CultureInfo]::InvariantCulture)
            states = [ordered]@{
                assets_provisioned = $true
                host_tools_observed = $true
                qml_tool_capabilities_validated = $true
                preflight_scope_satisfied = $true
                host_build_capability_validated = $false
                manual_validated = $false
                candidate_accepted = $false
                candidate_build_validated = $false
                acceptance_ready = $false
                staging_approved = $false
                package_approved = $false
                qt_network_approved = $false
                accessibility_passed = $false
            }
            validation_mode = 'offline_inputs_only'
            network_allowed = $false
            network_enforcement = 'not_enforced'
            network_observation = 'not_measured'
            observations = $expectedBindings
            capabilities = [ordered]@{
                qmlformat = [ordered]@{ check_non_mutating = $formatCapability.check_non_mutating; dirty_fixture_changed = $formatCapability.dirty_fixture_changed; second_pass_byte_stable = $formatCapability.second_pass_byte_stable; canonical_lf_round_trip = $formatCapability.canonical_lf_round_trip }
                qmllint = [ordered]@{ valid_fixture_accepted = $lintCapability.valid_fixture_accepted; invalid_fixture_rejected = $lintCapability.invalid_fixture_rejected }
                isolated_stage = [ordered]@{
                    manifest_sha256 = $isolatedStageCapability.manifest_sha256
                    entry_count = $isolatedStageCapability.entry_count
                    exact_inventory_validated = $isolatedStageCapability.exact_inventory_validated
                    qml_closure_tsv_sha256 = $isolatedStageCapability.qml_closure_tsv_sha256
                    pe_closure_tsv_sha256 = $isolatedStageCapability.pe_closure_tsv_sha256
                    pe_internal_import_edges_tsv_sha256 = $isolatedStageCapability.pe_internal_import_edges_tsv_sha256
                    pe_external_import_names_tsv_sha256 = $isolatedStageCapability.pe_external_import_names_tsv_sha256
                    removed_after_probe = $isolatedStageCapability.removed_after_probe
                }
                qmltestrunner = [ordered]@{
                    qml_models_loaded = $qmlTestCapability.qml_models_loaded
                    worker_script_loaded = $qmlTestCapability.worker_script_loaded
                    window_loaded = $qmlTestCapability.window_loaded
                    basic_controls_loaded = $qmlTestCapability.basic_controls_loaded
                    shapes_loaded = $qmlTestCapability.shapes_loaded
                    svg_image_loaded = $qmlTestCapability.svg_image_loaded
                    qt_test_passed = $qmlTestCapability.qt_test_passed
                }
                deployment = [ordered]@{
                    probe_only = $deploymentCapability.probe_only
                    staging_approved = $deploymentCapability.staging_approved
                    bundle_policy_status = $deploymentCapability.bundle_policy_status
                    dry_run_mapping_validated = $deploymentCapability.dry_run_mapping_validated
                    quick_import_scan_disabled = $deploymentCapability.quick_import_scan_disabled
                    mapping_entry_count = $deploymentCapability.mapping_entry_count
                }
                nvda = [ordered]@{ authenticode_valid = $true; manual_journey_validated = $false }
            }
            diagnostics = [ordered]@{ qt_network_staging_probe = 'diagnostic-no-go-final-bundle'; manual_nvda_journey = 'not-executed'; candidate_build = 'not-executed' }
        }
        $receiptBytes = ConvertTo-M0QtShellCanonicalBytes -Document $receiptDocument
        if (-not (Test-Json -Json ($script:M0QtShellUtf8.GetString($receiptBytes)) -Schema $evidenceSchemaText -ErrorAction SilentlyContinue)) {
            Throw-M0QtShellInvalidData 'M0QT083 derived live receipt failed its closed schema.'
        }
        Assert-M0QtShellReceiptSemantics -ReceiptDocument $receiptDocument -DescriptorDocument $descriptor `
            -ExpectedContractSha256 $ExpectedContractSha256 -ExpectedDescriptorSha256 $descriptorFile.sha256 | Out-Null
        $receiptHash = Get-M0QtShellSha256Bytes -Bytes $receiptBytes
        if ($PSBoundParameters.ContainsKey('ReceiptPath')) {
            Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $receiptParentPins
            $published = Publish-M0QtShellReceipt -LiteralPath $resolvedReceiptPath -Bytes $receiptBytes
            Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $receiptParentPins
            $receiptHash = $published.sha256
        }
        $returnValue = $receiptDocument | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
        return [pscustomobject][ordered]@{
            receipt = $returnValue
            receipt_sha256 = $receiptHash
        }
    }
    finally {
        if ($null -ne $isolatedStage) {
            Close-M0QtShellIsolatedStage -Stage $isolatedStage
        }
        if ($probeBaseCreated -and [IO.Directory]::Exists($probeBase) -and
            @(Get-ChildItem -LiteralPath $probeBase -Force -ErrorAction SilentlyContinue).Count -eq 0) {
            Remove-Item -LiteralPath $probeBase -Force -ErrorAction SilentlyContinue
        }
    }
    }
    finally {
        try {
            if (@($receiptParentPins).Count -gt 0) {
                Assert-M0QtShellPinnedDirectoriesUnchanged -PinnedDirectories $receiptParentPins
            }
        }
        finally {
            if (@($receiptParentPins).Count -gt 0) {
                Close-M0QtShellPinnedDirectories -PinnedDirectories $receiptParentPins
            }
            Close-M0QtShellRetainedFiles -Files $retainedFiles
        }
    }
}

Export-ModuleMember -Function Assert-M0QtShellReceipt, Test-M0QtShellToolchain
