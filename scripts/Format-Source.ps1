#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $ClangFormat,
    [Parameter(Mandatory)][string] $QmlFormat,
    [switch] $Apply,
    [string] $SourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-ToolFile([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing formatter tool file: $Path" }
    return (Get-Item -LiteralPath $Path).FullName
}

function Invoke-SourceTool([string] $Tool, [string[]] $ToolArguments) {
    # Argument arrays preserve spaces, Unicode and literal shell metacharacters.
    # Reset the status for script tools as well as native executables.
    $global:LASTEXITCODE = 0
    $output = @(& $Tool @ToolArguments)
    $toolExitCode = $LASTEXITCODE
    if ($toolExitCode -ne 0) { throw "Formatter tool exited with code ${toolExitCode}: $Tool" }
    return $output
}

function Get-SourceFiles([string[]] $Roots, [string[]] $Extensions) {
    $existingRoots = @($Roots | Where-Object { [IO.Directory]::Exists((Join-Path $SourceRoot $_)) })
    if ($existingRoots.Count -eq 0) { return }
    $ripgrep = Get-Command rg -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $ripgrep) {
        $arguments = @('--files', '--hidden', '--no-ignore')
        foreach ($extension in $Extensions) { $arguments += @('-g', "*$extension") }
        $arguments += @('--') + $existingRoots
        Push-Location -LiteralPath $SourceRoot
        try {
            $global:LASTEXITCODE = 0
            $relativeFiles = @(& $ripgrep.Source @arguments)
            $inventoryExitCode = $LASTEXITCODE
            if ($inventoryExitCode -notin @(0, 1)) { throw "Source inventory exited with code $inventoryExitCode" }
            $files = @($relativeFiles | ForEach-Object { Join-Path $SourceRoot $_ })
        } finally { Pop-Location }
    } else {
        $files = @($existingRoots | ForEach-Object {
            Get-ChildItem -LiteralPath (Join-Path $SourceRoot $_) -File -Recurse -Force |
                Where-Object { $_.Extension -in $Extensions } | ForEach-Object { $_.FullName }
        })
    }
    $files | Where-Object {
        $relative = [IO.Path]::GetRelativePath($SourceRoot, $_)
        $relative -notmatch '(?:\A|[\\/])(?:out|experiments)(?:[\\/]|\z)'
    } | Sort-Object -Unique
}

if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) { throw "Missing source root directory: $SourceRoot" }
$SourceRoot = (Get-Item -LiteralPath $SourceRoot).FullName
$ClangFormat = Resolve-ToolFile $ClangFormat
$QmlFormat = Resolve-ToolFile $QmlFormat
$cppFiles = @(Get-SourceFiles @('include', 'src', 'tests', 'tools') @('.cpp', '.hpp'))
$qmlFiles = @(Get-SourceFiles @('src/ui/qml', 'tests/ui/quick') @('.qml'))
if ($cppFiles.Count -eq 0) { throw 'C++ source inventory is empty' }
if ($qmlFiles.Count -eq 0) { throw 'QML source inventory is empty' }

$clangVersion = (Invoke-SourceTool $ClangFormat @('--version')) -join "`n"
if ($clangVersion -notmatch '\bclang-format version 19\.1\.1(?:\s|\z)') { throw 'Expected clang-format version 19.1.1' }
$qmlVersion = (Invoke-SourceTool $QmlFormat @('--version')) -join "`n"
if ($qmlVersion -notmatch '\bqmlformat 6\.11\.2(?:\s|\z)') { throw 'Expected qmlformat version 6.11.2' }

[string[]] $clangArguments = if ($Apply) { @('-i') } else { @('--dry-run', '--Werror') }
# Windows limits the complete process command line to 32767 UTF-16 units.
# Count executable/flags and quoted file arguments, leaving headroom for launch
# quoting. Arrays still preserve each literal path when an inventory needs more
# than one invocation; every batch must succeed before continuing to QML.
$maximumCommandCharacters = 30000
$clangPrefixCharacters = $ClangFormat.Length + 3
foreach ($argument in $clangArguments) { $clangPrefixCharacters += $argument.Length + 3 }
$clangBatch = [Collections.Generic.List[string]]::new()
$clangCharacters = $clangPrefixCharacters
foreach ($file in $cppFiles) {
    $fileCharacters = $file.Length + 3
    if ($clangPrefixCharacters + $fileCharacters -gt $maximumCommandCharacters) {
        throw "C++ formatter input exceeds the Windows command line budget: $file"
    }
    if ($clangBatch.Count -gt 0 -and $clangCharacters + $fileCharacters -gt $maximumCommandCharacters) {
        Invoke-SourceTool $ClangFormat ($clangArguments + $clangBatch.ToArray()) | Out-Null
        $clangBatch.Clear()
        $clangCharacters = $clangPrefixCharacters
    }
    $clangBatch.Add($file)
    $clangCharacters += $fileCharacters
}
if ($clangBatch.Count -gt 0) {
    Invoke-SourceTool $ClangFormat ($clangArguments + $clangBatch.ToArray()) | Out-Null
}
foreach ($file in $qmlFiles) {
    if ($Apply) {
        Invoke-SourceTool $QmlFormat @('-i', '--newline', 'unix', $file) | Out-Null
    } else {
        # qmlformat 6.11.2 has no --check. Compare its stdout without writing
        # the source, normalizing only newline representation on both sides.
        $lines = @(Invoke-SourceTool $QmlFormat @('--newline', 'unix', $file))
        $formatted = if ($lines.Count -eq 0) { '' } else { ($lines -join "`n") + "`n" }
        $original = [IO.File]::ReadAllText($file).Replace("`r`n", "`n").Replace("`r", "`n")
        if ($original -cne $formatted) { throw "QML formatting differs: $file" }
    }
}
$mode = if ($Apply) { 'Applied formatting' } else { 'Checked formatting' }
Write-Output "${mode}: $($cppFiles.Count) C++ files and $($qmlFiles.Count) QML files."
