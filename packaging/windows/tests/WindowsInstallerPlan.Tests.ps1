#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exportRuntime = Import-Module (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Force -PassThru
Import-Module (Join-Path $PSScriptRoot '../RcMusicInstallerPlan.psm1') -Force
Import-Module (Join-Path $PSScriptRoot '../RcMusicDownloadComponentPe.psm1') -Force
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
    'RcMusicAssociationSections.nsh', 'RcMusicAssociations.nsh', 'nsis-toolchain.json',
    'RcMusicInstallerComponent.psm1', 'RcMusicDownloadComponentPe.psm1')
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
function New-DownloadFixture([string] $Edition = 'Lite') {
    $f = if ($Edition -ceq 'CompleteExperimental') { New-CompleteFixture } else { New-Fixture }
    $roles = [ordered]@{
        Bootstrap = 'RCMusicDownloadWorker.exe'; Payload = 'rcmusic-download-network.dll'
        Core = 'Qt6Core.dll'; Network = 'Qt6Network.dll'; Msvcp140 = 'msvcp140.dll'; Msvcp140_1 = 'msvcp140_1.dll'
        Vcruntime140 = 'vcruntime140.dll'; Vcruntime140_1 = 'vcruntime140_1.dll'; Plugin = 'qrcmusicschanneloffline.dll'
        Media = 'RCMusicMediaWorker.exe'
    }
    $files = @($roles.GetEnumerator() | ForEach-Object {
        $component = if ($_.Key -ceq 'Media') { 'media' } else { 'network' }
        $relative = 'download-tools/' + $component + '/' + $_.Value
        $path = Join-Path $f.App $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic optional installer input: ' + $relative)
        [pscustomobject]@{ source = $path; relative_path = $relative; component = $component; role = $_.Key
            size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
    })
    $entries = @($files | Select-Object relative_path, size_bytes, sha256)
    $reports = @($files | ForEach-Object {
        [pscustomobject]@{ relative_path = $_.relative_path; machine = 'x64'; imports = @('KERNEL32.dll') }
    })
    Add-Member -InputObject $f.Manifest -MemberType NoteProperty -Name download_component -Value ([pscustomobject]@{
        schema_version = 1; selection_default = $false; receipt_sha256 = ('a' * 64); header_sha256 = ('b' * 64)
        activation_marker = [pscustomobject]@{ relative_path = 'download-tools/installed-v1'; content = "RCMusicDownloadTools-v1`n" }
        files = $entries; pe_closure = (Get-RcMusicDownloadComponentPeClosure -Files $files -Reports $reports)
    })
    $f.Manifest.files += $entries
    Add-Member -InputObject $f -NotePropertyName ToolFiles -NotePropertyValue $files
    Save-Manifest $f
    return $f
}
function New-NativeDownloadFixture([string] $Edition = 'Lite') {
    $f = New-DownloadFixture $Edition
    $youtube = [ordered]@{
        'extractor/yt-dlp.exe' = 'Extractor'; 'javascript/node.exe' = 'Node'
        'extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js' = 'EjsCore'
        'extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js' = 'EjsLibrary'
        'notices/python-LICENSE.txt' = 'Support'; 'notices/node-LICENSE.txt' = 'Support'
        'notices/yt-dlp-LICENSE.txt' = 'Support'; 'notices/ejs-LICENSE.txt' = 'Support'
        'notices/pyinstaller-COPYING.txt' = 'Support'
    }
    foreach ($relative in $youtube.Keys) {
        $target = 'download-tools/youtube/' + $relative
        $path = Join-Path $f.App $target
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, 'Synthetic owned YouTube runtime fixture: ' + $target, [Text.UTF8Encoding]::new($false))
        $f.ToolFiles += [pscustomobject]@{ source = $path; relative_path = $target; component = 'youtube'; role = $youtube[$relative]
            size_bytes = (Get-Item -LiteralPath $path).Length; sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
    }
    $entries = @($f.ToolFiles | Select-Object relative_path, size_bytes, sha256)
    $f.Manifest.files = @($f.Manifest.files | Where-Object { -not $_.relative_path.StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase) }) + $entries
    $f.Manifest.download_component.files = $entries
    $reports = @($f.ToolFiles | Where-Object { [IO.Path]::GetExtension($_.relative_path) -in @('.exe', '.dll', '.pyd') } | ForEach-Object {
        [pscustomobject]@{ relative_path = $_.relative_path; machine = 'x64'; imports = @('KERNEL32.dll') }
    })
    $f.Manifest.download_component.pe_closure = Get-RcMusicDownloadComponentPeClosure -Files $f.ToolFiles -Reports $reports
    $channels = @(for ($index = 0; $index -lt 2; $index++) {
        $isYoutube = $index -eq 1
        $name = if ($isYoutube) { 'com.rcmusic.youtube' } else { 'com.rcmusic.media' }
        $exe = if ($isYoutube) { 'rc_music_youtube_native_host.exe' } else { 'rc_music_native_host.exe' }
        $origin = 'chrome-extension://' + $(if ($isYoutube) { 'b' * 32 } else { 'a' * 32 }) + '/'
        $description = if ($isYoutube) { 'RC Music YouTube audio extension host' } else { 'RC Music media extension host' }
        $manifestText = (ConvertTo-Json -InputObject ([ordered]@{
            name = $name; description = $description; path = $exe; type = 'stdio'; allowed_origins = @($origin)
        }) -Depth 4) + "`n"
        foreach ($row in @(
            @{ relative_path = $exe; content = ('Synthetic owned non-executable host fixture: ' + $exe) },
            @{ relative_path = ($name + '.json'); content = $manifestText }
        )) {
            $path = Join-Path $f.App $row.relative_path
            [IO.File]::WriteAllText($path, $row.content, [Text.UTF8Encoding]::new($false))
            $entry = [pscustomobject]@{ relative_path = $row.relative_path
                size_bytes = (Get-Item -LiteralPath $path).Length; sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
            $f.Manifest.files += $entry
            $f.Manifest.download_component.files += $entry
        }
        [pscustomobject]@{
            protocol = $(if ($isYoutube) { 'youtube.audio.v1' } else { 'direct.media.v1' }); host_name = $name
            origin = $origin; endpoint = "rcmusic-planner-fixture-$index"
            receipt_sha256 = ('c' * 64); configuration_sha256 = ('d' * 64)
            executable = $exe; manifest = ($name + '.json')
        }
    })
    Add-Member -InputObject $f.Manifest.download_component -NotePropertyName native_hosts -NotePropertyValue ([pscustomobject]@{
        schema_version = 1; shared_header_sha256 = ('e' * 64); channels = $channels
    })
    Save-Manifest $f
    return $f
}
function Get-ConfigMacro($Plan, [string] $Name) {
    $match = [regex]::Match($Plan.config, ('(?ms)^!macro ' + [regex]::Escape($Name) + '(?: [^\r\n]*)?\r?\n(.*?)^!macroend\r?$'))
    Assert-True $match.Success ('Missing generated macro: ' + $Name)
    return $match.Groups[1].Value
}
function Get-ConfigPathToken($Plan, [string] $Relative) {
    $match = [regex]::Match($Plan.config, ('(?m)^!define (RCMUSIC_PATH_[0-9]+) "' + [regex]::Escape($Relative.Replace('/', '\')) + '"\r?$'))
    Assert-True $match.Success ('Missing generated path token: ' + $Relative)
    return $match.Groups[1].Value
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
    foreach ($edition in @('Lite', 'CompleteExperimental')) {
        foreach ($declaration in @(
            @{ Name = 'invalid-object'; Value = [pscustomobject]@{ available = $true; files = @() } },
            @{ Name = 'null'; Value = $null },
            @{ Name = 'false'; Value = $false }
        )) {
            Test-Case "$edition refuses declared download_component even when its value is $($declaration.Name)" {
                $f = if ($edition -ceq 'Lite') { New-Fixture } else { New-CompleteFixture }
                $baseline = New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition
                Assert-True ($null -ne $baseline) 'The existing local fixture must be valid before declaration'
                Add-Member -InputObject $f.Manifest -MemberType NoteProperty -Name download_component -Value $declaration.Value
                Save-Manifest $f
                $manifestBefore = (Get-FileHash -LiteralPath $f.ManifestPath).Hash
                $appBefore = (Get-FileHash -LiteralPath (Join-Path $f.App 'RCMusic.exe')).Hash
                Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition }
                Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Unsupported declaration created installer output'
                Assert-True ((Get-FileHash -LiteralPath $f.ManifestPath).Hash -ceq $manifestBefore) 'Declaration refusal changed its manifest'
                Assert-True ((Get-FileHash -LiteralPath (Join-Path $f.App 'RCMusic.exe')).Hash -ceq $appBefore) 'Declaration refusal changed the player'
            }
        }
        foreach ($relative in @(
            'download-tools/network/RCMusicDownloadWorker.exe',
            'DOWNLOAD-TOOLS/network/RCMusicDownloadWorker.exe',
            'DoWnLoAd-ToOlS\network\RCMusicDownloadWorker.exe',
            'download-tools/installed-v1',
            'DOWNLOAD-TOOLS\installed-v1'
        )) {
            Test-Case "$edition refuses undeclared optional payload path $relative" {
                $f = if ($edition -ceq 'Lite') { New-Fixture } else { New-CompleteFixture }
                $baseline = New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition
                Assert-True ($null -ne $baseline) 'The existing local fixture must be valid before optional files'
                $path = Join-Path $f.App $relative.Replace('\', '/')
                $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
                [IO.File]::WriteAllText($path, 'Synthetic optional payload input, never executed: ' + $relative)
                $before = (Get-FileHash -LiteralPath $path).Hash
                $f.Manifest.files += [pscustomobject]@{ relative_path = $relative; source = 'owned optional fixture only'
                    size_bytes = (Get-Item -LiteralPath $path).Length; sha256 = $before.ToLowerInvariant() }
                Save-Manifest $f
                $manifestBefore = (Get-FileHash -LiteralPath $f.ManifestPath).Hash
                Assert-Rejected { New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition }
                Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Undeclared optional files created installer output'
                Assert-True ((Get-FileHash -LiteralPath $path).Hash -ceq $before) 'Optional path refusal changed its source'
                Assert-True ((Get-FileHash -LiteralPath $f.ManifestPath).Hash -ceq $manifestBefore) 'Optional path refusal changed the manifest'
            }
        }
    }
    Test-Case 'a local-only plan keeps its constant payload identity and disables optional installer macros' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($null -eq $p.download_component -and $p.generated_files.Count -eq 0) 'Local-only plan materialized optional state'
        Assert-True ($p.config.Contains('!define RCMUSIC_PAYLOAD_ID "' + $p.payload_id + '"')) 'Local-only compile-time identity changed'
        Assert-True ($p.config.Contains('!define RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE 0')) 'Local-only plan does not explicitly disable component selection'
        foreach ($name in @('RCMUSIC_ACTIVATION_FILE', 'RCMUSIC_ACTIVATION_EXTRACT', 'RCMUSIC_VERIFY_ACTIVATION')) {
            Assert-True ([string]::IsNullOrWhiteSpace((Get-ConfigMacro $p $name))) ('Local-only activation macro is not inert: ' + $name)
        }
    }
    foreach ($edition in @('Lite', 'CompleteExperimental')) {
        Test-Case "$edition plans distinct base and selected-download identities without writing a marker" {
            $f = New-DownloadFixture $edition
            $before = (Get-FileHash -LiteralPath $f.ManifestPath).Hash
            $p = New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition
            Assert-True ($p.download_component.available -eq $true) 'Valid complete component metadata was refused'
            Assert-True ($p.payload_ids.base -cmatch '\A[0-9a-f]{64}\z' -and $p.payload_ids.downloads -cmatch '\A[0-9a-f]{64}\z') 'Variant identities must remain opaque SHA256 values'
            Assert-True ($p.payload_id -ceq $p.payload_ids.base -and $p.payload_ids.base -cne $p.payload_ids.downloads) 'Base and full selection collide'
            Assert-True ($p.config.Contains('!define RCMUSIC_BASE_PAYLOAD_ID "' + $p.payload_ids.base + '"')) 'Base variant ID missing'
            Assert-True ($p.config.Contains('!define RCMUSIC_DOWNLOAD_PAYLOAD_ID "' + $p.payload_ids.downloads + '"')) 'Full variant ID missing'
            Assert-True ($p.config.Contains('!define RCMUSIC_PAYLOAD_ID "$RcPayloadId"')) 'Optional installer did not use the runtime-selected variant ID'
            Assert-True ($p.config.Contains('!define RCMUSIC_DOWNLOAD_COMPONENT_AVAILABLE 1')) 'Optional availability missing'
            Assert-True ($p.files.Count -eq $f.Manifest.files.Count -and $p.download_component.files.Count -eq 10) 'Union or selected component inventory changed'
            $baseBytes = ($p.files | Where-Object { -not $_.relative_path.StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase) } | Measure-Object size_bytes -Sum).Sum
            $downloadBytes = ($p.download_component.files | Measure-Object size_bytes -Sum).Sum + 24
            Assert-True ($p.config.Contains('!define RCMUSIC_BASE_SIZE_KB ' + [long][Math]::Ceiling($baseBytes / 1024))) 'Mandatory section size includes optional files or omits base bytes'
            Assert-True ($p.config.Contains('!define RCMUSIC_DOWNLOAD_SIZE_KB ' + [long][Math]::Ceiling($downloadBytes / 1024))) 'Optional section size omits its tool inventory or activation marker'
            Assert-True ($p.files.relative_path -cnotcontains 'download-tools/installed-v1') 'Marker was forged as a runtime source file'
            Assert-True (-not (Test-Path -LiteralPath (Join-Path $f.App 'download-tools/installed-v1')) -and -not (Test-Path -LiteralPath $f.Output)) 'Planning activated or wrote the component'
            Assert-True ((Get-FileHash -LiteralPath $f.ManifestPath).Hash -ceq $before) 'Planning changed the manifest'
        }
    }
    Test-Case 'optional inventory and provenance changes invalidate both selection identities' {
        $f = New-DownloadFixture
        $first = New-RcMusicInstallerPlan $f.App $f.Output
        $f.Manifest.download_component.receipt_sha256 = 'c' * 64
        Save-Manifest $f
        $provenance = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($first.payload_ids.base -cne $provenance.payload_ids.base -and $first.payload_ids.downloads -cne $provenance.payload_ids.downloads) 'Optional provenance reused a variant identity'
        $relative = 'download-tools/media/RCMusicMediaWorker.exe'
        $path = Join-Path $f.App $relative
        [IO.File]::AppendAllText($path, ' changed synthetic bytes')
        foreach ($entry in @($f.Manifest.files) + @($f.Manifest.download_component.files)) {
            if ($entry.relative_path -ceq $relative) {
                $entry.size_bytes = (Get-Item -LiteralPath $path).Length
                $entry.sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
            }
        }
        Save-Manifest $f
        $changed = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($provenance.payload_ids.base -cne $changed.payload_ids.base -and $provenance.payload_ids.downloads -cne $changed.payload_ids.downloads) 'Optional bytes reused a variant identity'
    }
    Test-Case 'backslash component paths retain optional extraction and the canonical base selection identity' {
        $f = New-DownloadFixture
        $canonical = New-RcMusicInstallerPlan $f.App $f.Output
        foreach ($entry in @($f.Manifest.files) + @($f.Manifest.download_component.files)) {
            if ($entry.relative_path.StartsWith('download-tools/', [StringComparison]::OrdinalIgnoreCase)) {
                $entry.relative_path = $entry.relative_path.Replace('/', '\')
            }
        }
        Save-Manifest $f
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        $body = Get-ConfigMacro $p 'RCMUSIC_PAYLOAD_FILES'
        $gate = [regex]::Match($body, '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1')
        $toolToken = Get-ConfigPathToken $p 'download-tools/network/RCMusicDownloadWorker.exe'
        $baseToken = Get-ConfigPathToken $p 'RCMusic.exe'
        Assert-True ($gate.Success -and $body.IndexOf($toolToken) -gt $gate.Index -and $body.IndexOf($baseToken) -ge 0 -and $body.IndexOf($baseToken) -lt $gate.Index) 'Backslash tools became mandatory payload files'
        Assert-True ($p.payload_ids.base -ceq $canonical.payload_ids.base -and $p.payload_ids.downloads -ceq $canonical.payload_ids.downloads) 'Equivalent separators changed the base selection inventory or variant identities'
        foreach ($name in @('RCMUSIC_PAYLOAD_EXTRACT', 'RCMUSIC_PAYLOAD_DIRECTORIES')) {
            Assert-True ((Get-ConfigMacro $p $name) -cmatch '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1') ('Backslash tools escaped the optional boundary in ' + $name)
        }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Separator normalization created installer output'
    }
    foreach ($edition in @('Lite', 'CompleteExperimental')) {
        Test-Case "$edition keeps both native hosts and portable manifests exclusively in the optional file callbacks" {
            $f = New-NativeDownloadFixture $edition
            $before = (Get-FileHash -LiteralPath $f.ManifestPath).Hash
            $p = New-RcMusicInstallerPlan $f.App $f.Output -Edition $edition
            $files = Get-ConfigMacro $p 'RCMUSIC_PAYLOAD_FILES'
            $extract = Get-ConfigMacro $p 'RCMUSIC_PAYLOAD_EXTRACT'
            $fileGate = [regex]::Match($files, '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1')
            $extractGate = [regex]::Match($extract, '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1')
            Assert-True ($fileGate.Success -and $extractGate.Success) 'Native payload lost the component selection boundary'
            $baseToken = Get-ConfigPathToken $p 'RCMusic.exe'
            Assert-True ($files.IndexOf($baseToken) -ge 0 -and $files.IndexOf($baseToken) -lt $fileGate.Index) 'The local player moved into the optional selection'
            foreach ($relative in @('rc_music_native_host.exe', 'rc_music_youtube_native_host.exe', 'com.rcmusic.media.json', 'com.rcmusic.youtube.json')) {
                $token = Get-ConfigPathToken $p $relative
                Assert-True ($files.IndexOf($token) -gt $fileGate.Index -and [regex]::Matches($files, [regex]::Escape($token)).Count -eq 1) ('Verification/cleanup callback is not exclusively optional: ' + $relative)
                Assert-True ($extract.IndexOf('/oname=' + $relative) -gt $extractGate.Index) ('Host or manifest extraction is mandatory: ' + $relative)
                Assert-True ($p.download_component.files.relative_path -ccontains $relative) ('Native source missing from optional ownership: ' + $relative)
            }
            Assert-True ($p.payload_ids.base -cne $p.payload_ids.downloads -and $p.payload_id -ceq $p.payload_ids.base) 'Native selected and unselected installations share an identity'
            Assert-True ($f.Manifest.download_component.selection_default -eq $false -and -not $p.config.Contains('StrCpy $RcDownloadSelected 1')) 'Native availability enabled the default selection'
            Assert-True (-not (Test-Path -LiteralPath (Join-Path $f.App 'download-tools/installed-v1')) -and -not (Test-Path -LiteralPath $f.Output)) 'Planning installed or activated the native component'
            Assert-True ((Get-FileHash -LiteralPath $f.ManifestPath).Hash -ceq $before) 'Planning changed native metadata'
        }
    }
    Test-Case 'a portable native manifest change invalidates both installer selection identities' {
        $f = New-NativeDownloadFixture
        $first = New-RcMusicInstallerPlan $f.App $f.Output
        $channel = $f.Manifest.download_component.native_hosts.channels[1]
        $channel.origin = 'chrome-extension://' + ('c' * 32) + '/'
        $text = (ConvertTo-Json -InputObject ([ordered]@{
            name = $channel.host_name; description = 'RC Music YouTube audio extension host'; path = $channel.executable
            type = 'stdio'; allowed_origins = @($channel.origin)
        }) -Depth 4) + "`n"
        $path = Join-Path $f.App $channel.manifest
        [IO.File]::WriteAllText($path, $text, [Text.UTF8Encoding]::new($false))
        foreach ($entry in @($f.Manifest.files) + @($f.Manifest.download_component.files)) {
            if ($entry.relative_path -ceq $channel.manifest) {
                $entry.size_bytes = (Get-Item -LiteralPath $path).Length
                $entry.sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
            }
        }
        Save-Manifest $f
        $changed = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($first.payload_ids.base -cne $changed.payload_ids.base -and $first.payload_ids.downloads -cne $changed.payload_ids.downloads) 'Changed portable manifest reused a base or selected identity'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Manifest identity calculation wrote installer output'
    }
    Test-Case 'optional payload macros gate tools while leaving mandatory base files unconditional' {
        $f = New-DownloadFixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        $baseToken = Get-ConfigPathToken $p 'RCMusic.exe'
        $toolToken = Get-ConfigPathToken $p 'download-tools/network/RCMusicDownloadWorker.exe'
        $filesMacro = Get-ConfigMacro $p 'RCMUSIC_PAYLOAD_FILES'
        $gate = [regex]::Match($filesMacro, '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1')
        Assert-True ($gate.Success -and $filesMacro.IndexOf($baseToken) -lt $gate.Index -and $filesMacro.IndexOf($baseToken) -ge 0 -and $filesMacro.IndexOf($toolToken) -gt $gate.Index) 'Mandatory/player files or optional file callbacks escaped their selection boundary'
        foreach ($name in @('RCMUSIC_PAYLOAD_EXTRACT', 'RCMUSIC_PAYLOAD_DIRECTORIES')) {
            $body = Get-ConfigMacro $p $name
            Assert-True ($body -cmatch '\$\{If\}\s+\$RcDownloadSelected\s*==\s*1') ('Missing selection gate in ' + $name)
            Assert-True (-not $body.Contains('installed-v1')) 'Activation marker is part of ordinary extraction or directory data'
        }
        Assert-True (-not $filesMacro.Contains('RCMUSIC_ACTIVATION_PATH')) 'Marker must be published after ordinary payload verification'
    }
    Test-Case 'activation is a separate deterministic LF-only generated file with selected and absent verification' {
        $f = New-DownloadFixture
        $p = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($p.generated_files.Count -eq 1) 'The builder must materialize exactly one controlled marker input'
        $marker = $p.generated_files[0]
        $expected = "RCMusicDownloadTools-v1`n"
        $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::ASCII.GetBytes($expected))).ToLowerInvariant()
        Assert-True ($marker.relative_path -ceq 'download-component-marker.bin' -and $marker.content -ceq $expected -and [Text.Encoding]::ASCII.GetByteCount($marker.content) -eq 24 -and $marker.sha256 -ceq $hash) 'Generated marker bytes/path/hash do not match the fixed protocol'
        Assert-True ($p.config.Contains('!define RCMUSIC_ACTIVATION_PATH "download-tools\installed-v1"')) 'Activation path has no separate constant'
        $activation = Get-ConfigMacro $p 'RCMUSIC_ACTIVATION_FILE'
        Assert-True ($activation.Contains('RCMUSIC_ACTIVATION_PATH') -and $activation.Contains($hash) -and $activation.Contains('$RcDownloadSelected')) 'Cleanup cannot address the selected marker through its own callback'
        $extract = Get-ConfigMacro $p 'RCMUSIC_ACTIVATION_EXTRACT'
        Assert-True ($extract.Contains('download-component-marker.bin') -and $extract.Contains('/oname=installed-v1') -and $extract.Contains('$RcDownloadSelected')) 'The marker lacks a separately ordered extraction macro'
        $verify = Get-ConfigMacro $p 'RCMUSIC_VERIFY_ACTIVATION'
        Assert-True ($verify.Contains('$RcDownloadSelected') -and $verify.Contains($hash) -and $verify.Contains('HashFile') -and $verify.Contains('$RcAllowMissing') -and $verify.Contains('${Else}')) 'Activation verification must distinguish selected, absent and partial-uninstall states'
    }
    Test-Case 'ordinary local filenames resembling the optional prefix remain supported' {
        $f = New-Fixture
        $relative = 'download-tools-notes.txt'
        $path = Join-Path $f.App $relative
        [IO.File]::WriteAllText($path, 'Ordinary local runtime note')
        $f.Manifest.files += [pscustomobject]@{ relative_path = $relative; source = 'owned local fixture only'
            size_bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
        Save-Manifest $f
        $plan = New-RcMusicInstallerPlan $f.App $f.Output
        Assert-True ($plan.files.relative_path -ccontains $relative) 'The guard blocked a filename outside the reserved prefix'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Ordinary filename planning created output'
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
