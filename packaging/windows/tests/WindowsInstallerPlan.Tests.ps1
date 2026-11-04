#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exportRuntime = Import-Module (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Force -PassThru
Import-Module (Join-Path $PSScriptRoot '../RcMusicInstallerPlan.psm1') -Force
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$runId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $repository ('out/product/test-runs/rc-music-installer-plan/' + $runId)

function Assert-SafeTestRoot {
    $expectedRoot = Join-Path $repository ('out/product/test-runs/rc-music-installer-plan/' + $runId)
    if ($runId -cnotmatch '\A[0-9a-f]{32}\z' -or
        -not [IO.Path]::IsPathFullyQualified($testRoot) -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath($expectedRoot)) {
        throw 'Unsafe installer-plan test root identity'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try {
            $attributes = [IO.File]::GetAttributes($ancestor)
            if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
                ($attributes -band [IO.FileAttributes]::Directory) -eq 0) {
                throw 'Unsafe installer-plan test root ancestor'
            }
        } catch {
            $cause = $_.Exception.GetBaseException()
            if ($cause -isnot [IO.FileNotFoundException] -and $cause -isnot [IO.DirectoryNotFoundException]) { throw }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Installer-plan test GUID must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0 }
$recipeNames = @('New-RcMusicInstaller.ps1', 'RcMusicInstallerPlan.psm1', 'RcMusicRuntimePlan.psm1',
    'RcMusicFormatMatrix.psm1', 'RcMusicInstaller.nsi', 'RcMusicInstallerFiles.nsh',
    'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json')
function Assert-True([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Assert-Rejected([scriptblock] $Body) {
    $rejected = $false
    try { & $Body | Out-Null } catch { $rejected = $true }
    Assert-True $rejected 'Expected rejection'
}
function Copy-TestExport([string] $SourceRoot, [string] $Destination,
        [object[]] $RelativePaths, [object[]] $CatalogEntries = @()) {
    $paths = @($RelativePaths)
    foreach ($entry in $CatalogEntries) {
        if ($entry.relative_path -isnot [string]) { throw 'Unsafe catalog relative path type' }
        $paths += 'tests/audio/corpus/' + $entry.relative_path
    }
    $files = @($paths | ForEach-Object {
        [pscustomobject]@{ source = (Join-Path $SourceRoot $_); relative_path = $_ }
    })
    # Plan the entire inventory before mkdir/copy, then revalidate paths, reparse points and hashes.
    & $exportRuntime {
        param($Files, $SourceRoot, $Destination)
        $plan = New-RcMusicRuntimePlan -Files $Files -AllowedSourceRoots @($SourceRoot) -Destination $Destination
        Copy-RcMusicRuntimePlan -Plan $plan | Out-Null
    } $files $SourceRoot $Destination
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Save-Manifest($Fixture) {
    [IO.File]::WriteAllText($Fixture.ManifestPath, ($Fixture.Manifest | ConvertTo-Json -Depth 8))
}
function New-Fixture {
    $fixtureRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + ' espaço $literal 日本')
    $app = Join-Path $fixtureRoot 'app'
    $null = New-Item -ItemType Directory -Path (Join-Path $app 'qml/QtQuick/Controls')
    $entries = foreach ($relative in @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt', 'qml/QtQuick/Controls/qmldir')) {
        $path = Join-Path $app $relative
        [IO.File]::WriteAllText($path, 'Synthetic planning input: ' + $relative)
        [pscustomobject]@{ relative_path = $relative; source = 'irrelevant origin'; size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    $fixture = [pscustomobject]@{ App = $app; Output = (Join-Path $fixtureRoot 'installer'); ManifestPath = (Join-Path $fixtureRoot 'runtime-manifest.json')
        Manifest = [pscustomobject]@{ schema_version = 1; name = 'RC Music'; architecture = 'x64'; build_testing = $false
            source_commit = ('1' * 40); recipe_dirty = $false; distribution = 'private-local-development-only'; files = @($entries) } }
    Save-Manifest $fixture
    return $fixture
}
function Set-FixtureEdition($Fixture, $Edition, $Requirement) {
    Add-Member -InputObject $Fixture.Manifest -MemberType NoteProperty -Name edition -Value $Edition -Force
    Add-Member -InputObject $Fixture.Manifest -MemberType NoteProperty -Name browser_runtime_requirement -Value $Requirement -Force
    Save-Manifest $Fixture
}
function New-CompleteFixture {
    $fixture = New-Fixture
    foreach ($relative in @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt')) {
        $path = Join-Path $fixture.App $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic packaging notice fixture: ' + $relative)
        $fixture.Manifest.files += [pscustomobject]@{ relative_path = $relative; source = 'synthetic fixture only'
            size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }
    Set-FixtureEdition $fixture 'CompleteExperimental' 'existing-webview2-evergreen-x64'
    return $fixture
}
function New-FormatMatrix($Fixture) {
    $catalogPath = Join-Path $repository 'tests/audio/corpus/catalog.v1.json'
    $testPath = Join-Path $repository 'tests/qt/LocalAudioIntegrationTests.cpp'
    $matrix = [ordered]@{ schema_version = 2; scope = 'windows-x64-product-local-development'; validation_status = 'passed-local'
        runtime_manifest_sha256 = (Get-FileHash -LiteralPath $Fixture.ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        fixture_catalog_sha256 = (Get-FileHash -LiteralPath $catalogPath -Algorithm SHA256).Hash.ToLowerInvariant()
        test_source_sha256 = (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash.ToLowerInvariant()
        result_sha256 = ('a' * 64); backend = 'QtMultimedia/FFmpeg'
        formats = @([ordered]@{ extension = 'wav'; description = 'Áudio WAV (PCM)'; fixture_ids = @('wav-pcm-mono') }) }
    $path = Join-Path ([IO.Path]::GetDirectoryName($Fixture.ManifestPath)) 'formats.json'
    [IO.File]::WriteAllText($path, ($matrix | ConvertTo-Json -Depth 8))
    return [pscustomobject]@{ Path = $path; Data = $matrix }
}
function New-Export([switch] $WithCatalog) {
    $exportRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + ' export 日本')
    $relativePaths = @($recipeNames | ForEach-Object { 'packaging/windows/' + $_ })
    $catalogPaths = @()
    if ($WithCatalog) {
        $relativePaths += @('tests/audio/corpus/catalog.v1.json', 'tests/qt/LocalAudioIntegrationTests.cpp')
        $catalog = Get-Content -LiteralPath (Join-Path $repository 'tests/audio/corpus/catalog.v1.json') -Raw | ConvertFrom-Json
        $catalogPaths = @($catalog.fixtures)
    }
    Copy-TestExport $repository $exportRoot $relativePaths $catalogPaths
    $foreignDirectory = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + ' unrelated cwd')
    $null = New-Item -ItemType Directory -Path $foreignDirectory
    $module = Import-Module (Join-Path $exportRoot 'packaging/windows/RcMusicInstallerPlan.psm1') -Force -PassThru -NoClobber
    return [pscustomobject]@{ Root = $exportRoot; Module = $module; ForeignDirectory = $foreignDirectory }
}
function Invoke-ExportedPlan($Export, $Fixture, [string] $Matrix) {
    Push-Location -LiteralPath $Export.ForeignDirectory
    try {
        return & $Export.Module {
            param($Runtime, $MatrixPath)
            New-RcMusicInstallerPlan -RuntimeDirectory $Runtime -OutputDirectory 'out/product/installer-output' -FormatMatrix $MatrixPath
        } $Fixture.App $Matrix
    } finally { Pop-Location }
}
$exportJunctions = [Collections.Generic.List[string]]::new()
try {
    Test-Case 'legacy manifests default to canonical Lite with no browser prerequisite' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($p.edition -ceq 'Lite' -and $p.browser_runtime_requirement -ceq 'none') 'Legacy runtime did not resolve to Lite/none'
        Assert-True ($p.config.Contains('!define RCMUSIC_EDITION "Lite"')) 'Lite edition define missing'
        Assert-True ($p.config.Contains('!define RCMUSIC_REQUIRES_WEBVIEW2 0')) 'Lite unexpectedly requires a browser runtime'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Edition planning created output'
    }
    Test-Case 'explicit Lite metadata and legacy metadata share the same payload identity' {
        $f = New-Fixture
        $legacy = New-RcMusicInstallerPlan $f.App $f.Output
        Set-FixtureEdition $f 'Lite' 'none'
        $explicit = New-RcMusicInstallerPlan $f.App $f.Output -Edition lite
        Assert-True ($explicit.edition -ceq 'Lite' -and $explicit.browser_runtime_requirement -ceq 'none') 'Requested edition was not canonicalized'
        Assert-True ($legacy.payload_id -ceq $explicit.payload_id) 'Equivalent Lite manifests changed package identity'
        $f.Manifest.PSObject.Properties.Remove('browser_runtime_requirement')
        Save-Manifest $f
        $compatible = New-RcMusicInstallerPlan $f.App $f.Output -Edition Lite
        Assert-True ($compatible.payload_id -ceq $legacy.payload_id) 'Optional legacy Lite requirement became mandatory'
    }
    Test-Case 'CompleteExperimental requires explicit matching metadata and emits its prerequisite' {
        $f = New-CompleteFixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output -Edition completeexperimental
        Assert-True ($p.edition -ceq 'CompleteExperimental') 'Complete edition was not canonicalized'
        Assert-True ($p.browser_runtime_requirement -ceq 'existing-webview2-evergreen-x64') 'Exact existing-runtime prerequisite lost'
        Assert-True ($p.config.Contains('!define RCMUSIC_EDITION "CompleteExperimental"')) 'Complete edition define missing'
        Assert-True ($p.config.Contains('!define RCMUSIC_REQUIRES_WEBVIEW2 1')) 'Complete browser prerequisite not emitted'
        Assert-True ($p.config.Contains('!define RCMUSIC_INSTALL_ROOT "$LOCALAPPDATA\Programs\RC Music"')) 'Complete silently selected another installation identity'
        Assert-True ($p.config.Contains('!define RCMUSIC_REGISTRY_BASE "Software"')) 'Complete changed per-user registration root'
        Assert-True (-not $p.associations_enabled) 'Complete inferred unverified associations'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Complete planning created output'
    }
    Test-Case 'identical verified file inventories cannot reuse Lite and Complete package identities' {
        $f = New-CompleteFixture
        $complete = New-RcMusicInstallerPlan $f.App $f.Output -Edition CompleteExperimental
        Set-FixtureEdition $f 'Lite' 'none'
        $lite = New-RcMusicInstallerPlan $f.App $f.Output -Edition Lite
        Assert-True (($lite.files | ConvertTo-Json -Compress) -ceq ($complete.files | ConvertTo-Json -Compress)) 'Fixture inventories were not identical'
        Assert-True (($lite.recipe_hashes | ConvertTo-Json -Compress) -ceq ($complete.recipe_hashes | ConvertTo-Json -Compress)) 'Fixture recipes changed during identity comparison'
        Assert-True ($lite.payload_id -cne $complete.payload_id) 'Edition/prerequisite metadata was omitted from payload identity'
    }
    foreach ($edition in @('CompleteExperimental', 'Unknown', 'lite', '', $null, 7, @('Lite'), @{ edition = 'Lite' })) {
        Test-Case ('Lite rejects unknown mismatching or malformed manifest edition: ' + ($edition | ConvertTo-Json -Compress)) {
            $f = New-Fixture
            Set-FixtureEdition $f $edition 'none'
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected edition created output'
        }
    }
    foreach ($requirement in @('existing-webview2-evergreen-x64', 'unknown', 'None', "none`n", '', $null, 7, @('none'))) {
        Test-Case ('Lite rejects a noncanonical or malformed browser prerequisite: ' + ($requirement | ConvertTo-Json -Compress)) {
            $f = New-Fixture
            Set-FixtureEdition $f 'Lite' $requirement
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition Lite }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected Lite prerequisite created output'
        }
    }
    foreach ($change in @('legacy-edition', 'Lite-edition', 'unknown-edition', 'missing-requirement', 'none', 'unknown-requirement',
            'requirement-case', 'null-requirement', 'number-requirement', 'array-requirement')) {
        Test-Case "CompleteExperimental rejects incompatible runtime metadata: $change" {
            $f = New-CompleteFixture
            switch ($change) {
                'legacy-edition' { $f.Manifest.PSObject.Properties.Remove('edition') }
                'Lite-edition' { $f.Manifest.edition = 'Lite' }
                'unknown-edition' { $f.Manifest.edition = 'Unknown' }
                'missing-requirement' { $f.Manifest.PSObject.Properties.Remove('browser_runtime_requirement') }
                'none' { $f.Manifest.browser_runtime_requirement = 'none' }
                'unknown-requirement' { $f.Manifest.browser_runtime_requirement = 'download-evergreen' }
                'requirement-case' { $f.Manifest.browser_runtime_requirement = 'Existing-WebView2-Evergreen-x64' }
                'null-requirement' { $f.Manifest.browser_runtime_requirement = $null }
                'number-requirement' { $f.Manifest.browser_runtime_requirement = 7 }
                'array-requirement' { $f.Manifest.browser_runtime_requirement = @('existing-webview2-evergreen-x64') }
            }
            Save-Manifest $f
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition CompleteExperimental }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected Complete metadata created output'
        }
    }
    foreach ($required in @('notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt', 'WEBVIEW2-DEVELOPMENT.txt')) {
        Test-Case "CompleteExperimental requires its verified inventory notice: $required" {
            $f = New-CompleteFixture
            $f.Manifest.files = @($f.Manifest.files | Where-Object { $_.relative_path -cne $required })
            Save-Manifest $f
            Assert-True (Test-Path -LiteralPath (Join-Path $f.App $required)) 'Fixture must retain the unlisted file physically'
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition CompleteExperimental }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Missing inventory notice created output'
        }
    }
    Test-Case 'CompleteExperimental notices remain covered by runtime hash verification' {
        $f = New-CompleteFixture
        [IO.File]::AppendAllText((Join-Path $f.App 'notices/WebView2-SDK-LICENSE.txt'), ' changed synthetic fixture')
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition CompleteExperimental }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Changed notice created output'
    }
    Test-Case 'unsupported requested editions are rejected without output' {
        $f = New-Fixture
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition Unknown }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Unsupported requested edition created output'
    }
    foreach ($change in @('null', 'number', 'array', 'traversal', 'absolute', 'ads', 'missing', 'duplicate')) {
        Test-Case "export validates the whole catalog before copying: $change" {
            $caseRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + ' export-safety')
            $source = Join-Path $caseRoot 'source'
            $corpus = Join-Path $source 'tests/audio/corpus'
            $destination = Join-Path $caseRoot 'export'
            $null = [IO.Directory]::CreateDirectory($corpus)
            [IO.File]::WriteAllText((Join-Path $source 'first.txt'), 'first valid entry')
            [IO.File]::WriteAllText((Join-Path $corpus 'valid.txt'), 'valid catalog entry')
            [IO.File]::WriteAllText((Join-Path $corpus '7'), 'numeric name must not coerce a JSON number')
            $sentinel = Join-Path $caseRoot 'sentinel.txt'
            [IO.File]::WriteAllText($sentinel, 'owned sentinel outside both export and source')
            $before = (Get-FileHash -LiteralPath $sentinel).Hash
            $bad = $null
            switch ($change) {
                'number' { $bad = 7 }
                'array' { $bad = @('valid.txt') }
                'traversal' { $bad = '../../../../sentinel.txt' }
                'absolute' { $bad = $sentinel }
                'ads' { $bad = 'valid.txt:stream' }
                'missing' { $bad = 'missing.txt' }
                'duplicate' { $bad = 'valid.txt' }
            }
            $entries = [object[]]::new(2)
            $entries[0] = [pscustomobject]@{ relative_path = 'valid.txt' }
            $entries[1] = [pscustomobject]@{ relative_path = $bad }
            $rejected = $false
            try { Copy-TestExport $source $destination @('first.txt') $entries }
            catch { $rejected = $true }
            Assert-True $rejected 'Unsafe catalog export was accepted'
            Assert-True (-not (Test-Path -LiteralPath $destination)) 'Invalid late entry caused a partial export'
            Assert-True ((Get-FileHash -LiteralPath $sentinel).Hash -ceq $before) 'Export changed the owned sentinel'
        }
    }
    foreach ($side in @('source', 'destination')) {
        Test-Case "export rejects a $side reparse ancestor before copying" {
            $caseRoot = Join-Path $testRoot ([Guid]::NewGuid().ToString('N') + ' reparse-safety')
            $source = Join-Path $caseRoot 'source'
            $foreign = Join-Path $caseRoot 'owned-junction-target'
            $link = Join-Path $caseRoot 'link'
            $null = [IO.Directory]::CreateDirectory($source)
            $null = [IO.Directory]::CreateDirectory($foreign)
            [IO.File]::WriteAllText((Join-Path $source 'valid.txt'), 'synthetic source')
            [IO.File]::WriteAllText((Join-Path $foreign 'valid.txt'), 'owned junction sentinel')
            $before = (Get-FileHash -LiteralPath (Join-Path $foreign 'valid.txt')).Hash
            foreach ($path in @($link, $foreign)) {
                Assert-True ([IO.Path]::GetFullPath($path).StartsWith(
                    [IO.Path]::GetFullPath($testRoot) + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) 'Unsafe test junction target'
            }
            $null = New-Item -ItemType Junction -Path $link -Target $foreign
            $exportJunctions.Add($link)
            $destination = Join-Path $caseRoot 'export'
            if ($side -eq 'source') { $source = $link }
            else { $destination = Join-Path $link 'export' }
            $rejected = $false
            try { Copy-TestExport $source $destination @('valid.txt') }
            catch { $rejected = $true }
            Assert-True $rejected 'Reparse export was accepted'
            Assert-True (-not (Test-Path -LiteralPath $destination)) 'Reparse export wrote payload files'
            Assert-True ((Get-FileHash -LiteralPath (Join-Path $foreign 'valid.txt')).Hash -ceq $before) 'Junction sentinel changed'
        }
    }
    Test-Case 'explicit verified local matrix enables only its declared associations' {
        $f = New-Fixture
        $m = New-FormatMatrix $f
        $p = New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path
        Assert-True $p.associations_enabled 'Explicit local matrix was ignored'
        Assert-True ($p.formats.Count -eq 1 -and $p.formats[0].extension -ceq 'wav') 'An undeclared format was inferred'
        Assert-True ($p.formats[0].container -ceq 'WAV' -and $p.formats[0].profiles[0] -ceq 'PCM signed 16-bit little-endian') 'Container/profile evidence lost'
        Assert-True ($p.config.Contains('!define RCMUSIC_ASSOCIATIONS_ENABLED 1')) 'Associations not enabled in config'
        Assert-True ($p.config.Contains('!insertmacro ${CALLBACK} "wav" "Áudio WAV (PCM)"')) 'Explicit format was not emitted'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Planning created output'
    }
    Test-Case 'different declared matrices change the package identity and format signature' {
        $f = New-Fixture
        $m = New-FormatMatrix $f
        $off = New-RcMusicInstallerPlan $f.App $f.Output
        $on = New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path
        Assert-True ($off.payload_id -cne $on.payload_id) 'Format declaration reused the old uninstaller ID'
        Assert-True ($off.format_signature -cne $on.format_signature) 'Format declaration reused the empty signature'
    }
    Test-Case 'stale or unvalidated format declarations fail before creating output' {
        foreach ($field in @('runtime_manifest_sha256', 'fixture_catalog_sha256', 'test_source_sha256', 'validation_status', 'scope', 'backend')) {
            $f = New-Fixture
            $m = New-FormatMatrix $f
            $m.Data[$field] = 'invalid-or-stale'
            [IO.File]::WriteAllText($m.Path, ($m.Data | ConvertTo-Json -Depth 8))
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected matrix created output'
        }
    }
    Test-Case 'unsafe extension descriptions duplicates and candidate codecs are rejected' {
        foreach ($change in @('unsafe-extension', 'unsafe-description', 'duplicate', 'candidate', 'missing-fixture', 'empty')) {
            $f = New-Fixture
            $m = New-FormatMatrix $f
            switch ($change) {
                'unsafe-extension' { $m.Data.formats[0].extension = 'exe' }
                'unsafe-description' { $m.Data.formats[0].description = '${PAYLOAD}' }
                'duplicate' { $m.Data.formats += $m.Data.formats[0] }
                'candidate' { $m.Data.formats[0].fixture_ids = @('opus-candidate') }
                'missing-fixture' { $m.Data.formats[0].fixture_ids = @('not-in-corpus') }
                'empty' { $m.Data.formats = @() }
            }
            [IO.File]::WriteAllText($m.Path, ($m.Data | ConvertTo-Json -Depth 8))
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path }
        }
    }
    Test-Case 'matrix scalar types and trailing newlines fail before output' {
        foreach ($change in @('description-lf', 'extension-array', 'fixture-id-array', 'result-hash-array', 'wrong-container')) {
            $f = New-Fixture
            $m = New-FormatMatrix $f
            switch ($change) {
                'description-lf' { $m.Data.formats[0].description = "Áudio WAV`n" }
                'extension-array' { $m.Data.formats[0].extension = @('wav', 'mp3') }
                'fixture-id-array' { $m.Data.formats[0].fixture_ids = @(, @('wav-pcm-mono', 'mp3-cbr-stereo')) }
                'result-hash-array' { $m.Data.result_sha256 = @(('a' * 64), ('b' * 64)) }
                'wrong-container' { $m.Data.formats[0].fixture_ids = @('m4a-aac-lc') }
            }
            [IO.File]::WriteAllText($m.Path, ($m.Data | ConvertTo-Json -Depth 8))
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Invalid scalar produced output'
        }
    }
    Test-Case 'installer-only corrections produce a new package identity' {
        $before = Get-RcMusicInstallerIdentity -InventoryJson '[]' -RecipeHashes @(@{ path = 'template.nsi'; sha256 = ('a' * 64) })
        $after = Get-RcMusicInstallerIdentity -InventoryJson '[]' -RecipeHashes @(@{ path = 'template.nsi'; sha256 = ('b' * 64) })
        Assert-True ($before -cne $after) 'Installer-only fix reused the obsolete uninstaller identity'
    }
    Test-Case 'every installer implementation and helper is hashed through the single product inventory' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        $promotedPath = 'packaging/windows/RcMusicRuntimePlan.psm1'
        $promoted = @($p.recipe_hashes | Where-Object { $_.path -ceq $promotedPath })
        Assert-True ($promoted.Count -eq 1) 'Promoted runtime implementation is absent from the installer recipe'
        $expectedHash = (Get-FileHash -LiteralPath (Join-Path $repository $promotedPath) -Algorithm SHA256).Hash.ToLowerInvariant()
        Assert-True ($promoted[0].sha256 -ceq $expectedHash) 'Recipe did not hash the actual promoted implementation'
        Assert-True ($p.recipe_hashes.Count -eq $recipeNames.Count) 'Unexpected recipe shape or duplicate source'
        Assert-True (($p.recipe_hashes | ConvertTo-Json -Compress) -ceq (Get-RcMusicInstallerRecipeHashes | ConvertTo-Json -Compress)) 'Plan bypassed the shared recipe inventory'
        foreach ($name in $recipeNames) {
            $path = 'packaging/windows/' + $name
            $entry = @($p.recipe_hashes | Where-Object { $_.path -ceq $path })
            Assert-True ($entry.Count -eq 1) ('Product receipt key missing: ' + $path)
            $hash = (Get-FileHash -LiteralPath (Join-Path $repository $path) -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-True ($entry[0].sha256 -ceq $hash) ('Product receipt source changed: ' + $path)
        }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Recipe inspection created output'
    }
    Test-Case 'a runtime implementation-only correction changes the real package identity' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        $promotedPath = 'packaging/windows/RcMusicRuntimePlan.psm1'
        Assert-True ($p.recipe_hashes.path -ccontains $promotedPath) 'Runtime implementation has no package identity input'
        $inventory = @($p.files | ForEach-Object {
            [ordered]@{ path = $_.relative_path; size = $_.size_bytes; sha256 = $_.sha256 }
        }) | ConvertTo-Json -Depth 4 -Compress
        $packageIdentity = [ordered]@{ inventory = $inventory; format_signature = $p.format_signature
            format_matrix_sha256 = $null; edition = $p.edition
            browser_runtime_requirement = $p.browser_runtime_requirement } | ConvertTo-Json -Compress
        $before = Get-RcMusicInstallerIdentity -InventoryJson $packageIdentity -RecipeHashes $p.recipe_hashes
        Assert-True ($before -ceq $p.payload_id) 'The real package identity ignored its reported recipe'
        $changed = @($p.recipe_hashes | ForEach-Object {
            $hash = $_.sha256
            if ($_.path -ceq $promotedPath) { $hash = $(if ($hash -ceq ('a' * 64)) { 'b' * 64 } else { 'a' * 64 }) }
            [pscustomobject]@{ path = $_.path; sha256 = $hash }
        })
        $after = Get-RcMusicInstallerIdentity -InventoryJson $packageIdentity -RecipeHashes $changed
        Assert-True ($before -cne $after) 'Runtime-only correction reused the obsolete installer identity'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Identity calculation created output'
    }
    Test-Case 'plans a private per-user installer without creating its destination' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan -RuntimeDirectory $f.App -OutputDirectory $f.Output
        Assert-True ($p.payload_id -cmatch '^[0-9a-f]{64}$') 'Invalid immutable payload ID'
        Assert-True ($p.recipe_hashes.path -ccontains 'packaging/windows/RcMusicInstallerFiles.nsh') 'Native guard missing from package identity'
        Assert-True ($p.files.Count -eq 4) 'Manifest inventory lost'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Planning wrote output'
        Assert-True ($p.config -clike '*!define RCMUSIC_ASSOCIATIONS_ENABLED 0*') 'Unvalidated associations enabled'
        Assert-True ($p.config -clike '*!define RCMUSIC_REGISTRY_BASE "Software"*') 'Wrong per-user base'
        Assert-True ($p.config.Contains('!define RCMUSIC_INSTALL_ROOT "$LOCALAPPDATA\Programs\RC Music"')) 'Wrong installation root'
        Assert-True ($p.config.Contains('!macro RCMUSIC_PAYLOAD_EXTRACT')) 'No actual payload extraction'
    }
    Test-Case 'production config explicitly chooses the current user Start menu' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($p.config.Contains('!define RCMUSIC_SHORTCUT_ROOT "$SMPROGRAMS"')) 'Start menu destination is missing from the production recipe'
        Assert-True (-not $p.config.Contains('$DESKTOP') -and -not $p.config.Contains('$SMSTARTUP')) 'Recipe adds an unsolicited desktop or startup shortcut'
    }
    Test-Case 'payload identity is independent of manifest order and input location' {
        $a = New-Fixture
        $b = New-Fixture
        [array]::Reverse($b.Manifest.files)
        Save-Manifest $b
        $pa = New-RcMusicInstallerPlan $a.App $a.Output
        $pb = New-RcMusicInstallerPlan $b.App $b.Output
        Assert-True ($pa.payload_id -ceq $pb.payload_id) 'Same payload has different identity'
    }
    Test-Case 'changed bytes require a different payload identity' {
        $f = New-Fixture
        $before = New-RcMusicInstallerPlan $f.App $f.Output
        $exe = Join-Path $f.App 'RCMusic.exe'
        [IO.File]::AppendAllText($exe, ' new version')
        $f.Manifest.files[0].sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
        $f.Manifest.files[0].size_bytes = (Get-Item -LiteralPath $exe).Length
        Save-Manifest $f
        $after = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($before.payload_id -cne $after.payload_id) 'Changed payload reused ID'
    }
    Test-Case 'escapes literal NSIS dollars and emits explicit deepest-first directory cleanup' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($p.config.Contains('${U+24}literal')) 'Compile-time source dollar is not deferred once'
        Assert-True ($p.config.Contains('${U+24}{U+24}literal')) 'Output define dollar is not deferred twice'
        Assert-True (-not $p.config.Contains('$$literal')) 'Runtime-only dollar escaping leaked into a compile-time path'
        Assert-True (-not $p.config.Contains('File /r')) 'Recursive extraction is not an explicit inventory'
        Assert-True ($p.config.Contains('!define RCMUSIC_DIR_0000 "qml\QtQuick\Controls"')) 'Deepest directory token lost its literal'
        Assert-True ($p.config.Contains('!define RCMUSIC_DIR_0001 "qml\QtQuick"')) 'Parent directory token lost its literal'
        $deep = $p.config.IndexOf('!insertmacro ${CALLBACK} "RCMUSIC_DIR_0000"')
        $parent = $p.config.IndexOf('!insertmacro ${CALLBACK} "RCMUSIC_DIR_0001"')
        Assert-True ($deep -ge 0 -and $parent -gt $deep) 'Cleanup directories are not deepest first'
    }
    Test-Case 'rejects changed files before any installer output' {
        $f = New-Fixture
        [IO.File]::AppendAllText((Join-Path $f.App 'RCMusic.exe'), ' tampered')
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Failure created output'
    }
    Test-Case 'checks extraction errors at each file and directory before continuing' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        $errorChecks = [regex]::Matches($p.config, '(?m)^    IfErrors rcmusic_install_failed$').Count
        Assert-True ($errorChecks -eq 2 * $p.files.Count) 'Extraction can lose an earlier error before registration'
    }
    Test-Case 'rejects missing runtime files and mismatching sizes' {
        $f = New-Fixture
        $f.Manifest.files[0].size_bytes++
        Save-Manifest $f
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
        $f.Manifest.files[0].relative_path = 'absent.exe'
        Save-Manifest $f
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
    }
    Test-Case 'rejects traversal and duplicate destination names from a manifest' {
        foreach ($relative in @('../RCMusic.exe', 'QML/qtquick/controls/QMLDIR')) {
            $f = New-Fixture
            $f.Manifest.files[0].relative_path = $relative
            Save-Manifest $f
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
        }
    }
    Test-Case 'reserves installer-owned filenames against payload collisions' {
        foreach ($relative in @('Uninstall.exe', '.rcmusic-install.ini')) {
            $f = New-Fixture
            Copy-Item -LiteralPath (Join-Path $f.App 'RCMusic.exe') -Destination (Join-Path $f.App $relative)
            $f.Manifest.files += [pscustomobject]@{ relative_path = $relative; source = 'irrelevant';
                size_bytes = $f.Manifest.files[0].size_bytes; sha256 = $f.Manifest.files[0].sha256 }
            Save-Manifest $f
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
        }
    }
    Test-Case 'rejects test builds dirty recipes wrong architectures and unsupported manifests' {
        foreach ($field in @(@('build_testing', $true), @('recipe_dirty', $true), @('architecture', 'x86'),
                @('schema_version', 2), @('distribution', 'public-release'), @('source_commit', 'unknown'))) {
            $f = New-Fixture
            $f.Manifest.($field[0]) = $field[1]
            Save-Manifest $f
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output }
        }
    }
    Test-Case 'rejects existing or out-of-scope output directories' {
        $f = New-Fixture
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $testRoot }
        Assert-Rejected { New-RcMusicInstallerPlan $f.App (Join-Path $repository 'not-an-output') }
        Assert-Rejected { New-RcMusicInstallerPlan $f.App (Join-Path $repository 'out/m0') }
        Assert-Rejected { New-RcMusicInstallerPlan $f.App (Join-Path $repository 'out/product') }
        Assert-Rejected { New-RcMusicInstallerPlan $f.App (Join-Path $repository 'out/product-other/installer') }
    }
    Test-Case 'the exported OFF planner needs no experiments catalog or caller working directory' {
        $f = New-Fixture
        $export = New-Export
        $p = Invoke-ExportedPlan $export $f
        Assert-True (-not $p.associations_enabled -and $null -eq $p.format_matrix) 'OFF planning inferred a matrix'
        Assert-True ($p.output -ceq (Join-Path $export.Root 'out/product/installer-output')) 'Export used the original repository or caller directory'
        Assert-True (-not (Test-Path -LiteralPath $p.output)) 'Export created installer output'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $export.Root 'experiments'))) 'Export depends on historical sources'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $export.Root 'tests'))) 'OFF planning required matrix inputs'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $export.ForeignDirectory 'out'))) 'Export wrote into the caller directory'
    }
    Test-Case 'the exported ON planner preserves the explicit matrix and its existing runtime evidence' {
        $f = New-Fixture
        $m = New-FormatMatrix $f
        $before = @($f.ManifestPath, $m.Path | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash })
        $export = New-Export -WithCatalog
        $p = Invoke-ExportedPlan $export $f $m.Path
        Assert-True ($p.associations_enabled -and $p.formats.Count -eq 1 -and $p.formats[0].extension -ceq 'wav') 'Export lost the declared format'
        Assert-True ($p.source_commit -ceq $f.Manifest.source_commit) 'Export reanchored the runtime commit'
        Assert-True ($p.runtime_manifest_sha256 -ceq $before[0].ToLowerInvariant()) 'Export replaced runtime evidence'
        Assert-True ($p.format_matrix.sha256 -ceq $before[1].ToLowerInvariant()) 'Export replaced matrix evidence'
        $after = @($f.ManifestPath, $m.Path | ForEach-Object { (Get-FileHash -LiteralPath $_).Hash })
        Assert-True (($before -join '|') -ceq ($after -join '|')) 'Planning mutated its existing evidence'
        Assert-True (-not (Test-Path -LiteralPath $p.output)) 'Export created installer output'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $export.Root 'experiments'))) 'ON export depends on historical sources'
    }
    Test-Case 'changing only an exported helper changes its real recipe hash and package identity' {
        $f = New-Fixture
        $export = New-Export
        $before = Invoke-ExportedPlan $export $f
        $relative = 'packaging/windows/RcMusicRuntimePlan.psm1'
        $originalHash = (Get-FileHash -LiteralPath (Join-Path $repository $relative)).Hash
        $copy = Join-Path $export.Root $relative
        [IO.File]::AppendAllText($copy, "`n# Synthetic exported-copy mutation.`n", [Text.UTF8Encoding]::new($false))
        $after = Invoke-ExportedPlan $export $f
        $changed = @($after.recipe_hashes | Where-Object {
            $entry = $_
            $previous = @($before.recipe_hashes | Where-Object { $_.path -ceq $entry.path })
            $previous.Count -ne 1 -or $previous[0].sha256 -cne $entry.sha256
        })
        Assert-True ($changed.Count -eq 1 -and $changed[0].path -ceq $relative) 'Export hashes were not resolved from their actual copies'
        Assert-True ($changed[0].sha256 -ceq (Get-FileHash -LiteralPath $copy).Hash.ToLowerInvariant()) 'Export helper hash is stale'
        Assert-True ($before.payload_id -cne $after.payload_id) 'Changed exported implementation reused the package identity'
        Assert-True ((Get-FileHash -LiteralPath (Join-Path $repository $relative)).Hash -ceq $originalHash) 'Test mutated the original helper'
    }
    Test-Case 'a product matrix cannot silently downgrade to the historical schema' {
        $f = New-Fixture
        $m = New-FormatMatrix $f
        $m.Data.schema_version = 1
        $m.Data.scope = 'windows-x64-local-development'
        $m.Data.corpus_manifest_sha256 = $m.Data.fixture_catalog_sha256
        $m.Data.Remove('fixture_catalog_sha256')
        [IO.File]::WriteAllText($m.Path, ($m.Data | ConvertTo-Json -Depth 8))
        Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected legacy schema created product output'
    }
    Test-Case 'the product planner preserves the explicitly scoped historical output directory' {
        $f = New-Fixture
        $output = Join-Path $repository ('out/m0/installer-compat-' + $runId)
        $p = New-RcMusicInstallerPlan $f.App $output
        Assert-True ($p.output -ceq $output -and -not (Test-Path -LiteralPath $output)) 'Historical output compatibility changed'
    }
    if ($IncludeLegacy) {
        Test-Case 'the historical wrapper alone selects schema v1 without reapproving its evidence' {
            $f = New-Fixture
            $m = New-FormatMatrix $f
            $m.Data.schema_version = 1
            $m.Data.scope = 'windows-x64-local-development'
            $m.Data.corpus_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $repository 'experiments/m0/shared/audio-corpus/manifest.v1.json')).Hash.ToLowerInvariant()
            $m.Data.Remove('fixture_catalog_sha256')
            [IO.File]::WriteAllText($m.Path, ($m.Data | ConvertTo-Json -Depth 8))
            $before = (Get-FileHash -LiteralPath $m.Path).Hash
            Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path }
            $direct = New-RcMusicInstallerPlan $f.App $f.Output -FormatMatrix $m.Path -LegacyBaseline
            $legacy = Import-Module (Join-Path $repository 'experiments/m0/candidates/qt/windows-installer/WindowsInstallerPlan.psm1') -Force -PassThru -NoClobber
            $wrapped = & $legacy { param($Runtime, $Output, $Matrix)
                New-RcMusicInstallerPlan $Runtime $Output -FormatMatrix $Matrix
            } $f.App $f.Output $m.Path
            Assert-True ($wrapped.payload_id -ceq $direct.payload_id) 'Wrapper changed the product recipe or identity'
            Assert-True ($wrapped.format_signature -ceq $direct.format_signature -and $wrapped.associations_enabled) 'Legacy format declaration changed'
            Assert-True ($wrapped.format_matrix.sha256 -ceq $before.ToLowerInvariant() -and (Get-FileHash -LiteralPath $m.Path).Hash -ceq $before) 'Wrapper reapproved or rewrote the historical evidence'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Legacy planning created output'
        }
    }
} finally {
    # Unlink only owned junctions before the exact fresh GUID cleanup.
    Assert-SafeTestRoot
    foreach ($junction in $exportJunctions) { [IO.Directory]::Delete($junction) }
    Remove-Item -LiteralPath $testRoot -Recurse -Force
}
Write-Host "Windows installer plan: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
