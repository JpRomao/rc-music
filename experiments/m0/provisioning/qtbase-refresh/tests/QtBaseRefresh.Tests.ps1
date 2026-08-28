#Requires -Version 7.0
[CmdletBinding()]
param([string] $Filter = '*')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path $PSScriptRoot '../M0.QtBaseRefresh.psm1'
$launcherPath = Join-Path $PSScriptRoot '../Invoke-M0QtBaseRefresh.ps1'
$script:module = $null
$state = @{ Passed = 0; Failed = 0 }

function Test-Case {
    param(
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][scriptblock] $Body
    )

    if ($Name -notlike $Filter) { return }
    try {
        $null = & $Body
        $state.Passed++
        Write-Host "[PASS] $Name"
    }
    catch {
        $state.Failed++
        Write-Host "[FAIL] $Name :: $($_.Exception.Message)"
        Write-Host $_.ScriptStackTrace
    }
}

function Assert-True {
    param([bool] $Actual, [string] $Message)
    if (-not $Actual) { throw $Message }
}

function Assert-Equal {
    param($Actual, $Expected, [string] $Message = 'Values differ.')
    if (-not [object]::Equals($Actual, $Expected)) {
        throw "$Message Expected '$Expected', found '$Actual'."
    }
}

function Assert-SequenceEqual {
    param([object[]] $Actual, [object[]] $Expected, [string] $Message = 'Sequences differ.')
    $actualJson = ConvertTo-Json @($Actual) -Compress -Depth 30
    $expectedJson = ConvertTo-Json @($Expected) -Compress -Depth 30
    if ($actualJson -cne $expectedJson) {
        throw "$Message Expected '$expectedJson', found '$actualJson'."
    }
}

function Assert-Rejected {
    param([scriptblock] $Body, [string] $Pattern)
    try {
        & $Body | Out-Null
    }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) {
            throw "Wrong rejection. Expected '$Pattern', found '$($_.Exception.Message)'."
        }
        return $_
    }
    throw "Expected rejection matching '$Pattern'."
}

function Get-TestSha256 {
    param([Parameter(Mandatory)][string] $LiteralPath)
    return (Get-FileHash -LiteralPath $LiteralPath -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TestTextIdentity {
    param([Parameter(Mandatory)][AllowEmptyString()][string] $Text)
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Text)
    return [pscustomobject]@{
        size_bytes = [long]$bytes.Length
        sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    }
}

function Get-TestCanonicalInventoryIdentity {
    param([Parameter(Mandatory)] $Rows)
    $builder = [Text.StringBuilder]::new()
    $total = [long]0
    foreach ($row in @($Rows)) {
        $total += [long]$row.size_bytes
        $null = $builder.Append($row.relative_path).Append("`t")
        $null = $builder.Append(([long]$row.size_bytes).ToString([Globalization.CultureInfo]::InvariantCulture)).Append("`t")
        $null = $builder.Append($row.sha256).Append("`n")
    }
    $text = $builder.ToString()
    $identity = Get-TestTextIdentity $text
    return [pscustomobject]@{
        text = $text
        file_count = [long]@($Rows).Count
        total_size_bytes = $total
        inventory_sha256 = $identity.sha256
        inventory_size_bytes = $identity.size_bytes
    }
}

function Write-TestText {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][AllowEmptyString()][string] $Text)
    $parent = Split-Path -Parent $LiteralPath
    if (-not [IO.Directory]::Exists($parent)) {
        [IO.Directory]::CreateDirectory($parent) | Out-Null
    }
    [IO.File]::WriteAllText($LiteralPath, $Text, [Text.UTF8Encoding]::new($false))
}

function Write-TestJson {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)] $Document)
    Write-TestText -LiteralPath $LiteralPath -Text (($Document | ConvertTo-Json -Depth 100) + "`n")
    return Get-TestSha256 -LiteralPath $LiteralPath
}

function Copy-TestJson {
    param([Parameter(Mandatory)] $Document)
    return $Document | ConvertTo-Json -Compress -Depth 100 | ConvertFrom-Json -Depth 100 -DateKind String
}

if (Test-Path -LiteralPath $modulePath -PathType Leaf) {
    $script:module = Import-Module $modulePath -Force -PassThru
}

Test-Case 'the QtBase refresh module exports the minimal public orchestration contract' {
    Assert-True (Test-Path -LiteralPath $modulePath -PathType Leaf) `
        "Required module is absent: $modulePath"
    Assert-True ($null -ne $script:module) 'Required module could not be imported.'
    $exported = @($script:module.ExportedFunctions.Keys | Sort-Object)
    foreach ($required in @(
            'Assert-M0QtBaseRefreshContract',
            'Assert-M0QtBaseRefreshReceipt',
            'Invoke-M0QtBaseRefresh'
        )) {
        Assert-True ($exported -ccontains $required) "Required function '$required' is not exported."
    }
    $productionParameters = @((Get-Command -Name Invoke-M0QtBaseRefresh -Module $script:module.Name).Parameters.Keys)
    Assert-True ($productionParameters -cnotcontains 'Execute') 'The production entrypoint exposes an arbitrary execution callback.'
    Assert-Equal @($script:module.ExportedFunctions.Keys).Count 3 'The orchestrator exports unexpected functions.'
    Assert-Equal @($script:module.ExportedAliases.Keys).Count 0 'The orchestrator exports unexpected aliases.'
    Assert-Equal @($script:module.ExportedCmdlets.Keys).Count 0 'The orchestrator exports unexpected cmdlets.'
    Assert-Equal @($script:module.ExportedVariables.Keys).Count 0 'The orchestrator exports unexpected variables.'

    $moduleSource = Get-Content -Raw -LiteralPath $modulePath
    Assert-True ($moduleSource -cnotmatch '(?s)Invoke-M0QtBaseRefreshCore.*\[scriptblock\]\s*\$Execute') `
        'The production module still exposes an arbitrary private execution callback.'

    $tokens = $null
    $parseErrors = $null
    $moduleAst = [Management.Automation.Language.Parser]::ParseFile(
        $modulePath,
        [ref]$tokens,
        [ref]$parseErrors
    )
    Assert-Equal @($parseErrors).Count 0 'The production module must parse before inspecting dynamic dispatch.'
    $dynamicCalls = @($moduleAst.FindAll({
                param($node)
                $node -is [Management.Automation.Language.CommandAst] -and $null -eq $node.GetCommandName()
            }, $true))
    Assert-Equal $dynamicCalls.Count 1 'The production module contains an unexpected dynamic command dispatch.'
    $dynamicCommand = $dynamicCalls[0].CommandElements[0]
    Assert-True ($dynamicCommand -is [Management.Automation.Language.VariableExpressionAst] -and
        $dynamicCommand.VariablePath.UserPath -ceq 'runnerCommand') `
        'The only accepted dynamic dispatch is the authenticated runner command.'
}

Test-Case 'the production launcher authenticates the fixed v2 recipe before importing the orchestrator' {
    Assert-True (Test-Path -LiteralPath $launcherPath -PathType Leaf) `
        "Required production launcher is absent: $launcherPath"

    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        $launcherPath,
        [ref]$tokens,
        [ref]$parseErrors
    )
    Assert-Equal @($parseErrors).Count 0 'The production launcher must parse without errors.'
    Assert-True ($null -eq $ast.ParamBlock -or @($ast.ParamBlock.Parameters).Count -eq 0) `
        'The production launcher must not expose path, callback or hash overrides.'

    $text = Get-Content -Raw -LiteralPath $launcherPath
    foreach ($literal in @(
            'qtbase-refresh-contract.v2.json',
            'qtbase-refresh-contract.v2.json.sha256',
            'M0.QtBaseRefresh.psm1',
            'Invoke-M0QtBaseRefresh',
            'Invoke-M0QtBaseRefreshNative'
        )) {
        Assert-True ($text.Contains($literal, [StringComparison]::Ordinal)) `
            "The production launcher is missing its fixed '$literal' binding."
    }
    foreach ($forbidden in @('Invoke-Expression', 'ScriptBlock]::Create', 'DownloadString', 'Start-Process')) {
        Assert-True (-not $text.Contains($forbidden, [StringComparison]::OrdinalIgnoreCase)) `
            "The production launcher contains forbidden dynamic execution '$forbidden'."
    }
    $commandAsts = @($ast.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] }, $true))
    $leaseCalls = @($commandAsts | Where-Object { $_.GetCommandName() -ceq 'Open-M0BootstrapRecipeLease' })
    $importCalls = @($commandAsts | Where-Object { $_.GetCommandName() -ceq 'Import-Module' })
    Assert-Equal $leaseCalls.Count 2 'The launcher must call the authenticated lease primitive for contract and recipe.'
    Assert-Equal $importCalls.Count 1 'The launcher must contain exactly one orchestrator import call.'
    $lastAuthenticationOffset = [long](($leaseCalls | Sort-Object { $_.Extent.StartOffset } | Select-Object -Last 1).Extent.StartOffset)
    $importOffset = [long]$importCalls[0].Extent.StartOffset
    Assert-True ($importOffset -gt $lastAuthenticationOffset) `
        'The production launcher must authenticate the recipe before importing the orchestrator.'
}

if ($state.Failed -ne 0 -or $null -eq $script:module) {
    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    exit 1
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
$testRootParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out/m0/test-runs/qtbase-refresh'))
$testRunId = [Guid]::NewGuid().ToString('N')
$testRoot = [IO.Path]::GetFullPath((Join-Path $testRootParent $testRunId))
if ([IO.Path]::GetDirectoryName($testRoot) -cne $testRootParent -or
    [IO.Path]::GetFileName($testRoot) -cne $testRunId) {
    throw 'The test run root must be a GUID-named direct child of the fixed test parent.'
}
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

function Remove-M0QtBaseRefreshTestTree {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $LiteralPath)

    $root = [IO.Path]::GetFullPath($LiteralPath)
    $rootAttributes = [IO.File]::GetAttributes($root)
    if (($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        [IO.Directory]::Delete($root, $false)
        return
    }

    foreach ($entry in [IO.DirectoryInfo]::new($root).EnumerateFileSystemInfos()) {
        $attributes = $entry.Attributes
        $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
        $isReparsePoint = ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0
        if ($isReparsePoint) {
            if ($isDirectory) { [IO.Directory]::Delete($entry.FullName, $false) }
            else { [IO.File]::Delete($entry.FullName) }
            continue
        }
        if ($isDirectory) {
            Remove-M0QtBaseRefreshTestTree -LiteralPath $entry.FullName
            continue
        }
        if (($attributes -band [IO.FileAttributes]::ReadOnly) -ne 0) {
            [IO.File]::SetAttributes($entry.FullName, ($attributes -bxor [IO.FileAttributes]::ReadOnly))
        }
        [IO.File]::Delete($entry.FullName)
    }
    [IO.Directory]::Delete($root, $false)
}

try {
function Get-TestConfigSummaryText {
    return (@(
            '',
            'Building for: win32-msvc (x86_64, CPU features: )',
            'Compiler: msvc 19.44.35228.0',
            'Build options:',
            '  Mode ................................... debug and release (with debug info)',
            '  Building shared libraries (alias of  BUILD_SHARED_LIBS)  yes',
            '  Using Link Time Optimization (LTCG) .... no',
            '  Using Intel Control-flow Enforcement Technology (CET)  yes',
            '  OpenSSL ................................ no',
            '    Qt directly linked to OpenSSL ........ no',
            '  OpenSSL 1.1 ............................ no',
            '  OpenSSL 3.0 ............................ no',
            '  Using system zlib ...................... no',
            '  PCRE2 .................................. yes',
            '    Using system PCRE2 ................... no',
            '  Schannel ............................... yes',
            '  Accessibility .......................... yes',
            '  FreeType ............................... yes',
            '    Using system FreeType ................ no',
            '  HarfBuzz ............................... yes',
            '    Using system HarfBuzz ................ no',
            '    JPEG ................................. yes',
            '    PNG .................................. yes',
            '  OpenGL:',
            '    Desktop OpenGL ....................... no',
            '    Dynamic OpenGL ....................... yes',
            '    OpenGL ES 2.0 ........................ no',
            '    OpenGL ES 3.0 ........................ no',
            '    OpenGL ES 3.1 ........................ no',
            '    OpenGL ES 3.2 ........................ no',
            '  Vulkan ................................. yes',
            '    DirectWrite .......................... yes',
            '    DirectWrite 3 ........................ yes',
            '    DirectWrite COLRv1 Support ........... yes'
        ) -join "`n") + "`n"
}

function Get-TestCMakeCacheText {
    return (@(
            'CMAKE_CONFIGURATION_TYPES:UNINITIALIZED=RelWithDebInfo;Debug',
            'CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config',
            'BUILD_SHARED_LIBS:BOOL=ON',
            'BUILD_WITH_PCH:BOOL=OFF',
            'CMAKE_INTERPROCEDURAL_OPTIMIZATION:UNINITIALIZED=OFF',
            'QT_FEATURE_accessibility:INTERNAL=ON',
            'QT_FEATURE_direct2d:INTERNAL=ON',
            'QT_FEATURE_direct2d1_1:INTERNAL=ON',
            'QT_FEATURE_directwrite:INTERNAL=ON',
            'QT_FEATURE_directwrite3:INTERNAL=ON',
            'QT_FEATURE_directwritecolrv1:INTERNAL=ON',
            'QT_FEATURE_dynamicgl:INTERNAL=ON',
            'QT_FEATURE_freetype:INTERNAL=ON',
            'QT_FEATURE_harfbuzz:INTERNAL=ON',
            'QT_FEATURE_intelcet:INTERNAL=ON',
            'QT_FEATURE_jpeg:INTERNAL=ON',
            'QT_FEATURE_msvc_obj_debug_info:INTERNAL=ON',
            'QT_FEATURE_opengl:INTERNAL=ON',
            'QT_FEATURE_openssl:INTERNAL=OFF',
            'QT_FEATURE_pcre2:INTERNAL=ON',
            'QT_FEATURE_png:INTERNAL=ON',
            'QT_FEATURE_schannel:INTERNAL=ON',
            'QT_FEATURE_system_freetype:INTERNAL=OFF',
            'QT_FEATURE_system_harfbuzz:INTERNAL=OFF',
            'QT_FEATURE_system_jpeg:INTERNAL=OFF',
            'QT_FEATURE_system_pcre2:INTERNAL=OFF',
            'QT_FEATURE_system_png:INTERNAL=OFF',
            'QT_FEATURE_system_zlib:INTERNAL=OFF',
            'QT_FEATURE_systemtrayicon:INTERNAL=ON',
            'QT_FEATURE_vulkan:INTERNAL=ON'
        ) -join "`n") + "`n`n"
}

function New-TestLauncherFixture {
    $root = Join-Path $testRoot ('launcher-' + [Guid]::NewGuid().ToString('N'))
    $recipeRoot = Join-Path $root 'experiments/m0/provisioning/qtbase-refresh'
    $runParent = Join-Path $root 'q'
    [IO.Directory]::CreateDirectory($recipeRoot) | Out-Null
    [IO.Directory]::CreateDirectory($runParent) | Out-Null
    $fixtureLauncher = Join-Path $recipeRoot 'Invoke-M0QtBaseRefresh.ps1'
    [IO.File]::WriteAllBytes($fixtureLauncher, [IO.File]::ReadAllBytes($launcherPath))
    $orchestrator = Join-Path $recipeRoot 'M0.QtBaseRefresh.psm1'
    Write-TestText $orchestrator @'
$script:MarkerRoot = $PSScriptRoot
[IO.File]::WriteAllText((Join-Path $script:MarkerRoot 'imported.marker'), 'imported', [Text.UTF8Encoding]::new($false))
function Assert-M0QtBaseRefreshContract {
    param([string] $LiteralPath, [string] $ExpectedSha256)
    return [pscustomobject]@{ isolation = [pscustomobject]@{ run_parent_relative_path = 'q' } }
}
function Assert-M0QtBaseRefreshReceipt { param() }
function Invoke-M0QtBaseRefresh {
    param([string] $ContractPath, [string] $ExpectedContractSha256, [string] $RepositoryRoot, [string] $ToolRoot, [string] $RunRoot)
    [IO.File]::WriteAllText((Join-Path $script:MarkerRoot 'invoked.marker'), $RunRoot, [Text.UTF8Encoding]::new($false))
}
Export-ModuleMember -Function 'Assert-M0QtBaseRefreshContract', 'Assert-M0QtBaseRefreshReceipt', 'Invoke-M0QtBaseRefresh'
'@
    $runner = Join-Path $recipeRoot 'M0.QtBaseRefreshNative.psm1'
    Write-TestText $runner "function Invoke-M0QtBaseRefreshNative { param(`$Context) }`nExport-ModuleMember -Function 'Invoke-M0QtBaseRefreshNative'`n"
    $recipeFiles = @(
        foreach ($definition in @(
                @('orchestrator', $orchestrator, 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1'),
                @('runner', $runner, 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1'),
                @('entrypoint', $fixtureLauncher, 'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1')
            )) {
            [ordered]@{
                id = $definition[0]
                relative_path = $definition[2]
                size_bytes = [long](Get-Item -LiteralPath $definition[1]).Length
                sha256 = Get-TestSha256 $definition[1]
            }
        }
    )
    $contractPath = Join-Path $recipeRoot 'qtbase-refresh-contract.v2.json'
    $contract = [ordered]@{
        schema_version = [long]2
        recipe = [ordered]@{ files = $recipeFiles; runner_export = 'Invoke-M0QtBaseRefreshNative' }
    }
    $contractSha256 = Write-TestJson $contractPath $contract
    Write-TestText (Join-Path $recipeRoot 'qtbase-refresh-contract.v2.json.sha256') `
        "$contractSha256 *qtbase-refresh-contract.v2.json`n"
    return [pscustomobject]@{
        Root = $root
        Launcher = $fixtureLauncher
        Contract = $contractPath
        Orchestrator = $orchestrator
        Runner = $runner
        RunParent = $runParent
        ImportedMarker = Join-Path $recipeRoot 'imported.marker'
        InvokedMarker = Join-Path $recipeRoot 'invoked.marker'
    }
}

function New-QtBaseRefreshFixture {
    $fixtureRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N'))
    $inputsRoot = Join-Path $fixtureRoot 'inputs'
    $toolRoot = Join-Path $fixtureRoot 'toolchain'
    $runParent = Join-Path $fixtureRoot 'out/m0/qtbase-refresh-runs'
    [IO.Directory]::CreateDirectory($inputsRoot) | Out-Null
    [IO.Directory]::CreateDirectory($toolRoot) | Out-Null
    [IO.Directory]::CreateDirectory($runParent) | Out-Null

    $qtbasePath = Join-Path $inputsRoot 'qtbase-everywhere-src-6.11.2.tar.xz'
    $patchPath = Join-Path $inputsRoot '5a280ec.patch'
    $vulkanPath = Join-Path $inputsRoot 'Vulkan-Headers-sdk-1.2.182.0.tar.gz'
    Write-TestText $qtbasePath 'synthetic qtbase source archive bytes'
    Write-TestText $patchPath "diff --git a/src/3rdparty/freetype/src/truetype/ttgxvar.c b/src/3rdparty/freetype/src/truetype/ttgxvar.c`n"
    Write-TestText $vulkanPath 'synthetic Vulkan-Headers archive bytes'

    $toolIds = @(
        'cmd', 'vsdevcmd', 'cmake', 'cmcldeps', 'ninja', 'cl', 'link', 'lib', 'dumpbin', 'rc', 'mt', 'midl',
        'windows-h', 'dwrite-h', 'dwrite-2-h', 'dwrite-3-h', 'd2d1-h', 'd2d1-1-h', 'd3d11-h', 'dxgi1-6-h',
        'sdkddkver-h', 'sal-h', 'corecrt-h', 'ucrt-lib', 'kernel32-lib', 'user32-lib', 'gdi32-lib', 'dwrite-lib',
        'd2d1-lib', 'd3d11-lib', 'dxgi-lib', 'windowscodecs-lib', 'advapi32-lib', 'ole32-lib', 'shell32-lib', 'uuid-lib'
    )
    $tools = @()
    foreach ($id in $toolIds) {
        $relativePath = "authenticated/$id.bin"
        $toolPath = Join-Path $toolRoot $relativePath
        Write-TestText $toolPath "synthetic $id bytes"
        $tools += [ordered]@{
            id = $id
            relative_path = $relativePath
            size_bytes = [long](Get-Item -LiteralPath $toolPath).Length
            sha256 = Get-TestSha256 $toolPath
            version = 'synthetic-1.0'
        }
    }

    $target = 'src/3rdparty/freetype/src/truetype/ttgxvar.c'
    $features = [ordered]@{
        shared = $true
        debug_and_release = $true
        force_debug_info = $true
        bundled_freetype = $true
        bundled_harfbuzz = $true
        bundled_pcre2 = $true
        bundled_zlib = $true
        bundled_png = $true
        bundled_jpeg = $true
        opengl = 'dynamic'
        vulkan = $true
        directwrite = $true
        directwrite3 = $true
        directwrite_colrv1 = $true
        direct2d = $true
        direct2d1_1 = $true
        accessibility = $true
        systemtrayicon = $true
        openssl_runtime = $false
        schannel = $true
        intelcet = $true
        msvc_obj_debug_info = $true
    }
    $configurationArguments = @(
        '-prefix', '${INSTALL_ROOT}',
        '-shared',
        '-debug-and-release',
        '-force-debug-info',
        '-headersclean',
        '-nomake', 'examples',
        '-nomake', 'tests',
        '-no-ltcg',
        '-intelcet',
        '-qt-zlib',
        '-qt-pcre',
        '-qt-freetype',
        '-qt-harfbuzz',
        '-qt-libpng',
        '-qt-libjpeg',
        '-opengl', 'dynamic',
        '-vulkan',
        '-no-openssl',
        '-schannel',
        '-no-sql-psql',
        '-cmake-generator', 'Ninja Multi-Config',
        '--',
        '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
        '-DVulkan_INCLUDE_DIR=${VULKAN_INCLUDE}',
        '-DFEATURE_vulkan=ON',
        '-DFEATURE_directwrite=ON',
        '-DFEATURE_directwrite3=ON',
        '-DFEATURE_directwritecolrv1=ON',
        '-DFEATURE_direct2d=ON',
        '-DFEATURE_direct2d1_1=ON',
        '-DFEATURE_accessibility=ON',
        '-DFEATURE_systemtrayicon=ON',
        '-DFEATURE_schannel=ON',
        '-DFEATURE_openssl=OFF',
        '-DFEATURE_intelcet=ON',
        '-DFEATURE_msvc_obj_debug_info=ON',
        '-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF',
        '-DBUILD_WITH_PCH=OFF',
        '-DQT_BUILD_TESTS=OFF',
        '-DQT_INSTALL_CONFIG_INFO_FILES=ON',
        '-DQT_GENERATE_SBOM=OFF',
        '-DQT_GENERATE_SOURCE_SBOM=OFF',
        '-DCMAKE_MAKE_PROGRAM=${NINJA}',
        '-DCMAKE_C_COMPILER=${CL}',
        '-DCMAKE_CXX_COMPILER=${CL}',
        '-DCMAKE_RC_COMPILER=${RC}',
        '-DCMAKE_MT=${MT}'
    )
    $contract = [ordered]@{
        schema_version = 1
        refresh_id = 'm0.2-qtbase-freetype-backport-v1'
        product = [ordered]@{
            qt_version = '6.11.2'
            architecture = 'x86_64'
            compiler_abi = 'msvc2022_64'
        }
        inputs = [ordered]@{
            qtbase_source = [ordered]@{
                relative_path = 'inputs/qtbase-everywhere-src-6.11.2.tar.xz'
                size_bytes = [long](Get-Item -LiteralPath $qtbasePath).Length
                sha256 = Get-TestSha256 $qtbasePath
                commit_sha = 'ef55f427f2c8b410d34f8a7681020a3000cf6866'
            }
            freetype_patch = [ordered]@{
                relative_path = 'inputs/5a280ec.patch'
                size_bytes = [long](Get-Item -LiteralPath $patchPath).Length
                sha256 = Get-TestSha256 $patchPath
                target_relative_path = $target
                target_before_sha256 = 'b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10'
                target_after_sha256 = '9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346'
            }
            vulkan_headers = [ordered]@{
                relative_path = 'inputs/Vulkan-Headers-sdk-1.2.182.0.tar.gz'
                size_bytes = [long](Get-Item -LiteralPath $vulkanPath).Length
                sha256 = Get-TestSha256 $vulkanPath
                commit_sha = '37164a5726f7e6113810f9557903a117498421cf'
                header_relative_path = 'include/vulkan/vulkan_core.h'
                header_sha256 = '1c688d7628c679816277e2186e7832c2d6e3eace0de20ff5e1c46ae88473cc83'
                license_relative_path = 'LICENSE.txt'
                license_sha256 = 'cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30'
            }
        }
        tools = $tools
        environment = [ordered]@{
            forbidden_variables = @(
                'CL', '_CL_', 'LINK', '_LINK_',
                'CFLAGS', 'CXXFLAGS', 'LDFLAGS',
                'CMAKE_PREFIX_PATH', 'CMAKE_TOOLCHAIN_FILE',
                'QMAKESPEC', 'QT_HOST_PATH', 'VULKAN_SDK',
                'PYTHONHOME', 'PYTHONPATH', 'PERL5LIB'
            )
        }
        isolation = [ordered]@{
            run_parent_relative_path = 'out/m0/qtbase-refresh-runs'
            require_new_direct_child = $true
            receipt_relative_path = 'evidence/qtbase-refresh-receipt.v1.json'
        }
        configuration = [ordered]@{
            generator = 'Ninja Multi-Config'
            arguments = $configurationArguments
            build_arguments = @('--build', '${BUILD_ROOT}', '--config', 'RelWithDebInfo', '--parallel', '8')
            install_arguments = @('--install', '${BUILD_ROOT}', '--config', 'RelWithDebInfo')
            installed_configuration = 'RelWithDebInfo'
            expected_artifacts = @(
                'bin/Qt6Core.dll',
                'bin/Qt6Gui.dll',
                'bin/Qt6Network.dll',
                'bin/Qt6OpenGL.dll',
                'bin/Qt6Widgets.dll',
                'plugins/generic/qtuiotouchplugin.dll',
                'plugins/imageformats/qgif.dll',
                'plugins/imageformats/qico.dll',
                'plugins/imageformats/qjpeg.dll',
                'plugins/networkinformation/qnetworklistmanager.dll',
                'plugins/platforms/qoffscreen.dll',
                'plugins/platforms/qwindows.dll',
                'plugins/styles/qmodernwindowsstyle.dll',
                'plugins/tls/qcertonlybackend.dll',
                'plugins/tls/qschannelbackend.dll'
            )
            expected_features = $features
        }
    }

    $contractPath = Join-Path $fixtureRoot 'qtbase-refresh-contract.v1.json'
    $contractSha256 = Write-TestJson -LiteralPath $contractPath -Document $contract
    return [pscustomobject]@{
        Root = $fixtureRoot
        ToolRoot = $toolRoot
        RunParent = $runParent
        Contract = $contract
        ContractPath = $contractPath
        ContractSha256 = $contractSha256
        Target = $target
    }
}

function New-TestRunLeaf {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)][string] $RelativePath,
        [Parameter(Mandatory)][AllowEmptyString()][string] $Content
    )
    $path = Join-Path $Context.run_root $RelativePath
    Write-TestText -LiteralPath $path -Text $Content
    return [pscustomobject][ordered]@{
        relative_path = $RelativePath.Replace('\\', '/')
        size_bytes = [long](Get-Item -LiteralPath $path).Length
        sha256 = Get-TestSha256 -LiteralPath $path
    }
}

function Set-TestPeAsciiString {
    param(
        [Parameter(Mandatory)][byte[]] $Bytes,
        [Parameter(Mandatory)][string] $OldValue,
        [Parameter(Mandatory)][string] $NewValue
    )
    $oldBytes = [Text.Encoding]::ASCII.GetBytes($OldValue)
    $newBytes = [Text.Encoding]::ASCII.GetBytes($NewValue)
    if ($newBytes.Length -gt $oldBytes.Length) { throw "PE fixture replacement '$NewValue' does not fit '$OldValue'." }
    $match = -1
    for ($offset = 0; $offset -le $Bytes.Length - $oldBytes.Length; $offset++) {
        $equal = $true
        for ($index = 0; $index -lt $oldBytes.Length; $index++) {
            if ($Bytes[$offset + $index] -ne $oldBytes[$index]) { $equal = $false; break }
        }
        if ($equal) { $match = $offset; break }
    }
    if ($match -lt 0) { throw "PE fixture source string '$OldValue' is absent." }
    [Array]::Clear($Bytes, $match, $oldBytes.Length)
    [Array]::Copy($newBytes, 0, $Bytes, $match, $newBytes.Length)
}

function New-TestRunPeLeaf {
    param([Parameter(Mandatory)] $Context, [Parameter(Mandatory)][string] $RelativePath)
    $path = Join-Path $Context.run_root $RelativePath
    $parent = Split-Path -Parent $path
    if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
    $fixturePe = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)) 'System32/version.dll'
    $bytes = [IO.File]::ReadAllBytes($fixturePe)
    $canonicalPath = $RelativePath.Replace([char]92, [char]47)
    if ($canonicalPath -ceq 'install/bin/Qt6Gui.dll') {
        Set-TestPeAsciiString $bytes 'KERNELBASE.dll' 'Qt6Core.dll'
        Set-TestPeAsciiString $bytes 'KERNEL32.dll' 'DWrite.dll'
        Set-TestPeAsciiString $bytes 'GetFileVersionInfoByHandle' '??0QColor@@QEAA@HHHH@Z'
    }
    elseif ($canonicalPath -ceq 'install/plugins/platforms/qminimal.dll') {
        Set-TestPeAsciiString $bytes 'KERNELBASE.dll' 'Qt6Gui.dll'
        Set-TestPeAsciiString $bytes 'KERNEL32.dll' 'Qt6Core.dll'
    }
    [IO.File]::WriteAllBytes($path, $bytes)
    return [pscustomobject][ordered]@{
        relative_path = $canonicalPath
        size_bytes = [long]$bytes.Length
        sha256 = Get-TestSha256 -LiteralPath $path
    }
}

function New-TestRepositoryLeaf {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)][string] $RelativePath,
        [Parameter(Mandatory)][string] $Content
    )
    $path = Join-Path $Fixture.Root $RelativePath
    Write-TestText -LiteralPath $path -Text $Content
    return [pscustomobject][ordered]@{
        relative_path = $RelativePath.Replace('\\', '/')
        size_bytes = [long](Get-Item -LiteralPath $path).Length
        sha256 = Get-TestSha256 -LiteralPath $path
    }
}

function New-TestPhysicalEvidence {
    param([Parameter(Mandatory)] $Fixture, [Parameter(Mandatory)] $Context)
    $recipePaths = @(
        'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1',
        'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1',
        'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1'
    )
    $recipeFiles = @(
        foreach ($relativePath in $recipePaths) {
            New-TestRepositoryLeaf $Fixture $relativePath "synthetic recipe $relativePath`n"
        }
    )

    $trees = @()
    foreach ($id in @('qtbase-before', 'qtbase-patched', 'vulkan', 'install')) {
        $contentHash = [Convert]::ToHexString(
            [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes("$id bytes"))
        ).ToLowerInvariant()
        $inventory = New-TestRunLeaf $Context "evidence/inventories/$id.tsv" "file.bin`t8`t$contentHash`n"
        $trees += [pscustomobject][ordered]@{
            id = $id
            file_count = [long]1
            total_size_bytes = [long]8
            inventory = $inventory
        }
    }

    $commands = @()
    foreach ($id in @('configure', 'build', 'install')) {
        $commands += [pscustomobject][ordered]@{
            id = $id
            exit_code = [long]0
            stdout = New-TestRunLeaf $Context "evidence/logs/$id.stdout.log" "$id stdout`n"
            stderr = New-TestRunLeaf $Context "evidence/logs/$id.stderr.log" ''
        }
    }

    $reports = @()
    foreach ($id in @(
            'configure-summary', 'cmake-cache', 'qtgui-freetype-dependency',
            'qt6gui-headers', 'qt6gui-imports', 'qt6gui-exports', 'qminimal-imports'
        )) {
        $reports += [pscustomobject][ordered]@{
            id = $id
            file = New-TestRunLeaf $Context "evidence/reports/$id.txt" "$id report`n"
        }
    }

    $artifacts = @(
        foreach ($relativePath in @($Fixture.Contract.configuration.expected_artifacts)) {
            New-TestRunPeLeaf $Context "install/$relativePath"
        }
    )
    $directConsumers = @(
        New-TestRunPeLeaf $Context 'install/plugins/platforms/qminimal.dll'
    )

    return [pscustomobject][ordered]@{
        recipe = [pscustomobject][ordered]@{
            files = $recipeFiles
        }
        trees = $trees
        commands = $commands
        reports = $reports
        artifacts = $artifacts
        direct_consumers = $directConsumers
    }
}

function New-TestTreeEvidence {
    param(
        [Parameter(Mandatory)] $Context,
        [Parameter(Mandatory)] $TreeContract
    )
    $root = Join-Path $Context.run_root $TreeContract.root_relative_path
    $rows = [Collections.Generic.SortedDictionary[string, object]]::new([StringComparer]::Ordinal)
    foreach ($file in @(Get-ChildItem -LiteralPath $root -File -Recurse -Force)) {
        $relativePath = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        $rows.Add($relativePath, [pscustomobject]@{
            relative_path = $relativePath
            size_bytes = [long]$file.Length
            sha256 = Get-TestSha256 $file.FullName
        })
    }
    $identity = Get-TestCanonicalInventoryIdentity @($rows.Values)
    $inventory = New-TestRunLeaf $Context $TreeContract.inventory_relative_path $identity.text
    return [pscustomobject][ordered]@{
        id = $TreeContract.id
        root_relative_path = $TreeContract.root_relative_path
        file_count = $identity.file_count
        total_size_bytes = $identity.total_size_bytes
        inventory = $inventory
    }
}

function New-TestV2PhysicalEvidence {
    param([Parameter(Mandatory)] $Fixture, [Parameter(Mandatory)] $Context)
    $treeContracts = @($Fixture.Contract.trees)
    Write-TestText `
        (Join-Path $Context.run_root "$($treeContracts[0].root_relative_path)/$($Fixture.Target)") `
        $Fixture.V2BeforeContent
    Write-TestText `
        (Join-Path $Context.run_root "$($treeContracts[1].root_relative_path)/$($Fixture.Target)") `
        $Fixture.V2PatchedContent
    Write-TestText `
        (Join-Path $Context.run_root "$($treeContracts[2].root_relative_path)/include/vulkan/vulkan_core.h") `
        $Fixture.V2VulkanHeaderContent
    Write-TestText `
        (Join-Path $Context.run_root "$($treeContracts[2].root_relative_path)/LICENSE.txt") `
        $Fixture.V2VulkanLicenseContent

    $commands = @()
    for ($index = 0; $index -lt $Fixture.Contract.commands.Count; $index++) {
        $contractCommand = $Fixture.Contract.commands[$index]
        $arguments = if ($contractCommand.arguments_binding -ceq 'configuration.arguments') {
            @($Fixture.Contract.configuration.arguments)
        }
        elseif ($contractCommand.arguments_binding -ceq 'configuration.build_arguments') {
            @($Fixture.Contract.configuration.build_arguments)
        }
        else {
            @($Fixture.Contract.configuration.install_arguments)
        }
        $commands += [pscustomobject][ordered]@{
            id = $contractCommand.id
            tool_id = $contractCommand.tool_id
            script_relative_path = $contractCommand.script_relative_path
            arguments = $arguments
            working_directory_relative_path = $contractCommand.working_directory_relative_path
            exit_code = [long]0
            stdout = New-TestRunLeaf $Context $contractCommand.stdout_relative_path "$($contractCommand.id) stdout`n"
            stderr = New-TestRunLeaf $Context $contractCommand.stderr_relative_path ''
        }
    }

    $artifacts = @(
        foreach ($relativePath in @($Fixture.Contract.configuration.expected_artifacts)) {
            New-TestRunPeLeaf $Context "install/$relativePath"
        }
    )
    $directConsumers = @(New-TestRunPeLeaf $Context 'install/plugins/platforms/qminimal.dll')
    $reports = @(
        foreach ($contractReport in @($Fixture.Contract.reports)) {
            $subjectContent = switch ($contractReport.id) {
                'configure-summary' { Get-TestConfigSummaryText; break }
                'cmake-cache' { Get-TestCMakeCacheText; break }
                'qtgui-freetype-dependency' { "build bin\Qt6Gui.dll lib\Qt6Gui.lib: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = d3d11.lib lib\Qt6BundledFreetype.lib`nbuild plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6Gui.lib lib\Qt6BundledFreetype.lib`n`n"; break }
                default { $null }
            }
            if ($null -ne $subjectContent) {
                $subject = New-TestRunLeaf $Context $contractReport.subject_relative_path $subjectContent
            }
            else {
                $subjectPath = Join-Path $Context.run_root $contractReport.subject_relative_path
                $subject = [pscustomobject][ordered]@{
                    relative_path = $contractReport.subject_relative_path
                    size_bytes = [long](Get-Item -LiteralPath $subjectPath).Length
                    sha256 = Get-TestSha256 $subjectPath
                }
            }
            $reportContent = switch ($contractReport.id) {
                'configure-summary' { $subjectContent; break }
                'cmake-cache' { $subjectContent; break }
                'qtgui-freetype-dependency' { "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`nplugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n"; break }
                'qt6gui-headers' { "Dump of file Qt6Gui.dll`nPE signature found`nFile Type: DLL`nFILE HEADER VALUES`n8664 machine (x64)`n20B magic # (PE32+)`n"; break }
                'qt6gui-imports' { "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following imports:`nQt6Core.dll`nDWrite.dll`nKERNEL32.dll`n"; break }
                'qt6gui-exports' { "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following exports for Qt6Gui.dll`nordinal hint RVA name`n      1    0 00001000 ??0QColor@@QEAA@HHHH@Z`n"; break }
                'qminimal-imports' { "Dump of file qminimal.dll`nFile Type: DLL`nSection contains the following imports:`nQt6Gui.dll`nQt6Core.dll`n"; break }
            }
            [pscustomobject][ordered]@{
                id = $contractReport.id
                producer_id = $contractReport.producer_id
                arguments = @($contractReport.arguments)
                subject = $subject
                exit_code = [long]0
                file = New-TestRunLeaf $Context $contractReport.relative_path $reportContent
            }
        }
    )
    $trees = @(
        foreach ($treeContract in $treeContracts) {
            New-TestTreeEvidence $Context $treeContract
        }
    )
    return [pscustomobject][ordered]@{
        trees = $trees
        commands = $commands
        reports = $reports
        artifacts = $artifacts
        direct_consumers = $directConsumers
    }
}

function New-TestV2Observation {
    param([Parameter(Mandatory)] $Fixture, [Parameter(Mandatory)] $Context)
    $contract = $Fixture.Contract
    return [pscustomobject][ordered]@{
        execution = [pscustomobject][ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
        patch = [pscustomobject][ordered]@{
            changed_relative_paths = @($Fixture.Target)
            target_relative_path = $Fixture.Target
            target_before_sha256 = $contract.inputs.freetype_patch.target_before_sha256
            target_after_sha256 = $contract.inputs.freetype_patch.target_after_sha256
        }
        configuration = [pscustomobject][ordered]@{
            generator = $contract.configuration.generator
            arguments = @($contract.configuration.arguments)
            build_arguments = @($contract.configuration.build_arguments)
            install_arguments = @($contract.configuration.install_arguments)
            installed_configuration = $contract.configuration.installed_configuration
            artifacts = @($contract.configuration.expected_artifacts)
            features = [pscustomobject]$contract.configuration.expected_features
        }
        evidence = New-TestV2PhysicalEvidence $Fixture $Context
    }
}

function New-ValidObservation {
    param([Parameter(Mandatory)] $Fixture, [Parameter(Mandatory)] $Context)
    if ($Fixture.Contract.schema_version -eq 2) {
        return New-TestV2Observation $Fixture $Context
    }
    $contract = $Fixture.Contract
    return [pscustomobject]@{
        execution = [pscustomobject]@{
            exit_code = 0
            stdout = ''
            stderr = ''
        }
        patch = [pscustomobject]@{
            changed_relative_paths = @($Fixture.Target)
            target_relative_path = $Fixture.Target
            target_before_sha256 = $contract.inputs.freetype_patch.target_before_sha256
            target_after_sha256 = $contract.inputs.freetype_patch.target_after_sha256
        }
        configuration = [pscustomobject]@{
            generator = $contract.configuration.generator
            arguments = @($contract.configuration.arguments)
            build_arguments = @($contract.configuration.build_arguments)
            install_arguments = @($contract.configuration.install_arguments)
            installed_configuration = $contract.configuration.installed_configuration
            artifacts = @($contract.configuration.expected_artifacts)
            features = [pscustomobject]$contract.configuration.expected_features
        }
        evidence = New-TestPhysicalEvidence $Fixture $Context
    }
}

function Get-InvokeArguments {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)][string] $RunName,
        [Parameter(Mandatory)][scriptblock] $Execute
    )
    if ($Fixture.Contract.schema_version -ne 2) {
        $Fixture = ConvertTo-QtBaseRefreshV2Fixture $Fixture
    }
    return @{
        ContractPath = $Fixture.ContractPath
        ExpectedContractSha256 = $Fixture.ContractSha256
        RepositoryRoot = $Fixture.Root
        ToolRoot = $Fixture.ToolRoot
        RunRoot = Join-Path $Fixture.RunParent $RunName
        Execute = $Execute
    }
}

function Invoke-TestQtBaseRefresh {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $ContractPath,
        [Parameter(Mandatory)] [string] $ExpectedContractSha256,
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $ToolRoot,
        [Parameter(Mandatory)] [string] $RunRoot,
        [Parameter(Mandatory)] [scriptblock] $Execute
    )
    $entrypoint = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1'
    $orchestrator = Join-Path $RepositoryRoot 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1'
    $arguments = @{
        ContractPath = $ContractPath
        ExpectedContractSha256 = $ExpectedContractSha256
        RepositoryRoot = $RepositoryRoot
        ToolRoot = $ToolRoot
        RunRoot = $RunRoot
    }
    Set-Variable -Name M0QtBaseRefreshTestExecute -Scope Global -Value $Execute -Force
    $fixtureModule = $null
    try {
        Remove-Module -ModuleInfo $script:module -Force -ErrorAction Stop
        $fixtureModule = Import-Module -Name $orchestrator -Force -PassThru
        if ($null -eq $fixtureModule) { throw 'Fixture orchestrator import did not return a module.' }
        return & $entrypoint -ModuleName $fixtureModule.Name -Arguments $arguments
    }
    finally {
        if ($null -ne $fixtureModule) {
            Remove-Module -ModuleInfo $fixtureModule -Force -ErrorAction SilentlyContinue
        }
        $script:module = Import-Module -Name $modulePath -Force -PassThru
        Remove-Variable -Name M0QtBaseRefreshTestExecute -Scope Global -Force -ErrorAction SilentlyContinue
    }
}

function ConvertTo-QtBaseRefreshV2Fixture {
    param([Parameter(Mandatory)] $Fixture)
    $recipeDefinitions = @(
        @('orchestrator', 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1'),
        @('runner', 'experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1'),
        @('entrypoint', 'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1')
    )
    $runnerBridge = @'
function Invoke-M0QtBaseRefreshNative {
    param([Parameter(Mandatory)] $Context)
    $execute = Get-Variable -Name M0QtBaseRefreshTestExecute -Scope Global -ValueOnly -ErrorAction Stop
    & $execute $Context
}
Export-ModuleMember -Function 'Invoke-M0QtBaseRefreshNative'
'@
    $entrypointBridge = @'
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $ModuleName,
    [Parameter(Mandatory)] [hashtable] $Arguments
)
& "$ModuleName\Invoke-M0QtBaseRefresh" @Arguments
'@
    $recipeFiles = @(
        foreach ($definition in $recipeDefinitions) {
            $leaf = if ($definition[0] -ceq 'orchestrator') {
                $destination = Join-Path $Fixture.Root $definition[1]
                $parent = Split-Path -Parent $destination
                if (-not [IO.Directory]::Exists($parent)) { [IO.Directory]::CreateDirectory($parent) | Out-Null }
                [IO.File]::WriteAllBytes($destination, [IO.File]::ReadAllBytes($modulePath))
                [pscustomobject]@{
                    relative_path = $definition[1]
                    size_bytes = [long](Get-Item -LiteralPath $destination).Length
                    sha256 = Get-TestSha256 $destination
                }
            }
            elseif ($definition[0] -ceq 'runner') {
                New-TestRepositoryLeaf $Fixture $definition[1] $runnerBridge
            }
            else {
                New-TestRepositoryLeaf $Fixture $definition[1] $entrypointBridge
            }
            [ordered]@{
                id = $definition[0]
                relative_path = $leaf.relative_path
                size_bytes = $leaf.size_bytes
                sha256 = $leaf.sha256
            }
        }
    )
    $trees = @(
        [ordered]@{
            id = 'qtbase-before'
            root_relative_path = 'source/qtbase-before'
            inventory_relative_path = 'evidence/inventories/qtbase-before.tsv'
            expected = [ordered]@{ file_count = [long]1; total_size_bytes = [long]8; inventory_sha256 = ('1' * 64) }
        },
        [ordered]@{
            id = 'qtbase-patched'
            root_relative_path = 'source/qtbase-patched'
            inventory_relative_path = 'evidence/inventories/qtbase-patched.tsv'
            expected = [ordered]@{ file_count = [long]1; total_size_bytes = [long]8; inventory_sha256 = ('2' * 64) }
        },
        [ordered]@{
            id = 'vulkan'
            root_relative_path = 'source/vulkan'
            inventory_relative_path = 'evidence/inventories/vulkan.tsv'
            expected = [ordered]@{ file_count = [long]1; total_size_bytes = [long]8; inventory_sha256 = ('3' * 64) }
        },
        [ordered]@{
            id = 'install'
            root_relative_path = 'install'
            inventory_relative_path = 'evidence/inventories/install.tsv'
            expected = $null
        }
    )
    $v1 = $Fixture.Contract
    $beforeContent = "before target`n"
    $patchedContent = "patched target`r`n"
    $vulkanHeaderContent = "synthetic vulkan header`n"
    $vulkanLicenseContent = "synthetic vulkan license`n"
    $beforeIdentity = Get-TestTextIdentity $beforeContent
    $patchedIdentity = Get-TestTextIdentity $patchedContent
    $vulkanHeaderIdentity = Get-TestTextIdentity $vulkanHeaderContent
    $vulkanLicenseIdentity = Get-TestTextIdentity $vulkanLicenseContent
    $beforeInventory = Get-TestCanonicalInventoryIdentity @(
        [pscustomobject]@{ relative_path = $Fixture.Target; size_bytes = $beforeIdentity.size_bytes; sha256 = $beforeIdentity.sha256 }
    )
    $patchedInventory = Get-TestCanonicalInventoryIdentity @(
        [pscustomobject]@{ relative_path = $Fixture.Target; size_bytes = $patchedIdentity.size_bytes; sha256 = $patchedIdentity.sha256 }
    )
    $vulkanInventory = Get-TestCanonicalInventoryIdentity @(
        [pscustomobject]@{ relative_path = 'LICENSE.txt'; size_bytes = $vulkanLicenseIdentity.size_bytes; sha256 = $vulkanLicenseIdentity.sha256 },
        [pscustomobject]@{ relative_path = 'include/vulkan/vulkan_core.h'; size_bytes = $vulkanHeaderIdentity.size_bytes; sha256 = $vulkanHeaderIdentity.sha256 }
    )
    $trees[0].expected.file_count = $beforeInventory.file_count
    $trees[0].expected.total_size_bytes = $beforeInventory.total_size_bytes
    $trees[0].expected.inventory_sha256 = $beforeInventory.inventory_sha256
    $trees[1].expected.file_count = $patchedInventory.file_count
    $trees[1].expected.total_size_bytes = $patchedInventory.total_size_bytes
    $trees[1].expected.inventory_sha256 = $patchedInventory.inventory_sha256
    $trees[2].expected.file_count = $vulkanInventory.file_count
    $trees[2].expected.total_size_bytes = $vulkanInventory.total_size_bytes
    $trees[2].expected.inventory_sha256 = $vulkanInventory.inventory_sha256
    $v1.inputs.freetype_patch.target_before_sha256 = $beforeIdentity.sha256
    $v1.inputs.freetype_patch.target_after_sha256 = $patchedIdentity.sha256
    $v1.inputs.vulkan_headers.header_sha256 = $vulkanHeaderIdentity.sha256
    $v1.inputs.vulkan_headers.license_sha256 = $vulkanLicenseIdentity.sha256
    $isolation = [ordered]@{
        run_parent_relative_path = 'q'
        require_new_direct_child = $v1.isolation.require_new_direct_child
        receipt_relative_path = 'evidence/qtbase-refresh-receipt.v2.json'
    }
    $Fixture.RunParent = Join-Path $Fixture.Root 'q'
    [IO.Directory]::CreateDirectory($Fixture.RunParent) | Out-Null
    $commands = @(
        [ordered]@{
            id = 'configure'
            tool_id = 'cmd'
            script_relative_path = 'source/qtbase-patched/configure.bat'
            arguments_binding = 'configuration.arguments'
            working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/configure.stdout.log'
            stderr_relative_path = 'evidence/logs/configure.stderr.log'
        },
        [ordered]@{
            id = 'build'
            tool_id = 'cmake'
            script_relative_path = $null
            arguments_binding = 'configuration.build_arguments'
            working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/build.stdout.log'
            stderr_relative_path = 'evidence/logs/build.stderr.log'
        },
        [ordered]@{
            id = 'install'
            tool_id = 'cmake'
            script_relative_path = $null
            arguments_binding = 'configuration.install_arguments'
            working_directory_relative_path = 'b'
            stdout_relative_path = 'evidence/logs/install.stdout.log'
            stderr_relative_path = 'evidence/logs/install.stderr.log'
        }
    )
    $reports = @(
        [ordered]@{ id = 'configure-summary'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'b/config.summary'; relative_path = 'evidence/reports/configure-summary.txt' },
        [ordered]@{ id = 'cmake-cache'; producer_id = 'runner'; arguments = @('copy-verbatim'); subject_relative_path = 'b/CMakeCache.txt'; relative_path = 'evidence/reports/cmake-cache.txt' },
        [ordered]@{ id = 'qtgui-freetype-dependency'; producer_id = 'runner'; arguments = @('analyze-freetype-link-graph'); subject_relative_path = 'b/CMakeFiles/impl-RelWithDebInfo.ninja'; relative_path = 'evidence/reports/qtgui-freetype-dependency.txt' },
        [ordered]@{ id = 'qt6gui-headers'; producer_id = 'dumpbin'; arguments = @('/nologo', '/headers'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-headers.txt' },
        [ordered]@{ id = 'qt6gui-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-imports.txt' },
        [ordered]@{ id = 'qt6gui-exports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/exports'); subject_relative_path = 'install/bin/Qt6Gui.dll'; relative_path = 'evidence/reports/qt6gui-exports.txt' },
        [ordered]@{ id = 'qminimal-imports'; producer_id = 'dumpbin'; arguments = @('/nologo', '/imports'); subject_relative_path = 'install/plugins/platforms/qminimal.dll'; relative_path = 'evidence/reports/qminimal-imports.txt' }
    )
    $v2 = [ordered]@{
        schema_version = [long]2
        refresh_id = 'm0.2-qtbase-freetype-backport-v2'
        product = $v1.product
        recipe = [ordered]@{
            files = $recipeFiles
            runner_export = 'Invoke-M0QtBaseRefreshNative'
        }
        inputs = $v1.inputs
        tools = $v1.tools
        environment = $v1.environment
        isolation = $isolation
        trees = $trees
        commands = $commands
        reports = $reports
        configuration = $v1.configuration
    }
    $path = Join-Path $Fixture.Root 'qtbase-refresh-contract.v2.json'
    $sha256 = Write-TestJson -LiteralPath $path -Document $v2
    $Fixture.Contract = $v2
    $Fixture.ContractPath = $path
    $Fixture.ContractSha256 = $sha256
    $Fixture | Add-Member -NotePropertyName V2BeforeContent -NotePropertyValue $beforeContent -Force
    $Fixture | Add-Member -NotePropertyName V2PatchedContent -NotePropertyValue $patchedContent -Force
    $Fixture | Add-Member -NotePropertyName V2VulkanHeaderContent -NotePropertyValue $vulkanHeaderContent -Force
    $Fixture | Add-Member -NotePropertyName V2VulkanLicenseContent -NotePropertyValue $vulkanLicenseContent -Force
    return $Fixture
}

function Write-ContractMutant {
    param([Parameter(Mandatory)] $Fixture, [Parameter(Mandatory)] $Document, [Parameter(Mandatory)][string] $Name)
    $path = Join-Path $Fixture.Root "$Name.json"
    $sha256 = Write-TestJson -LiteralPath $path -Document $Document
    return [pscustomobject]@{ Path = $path; Sha256 = $sha256 }
}

Test-Case 'contract validation is hash-bound and returns the accepted closed document' {
    $fixture = New-QtBaseRefreshFixture
    $accepted = Assert-M0QtBaseRefreshContract `
        -LiteralPath $fixture.ContractPath `
        -ExpectedSha256 $fixture.ContractSha256
    Assert-Equal $accepted.refresh_id 'm0.2-qtbase-freetype-backport-v1'
    Assert-Rejected {
        Assert-M0QtBaseRefreshContract -LiteralPath $fixture.ContractPath -ExpectedSha256 ('0' * 64)
    } 'contract|sha256|hash|digest'
}

Test-Case 'schema v1 verifier remains compatible with the receipt shape in commit 33bce70' {
    $fixture = New-QtBaseRefreshFixture
    $receiptPath = Join-Path $fixture.Root 'historical-receipt.v1.json'
    $receipt = [ordered]@{
        schema_version = [long]1
        receipt_id = 'm0.2-qtbase-freetype-backport-v1-receipt'
        contract_sha256 = $fixture.ContractSha256
        refresh_id = 'm0.2-qtbase-freetype-backport-v1'
        captured_utc = '2026-08-27T12:00:00.0000000+00:00'
        execution = [ordered]@{ exit_code = [long]0; stdout = ''; stderr = '' }
        patch = [ordered]@{
            changed_relative_paths = @($fixture.Target)
            target_relative_path = $fixture.Target
            target_before_sha256 = $fixture.Contract.inputs.freetype_patch.target_before_sha256
            target_after_sha256 = $fixture.Contract.inputs.freetype_patch.target_after_sha256
        }
        configuration = [ordered]@{
            generator = $fixture.Contract.configuration.generator
            arguments = @($fixture.Contract.configuration.arguments)
            build_arguments = @($fixture.Contract.configuration.build_arguments)
            install_arguments = @($fixture.Contract.configuration.install_arguments)
            installed_configuration = $fixture.Contract.configuration.installed_configuration
            artifacts = @($fixture.Contract.configuration.expected_artifacts)
            features = $fixture.Contract.configuration.expected_features
        }
        limits = @(
            'diagnostic M0 rebuild only',
            'does not approve a Qt toolkit decision',
            'does not approve redistribution or publication',
            'does not replace the official SDK or historical packages'
        )
    }
    $receiptSha256 = Write-TestJson $receiptPath $receipt
    $accepted = Assert-M0QtBaseRefreshReceipt `
        -LiteralPath $receiptPath `
        -ExpectedReceiptSha256 $receiptSha256 `
        -ContractPath $fixture.ContractPath `
        -ExpectedContractSha256 $fixture.ContractSha256
    Assert-Equal $accepted.schema_version ([long]1)
    Assert-True ($null -eq $accepted.PSObject.Properties['evidence']) 'Historical v1 verification invented a new evidence field.'
}

Test-Case 'the historical schema v1 contract and sidecar remain byte-for-byte immutable' {
    $v1ContractPath = Join-Path $PSScriptRoot '../qtbase-refresh-contract.v1.json'
    $v1SidecarPath = Join-Path $PSScriptRoot '../qtbase-refresh-contract.v1.json.sha256'
    Assert-Equal ([long](Get-Item -LiteralPath $v1ContractPath).Length) ([long]15475) 'Historical v1 contract size drifted.'
    Assert-Equal (Get-TestSha256 $v1ContractPath) '2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6' `
        'Historical v1 contract SHA-256 drifted.'
    $sidecarBytes = [IO.File]::ReadAllBytes($v1SidecarPath)
    $expectedSidecar = [Text.UTF8Encoding]::new($false).GetBytes(
        "2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6 *qtbase-refresh-contract.v1.json`n"
    )
    Assert-Equal ([Convert]::ToBase64String($sidecarBytes)) ([Convert]::ToBase64String($expectedSidecar)) `
        'Historical v1 sidecar bytes drifted.'
}

Test-Case 'the production schema v2 contract and sidecar authenticate the committed recipe' {
    $productionContractPath = Join-Path $PSScriptRoot '../qtbase-refresh-contract.v2.json'
    $productionSidecarPath = Join-Path $PSScriptRoot '../qtbase-refresh-contract.v2.json.sha256'
    Assert-True ([IO.File]::Exists($productionContractPath)) 'Production schema-v2 contract is missing.'
    Assert-True ([IO.File]::Exists($productionSidecarPath)) 'Production schema-v2 sidecar is missing.'

    $contractBytes = [IO.File]::ReadAllBytes($productionContractPath)
    $sidecarBytes = [IO.File]::ReadAllBytes($productionSidecarPath)
    foreach ($binding in @(
            @('contract', $contractBytes),
            @('sidecar', $sidecarBytes)
        )) {
        $bytes = [byte[]]$binding[1]
        Assert-True ($bytes.Length -gt 0) "Production schema-v2 $($binding[0]) is empty."
        Assert-True (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf)) `
            "Production schema-v2 $($binding[0]) contains a UTF-8 BOM."
        try { $null = [Text.UTF8Encoding]::new($false, $true).GetString($bytes) }
        catch { throw "Production schema-v2 $($binding[0]) is not strict UTF-8." }
    }

    $sidecarText = [Text.UTF8Encoding]::new($false, $true).GetString($sidecarBytes)
    $sidecarMatch = [regex]::Match(
        $sidecarText,
        '\A(?<sha256>[0-9a-f]{64}) \*qtbase-refresh-contract\.v2\.json\n\z',
        [Text.RegularExpressions.RegexOptions]::CultureInvariant
    )
    Assert-True $sidecarMatch.Success 'Production schema-v2 sidecar is not canonical.'
    $expectedContractSha256 = $sidecarMatch.Groups['sha256'].Value
    Assert-Equal (Get-TestSha256 $productionContractPath) $expectedContractSha256 `
        'Production schema-v2 sidecar does not authenticate the contract bytes.'

    $accepted = Assert-M0QtBaseRefreshContract `
        -LiteralPath $productionContractPath `
        -ExpectedSha256 $expectedContractSha256
    Assert-Equal $accepted.schema_version ([long]2)
    Assert-Equal $accepted.refresh_id 'm0.2-qtbase-freetype-backport-v2'
    Assert-Equal $accepted.isolation.run_parent_relative_path 'q' `
        'Production RunRoot parent does not preserve the measured Windows path budget.'
    Assert-Equal $accepted.isolation.receipt_relative_path 'evidence/qtbase-refresh-receipt.v2.json'
    Assert-SequenceEqual @($accepted.commands | ForEach-Object working_directory_relative_path) @('b', 'b', 'b') `
        'Production build working directories do not preserve the measured Windows path budget.'
    Assert-SequenceEqual @($accepted.reports | Select-Object -First 3 | ForEach-Object subject_relative_path) @(
        'b/config.summary', 'b/CMakeCache.txt', 'b/CMakeFiles/impl-RelWithDebInfo.ninja'
    ) 'Production build report subjects do not preserve the measured Windows path budget.'
    $projectedRunRoot = Join-Path (Join-Path $repositoryRoot $accepted.isolation.run_parent_relative_path) ('v' * 26)
    $projectedWorstObject = Join-Path $projectedRunRoot (
        'b/src/platformsupport/devicediscovery/CMakeFiles/DeviceDiscoverySupportPrivate.dir/' +
        'RelWithDebInfo/' + ('0' * 32) + '/mocs_compilation_RelWithDebInfo.cpp.obj'
    )
    Assert-True ($projectedWorstObject.Length -le 245) `
        "Projected worst Qtbase object path is $($projectedWorstObject.Length) characters; the closed budget is 245."
    foreach ($spec in @($accepted.recipe.files)) {
        $leaf = Join-Path $repositoryRoot $spec.relative_path
        Assert-True ([IO.File]::Exists($leaf)) "Production recipe leaf '$($spec.id)' is missing."
        Assert-Equal ([long](Get-Item -LiteralPath $leaf).Length) ([long]$spec.size_bytes) `
            "Production recipe leaf '$($spec.id)' size drifted."
        Assert-Equal (Get-TestSha256 $leaf) $spec.sha256 `
            "Production recipe leaf '$($spec.id)' hash drifted."
    }
}

Test-Case 'contract v2 externally pins the closed production recipe and physical tree layout' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $accepted = Assert-M0QtBaseRefreshContract `
        -LiteralPath $fixture.ContractPath `
        -ExpectedSha256 $fixture.ContractSha256
    Assert-Equal $accepted.schema_version ([long]2)
    Assert-Equal $accepted.recipe.runner_export 'Invoke-M0QtBaseRefreshNative'

    foreach ($kind in @('recipe-hash', 'recipe-path', 'recipe-id', 'runner-export', 'tree-root', 'tree-inventory', 'install-pin')) {
        $mutant = Copy-TestJson $fixture.Contract
        if ($kind -eq 'recipe-hash') { $mutant.recipe.files[1].sha256 = 'f' * 63 }
        elseif ($kind -eq 'recipe-path') { $mutant.recipe.files[1].relative_path = '../runner.psm1' }
        elseif ($kind -eq 'recipe-id') { $mutant.recipe.files[1].id = 'orchestrator' }
        elseif ($kind -eq 'runner-export') { $mutant.recipe.runner_export = 'Invoke-Arbitrary' }
        elseif ($kind -eq 'tree-root') { $mutant.trees[0].root_relative_path = '../outside' }
        elseif ($kind -eq 'tree-inventory') { $mutant.trees[0].inventory_relative_path = 'evidence/other.tsv' }
        else { $mutant.trees[3].expected = [pscustomobject]@{ file_count = [long]1; total_size_bytes = [long]1; inventory_sha256 = ('a' * 64) } }
        $written = Write-ContractMutant $fixture $mutant "v2-$kind"
        Assert-Rejected {
            Assert-M0QtBaseRefreshContract -LiteralPath $written.Path -ExpectedSha256 $written.Sha256
        } 'contract|recipe|runner|tree|layout|path|sha256|digest|duplicate|expected|install'
    }
}

Test-Case 'contract identity fields and one-item sequences reject JSON shape coercion' {
    foreach ($schema in @(1, 2)) {
        foreach ($kind in @('refresh-id-array', 'qt-version-array', 'generator-array', 'installed-configuration-array')) {
            $fixture = New-QtBaseRefreshFixture
            if ($schema -eq 2) { $fixture = ConvertTo-QtBaseRefreshV2Fixture $fixture }
            $mutant = Copy-TestJson $fixture.Contract
            if ($kind -eq 'refresh-id-array') { $mutant.refresh_id = @([string]$mutant.refresh_id) }
            elseif ($kind -eq 'qt-version-array') { $mutant.product.qt_version = @([string]$mutant.product.qt_version) }
            elseif ($kind -eq 'generator-array') { $mutant.configuration.generator = @([string]$mutant.configuration.generator) }
            else { $mutant.configuration.installed_configuration = @([string]$mutant.configuration.installed_configuration) }
            $written = Write-ContractMutant $fixture $mutant "shape-v$schema-$kind"
            Assert-Rejected {
                Assert-M0QtBaseRefreshContract $written.Path $written.Sha256
            } 'contract|identity|string|generator|configuration|shape|sequence|accepted'
        }
    }

    $v2 = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $scalarSequence = Copy-TestJson $v2.Contract
    $scalarSequence.reports[0].arguments = 'copy-verbatim'
    $writtenScalar = Write-ContractMutant $v2 $scalarSequence 'shape-v2-scalar-report-arguments'
    Assert-Rejected {
        Assert-M0QtBaseRefreshContract $writtenScalar.Path $writtenScalar.Sha256
    } 'contract|report|arguments|array|sequence|shape'
}

Test-Case 'RunRoot creation uses an exclusive atomic directory primitive' {
    $root = Join-Path $testRoot ('exclusive-directory-' + [Guid]::NewGuid().ToString('N'))
    & $script:module {
        param($literalPath)
        New-M0ExclusiveDirectory -LiteralPath $literalPath
    } $root
    Assert-True ([IO.Directory]::Exists($root)) 'Exclusive directory creation did not create the requested RunRoot.'
    Assert-Rejected {
        & $script:module {
            param($literalPath)
            New-M0ExclusiveDirectory -LiteralPath $literalPath
        } $root
    } 'exist|already|exclusive|directory|cannot create'

    $source = Get-Content -Raw -LiteralPath $modulePath
    Assert-True (-not $source.Contains('[IO.Directory]::CreateDirectory($runRootFull)', [StringComparison]::Ordinal)) `
        'RunRoot still uses the nonexclusive Directory.CreateDirectory check/create sequence.'
}

Test-Case 'tool paths remain contained when ToolRoot is a Windows volume root' {
    $windows = [Environment]::GetFolderPath([Environment+SpecialFolder]::Windows)
    $volumeRoot = [IO.Path]::GetPathRoot($windows)
    $relative = [IO.Path]::GetRelativePath($volumeRoot, (Join-Path $windows 'System32/cmd.exe')).Replace('\', '/')
    $resolved = & $script:module {
        param($root, $path)
        Resolve-M0ContainedPath $root $path 'volume-root tool'
    } $volumeRoot $relative
    Assert-Equal $resolved ([IO.Path]::GetFullPath((Join-Path $volumeRoot $relative)))
}

Test-Case 'contract v2 closes native command bindings and the semantic report set' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $accepted = Assert-M0QtBaseRefreshContract $fixture.ContractPath $fixture.ContractSha256
    Assert-Equal @($accepted.commands).Count 3
    Assert-Equal @($accepted.reports).Count 7

    foreach ($kind in @(
            'command-order', 'command-tool', 'command-script', 'command-binding', 'command-cwd', 'command-log',
            'command-extra', 'report-order', 'report-producer', 'report-arguments', 'report-subject', 'report-path', 'report-extra'
        )) {
        $mutant = Copy-TestJson $fixture.Contract
        if ($kind -eq 'command-order') { $mutant.commands = @($mutant.commands[1], $mutant.commands[0], $mutant.commands[2]) }
        elseif ($kind -eq 'command-tool') { $mutant.commands[0].tool_id = 'cmake' }
        elseif ($kind -eq 'command-script') { $mutant.commands[1].script_relative_path = 'source/qtbase-patched/configure.bat' }
        elseif ($kind -eq 'command-binding') { $mutant.commands[2].arguments_binding = 'configuration.build_arguments' }
        elseif ($kind -eq 'command-cwd') { $mutant.commands[0].working_directory_relative_path = '../outside' }
        elseif ($kind -eq 'command-log') { $mutant.commands[0].stdout_relative_path = 'evidence/logs/other.log' }
        elseif ($kind -eq 'command-extra') { $mutant.commands[0] | Add-Member -NotePropertyName arguments -NotePropertyValue @('arbitrary') }
        elseif ($kind -eq 'report-order') { $mutant.reports = @($mutant.reports[1], $mutant.reports[0]) + @($mutant.reports[2..6]) }
        elseif ($kind -eq 'report-producer') { $mutant.reports[3].producer_id = 'runner' }
        elseif ($kind -eq 'report-arguments') { $mutant.reports[3].arguments = @('/headers') }
        elseif ($kind -eq 'report-subject') { $mutant.reports[3].subject_relative_path = 'install/bin/Qt6Core.dll' }
        elseif ($kind -eq 'report-path') { $mutant.reports[0].relative_path = '../report.txt' }
        else { $mutant.reports[0] | Add-Member -NotePropertyName producer -NotePropertyValue 'callback' }
        $written = Write-ContractMutant $fixture $mutant "v2-bindings-$kind"
        Assert-Rejected {
            Assert-M0QtBaseRefreshContract $written.Path $written.Sha256
        } 'contract|command|tool|script|binding|working|log|report|path|exact|sequence'
    }
}

Test-Case 'production entrypoint authenticates and invokes only the contract-pinned runner export' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $runnerSpec = @($fixture.Contract.recipe.files | Where-Object id -ceq 'runner')[0]
    $runnerPath = Join-Path $fixture.Root $runnerSpec.relative_path
    Write-TestText $runnerPath @'
function Invoke-M0QtBaseRefreshNative {
    param([Parameter(Mandatory)] $Context)
    throw 'synthetic pinned runner invoked'
}
Export-ModuleMember -Function 'Invoke-M0QtBaseRefreshNative'
'@
    $runnerSpec.size_bytes = [long](Get-Item -LiteralPath $runnerPath).Length
    $runnerSpec.sha256 = Get-TestSha256 $runnerPath
    $fixture.ContractSha256 = Write-TestJson $fixture.ContractPath $fixture.Contract
    $runRoot = Join-Path $fixture.RunParent 'pinned-runner'
    $arguments = Get-InvokeArguments $fixture 'pinned-runner' { throw 'fixture callback must not replace the pinned runner' }

    Assert-Rejected {
        Invoke-TestQtBaseRefresh @arguments
    } 'synthetic pinned runner invoked'
    Assert-True (-not [IO.File]::Exists((Join-Path $runRoot $fixture.Contract.isolation.receipt_relative_path))) 'Failing pinned runner exposed a receipt.'

    $tampered = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $tamperedRunner = @($tampered.Contract.recipe.files | Where-Object id -ceq 'runner')[0]
    Write-TestText (Join-Path $tampered.Root $tamperedRunner.relative_path) 'tampered after external contract hash'
    $tamperedRunRoot = Join-Path $tampered.RunParent 'tampered-runner'
    $tamperedArguments = Get-InvokeArguments $tampered 'tampered-runner' { throw 'tampered runner must not execute' }
    Assert-Rejected {
        Invoke-TestQtBaseRefresh @tamperedArguments
    } 'runner|recipe|sha256|hash|size|pin|authentic'
    Assert-True (-not [IO.Directory]::Exists($tamperedRunRoot)) 'Tampered runner created a RunRoot.'
}

Test-Case 'production launcher authenticates contract and every recipe leaf before import in a fresh process' {
    $nominal = New-TestLauncherFixture
    $nominalOutput = & pwsh -NoProfile -NonInteractive -File $nominal.Launcher 2>&1 | Out-String
    Assert-Equal $LASTEXITCODE 0 "Nominal launcher fixture failed: $nominalOutput"
    Assert-True ([IO.File]::Exists($nominal.ImportedMarker)) 'The authenticated orchestrator was not imported.'
    Assert-True ([IO.File]::Exists($nominal.InvokedMarker)) 'The authenticated orchestrator was not invoked.'
    $invokedRunRoot = [IO.File]::ReadAllText($nominal.InvokedMarker)
    Assert-Equal ([IO.Path]::GetDirectoryName($invokedRunRoot)) $nominal.RunParent `
        'The launcher did not create its RunRoot under the contract-pinned direct parent.'
    Assert-True ([IO.Path]::GetFileName($invokedRunRoot) -cmatch '^[0-9a-v]{26}$') `
        'The launcher RunRoot name must retain all 128 GUID bits in canonical lowercase base32.'
    Assert-True ([IO.Path]::GetFileName($invokedRunRoot)[25] -cmatch '^[048cgkos]$') `
        'The launcher RunRoot base32 padding bits are not canonical.'

    foreach ($kind in @('contract', 'orchestrator', 'runner', 'entrypoint')) {
        $fixture = New-TestLauncherFixture
        $tamperPath = switch ($kind) {
            'contract' { $fixture.Contract; break }
            'orchestrator' { $fixture.Orchestrator; break }
            'runner' { $fixture.Runner; break }
            'entrypoint' { $fixture.Launcher; break }
        }
        [IO.File]::AppendAllText($tamperPath, "tampered after external hash`n", [Text.UTF8Encoding]::new($false))
        $output = & pwsh -NoProfile -NonInteractive -File $fixture.Launcher 2>&1 | Out-String
        Assert-True ($LASTEXITCODE -ne 0) "Launcher accepted tampered $kind bytes. Output: $output"
        Assert-True (-not [IO.File]::Exists($fixture.ImportedMarker)) "Tampered $kind bytes reached orchestrator import."
        Assert-True (-not [IO.File]::Exists($fixture.InvokedMarker)) "Tampered $kind bytes reached orchestrator invocation."
    }
}

Test-Case 'execution rejects an authenticated byte copy loaded from a different physical orchestrator leaf' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $execute = { param($context) New-TestV2Observation $fixture $context }.GetNewClosure()
    $invoke = Get-InvokeArguments $fixture 'wrong-physical-orchestrator' $execute
    $entrypoint = Join-Path $fixture.Root 'experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1'
    $arguments = @{
        ContractPath = $invoke.ContractPath
        ExpectedContractSha256 = $invoke.ExpectedContractSha256
        RepositoryRoot = $invoke.RepositoryRoot
        ToolRoot = $invoke.ToolRoot
        RunRoot = $invoke.RunRoot
    }
    Set-Variable -Name M0QtBaseRefreshTestExecute -Scope Global -Value $execute -Force
    try {
        Assert-Rejected {
            & $entrypoint -ModuleName $script:module.Name -Arguments $arguments
        } 'orchestrator|physical|recipe|identity|path|leaf'
    }
    finally {
        Remove-Variable -Name M0QtBaseRefreshTestExecute -Scope Global -Force -ErrorAction SilentlyContinue
    }
}

Test-Case 'physical tree validation recomputes the canonical inventory instead of trusting its TSV' {
    $root = Join-Path $testRoot ('physical-tree-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $leafPath = Join-Path $root 'leaf.bin'
    Write-TestText $leafPath 'physical bytes'
    $leafSize = [long](Get-Item -LiteralPath $leafPath).Length
    $leafSha256 = Get-TestSha256 $leafPath
    $inventoryPath = Join-Path $testRoot ('inventory-' + [Guid]::NewGuid().ToString('N') + '.tsv')
    Write-TestText $inventoryPath "leaf.bin`t$leafSize`t$leafSha256`n"
    $inventorySha256 = Get-TestSha256 $inventoryPath

    $accepted = & $script:module {
        param($treeRoot, $inventory, $count, $total, $hash)
        Assert-M0PhysicalTreeInventory $treeRoot $inventory $count $total $hash 'test physical tree'
    } $root $inventoryPath ([long]1) $leafSize $inventorySha256
    Assert-Equal $accepted.inventory_sha256 $inventorySha256

    $forgedLeafSha256 = '0' * 64
    Write-TestText $inventoryPath "leaf.bin`t$leafSize`t$forgedLeafSha256`n"
    $forgedInventorySha256 = Get-TestSha256 $inventoryPath
    Assert-Rejected {
        & $script:module {
            param($treeRoot, $inventory, $count, $total, $hash)
            Assert-M0PhysicalTreeInventory $treeRoot $inventory $count $total $hash 'forged physical tree'
        } $root $inventoryPath ([long]1) $leafSize $forgedInventorySha256
    } 'inventory does not match the physical tree'
}

Test-Case 'PE validation rejects a directory-free synthetic DLL blob' {
    $path = Join-Path $testRoot ('signature-only-' + [Guid]::NewGuid().ToString('N') + '.dll')
    $bytes = [byte[]]::new(1024)
    $bytes[0] = 0x4d
    $bytes[1] = 0x5a
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3c)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes([uint16]1).CopyTo($bytes, 0x86)
    [BitConverter]::GetBytes([uint16]0x00f0).CopyTo($bytes, 0x94)
    [BitConverter]::GetBytes([uint16]0x2022).CopyTo($bytes, 0x96)
    [BitConverter]::GetBytes([uint16]0x020b).CopyTo($bytes, 0x98)
    [BitConverter]::GetBytes([uint32]0x200).CopyTo($bytes, 0x9c)
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($bytes, 0xa8)
    [BitConverter]::GetBytes([uint64]0x0000000180000000).CopyTo($bytes, 0xb0)
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($bytes, 0xb8)
    [BitConverter]::GetBytes([uint32]0x200).CopyTo($bytes, 0xbc)
    [BitConverter]::GetBytes([uint32]0x2000).CopyTo($bytes, 0xd0)
    [BitConverter]::GetBytes([uint32]0x200).CopyTo($bytes, 0xd4)
    [BitConverter]::GetBytes([uint16]2).CopyTo($bytes, 0xdc)
    [BitConverter]::GetBytes([uint16]0x8160).CopyTo($bytes, 0xde)
    [BitConverter]::GetBytes([uint32]16).CopyTo($bytes, 0x104)
    [Text.Encoding]::ASCII.GetBytes('.text').CopyTo($bytes, 0x188)
    [BitConverter]::GetBytes([uint32]0x100).CopyTo($bytes, 0x190)
    [BitConverter]::GetBytes([uint32]0x1000).CopyTo($bytes, 0x194)
    [BitConverter]::GetBytes([uint32]0x200).CopyTo($bytes, 0x198)
    [BitConverter]::GetBytes([uint32]0x200).CopyTo($bytes, 0x19c)
    [BitConverter]::GetBytes([uint32]0x60000020).CopyTo($bytes, 0x1ac)
    $bytes[0x200] = 0xc3
    [IO.File]::WriteAllBytes($path, $bytes)
    Assert-Rejected {
        & $script:module { param($leaf) Assert-M0PeX64 $leaf 'signature-only PE' } $path
    } 'PE|optional|section|DLL|complete|header|image|export|import|directory'
}

Test-Case 'physical tree validation can retain leaves and directories through the receipt commit point' {
    $root = Join-Path $testRoot ('retained-tree-' + [Guid]::NewGuid().ToString('N'))
    $treeRoot = Join-Path $root 'tree'
    $leafPath = Join-Path $treeRoot 'nested/leaf.txt'
    $inventoryPath = Join-Path $root 'tree.tsv'
    Write-TestText $leafPath 'retained physical bytes'
    $identity = Get-TestTextIdentity 'retained physical bytes'
    $inventory = Get-TestCanonicalInventoryIdentity @(
        [pscustomobject]@{ relative_path = 'nested/leaf.txt'; size_bytes = $identity.size_bytes; sha256 = $identity.sha256 }
    )
    Write-TestText $inventoryPath $inventory.text
    $leases = [Collections.Generic.List[object]]::new()
    try {
        & $script:module {
            param($physicalRoot, $physicalInventory, $count, $total, $hash, $sink)
            Assert-M0PhysicalTreeInventory `
                -TreeRoot $physicalRoot `
                -InventoryPath $physicalInventory `
                -ExpectedFileCount $count `
                -ExpectedTotalSizeBytes $total `
                -ExpectedInventorySha256 $hash `
                -Context 'retained physical tree' `
                -LeaseSink $sink
        } $treeRoot $inventoryPath $inventory.file_count $inventory.total_size_bytes $inventory.inventory_sha256 $leases
        Assert-True ($leases.Count -ge 3) 'Physical tree did not retain its root, nested directory and leaf.'
        Assert-Rejected {
            [IO.File]::AppendAllText($leafPath, 'tamper', [Text.UTF8Encoding]::new($false))
        } 'used by another process|sharing|access|physical|lease'
        [IO.File]::WriteAllText((Join-Path $treeRoot 'injected.txt'), 'tamper', [Text.UTF8Encoding]::new($false))
        Assert-Rejected {
            & $script:module {
                param($sink)
                Assert-M0PhysicalFileLeasesUnchanged $sink 'retained tree commit point'
            } $leases
        } 'physical tree|count|inventory|commit point|changed'
    }
    finally {
        & $script:module { param($sink) Close-M0PhysicalFileLeases $sink } $leases
    }
}

Test-Case 'schema v2 core derives recipe identity and verifies every declared physical tree' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $execute = { param($context) New-TestV2Observation $fixture $context }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'v2-physical-evidence' $execute
    $result = Invoke-TestQtBaseRefresh @arguments
    Assert-True ([IO.File]::Exists($result.receipt_path)) 'Schema v2 did not publish its receipt.'
    Assert-True ($result.receipt_path.EndsWith('qtbase-refresh-receipt.v2.json', [StringComparison]::Ordinal)) 'Schema v2 published a v1 receipt path.'

    $accepted = Assert-M0QtBaseRefreshReceipt `
        -LiteralPath $result.receipt_path `
        -ExpectedReceiptSha256 $result.receipt_sha256 `
        -ContractPath $fixture.ContractPath `
        -ExpectedContractSha256 $fixture.ContractSha256 `
        -RepositoryRoot $fixture.Root
    Assert-Equal $accepted.schema_version ([long]2)

    $forgedFixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $forgedExecute = {
        param($context)
        $observation = New-TestV2Observation $forgedFixture $context
        $treePath = Join-Path $context.run_root "$($forgedFixture.Contract.trees[0].root_relative_path)/$($forgedFixture.Target)"
        Write-TestText $treePath 'tree bytes changed after its TSV was written'
        return $observation
    }.GetNewClosure()
    $forgedArguments = Get-InvokeArguments $forgedFixture 'v2-forged-tree' $forgedExecute
    Assert-Rejected { Invoke-TestQtBaseRefresh @forgedArguments } 'inventory does not match the physical tree|physical tree count or total differs'
    Assert-True (-not [IO.File]::Exists((Join-Path $forgedArguments.RunRoot $forgedFixture.Contract.isolation.receipt_relative_path))) 'A forged v2 tree published a receipt.'
}

Test-Case 'schema v2 rejects opaque report bytes even when their physical hash is self-consistent' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $execute = {
        param($context)
        $observation = New-TestV2Observation $fixture $context
        $report = @($observation.evidence.reports | Where-Object id -CEQ 'qt6gui-imports')[0]
        $reportPath = Join-Path $context.run_root $report.file.relative_path
        Write-TestText $reportPath "opaque callback report`n"
        $report.file.size_bytes = [long](Get-Item -LiteralPath $reportPath).Length
        $report.file.sha256 = Get-TestSha256 $reportPath
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'v2-opaque-report' $execute
    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'semantic content is invalid'
    Assert-True (-not [IO.File]::Exists((Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path))) 'Opaque report bytes published a v2 receipt.'
}

Test-Case 'verbatim CMake reports preserve authenticated CRLF source bytes and final blank line' {
    $root = Join-Path $testRoot ('verbatim-crlf-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'CMakeCache.txt'
    $report = Join-Path $root 'cmake-cache.txt'
    $text = (Get-TestCMakeCacheText).Replace("`n", "`r`n")
    [IO.File]::WriteAllText($subject, $text, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($report, $text, [Text.UTF8Encoding]::new($false))
    $spec = [pscustomobject]@{ id = 'cmake-cache' }
    & $script:module {
        param($reportSpec, $reportPath, $subjectPath)
        Assert-M0V2SemanticReport $reportSpec $reportPath $subjectPath 'verbatim CMake report'
    } $spec $report $subject
}

Test-Case 'configure summary accepts the authenticated Qt 6.11.2 CRLF preamble' {
    $root = Join-Path $testRoot ('config-summary-real-preamble-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'config.summary'
    $report = Join-Path $root 'configure-summary.txt'
    $text = (Get-TestConfigSummaryText).Replace("`n", "`r`n")
    [IO.File]::WriteAllText($subject, $text, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($report, $text, [Text.UTF8Encoding]::new($false))
    $spec = [pscustomobject]@{ id = 'configure-summary' }
    & $script:module {
        param($reportSpec, $reportPath, $subjectPath)
        Assert-M0V2SemanticReport $reportSpec $reportPath $subjectPath 'authenticated Qt configure summary'
    } $spec $report $subject
}

Test-Case 'verbatim reports reject report-specific final newline drift' {
    $configSummary = Get-TestConfigSummaryText
    $cmakeCache = Get-TestCMakeCacheText
    $cases = @(
        @{
            id = 'configure-summary'
            subject_name = 'config.summary'
            report_name = 'configure-summary.txt'
            text = $configSummary.Substring(0, $configSummary.Length - 1)
            expected_error = 'exactly 1 final newline'
        },
        @{
            id = 'configure-summary'
            subject_name = 'config.summary'
            report_name = 'configure-summary.txt'
            text = $configSummary + "`n"
            expected_error = 'exactly 1 final newline'
        },
        @{
            id = 'cmake-cache'
            subject_name = 'CMakeCache.txt'
            report_name = 'cmake-cache.txt'
            text = $cmakeCache.Substring(0, $cmakeCache.Length - 1)
            expected_error = 'exactly 2 final newline'
        },
        @{
            id = 'cmake-cache'
            subject_name = 'CMakeCache.txt'
            report_name = 'cmake-cache.txt'
            text = $cmakeCache + "`n"
            expected_error = 'exactly 2 final newline'
        }
    )
    foreach ($case in $cases) {
        $root = Join-Path $testRoot ('verbatim-final-newline-drift-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $subject = Join-Path $root $case.subject_name
        $report = Join-Path $root $case.report_name
        Write-TestText $subject $case.text
        Write-TestText $report $case.text
        Assert-Rejected {
            & $script:module {
                param($reportId, $reportPath, $subjectPath)
                Assert-M0V2SemanticReport ([pscustomobject]@{ id = $reportId }) $reportPath $subjectPath 'drifted final newline report'
            } $case.id $report $subject
        } $case.expected_error
    }
}

Test-Case 'verbatim configure summary rejects mixed CRLF and LF source bytes' {
    $root = Join-Path $testRoot ('config-summary-mixed-newlines-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'config.summary'
    $report = Join-Path $root 'configure-summary.txt'
    $text = (Get-TestConfigSummaryText).Replace("`n", "`r`n").Replace(
        "Build options:`r`n",
        "Build options:`n",
        [StringComparison]::Ordinal)
    [IO.File]::WriteAllText($subject, $text, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($report, $text, [Text.UTF8Encoding]::new($false))
    Assert-Rejected {
        & $script:module {
            param($reportPath, $subjectPath)
            Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'configure-summary' }) $reportPath $subjectPath 'mixed-newline Qt configure summary'
        } $report $subject
    } 'consistent LF or CRLF'
}

Test-Case 'configure summary rejects target or compiler preamble drift' {
    foreach ($mutation in @(
            @{ expected = 'win32-msvc (x86_64, CPU features: )'; replacement = 'win32-msvc (x86, CPU features: )' },
            @{ expected = 'msvc 19.44.35228.0'; replacement = 'msvc 19.44.35229.0' }
        )) {
        $root = Join-Path $testRoot ('config-summary-preamble-drift-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $subject = Join-Path $root 'config.summary'
        $report = Join-Path $root 'configure-summary.txt'
        $text = (Get-TestConfigSummaryText).Replace(
            $mutation.expected,
            $mutation.replacement,
            [StringComparison]::Ordinal)
        Write-TestText $subject $text
        Write-TestText $report $text
        Assert-Rejected {
            & $script:module {
                param($reportPath, $subjectPath)
                Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'configure-summary' }) $reportPath $subjectPath 'drifted Qt configure summary'
            } $report $subject
        } 'configure summary|semantic|bound source copy'
    }
}

Test-Case 'CMake cache rejects free-floating expected text and an unpinned configuration value' {
    $root = Join-Path $testRoot ('cmake-cache-semantic-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'CMakeCache.txt'
    $report = Join-Path $root 'cmake-cache.txt'
    $text = "CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config`nCMAKE_CONFIGURATION_TYPES:STRING=Release`n# stray RelWithDebInfo;Debug`n`n"
    Write-TestText $subject $text
    Write-TestText $report $text
    Assert-Rejected {
        & $script:module {
            param($reportPath, $subjectPath)
            Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'cmake-cache' }) $reportPath $subjectPath 'unpinned CMake cache'
        } $report $subject
    } 'CMAKE_CONFIGURATION_TYPES.*drifted'
}

Test-Case 'configuration summary rejects contradictory values for an expected label' {
    foreach ($contradiction in @(
            '  Mode ................................... release',
            '    Dynamic OpenGL ....................... no',
            '  Using system zlib ...................... yes'
        )) {
        $root = Join-Path $testRoot ('config-summary-contradiction-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $subject = Join-Path $root 'config.summary'
        $report = Join-Path $root 'configure-summary.txt'
        $text = (Get-TestConfigSummaryText) + $contradiction + "`n"
        Write-TestText $subject $text
        Write-TestText $report $text
        Assert-Rejected {
            & $script:module {
                param($reportPath, $subjectPath)
                Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'configure-summary' }) $reportPath $subjectPath 'contradictory config summary'
            } $report $subject
        } 'configuration|summary|semantic|expected|Mode|OpenGL|zlib'
    }
}

Test-Case 'FreeType dependency report rejects disconnected Ninja aliases' {
    $root = Join-Path $testRoot ('ninja-aliases-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'impl-RelWithDebInfo.ninja'
    $report = Join-Path $root 'qtgui-freetype-dependency.txt'
    Write-TestText $subject "build Qt6Gui: phony`nbuild qminimal: phony`nbuild Qt6BundledFreetype: phony`n`n"
    Write-TestText $report "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`nplugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n"
    $spec = [pscustomobject]@{ id = 'qtgui-freetype-dependency' }
    Assert-Rejected {
        & $script:module {
            param($reportSpec, $reportPath, $subjectPath)
            Assert-M0V2SemanticReport $reportSpec $reportPath $subjectPath 'disconnected Ninja aliases'
        } $spec $report $subject
    } 'FreeType|edge|dependency|Ninja|semantic'
}

Test-Case 'FreeType dependency subject accepts generated Ninja CRLF final blank line' {
    $root = Join-Path $testRoot ('ninja-real-newlines-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'impl-RelWithDebInfo.ninja'
    $report = Join-Path $root 'qtgui-freetype-dependency.txt'
    $ninjaText = "build bin\Qt6Gui.dll lib\Qt6Gui.lib: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`nbuild plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`n`n".Replace("`n", "`r`n")
    [IO.File]::WriteAllText($subject, $ninjaText, [Text.UTF8Encoding]::new($false))
    Write-TestText $report "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`nplugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n"
    & $script:module {
        param($reportPath, $subjectPath)
        Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'qtgui-freetype-dependency' }) $reportPath $subjectPath 'generated Ninja subject'
    } $report $subject
}

Test-Case 'FreeType dependency subject rejects generated Ninja final newline drift' {
    $ninjaText = "build bin\Qt6Gui.dll lib\Qt6Gui.lib: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`nbuild plugins\platforms\qminimal.dll: CXX_MODULE_LIBRARY_LINKER__QMinimalIntegrationPlugin_RelWithDebInfo objects | lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib`n`n".Replace("`n", "`r`n")
    foreach ($text in @(
            $ninjaText.Substring(0, $ninjaText.Length - 2),
            ($ninjaText + "`r`n")
        )) {
        $root = Join-Path $testRoot ('ninja-final-newline-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($root) | Out-Null
        $subject = Join-Path $root 'impl-RelWithDebInfo.ninja'
        $report = Join-Path $root 'qtgui-freetype-dependency.txt'
        Write-TestText $subject $text
        Write-TestText $report "bin/Qt6Gui.dll`tlib/Qt6BundledFreetype.lib`nplugins/platforms/qminimal.dll`tlib/Qt6BundledFreetype.lib`n"
        Assert-Rejected {
            & $script:module {
                param($reportPath, $subjectPath)
                Assert-M0V2SemanticReport ([pscustomobject]@{ id = 'qtgui-freetype-dependency' }) $reportPath $subjectPath 'Ninja subject with final newline drift'
            } $report $subject
        } 'exactly 2 final newline'
    }
}

Test-Case 'FreeType dependency parser rejects rule tokens and substring lookalikes' {
    foreach ($line in @(
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo Notlib\Qt6BundledFreetype.libFake`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib",
            "build bin\Qt6Gui.dll: lib\Qt6BundledFreetype.lib unrelated`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib",
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = Notlib\Qt6BundledFreetype.libFake",
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo objects # lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = lib\Qt6BundledFreetype.lib",
            "build bin\Qt6Gui.dll: CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo lib\Qt6BundledFreetype.lib`n  LINK_LIBRARIES = unrelated.lib # lib\Qt6BundledFreetype.lib"
        )) {
        $accepted = & $script:module {
            param($ninjaLine)
            Test-M0NinjaDependencyEdge ($ninjaLine + "`n") 'bin/Qt6Gui.dll' `
                'CXX_SHARED_LIBRARY_LINKER__Gui_RelWithDebInfo' 'lib/Qt6BundledFreetype.lib'
        } $line
        Assert-True (-not $accepted) "A fake Ninja dependency was accepted: $line"
    }
}

Test-Case 'dumpbin reports reject marker-only text without bound imports and exports' {
    $root = Join-Path $testRoot ('dumpbin-markers-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $subject = Join-Path $root 'subject.dll'
    [IO.File]::Copy((Join-Path $env:SystemRoot 'System32/version.dll'), $subject)

    $cases = @(
        [pscustomobject]@{
            id = 'qt6gui-imports'
            text = "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following imports:`nKERNEL32.dll`n"
        },
        [pscustomobject]@{
            id = 'qt6gui-exports'
            text = "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following exports for Qt6Gui.dll`nordinal hint RVA name`n"
        },
        [pscustomobject]@{
            id = 'qminimal-imports'
            text = "Dump of file qminimal.dll`nFile Type: DLL`nSection contains the following imports:`nQt6Gui.dll`n"
        }
    )

    foreach ($case in $cases) {
        $report = Join-Path $root "$($case.id).txt"
        Write-TestText $report $case.text
        Assert-Rejected {
            & $script:module {
                param($reportSpec, $reportPath, $subjectPath)
                Assert-M0V2SemanticReport $reportSpec $reportPath $subjectPath 'marker-only dumpbin report'
            } ([pscustomobject]@{ id = $case.id }) $report $subject
        } 'dumpbin|import|export|semantic|symbol|DLL'
    }
}

Test-Case 'dumpbin semantics are derived from the bound PE import and export tables' {
    $root = Join-Path $testRoot ('dumpbin-pe-tables-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $qtGui = Join-Path $root 'Qt6Gui.dll'
    $qminimal = Join-Path $root 'qminimal.dll'
    [IO.File]::Copy((Join-Path $env:SystemRoot 'System32/version.dll'), $qtGui)
    [IO.File]::Copy((Join-Path $env:SystemRoot 'System32/version.dll'), $qminimal)

    $cases = @(
        [pscustomobject]@{
            id = 'qt6gui-imports'; subject = $qtGui
            text = "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following imports:`nQt6Core.dll`nDWrite.dll`n"
        },
        [pscustomobject]@{
            id = 'qt6gui-exports'; subject = $qtGui
            text = "Dump of file Qt6Gui.dll`nFile Type: DLL`nSection contains the following exports for Qt6Gui.dll`nordinal hint RVA name`n      1    0 00001000 ??0QColor@@QEAA@HHHH@Z`n"
        },
        [pscustomobject]@{
            id = 'qminimal-imports'; subject = $qminimal
            text = "Dump of file qminimal.dll`nFile Type: DLL`nSection contains the following imports:`nQt6Gui.dll`nQt6Core.dll`n"
        }
    )
    foreach ($case in $cases) {
        $report = Join-Path $root "$($case.id).txt"
        Write-TestText $report $case.text
        Assert-Rejected {
            & $script:module {
                param($reportSpec, $reportPath, $subjectPath)
                Assert-M0V2SemanticReport $reportSpec $reportPath $subjectPath 'PE-bound dumpbin report'
            } ([pscustomobject]@{ id = $case.id }) $report $case.subject
        } 'PE|import|export|Qt6|DWrite|QColor|subject|semantic'
    }
}

Test-Case 'schema v2 command evidence must match the pinned tool argv cwd and log paths' {
    foreach ($kind in @('tool', 'script', 'arguments', 'working-directory', 'stdout-path', 'exit-code')) {
        $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
        $execute = {
            param($context)
            $observation = New-TestV2Observation $fixture $context
            $command = $observation.evidence.commands[0]
            if ($kind -eq 'tool') { $command.tool_id = 'cmake' }
            elseif ($kind -eq 'script') { $command.script_relative_path = $null }
            elseif ($kind -eq 'arguments') { $command.arguments = @('--arbitrary') }
            elseif ($kind -eq 'working-directory') { $command.working_directory_relative_path = 'install' }
            elseif ($kind -eq 'stdout-path') { $command.stdout.relative_path = 'evidence/logs/other.log' }
            else { $command.exit_code = [long]7 }
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "v2-command-$kind" $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'command|tool|script|argument|working|stdout|log|exit code|path|caminho|specified|physical|missing'
        Assert-True (-not [IO.File]::Exists((Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path))) "Command drift '$kind' published a v2 receipt."
    }
}

Test-Case 'contract objects reject missing and extra fields even with a recomputed external hash' {
    $fixture = New-QtBaseRefreshFixture
    $mutants = @()

    $topExtra = Copy-TestJson $fixture.Contract
    $topExtra | Add-Member -NotePropertyName unchecked -NotePropertyValue $true
    $mutants += Write-ContractMutant $fixture $topExtra 'top-extra'

    $nestedExtra = Copy-TestJson $fixture.Contract
    $nestedExtra.inputs.freetype_patch | Add-Member -NotePropertyName second_target -NotePropertyValue 'other.c'
    $mutants += Write-ContractMutant $fixture $nestedExtra 'nested-extra'

    $toolExtra = Copy-TestJson $fixture.Contract
    $toolExtra.tools[0] | Add-Member -NotePropertyName ambient_lookup_allowed -NotePropertyValue $true
    $mutants += Write-ContractMutant $fixture $toolExtra 'tool-extra'

    $featureExtra = Copy-TestJson $fixture.Contract
    $featureExtra.configuration.expected_features | Add-Member -NotePropertyName unchecked_feature -NotePropertyValue $true
    $mutants += Write-ContractMutant $fixture $featureExtra 'feature-extra'

    $missing = Copy-TestJson $fixture.Contract
    $missing.inputs.vulkan_headers.PSObject.Properties.Remove('license_sha256')
    $mutants += Write-ContractMutant $fixture $missing 'missing-field'

    foreach ($mutant in $mutants) {
        Assert-Rejected {
            Assert-M0QtBaseRefreshContract -LiteralPath $mutant.Path -ExpectedSha256 $mutant.Sha256
        } 'contract|schema|field|property|exact'
    }
}

Test-Case 'contract scalar types are strict even when textual values look equivalent' {
    $fixture = New-QtBaseRefreshFixture
    $mutations = @(
        'schema-string', 'schema-fraction', 'schema-boolean',
        'size-string', 'size-fraction', 'size-boolean',
        'boolean-string', 'feature-string', 'version-number'
    )
    foreach ($kind in $mutations) {
        $mutant = Copy-TestJson $fixture.Contract
        if ($kind -eq 'schema-string') { $mutant.schema_version = '1' }
        elseif ($kind -eq 'schema-fraction') { $mutant.schema_version = 1.1 }
        elseif ($kind -eq 'schema-boolean') { $mutant.schema_version = $true }
        elseif ($kind -eq 'size-string') { $mutant.inputs.qtbase_source.size_bytes = [string]$mutant.inputs.qtbase_source.size_bytes }
        elseif ($kind -eq 'size-fraction') { $mutant.inputs.qtbase_source.size_bytes = 1.1 }
        elseif ($kind -eq 'size-boolean') { $mutant.inputs.qtbase_source.size_bytes = $true }
        elseif ($kind -eq 'boolean-string') { $mutant.isolation.require_new_direct_child = 'true' }
        elseif ($kind -eq 'feature-string') { $mutant.configuration.expected_features.vulkan = 'true' }
        else { $mutant.tools[0].version = 3.31 }
        $written = Write-ContractMutant $fixture $mutant "type-$kind"
        Assert-Rejected {
            Assert-M0QtBaseRefreshContract -LiteralPath $written.Path -ExpectedSha256 $written.Sha256
        } 'contract|schema|type|integer|size|boolean|feature|version|string'
    }
}

Test-Case 'contract pins the source archive patch Vulkan headers and the exact host inventory' {
    $fixture = New-QtBaseRefreshFixture
    $mutations = @('source-hash', 'patch-target', 'vulkan-commit', 'missing-tool', 'duplicate-tool', 'tool-hash')
    foreach ($kind in $mutations) {
        $mutant = Copy-TestJson $fixture.Contract
        switch ($kind) {
            'source-hash' { $mutant.inputs.qtbase_source.sha256 = 'not-a-digest' }
            'patch-target' { $mutant.inputs.freetype_patch.target_relative_path = '../other.c' }
            'vulkan-commit' { $mutant.inputs.vulkan_headers.commit_sha = '37164a5' }
            'missing-tool' { $mutant.tools = @($mutant.tools | Where-Object id -cne 'link') }
            'duplicate-tool' { $mutant.tools[3].id = 'cl' }
            'tool-hash' { $mutant.tools[0].sha256 = 'f' * 63 }
        }
        $written = Write-ContractMutant $fixture $mutant "pin-$kind"
        Assert-Rejected {
            Assert-M0QtBaseRefreshContract -LiteralPath $written.Path -ExpectedSha256 $written.Sha256
        } 'contract|source|patch|target|Vulkan|commit|tool|sha256|digest|path'
    }
}

Test-Case 'orchestration authenticates all physical input and tool bytes before callback or receipt' {
    foreach ($kind in @('source', 'patch', 'vulkan', 'tool')) {
        $fixture = New-QtBaseRefreshFixture
        if ($kind -eq 'source') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.qtbase_source.relative_path) 'tampered source' }
        elseif ($kind -eq 'patch') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.freetype_patch.relative_path) 'tampered patch' }
        elseif ($kind -eq 'vulkan') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.vulkan_headers.relative_path) 'tampered Vulkan' }
        else { Write-TestText (Join-Path $fixture.ToolRoot $fixture.Contract.tools[0].relative_path) 'tampered tool' }

        $callState = @{ Count = 0 }
        $execute = { param($context) $callState.Count++; New-ValidObservation $fixture $context }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "tamper-$kind" $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'input|tool|sha256|hash|size|pin|authentic'
        Assert-Equal $callState.Count 0 'An unauthenticated byte reached the execution callback.'
        Assert-True (-not [IO.Directory]::Exists($arguments.RunRoot)) 'Rejected bytes created a run root.'
    }
}

Test-Case 'forbidden ambient build environment injections fail before callback' {
    foreach ($name in @(
            'CL', '_CL_', 'LINK', '_LINK_',
            'CFLAGS', 'CXXFLAGS', 'LDFLAGS',
            'CMAKE_PREFIX_PATH', 'CMAKE_TOOLCHAIN_FILE',
            'QMAKESPEC', 'QT_HOST_PATH', 'VULKAN_SDK',
            'PYTHONHOME', 'PYTHONPATH', 'PERL5LIB'
        )) {
        $fixture = New-QtBaseRefreshFixture
        $previous = [Environment]::GetEnvironmentVariable($name, 'Process')
        try {
            [Environment]::SetEnvironmentVariable($name, 'synthetic-injection', 'Process')
            $callState = @{ Count = 0 }
            $execute = { param($context) $callState.Count++; New-ValidObservation $fixture $context }.GetNewClosure()
            $arguments = Get-InvokeArguments $fixture "forbidden-$name" $execute
            Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'environment|forbidden|CL|LINK|inject'
            Assert-Equal $callState.Count 0 "Forbidden variable '$name' reached the callback."
            Assert-True (-not [IO.Directory]::Exists($arguments.RunRoot)) "Forbidden variable '$name' created a run root."
        }
        finally {
            [Environment]::SetEnvironmentVariable($name, $previous, 'Process')
        }
    }
}

Test-Case 'RunRoot must be a new direct child of the pinned isolated parent' {
    $fixture = ConvertTo-QtBaseRefreshV2Fixture (New-QtBaseRefreshFixture)
    $callState = @{ Count = 0 }
    $execute = { param($context) $callState.Count++; New-ValidObservation $fixture $context }.GetNewClosure()
    $outside = Join-Path $fixture.Root 'outside-run'
    $nested = Join-Path (Join-Path $fixture.RunParent 'nested') 'grandchild'
    $existing = Join-Path $fixture.RunParent 'already-exists'
    [IO.Directory]::CreateDirectory($existing) | Out-Null
    foreach ($invalidRoot in @($outside, $nested, $existing, $fixture.RunParent)) {
        $arguments = @{
            ContractPath = $fixture.ContractPath
            ExpectedContractSha256 = $fixture.ContractSha256
            RepositoryRoot = $fixture.Root
            ToolRoot = $fixture.ToolRoot
            RunRoot = $invalidRoot
            Execute = $execute
        }
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'RunRoot|run root|isolated|direct child|new|exist|parent'
    }
    Assert-Equal $callState.Count 0 'An invalid RunRoot reached the callback.'
}

Test-Case 'reparse-point tool roots are rejected before callback or RunRoot creation' {
    if (-not $IsWindows) { return }
    $fixture = New-QtBaseRefreshFixture
    $junction = Join-Path $fixture.Root 'toolchain-junction'
    $null = New-Item -ItemType Junction -Path $junction -Target $fixture.ToolRoot
    $callState = @{ Count = 0 }
    $execute = { param($context) $callState.Count++; New-ValidObservation $fixture $context }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'reparse-tool-root' $execute
    $arguments.ToolRoot = $junction
    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'reparse|physical|tool|root|ancestor'
    Assert-Equal $callState.Count 0 'A reparse-point tool root reached the callback.'
    Assert-True (-not [IO.Directory]::Exists($arguments.RunRoot)) 'A reparse-point tool root created a RunRoot.'
}

Test-Case 'one successful callback must report the single patch target and exact configuration features' {
    $fixture = New-QtBaseRefreshFixture
    $callState = @{ Count = 0 }
    $execute = {
        param($context)
        $callState.Count++
        Assert-True ([IO.Directory]::Exists($context.run_root)) 'The new RunRoot was not materialized for execution.'
        Assert-Equal $context.contract_sha256 $fixture.ContractSha256 'Callback received a different contract identity.'
        Assert-Equal $context.paths.qtbase_source (Join-Path $fixture.Root $fixture.Contract.inputs.qtbase_source.relative_path)
        Assert-Equal $context.paths.freetype_patch (Join-Path $fixture.Root $fixture.Contract.inputs.freetype_patch.relative_path)
        Assert-Equal $context.paths.vulkan_headers (Join-Path $fixture.Root $fixture.Contract.inputs.vulkan_headers.relative_path)
        Assert-Equal @($context.tool_paths.PSObject.Properties).Count @($fixture.Contract.tools).Count 'Callback did not receive the exact authenticated tool inventory.'
        return New-ValidObservation $fixture $context
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'successful-run' $execute
    $result = Invoke-TestQtBaseRefresh @arguments

    Assert-Equal $callState.Count 1 'Successful execution callback was not invoked exactly once.'
    Assert-Equal $result.contract_sha256 $fixture.ContractSha256
    Assert-True ([IO.File]::Exists($result.evidence_manifest_path)) 'Successful execution did not publish an evidence manifest.'
    Assert-Equal (Get-TestSha256 $result.evidence_manifest_path) $result.evidence_manifest_sha256 'Returned manifest hash does not authenticate its bytes.'
    Assert-True ([IO.File]::Exists($result.receipt_path)) 'Successful execution did not publish a receipt.'
    Assert-Equal (Get-TestSha256 $result.receipt_path) $result.receipt_sha256 'Returned receipt hash does not authenticate its bytes.'
}

Test-Case 'a successful callback without physical evidence cannot publish a receipt' {
    $fixture = New-QtBaseRefreshFixture
    $execute = {
        param($context)
        $observation = New-ValidObservation $fixture $context
        $observation.PSObject.Properties.Remove('evidence')
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'missing-physical-evidence' $execute

    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'evidence|manifest|artifact|physical'
    $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
    Assert-True (-not [IO.File]::Exists($receiptPath)) 'A synthetic success without physical evidence published a receipt.'
}

Test-Case 'evidence references must be closed complete and authenticate physical leaves' {
    foreach ($kind in @(
            'extra-field', 'missing-artifact', 'artifact-hash', 'unsafe-log', 'duplicate-artifact',
            'non-pe-artifact', 'inventory-order', 'inventory-case-duplicate', 'inventory-total'
        )) {
        $fixture = New-QtBaseRefreshFixture
        $execute = {
            param($context)
            $observation = New-ValidObservation $fixture $context
            if ($kind -eq 'extra-field') {
                $observation.evidence | Add-Member -NotePropertyName unchecked -NotePropertyValue $true
            }
            elseif ($kind -eq 'missing-artifact') {
                [IO.File]::Delete((Join-Path $context.run_root $observation.evidence.artifacts[0].relative_path))
            }
            elseif ($kind -eq 'artifact-hash') {
                $observation.evidence.artifacts[0].sha256 = '0' * 64
            }
            elseif ($kind -eq 'unsafe-log') {
                $observation.evidence.commands[0].stdout.relative_path = '../outside.log'
            }
            elseif ($kind -eq 'duplicate-artifact') {
                $observation.evidence.artifacts[1].relative_path = $observation.evidence.artifacts[0].relative_path.ToUpperInvariant()
            }
            elseif ($kind -eq 'non-pe-artifact') {
                $artifactPath = Join-Path $context.run_root $observation.evidence.artifacts[0].relative_path
                Write-TestText $artifactPath 'not a PE image'
                $observation.evidence.artifacts[0].size_bytes = [long](Get-Item -LiteralPath $artifactPath).Length
                $observation.evidence.artifacts[0].sha256 = Get-TestSha256 $artifactPath
            }
            elseif ($kind -eq 'inventory-total') {
                $observation.evidence.trees[0].total_size_bytes = [long]9
            }
            else {
                $tree = $observation.evidence.trees[0]
                $inventoryPath = Join-Path $context.run_root $tree.inventory.relative_path
                $digestA = 'a' * 64
                $digestB = 'b' * 64
                $content = if ($kind -eq 'inventory-order') {
                    "z.bin`t8`t$digestA`na.bin`t8`t$digestB`n"
                }
                else {
                    "a.bin`t8`t$digestA`nA.bin`t8`t$digestB`n"
                }
                Write-TestText $inventoryPath $content
                $tree.file_count = [long]2
                $tree.total_size_bytes = [long]16
                $tree.inventory.size_bytes = [long](Get-Item -LiteralPath $inventoryPath).Length
                $tree.inventory.sha256 = Get-TestSha256 $inventoryPath
            }
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "bad-evidence-$kind" $execute

        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'evidence|manifest|artifact|reference|physical|path|caminho|sistema|encontrar|hash|duplicate|exact|missing'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) "Invalid evidence '$kind' published a receipt."
    }
}

Test-Case 'a patch report with zero or multiple changed targets prevents receipt publication' {
    foreach ($kind in @('zero', 'multiple', 'before', 'after')) {
        $fixture = New-QtBaseRefreshFixture
        $callState = @{ Count = 0 }
        $execute = {
            param($context)
            $callState.Count++
            $observation = New-ValidObservation $fixture $context
            if ($kind -eq 'zero') { $observation.patch.changed_relative_paths = @() }
            elseif ($kind -eq 'multiple') { $observation.patch.changed_relative_paths = @($fixture.Target, 'src/other.c') }
            elseif ($kind -eq 'before') { $observation.patch.target_before_sha256 = '0' * 64 }
            else { $observation.patch.target_after_sha256 = '0' * 64 }
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "bad-patch-$kind" $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'patch|target|changed|before|after|identity|single|exact'
        Assert-Equal $callState.Count 1 'Patch observation callback did not execute exactly once.'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) 'Invalid patch evidence published a receipt.'
    }
}

Test-Case 'configuration generator arguments artifacts and feature drift prevent receipt publication' {
    foreach ($kind in @('generator', 'arguments', 'build-arguments', 'install-arguments', 'installed-configuration', 'artifacts', 'feature', 'extra-feature')) {
        $fixture = New-QtBaseRefreshFixture
        $callState = @{ Count = 0 }
        $execute = {
            param($context)
            $callState.Count++
            $observation = New-ValidObservation $fixture $context
            if ($kind -eq 'generator') { $observation.configuration.generator = 'Ninja' }
            elseif ($kind -eq 'arguments') { $observation.configuration.arguments = @($observation.configuration.arguments | Where-Object { $_ -cne '-debug-and-release' }) }
            elseif ($kind -eq 'build-arguments') { $observation.configuration.build_arguments = @('--build', 'other') }
            elseif ($kind -eq 'install-arguments') { $observation.configuration.install_arguments = @('--install', 'other') }
            elseif ($kind -eq 'installed-configuration') { $observation.configuration.installed_configuration = 'Debug' }
            elseif ($kind -eq 'artifacts') { $observation.configuration.artifacts = @('bin/Qt6Core.dll') }
            elseif ($kind -eq 'feature') { $observation.configuration.features.vulkan = $false }
            else { $observation.configuration.features | Add-Member -NotePropertyName ambient -NotePropertyValue $true }
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "bad-configuration-$kind" $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'configuration|generator|argument|feature|expected|exact|drift'
        Assert-Equal $callState.Count 1 'Configuration observation callback did not execute exactly once.'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) 'Invalid configuration evidence published a receipt.'
    }
}

Test-Case 'callback failure propagates after one call and cannot expose a receipt' {
    $fixture = New-QtBaseRefreshFixture
    $callState = @{ Count = 0 }
    $failure = [InvalidOperationException]::new('synthetic QtBase build failure')
    $execute = { param($context) $callState.Count++; throw $failure }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'callback-failure' $execute
    $rejection = Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'synthetic QtBase build failure'
    Assert-True ([object]::ReferenceEquals($rejection.Exception, $failure)) 'Callback exception identity was not preserved.'
    Assert-Equal $callState.Count 1 'Failing execution callback was not invoked exactly once.'
    $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
    Assert-True (-not [IO.File]::Exists($receiptPath)) 'Callback failure exposed a receipt.'
}

Test-Case 'nonzero native result preserves output and cannot expose a receipt' {
    $fixture = New-QtBaseRefreshFixture
    $callState = @{ Count = 0 }
    $execute = {
        param($context)
        $callState.Count++
        $observation = New-ValidObservation $fixture $context
        $observation.execution.exit_code = 7
        $observation.execution.stdout = "configure output`nbuild output"
        $observation.execution.stderr = 'synthetic compiler error'
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'native-failure' $execute
    $rejection = Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } '7|configure output|compiler error'
    Assert-True ($rejection.Exception.Message.Contains('configure output')) 'Native stdout was not preserved in the rejection.'
    Assert-True ($rejection.Exception.Message.Contains('synthetic compiler error')) 'Native stderr was not preserved in the rejection.'
    Assert-Equal $callState.Count 1 'Nonzero native execution callback was not invoked exactly once.'
    $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
    Assert-True (-not [IO.File]::Exists($receiptPath)) 'Nonzero native execution exposed a receipt.'
}

Test-Case 'boolean and fractional native exit codes are rejected without a receipt' {
    foreach ($invalidExit in @($false, 0.1)) {
        $fixture = New-QtBaseRefreshFixture
        $execute = {
            param($context)
            $observation = New-ValidObservation $fixture $context
            $observation.execution.exit_code = $invalidExit
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture ('invalid-exit-' + [Guid]::NewGuid().ToString('N')) $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'exit_code|exit code|integer|type'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) 'Invalid native exit-code type exposed a receipt.'
    }
}

Test-Case 'callback receives a defensive contract copy and cannot redefine postconditions' {
    $fixture = New-QtBaseRefreshFixture
    $execute = {
        param($context)
        $context.contract.refresh_id = 'mutated-refresh'
        $context.contract.inputs.freetype_patch.target_after_sha256 = '0' * 64
        $context.contract.isolation.receipt_relative_path = 'redirected/receipt.json'
        $observation = New-ValidObservation $fixture $context
        $observation.patch.target_after_sha256 = '0' * 64
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'mutated-context-contract' $execute
    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'contract|patch|target|identity|postcondition|drift'
    Assert-True (-not [IO.File]::Exists((Join-Path $arguments.RunRoot 'redirected/receipt.json'))) 'Callback contract mutation redirected receipt publication.'
    Assert-True (-not [IO.File]::Exists((Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path))) 'Callback contract mutation exposed the canonical receipt.'
}

Test-Case 'callback-created evidence junction is rejected before receipt publication' {
    if (-not $IsWindows) { return }
    $fixture = New-QtBaseRefreshFixture
    $outside = Join-Path $fixture.Root 'outside-evidence'
    [IO.Directory]::CreateDirectory($outside) | Out-Null
    $execute = {
        param($context)
        $null = New-Item -ItemType Junction -Path (Join-Path $context.run_root 'evidence') -Target $outside
        return New-ValidObservation $fixture $context
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'evidence-junction' $execute
    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'reparse|junction|physical|evidence|receipt|ancestor'
    Assert-True (-not [IO.File]::Exists((Join-Path $outside 'qtbase-refresh-receipt.v1.json'))) 'Receipt escaped through the evidence junction.'
}

Test-Case 'post-execution drift in contract inputs or tools prevents receipt publication' {
    foreach ($kind in @('contract', 'source', 'patch', 'vulkan', 'tool')) {
        $fixture = New-QtBaseRefreshFixture
        $callState = @{ Count = 0 }
        $execute = {
            param($context)
            $callState.Count++
            if ($kind -eq 'contract') { Write-TestText $fixture.ContractPath 'tampered contract after authentication' }
            elseif ($kind -eq 'source') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.qtbase_source.relative_path) 'tampered source after authentication' }
            elseif ($kind -eq 'patch') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.freetype_patch.relative_path) 'tampered patch after authentication' }
            elseif ($kind -eq 'vulkan') { Write-TestText (Join-Path $fixture.Root $fixture.Contract.inputs.vulkan_headers.relative_path) 'tampered Vulkan after authentication' }
            else { Write-TestText (Join-Path $fixture.ToolRoot $fixture.Contract.tools[0].relative_path) 'tampered tool after authentication' }
            return New-ValidObservation $fixture $context
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "post-drift-$kind" $execute
        Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'post|drift|contract|input|tool|sha256|hash|size|pin|authentic'
        Assert-Equal $callState.Count 1 'Post-drift callback did not execute exactly once.'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) 'Post-execution drift exposed a receipt.'
    }
}

Test-Case 'authenticated input handles prevent callback swap and restore' {
    if (-not $IsWindows) { return }
    $fixture = New-QtBaseRefreshFixture
    $sourcePath = Join-Path $fixture.Root $fixture.Contract.inputs.qtbase_source.relative_path
    $backupPath = "$sourcePath.swap"
    $execute = {
        param($context)
        [IO.File]::Move($sourcePath, $backupPath)
        try { Write-TestText $sourcePath 'substituted source bytes' }
        finally {
            if ([IO.File]::Exists($sourcePath)) { [IO.File]::Delete($sourcePath) }
            if ([IO.File]::Exists($backupPath)) { [IO.File]::Move($backupPath, $sourcePath) }
        }
        return New-ValidObservation $fixture $context
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'swap-restore-input' $execute

    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'input|source|sharing|used by another process|access|rename|move|authentic'
    Assert-True (-not [IO.File]::Exists((Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path))) 'Swap/restore exposed a receipt.'
}

Test-Case 'hardlinked evidence leaves cannot publish or verify a receipt' {
    if (-not $IsWindows) { return }
    $fixture = New-QtBaseRefreshFixture
    $execute = {
        param($context)
        $observation = New-ValidObservation $fixture $context
        $artifactPath = Join-Path $context.run_root $observation.evidence.artifacts[0].relative_path
        $null = New-Item -ItemType HardLink -Path "$artifactPath.alias" -Target $artifactPath
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'hardlinked-artifact' $execute
    Assert-Rejected { Invoke-TestQtBaseRefresh @arguments } 'evidence|artifact|physical|hardlink|link|identity'

    $verifyFixture = New-QtBaseRefreshFixture
    $verifyExecute = { param($context) New-ValidObservation $verifyFixture $context }.GetNewClosure()
    $verifyArguments = Get-InvokeArguments $verifyFixture 'hardlinked-manifest' $verifyExecute
    $result = Invoke-TestQtBaseRefresh @verifyArguments
    $null = New-Item -ItemType HardLink -Path "$($result.evidence_manifest_path).alias" -Target $result.evidence_manifest_path
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $result.receipt_path `
            -ExpectedReceiptSha256 $result.receipt_sha256 `
            -ContractPath $verifyFixture.ContractPath `
            -ExpectedContractSha256 $verifyFixture.ContractSha256 `
            -RepositoryRoot $verifyFixture.Root
    } 'evidence|manifest|physical|hardlink|link|identity'
}

Test-Case 'retained publication keeps the manifest immutable through the receipt commit point' {
    if (-not $IsWindows) { return }
    $runRoot = Join-Path $testRoot ('retained-publication-' + [Guid]::NewGuid().ToString('N'))
    $evidenceRoot = Join-Path $runRoot 'evidence'
    [IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
    $manifestPath = Join-Path $evidenceRoot 'manifest.json'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes("{}`n")
    $publication = & $script:module {
        param($path, $content, $root)
        Publish-M0QtBaseRefreshReceipt -LiteralPath $path -Bytes $content -RunRoot $root -RetainPublishedLease
    } $manifestPath $bytes $runRoot
    try {
        Assert-True ($null -ne $publication.lease.stream) 'Retained publication did not return its physical lease.'
        Assert-Rejected {
            [IO.File]::AppendAllText($manifestPath, 'tamper', [Text.UTF8Encoding]::new($false))
        } 'used by another process|sharing|access|physical|lease'
    }
    finally {
        if ($null -ne $publication.lease.stream) { $publication.lease.stream.Dispose() }
    }
}

Test-Case 'uncommitted manifest and receipt publications are deleted on failure' {
    $runRoot = Join-Path $testRoot ('failed-publication-' + [Guid]::NewGuid().ToString('N'))
    $evidenceRoot = Join-Path $runRoot 'evidence'
    [IO.Directory]::CreateDirectory($evidenceRoot) | Out-Null
    $publishedPath = Join-Path $evidenceRoot 'receipt.json'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes("{}`n")
    $publication = & $script:module {
        param($path, $content, $root)
        Publish-M0QtBaseRefreshReceipt -LiteralPath $path -Bytes $content -RunRoot $root -RetainPublishedLease
    } $publishedPath $bytes $runRoot
    try {
        & $script:module {
            param($publishedLease)
            Set-M0UncommittedPublicationsDeleteOnClose @($publishedLease)
        } $publication.lease
    }
    finally {
        $publication.lease.stream.Dispose()
    }
    Assert-True (-not [IO.File]::Exists($publishedPath)) 'A failed publication left an orphan receipt that could later verify.'

    $source = Get-Content -Raw -LiteralPath $modulePath
    Assert-True ($source.Contains('Set-M0UncommittedPublicationsDeleteOnClose $publicationLeases', [StringComparison]::Ordinal)) `
        'The orchestration failure path does not delete published manifest and receipt leaves.'
}

Test-Case 'lease revalidation detects a hardlink added after authentication' {
    if (-not $IsWindows) { return }
    $root = Join-Path $testRoot ('late-hardlink-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $path = Join-Path $root 'leaf.bin'
    Write-TestText $path "leaf bytes`n"
    $sha256 = Get-TestSha256 $path
    $lease = & $script:module {
        param($leafPath, $length, $hash, $boundary)
        Open-M0PhysicalFileLease $leafPath $length $hash 'late-hardlink leaf' -BoundaryRoot $boundary -RequireSingleLink
    } $path ([long](Get-Item -LiteralPath $path).Length) $sha256 $root
    try {
        $null = New-Item -ItemType HardLink -Path "$path.alias" -Target $path
        Assert-Rejected {
            & $script:module {
                param($physicalLease)
                Assert-M0PhysicalFileLeasesUnchanged @($physicalLease) 'late-hardlink revalidation'
            } $lease
        } 'hardlink count changed after authentication'
    }
    finally { $lease.stream.Dispose() }
}

Test-Case 'receipt verification rejects a copied run outside the contract-pinned parent' {
    $fixture = New-QtBaseRefreshFixture
    $execute = { param($context) New-ValidObservation $fixture $context }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'canonical-run' $execute
    $result = Invoke-TestQtBaseRefresh @arguments

    $copiedParent = Join-Path $fixture.Root 'copied-parent'
    [IO.Directory]::CreateDirectory($copiedParent) | Out-Null
    $copiedRun = Join-Path $copiedParent ([IO.Path]::GetFileName($arguments.RunRoot))
    Copy-Item -LiteralPath $arguments.RunRoot -Destination $copiedRun -Recurse
    $copiedReceipt = Join-Path $copiedRun $fixture.Contract.isolation.receipt_relative_path
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $copiedReceipt `
            -ExpectedReceiptSha256 $result.receipt_sha256 `
            -ContractPath $fixture.ContractPath `
            -ExpectedContractSha256 $fixture.ContractSha256 `
            -RepositoryRoot $fixture.Root
    } 'contract-pinned run parent|run parent|direct child|isolated'
}

Test-Case 'receipt verification is externally hash-bound and rejects byte or semantic tamper' {
    $fixture = New-QtBaseRefreshFixture
    $execute = { param($context) New-ValidObservation $fixture $context }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'receipt-verification' $execute
    $result = Invoke-TestQtBaseRefresh @arguments

    $accepted = Assert-M0QtBaseRefreshReceipt `
        -LiteralPath $result.receipt_path `
        -ExpectedReceiptSha256 $result.receipt_sha256 `
        -ContractPath $fixture.ContractPath `
        -ExpectedContractSha256 $fixture.ContractSha256 `
        -RepositoryRoot $fixture.Root
    Assert-Equal $accepted.contract_sha256 $fixture.ContractSha256

    [IO.File]::AppendAllText($result.receipt_path, " `n", [Text.UTF8Encoding]::new($false))
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $result.receipt_path `
            -ExpectedReceiptSha256 $result.receipt_sha256 `
            -ContractPath $fixture.ContractPath `
            -ExpectedContractSha256 $fixture.ContractSha256 `
            -RepositoryRoot $fixture.Root
    } 'receipt|sha256|hash|digest|tamper'

    $semanticFixture = New-QtBaseRefreshFixture
    $semanticExecute = { param($context) New-ValidObservation $semanticFixture $context }.GetNewClosure()
    $semanticArguments = Get-InvokeArguments $semanticFixture 'semantic-receipt-tamper' $semanticExecute
    $semanticResult = Invoke-TestQtBaseRefresh @semanticArguments
    $receipt = Get-Content -LiteralPath $semanticResult.receipt_path -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $receipt.configuration.features.vulkan = $false
    $tamperedSha256 = Write-TestJson -LiteralPath $semanticResult.receipt_path -Document $receipt
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $semanticResult.receipt_path `
            -ExpectedReceiptSha256 $tamperedSha256 `
            -ContractPath $semanticFixture.ContractPath `
            -ExpectedContractSha256 $semanticFixture.ContractSha256 `
            -RepositoryRoot $semanticFixture.Root
    } 'receipt|configuration|feature|contract|semantic|drift'
}

Test-Case 'receipt verification revalidates the manifest and every physical evidence leaf' {
    $fixture = New-QtBaseRefreshFixture
    $execute = { param($context) New-ValidObservation $fixture $context }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'evidence-leaf-verification' $execute
    $result = Invoke-TestQtBaseRefresh @arguments
    $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $manifest = Get-Content -LiteralPath $result.evidence_manifest_path -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $artifactPath = Join-Path $arguments.RunRoot $manifest.artifacts[0].relative_path
    [IO.File]::AppendAllText($artifactPath, 'tampered', [Text.UTF8Encoding]::new($false))

    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $result.receipt_path `
            -ExpectedReceiptSha256 $result.receipt_sha256 `
            -ContractPath $fixture.ContractPath `
            -ExpectedContractSha256 $fixture.ContractSha256 `
            -RepositoryRoot $fixture.Root
    } 'evidence|manifest|artifact|physical|sha256|hash|size|tamper'
}

    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    if ($state.Failed -ne 0) { exit 1 }
}
finally {
    if ([IO.Directory]::Exists($testRoot)) {
        $cleanupRoot = [IO.Path]::GetFullPath($testRoot)
        $parsedCleanupRunId = [Guid]::Empty
        $hasGuidLeaf = [Guid]::TryParseExact(
            [IO.Path]::GetFileName($cleanupRoot),
            'N',
            [ref]$parsedCleanupRunId
        )
        if ([IO.Path]::GetDirectoryName($cleanupRoot) -cne $testRootParent -or
            [IO.Path]::GetFileName($cleanupRoot) -cne $testRunId -or
            -not $hasGuidLeaf) {
            throw 'Refusing to clean a test run root outside the fixed GUID child boundary.'
        }
        Remove-M0QtBaseRefreshTestTree -LiteralPath $cleanupRoot
    }
}
