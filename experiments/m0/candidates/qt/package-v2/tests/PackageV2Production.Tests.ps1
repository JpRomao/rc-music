#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$corePath = Join-Path $root 'M0.QtShellCompositePackage.psm1'
$failures = [Collections.Generic.List[string]]::new()
function Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; Write-Host "PASS: $Name" }
    catch { $failures.Add("FAIL: $Name`n$($_.Exception.Message)"); Write-Host $failures[-1] }
}
function Need([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Same($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', found '$Actual'." }
}
function Join-Ordinal([object[]] $Values) {
    [string[]] $copy = @($Values | ForEach-Object { [string] $_ })
    [Array]::Sort($copy, [StringComparer]::Ordinal)
    return $copy -join "`n"
}
function Private([Management.Automation.PSModuleInfo] $Module, [string] $Name, [object[]] $Arguments) {
    return & $Module {
        param($FunctionName, $FunctionArguments)
        $command = Get-Command -Name $FunctionName -CommandType Function -ErrorAction Stop
        if ($FunctionArguments.Count -ne 1 -or
            $FunctionArguments[0] -isnot [Collections.IDictionary]) {
            throw 'Private test invocation requires one named-argument dictionary.'
        }
        $namedArguments = $FunctionArguments[0]
        & $command @namedArguments
    } $Name $Arguments
}

Case 'production API requires distinct build output and scratch roots without QtRoot' {
    Need ([IO.File]::Exists($corePath)) 'Package-v2 core module is missing.'
    $module = Import-Module $corePath -Force -PassThru
    $command = $module.ExportedFunctions['Invoke-M0QtShellCompositePackage']
    foreach ($name in @('BuildRoot', 'OutputRoot', 'ScratchRoot')) {
        Need $command.Parameters.ContainsKey($name) "Production API lacks $name."
        $mandatory = @($command.Parameters[$name].Attributes | Where-Object {
                $_ -is [Management.Automation.ParameterAttribute] -and $_.Mandatory
            }).Count -eq 1
        Need $mandatory "Production API makes $name optional."
    }
    Need (-not $command.Parameters.ContainsKey('QtRoot')) 'Production API accepts arbitrary QtRoot.'
}

Case 'production contract keeps Qt tools inside the composite SDK boundary' {
    $module = Import-Module $corePath -Force -PassThru
    $contract = Get-Content -Raw -LiteralPath (Join-Path $root `
            'qt-shell-composite-package-contract.v2.json') |
        ConvertFrom-Json -Depth 100 -DateKind String
    $windeployqt = @($contract.inputs.tools | Where-Object id -CEQ 'windeployqt')[0]
    $windeployqt.path_kind = 'absolute'
    $windeployqt.path = 'C:\official-qt\bin\windeployqt.exe'
    $threw = $false
    try {
        Private $module 'Assert-M0PackageV2ProductionContract' @(@{
                Contract = $contract
            }) | Out-Null
    }
    catch { $threw = $true }
    Need $threw 'Production contract accepted windeployqt outside the composite SDK.'
}

Case 'production roots are fresh non-overlapping children of the out m0 workspace only' {
    $module = Import-Module $corePath -Force -PassThru
    $repository = Join-Path ([IO.Path]::GetTempPath()) `
        ('m0-package-roots-' + [Guid]::NewGuid().ToString('N'))
    $workspace = Join-Path $repository 'out/m0'
    $shell = Join-Path $repository 'experiments/m0/candidates/qt/shell'
    $package = Join-Path $repository 'experiments/m0/candidates/qt/package-v2'
    $composite = Join-Path $repository 'q/composite-run/sdk'
    foreach ($directory in @($workspace, $shell, $package, $composite)) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    try {
        $valid = Private $module 'Assert-M0PackageV2FreshRoots' @(@{
                RepositoryRoot = $repository
                BuildRoot = Join-Path $workspace 'package-build'
                OutputRoot = Join-Path $workspace 'package-output'
                ScratchRoot = Join-Path $workspace 'package-scratch'
            })
        Same $valid.output_root ([IO.Path]::GetFullPath(
                (Join-Path $workspace 'package-output'))) 'Canonical output root drifted.'

        foreach ($unsafeOutput in @(
                (Join-Path $shell 'package-output'),
                (Join-Path $package 'package-output'),
                (Join-Path $composite 'package-output'))) {
            $threw = $false
            try {
                Private $module 'Assert-M0PackageV2FreshRoots' @(@{
                        RepositoryRoot = $repository
                        BuildRoot = Join-Path $workspace 'package-build'
                        OutputRoot = $unsafeOutput
                        ScratchRoot = Join-Path $workspace 'package-scratch'
                    }) | Out-Null
            }
            catch { $threw = $true }
            Need $threw "Production accepted a root in an authenticated input tree: $unsafeOutput"
        }
    }
    finally { [IO.Directory]::Delete($repository, $true) }
}

Case 'pure build plan closes Qt-on core-off qmllint and exact CTest inventory commands' {
    $module = Import-Module $corePath -Force -PassThru
    $context = [pscustomobject][ordered]@{
        repository_root = 'C:\repo with spaces'
        source_root = 'C:\repo with spaces\experiments\m0\candidates\qt\shell'
        composite_sdk_root = 'C:\repo with spaces\q\run\sdk'
        catch2_root = 'C:\repo with spaces\out\m0\toolchains\Catch2-3.15.3'
        qt_on_build_root = 'C:\repo with spaces\out\build Qt ON'
        core_off_build_root = 'C:\repo with spaces\out\build core OFF'
        tools = [pscustomobject][ordered]@{
            cmake = 'C:\tools\cmake.exe'; ctest = 'C:\tools\ctest.exe'; ninja = 'C:\tools\ninja.exe'
            cl = 'C:\tools\cl.exe'; link = 'C:\tools\link.exe'
            rc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe'
            mt = 'C:\tools\mt.exe'
            dumpbin = 'C:\tools\dumpbin.exe'
        }
    }
    $plan = Private $module 'New-M0PackageV2BuildPlan' @(@{ Context = $context })
    Same ($plan.PSObject.Properties.Name -join ',') 'schema_version,commands' 'Build plan shape drifted.'
    Same $plan.schema_version ([long]2) 'Build plan schema drifted.'
    $expectedIds = @('qt-on-configure', 'qt-on-build', 'qt-on-qmllint', 'qt-on-ctest-list',
        'qt-on-ctest', 'core-off-configure', 'core-off-build', 'core-off-ctest-list',
        'core-off-ctest', 'core-off-import-audit')
    Same (@($plan.commands.id) -join ',') ($expectedIds -join ',') 'Build command order drifted.'
    foreach ($row in @($plan.commands)) {
        Same ($row.PSObject.Properties.Name -join ',') `
            'id,file_path,arguments,working_directory,timeout_seconds,expected_test_count' `
            "Build command $($row.id) shape drifted."
        Need ($row.arguments -is [object[]]) "Build command $($row.id) arguments are not an array."
        Need ($row.timeout_seconds -is [long] -and $row.timeout_seconds -gt 0) `
            "Build command $($row.id) timeout is invalid."
    }
    $qtConfigure = @($plan.commands | Where-Object id -CEQ 'qt-on-configure')[0]
    $coreConfigure = @($plan.commands | Where-Object id -CEQ 'core-off-configure')[0]
    $qtArguments = @($qtConfigure.arguments) -join "`n"
    $coreArguments = @($coreConfigure.arguments) -join "`n"
    foreach ($needle in @('-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
            '-DM0_QT_SHELL_SUPPORT=ON', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
            '-DM0_CATCH2_SOURCE_DIR:PATH=C:/repo with spaces/out/m0/toolchains/Catch2-3.15.3',
            '-DCMAKE_MAKE_PROGRAM:FILEPATH=C:/tools/ninja.exe',
            '-DCMAKE_CXX_COMPILER:FILEPATH=C:/tools/cl.exe',
            '-DCMAKE_LINKER:FILEPATH=C:/tools/link.exe',
            '-DCMAKE_RC_COMPILER:FILEPATH=C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe',
            '-DCMAKE_MT:FILEPATH=C:/tools/mt.exe',
            '-DCMAKE_PREFIX_PATH:PATH=C:/repo with spaces/q/run/sdk',
            '-DQt6_DIR:PATH=C:/repo with spaces/q/run/sdk/lib/cmake/Qt6')) {
        Need $qtArguments.Contains($needle, [StringComparison]::Ordinal) "Qt-on configure lacks '$needle'."
    }
    $cmakePathTypes = [ordered]@{
        M0_CATCH2_SOURCE_DIR = 'PATH'
        CMAKE_MAKE_PROGRAM = 'FILEPATH'
        CMAKE_CXX_COMPILER = 'FILEPATH'
        CMAKE_LINKER = 'FILEPATH'
        CMAKE_RC_COMPILER = 'FILEPATH'
        CMAKE_MT = 'FILEPATH'
        CMAKE_PREFIX_PATH = 'PATH'
        Qt6_DIR = 'PATH'
    }
    foreach ($configure in @($qtConfigure, $coreConfigure)) {
        foreach ($definition in $cmakePathTypes.GetEnumerator()) {
            $name = [string]$definition.Key
            $type = [string]$definition.Value
            $allNameMatches = @($configure.arguments | Where-Object {
                    $_.StartsWith("-D${name}=", [StringComparison]::Ordinal) -or
                    $_.StartsWith("-D${name}:", [StringComparison]::Ordinal)
                })
            $matches = @($configure.arguments | Where-Object {
                    $_.StartsWith("-D${name}:$type=", [StringComparison]::Ordinal)
                })
            if ($configure.id -ceq 'core-off-configure' -and
                $name -in @('CMAKE_PREFIX_PATH', 'Qt6_DIR')) {
                Same $allNameMatches.Count 0 "Core-off configure received '$name'."
                continue
            }
            Same $allNameMatches.Count 1 "Configure cache path '$name' is not singular."
            Same $matches.Count 1 "Configure cache path '$name' is not singular."
            Need (-not $matches[0].Contains('\', [StringComparison]::Ordinal)) `
                "Configure cache path '$name' is not normalized: '$($matches[0])'."
        }
    }
    Need $coreArguments.Contains('-DM0_QT_SHELL_SUPPORT=OFF', [StringComparison]::Ordinal) `
        'Core-off configure enables Qt.'
    Need (-not $coreArguments.Contains('C:/repo with spaces/q/run/sdk', [StringComparison]::OrdinalIgnoreCase)) `
        'Core-off configure receives a Qt SDK path.'
    Same (@($plan.commands | Where-Object id -CEQ 'qt-on-ctest-list')[0].expected_test_count) ([long]65) `
        'Qt-on expected CTest count drifted.'
    Same (@($plan.commands | Where-Object id -CEQ 'core-off-ctest-list')[0].expected_test_count) ([long]55) `
        'Core-off expected CTest count drifted.'
    Need ((@($plan.commands | Where-Object id -CEQ 'qt-on-qmllint')[0].arguments -join ' ').Contains('all_qmllint')) `
        'Qt-on plan does not execute the generated qmllint target.'

    Same (Private $module 'ConvertTo-M0PackageV2CMakePath' @(@{
                Value = '\\server\share\path with spaces'
            })) `
        '//server/share/path with spaces' 'CMake UNC path normalization drifted.'
    foreach ($unsafe in @('C:\repo;injected', '\\?\C:\repo', '\\.\C:\repo',
            '//?/C:/repo', '//./C:/repo')) {
        $threw = $false
        try {
            Private $module 'ConvertTo-M0PackageV2CMakePath' @(@{
                    Value = $unsafe
                }) | Out-Null
        }
        catch { $threw = $true }
        Need $threw "CMake cache path accepted unsafe syntax: '$unsafe'."
    }
}

Case 'CTest inventory parser accepts exact count and rejects missing skipped or disabled tests' {
    $module = Import-Module $corePath -Force -PassThru
    $tests = for ($index = 1; $index -le 65; $index++) {
        [pscustomobject]@{ name = ('case-{0:d2}' -f $index); properties = @() }
    }
    $json = [pscustomobject]@{ kind = 'ctestInfo'; version = [pscustomobject]@{ major = 1; minor = 0 }; tests = $tests } |
        ConvertTo-Json -Depth 10 -Compress
    $result = Private $module 'Assert-M0PackageV2CTestInventory' @(@{
            Json = $json; ExpectedCount = [long]65; Context = 'Qt-on CTest'
        })
    Same $result.test_count ([long]65) 'CTest count was not preserved.'
    foreach ($bad in @(
            [pscustomobject]@{ json = ([pscustomobject]@{ kind = 'ctestInfo'; version = [pscustomobject]@{ major = 1; minor = 0 }; tests = @($tests | Select-Object -First 64) } | ConvertTo-Json -Depth 10 -Compress); reason = 'missing' },
            [pscustomobject]@{ json = ([pscustomobject]@{ kind = 'ctestInfo'; version = [pscustomobject]@{ major = 1; minor = 0 }; tests = @($tests[0..63]) + @([pscustomobject]@{ name = 'case-disabled'; properties = @([pscustomobject]@{ name = 'DISABLED'; value = $true }) }) } | ConvertTo-Json -Depth 10 -Compress); reason = 'disabled' }
        )) {
        $threw = $false
        try {
            Private $module 'Assert-M0PackageV2CTestInventory' @(@{
                    Json = $bad.json; ExpectedCount = [long]65; Context = 'Qt-on CTest'
                }) | Out-Null
        }
        catch { $threw = $true }
        Need $threw "CTest parser accepted $($bad.reason) coverage."
    }
}

Case 'core-off import audit rejects any Qt dependency and requires the core test executable' {
    $module = Import-Module $corePath -Force -PassThru
    $valid = @([pscustomobject]@{ relative_path = 'm0_qt_core_tests.exe'; machine = 'x64'; imports = @('KERNEL32.dll', 'VCRUNTIME140.dll') })
    $result = Private $module 'Assert-M0PackageV2CoreOffImports' @(@{ Records = $valid })
    Same $result.core_executable 'm0_qt_core_tests.exe' 'Core import audit lost its subject.'
    foreach ($records in @(
            @([pscustomobject]@{ relative_path = 'other.exe'; machine = 'x64'; imports = @('KERNEL32.dll') }),
            @([pscustomobject]@{ relative_path = 'm0_qt_core_tests.exe'; machine = 'x64'; imports = @('Qt6Core.dll') }),
            @([pscustomobject]@{ relative_path = 'm0_qt_core_tests.exe'; machine = 'x86'; imports = @('KERNEL32.dll') })
        )) {
        $threw = $false
        try { Private $module 'Assert-M0PackageV2CoreOffImports' @(@{ Records = $records }) | Out-Null }
        catch { $threw = $true }
        Need $threw 'Core-off audit accepted a missing, Qt-linked, or non-x64 subject.'
    }
}

Case 'deployment plan permits only composite SDK and generated application QML sources' {
    $module = Import-Module $corePath -Force -PassThru
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('m0-package-v2-deploy-' + [guid]::NewGuid().ToString('N'))
    $sdk = Join-Path $temp 'composite sdk'
    $qml = Join-Path $temp 'build qml/MusicPlayer/M0'
    $destination = Join-Path $temp 'package/app'
    $qmlDestination = Join-Path $destination 'qml/MusicPlayer/M0'
    $outside = Join-Path $temp 'official-sdk'
    foreach ($directory in @($sdk, $qml, $destination, $qmlDestination, $outside)) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    try {
        $qtSource = Join-Path $sdk 'Qt6Core.dll'
        $qmlSource = Join-Path $qml 'Main.qml'
        $outsideSource = Join-Path $outside 'Qt6Gui.dll'
        [IO.File]::WriteAllText($qtSource, 'qt')
        [IO.File]::WriteAllText($qmlSource, 'qml')
        [IO.File]::WriteAllText($outsideSource, 'outside')
        $files = @(
            [pscustomobject]@{ source = $qtSource; target = $destination },
            [pscustomobject]@{ source = $qmlSource; target = $qmlDestination }
        )
        $plan = Private $module 'ConvertTo-M0PackageV2DeploymentPlan' @(@{
                Files = $files; CompositeSdkRoot = $sdk; ApplicationQmlRoot = $qml; DestinationRoot = $destination
            })
        Same (@($plan.files).Count) 2 'Deployment plan lost a file.'
        Same (@($plan.files.relative_path) -join ',') `
            'Qt6Core.dll,qml/MusicPlayer/M0/Main.qml' `
            'Deployment directory targets did not append source basenames.'
        Same (Join-Ordinal @($plan.files.source_kind)) (Join-Ordinal @('composite-sdk', 'application-qml')) `
            'Deployment source classification drifted.'
        $threw = $false
        try {
            Private $module 'ConvertTo-M0PackageV2DeploymentPlan' @(@{
                Files = @([pscustomobject]@{ source = $outsideSource; target = (Join-Path $destination 'Qt6Gui.dll') })
                    CompositeSdkRoot = $sdk; ApplicationQmlRoot = $qml; DestinationRoot = $destination
                }) | Out-Null
        }
        catch { $threw = $true }
        Need $threw 'Deployment accepted an official/external SDK source.'
    }
    finally { if ([IO.Directory]::Exists($temp)) { [IO.Directory]::Delete($temp, $true) } }
}

Case 'dry-run and actual deployment plans must be byte-for-byte canonical equals' {
    $module = Import-Module $corePath -Force -PassThru
    $left = [pscustomobject][ordered]@{
        schema_version = [long]2
        files = @([pscustomobject][ordered]@{
                relative_path = 'Qt6Core.dll'; source_kind = 'composite-sdk';
                source_relative_path = 'bin/Qt6Core.dll'; size_bytes = [long]2; sha256 = 'a' * 64
            })
    }
    $right = $left | ConvertTo-Json -Depth 10 | ConvertFrom-Json -Depth 10 -DateKind String
    Private $module 'Assert-M0PackageV2DeploymentPlansEqual' @(@{ DryRun = $left; Actual = $right }) | Out-Null
    $right.files[0].sha256 = 'b' * 64
    $threw = $false
    try { Private $module 'Assert-M0PackageV2DeploymentPlansEqual' @(@{ DryRun = $left; Actual = $right }) | Out-Null }
    catch { $threw = $true }
    Need $threw 'Deployment accepted dry-run/actual byte drift.'
}

if ($failures.Count) { throw "Package v2 production Reds: $($failures.Count) failure(s)." }
