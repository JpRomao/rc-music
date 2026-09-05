#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $RuntimeDirectory,
    [Parameter(Mandatory)][string] $OutputDirectory,
    [string] $MakeNsis,
    [string] $FormatMatrix
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required to build the installer' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
Import-Module (Join-Path $PSScriptRoot 'WindowsInstallerPlan.psm1') -Force
$plan = New-RcMusicInstallerPlan -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory -FormatMatrix $FormatMatrix
if (-not $MakeNsis) { $MakeNsis = Join-Path $repository 'out/m0/toolchains/nsis-3.12/makensis.exe' }
$MakeNsis = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'nsis-toolchain.json') -Raw | ConvertFrom-Json
$compilerHash = (Get-FileHash -LiteralPath $MakeNsis -Algorithm SHA256).Hash.ToLowerInvariant()
if ($compilerHash -cne $toolchain.compilerSha256) { throw 'NSIS compiler does not match the recorded toolchain' }

function Get-InstallerSources {
    $names = @('New-RcMusicInstaller.ps1', 'WindowsInstallerPlan.psm1', 'WindowsRuntimePlan.psm1',
        'RcMusicInstaller.nsi', 'RcMusicInstallerFiles.nsh', 'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json')
    return @($names | ForEach-Object {
        [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
$sourceFiles = Get-InstallerSources
$sourceBefore = $sourceFiles | ConvertTo-Json -Compress
if ($sourceBefore -cne ($plan.recipe_hashes | ConvertTo-Json -Compress)) { throw 'Installer recipe changed after planning' }
$gitCommit = @(& git --no-optional-locks -C $repository rev-parse --verify HEAD 2>&1)
if ($LASTEXITCODE -ne 0 -or $gitCommit.Count -ne 1) { throw 'Cannot record installer recipe commit' }
$recipeStatus = @(& git --no-optional-locks -C $repository status --porcelain=v1 --untracked-files=all -- experiments/m0/candidates/qt/windows-installer 2>&1)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record installer recipe status' }

# A failed build may leave this fresh directory without a success receipt.
# Never overwrite or automatically delete a previous build or installer.
$null = New-Item -ItemType Directory -Path $plan.output
$configPath = Join-Path $plan.output 'installer-config.nsh'
$installerPath = Join-Path $plan.output 'RCMusic-Setup.exe'
[IO.File]::WriteAllText($configPath, $plan.config, [Text.UTF8Encoding]::new($false))
$configHash = (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant()
$start = [Diagnostics.ProcessStartInfo]::new($MakeNsis)
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.WorkingDirectory = $PSScriptRoot
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
foreach ($argument in @('/INPUTCHARSET', 'UTF8', '/V2', '/WX',
        ('/DRCMUSIC_CONFIG=' + (ConvertTo-RcMusicNsisLiteral $configPath)), (Join-Path $PSScriptRoot 'RcMusicInstaller.nsi'))) {
    $start.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
$started = $false
try {
    $started = $process.Start()
    if (-not $started) { throw 'Could not start NSIS compiler' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) { throw 'Installer compilation exceeded 60 seconds' }
    $output = $stdout.GetAwaiter().GetResult()
    $errorText = $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0 -or -not [string]::IsNullOrWhiteSpace($errorText)) {
        throw "NSIS compilation failed: $output`n$errorText"
    }
} finally {
    try {
        if ($started -and -not $process.HasExited) {
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { throw 'Owned NSIS compiler survived cleanup' }
        }
    } finally { $process.Dispose() }
}
foreach ($entry in $plan.files) {
    if ((Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash.ToLowerInvariant() -cne $entry.sha256) {
        throw 'Runtime input changed while compiling the installer'
    }
}
if ((Get-FileHash -LiteralPath $plan.runtime_manifest -Algorithm SHA256).Hash.ToLowerInvariant() -cne $plan.runtime_manifest_sha256 -or
    (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $configHash -or
    ((Get-InstallerSources | ConvertTo-Json -Compress) -cne $sourceBefore)) {
    throw 'Installer recipe, generated config or runtime manifest changed during compilation'
}
if ($null -ne $plan.format_matrix -and
    (Get-FileHash -LiteralPath $plan.format_matrix.path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $plan.format_matrix.sha256) {
    throw 'Format matrix changed while compiling the installer'
}
$receiptPath = Join-Path $plan.output 'installer-receipt.json'
$receipt = [ordered]@{
    schema_version = 1; name = 'RC Music'; distribution = 'private-local-development-only'
    associations_enabled = $plan.associations_enabled; payload_id = $plan.payload_id
    format_signature = $plan.format_signature; formats = $plan.formats; format_matrix = $plan.format_matrix
    runtime_source_commit = $plan.source_commit; runtime_manifest_sha256 = $plan.runtime_manifest_sha256
    recipe_commit = [string] $gitCommit[0]; recipe_dirty = ($recipeStatus.Count -ne 0)
    sources = $sourceFiles; compiler_sha256 = $compilerHash; config_sha256 = $configHash
    installer_sha256 = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    files = $plan.files
    prerequisites = 'Windows x64 and compatible Microsoft Visual C++ Redistributable; no runtime PowerShell dependency'
}
[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
[pscustomobject]@{ installer = $installerPath; receipt = $receiptPath; payload_id = $plan.payload_id }
