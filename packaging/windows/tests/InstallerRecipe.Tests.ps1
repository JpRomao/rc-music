#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testParent = Join-Path $repository 'out/product/test-runs/installer-recipe'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0 }
function Assert-SafeTestRoot {
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe test root'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe test ancestor' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Invoke-Recipe([string[]] $Arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.WorkingDirectory = $testRoot
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoProfile', '-NonInteractive', '-File', $recipe) + $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) { throw 'Could not start isolated PowerShell' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) { throw 'Recipe preflight exceeded 15 seconds' }
        return [pscustomobject]@{ ExitCode = $process.ExitCode
            Stdout = $stdout.GetAwaiter().GetResult(); Stderr = $stderr.GetAwaiter().GetResult() }
    } finally {
        try {
            if ($started -and -not $process.HasExited) {
                $process.Kill($true)
                if (-not $process.WaitForExit(5000)) { throw 'Owned PowerShell survived cleanup' }
            }
        } finally { $process.Dispose() }
    }
}
Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    $export = Join-Path $testRoot 'source exportada 日本'
    $recipeRoot = Join-Path $export 'packaging/windows'
    $app = Join-Path $export 'out/product/synthetic-runtime/app'
    $null = New-Item -ItemType Directory -Path $recipeRoot, $app
    # No corpus, catalogue, Git checkout or experiment is needed for OFF preflight.
    foreach ($name in @('New-RcMusicInstaller.ps1', 'RcMusicInstallerPlan.psm1', 'RcMusicRuntimePlan.psm1',
            'RcMusicFormatMatrix.psm1', 'RcMusicInstaller.nsi', 'RcMusicInstallerFiles.nsh',
            'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json',
            'RcMusicInstallerComponent.psm1', 'RcMusicDownloadComponentPe.psm1')) {
        Copy-Item -LiteralPath (Join-Path $repository ('packaging/windows/' + $name)) -Destination $recipeRoot
    }
    $recipe = Join-Path $recipeRoot 'New-RcMusicInstaller.ps1'
    Test-Case 'builder writes the generated activation marker as exact LF bytes without overwriting files' {
        $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($recipe, [ref]$null, [ref]$parseErrors)
        $writer = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -ceq 'Write-RcMusicInstallerGeneratedFiles'
        }, $true)
        Assert-True ($null -ne $writer) 'Generated-file writer missing'
        $generatedRoot = Join-Path $testRoot 'generated'
        $null = [IO.Directory]::CreateDirectory($generatedRoot)
        $content = "RCMusicDownloadTools-v1`n"
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($content))).ToLowerInvariant()
        $plan = [pscustomobject]@{ output = $generatedRoot; generated_files = @(
            [pscustomobject]@{ relative_path = 'download-component-marker.bin'; content = $content; sha256 = $hash }
        ) }
        $written = @(& $writer.Body.GetScriptBlock() -Plan $plan)
        $path = Join-Path $generatedRoot 'download-component-marker.bin'
        Assert-True ([IO.File]::Exists($path)) 'Marker was not materialized'
        Assert-True ([IO.File]::ReadAllBytes($path).Length -eq 24) 'Marker must contain 24 bytes'
        Assert-True ([IO.File]::ReadAllText($path) -ceq $content) 'Marker LF changed'
        Assert-True ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ceq $hash) 'Marker hash mismatch'
        Assert-True ($written.Count -eq 1 -and $written[0].sha256 -ceq $hash) 'Generated inventory missing'
        $rejected = $false
        try { & $writer.Body.GetScriptBlock() -Plan $plan | Out-Null } catch { $rejected = $true }
        Assert-True $rejected 'Existing output was overwritten'
        Assert-True ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ceq $hash) 'Existing marker changed'
    }
    Test-Case 'builder rejects unexpected generated names and content before writing' {
        $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($recipe, [ref]$null, [ref]$parseErrors)
        $writer = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -ceq 'Write-RcMusicInstallerGeneratedFiles'
        }, $true)
        $generatedRoot = Join-Path $testRoot 'invalid-generated'
        $null = [IO.Directory]::CreateDirectory($generatedRoot)
        foreach ($bad in @(
            [pscustomobject]@{ relative_path = '../unexpected.txt'; content = "RCMusicDownloadTools-v1`n"; sha256 = ('a' * 64) },
            [pscustomobject]@{ relative_path = 'download-component-marker.bin'; content = "RCMusicDownloadTools-v1`r`n"; sha256 = ('a' * 64) },
            [pscustomobject]@{ relative_path = 'download-component-marker.bin'; content = "RCMusicDownloadTools-v1`n"; sha256 = ('a' * 64) }
        )) {
            $plan = [pscustomobject]@{ output = $generatedRoot; generated_files = @($bad) }
            $rejected = $false
            try { & $writer.Body.GetScriptBlock() -Plan $plan | Out-Null } catch { $rejected = $true }
            Assert-True $rejected 'Invalid generated marker was accepted'
            Assert-True (@(Get-ChildItem -LiteralPath $generatedRoot -Force).Count -eq 0) 'Invalid marker wrote a file'
        }
    }
    $entries = @(foreach ($name in @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')) {
        $path = Join-Path $app $name
        [IO.File]::WriteAllText($path, 'Synthetic planning bytes only')
        [pscustomobject]@{ relative_path = $name; source = 'not a runtime origin'
            size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $manifest = @{ schema_version = 1; name = 'RC Music'; architecture = 'x64'; build_testing = $false
        recipe_dirty = $false; source_commit = ('1' * 40); distribution = 'private-local-development-only'; files = $entries }
    [IO.File]::WriteAllText((Join-Path ([IO.Path]::GetDirectoryName($app)) 'runtime-manifest.json'),
        ($manifest | ConvertTo-Json -Depth 6))
    $compiler = Join-Path $export 'untrusted-compiler.txt'
    [IO.File]::WriteAllText($compiler, 'This is never executed: the pinned hash must reject it')
    $output = Join-Path $export 'out/product/rejected-installer'
    Test-Case 'exported OFF recipe rejects untrusted compiler bytes before output or execution' {
        $result = Invoke-Recipe @('-RuntimeDirectory', $app, '-OutputDirectory', $output, '-MakeNsis', $compiler)
        Assert-True ($result.ExitCode -eq 1) 'Preflight did not fail'
        Assert-True ($result.Stdout -ceq '') 'Preflight emitted unexpected stdout'
        Assert-True ($result.Stderr -match 'NSIS compiler does not match the recorded toolchain') "Wrong rejection: $($result.Stderr)"
        Assert-True (-not (Test-Path -LiteralPath $output)) 'Rejected compiler created output'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $export 'experiments'))) 'Export acquired an experiment dependency'
    }
    Test-Case 'product recipe requires an explicit compiler without falling back to historical toolchains' {
        $result = Invoke-Recipe @('-RuntimeDirectory', $app, '-OutputDirectory', $output)
        Assert-True ($result.ExitCode -eq 1 -and $result.Stderr -match 'MakeNsis') 'Missing explicit compiler was not rejected'
        Assert-True (-not (Test-Path -LiteralPath $output)) 'Missing compiler created output'
    }
    Test-Case 'Complete request refuses a Lite manifest before inspecting or executing the compiler' {
        $result = Invoke-Recipe @('-RuntimeDirectory', $app, '-OutputDirectory', $output, '-MakeNsis', $compiler, '-Edition', 'CompleteExperimental')
        Assert-True ($result.ExitCode -eq 1 -and $result.Stdout -ceq '') 'Complete preflight did not fail cleanly'
        Assert-True ($result.Stderr -match '(?i)edition') "Wrong Complete rejection: $($result.Stderr)"
        Assert-True ($result.Stderr -notmatch 'NSIS compiler does not match') 'Edition was ignored until compiler verification'
        Assert-True (-not (Test-Path -LiteralPath $output)) 'Mismatched edition created output'
    }
    Test-Case 'builder and planner share the real recipe inventory before and after compilation' {
        $parseErrors = $null
        $ast = [Management.Automation.Language.Parser]::ParseFile($recipe, [ref]$null, [ref]$parseErrors)
        Assert-True ($parseErrors.Count -eq 0) 'Invalid PowerShell recipe'
        $calls = @($ast.FindAll({ param($node)
            $node -is [Management.Automation.Language.CommandAst] -and
                $node.GetCommandName() -ceq 'Get-RcMusicInstallerRecipeHashes'
        }, $true))
        Assert-True ($calls.Count -eq 2) 'Recipe inventory is not revalidated through the common API'
        $oldHelper = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-InstallerSources'
        }, $true)
        Assert-True ($null -eq $oldHelper) 'Builder retains a second recipe inventory'
    }
} finally {
    Assert-SafeTestRoot
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "Installer recipe: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
