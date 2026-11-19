#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-RcMusicBuildDirectory {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $BuildDirectory,
        [Parameter(Mandatory)][ValidateSet('ON', 'OFF')][string] $Testing,
        [Collections.IDictionary] $Tools
    )
    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $build = [IO.Path]::GetFullPath($BuildDirectory).TrimEnd('\', '/')
    $output = [IO.Path]::GetFullPath((Join-Path $root 'out/product')).TrimEnd('\', '/') + '\'
    if (-not $build.StartsWith($output, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build directory must be a child of the product output area'
    }
    $ancestor = $build
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Build output may not traverse a reparse point'
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    $cachePath = Join-Path $build 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { return }
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^([^#/][^:]*):[^=]+=(.*)$') {
            if ($cache.ContainsKey($Matches[1])) { throw 'Duplicate CMake cache entry' }
            $cache[$Matches[1]] = $Matches[2]
        }
    }
    foreach ($entry in @{
        CMAKE_GENERATOR = 'Ninja'; CMAKE_BUILD_TYPE = 'Release'
        BUILD_TESTING = $Testing; RC_MUSIC_BUILD_DESKTOP = 'ON'
    }.GetEnumerator()) {
        if ($cache[$entry.Key] -cne $entry.Value) { throw "Unsupported cached configuration: $($entry.Key)" }
    }
    $homePath = $cache['CMAKE_HOME_DIRECTORY']
    if ($homePath -isnot [string] -or -not [IO.Path]::IsPathFullyQualified($homePath) -or
        [IO.Path]::GetFullPath($homePath).TrimEnd('\', '/') -ine $root) {
        throw 'Unsupported cached configuration: source directory'
    }
    if ($Tools) {
        $paths = @{
            CMAKE_COMMAND = $Tools.CMake; CMAKE_CXX_COMPILER = $Tools.Compiler; CMAKE_MAKE_PROGRAM = $Tools.Ninja
            Qt6_DIR = Join-Path $Tools.QtPrefix 'lib/cmake/Qt6'
            Qt6Multimedia_DIR = $Tools.QtMultimediaPackage; QT_HOST_PATH = $Tools.QtHost
            TagLib_DIR = Join-Path $Tools.TagLibPrefix 'lib/cmake/taglib'
        }
        if ($Testing -eq 'ON') { $paths['RC_MUSIC_CATCH2_SOURCE_DIR'] = $Tools.Catch2Source }
        # Caches configured before the listening room have no package entry yet; the next configure adds it.
        if ($cache.ContainsKey('LibDataChannel_DIR') -and $cache['LibDataChannel_DIR'] -notlike '*-NOTFOUND') {
            $paths['LibDataChannel_DIR'] = Join-Path $Tools.DataChannelPrefix 'lib/cmake/LibDataChannel'
        }
        foreach ($key in $cache.Keys) {
            if ($key -match '^(Qt6\w+)_DIR$' -and $key -ne 'Qt6Multimedia_DIR' -and
                $cache[$key] -notlike '*-NOTFOUND') {
                $package = $Matches[1]
                $paths[$key] = @(Join-Path $Tools.QtPrefix "lib/cmake/$package")
                if ($package.EndsWith('Tools')) { $paths[$key] += Join-Path $Tools.QtHost "lib/cmake/$package" }
            }
        }
        foreach ($entry in $paths.GetEnumerator()) {
            $actual = $cache[$entry.Key]
            $matchesSelected = $false
            if ($actual -is [string] -and [IO.Path]::IsPathFullyQualified($actual)) {
                $actual = [IO.Path]::GetFullPath($actual).TrimEnd('\', '/')
                foreach ($expected in @($entry.Value)) {
                    if ($actual -ieq [IO.Path]::GetFullPath($expected).TrimEnd('\', '/')) { $matchesSelected = $true }
                }
            }
            if (-not $matchesSelected) {
                throw "Unsupported cached toolchain: $($entry.Key). Select a fresh product build directory for another toolchain."
            }
        }
    }
}

function Read-RcMusicDevelopmentTools {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][ValidateSet('format', 'check', 'build', 'test', 'lint', 'validate')][string] $Task
    )
    $data = Import-PowerShellDataFile -LiteralPath $Path
    $allowed = @('CMake', 'CTest', 'Ninja', 'Compiler', 'ClangFormat', 'QmlFormat', 'QtPrefix',
        'QtMultimediaPackage', 'QtHost', 'TagLibPrefix', 'DataChannelPrefix', 'Catch2Source', 'VisualStudioRoot')
    foreach ($key in $data.Keys) {
        if ($key -notin $allowed) { throw "Unknown toolchain field: $key" }
        if ($data[$key] -isnot [string] -or [string]::IsNullOrWhiteSpace($data[$key]) -or
            $data[$key].Contains(';') -or $data[$key] -match '[\x00\r\n]') {
            throw "Invalid toolchain path: $key"
        }
    }
    $required = @()
    if ($Task -in @('format', 'check', 'validate')) { $required += @('ClangFormat', 'QmlFormat') }
    if ($Task -in @('build', 'test', 'lint', 'validate')) {
        $required += @('CMake', 'Ninja', 'Compiler', 'QtPrefix', 'QtMultimediaPackage',
            'QtHost', 'TagLibPrefix', 'DataChannelPrefix', 'VisualStudioRoot')
        if ($Task -ne 'build') { $required += @('CTest', 'Catch2Source') }
    }
    $resolved = @{}
    foreach ($key in $required) {
        if (-not $data.ContainsKey($key)) { throw "Missing toolchain field: $key" }
        $pathValue = $data[$key]
        if (-not [IO.Path]::IsPathFullyQualified($pathValue)) { $pathValue = Join-Path $RepositoryRoot $pathValue }
        $pathValue = [IO.Path]::GetFullPath($pathValue)
        $pathType = if ($key -in @('CMake', 'CTest', 'Ninja', 'Compiler', 'ClangFormat', 'QmlFormat')) { 'Leaf' } else { 'Container' }
        if (-not (Test-Path -LiteralPath $pathValue -PathType $pathType)) { throw "Toolchain path is unavailable: $key" }
        $resolved[$key] = $pathValue
    }
    return $resolved
}

function Get-RcMusicDevelopmentPlan {
    param(
        [Parameter(Mandatory)][ValidateSet('format', 'check', 'build', 'test', 'lint', 'validate')][string] $Task,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $BuildDirectory,
        [Parameter(Mandatory)][Collections.IDictionary] $Tools,
        [ValidateRange(1, 16)][int] $Jobs = 4,
        [switch] $Apply,
        [string] $DiagnosticsDirectory
    )
    if ($Apply -and $Task -ne 'format') { throw 'Apply is only valid for format' }
    $steps = [Collections.Generic.List[object]]::new()
    $pwsh = (Get-Process -Id $PID).Path
    function Add-Step([string] $Name, [string] $File, [string[]] $Arguments) {
        $steps.Add([pscustomobject]@{ Name = $Name; File = $File; Arguments = $Arguments; WorkingDirectory = $RepositoryRoot })
    }
    if ($Task -in @('format', 'check', 'validate')) {
        $arguments = @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/Format-Source.ps1'),
            '-ClangFormat', $Tools.ClangFormat, '-QmlFormat', $Tools.QmlFormat)
        if ($Apply) { $arguments += '-Apply' }
        Add-Step 'format' $pwsh $arguments
    }
    if ($Task -in @('check', 'validate')) {
        Add-Step 'docs' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/validate-docs.ps1'))
        Add-Step 'traceability-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/tests/Traceability.Tests.ps1'))
        Add-Step 'traceability' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/validate-traceability.ps1'))
        Add-Step 'runner-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/tests/DevelopmentCommands.Tests.ps1'))
        Add-Step 'diagnostics-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/tests/DevelopmentDiagnostics.Tests.ps1'))
        Add-Step 'format-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/tests/FormatSource.Tests.ps1'))
        Add-Step 'runtime-plan-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/WindowsRuntimePlan.Tests.ps1'))
        Add-Step 'pe-dependency-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/PeDependencyReport.Tests.ps1'))
        Add-Step 'runtime-recipe-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/RuntimeRecipe.Tests.ps1'))
        Add-Step 'production-boundary-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'scripts/tests/ProductionBoundary.Tests.ps1'))
        Add-Step 'format-matrix-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/FormatMatrix.Tests.ps1'))
        Add-Step 'installer-plan-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/WindowsInstallerPlan.Tests.ps1'))
        Add-Step 'installer-sources-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/InstallerSources.Tests.ps1'))
        Add-Step 'installer-recipe-tests' $pwsh @('-NoProfile', '-File', (Join-Path $RepositoryRoot 'packaging/windows/tests/InstallerRecipe.Tests.ps1'))
        Add-Step 'diff' 'git' @('diff', '--check')
    }
    if ($Task -in @('build', 'test', 'lint', 'validate')) {
        $testing = if ($Task -eq 'build') { 'OFF' } else { 'ON' }
        $configure = @('-S', $RepositoryRoot, '-B', $BuildDirectory, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DRC_MUSIC_BUILD_DESKTOP=ON', "-DBUILD_TESTING=$testing",
            # --profile-root (S13.1) is a manual test build; a reused cache never keeps it ON here.
            '-DRC_MUSIC_TEST_PROFILE_ROOT=OFF',
            "-DCMAKE_MAKE_PROGRAM=$($Tools.Ninja)", "-DCMAKE_CXX_COMPILER=$($Tools.Compiler)",
            "-DCMAKE_PREFIX_PATH=$($Tools.QtPrefix);$($Tools.TagLibPrefix);$($Tools.DataChannelPrefix)",
            "-DQt6Multimedia_DIR=$($Tools.QtMultimediaPackage)", "-DQT_HOST_PATH=$($Tools.QtHost)")
        if ($testing -eq 'ON') { $configure += "-DRC_MUSIC_CATCH2_SOURCE_DIR=$($Tools.Catch2Source)" }
        Add-Step 'configure' $Tools.CMake $configure
        $arguments = @('--build', $BuildDirectory, '--parallel', [string]$Jobs)
        if ($Task -eq 'build') { $arguments += @('--target', 'm0_qt_shell') }
        Add-Step 'build' $Tools.CMake $arguments
        if ($Task -in @('lint', 'validate')) {
            Add-Step 'lint' $Tools.CMake @('--build', $BuildDirectory, '--target', 'all_qmllint')
        }
        if ($Task -in @('test', 'validate')) {
            $arguments = @('--test-dir', $BuildDirectory, '--output-on-failure', '--no-tests=error')
            if ($DiagnosticsDirectory) { $arguments += @('--output-junit', (Join-Path $DiagnosticsDirectory 'ctest.xml')) }
            Add-Step 'test' $Tools.CTest $arguments
        }
    }
    return $steps.ToArray()
}

function Invoke-RcMusicDevelopmentPlan {
    param([Parameter(Mandatory)][object[]] $Plan, [scriptblock] $Invoker, $DiagnosticsContext)
    if ($Plan.Count -eq 0) { throw 'Development plan must not be empty' }
    if (-not $Invoker) {
        $Invoker = {
            param($step)
            $arguments = [string[]]$step.Arguments
            $output = @(& $step.File @arguments 2>&1)
            [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
        }
    }
    foreach ($step in $Plan) {
        Write-Host "[$($step.Name)] $([IO.Path]::GetFileName($step.File))"
        $record = if ($DiagnosticsContext) { Start-RcMusicDiagnosticStep -Context $DiagnosticsContext -Name $step.Name } else { $null }
        $result = $null
        try {
            $junit = $DiagnosticsContext -and $step.Name -ceq 'test' -and $step.Arguments -contains '--output-junit'
            if ($junit) {
                $index = [array]::IndexOf([string[]]$step.Arguments, '--output-junit')
                if ($index + 1 -ge $step.Arguments.Count -or
                    $step.Arguments[$index + 1] -cne (Join-Path $DiagnosticsContext.Directory 'ctest.xml')) { throw 'JUnit must belong to the diagnostic run' }
                Assert-RcMusicDiagnosticJUnit -Context $DiagnosticsContext -Before
            }
            Push-Location -LiteralPath $step.WorkingDirectory
            try { $result = & $Invoker $step } finally { Pop-Location }
            if ($null -eq $result -or $result -is [array] -or
                $null -eq $result.PSObject.Properties['ExitCode'] -or $result.ExitCode -isnot [int] -or
                $null -eq $result.PSObject.Properties['Output']) { throw "Invalid process result for $($step.Name)" }
            foreach ($line in $result.Output) { Write-Host ([string]$line) }
            if ($result.ExitCode -ne 0) { throw "Development step $($step.Name) failed with exit code $($result.ExitCode)" }
            if ($junit) { Assert-RcMusicDiagnosticJUnit -Context $DiagnosticsContext }
        } catch {
            $originalFailure = $_
            if ($record) {
                try {
                    $validResult = $null -ne $result -and $result -isnot [array] -and
                        $null -ne $result.PSObject.Properties['ExitCode'] -and $result.ExitCode -is [int] -and
                        $null -ne $result.PSObject.Properties['Output']
                    $code = if ($validResult) { $result.ExitCode } else { $null }
                    $output = if ($validResult) { @($result.Output) } else { @() }
                    Complete-RcMusicDiagnosticStep -Context $DiagnosticsContext -Step $record -ExitCode $code -Output $output -Failure $originalFailure.Exception.Message
                } catch { Write-Warning "Could not preserve diagnostic step: $($_.Exception.Message)" -WarningAction Continue }
            }
            throw $originalFailure
        }
        if ($record) { Complete-RcMusicDiagnosticStep -Context $DiagnosticsContext -Step $record -ExitCode $result.ExitCode -Output @($result.Output) }
    }
}

Export-ModuleMember -Function Read-RcMusicDevelopmentTools, Assert-RcMusicBuildDirectory,
    Get-RcMusicDevelopmentPlan, Invoke-RcMusicDevelopmentPlan
