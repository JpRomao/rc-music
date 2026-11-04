#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required.' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testRoot = Join-Path $repository ('out/product/runtime-recipe-' + [guid]::NewGuid().ToString('N'))
$sourceNames = @('New-RcMusicLocalRuntime.ps1', 'RcMusicRuntimePlan.psm1', 'PeDependencyReport.psm1',
    'qt.conf', 'LOCAL-DEVELOPMENT.txt')
$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', found '$Actual'." }
}
function Assert-SafeTestRoot {
    $parent = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -cnotmatch '\Aruntime-recipe-[0-9a-f]{32}\z') { throw 'Unsafe test root.' }
    $ancestor = $resolved
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe reparse ancestor.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Read-Ast([string] $Path) {
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) { throw "Invalid PowerShell: $Path" }
    return $ast
}
function Read-RecipeFiles([string] $Edition = 'Lite') {
    $ast = Read-Ast $exportedRecipe
    $functions = @($ast.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-RuntimeRecipeFiles'
    }, $true))
    Assert-Equal $functions.Count 1
    # Execute only this inventory function, never the script body. The AST keeps
    # the exported file identity so PSScriptRoot resolves beside the five copies.
    return @(& $functions[0].Body.GetScriptBlock() -Edition $Edition)
}
function Invoke-RecipeChild([string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $foreignDirectory
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile', '-NonInteractive', '-File', $exportedRecipe) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'PowerShell recipe child could not start.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { throw 'PowerShell recipe child exceeded 15 seconds.' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode
            Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned PowerShell child survived cleanup.' }
            }
        } finally { $process.Dispose() }
    }
}
Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root already exists.' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    $exportRoot = Join-Path $testRoot 'source exportado 日本'
    $exportDirectory = Join-Path $exportRoot 'packaging/windows'
    $buildDirectory = Join-Path $exportRoot 'out/product/build inválido'
    $foreignDirectory = Join-Path $testRoot 'cwd alheio'
    $null = New-Item -ItemType Directory -Path $exportDirectory, $buildDirectory, $foreignDirectory
    foreach ($name in $sourceNames) {
        $original = Join-Path $repository ('packaging/windows/' + $name)
        $copy = Join-Path $exportDirectory $name
        Copy-Item -LiteralPath $original -Destination $copy
        Assert-Equal ((Get-FileHash -LiteralPath $copy).Hash) ((Get-FileHash -LiteralPath $original).Hash)
    }
    Copy-Item -LiteralPath (Join-Path $repository 'packaging/windows/WEBVIEW2-DEVELOPMENT.txt') -Destination $exportDirectory
    $exportedRecipe = Join-Path $exportDirectory 'New-RcMusicLocalRuntime.ps1'
    $cache = "CMAKE_BUILD_TYPE:STRING=Debug`nBUILD_TESTING:BOOL=OFF`nCMAKE_HOME_DIRECTORY:INTERNAL=$exportRoot`nRC_MUSIC_BUILD_DESKTOP:BOOL=ON`n"
    [IO.File]::WriteAllText((Join-Path $buildDirectory 'CMakeCache.txt'), $cache, [Text.UTF8Encoding]::new($false))
    Test-Case 'all three exported PowerShell files parse without historical files' {
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
        foreach ($name in $sourceNames | Where-Object { $_ -match '\.ps(?:1|m1)$' }) {
            $null = Read-Ast (Join-Path $exportDirectory $name)
        }
    }
    Test-Case 'the real exported recipe rejects Debug from a foreign cwd before tools or output creation' {
        $result = Invoke-RecipeChild @('-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-runtime')
        Assert-Equal $result.ExitCode 1
        Assert-Equal $result.Stdout ''
        if ($result.Stderr -notmatch 'Unsupported desktop build configuration:\s*CMAKE_BUILD_TYPE') {
            throw "Wrong preflight rejection: $($result.Stderr)"
        }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-runtime')) $false
        Assert-Equal (Test-Path -LiteralPath (Join-Path $foreignDirectory 'out')) $false
    }
    Test-Case 'the product requires an explicit build directory without starting a build' {
        $result = Invoke-RecipeChild @('-OutputDirectory', 'out/product/missing-build')
        Assert-Equal $result.ExitCode 1
        if ($result.Stderr -notmatch 'BuildDirectory') { throw 'Missing build parameter was not identified.' }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/missing-build')) $false
    }
    Test-Case 'the recipe inventories exactly five repository-relative paths and their exported bytes' {
        $files = @(Read-RecipeFiles)
        Assert-Equal $files.Count 5
        Assert-Equal (($files.path | Sort-Object -CaseSensitive) -join '|') (($sourceNames | ForEach-Object { 'packaging/windows/' + $_ } | Sort-Object -CaseSensitive) -join '|')
        foreach ($entry in $files) {
            if ($entry.sha256 -cnotmatch '\A[0-9a-f]{64}\z') { throw 'Invalid recipe SHA256.' }
            Assert-Equal $entry.sha256 ((Get-FileHash -LiteralPath (Join-Path $exportRoot $entry.path)).Hash.ToLowerInvariant())
        }
    }
    Test-Case 'Complete explicitly reaches its SDK preflight before any tool, output or Git operation' {
        $cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
        $previousCache = [IO.File]::ReadAllText($cacheFile)
        try {
            $completeCache = "CMAKE_BUILD_TYPE:STRING=Release`nBUILD_TESTING:BOOL=OFF`nCMAKE_HOME_DIRECTORY:INTERNAL=$exportRoot`nRC_MUSIC_BUILD_DESKTOP:BOOL=ON`nRC_MUSIC_BUILD_EMBEDDED_BROWSER:BOOL=ON`n"
            foreach ($option in @('BROWSER_INTEGRATION', 'DOWNLOADS', 'DOWNLOAD_NETWORK', 'DOWNLOAD_PERSISTENCE', 'NATIVE_MESSAGING')) {
                $completeCache += "RC_MUSIC_BUILD_$($option):BOOL=OFF`n"
            }
            [IO.File]::WriteAllText($cacheFile, $completeCache, [Text.UTF8Encoding]::new($false))
            $result = Invoke-RecipeChild @('-Edition', 'CompleteExperimental', '-BuildDirectory', $buildDirectory, '-OutputDirectory', 'out/product/rejected-complete')
            Assert-Equal $result.ExitCode 1
            Assert-Equal $result.Stdout ''
            if ($result.Stderr -notmatch 'RC_MUSIC_WEBVIEW2_SDK_ROOT') { throw "Wrong Complete preflight: $($result.Stderr)" }
            Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'out/product/rejected-complete')) $false
        } finally { [IO.File]::WriteAllText($cacheFile, $previousCache, [Text.UTF8Encoding]::new($false)) }
    }
    Test-Case 'Complete recipe provenance additionally binds its own exported prerequisite notice' {
        $files = @(Read-RecipeFiles -Edition CompleteExperimental)
        Assert-Equal $files.Count 6
        $extra = @($files | Where-Object path -CEQ 'packaging/windows/WEBVIEW2-DEVELOPMENT.txt')
        Assert-Equal $extra.Count 1
        Assert-Equal $extra[0].sha256 ((Get-FileHash -LiteralPath (Join-Path $exportDirectory 'WEBVIEW2-DEVELOPMENT.txt')).Hash.ToLowerInvariant())
        Assert-Equal @(Read-RecipeFiles -Edition Lite).Count 5
    }
    Test-Case 'changing only the exported parser changes only that recorded hash' {
        $before = @(Read-RecipeFiles)
        $parserCopy = Join-Path $exportDirectory 'PeDependencyReport.psm1'
        $parserOriginal = Join-Path $repository 'packaging/windows/PeDependencyReport.psm1'
        $originalHash = (Get-FileHash -LiteralPath $parserOriginal).Hash
        [IO.File]::AppendAllText($parserCopy, "`n# Synthetic exported-copy mutation.`n", [Text.UTF8Encoding]::new($false))
        $after = @(Read-RecipeFiles)
        $changed = @($after | Where-Object {
            $entry = $_
            $previous = @($before | Where-Object { $_.path -ceq $entry.path })
            $previous.Count -ne 1 -or $previous[0].sha256 -cne $entry.sha256
        })
        Assert-Equal $changed.Count 1
        Assert-Equal $changed[0].path 'packaging/windows/PeDependencyReport.psm1'
        Assert-Equal $changed[0].sha256 ((Get-FileHash -LiteralPath $parserCopy).Hash.ToLowerInvariant())
        Assert-Equal ((Get-FileHash -LiteralPath $parserOriginal).Hash) $originalHash
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "Runtime recipe: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
