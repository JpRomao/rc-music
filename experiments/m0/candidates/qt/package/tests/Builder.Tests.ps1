#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../QtShellPackageBuilder.psm1') -Force

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$testRoot = Join-Path $repository ('out/m0/test-runs/qt-package/builder-' + [Guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
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

$qtRoot = Join-Path $testRoot 'Qt SDK'
$packageRoot = Join-Path $testRoot 'pacote acentuação/app'
$null = New-Item -ItemType Directory -Path (Join-Path $qtRoot 'bin')
$null = New-Item -ItemType Directory -Path (Join-Path $qtRoot 'qml/QtQuick')
[IO.File]::WriteAllText((Join-Path $qtRoot 'bin/Qt6Core.dll'), 'synthetic DLL bytes')
[IO.File]::WriteAllText((Join-Path $qtRoot 'qml/QtQuick/qmldir'), 'synthetic QML module')
$mapping = @{ files = @(
    @{ source = Join-Path $qtRoot 'bin/Qt6Core.dll'; target = $packageRoot },
    @{ source = Join-Path $qtRoot 'qml/QtQuick/qmldir'; target = Join-Path $packageRoot 'qml/QtQuick' }
) }

Test-Case 'deployment maps actual files to portable, hash-identified paths' {
    $plan = ConvertTo-QtShellDeploymentPlan -DeploymentJson $mapping -QtRoot $qtRoot -PackageRoot $packageRoot
    Assert-Equal $plan.files.Count 2
    Assert-Equal $plan.files[0].relative_path 'Qt6Core.dll'
    Assert-Equal $plan.files[0].source_path 'bin/Qt6Core.dll'
    Assert-Equal $plan.files[0].sha256 (Get-FileHash -LiteralPath $mapping.files[0].source -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Equal $plan.files[1].relative_path 'qml/QtQuick/qmldir'
}
Test-Case 'deployment rejects a source outside the SDK' {
    $bad = @{ files = @(@{ source = $PSCommandPath; target = $packageRoot }) }
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan $bad $qtRoot $packageRoot } 'outside'
}
Test-Case 'deployment rejects an escaping target and duplicate destination' {
    $bad = @{ files = @(@{ source = $mapping.files[0].source; target = Join-Path $packageRoot '..' }) }
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan $bad $qtRoot $packageRoot } 'outside|traversal'
    $bad = @{ files = @($mapping.files[0], $mapping.files[0]) }
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan $bad $qtRoot $packageRoot } 'duplicate'
}
Test-Case 'deployment rejects executable tools and an empty plan' {
    $tool = Join-Path $qtRoot 'bin/tool.exe'
    [IO.File]::WriteAllText($tool, 'tool')
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan @{ files = @(@{ source = $tool; target = $packageRoot }) } $qtRoot $packageRoot } 'payload'
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan @{ files = @() } $qtRoot $packageRoot } 'empty'
}
Test-Case 'deployment includes the explicitly scoped application QML module without accepting other build files' {
    $applicationQml = Join-Path $testRoot 'build/qml/MusicPlayer/M0'
    $null = New-Item -ItemType Directory -Path $applicationQml
    $qmlFile = Join-Path $applicationQml 'Main.qml'
    [IO.File]::WriteAllText($qmlFile, 'Item {}')
    $qmlMapping = @{ files = @(@{ source = $qmlFile; target = Join-Path $packageRoot 'qml/MusicPlayer/M0' }) }
    $plan = ConvertTo-QtShellDeploymentPlan $qmlMapping $qtRoot $packageRoot -ApplicationQmlRoot $applicationQml
    Assert-Equal $plan.files[0].origin 'application-qml'
    Assert-Equal $plan.files[0].source_path 'Main.qml'
    Assert-Equal $plan.files[0].relative_path 'qml/MusicPlayer/M0/Main.qml'
    $qmlMapping.files[0].source = Join-Path $testRoot 'build/other.qml'
    Assert-Rejected { ConvertTo-QtShellDeploymentPlan $qmlMapping $qtRoot $packageRoot -ApplicationQmlRoot $applicationQml } 'outside'
}
Test-Case 'scoped path accepts a new Unicode child but rejects aliases and traversal' {
    Assert-Equal (Resolve-QtShellPackageChildPath $testRoot 'ação com espaços') (Join-Path $testRoot 'ação com espaços')
    foreach ($name in @('../escape', 'one/../two', 'NUL', 'CON.txt', 'CONIN$', 'CONOUT$.txt', 'COM1 .foo', 'file:stream', 'trailing.', 'trailing ', 'a//b')) {
        Assert-Rejected { Resolve-QtShellPackageChildPath $testRoot $name } 'path|name|outside|traversal|reserved'
    }
}
Test-Case 'destination creation is scoped and never overwrites' {
    $path = New-QtShellPackageDirectory -Parent $testRoot -Name 'fresh'
    Assert-Equal (Test-Path -LiteralPath $path -PathType Container) $true
    Assert-Rejected { New-QtShellPackageDirectory -Parent $testRoot -Name 'fresh' } 'exist'
    Assert-Rejected { New-QtShellPackageDirectory -Parent $testRoot -Name '../elsewhere' } 'name|path|traversal'
}
Test-Case 'scoped path rejects a reparse parent without touching its target' {
    $outside = Join-Path $testRoot 'outside'
    $null = New-Item -ItemType Directory -Path $outside
    $junction = Join-Path $testRoot 'junction'
    $null = New-Item -ItemType Junction -Path $junction -Target $outside
    Assert-Rejected { Resolve-QtShellPackageChildPath $testRoot 'junction/child' } 'reparse'
    Assert-Equal (Test-Path -LiteralPath (Join-Path $outside 'child')) $false
}

$sourceRoot = Join-Path $testRoot 'source'
$cache = @"
CMAKE_BUILD_TYPE:STRING=Release
BUILD_TESTING:BOOL=ON
M0_QT_SHELL_SUPPORT:BOOL=ON
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_HOME_DIRECTORY:INTERNAL=$sourceRoot
Qt6_DIR:PATH=$qtRoot/lib/cmake/Qt6
"@
Test-Case 'cache contract requires Release, Qt, test hooks and the expected sources' {
    $result = Test-QtShellReleaseCache -CacheText $cache -SourceRoot $sourceRoot -QtRoot $qtRoot
    Assert-Equal $result.configuration 'Release'
    Assert-Equal $result.test_hooks $true
    foreach ($bad in @($cache.Replace('=Release', '=Debug'), $cache.Replace('BUILD_TESTING:BOOL=ON', 'BUILD_TESTING:BOOL=OFF'), $cache.Replace('=Ninja', '=Other'), $cache.Replace($sourceRoot, "$sourceRoot-other"))) {
        Assert-Rejected { Test-QtShellReleaseCache $bad $sourceRoot $qtRoot } 'cache|Release|BUILD_TESTING|Ninja|source'
    }
}
Test-Case 'cache rejects duplicate keys and a substituted Qt SDK' {
    Assert-Rejected { Test-QtShellReleaseCache ($cache + "`nBUILD_TESTING:BOOL=ON") $sourceRoot $qtRoot } 'duplicate'
    Assert-Rejected { Test-QtShellReleaseCache ($cache.Replace($qtRoot, "$qtRoot-other")) $sourceRoot $qtRoot } 'Qt|cache'
}
Test-Case 'compiler provenance parses Windows CRLF and rejects version drift' {
    $compilerText = "set(CMAKE_CXX_COMPILER `"C:/tools/cl.exe`")`r`nset(CMAKE_CXX_COMPILER_VERSION `"19.43.34808.0`")`r`n"
    $qtText = "QT_VERSION = 6.11.2`r`nQT_MSVC_MAJOR_VERSION = 19`r`nQT_MSVC_MINOR_VERSION = 44`r`nQT_MSVC_PATCH_VERSION = 35227`r`n"
    foreach ($newlineCompiler in @($compilerText, $compilerText.Replace("`r`n", "`n"))) {
        $identity = Get-QtShellCompilerIdentity $newlineCompiler $qtText
        Assert-Equal $identity.shell_msvc '19.43.34808.0'
        Assert-Equal $identity.qt_build_msvc '19.44.35227'
        Assert-Equal $identity.compiler 'C:/tools/cl.exe'
    }
    Assert-Rejected { Get-QtShellCompilerIdentity ($compilerText.Replace('19.43', '19.42')) $qtText } 'Compiler drift'
    Assert-Rejected { Get-QtShellCompilerIdentity $compilerText ($qtText.Replace('= 44', '= 43')) } 'Qt.*drift'
}
Test-Case 'PE report inventories imports and delay imports, and enforces x64' {
    $report = "FILE HEADER VALUES`n            8664 machine (x64)`n    Image has the following dependencies:`n        Qt6Core.dll`n        KERNEL32.dll`n    Image has the following delay load dependencies:`n        USER32.dll`n        Qt6Core.dll`n"
    $result = ConvertFrom-QtShellPeReport $report
    Assert-Equal $result.machine 'x64'
    Assert-Equal ($result.imports -join ',') 'KERNEL32.dll,Qt6Core.dll,USER32.dll'
    Assert-Rejected { ConvertFrom-QtShellPeReport ($report.Replace('8664 machine (x64)', '14C machine (x86)')) } 'x64'
    Assert-Rejected { ConvertFrom-QtShellPeReport 'not a PE report' } 'x64|report'
}

$runtimeFiles = @(
    'm0_qt_shell.exe', 'qt.conf', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'Qt6Qml.dll', 'Qt6Quick.dll', 'Qt6QuickControls2.dll', 'Qt6QuickLayouts.dll', 'Qt6Svg.dll',
    'plugins/platforms/qwindows.dll', 'plugins/platforms/qoffscreen.dll',
    'plugins/iconengines/qsvgicon.dll', 'plugins/imageformats/qsvg.dll',
    'qml/QtQuick/qmldir', 'qml/QtQuick/Controls/qmldir', 'qml/QtQuick/Controls/Basic/qmldir',
    'qml/QtQuick/Templates/qmldir', 'qml/QtQuick/Layouts/qmldir'
)
Test-Case 'runtime policy requires the actual shell, Qt Widgets, Basic, layout and SVG paths' {
    Assert-QtShellRuntimeFiles -RelativePaths $runtimeFiles
    foreach ($file in $runtimeFiles) {
        Assert-Rejected { Assert-QtShellRuntimeFiles -RelativePaths @($runtimeFiles | Where-Object { $_ -cne $file }) } 'missing'
    }
}
Test-Case 'runtime policy rejects test DLLs, tools, audio and a copied compiler runtime' {
    foreach ($extra in @('Qt6Test.dll', 'm0_qt_quick_tests.exe', 'vc_redist.x64.exe', 'vcruntime140.dll', 'msvcp140.dll', 'track.mp3', 'plugins/qmltooling/qmldbg_tcp.dll', 'Qt6Cored.dll')) {
        Assert-Rejected { Assert-QtShellRuntimeFiles -RelativePaths @($runtimeFiles + $extra) } 'forbidden'
    }
}

Write-Host "Builder: $($state.Passed) passed, $($state.Failed) failed. Fixtures retained at $testRoot"
if ($state.Failed -ne 0) { exit 1 }
