#Requires -Version 7.0
[CmdletBinding()]
param([string] $Filter = '*')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$prerequisiteModule = Import-Module (Join-Path $PSScriptRoot '../QtShellPackagePrerequisites.psm1') -Force -PassThru
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$testRoot = Join-Path $repository ('out/m0/test-runs/qt-package/prerequisites-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0 }

function Test-Case([string] $Name, [scriptblock] $Body) {
    if ($Name -notlike $Filter) { return }
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection.'
}
function New-EnvironmentFixture {
    $installation = 'C:\VS sintético com espaços\18\BuildTools'
    $toolset = "$installation\VC\Tools\MSVC\14.44.35207"
    $programFilesX86 = 'C:\Programas (x86)'
    $sdk = "$programFilesX86\Windows Kits\10"
    $netfx = "$programFilesX86\Windows Kits\NETFXSDK\4.8"
    return @{
        CompilerPath = "$toolset\bin\Hostx64\x64\cl.exe"
        ToolsetVersion = '14.44.35207'
        ProgramFilesX86 = $programFilesX86
        WindowsDirectory = 'C:\Windows'
        Values = @{
            VCToolsInstallDir = "$toolset\"
            VCToolsVersion = '14.44.35207'
            VSINSTALLDIR = "$installation\"
            VCINSTALLDIR = "$installation\VC\"
            WindowsSdkDir = "$sdk\"
            WindowsSDKVersion = '10.0.22621.0\'
            WindowsSDKLibVersion = '10.0.22621.0\'
            VSCMD_ARG_HOST_ARCH = 'x64'
            VSCMD_ARG_TGT_ARCH = 'x64'
            INCLUDE = "$toolset\include;$sdk\include\10.0.22621.0\ucrt;$sdk\include\10.0.22621.0\um;$sdk\include\10.0.22621.0\shared;$sdk\include\10.0.22621.0\winrt;$sdk\include\10.0.22621.0\cppwinrt;$netfx\include\um"
            LIB = "$toolset\lib\x64;$netfx\lib\um\x64;$sdk\lib\10.0.22621.0\ucrt\x64;$sdk\lib\10.0.22621.0\um\x64"
            LIBPATH = "$toolset\lib\x64;$toolset\lib\x86\store\references;$sdk\UnionMetadata\10.0.22621.0;$sdk\References\10.0.22621.0;C:\Windows\Microsoft.NET\Framework64\v4.0.30319"
            PATH = 'C:\ambient path is not evidence'
            PRIVATE_TOKEN = 'must-not-be-returned'
        }
    }
}

Test-Case 'environment accepts the known x64 14.44 SDK22621 roots and safe snapshot' {
    $fixture = New-EnvironmentFixture
    $snapshot = Get-QtShellPackageEnvironment @fixture
    Assert-Equal $snapshot.VCToolsInstallDir $fixture.Values.VCToolsInstallDir.TrimEnd('\')
    Assert-Equal $snapshot.WindowsSDKVersion '10.0.22621.0'
    foreach ($name in @('INCLUDE', 'LIB', 'LIBPATH')) { Assert-Equal $snapshot.$name $fixture.Values[$name] }
    Assert-Equal @($snapshot.PSObject.Properties).Count 12
    Assert-Equal (($snapshot | ConvertTo-Json -Compress) -match 'PRIVATE_TOKEN|must-not-be-returned|ambient path') $false
}

Test-Case 'environment normalizes benign internal double slashes and path casing' {
    $fixture = New-EnvironmentFixture
    $baseline = Get-QtShellPackageEnvironment @fixture | ConvertTo-Json -Compress
    foreach ($name in @('VCToolsInstallDir', 'VSINSTALLDIR', 'VCINSTALLDIR', 'WindowsSdkDir', 'INCLUDE', 'LIB', 'LIBPATH')) {
        $fixture.Values[$name] = $fixture.Values[$name].Replace('\', '\\').ToUpperInvariant()
    }
    Assert-Equal (Get-QtShellPackageEnvironment @fixture | ConvertTo-Json -Compress) $baseline
}

Test-Case 'environment rejects each absent required field' {
    foreach ($name in @('VCToolsInstallDir', 'VCToolsVersion', 'WindowsSdkDir', 'WindowsSDKVersion', 'VSCMD_ARG_HOST_ARCH', 'VSCMD_ARG_TGT_ARCH', 'INCLUDE', 'LIB', 'LIBPATH')) {
        $fixture = New-EnvironmentFixture
        $fixture.Values.Remove($name)
        Assert-Rejected { Get-QtShellPackageEnvironment @fixture } $name
    }
}

Test-Case 'environment rejects mismatched versions architectures and installation roots' {
    $changes = @{
        VCToolsVersion = '14.43.34808'; WindowsSDKVersion = '10.0.26100.0'
        WindowsSDKLibVersion = '10.0.26100.0'; VSCMD_ARG_HOST_ARCH = 'x86'; VSCMD_ARG_TGT_ARCH = 'x86'
        VCToolsInstallDir = 'C:\OldVC'; VSINSTALLDIR = 'C:\OldVS'; VCINSTALLDIR = 'C:\OldVC'; WindowsSdkDir = 'C:\OtherSDK'
    }
    foreach ($name in $changes.Keys) {
        $fixture = New-EnvironmentFixture
        $fixture.Values[$name] = $changes[$name]
        Assert-Rejected { Get-QtShellPackageEnvironment @fixture } $name
    }
}

Test-Case 'environment rejects compiler family or Hostx64 x64 mismatch' {
    $fixture = New-EnvironmentFixture
    $fixture.ToolsetVersion = '14.43.34808'
    Assert-Rejected { Get-QtShellPackageEnvironment @fixture } 'ToolsetVersion'
    $fixture = New-EnvironmentFixture
    $fixture.CompilerPath = $fixture.CompilerPath.Replace('Hostx64', 'Hostx86')
    Assert-Rejected { Get-QtShellPackageEnvironment @fixture } 'CompilerPath'
}

Test-Case 'environment rejects injected missing reordered and stale include or library roots' {
    foreach ($name in @('INCLUDE', 'LIB', 'LIBPATH')) {
        $fixture = New-EnvironmentFixture
        $original = $fixture.Values[$name]
        $parts = $original.Split(';')
        $variants = @("C:\injected;$original", "$original;C:\injected", ($parts[1..($parts.Count - 1)] -join ';'), ($original.Replace('10.0.22621.0', '10.0.26100.0')), ($original.Replace(';', ';;')))
        $swapped = $parts.Clone()
        $swapped[0] = $parts[1]; $swapped[1] = $parts[0]
        $variants += $swapped -join ';'
        foreach ($variant in $variants) {
            $fixture.Values[$name] = $variant
            Assert-Rejected { Get-QtShellPackageEnvironment @fixture } $name
        }
    }
}

Test-Case 'environment rejects compiler and linker flag injection' {
    foreach ($name in @('CL', '_CL_', 'LINK', '_LINK_')) {
        $fixture = New-EnvironmentFixture
        $fixture.Values[$name] = '/DUNEXPECTED'
        Assert-Rejected { Get-QtShellPackageEnvironment @fixture } ([regex]::Escape($name))
    }
}

Test-Case 'environment rejects relative UNC traversal device and ADS paths' {
    foreach ($value in @('relative', '\\server\share\VC', 'C:\VS\..\VC', '\\?\C:\VC', 'C:\VC:stream')) {
        $fixture = New-EnvironmentFixture
        $fixture.Values.VCToolsInstallDir = $value
        Assert-Rejected { Get-QtShellPackageEnvironment @fixture } 'VCToolsInstallDir'
    }
}

Test-Case 'environment permits absent optional VS and SDK library fields' {
    $fixture = New-EnvironmentFixture
    foreach ($name in @('VSINSTALLDIR', 'VCINSTALLDIR', 'WindowsSDKLibVersion')) { $fixture.Values.Remove($name) }
    $snapshot = Get-QtShellPackageEnvironment @fixture
    foreach ($name in @('VSINSTALLDIR', 'VCINSTALLDIR', 'WindowsSDKLibVersion')) { Assert-Equal $snapshot.$name '' }
}

Test-Case 'pinned file reader checks the exact bytes without executing the file' {
    $path = Join-Path $testRoot 'synthetic.exe'
    [IO.File]::WriteAllText($path, 'not an executable')
    $sha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $path).Length
    $observed = & $prerequisiteModule { param($p, $h, $s) Read-PrerequisitePinnedFile $p $h $s } $path $sha $size
    Assert-Equal $observed.sha256 $sha
    Assert-Equal $observed.size_bytes $size
    Assert-Rejected { & $prerequisiteModule { param($p, $s) Read-PrerequisitePinnedFile $p ('a' * 64) $s } $path $size } 'SHA256'
    Assert-Rejected { & $prerequisiteModule { param($p, $h, $s) Read-PrerequisitePinnedFile $p $h ($s + 1) } $path $sha $size } 'size'
    [IO.File]::WriteAllText($path, 'not an executablE')
    Assert-Rejected { & $prerequisiteModule { param($p, $h, $s) Read-PrerequisitePinnedFile $p $h $s } $path $sha $size } 'SHA256'
}

Test-Case 'pinned file reader rejects a reparse ancestor' {
    $target = Join-Path $testRoot 'target'
    $null = New-Item -ItemType Directory -Path $target
    $path = Join-Path $target 'plain.exe'
    [IO.File]::WriteAllText($path, 'plain bytes')
    $sha = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    $size = (Get-Item -LiteralPath $path).Length
    $junction = Join-Path $testRoot 'junction'
    $null = New-Item -ItemType Junction -Path $junction -Target $target
    Assert-Rejected { & $prerequisiteModule { param($p, $h, $s) Read-PrerequisitePinnedFile $p $h $s } (Join-Path $junction 'plain.exe') $sha $size } 'reparse'
}

Test-Case 'authenticated descriptor maps exactly the four available executor pins' {
    $descriptor = Get-Content -LiteralPath (Join-Path $repository 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain.v1.json') -Raw | ConvertFrom-Json
    $pins = @(& $prerequisiteModule { param($d) Get-PrerequisiteToolPins $d 'C:\synthetic-qt' 'C:\Program Files' } $descriptor)
    Assert-Equal ($pins.name -join ',') 'cmake,ninja,windeployqt,qtpaths'
    Assert-Equal $pins[0].sha256 '8893e3f7647e2d1e0b18af84e53f136a976cefbfd2c15aaddc4b934936a3637f'
    Assert-Equal $pins[1].sha256 '5020138b3757035df9dca9a2243624d5810ffa6ae24444bd95f752cbd1b89123'
    Assert-Equal $pins[2].sha256 'd7a4db063f16cb420c8f89a8393ae92749b0f08a74736c929f795ff9e7b9395b'
    Assert-Equal $pins[3].sha256 'a501a6b7e18dc3bed8c91dc6eb3956199e7a5cbb02bec187f964b33212fe011f'
    Assert-Equal $pins[1].path 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
    Assert-Equal $pins[3].path 'C:\synthetic-qt\bin\qtpaths.exe'
}

Test-Case 'cache requires the pinned Ninja and x64 RC MT from SDK22621 without duplicate keys' {
    $cache = "CMAKE_MAKE_PROGRAM:FILEPATH=C:/Tools/ninja.exe`nCMAKE_RC_COMPILER:FILEPATH=C:/SDK/bin/10.0.22621.0/x64/rc.exe`nCMAKE_MT:FILEPATH=C:/SDK/bin/10.0.22621.0/x64/mt.exe`n"
    $observed = & $prerequisiteModule { param($text) Get-PrerequisiteCacheTools $text 'C:\Tools\ninja.exe' 'C:\SDK' } $cache
    Assert-Equal $observed.rc 'C:\SDK\bin\10.0.22621.0\x64\rc.exe'
    Assert-Equal $observed.mt 'C:\SDK\bin\10.0.22621.0\x64\mt.exe'
    foreach ($name in @('CMAKE_MAKE_PROGRAM', 'CMAKE_RC_COMPILER', 'CMAKE_MT')) {
        $missing = $cache -replace "(?m)^${name}:[^\n]*\n", ''
        $wrong = $cache -replace "(?m)^${name}:[^\n]*", "${name}:FILEPATH=C:/unexpected.exe"
        $duplicate = $cache + "${name}:FILEPATH=C:/unexpected.exe`n"
        foreach ($variant in @($missing, $wrong, $duplicate)) {
            Assert-Rejected { & $prerequisiteModule { param($text) Get-PrerequisiteCacheTools $text 'C:\Tools\ninja.exe' 'C:\SDK' } $variant } $name
        }
    }
    foreach ($variant in @($cache.Replace('22621', '26100'), $cache.Replace('/x64/', '/x86/'))) {
        Assert-Rejected { & $prerequisiteModule { param($text) Get-PrerequisiteCacheTools $text 'C:\Tools\ninja.exe' 'C:\SDK' } $variant } 'CMAKE_RC_COMPILER'
    }
}

$historicalRelative = 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-contract.v1.json'
$historicalContract = Get-Content -LiteralPath (Join-Path $repository $historicalRelative) -Raw | ConvertFrom-Json
function New-HistoricalCopy {
    $copyRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path (Join-Path $copyRoot 'experiments/m0/provisioning/qt-shell') -Force
    foreach ($relative in @($historicalRelative) + @($historicalContract.files.path)) {
        Copy-Item -LiteralPath (Join-Path $repository $relative) -Destination (Join-Path $copyRoot $relative)
    }
    return @{
        RepositoryRoot = $copyRoot
        QtRoot = Join-Path $copyRoot 'out/m0/toolchains/qt-6.11.2-msvc2022_64'
        CachePath = Join-Path $copyRoot 'unused-cache.txt'
        CompilerPath = 'C:\unused\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe'
        ToolsetVersion = '14.44.35207'
    }
}

Test-Case 'public prerequisites rejects a changed historical contract before trusting any member' {
    $arguments = New-HistoricalCopy
    [IO.File]::AppendAllText((Join-Path $arguments.RepositoryRoot $historicalRelative), ' ')
    Assert-Rejected { Get-QtShellPackagePrerequisites @arguments } 'SHA256.*qt-shell-toolchain-contract'
}

Test-Case 'public prerequisites authenticates all eight historical members before environment or tools' {
    Assert-Equal @($historicalContract.files).Count 8
    foreach ($member in $historicalContract.files) {
        $arguments = New-HistoricalCopy
        [IO.File]::AppendAllText((Join-Path $arguments.RepositoryRoot $member.path), ' ')
        Assert-Rejected { Get-QtShellPackagePrerequisites @arguments } ('size.*' + [regex]::Escape([IO.Path]::GetFileName($member.path)))
    }
}

Test-Case 'historical originals still match ac00 and all eight immutable byte pins' {
    Assert-Equal (Get-FileHash -LiteralPath (Join-Path $repository $historicalRelative) -Algorithm SHA256).Hash.ToLowerInvariant() 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952'
    foreach ($member in $historicalContract.files) {
        $path = Join-Path $repository $member.path
        Assert-Equal (Get-Item -LiteralPath $path).Length $member.size_bytes
        Assert-Equal (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() $member.sha256
    }
}

Write-Host "Prerequisites: $($state.Passed) passed; $($state.Failed) failed."
if ($state.Failed -ne 0 -or $state.Passed -eq 0) { exit 1 }
