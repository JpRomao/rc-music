#Requires -Version 7.2

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:ModuleCaptureDeadlineMilliseconds = 440
$script:VerifierFields = @(
    'verified',
    'package_id',
    'contract_sha256',
    'composite_receipt_sha256',
    'manifest_sha256',
    'receipt_sha256',
    'manifest_path',
    'payload',
    'evidence'
)
$script:VerifierBindingFields = @(
    'package_id',
    'contract_sha256',
    'composite_receipt_sha256',
    'manifest_sha256',
    'receipt_sha256',
    'manifest_path'
)
$script:Journeys = @(
    [pscustomobject][ordered]@{
        id = 'tray-lifecycle'
        arguments = @('--m0-smoke=tray-lifecycle')
    },
    [pscustomobject][ordered]@{
        id = 'error-recovery'
        arguments = @('--m0-scenario=unavailable', '--m0-smoke=error-recovery')
    }
)

if ($IsWindows) {
    $nativeNamespace = 'M0PackageSmokeNative_' + [Guid]::NewGuid().ToString('N')
    $nativeCode = @'
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace __NAMESPACE__
{
    internal static class NativeMethods
    {
        internal const uint CreateSuspended = 0x00000004;
        internal const uint CreateUnicodeEnvironment = 0x00000400;
        internal const uint CreateNoWindow = 0x08000000;
        internal const uint StartfUseStdHandles = 0x00000100;
        internal const uint HandleFlagInherit = 0x00000001;
        internal const uint JobObjectLimitKillOnJobClose = 0x00002000;
        internal const int JobObjectBasicAccountingInformation = 1;
        internal const int JobObjectExtendedLimitInformation = 9;
        internal const uint WaitObject0 = 0x00000000;
        internal const uint WaitTimeout = 0x00000102;
        internal const uint WaitFailed = 0xFFFFFFFF;
        internal const uint SmokeTerminationExitCode = 197;

        [StructLayout(LayoutKind.Sequential)]
        internal struct StartupInfo
        {
            internal uint Size;
            internal IntPtr Reserved;
            internal IntPtr Desktop;
            internal IntPtr Title;
            internal uint X;
            internal uint Y;
            internal uint XSize;
            internal uint YSize;
            internal uint XCountChars;
            internal uint YCountChars;
            internal uint FillAttribute;
            internal uint Flags;
            internal ushort ShowWindow;
            internal ushort ReservedByteCount;
            internal IntPtr ReservedBytes;
            internal IntPtr StandardInput;
            internal IntPtr StandardOutput;
            internal IntPtr StandardError;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ProcessInformation
        {
            internal IntPtr Process;
            internal IntPtr Thread;
            internal uint ProcessId;
            internal uint ThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct BasicLimitInformation
        {
            internal long PerProcessUserTimeLimit;
            internal long PerJobUserTimeLimit;
            internal uint LimitFlags;
            internal UIntPtr MinimumWorkingSetSize;
            internal UIntPtr MaximumWorkingSetSize;
            internal uint ActiveProcessLimit;
            internal UIntPtr Affinity;
            internal uint PriorityClass;
            internal uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct IoCounters
        {
            internal ulong ReadOperationCount;
            internal ulong WriteOperationCount;
            internal ulong OtherOperationCount;
            internal ulong ReadTransferCount;
            internal ulong WriteTransferCount;
            internal ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct ExtendedLimitInformation
        {
            internal BasicLimitInformation BasicLimitInformation;
            internal IoCounters IoInfo;
            internal UIntPtr ProcessMemoryLimit;
            internal UIntPtr JobMemoryLimit;
            internal UIntPtr PeakProcessMemoryUsed;
            internal UIntPtr PeakJobMemoryUsed;
        }

        [StructLayout(LayoutKind.Sequential)]
        internal struct BasicAccountingInformation
        {
            internal long TotalUserTime;
            internal long TotalKernelTime;
            internal long ThisPeriodTotalUserTime;
            internal long ThisPeriodTotalKernelTime;
            internal uint TotalPageFaultCount;
            internal uint TotalProcesses;
            internal uint ActiveProcesses;
            internal uint TotalTerminatedProcesses;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern IntPtr CreateJobObjectW(IntPtr jobAttributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool SetInformationJobObject(
            IntPtr job,
            int informationClass,
            ref ExtendedLimitInformation information,
            uint informationLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool QueryInformationJobObject(
            IntPtr job,
            int informationClass,
            out BasicAccountingInformation information,
            uint informationLength,
            IntPtr returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool TerminateJobObject(IntPtr job, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool TerminateProcess(IntPtr process, uint exitCode);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool CreateProcessW(
            string applicationName,
            StringBuilder commandLine,
            IntPtr processAttributes,
            IntPtr threadAttributes,
            bool inheritHandles,
            uint creationFlags,
            IntPtr environment,
            string currentDirectory,
            ref StartupInfo startupInfo,
            out ProcessInformation processInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint ResumeThread(IntPtr thread);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        internal static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);
    }

    internal static class CommandLineBuilder
    {
        internal static StringBuilder Build(string executable, string[] arguments)
        {
            var commandLine = new StringBuilder(Quote(executable));
            foreach (string argument in arguments)
            {
                commandLine.Append(' ').Append(Quote(argument));
            }
            return commandLine;
        }

        private static string Quote(string argument)
        {
            var quoted = new StringBuilder("\"");
            int pendingBackslashes = 0;
            foreach (char value in argument)
            {
                if (value == '\\')
                {
                    pendingBackslashes++;
                    continue;
                }
                if (value == '"')
                {
                    quoted.Append('\\', pendingBackslashes * 2 + 1).Append(value);
                    pendingBackslashes = 0;
                    continue;
                }
                quoted.Append('\\', pendingBackslashes).Append(value);
                pendingBackslashes = 0;
            }
            quoted.Append('\\', pendingBackslashes * 2).Append('"');
            return quoted.ToString();
        }
    }

    internal static class EnvironmentBlockBuilder
    {
        internal static IntPtr Allocate(IDictionary values)
        {
            var keys = new List<string>();
            foreach (DictionaryEntry entry in values)
            {
                keys.Add(Convert.ToString(entry.Key));
            }
            keys.Sort(StringComparer.OrdinalIgnoreCase);

            var block = new StringBuilder();
            foreach (string key in keys)
            {
                block.Append(key)
                    .Append('=')
                    .Append(Convert.ToString(values[key]))
                    .Append('\0');
            }
            block.Append('\0');
            return Marshal.StringToHGlobalUni(block.ToString());
        }
    }

    public sealed class ModuleCaptureResult
    {
        public string[] ModulePaths { get; private set; }
        public string[] FirstSampleModulePaths { get; private set; }
        public double SamplingElapsedMilliseconds { get; private set; }
        public double LastSuccessfulSampleMilliseconds { get; private set; }
        public int SuccessfulSamples { get; private set; }
        public int FailedSamples { get; private set; }

        internal ModuleCaptureResult(
            string[] modulePaths,
            string[] firstSampleModulePaths,
            double samplingElapsedMilliseconds,
            double lastSuccessfulSampleMilliseconds,
            int successfulSamples,
            int failedSamples)
        {
            ModulePaths = modulePaths;
            FirstSampleModulePaths = firstSampleModulePaths;
            SamplingElapsedMilliseconds = samplingElapsedMilliseconds;
            LastSuccessfulSampleMilliseconds = lastSuccessfulSampleMilliseconds;
            SuccessfulSamples = successfulSamples;
            FailedSamples = failedSamples;
        }
    }

    internal static class ModuleSampler
    {
        internal static ModuleCaptureResult Capture(
            uint processId,
            Stopwatch clock,
            int deadlineMilliseconds)
        {
            var captured = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            string[] firstSample = CaptureInitialSnapshot(
                processId,
                clock,
                deadlineMilliseconds,
                captured);
            int successfulSamples = 0;
            int failedSamples = 0;
            bool processExited = false;
            double lastSuccessfulSampleMilliseconds = clock.Elapsed.TotalMilliseconds;

            successfulSamples++;

            while (clock.ElapsedMilliseconds < deadlineMilliseconds && !processExited)
            {
                string[] snapshot;
                bool sampleSucceeded = TryCaptureSnapshot(processId, out snapshot, out processExited);
                if (sampleSucceeded)
                {
                    AddSnapshot(captured, snapshot);
                    successfulSamples++;
                    lastSuccessfulSampleMilliseconds = clock.Elapsed.TotalMilliseconds;
                }
                else if (!processExited)
                {
                    failedSamples++;
                }

                int remaining = deadlineMilliseconds - (int)clock.ElapsedMilliseconds;
                if (!processExited && remaining > 0)
                {
                    Thread.Sleep(Math.Min(2, remaining));
                }
            }

            var modulePaths = new string[captured.Count];
            captured.CopyTo(modulePaths);
            Array.Sort(modulePaths, StringComparer.OrdinalIgnoreCase);
            double samplingElapsedMilliseconds = clock.Elapsed.TotalMilliseconds;
            if (!processExited &&
                samplingElapsedMilliseconds - lastSuccessfulSampleMilliseconds > 25)
            {
                throw new InvalidOperationException(
                    "Process.Modules sampling ended with a stale successful snapshot.");
            }
            return new ModuleCaptureResult(
                modulePaths,
                firstSample,
                samplingElapsedMilliseconds,
                lastSuccessfulSampleMilliseconds,
                successfulSamples,
                failedSamples);
        }

        private static string[] CaptureInitialSnapshot(
            uint processId,
            Stopwatch clock,
            int deadlineMilliseconds,
            HashSet<string> captured)
        {
            while (clock.ElapsedMilliseconds < deadlineMilliseconds)
            {
                string[] snapshot;
                bool processExited;
                if (TryCaptureSnapshot(processId, out snapshot, out processExited))
                {
                    AddSnapshot(captured, snapshot);
                    return snapshot;
                }
                if (processExited)
                {
                    break;
                }
                Thread.Sleep(1);
            }
            throw new InvalidOperationException(
                "No Process.Modules snapshot could be captured while the primary process was suspended.");
        }

        private static void AddSnapshot(HashSet<string> captured, string[] snapshot)
        {
            foreach (string modulePath in snapshot)
            {
                captured.Add(modulePath);
            }
        }

        private static bool TryCaptureSnapshot(
            uint processId,
            out string[] modulePaths,
            out bool processExited)
        {
            modulePaths = new string[0];
            processExited = false;
            try
            {
                using (Process process = Process.GetProcessById((int)processId))
                {
                    process.Refresh();
                    var snapshot = new List<string>();
                    foreach (ProcessModule module in process.Modules)
                    {
                        snapshot.Add(module.FileName);
                    }
                    modulePaths = snapshot.ToArray();
                    Array.Sort(modulePaths, StringComparer.OrdinalIgnoreCase);
                    processExited = process.HasExited;
                    return true;
                }
            }
            catch (ArgumentException)
            {
                processExited = true;
                return false;
            }
            catch (InvalidOperationException)
            {
                return false;
            }
            catch (Win32Exception)
            {
                return false;
            }
        }
    }

    public sealed class Session : IDisposable
    {
        private IntPtr job;
        private IntPtr process;
        private IntPtr primaryThread;
        private bool assignedToJob;
        private bool disposed;

        public uint Pid { get; private set; }
        public bool Resumed { get; private set; }

        public static Session Start(
            string executable,
            string[] arguments,
            string currentDirectory,
            IDictionary environment,
            string standardOutputPath,
            string standardErrorPath)
        {
            return StartCore(
                executable,
                arguments,
                currentDirectory,
                environment,
                standardOutputPath,
                standardErrorPath,
                NativeMethods.AssignProcessToJobObject);
        }

        private static Session StartCore(
            string executable,
            string[] arguments,
            string currentDirectory,
            IDictionary environment,
            string standardOutputPath,
            string standardErrorPath,
            Func<IntPtr, IntPtr, bool> assignProcessToJob)
        {
            var session = new Session();
            IntPtr environmentBlock = IntPtr.Zero;
            FileStream standardOutput = null;
            FileStream standardError = null;
            try
            {
                session.CreateKillOnCloseJob();
                standardOutput = CreateInheritedOutput(standardOutputPath);
                standardError = CreateInheritedOutput(standardErrorPath);
                environmentBlock = EnvironmentBlockBuilder.Allocate(environment);
                session.CreateSuspendedProcess(
                    executable,
                    arguments,
                    currentDirectory,
                    environmentBlock,
                    standardOutput.SafeFileHandle.DangerousGetHandle(),
                    standardError.SafeFileHandle.DangerousGetHandle());

                if (!assignProcessToJob(session.job, session.process))
                {
                    int nativeError = Marshal.GetLastWin32Error();
                    throw new Win32Exception(
                        nativeError,
                        "AssignProcessToJobObject failed for PID " + session.Pid + ".");
                }
                session.assignedToJob = true;
                return session;
            }
            catch
            {
                try
                {
                    session.TerminatePrimaryProcessAndWait();
                }
                finally
                {
                    session.Dispose();
                }
                throw;
            }
            finally
            {
                if (environmentBlock != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(environmentBlock);
                }
                if (standardOutput != null)
                {
                    standardOutput.Dispose();
                }
                if (standardError != null)
                {
                    standardError.Dispose();
                }
            }
        }

        public ModuleCaptureResult ResumeAndCaptureModules(int deadlineMilliseconds)
        {
            if (!assignedToJob)
            {
                throw new InvalidOperationException("The primary process is not assigned to its Job Object.");
            }
            if (deadlineMilliseconds <= 0 || deadlineMilliseconds >= 500)
            {
                throw new ArgumentOutOfRangeException(
                    "deadlineMilliseconds",
                    "The live module capture deadline must be strictly between zero and 500 ms.");
            }

            var clock = Stopwatch.StartNew();
            ResumePrimaryThread();
            return ModuleSampler.Capture(Pid, clock, deadlineMilliseconds);
        }

        public void Resume()
        {
            ResumePrimaryThread();
        }

        public bool Wait(int milliseconds)
        {
            if (milliseconds < 0)
            {
                throw new ArgumentOutOfRangeException("milliseconds");
            }
            uint waitResult = NativeMethods.WaitForSingleObject(process, (uint)milliseconds);
            if (waitResult == NativeMethods.WaitObject0)
            {
                return true;
            }
            if (waitResult == NativeMethods.WaitTimeout)
            {
                return false;
            }
            throw new Win32Exception(Marshal.GetLastWin32Error(), "WaitForSingleObject failed.");
        }

        public uint GetActiveProcessCount()
        {
            ThrowIfDisposed();
            NativeMethods.BasicAccountingInformation accounting;
            bool queried = NativeMethods.QueryInformationJobObject(
                job,
                NativeMethods.JobObjectBasicAccountingInformation,
                out accounting,
                (uint)Marshal.SizeOf<NativeMethods.BasicAccountingInformation>(),
                IntPtr.Zero);
            if (!queried)
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "QueryInformationJobObject failed.");
            }
            return accounting.ActiveProcesses;
        }

        public uint TerminateTreeAndCountSurvivors(int waitMilliseconds)
        {
            if (waitMilliseconds < 0)
            {
                throw new ArgumentOutOfRangeException("waitMilliseconds");
            }

            uint activeProcesses = GetActiveProcessCount();
            if (activeProcesses == 0)
            {
                return 0;
            }
            if (!NativeMethods.TerminateJobObject(job, NativeMethods.SmokeTerminationExitCode))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "TerminateJobObject failed.");
            }

            var clock = Stopwatch.StartNew();
            do
            {
                activeProcesses = GetActiveProcessCount();
                if (activeProcesses == 0)
                {
                    return 0;
                }
                if (clock.ElapsedMilliseconds < waitMilliseconds)
                {
                    Thread.Sleep(5);
                }
            }
            while (clock.ElapsedMilliseconds < waitMilliseconds);
            return activeProcesses;
        }

        public void Dispose()
        {
            if (disposed)
            {
                return;
            }
            disposed = true;

            TryTerminatePrimaryProcess();
            TryTerminateJob();
            CloseNativeHandle(ref primaryThread);
            if (process != IntPtr.Zero)
            {
                NativeMethods.WaitForSingleObject(process, 5000);
            }
            CloseNativeHandle(ref process);
            CloseNativeHandle(ref job);
        }

        private static FileStream CreateInheritedOutput(string path)
        {
            var stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
            bool configured = NativeMethods.SetHandleInformation(
                stream.SafeFileHandle.DangerousGetHandle(),
                NativeMethods.HandleFlagInherit,
                NativeMethods.HandleFlagInherit);
            if (!configured)
            {
                int nativeError = Marshal.GetLastWin32Error();
                stream.Dispose();
                throw new Win32Exception(nativeError, "SetHandleInformation failed.");
            }
            return stream;
        }

        private void CreateKillOnCloseJob()
        {
            job = NativeMethods.CreateJobObjectW(IntPtr.Zero, null);
            if (job == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateJobObjectW failed.");
            }

            var limits = new NativeMethods.ExtendedLimitInformation();
            limits.BasicLimitInformation.LimitFlags = NativeMethods.JobObjectLimitKillOnJobClose;
            bool configured = NativeMethods.SetInformationJobObject(
                job,
                NativeMethods.JobObjectExtendedLimitInformation,
                ref limits,
                (uint)Marshal.SizeOf<NativeMethods.ExtendedLimitInformation>());
            if (!configured)
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "SetInformationJobObject failed.");
            }
        }

        private void CreateSuspendedProcess(
            string executable,
            string[] arguments,
            string currentDirectory,
            IntPtr environmentBlock,
            IntPtr standardOutput,
            IntPtr standardError)
        {
            var startupInfo = new NativeMethods.StartupInfo();
            startupInfo.Size = (uint)Marshal.SizeOf<NativeMethods.StartupInfo>();
            startupInfo.Flags = NativeMethods.StartfUseStdHandles;
            startupInfo.StandardInput = IntPtr.Zero;
            startupInfo.StandardOutput = standardOutput;
            startupInfo.StandardError = standardError;

            NativeMethods.ProcessInformation created;
            bool started = NativeMethods.CreateProcessW(
                executable,
                CommandLineBuilder.Build(executable, arguments),
                IntPtr.Zero,
                IntPtr.Zero,
                true,
                NativeMethods.CreateSuspended |
                    NativeMethods.CreateUnicodeEnvironment |
                    NativeMethods.CreateNoWindow,
                environmentBlock,
                currentDirectory,
                ref startupInfo,
                out created);
            if (!started)
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "CreateProcessW failed for cwd [" + currentDirectory + "] executable [" + executable + "].");
            }

            process = created.Process;
            primaryThread = created.Thread;
            Pid = created.ProcessId;
        }

        private void ResumePrimaryThread()
        {
            ThrowIfDisposed();
            if (Resumed)
            {
                throw new InvalidOperationException("The primary thread has already been resumed.");
            }
            uint previousSuspendCount = NativeMethods.ResumeThread(primaryThread);
            if (previousSuspendCount == UInt32.MaxValue)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "ResumeThread failed.");
            }
            Resumed = true;
            CloseNativeHandle(ref primaryThread);
        }

        private void TryTerminatePrimaryProcess()
        {
            if (process == IntPtr.Zero)
            {
                return;
            }
            uint waitResult = NativeMethods.WaitForSingleObject(process, 0);
            if (waitResult == NativeMethods.WaitTimeout)
            {
                NativeMethods.TerminateProcess(process, NativeMethods.SmokeTerminationExitCode);
            }
        }

        private void TerminatePrimaryProcessAndWait()
        {
            if (process == IntPtr.Zero)
            {
                return;
            }

            uint waitResult = NativeMethods.WaitForSingleObject(process, 0);
            if (waitResult == NativeMethods.WaitObject0)
            {
                return;
            }
            if (waitResult != NativeMethods.WaitTimeout)
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Waiting for the unassigned primary process failed.");
            }
            if (!NativeMethods.TerminateProcess(process, NativeMethods.SmokeTerminationExitCode))
            {
                throw new Win32Exception(
                    Marshal.GetLastWin32Error(),
                    "Terminating the unassigned primary process failed.");
            }
            if (NativeMethods.WaitForSingleObject(process, 5000) != NativeMethods.WaitObject0)
            {
                throw new InvalidOperationException(
                    "The unassigned primary process did not terminate within five seconds.");
            }
        }

        private void TryTerminateJob()
        {
            if (job != IntPtr.Zero)
            {
                NativeMethods.TerminateJobObject(job, NativeMethods.SmokeTerminationExitCode);
            }
        }

        private static void CloseNativeHandle(ref IntPtr handle)
        {
            if (handle != IntPtr.Zero)
            {
                NativeMethods.CloseHandle(handle);
                handle = IntPtr.Zero;
            }
        }

        private void ThrowIfDisposed()
        {
            if (disposed)
            {
                throw new ObjectDisposedException("Session");
            }
        }
    }
}
'@.Replace('__NAMESPACE__', $nativeNamespace)

    Add-Type -TypeDefinition $nativeCode
    $nativeAssembly = [AppDomain]::CurrentDomain.GetAssemblies() |
        Where-Object { $null -ne $_.GetType("$nativeNamespace.Session", $false) } |
        Select-Object -First 1
    if ($null -eq $nativeAssembly) {
        throw 'The native smoke session type was not loaded.'
    }
    $script:NativeSession = $nativeAssembly.GetType("$nativeNamespace.Session", $true)
}

function Test-M0Within {
    param(
        [Parameter(Mandatory)]
        [string]$Path,

        [Parameter(Mandatory)]
        [string]$Root,

        [switch]$AllowEqual
    )

    $pathFull = [IO.Path]::GetFullPath($Path)
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if ($AllowEqual -and $pathFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }
    return $pathFull.StartsWith($rootFull + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Resolve-M0ManifestPath {
    param(
        [Parameter(Mandatory)]
        [string]$ManifestPath,

        [Parameter(Mandatory)]
        [string]$PackageRoot
    )

    $segments = @($ManifestPath -split '[/\\]')
    if ([string]::IsNullOrWhiteSpace($ManifestPath) -or
        [IO.Path]::IsPathRooted($ManifestPath) -or
        $ManifestPath.Contains(':') -or
        $segments -contains '..' -or
        $segments -contains '.') {
        throw 'Verifier manifest_path must be a safe package-relative path.'
    }
    $platformPath = $ManifestPath.Replace('/', [IO.Path]::DirectorySeparatorChar)
    return [IO.Path]::GetFullPath((Join-Path $PackageRoot $platformPath))
}

function Assert-M0Verifier {
    param(
        [Parameter(Mandatory)]
        $Result,

        [Parameter(Mandatory)]
        [string]$Phase,

        [Parameter(Mandatory)]
        [string]$PackageRoot,

        [Parameter(Mandatory)]
        [string]$ReceiptPath
    )

    if ($null -eq $Result) {
        throw "$Phase verifier returned no result."
    }
    $actualFields = @($Result.PSObject.Properties.Name)
    if (($actualFields -join ',') -cne ($script:VerifierFields -join ',')) {
        throw "$Phase verifier result is open, incomplete, or reordered."
    }
    if ($Result.verified -ne $true) {
        throw "$Phase verification failed."
    }

    $canonicalReceipt = Join-Path $PackageRoot 'evidence/qt-shell-composite-package-receipt.v2.json'
    if (-not [IO.Path]::GetFullPath($ReceiptPath).Equals(
            [IO.Path]::GetFullPath($canonicalReceipt),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReceiptPath must be the package-local canonical receipt.'
    }

    $manifestPath = Resolve-M0ManifestPath -ManifestPath $Result.manifest_path -PackageRoot $PackageRoot
    if (-not (Test-M0Within -Path $manifestPath -Root $PackageRoot)) {
        throw "$Phase verifier manifest is outside package."
    }
    return $Result
}

function Assert-M0VerifierBinding {
    param(
        [Parameter(Mandatory)]
        $Actual,

        [Parameter(Mandatory)]
        $Expected,

        [Parameter(Mandatory)]
        [string]$Phase
    )

    foreach ($field in $script:VerifierBindingFields) {
        if ($Actual.$field -cne $Expected.$field) {
            throw "$Phase verifier binding drifted: $field"
        }
    }
}

function New-M0Environment {
    param(
        [Parameter(Mandatory)]
        [string]$WorkingDirectory
    )

    $system32 = Join-Path $env:SystemRoot 'System32'
    $profile = Join-Path $WorkingDirectory 'profile'
    $temporary = Join-Path $WorkingDirectory 'temp'
    return [ordered]@{
        SystemRoot = $env:SystemRoot
        WINDIR = $env:SystemRoot
        PATH = $system32
        TEMP = $temporary
        TMP = $temporary
        USERPROFILE = $profile
        APPDATA = Join-Path $profile 'AppData/Roaming'
        LOCALAPPDATA = Join-Path $profile 'AppData/Local'
        QT_QPA_PLATFORM = 'offscreen'
        QT_QUICK_BACKEND = 'software'
        QT_QUICK_CONTROLS_STYLE = 'Basic'
        QT_FATAL_WARNINGS = '1'
        QT_FORCE_STDERR_LOGGING = '1'
        QML_DISABLE_DISK_CACHE = '1'
    }
}

function Get-M0Payload {
    param(
        [Parameter(Mandatory)]
        $Payload,

        [Parameter(Mandatory)]
        [string]$PackageRoot
    )

    $paths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Payload)) {
        $relativePath = if ($entry -is [string]) {
            $entry
        }
        else {
            [string]$entry.relative_path
        }
        $hasUnsafeSegment = @($relativePath -split '[/\\]') -contains '..'
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            [IO.Path]::IsPathRooted($relativePath) -or
            $hasUnsafeSegment -or
            $relativePath.Contains(':')) {
            throw 'Unsafe verifier payload path.'
        }

        $platformPath = $relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar)
        $fullPath = [IO.Path]::GetFullPath((Join-Path $PackageRoot $platformPath))
        if (-not (Test-M0Within -Path $fullPath -Root $PackageRoot)) {
            throw 'Verifier payload path escapes the package.'
        }
        [void]$paths.Add($fullPath)
    }
    return $paths
}

function New-M0RequiredModuleSet {
    $required = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in @(
            'm0_qt_shell.exe',
            'Qt6Core.dll',
            'Qt6Gui.dll',
            'Qt6Widgets.dll',
            'Qt6Quick.dll',
            'qoffscreen.dll')) {
        [void]$required.Add($name)
    }
    return ,$required
}

function Assert-M0LoadedModulePath {
    param(
        [Parameter(Mandatory)]
        [string]$ModulePath,

        [Parameter(Mandatory)]
        [string]$AppRoot,

        [Parameter(Mandatory)]
        [Collections.Generic.HashSet[string]]$Payload
    )

    $fullPath = [IO.Path]::GetFullPath($ModulePath)
    $moduleName = [IO.Path]::GetFileName($fullPath)
    $system32 = Join-Path $env:SystemRoot 'System32'
    $isCrt = $moduleName -match '^(?i:vcruntime|msvcp|ucrtbase).*\.dll$'
    $isQtOrPlugin = $moduleName -match '^(?i:Qt6|qoffscreen|qsvg|qtquick)'

    if ($isCrt -and -not (Test-M0Within -Path $fullPath -Root $system32)) {
        throw "CRT loaded outside System32: $fullPath"
    }
    if (Test-M0Within -Path $fullPath -Root $AppRoot) {
        if (-not $Payload.Contains($fullPath)) {
            throw "Package-local module absent from authenticated payload: $moduleName"
        }
    }
    elseif (-not (Test-M0Within -Path $fullPath -Root $system32)) {
        throw "Module loaded outside app or approved System32 root: $fullPath"
    }
    if ($isQtOrPlugin -and -not (Test-M0Within -Path $fullPath -Root $AppRoot)) {
        throw "Qt/plugin loaded outside app: $fullPath"
    }
    return $moduleName
}

function Assert-M0Modules {
    param(
        [Parameter(Mandatory)]
        $Session,

        [Parameter(Mandatory)]
        [string]$AppRoot,

        [Parameter(Mandatory)]
        [Collections.Generic.HashSet[string]]$Payload
    )

    if ([double]$Session.capture_elapsed_ms -ge 500) {
        throw 'Live module capture must complete strictly before 500ms.'
    }

    $required = New-M0RequiredModuleSet
    foreach ($modulePath in @($Session.module_paths)) {
        $moduleName = Assert-M0LoadedModulePath `
            -ModulePath $modulePath `
            -AppRoot $AppRoot `
            -Payload $Payload
        [void]$required.Remove($moduleName)
    }
    if ($required.Count -ne 0) {
        throw "Incomplete live module capture: $($required -join ', ')"
    }
}

function Read-M0CaptureFile {
    param(
        [Parameter(Mandatory)]
        [string]$LiteralPath
    )

    if (-not [IO.File]::Exists($LiteralPath)) {
        return ''
    }
    return [IO.File]::ReadAllText($LiteralPath)
}

function Invoke-M0RealJourney {
    param(
        [Parameter(Mandatory)]
        [string]$Executable,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [Parameter(Mandatory)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory)]
        [Collections.IDictionary]$Environment,

        [Parameter(Mandatory)]
        [int]$TimeoutMilliseconds
    )

    if ($TimeoutMilliseconds -le 0) {
        throw 'TimeoutMilliseconds must be positive.'
    }

    $standardOutputPath = Join-Path $WorkingDirectory 'stdout.bin'
    $standardErrorPath = Join-Path $WorkingDirectory 'stderr.bin'
    $native = $null
    $process = $null
    try {
        $native = $script:NativeSession::Start(
            $Executable,
            $Arguments,
            $WorkingDirectory,
            $Environment,
            $standardOutputPath,
            $standardErrorPath)
        $process = [Diagnostics.Process]::GetProcessById([int]$native.Pid)

        $captureDeadline = [Math]::Min(
            $script:ModuleCaptureDeadlineMilliseconds,
            [Math]::Max(1, $TimeoutMilliseconds - 1))
        $capture = $native.ResumeAndCaptureModules($captureDeadline)
        if ($capture.SuccessfulSamples -eq 0) {
            throw 'Live module capture produced no successful Process.Modules sample.'
        }

        $remainingTimeout = [Math]::Max(
            0,
            $TimeoutMilliseconds - [int][Math]::Ceiling($capture.SamplingElapsedMilliseconds))
        $completed = $native.Wait($remainingTimeout)
        $timedOut = -not $completed
        $exitCode = if ($timedOut) {
            [long]197
        }
        else {
            [long]$process.ExitCode
        }

        $survivorCount = [long]$native.TerminateTreeAndCountSurvivors(5000)
        if (-not $native.Wait(5000)) {
            throw 'The primary process handle was not signaled after Job Object cleanup.'
        }
        return [pscustomobject][ordered]@{
            exit_code = $exitCode
            stdout = Read-M0CaptureFile -LiteralPath $standardOutputPath
            stderr = Read-M0CaptureFile -LiteralPath $standardErrorPath
            timed_out = $timedOut
            capture_elapsed_ms = [double]$capture.SamplingElapsedMilliseconds
            last_sample_elapsed_ms = [double]$capture.LastSuccessfulSampleMilliseconds
            module_paths = @($capture.ModulePaths)
            first_sample_module_paths = @($capture.FirstSampleModulePaths)
            sample_count = [long]$capture.SuccessfulSamples
            survivor_count = $survivorCount
            native = $native
            process = $process
        }
    }
    catch {
        if ($null -ne $native) {
            $native.Dispose()
        }
        if ($null -ne $process) {
            $process.Dispose()
        }
        throw
    }
}

function New-M0RealProcessOperations {
    $runJourney = {
        param($executable, $arguments, $workingDirectory, $environment, $timeoutMilliseconds)
        Invoke-M0RealJourney `
            -Executable ([string]$executable) `
            -Arguments ([string[]]$arguments) `
            -WorkingDirectory ([string]$workingDirectory) `
            -Environment ([Collections.IDictionary]$environment) `
            -TimeoutMilliseconds ([int]$timeoutMilliseconds)
    }
    $terminate = {
        param($session)
        $session.survivor_count = [long]$session.native.TerminateTreeAndCountSurvivors(5000)
        if ($session.survivor_count -ne 0) {
            throw "Job Object cleanup left $($session.survivor_count) active process(es)."
        }
    }
    $dispose = {
        param($session)
        $session.native.Dispose()
        $session.process.Dispose()
    }
    return @{
        RunJourney = $runJourney
        Terminate = $terminate
        Dispose = $dispose
    }
}

function Resolve-M0ProcessOperations {
    param(
        [hashtable]$ProcessOperations
    )

    if ($null -eq $ProcessOperations) {
        $ProcessOperations = New-M0RealProcessOperations
    }
    foreach ($operation in @('RunJourney', 'Terminate', 'Dispose')) {
        if ($null -eq $ProcessOperations[$operation]) {
            throw "ProcessOperations.$operation is required."
        }
    }
    return $ProcessOperations
}

function New-M0VerifierAction {
    param(
        [Parameter(Mandatory)]
        [string]$ReceiptPath,

        [Parameter(Mandatory)]
        [string]$ExpectedReceiptSha256,

        [Parameter(Mandatory)]
        [string]$ContractPath,

        [Parameter(Mandatory)]
        [string]$ExpectedContractSha256,

        [Parameter(Mandatory)]
        [string]$CompositeReceiptPath,

        [Parameter(Mandatory)]
        [string]$ExpectedCompositeReceiptSha256,

        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [string]$ExpectedQtBaseReceiptSha256
    )

    $corePath = Join-Path $PSScriptRoot 'M0.QtShellCompositePackage.psm1'
    $core = Import-Module $corePath -Force -PassThru
    return {
        param($phase)
        & $core.ExportedFunctions['Assert-M0QtShellCompositePackageReceipt'] `
            -LiteralPath $ReceiptPath `
            -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
            -ContractPath $ContractPath `
            -ExpectedContractSha256 $ExpectedContractSha256 `
            -CompositeReceiptPath $CompositeReceiptPath `
            -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
            -RepositoryRoot $RepositoryRoot `
            -ExpectedQtBaseReceiptSha256 $ExpectedQtBaseReceiptSha256
    }.GetNewClosure()
}

function New-M0SmokeWorkspace {
    param(
        [Parameter(Mandatory)]
        [string]$PackageRoot,

        [scriptblock]$WorkspaceFactory
    )

    $workspace = if ($null -ne $WorkspaceFactory) {
        [IO.Path]::GetFullPath((& $WorkspaceFactory))
    }
    else {
        Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-smoke-' + [Guid]::NewGuid().ToString('N'))
    }
    if (Test-M0Within -Path $workspace -Root $PackageRoot -AllowEqual) {
        throw 'Workspace must be outside package.'
    }
    if ([IO.Directory]::Exists($workspace) -or [IO.File]::Exists($workspace)) {
        throw 'Workspace must be new.'
    }
    [IO.Directory]::CreateDirectory($workspace) | Out-Null
    return $workspace
}

function ConvertTo-M0CanonicalModulePaths {
    param(
        [Parameter(Mandatory)]
        [object[]]$ModulePaths
    )

    $unique = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($modulePath in $ModulePaths) {
        [void]$unique.Add([IO.Path]::GetFullPath([string]$modulePath))
    }
    $canonical = [string[]]@($unique)
    [Array]::Sort($canonical, [StringComparer]::OrdinalIgnoreCase)
    return $canonical
}

function Invoke-M0SmokeJourney {
    param(
        [Parameter(Mandatory)]
        $Journey,

        [Parameter(Mandatory)]
        [string]$Executable,

        [Parameter(Mandatory)]
        [string]$PackageRoot,

        [Parameter(Mandatory)]
        [string]$AppRoot,

        [Parameter(Mandatory)]
        [string]$ReceiptPath,

        [Parameter(Mandatory)]
        [string]$Workspace,

        [Parameter(Mandatory)]
        [scriptblock]$VerifyReceiptAction,

        [Parameter(Mandatory)]
        [hashtable]$ProcessOperations,

        [Parameter(Mandatory)]
        [int]$TimeoutMilliseconds,

        $ExpectedBinding
    )

    $prePhase = "$($Journey.id)-pre"
    $before = Assert-M0Verifier `
        -Result (& $VerifyReceiptAction $prePhase) `
        -Phase $prePhase `
        -PackageRoot $PackageRoot `
        -ReceiptPath $ReceiptPath
    if ($null -ne $ExpectedBinding) {
        Assert-M0VerifierBinding -Actual $before -Expected $ExpectedBinding -Phase $prePhase
    }

    $payload = Get-M0Payload -Payload $before.payload -PackageRoot $PackageRoot
    $workingDirectory = Join-Path $Workspace $Journey.id
    [IO.Directory]::CreateDirectory($workingDirectory) | Out-Null
    $session = $null
    try {
        $session = & $ProcessOperations.RunJourney `
            $Executable `
            $Journey.arguments `
            $workingDirectory `
            (New-M0Environment -WorkingDirectory $workingDirectory) `
            $TimeoutMilliseconds
        if ($session.exit_code -ne 0 -or
            $session.stdout -cne '' -or
            $session.stderr -cne '' -or
            $session.timed_out -eq $true) {
            throw "Journey $($Journey.id) failed."
        }
        Assert-M0Modules -Session $session -AppRoot $AppRoot -Payload $payload
        if ([long]$session.survivor_count -ne 0) {
            throw 'Surviving processes detected.'
        }
    }
    catch {
        if ($null -ne $session) {
            & $ProcessOperations.Terminate $session
        }
        throw
    }
    finally {
        if ($null -ne $session) {
            & $ProcessOperations.Dispose $session
        }
    }

    $postPhase = "$($Journey.id)-post"
    $after = Assert-M0Verifier `
        -Result (& $VerifyReceiptAction $postPhase) `
        -Phase $postPhase `
        -PackageRoot $PackageRoot `
        -ReceiptPath $ReceiptPath
    Assert-M0VerifierBinding -Actual $after -Expected $before -Phase $postPhase

    $canonicalModulePaths = ConvertTo-M0CanonicalModulePaths -ModulePaths @($session.module_paths)
    $lastSampleElapsed = if ($session.PSObject.Properties.Name -contains 'last_sample_elapsed_ms') {
        [double]$session.last_sample_elapsed_ms
    }
    else {
        [double]$session.capture_elapsed_ms
    }

    return [pscustomobject][ordered]@{
        binding = $before
        result = [pscustomobject][ordered]@{
            journey = $Journey.id
            working_directory = $workingDirectory
            exit_code = [long]$session.exit_code
            stdout = [string]$session.stdout
            stderr = [string]$session.stderr
            timed_out = [bool]$session.timed_out
            module_capture_ms = [double]$session.capture_elapsed_ms
            last_module_sample_ms = $lastSampleElapsed
            module_paths = $canonicalModulePaths
            survivor_count = [long]$session.survivor_count
        }
    }
}

function Test-M0QtShellCompositePackageSmoke {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)]
        [string]$PackageRoot,

        [Parameter(Mandatory)]
        [string]$ReceiptPath,

        [Parameter(Mandatory)]
        [string]$ExpectedReceiptSha256,

        [Parameter(Mandatory)]
        [string]$ContractPath,

        [Parameter(Mandatory)]
        [string]$ExpectedContractSha256,

        [Parameter(Mandatory)]
        [string]$CompositeReceiptPath,

        [Parameter(Mandatory)]
        [string]$ExpectedCompositeReceiptSha256,

        [Parameter(Mandatory)]
        [string]$RepositoryRoot,

        [string]$ExpectedQtBaseReceiptSha256,

        [scriptblock]$VerifyReceiptAction,

        [hashtable]$ProcessOperations,

        [scriptblock]$WorkspaceFactory,

        [int]$TimeoutMilliseconds = 15000
    )

    if (-not $IsWindows -or -not [Environment]::Is64BitProcess) {
        throw 'Windows x64 PowerShell is required.'
    }
    if ($TimeoutMilliseconds -le 0) {
        throw 'TimeoutMilliseconds must be positive.'
    }

    $package = [IO.Path]::GetFullPath($PackageRoot)
    $app = Join-Path $package 'app'
    $evidence = Join-Path $package 'evidence'
    $executable = Join-Path $app 'm0_qt_shell.exe'
    if (-not [IO.Directory]::Exists($app) -or
        -not [IO.Directory]::Exists($evidence) -or
        -not [IO.File]::Exists($executable)) {
        throw 'Package must contain app, evidence, and app/m0_qt_shell.exe.'
    }

    $canonicalReceipt = Join-Path $evidence 'qt-shell-composite-package-receipt.v2.json'
    if (-not [IO.Path]::GetFullPath($ReceiptPath).Equals(
            [IO.Path]::GetFullPath($canonicalReceipt),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'ReceiptPath must be exactly package/evidence/qt-shell-composite-package-receipt.v2.json.'
    }

    if ($null -eq $VerifyReceiptAction) {
        $VerifyReceiptAction = New-M0VerifierAction `
            -ReceiptPath $ReceiptPath `
            -ExpectedReceiptSha256 $ExpectedReceiptSha256 `
            -ContractPath $ContractPath `
            -ExpectedContractSha256 $ExpectedContractSha256 `
            -CompositeReceiptPath $CompositeReceiptPath `
            -ExpectedCompositeReceiptSha256 $ExpectedCompositeReceiptSha256 `
            -RepositoryRoot $RepositoryRoot `
            -ExpectedQtBaseReceiptSha256 $ExpectedQtBaseReceiptSha256
    }
    $ProcessOperations = Resolve-M0ProcessOperations -ProcessOperations $ProcessOperations
    $workspace = New-M0SmokeWorkspace -PackageRoot $package -WorkspaceFactory $WorkspaceFactory

    $journeyResults = [Collections.Generic.List[object]]::new()
    $binding = $null
    foreach ($journey in $script:Journeys) {
        $journeyExecution = Invoke-M0SmokeJourney `
            -Journey $journey `
            -Executable $executable `
            -PackageRoot $package `
            -AppRoot $app `
            -ReceiptPath $ReceiptPath `
            -Workspace $workspace `
            -VerifyReceiptAction $VerifyReceiptAction `
            -ProcessOperations $ProcessOperations `
            -TimeoutMilliseconds $TimeoutMilliseconds `
            -ExpectedBinding $binding
        if ($null -eq $binding) {
            $binding = $journeyExecution.binding
        }
        $journeyResults.Add($journeyExecution.result)
    }

    $final = Assert-M0Verifier `
        -Result (& $VerifyReceiptAction 'final') `
        -Phase 'final' `
        -PackageRoot $package `
        -ReceiptPath $ReceiptPath
    Assert-M0VerifierBinding -Actual $final -Expected $binding -Phase 'final'

    return [pscustomobject][ordered]@{
        verified = $true
        package_id = $final.package_id
        package_root = $package
        contract_sha256 = $final.contract_sha256
        composite_receipt_sha256 = $final.composite_receipt_sha256
        manifest_sha256 = $final.manifest_sha256
        receipt_sha256 = $final.receipt_sha256
        journeys = $journeyResults.ToArray()
        workspace = $workspace
        limits = @(
            'offscreen diagnostic only',
            'sampling is not a complete loader trace',
            'no accessibility ABI or redistribution proof'
        )
    }
}

Export-ModuleMember -Function Test-M0QtShellCompositePackageSmoke
