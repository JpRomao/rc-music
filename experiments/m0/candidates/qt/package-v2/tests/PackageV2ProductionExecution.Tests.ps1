#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$corePath = Join-Path $root 'M0.QtShellCompositePackage.psm1'
$module = Import-Module $corePath -Force -PassThru
$failures = [Collections.Generic.List[string]]::new()

function Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; Write-Host "PASS: $Name" }
    catch { $failures.Add("FAIL: $Name`n$($_.Exception.Message)"); Write-Host $failures[-1] }
}
function Need([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Same($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', found '$Actual'." }
}
function Reject([scriptblock] $Body, [string] $Pattern) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Wrong rejection: $($_.Exception.Message)" }
        return
    }
    throw 'Expected rejection.'
}
function Private([string] $Name, [hashtable] $Arguments) {
    return & $module {
        param($FunctionName, $NamedArguments)
        $command = Get-Command -Name $FunctionName -CommandType Function -ErrorAction Stop
        & $command @NamedArguments
    } $Name $Arguments
}

function New-TarFixture([object[]] $Entries) {
    $path = Join-Path ([IO.Path]::GetTempPath()) ('m0-catch2-' + [Guid]::NewGuid().ToString('N') + '.tar.gz')
    $file = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $gzip = [IO.Compression.GZipStream]::new($file, [IO.Compression.CompressionMode]::Compress, $false)
    $writer = [System.Formats.Tar.TarWriter]::new($gzip, $false)
    try {
        $rootEntry = [System.Formats.Tar.PaxTarEntry]::new(
            [System.Formats.Tar.TarEntryType]::Directory, 'Catch2-3.15.3/')
        $writer.WriteEntry($rootEntry)
        foreach ($spec in $Entries) {
            $entry = [System.Formats.Tar.PaxTarEntry]::new($spec.type, $spec.name)
            if ($spec.type -eq [System.Formats.Tar.TarEntryType]::RegularFile) {
                $bytes = [Text.Encoding]::UTF8.GetBytes([string] $spec.text)
                $entry.DataStream = [IO.MemoryStream]::new($bytes, $false)
            }
            elseif ($spec.PSObject.Properties['link_name']) {
                $entry.LinkName = [string] $spec.link_name
            }
            $writer.WriteEntry($entry)
            if ($null -ne $entry.DataStream) { $entry.DataStream.Dispose() }
        }
    }
    finally { $writer.Dispose(); $gzip.Dispose(); $file.Dispose() }
    return $path
}

Case 'production operations retain the authenticated module scope' {
    $buildRoot = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-operation-scope-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($buildRoot) | Out-Null
    try {
        $operations = Private 'New-M0PackageV2ProductionOperations' @{}
        foreach ($name in @(
                'verify_composite', 'build_candidate', 'publish',
                'verify_package')) {
            $operation = $operations.$name
            Need ($null -ne $operation.Module) `
                "Production operation '$name' lost its module session state."
            Same ([IO.Path]::GetFullPath($operation.Module.Path)) $corePath `
                "Production operation '$name' escaped the authenticated module."
        }
        Reject {
            & $operations.build_candidate ([pscustomobject]@{
                    build_root = $buildRoot
                })
        } '^BuildRoot is no longer fresh before candidate construction\.$'
    }
    finally {
        if ([IO.Directory]::Exists($buildRoot)) {
            [IO.Directory]::Delete($buildRoot, $true)
        }
    }
}

Case 'public production entry accepts initially empty retained-lease sinks' {
    $repository = [IO.Path]::GetFullPath((Join-Path $root '../../../../..'))
    $contractPath = Join-Path $root 'qt-shell-composite-package-contract.v2.json'
    $runParent = Join-Path $repository (
        'out/m0/test-runs/package-v2-empty-leases-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($runParent) | Out-Null
    try {
        Reject {
            Invoke-M0QtShellCompositePackage `
                -ContractPath $contractPath `
                -ExpectedContractSha256 ('0' * 64) `
                -CompositeReceiptPath $contractPath `
                -ExpectedCompositeReceiptSha256 ('1' * 64) `
                -ExpectedQtBaseReceiptSha256 ('2' * 64) `
                -RepositoryRoot $repository `
                -BuildRoot (Join-Path $runParent 'build') `
                -OutputRoot (Join-Path $runParent 'package') `
                -ScratchRoot (Join-Path $runParent 'scratch')
        } 'production contract SHA-256 drifted'
    }
    finally {
        if ([IO.Directory]::Exists($runParent)) {
            [IO.Directory]::Delete($runParent, $true)
        }
    }
}

Case 'pinned-file authentication accepts initially empty retained-lease sinks' {
    $leases = [Collections.Generic.List[object]]::new()
    $files = [Collections.Generic.List[object]]::new()
    $item = Get-Item -LiteralPath $corePath -Force
    $pin = [pscustomobject][ordered]@{
        relative_path = 'M0.QtShellCompositePackage.psm1'
        size_bytes = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $corePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    try {
        $lease = Private 'Open-M0PackageV2PinnedFile' @{
            LiteralPath = $corePath
            Pin = $pin
            BoundaryRoot = $root
            Context = 'Empty lease-sink regression fixture'
            Leases = $leases
            Files = $files
        }
        Need ($leases.Count -eq 1 -and $files.Count -eq 1) `
            'Pinned-file authentication did not populate both initially empty sinks.'
        Same $lease.sha256 $pin.sha256 'Pinned-file authentication changed the file hash.'
    }
    finally {
        Private 'Close-M0PackageV2Leases' @{ Leases = $leases } | Out-Null
    }
}

Case 'absolute-tool pins retain legitimate hardlink counts and detect count drift' {
    if (-not $IsWindows) { return }
    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-hardlink-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
    $primary = Join-Path $fixtureRoot 'tool.exe'
    $secondLink = Join-Path $fixtureRoot 'tool-alias.exe'
    $thirdLink = Join-Path $fixtureRoot 'tool-late-alias.exe'
    [IO.File]::WriteAllBytes($primary, [byte[]](1, 2, 3, 4))
    New-Item -ItemType HardLink -Path $secondLink -Target $primary | Out-Null
    $item = Get-Item -LiteralPath $primary -Force
    $pin = [pscustomobject][ordered]@{
        relative_path = 'tool.exe'
        size_bytes = [long]$item.Length
        sha256 = (Get-FileHash -LiteralPath $primary -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $defaultLeases = [Collections.Generic.List[object]]::new()
    $defaultFiles = [Collections.Generic.List[object]]::new()
    $retainedLeases = [Collections.Generic.List[object]]::new()
    $retainedFiles = [Collections.Generic.List[object]]::new()
    try {
        Reject {
            Private 'Open-M0PackageV2PinnedFile' @{
                LiteralPath = $primary
                Pin = $pin
                BoundaryRoot = $fixtureRoot
                Context = 'Recipe hardlink regression fixture'
                Leases = $defaultLeases
                Files = $defaultFiles
            }
        } 'hardlink count is 2|exactly one'
        Need ($defaultLeases.Count -eq 0 -and $defaultFiles.Count -eq 0) `
            'Default single-link rejection retained a partial lease.'

        $lease = Private 'Open-M0PackageV2PinnedFile' @{
            LiteralPath = $primary
            Pin = $pin
            BoundaryRoot = $fixtureRoot
            Context = 'Absolute tool hardlink regression fixture'
            Leases = $retainedLeases
            Files = $retainedFiles
            AllowMultipleHardlinksForAbsoluteTool = $true
        }
        Same $lease.link_count ([long]2) 'Absolute-tool link count was not retained.'
        New-Item -ItemType HardLink -Path $thirdLink -Target $primary | Out-Null
        Reject {
            Private 'Assert-M0PackageV2FileLeasesUnchanged' @{
                Files = $retainedFiles
                Context = 'Absolute tool hardlink resweep'
            }
        } 'hardlink|drift'
    }
    finally {
        Private 'Close-M0PackageV2Leases' @{ Leases = $defaultLeases } | Out-Null
        Private 'Close-M0PackageV2Leases' @{ Leases = $retainedLeases } | Out-Null
        if ([IO.Directory]::Exists($fixtureRoot)) {
            [IO.Directory]::Delete($fixtureRoot, $true)
        }
    }
}

Case 'ProcessStartInfo uses ArgumentList and an exact explicit environment' {
    $environment = [ordered]@{
        SystemRoot = 'C:\Windows'
        TEMP = 'C:\private temp'
        PATH = 'C:\tools;C:\Windows\System32'
        VSLANG = '1033'
    }
    $info = Private 'New-M0PackageV2ProcessStartInfo' @{
        FilePath = 'C:\tools\cmake.exe'
        Arguments = [string[]]@('-S', 'C:\source with spaces', '-B', 'C:\build')
        WorkingDirectory = 'C:\repo'
        Environment = $environment
    }
    Same $info.FileName 'C:\tools\cmake.exe' 'Executable path drifted.'
    Same ($info.ArgumentList -join '|') '-S|C:\source with spaces|-B|C:\build' `
        'Arguments were concatenated or re-quoted.'
    Same $info.WorkingDirectory 'C:\repo' 'Working directory drifted.'
    Need (-not $info.UseShellExecute -and $info.RedirectStandardOutput -and
        $info.RedirectStandardError -and $info.CreateNoWindow) 'Process isolation flags drifted.'
    Same $info.Environment.Count $environment.Count 'Inherited environment escaped.'
    foreach ($entry in $environment.GetEnumerator()) {
        Same $info.Environment[$entry.Key] $entry.Value "Environment '$($entry.Key)' drifted."
    }
}

Case 'process capture and timeout termination remain physically bounded' {
    $pwsh = (Get-Process -Id $PID).Path
    $environment = [ordered]@{
        SystemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        PATH = [Environment]::SystemDirectory
        TEMP = [IO.Path]::GetTempPath()
        TMP = [IO.Path]::GetTempPath()
    }
    $captureCommand = [pscustomobject]@{
        id = 'bounded-capture-probe'
        file_path = $pwsh
        arguments = [object[]]@(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-Command',
            '[Console]::Out.Write("x" * 4096)')
        working_directory = $root
        timeout_seconds = [long]10
    }
    $captureMessage = ''
    try {
        Private 'Invoke-M0PackageV2BoundedProcess' @{
            Command = $captureCommand
            Environment = $environment
            CaptureLimitBytes = [long]128
            TerminationTimeoutMilliseconds = [long]3000
        } | Out-Null
    }
    catch { $captureMessage = $_.Exception.Message }
    Need ($captureMessage -match 'exceeded the 128-byte capture boundary') `
        "Process capture was not effectively bounded: $captureMessage"

    $timeoutCommand = [pscustomobject]@{
        id = 'bounded-timeout-probe'
        file_path = $pwsh
        arguments = [object[]]@(
            '-NoLogo', '-NoProfile', '-NonInteractive', '-Command',
            'Start-Sleep -Seconds 30')
        working_directory = $root
        timeout_seconds = [long]1
    }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $timeoutResult = Private 'Invoke-M0PackageV2BoundedProcess' @{
        Command = $timeoutCommand
        Environment = $environment
        CaptureLimitBytes = [long]1024
        TerminationTimeoutMilliseconds = [long]3000
    }
    $timer.Stop()
    Need ($timeoutResult.timed_out -eq $true -and $timeoutResult.exit_code -eq -1) `
        'Timed-out process did not return the closed timeout result.'
    Need ($timer.ElapsedMilliseconds -lt 7000) `
        'Timed-out process termination exceeded its bounded grace period.'

    $preflightRoot = Join-Path ([IO.Path]::GetTempPath()) `
        ('m0-runner-preflight-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($preflightRoot) | Out-Null
    $startedMarker = Join-Path $preflightRoot 'started.txt'
    try {
        $invalidTimeoutCommand = [pscustomobject]@{
            id = 'invalid-timeout-probe'
            file_path = $pwsh
            arguments = [object[]]@(
                '-NoLogo', '-NoProfile', '-NonInteractive', '-Command',
                "[IO.File]::WriteAllText('$($startedMarker.Replace("'", "''"))', 'started')")
            working_directory = $root
            timeout_seconds = [long]([Math]::Floor([int]::MaxValue / 1000) + 1)
        }
        Reject {
            Private 'Invoke-M0PackageV2BoundedProcess' @{
                Command = $invalidTimeoutCommand
                Environment = $environment
            }
        } 'timeout.+boundary'
        Start-Sleep -Milliseconds 500
        Need (-not [IO.File]::Exists($startedMarker)) `
            'Runner started a process before validating its timeout boundary.'
    }
    finally { [IO.Directory]::Delete($preflightRoot, $true) }
}

Case 'build command executor enforces exact CTest inventories and core-off imports' {
    $commands = @(
        [pscustomobject][ordered]@{
            id = 'qt-on-ctest-list'; file_path = 'C:\ctest.exe'; arguments = [object[]]@('--show-only=json-v1')
            working_directory = 'C:\repo'; timeout_seconds = [long]30; expected_test_count = [long]65
        },
        [pscustomobject][ordered]@{
            id = 'core-off-import-audit'; file_path = 'C:\dumpbin.exe'; arguments = [object[]]@('/dependents')
            working_directory = 'C:\repo'; timeout_seconds = [long]30; expected_test_count = $null
        }
    )
    $tests = for ($index = 1; $index -le 65; $index++) {
        [pscustomobject]@{
            name = "case-$index"
            properties = @([pscustomobject]@{
                    name = 'SKIP_RETURN_CODE'
                    value = [long]4
                })
        }
    }
    $inventory = [pscustomobject]@{
        kind = 'ctestInfo'; version = [pscustomobject]@{ major = [long]1; minor = [long]0 }; tests = $tests
    } | ConvertTo-Json -Depth 10 -Compress
    $events = [Collections.Generic.List[string]]::new()
    $runner = {
        param($Command, $Environment)
        $events.Add($Command.id)
        if ($Command.id -ceq 'qt-on-ctest-list') {
            return [pscustomobject]@{ exit_code = [long]0; stdout = $inventory; stderr = ''; timed_out = $false }
        }
        return [pscustomobject]@{
            exit_code = [long]0
            stdout = "8664 machine (x64)`nImage has the following dependencies:`n KERNEL32.dll`n VCRUNTIME140.dll"
            stderr = ''
            timed_out = $false
        }
    }.GetNewClosure()
    $result = Private 'Invoke-M0PackageV2BuildCommands' @{
        Plan = [pscustomobject]@{ schema_version = [long]2; commands = $commands }
        Environment = [ordered]@{ VSLANG = '1033' }
        RunAction = $runner
    }
    Same ($events -join ',') 'qt-on-ctest-list,core-off-import-audit' 'Command order drifted.'
    Same $result.ctest_inventories.'qt-on-ctest-list'.test_count ([long]65) `
        'CTest proof was not retained.'
    Same $result.core_off_imports.core_executable 'm0_qt_core_tests.exe' `
        'Core-off import proof was not retained.'

    $badRunner = {
        param($Command, $Environment)
        [pscustomobject]@{ exit_code = [long]7; stdout = ''; stderr = 'failed'; timed_out = $false }
    }
    Reject {
        Private 'Invoke-M0PackageV2BuildCommands' @{
            Plan = [pscustomobject]@{ schema_version = [long]2; commands = @($commands[0]) }
            Environment = [ordered]@{ VSLANG = '1033' }
            RunAction = $badRunner
        }
    } 'qt-on-ctest-list|exit|failed'
}

Case 'CTest inventory permits only the numeric Catch2 skip-return sentinel four' {
    $inventoryWith = {
        param($Property)
        [pscustomobject]@{
            kind = 'ctestInfo'
            version = [pscustomobject]@{ major = [long]1; minor = [long]0 }
            tests = @([pscustomobject]@{
                    name = 'catch2-case'
                    properties = @($Property)
                })
        } | ConvertTo-Json -Depth 10 -Compress
    }
    $accepted = & $inventoryWith ([pscustomobject]@{
            name = 'SKIP_RETURN_CODE'
            value = [long]4
        })
    $proof = Private 'Assert-M0PackageV2CTestInventory' @{
        Json = $accepted
        ExpectedCount = [long]1
        Context = 'Catch2 CTest inventory'
    }
    Same $proof.test_count ([long]1) 'Catch2 skip-return sentinel lost the test inventory.'

    foreach ($bad in @(
            [pscustomobject]@{ name = 'SKIP_RETURN_CODE'; value = [long]3 },
            [pscustomobject]@{ name = 'SKIP_RETURN_CODE'; value = [long]5 },
            [pscustomobject]@{ name = 'SKIP_RETURN_CODE'; value = '4' },
            [pscustomobject]@{ name = 'SKIP_REGULAR_EXPRESSION'; value = '.*' },
            [pscustomobject]@{ name = 'DISABLED'; value = $true })) {
        Reject {
            Private 'Assert-M0PackageV2CTestInventory' @{
                Json = (& $inventoryWith $bad)
                ExpectedCount = [long]1
                Context = 'Catch2 CTest inventory'
            }
        } 'disabled|skip|SKIP|sentinel|return'
    }
}

Case 'CTest JUnit proves exact executed count and rejects a skipped or Not Run case' {
    $cases = 1..65 | ForEach-Object {
        "<testcase name=`"case-$_`" status=`"run`"/>"
    }
    $xml = "<testsuite tests=`"65`" failures=`"0`" errors=`"0`" skipped=`"0`" disabled=`"0`">" +
        ($cases -join '') + '</testsuite>'
    $proof = Private 'Assert-M0PackageV2CTestJUnit' @{
        Xml = $xml; ExpectedCount = [long]65; Context = 'Qt-on CTest'
    }
    Same $proof.test_count ([long]65) 'JUnit executed test count drifted.'
    Same $proof.skipped ([long]0) 'JUnit skipped proof drifted.'

    $skipped = $xml.Replace(
        '<testcase name="case-65" status="run"/>',
        '<testcase name="case-65" status="notrun"><skipped/></testcase>')
    Reject {
        Private 'Assert-M0PackageV2CTestJUnit' @{
            Xml = $skipped; ExpectedCount = [long]65; Context = 'Qt-on CTest'
        }
    } 'skipped|not-run|Not Run'

    foreach ($elementName in @('failure', 'error')) {
        $failed = $xml.Replace(
            '<testcase name="case-65" status="run"/>',
            "<testcase name=`"case-65`" status=`"run`"><$elementName/></testcase>")
        Reject {
            Private 'Assert-M0PackageV2CTestJUnit' @{
                Xml = $failed; ExpectedCount = [long]65; Context = 'Qt-on CTest'
            }
        } 'failure|error|failed'
    }
    $wrongAggregate = $xml.Replace('tests="65"', 'tests="64"')
    Reject {
        Private 'Assert-M0PackageV2CTestJUnit' @{
            Xml = $wrongAggregate; ExpectedCount = [long]65; Context = 'Qt-on CTest'
        }
    } 'aggregate|tests|count'
    $disabledAttribute = $xml.Replace(
        '<testcase name="case-65" status="run"/>',
        '<testcase name="case-65" status="run" disabled="true"/>')
    Reject {
        Private 'Assert-M0PackageV2CTestJUnit' @{
            Xml = $disabledAttribute; ExpectedCount = [long]65; Context = 'Qt-on CTest'
        }
    } 'disabled'
    $duplicateName = $xml.Replace('name="case-65"', 'name="case-1"')
    Reject {
        Private 'Assert-M0PackageV2CTestJUnit' @{
            Xml = $duplicateName; ExpectedCount = [long]65; Context = 'Qt-on CTest'
        }
    } 'duplicate|name'
}

Case 'CTest execution names exactly match the authenticated inventory names' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) `
        ('m0-ctest-name-proof-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    $junitPath = Join-Path $temp 'results.xml'
    $inventoryTests = 1..65 | ForEach-Object {
        [pscustomobject]@{ name = "case-$_"; properties = @() }
    }
    $inventory = [pscustomobject]@{
        kind = 'ctestInfo'
        version = [pscustomobject]@{ major = [long]1; minor = [long]0 }
        tests = $inventoryTests
    } | ConvertTo-Json -Depth 10 -Compress
    $commands = @(
        [pscustomobject]@{
            id = 'qt-on-ctest-list'; file_path = 'C:\ctest.exe'
            arguments = [object[]]@('--show-only=json-v1')
            working_directory = 'C:\repo'; timeout_seconds = [long]30
            expected_test_count = [long]65
        },
        [pscustomobject]@{
            id = 'qt-on-ctest'; file_path = 'C:\ctest.exe'
            arguments = [object[]]@('--output-junit', $junitPath)
            working_directory = 'C:\repo'; timeout_seconds = [long]30
            expected_test_count = $null
        })
    try {
        $runtimeState = [pscustomobject]@{
            names = [object[]] (1..65 | ForEach-Object { "case-$_" })
        }
        $runner = {
            param($Command, $Environment)
            if ($Command.id -ceq 'qt-on-ctest-list') {
                return [pscustomobject]@{
                    exit_code = [long]0; stdout = $inventory; stderr = ''; timed_out = $false
                }
            }
            $cases = $runtimeState.names | ForEach-Object {
                "<testcase name=`"$_`" status=`"run`"/>"
            }
            $xml = "<testsuite tests=`"65`" failures=`"0`" errors=`"0`" skipped=`"0`" disabled=`"0`">" +
                ($cases -join '') + '</testsuite>'
            [IO.File]::WriteAllText($junitPath, $xml)
            [pscustomobject]@{
                exit_code = [long]0; stdout = ''; stderr = ''; timed_out = $false
            }
        }.GetNewClosure()
        $proof = Private 'Invoke-M0PackageV2BuildCommands' @{
            Plan = [pscustomobject]@{ schema_version = [long]2; commands = $commands }
            Environment = [ordered]@{ VSLANG = '1033' }
            RunAction = $runner
        }
        Same (@($proof.ctest_junit.'qt-on-ctest'.names) -join ',') `
            (@($runtimeState.names | Sort-Object) -join ',') `
            'JUnit name proof was not retained canonically.'

        $runtimeState.names = [object[]]@((1..64 | ForEach-Object { "case-$_" }) + 'different-65')
        Reject {
            Private 'Invoke-M0PackageV2BuildCommands' @{
                Plan = [pscustomobject]@{ schema_version = [long]2; commands = $commands }
                Environment = [ordered]@{ VSLANG = '1033' }
                RunAction = $runner
            }
        } 'names differ'
    }
    finally { [IO.Directory]::Delete($temp, $true) }
}

Case 'windeployqt permits only its one pinned translations warning' {
    $sdk = 'C:\repo\q\run\sdk'
    Private 'Assert-M0PackageV2DeployDiagnostics' @{
        StandardError = ''
        CompositeSdkRoot = $sdk
        Context = 'dry run'
    } | Out-Null
    $warning = "Warning: Translations will not be available due to the following error.`r`n" +
        "Cannot open C:/repo/q/run/sdk/translations/catalogs.json"
    $result = Private 'Assert-M0PackageV2DeployDiagnostics' @{
        StandardError = $warning
        CompositeSdkRoot = $sdk
        Context = 'dry run'
    }
    Same $result.warning_count ([long]1) 'Pinned deploy warning was not retained.'
    Reject {
        Private 'Assert-M0PackageV2DeployDiagnostics' @{
            StandardError = "$warning`nWarning: unexpected plugin fallback"
            CompositeSdkRoot = $sdk
            Context = 'dry run'
        }
    } 'unexpected|diagnostic|stderr'
}

Case 'deployment treats windeployqt targets as directories in dry and actual plans' {
    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-deploy-targets-' + [Guid]::NewGuid().ToString('N'))
    $sdk = Join-Path $fixtureRoot 'sdk'
    $qtBuild = Join-Path $fixtureRoot 'qt-build'
    $applicationRoot = Join-Path $fixtureRoot 'application'
    $coreSource = Join-Path $sdk 'bin/Qt6Core.dll'
    $pluginSource = Join-Path $sdk 'plugins/platforms/qwindows.dll'
    $d3dSource = Join-Path $sdk 'd3dcompiler_47.dll'
    $executable = Join-Path $qtBuild 'm0_qt_shell.exe'
    foreach ($directory in @(
            (Split-Path -Parent $coreSource),
            (Split-Path -Parent $pluginSource),
            (Join-Path $qtBuild 'qml/MusicPlayer/M0'),
            (Join-Path $fixtureRoot 'source/qml'))) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    [IO.File]::WriteAllText($coreSource, 'qt-core')
    [IO.File]::WriteAllText($pluginSource, 'qwindows')
    [IO.File]::WriteAllText($d3dSource, 'd3d')
    [IO.File]::WriteAllText($executable, 'shell')
    $mappings = @(
        [pscustomobject][ordered]@{
            source = $coreSource
            target = $applicationRoot
        },
        [pscustomobject][ordered]@{
            source = $pluginSource
            target = Join-Path $applicationRoot 'plugins/platforms'
        })
    $runner = {
        param($Command, $Environment)
        [IO.Directory]::CreateDirectory($applicationRoot) | Out-Null
        [IO.Directory]::CreateDirectory((Join-Path $applicationRoot 'plugins')) | Out-Null
        if ($Command.id -ceq 'deploy-actual') {
            [IO.Directory]::CreateDirectory(
                (Join-Path $applicationRoot 'plugins/platforms')) | Out-Null
            [IO.File]::Copy($coreSource, (Join-Path $applicationRoot 'Qt6Core.dll'), $false)
            [IO.File]::Copy(
                $pluginSource,
                (Join-Path $applicationRoot 'plugins/platforms/qwindows.dll'),
                $false)
        }
        return [pscustomobject][ordered]@{
            exit_code = [long]0
            stdout = [pscustomobject][ordered]@{ files = $mappings } |
                ConvertTo-Json -Depth 10 -Compress
            stderr = ''
            timed_out = $false
        }
    }.GetNewClosure()
    try {
        $result = Private 'Invoke-M0PackageV2Deployment' @{
            Context = [pscustomobject][ordered]@{
                repository_root = $fixtureRoot
                source_root = Join-Path $fixtureRoot 'source'
                composite_sdk_root = $sdk
                tools = [pscustomobject][ordered]@{
                    windeployqt = 'C:\fixture\windeployqt.exe'
                    qtpaths = 'C:\fixture\qtpaths.exe'
                }
            }
            QtOnBuildRoot = $qtBuild
            ApplicationRoot = $applicationRoot
            Environment = [ordered]@{ VSLANG = '1033' }
            RunAction = $runner
        }
        $expected = @(
            'Qt6Core.dll',
            'd3dcompiler_47.dll',
            'plugins/platforms/qwindows.dll')
        Same (@($result.dry_plan.files.relative_path) -join ',') ($expected -join ',') `
            'Dry-run directory targets did not resolve to canonical file paths.'
        Same (@($result.actual_plan.files.relative_path) -join ',') ($expected -join ',') `
            'Actual directory targets or synthetic D3D mapping drifted.'
        Need ([IO.File]::Exists((Join-Path $applicationRoot 'd3dcompiler_47.dll'))) `
            'Synthetic D3D file was not copied to its separately resolved target file.'

        if ($IsWindows) {
            $reparseDestination = Join-Path $fixtureRoot 'reparse-application'
            $outsideDirectory = Join-Path $fixtureRoot 'outside-target'
            $junction = Join-Path $reparseDestination 'plugins'
            [IO.Directory]::CreateDirectory($reparseDestination) | Out-Null
            [IO.Directory]::CreateDirectory($outsideDirectory) | Out-Null
            New-Item -ItemType Junction -Path $junction -Target $outsideDirectory | Out-Null
            try {
                Reject {
                    Private 'ConvertTo-M0PackageV2DeploymentPlan' @{
                        Files = [object[]]@([pscustomobject][ordered]@{
                                source = $pluginSource
                                target = Join-Path $junction 'future-platforms'
                            })
                        CompositeSdkRoot = $sdk
                        ApplicationQmlRoot = Join-Path $qtBuild 'qml/MusicPlayer/M0'
                        DestinationRoot = $reparseDestination
                    }
                } 'reparse|physical|ancestr'
            }
            finally {
                if ([IO.Directory]::Exists($junction)) {
                    [IO.Directory]::Delete($junction, $false)
                }
            }
        }
    }
    finally {
        if ([IO.Directory]::Exists($fixtureRoot)) {
            [IO.Directory]::Delete($fixtureRoot, $true)
        }
    }
}

Case 'publication entries are canonical and force receipt to the final position' {
    $rows = @(
        [pscustomobject]@{ relative_path = 'evidence/qt-shell-composite-package-receipt.v2.json'; size_bytes = [long]2; sha256 = 'b' * 64 },
        [pscustomobject]@{ relative_path = 'app/z.dll'; size_bytes = [long]1; sha256 = 'a' * 64 },
        [pscustomobject]@{ relative_path = 'evidence/qt-shell-composite-package-manifest.v2.json'; size_bytes = [long]3; sha256 = 'c' * 64 }
    )
    $entries = Private 'New-M0PackageV2PublicationEntries' @{
        SourceRoot = 'C:\candidate'
        Rows = $rows
        ReceiptRelativePath = 'evidence/qt-shell-composite-package-receipt.v2.json'
    }
    Same (@($entries.relative_path) -join ',') `
        'app/z.dll,evidence/qt-shell-composite-package-manifest.v2.json,evidence/qt-shell-composite-package-receipt.v2.json' `
        'Publication order drifted.'
    Same ($entries[-1].PSObject.Properties.Name -join ',') `
        'relative_path,size_bytes,sha256' 'Receipt pin shape drifted.'
}

Case 'closed build result canonicalizes verifier evidence without relaxing exact pins' {
    $fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) (
        'm0-package-closed-result-' + [Guid]::NewGuid().ToString('N'))
    $outputRoot = Join-Path $fixtureRoot 'package'
    $candidateRoot = Join-Path $fixtureRoot 'candidate'
    $manifestRelative = 'evidence/qt-shell-composite-package-manifest.v2.json'
    $receiptRelative = 'evidence/qt-shell-composite-package-receipt.v2.json'
    foreach ($directory in @(
            (Join-Path $outputRoot 'evidence'),
            (Join-Path $candidateRoot 'evidence'))) {
        [IO.Directory]::CreateDirectory($directory) | Out-Null
    }
    [IO.File]::WriteAllText((Join-Path $outputRoot $manifestRelative), 'manifest')
    [IO.File]::WriteAllText((Join-Path $outputRoot $receiptRelative), 'receipt')
    $rows = [ordered]@{
        payload = [pscustomobject][ordered]@{
            relative_path = 'app/m0_qt_shell.exe'; size_bytes = [long]1; sha256 = '1' * 64
        }
        manifest = [pscustomobject][ordered]@{
            relative_path = $manifestRelative; size_bytes = [long]2; sha256 = '2' * 64
        }
        sbom = [pscustomobject][ordered]@{
            relative_path = 'evidence/sbom/qt.spdx.json'; size_bytes = [long]3; sha256 = '3' * 64
        }
        receipt = [pscustomobject][ordered]@{
            relative_path = $receiptRelative; size_bytes = [long]4; sha256 = '4' * 64
        }
    }
    $context = [pscustomobject][ordered]@{
        output_root = $outputRoot
        expected_contract_sha256 = 'a' * 64
        expected_composite_receipt_sha256 = 'b' * 64
    }
    $candidate = [pscustomobject][ordered]@{
        source_root = $candidateRoot
        manifest_relative_path = $manifestRelative
        receipt_relative_path = $receiptRelative
        receipt_sha256 = '4' * 64
        entries = [object[]]@(
            $rows.payload, $rows.manifest, $rows.sbom, $rows.receipt)
    }
    $verified = [pscustomobject][ordered]@{
        verified = $true
        package_id = 'm0.2-qt-shell-composite-package-v2'
        contract_sha256 = 'a' * 64
        composite_receipt_sha256 = 'b' * 64
        manifest_sha256 = '2' * 64
        receipt_sha256 = '4' * 64
        manifest_path = $manifestRelative
        payload = [object[]]@($rows.payload)
        evidence = [object[]]@($rows.sbom, $rows.manifest, $rows.receipt)
    }
    try {
        $result = Private 'New-M0PackageV2ClosedBuildResult' @{
            Context = $context
            Candidate = $candidate
            Verified = $verified
        }
        Same (@($result.entries.relative_path) -join ',') `
            (@($candidate.entries.relative_path) -join ',') `
            'Verifier evidence order changed the canonical closed result.'

        $duplicate = $verified.PSObject.Copy()
        $duplicate.payload = [object[]]@($rows.payload, $rows.payload)
        Reject {
            Private 'New-M0PackageV2ClosedBuildResult' @{
                Context = $context; Candidate = $candidate; Verified = $duplicate
            }
        } 'duplicate|alias|inventory'

        $wrongPin = $verified.PSObject.Copy()
        $wrongPin.evidence = [object[]]@(
            $rows.sbom,
            [pscustomobject][ordered]@{
                relative_path = $manifestRelative; size_bytes = [long]2; sha256 = '9' * 64
            },
            $rows.receipt)
        Reject {
            Private 'New-M0PackageV2ClosedBuildResult' @{
                Context = $context; Candidate = $candidate; Verified = $wrongPin
            }
        } 'differs|inventory|pin'

        $receiptNotFinal = $verified.PSObject.Copy()
        $receiptNotFinal.evidence = [object[]]@(
            $rows.sbom, $rows.receipt, $rows.manifest)
        Reject {
            Private 'New-M0PackageV2ClosedBuildResult' @{
                Context = $context; Candidate = $candidate; Verified = $receiptNotFinal
            }
        } 'receipt|final'
    }
    finally {
        if ([IO.Directory]::Exists($fixtureRoot)) {
            [IO.Directory]::Delete($fixtureRoot, $true)
        }
    }
}

Case 'generated evidence and controls use create-new without overwriting bytes' {
    $temp = Join-Path ([IO.Path]::GetTempPath()) `
        ('m0-package-create-new-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    $path = Join-Path $temp 'control.json'
    [IO.File]::WriteAllText($path, 'sentinel')
    try {
        Reject {
            Private 'Write-M0PackageV2Utf8File' @{
                LiteralPath = $path
                Text = 'replacement'
            }
        } 'fresh|exists|create|already'
        Same ([IO.File]::ReadAllText($path)) 'sentinel' `
            'Create-new evidence writer overwrote an existing file.'
    }
    finally { [IO.Directory]::Delete($temp, $true) }
}

Case 'Qt-on and deployment environments are closed while core-off has no Qt path or variables' {
    $contract = Get-Content -Raw -LiteralPath (Join-Path $root 'qt-shell-composite-package-contract.v2.json') |
        ConvertFrom-Json -Depth 100 -DateKind String
    $toolMap = [ordered]@{}
    $sdk = [IO.Path]::GetFullPath((Join-Path $root '../../../../../q/f75eb9bea8280844c6d1a2b3f4/sdk'))
    foreach ($tool in @($contract.inputs.tools)) {
        $toolMap[$tool.id] = if ($tool.path_kind -ceq 'absolute') {
            [string] $tool.path
        }
        else { Join-Path $sdk $tool.path }
    }
    $privateTemp = Join-Path ([IO.Path]::GetTempPath()) ('m0-env-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($privateTemp) | Out-Null
    try {
        $environments = Private 'New-M0PackageV2BuildEnvironment' @{
            Context = [pscustomobject]@{ tools = [pscustomobject]$toolMap; composite_sdk_root = $sdk }
            PrivateTempRoot = $privateTemp
        }
        foreach ($name in @('QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QT_QUICK_CONTROLS_STYLE',
                'QT_FATAL_WARNINGS', 'QT_FORCE_STDERR_LOGGING', 'QML_DISABLE_DISK_CACHE')) {
            Need $environments.qt_on.Contains($name) "Qt-on environment lacks '$name'."
            Need $environments.deployment.Contains($name) "Deployment environment lacks '$name'."
            Need (-not $environments.core_off.Contains($name)) "Core-off environment contains '$name'."
        }
        Need $environments.qt_on.PATH.Contains($sdk, [StringComparison]::OrdinalIgnoreCase) `
            'Qt-on PATH lacks the composite SDK.'
        Need (-not $environments.core_off.PATH.Contains($sdk, [StringComparison]::OrdinalIgnoreCase)) `
            'Core-off PATH contains the composite SDK.'
        $redacted = Private 'ConvertTo-M0PackageV2RedactedEnvironment' @{
            Environment = $environments.qt_on
        }
        Need (@($redacted | Where-Object value -CNE '<redacted>').Count -eq 6) `
            'Redacted environment leaked a path or hid an explicit Qt control.'
    }
    finally { [IO.Directory]::Delete($privateTemp, $true) }
}

Case 'Git source authentication records the live commit and binds every tree to that commit' {
    $commit = 'e' * 40
    $pins = [ordered]@{
        shell_tree = [pscustomobject]@{
            relative_path = 'experiments/m0/candidates/qt/shell'
            git_tree = '1' * 40
        }
        fixture_tree = [pscustomobject]@{
            relative_path = 'experiments/m0/shared/shell-fixture'
            git_tree = '2' * 40
        }
        protocol_tree = [pscustomobject]@{
            relative_path = 'experiments/m0/protocol'
            git_tree = '3' * 40
        }
    }
    $treeBySpec = [ordered]@{}
    $treeListingRows = [Collections.Generic.List[string]]::new()
    $indexListingRows = [Collections.Generic.List[string]]::new()
    $blobDigit = [long]4
    foreach ($pin in $pins.Values) {
        $treeBySpec["${commit}:$($pin.relative_path)"] = $pin.git_tree
        $path = "$($pin.relative_path)/source.txt"
        $blob = ('{0:x}' -f $blobDigit).PadLeft(40, [char] '0')
        $treeListingRows.Add("100644 blob $blob`t$path")
        $indexListingRows.Add("H $path")
        $blobDigit++
    }
    $treeListing = ($treeListingRows -join [char]0) + [char]0
    $indexListing = ($indexListingRows -join [char]0) + [char]0
    $physicalProof = {
        param($Context, $TreeInputs, $ExpectedEntries, $Phase)
        [pscustomobject][ordered]@{
            file_count = [long] $ExpectedEntries.Count
            physical_inventory_sha256 = 'a' * 64
        }
    }
    $seenTreeSpecs = [Collections.Generic.List[string]]::new()
    $runner = {
        param($Command, $Environment)
        [string[]] $arguments = @($Command.arguments)
        if ($arguments -contains 'status') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = ''; stderr = ''; timed_out = $false
            }
        }
        if ($arguments -contains 'ls-tree') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = $treeListing; stderr = ''; timed_out = $false
            }
        }
        if ($arguments -contains 'ls-files') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = $indexListing; stderr = ''; timed_out = $false
            }
        }
        $spec = [string] $arguments[-1]
        if ($spec -ceq 'HEAD') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = "$commit`n"; stderr = ''; timed_out = $false
            }
        }
        $seenTreeSpecs.Add($spec)
        if (-not $treeBySpec.Contains($spec)) {
            return [pscustomobject]@{
                exit_code = [long]2; stdout = ''; stderr = "unexpected tree spec: $spec"; timed_out = $false
            }
        }
        return [pscustomobject]@{
            exit_code = [long]0; stdout = "$($treeBySpec[$spec])`n"; stderr = ''; timed_out = $false
        }
    }.GetNewClosure()
    $proof = Private 'Assert-M0PackageV2GitSources' @{
        Context = [pscustomobject]@{
            repository_root = 'C:\repo'
            tools = [pscustomobject]@{ git = 'C:\tools\git.exe' }
            contract = [pscustomobject]@{
                inputs = [pscustomobject]@{ sources = [pscustomobject] $pins }
            }
        }
        Environment = [ordered]@{ VSLANG = '1033' }
        RunAction = $runner
        Phase = 'pre'
        PhysicalProofAction = $physicalProof
    }
    Same $proof.source_commit $commit 'Source proof did not record the live HEAD.'
    Same ($seenTreeSpecs -join ',') ($treeBySpec.Keys -join ',') `
        'Source trees were not resolved against the recorded commit.'
    Same ($proof.trees.PSObject.Properties.Value -join ',') `
        ($pins.Values.git_tree -join ',') 'Pinned tree proof drifted.'

    $movingState = @{ head_reads = [long]0 }
    $movingRunner = {
        param($Command, $Environment)
        [string[]] $arguments = @($Command.arguments)
        if ($arguments -contains 'status') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = ''; stderr = ''; timed_out = $false
            }
        }
        if ($arguments -contains 'ls-tree') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = $treeListing; stderr = ''; timed_out = $false
            }
        }
        if ($arguments -contains 'ls-files') {
            return [pscustomobject]@{
                exit_code = [long]0; stdout = $indexListing; stderr = ''; timed_out = $false
            }
        }
        $spec = [string] $arguments[-1]
        if ($spec -ceq 'HEAD') {
            $movingState.head_reads++
            $value = if ($movingState.head_reads -eq 1) { $commit } else { 'f' * 40 }
            return [pscustomobject]@{
                exit_code = [long]0; stdout = "$value`n"; stderr = ''; timed_out = $false
            }
        }
        return [pscustomobject]@{
            exit_code = [long]0; stdout = "$($treeBySpec[$spec])`n"; stderr = ''; timed_out = $false
        }
    }.GetNewClosure()
    Reject {
        Private 'Assert-M0PackageV2GitSources' @{
            Context = [pscustomobject]@{
                repository_root = 'C:\repo'
                tools = [pscustomobject]@{ git = 'C:\tools\git.exe' }
                contract = [pscustomobject]@{
                    inputs = [pscustomobject]@{ sources = [pscustomobject] $pins }
                }
            }
            Environment = [ordered]@{ VSLANG = '1033' }
            RunAction = $movingRunner
            Phase = 'post'
            PhysicalProofAction = $physicalProof
        }
    } 'HEAD|changed|stable'
}

Case 'Git source authentication rejects hidden index flags and changed worktree bytes' {
    $git = [string] @(Get-Command git -CommandType Application -All -ErrorAction Stop)[0].Source
    $repository = Join-Path ([IO.Path]::GetTempPath()) `
        ('m0-source-auth-' + [Guid]::NewGuid().ToString('N'))
    $paths = [ordered]@{
        shell_tree = 'experiments/m0/candidates/qt/shell'
        fixture_tree = 'experiments/m0/shared/shell-fixture'
        protocol_tree = 'experiments/m0/protocol'
    }
    [IO.Directory]::CreateDirectory($repository) | Out-Null
    foreach ($relative in $paths.Values) {
        $directory = Join-Path $repository $relative
        [IO.Directory]::CreateDirectory($directory) | Out-Null
        [IO.File]::WriteAllText((Join-Path $directory 'source.txt'), "original-$relative`n")
    }
    $leases = [Collections.Generic.List[object]]::new()
    try {
        & $git -C $repository init --quiet
        & $git -C $repository config user.name 'Package Test'
        & $git -C $repository config user.email 'package-test@example.invalid'
        & $git -C $repository add -- @($paths.Values)
        & $git -C $repository commit --quiet -m 'source fixture'
        Need ($LASTEXITCODE -eq 0) 'Could not commit the source authentication fixture.'

        $treePins = [ordered]@{}
        foreach ($entry in $paths.GetEnumerator()) {
            $treePins[$entry.Key] = [pscustomobject]@{
                relative_path = $entry.Value
                git_tree = [string] (& $git -C $repository rev-parse "HEAD:$($entry.Value)")
            }
        }
        $context = [pscustomobject]@{
            repository_root = $repository
            tools = [pscustomobject]@{ git = $git }
            contract = [pscustomobject]@{
                inputs = [pscustomobject]@{ sources = [pscustomobject] $treePins }
            }
            retained_leases = $leases
            retained_files = [Collections.Generic.List[object]]::new()
            retained_directories = [Collections.Generic.List[object]]::new()
            source_file_leases = [Collections.Generic.Dictionary[string, object]]::new(
                [StringComparer]::OrdinalIgnoreCase)
            source_tree_envelopes = [Collections.Generic.List[object]]::new()
        }
        $systemRoot = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
        $environment = [ordered]@{
            SystemRoot = $systemRoot
            WINDIR = $systemRoot
            COMSPEC = Join-Path $systemRoot 'System32/cmd.exe'
            TEMP = [IO.Path]::GetTempPath()
            TMP = [IO.Path]::GetTempPath()
            PATH = (([IO.Path]::GetDirectoryName($git)), (Join-Path $systemRoot 'System32')) -join ';'
            GIT_CONFIG_NOSYSTEM = '1'
            GIT_CONFIG_GLOBAL = 'NUL'
            GIT_OPTIONAL_LOCKS = '0'
            LC_ALL = 'C'
        }
        $runner = {
            param($Command, $Environment)
            return & $module {
                param($InnerCommand, $InnerEnvironment)
                Invoke-M0PackageV2BoundedProcess -Command $InnerCommand `
                    -Environment $InnerEnvironment
            } $Command $Environment
        }.GetNewClosure()
        $subjectRelative = "$($paths.shell_tree)/source.txt"
        $subject = Join-Path $repository $subjectRelative

        foreach ($flag in @('assume-unchanged', 'skip-worktree')) {
            [IO.File]::WriteAllText($subject, "original-$($paths.shell_tree)`n")
            & $git -C $repository update-index --no-assume-unchanged --no-skip-worktree -- $subjectRelative
            & $git -C $repository update-index "--$flag" -- $subjectRelative
            [IO.File]::WriteAllText($subject, "changed-but-hidden-$flag`n")
            Reject {
                Private 'Assert-M0PackageV2GitSources' @{
                    Context = $context
                    Environment = $environment
                    RunAction = $runner
                    Phase = 'pre'
                }
            } 'index|flag|worktree|source|drift'
        }
    }
    finally {
        Private 'Close-M0PackageV2Leases' @{ Leases = $leases } | Out-Null
        if ([IO.Directory]::Exists($repository)) {
            foreach ($file in @(Get-ChildItem -LiteralPath $repository -File -Recurse -Force)) {
                if ($file.IsReadOnly) { $file.IsReadOnly = $false }
            }
            [IO.Directory]::Delete($repository, $true)
        }
    }
}

Case 'Catch2 extractor accepts only create-new regular files under one canonical root' {
    $regular = [System.Formats.Tar.TarEntryType]::RegularFile
    $directory = [System.Formats.Tar.TarEntryType]::Directory
    $validArchive = New-TarFixture @(
        [pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/CMakeLists.txt'; text = 'project(Catch2)' },
        [pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/LICENSE.txt'; text = 'license' })
    $validDestination = Join-Path ([IO.Path]::GetTempPath()) ('m0-catch-valid-' + [Guid]::NewGuid().ToString('N'))
    try {
        $result = Private 'Expand-M0PackageV2Catch2Archive' @{
            ArchivePath = $validArchive; DestinationRoot = $validDestination
        }
        Same $result.file_count ([long]2) 'Valid archive file count drifted.'
        Need ([IO.File]::Exists((Join-Path $validDestination 'LICENSE.txt'))) `
            'Valid archive was not extracted.'
    }
    finally {
        if ([IO.Directory]::Exists($validDestination)) { [IO.Directory]::Delete($validDestination, $true) }
        [IO.File]::Delete($validArchive)
    }

    $badCases = @(
        [pscustomobject]@{ name = 'parent'; entries = @([pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/../escape'; text = 'x' }) },
        [pscustomobject]@{ name = 'absolute'; entries = @([pscustomobject]@{ type = $regular; name = 'C:/absolute'; text = 'x' }) },
        [pscustomobject]@{ name = 'ads'; entries = @([pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/file:ads'; text = 'x' }) },
        [pscustomobject]@{ name = 'duplicate'; entries = @(
                [pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/a.txt'; text = 'x' },
                [pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/a.txt'; text = 'y' }) },
        [pscustomobject]@{ name = 'case-alias'; entries = @(
                [pscustomobject]@{ type = $directory; name = 'Catch2-3.15.3/Folder/'; text = '' },
                [pscustomobject]@{ type = $regular; name = 'Catch2-3.15.3/folder/a.txt'; text = 'x' }) },
        [pscustomobject]@{ name = 'symlink'; entries = @([pscustomobject]@{
                    type = [System.Formats.Tar.TarEntryType]::SymbolicLink
                    name = 'Catch2-3.15.3/link'; link_name = 'CMakeLists.txt'; text = '' }) },
        [pscustomobject]@{ name = 'hardlink'; entries = @([pscustomobject]@{
                    type = [System.Formats.Tar.TarEntryType]::HardLink
                    name = 'Catch2-3.15.3/link'; link_name = 'CMakeLists.txt'; text = '' }) }
    )
    foreach ($bad in $badCases) {
        $archive = New-TarFixture $bad.entries
        $destination = Join-Path ([IO.Path]::GetTempPath()) ('m0-catch-bad-' + [Guid]::NewGuid().ToString('N'))
        try {
            Reject {
                Private 'Expand-M0PackageV2Catch2Archive' @{
                    ArchivePath = $archive; DestinationRoot = $destination
                }
            } 'Catch2|relative|unsupported|collision|duplicate|alias'
        }
        finally {
            if ([IO.Directory]::Exists($destination)) { [IO.Directory]::Delete($destination, $true) }
            [IO.File]::Delete($archive)
        }
    }
    $existing = Join-Path ([IO.Path]::GetTempPath()) ('m0-catch-existing-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($existing) | Out-Null
    $archive = New-TarFixture @()
    try {
        Reject {
            Private 'Expand-M0PackageV2Catch2Archive' @{
                ArchivePath = $archive; DestinationRoot = $existing
            }
        } 'fresh|exists'
    }
    finally { [IO.Directory]::Delete($existing, $true); [IO.File]::Delete($archive) }
}

if ($failures.Count) { throw "Package v2 production execution Reds: $($failures.Count) failure(s)." }
