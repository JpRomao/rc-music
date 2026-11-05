#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $RuntimeDirectory,
    [Parameter(Mandatory)][string] $OutputDirectory,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string] $MakeNsis,
    [string] $FormatMatrix,
    [switch] $LegacyBaseline,
    [ValidateSet('Lite', 'CompleteExperimental')] [string] $Edition = 'Lite'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Windows x64 PowerShell is required to build the installer' }
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Import-Module (Join-Path $PSScriptRoot 'RcMusicInstallerPlan.psm1') -Force
function Write-RcMusicInstallerGeneratedFiles([object] $Plan) {
    if (@($Plan.generated_files).Count -eq 0) { return @() }
    if (@($Plan.generated_files).Count -ne 1) { throw 'Unexpected generated installer inventory' }
    $entry = $Plan.generated_files[0]
    $content = "RCMusicDownloadTools-v1`n"
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($content)
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($entry.relative_path -cne 'download-component-marker.bin' -or $entry.content -cne $content -or
        $entry.sha256 -cne $hash) { throw 'Invalid generated activation marker' }
    $path = Join-Path $Plan.output $entry.relative_path
    $stream = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes); $stream.Flush($true) } finally { $stream.Dispose() }
    if ((Get-Item -LiteralPath $path).Length -ne 24 -or
        (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $hash) { throw 'Generated activation marker changed' }
    return [pscustomobject]@{ relative_path = $entry.relative_path; source = $path; size_bytes = 24; sha256 = $hash }
}
$plan = New-RcMusicInstallerPlan -RuntimeDirectory $RuntimeDirectory -OutputDirectory $OutputDirectory `
    -FormatMatrix $FormatMatrix -LegacyBaseline:$LegacyBaseline -Edition $Edition
$MakeNsis = (Resolve-Path -LiteralPath $MakeNsis).ProviderPath
$toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'nsis-toolchain.json') -Raw | ConvertFrom-Json
$compilerHash = (Get-FileHash -LiteralPath $MakeNsis -Algorithm SHA256).Hash.ToLowerInvariant()
if ($compilerHash -cne $toolchain.compilerSha256) { throw 'NSIS compiler does not match the recorded toolchain' }

$sourceFiles = @(Get-RcMusicInstallerRecipeHashes)
$sourceBefore = $sourceFiles | ConvertTo-Json -Compress
if ($sourceBefore -cne ($plan.recipe_hashes | ConvertTo-Json -Compress)) { throw 'Installer recipe changed after planning' }
$gitCommit = @(& git --no-optional-locks -C $repository rev-parse --verify HEAD 2>&1)
if ($LASTEXITCODE -ne 0 -or $gitCommit.Count -ne 1) { throw 'Cannot record installer recipe commit' }
$recipeStatus = @(& git --no-optional-locks -C $repository status --porcelain=v1 --untracked-files=all -- `
    packaging/windows tests/audio/corpus/catalog.v1.json 2>&1)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record installer recipe status' }

# A failed build may leave this fresh directory without a success receipt.
# Never overwrite or automatically delete a previous build or installer.
$null = New-Item -ItemType Directory -Path $plan.output
$generatedFiles = @(Write-RcMusicInstallerGeneratedFiles -Plan $plan)
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
foreach ($entry in $generatedFiles) {
    if ((Get-Item -LiteralPath $entry.source).Length -ne $entry.size_bytes -or
        (Get-FileHash -LiteralPath $entry.source).Hash.ToLowerInvariant() -cne $entry.sha256) {
        throw 'Generated installer input changed during compilation'
    }
}
if ((Get-FileHash -LiteralPath $plan.runtime_manifest -Algorithm SHA256).Hash.ToLowerInvariant() -cne $plan.runtime_manifest_sha256 -or
    (Get-FileHash -LiteralPath $configPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $configHash -or
    ((Get-RcMusicInstallerRecipeHashes | ConvertTo-Json -Compress) -cne $sourceBefore)) {
    throw 'Installer recipe, generated config or runtime manifest changed during compilation'
}
if ($null -ne $plan.format_matrix -and
    (Get-FileHash -LiteralPath $plan.format_matrix.path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $plan.format_matrix.sha256) {
    throw 'Format matrix changed while compiling the installer'
}
$receiptPath = Join-Path $plan.output 'installer-receipt.json'
$receipt = [ordered]@{
    schema_version = 1; name = 'RC Music'; distribution = 'private-local-development-only'
    edition = $plan.edition; browser_runtime_requirement = $plan.browser_runtime_requirement
    associations_enabled = $plan.associations_enabled; payload_id = $plan.payload_id
    format_signature = $plan.format_signature; formats = $plan.formats; format_matrix = $plan.format_matrix
    runtime_source_commit = $plan.source_commit; runtime_manifest_sha256 = $plan.runtime_manifest_sha256
    recipe_commit = [string] $gitCommit[0]; recipe_dirty = ($recipeStatus.Count -ne 0)
    recipe_path_base = 'repository'; sources = $sourceFiles
    compiler_sha256 = $compilerHash; config_sha256 = $configHash
    installer_sha256 = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    files = $plan.files
    payload_ids = $plan.payload_ids; download_component = $plan.download_component; generated_files = $generatedFiles
    prerequisites = 'Windows x64 and compatible Microsoft Visual C++ Redistributable; no runtime PowerShell dependency' +
        $(if ($plan.edition -ceq 'CompleteExperimental') { '; internal navigation requires existing WebView2 Evergreen Runtime x64; runtime is not bundled or installed' })
}
[IO.File]::WriteAllText($receiptPath, ($receipt | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
[pscustomobject]@{ installer = $installerPath; receipt = $receiptPath; payload_id = $plan.payload_id }
