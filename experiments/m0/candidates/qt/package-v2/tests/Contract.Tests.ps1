#Requires -Version 7.2
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$packageRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$contractPath = Join-Path $packageRoot 'qt-shell-composite-package-contract.v2.json'
$sidecarPath = "$contractPath.sha256"
$launcherPath = Join-Path $packageRoot 'Invoke-M0QtShellCompositePackage.ps1'
$modulePath = Join-Path $packageRoot 'M0.QtShellCompositePackage.psm1'
$nativeModulePath = Join-Path $packageRoot 'M0.QtShellCompositePackageNative.psm1'
$smokeModulePath = Join-Path $packageRoot 'M0.QtShellCompositePackageSmoke.psm1'
$smokeLauncherPath = Join-Path $packageRoot 'Test-M0QtShellCompositePackageSmoke.ps1'
$recipePaths = @(
    'experiments/m0/candidates/qt/package-v2/Invoke-M0QtShellCompositePackage.ps1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackage.psm1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageNative.psm1',
    'experiments/m0/candidates/qt/package-v2/M0.QtShellCompositePackageSmoke.psm1',
    'experiments/m0/candidates/qt/package-v2/Test-M0QtShellCompositePackageSmoke.ps1',
    'experiments/m0/candidates/qt/package-v2/qt.conf',
    'experiments/m0/candidates/qt/package-v2/PACKAGE-LIMITATIONS.txt'
)
$v1Paths = @(
    'experiments/m0/candidates/qt/package/QtShellPackageBuilder.psm1',
    'experiments/m0/candidates/qt/package/New-QtShellPackage.ps1'
)

$state = @{ Passed = 0; Failed = 0 }
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected, [string] $Message) {
    if ($Actual -cne $Expected) { throw "$Message Expected '$Expected', found '$Actual'." }
}
function Get-Sha256([string] $Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function ConvertTo-OrdinalList([object[]] $Values) {
    [string[]] $copy = @($Values | ForEach-Object { [string] $_ })
    [Array]::Sort($copy, [StringComparer]::Ordinal)
    return $copy -join "`n"
}
function Assert-Fields($Object, [string[]] $Expected, [string] $Context) {
    Assert-True ($Object -is [pscustomobject]) "$Context must be an object."
    $actual = ConvertTo-OrdinalList @($Object.PSObject.Properties.Name)
    $wanted = ConvertTo-OrdinalList $Expected
    Assert-Equal $actual $wanted "$Context fields drifted."
}
function Assert-Sha([object] $Value, [string] $Context) {
    Assert-True ($Value -is [string] -and $Value -cmatch '^[0-9a-f]{64}$') `
        "$Context must be one lowercase SHA-256."
}
function Assert-Pin($Pin, [string] $Context) {
    Assert-Fields $Pin @('relative_path', 'size_bytes', 'sha256') $Context
    Assert-True ($Pin.relative_path -is [string] -and $Pin.relative_path -cnotmatch '^[A-Za-z]:|^[/\\]|(^|[/\\])\.\.($|[/\\])') `
        "$Context relative path is unsafe."
    Assert-True ($Pin.size_bytes -is [long] -and $Pin.size_bytes -ge 0) "$Context size is invalid."
    Assert-Sha $Pin.sha256 "$Context hash"
}
function Assert-NoDuplicateJsonProperties([Text.Json.JsonElement] $Element, [string] $Context) {
    if ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            Assert-True ($seen.Add($property.Name)) "$Context contains duplicate property '$($property.Name)'."
            Assert-NoDuplicateJsonProperties $property.Value "$Context.$($property.Name)"
        }
    }
    elseif ($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        $index = 0
        foreach ($item in $Element.EnumerateArray()) {
            Assert-NoDuplicateJsonProperties $item "$Context[$index]"
            $index++
        }
    }
}

Test-Case 'package-v2 production surface is additive and complete' {
    foreach ($path in @($contractPath, $sidecarPath, $launcherPath, $modulePath, $nativeModulePath,
            $smokeModulePath, $smokeLauncherPath, (Join-Path $packageRoot 'qt.conf'),
            (Join-Path $packageRoot 'PACKAGE-LIMITATIONS.txt'))) {
        Assert-True ([IO.File]::Exists($path)) "Package-v2 production member is missing: $path"
    }
    foreach ($relative in $v1Paths) {
        Assert-True ([IO.File]::Exists((Join-Path $repository $relative))) "Historical v1 member is missing: $relative"
    }
}

Test-Case 'package-v2 contract is strict duplicate-free UTF-8 and externally hashable' {
    Assert-True ([IO.File]::Exists($contractPath)) "Missing '$contractPath'."
    $bytes = [IO.File]::ReadAllBytes($contractPath)
    Assert-True (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xef -and $bytes[1] -eq 0xbb -and $bytes[2] -eq 0xbf)) `
        'Contract contains a UTF-8 BOM.'
    $text = [Text.UTF8Encoding]::new($false, $true).GetString($bytes)
    $document = [Text.Json.JsonDocument]::Parse($text)
    try { Assert-NoDuplicateJsonProperties $document.RootElement '$' }
    finally { $document.Dispose() }
    $expectedSidecar = "$(Get-Sha256 $contractPath)  qt-shell-composite-package-contract.v2.json`n"
    Assert-Equal ([IO.File]::ReadAllText($sidecarPath, [Text.UTF8Encoding]::new($false, $true))) `
        $expectedSidecar 'Contract sidecar drifted.'
}

Test-Case 'package-v2 contract closes platform compiler Qt and external composite anchors' {
    $contract = Get-Content -Raw -LiteralPath $contractPath | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Fields $contract @('schema_version', 'package_id', 'product', 'recipe', 'inputs', 'build',
        'deployment', 'envelope', 'smoke', 'security', 'limits') 'Contract root'
    Assert-Equal $contract.schema_version ([long]2) 'Schema drifted.'
    Assert-Equal $contract.package_id 'm0.2-qt-shell-composite-package-v2' 'Package identity drifted.'
    Assert-Fields $contract.product @('qt_version', 'edition', 'architecture', 'compiler_abi',
        'windows_sdk_version', 'msvc_compiler_version', 'msvc_toolset_version', 'qt_config_msvc_version') 'Product'
    Assert-Equal $contract.product.qt_version '6.11.2' 'Qt version drifted.'
    Assert-Equal $contract.product.edition 'Desktop' 'Qt edition drifted.'
    Assert-Equal $contract.product.architecture 'x86_64' 'Architecture drifted.'
    Assert-Equal $contract.product.compiler_abi 'msvc2022_64' 'Compiler ABI drifted.'
    Assert-Equal $contract.product.windows_sdk_version '10.0.26100.0' 'Windows SDK drifted.'
    Assert-Equal $contract.product.msvc_compiler_version '19.44.35228' 'MSVC compiler drifted.'
    Assert-Equal $contract.product.msvc_toolset_version '14.44.35207' 'MSVC toolset drifted.'
    Assert-Equal $contract.product.qt_config_msvc_version '19.44.35228' 'Qt config compiler identity drifted.'
    Assert-Fields $contract.inputs @('composite_contract', 'composite_receipt', 'qtbase_receipt',
        'sources', 'catch2', 'tools', 'sbom_files') 'Inputs'
    foreach ($name in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        Assert-Pin $contract.inputs.$name "External anchor $name"
    }
    Assert-Equal $contract.inputs.composite_contract.sha256 `
        'f75eb9bec13639f8b5201c0db749dd67cc4840732578c159d09218224e357f28' 'Composite contract anchor drifted.'
    Assert-Equal $contract.inputs.composite_receipt.sha256 `
        '30399b3099cf04bdd50d6f083d077e82a590afefeec0617fda89ef5266a1d63e' 'Composite receipt anchor drifted.'
    Assert-Equal $contract.inputs.qtbase_receipt.sha256 `
        '8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619' 'Qtbase receipt anchor drifted.'
    Assert-True ($null -eq $contract.PSObject.Properties['qt_root'] -and
        $null -eq $contract.inputs.PSObject.Properties['qt_root']) `
        'Contract accepts an arbitrary QtRoot instead of the authenticated composite receipt.'
}

Test-Case 'package-v2 contract pins sources tools SBOMs and every recipe member' {
    $contract = Get-Content -Raw -LiteralPath $contractPath | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Fields $contract.recipe @('files', 'core_exports', 'native_export', 'smoke_export') 'Recipe'
    Assert-Equal (ConvertTo-OrdinalList @($contract.recipe.core_exports)) `
        (ConvertTo-OrdinalList @('Assert-M0QtShellCompositePackageReceipt', 'Invoke-M0QtShellCompositePackage')) `
        'Core export contract drifted.'
    Assert-Equal $contract.recipe.native_export 'Invoke-M0QtShellCompositePackageNative' 'Native export drifted.'
    Assert-Equal $contract.recipe.smoke_export 'Test-M0QtShellCompositePackageSmoke' 'Smoke export drifted.'
    Assert-Equal (ConvertTo-OrdinalList @($contract.recipe.files.relative_path)) `
        (ConvertTo-OrdinalList $recipePaths) 'Recipe members drifted.'
    foreach ($member in @($contract.recipe.files)) {
        Assert-Pin $member "Recipe member $($member.relative_path)"
        $path = Join-Path $repository $member.relative_path
        Assert-True ([IO.File]::Exists($path)) "Pinned recipe member is missing: $($member.relative_path)"
        Assert-Equal ([long](Get-Item -LiteralPath $path).Length) ([long]$member.size_bytes) 'Recipe size drifted.'
        Assert-Equal (Get-Sha256 $path) $member.sha256 'Recipe hash drifted.'
    }
    Assert-Fields $contract.inputs.sources @('shell_tree', 'fixture_tree', 'protocol_tree', 'cmakelists',
        'fixture_manifest', 'protocol') 'Source anchors'
    foreach ($treeName in @('shell_tree', 'fixture_tree', 'protocol_tree')) {
        $tree = $contract.inputs.sources.$treeName
        Assert-Fields $tree @('relative_path', 'git_tree') "Source tree $treeName"
        Assert-True ($tree.git_tree -is [string] -and $tree.git_tree -cmatch '^[0-9a-f]{40}$') `
            "Source tree $treeName identity is malformed."
    }
    foreach ($fileName in @('cmakelists', 'fixture_manifest', 'protocol')) {
        Assert-Pin $contract.inputs.sources.$fileName "Source file $fileName"
    }
    Assert-Fields $contract.inputs.catch2 @('version', 'commit', 'source_relative_path',
        'archive_relative_path', 'archive_size_bytes', 'archive_sha256') 'Catch2'
    Assert-Equal $contract.inputs.catch2.version '3.15.3' 'Catch2 version drifted.'
    Assert-Equal $contract.inputs.catch2.commit '8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb' 'Catch2 commit drifted.'
    Assert-Sha $contract.inputs.catch2.archive_sha256 'Catch2 archive hash'
    $toolIds = @('cmake', 'ctest', 'ninja', 'cl', 'link', 'dumpbin', 'rc', 'mt',
        'vsdevcmd', 'cmd', 'git', 'windeployqt', 'qtpaths', 'qmllint')
    Assert-Equal (ConvertTo-OrdinalList @($contract.inputs.tools.id)) (ConvertTo-OrdinalList $toolIds) `
        'Pinned tool set drifted.'
    foreach ($tool in @($contract.inputs.tools)) {
        Assert-Fields $tool @('id', 'version', 'path_kind', 'path', 'size_bytes', 'sha256') "Tool $($tool.id)"
        Assert-True ($tool.path_kind -cin @('absolute', 'composite-sdk-relative')) "Tool $($tool.id) path kind is invalid."
        Assert-Sha $tool.sha256 "Tool $($tool.id) hash"
    }
    Assert-Equal (ConvertTo-OrdinalList @($contract.inputs.sbom_files.id)) `
        (ConvertTo-OrdinalList @('qtdeclarative-binary', 'qtsvg-binary')) 'SBOM set drifted.'
    foreach ($sbom in @($contract.inputs.sbom_files)) {
        Assert-Fields $sbom @('id', 'source_relative_path', 'target_relative_path', 'size_bytes', 'sha256') `
            "SBOM $($sbom.id)"
        Assert-Sha $sbom.sha256 "SBOM $($sbom.id) hash"
    }
}

Test-Case 'package-v2 recipe members have deterministic LF checkout attributes' {
    foreach ($relativePath in $recipePaths) {
        $attributeRows = @(& git -C $repository check-attr text eol -- $relativePath)
        Assert-Equal $LASTEXITCODE 0 "Git attributes failed for '$relativePath'."
        $attributes = @{}
        foreach ($row in $attributeRows) {
            if ($row -notmatch '^.+: (text|eol): (.+)$') {
                throw "Unexpected Git attribute row for '$relativePath': '$row'."
            }
            $attributes[$Matches[1]] = $Matches[2]
        }
        Assert-Equal $attributes.text 'set' "Recipe member '$relativePath' is not text."
        Assert-Equal $attributes.eol 'lf' "Recipe member '$relativePath' has unstable EOL checkout."
    }
}

Test-Case 'package-v2 contract pins resolve to the current physical inputs and source trees' {
    $contract = Get-Content -Raw -LiteralPath $contractPath |
        ConvertFrom-Json -Depth 100 -DateKind String

    foreach ($name in @('composite_contract', 'composite_receipt', 'qtbase_receipt')) {
        $pin = $contract.inputs.$name
        $path = Join-Path $repository $pin.relative_path
        Assert-True ([IO.File]::Exists($path)) "External input is missing: $name"
        Assert-Equal ([long](Get-Item -LiteralPath $path).Length) ([long]$pin.size_bytes) `
            "External input size drifted: $name"
        Assert-Equal (Get-Sha256 $path) $pin.sha256 "External input hash drifted: $name"
    }

    foreach ($treeName in @('shell_tree', 'fixture_tree', 'protocol_tree')) {
        $tree = $contract.inputs.sources.$treeName
        $actualTree = (& git -C $repository rev-parse "HEAD:$($tree.relative_path)").Trim()
        Assert-Equal $LASTEXITCODE 0 "Could not resolve source tree $treeName."
        Assert-Equal $actualTree $tree.git_tree "Source tree drifted: $treeName"
    }
    foreach ($fileName in @('cmakelists', 'fixture_manifest', 'protocol')) {
        $pin = $contract.inputs.sources.$fileName
        $path = Join-Path $repository $pin.relative_path
        Assert-True ([IO.File]::Exists($path)) "Pinned source file is missing: $fileName"
        Assert-Equal ([long](Get-Item -LiteralPath $path).Length) ([long]$pin.size_bytes) `
            "Pinned source size drifted: $fileName"
        Assert-Equal (Get-Sha256 $path) $pin.sha256 "Pinned source hash drifted: $fileName"
    }

    $catchArchive = Join-Path $repository $contract.inputs.catch2.archive_relative_path
    Assert-True ([IO.File]::Exists($catchArchive)) 'Pinned Catch2 archive is missing.'
    Assert-Equal ([long](Get-Item -LiteralPath $catchArchive).Length) `
        ([long]$contract.inputs.catch2.archive_size_bytes) 'Catch2 archive size drifted.'
    Assert-Equal (Get-Sha256 $catchArchive) $contract.inputs.catch2.archive_sha256 `
        'Catch2 archive hash drifted.'

    $compositeReceiptPath = Join-Path $repository $contract.inputs.composite_receipt.relative_path
    $compositeRunRoot = [IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($compositeReceiptPath))
    $compositeSdkRoot = Join-Path $compositeRunRoot 'sdk'
    foreach ($tool in @($contract.inputs.tools)) {
        $path = if ($tool.path_kind -ceq 'absolute') {
            [IO.Path]::GetFullPath([string]$tool.path)
        }
        else {
            Join-Path $compositeSdkRoot ([string]$tool.path)
        }
        Assert-True ([IO.File]::Exists($path)) "Pinned tool is missing: $($tool.id)"
        Assert-Equal ([long](Get-Item -LiteralPath $path).Length) ([long]$tool.size_bytes) `
            "Pinned tool size drifted: $($tool.id)"
        Assert-Equal (Get-Sha256 $path) $tool.sha256 "Pinned tool hash drifted: $($tool.id)"
    }
    foreach ($sbom in @($contract.inputs.sbom_files)) {
        $path = Join-Path $compositeSdkRoot ([string]$sbom.source_relative_path)
        Assert-True ([IO.File]::Exists($path)) "Pinned SBOM is missing: $($sbom.id)"
        Assert-Equal ([long](Get-Item -LiteralPath $path).Length) ([long]$sbom.size_bytes) `
            "Pinned SBOM size drifted: $($sbom.id)"
        Assert-Equal (Get-Sha256 $path) $sbom.sha256 "Pinned SBOM hash drifted: $($sbom.id)"
    }
    $qconfig = $contract.deployment.composite_qconfig
    $qconfigPath = Join-Path $compositeSdkRoot ([string]$qconfig.relative_path)
    Assert-True ([IO.File]::Exists($qconfigPath)) 'Pinned composite qconfig is missing.'
    Assert-Equal ([long](Get-Item -LiteralPath $qconfigPath).Length) ([long]$qconfig.size_bytes) `
        'Composite qconfig size drifted.'
    Assert-Equal (Get-Sha256 $qconfigPath) $qconfig.sha256 'Composite qconfig hash drifted.'
}

Test-Case 'package-v2 contract closes build deployment envelope smoke and security policy' {
    $contract = Get-Content -Raw -LiteralPath $contractPath | ConvertFrom-Json -Depth 100 -DateKind String
    Assert-Fields $contract.build @('generator', 'configuration', 'qt_on', 'core_off', 'qml_lint') 'Build'
    Assert-Equal $contract.build.generator 'Ninja' 'Build generator drifted.'
    Assert-Equal $contract.build.configuration 'Release' 'Build configuration drifted.'
    Assert-Equal $contract.build.qt_on.support_enabled $true 'Qt-on build is disabled.'
    Assert-Equal $contract.build.qt_on.expected_ctest_count ([long]65) 'Qt-on CTest count drifted.'
    Assert-Equal $contract.build.core_off.support_enabled $false 'Core-off build enabled Qt support.'
    Assert-Equal $contract.build.core_off.expected_ctest_count ([long]55) 'Core-off CTest count drifted.'
    Assert-Equal $contract.build.qml_lint.required $true 'qmllint is not mandatory.'
    Assert-Equal $contract.deployment.dry_run_equals_actual $true 'Deploy dry-run/actual equality is not mandatory.'
    Assert-Equal $contract.deployment.composite_sdk_only $true 'Deployment permits a non-composite Qt SDK.'
    Assert-Equal $contract.deployment.application_qml_only $true 'Deployment permits arbitrary application QML sources.'
    Assert-Equal $contract.deployment.allow_official_sdk_fallback $false 'Official Qt SDK fallback is enabled.'
    Assert-Equal $contract.deployment.allow_system32_qt_or_d3d_fallback $false 'System32 Qt/D3D fallback is enabled.'
    Assert-Fields $contract.envelope @('top_level', 'payload_relative_path', 'evidence_relative_path',
        'manifest_relative_path', 'receipt_relative_path', 'receipt_is_final_entry', 'create_new',
        'allow_reparse_points', 'allow_ads', 'allow_hardlinks', 'path_comparer') 'Envelope'
    Assert-Equal (ConvertTo-OrdinalList @($contract.envelope.top_level)) (ConvertTo-OrdinalList @('app', 'evidence')) `
        'Top-level envelope drifted.'
    Assert-Equal $contract.envelope.receipt_is_final_entry $true 'Receipt is not the final entry.'
    Assert-Equal $contract.envelope.create_new $true 'Create-new publication is disabled.'
    Assert-Equal $contract.envelope.allow_reparse_points $false 'Reparse points are allowed.'
    Assert-Equal $contract.envelope.allow_ads $false 'ADS are allowed.'
    Assert-Equal $contract.envelope.allow_hardlinks $false 'Hardlinks are allowed.'
    Assert-Equal $contract.envelope.path_comparer 'OrdinalIgnoreCase' 'Path comparer drifted.'
    Assert-Equal (ConvertTo-OrdinalList @($contract.smoke.variants)) `
        (ConvertTo-OrdinalList @('original', 'relocated-spaces-accents')) 'Smoke variants drifted.'
    Assert-Equal (ConvertTo-OrdinalList @($contract.smoke.journeys.id)) `
        (ConvertTo-OrdinalList @('tray-lifecycle', 'error-recovery')) 'Smoke journeys drifted.'
    $trayJourney = @($contract.smoke.journeys | Where-Object id -CEQ 'tray-lifecycle')[0]
    $errorJourney = @($contract.smoke.journeys | Where-Object id -CEQ 'error-recovery')[0]
    Assert-Fields $trayJourney @('id', 'arguments') 'Tray smoke journey'
    Assert-Fields $errorJourney @('id', 'arguments') 'Error smoke journey'
    Assert-Equal (@($trayJourney.arguments) -join "`n") '--m0-smoke=tray-lifecycle' `
        'Tray smoke arguments drifted.'
    Assert-Equal (@($errorJourney.arguments) -join "`n") `
        "--m0-scenario=unavailable`n--m0-smoke=error-recovery" `
        'Error smoke arguments drifted.'
    Assert-Equal $contract.smoke.module_capture_deadline_ms ([long]500) 'Module capture deadline drifted.'
    Assert-Equal $contract.smoke.verify_before_and_after_each $true 'Per-journey verification is disabled.'
    Assert-Equal $contract.smoke.require_zero_survivors $true 'Zero-survivor policy is disabled.'
    Assert-Fields $contract.security @('redistribution_allowed', 'chk_sec', 'qtbase_sbom_status',
        'notices_status', 'sources_status') 'Security status'
    Assert-Equal $contract.security.redistribution_allowed $false 'Contract approved redistribution.'
    Assert-Equal $contract.security.chk_sec 'open' 'Contract falsely closed CHK-SEC.'
    Assert-Equal $contract.security.qtbase_sbom_status 'absent' 'Qtbase SBOM status drifted.'
    Assert-Equal $contract.security.notices_status 'incomplete' 'Notices status drifted.'
    Assert-Equal $contract.security.sources_status 'incomplete' 'Sources status drifted.'
    foreach ($needle in @('ABI', 'clean machine', 'CRT', 'PCRE2', 'licenses', 'notices', 'SBOM',
            'native', 'Linux', 'M0.2', 'stack', 'hostile concurrent same-user',
            'translations/catalogs.json', 'Kill(entireProcessTree)', 'Windows Job Object',
            'detached descendants')) {
        Assert-True ((@($contract.limits) -join "`n").Contains($needle, [StringComparison]::OrdinalIgnoreCase)) `
            "Contract limitations omit '$needle'."
    }
    $limitationText = [IO.File]::ReadAllText(
        (Join-Path $packageRoot 'PACKAGE-LIMITATIONS.txt'), [Text.UTF8Encoding]::new($false, $true))
    foreach ($needle in @('writer', 'mesmo usuario', 'translations/catalogs.json',
            'qualquer outro stderr', 'Kill(entireProcessTree)', 'Windows Job Object',
            'descendentes destacados')) {
        Assert-True ($limitationText.Contains($needle, [StringComparison]::OrdinalIgnoreCase)) `
            "Package limitations omit '$needle'."
    }
}

Test-Case 'package-v2 records the launcher self-authentication trust-root limitation' {
    $contract = Get-Content -Raw -LiteralPath $contractPath |
        ConvertFrom-Json -Depth 100 -DateKind String
    $contractLimits = @($contract.limits) -join "`n"
    foreach ($needle in @('trust root', 'launcher', 'wrapper', 'external trusted signature/bootstrap')) {
        Assert-True ($contractLimits.Contains($needle, [StringComparison]::OrdinalIgnoreCase)) `
            "Contract omits the bootstrap limitation marker '$needle'."
    }
    $limitationText = [IO.File]::ReadAllText(
        (Join-Path $packageRoot 'PACKAGE-LIMITATIONS.txt'), [Text.UTF8Encoding]::new($false, $true))
    foreach ($needle in @('raiz de confianca', 'launcher', 'wrapper', 'bootstrap externo',
            'nao elimina')) {
        Assert-True ($limitationText.Contains($needle, [StringComparison]::OrdinalIgnoreCase)) `
            "Package limitations omit the bootstrap marker '$needle'."
    }
}

Test-Case 'package-v2 modules expose only the closed public API and v1 bytes remain untouched' {
    $core = Import-Module $modulePath -Force -PassThru
    $native = Import-Module $nativeModulePath -Force -PassThru
    $smoke = Import-Module $smokeModulePath -Force -PassThru
    Assert-Equal (ConvertTo-OrdinalList @($core.ExportedFunctions.Keys)) `
        (ConvertTo-OrdinalList @('Assert-M0QtShellCompositePackageReceipt', 'Invoke-M0QtShellCompositePackage')) `
        'Core exports drifted.'
    Assert-Equal (ConvertTo-OrdinalList @($native.ExportedFunctions.Keys)) `
        'Invoke-M0QtShellCompositePackageNative' 'Native exports drifted.'
    Assert-Equal (ConvertTo-OrdinalList @($smoke.ExportedFunctions.Keys)) `
        'Test-M0QtShellCompositePackageSmoke' 'Smoke exports drifted.'
    foreach ($relative in $v1Paths) {
        $changes = @(& git -C $repository diff -- $relative)
        Assert-Equal $LASTEXITCODE 0 "Could not inspect historical v1 member $relative."
        Assert-Equal $changes.Count 0 "Historical v1 bytes drifted: $relative"
    }
}

Write-Host "Tests: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -ne 0) { exit 1 }
