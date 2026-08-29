[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$testFile = Join-Path $RepositoryRoot 'experiments\m0\audio-corpus\tests\AudioCorpus.Tests.ps1'
$testRunParent = Join-Path $RepositoryRoot 'out\m0\audio-corpus-test-runs'
$TestTempRoot = Join-Path $testRunParent ([Guid]::NewGuid().ToString('N'))
$TestRunState = [pscustomobject]@{
    Passed = 0
    Failed = 0
    Failures = [Collections.Generic.List[string]]::new()
}

function Test-Case {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Body
    )

    try {
        & $Body
        $TestRunState.Passed++
        Write-Host "[PASS] $Name"
    }
    catch {
        $TestRunState.Failed++
        $failure = "[FAIL] $Name :: $($_.Exception.Message)"
        $TestRunState.Failures.Add($failure)
        Write-Host $failure
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [string] $Message = 'Expected condition to be true.'
    )

    if (-not $Condition) {
        throw [InvalidOperationException]::new($Message)
    }
}

function Assert-Equal {
    param(
        [AllowNull()] $Actual,
        [AllowNull()] $Expected,
        [string] $Message = 'Values differ.'
    )

    if ($Actual -ne $Expected) {
        throw [InvalidOperationException]::new("$Message Expected '$Expected', got '$Actual'.")
    }
}

function Assert-SequenceEqual {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [string] $Message = 'Sequences differ.'
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [InvalidOperationException]::new("$Message Expected $($Expected.Count) items, got $($Actual.Count).")
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($Actual[$index] -cne $Expected[$index]) {
            throw [InvalidOperationException]::new("$Message Difference at ${index}: expected '$($Expected[$index])', got '$($Actual[$index])'.")
        }
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)] [scriptblock] $Body,
        [string] $MessagePattern = ''
    )

    $message = $null
    try { & $Body } catch { $message = $_.Exception.Message }
    if ($null -eq $message) {
        throw [InvalidOperationException]::new('Expected action to throw.')
    }
    if ($MessagePattern -and $message -notmatch $MessagePattern) {
        throw [InvalidOperationException]::new("Exception '$message' did not match '$MessagePattern'.")
    }
}

New-Item -ItemType Directory -Path $TestTempRoot -Force | Out-Null
try {
    & $testFile
}
catch {
    $TestRunState.Failed++
    $failure = "[FAIL] test load :: $($_.Exception.Message)"
    $TestRunState.Failures.Add($failure)
    Write-Host $failure
}
finally {
    if (Test-Path -LiteralPath $TestTempRoot) {
        Remove-Item -LiteralPath $TestTempRoot -Recurse -Force
    }
}

Write-Host "Tests: $($TestRunState.Passed) passed, $($TestRunState.Failed) failed."
if ($TestRunState.Failed -gt 0) { exit 1 }
