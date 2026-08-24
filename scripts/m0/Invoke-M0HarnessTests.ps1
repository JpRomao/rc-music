[CmdletBinding()]
param(
    [string[]] $TestFile = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [System.PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}
$PSNativeCommandUseErrorActionPreference = $true

$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$modulePath = Join-Path $RepositoryRoot 'experiments\m0\harness\M0.Harness.psd1'
Import-Module $modulePath -Force -ErrorAction Stop
$testsRoot = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\harness\tests'
$testRunParent = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'out\m0\test-runs'
$TestTempRoot = Resolve-M0ScopedPath -BasePath $testRunParent -CandidatePath ([Guid]::NewGuid().ToString('N'))

$script:Passed = 0
$script:Failed = 0
$script:Failures = [Collections.Generic.List[string]]::new()

function Test-Case {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Body
    )

    try {
        & $Body
        $script:Passed++
        Write-Host "[PASS] $Name"
    }
    catch {
        $script:Failed++
        $failure = "[FAIL] $Name :: $($_.Exception.Message)"
        $script:Failures.Add($failure)
        Write-Host $failure
        throw
    }
}

function Assert-True {
    param(
        [Parameter(Mandatory)] [bool] $Condition,
        [string] $Message = 'Expected condition to be true.'
    )

    if (-not $Condition) {
        throw [System.InvalidOperationException]::new($Message)
    }
}

function Assert-Equal {
    param(
        [AllowNull()] $Actual,
        [AllowNull()] $Expected,
        [string] $Message = 'Values differ.'
    )

    if ($Actual -ne $Expected) {
        throw [System.InvalidOperationException]::new("$Message Expected '$Expected', got '$Actual'.")
    }
}

function Assert-SequenceEqual {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [string] $Message = 'Sequences differ.'
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [System.InvalidOperationException]::new("$Message Expected $($Expected.Count) items, got $($Actual.Count).")
    }

    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($Actual[$index] -ne $Expected[$index]) {
            throw [System.InvalidOperationException]::new("$Message Difference at ${index}: expected '$($Expected[$index])', got '$($Actual[$index])'.")
        }
    }
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)] [scriptblock] $Body,
        [string] $MessagePattern = ''
    )

    $didThrow = $false
    $actualMessage = ''
    try {
        & $Body
    }
    catch {
        $didThrow = $true
        $actualMessage = $_.Exception.Message
    }

    if (-not $didThrow) {
        throw [System.InvalidOperationException]::new('Expected action to throw.')
    }
    if ($MessagePattern -and $actualMessage -notmatch $MessagePattern) {
        throw [System.InvalidOperationException]::new("Exception '$actualMessage' did not match '$MessagePattern'.")
    }
}

function Resolve-TestFilePath {
    param(
        [Parameter(Mandatory)] [string] $InputPath
    )

    $contained = Resolve-M0ScopedPath -BasePath $testsRoot -CandidatePath $InputPath
    $resolved = (Resolve-Path -LiteralPath $contained -ErrorAction Stop).Path
    $null = Resolve-M0ScopedPath -BasePath $testsRoot -CandidatePath $resolved
    if ([IO.Path]::GetExtension($resolved) -ne '.ps1') {
        throw [ArgumentException]::new("Focused test path '$InputPath' resolves outside the M0 harness test root or is not a .ps1 file.")
    }
    return $resolved
}

function Resolve-TestFiles {
    if ($TestFile.Count -eq 0) {
        return @(Get-ChildItem -LiteralPath $testsRoot -Filter '*.Tests.ps1' -File | Sort-Object Name | Select-Object -ExpandProperty FullName)
    }

    $resolved = foreach ($item in $TestFile) {
        Resolve-TestFilePath -InputPath $item
    }
    return @($resolved)
}

New-Item -ItemType Directory -Path $TestTempRoot -Force | Out-Null

try {
    $files = @(Resolve-TestFiles)
    if ($files.Count -eq 0) {
        throw [System.InvalidOperationException]::new("No tests found under '$testsRoot'.")
    }

    foreach ($file in $files) {
        . $file
    }
}
catch {
    if ($script:Failed -eq 0) {
        $script:Failed++
        $failure = "[FAIL] harness load :: $($_.Exception.Message)"
        $script:Failures.Add($failure)
        Write-Host $failure
    }
}
finally {
    $resolvedTemp = Resolve-M0ScopedPath -BasePath $testRunParent -CandidatePath $TestTempRoot
    if (Test-Path -LiteralPath $resolvedTemp) {
        Remove-Item -LiteralPath $resolvedTemp -Recurse -Force
    }
}

Write-Host "Tests: $($script:Passed) passed, $($script:Failed) failed."
if ($script:Failed -gt 0) {
    exit 1
}
