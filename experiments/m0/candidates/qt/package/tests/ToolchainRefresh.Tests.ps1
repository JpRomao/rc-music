#Requires -Version 7.0
[CmdletBinding()]
param([string] $Filter = '*')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../QtShellPackageBuilder.psm1') -Force

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$testRoot = Join-Path $repository ('out/m0/test-runs/qt-package/refresh-' + [Guid]::NewGuid().ToString('N'))
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
function Get-TestHash([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function New-SyntheticRefresh {
    # Deliberately synthetic versions/hashes; these are not installation pins.
    $installRoot = Join-Path $testRoot 'synthetic VS com acentuação'
    $tools = @(
        [pscustomobject] @{
            name = 'cl'; relative_path = 'VC/Tools/MSVC/14.44.40000/bin/Hostx64/x64/cl.exe'
            size_bytes = 11L; sha256 = ('a' * 64); file_version = '19.44.40000.0'; product_version = '14.44.40000.0'
            signer_subject = 'CN=Microsoft Corporation, O=Microsoft Corporation, C=US'; signer_thumbprint = ('A' * 40)
        },
        [pscustomobject] @{
            name = 'link'; relative_path = 'VC/Tools/MSVC/14.44.40000/bin/Hostx64/x64/link.exe'
            size_bytes = 12L; sha256 = ('b' * 64); file_version = '14.44.40000.0'; product_version = '14.44.40000.0'
            signer_subject = 'CN=Microsoft Corporation, O=Microsoft Corporation, C=US'; signer_thumbprint = ('B' * 40)
        },
        [pscustomobject] @{
            name = 'dumpbin'; relative_path = 'VC/Tools/MSVC/14.44.40000/bin/Hostx64/x64/dumpbin.exe'
            size_bytes = 13L; sha256 = ('c' * 64); file_version = '14.44.40000.0'; product_version = '14.44.40000.0'
            signer_subject = 'CN=Microsoft Corporation, O=Microsoft Corporation, C=US'; signer_thumbprint = ('C' * 40)
        }
    )
    $contract = [pscustomobject] @{
        schema_version = 1; refresh_id = 'm0.2-qt-msvc-refresh-v1'
        historical_contract_sha256 = 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952'
        installation_root = $installRoot; toolset_version = '14.44.40000'
        qt_version = '6.11.2'; qt_build_msvc = '19.44.35227'
        provisioning = [pscustomobject] @{
            bootstrapper_version = '18.9.12120.119'; bootstrapper_sha256 = ('d' * 64)
            component_id = 'Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64'
        }
        tools = $tools
    }
    $observed = @($tools | ForEach-Object {
        [pscustomobject] @{
            name = $_.name; path = Join-Path $installRoot $_.relative_path
            size_bytes = $_.size_bytes; sha256 = $_.sha256
            file_version = $_.file_version; product_version = $_.product_version
            signature_status = 'Valid'; signer_subject = $_.signer_subject; signer_thumbprint = $_.signer_thumbprint
        }
    })
    $cl = $observed[0].path.Replace('\', '/')
    $link = $observed[1].path.Replace('\', '/')
    $info = @"
set(CMAKE_CXX_COMPILER "$cl")
set(CMAKE_CXX_COMPILER_ID "MSVC")
set(CMAKE_CXX_COMPILER_VERSION "19.44.40000.0")
set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID x64)
set(CMAKE_LINKER "$link")
set(CMAKE_CXX_COMPILER_LINKER_ID "MSVC")
set(CMAKE_CXX_COMPILER_LINKER_VERSION 14.44.40000.0)
"@
    $cache = @"
CMAKE_CXX_COMPILER:FILEPATH=$cl
CMAKE_LINKER:FILEPATH=$link
"@
    $qt = @"
QT_ARCH = x86_64
QT_VERSION = 6.11.2
QT_MSVC_MAJOR_VERSION = 19
QT_MSVC_MINOR_VERSION = 44
QT_MSVC_PATCH_VERSION = 35227
"@
    return @{ Contract = $contract; CompilerInfoText = $info; CacheText = $cache; QtConfigText = $qt; ToolEvidence = $observed }
}

Test-Case 'refresh accepts only coherent pinned x64 MSVC 14.44 observations and preserves provenance' {
    $inputData = New-SyntheticRefresh
    $identity = Get-QtShellMsvcRefreshIdentity @inputData
    Assert-Equal $identity.shell_msvc '19.44.40000.0'
    Assert-Equal $identity.linker_msvc '14.44.40000.0'
    Assert-Equal $identity.qt_build_msvc '19.44.35227'
    Assert-Equal $identity.compiler $inputData.ToolEvidence[0].path
    Assert-Equal $identity.linker $inputData.ToolEvidence[1].path
    Assert-Equal $identity.dumpbin $inputData.ToolEvidence[2].path
    Assert-Equal $identity.tools.Count 3
}
Test-Case 'refresh accepts CRLF or LF without executing CMake text' {
    $inputData = New-SyntheticRefresh
    $crlf = [string][char]13 + [char]10
    foreach ($newline in @($crlf, [string][char]10)) {
        foreach ($name in @('CompilerInfoText', 'CacheText', 'QtConfigText')) {
            $inputData[$name] = $inputData[$name] -replace '\r?\n', $newline
        }
        $identity = Get-QtShellMsvcRefreshIdentity @inputData
        Assert-Equal $identity.shell_msvc '19.44.40000.0'
    }
}
Test-Case 'refresh rejects historical 19.43, later families and a CMake patch different from the pin' {
    foreach ($version in @('19.43.34808.0', '19.50.35729.0', '19.44.40001.0')) {
        $inputData = New-SyntheticRefresh
        $inputData.CompilerInfoText = $inputData.CompilerInfoText.Replace('19.44.40000.0', $version)
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'compiler.*version|compiler.*drift'
    }
}
Test-Case 'refresh rejects any changed compiler or linker path in CMake info or cache' {
    foreach ($textName in @('CompilerInfoText', 'CacheText')) {
        foreach ($toolIndex in @(0, 1)) {
            $inputData = New-SyntheticRefresh
            $old = $inputData.ToolEvidence[$toolIndex].path.Replace('\', '/')
            $inputData[$textName] = $inputData[$textName].Replace($old, $old.Replace('/bin/', '/other-bin/'))
            Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'compiler.*path|linker.*path'
        }
    }
}
Test-Case 'refresh rejects duplicated or missing CMake identity fields' {
    foreach ($field in @('CMAKE_CXX_COMPILER', 'CMAKE_CXX_COMPILER_VERSION', 'CMAKE_LINKER', 'CMAKE_CXX_COMPILER_LINKER_VERSION')) {
        $inputData = New-SyntheticRefresh
        $line = @($inputData.CompilerInfoText -split '\r?\n' | Where-Object { $_ -match ('^set\(' + $field + ' ') })[0]
        $inputData.CompilerInfoText += [char]10 + $line
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'duplicate|exactly once'
    }
    $inputData = New-SyntheticRefresh
    $inputData.CacheText = $inputData.CacheText -replace '(?m)^CMAKE_LINKER:[^\r\n]+', ''
    Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'linker|exactly once'
    $inputData = New-SyntheticRefresh
    $inputData.CacheText += [char]10 + ($inputData.CacheText -split '\r?\n')[0]
    Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'duplicate|exactly once'
}
Test-Case 'refresh rejects another compiler ID, x86 and mismatched linker probe version' {
    foreach ($pair in @(@('"MSVC"', '"Clang"'), @(' x64)', ' X86)'), @('VERSION 14.44.40000.0)', 'VERSION 14.43.34808.0)'))) {
        $inputData = New-SyntheticRefresh
        $inputData.CompilerInfoText = $inputData.CompilerInfoText.Replace($pair[0], $pair[1])
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'MSVC|x64|linker.*version'
    }
}
Test-Case 'refresh rejects a drifted Qt version, architecture, compiler or duplicated qconfig field' {
    foreach ($pair in @(@('6.11.2', '6.11.3'), @('x86_64', 'i386'), @('= 44', '= 43'), @('= 35227', '= 35228'))) {
        $inputData = New-SyntheticRefresh
        $inputData.QtConfigText = $inputData.QtConfigText.Replace($pair[0], $pair[1])
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'Qt'
    }
    $inputData = New-SyntheticRefresh
    $inputData.QtConfigText += [char]10 + 'QT_MSVC_MINOR_VERSION = 44'
    Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'Qt|exactly once'
}
Test-Case 'refresh rejects missing, duplicate and extra tool observations' {
    foreach ($kind in @('missing', 'duplicate', 'extra')) {
        $inputData = New-SyntheticRefresh
        if ($kind -eq 'missing') { $inputData.ToolEvidence = @($inputData.ToolEvidence[0], $inputData.ToolEvidence[1]) }
        elseif ($kind -eq 'duplicate') { $inputData.ToolEvidence[2].name = 'CL' }
        else { $inputData.ToolEvidence += $inputData.ToolEvidence[0] }
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'tool|duplicate'
    }
}
Test-Case 'refresh authenticates the bytes, sizes, versions and signatures of all three tools' {
    foreach ($toolIndex in @(0, 1, 2)) {
        foreach ($field in @('sha256', 'size_bytes', 'file_version', 'product_version', 'signature_status', 'signer_thumbprint', 'signer_subject', 'path')) {
            $inputData = New-SyntheticRefresh
            $tool = $inputData.ToolEvidence[$toolIndex]
            if ($field -eq 'size_bytes') { $tool.$field = 999L }
            elseif ($field -eq 'path') { $tool.$field = $tool.path.Replace('Hostx64', 'Hostx86') }
            else { $tool.$field = 'different' }
            Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'tool|signature|identity|version|hash|size|path'
        }
    }
}
Test-Case 'refresh contract cannot bless a different MSVC family, mixed builds or an older-than-Qt linker' {
    foreach ($kind in @('old-family', 'later-family', 'mixed-builds', 'too-old')) {
        $inputData = New-SyntheticRefresh
        if ($kind -eq 'old-family') { $inputData.Contract.tools[0].file_version = '19.43.34808.0' }
        elseif ($kind -eq 'later-family') { $inputData.Contract.tools[0].file_version = '19.50.35729.0' }
        elseif ($kind -eq 'mixed-builds') { $inputData.Contract.tools[1].file_version = '14.44.40001.0' }
        else {
            foreach ($tool in $inputData.Contract.tools) {
                $tool.file_version = $tool.file_version.Replace('40000', '35226')
                $tool.product_version = $tool.product_version.Replace('40000', '35226')
            }
        }
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } '14.44|coherent|Qt|version|family'
    }
}
Test-Case 'refresh contract is closed and keeps its historical baseline immutable' {
    foreach ($kind in @('schema', 'baseline', 'extra', 'tool-extra', 'bad-hash', 'string-size')) {
        $inputData = New-SyntheticRefresh
        if ($kind -eq 'schema') { $inputData.Contract.schema_version = 2 }
        elseif ($kind -eq 'baseline') { $inputData.Contract.historical_contract_sha256 = '0' * 64 }
        elseif ($kind -eq 'extra') { $inputData.Contract | Add-Member -NotePropertyName allow_old_compiler -NotePropertyValue $true }
        elseif ($kind -eq 'tool-extra') { $inputData.Contract.tools[0] | Add-Member -NotePropertyName unchecked -NotePropertyValue $true }
        elseif ($kind -eq 'bad-hash') { $inputData.Contract.tools[0].sha256 = 'not a digest' }
        else { $inputData.Contract.tools[0].size_bytes = '11' }
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'contract|schema|baseline|hash|size'
    }
}
Test-Case 'refresh contract requires exact tools from one normal local Hostx64 x64 directory' {
    foreach ($kind in @('traversal', 'other-bin', 'device', 'duplicate', 'relative-root')) {
        $inputData = New-SyntheticRefresh
        if ($kind -eq 'traversal') { $inputData.Contract.tools[2].relative_path = '../dumpbin.exe' }
        elseif ($kind -eq 'other-bin') { $inputData.Contract.tools[2].relative_path = $inputData.Contract.tools[2].relative_path.Replace('Hostx64', 'Hostx86') }
        elseif ($kind -eq 'device') { $inputData.Contract.installation_root = '\\?\C:\synthetic' }
        elseif ($kind -eq 'duplicate') { $inputData.Contract.tools[2].name = 'cl' }
        else { $inputData.Contract.installation_root = 'relative/root' }
        Assert-Rejected { Get-QtShellMsvcRefreshIdentity @inputData } 'contract|tool|path|directory|root|duplicate'
    }
}
Test-Case 'receipt refuses an unauthenticated contract before reading or executing any tool' {
    $inputData = New-SyntheticRefresh
    $contractPath = Join-Path $testRoot 'synthetic-contract.json'
    [IO.File]::WriteAllText($contractPath, ($inputData.Contract | ConvertTo-Json -Depth 8))
    Assert-Rejected {
        New-QtShellMsvcRefreshReceipt -ContractPath $contractPath -ExpectedContractSha256 ('0' * 64) -CompilerInfoPath (Join-Path $testRoot 'missing-info') -CachePath (Join-Path $testRoot 'missing-cache') -QtConfigPath (Join-Path $testRoot 'missing-qt')
    } 'contract.*hash|contract.*digest'
}
Test-Case 'receipt rejects a reparse contract without following it' {
    $target = Join-Path $testRoot 'contract-target'
    $null = New-Item -ItemType Directory -Path $target
    $contractPath = Join-Path $target 'contract.json'
    [IO.File]::WriteAllText($contractPath, '{}')
    $junction = Join-Path $testRoot 'contract-junction'
    $null = New-Item -ItemType Junction -Path $junction -Target $target
    Assert-Rejected {
        New-QtShellMsvcRefreshReceipt -ContractPath (Join-Path $junction 'contract.json') -ExpectedContractSha256 (Get-TestHash $contractPath) -CompilerInfoPath (Join-Path $testRoot 'missing-info') -CachePath (Join-Path $testRoot 'missing-cache') -QtConfigPath (Join-Path $testRoot 'missing-qt')
    } 'reparse'
}
Test-Case 'receipt reads actual tool bytes and refuses synthetic unsigned executables' {
    $inputData = New-SyntheticRefresh
    foreach ($pin in $inputData.Contract.tools) {
        $toolPath = Join-Path $inputData.Contract.installation_root $pin.relative_path
        $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($toolPath)) -Force
        [IO.File]::WriteAllText($toolPath, 'Synthetic bytes only; this file must never execute.')
        $pin.sha256 = Get-TestHash $toolPath
        $pin.size_bytes = (Get-Item -LiteralPath $toolPath).Length
    }
    $paths = @{
        ContractPath = Join-Path $testRoot 'unsigned-contract.json'
        CompilerInfoPath = Join-Path $testRoot 'unsigned-info.cmake'
        CachePath = Join-Path $testRoot 'unsigned-cache.txt'
        QtConfigPath = Join-Path $testRoot 'unsigned-qconfig.pri'
    }
    [IO.File]::WriteAllText($paths.CompilerInfoPath, $inputData.CompilerInfoText)
    [IO.File]::WriteAllText($paths.CachePath, $inputData.CacheText)
    [IO.File]::WriteAllText($paths.QtConfigPath, $inputData.QtConfigText)
    [IO.File]::WriteAllText($paths.ContractPath, ($inputData.Contract | ConvertTo-Json -Depth 8))
    $paths.ExpectedContractSha256 = Get-TestHash $paths.ContractPath
    Assert-Rejected { New-QtShellMsvcRefreshReceipt @paths } 'Tool identity|Tool signature'
    $inputData.Contract.tools[0].sha256 = '0' * 64
    [IO.File]::WriteAllText($paths.ContractPath, ($inputData.Contract | ConvertTo-Json -Depth 8))
    $paths.ExpectedContractSha256 = Get-TestHash $paths.ContractPath
    Assert-Rejected { New-QtShellMsvcRefreshReceipt @paths } 'Tool bytes'
}
Test-Case 'historical preflight contract and every authenticated family member retain original bytes' {
    $contractPath = Join-Path $repository 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-contract.v1.json'
    Assert-Equal (Get-TestHash $contractPath) 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952'
    $historical = Get-Content -LiteralPath $contractPath -Raw | ConvertFrom-Json
    foreach ($file in $historical.files) {
        $path = Join-Path $repository $file.path
        Assert-Equal (Get-TestHash $path) $file.sha256
        Assert-Equal (Get-Item -LiteralPath $path).Length $file.size_bytes
    }
}

Write-Host "Toolchain refresh: $($state.Passed) passed, $($state.Failed) failed. Fixtures retained at $testRoot"
if ($state.Failed -ne 0) { exit 1 }
