Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:M0ProcessUtf8 = [Text.UTF8Encoding]::new($false, $true)
$script:M0ProcessModuleInstanceId = [Guid]::NewGuid().ToString('N')
$script:M0ProcessModuleSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
$script:M0ProcessPilotRegistry = @{}
$script:M0ProcessRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')).TrimEnd('\', '/')
$script:M0ProcessNativeTypeNames = @(
    'M0ProcessNativeV1.PilotSession',
    'M0ProcessNativeV1.ProcessCapability',
    'M0ProcessNativeV1.TrustedFile'
)
$script:M0ProcessNativeAssembly = $null
$existingNativeTypes = @($script:M0ProcessNativeTypeNames | ForEach-Object { $_ -as [type] } | Where-Object { $null -ne $_ })
if ($IsWindows -and $existingNativeTypes.Count -gt 0) {
    throw [IO.InvalidDataException]::new('M0P001 M0.Process refuses all preloaded native process types; reuse the already loaded exact module without Force or reload.')
}

if ($IsWindows -and $existingNativeTypes.Count -eq 0) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace M0ProcessNativeV1
{
    public static class TrustedFile
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct FILE_TIME { public uint Low; public uint High; }

        [StructLayout(LayoutKind.Sequential)]
        private struct BY_HANDLE_FILE_INFORMATION
        {
            public uint FileAttributes;
            public FILE_TIME CreationTime;
            public FILE_TIME LastAccessTime;
            public FILE_TIME LastWriteTime;
            public uint VolumeSerialNumber;
            public uint FileSizeHigh;
            public uint FileSizeLow;
            public uint NumberOfLinks;
            public uint FileIndexHigh;
            public uint FileIndexLow;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(
            SafeFileHandle handle, StringBuilder path, uint pathLength, uint flags);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetFileInformationByHandle(
            SafeFileHandle handle, out BY_HANDLE_FILE_INFORMATION information);

        public static void Validate(SafeFileHandle handle, string expectedPath)
        {
            if (handle == null || handle.IsInvalid || handle.IsClosed)
                throw new InvalidDataException("Trusted helper handle is unavailable.");
            if (!GetFileInformationByHandle(handle, out var information))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;
            const uint FILE_ATTRIBUTE_REPARSE_POINT = 0x400;
            if ((information.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
                information.NumberOfLinks != 1)
                throw new InvalidDataException("Trusted helper must be a regular single-link file.");

            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            string finalPath = buffer.ToString();
            if (finalPath.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
                finalPath = @"\\" + finalPath.Substring(8);
            else if (finalPath.StartsWith(@"\\?\", StringComparison.OrdinalIgnoreCase))
                finalPath = finalPath.Substring(4);
            if (!string.Equals(Path.GetFullPath(finalPath), Path.GetFullPath(expectedPath),
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Trusted helper final handle path differs from its literal path.");
        }
    }

    public sealed class ProcessCapability : IDisposable
    {
        private SafeProcessHandle handle;
        public uint Pid { get; }
        public ulong CreationTimeFileTimeUtc { get; }
        public bool JobMembershipRevalidated { get; }

        internal ProcessCapability(SafeProcessHandle handle, uint pid, ulong creationTime, bool inJob)
        {
            this.handle = handle;
            Pid = pid;
            CreationTimeFileTimeUtc = creationTime;
            JobMembershipRevalidated = inJob;
        }

        public bool WaitForExit(int timeoutMilliseconds)
        {
            if (handle == null || handle.IsInvalid || handle.IsClosed)
                return true;
            uint result = NativeMethods.WaitForSingleObject(handle, checked((uint)timeoutMilliseconds));
            if (result == NativeMethods.WAIT_OBJECT_0) return true;
            if (result == NativeMethods.WAIT_TIMEOUT) return false;
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        public ulong RevalidateCreationTime()
        {
            if (handle == null || handle.IsInvalid || handle.IsClosed)
                throw new ObjectDisposedException(nameof(ProcessCapability));
            return NativeMethods.GetCreationTime(handle);
        }

        public void Dispose()
        {
            if (handle != null) { handle.Dispose(); handle = null; }
        }
    }

    public sealed class PilotSession : IDisposable
    {
        private SafeFileHandle job;
        private SafeProcessHandle rootProcess;
        private bool jobClosed;
        public uint RootPid { get; private set; }
        public ulong RootCreationTimeFileTimeUtc { get; private set; }
        public long QpcBeforeCreate { get; private set; }
        public long QpcAfterResume { get; private set; }
        public long QpcFrequency { get; private set; }

        private PilotSession() { }

        public static PilotSession Create(string executable, string[] arguments, string workingDirectory)
        {
            if (string.IsNullOrWhiteSpace(executable)) throw new ArgumentException(nameof(executable));
            var result = new PilotSession();
            if (!NativeMethods.QueryPerformanceFrequency(out long frequency) || frequency <= 0)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            result.QpcFrequency = frequency;
            result.job = NativeMethods.CreateJobObject(IntPtr.Zero, null);
            if (result.job == null || result.job.IsInvalid)
                throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                NativeMethods.SetKillOnJobClose(result.job);
                var commandLine = new StringBuilder(QuoteArgument(executable));
                foreach (string argument in arguments)
                {
                    commandLine.Append(' ');
                    commandLine.Append(QuoteArgument(argument));
                }
                var startup = new NativeMethods.STARTUPINFO();
                startup.cb = Marshal.SizeOf<NativeMethods.STARTUPINFO>();
                if (!NativeMethods.QueryPerformanceCounter(out long before))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                result.QpcBeforeCreate = before;
                if (!NativeMethods.CreateProcessW(executable, commandLine, IntPtr.Zero, IntPtr.Zero, false,
                        NativeMethods.CREATE_SUSPENDED | NativeMethods.CREATE_NO_WINDOW,
                        IntPtr.Zero, workingDirectory, ref startup, out var processInfo))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                result.rootProcess = new SafeProcessHandle(processInfo.hProcess, true);
                try
                {
                    result.RootPid = processInfo.dwProcessId;
                    result.RootCreationTimeFileTimeUtc = NativeMethods.GetCreationTime(result.rootProcess);
                    if (!NativeMethods.AssignProcessToJobObject(result.job, result.rootProcess))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    if (NativeMethods.ResumeThread(processInfo.hThread) == UInt32.MaxValue)
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    if (!NativeMethods.QueryPerformanceCounter(out long after))
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    result.QpcAfterResume = after;
                }
                catch (Exception originalFailure)
                {
                    if (!NativeMethods.TerminateProcess(result.rootProcess, 1))
                    {
                        var cleanupFailure = new Win32Exception(Marshal.GetLastWin32Error(),
                            "Failed to terminate the exact suspended root after launch setup failure.");
                        throw new AggregateException(originalFailure, cleanupFailure);
                    }
                    uint waitResult = NativeMethods.WaitForSingleObject(result.rootProcess, 5000);
                    if (waitResult != NativeMethods.WAIT_OBJECT_0)
                    {
                        var cleanupFailure = new IOException(
                            "Exact suspended root did not signal after checked termination; wait result " + waitResult + ".");
                        throw new AggregateException(originalFailure, cleanupFailure);
                    }
                    throw;
                }
                finally
                {
                    NativeMethods.CloseHandle(processInfo.hThread);
                }
                return result;
            }
            catch
            {
                result.Dispose();
                throw;
            }
        }

        public uint[] QueryProcessIds()
        {
            if (jobClosed || job == null || job.IsInvalid || job.IsClosed)
                throw new ObjectDisposedException(nameof(PilotSession));
            const int capacity = 256;
            int size = checked(8 + capacity * IntPtr.Size);
            IntPtr buffer = Marshal.AllocHGlobal(size);
            try
            {
                if (!NativeMethods.QueryInformationJobObject(job, 3, buffer, (uint)size, out _))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                uint count = checked((uint)Marshal.ReadInt32(buffer, 4));
                if (count > capacity) throw new IOException("Job process list exceeded the pilot safety bound.");
                var values = new List<uint>((int)count);
                for (int index = 0; index < count; index++)
                {
                    long raw = IntPtr.Size == 8
                        ? Marshal.ReadInt64(buffer, 8 + index * IntPtr.Size)
                        : Marshal.ReadInt32(buffer, 8 + index * IntPtr.Size);
                    if (raw <= 0 || raw > UInt32.MaxValue) throw new IOException("Job returned an invalid PID hint.");
                    values.Add((uint)raw);
                }
                return values.ToArray();
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        public ProcessCapability Capture(uint pid)
        {
            if (jobClosed || job == null || job.IsInvalid || job.IsClosed)
                throw new ObjectDisposedException(nameof(PilotSession));
            SafeProcessHandle process = NativeMethods.OpenProcess(
                NativeMethods.PROCESS_QUERY_LIMITED_INFORMATION | NativeMethods.SYNCHRONIZE, false, pid);
            if (process == null || process.IsInvalid)
            {
                process?.Dispose();
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            try
            {
                ulong creation = NativeMethods.GetCreationTime(process);
                if (!NativeMethods.IsProcessInJob(process, job, out bool inJob))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                if (!inJob) throw new InvalidDataException("PID hint no longer identifies a member of the pilot Job.");
                var capability = new ProcessCapability(process, pid, creation, true);
                process = null;
                return capability;
            }
            finally { process?.Dispose(); }
        }

        public void CloseJob()
        {
            if (jobClosed) return;
            jobClosed = true;
            if (job != null) { job.Dispose(); job = null; }
        }

        public void Dispose()
        {
            CloseJob();
            if (rootProcess != null) { rootProcess.Dispose(); rootProcess = null; }
        }

        private static string QuoteArgument(string value)
        {
            if (value == null) return "\"\"";
            if (value.Length > 0 && value.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return value;
            var result = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in value)
            {
                if (character == '\\') { backslashes++; continue; }
                if (character == '"')
                {
                    result.Append('\\', backslashes * 2 + 1);
                    result.Append('"');
                    backslashes = 0;
                    continue;
                }
                result.Append('\\', backslashes);
                backslashes = 0;
                result.Append(character);
            }
            result.Append('\\', backslashes * 2);
            result.Append('"');
            return result.ToString();
        }
    }

    internal static class NativeMethods
    {
        internal const uint CREATE_SUSPENDED = 0x00000004;
        internal const uint CREATE_NO_WINDOW = 0x08000000;
        internal const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
        internal const uint SYNCHRONIZE = 0x00100000;
        internal const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;
        internal const uint WAIT_OBJECT_0 = 0;
        internal const uint WAIT_TIMEOUT = 258;

        [StructLayout(LayoutKind.Sequential)]
        internal struct STARTUPINFO
        {
            public int cb; public IntPtr lpReserved; public IntPtr lpDesktop; public IntPtr lpTitle;
            public uint dwX; public uint dwY; public uint dwXSize; public uint dwYSize;
            public uint dwXCountChars; public uint dwYCountChars; public uint dwFillAttribute;
            public uint dwFlags; public ushort wShowWindow; public ushort cbReserved2;
            public IntPtr lpReserved2; public IntPtr hStdInput; public IntPtr hStdOutput; public IntPtr hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct PROCESS_INFORMATION
        {
            public IntPtr hProcess; public IntPtr hThread; public uint dwProcessId; public uint dwThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct FILETIME { public uint Low; public uint High; }

        [StructLayout(LayoutKind.Sequential)]
        internal struct JOBOBJECT_BASIC_LIMIT_INFORMATION
        {
            public long PerProcessUserTimeLimit; public long PerJobUserTimeLimit; public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize; public UIntPtr MaximumWorkingSetSize; public uint ActiveProcessLimit;
            public UIntPtr Affinity; public uint PriorityClass; public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct IO_COUNTERS
        {
            public ulong ReadOperationCount; public ulong WriteOperationCount; public ulong OtherOperationCount;
            public ulong ReadTransferCount; public ulong WriteTransferCount; public ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation; public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit; public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed; public UIntPtr PeakJobMemoryUsed;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern SafeFileHandle CreateJobObject(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(SafeFileHandle job, int infoClass, IntPtr info, uint length);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool CreateProcessW(string applicationName, StringBuilder commandLine,
            IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles, uint creationFlags,
            IntPtr environment, string currentDirectory, ref STARTUPINFO startupInfo, out PROCESS_INFORMATION processInformation);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool AssignProcessToJobObject(SafeFileHandle job, SafeProcessHandle process);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool QueryInformationJobObject(SafeFileHandle job, int infoClass, IntPtr info, uint length, out uint returnedLength);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern SafeProcessHandle OpenProcess(uint access, bool inheritHandle, uint processId);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool GetProcessTimes(SafeProcessHandle process, out FILETIME creation, out FILETIME exit, out FILETIME kernel, out FILETIME user);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool IsProcessInJob(SafeProcessHandle process, SafeFileHandle job, out bool result);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool TerminateProcess(SafeProcessHandle process, uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint WaitForSingleObject(SafeProcessHandle handle, uint milliseconds);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool QueryPerformanceCounter(out long value);
        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool QueryPerformanceFrequency(out long value);

        internal static void SetKillOnJobClose(SafeFileHandle job)
        {
            var information = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            information.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            int size = Marshal.SizeOf<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>();
            IntPtr buffer = Marshal.AllocHGlobal(size);
            try
            {
                Marshal.StructureToPtr(information, buffer, false);
                if (!SetInformationJobObject(job, 9, buffer, (uint)size))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        internal static ulong GetCreationTime(SafeProcessHandle process)
        {
            if (!GetProcessTimes(process, out var creation, out _, out _, out _))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            ulong value = ((ulong)creation.High << 32) | creation.Low;
            if (value == 0) throw new InvalidDataException("Process creation FILETIME cannot be zero.");
            return value;
        }
    }
}
'@
    $loadedNativeTypes = @($script:M0ProcessNativeTypeNames | ForEach-Object { $_ -as [type] } | Where-Object { $null -ne $_ })
    if ($loadedNativeTypes.Count -ne $script:M0ProcessNativeTypeNames.Count -or
        @($loadedNativeTypes | Where-Object { -not [object]::ReferenceEquals($_.Assembly, $loadedNativeTypes[0].Assembly) }).Count -ne 0) {
        throw [IO.InvalidDataException]::new('M0P001 M0.Process native type compilation produced an incoherent assembly.')
    }
    $script:M0ProcessNativeAssembly = $loadedNativeTypes[0].Assembly
}

function Assert-M0ProcessNativeContract {
    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0P001 native process contract is unavailable outside Windows.')
    }
    $currentModuleSha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant()
    if ($currentModuleSha256 -cne $script:M0ProcessModuleSha256) {
        throw [IO.InvalidDataException]::new('M0P001 loaded process runtime differs from its exact source bytes.')
    }
    $trustedNativeAssembly = $script:M0ProcessNativeAssembly
    $types = @($script:M0ProcessNativeTypeNames | ForEach-Object { $_ -as [type] } | Where-Object { $null -ne $_ })
    if ($types.Count -ne $script:M0ProcessNativeTypeNames.Count -or $null -eq $trustedNativeAssembly -or
        @($types | Where-Object { -not [object]::ReferenceEquals($_.Assembly, $trustedNativeAssembly) }).Count -ne 0) {
        throw [IO.InvalidDataException]::new('M0P001 native process types do not match the recorded M0.Process assembly identity.')
    }
    return [pscustomobject][ordered]@{
        contract_id = 'm0-process-native-v1'
        module_sha256 = $script:M0ProcessModuleSha256
        module_instance_id = $script:M0ProcessModuleInstanceId
        assembly_full_name = [string]$trustedNativeAssembly.FullName
        type_count = $types.Count
    }
}

function Get-M0ProcessSha256Bytes {
    param([Parameter(Mandatory)] [byte[]] $Bytes)

    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Read-M0ProcessTextFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Name
    )

    $path = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0P001 required $Name is missing: '$path'.")
    }
    $item = Get-Item -LiteralPath $path -Force
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
        throw [IO.InvalidDataException]::new("M0P001 links are forbidden for trusted $Name '$path'.")
    }
    $bytes = [IO.File]::ReadAllBytes($path)
    if ($bytes.Length -eq 0 -or
        ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) {
        throw [IO.InvalidDataException]::new("M0P001 $Name must be non-empty strict UTF-8 without BOM.")
    }
    try {
        $text = $script:M0ProcessUtf8.GetString($bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0P001 $Name is not strict UTF-8.", $_.Exception)
    }
    if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal) -or
        $text.EndsWith("`n`n", [StringComparison]::Ordinal) -or $text -match '(?m)[\t ]+$') {
        throw [IO.InvalidDataException]::new("M0P001 $Name must use LF, exactly one final newline and no trailing whitespace.")
    }
    return [pscustomobject][ordered]@{
        path = $path
        bytes = $bytes
        text = $text
        sha256 = Get-M0ProcessSha256Bytes -Bytes $bytes
    }
}

function ConvertFrom-M0ProcessJsonFile {
    param(
        [Parameter(Mandatory)] $File,
        [Parameter(Mandatory)] [string] $SchemaPath,
        [Parameter(Mandatory)] [string] $Name
    )

    if (-not (Test-Json -Json $File.text -SchemaFile $SchemaPath -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new("M0P002 $Name failed its closed JSON schema.")
    }
    try {
        return ($File.text | ConvertFrom-Json -Depth 100 -DateKind String)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0P002 $Name is not valid JSON.", $_.Exception)
    }
}

function Assert-M0ProcessGoldenSemantics {
    param([Parameter(Mandatory)] $Golden)

    try {
        foreach ($vector in @($Golden.attribution_vectors)) {
            $actual = Resolve-M0ProcessAttribution -RootIdentity $vector.root_identity -Records @($vector.records) `
                -ExpectedExecutableSha256 ([string]$vector.expected_executable_sha256) -Nonce ([string]$vector.nonce) `
                -LaunchQpcTicks ([string]$vector.launch_qpc_ticks) `
                -AttemptEndQpcTicks ([string]$vector.attempt_end_qpc_ticks) -TraceState complete
            $actualJson = $actual | ConvertTo-Json -Compress -Depth 30
            $expectedJson = $vector.expected | ConvertTo-Json -Compress -Depth 30
            if ($actualJson -cne $expectedJson) {
                throw [IO.InvalidDataException]::new("attribution vector '$($vector.case_id)' expected output differs")
            }
        }
        foreach ($vector in @($Golden.cleanup_vectors)) {
            $actual = Resolve-M0ProcessCleanupPlan -Observations @($vector.observations)
            $actualShape = [pscustomobject][ordered]@{
                complete = [bool]$actual.complete
                actions = @($actual.actions | ForEach-Object { [string]$_.action })
            }
            $actualJson = $actualShape | ConvertTo-Json -Compress -Depth 10
            $expectedJson = $vector.expected | ConvertTo-Json -Compress -Depth 10
            if ($actualJson -cne $expectedJson) {
                throw [IO.InvalidDataException]::new("cleanup vector '$($vector.case_id)' expected output differs")
            }
        }
        foreach ($vector in @($Golden.linux_vectors)) {
            $actual = Resolve-M0LinuxProcessIdentity -ProcStat ([string]$vector.proc_stat) `
                -ExpectedPid $vector.expected_pid -ExpectedStartTicks ([string]$vector.expected_start_ticks) `
                -ExpectedBootIdSha256 ([string]$vector.expected_boot_id_sha256) `
                -ObservedBootIdSha256 ([string]$vector.observed_boot_id_sha256) `
                -PidfdAvailable $vector.pidfd_available
            $actualJson = $actual | ConvertTo-Json -Compress -Depth 20
            $expectedJson = $vector.expected | ConvertTo-Json -Compress -Depth 20
            if ($actualJson -cne $expectedJson) {
                throw [IO.InvalidDataException]::new("Linux vector '$($vector.case_id)' expected output differs")
            }
        }
    }
    catch {
        throw [IO.InvalidDataException]::new('M0P005 process golden semantic execution failed.', $_.Exception)
    }
}

function Assert-M0ProcessArtifacts {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $ProtocolRoot)

    $root = [IO.Path]::GetFullPath($ProtocolRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0P001 protocol root is missing: '$root'.")
    }
    $schemaRoot = Join-Path $root 'schemas'
    $descriptorFile = Read-M0ProcessTextFile -LiteralPath (Join-Path $root 'process.v1.json') -Name 'process descriptor'
    $sidecarFile = Read-M0ProcessTextFile -LiteralPath (Join-Path $root 'process.v1.sha256') -Name 'process descriptor sidecar'
    $expectedSidecar = "$($descriptorFile.sha256)  process.v1.json`n"
    if ($sidecarFile.text -cne $expectedSidecar) {
        throw [IO.InvalidDataException]::new('M0P003 process descriptor sidecar format/hash differs.')
    }
    $descriptor = ConvertFrom-M0ProcessJsonFile -File $descriptorFile `
        -SchemaPath (Join-Path $schemaRoot 'process.schema.json') -Name 'process descriptor'
    $goldenPath = Join-Path $root ([string]$descriptor.golden_relative_path).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $goldenFile = Read-M0ProcessTextFile -LiteralPath $goldenPath -Name 'process golden'
    if ($goldenFile.sha256 -cne [string]$descriptor.golden_sha256) {
        throw [IO.InvalidDataException]::new('M0P003 process golden hash differs from the descriptor.')
    }
    $golden = ConvertFrom-M0ProcessJsonFile -File $goldenFile `
        -SchemaPath (Join-Path $schemaRoot 'process-golden.schema.json') -Name 'process golden'
    if ($golden.process_id -cne $descriptor.process_id) {
        throw [IO.InvalidDataException]::new('M0P004 process golden identity differs from the descriptor.')
    }
    $snapshotSchemaPath = [IO.Path]::GetFullPath((Join-Path $root (
        [string]$descriptor.pilot_contract.snapshot_schema_relative_path
    ).Replace('/', [IO.Path]::DirectorySeparatorChar)))
    if (-not $snapshotSchemaPath.StartsWith(
            [IO.Path]::GetFullPath($schemaRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw [IO.InvalidDataException]::new('M0P004 process-tree schema path escaped the trusted schema root.')
    }
    $snapshotSchemaFile = Read-M0ProcessTextFile -LiteralPath $snapshotSchemaPath -Name 'process-tree snapshot schema'
    if ($snapshotSchemaFile.sha256 -cne [string]$descriptor.pilot_contract.snapshot_schema_sha256) {
        throw [IO.InvalidDataException]::new('M0P004 process-tree snapshot schema hash differs from the descriptor.')
    }
    try { $null = $snapshotSchemaFile.text | ConvertFrom-Json -Depth 100 -DateKind String }
    catch { throw [IO.InvalidDataException]::new('M0P004 process-tree snapshot schema is invalid JSON.', $_.Exception) }
    $artifactSchemaPath = [IO.Path]::GetFullPath((Join-Path $root (
        [string]$descriptor.pilot_contract.artifact_schema_relative_path
    ).Replace('/', [IO.Path]::DirectorySeparatorChar)))
    if (-not $artifactSchemaPath.StartsWith(
            [IO.Path]::GetFullPath($schemaRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw [IO.InvalidDataException]::new('M0P004 process-tree artifact schema path escaped the trusted schema root.')
    }
    $artifactSchemaFile = Read-M0ProcessTextFile -LiteralPath $artifactSchemaPath -Name 'process-tree artifact schema'
    if ($artifactSchemaFile.sha256 -cne [string]$descriptor.pilot_contract.artifact_schema_sha256) {
        throw [IO.InvalidDataException]::new('M0P004 process-tree artifact schema hash differs from the descriptor.')
    }
    try { $null = $artifactSchemaFile.text | ConvertFrom-Json -Depth 100 -DateKind String }
    catch { throw [IO.InvalidDataException]::new('M0P004 process-tree artifact schema is invalid JSON.', $_.Exception) }
    Assert-M0ProcessGoldenSemantics -Golden $golden
    return [pscustomobject][ordered]@{
        process_id = [string]$descriptor.process_id
        golden_id = [string]$golden.golden_id
        descriptor_sha256 = $descriptorFile.sha256
        golden_sha256 = $goldenFile.sha256
        snapshot_schema_path = $snapshotSchemaPath
        snapshot_schema_sha256 = $snapshotSchemaFile.sha256
        artifact_schema_path = $artifactSchemaPath
        artifact_schema_sha256 = $artifactSchemaFile.sha256
        descriptor = $descriptor
        golden = $golden
    }
}

function Resolve-M0ProcessAttribution {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] $RootIdentity,
        [Parameter(Mandatory)] [object[]] $Records,
        [Parameter(Mandatory)] [string] $ExpectedExecutableSha256,
        [Parameter(Mandatory)] [string] $Nonce,
        [Parameter(Mandatory)] $LaunchQpcTicks,
        [Parameter(Mandatory)] $AttemptEndQpcTicks,
        [Parameter(Mandatory)] [ValidateSet('complete', 'partial', 'unavailable')] [string] $TraceState,
        [ValidateSet('lost-event', 'buffer-overflow', 'trace-started-after-launch', 'identity-conflict', 'parentage-conflict')]
        [string] $TraceGapCode
    )

    if ($ExpectedExecutableSha256 -cnotmatch '^[a-f0-9]{64}$' -or $Nonce -cnotmatch '^[a-f0-9]{32}$') {
        throw [ArgumentException]::new('M0P010 expected executable SHA-256 or nonce format is invalid.')
    }
    $traceStates = [Collections.Generic.HashSet[string]]::new(
        [string[]]@('complete', 'partial', 'unavailable'), [StringComparer]::Ordinal
    )
    $traceGapCodes = [Collections.Generic.HashSet[string]]::new(
        [string[]]@('lost-event', 'buffer-overflow', 'trace-started-after-launch', 'identity-conflict', 'parentage-conflict'),
        [StringComparer]::Ordinal
    )
    if (-not $traceStates.Contains($TraceState)) {
        throw [ArgumentException]::new('M0P013 trace state must use the canonical ordinal enum spelling.')
    }
    if (-not [string]::IsNullOrEmpty($TraceGapCode) -and -not $traceGapCodes.Contains($TraceGapCode)) {
        throw [ArgumentException]::new('M0P013 trace gap cause must use the canonical ordinal enum spelling.')
    }
    if (($TraceState -ceq 'partial' -and [string]::IsNullOrEmpty($TraceGapCode)) -or
        ($TraceState -cne 'partial' -and -not [string]::IsNullOrEmpty($TraceGapCode))) {
        throw [ArgumentException]::new('M0P013 partial trace state requires exactly one closed trace gap cause.')
    }
    if ($TraceState -ceq 'unavailable') {
        return [pscustomobject][ordered]@{
            state = 'unavailable'
            error_code = 'etw_trace_unavailable'
            missing_capabilities = @('etw-process-start-stop')
            identities = @()
            sources = @()
        }
    }
    $launchTicks = [Numerics.BigInteger]::Zero
    $attemptEndTicks = [Numerics.BigInteger]::Zero
    if (-not [Numerics.BigInteger]::TryParse([string]$LaunchQpcTicks, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$launchTicks) -or
        -not [Numerics.BigInteger]::TryParse([string]$AttemptEndQpcTicks, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$attemptEndTicks) -or
        $launchTicks.Sign -lt 0 -or $attemptEndTicks -le $launchTicks -or
        $launchTicks.ToString([Globalization.CultureInfo]::InvariantCulture) -cne [string]$LaunchQpcTicks -or
        $attemptEndTicks.ToString([Globalization.CultureInfo]::InvariantCulture) -cne [string]$AttemptEndQpcTicks) {
        throw [ArgumentException]::new('M0P012 launch/attempt QPC boundaries are invalid.')
    }

    function ConvertTo-M0ProcessIdentity {
        param([Parameter(Mandatory)] $Identity, [Parameter(Mandatory)] [string] $Name)

        $properties = @($Identity.PSObject.Properties.Name | Sort-Object)
        if ($properties.Count -ne 2 -or $properties[0] -cne 'creation_time_filetime_utc' -or $properties[1] -cne 'pid') {
            throw [ArgumentException]::new("M0P010 $Name must contain exactly PID and creation time identity.")
        }
        $integerTypes = @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64], [uint64])
        $isInteger = $false
        foreach ($type in $integerTypes) {
            if ($Identity.pid -is $type) { $isInteger = $true; break }
        }
        [uint64]$pid = 0
        if (-not $isInteger -or -not [uint64]::TryParse([string]$Identity.pid, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$pid) -or $pid -eq 0 -or $pid -gt [uint32]::MaxValue) {
            throw [ArgumentException]::new("M0P010 $Name PID must be an integer in the uint32 process range.")
        }
        [uint64]$creation = 0
        $creationText = [string]$Identity.creation_time_filetime_utc
        if ($Identity.creation_time_filetime_utc -isnot [string] -or
            -not [uint64]::TryParse($creationText, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$creation) -or
            $creation -eq 0 -or
            $creation.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $creationText) {
            throw [ArgumentException]::new("M0P010 $Name creation/birth token is not a canonical positive decimal string.")
        }
        return [pscustomobject][ordered]@{
            pid = [uint32]$pid
            creation_time_filetime_utc = $creationText
            key = "$pid|$creationText"
        }
    }

    $root = ConvertTo-M0ProcessIdentity -Identity $RootIdentity -Name 'root identity'
    $attributed = [Collections.Generic.List[object]]::new()
    $sources = [Collections.Generic.List[string]]::new()
    $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $observedKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $unresolvedExternal = $false
    $previousSequence = 0
    foreach ($record in $Records) {
        $expectedRecordProperties = @(
            'birth_qpc_ticks', 'etw_birth_observed', 'executable_sha256', 'external_attribution_required', 'identity',
            'job_member', 'nonce_argument_match', 'observation_seq', 'observed_after_launch', 'observed_nonce',
            'parent_identity', 'within_attempt_window'
        )
        $recordProperties = @($record.PSObject.Properties.Name | Sort-Object)
        if ($recordProperties.Count -ne $expectedRecordProperties.Count) {
            throw [ArgumentException]::new('M0P011 attribution record is not a closed object.')
        }
        for ($propertyIndex = 0; $propertyIndex -lt $expectedRecordProperties.Count; $propertyIndex++) {
            if ($recordProperties[$propertyIndex] -cne $expectedRecordProperties[$propertyIndex]) {
                throw [ArgumentException]::new('M0P011 attribution record properties differ from the closed contract.')
            }
        }
        foreach ($booleanName in @(
            'job_member', 'etw_birth_observed', 'nonce_argument_match', 'external_attribution_required',
            'observed_after_launch', 'within_attempt_window'
        )) {
            if ($record.$booleanName -isnot [bool]) {
                throw [ArgumentException]::new("M0P011 attribution record '$booleanName' must be a boolean.")
            }
        }
        if ([string]$record.executable_sha256 -cnotmatch '^[a-f0-9]{64}$') {
            throw [ArgumentException]::new('M0P011 attribution record executable SHA-256 is invalid.')
        }
        if ([bool]$record.nonce_argument_match) {
            if ($record.observed_nonce -isnot [string] -or [string]$record.observed_nonce -cnotmatch '^[a-f0-9]{32}$') {
                throw [ArgumentException]::new('M0P011 an exact observed nonce is required when nonce_argument_match is true.')
            }
        }
        elseif ($null -ne $record.observed_nonce) {
            throw [ArgumentException]::new('M0P011 observed nonce must be null when nonce_argument_match is false.')
        }
        $birthTicks = [Numerics.BigInteger]::Zero
        if (-not [Numerics.BigInteger]::TryParse([string]$record.birth_qpc_ticks, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$birthTicks) -or $birthTicks.Sign -lt 0 -or
            $birthTicks.ToString([Globalization.CultureInfo]::InvariantCulture) -cne [string]$record.birth_qpc_ticks) {
            throw [ArgumentException]::new('M0P012 process birth QPC ticks are invalid.')
        }
        $derivedAfterLaunch = $birthTicks -gt $launchTicks
        $derivedWithinWindow = $derivedAfterLaunch -and $birthTicks -le $attemptEndTicks
        if ([bool]$record.observed_after_launch -ne $derivedAfterLaunch -or
            [bool]$record.within_attempt_window -ne $derivedWithinWindow) {
            throw [ArgumentException]::new('M0P012 process birth QPC timing/window flags contradict the closed boundaries.')
        }
        [int64]$sequence = 0
        $sequenceIsInteger = $false
        foreach ($type in @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64])) {
            if ($record.observation_seq -is $type) { $sequenceIsInteger = $true; break }
        }
        if (-not $sequenceIsInteger -or
            -not [int64]::TryParse([string]$record.observation_seq, [ref]$sequence) -or
            $sequence.ToString([Globalization.CultureInfo]::InvariantCulture) -cne [string]$record.observation_seq -or
            $sequence -le $previousSequence -or $sequence -gt [int32]::MaxValue) {
            throw [ArgumentException]::new('M0P011 attribution observation sequence must be a canonical integer, unique and strictly increasing.')
        }
        $previousSequence = $sequence
        $identity = ConvertTo-M0ProcessIdentity -Identity $record.identity -Name "record $sequence identity"
        if (-not $observedKeys.Add($identity.key)) {
            throw [ArgumentException]::new("M0P011 duplicate process identity at observation $sequence.")
        }
        $parent = $null
        if ($null -ne $record.parent_identity) {
            $parent = ConvertTo-M0ProcessIdentity -Identity $record.parent_identity -Name "record $sequence parent identity"
        }
        if ([bool]$record.etw_birth_observed -and $null -eq $parent) {
            throw [ArgumentException]::new("M0P011 ETW birth record $sequence requires the complete parent identity.")
        }
        $source = $null
        if ($identity.key -ceq $root.key) {
            if (-not $derivedAfterLaunch -or -not $derivedWithinWindow) {
                throw [ArgumentException]::new('M0P012 root handle birth must be causally inside the post-launch attempt window.')
            }
            $source = 'root-handle'
        }
        elseif ([bool]$record.job_member) {
            $source = 'job-membership'
        }
        elseif ([bool]$record.etw_birth_observed -and $null -ne $parent -and $keys.Contains($parent.key)) {
            $source = 'etw-birth-descendant'
        }
        if ($null -eq $source -and [bool]$record.nonce_argument_match -and
            [string]$record.observed_nonce -ceq $Nonce -and
            [string]$record.executable_sha256 -ceq $ExpectedExecutableSha256 -and
            $derivedAfterLaunch -and $derivedWithinWindow) {
            $source = 'nonce-executable-match'
        }

        if ($null -eq $source) {
            if ([bool]$record.external_attribution_required) {
                if ($TraceState -ceq 'partial') {
                    $unresolvedExternal = $true
                    continue
                }
                return [pscustomobject][ordered]@{
                    state = 'partial'
                    error_code = 'unresolved_external_attribution'
                    missing_capabilities = @('external-attribution-proof')
                    identities = @()
                    sources = @()
                }
            }
            continue
        }
        if (-not $keys.Add($identity.key)) {
            throw [ArgumentException]::new("M0P011 duplicate process identity at observation $sequence.")
        }
        $attributed.Add([pscustomobject][ordered]@{
            pid = $identity.pid
            creation_time_filetime_utc = $identity.creation_time_filetime_utc
        })
        $sources.Add($source)
    }
    $rootMissing = -not $keys.Contains($root.key)
    if ($TraceState -ceq 'partial') {
        $missing = [Collections.Generic.List[string]]::new()
        $missing.Add('etw-process-start-stop')
        if ($rootMissing) { $missing.Add('root-handle') }
        if ($unresolvedExternal) { $missing.Add('external-attribution-proof') }
        $partialError = if ($rootMissing) {
            'root_identity_missing'
        }
        elseif ($unresolvedExternal) {
            'unresolved_external_attribution'
        }
        else {
            'etw_trace_incomplete'
        }
        return [pscustomobject][ordered]@{
            state = 'partial'
            error_code = $partialError
            trace_gap_code = $TraceGapCode
            missing_capabilities = @($missing)
            identities = @($attributed)
            sources = @($sources)
        }
    }
    if ($rootMissing) {
        return [pscustomobject][ordered]@{
            state = 'partial'
            error_code = 'root_identity_missing'
            missing_capabilities = @('root-handle')
            identities = @()
            sources = @()
        }
    }
    return [pscustomobject][ordered]@{
        state = 'complete'
        error_code = $null
        missing_capabilities = @()
        identities = @($attributed)
        sources = @($sources)
    }
}

function Resolve-M0ProcessCleanupPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [object[]] $Observations)

    if ($Observations.Count -eq 0) {
        throw [ArgumentException]::new('M0P020 at least one synthetic cleanup observation is required.')
    }

    function ConvertTo-M0CleanupIdentity {
        param([Parameter(Mandatory)] $Identity, [Parameter(Mandatory)] [string] $Name)

        $properties = @($Identity.PSObject.Properties.Name | Sort-Object)
        if ($properties.Count -ne 2 -or $properties[0] -cne 'creation_time_filetime_utc' -or $properties[1] -cne 'pid') {
            throw [ArgumentException]::new("M0P020 $Name is not a closed PID+birth identity.")
        }
        $integerTypes = @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64], [uint64])
        $isInteger = $false
        foreach ($type in $integerTypes) {
            if ($Identity.pid -is $type) { $isInteger = $true; break }
        }
        [uint64]$pid = 0
        if (-not $isInteger -or -not [uint64]::TryParse([string]$Identity.pid, [ref]$pid) -or
            $pid -eq 0 -or $pid -gt [uint32]::MaxValue) {
            throw [ArgumentException]::new("M0P020 $Name PID must be an integer in the uint32 process range.")
        }
        $birthText = [string]$Identity.creation_time_filetime_utc
        [uint64]$birth = 0
        if ($Identity.creation_time_filetime_utc -isnot [string] -or
            -not [uint64]::TryParse($birthText, [Globalization.NumberStyles]::None,
                [Globalization.CultureInfo]::InvariantCulture, [ref]$birth) -or $birth -eq 0 -or
            $birth.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $birthText) {
            throw [ArgumentException]::new("M0P020 $Name birth token must be a canonical positive decimal string.")
        }
        return [pscustomobject][ordered]@{
            pid = [uint32]$pid
            creation_time_filetime_utc = $birthText
            key = "$pid|$birthText"
        }
    }

    $actions = [Collections.Generic.List[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $complete = $true
    for ($index = 0; $index -lt $Observations.Count; $index++) {
        $observation = $Observations[$index]
        $properties = @($observation.PSObject.Properties.Name | Sort-Object)
        if ($properties.Count -ne 2 -or $properties[0] -cne 'live_identity' -or $properties[1] -cne 'recorded_identity') {
            throw [ArgumentException]::new("M0P020 cleanup observation $index is not a closed synthetic planner input.")
        }
        $recorded = ConvertTo-M0CleanupIdentity -Identity $observation.recorded_identity -Name "recorded identity $index"
        if (-not $seen.Add($recorded.key)) {
            throw [ArgumentException]::new("M0P020 duplicate recorded cleanup identity at index $index.")
        }
        if ($null -eq $observation.live_identity) {
            $action = 'already-exited'
        }
        else {
            $live = ConvertTo-M0CleanupIdentity -Identity $observation.live_identity -Name "live identity $index"
            if ($live.key -ceq $recorded.key) {
                $action = 'terminate-exact-identity'
            }
            else {
                $action = 'skip-identity-mismatch'
                $complete = $false
            }
        }
        $actions.Add([pscustomobject][ordered]@{
            pid = $recorded.pid
            creation_time_filetime_utc = $recorded.creation_time_filetime_utc
            action = $action
        })
    }
    return [pscustomobject][ordered]@{
        complete = $complete
        actions = @($actions)
    }
}

function Resolve-M0LinuxProcessIdentity {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ProcStat,
        [Parameter(Mandatory)] $ExpectedPid,
        [Parameter(Mandatory)] [string] $ExpectedStartTicks,
        [Parameter(Mandatory)] [string] $ExpectedBootIdSha256,
        [Parameter(Mandatory)] [string] $ObservedBootIdSha256,
        [Parameter(Mandatory)] $PidfdAvailable
    )

    if ($PidfdAvailable -isnot [bool]) {
        throw [ArgumentException]::new('M0P030 pidfd availability must be a boolean.')
    }
    if ($ExpectedBootIdSha256 -cnotmatch '^[a-f0-9]{64}$' -or
        $ObservedBootIdSha256 -cnotmatch '^[a-f0-9]{64}$' -or
        $ExpectedStartTicks -cnotmatch '^[1-9][0-9]{0,19}$') {
        throw [ArgumentException]::new('M0P030 Linux expected start ticks or boot ID SHA-256 is invalid.')
    }
    $pidTypes = @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64], [uint64])
    $expectedPidIsInteger = $false
    foreach ($type in $pidTypes) {
        if ($ExpectedPid -is $type) { $expectedPidIsInteger = $true; break }
    }
    [uint64]$expectedPidValue = 0
    if (-not $expectedPidIsInteger -or -not [uint64]::TryParse([string]$ExpectedPid, [ref]$expectedPidValue) -or
        $expectedPidValue -eq 0 -or $expectedPidValue -gt [uint32]::MaxValue) {
        throw [ArgumentException]::new('M0P030 Linux expected PID must be an integer in the uint32 process range.')
    }

    $openIndex = $ProcStat.IndexOf(' (', [StringComparison]::Ordinal)
    $closeIndex = $ProcStat.LastIndexOf(') ', [StringComparison]::Ordinal)
    if ($openIndex -le 0 -or $closeIndex -le ($openIndex + 2)) {
        throw [IO.InvalidDataException]::new('M0P031 Linux proc stat has no closed comm field.')
    }
    $pidText = $ProcStat.Substring(0, $openIndex)
    [uint64]$observedPid = 0
    if (-not [uint64]::TryParse($pidText, [Globalization.NumberStyles]::None,
            [Globalization.CultureInfo]::InvariantCulture, [ref]$observedPid) -or
        $observedPid -eq 0 -or $observedPid -gt [uint32]::MaxValue -or
        $observedPid.ToString([Globalization.CultureInfo]::InvariantCulture) -cne $pidText) {
        throw [IO.InvalidDataException]::new('M0P031 Linux proc stat PID is invalid.')
    }
    $comm = $ProcStat.Substring($openIndex + 2, $closeIndex - ($openIndex + 2))
    if ([string]::IsNullOrEmpty($comm) -or $comm.Contains("`r") -or $comm.Contains("`n")) {
        throw [IO.InvalidDataException]::new('M0P031 Linux proc stat comm is invalid.')
    }
    $tail = $ProcStat.Substring($closeIndex + 2)
    $fields = @($tail -split ' ' | Where-Object { $_.Length -gt 0 })
    if ($fields.Count -lt 20 -or $fields[0] -cnotmatch '^[A-Za-z]$') {
        throw [IO.InvalidDataException]::new('M0P031 Linux proc stat is truncated before field 22.')
    }
    $startTicks = [string]$fields[19]
    if ($startTicks -cnotmatch '^[1-9][0-9]{0,19}$') {
        throw [IO.InvalidDataException]::new('M0P031 Linux proc stat starttime field 22 is invalid.')
    }

    if ($observedPid -ne $expectedPidValue -or $startTicks -cne $ExpectedStartTicks -or
        $ObservedBootIdSha256 -cne $ExpectedBootIdSha256) {
        return [pscustomobject][ordered]@{
            state = 'unavailable'
            error_code = 'linux_identity_mismatch'
            missing_capabilities = @('stable-linux-process-identity')
            comm = $null
            start_ticks = $null
        }
    }
    if (-not [bool]$PidfdAvailable) {
        return [pscustomobject][ordered]@{
            state = 'partial'
            error_code = 'linux_pidfd_unavailable'
            missing_capabilities = @('pidfd', 'linux-live-smoke', 'exclusive-cgroup-v2')
            comm = $comm
            start_ticks = $startTicks
        }
    }
    return [pscustomobject][ordered]@{
        state = 'partial'
        error_code = 'linux_live_not_proven'
        missing_capabilities = @('linux-live-smoke', 'exclusive-cgroup-v2')
        comm = $comm
        start_ticks = $startTicks
    }
}

function Get-M0ProcessPilotState {
    param([Parameter(Mandatory)] $Pilot)

    $properties = @($Pilot.PSObject.Properties.Name | Sort-Object)
    if ($properties.Count -ne 3 -or $properties[0] -cne 'module_instance_id' -or
        $properties[1] -cne 'opaque_id' -or $properties[2] -cne 'schema_id' -or
        [string]$Pilot.schema_id -cne 'm0.process-pilot-capability.v1' -or
        [string]$Pilot.module_instance_id -cne $script:M0ProcessModuleInstanceId -or
        [string]$Pilot.opaque_id -cnotmatch '^[a-f0-9]{32}$') {
        throw [UnauthorizedAccessException]::new('M0P041 invalid opaque process pilot capability token.')
    }
    $opaqueId = [string]$Pilot.opaque_id
    if (-not $script:M0ProcessPilotRegistry.ContainsKey($opaqueId)) {
        throw [UnauthorizedAccessException]::new('M0P041 unregistered opaque process pilot capability token.')
    }
    $state = $script:M0ProcessPilotRegistry[$opaqueId]
    if (-not [object]::ReferenceEquals($Pilot, $state.public_capability)) {
        throw [UnauthorizedAccessException]::new('M0P041 reconstructed opaque process pilot capability is forbidden.')
    }
    return $state
}

function ConvertTo-M0ProcessPublicJson {
    param([Parameter(Mandatory)] $Value)

    return ($Value | ConvertTo-Json -Compress -Depth 20)
}

function ConvertFrom-M0ProcessPublicJson {
    param([Parameter(Mandatory)] [string] $Json)

    return ($Json | ConvertFrom-Json -Depth 20 -DateKind String)
}

function Assert-M0PilotHelperFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $ExpectedSha256
    )

    if ($ExpectedSha256 -cnotmatch '^[a-f0-9]{64}$') {
        throw [ArgumentException]::new('M0P040 trusted helper SHA-256 is invalid.')
    }
    $path = [IO.Path]::GetFullPath($LiteralPath)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new('M0P040 the literal anchored helper is missing.')
    }
    $stream = $null
    try {
        $stream = [IO.FileStream]::new(
            $path,
            [IO.FileMode]::Open,
            [IO.FileAccess]::Read,
            [IO.FileShare]::Read
        )
        [M0ProcessNativeV1.TrustedFile]::Validate($stream.SafeFileHandle, $path)
        $actualSha256 = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData($stream)
        ).ToLowerInvariant()
        if ($actualSha256 -cne $ExpectedSha256) {
            throw [IO.InvalidDataException]::new('M0P040 synthetic helper SHA-256 differs from the anchored descriptor.')
        }
        $stream.Position = 0
        $result = $stream
        $stream = $null
        return $result
    }
    catch {
        if ($null -ne $stream) { $stream.Dispose() }
        throw
    }
}

function Assert-M0PilotRepositoryRoot {
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new('M0P040 repository root is missing.')
    }
    if (-not [string]::Equals($root, $script:M0ProcessRepositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw [UnauthorizedAccessException]::new('M0P040 repository root differs from the trusted root derived from the loaded module.')
    }
    return $root
}

function Assert-M0PilotDirectoryChain {
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [string] $RunRoot,
        [switch] $CreateParents
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $paths = [Collections.Generic.List[string]]::new()
    $paths.Add($root)
    $paths.Add((Join-Path $root 'out'))
    $paths.Add((Join-Path $root 'out\m0'))
    $paths.Add((Join-Path $root 'out\m0\process-pilot'))
    for ($index = 0; $index -lt $paths.Count; $index++) {
        $path = [IO.Path]::GetFullPath($paths[$index]).TrimEnd('\', '/')
        if (-not [string]::Equals($path, $root, [StringComparison]::OrdinalIgnoreCase) -and
            -not $path.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw [IO.InvalidDataException]::new('M0P040 output directory ancestor escaped the repository root.')
        }
        if (-not (Test-Path -LiteralPath $path -PathType Container)) {
            if (-not $CreateParents -or $index -eq 0) {
                throw [IO.DirectoryNotFoundException]::new('M0P040 required output directory ancestor is missing.')
            }
            New-Item -ItemType Directory -Path $path | Out-Null
        }
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            throw [IO.InvalidDataException]::new('M0P040 linked output directory ancestor is forbidden.')
        }
    }
    $pilotParent = [IO.Path]::GetFullPath($paths[$paths.Count - 1]).TrimEnd('\', '/')
    if (-not [string]::IsNullOrEmpty($RunRoot)) {
        $runPath = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/')
        if ([IO.Path]::GetFullPath((Split-Path -Parent $runPath)).TrimEnd('\', '/') -cne $pilotParent -or
            (Split-Path -Leaf $runPath) -cnotmatch '^[a-f0-9]{32}$' -or
            -not (Test-Path -LiteralPath $runPath -PathType Container)) {
            throw [IO.InvalidDataException]::new('M0P040 process pilot run leaf is outside the closed GUID boundary.')
        }
        $runItem = Get-Item -LiteralPath $runPath -Force -ErrorAction Stop
        if (($runItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($runItem.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$runItem.LinkType))) {
            throw [IO.InvalidDataException]::new('M0P040 linked process pilot run leaf is forbidden.')
        }
    }
    return $pilotParent
}

function Remove-M0PilotRunLeaf {
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $RunRoot
    )

    try {
        $expectedParent = Assert-M0PilotDirectoryChain -RepositoryRoot $RepositoryRoot -RunRoot $RunRoot
    }
    catch {
        return $false
    }
    $runPath = [IO.Path]::GetFullPath($RunRoot).TrimEnd('\', '/')
    $actualParent = [IO.Path]::GetFullPath((Split-Path -Parent $runPath)).TrimEnd('\', '/')
    $leaf = Split-Path -Leaf $runPath
    if ($leaf -cnotmatch '^[a-f0-9]{32}$' -or $actualParent -cne $expectedParent) {
        return $false
    }
    foreach ($path in @($expectedParent, $runPath)) {
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            return $false
        }
    }
    $allowedMarkerNames = [Collections.Generic.HashSet[string]]::new(
        [string[]]@('root.ready.json', 'child.ready.json', 'grandchild.ready.json'),
        [StringComparer]::Ordinal
    )
    $safe = $true
    foreach ($entry in @(Get-ChildItem -LiteralPath $runPath -Force)) {
        if (-not $allowedMarkerNames.Contains($entry.Name) -or $entry.PSIsContainer -or
            ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($entry.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$entry.LinkType))) {
            $safe = $false
            continue
        }
        Remove-Item -LiteralPath $entry.FullName -Force
    }
    if (-not $safe -or @(Get-ChildItem -LiteralPath $runPath -Force).Count -ne 0) {
        return $false
    }
    Remove-Item -LiteralPath $runPath -Force
    return $true
}

function ConvertTo-M0CanonicalProcessTreeSnapshot {
    param([Parameter(Mandatory)] $Snapshot)

    function ConvertIdentity {
        param([Parameter(Mandatory)] $Identity)
        return [pscustomobject][ordered]@{
            pid = [uint32]$Identity.pid
            creation_time_filetime_utc = [string]$Identity.creation_time_filetime_utc
        }
    }
    function IdentityKey {
        param([Parameter(Mandatory)] $Identity)
        return "$([uint32]$Identity.pid)|$([string]$Identity.creation_time_filetime_utc)"
    }

    $roleOrder = @('root', 'child', 'grandchild')
    $nodeByRole = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::Ordinal)
    $identityKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($node in @($Snapshot.nodes)) {
        $role = [string]$node.role
        if ($nodeByRole.ContainsKey($role)) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree snapshot contains a duplicate role.')
        }
        $nodeByRole[$role] = $node
        if (-not $identityKeys.Add((IdentityKey -Identity $node.identity))) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree snapshot contains a duplicate identity.')
        }
    }
    $declaredRoles = @($Snapshot.roles | ForEach-Object { [string]$_ })
    if ($declaredRoles.Count -ne $nodeByRole.Count) {
        throw [IO.InvalidDataException]::new('M0P060 process-tree role inventory differs from its node inventory.')
    }
    for ($index = 0; $index -lt $declaredRoles.Count; $index++) {
        if ($declaredRoles[$index] -cne $roleOrder[$index] -or
            -not $nodeByRole.ContainsKey($declaredRoles[$index])) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree roles are not the closed observed topology prefix.')
        }
    }
    for ($index = 0; $index -lt $declaredRoles.Count; $index++) {
        $node = $nodeByRole[$declaredRoles[$index]]
        if ($index -eq 0) {
            if ($null -ne $node.parent_identity) {
                throw [IO.InvalidDataException]::new('M0P060 process-tree root cannot have a parent identity.')
            }
            continue
        }
        $parent = $nodeByRole[$declaredRoles[$index - 1]]
        if ($null -eq $node.parent_identity -or
            (IdentityKey -Identity $node.parent_identity) -cne (IdentityKey -Identity $parent.identity)) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree snapshot parent identities do not form the observed role prefix.')
        }
    }

    $canonicalNodes = [Collections.Generic.List[object]]::new()
    foreach ($role in $declaredRoles) {
        $node = $nodeByRole[$role]
        $canonicalNodes.Add([pscustomobject][ordered]@{
            role = $role
            identity = ConvertIdentity -Identity $node.identity
            parent_identity = if ($null -eq $node.parent_identity) { $null } else { ConvertIdentity -Identity $node.parent_identity }
            executable_sha256 = [string]$node.executable_sha256
            job_membership_revalidated = [bool]$node.job_membership_revalidated
        })
    }
    $canonicalAuxiliary = [Collections.Generic.List[object]]::new()
    foreach ($node in @($Snapshot.auxiliary_job_members | Sort-Object `
            @{ Expression = { [uint64]$_.identity.pid }; Ascending = $true }, `
            @{ Expression = { [string]$_.identity.creation_time_filetime_utc }; Ascending = $true })) {
        if (-not $identityKeys.Add((IdentityKey -Identity $node.identity))) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree auxiliary set contains a duplicate identity.')
        }
        $canonicalAuxiliary.Add([pscustomobject][ordered]@{
            identity = ConvertIdentity -Identity $node.identity
            job_membership_revalidated = [bool]$node.job_membership_revalidated
            role = 'job-auxiliary'
        })
    }
    if ([int]$Snapshot.job_member_hint_count -ne $identityKeys.Count -or
        [int]$Snapshot.retained_identity_count -ne $identityKeys.Count) {
        throw [IO.InvalidDataException]::new('M0P060 process-tree retained counts differ from its unique identity set.')
    }
    $missingSet = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($capability in @($Snapshot.missing_capabilities)) {
        if (-not $missingSet.Add([string]$capability)) {
            throw [IO.InvalidDataException]::new('M0P060 process-tree contains a duplicate missing capability.')
        }
    }
    $state = [string]$Snapshot.state
    $errorCode = if ($null -eq $Snapshot.error_code) { $null } else { [string]$Snapshot.error_code }
    $traceState = [string]$Snapshot.trace.state
    if ($missingSet.Contains('safe-process-identity') -and
        ($state -cne 'unavailable' -or $errorCode -cne 'safe_identity_unavailable')) {
        throw [IO.InvalidDataException]::new('M0P060 missing safe process identity is only valid for the closed unavailable identity cause.')
    }
    if ($missingSet.Contains('root-handle') -and
        ($state -cne 'partial' -or $errorCode -cne 'root_identity_missing')) {
        throw [IO.InvalidDataException]::new('M0P060 missing root handle is only valid for the closed missing-root cause.')
    }
    if ($missingSet.Contains('external-attribution-proof')) {
        $expectedExternalError = if ($missingSet.Contains('root-handle')) {
            'root_identity_missing'
        }
        else {
            'unresolved_external_attribution'
        }
        if ($state -cne 'partial' -or $errorCode -cne $expectedExternalError) {
            throw [IO.InvalidDataException]::new('M0P060 missing external attribution proof contradicts closed root-cause precedence.')
        }
    }
    if ($missingSet.Contains('job-completion-port') -and [bool]$Snapshot.capabilities.completion_port) {
        throw [IO.InvalidDataException]::new('M0P060 missing completion-port capability contradicts its live capability flag.')
    }
    if ($state -ceq 'partial' -and -not [bool]$Snapshot.capabilities.completion_port -and
        -not $missingSet.Contains('job-completion-port')) {
        throw [IO.InvalidDataException]::new('M0P060 absent partial completion-port capability is not recorded as missing.')
    }
    if ($traceState -ceq 'complete' -and
        ($missingSet.Contains('etw-process-start-stop') -or
            -not [bool]$Snapshot.capabilities.etw_process_start_stop)) {
        throw [IO.InvalidDataException]::new('M0P060 complete ETW trace contradicts its capability or missing-capability inventory.')
    }
    if ($traceState -in @('partial', 'unavailable') -and
        (-not $missingSet.Contains('etw-process-start-stop') -or
            [bool]$Snapshot.capabilities.etw_process_start_stop)) {
        throw [IO.InvalidDataException]::new('M0P060 incomplete ETW trace lacks its missing capability or claims a live ETW capability.')
    }
    if ($state -ceq 'complete') {
        if ($null -ne $errorCode -or $missingSet.Count -ne 0 -or
            [string]$Snapshot.trace.state -cne 'complete' -or -not [bool]$Snapshot.trace.complete -or
            -not [bool]$Snapshot.trace.collected -or $declaredRoles.Count -ne 3) {
            throw [IO.InvalidDataException]::new('M0P060 complete process-tree state contradicts its trace, errors or topology.')
        }
    }
    elseif ($state -ceq 'partial') {
        switch ($errorCode) {
            'etw_live_not_implemented' {
                if (-not $missingSet.Contains('etw-process-start-stop') -or
                    [string]$Snapshot.trace.state -cne 'partial' -or [bool]$Snapshot.trace.complete -or
                    [bool]$Snapshot.trace.collected -or [string]$Snapshot.trace.error_code -cne 'etw_live_not_implemented' -or
                    @($Snapshot.trace.gap_codes).Count -ne 1 -or [string]$Snapshot.trace.gap_codes[0] -cne 'not-collected' -or
                    [bool]$Snapshot.capabilities.etw_process_start_stop) {
                    throw [IO.InvalidDataException]::new('M0P060 ETW-live process-tree cause contradicts trace health or capability state.')
                }
            }
            'etw_trace_incomplete' {
                if (-not $missingSet.Contains('etw-process-start-stop') -or
                    [string]$Snapshot.trace.state -cne 'partial' -or [bool]$Snapshot.trace.complete -or
                    -not [bool]$Snapshot.trace.collected -or [string]$Snapshot.trace.error_code -cne 'etw_trace_incomplete' -or
                    [bool]$Snapshot.capabilities.etw_process_start_stop) {
                    throw [IO.InvalidDataException]::new('M0P060 ETW-gap process-tree cause contradicts trace health or capability state.')
                }
            }
            'root_identity_missing' {
                if (-not $missingSet.Contains('root-handle') -or $declaredRoles.Count -ne 0 -or
                    $identityKeys.Count -ne 0 -or [string]$Snapshot.cleanup_authority -cne 'none-no-mutation-capability') {
                    throw [IO.InvalidDataException]::new('M0P060 missing-root process-tree cause contradicts topology or mutation authority.')
                }
            }
            'unresolved_external_attribution' {
                if (-not $missingSet.Contains('external-attribution-proof')) {
                    throw [IO.InvalidDataException]::new('M0P060 external-attribution process-tree cause lacks its missing proof capability.')
                }
            }
            default {
                throw [IO.InvalidDataException]::new('M0P060 partial process-tree uses an unknown error cause.')
            }
        }
    }
    elseif ($state -ceq 'unavailable') {
        $requiredMissing = if ($errorCode -ceq 'safe_identity_unavailable') {
            'safe-process-identity'
        }
        elseif ($errorCode -ceq 'etw_trace_unavailable') {
            'etw-process-start-stop'
        }
        else {
            throw [IO.InvalidDataException]::new('M0P060 unavailable process-tree uses an unknown error cause.')
        }
        if (-not $missingSet.Contains($requiredMissing) -or $declaredRoles.Count -ne 0 -or
            $identityKeys.Count -ne 0 -or [string]$Snapshot.trace.state -cne 'unavailable' -or
            [string]$Snapshot.cleanup_authority -cne 'none-no-mutation-capability') {
            throw [IO.InvalidDataException]::new('M0P060 unavailable process-tree contradicts topology, trace or missing capability.')
        }
    }
    else {
        throw [IO.InvalidDataException]::new('M0P060 process-tree uses an unknown state.')
    }
    if ($declaredRoles.Count -eq 0) {
        if ($canonicalAuxiliary.Count -ne 0 -or $identityKeys.Count -ne 0 -or
            [string]$Snapshot.cleanup_authority -cne 'none-no-mutation-capability' -or
            [bool]$Snapshot.capabilities.job_object_kill_on_close -or
            [bool]$Snapshot.capabilities.membership_handles_revalidated) {
            throw [IO.InvalidDataException]::new('M0P060 empty process-tree topology cannot retain Job or mutation authority.')
        }
    }
    elseif (-not [bool]$Snapshot.capabilities.job_object_kill_on_close -or
        -not [bool]$Snapshot.capabilities.membership_handles_revalidated) {
        throw [IO.InvalidDataException]::new('M0P060 observed process-tree topology lacks its retained Job membership authority.')
    }
    $qpcBefore = if ($null -eq $Snapshot.qpc_before_create_ticks) { $null } else { [string]$Snapshot.qpc_before_create_ticks }
    $qpcAfter = if ($null -eq $Snapshot.qpc_after_resume_ticks) { $null } else { [string]$Snapshot.qpc_after_resume_ticks }
    $qpcFrequency = if ($null -eq $Snapshot.qpc_frequency) { $null } else { [string]$Snapshot.qpc_frequency }
    if (($null -eq $qpcBefore) -ne ($null -eq $qpcFrequency)) {
        throw [IO.InvalidDataException]::new('M0P060 process-tree QPC boundary and frequency availability differ.')
    }
    if ($declaredRoles.Count -gt 0 -and $null -eq $qpcAfter) {
        throw [IO.InvalidDataException]::new('M0P060 an observed process-tree requires both launch QPC boundaries.')
    }
    if ($null -ne $qpcAfter -and
        ($null -eq $qpcBefore -or $null -eq $qpcFrequency -or
            [Numerics.BigInteger]::Parse($qpcAfter) -le [Numerics.BigInteger]::Parse($qpcBefore))) {
        throw [IO.InvalidDataException]::new('M0P060 process-tree QPC ordering is not causal.')
    }

    $missingOrder = @(
        'etw-process-start-stop', 'job-completion-port', 'root-handle',
        'external-attribution-proof', 'safe-process-identity'
    )
    $canonicalMissing = @($Snapshot.missing_capabilities | Sort-Object {
        [Array]::IndexOf($missingOrder, [string]$_)
    } | ForEach-Object { [string]$_ })
    $gapOrder = @(
        'not-collected', 'lost-event', 'buffer-overflow', 'trace-started-after-launch',
        'identity-conflict', 'parentage-conflict', 'adapter-unavailable'
    )
    $canonicalGaps = @($Snapshot.trace.gap_codes | Sort-Object {
        [Array]::IndexOf($gapOrder, [string]$_)
    } | ForEach-Object { [string]$_ })

    return [pscustomobject][ordered]@{
        '$schema' = [string]$Snapshot.'$schema'
        schema_id = [string]$Snapshot.schema_id
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        platform = [string]$Snapshot.platform
        state = [string]$Snapshot.state
        error_code = if ($null -eq $Snapshot.error_code) { $null } else { [string]$Snapshot.error_code }
        missing_capabilities = $canonicalMissing
        trace = [pscustomobject][ordered]@{
            state = [string]$Snapshot.trace.state
            adapter = [string]$Snapshot.trace.adapter
            complete = [bool]$Snapshot.trace.complete
            collected = [bool]$Snapshot.trace.collected
            gap_codes = $canonicalGaps
            error_code = if ($null -eq $Snapshot.trace.error_code) { $null } else { [string]$Snapshot.trace.error_code }
        }
        capabilities = [pscustomobject][ordered]@{
            job_object_kill_on_close = [bool]$Snapshot.capabilities.job_object_kill_on_close
            breakaway_flags = [string]$Snapshot.capabilities.breakaway_flags
            membership_handles_revalidated = [bool]$Snapshot.capabilities.membership_handles_revalidated
            helper_sha256_verified = [bool]$Snapshot.capabilities.helper_sha256_verified
            helper_file_locked_until_readiness = [bool]$Snapshot.capabilities.helper_file_locked_until_readiness
            completion_port = [bool]$Snapshot.capabilities.completion_port
            etw_process_start_stop = [bool]$Snapshot.capabilities.etw_process_start_stop
            linux_live_status = [string]$Snapshot.capabilities.linux_live_status
        }
        roles = $declaredRoles
        nodes = @($canonicalNodes)
        auxiliary_job_members = @($canonicalAuxiliary)
        job_member_hint_count = [int]$Snapshot.job_member_hint_count
        retained_identity_count = [int]$Snapshot.retained_identity_count
        qpc_before_create_ticks = $qpcBefore
        qpc_after_resume_ticks = $qpcAfter
        qpc_frequency = $qpcFrequency
        cleanup_authority = [string]$Snapshot.cleanup_authority
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
    }
}

function New-M0ProcessTreeArtifact {
    [CmdletBinding(DefaultParameterSetName = 'Snapshot')]
    param(
        [Parameter(Mandatory, ParameterSetName = 'Snapshot')] $Snapshot,
        [Parameter(Mandatory, ParameterSetName = 'Unavailable')]
        [ValidateSet('etw_trace_unavailable', 'safe_identity_unavailable')]
        [string] $UnavailableErrorCode,
        [Parameter(Mandatory)] [string] $ExperimentId,
        [Parameter(Mandatory)] [string] $RunId
    )

    foreach ($binding in @(
        [pscustomobject]@{ name = 'experiment_id'; value = $ExperimentId },
        [pscustomobject]@{ name = 'run_id'; value = $RunId }
    )) {
        if ($binding.value -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$') {
            throw [ArgumentException]::new("M0P060 artifact $($binding.name) is outside the evidence ID contract.")
        }
    }
    $artifacts = Assert-M0ProcessArtifacts -ProtocolRoot (Join-Path $script:M0ProcessRepositoryRoot 'experiments\m0\protocol')
    if ($PSCmdlet.ParameterSetName -ceq 'Unavailable') {
        $unavailableErrors = [Collections.Generic.HashSet[string]]::new(
            [string[]]@('etw_trace_unavailable', 'safe_identity_unavailable'), [StringComparer]::Ordinal
        )
        if (-not $unavailableErrors.Contains($UnavailableErrorCode)) {
            throw [ArgumentException]::new('M0P060 unavailable fallback error must use the canonical ordinal enum spelling.')
        }
        $Snapshot = [pscustomobject][ordered]@{
            '$schema' = 'schemas/process-tree.schema.json'
            schema_id = 'm0.process-tree-smoke.v1'
            candidate_code = 'P'
            scenario_id = 'synthetic-process-tree'
            attempt = 1
            platform = 'windows'
            state = 'unavailable'
            error_code = $UnavailableErrorCode
            missing_capabilities = @(if ($UnavailableErrorCode -ceq 'safe_identity_unavailable') {
                    'etw-process-start-stop'
                    'safe-process-identity'
                }
                else {
                    'etw-process-start-stop'
                })
            trace = [pscustomobject][ordered]@{
                state = 'unavailable'
                adapter = 'etw-process-start-stop'
                complete = $false
                collected = $false
                gap_codes = @('adapter-unavailable')
                error_code = 'etw_trace_unavailable'
            }
            capabilities = [pscustomobject][ordered]@{
                job_object_kill_on_close = $false
                breakaway_flags = 'none'
                membership_handles_revalidated = $false
                helper_sha256_verified = $false
                helper_file_locked_until_readiness = $false
                completion_port = $false
                etw_process_start_stop = $false
                linux_live_status = 'not_proven'
            }
            roles = @()
            nodes = @()
            auxiliary_job_members = @()
            job_member_hint_count = 0
            retained_identity_count = 0
            qpc_before_create_ticks = $null
            qpc_after_resume_ticks = $null
            qpc_frequency = $null
            cleanup_authority = 'none-no-mutation-capability'
            evidence_role = 'synthetic_pilot'
            decision_eligible = $false
        }
    }
    $snapshotJson = ConvertTo-M0ProcessPublicJson -Value $Snapshot
    if (-not (Test-Json -Json $snapshotJson -SchemaFile $artifacts.snapshot_schema_path -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0P060 artifact input is not a closed synthetic P process-tree snapshot.')
    }
    $snapshotCopy = ConvertTo-M0CanonicalProcessTreeSnapshot -Snapshot (
        ConvertFrom-M0ProcessPublicJson -Json $snapshotJson
    )
    $document = [pscustomobject][ordered]@{
        '$schema' = 'schemas/process-tree-artifact.schema.json'
        schema_id = 'm0.process-tree-artifact.v1'
        schema_version = 1
        experiment_id = $ExperimentId
        run_id = $RunId
        artifact_id = 'process-tree'
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        snapshot = $snapshotCopy
    }
    $canonicalJson = (ConvertTo-M0ProcessPublicJson -Value $document) + "`n"
    if (-not (Test-Json -Json $canonicalJson -SchemaFile $artifacts.artifact_schema_path -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0P060 bound process-tree artifact failed its closed schema.')
    }
    $bytes = [Text.UTF8Encoding]::new($false, $true).GetBytes($canonicalJson)
    $sha256 = Get-M0ProcessSha256Bytes -Bytes $bytes
    return [pscustomobject][ordered]@{
        artifact_id = 'process-tree'
        relative_path = "artifacts/$RunId.process-tree.json"
        sha256 = $sha256
        size_bytes = [long]$bytes.Length
        transient = $false
        redacted = $true
        canonical_json = $canonicalJson
        document = ConvertFrom-M0ProcessPublicJson -Json ($canonicalJson.TrimEnd("`n"))
    }
}

function Resolve-M0ProcessPilotOutcome {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [ValidateSet('complete', 'partial', 'unavailable')] [string] $ProcessState,
        [AllowNull()] $ProcessErrorCode,
        [Parameter(Mandatory)] $CleanupComplete,
        [Parameter(Mandatory)] $SurvivorCount,
        [AllowNull()] $CleanupErrorCode
    )

    $processStates = [Collections.Generic.HashSet[string]]::new(
        [string[]]@('complete', 'partial', 'unavailable'), [StringComparer]::Ordinal
    )
    $partialProcessErrorCodes = [Collections.Generic.HashSet[string]]::new(
        [string[]]@(
            'etw_live_not_implemented', 'etw_trace_incomplete',
            'root_identity_missing', 'unresolved_external_attribution'
        ),
        [StringComparer]::Ordinal
    )
    $unavailableProcessErrorCodes = [Collections.Generic.HashSet[string]]::new(
        [string[]]@('etw_trace_unavailable', 'safe_identity_unavailable'), [StringComparer]::Ordinal
    )
    $cleanupErrorCodes = [Collections.Generic.HashSet[string]]::new(
        [string[]]@(
            'snapshot_validation_failed', 'identity_revalidation_failed', 'retained_handle_wait_failed',
            'helper_survived_shutdown', 'job_close_failed', 'unsafe_run_root_contents'
        ),
        [StringComparer]::Ordinal
    )
    if (-not $processStates.Contains($ProcessState)) {
        throw [ArgumentException]::new('M0P061 process state must use the canonical ordinal enum spelling.')
    }
    if ($CleanupComplete -isnot [bool]) {
        throw [ArgumentException]::new('M0P061 cleanup completion must be a boolean.')
    }
    $survivorIsInteger = $false
    foreach ($type in @([byte], [sbyte], [int16], [uint16], [int32], [uint32], [int64])) {
        if ($SurvivorCount -is $type) { $survivorIsInteger = $true; break }
    }
    [int64]$survivors = 0
    if (-not $survivorIsInteger -or -not [int64]::TryParse([string]$SurvivorCount, [ref]$survivors) -or
        $survivors -lt 0 -or $survivors -gt 256) {
        throw [ArgumentException]::new('M0P061 survivor count must be a canonical nonnegative integer within the pilot bound.')
    }
    if ($ProcessState -ceq 'complete') {
        if ($null -ne $ProcessErrorCode) {
            throw [ArgumentException]::new('M0P061 process state and error code contradict the closed outcome contract.')
        }
    }
    elseif ($ProcessErrorCode -isnot [string] -or
        ($ProcessState -ceq 'partial' -and -not $partialProcessErrorCodes.Contains([string]$ProcessErrorCode)) -or
        ($ProcessState -ceq 'unavailable' -and -not $unavailableProcessErrorCodes.Contains([string]$ProcessErrorCode))) {
        throw [ArgumentException]::new('M0P061 process state requires a closed process error enum value with canonical ordinal spelling.')
    }
    if ([bool]$CleanupComplete) {
        if ($survivors -ne 0 -or $null -ne $CleanupErrorCode) {
            throw [ArgumentException]::new('M0P061 completed cleanup cannot have survivors or an error code.')
        }
    }
    elseif ($CleanupErrorCode -isnot [string] -or
        -not $cleanupErrorCodes.Contains([string]$CleanupErrorCode)) {
        throw [ArgumentException]::new('M0P061 incomplete cleanup requires a closed cleanup error enum value.')
    }
    if ($survivors -eq 0 -and [string]$CleanupErrorCode -in @(
            'retained_handle_wait_failed', 'helper_survived_shutdown'
        )) {
        throw [ArgumentException]::new('M0P061 survivor cleanup errors require at least one retained-handle survivor.')
    }

    $checkStatus = $null
    $checkReason = $null
    $checkStatusReason = $null
    $checkError = $null
    $runStatus = $null
    $runStatusReason = $null
    $runError = $null
    if ($survivors -gt 0) {
        $checkStatus = 'failed'
        $checkReason = 'helper_survived_shutdown'
        $checkStatusReason = 'helper_survived_shutdown'
        $checkError = 'helper_survived_shutdown'
        $runStatus = 'failed'
        $runStatusReason = 'helper_survived_shutdown'
        $runError = 'helper_survived_shutdown'
    }
    elseif (-not [bool]$CleanupComplete) {
        $checkStatus = 'skipped'
        $checkReason = [string]$CleanupErrorCode
        $checkStatusReason = 'incomplete_evidence'
        $runStatus = 'inconclusive'
        $runStatusReason = 'incomplete_evidence'
    }
    elseif ($ProcessState -ceq 'complete') {
        $checkStatus = 'passed'
        $runStatus = 'valid'
    }
    else {
        $checkStatus = 'skipped'
        $checkReason = "process_tree_$ProcessState"
        $checkStatusReason = 'incomplete_evidence'
        $runStatus = 'inconclusive'
        $runStatusReason = 'incomplete_evidence'
    }
    $checkPayload = [pscustomobject][ordered]@{
        check_id = 'CHK-PROCESS'
        status = $checkStatus
        reason_code = $checkReason
        status_reason = $checkStatusReason
        error_code = $checkError
    }
    $checksOmitted = if ($checkStatus -ceq 'skipped') {
        @([pscustomobject][ordered]@{ check_id = 'CHK-PROCESS'; reason_code = $checkReason })
    }
    else { @() }
    return [pscustomobject][ordered]@{
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        check_payload = $checkPayload
        run_status = $runStatus
        run_status_reason = $runStatusReason
        run_error_code = $runError
        checks_omitted = $checksOmitted
    }
}

function Open-M0SyntheticProcessPilot {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    if (-not $IsWindows) {
        throw [PlatformNotSupportedException]::new('M0P040 the live synthetic process smoke is Windows-only; Linux remains not proven.')
    }
    $root = Assert-M0PilotRepositoryRoot -RepositoryRoot $RepositoryRoot

    function Assert-M0PilotUnlinkedPath {
        param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $Name)
        $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($item.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$item.LinkType))) {
            throw [IO.InvalidDataException]::new("M0P040 $Name cannot be a reparse point, symlink or hardlink.")
        }
    }

    Assert-M0PilotUnlinkedPath -Path $root -Name 'repository root'
    $protocolRoot = Join-Path $root 'experiments\m0\protocol'
    $processArtifacts = Assert-M0ProcessArtifacts -ProtocolRoot $protocolRoot
    $helperPath = [IO.Path]::GetFullPath((Join-Path $root ([string]$processArtifacts.descriptor.pilot_contract.helper_relative_path)))
    if (-not $helperPath.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $helperPath -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new('M0P040 the literal synthetic helper is missing or outside the repository.')
    }
    foreach ($path in @(
        (Join-Path $root 'scripts'),
        (Join-Path $root 'scripts\m0'),
        $helperPath
    )) {
        Assert-M0PilotUnlinkedPath -Path $path -Name 'synthetic helper path'
    }
    $pilotParent = Assert-M0PilotDirectoryChain -RepositoryRoot $root -CreateParents
    $runRoot = Join-Path $pilotParent ([Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $runRoot | Out-Null
    $null = Assert-M0PilotDirectoryChain -RepositoryRoot $root -RunRoot $runRoot

    $nonce = [Guid]::NewGuid().ToString('N')
    $pwshPath = [IO.Path]::GetFullPath((Get-Process -Id $PID).Path)
    $native = $null
    $helperLock = $null
    try {
        $helperLock = Assert-M0PilotHelperFile -LiteralPath $helperPath `
            -ExpectedSha256 ([string]$processArtifacts.descriptor.pilot_contract.helper_sha256)
        $arguments = @(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-File', $helperPath,
            '-Role', 'root', '-Nonce', $nonce, '-ReadyRoot', $runRoot, '-ParentPid', '0'
        )
        $native = [M0ProcessNativeV1.PilotSession]::Create($pwshPath, $arguments, $root)
        $opaqueId = [Guid]::NewGuid().ToString('N')
        $publicCapability = [pscustomobject][ordered]@{
            schema_id = 'm0.process-pilot-capability.v1'
            opaque_id = $opaqueId
            module_instance_id = $script:M0ProcessModuleInstanceId
        }
        $state = [pscustomobject][ordered]@{
            public_capability = $publicCapability
            native = $native
            helper_lock = $helperLock
            snapshot_schema_path = [string]$processArtifacts.snapshot_schema_path
            repository_root = $root
            run_root = $runRoot
            nonce_sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
                [Text.Encoding]::UTF8.GetBytes($nonce))).ToLowerInvariant()
            executable_sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData(
                [IO.File]::ReadAllBytes($pwshPath))).ToLowerInvariant()
            capabilities = [Collections.Generic.List[object]]::new()
            snapshot_json = $null
            close_result_json = $null
            native_cleanup_actions = 0
        }
        $script:M0ProcessPilotRegistry[$opaqueId] = $state
        $native = $null
        $helperLock = $null
        return $publicCapability
    }
    catch {
        if ($null -ne $native) { $native.Dispose() }
        if ($null -ne $helperLock) { $helperLock.Dispose() }
        $null = Remove-M0PilotRunLeaf -RepositoryRoot $root -RunRoot $runRoot
        throw
    }
}

function Get-M0SyntheticProcessPilotSnapshot {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Pilot)

    $state = Get-M0ProcessPilotState -Pilot $Pilot
    if ($null -ne $state.snapshot_json) {
        return ConvertFrom-M0ProcessPublicJson -Json $state.snapshot_json
    }
    if ($null -ne $state.close_result_json) {
        throw [InvalidOperationException]::new('M0P041 a closed pilot cannot produce a new snapshot.')
    }

    $roles = @('root', 'child', 'grandchild')
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $ready = $true
        foreach ($role in $roles) {
            if (-not (Test-Path -LiteralPath (Join-Path $state.run_root "$role.ready.json") -PathType Leaf)) {
                $ready = $false
                break
            }
        }
        if (-not $ready) { [Threading.Thread]::Sleep(25) }
    } while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if (-not $ready) {
        throw [TimeoutException]::new('M0P042 synthetic root/child/grandchild readiness timed out.')
    }

    $jobPids = @($state.native.QueryProcessIds())
    $jobPidSet = [Collections.Generic.HashSet[uint32]]::new()
    foreach ($pidHint in $jobPids) {
        if (-not $jobPidSet.Add([uint32]$pidHint)) {
            throw [IO.InvalidDataException]::new('M0P042 Job PID hint list contains a duplicate identity hint.')
        }
    }
    if ($jobPidSet.Count -lt $roles.Count) {
        throw [IO.InvalidDataException]::new('M0P042 Job membership set cannot contain the closed three-node synthetic tree.')
    }
    $nodes = [Collections.Generic.List[object]]::new()
    $auxiliaryJobMembers = [Collections.Generic.List[object]]::new()
    $identityByRole = @{}
    $births = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $rolePidSet = [Collections.Generic.HashSet[uint32]]::new()
    $identityKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($role in $roles) {
        $markerPath = Join-Path $state.run_root "$role.ready.json"
        $markerItem = Get-Item -LiteralPath $markerPath -Force
        if (($markerItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($markerItem.PSObject.Properties['LinkType'] -and -not [string]::IsNullOrEmpty([string]$markerItem.LinkType))) {
            throw [IO.InvalidDataException]::new('M0P042 synthetic readiness marker cannot be a link.')
        }
        $text = [IO.File]::ReadAllText($markerPath, $script:M0ProcessUtf8)
        if ($text.Contains("`r") -or -not $text.EndsWith("`n", [StringComparison]::Ordinal)) {
            throw [IO.InvalidDataException]::new('M0P042 synthetic readiness marker encoding/newline differs.')
        }
        $marker = $text | ConvertFrom-Json -Depth 10 -DateKind String
        $properties = @($marker.PSObject.Properties.Name | Sort-Object)
        if ($properties.Count -ne 4 -or $properties[0] -cne 'nonce_sha256' -or
            $properties[1] -cne 'parent_pid' -or $properties[2] -cne 'pid' -or $properties[3] -cne 'role' -or
            [string]$marker.role -cne $role -or [string]$marker.nonce_sha256 -cne $state.nonce_sha256) {
            throw [IO.InvalidDataException]::new('M0P042 synthetic readiness marker identity differs.')
        }
        [uint32]$markerPid = [uint32]$marker.pid
        [uint32]$markerParentPid = [uint32]$marker.parent_pid
        $expectedParentPid = switch ($role) {
            'root' { [uint32]0 }
            'child' { [uint32]$identityByRole['root'].pid }
            'grandchild' { [uint32]$identityByRole['child'].pid }
        }
        if ($markerParentPid -ne $expectedParentPid) {
            throw [IO.InvalidDataException]::new('M0P042 readiness marker parent chain differs from root-child-grandchild.')
        }
        if (-not $jobPidSet.Contains($markerPid)) {
            throw [IO.InvalidDataException]::new('M0P042 readiness PID is not present in the Job hint list.')
        }
        if (-not $rolePidSet.Add($markerPid)) {
            throw [IO.InvalidDataException]::new('M0P042 two synthetic roles resolved to the same PID hint.')
        }
        $capability = $state.native.Capture($markerPid)
        try {
            $birth = $capability.CreationTimeFileTimeUtc.ToString([Globalization.CultureInfo]::InvariantCulture)
            if (-not $births.Add($birth)) {
                throw [IO.InvalidDataException]::new('M0P042 synthetic Job members have duplicate birth identities.')
            }
            if ($role -ceq 'root' -and
                ($markerPid -ne $state.native.RootPid -or $birth -cne $state.native.RootCreationTimeFileTimeUtc.ToString())) {
                throw [IO.InvalidDataException]::new('M0P042 retained root handle identity differs from CreateProcess.')
            }
            $identity = [pscustomobject][ordered]@{
                pid = $markerPid
                creation_time_filetime_utc = $birth
            }
            if (-not $identityKeys.Add("$markerPid|$birth")) {
                throw [IO.InvalidDataException]::new('M0P042 duplicate retained Job identity.')
            }
            $parentIdentity = switch ($role) {
                'root' { $null }
                'child' { $identityByRole['root'] }
                'grandchild' { $identityByRole['child'] }
            }
            $node = [pscustomobject][ordered]@{
                role = $role
                identity = $identity
                parent_identity = $parentIdentity
                executable_sha256 = $state.executable_sha256
                job_membership_revalidated = [bool]$capability.JobMembershipRevalidated
            }
            $state.capabilities.Add($capability)
            $capability = $null
            $identityByRole[$role] = $identity
            $nodes.Add($node)
        }
        finally {
            if ($null -ne $capability) { $capability.Dispose() }
        }
    }
    foreach ($pidHint in @($jobPids | Sort-Object)) {
        [uint32]$auxiliaryPid = [uint32]$pidHint
        if ($rolePidSet.Contains($auxiliaryPid)) { continue }
        $capability = $state.native.Capture($auxiliaryPid)
        try {
            $birth = $capability.CreationTimeFileTimeUtc.ToString([Globalization.CultureInfo]::InvariantCulture)
            if (-not $identityKeys.Add("$auxiliaryPid|$birth")) {
                throw [IO.InvalidDataException]::new('M0P042 duplicate retained auxiliary Job identity.')
            }
            $auxiliaryJobMembers.Add([pscustomobject][ordered]@{
                identity = [pscustomobject][ordered]@{
                    pid = $auxiliaryPid
                    creation_time_filetime_utc = $birth
                }
                job_membership_revalidated = [bool]$capability.JobMembershipRevalidated
                role = 'job-auxiliary'
            })
            $state.capabilities.Add($capability)
            $capability = $null
        }
        finally {
            if ($null -ne $capability) { $capability.Dispose() }
        }
    }
    $reconciledJobPidSet = [Collections.Generic.HashSet[uint32]]::new()
    foreach ($pidHint in @($state.native.QueryProcessIds())) {
        if (-not $reconciledJobPidSet.Add([uint32]$pidHint)) {
            throw [IO.InvalidDataException]::new('M0P042 reconciled Job PID hint list contains a duplicate.')
        }
    }
    if (-not $jobPidSet.SetEquals($reconciledJobPidSet) -or $state.capabilities.Count -ne $jobPidSet.Count) {
        throw [IO.InvalidDataException]::new('M0P042 Job membership changed during retained-handle reconciliation.')
    }
    $snapshot = [pscustomobject][ordered]@{
        '$schema' = 'schemas/process-tree.schema.json'
        schema_id = 'm0.process-tree-smoke.v1'
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        platform = 'windows'
        state = 'partial'
        error_code = 'etw_live_not_implemented'
        missing_capabilities = @('etw-process-start-stop', 'job-completion-port')
        trace = [pscustomobject][ordered]@{
            state = 'partial'
            adapter = 'etw-process-start-stop'
            complete = $false
            collected = $false
            gap_codes = @('not-collected')
            error_code = 'etw_live_not_implemented'
        }
        capabilities = [pscustomobject][ordered]@{
            job_object_kill_on_close = $true
            breakaway_flags = 'none'
            membership_handles_revalidated = $true
            helper_sha256_verified = $true
            helper_file_locked_until_readiness = $true
            completion_port = $false
            etw_process_start_stop = $false
            linux_live_status = 'not_proven'
        }
        roles = $roles
        nodes = @($nodes)
        auxiliary_job_members = @($auxiliaryJobMembers)
        job_member_hint_count = $jobPidSet.Count
        retained_identity_count = $state.capabilities.Count
        qpc_before_create_ticks = $state.native.QpcBeforeCreate.ToString([Globalization.CultureInfo]::InvariantCulture)
        qpc_after_resume_ticks = $state.native.QpcAfterResume.ToString([Globalization.CultureInfo]::InvariantCulture)
        qpc_frequency = $state.native.QpcFrequency.ToString([Globalization.CultureInfo]::InvariantCulture)
        cleanup_authority = 'private-opaque-registry-only'
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
    }
    $snapshotJson = ConvertTo-M0ProcessPublicJson -Value $snapshot
    if (-not (Test-Json -Json $snapshotJson -SchemaFile $state.snapshot_schema_path -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0P042 public process-tree snapshot failed its closed artifact schema.')
    }
    $state.snapshot_json = $snapshotJson
    if ($null -ne $state.helper_lock) {
        $state.helper_lock.Dispose()
        $state.helper_lock = $null
    }
    return ConvertFrom-M0ProcessPublicJson -Json $state.snapshot_json
}

function Close-M0SyntheticProcessPilot {
    [CmdletBinding()]
    param([Parameter(Mandatory)] $Pilot)

    $state = Get-M0ProcessPilotState -Pilot $Pilot
    if ($null -ne $state.close_result_json) {
        return ConvertFrom-M0ProcessPublicJson -Json $state.close_result_json
    }
    $snapshotFailure = $null
    if ($null -eq $state.snapshot_json) {
        try { $null = Get-M0SyntheticProcessPilotSnapshot -Pilot $Pilot }
        catch { $snapshotFailure = $_ }
    }
    $failureCodes = [Collections.Generic.List[string]]::new()
    if ($null -ne $snapshotFailure) { $failureCodes.Add('snapshot_validation_failed') }
    foreach ($capability in $state.capabilities) {
        try {
            if (-not [bool]$capability.JobMembershipRevalidated -or
                $capability.RevalidateCreationTime() -ne $capability.CreationTimeFileTimeUtc) {
                if (-not $failureCodes.Contains('identity_revalidation_failed')) {
                    $failureCodes.Add('identity_revalidation_failed')
                }
            }
        }
        catch {
            if (-not $failureCodes.Contains('identity_revalidation_failed')) {
                $failureCodes.Add('identity_revalidation_failed')
            }
        }
    }

    $survivors = 0
    try {
        $state.native.CloseJob()
        $state.native_cleanup_actions++
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        foreach ($capability in $state.capabilities) {
            try {
                $remaining = [int][Math]::Max(0, [Math]::Ceiling(($deadline - [DateTime]::UtcNow).TotalMilliseconds))
                if (-not $capability.WaitForExit($remaining)) { $survivors++ }
            }
            catch {
                $survivors++
                if (-not $failureCodes.Contains('retained_handle_wait_failed')) {
                    $failureCodes.Add('retained_handle_wait_failed')
                }
            }
        }
        if ($survivors -gt 0 -and -not $failureCodes.Contains('helper_survived_shutdown')) {
            $failureCodes.Add('helper_survived_shutdown')
        }
    }
    catch {
        if (-not $failureCodes.Contains('job_close_failed')) { $failureCodes.Add('job_close_failed') }
    }
    finally {
        foreach ($capability in $state.capabilities) {
            try { $capability.Dispose() } catch { }
        }
        if ($null -ne $state.helper_lock) {
            try { $state.helper_lock.Dispose() } catch { }
            $state.helper_lock = $null
        }
        try { $state.native.Dispose() } catch { }
    }

    try {
        if (-not (Remove-M0PilotRunLeaf -RepositoryRoot $state.repository_root -RunRoot $state.run_root)) {
            $failureCodes.Add('unsafe_run_root_contents')
        }
    }
    catch {
        $failureCodes.Add('unsafe_run_root_contents')
    }

    $cleanupComplete = $failureCodes.Count -eq 0 -and $survivors -eq 0
    $result = [pscustomobject][ordered]@{
        schema_id = 'm0.process-tree-cleanup.v1'
        cleanup_complete = $cleanupComplete
        survivor_count = $survivors
        retained_identity_count = $state.capabilities.Count
        native_cleanup_actions = $state.native_cleanup_actions
        cleanup_action = 'close-last-job-handle-and-wait-retained-process-handles'
        individual_terminate_process_calls = 0
        candidate_code = 'P'
        scenario_id = 'synthetic-process-tree'
        attempt = 1
        evidence_role = 'synthetic_pilot'
        decision_eligible = $false
        error_code = if ($cleanupComplete) { $null } else { $failureCodes[0] }
        failure_codes = @($failureCodes)
    }
    $state.close_result_json = ConvertTo-M0ProcessPublicJson -Value $result
    return ConvertFrom-M0ProcessPublicJson -Json $state.close_result_json
}

$ExecutionContext.SessionState.Module.OnRemove = {
    foreach ($state in @($script:M0ProcessPilotRegistry.Values)) {
        if ($null -ne $state.close_result_json) { continue }
        try {
            $null = Close-M0SyntheticProcessPilot -Pilot $state.public_capability
        }
        catch {
            try { $state.native.CloseJob() } catch { }
            foreach ($capability in $state.capabilities) {
                try { $capability.Dispose() } catch { }
            }
            try { $state.native.Dispose() } catch { }
        }
    }
    $script:M0ProcessPilotRegistry.Clear()
}

Export-ModuleMember -Function @(
    'Assert-M0ProcessArtifacts',
    'Close-M0SyntheticProcessPilot',
    'Get-M0SyntheticProcessPilotSnapshot',
    'New-M0ProcessTreeArtifact',
    'Open-M0SyntheticProcessPilot',
    'Resolve-M0LinuxProcessIdentity',
    'Resolve-M0ProcessAttribution',
    'Resolve-M0ProcessCleanupPlan',
    'Resolve-M0ProcessPilotOutcome'
)
