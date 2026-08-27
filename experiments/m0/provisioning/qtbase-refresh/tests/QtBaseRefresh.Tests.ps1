#Requires -Version 7.0
[CmdletBinding()]
param([string] $Filter = '*')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path $PSScriptRoot '../M0.QtBaseRefresh.psm1'
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

function Write-TestText {
    param([Parameter(Mandatory)][string] $LiteralPath, [Parameter(Mandatory)][string] $Text)
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

Test-Case 'the QtBase refresh module exports the minimal public orchestration contract' {
    Assert-True (Test-Path -LiteralPath $modulePath -PathType Leaf) `
        "Required module is absent: $modulePath"
    $script:module = Import-Module $modulePath -Force -PassThru
    $exported = @($script:module.ExportedFunctions.Keys | Sort-Object)
    foreach ($required in @(
            'Assert-M0QtBaseRefreshContract',
            'Assert-M0QtBaseRefreshReceipt',
            'Invoke-M0QtBaseRefresh'
        )) {
        Assert-True ($exported -ccontains $required) "Required function '$required' is not exported."
    }
}

if ($state.Failed -ne 0 -or $null -eq $script:module) {
    Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
    exit 1
}

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
$testRoot = Join-Path $repositoryRoot ('out/m0/test-runs/qtbase-refresh/' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null

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

function New-ValidObservation {
    param([Parameter(Mandatory)] $Fixture)
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
    }
}

function Get-InvokeArguments {
    param(
        [Parameter(Mandatory)] $Fixture,
        [Parameter(Mandatory)][string] $RunName,
        [Parameter(Mandatory)][scriptblock] $Execute
    )
    return @{
        ContractPath = $Fixture.ContractPath
        ExpectedContractSha256 = $Fixture.ContractSha256
        RepositoryRoot = $Fixture.Root
        ToolRoot = $Fixture.ToolRoot
        RunRoot = Join-Path $Fixture.RunParent $RunName
        Execute = $Execute
    }
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
        $execute = { param($context) $callState.Count++; New-ValidObservation $fixture }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "tamper-$kind" $execute
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'input|tool|sha256|hash|size|pin|authentic'
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
            $execute = { param($context) $callState.Count++; New-ValidObservation $fixture }.GetNewClosure()
            $arguments = Get-InvokeArguments $fixture "forbidden-$name" $execute
            Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'environment|forbidden|CL|LINK|inject'
            Assert-Equal $callState.Count 0 "Forbidden variable '$name' reached the callback."
            Assert-True (-not [IO.Directory]::Exists($arguments.RunRoot)) "Forbidden variable '$name' created a run root."
        }
        finally {
            [Environment]::SetEnvironmentVariable($name, $previous, 'Process')
        }
    }
}

Test-Case 'RunRoot must be a new direct child of the pinned isolated parent' {
    $fixture = New-QtBaseRefreshFixture
    $callState = @{ Count = 0 }
    $execute = { param($context) $callState.Count++; New-ValidObservation $fixture }.GetNewClosure()
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
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'RunRoot|run root|isolated|direct child|new|exist|parent'
    }
    Assert-Equal $callState.Count 0 'An invalid RunRoot reached the callback.'
}

Test-Case 'reparse-point tool roots are rejected before callback or RunRoot creation' {
    if (-not $IsWindows) { return }
    $fixture = New-QtBaseRefreshFixture
    $junction = Join-Path $fixture.Root 'toolchain-junction'
    $null = New-Item -ItemType Junction -Path $junction -Target $fixture.ToolRoot
    $callState = @{ Count = 0 }
    $execute = { param($context) $callState.Count++; New-ValidObservation $fixture }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'reparse-tool-root' $execute
    $arguments.ToolRoot = $junction
    Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'reparse|physical|tool|root|ancestor'
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
        return New-ValidObservation $fixture
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'successful-run' $execute
    $result = Invoke-M0QtBaseRefresh @arguments

    Assert-Equal $callState.Count 1 'Successful execution callback was not invoked exactly once.'
    Assert-Equal $result.contract_sha256 $fixture.ContractSha256
    Assert-True ([IO.File]::Exists($result.receipt_path)) 'Successful execution did not publish a receipt.'
    Assert-Equal (Get-TestSha256 $result.receipt_path) $result.receipt_sha256 'Returned receipt hash does not authenticate its bytes.'
}

Test-Case 'a patch report with zero or multiple changed targets prevents receipt publication' {
    foreach ($kind in @('zero', 'multiple', 'before', 'after')) {
        $fixture = New-QtBaseRefreshFixture
        $callState = @{ Count = 0 }
        $execute = {
            param($context)
            $callState.Count++
            $observation = New-ValidObservation $fixture
            if ($kind -eq 'zero') { $observation.patch.changed_relative_paths = @() }
            elseif ($kind -eq 'multiple') { $observation.patch.changed_relative_paths = @($fixture.Target, 'src/other.c') }
            elseif ($kind -eq 'before') { $observation.patch.target_before_sha256 = '0' * 64 }
            else { $observation.patch.target_after_sha256 = '0' * 64 }
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "bad-patch-$kind" $execute
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'patch|target|changed|before|after|identity|single|exact'
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
            $observation = New-ValidObservation $fixture
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
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'configuration|generator|argument|feature|expected|exact|drift'
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
    $rejection = Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'synthetic QtBase build failure'
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
        $observation = New-ValidObservation $fixture
        $observation.execution.exit_code = 7
        $observation.execution.stdout = "configure output`nbuild output"
        $observation.execution.stderr = 'synthetic compiler error'
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'native-failure' $execute
    $rejection = Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } '7|configure output|compiler error'
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
            $observation = New-ValidObservation $fixture
            $observation.execution.exit_code = $invalidExit
            return $observation
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture ('invalid-exit-' + [Guid]::NewGuid().ToString('N')) $execute
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'exit_code|exit code|integer|type'
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
        $observation = New-ValidObservation $fixture
        $observation.patch.target_after_sha256 = '0' * 64
        return $observation
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'mutated-context-contract' $execute
    Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'contract|patch|target|identity|postcondition|drift'
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
        return New-ValidObservation $fixture
    }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'evidence-junction' $execute
    Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'reparse|junction|physical|evidence|receipt|ancestor'
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
            return New-ValidObservation $fixture
        }.GetNewClosure()
        $arguments = Get-InvokeArguments $fixture "post-drift-$kind" $execute
        Assert-Rejected { Invoke-M0QtBaseRefresh @arguments } 'post|drift|contract|input|tool|sha256|hash|size|pin|authentic'
        Assert-Equal $callState.Count 1 'Post-drift callback did not execute exactly once.'
        $receiptPath = Join-Path $arguments.RunRoot $fixture.Contract.isolation.receipt_relative_path
        Assert-True (-not [IO.File]::Exists($receiptPath)) 'Post-execution drift exposed a receipt.'
    }
}

Test-Case 'receipt verification is externally hash-bound and rejects byte or semantic tamper' {
    $fixture = New-QtBaseRefreshFixture
    $execute = { param($context) New-ValidObservation $fixture }.GetNewClosure()
    $arguments = Get-InvokeArguments $fixture 'receipt-verification' $execute
    $result = Invoke-M0QtBaseRefresh @arguments

    $accepted = Assert-M0QtBaseRefreshReceipt `
        -LiteralPath $result.receipt_path `
        -ExpectedReceiptSha256 $result.receipt_sha256 `
        -ContractPath $fixture.ContractPath `
        -ExpectedContractSha256 $fixture.ContractSha256
    Assert-Equal $accepted.contract_sha256 $fixture.ContractSha256

    [IO.File]::AppendAllText($result.receipt_path, " `n", [Text.UTF8Encoding]::new($false))
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $result.receipt_path `
            -ExpectedReceiptSha256 $result.receipt_sha256 `
            -ContractPath $fixture.ContractPath `
            -ExpectedContractSha256 $fixture.ContractSha256
    } 'receipt|sha256|hash|digest|tamper'

    $semanticFixture = New-QtBaseRefreshFixture
    $semanticExecute = { param($context) New-ValidObservation $semanticFixture }.GetNewClosure()
    $semanticArguments = Get-InvokeArguments $semanticFixture 'semantic-receipt-tamper' $semanticExecute
    $semanticResult = Invoke-M0QtBaseRefresh @semanticArguments
    $receipt = Get-Content -LiteralPath $semanticResult.receipt_path -Raw | ConvertFrom-Json -Depth 100 -DateKind String
    $receipt.configuration.features.vulkan = $false
    $tamperedSha256 = Write-TestJson -LiteralPath $semanticResult.receipt_path -Document $receipt
    Assert-Rejected {
        Assert-M0QtBaseRefreshReceipt `
            -LiteralPath $semanticResult.receipt_path `
            -ExpectedReceiptSha256 $tamperedSha256 `
            -ContractPath $semanticFixture.ContractPath `
            -ExpectedContractSha256 $semanticFixture.ContractSha256
    } 'receipt|configuration|feature|contract|semantic|drift'
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -ne 0) { exit 1 }
