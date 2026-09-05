#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'WindowsRuntimePlan.psm1') -Force

function ConvertTo-RcMusicNsisLiteral([string] $Value, [switch] $Runtime,
    [ValidateRange(1, 2)][int] $Passes = 1) {
    if ($Value -match '["\x00-\x1f]') { throw 'Quotes and control characters are not valid installer path inputs' }
    # $$ is a runtime escape, not a preprocessor/File-source escape. Defer the
    # dollar through each known preprocessor pass, then escape runtime strings.
    $dollar = '${U+24}' + ('{U+24}' * ($Passes - 1))
    if ($Runtime) { $dollar += $dollar }
    return $Value.Replace('$', $dollar)
}

function Get-RcMusicTextSha256([string] $Text) {
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text))).ToLowerInvariant()
}

function Get-RcMusicInstallerIdentity {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $InventoryJson, [Parameter(Mandatory)][object[]] $RecipeHashes)
    $recipe = @($RecipeHashes | Sort-Object -Property path -CaseSensitive | ForEach-Object {
        if ($_.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Invalid installer recipe hash' }
        [ordered]@{ path = $_.path; sha256 = $_.sha256 }
    })
    return Get-RcMusicTextSha256 ([ordered]@{ schema_version = 1; inventory = $InventoryJson; recipe = $recipe } |
        ConvertTo-Json -Depth 5 -Compress)
}

function New-RcMusicInstallerPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RuntimeDirectory,
        [Parameter(Mandatory)][string] $OutputDirectory)
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../..'))
    $output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
    $outputRoot = Join-Path $repository 'out/m0/'
    if (-not $output.StartsWith($outputRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Test-Path -LiteralPath $output)) { throw 'Installer output must be a fresh directory below out/m0' }
    $runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).ProviderPath
    $manifestPath = Join-Path ([IO.Path]::GetDirectoryName($runtime.TrimEnd('\', '/'))) 'runtime-manifest.json'
    # Validate the manifest path and its ancestors before reading it.
    $null = New-RcMusicRuntimePlan -Files @(@{ source = $manifestPath; relative_path = 'runtime-manifest.json' }) `
        -AllowedSourceRoots @([IO.Path]::GetDirectoryName($manifestPath)) -Destination $output
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or $manifest.name -cne 'RC Music' -or
        $manifest.architecture -cne 'x64' -or $manifest.build_testing -isnot [bool] -or $manifest.build_testing -or
        $manifest.recipe_dirty -isnot [bool] -or $manifest.recipe_dirty -or
        $manifest.source_commit -cnotmatch '^[0-9a-f]{40}$' -or
        $manifest.distribution -cne 'private-local-development-only') {
        throw 'Installer requires a clean private production-mode x64 runtime manifest'
    }
    $mappings = foreach ($entry in $manifest.files) {
        if ($entry.relative_path -isnot [string] -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $entry.relative_path -match '^(?i:Uninstall\.exe|\.rcmusic-install\.ini)(?:[\\/]|$)') {
            throw 'Invalid or installer-reserved runtime entry'
        }
        @{ source = (Join-Path $runtime $entry.relative_path); relative_path = $entry.relative_path }
    }
    $verified = New-RcMusicRuntimePlan -Files @($mappings) -AllowedSourceRoots @($runtime) -Destination $output
    for ($index = 0; $index -lt $verified.files.Count; $index++) {
        $actual = $verified.files[$index]
        $expected = $manifest.files[$index]
        if ($actual.sha256 -cne $expected.sha256 -or $actual.size_bytes -cne $expected.size_bytes) {
            throw "Runtime bytes no longer match the manifest: $($actual.relative_path)"
        }
    }
    foreach ($required in @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')) {
        if ($verified.files.relative_path -cnotcontains $required) { throw "Missing required runtime file: $required" }
    }
    $files = @($verified.files | Sort-Object -Property relative_path -CaseSensitive -Culture '')
    $identity = @($files | ForEach-Object {
        [ordered]@{ path = $_.relative_path; size = $_.size_bytes; sha256 = $_.sha256 }
    }) | ConvertTo-Json -Depth 4 -Compress
    $recipeHashes = @(@('New-RcMusicInstaller.ps1', 'WindowsInstallerPlan.psm1', 'WindowsRuntimePlan.psm1',
            'RcMusicInstaller.nsi', 'RcMusicInstallerFiles.nsh', 'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json') |
        ForEach-Object {
            [pscustomobject]@{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant() }
        })
    $payloadId = Get-RcMusicInstallerIdentity -InventoryJson $identity -RecipeHashes $recipeHashes
    $formatSignature = Get-RcMusicTextSha256 '[]'
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('; Generated from verified runtime bytes. Private local development only.')
    $lines.Add('!define RCMUSIC_OUTPUT "' + (ConvertTo-RcMusicNsisLiteral (Join-Path $output 'RCMusic-Setup.exe') -Passes 2) + '"')
    $lines.Add('!define RCMUSIC_INSTALL_ROOT "$LOCALAPPDATA\Programs\RC Music"')
    $lines.Add('!define RCMUSIC_REGISTRY_BASE "Software"')
    $lines.Add('!define RCMUSIC_PAYLOAD_ID "' + $payloadId + '"')
    $lines.Add('!define RCMUSIC_FORMAT_SIGNATURE "' + $formatSignature + '"')
    # No format is inferred from extensions or from the experimental runtime
    # smoke. The approved matrix will be a separate explicit build input.
    $lines.Add('!define RCMUSIC_ASSOCIATIONS_ENABLED 0')
    $lines.Add('!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK')
    $lines.Add('!macroend')
    for ($index = 0; $index -lt $files.Count; $index++) {
        $token = 'RCMUSIC_PATH_{0:D4}' -f $index
        $lines.Add('!define ' + $token + ' "' + (ConvertTo-RcMusicNsisLiteral $files[$index].relative_path.Replace('/', '\') -Runtime -Passes 2) + '"')
    }
    $lines.Add('!macro RCMUSIC_PAYLOAD_EXTRACT')
    foreach ($entry in $files) {
        $relative = $entry.relative_path.Replace('/', '\')
        $parent = [IO.Path]::GetDirectoryName($relative)
        $lines.Add('    ClearErrors')
        $lines.Add('    SetOutPath "$INSTDIR' + $(if ($parent) { '\' + (ConvertTo-RcMusicNsisLiteral $parent -Runtime) }) + '"')
        $lines.Add('    IfErrors rcmusic_install_failed')
        $lines.Add('    ClearErrors')
        $lines.Add('    File "/oname=' + (ConvertTo-RcMusicNsisLiteral ([IO.Path]::GetFileName($relative)) -Runtime) + '" "' +
            (ConvertTo-RcMusicNsisLiteral $entry.source) + '"')
        $lines.Add('    IfErrors rcmusic_install_failed')
    }
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_PAYLOAD_FILES CALLBACK')
    $directories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    for ($index = 0; $index -lt $files.Count; $index++) {
        $entry = $files[$index]
        $relative = $entry.relative_path.Replace('/', '\')
        $token = 'RCMUSIC_PATH_{0:D4}' -f $index
        # Pass tokens through nested callbacks; expand their data only at the
        # final NSIS instruction so every literal has exactly two passes.
        $lines.Add('    !insertmacro ${CALLBACK} "' + $token + '" "' + $entry.sha256 + '"')
        $parent = [IO.Path]::GetDirectoryName($relative)
        while ($parent) { $null = $directories.Add($parent); $parent = [IO.Path]::GetDirectoryName($parent) }
    }
    $lines.Add('!macroend')
    $sortedDirectories = @($directories | Sort-Object -Property @{ Expression = { $_.Split('\').Count }; Descending = $true },
        @{ Expression = { $_ }; Descending = $false })
    for ($index = 0; $index -lt $sortedDirectories.Count; $index++) {
        $token = 'RCMUSIC_DIR_{0:D4}' -f $index
        $lines.Add('!define ' + $token + ' "' + (ConvertTo-RcMusicNsisLiteral $sortedDirectories[$index] -Runtime -Passes 2) + '"')
    }
    $lines.Add('!macro RCMUSIC_PAYLOAD_DIRECTORIES CALLBACK')
    for ($index = 0; $index -lt $sortedDirectories.Count; $index++) {
        $token = 'RCMUSIC_DIR_{0:D4}' -f $index
        $lines.Add('    !insertmacro ${CALLBACK} "' + $token + '"')
    }
    $lines.Add('!macroend')
    return [pscustomobject]@{ output = $output; runtime = $runtime; runtime_manifest = $manifestPath
        runtime_manifest_sha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        source_commit = $manifest.source_commit; payload_id = $payloadId; files = $files; recipe_hashes = $recipeHashes
        config = ($lines -join "`n") + "`n" }
}

Export-ModuleMember -Function New-RcMusicInstallerPlan, Get-RcMusicInstallerIdentity, ConvertTo-RcMusicNsisLiteral
