#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'RcMusicFormatMatrix.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'RcMusicInstallerComponent.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'RcMusicExtensionComponent.psm1') -Force

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
            'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json',
            'RcMusicInstallerComponent.psm1', 'RcMusicDownloadComponentPe.psm1',
            'RcMusicExtensionComponent.psm1', 'RcMusicChromiumIdentity.psm1', 'RcMusicBrowserHosts.nsh') | ForEach-Object {
        $path = 'packaging/windows/' + $_
        [pscustomobject]@{ path = $path
            sha256 = (Get-FileHash -LiteralPath (Join-Path $repository $path) -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}

function Add-RcMusicInstallerGroupCondition([Collections.Generic.List[string]] $Lines, [int] $Group) {
    switch ($Group) {
        0 { }
        1 { $Lines.Add('    ${If} $RcDownloadSelected == 1') }
        2 { $Lines.Add('    ${If} $RcBrowserExtensionsSelected == 2'); $Lines.Add('    ${OrIf} $RcBrowserExtensionsSelected == 6') }
        4 { $Lines.Add('    ${If} $RcBrowserExtensionsSelected == 4'); $Lines.Add('    ${OrIf} $RcBrowserExtensionsSelected == 6') }
        6 { $Lines.Add('    ${If} $RcBrowserExtensionsSelected != 0') }
        default { throw 'Unknown installer file group' }
    }
}

function New-RcMusicInstallerPlan {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string] $RuntimeDirectory,
        [Parameter(Mandatory)][string] $OutputDirectory, [string] $FormatMatrix, [switch] $LegacyBaseline,
        [ValidateSet('Lite', 'CompleteExperimental')][string] $Edition = 'Lite',
        [AllowEmptyString()][string] $MainExtensionPackageReceipt,
        [AllowEmptyString()][string] $MainExtensionPackageReceiptSha256,
        [AllowEmptyString()][string] $YoutubeExtensionPackageReceipt,
        [AllowEmptyString()][string] $YoutubeExtensionPackageReceiptSha256)
    $extensionInputs = @()
    foreach ($channel in @('Main', 'Youtube')) {
        $pathName = $channel + 'ExtensionPackageReceipt'
        $pinName = $pathName + 'Sha256'
        if ($PSBoundParameters.ContainsKey($pathName) -or $PSBoundParameters.ContainsKey($pinName)) {
            if (-not $PSBoundParameters.ContainsKey($pathName) -or -not $PSBoundParameters.ContainsKey($pinName) -or
                [string]::IsNullOrWhiteSpace($PSBoundParameters[$pathName]) -or
                $PSBoundParameters[$pinName] -cnotmatch '\A[0-9a-f]{64}\z') {
                throw 'Extension packages require paired receipt paths and explicit lowercase SHA256 pins'
            }
            $extensionInputs += [pscustomobject]@{channel=$channel;path=$PSBoundParameters[$pathName];pin=$PSBoundParameters[$pinName]}
        }
    }
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
    # S13.1: a runtime that accepts --profile-root is a local test build, never installable.
    $testProfileRootProperty = $manifest.PSObject.Properties['test_profile_root']
    if ($null -ne $testProfileRootProperty -and
        ($testProfileRootProperty.Value -isnot [bool] -or $testProfileRootProperty.Value)) {
        throw 'Installer refuses a runtime built with the --profile-root test option'
    }
    $component = Get-RcMusicInstallerComponent -Manifest $manifest -RuntimeDirectory $runtime
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
            $entry.relative_path -match '^(?i:Uninstall\.exe|\.rcmusic-install\.ini|browser-extensions)(?:[\\/]|$)') {
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
    $requiredFiles = @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($canonicalEdition -ceq 'CompleteExperimental') {
        $requiredFiles += @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt')
    }
    foreach ($required in $requiredFiles) {
        if ($verified.files.relative_path -cnotcontains $required) { throw "Missing required runtime file: $required" }
    }
    $browserExtensions = @()
    foreach ($inputPackage in $extensionInputs) {
        if ($null -eq $component -or -not $manifest.download_component.PSObject.Properties['native_hosts']) {
            throw 'Extension packages require the matching enabled native host component'
        }
        $hostName = if ($inputPackage.channel -ceq 'Main') { 'com.rcmusic.media' } else { 'com.rcmusic.youtube' }
        $nativeChannel = @($manifest.download_component.native_hosts.channels | Where-Object host_name -CEQ $hostName)
        if ($nativeChannel.Count -ne 1) { throw 'An extension requires its exact native channel' }
        $browserExtensions += Get-RcMusicExtensionComponentPlan -ReceiptPath $inputPackage.path -ReceiptSha256 $inputPackage.pin `
            -Channel $inputPackage.channel -NativeChannel $nativeChannel[0]
    }
    $optionalPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    if ($null -ne $component) {
        foreach ($entry in $component.files) { $null = $optionalPaths.Add($entry.relative_path.Replace('\', '/')) }
    }
    $sortedFiles = @($verified.files | Sort-Object -Property relative_path -CaseSensitive -Culture '')
    $baseFiles = @($sortedFiles | Where-Object { -not $optionalPaths.Contains($_.relative_path) })
    $optionalFiles = @($sortedFiles | Where-Object { $optionalPaths.Contains($_.relative_path) })
    # A single boundary in each generated macro keeps the mandatory files
    # unconditional and the optional inventory together under the selection.
    $files = @($baseFiles) + @($optionalFiles)
    $fileGroups = [Collections.Generic.Dictionary[string,int]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $files) { $fileGroups.Add($entry.relative_path, $(if ($optionalPaths.Contains($entry.relative_path)) {1} else {0})) }
    foreach ($extension in $browserExtensions) {
        foreach ($entry in $extension.files) {
            if ($fileGroups.ContainsKey($entry.relative_path)) { throw 'An extension collides with the runtime inventory' }
            $fileGroups.Add($entry.relative_path, $(if ($extension.channel -ceq 'Main') {2} else {4}))
            $files += $entry
        }
    }
    $files = @($files | Sort-Object @{Expression={$fileGroups[$_.relative_path]}}, @{Expression={$_.relative_path}} -CaseSensitive -Culture '')
    $baseSizeKb = [long][Math]::Ceiling(($baseFiles | Measure-Object size_bytes -Sum).Sum / 1024)
    $downloadSizeKb = if ($null -ne $component) {
        [long][Math]::Ceiling((($optionalFiles | Measure-Object size_bytes -Sum).Sum + $component.marker.size_bytes) / 1024)
    } else { 0 }
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
    $payloadIds = $null
    $payloadVariants = @()
    $generatedFiles = @()
    if ($null -ne $component) {
        $variants = @{}
        foreach ($selection in @(0, 1)) {
            $selectedFiles = @($baseFiles)
            if ($selection -eq 1) { $selectedFiles += @($optionalFiles) + @($component.marker) }
            $selectedInventory = @($selectedFiles | ForEach-Object {
                [ordered]@{ path = $_.relative_path; size = $_.size_bytes; sha256 = $_.sha256 }
            }) | ConvertTo-Json -Depth 4 -Compress
            $variantIdentity = [ordered]@{
                inventory = $selectedInventory; available_inventory = $identity; download_selection = $selection
                receipt_sha256 = $component.receipt_sha256; header_sha256 = $component.header_sha256
                format_signature = $formatSignature
                format_matrix_sha256 = $(if ($declaration) { $declaration.sha256 } else { $null })
                edition = $canonicalEdition; browser_runtime_requirement = $browserRequirement
            } | ConvertTo-Json -Compress
            $variants[$selection] = Get-RcMusicInstallerIdentity -InventoryJson $variantIdentity -RecipeHashes $recipeHashes
        }
        $payloadIds = [pscustomobject]@{ base = $variants[0]; downloads = $variants[1] }
        $payloadId = $payloadIds.base
        $generatedFiles = @([pscustomobject]@{ relative_path = 'download-component-marker.bin'
            content = $component.marker.content; sha256 = $component.marker.sha256 })
    }
    if ($browserExtensions.Count -gt 0) {
        $availableMask = 1
        foreach ($extension in $browserExtensions) { $availableMask = $availableMask -bor $(if($extension.channel -ceq 'Main'){2}else{4}) }
        foreach ($mask in @(0,1,3,5,7)) {
            if (($mask -band $availableMask) -ne $mask) { continue }
            $selected = @($files | Where-Object { $group=$fileGroups[$_.relative_path];$group -eq 0 -or ($mask -band $group) -ne 0 })
            if ($mask -band 1) { $selected += $component.marker }
            $selectedInventory = @($selected | ForEach-Object { [ordered]@{path=$_.relative_path;size=$_.size_bytes;sha256=$_.sha256} })
            $variant = [ordered]@{inventory=$selectedInventory;available_inventory=$identity;component_mask=$mask
                receipt_sha256=$component.receipt_sha256;header_sha256=$component.header_sha256
                extension_receipts=@($browserExtensions | ForEach-Object {[ordered]@{channel=$_.channel;sha256=$_.receipt_sha256}})
                format_signature=$formatSignature;format_matrix_sha256=$(if($declaration){$declaration.sha256}else{$null})
                edition=$canonicalEdition;browser_runtime_requirement=$browserRequirement}
            $payloadVariants += [pscustomobject]@{mask=$mask;payload_id=(Get-RcMusicInstallerIdentity -InventoryJson ($variant|ConvertTo-Json -Depth 8 -Compress) -RecipeHashes $recipeHashes)}
        }
        $payloadIds = [pscustomobject]@{base=($payloadVariants|Where-Object mask -EQ 0).payload_id;downloads=($payloadVariants|Where-Object mask -EQ 1).payload_id}
        $payloadId = $payloadIds.base
    }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('; Generated from verified runtime bytes. Private local development only.')
    $lines.Add('!define RCMUSIC_OUTPUT "' + (ConvertTo-RcMusicNsisLiteral (Join-Path $output 'RCMusic-Setup.exe') -Passes 2) + '"')
    $lines.Add('!define RCMUSIC_INSTALL_ROOT "$LOCALAPPDATA\Programs\RC Music"')
    $lines.Add('!define RCMUSIC_SHORTCUT_ROOT "$SMPROGRAMS"')
    $lines.Add('!define RCMUSIC_REGISTRY_BASE "Software"')
    $lines.Add('!define RCMUSIC_EDITION "' + $canonicalEdition + '"')
    $lines.Add('!define RCMUSIC_REQUIRES_WEBVIEW2 ' + [int] ($canonicalEdition -ceq 'CompleteExperimental'))
    $lines.Add('!define RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE ' + [int] ($null -ne $component))
    $lines.Add('!define RCMUSIC_BASE_SIZE_KB ' + $baseSizeKb)
    $lines.Add('!define RCMUSIC_DOWNLOAD_SIZE_KB ' + $downloadSizeKb)
    $lines.Add('!define RCMUSIC_BROWSER_EXTENSIONS_AVAILABLE ' + [int]($browserExtensions.Count -gt 0))
    foreach ($channel in @('Main','Youtube')) {
        $extension = @($browserExtensions|Where-Object channel -CEQ $channel)
        $name = $channel.ToUpperInvariant()
        $lines.Add('!define RCMUSIC_' + $name + '_EXTENSION_AVAILABLE ' + [int]($extension.Count -eq 1))
        $size = if($extension.Count -eq 1){[long][Math]::Ceiling(($extension[0].files|Measure-Object size_bytes -Sum).Sum/1024)}else{0}
        $lines.Add('!define RCMUSIC_' + $name + '_EXTENSION_SIZE_KB ' + $size)
    }
    if ($browserExtensions.Count -gt 0) {
        foreach ($mode in @('SELECT','DETECT')) {
            $lines.Add('!macro RCMUSIC_' + $mode + '_VARIANT')
            $lines.Add('    StrCpy $RcStatus 1')
            $first=$true
            foreach ($variant in $payloadVariants) {
                $test = if($mode -ceq 'SELECT'){'$RcVariantMask == ' + $variant.mask}else{'$0 S== "${RCMUSIC_INSTALL_ROOT}\' + $variant.payload_id + '"'}
                $lines.Add('    ${' + $(if($first){'If'}else{'ElseIf'}) + '} ' + $test)
                $first=$false
                $lines.Add('        StrCpy $RcPayloadId "' + $variant.payload_id + '"')
                $lines.Add('        StrCpy $RcDownloadSelected ' + ($variant.mask -band 1))
                $lines.Add('        StrCpy $RcBrowserExtensionsSelected ' + ($variant.mask -band 6))
                $lines.Add('        StrCpy $RcVariantMask ' + $variant.mask)
                $lines.Add('        StrCpy $RcStatus 0')
            }
            $lines.Add('    ${EndIf}')
            $lines.Add('!macroend')
        }
        $lines.Add('!macro RCMUSIC_BROWSER_HOSTS CALLBACK')
        foreach ($extension in $browserExtensions) {
            $bit=if($extension.channel -ceq 'Main'){2}else{4}
            $lines.Add('    ${If} $RcBrowserExtensionsSelected == ' + $bit)
            $lines.Add('    ${OrIf} $RcBrowserExtensionsSelected == 6')
            foreach ($browser in @('BraveSoftware\Brave-Browser','Google\Chrome','Microsoft\Edge')) {
                $lines.Add('        !insertmacro ${CALLBACK} "' + $browser + '\NativeMessagingHosts\' + $extension.native_host_name + '" "' + $extension.native_host_name + '.json"')
            }
            $lines.Add('    ${EndIf}')
        }
        $lines.Add('!macroend')
    }
    if ($null -ne $component) {
        $lines.Add('!define RCMUSIC_BASE_PAYLOAD_ID "' + $payloadIds.base + '"')
        $lines.Add('!define RCMUSIC_DOWNLOAD_PAYLOAD_ID "' + $payloadIds.downloads + '"')
        $lines.Add('!define RCMUSIC_PAYLOAD_ID "$RcPayloadId"')
        $lines.Add('!define RCMUSIC_ACTIVATION_PATH "download-tools\installed-v1"')
    } else {
        $lines.Add('!define RCMUSIC_PAYLOAD_ID "' + $payloadId + '"')
    }
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
    $currentGroup = 0
    foreach ($entry in $files) {
        $group=$fileGroups[$entry.relative_path]
        if ($group -ne $currentGroup) {
            if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
            Add-RcMusicInstallerGroupCondition $lines $group
            $currentGroup=$group
        }
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
    if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_PAYLOAD_FILES CALLBACK')
    $directoryGroups = [Collections.Generic.Dictionary[string,int]]::new([StringComparer]::OrdinalIgnoreCase)
    $currentGroup = 0
    for ($index = 0; $index -lt $files.Count; $index++) {
        $entry = $files[$index]
        $group=$fileGroups[$entry.relative_path]
        if ($group -ne $currentGroup) {
            if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
            Add-RcMusicInstallerGroupCondition $lines $group
            $currentGroup=$group
        }
        $relative = $entry.relative_path.Replace('/', '\')
        $token = 'RCMUSIC_PATH_{0:D4}' -f $index
        # Pass tokens through nested callbacks; expand their data only at the
        # final NSIS instruction so every literal has exactly two passes.
        $lines.Add('    !insertmacro ${CALLBACK} "' + $token + '" "' + $entry.sha256 + '"')
        $parent = [IO.Path]::GetDirectoryName($relative)
        while ($parent) {
            if ($directoryGroups.ContainsKey($parent)) { $directoryGroups[$parent]=$directoryGroups[$parent] -bor $group }
            else { $directoryGroups.Add($parent,$group) }
            $parent = [IO.Path]::GetDirectoryName($parent)
        }
    }
    if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
    $lines.Add('!macroend')
    $sortedDirectories = @($directoryGroups.Keys | Sort-Object -Property @{Expression={$directoryGroups[$_]}}, @{ Expression = { $_.Split('\').Count }; Descending = $true },
        @{ Expression = { $_ }; Descending = $false })
    for ($index = 0; $index -lt $sortedDirectories.Count; $index++) {
        $token = 'RCMUSIC_DIR_{0:D4}' -f $index
        $lines.Add('!define ' + $token + ' "' + (ConvertTo-RcMusicNsisLiteral $sortedDirectories[$index] -Runtime -Passes 2) + '"')
    }
    $lines.Add('!macro RCMUSIC_PAYLOAD_DIRECTORIES CALLBACK')
    $currentGroup = 0
    for ($index = 0; $index -lt $sortedDirectories.Count; $index++) {
        $group=$directoryGroups[$sortedDirectories[$index]]
        if ($group -ne $currentGroup) {
            if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
            Add-RcMusicInstallerGroupCondition $lines $group
            $currentGroup=$group
        }
        $token = 'RCMUSIC_DIR_{0:D4}' -f $index
        $lines.Add('    !insertmacro ${CALLBACK} "' + $token + '"')
    }
    if ($currentGroup -ne 0) { $lines.Add('    ${EndIf}') }
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_ACTIVATION_FILE CALLBACK')
    if ($null -ne $component) {
        $lines.Add('    ${If} $RcDownloadSelected == 1')
        $lines.Add('        !insertmacro ${CALLBACK} "RCMUSIC_ACTIVATION_PATH" "' + $component.marker.sha256 + '"')
        $lines.Add('    ${EndIf}')
    }
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_ACTIVATION_EXTRACT')
    if ($null -ne $component) {
        $lines.Add('    ${If} $RcDownloadSelected == 1')
        $lines.Add('        ClearErrors')
        $lines.Add('        SetOutPath "$INSTDIR\download-tools"')
        $lines.Add('        IfErrors rcmusic_install_failed')
        $lines.Add('        ClearErrors')
        $lines.Add('        File "/oname=installed-v1" "' +
            (ConvertTo-RcMusicNsisLiteral (Join-Path $output 'download-component-marker.bin')) + '"')
        $lines.Add('        IfErrors rcmusic_install_failed')
        $lines.Add('    ${EndIf}')
    }
    $lines.Add('!macroend')
    $lines.Add('!macro RCMUSIC_VERIFY_ACTIVATION PREFIX')
    if ($null -ne $component) {
        $lines.Add('    StrCpy $RcPath "$INSTDIR\${RCMUSIC_ACTIVATION_PATH}"')
        $lines.Add('    Call ${PREFIX}RCMusic.HashFile')
        $lines.Add('    ${If} $RcDownloadSelected == 1')
        $lines.Add('        ${If} $RcStatus == 2')
        $lines.Add('        ${AndIf} $RcAllowMissing == 1')
        $lines.Add('            ; An earlier explicit uninstall may have removed its marker.')
        $lines.Add('        ${ElseIf} $RcStatus != 0')
        $lines.Add('            Goto rcmusic_verify_failed')
        $lines.Add('        ${ElseIf} $RcHash S!= "' + $component.marker.sha256 + '"')
        $lines.Add('            Goto rcmusic_verify_failed')
        $lines.Add('        ${EndIf}')
        $lines.Add('    ${Else}')
        $lines.Add('        ${If} $RcDownloadSelected != 0')
        $lines.Add('        ${OrIf} $RcStatus != 2')
        $lines.Add('            Goto rcmusic_verify_failed')
        $lines.Add('        ${EndIf}')
        $lines.Add('    ${EndIf}')
        $lines.Add('    StrCpy $RcStatus 0')
    }
    $lines.Add('!macroend')
    return [pscustomobject]@{ output = $output; runtime = $runtime; runtime_manifest = $manifestPath
        edition = $canonicalEdition; browser_runtime_requirement = $browserRequirement
        runtime_manifest_sha256 = $manifestHash
        source_commit = $manifest.source_commit; payload_id = $payloadId; files = $files; recipe_hashes = $recipeHashes
        payload_ids = $payloadIds; payload_variants=$payloadVariants; browser_extensions=$browserExtensions
        download_component = $component; generated_files = $generatedFiles
        associations_enabled = ($formats.Count -gt 0); format_signature = $formatSignature; formats = $formats
        format_matrix = $declaration
        config = ($lines -join "`n") + "`n" }
}

Export-ModuleMember -Function New-RcMusicInstallerPlan, Get-RcMusicInstallerRecipeHashes, Get-RcMusicInstallerIdentity, ConvertTo-RcMusicNsisLiteral
