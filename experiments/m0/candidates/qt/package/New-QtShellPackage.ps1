#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $BuildDirectory,
    [Parameter(Mandatory)] [string] $Name
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if (-not $IsWindows) { throw 'This local evaluation package requires Windows x64.' }
Import-Module (Join-Path $PSScriptRoot 'QtShellPackageBuilder.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'QtShellPackageManifest.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'QtShellPackagePrerequisites.psm1') -Force

function Get-Sha256([string] $Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Write-Json([string] $Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 60) + "`n", [Text.UTF8Encoding]::new($false))
}
function Invoke-Native([string] $File, [string[]] $Arguments, [string] $WorkingDirectory) {
    $start = [Diagnostics.ProcessStartInfo]::new($File)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.WorkingDirectory = $WorkingDirectory
    $start.Environment['VSLANG'] = '1033'
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Could not start $File" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(120000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "Tool timed out; only its retained process tree was terminated: $File"
        }
        $result = [pscustomobject] @{ stdout = $stdout.GetAwaiter().GetResult(); stderr = $stderr.GetAwaiter().GetResult(); exit_code = $process.ExitCode }
        if ($result.exit_code -ne 0) { throw "Tool failed ($($result.exit_code)): $File`n$($result.stdout)`n$($result.stderr)" }
        return $result
    }
    finally { $process.Dispose() }
}

$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
$source = Resolve-QtShellPackageChildPath $repository 'experiments/m0/candidates/qt/shell'
$qt = Resolve-QtShellPackageChildPath $repository 'out/m0/toolchains/qt-6.11.2-msvc2022_64'
$buildParent = Resolve-QtShellPackageChildPath $repository 'out/m0/builds/qt-shell'
$build = Resolve-QtShellPackageChildPath $buildParent ([IO.Path]::GetFullPath($BuildDirectory, $repository))
$applicationQml = Resolve-QtShellPackageChildPath $build 'qml/MusicPlayer/M0'
$cachePath = Resolve-QtShellPackageChildPath $build 'CMakeCache.txt'
$compilerInfoPath = Resolve-QtShellPackageChildPath $build 'CMakeFiles/3.31.5/CMakeCXXCompiler.cmake'
$qtConfigPath = Resolve-QtShellPackageChildPath $qt 'mkspecs/qconfig.pri'
$refreshContractRelative = 'experiments/m0/provisioning/qt-shell/msvc-refresh-contract.v1.json'
$refreshContract = Resolve-QtShellPackageChildPath $repository $refreshContractRelative
$refreshContractHash = 'eead467f5a597a72b6ff45812b40040db629232bc089d0c61c0dd64c73693f03'
$contract = Resolve-QtShellPackageChildPath $repository 'experiments/m0/provisioning/qt-shell/qt-shell-toolchain-contract.v1.json'
if ((Get-Sha256 $contract) -cne 'ac00a56584f2aed24732d44f9b26107084d37a73467fc56a860feeb850181952') {
    throw 'The historical toolchain contract has changed; review the package recipe first.'
}

$sourcePaths = @('experiments/m0/candidates/qt/shell', 'experiments/m0/shared/shell-fixture', 'experiments/m0/protocol')
function Get-SourceStatus {
    $status = @(& git -C $repository status --porcelain=v1 --untracked-files=all -- @sourcePaths)
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect source Git status.' }
    return $status
}
if (@(Get-SourceStatus).Count -ne 0) { throw 'Commit shell/fixture/protocol changes before building an identified package.' }
$commit = @(& git -C $repository rev-parse HEAD) -join ''
if ($LASTEXITCODE -ne 0 -or $commit -cnotmatch '^[a-f0-9]{40}$') { throw 'Could not identify the source commit.' }
$recipeStatus = @(& git -C $repository status --porcelain=v1 --untracked-files=all -- 'experiments/m0/candidates/qt/package' 'packaging/windows/PeDependencyReport.psm1' $refreshContractRelative)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect package recipe status.' }

$packageParent = Resolve-QtShellPackageChildPath $repository 'out/m0/packages/qt-shell'
if ($Name.Contains('/') -or $Name.Contains('\')) { throw 'Package name must be one path component.' }
$plannedDestination = Resolve-QtShellPackageChildPath $packageParent $Name
if (Test-Path -LiteralPath $plannedDestination) { throw "Package destination already exists: $plannedDestination" }
Write-Host "Rebuilding the Release shell with authenticated MSVC 14.44 for commit $commit..."
$refreshedBuild = Invoke-QtShellRefreshedBuild -ContractPath $refreshContract -ExpectedContractSha256 $refreshContractHash -CompilerInfoPath $compilerInfoPath -CachePath $cachePath -QtConfigPath $qtConfigPath -SourceRoot $source -QtRoot $qt -BuildAction {
    param($identity)
    $prerequisites = Get-QtShellPackagePrerequisites -RepositoryRoot $repository -QtRoot $qt -CachePath $cachePath -CompilerPath $identity.compiler -ToolsetVersion $identity.toolset_version
    $cmake = @($prerequisites.tools | Where-Object { $_.name -ceq 'cmake' })[0].path
    Invoke-Native $cmake @('--build', $build, '--target', 'm0_qt_shell') $repository
}
if (@(Get-SourceStatus).Count -ne 0 -or (@(& git -C $repository rev-parse HEAD) -join '') -cne $commit) {
    throw 'Source state changed during the build.'
}
$configuration = $refreshedBuild.configuration
$receipt = $refreshedBuild.receipt
$compilerIdentity = $receipt.identity
$prerequisites = Get-QtShellPackagePrerequisites -RepositoryRoot $repository -QtRoot $qt -CachePath $cachePath -CompilerPath $compilerIdentity.compiler -ToolsetVersion $compilerIdentity.toolset_version
$deploy = @($prerequisites.tools | Where-Object { $_.name -ceq 'windeployqt' })[0].path
$qtpaths = @($prerequisites.tools | Where-Object { $_.name -ceq 'qtpaths' })[0].path
$dumpbin = $compilerIdentity.dumpbin

# No stage is created until the build and both identity checks succeed.
$destination = New-QtShellPackageDirectory -Parent $packageParent -Name $Name
$payload = Resolve-QtShellPackageChildPath $destination 'app'
$evidence = Resolve-QtShellPackageChildPath $destination 'evidence'
$null = New-Item -ItemType Directory -Path $payload, $evidence
$buildResult = $refreshedBuild.build_result
[IO.File]::WriteAllText((Join-Path $evidence 'build.log'), $buildResult.stdout + $buildResult.stderr)
$receiptPath = Join-Path $evidence 'msvc-refresh-receipt.v1.json'
Write-Json $receiptPath $receipt
$executable = Resolve-QtShellPackageChildPath $build 'm0_qt_shell.exe'
$arguments = @(
    '--release', '--qtpaths', $qtpaths, '--qmldir', (Join-Path $source 'qml'),
    '--qmlimport', (Join-Path $build 'qml'), '--dir', $payload,
    '--plugindir', (Join-Path $payload 'plugins'), '--qml-deploy-dir', (Join-Path $payload 'qml'),
    '--no-patchqt', '--no-compiler-runtime', '--no-translations', '--no-ffmpeg',
    '--no-system-d3d-compiler', '--no-system-dxc-compiler', '--skip-plugin-types', 'qmltooling',
    '--include-plugins', 'qsvg,qsvgicon,qoffscreen'
)
Write-Host 'Scanning the actual Qt and application QML imports...'
$dryRun = Invoke-Native $deploy @($arguments + @('--dry-run', '--json', $executable)) $repository
if (-not [string]::IsNullOrWhiteSpace($dryRun.stderr)) { throw "Deployment scan diagnostic requires review: $($dryRun.stderr)" }
# The VS environment can redirect D3D discovery to a Windows SDK. Use only the
# separately provisioned Qt-toolchain copy, never a host SDK/System32 copy.
$d3d = [pscustomobject] @{ source = Join-Path $qt 'd3dcompiler_47.dll'; target = $payload }
$dryJson = $dryRun.stdout | ConvertFrom-Json -Depth 50
$plan = ConvertTo-QtShellDeploymentPlan @{ files = @($dryJson.files) + @($d3d) } $qt $payload -ApplicationQmlRoot $applicationQml
Write-Json (Join-Path $evidence 'deployment-plan.v1.json') $plan
Copy-Item -LiteralPath $executable -Destination (Join-Path $payload 'm0_qt_shell.exe')
Write-Host "Deploying $($plan.files.Count) mapped files into the new local stage..."
$deployment = Invoke-Native $deploy @($arguments + @('--json', $executable)) $repository
if (-not [string]::IsNullOrWhiteSpace($deployment.stderr)) { throw "Deployment diagnostic requires review: $($deployment.stderr)" }
$deployedJson = $deployment.stdout | ConvertFrom-Json -Depth 50
Copy-Item -LiteralPath $d3d.source -Destination (Join-Path $payload 'd3dcompiler_47.dll')
$actualPlan = ConvertTo-QtShellDeploymentPlan @{ files = @($deployedJson.files) + @($d3d) } $qt $payload -ApplicationQmlRoot $applicationQml
if (-not [string]::Equals(($actualPlan | ConvertTo-Json -Depth 20 -Compress), ($plan | ConvertTo-Json -Depth 20 -Compress), [StringComparison]::Ordinal)) {
    throw 'Deployment changed between the scan and copy.'
}

Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'qt.conf') -Destination (Join-Path $payload 'qt.conf')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'LOCAL-EVALUATION.txt') -Destination (Join-Path $payload 'LOCAL-EVALUATION.txt')
$thirdParty = Join-Path $payload 'third-party'
$null = New-Item -ItemType Directory -Path $thirdParty
$sbomNames = @('qtbase-6.11.2.spdx', 'qtdeclarative-6.11.2.spdx', 'qtsvg-6.11.2.spdx')
foreach ($sbomName in $sbomNames) { Copy-Item -LiteralPath (Join-Path $qt "sbom/$sbomName") -Destination (Join-Path $thirdParty $sbomName) }

$inventory = New-QtShellPackageManifest -PackageRoot $payload -Metadata ([pscustomobject] @{ purpose = 'intermediate-validation' })
$expectedPaths = @($plan.files.relative_path) + @('m0_qt_shell.exe', 'qt.conf', 'LOCAL-EVALUATION.txt') + @($sbomNames | ForEach-Object { "third-party/$_" })
$expectedSet = [Collections.Generic.HashSet[string]]::new([string[]] $expectedPaths, [StringComparer]::Ordinal)
if ($inventory.files.Count -ne $expectedPaths.Count -or
    -not $expectedSet.SetEquals([string[]] @($inventory.files.relative_path))) {
    throw 'The deployed payload differs from the exact planned file set.'
}
Assert-QtShellRuntimeFiles @($inventory.files.relative_path)
$byPath = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $inventory.files) { $byPath[$file.relative_path] = $file }
foreach ($file in $plan.files) {
    if ($byPath[$file.relative_path].sha256 -cne $file.sha256 -or $byPath[$file.relative_path].size_bytes -ne $file.size_bytes) {
        throw "Deployed bytes differ from the source mapping: $($file.relative_path)"
    }
}
if ($byPath['m0_qt_shell.exe'].sha256 -cne (Get-Sha256 $executable)) { throw 'The staged executable differs from the build.' }

Write-Host 'Checking x64 PE imports against the payload or explicit Windows prerequisites...'
$peFiles = @($inventory.files | Where-Object { $_.relative_path -match '\.(?:dll|exe)$' })
$peRecords = [Collections.Generic.List[object]]::new()
$externalFiles = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $peFiles) {
    $absolute = Join-Path $payload $file.relative_path
    $report = Invoke-Native $dumpbin @('/nologo', '/headers', '/dependents', $absolute) $repository
    if (-not [string]::IsNullOrWhiteSpace($report.stderr)) { throw "PE diagnostic requires review: $($report.stderr)" }
    $parsed = ConvertFrom-QtShellPeReport $report.stdout
    $imports = @($parsed.imports | ForEach-Object {
        $dependency = $_
        $sibling = [IO.Path]::GetRelativePath($payload, (Join-Path ([IO.Path]::GetDirectoryName($absolute)) $dependency)).Replace('\', '/')
        if ($byPath.ContainsKey($sibling)) { [pscustomobject] @{ name = $dependency; kind = 'payload'; path = $sibling } }
        elseif ($byPath.ContainsKey($dependency)) { [pscustomobject] @{ name = $dependency; kind = 'payload'; path = $dependency } }
        elseif ($dependency -match '^(?:api|ext)-ms-win-[a-z0-9-]+\.dll$') {
            [pscustomobject] @{ name = $dependency; kind = 'windows-api-set'; path = $null }
        }
        else {
            $systemFile = Join-Path ([Environment]::SystemDirectory) $dependency
            if ($dependency -match '^Qt6' -or -not (Test-Path -LiteralPath $systemFile -PathType Leaf)) {
                throw "Unresolved PE dependency: $($file.relative_path) -> $dependency"
            }
            $kind = if ($dependency -match '^(?:vcruntime|msvcp|concrt|vccorlib)') { 'host-msvc-runtime' } else { 'windows-system' }
            if (-not $externalFiles.ContainsKey($dependency)) {
                $systemInfo = Get-Item -LiteralPath $systemFile
                $externalFiles.Add($dependency, [pscustomobject] [ordered] @{
                    name = $dependency; kind = $kind; location = "Windows/System32/$dependency"
                    file_version = $systemInfo.VersionInfo.FileVersion; sha256 = Get-Sha256 $systemFile
                })
            }
            [pscustomobject] @{ name = $dependency; kind = $kind; path = "Windows/System32/$dependency" }
        }
    })
    $peRecords.Add([pscustomobject] [ordered] @{ relative_path = $file.relative_path; machine = $parsed.machine; imports = $imports })
}
[string[]] $externalNames = @($externalFiles.Keys)
[Array]::Sort($externalNames, [StringComparer]::Ordinal)
$closure = [pscustomobject] [ordered] @{
    schema_version = 1
    files = @($peRecords)
    external_files = @($externalNames | ForEach-Object { $externalFiles[$_] })
    limitation = 'Static imports, including delay imports; not a full runtime LoadLibrary or clean-machine proof.'
}
Write-Json (Join-Path $evidence 'pe-closure.v1.json') $closure

$recipeFiles = @(
    'experiments/m0/candidates/qt/package/New-QtShellPackage.ps1',
    'experiments/m0/candidates/qt/package/QtShellPackageBuilder.psm1',
    'experiments/m0/candidates/qt/package/QtShellPackageManifest.psm1',
    'experiments/m0/candidates/qt/package/QtShellPackagePrerequisites.psm1',
    'experiments/m0/candidates/qt/package/qt.conf',
    'experiments/m0/candidates/qt/package/LOCAL-EVALUATION.txt',
    'packaging/windows/PeDependencyReport.psm1'
)
$metadata = [pscustomobject] [ordered] @{
    purpose = 'm0.2-private-local-evaluation'
    source_commit = $commit
    source_paths_clean = $true
    recipe_dirty = ($recipeStatus.Count -ne 0)
    configuration = $configuration.configuration
    architecture = 'x64'
    test_hooks = $configuration.test_hooks
    qt_version = '6.11.2'
    qt_build_msvc = $compilerIdentity.qt_build_msvc
    shell_msvc = $compilerIdentity.shell_msvc
    linker_msvc = $compilerIdentity.linker_msvc
    toolset_version = $compilerIdentity.toolset_version
    toolchain_contract_sha256 = Get-Sha256 $contract
    msvc_refresh_contract_sha256 = $receipt.refresh_contract_sha256
    msvc_refresh_receipt_path = 'evidence/msvc-refresh-receipt.v1.json'
    msvc_refresh_receipt_sha256 = Get-Sha256 $receiptPath
    fixture_manifest_sha256 = Get-Sha256 (Join-Path $repository 'experiments/m0/shared/shell-fixture/manifest.v1.json')
    protocol_sha256 = Get-Sha256 (Join-Path $repository 'experiments/m0/protocol/protocol.v1.json')
    cmake_cache_sha256 = $receipt.cmake_cache_sha256
    compiler_info_sha256 = $receipt.compiler_info_sha256
    deployment_plan_sha256 = Get-Sha256 (Join-Path $evidence 'deployment-plan.v1.json')
    pe_closure_sha256 = Get-Sha256 (Join-Path $evidence 'pe-closure.v1.json')
    tools = @($prerequisites.tools) + @($compilerIdentity.tools)
    build_environment = $prerequisites.environment
    recipe_path_base = 'repository'
    recipe = @($recipeFiles | ForEach-Object { [pscustomobject] @{ relative_path = $_; sha256 = Get-Sha256 (Resolve-QtShellPackageChildPath $repository $_) } })
    external_msvc_runtime = @($closure.external_files | Where-Object { $_.kind -eq 'host-msvc-runtime' })
    redistribution_allowed = $false
    known_limits = @(
        'Host MSVC runtime is external; no redistributable or Windows DLL is included.',
        'Pinned MSVC 19.44 compiler/linker and SDK environment do not certify ABI or execution on a clean machine.',
        'Selected launchers are authenticated; Qt codegen/scanner tools and every transitive header/library are not individually attested.',
        'Project redistribution license, full third-party notices/sources and D3D/Mesa license review remain open.',
        'DXC/Direct3D 12 is excluded; native graphics, tray/Explorer, NVDA, real HC/DPI, Linux and performance remain unverified.',
        'Quiescent inputs and destination required; no concurrent-writer security guarantee.'
    )
}
$manifest = New-QtShellPackageManifest -PackageRoot $payload -Metadata $metadata
$manifestPath = Join-Path $destination 'manifest.v1.json'
Write-Json $manifestPath $manifest
$manifestHash = Get-Sha256 $manifestPath
[IO.File]::WriteAllText("$manifestPath.sha256", "$manifestHash  manifest.v1.json`n", [Text.UTF8Encoding]::new($false))
$null = Test-QtShellPackageManifest -PackageRoot $payload -ManifestPath $manifestPath -ExpectedManifestSha256 $manifestHash
[pscustomobject] [ordered] @{
    package_root = $payload; manifest_path = $manifestPath; manifest_sha256 = $manifestHash
    source_commit = $commit; files = $manifest.files.Count
    payload_bytes = ($manifest.files | Measure-Object -Property size_bytes -Sum).Sum
    pe_files = $peRecords.Count; smoke_passed = $false; redistribution_allowed = $false
}
