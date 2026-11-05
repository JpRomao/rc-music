#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$sourceDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
# Reviewed product snapshot. Three includes preserve the promotion baseline
# from 3f539d6ed39d5e990fa736e3b324fa3de67a8fc2; the main script and browser
# helper add explicit optional components, covered by focused contract tests.
# This inventory test never invokes Git or NSIS.
$sources = [ordered]@{
    'RcMusicInstaller.nsi' = 'd6dc65afe81313194ab1a4dc672b502e1c661c4f25dfb3c4f197382615d3ee37'
    'RcMusicInstallerFiles.nsh' = '530f28af503ae23c54461c57a82d95802d44ed9e129edbb3d842b1322469bb06'
    'RcMusicAssociationSections.nsh' = '67174c0a56f73049fc3cb4dd13e064ec41f60fdc0ab5c1f65ccd787b5e6a585d'
    'RcMusicAssociations.nsh' = '56321242fad0615189474d63a6f32d81d5cc5e92a7a38158a5d9eb9cb760baca'
    'RcMusicBrowserHosts.nsh' = '08fec82c47263aeb3569ec2af4fdf4d822155220f174ad80f2181ba407a28577'
}
$sdkIncludes = @('LogicLib.nsh', 'FileFunc.nsh', 'x64.nsh', 'MUI2.nsh', 'Sections.nsh')
$state = @{ Passed = 0; Failed = 0 }
$runId = [Guid]::NewGuid().ToString('N')
$testParent = Join-Path $repository 'out/product/test-runs/rc-music-installer-sources'
$testRoot = Join-Path $testParent $runId

function Test-Case([string] $Label, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Label" }
    catch { $state.Failed++; Write-Host "[FAIL] $Label :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'" }
}
function Assert-SafeTestRoot {
    if ($runId -cnotmatch '\A[0-9a-f]{32}\z' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $runId))) {
        throw 'Unsafe installer-source test root'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try {
            $attributes = [IO.File]::GetAttributes($ancestor)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($attributes -band [IO.FileAttributes]::Directory) -eq 0) {
                throw 'Unsafe installer-source test ancestor'
            }
        } catch {
            $cause = $_.Exception.GetBaseException()
            if ($cause -isnot [IO.FileNotFoundException] -and $cause -isnot [IO.DirectoryNotFoundException]) { throw }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Assert-SourceBytes([string] $Directory, [string] $Name) {
    $path = Join-Path $Directory $Name
    if (-not [IO.File]::Exists($path)) { throw "Promoted NSIS source is missing: $Name" }
    Assert-Equal (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() $sources[$Name]
}
function Assert-ProjectIncludeClosure([string] $Directory) {
    # Audit only literal project includes; SDK files and the explicit generated
    # RCMUSIC_CONFIG are external inputs. This is not an NSIS preprocessor.
    $pending = [Collections.Generic.Stack[string]]::new()
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $pending.Push('RcMusicInstaller.nsi')
    while ($pending.Count) {
        $name = $pending.Pop()
        if (-not $visited.Add($name)) { continue }
        Assert-SourceBytes $Directory $name
        $content = [IO.File]::ReadAllText((Join-Path $Directory $name))
        foreach ($match in [regex]::Matches($content, '(?m)^!include\s+(?:"([^"\r\n]+)"|([^\s\r\n]+))\s*$')) {
            $include = if ($match.Groups[1].Success) { $match.Groups[1].Value } else { $match.Groups[2].Value }
            if ($include -ceq '${RCMUSIC_CONFIG}' -or $sdkIncludes -ccontains $include) { continue }
            if (-not $sources.Contains($include)) { throw "Unexpected project include: $include" }
            $pending.Push($include)
        }
    }
    Assert-Equal (@($visited | Sort-Object) -join '|') (@($sources.Keys | Sort-Object) -join '|')
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Installer-source test GUID must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    foreach ($name in $sources.Keys) {
        Test-Case "promoted NSIS bytes match the recorded original: $name" {
            Assert-SourceBytes $sourceDirectory $name
        }
    }
    Test-Case 'exported five-file include closure is independent of experiments and caller CWD' {
        $exportRoot = Join-Path $testRoot 'exported product 日本'
        $exportDirectory = Join-Path $exportRoot 'packaging/windows'
        $foreignDirectory = Join-Path $testRoot 'unrelated working directory'
        $null = New-Item -ItemType Directory -Path $exportDirectory, $foreignDirectory
        foreach ($name in $sources.Keys) {
            Copy-Item -LiteralPath (Join-Path $sourceDirectory $name) -Destination (Join-Path $exportDirectory $name)
        }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
        Push-Location -LiteralPath $foreignDirectory
        try { Assert-ProjectIncludeClosure $exportDirectory }
        finally { Pop-Location }
        Assert-Equal (@(Get-ChildItem -LiteralPath $exportDirectory -File).Count) 5
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
    }
    if ($IncludeLegacy) {
        # Only the four historical entrypoints have legacy forwarders. The new
        # helper is reached through the promoted main template's include path.
        foreach ($name in @($sources.Keys | Where-Object { $_ -cne 'RcMusicBrowserHosts.nsh' })) {
            Test-Case "legacy NSIS include forwards without changing compiler CWD: $name" {
                $legacy = Join-Path $repository ('experiments/m0/candidates/qt/windows-installer/' + $name)
                $commands = @([IO.File]::ReadAllLines($legacy) | Where-Object { $_.Trim() -and -not $_.TrimStart().StartsWith(';') })
                $relative = '${__FILEDIR__}/../../../../../packaging/windows'
                Assert-Equal ($commands -join "`n") (('!addincludedir "' + $relative + '"') + "`n" +
                    ('!include "' + $relative + '/' + $name + '"'))
                $resolved = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($legacy)) ('../../../../../packaging/windows/' + $name)))
                Assert-Equal $resolved (Join-Path $sourceDirectory $name)
                Assert-ProjectIncludeClosure $sourceDirectory
            }
        }
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
Write-Host "Installer source contracts: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed) { exit 1 }
