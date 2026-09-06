#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$formatter = Join-Path $repository 'scripts/Format-Source.ps1'
$testParent = Join-Path $repository 'out/product/test-runs/source-format'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId

function Assert-SafeTestRoot([string] $Root, [string] $Parent, [string] $Id) {
    if ($Id -cnotmatch '\A[0-9a-f]{32}\z' -or -not [IO.Path]::IsPathFullyQualified($Root) -or
        -not [IO.Path]::IsPathFullyQualified($Parent)) { throw 'Unsafe test root identity' }
    $resolved = [IO.Path]::GetFullPath($Root)
    $expected = [IO.Path]::GetFullPath((Join-Path $Parent $Id))
    if (-not $resolved.Equals($expected, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test root leaf' }
    $ancestor = $resolved
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Unsafe test root: reparse ancestor' }
            if (-not $item.PSIsContainer) { throw 'Unsafe test root: ancestor is not a directory' }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

Assert-SafeTestRoot $testRoot $testParent $testId
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0; Next = 0 }

function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) {
    if (-not $Value) { throw $Message }
}
function Set-File([string] $Path, [string] $Text) {
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function New-Fixture {
    $state.Next++
    $directory = Join-Path $testRoot ('case ' + $state.Next + ' 日本; $literal')
    $source = Join-Path $directory 'source tree'
    $tools = Join-Path $directory 'tool paths'
    $null = New-Item -ItemType Directory -Path $source, $tools
    $cpp = Join-Path $source 'src/áudio 日本.cpp'
    $header = Join-Path $source 'include/m0/Track.hpp'
    $qml = Join-Path $source 'src/ui/qml/Player 日本.qml'
    Set-File $cpp "int main() {}`r`n"
    Set-File $header "#pragma once`r`n"
    Set-File $qml "import QtQuick`r`nItem {}`r`n"
    Set-File (Join-Path $source '.clang-format') "BasedOnStyle: LLVM`n"
    $config = @{
        ClangVersion = 'clang-format version 19.1.1'; QmlVersion = 'qmlformat 6.11.2'
        ClangVersionExit = 0; QmlVersionExit = 0; ClangExit = 0; QmlExit = 0
    }
    $configFile = Join-Path $tools 'configuration.json'
    Set-File $configFile (ConvertTo-Json $config)
    $fakeTool = @'
$settings = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'configuration.json') | ConvertFrom-Json
$kind = if ($MyInvocation.MyCommand.Name.StartsWith('clang')) { 'Clang' } else { 'Qml' }
$record = @{ kind = $kind; arguments = @($args) }
[IO.File]::AppendAllText((Join-Path $PSScriptRoot 'calls.jsonl'), ((ConvertTo-Json $record -Compress) + "`n"))
if ($args.Count -eq 1 -and $args[0] -ceq '--version') {
    Write-Output $settings."${kind}Version"
    $global:LASTEXITCODE = $settings."${kind}VersionExit"
    return
}
$global:LASTEXITCODE = $settings."${kind}Exit"
if ($global:LASTEXITCODE -ne 0) { return }
$files = @($args | Where-Object { [IO.File]::Exists($_) })
if ($args -contains '-i') {
    foreach ($file in $files) {
        $text = [IO.File]::ReadAllText($file).Replace("`r`n", "`n").Replace("`r", "`n").Replace('BAD', 'GOOD')
        [IO.File]::WriteAllText($file, $text, [Text.UTF8Encoding]::new($false))
    }
} elseif ($kind -ceq 'Qml') {
    if ($files.Count -ne 1) { throw 'Expected exactly one QML input' }
    $text = [IO.File]::ReadAllText($files[0]).Replace("`r`n", "`n").Replace("`r", "`n").Replace('BAD', 'GOOD')
    $text.TrimEnd("`n") -split "`n" | Write-Output
}
'@
    $clang = Join-Path $tools 'clang fake.ps1'
    $qmlTool = Join-Path $tools 'qml fake.ps1'
    Set-File $clang $fakeTool
    Set-File $qmlTool $fakeTool
    return @{ Source = $source; Clang = $clang; Qml = $qmlTool; CppFile = $cpp; Header = $header
        QmlFile = $qml; Config = $config; ConfigFile = $configFile; Log = (Join-Path $tools 'calls.jsonl') }
}
function Invoke-Formatter($Fixture, [switch] $Apply) {
    & $formatter -ClangFormat $Fixture.Clang -QmlFormat $Fixture.Qml -SourceRoot $Fixture.Source -Apply:$Apply
}
function Get-Calls($Fixture) {
    if ([IO.File]::Exists($Fixture.Log)) {
        Get-Content -LiteralPath $Fixture.Log | ForEach-Object { ConvertFrom-Json $_ }
    }
}
function Save-Config($Fixture) { Set-File $Fixture.ConfigFile (ConvertTo-Json $Fixture.Config) }
function Assert-Rejected([scriptblock] $Operation, [string] $Pattern) {
    try { & $Operation | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected formatter rejection'
}

try {
    Test-Case 'fixture ownership rejects a malformed ID and a mismatched leaf' {
        Assert-Rejected { Assert-SafeTestRoot $testRoot $testParent '../outside' } 'Unsafe test'
        Assert-Rejected { Assert-SafeTestRoot $testParent $testParent $testId } 'Unsafe test'
    }
    Test-Case 'fixture ownership rejects reparse ancestors before creation or cleanup' {
        $outside = Join-Path $testRoot 'outside-owned'
        $junction = Join-Path $testRoot 'junction-owned'
        $id = [Guid]::NewGuid().ToString('N')
        $null = [IO.Directory]::CreateDirectory($outside)
        $sentinel = Join-Path $outside 'preserve.txt'
        Set-File $sentinel 'preserve'
        $null = New-Item -ItemType Junction -Path $junction -Target $outside
        try {
            $nested = Join-Path $junction $id
            Assert-Rejected { Assert-SafeTestRoot $nested $junction $id } 'reparse'
            Assert-True (-not [IO.Directory]::Exists((Join-Path $outside $id))) 'Guard created an external leaf'
            $null = [IO.Directory]::CreateDirectory((Join-Path $outside $id))
            Assert-Rejected { Assert-SafeTestRoot $nested $junction $id } 'reparse'
            Assert-True ([IO.Directory]::Exists((Join-Path $outside $id))) 'Guard removed an external leaf'
            Assert-True ([IO.File]::ReadAllText($sentinel) -ceq 'preserve') 'Guard changed the sentinel'
        } finally {
            # Remove only this owned junction entry; never recurse through it.
            [IO.Directory]::Delete($junction)
        }
    }
    Test-Case 'check handles literal Unicode and spaces, uses correct flags and preserves bytes' {
        $fixture = New-Fixture
        $before = @($fixture.CppFile, $fixture.Header, $fixture.QmlFile | ForEach-Object {
            (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
        }) -join '|'
        $result = @(Invoke-Formatter $fixture)
        Assert-True (($result -join ' ') -match '2 C\+\+.*1 QML') 'Missing exact source counts'
        $after = @($fixture.CppFile, $fixture.Header, $fixture.QmlFile | ForEach-Object {
            (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash
        }) -join '|'
        Assert-True ($before -ceq $after) 'Check changed source bytes'
        $calls = @(Get-Calls $fixture)
        $clangCall = @($calls | Where-Object { $_.kind -ceq 'Clang' -and $_.arguments -notcontains '--version' })
        $qmlCall = @($calls | Where-Object { $_.kind -ceq 'Qml' -and $_.arguments -notcontains '--version' })
        Assert-True ($clangCall.Count -eq 1 -and $clangCall[0].arguments -contains '--dry-run' -and
            $clangCall[0].arguments -contains '--Werror' -and $clangCall[0].arguments -contains $fixture.CppFile) 'Wrong clang check command'
        Assert-True ($qmlCall.Count -eq 1 -and $qmlCall[0].arguments -contains '--newline' -and
            $qmlCall[0].arguments -contains 'unix' -and $qmlCall[0].arguments -contains $fixture.QmlFile -and
            $qmlCall[0].arguments -notcontains '--check' -and $qmlCall[0].arguments -notcontains '--dry-run') 'Wrong QML check command'
    }
    Test-Case 'enumerates all four C++ roots but excludes experiments and output caches' {
        $fixture = New-Fixture
        Set-File (Join-Path $fixture.Source 'tests/Contract.cpp') "int test() {}`n"
        Set-File (Join-Path $fixture.Source 'tools/Generator.hpp') "#pragma once`n"
        foreach ($relative in @('out/generated.cpp', 'experiments/demo.cpp', 'src/out/cache.hpp',
                'tests/experiments/demo.cpp', 'out/Bad.qml', 'experiments/Bad.qml', 'src/ui/qml/out/Bad.qml')) {
            Set-File (Join-Path $fixture.Source $relative) "BAD`n"
        }
        $result = @(Invoke-Formatter $fixture)
        Assert-True (($result -join ' ') -match '4 C\+\+.*1 QML') 'Wrong source inventory'
        $arguments = @(Get-Calls $fixture | ForEach-Object { $_.arguments })
        Assert-True (-not ($arguments | Where-Object { $_ -match '[\\/](?:out|experiments)[\\/](?:generated|demo|cache|Bad)\.' })) 'Excluded file reached a formatter'
    }
    Test-Case 'inventory fallback without rg keeps the same source boundaries' {
        $fixture = New-Fixture
        Set-File (Join-Path $fixture.Source 'tools/Generator.cpp') "int generate() {}`n"
        Set-File (Join-Path $fixture.Source 'src/out/Excluded.cpp') "BAD`n"
        Set-File (Join-Path $fixture.Source 'src/ui/qml/experiments/Excluded.qml') "BAD`n"
        $originalPath = $env:PATH
        try {
            $env:PATH = ''
            $result = @(Invoke-Formatter $fixture)
        } finally { $env:PATH = $originalPath }
        Assert-True (($result -join ' ') -match '3 C\+\+.*1 QML') 'Fallback inventory differs'
        $arguments = @(Get-Calls $fixture | ForEach-Object { $_.arguments })
        Assert-True (-not ($arguments | Where-Object { $_ -match 'Excluded\.' })) 'Fallback included a cache or experiment'
    }
    Test-Case 'Apply formats both languages explicitly' {
        $fixture = New-Fixture
        Set-File $fixture.CppFile "BAD`n"
        Set-File $fixture.QmlFile "BAD`n"
        Invoke-Formatter $fixture -Apply | Out-Null
        Assert-True ([IO.File]::ReadAllText($fixture.CppFile) -ceq "GOOD`n") 'C++ Apply was not executed'
        Assert-True ([IO.File]::ReadAllText($fixture.QmlFile) -ceq "GOOD`n") 'QML Apply was not executed'
        $calls = @(Get-Calls $fixture | Where-Object { $_.arguments -notcontains '--version' })
        Assert-True ($calls.Count -eq 2) 'Unexpected Apply command count'
        foreach ($call in $calls) { Assert-True ($call.arguments -contains '-i') 'Apply did not pass -i' }
    }
    Test-Case 'QML mismatch rejects without modifying the file' {
        $fixture = New-Fixture
        Set-File $fixture.QmlFile "BAD`r`n"
        $before = (Get-FileHash -LiteralPath $fixture.QmlFile).Hash
        Assert-Rejected { Invoke-Formatter $fixture } 'QML.*format|format.*QML'
        Assert-True ((Get-FileHash -LiteralPath $fixture.QmlFile).Hash -ceq $before) 'Rejected check changed QML'
    }
    Test-Case 'QML check does not hide a missing final newline' {
        $fixture = New-Fixture
        Set-File $fixture.QmlFile 'Item {}'
        Assert-Rejected { Invoke-Formatter $fixture } 'QML.*format|format.*QML'
        Assert-True ([IO.File]::ReadAllText($fixture.QmlFile) -ceq 'Item {}') 'Check added a newline'
    }
    foreach ($kind in @('Clang', 'Qml')) {
        Test-Case "$kind formatter failure is propagated" {
            $fixture = New-Fixture
            $fixture.Config["${kind}Exit"] = 7
            Save-Config $fixture
            Assert-Rejected { Invoke-Formatter $fixture } 'exit.*7|code.*7'
        }
        Test-Case "$kind version probe failure is propagated" {
            $fixture = New-Fixture
            $fixture.Config["${kind}VersionExit"] = 9
            Save-Config $fixture
            Assert-Rejected { Invoke-Formatter $fixture } 'exit.*9|code.*9'
        }
        Test-Case "$kind version is pinned exactly" {
            $fixture = New-Fixture
            $fixture.Config["${kind}Version"] = if ($kind -ceq 'Clang') { 'clang-format version 19.1.10' } else { 'qmlformat 6.11.20' }
            Save-Config $fixture
            Assert-Rejected { Invoke-Formatter $fixture } 'version|19\.1\.1|6\.11\.2'
        }
    }
    Test-Case 'missing tool is rejected before formatting' {
        $fixture = New-Fixture
        $fixture.Clang = Join-Path $fixture.Source 'missing clang.exe'
        Assert-Rejected { Invoke-Formatter $fixture } 'tool|file|missing'
    }
    Test-Case 'an empty C++ inventory is not success' {
        $fixture = New-Fixture
        Remove-Item -LiteralPath $fixture.CppFile, $fixture.Header
        Assert-Rejected { Invoke-Formatter $fixture } 'C\+\+.*empty|no C\+\+|C\+\+.*source'
    }
    Test-Case 'an empty QML inventory is not success' {
        $fixture = New-Fixture
        Remove-Item -LiteralPath $fixture.QmlFile
        Assert-Rejected { Invoke-Formatter $fixture } 'QML.*empty|no QML|QML.*source'
    }
} finally {
    Assert-SafeTestRoot $testRoot $testParent $testId
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "Source format: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
