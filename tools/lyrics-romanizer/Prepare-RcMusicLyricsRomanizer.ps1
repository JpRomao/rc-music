#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PythonExecutable,
    [Parameter(Mandatory)]
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'

function Invoke-RuntimePython {
    param(
        [string]$Executable,
        [string]$Failure,
        [string[]]$Arguments
    )

    $start = [System.Diagnostics.ProcessStartInfo]::new($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment.Clear()
    $windows = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $start.Environment['SystemRoot'] = $windows
    $start.Environment['WINDIR'] = $windows
    $start.Environment['TEMP'] = [System.IO.Path]::GetTempPath()
    $start.Environment['TMP'] = $start.Environment['TEMP']
    $start.Environment['PIP_CONFIG_FILE'] = 'NUL'
    foreach ($argument in $Arguments) {
        $start.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        if ($stdout.Result) {
            Write-Host $stdout.Result.TrimEnd()
        }
        if ($stderr.Result) {
            Write-Host $stderr.Result.TrimEnd()
        }
        if ($process.ExitCode -ne 0) {
            throw $Failure
        }
    } finally {
        $process.Dispose()
    }
}

$entrypoint = Get-Item -LiteralPath (Join-Path $PSScriptRoot 'romanize.py')
$lock = Get-Item -LiteralPath (Join-Path $PSScriptRoot 'requirements.lock')
$python = Get-Item -LiteralPath $PythonExecutable
if ($python.PSIsContainer -or $python.Extension -ine '.exe') {
    throw 'Select an installed Windows CPython executable.'
}

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    throw "The runtime directory already exists: $output"
}

Invoke-RuntimePython -Executable $python.FullName -Failure 'Use 64-bit CPython 3.12, 3.13 or 3.14 for Windows.' -Arguments @(
    '-I', '-B', '-c', 'import struct, sys; sys.exit(not (sys.platform == "win32" and sys.implementation.name == "cpython" and (3, 12) <= sys.version_info[:2] <= (3, 14) and struct.calcsize("P") == 8))'
)
Invoke-RuntimePython -Executable $python.FullName -Failure 'Could not create the romanizer environment.' -Arguments @(
    '-I', '-B', '-m', 'venv', $output
)

$runtimePython = Join-Path $output 'Scripts/python.exe'
Invoke-RuntimePython -Executable $runtimePython -Failure 'Could not install the pinned romanizer wheels.' -Arguments @(
    '-I', '-B', '-m', 'pip', '--isolated', '--disable-pip-version-check', '--no-cache-dir',
    'install', '--index-url', 'https://pypi.org/simple', '--require-hashes', '--only-binary=:all:', '-r', $lock.FullName
)
Invoke-RuntimePython -Executable $runtimePython -Failure 'The romanizer environment failed its dependency check.' -Arguments @(
    '-I', '-B', '-m', 'pip', '--isolated', '--disable-pip-version-check', 'check'
)

# The entrypoint is the last file copied so an interrupted install is not detected.
Copy-Item -LiteralPath $entrypoint.FullName -Destination $output
Write-Output $output
