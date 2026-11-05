#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'RcMusicFormatMatrix.psm1') -Force

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

function Get-RcMusicInstallerRecipeHashes {
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    return @(@('New-RcMusicInstaller.ps1', 'RcMusicInstallerPlan.psm1', 'RcMusicRuntimePlan.psm1',
            'RcMusicFormatMatrix.psm1', 'RcMusicInstaller.nsi', 'RcMusicInstallerFiles.nsh',
            'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json') | ForEach-Object {
        $path = 'packaging/windows/' + $_
        [pscustomobject]@{ path = $path
            sha256 = (Get-FileHash -LiteralPath (Join-Path $repository $path) -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}

function New-RcMusicInstallerPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RuntimeDirectory,
        [Parameter(Mandatory)][string] $OutputDirectory, [string] $FormatMatrix, [switch] $LegacyBaseline,
        [ValidateSet('Lite', 'CompleteExperimental')][string] $Edition = 'Lite')
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $output = [IO.Path]::GetFullPath($OutputDirectory, $repository)
    $outputRoots = @('out/product/', 'out/m0/') | ForEach-Object { Join-Path $repository $_ }
    $contained = @($outputRoots | Where-Object { $output.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count -gt 0
    if (-not $contained -or (Test-Path -LiteralPath $output)) {
        throw 'Installer output must be a fresh directory below out/product or out/m0'
    }
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
    # This installer still extracts one unconditional inventory. A declared
    # optional component must wait for an explicit selection/ownership flow;
    # even null or false metadata cannot silently become a base installation.
    if ($null -ne $manifest.PSObject.Properties['download_component']) {
        throw 'Installer v1 does not support an optional download component'
    }
    $canonicalEdition = if ($Edition -ieq 'CompleteExperimental') { 'CompleteExperimental' } else { 'Lite' }
    $editionProperty = $manifest.PSObject.Properties['edition']
    $manifestEdition = 'Lite'
    if ($null -ne $editionProperty) {
        if ($editionProperty.Value -isnot [string] -or
            ($editionProperty.Value -cne 'Lite' -and $editionProperty.Value -cne 'CompleteExperimental')) {
            throw 'Runtime manifest edition must be canonical Lite or CompleteExperimental'
        }
        $manifestEdition = $editionProperty.Value
    }
    if ($manifestEdition -cne $canonicalEdition) { throw 'Runtime manifest edition does not match the requested installer edition' }
    $requirementProperty = $manifest.PSObject.Properties['browser_runtime_requirement']
    $browserRequirement = 'none'
    if ($canonicalEdition -ceq 'CompleteExperimental') {
        $browserRequirement = 'existing-webview2-evergreen-x64'
        if ($null -eq $editionProperty -or $null -eq $requirementProperty -or
            $requirementProperty.Value -isnot [string] -or $requirementProperty.Value -cne $browserRequirement) {
            throw 'CompleteExperimental requires explicit edition and existing WebView2 Evergreen x64 prerequisite'
        }
    } elseif ($null -ne $requirementProperty -and
        ($requirementProperty.Value -isnot [string] -or $requirementProperty.Value -cne 'none')) {
        throw 'Lite runtime cannot require a browser runtime'
    }
    $mappings = foreach ($entry in $manifest.files) {
        if ($entry.relative_path -isnot [string] -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            $entry.relative_path -match '^(?i:Uninstall\.exe|\.rcmusic-install\.ini)(?:[\\/]|$)') {
            throw 'Invalid or installer-reserved runtime entry'
        }
        if ($entry.relative_path.Replace('\', '/').StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Installer v1 cannot install undeclared download tools'
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
    $requiredFiles = @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($canonicalEdition -ceq 'CompleteExperimental') {
        $requiredFiles += @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt')
    }
    foreach ($required in $requiredFiles) {
        if ($verified.files.relative_path -cnotcontains $required) { throw "Missing required runtime file: $required" }
    }
    $files = @($verified.files | Sort-Object -Property relative_path -CaseSensitive -Culture '')
    $identity = @($files | ForEach-Object {
        [ordered]@{ path = $_.relative_path; size = $_.size_bytes; sha256 = $_.sha256 }
    }) | ConvertTo-Json -Depth 4 -Compress
    $recipeHashes = Get-RcMusicInstallerRecipeHashes
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $declaration = if ($FormatMatrix) { Read-RcMusicFormatMatrix -Path $FormatMatrix -RuntimeManifestHash $manifestHash -Destination $output -LegacyBaseline:$LegacyBaseline } else { $null }
    $formats = @(if ($declaration) { $declaration.formats })
    $associationJson = ConvertTo-Json -InputObject @($formats | ForEach-Object {
        [ordered]@{ extension = $_.extension; description = $_.description }
    }) -Compress
    $formatSignature = Get-RcMusicTextSha256 $associationJson
    $packageIdentity = [ordered]@{ inventory = $identity; format_signature = $formatSignature
        format_matrix_sha256 = $(if ($declaration) { $declaration.sha256 } else { $null })
        edition = $canonicalEdition; browser_runtime_requirement = $browserRequirement } | ConvertTo-Json -Compress
    $payloadId = Get-RcMusicInstallerIdentity -InventoryJson $packageIdentity -RecipeHashes $recipeHashes
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('; Generated from verified runtime bytes. Private local development only.')
    $lines.Add('!define RCMUSIC_OUTPUT "' + (ConvertTo-RcMusicNsisLiteral (Join-Path $output 'RCMusic-Setup.exe') -Passes 2) + '"')
    $lines.Add('!define RCMUSIC_INSTALL_ROOT "$LOCALAPPDATA\Programs\RC Music"')
    $lines.Add('!define RCMUSIC_SHORTCUT_ROOT "$SMPROGRAMS"')
    $lines.Add('!define RCMUSIC_REGISTRY_BASE "Software"')
    $lines.Add('!define RCMUSIC_EDITION "' + $canonicalEdition + '"')
    $lines.Add('!define RCMUSIC_REQUIRES_WEBVIEW2 ' + [int] ($canonicalEdition -ceq 'CompleteExperimental'))
    $lines.Add('!define RCMUSIC_PAYLOAD_ID "' + $payloadId + '"')
    $lines.Add('!define RCMUSIC_FORMAT_SIGNATURE "' + $formatSignature + '"')
    # No declaration is inferred when the explicit validated matrix is absent.
    $lines.Add('!define RCMUSIC_ASSOCIATIONS_ENABLED ' + [int] ($formats.Count -gt 0))
    $lines.Add('!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK')
    foreach ($format in $formats) {
        $lines.Add('    !insertmacro ${CALLBACK} "' + $format.extension + '" "' + $format.description + '"')
    }
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
        edition = $canonicalEdition; browser_runtime_requirement = $browserRequirement
        runtime_manifest_sha256 = $manifestHash
        source_commit = $manifest.source_commit; payload_id = $payloadId; files = $files; recipe_hashes = $recipeHashes
        associations_enabled = ($formats.Count -gt 0); format_signature = $formatSignature; formats = $formats
        format_matrix = $declaration
        config = ($lines -join "`n") + "`n" }
}

Export-ModuleMember -Function New-RcMusicInstallerPlan, Get-RcMusicInstallerRecipeHashes, Get-RcMusicInstallerIdentity, ConvertTo-RcMusicNsisLiteral
