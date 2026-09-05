#Requires -Version 7.0
[CmdletBinding()]
param([string] $MakeNsis)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
if (-not $MakeNsis) { $MakeNsis = Join-Path $repository 'out/m0/toolchains/nsis-3.12/makensis.exe' }
$toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../nsis-toolchain.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath $MakeNsis -Algorithm SHA256).Hash.ToLowerInvariant() -cne $toolchain.compilerSha256) {
    throw 'Unexpected NSIS compiler hash'
}
$testParent = Join-Path $repository 'out/m0/test-runs/rc-music-installer-files'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$null = New-Item -ItemType Directory -Path $testRoot
$executable = Join-Path $testRoot 'Probe.exe'
$source = Join-Path $testRoot 'source ação 日本.txt'
$readonly = Join-Path $testRoot 'readonly.txt'
$junction = Join-Path $testRoot 'junction'
$state = @{ Passed = 0; Failed = 0 }
$ownedProcesses = [Collections.Generic.List[Diagnostics.Process]]::new()

function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Start-Probe([string] $Case, [switch] $Uninstall) {
    $program = if ($Uninstall) { Join-Path $testRoot 'Uninstall.exe' } else { $executable }
    $start = [Diagnostics.ProcessStartInfo]::new($program)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $testRoot
    # _?= must be the last unquoted tail; the original uninstaller executes synchronously.
    $start.Arguments = if ($Uninstall) { "/CASE=$Case _?=$testRoot" } else { "/CASE=$Case" }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    if (-not $process.Start()) { throw 'Could not start native helper probe' }
    $ownedProcesses.Add($process)
    return $process
}
function Read-Result([string] $Case) {
    $data = Get-Content -LiteralPath (Join-Path $testRoot "report-$Case.txt") -Raw | ConvertFrom-StringData
    Assert-Equal $data.preserved '1'
    return $data
}
function Invoke-Probe([string] $Case, [switch] $Uninstall) {
    $process = Start-Probe $Case -Uninstall:$Uninstall
    if (-not $process.WaitForExit(15000)) { throw 'Native helper probe timed out' }
    Assert-Equal $process.ExitCode 0
    return Read-Result $Case
}

try {
    [IO.File]::WriteAllBytes($source, [Text.Encoding]::UTF8.GetBytes('abc'))
    [IO.File]::WriteAllBytes((Join-Path $testRoot 'empty.txt'), [byte[]]::new(0))
    $largeBytes = [byte[]]::new(131089)
    for ($index = 0; $index -lt $largeBytes.Length; $index++) { $largeBytes[$index] = $index % 251 }
    $largePath = Join-Path $testRoot 'large.bin'
    [IO.File]::WriteAllBytes($largePath, $largeBytes)
    $largeHash = (Get-FileHash -LiteralPath $largePath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($readonly, 'read-only sentinel')
    [IO.File]::SetAttributes($readonly, [IO.FileAttributes]::ReadOnly)
    $null = New-Item -ItemType Junction -Path $junction -Target $testRoot
    & $MakeNsis /WX /V2 /INPUTCHARSET UTF8 "/DRCMUSIC_TEST_OUTPUT=$executable" "/DRCMUSIC_TEST_DIRECTORY=$testRoot" `
        (Join-Path $PSScriptRoot 'InstallerFilesHarness.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'Native helper probe did not compile' }
    $bootstrap = Start-Probe 'bootstrap'
    if (-not $bootstrap.WaitForExit(15000)) { throw 'Uninstaller bootstrap timed out' }
    Assert-Equal $bootstrap.ExitCode 0

    foreach ($uninstall in @($false, $true)) {
        $label = if ($uninstall) { 'uninstaller' } else { 'installer' }
        Test-Case "$label hashes Unicode-path file and preserves registers" {
            $result = Invoke-Probe 'hash' -Uninstall:$uninstall
            Assert-Equal $result.status '0'
            Assert-Equal $result.hash 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad'
        }
        Test-Case "$label hashes empty file" {
            $result = Invoke-Probe 'empty' -Uninstall:$uninstall
            Assert-Equal $result.status '0'
            Assert-Equal $result.hash 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'
        }
        Test-Case "$label hashes multiple full blocks and a partial final block" {
            $result = Invoke-Probe 'large' -Uninstall:$uninstall
            Assert-Equal $result.status '0'
            Assert-Equal $result.hash $largeHash
        }
        Test-Case "$label distinguishes missing file" {
            $result = Invoke-Probe 'missing' -Uninstall:$uninstall
            Assert-Equal $result.status '2'
            Assert-Equal $result.hash ''
        }
        foreach ($case in @('validate', 'validate-missing')) {
            Test-Case "$label validates safe path $case" { Assert-Equal (Invoke-Probe $case -Uninstall:$uninstall).status '0' }
        }
        foreach ($case in @('directory', 'readonly', 'reparse', 'validate-reparse')) {
            Test-Case "$label rejects $case" { Assert-Equal (Invoke-Probe $case -Uninstall:$uninstall).status '1' }
        }
        Test-Case "$label rejects file already open elsewhere" {
            $locked = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
            try { Assert-Equal (Invoke-Probe 'hash' -Uninstall:$uninstall).status '1' }
            finally { $locked.Dispose() }
        }
        Test-Case "$label releases mutex idempotently" {
            Assert-Equal (Invoke-Probe 'mutex-cycle' -Uninstall:$uninstall).status '0'
            Assert-Equal (Get-Content -LiteralPath (Join-Path $testRoot 'release-status.txt') -Raw) '0'
        }
    }
    Test-Case 'live installer mutex excludes second process and releases for retry' {
        $holder = Start-Probe 'mutex-hold'
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        while (-not (Test-Path -LiteralPath (Join-Path $testRoot 'holder.ready'))) {
            if ($holder.HasExited -or [DateTime]::UtcNow -gt $deadline) { throw 'Mutex holder did not publish readiness' }
            Start-Sleep -Milliseconds 25
        }
        Assert-Equal (Read-Result 'mutex-hold').status '0'
        Assert-Equal (Invoke-Probe 'mutex' -Uninstall).status '1'
        [IO.File]::WriteAllText((Join-Path $testRoot 'release.flag'), 'release')
        if (-not $holder.WaitForExit(15000)) { throw 'Mutex holder failed to release' }
        Assert-Equal $holder.ExitCode 0
        Assert-Equal (Invoke-Probe 'mutex').status '0'
    }
} finally {
    foreach ($process in $ownedProcesses) {
        if (-not $process.HasExited) { $process.Kill($true); $process.WaitForExit() }
        $process.Dispose()
    }
    if (Test-Path -LiteralPath $junction) { Remove-Item -LiteralPath $junction -Force }
    if (Test-Path -LiteralPath $readonly) { [IO.File]::SetAttributes($readonly, [IO.FileAttributes]::Normal) }
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or $resolved -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe native helper test cleanup target'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
Write-Host "Installer files helper: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
