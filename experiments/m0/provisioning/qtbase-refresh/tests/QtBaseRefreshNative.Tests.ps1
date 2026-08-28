#Requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'M0.QtBaseRefreshNative.psm1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('m0-qtbase-refresh-native-tests-' + [Guid]::NewGuid().ToString('N'))
$state = [ordered]@{ Passed = 0; Failed = 0 }
$expectedNativeConfigureSummary = (@(
        ''
        'Building for: win32-msvc (x86_64, CPU features: )'
        'Compiler: msvc 19.44.35228.0'
        'Build options:'
        '  Mode ................................... debug and release (with debug info)'
        '  Building shared libraries (alias of  BUILD_SHARED_LIBS)  yes'
        '  Using Link Time Optimization (LTCG) .... no'
        '  Using Intel Control-flow Enforcement Technology (CET)  yes'
        '  OpenSSL ................................ no'
        '    Qt directly linked to OpenSSL ........ no'
        '  OpenSSL 1.1 ............................ no'
        '  OpenSSL 3.0 ............................ no'
        '  Using system zlib ...................... no'
        '  PCRE2 .................................. yes'
        '    Using system PCRE2 ................... no'
        '  Schannel ............................... yes'
        '  Accessibility .......................... yes'
        '  FreeType ............................... yes'
        '    Using system FreeType ................ no'
        '  HarfBuzz ............................... yes'
        '    Using system HarfBuzz ................ no'
        '    JPEG ................................. yes'
        '    PNG .................................. yes'
        '  OpenGL:'
        '    Desktop OpenGL ....................... no'
        '    Dynamic OpenGL ....................... yes'
        '    OpenGL ES 2.0 ........................ no'
        '    OpenGL ES 3.0 ........................ no'
        '    OpenGL ES 3.1 ........................ no'
        '    OpenGL ES 3.2 ........................ no'
        '  Vulkan ................................. yes'
        '    DirectWrite .......................... yes'
        '    DirectWrite 3 ........................ yes'
        '    DirectWrite COLRv1 Support ........... yes'
    ) -join "`n") + "`n"
$expectedNativeCMakeCache = (@(
        'CMAKE_CONFIGURATION_TYPES:UNINITIALIZED=RelWithDebInfo;Debug'
        'CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config'
        'BUILD_SHARED_LIBS:BOOL=ON'
        'BUILD_WITH_PCH:BOOL=OFF'
        'CMAKE_INTERPROCEDURAL_OPTIMIZATION:UNINITIALIZED=OFF'
        'QT_FEATURE_accessibility:INTERNAL=ON'
        'QT_FEATURE_direct2d:INTERNAL=ON'
        'QT_FEATURE_direct2d1_1:INTERNAL=ON'
        'QT_FEATURE_directwrite:INTERNAL=ON'
        'QT_FEATURE_directwrite3:INTERNAL=ON'
        'QT_FEATURE_directwritecolrv1:INTERNAL=ON'
        'QT_FEATURE_dynamicgl:INTERNAL=ON'
        'QT_FEATURE_freetype:INTERNAL=ON'
        'QT_FEATURE_harfbuzz:INTERNAL=ON'
        'QT_FEATURE_intelcet:INTERNAL=ON'
        'QT_FEATURE_jpeg:INTERNAL=ON'
        'QT_FEATURE_msvc_obj_debug_info:INTERNAL=ON'
        'QT_FEATURE_opengl:INTERNAL=ON'
        'QT_FEATURE_openssl:INTERNAL=OFF'
        'QT_FEATURE_pcre2:INTERNAL=ON'
        'QT_FEATURE_png:INTERNAL=ON'
        'QT_FEATURE_schannel:INTERNAL=ON'
        'QT_FEATURE_system_freetype:INTERNAL=OFF'
        'QT_FEATURE_system_harfbuzz:INTERNAL=OFF'
        'QT_FEATURE_system_jpeg:INTERNAL=OFF'
        'QT_FEATURE_system_pcre2:INTERNAL=OFF'
        'QT_FEATURE_system_png:INTERNAL=OFF'
        'QT_FEATURE_system_zlib:INTERNAL=OFF'
        'QT_FEATURE_systemtrayicon:INTERNAL=ON'
        'QT_FEATURE_vulkan:INTERNAL=ON'
    ) -join "`n") + "`n"

function Test-Case {
    param([Parameter(Mandatory)] [string] $Name, [Parameter(Mandatory)] [scriptblock] $Body)
    try {
        & $Body
        $state.Passed++
        Write-Host "PASS: $Name"
    }
    catch {
        $state.Failed++
        Write-Host "FAIL: $Name`n$($_.Exception.ToString())" -ForegroundColor Red
    }
}

function Assert-True {
    param([Parameter(Mandatory)] [bool] $Condition, [Parameter(Mandatory)] [string] $Message)
    if (-not $Condition) { throw $Message }
}

function Assert-Equal {
    param($Actual, $Expected, [string] $Message = 'Values differ.')
    if ($Actual -is [array] -or $Expected -is [array]) {
        $actualValues = @($Actual)
        $expectedValues = @($Expected)
        if ($actualValues.Count -ne $expectedValues.Count) {
            throw "$Message Expected $($expectedValues.Count) values, got $($actualValues.Count)."
        }
        for ($index = 0; $index -lt $expectedValues.Count; $index++) {
            if ($actualValues[$index] -cne $expectedValues[$index]) {
                throw "$Message Difference at index ${index}: '$($actualValues[$index])' != '$($expectedValues[$index])'."
            }
        }
        return
    }
    if ($Actual -cne $Expected) { throw "$Message '$Actual' != '$Expected'." }
}

function Assert-Rejected {
    param([Parameter(Mandatory)] [scriptblock] $Body, [Parameter(Mandatory)] [string] $Pattern)
    try { & $Body }
    catch {
        if ($_.Exception.ToString() -notmatch $Pattern) {
            throw "Expected rejection matching '$Pattern', got: $($_.Exception.ToString())"
        }
        return
    }
    throw "Expected rejection matching '$Pattern', but the operation succeeded."
}

function Get-TestSha256Bytes {
    param([Parameter(Mandatory)] [AllowEmptyCollection()] [byte[]] $Bytes)
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()
}

function Get-TestSha256File {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Write-TestBytes {
    param([Parameter(Mandatory)] [string] $LiteralPath, [Parameter(Mandatory)] [byte[]] $Bytes)
    [IO.Directory]::CreateDirectory((Split-Path -Parent $LiteralPath)) | Out-Null
    [IO.File]::WriteAllBytes($LiteralPath, $Bytes)
}

function New-TestTarGz {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [object[]] $Entries,
        [Parameter(Mandatory)] [Collections.IDictionary] $GlobalAttributes
    )
    [IO.Directory]::CreateDirectory((Split-Path -Parent $LiteralPath)) | Out-Null
    $file = [IO.File]::Create($LiteralPath)
    try {
        $gzip = [IO.Compression.GZipStream]::new($file, [IO.Compression.CompressionLevel]::Optimal, $true)
        try {
            $writer = [System.Formats.Tar.TarWriter]::new($gzip, [System.Formats.Tar.TarEntryFormat]::Ustar, $true)
            try {
                $pairs = [Collections.Generic.List[Collections.Generic.KeyValuePair[string,string]]]::new()
                foreach ($key in $GlobalAttributes.Keys) {
                    $pairs.Add([Collections.Generic.KeyValuePair[string,string]]::new([string]$key, [string]$GlobalAttributes[$key]))
                }
                $writer.WriteEntry([System.Formats.Tar.PaxGlobalExtendedAttributesTarEntry]::new($pairs))
                foreach ($spec in $Entries) {
                    if ($spec.Kind -eq 'pax-file') {
                        $extended = [Collections.Generic.List[Collections.Generic.KeyValuePair[string,string]]]::new()
                        foreach ($key in $spec.Attributes.Keys) {
                            $extended.Add([Collections.Generic.KeyValuePair[string,string]]::new([string]$key, [string]$spec.Attributes[$key]))
                        }
                        $entry = [System.Formats.Tar.PaxTarEntry]::new(
                            [System.Formats.Tar.TarEntryType]::RegularFile,
                            [string]$spec.Name,
                            $extended)
                    }
                    else {
                        $type = switch ($spec.Kind) {
                            'directory' { [System.Formats.Tar.TarEntryType]::Directory }
                            'file' { [System.Formats.Tar.TarEntryType]::RegularFile }
                            'symlink' { [System.Formats.Tar.TarEntryType]::SymbolicLink }
                            'hardlink' { [System.Formats.Tar.TarEntryType]::HardLink }
                            default { throw "Unknown test tar entry kind '$($spec.Kind)'." }
                        }
                        $entry = [System.Formats.Tar.UstarTarEntry]::new($type, [string]$spec.Name)
                    }
                    if ($spec.PSObject.Properties.Name -contains 'LinkName') { $entry.LinkName = [string]$spec.LinkName }
                    if ($spec.PSObject.Properties.Name -contains 'Content') {
                        $entry.DataStream = [IO.MemoryStream]::new([Text.UTF8Encoding]::new($false).GetBytes([string]$spec.Content), $false)
                    }
                    try { $writer.WriteEntry($entry) }
                    finally { if ($null -ne $entry.DataStream) { $entry.DataStream.Dispose() } }
                }
            }
            finally { $writer.Dispose() }
        }
        finally { $gzip.Dispose() }
    }
    finally { $file.Dispose() }
}

function New-ValidArchiveFixture {
    param([Parameter(Mandatory)] [string] $Name)
    $fixtureRoot = Join-Path $testRoot $Name
    [IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
    $archivePath = Join-Path $fixtureRoot 'package.tar.gz'
    $entries = @(
        [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/' },
        [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/include/' },
        [pscustomobject]@{ Kind = 'file'; Name = 'pkg/include/a.h'; Content = "alpha`n" }
    )
    New-TestTarGz -LiteralPath $archivePath -Entries $entries -GlobalAttributes ([ordered]@{ comment = '0123456789abcdef' })
    $fileBytes = [Text.UTF8Encoding]::new($false).GetBytes("alpha`n")
    $inventoryText = "include/a.h`t$($fileBytes.Length)`t$(Get-TestSha256Bytes $fileBytes)`n"
    return [pscustomobject]@{
        Root = $fixtureRoot
        ArchivePath = $archivePath
        ArchiveSize = [long](Get-Item -LiteralPath $archivePath).Length
        ArchiveSha256 = Get-TestSha256File $archivePath
        ExpectedInventoryText = $inventoryText
        ExpectedInventorySha256 = Get-TestSha256Bytes ([Text.UTF8Encoding]::new($false).GetBytes($inventoryText))
    }
}

if (-not [IO.File]::Exists($modulePath)) {
    throw "Native Qtbase refresh module is missing: $modulePath"
}
$productionModuleSource = [IO.File]::ReadAllText($modulePath)
$processInsertionPoint = '    # TEST-FIXTURE-INSERTION-POINT: PROCESS'
$preparationInsertionPoint = '    # TEST-FIXTURE-INSERTION-POINT: PREPARATION'
foreach ($insertionPoint in @($processInsertionPoint, $preparationInsertionPoint)) {
    if ($productionModuleSource.IndexOf($insertionPoint, [StringComparison]::Ordinal) -lt 0 -or
        $productionModuleSource.IndexOf($insertionPoint, [StringComparison]::Ordinal) -ne
        $productionModuleSource.LastIndexOf($insertionPoint, [StringComparison]::Ordinal)) {
        throw "Native test fixture insertion point is missing or ambiguous: $insertionPoint"
    }
}
$processFixtureCode = @'
    $testProcessCallback = Get-Variable -Name NativeFixtureProcessCallback -Scope Script -ValueOnly -ErrorAction SilentlyContinue
    if ($null -ne $testProcessCallback) {
        $results = @(& $testProcessCallback $Request)
        if ($results.Count -ne 1 -or $null -eq $results[0]) { throw "Native fixture process callback returned no single result for '$($Request.id)'." }
        $result = $results[0]
        Assert-M0NativeExactProperties $result @('exit_code', 'timed_out') "Native fixture process result '$($Request.id)'"
        if (($result.exit_code -isnot [long] -and $result.exit_code -isnot [int]) -or $result.timed_out -isnot [bool]) {
            throw "Native fixture process '$($Request.id)' returned invalid result types."
        }
        return [pscustomobject]@{ exit_code = [long]$result.exit_code; timed_out = [bool]$result.timed_out }
    }
'@
$preparationFixtureCode = @'
    $testPreparationCallback = Get-Variable -Name NativeFixturePreparationCallback -Scope Script -ValueOnly -ErrorAction SilentlyContinue
    if ($null -ne $testPreparationCallback) {
        $results = @(& $testPreparationCallback $Context)
        if ($results.Count -ne 1 -or $null -eq $results[0]) { throw 'Native fixture preparation callback returned no single result.' }
        return $results[0]
    }
'@
$instrumentedModuleSource = $productionModuleSource.Replace($processInsertionPoint, $processFixtureCode).
    Replace($preparationInsertionPoint, $preparationFixtureCode)
$instrumentedModulePath = Join-Path $testRoot 'fixture-module/M0.QtBaseRefreshNative.TestFixture.psm1'
Write-TestBytes $instrumentedModulePath ([Text.UTF8Encoding]::new($false).GetBytes($instrumentedModuleSource))
$module = Import-Module -Name $instrumentedModulePath -Force -PassThru

function New-M0CanonicalTreeInventory {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $TreeRoot, [Parameter(Mandatory)] [string] $InventoryPath)
    return & $script:module {
        param($arguments)
        New-M0CanonicalTreeInventory @arguments
    } $PSBoundParameters
}

function Assert-M0TreeMatchesInventory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $TreeRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedFileCount,
        [Parameter(Mandatory)] [long] $ExpectedTotalBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256
    )
    return & $script:module {
        param($arguments)
        Assert-M0TreeMatchesInventory @arguments
    } $PSBoundParameters
}

function Expand-M0AuthenticatedTarGz {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ArchivePath,
        [Parameter(Mandatory)] [string] $DestinationRoot,
        [Parameter(Mandatory)] [string] $InventoryPath,
        [Parameter(Mandatory)] [long] $ExpectedArchiveSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedArchiveSha256,
        [Parameter(Mandatory)] [string] $ExpectedTopLevelDirectory,
        [Parameter(Mandatory)] [Collections.IDictionary] $ExpectedGlobalPaxAttributes,
        [Parameter(Mandatory)] [long] $ExpectedArchiveEntryCount,
        [Parameter(Mandatory)] [long] $ExpectedDirectoryCount,
        [Parameter(Mandatory)] [long] $ExpectedFileCount,
        [Parameter(Mandatory)] [long] $ExpectedTotalFileBytes,
        [Parameter(Mandatory)] [string] $ExpectedInventorySha256
    )
    return & $script:module {
        param($arguments)
        Expand-M0AuthenticatedTarGz @arguments
    } $PSBoundParameters
}

function Invoke-M0CopyAndApplySingleTargetPatch {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $SourceTreeRoot,
        [Parameter(Mandatory)] [string] $DestinationTreeRoot,
        [Parameter(Mandatory)] [string] $PatchPath,
        [Parameter(Mandatory)] [long] $ExpectedPatchSizeBytes,
        [Parameter(Mandatory)] [string] $ExpectedPatchSha256,
        [Parameter(Mandatory)] [string] $TargetRelativePath,
        [Parameter(Mandatory)] [string] $ExpectedPatchTargetRelativePath,
        [Parameter(Mandatory)] [string] $ExpectedBeforeSha256,
        [Parameter(Mandatory)] [string] $ExpectedAfterSha256
    )
    return & $script:module {
        param($arguments)
        Invoke-M0CopyAndApplySingleTargetPatch @arguments
    } $PSBoundParameters
}

function Write-TestPeX64 {
    param([Parameter(Mandatory)] [string] $LiteralPath)
    $bytes = [byte[]]::new(512)
    $bytes[0] = 0x4d
    $bytes[1] = 0x5a
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3c)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($bytes, 0x84)
    Write-TestBytes $LiteralPath $bytes
}

function New-TestPhysicalReference {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $LiteralPath
    )
    $item = Get-Item -LiteralPath $LiteralPath -Force
    return [pscustomobject][ordered]@{
        relative_path = [IO.Path]::GetRelativePath($RunRoot, $item.FullName).Replace('\', '/')
        size_bytes = [long]$item.Length
        sha256 = Get-TestSha256File $item.FullName
    }
}

function New-TestTreeObservation {
    param(
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [string] $Id,
        [Parameter(Mandatory)] [string] $RootRelativePath,
        [Parameter(Mandatory)] [string] $InventoryRelativePath
    )
    $treeRoot = Join-Path $RunRoot $RootRelativePath
    $inventoryPath = Join-Path $RunRoot $InventoryRelativePath
    $summary = New-M0CanonicalTreeInventory -TreeRoot $treeRoot -InventoryPath $inventoryPath
    return [pscustomobject][ordered]@{
        id = $Id
        root_relative_path = $RootRelativePath
        file_count = [long]$summary.file_count
        total_size_bytes = [long]$summary.total_bytes
        inventory = New-TestPhysicalReference $RunRoot $inventoryPath
    }
}

function New-NativeRunnerFixture {
    param([Parameter(Mandatory)] [string] $Name)
    $root = Join-Path $testRoot $Name
    $runRoot = Join-Path $root 'run'
    $toolRoot = Join-Path $root 'tools'
    [IO.Directory]::CreateDirectory($runRoot) | Out-Null
    [IO.Directory]::CreateDirectory($toolRoot) | Out-Null

    $targetRelativePath = 'src/3rdparty/freetype/src/truetype/ttgxvar.c'
    $beforeBytes = [Text.UTF8Encoding]::new($false).GetBytes("before`n")
    $afterBytes = [Text.UTF8Encoding]::new($false).GetBytes("after`r`n")
    Write-TestBytes (Join-Path $runRoot "source/qtbase-before/$targetRelativePath") $beforeBytes
    Write-TestBytes (Join-Path $runRoot "source/qtbase-patched/$targetRelativePath") $afterBytes
    $configureBytes = [Text.UTF8Encoding]::new($false).GetBytes("@echo off`r`n")
    Write-TestBytes (Join-Path $runRoot 'source/qtbase-before/configure.bat') $configureBytes
    Write-TestBytes (Join-Path $runRoot 'source/qtbase-patched/configure.bat') $configureBytes
    Write-TestBytes (Join-Path $runRoot 'source/vulkan/include/vulkan/vulkan_core.h') ([byte[]](1, 2, 3))
    Write-TestBytes (Join-Path $runRoot 'source/vulkan/LICENSE.txt') ([byte[]](4, 5, 6))
    $trees = @(
        New-TestTreeObservation $runRoot 'qtbase-before' 'source/qtbase-before' 'evidence/inventories/qtbase-before.tsv'
        New-TestTreeObservation $runRoot 'qtbase-patched' 'source/qtbase-patched' 'evidence/inventories/qtbase-patched.tsv'
        New-TestTreeObservation $runRoot 'vulkan' 'source/vulkan' 'evidence/inventories/vulkan.tsv'
    )

    $toolPaths = [ordered]@{
        cmd = Join-Path $toolRoot 'Windows/System32/cmd.exe'
        vsdevcmd = Join-Path $toolRoot 'Program Files/Microsoft Visual Studio/18/BuildTools/Common7/Tools/VsDevCmd.bat'
        cmake = Join-Path $toolRoot 'Program Files/CMake/bin/cmake.exe'
        ninja = Join-Path $toolRoot 'Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
        cl = Join-Path $toolRoot 'Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
        link = Join-Path $toolRoot 'Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/link.exe'
        rc = Join-Path $toolRoot 'Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe'
        mt = Join-Path $toolRoot 'Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/mt.exe'
        dumpbin = Join-Path $toolRoot 'Program Files/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe'
    }
    foreach ($path in $toolPaths.Values) { Write-TestBytes $path ([byte[]](0x4d, 0x5a, 1, 2)) }

    $expectedArtifacts = @(
        'bin/Qt6Core.dll', 'bin/Qt6Gui.dll', 'bin/Qt6Network.dll', 'bin/Qt6OpenGL.dll', 'bin/Qt6Widgets.dll',
        'plugins/generic/qtuiotouchplugin.dll', 'plugins/imageformats/qgif.dll', 'plugins/imageformats/qico.dll',
        'plugins/imageformats/qjpeg.dll', 'plugins/networkinformation/qnetworklistmanager.dll',
        'plugins/platforms/qoffscreen.dll', 'plugins/platforms/qwindows.dll',
        'plugins/styles/qmodernwindowsstyle.dll', 'plugins/tls/qcertonlybackend.dll', 'plugins/tls/qschannelbackend.dll'
    )
    $features = [pscustomobject][ordered]@{
        shared = $true; debug_and_release = $true; force_debug_info = $true
        bundled_freetype = $true; bundled_harfbuzz = $true; bundled_pcre2 = $true
        bundled_zlib = $true; bundled_png = $true; bundled_jpeg = $true
        opengl = 'dynamic'; vulkan = $true; directwrite = $true; directwrite3 = $true
        directwrite_colrv1 = $true; direct2d = $true; direct2d1_1 = $true
        accessibility = $true; systemtrayicon = $true; openssl_runtime = $false
        schannel = $true; intelcet = $true; msvc_obj_debug_info = $true
    }
    $commands = @(
        [pscustomobject][ordered]@{
            id = 'configure'; tool_id = 'cmd'; script_relative_path = 'source/qtbase-patched/configure.bat'
            arguments_binding = 'configuration.arguments'; working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/configure.stdout.log'; stderr_relative_path = 'evidence/logs/configure.stderr.log'
        },
        [pscustomobject][ordered]@{
            id = 'build'; tool_id = 'cmake'; script_relative_path = $null
            arguments_binding = 'configuration.build_arguments'; working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/build.stdout.log'; stderr_relative_path = 'evidence/logs/build.stderr.log'
        },
        [pscustomobject][ordered]@{
            id = 'install'; tool_id = 'cmake'; script_relative_path = $null
            arguments_binding = 'configuration.install_arguments'; working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/install.stdout.log'; stderr_relative_path = 'evidence/logs/install.stderr.log'
        }
    )
    $reports = @(
        [pscustomobject][ordered]@{ id = 'configure-summary'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'b/config.summary'; relative_path = 'evidence/reports/configure-summary.txt' },
        [pscustomobject][ordered]@{ id = 'cmake-cache'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'b/CMakeCache.txt'; relative_path = 'evidence/reports/cmake-cache.txt' },
        [pscustomobject][ordered]@{ id = 'qtgui-freetype-dependency'; producer_id = 'runner'; arguments = @('analyze-freetype-link-graph'); subject_relative_path = 'b/CMakeFiles/impl-RelWithDebInfo.ninja'; relative_path = 'evidence/reports/qtgui-freetype-dependency.txt' },
        [pscustomobject][ordered]@{ id = 'qt6gui-headers'; producer_id = 'dumpbin'; arguments = @('/nologo', '/headers'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-headers.txt' },
        [pscustomobject][ordered]@{ id = 'qt6gui-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-imports.txt' },
        [pscustomobject][ordered]@{ id = 'qt6gui-exports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/exports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-exports.txt' },
        [pscustomobject][ordered]@{ id = 'qminimal-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/plugins/platforms/qminimal.dll'; relative_path = 'evidence/reports/qminimal-imports.txt' }
    )
    $treeContract = @(
        [pscustomobject][ordered]@{ id = 'qtbase-before'; root_relative_path = 'source/qtbase-before'; inventory_relative_path = 'evidence/inventories/qtbase-before.tsv'; expected = [pscustomobject][ordered]@{ file_count = $trees[0].file_count; total_size_bytes = $trees[0].total_size_bytes; inventory_sha256 = $trees[0].inventory.sha256 } },
        [pscustomobject][ordered]@{ id = 'qtbase-patched'; root_relative_path = 'source/qtbase-patched'; inventory_relative_path = 'evidence/inventories/qtbase-patched.tsv'; expected = [pscustomobject][ordered]@{ file_count = $trees[1].file_count; total_size_bytes = $trees[1].total_size_bytes; inventory_sha256 = $trees[1].inventory.sha256 } },
        [pscustomobject][ordered]@{ id = 'vulkan'; root_relative_path = 'source/vulkan'; inventory_relative_path = 'evidence/inventories/vulkan.tsv'; expected = [pscustomobject][ordered]@{ file_count = $trees[2].file_count; total_size_bytes = $trees[2].total_size_bytes; inventory_sha256 = $trees[2].inventory.sha256 } },
        [pscustomobject][ordered]@{ id = 'install'; root_relative_path = 'install'; inventory_relative_path = 'evidence/inventories/install.tsv'; expected = $null }
    )
    $contract = [pscustomobject][ordered]@{
        schema_version = [long]2
        refresh_id = 'native-test-v2'
        product = [pscustomobject][ordered]@{ qt_version = '6.11.2'; architecture = 'x86_64'; compiler_abi = 'msvc2022_64' }
        recipe = [pscustomobject][ordered]@{ files = @(); runner_export = 'Invoke-M0QtBaseRefreshNative' }
        inputs = [pscustomobject][ordered]@{
            qtbase_source = [pscustomobject][ordered]@{ relative_path = 'qt.tar.gz'; size_bytes = [long]1; sha256 = '1' * 64; commit_sha = '1' * 40 }
            freetype_patch = [pscustomobject][ordered]@{
                relative_path = 'patch.diff'; size_bytes = [long]1; sha256 = '2' * 64
                target_relative_path = $targetRelativePath
                target_before_sha256 = Get-TestSha256Bytes $beforeBytes
                target_after_sha256 = Get-TestSha256Bytes $afterBytes
            }
            vulkan_headers = [pscustomobject][ordered]@{
                relative_path = 'vulkan.tar.gz'; size_bytes = [long]1; sha256 = '3' * 64; commit_sha = '3' * 40
                header_relative_path = 'include/vulkan/vulkan_core.h'; header_sha256 = Get-TestSha256File (Join-Path $runRoot 'source/vulkan/include/vulkan/vulkan_core.h')
                license_relative_path = 'LICENSE.txt'; license_sha256 = Get-TestSha256File (Join-Path $runRoot 'source/vulkan/LICENSE.txt')
            }
        }
        tools = @()
        environment = [pscustomobject][ordered]@{ forbidden_variables = @('CL', '_CL_', 'LINK', '_LINK_', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS') }
        isolation = [pscustomobject][ordered]@{ run_parent_relative_path = 'unused'; require_new_direct_child = $true; receipt_relative_path = 'evidence/receipt.v2.json' }
        trees = $treeContract
        commands = $commands
        reports = $reports
        configuration = [pscustomobject][ordered]@{
            generator = 'Ninja Multi-Config'
            arguments = @('-prefix', '${INSTALL_ROOT}', '-DVulkan_INCLUDE_DIR=${VULKAN_INCLUDE}', '-DCMAKE_MAKE_PROGRAM=${NINJA}', '-DCMAKE_C_COMPILER=${CL}', '-DCMAKE_CXX_COMPILER=${CL}', '-DCMAKE_RC_COMPILER=${RC}', '-DCMAKE_MT=${MT}')
            build_arguments = @('--build', '${BUILD_ROOT}', '--config', 'RelWithDebInfo')
            install_arguments = @('--install', '${BUILD_ROOT}', '--config', 'RelWithDebInfo')
            installed_configuration = 'RelWithDebInfo'
            expected_artifacts = $expectedArtifacts
            expected_features = $features
        }
    }
    $prepared = [pscustomobject]@{
        patch = [pscustomobject][ordered]@{
            changed_relative_paths = @($targetRelativePath)
            target_relative_path = $targetRelativePath
            target_before_sha256 = $contract.inputs.freetype_patch.target_before_sha256
            target_after_sha256 = $contract.inputs.freetype_patch.target_after_sha256
        }
        trees = $trees
    }
    return [pscustomobject]@{
        Root = $root
        RunRoot = $runRoot
        ToolRoot = $toolRoot
        ToolPaths = [pscustomobject]$toolPaths
        Contract = $contract
        Prepared = $prepared
        ExpectedArtifacts = $expectedArtifacts
        Context = [pscustomobject][ordered]@{
            contract_sha256 = '4' * 64
            contract = $contract
            run_root = $runRoot
            paths = [pscustomobject][ordered]@{ qtbase_source = 'unused-qt'; freetype_patch = 'unused-patch'; vulkan_headers = 'unused-vulkan' }
            tool_paths = [pscustomobject]$toolPaths
            recipe_paths = [pscustomobject][ordered]@{ orchestrator = 'unused'; runner = $modulePath; entrypoint = 'unused' }
        }
    }
}

function New-FakeNativeProcessBoundary {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)] $State,
        [string] $FailId,
        [string] $TimeoutId,
        [switch] $InvalidEnvironment,
        [switch] $PathBudgetWarning,
        [switch] $InstallHardlinkAliases,
        [switch] $LockInstallHardlinkAlias
    )
    $configureSummary = $expectedNativeConfigureSummary
    $cmakeCache = $expectedNativeCMakeCache
    $boundary = {
        param($request)
        $State.Calls.Add($request)
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($request.stdout_path)) | Out-Null
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($request.stderr_path)) | Out-Null
        if ($request.id -ceq 'vsdevcmd-environment') {
            $clDirectory = [IO.Path]::GetDirectoryName($Fixture.ToolPaths.cl)
            $sdkDirectory = [IO.Path]::GetDirectoryName($Fixture.ToolPaths.rc)
            $cmakeDirectory = [IO.Path]::GetDirectoryName($Fixture.ToolPaths.cmake)
            $version = if ($InvalidEnvironment) { '14.43.00000' } else { '14.44.35207' }
            $text = @(
                "PATH=$clDirectory;$sdkDirectory;$cmakeDirectory"
                'PATHEXT=.EXE;.BAT;.CMD'
                'VSCMD_ARG_HOST_ARCH=x64'
                'VSCMD_ARG_TGT_ARCH=x64'
                "VCToolsVersion=$version"
                'WindowsSDKVersion=10.0.26100.0\'
                "VCToolsInstallDir=$([IO.Path]::GetFullPath((Join-Path $clDirectory '../../..')))\"
                "WindowsSdkDir=$([IO.Path]::GetFullPath((Join-Path $sdkDirectory '../../..')))\"
            ) -join "`r`n"
            [IO.File]::WriteAllText($request.stdout_path, $text + "`r`n", [Text.UTF8Encoding]::new($false))
            [IO.File]::WriteAllBytes($request.stderr_path, [byte[]]::new(0))
            return [pscustomobject]@{ exit_code = [long]0; timed_out = $false }
        }

        [IO.File]::WriteAllText($request.stdout_path, "$($request.id) stdout`r`n", [Text.UTF8Encoding]::new($false))
        $stderrText = if ($PathBudgetWarning -and $request.id -ceq 'configure') {
            @(
                'CMake Warning in src/platformsupport/devicediscovery/CMakeLists.txt:'
                '  The object file directory has 209 characters.'
                '  The maximum full path to an object file is 250 characters (see CMAKE_OBJECT_PATH_MAX).'
                '  Object file cannot be safely placed under this directory.'
            ) -join "`r`n"
        }
        else { "$($request.id) stderr" }
        [IO.File]::WriteAllText($request.stderr_path, $stderrText + "`r`n", [Text.UTF8Encoding]::new($false))
        if ($request.id -ceq $TimeoutId) { return [pscustomobject]@{ exit_code = [long]-1; timed_out = $true } }
        if ($request.id -ceq $FailId) { return [pscustomobject]@{ exit_code = [long]7; timed_out = $false } }

        if ($request.id -ceq 'configure') {
            Write-TestBytes (Join-Path $Fixture.RunRoot 'b/config.summary') ([Text.UTF8Encoding]::new($false).GetBytes($configureSummary))
            Write-TestBytes (Join-Path $Fixture.RunRoot 'b/CMakeCache.txt') ([Text.UTF8Encoding]::new($false).GetBytes($cmakeCache))
            Write-TestBytes (Join-Path $Fixture.RunRoot 'b/CMakeFiles/impl-RelWithDebInfo.ninja') ([Text.UTF8Encoding]::new($false).GetBytes(
                    "build bin\Qt6Gui.dll lib\Qt6Gui.lib: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo gui.obj | lib\Qt6BundledFreetype.lib`n" +
                    "  LINK_LIBRARIES = d3d11.lib lib\Qt6BundledFreetype.lib`n" +
                    "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo qminimal.obj | lib\Qt6BundledFreetype.lib`n" +
                    "  LINK_LIBRARIES = lib\Qt6Gui.lib lib\Qt6BundledFreetype.lib`n"))
        }
        elseif ($request.id -ceq 'install') {
            foreach ($relative in $Fixture.ExpectedArtifacts) { Write-TestPeX64 (Join-Path $Fixture.RunRoot "install/$relative") }
            Write-TestPeX64 (Join-Path $Fixture.RunRoot 'install/plugins/platforms/qminimal.dll')
            if ($InstallHardlinkAliases) {
                $primary = Join-Path $Fixture.RunRoot 'install/bin/androiddeployqt.exe'
                $alias = Join-Path $Fixture.RunRoot 'install/bin/androiddeployqt6.exe'
                Write-TestPeX64 $primary
                $null = New-Item -ItemType HardLink -Path $alias -Target $primary
                if ($LockInstallHardlinkAlias) {
                    $State.LockedHandle = [IO.FileStream]::new(
                        $primary, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
                }
            }
        }
        elseif ($request.id.StartsWith('report:', [StringComparison]::Ordinal)) {
            $reportId = $request.id.Substring('report:'.Length)
            $markers = switch ($reportId) {
                'qt6gui-headers' { "Dump of file Qt6Gui.dll`r`nPE signature found`r`nFile Type: DLL`r`nFILE HEADER VALUES`r`n8664 machine (x64)`r`n20B magic # (PE32+)`r`n" }
                'qt6gui-imports' { "Dump of file Qt6Gui.dll`r`nFile Type: DLL`r`nSection contains the following imports:`r`nQt6Core.dll`r`nDWrite.dll`r`n" }
                'qt6gui-exports' { "Dump of file Qt6Gui.dll`r`nFile Type: DLL`r`nSection contains the following exports for Qt6Gui.dll`r`nordinal hint RVA name`r`n      1    0 00001000 ??0QColor@@QEAA@HHHH@Z`r`n" }
                'qminimal-imports' { "Dump of file qminimal.dll`r`nFile Type: DLL`r`nSection contains the following imports:`r`nQt6Gui.dll`r`nQt6Core.dll`r`n" }
                default { throw "Unexpected fake report id '$reportId'." }
            }
            [IO.File]::WriteAllText($request.stdout_path, $markers, [Text.UTF8Encoding]::new($false))
            [IO.File]::WriteAllBytes($request.stderr_path, [byte[]]::new(0))
        }
        return [pscustomobject]@{ exit_code = [long]0; timed_out = $false }
    }.GetNewClosure()
    return $boundary
}

function Set-NativeRunnerTestBoundaries {
    param([Parameter(Mandatory)] $Prepared, [Parameter(Mandatory)] [scriptblock] $ProcessBoundary)
    $preparationBoundary = { param($context) return $Prepared }.GetNewClosure()
    & $script:module {
        param($prepare, $process)
        $script:NativeFixturePreparationCallback = $prepare
        $script:NativeFixtureProcessCallback = $process
    } $preparationBoundary $ProcessBoundary
}

function Clear-NativeRunnerTestBoundaries {
    & $script:module {
        $script:NativeFixturePreparationCallback = $null
        $script:NativeFixtureProcessCallback = $null
    }
}

function Assert-NativeFreetypeSubjectRejected {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [string] $Text
    )
    $root = Join-Path $testRoot ('freetype-ninja-' + $Name)
    $subject = Join-Path $root 'CMakeFiles/impl-RelWithDebInfo.ninja'
    $report = Join-Path $root 'evidence/qtgui-freetype-dependency.txt'
    Write-TestBytes $subject ([Text.UTF8Encoding]::new($false).GetBytes($Text))
    Assert-Rejected {
        & $script:module {
            param($subjectPath, $reportPath, $runRoot)
            Write-M0NativeFreetypeDependencyReport $subjectPath $reportPath $runRoot
        } $subject $report $root
    } 'FreeType|Ninja|edge|LINK_LIBRARIES|rule|dependency'
    Assert-True (-not [IO.File]::Exists($report)) "Rejected Ninja mutant '$Name' published a report."
}

try {
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null

    Test-Case 'module exports only the contract-pinned native runner entrypoint' {
        $exports = @(Get-Command -Module $module.Name -CommandType Function | Sort-Object Name | Select-Object -ExpandProperty Name)
        Assert-Equal $exports @('Invoke-M0QtBaseRefreshNative') 'Native module export surface drifted.'
        $command = Get-Command -Name Invoke-M0QtBaseRefreshNative -Module $module.Name
        Assert-True $command.Parameters['Context'].Attributes.Mandatory 'Native runner Context parameter must remain mandatory.'
    }

    Test-Case 'production native source contains no mutable callback dispatch seams' {
        $source = [IO.File]::ReadAllText($modulePath)
        Assert-True (-not $source.Contains('M0NativeProcessBoundary', [StringComparison]::Ordinal)) `
            'Production source retained the mutable process callback seam.'
        Assert-True (-not $source.Contains('M0NativePreparationBoundary', [StringComparison]::Ordinal)) `
            'Production source retained the mutable preparation callback seam.'
        Assert-True (-not [regex]::IsMatch($source, '&\s*\$script:[A-Za-z0-9_]*Boundary')) `
            'Production source retained scriptblock dispatch through a mutable script-scope boundary.'
    }

    Test-Case 'native file identity ignores a hostile preloaded fixed namespace and supports reimport' {
        if (-not $IsWindows) { return }
        $root = Join-Path $testRoot 'hostile-native-namespace'
        $probePath = Join-Path $root 'probe.ps1'
        $probe = @'
param([string] $ModulePath, [string] $Root)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @"
namespace M0QtBaseRefreshTreeNativeV1 { public static class FileIdentity { } }
"@
function Invoke-IdentityProbe {
    param([string] $InventoryName)
    $module = Import-Module -Name $ModulePath -Force -PassThru
    try {
        $tree = Join-Path $Root ('tree-' + $InventoryName)
        [IO.Directory]::CreateDirectory($tree) | Out-Null
        [IO.File]::WriteAllText((Join-Path $tree 'leaf.txt'), 'identity')
        $inventory = Join-Path $Root ($InventoryName + '.tsv')
        $result = & $module {
            param($treeRoot, $inventoryPath)
            New-M0CanonicalTreeInventory -TreeRoot $treeRoot -InventoryPath $inventoryPath
        } $tree $inventory
        if ($result.file_count -ne 1 -or -not [IO.File]::Exists($inventory)) { throw 'identity probe failed' }
    }
    finally { Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue }
}
Invoke-IdentityProbe 'first'
Invoke-IdentityProbe 'second'
'OK'
'@
        Write-TestBytes $probePath ([Text.UTF8Encoding]::new($false).GetBytes($probe))
        $output = @(& (Get-Process -Id $PID).Path -NoLogo -NoProfile -File $probePath `
                -ModulePath $modulePath -Root $root 2>&1)
        Assert-Equal $LASTEXITCODE 0 "Fresh-process hostile namespace probe failed: $($output -join ' | ')"
        Assert-True ($output -contains 'OK') 'Fresh-process hostile namespace probe did not complete both imports.'
    }

    Test-Case 'process-tree teardown is bounded when Kill does not complete the wait task' {
        $completedProcess = [pscustomobject]@{ kill_calls = 0 }
        $completedProcess | Add-Member -MemberType ScriptMethod -Name Kill -Value {
            param([bool] $EntireProcessTree)
            $this.kill_calls++
            throw 'Kill must not run after process exit.'
        }
        & $script:module {
            param($process, $waitTask)
            Stop-M0NativeProcessTree -Process $process -WaitTask $waitTask -TimeoutMilliseconds 100
        } $completedProcess ([Threading.Tasks.Task]::CompletedTask)
        Assert-Equal $completedProcess.kill_calls 0 'Teardown attempted to kill a process whose wait task was already complete.'

        $never = [Threading.Tasks.TaskCompletionSource[object]]::new(
            [Threading.Tasks.TaskCreationOptions]::RunContinuationsAsynchronously)
        $fakeProcess = [pscustomobject]@{ kill_calls = 0 }
        $fakeProcess | Add-Member -MemberType ScriptMethod -Name Kill -Value {
            param([bool] $EntireProcessTree)
            $this.kill_calls++
        }
        $watch = [Diagnostics.Stopwatch]::StartNew()
        Assert-Rejected {
            & $script:module {
                param($process, $waitTask)
                Stop-M0NativeProcessTree -Process $process -WaitTask $waitTask -TimeoutMilliseconds 100
            } $fakeProcess $never.Task
        } 'terminate|timeout|deadline|process tree'
        $watch.Stop()
        Assert-Equal $fakeProcess.kill_calls 1 'Bounded teardown did not attempt one process-tree kill.'
        Assert-True ($watch.ElapsedMilliseconds -lt 2000) 'Ignored Kill caused teardown to block beyond its strict test deadline.'
    }

    Test-Case 'native log tail reads at most the final 8192 bytes without ReadAllBytes' {
        $root = Join-Path $testRoot 'large-log-tail'
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $logPath = Join-Path $root 'large.log'
        $stream = [IO.FileStream]::new($logPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        try {
            $block = [byte[]]::new(1024 * 1024)
            [Array]::Fill[byte]($block, [byte][char]'x')
            for ($index = 0; $index -lt 16; $index++) { $stream.Write($block, 0, $block.Length) }
            $tailMarker = [Text.Encoding]::UTF8.GetBytes("TAIL-MARKER`n")
            $stream.Write($tailMarker, 0, $tailMarker.Length)
        }
        finally { $stream.Dispose() }
        $tail = & $script:module { param($path) Read-M0NativeLogTail $path } $logPath
        Assert-True ($tail.EndsWith("TAIL-MARKER`n", [StringComparison]::Ordinal)) 'Log tail omitted the final marker.'
        Assert-True ([Text.Encoding]::UTF8.GetByteCount($tail) -le 8192) 'Log tail returned more than 8192 bytes.'

        $tokens = $null
        $errors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($modulePath, [ref]$tokens, [ref]$errors)
        Assert-Equal @($errors).Count 0 'Production module did not parse during the log-tail static guard.'
        $functionAst = $ast.Find({
                param($node)
                $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -ceq 'Read-M0NativeLogTail'
            }, $true)
        Assert-True ($null -ne $functionAst) 'Read-M0NativeLogTail AST was not found.'
        Assert-True (-not $functionAst.Extent.Text.Contains('ReadAllBytes', [StringComparison]::Ordinal)) `
            'Read-M0NativeLogTail still materializes the entire log.'
        Assert-True ($functionAst.Extent.Text.Contains('FileStream', [StringComparison]::Ordinal)) `
            'Read-M0NativeLogTail does not use bounded FileStream I/O.'
    }

    Test-Case 'canonical inventory is UTF-8 LF ordinal and binds every physical file' {
        $root = Join-Path $testRoot 'canonical-tree'
        Write-TestBytes (Join-Path $root 'z.txt') ([Text.UTF8Encoding]::new($false).GetBytes("zulu`n"))
        Write-TestBytes (Join-Path $root 'A/a.txt') ([Text.UTF8Encoding]::new($false).GetBytes("alpha`n"))
        $inventoryPath = Join-Path $testRoot 'canonical-inventory.tsv'
        $result = New-M0CanonicalTreeInventory -TreeRoot $root -InventoryPath $inventoryPath

        $aBytes = [IO.File]::ReadAllBytes((Join-Path $root 'A/a.txt'))
        $zBytes = [IO.File]::ReadAllBytes((Join-Path $root 'z.txt'))
        $expected = "A/a.txt`t$($aBytes.Length)`t$(Get-TestSha256Bytes $aBytes)`n" +
            "z.txt`t$($zBytes.Length)`t$(Get-TestSha256Bytes $zBytes)`n"
        $actualBytes = [IO.File]::ReadAllBytes($inventoryPath)
        Assert-Equal ([Text.UTF8Encoding]::new($false, $true).GetString($actualBytes)) $expected
        Assert-True (-not ($actualBytes -contains 13)) 'Canonical inventory contains CR bytes.'
        Assert-Equal $result.file_count ([long]2)
        Assert-Equal $result.total_bytes ([long]($aBytes.Length + $zBytes.Length))
        Assert-Equal $result.sha256 (Get-TestSha256Bytes ([Text.UTF8Encoding]::new($false).GetBytes($expected)))

        $verified = Assert-M0TreeMatchesInventory `
            -TreeRoot $root `
            -InventoryPath $inventoryPath `
            -ExpectedFileCount 2 `
            -ExpectedTotalBytes ($aBytes.Length + $zBytes.Length) `
            -ExpectedInventorySha256 $result.sha256
        Assert-Equal $verified.sha256 $result.sha256

        [IO.File]::AppendAllText((Join-Path $root 'z.txt'), 'tamper', [Text.UTF8Encoding]::new($false))
        Assert-Rejected {
            Assert-M0TreeMatchesInventory `
                -TreeRoot $root `
                -InventoryPath $inventoryPath `
                -ExpectedFileCount 2 `
                -ExpectedTotalBytes ($aBytes.Length + $zBytes.Length) `
                -ExpectedInventorySha256 $result.sha256
        } 'inventory|tree|physical|mismatch|hash|size'
    }

    Test-Case 'physical inventory supports long Windows paths and still rejects alternate data streams' {
        $root = Join-Path $testRoot 'long-path-tree'
        $leafParent = $root
        foreach ($segment in @(
                'segment-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa',
                'segment-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb',
                'segment-cccccccccccccccccccccccccccccccc',
                'segment-dddddddddddddddddddddddddddddddd',
                'segment-eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee'
            )) {
            $leafParent = Join-Path $leafParent $segment
        }
        $leaf = Join-Path $leafParent 'leaf.txt'
        $leafBytes = [Text.UTF8Encoding]::new($false).GetBytes("long-path`n")
        Write-TestBytes $leaf $leafBytes
        Assert-True ($leaf.Length -gt 260) 'Regression fixture did not exceed the legacy Windows MAX_PATH boundary.'

        $inventoryPath = Join-Path $testRoot 'long-path.tsv'
        $result = New-M0CanonicalTreeInventory -TreeRoot $root -InventoryPath $inventoryPath
        Assert-Equal $result.file_count ([long]1)
        Assert-Equal $result.total_bytes ([long]$leafBytes.Length)

        [IO.File]::WriteAllBytes($leaf + ':m0-probe', [byte[]](1, 2, 3))
        Assert-Rejected {
            New-M0CanonicalTreeInventory `
                -TreeRoot $root `
                -InventoryPath (Join-Path $testRoot 'long-path-ads.tsv')
        } 'alternate data stream|ADS'
    }

    Test-Case 'inventory rejects noncanonical text and physical hardlinks or reparse points' {
        $root = Join-Path $testRoot 'unsafe-tree'
        Write-TestBytes (Join-Path $root 'leaf.txt') ([byte[]](1, 2, 3))
        if ($IsWindows) {
            $null = New-Item -ItemType HardLink -Path (Join-Path $root 'leaf-alias.txt') -Target (Join-Path $root 'leaf.txt')
            Assert-Rejected {
                New-M0CanonicalTreeInventory -TreeRoot $root -InventoryPath (Join-Path $testRoot 'hardlink.tsv')
            } 'hardlink|link count|physical'
            [IO.File]::Delete((Join-Path $root 'leaf-alias.txt'))

            $outside = Join-Path $testRoot 'outside-tree'
            [IO.Directory]::CreateDirectory($outside) | Out-Null
            $null = New-Item -ItemType Junction -Path (Join-Path $root 'junction') -Target $outside
            Assert-Rejected {
                New-M0CanonicalTreeInventory -TreeRoot $root -InventoryPath (Join-Path $testRoot 'junction.tsv')
            } 'reparse|junction|physical'
        }

        $clean = Join-Path $testRoot 'noncanonical-tree'
        Write-TestBytes (Join-Path $clean 'a.txt') ([byte[]](4, 5, 6))
        $inventoryPath = Join-Path $testRoot 'noncanonical.tsv'
        $line = "a.txt`t3`t$(Get-TestSha256File (Join-Path $clean 'a.txt'))`r`n"
        [IO.File]::WriteAllText($inventoryPath, $line, [Text.UTF8Encoding]::new($false))
        Assert-Rejected {
            Assert-M0TreeMatchesInventory `
                -TreeRoot $clean `
                -InventoryPath $inventoryPath `
                -ExpectedFileCount 1 `
                -ExpectedTotalBytes 3 `
                -ExpectedInventorySha256 (Get-TestSha256File $inventoryPath)
        } 'canonical|LF|CR|inventory'
    }

    Test-Case 'authenticated tar.gz extracts one anchored top-level tree and emits its canonical inventory' {
        $fixture = New-ValidArchiveFixture 'valid-archive'
        $destination = Join-Path $fixture.Root 'tree'
        $inventoryPath = Join-Path $fixture.Root 'tree.tsv'
        $result = Expand-M0AuthenticatedTarGz `
            -ArchivePath $fixture.ArchivePath `
            -DestinationRoot $destination `
            -InventoryPath $inventoryPath `
            -ExpectedArchiveSizeBytes $fixture.ArchiveSize `
            -ExpectedArchiveSha256 $fixture.ArchiveSha256 `
            -ExpectedTopLevelDirectory 'pkg' `
            -ExpectedGlobalPaxAttributes ([ordered]@{ comment = '0123456789abcdef' }) `
            -ExpectedArchiveEntryCount 4 `
            -ExpectedDirectoryCount 2 `
            -ExpectedFileCount 1 `
            -ExpectedTotalFileBytes 6 `
            -ExpectedInventorySha256 $fixture.ExpectedInventorySha256

        Assert-Equal ([IO.File]::ReadAllText((Join-Path $destination 'include/a.h'))) "alpha`n"
        Assert-Equal ([IO.File]::ReadAllText($inventoryPath)) $fixture.ExpectedInventoryText
        Assert-Equal $result.archive_entry_count ([long]4)
        Assert-Equal $result.directory_count ([long]2)
        Assert-Equal $result.file_count ([long]1)
        Assert-Equal $result.total_bytes ([long]6)
        Assert-Equal $result.sha256 $fixture.ExpectedInventorySha256
    }

    Test-Case 'authenticated tar.gz preserves zero-byte regular files with a null TarReader stream' {
        $root = Join-Path $testRoot 'zero-byte-archive'
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $archive = Join-Path $root 'zero.tar.gz'
        New-TestTarGz -LiteralPath $archive -Entries @(
            [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/' },
            [pscustomobject]@{ Kind = 'file'; Name = 'pkg/empty.cpp'; Content = '' }
        ) -GlobalAttributes ([ordered]@{ comment = 'zero' })
        $emptyHash = Get-TestSha256Bytes ([byte[]]::new(0))
        $inventoryText = "empty.cpp`t0`t$emptyHash`n"
        $inventoryHash = Get-TestSha256Bytes ([Text.UTF8Encoding]::new($false).GetBytes($inventoryText))
        $result = Expand-M0AuthenticatedTarGz `
            -ArchivePath $archive `
            -DestinationRoot (Join-Path $root 'tree') `
            -InventoryPath (Join-Path $root 'tree.tsv') `
            -ExpectedArchiveSizeBytes ([long](Get-Item -LiteralPath $archive).Length) `
            -ExpectedArchiveSha256 (Get-TestSha256File $archive) `
            -ExpectedTopLevelDirectory 'pkg' `
            -ExpectedGlobalPaxAttributes ([ordered]@{ comment = 'zero' }) `
            -ExpectedArchiveEntryCount 3 `
            -ExpectedDirectoryCount 1 `
            -ExpectedFileCount 1 `
            -ExpectedTotalFileBytes 0 `
            -ExpectedInventorySha256 $inventoryHash
        Assert-Equal $result.total_bytes ([long]0)
        Assert-True ([IO.File]::Exists((Join-Path $root 'tree/empty.cpp'))) 'Zero-byte file was not extracted.'
        Assert-Equal ([long](Get-Item -LiteralPath (Join-Path $root 'tree/empty.cpp')).Length) ([long]0)
    }

    Test-Case 'tar extraction rejects absolute traversal ADS links PAX drift and case collisions' {
        $mutants = @(
            [pscustomobject]@{ Name = 'absolute'; Entry = [pscustomobject]@{ Kind = 'file'; Name = '/absolute.txt'; Content = 'x' }; Pax = [ordered]@{ comment = 'ok' } },
            [pscustomobject]@{ Name = 'traversal'; Entry = [pscustomobject]@{ Kind = 'file'; Name = 'pkg/../escape.txt'; Content = 'x' }; Pax = [ordered]@{ comment = 'ok' } },
            [pscustomobject]@{ Name = 'ads'; Entry = [pscustomobject]@{ Kind = 'file'; Name = 'pkg/file.txt:stream'; Content = 'x' }; Pax = [ordered]@{ comment = 'ok' } },
            [pscustomobject]@{ Name = 'symlink'; Entry = [pscustomobject]@{ Kind = 'symlink'; Name = 'pkg/link'; LinkName = '../escape' }; Pax = [ordered]@{ comment = 'ok' } },
            [pscustomobject]@{ Name = 'hardlink'; Entry = [pscustomobject]@{ Kind = 'hardlink'; Name = 'pkg/link'; LinkName = 'pkg/file.txt' }; Pax = [ordered]@{ comment = 'ok' } },
            [pscustomobject]@{ Name = 'pax'; Entry = [pscustomobject]@{ Kind = 'file'; Name = 'pkg/file.txt'; Content = 'x' }; Pax = [ordered]@{ unexpected = 'drift' } }
        )
        foreach ($mutant in $mutants) {
            $root = Join-Path $testRoot ('tar-mutant-' + $mutant.Name)
            [IO.Directory]::CreateDirectory($root) | Out-Null
            $archive = Join-Path $root 'mutant.tar.gz'
            New-TestTarGz -LiteralPath $archive -Entries @(
                [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/' },
                $mutant.Entry
            ) -GlobalAttributes $mutant.Pax
            $archiveSize = [long](Get-Item -LiteralPath $archive).Length
            $destination = Join-Path $root 'tree'
            $inventory = Join-Path $root 'tree.tsv'
            Assert-Rejected {
                Expand-M0AuthenticatedTarGz `
                    -ArchivePath $archive `
                    -DestinationRoot $destination `
                    -InventoryPath $inventory `
                    -ExpectedArchiveSizeBytes $archiveSize `
                    -ExpectedArchiveSha256 (Get-TestSha256File $archive) `
                    -ExpectedTopLevelDirectory 'pkg' `
                    -ExpectedGlobalPaxAttributes ([ordered]@{ comment = 'ok' }) `
                    -ExpectedArchiveEntryCount 3 `
                    -ExpectedDirectoryCount 1 `
                    -ExpectedFileCount 1 `
                    -ExpectedTotalFileBytes 1 `
                    -ExpectedInventorySha256 ('0' * 64)
            } 'tar|archive|path|absolute|traversal|parent|ADS|colon|link|PAX|attribute|type|safe'
            Assert-True (-not [IO.Directory]::Exists($destination)) "Rejected tar mutant '$($mutant.Name)' left a partial tree."
            Assert-True (-not [IO.File]::Exists($inventory)) "Rejected tar mutant '$($mutant.Name)' left a partial inventory."
        }

        $caseRoot = Join-Path $testRoot 'tar-case-collision'
        [IO.Directory]::CreateDirectory($caseRoot) | Out-Null
        $caseArchive = Join-Path $caseRoot 'case.tar.gz'
        New-TestTarGz -LiteralPath $caseArchive -Entries @(
            [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/' },
            [pscustomobject]@{ Kind = 'file'; Name = 'pkg/A.txt'; Content = 'A' },
            [pscustomobject]@{ Kind = 'file'; Name = 'pkg/a.txt'; Content = 'a' }
        ) -GlobalAttributes ([ordered]@{ comment = 'ok' })
        Assert-Rejected {
            Expand-M0AuthenticatedTarGz `
                -ArchivePath $caseArchive `
                -DestinationRoot (Join-Path $caseRoot 'tree') `
                -InventoryPath (Join-Path $caseRoot 'tree.tsv') `
                -ExpectedArchiveSizeBytes ([long](Get-Item -LiteralPath $caseArchive).Length) `
                -ExpectedArchiveSha256 (Get-TestSha256File $caseArchive) `
                -ExpectedTopLevelDirectory 'pkg' `
                -ExpectedGlobalPaxAttributes ([ordered]@{ comment = 'ok' }) `
                -ExpectedArchiveEntryCount 4 `
                -ExpectedDirectoryCount 1 `
                -ExpectedFileCount 2 `
                -ExpectedTotalFileBytes 2 `
                -ExpectedInventorySha256 ('0' * 64)
        } 'case|collision|duplicate|path'
    }

    Test-Case 'tar extraction rejects per-entry PAX metadata and every mismatched anchor' {
        $paxRoot = Join-Path $testRoot 'tar-entry-pax'
        [IO.Directory]::CreateDirectory($paxRoot) | Out-Null
        $paxArchive = Join-Path $paxRoot 'entry-pax.tar.gz'
        New-TestTarGz -LiteralPath $paxArchive -Entries @(
            [pscustomobject]@{ Kind = 'directory'; Name = 'pkg/' },
            [pscustomobject]@{ Kind = 'pax-file'; Name = 'pkg/a.txt'; Content = 'x'; Attributes = [ordered]@{ note = 'unexpected' } }
        ) -GlobalAttributes ([ordered]@{ comment = 'ok' })
        Assert-Rejected {
            Expand-M0AuthenticatedTarGz `
                -ArchivePath $paxArchive `
                -DestinationRoot (Join-Path $paxRoot 'tree') `
                -InventoryPath (Join-Path $paxRoot 'tree.tsv') `
                -ExpectedArchiveSizeBytes ([long](Get-Item -LiteralPath $paxArchive).Length) `
                -ExpectedArchiveSha256 (Get-TestSha256File $paxArchive) `
                -ExpectedTopLevelDirectory 'pkg' `
                -ExpectedGlobalPaxAttributes ([ordered]@{ comment = 'ok' }) `
                -ExpectedArchiveEntryCount 3 `
                -ExpectedDirectoryCount 1 `
                -ExpectedFileCount 1 `
                -ExpectedTotalFileBytes 1 `
                -ExpectedInventorySha256 ('0' * 64)
        } 'PAX|extended|metadata|format'

        foreach ($anchor in @('archive-size', 'archive-hash', 'entries', 'directories', 'files', 'bytes', 'inventory')) {
            $fixture = New-ValidArchiveFixture ('anchor-' + $anchor)
            $arguments = @{
                ArchivePath = $fixture.ArchivePath
                DestinationRoot = Join-Path $fixture.Root 'tree'
                InventoryPath = Join-Path $fixture.Root 'tree.tsv'
                ExpectedArchiveSizeBytes = $fixture.ArchiveSize
                ExpectedArchiveSha256 = $fixture.ArchiveSha256
                ExpectedTopLevelDirectory = 'pkg'
                ExpectedGlobalPaxAttributes = [ordered]@{ comment = '0123456789abcdef' }
                ExpectedArchiveEntryCount = 4
                ExpectedDirectoryCount = 2
                ExpectedFileCount = 1
                ExpectedTotalFileBytes = 6
                ExpectedInventorySha256 = $fixture.ExpectedInventorySha256
            }
            switch ($anchor) {
                'archive-size' { $arguments.ExpectedArchiveSizeBytes++ }
                'archive-hash' { $arguments.ExpectedArchiveSha256 = '0' * 64 }
                'entries' { $arguments.ExpectedArchiveEntryCount++ }
                'directories' { $arguments.ExpectedDirectoryCount++ }
                'files' { $arguments.ExpectedFileCount++ }
                'bytes' { $arguments.ExpectedTotalFileBytes++ }
                'inventory' { $arguments.ExpectedInventorySha256 = '0' * 64 }
            }
            Assert-Rejected { Expand-M0AuthenticatedTarGz @arguments } 'archive|anchor|count|size|hash|digest|inventory|expected'
        }
    }

    Test-Case 'final extracted-inventory mismatch leaves no published tree or inventory' {
        $fixture = New-ValidArchiveFixture 'final-inventory-mismatch'
        $destination = Join-Path $fixture.Root 'tree'
        $inventory = Join-Path $fixture.Root 'tree.tsv'
        Assert-Rejected {
            Expand-M0AuthenticatedTarGz `
                -ArchivePath $fixture.ArchivePath `
                -DestinationRoot $destination `
                -InventoryPath $inventory `
                -ExpectedArchiveSizeBytes $fixture.ArchiveSize `
                -ExpectedArchiveSha256 $fixture.ArchiveSha256 `
                -ExpectedTopLevelDirectory 'pkg' `
                -ExpectedGlobalPaxAttributes ([ordered]@{ comment = '0123456789abcdef' }) `
                -ExpectedArchiveEntryCount 4 `
                -ExpectedDirectoryCount 2 `
                -ExpectedFileCount 1 `
                -ExpectedTotalFileBytes 6 `
                -ExpectedInventorySha256 ('0' * 64)
        } 'inventory|anchor|expected'
        Assert-True (-not [IO.Directory]::Exists($destination)) 'Rejected final inventory left a partial extracted tree.'
        Assert-True (-not [IO.File]::Exists($inventory)) 'Rejected final inventory left a partial inventory file.'
    }

    Test-Case 'single-target patch copies the tree and deliberately serializes the full target as CRLF' {
        $root = Join-Path $testRoot 'patch-green'
        $source = Join-Path $root 'before'
        $destination = Join-Path $root 'patched'
        $targetRelative = 'vendor/file.c'
        $beforeText = "alpha`nold one`nold two`nomega`n"
        $afterText = "alpha`r`nnew one`r`nnew two`r`nomega`r`n"
        $beforeBytes = [Text.UTF8Encoding]::new($false).GetBytes($beforeText)
        $afterBytes = [Text.UTF8Encoding]::new($false).GetBytes($afterText)
        Write-TestBytes (Join-Path $source $targetRelative) $beforeBytes
        Write-TestBytes (Join-Path $source 'untouched.txt') ([Text.UTF8Encoding]::new($false).GetBytes("same`n"))
        $patchPath = Join-Path $root 'change.patch'
        $patchText = @'
From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001
Subject: [PATCH] fixture
---
 file.c | 4 ++--
 1 file changed, 2 insertions(+), 2 deletions(-)

diff --git a/file.c b/file.c
index 111111111..222222222 100644
--- a/file.c
+++ b/file.c
@@ -1,4 +1,4 @@
 alpha
-old one
-old two
+new one
+new two
 omega
'@
        $patchBytes = [Text.UTF8Encoding]::new($false).GetBytes($patchText.Replace("`r`n", "`n") + "`n")
        Write-TestBytes $patchPath $patchBytes

        $result = Invoke-M0CopyAndApplySingleTargetPatch `
            -SourceTreeRoot $source `
            -DestinationTreeRoot $destination `
            -PatchPath $patchPath `
            -ExpectedPatchSizeBytes $patchBytes.Length `
            -ExpectedPatchSha256 (Get-TestSha256Bytes $patchBytes) `
            -TargetRelativePath $targetRelative `
            -ExpectedPatchTargetRelativePath 'file.c' `
            -ExpectedBeforeSha256 (Get-TestSha256Bytes $beforeBytes) `
            -ExpectedAfterSha256 (Get-TestSha256Bytes $afterBytes)

        Assert-Equal ([IO.File]::ReadAllBytes((Join-Path $destination $targetRelative))) $afterBytes
        Assert-Equal ([IO.File]::ReadAllBytes((Join-Path $source $targetRelative))) $beforeBytes
        Assert-Equal (Get-TestSha256File (Join-Path $destination 'untouched.txt')) (Get-TestSha256File (Join-Path $source 'untouched.txt'))
        Assert-Equal @($result.changed_relative_paths) @($targetRelative)
        Assert-Equal $result.target_before_sha256 (Get-TestSha256Bytes $beforeBytes)
        Assert-Equal $result.target_after_sha256 (Get-TestSha256Bytes $afterBytes)
        Assert-Equal $result.line_endings 'CRLF'

        $preexisting = Join-Path $root 'preexisting-destination'
        $sentinel = Join-Path $preexisting 'user-work.txt'
        Write-TestBytes $sentinel ([Text.UTF8Encoding]::new($false).GetBytes("preserve me`n"))
        $sentinelSha256 = Get-TestSha256File $sentinel
        Assert-Rejected {
            Invoke-M0CopyAndApplySingleTargetPatch `
                -SourceTreeRoot $source `
                -DestinationTreeRoot $preexisting `
                -PatchPath $patchPath `
                -ExpectedPatchSizeBytes $patchBytes.Length `
                -ExpectedPatchSha256 (Get-TestSha256Bytes $patchBytes) `
                -TargetRelativePath $targetRelative `
                -ExpectedPatchTargetRelativePath 'file.c' `
                -ExpectedBeforeSha256 (Get-TestSha256Bytes $beforeBytes) `
                -ExpectedAfterSha256 (Get-TestSha256Bytes $afterBytes)
        } 'destination|already exists|preexisting'
        Assert-True ([IO.File]::Exists($sentinel)) 'Rejected patch deleted a preexisting destination sentinel.'
        Assert-Equal (Get-TestSha256File $sentinel) $sentinelSha256 'Rejected patch changed preexisting user work.'
    }

    Test-Case 'single-target patch rejects target identity drift malformed hunks and multiple files' {
        foreach ($kind in @('before', 'after', 'target', 'hunk', 'new-start', 'multiple')) {
            $root = Join-Path $testRoot ('patch-red-' + $kind)
            $source = Join-Path $root 'before'
            Write-TestBytes (Join-Path $source 'vendor/file.c') ([Text.UTF8Encoding]::new($false).GetBytes("one`ntwo`n"))
            $patchPath = Join-Path $root 'change.patch'
            $patchText = "diff --git a/file.c b/file.c`n--- a/file.c`n+++ b/file.c`n@@ -1,2 +1,2 @@`n one`n-two`n+changed`n"
            if ($kind -eq 'target') { $patchText = $patchText.Replace('file.c', 'other.c') }
            if ($kind -eq 'hunk') { $patchText = $patchText.Replace('-two', '-not-two') }
            if ($kind -eq 'new-start') { $patchText = $patchText.Replace('+1,2 @@', '+999,2 @@') }
            if ($kind -eq 'multiple') {
                $patchText += "diff --git a/second.c b/second.c`n--- a/second.c`n+++ b/second.c`n@@ -1 +1 @@`n-old`n+new`n"
            }
            $patchBytes = [Text.UTF8Encoding]::new($false).GetBytes($patchText)
            Write-TestBytes $patchPath $patchBytes
            $expectedAfter = [Text.UTF8Encoding]::new($false).GetBytes("one`r`nchanged`r`n")
            $arguments = @{
                SourceTreeRoot = $source
                DestinationTreeRoot = Join-Path $root 'patched'
                PatchPath = $patchPath
                ExpectedPatchSizeBytes = $patchBytes.Length
                ExpectedPatchSha256 = Get-TestSha256Bytes $patchBytes
                TargetRelativePath = 'vendor/file.c'
                ExpectedPatchTargetRelativePath = 'file.c'
                ExpectedBeforeSha256 = Get-TestSha256File (Join-Path $source 'vendor/file.c')
                ExpectedAfterSha256 = Get-TestSha256Bytes $expectedAfter
            }
            if ($kind -eq 'before') { $arguments.ExpectedBeforeSha256 = '0' * 64 }
            if ($kind -eq 'after') { $arguments.ExpectedAfterSha256 = '0' * 64 }
            Assert-Rejected { Invoke-M0CopyAndApplySingleTargetPatch @arguments } 'patch|target|before|after|hash|hunk|single|multiple|context|identity'
        }
    }

    Test-Case 'copy failure removes only the partial destination owned by that invocation' {
        $root = Join-Path $testRoot 'partial-copy-cleanup'
        $source = Join-Path $root 'source'
        $destination = Join-Path $root 'partial-destination'
        Write-TestBytes (Join-Path $source 'leaf.txt') ([Text.UTF8Encoding]::new($false).GetBytes("source`n"))
        $sourceData = & $script:module {
            param($treeRoot)
            Get-M0NativeTreeData $treeRoot
        } $source
        & $script:module {
            $script:TestOriginalOpenPhysicalFile = ${function:Open-M0NativePhysicalFile}
            Set-Item -LiteralPath Function:Open-M0NativePhysicalFile -Value {
                param(
                    [string] $LiteralPath,
                    [string] $Context,
                    [string] $BoundaryRoot,
                    [Nullable[long]] $ExpectedSizeBytes,
                    [string] $ExpectedSha256,
                    [switch] $SkipReparseChain
                )
                if ($Context.StartsWith('Source tree copy', [StringComparison]::Ordinal)) {
                    throw 'synthetic copy failure after destination ownership'
                }
                return & $script:TestOriginalOpenPhysicalFile @PSBoundParameters
            }
        }
        try {
            Assert-Rejected {
                & $script:module {
                    param($treeData, $destinationRoot)
                    Copy-M0NativeTreeFromData $treeData $destinationRoot
                } $sourceData $destination
            } 'synthetic copy failure|copy|destination'
            Assert-True (-not [IO.Directory]::Exists($destination)) 'Owned partial copy destination survived rejection.'
            Assert-True ([IO.File]::Exists((Join-Path $source 'leaf.txt'))) 'Partial-copy cleanup changed the source tree.'
        }
        finally {
            & $script:module {
                Set-Item -LiteralPath Function:Open-M0NativePhysicalFile -Value $script:TestOriginalOpenPhysicalFile
                Remove-Variable -Name TestOriginalOpenPhysicalFile -Scope Script
            }
        }
    }

    Test-Case 'FreeType graph rejects dependency text that occurs only after Ninja comments' {
        Assert-NativeFreetypeSubjectRejected 'comment-only' (
            "build bin\Qt6Gui.dll lib\Qt6Gui.lib: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo gui.obj # lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = gui.lib # lib\Qt6BundledFreetype.lib`n" +
            "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo qminimal.obj # lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = gui.lib # lib\Qt6BundledFreetype.lib`n")
    }

    Test-Case 'FreeType graph rejects rule and dependency substring lookalikes' {
        Assert-NativeFreetypeSubjectRejected 'rule-and-token-lookalikes' (
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo_lib\Qt6BundledFreetype.lib gui.obj | lib\Qt6BundledFreetype.lib.fake`n" +
            "  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib.fake`n" +
            "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo_lib\Qt6BundledFreetype.lib qminimal.obj | prefixlib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = prefixlib\Qt6BundledFreetype.lib`n")
        Assert-NativeFreetypeSubjectRejected 'wrong-exact-rules' (
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__GuiLookalike_RelWithDebInfo gui.obj | lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`n" +
            "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPluginLookalike_RelWithDebInfo qminimal.obj | lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`n")
    }

    Test-Case 'FreeType graph rejects an edge without its associated LINK_LIBRARIES binding' {
        Assert-NativeFreetypeSubjectRejected 'missing-link-libraries' (
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo gui.obj | lib\Qt6BundledFreetype.lib`n" +
            "  FLAGS = /DGUI`n" +
            "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo qminimal.obj | lib\Qt6BundledFreetype.lib`n" +
            "  FLAGS = /DQMINIMAL`n")
    }

    Test-Case 'FreeType graph rejects LINK_LIBRARIES lookalikes and commented dependencies' {
        Assert-NativeFreetypeSubjectRejected 'link-libraries-lookalike' (
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo gui.obj | lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib.fake # lib\Qt6BundledFreetype.lib`n" +
            "build plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo qminimal.obj | lib\Qt6BundledFreetype.lib`n" +
            "  LINK_LIBRARIES = prefixlib\Qt6BundledFreetype.lib # lib\Qt6BundledFreetype.lib`n")
    }

    Test-Case 'native runner executes the closed v2 sequence and returns complete physical evidence' {
        $fixture = New-NativeRunnerFixture 'runner-green'
        $state = [pscustomobject]@{ Calls = [Collections.Generic.List[object]]::new() }
        $boundary = New-FakeNativeProcessBoundary $fixture $state
        Set-NativeRunnerTestBoundaries $fixture.Prepared $boundary
        $sentinelName = 'M0_NATIVE_CALLER_SENTINEL'
        $previousSentinel = [Environment]::GetEnvironmentVariable($sentinelName, 'Process')
        [Environment]::SetEnvironmentVariable($sentinelName, 'preserve-caller', 'Process')
        try {
            $result = Invoke-M0QtBaseRefreshNative -Context $fixture.Context
            Assert-Equal @($result.PSObject.Properties.Name) @('execution', 'patch', 'configuration', 'evidence') 'Runner observation shape drifted.'
            Assert-Equal $result.execution.exit_code ([long]0)
            Assert-Equal @($result.patch.changed_relative_paths) @($fixture.Contract.inputs.freetype_patch.target_relative_path)
            Assert-Equal @($result.evidence.trees).Count 4
            Assert-Equal @($result.evidence.commands | ForEach-Object id) @('configure', 'build', 'install')
            Assert-Equal @($result.evidence.reports | ForEach-Object id) @(
                'configure-summary', 'cmake-cache', 'qtgui-freetype-dependency',
                'qt6gui-headers', 'qt6gui-imports', 'qt6gui-exports', 'qminimal-imports'
            )
            Assert-Equal @($result.evidence.artifacts).Count 15
            Assert-Equal @($result.evidence.direct_consumers).Count 1
            $freetypeReport = Join-Path $fixture.RunRoot 'evidence/reports/qtgui-freetype-dependency.txt'
            Assert-Equal ([IO.File]::ReadAllText($freetypeReport)) (
                "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`n" +
                "plugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n") 'FreeType report was not canonical.'
            Assert-Equal ([IO.File]::ReadAllText((Join-Path $fixture.RunRoot 'b/config.summary'))) `
                $expectedNativeConfigureSummary 'Configure summary fixture drifted from the verified Qt output.'
            Assert-Equal ([IO.File]::ReadAllText((Join-Path $fixture.RunRoot 'evidence/reports/configure-summary.txt'))) `
                $expectedNativeConfigureSummary 'Configure summary report was not copied verbatim.'
            Assert-Equal ([IO.File]::ReadAllText((Join-Path $fixture.RunRoot 'b/CMakeCache.txt'))) `
                $expectedNativeCMakeCache 'CMake cache fixture drifted from the verified Qt output.'
            Assert-Equal ([IO.File]::ReadAllText((Join-Path $fixture.RunRoot 'evidence/reports/cmake-cache.txt'))) `
                $expectedNativeCMakeCache 'CMake cache report was not copied verbatim.'
            foreach ($reportId in @('qt6gui-headers', 'qt6gui-imports', 'qt6gui-exports', 'qminimal-imports')) {
                $reportPath = Join-Path $fixture.RunRoot "evidence/reports/$reportId.txt"
                $reportBytes = [IO.File]::ReadAllBytes($reportPath)
                Assert-True (-not ($reportBytes -contains 13)) "Canonical dumpbin report '$reportId' contains CR."
                Assert-True ($reportBytes[-1] -eq 10 -and ($reportBytes.Length -lt 2 -or $reportBytes[-2] -ne 10)) `
                    "Canonical dumpbin report '$reportId' does not have exactly one final LF."
            }
            Assert-Equal @($state.Calls | ForEach-Object id) @(
                'vsdevcmd-environment', 'configure', 'build', 'install',
                'report:qt6gui-headers', 'report:qt6gui-imports', 'report:qt6gui-exports', 'report:qminimal-imports'
            ) 'Native process sequence drifted.'

            $configure = @($state.Calls | Where-Object id -ceq 'configure')[0]
            $build = @($state.Calls | Where-Object id -ceq 'build')[0]
            Assert-Equal $configure.file_path $fixture.ToolPaths.cmd
            Assert-Equal $build.file_path $fixture.ToolPaths.cmake
            Assert-True (-not (($configure.resolved_arguments -join ' ').Contains('${', [StringComparison]::Ordinal))) 'Configure retained an unresolved token.'
            Assert-True (($configure.resolved_arguments -join '|').Contains(
                    $fixture.ToolPaths.ninja.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase)) 'Ninja token was not bound to its pinned path.'
            foreach ($cmakePathBinding in @($configure.resolved_arguments | Where-Object { $_ -match '^-D(CMAKE_.+|Vulkan_INCLUDE_DIR)=' })) {
                Assert-True (-not $cmakePathBinding.Contains('\')) 'Resolved CMake path token retained a Windows backslash.'
            }
            $expectedCmakeDirectory = [IO.Path]::GetDirectoryName($fixture.ToolPaths.cmake)
            $firstPath = ([string]$configure.environment['PATH']).Split(';')[0]
            Assert-Equal $firstPath $expectedCmakeDirectory 'CMake bin was not first in the child PATH.'
            Assert-Equal ([Environment]::GetEnvironmentVariable($sentinelName, 'Process')) 'preserve-caller' 'Runner mutated caller environment.'
            foreach ($command in @($result.evidence.commands)) {
                Assert-True ([IO.File]::Exists((Join-Path $fixture.RunRoot $command.stdout.relative_path))) "Missing stdout evidence for $($command.id)."
                Assert-True ([IO.File]::Exists((Join-Path $fixture.RunRoot $command.stderr.relative_path))) "Missing stderr evidence for $($command.id)."
            }
        }
        finally {
            [Environment]::SetEnvironmentVariable($sentinelName, $previousSentinel, 'Process')
            Clear-NativeRunnerTestBoundaries
        }
    }

    Test-Case 'native runner materializes hardlinked install aliases before physical inventory' {
        $fixture = New-NativeRunnerFixture 'runner-install-hardlinks'
        $state = [pscustomobject]@{ Calls = [Collections.Generic.List[object]]::new() }
        $boundary = New-FakeNativeProcessBoundary $fixture $state -InstallHardlinkAliases
        Set-NativeRunnerTestBoundaries $fixture.Prepared $boundary
        try {
            $result = Invoke-M0QtBaseRefreshNative -Context $fixture.Context
            $primary = Join-Path $fixture.RunRoot 'install/bin/androiddeployqt.exe'
            $alias = Join-Path $fixture.RunRoot 'install/bin/androiddeployqt6.exe'
            foreach ($path in @($primary, $alias)) {
                $linkCount = & $script:module {
                    param($literalPath)
                    $handle = $script:NativeFileIdentityType::OpenReadOnlyWithoutWriteDeleteSharing(
                        (ConvertTo-M0NativeExtendedPath $literalPath))
                    try { return [long]$script:NativeFileIdentityType::GetLinkCount($handle) }
                    finally { $handle.Dispose() }
                } $path
                Assert-Equal $linkCount ([long]1) "Installed alias '$path' retained a mutable hardlink."
            }
            Assert-Equal ([Convert]::ToHexString([IO.File]::ReadAllBytes($primary))) `
                ([Convert]::ToHexString([IO.File]::ReadAllBytes($alias))) `
                'Materialized install aliases do not preserve identical bytes.'
            $installTree = @($result.evidence.trees | Where-Object id -ceq 'install')[0]
            $inventoryText = [IO.File]::ReadAllText((Join-Path $fixture.RunRoot $installTree.inventory.relative_path))
            Assert-True $inventoryText.Contains("bin/androiddeployqt.exe`t", [StringComparison]::Ordinal) `
                'Install inventory omitted the primary tool alias.'
            Assert-True $inventoryText.Contains("bin/androiddeployqt6.exe`t", [StringComparison]::Ordinal) `
                'Install inventory omitted the versioned tool alias.'
            Assert-True (-not [IO.Directory]::Exists((Join-Path $fixture.RunRoot 'n'))) `
                'Install hardlink materialization left its private scratch directory behind.'
        }
        finally { Clear-NativeRunnerTestBoundaries }
    }

    Test-Case 'install hardlink materialization fails closed and removes scratch when replacement is blocked' {
        $fixture = New-NativeRunnerFixture 'runner-install-hardlink-locked'
        $state = [pscustomobject]@{ Calls = [Collections.Generic.List[object]]::new(); LockedHandle = $null }
        $boundary = New-FakeNativeProcessBoundary `
            -Fixture $fixture `
            -State $state `
            -InstallHardlinkAliases `
            -LockInstallHardlinkAlias
        Set-NativeRunnerTestBoundaries $fixture.Prepared $boundary
        try {
            Assert-Rejected {
                Invoke-M0QtBaseRefreshNative -Context $fixture.Context
            } 'access|denied|move|replace|hardlink|used by another process'
            Assert-Equal @($state.Calls | ForEach-Object id) @(
                'vsdevcmd-environment', 'configure', 'build', 'install'
            ) 'Reports ran after install hardlink materialization failed.'
            Assert-True (-not [IO.Directory]::Exists((Join-Path $fixture.RunRoot 'n'))) `
                'Rejected install hardlink materialization left its scratch directory behind.'
            Assert-True (-not [IO.File]::Exists((Join-Path $fixture.RunRoot 'evidence/inventories/install.tsv'))) `
                'Rejected install hardlink materialization published an install inventory.'
        }
        finally {
            if ($null -ne $state.LockedHandle) { $state.LockedHandle.Dispose() }
            Clear-NativeRunnerTestBoundaries
        }
    }

    Test-Case 'native runner aborts on environment drift nonzero exit timeout and unknown token' {
        foreach ($kind in @('caller-environment', 'environment', 'nonzero', 'timeout', 'token', 'path-budget', 'report')) {
            $fixture = New-NativeRunnerFixture ('runner-red-' + $kind)
            $state = [pscustomobject]@{ Calls = [Collections.Generic.List[object]]::new() }
            if ($kind -eq 'token') {
                $fixture.Contract.configuration.arguments = @($fixture.Contract.configuration.arguments) + @('-DUNSAFE=${AMBIENT}')
            }
            $boundaryArguments = @{ Fixture = $fixture; State = $state }
            if ($kind -eq 'environment') { $boundaryArguments.InvalidEnvironment = $true }
            elseif ($kind -eq 'nonzero') { $boundaryArguments.FailId = 'configure' }
            elseif ($kind -eq 'timeout') { $boundaryArguments.TimeoutId = 'configure' }
            elseif ($kind -eq 'path-budget') { $boundaryArguments.PathBudgetWarning = $true }
            elseif ($kind -eq 'report') { $boundaryArguments.FailId = 'report:qt6gui-headers' }
            $boundary = New-FakeNativeProcessBoundary @boundaryArguments
            Set-NativeRunnerTestBoundaries $fixture.Prepared $boundary
            $previousCl = [Environment]::GetEnvironmentVariable('CL', 'Process')
            if ($kind -eq 'caller-environment') {
                [Environment]::SetEnvironmentVariable('CL', '/DUNTRUSTED_CALLER_INJECTION', 'Process')
            }
            try {
                $pattern = switch ($kind) {
                    'caller-environment' { 'Forbidden build environment injection|CL' }
                    'environment' { 'toolchain environment|VCToolsVersion|14\.44' }
                    'nonzero' { 'configure.*exit code 7|exit code 7.*configure' }
                    'timeout' { 'configure.*timed out|timed out.*configure' }
                    'token' { 'unknown.*token|token.*AMBIENT|AMBIENT.*token' }
                    'path-budget' { 'CMAKE_OBJECT_PATH_MAX|object.*path|path.*budget|cannot be safely placed' }
                    'report' { 'qt6gui-headers.*exit code 7|exit code 7.*qt6gui-headers' }
                }
                Assert-Rejected {
                    Invoke-M0QtBaseRefreshNative -Context $fixture.Context
                } $pattern
                $ids = @($state.Calls | ForEach-Object id)
                $expectedIds = @(if ($kind -in @('caller-environment', 'token')) { @() }
                    elseif ($kind -eq 'environment') { @('vsdevcmd-environment') }
                    elseif ($kind -eq 'report') {
                        @('vsdevcmd-environment', 'configure', 'build', 'install', 'report:qt6gui-headers')
                    }
                    else { @('vsdevcmd-environment', 'configure') })
                Assert-Equal $ids.Count $expectedIds.Count "Rejected '$kind' process sequence length drifted."
                for ($index = 0; $index -lt $expectedIds.Count; $index++) {
                    Assert-Equal $ids[$index] $expectedIds[$index] "Rejected '$kind' process sequence drifted at index $index."
                }
                if ($kind -ne 'report') {
                    Assert-True (-not ($ids -contains 'build')) "Rejected '$kind' execution reached build."
                    Assert-True (-not ($ids -contains 'install')) "Rejected '$kind' execution reached install."
                    Assert-True (-not [IO.Directory]::Exists((Join-Path $fixture.RunRoot 'install'))) "Rejected '$kind' execution created install output."
                }
                else {
                    Assert-True (-not ($ids -contains 'report:qt6gui-imports')) 'Rejected report execution reached the next producer.'
                    Assert-True (-not [IO.File]::Exists((Join-Path $fixture.RunRoot 'evidence/inventories/install.tsv'))) `
                        'Rejected report execution published the install inventory.'
                }
                Assert-True (-not [IO.File]::Exists((Join-Path $fixture.RunRoot 'evidence/receipt.v2.json'))) `
                    "Rejected '$kind' execution published a receipt."
                Assert-Equal (@(Get-ChildItem -LiteralPath (Join-Path $fixture.RunRoot 'evidence') -Recurse -File -Filter '*.tmp' -ErrorAction SilentlyContinue).Count) 0 `
                    "Rejected '$kind' execution left raw report temp files."
            }
            finally {
                [Environment]::SetEnvironmentVariable('CL', $previousCl, 'Process')
                Clear-NativeRunnerTestBoundaries
            }
        }
    }

    Test-Case 'default process boundary streams both pipes and kills a timed-out process' {
        Clear-NativeRunnerTestBoundaries
        $root = Join-Path $testRoot 'process-boundary'
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $environment = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
            $environment[[string]$entry.Key] = [string]$entry.Value
        }
        $request = [pscustomobject]@{
            id = 'stream-probe'
            file_path = Join-Path $PSHOME 'pwsh.exe'
            arguments = @('-NoLogo', '-NoProfile', '-Command', '[Console]::Out.Write("stdout-probe"); [Console]::Error.Write("stderr-probe")')
            logical_arguments = @('-NoLogo', '-NoProfile', '-Command', 'stream-probe')
            resolved_arguments = @('-NoLogo', '-NoProfile', '-Command', 'stream-probe')
            working_directory = $root
            environment = $environment
            stdout_path = Join-Path $root 'stdout.log'
            stderr_path = Join-Path $root 'stderr.log'
            timeout_seconds = 10
        }
        $result = & $script:module { param($value) Invoke-M0NativeBoundedProcess $value } $request
        Assert-Equal $result.exit_code ([long]0)
        Assert-True (-not $result.timed_out) 'Short process unexpectedly timed out.'
        Assert-Equal ([IO.File]::ReadAllText($request.stdout_path)) 'stdout-probe'
        Assert-Equal ([IO.File]::ReadAllText($request.stderr_path)) 'stderr-probe'
        $stdoutHash = Get-TestSha256File $request.stdout_path
        $stderrHash = Get-TestSha256File $request.stderr_path
        Assert-Rejected {
            & $script:module { param($value) Invoke-M0NativeBoundedProcess $value } $request
        } 'CreateNew|already exists|output'
        Assert-Equal (Get-TestSha256File $request.stdout_path) $stdoutHash 'CreateNew rejection changed stdout evidence.'
        Assert-Equal (Get-TestSha256File $request.stderr_path) $stderrHash 'CreateNew rejection changed stderr evidence.'

        $batchPath = Join-Path $root 'directory with spaces/quote-probe.bat'
        Write-TestBytes $batchPath ([Text.Encoding]::ASCII.GetBytes("@echo off`r`necho %~1`r`n"))
        $cmdRequest = [pscustomobject][ordered]@{
            id = 'cmd-quote-probe'
            file_path = Join-Path $env:SystemRoot 'System32/cmd.exe'
            arguments = @('/d', '/s', '/c', ('call "' + $batchPath + '" "quoted value"'))
            logical_arguments = @('quoted value')
            resolved_arguments = @('quoted value')
            working_directory = $root
            environment = $environment
            stdout_path = Join-Path $root 'cmd.stdout.log'
            stderr_path = Join-Path $root 'cmd.stderr.log'
            timeout_seconds = 10
        }
        $cmdResult = & $script:module { param($value) Invoke-M0NativeBoundedProcess $value } $cmdRequest
        Assert-Equal $cmdResult.exit_code ([long]0)
        Assert-Equal ([IO.File]::ReadAllText($cmdRequest.stdout_path).Trim()) 'quoted value' 'cmd did not preserve quoted paths and values.'

        $timeoutRequest = [pscustomobject]@{
            id = 'timeout-probe'
            file_path = Join-Path $PSHOME 'pwsh.exe'
            arguments = @('-NoLogo', '-NoProfile', '-Command', '[Threading.Thread]::Sleep(5000)')
            logical_arguments = @('-NoLogo', '-NoProfile', '-Command', 'timeout-probe')
            resolved_arguments = @('-NoLogo', '-NoProfile', '-Command', 'timeout-probe')
            working_directory = $root
            environment = $environment
            stdout_path = Join-Path $root 'timeout.stdout.log'
            stderr_path = Join-Path $root 'timeout.stderr.log'
            timeout_seconds = 1
        }
        $timed = & $script:module { param($value) Invoke-M0NativeBoundedProcess $value } $timeoutRequest
        Assert-True $timed.timed_out 'Long process did not report timeout.'
        Assert-True ($timed.exit_code -ne 0) 'Timed-out process reported success.'
    }
}
finally {
    Remove-Module -ModuleInfo $module -Force -ErrorAction SilentlyContinue
    if ([IO.Directory]::Exists($testRoot)) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -ne 0) { exit 1 }
