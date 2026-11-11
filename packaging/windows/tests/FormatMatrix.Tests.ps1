#Requires -Version 7.0
[CmdletBinding()]
param([switch] $IncludeLegacy)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exportRuntime = Import-Module (Join-Path $PSScriptRoot '../RcMusicRuntimePlan.psm1') -Force -PassThru
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testRoot = Join-Path $repository ('out/product/format-matrix-' + [guid]::NewGuid().ToString('N'))
$state = @{ Passed = 0; Failed = 0; Next = 0 }
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
function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected) {
    if ($Actual -cne $Expected) { throw "Expected '$Expected', got '$Actual'." }
}
function Assert-Rejected([scriptblock] $Body) {
    try { & $Body | Out-Null }
    catch {
        if ($_.Exception.Message -notmatch '(?i)matrix|catalog|fixture|format declaration|runtime input') {
            throw "Wrong rejection: $($_.Exception.Message)"
        }
        return
    }
    throw 'Expected rejection.'
}
function Assert-SafeRoot {
    $expected = [IO.Path]::GetFullPath((Join-Path $repository 'out/product')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not [IO.Path]::GetFullPath($testRoot).StartsWith($expected, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($testRoot) -cnotmatch '\Aformat-matrix-[0-9a-f]{32}\z') { throw 'Unsafe test root.' }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe reparse ancestor.' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Save-Matrix($Scenario) {
    [IO.File]::WriteAllText($Scenario.Path, ($Scenario.Data | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
}
function New-Scenario {
    $state.Next++
    $base = Join-Path $testRoot ('case-' + $state.Next)
    $null = New-Item -ItemType Directory -Path $base
    $scenario = [pscustomobject]@{ Path = Join-Path $base 'matrix.json'; Destination = Join-Path $base 'output'
        Data = [ordered]@{ schema_version = 2; scope = 'windows-x64-product-local-development'
            validation_status = 'passed-local'; backend = 'QtMultimedia/FFmpeg'
            runtime_manifest_sha256 = ('a' * 64); fixture_catalog_sha256 = Hash $exportCatalog
            test_source_sha256 = Hash $exportTest; result_sha256 = ('b' * 64)
            formats = @([ordered]@{ extension = 'wav'; description = 'Áudio WAV (PCM)'; fixture_ids = @('wav-pcm-mono') }) } }
    Save-Matrix $scenario
    return $scenario
}
function Read-Matrix($Scenario, [switch] $LegacyBaseline) {
    if ($null -eq $reader) { throw 'Independent reader unavailable.' }
    return & $reader -Path $Scenario.Path -RuntimeManifestHash ('a' * 64) -Destination $Scenario.Destination -LegacyBaseline:$LegacyBaseline
}
Assert-SafeRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root must be fresh.' }
$null = New-Item -ItemType Directory -Path $testRoot
$reader = $null
$module = $null
$exportJunctions = [Collections.Generic.List[string]]::new()
try {
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
    $exportRoot = Join-Path $testRoot 'source sem histórico 日本'
    $exportPackaging = Join-Path $exportRoot 'packaging/windows'
    $exportCorpus = Join-Path $exportRoot 'tests/audio/corpus'
    $exportCatalog = Join-Path $exportCorpus 'catalog.v1.json'
    $exportTest = Join-Path $exportRoot 'tests/qt/LocalAudioIntegrationTests.cpp'
    $catalogPath = Join-Path $repository 'tests/audio/corpus/catalog.v1.json'
    $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
    Copy-TestExport $repository $exportRoot @('tests/audio/corpus/catalog.v1.json',
        'tests/qt/LocalAudioIntegrationTests.cpp', 'packaging/windows/RcMusicRuntimePlan.psm1') @($catalog.fixtures)
    Test-Case 'catalog contains seven factual baseline inputs without runtime approval' {
        Assert-Equal $catalog.schema_version 1
        Assert-Equal $catalog.purpose 'synthetic-validation-inputs-not-runtime-approval'
        Assert-Equal $catalog.license 'LicenseRef-MusicPlayer-M0-Private'
        Assert-Equal ($catalog.fixtures.id -join '|') 'flac-16-stereo|m4a-aac-lc|mp3-cbr-stereo|mp3-unicode-tags|mp3-vbr-stereo|ogg-vorbis|wav-pcm-mono'
        foreach ($key in @('validation_status', 'runtime_manifest_sha256', 'result_sha256')) {
            Assert-True ($null -eq $catalog.PSObject.Properties[$key]) 'Catalog must not contain approval.'
        }
        foreach ($fixture in $catalog.fixtures) {
            Assert-Equal $fixture.classification 'baseline'
            Assert-True $fixture.relative_path.StartsWith('fixtures/valid/') 'Candidate or edge input entered baseline.'
            foreach ($capability in @('decodable', 'duration_known', 'seekable', 'play_to_end')) {
                Assert-Equal $fixture.required_capabilities.$capability $true
            }
        }
    }
    Test-Case 'catalog facts match promoted bytes and the baseline CMake pins' {
        $pins = Get-Content -LiteralPath (Join-Path $repository 'cmake/RcMusicAudioFixtures.cmake') -Raw
        $entries = @([regex]::Matches($pins, '_rc_music_require_audio_fixture\("\$\{corpus\}" "(fixtures/valid/[^"]+)" ([0-9]+)\s+"([0-9a-f]{64})"\)'))
        Assert-Equal $entries.Count $catalog.fixtures.Count
        foreach ($fixture in $catalog.fixtures) {
            $matches = @($entries | Where-Object { $_.Groups[1].Value -ceq $fixture.relative_path })
            Assert-Equal $matches.Count 1
            Assert-Equal ([long]$matches[0].Groups[2].Value) $fixture.size_bytes
            Assert-Equal $matches[0].Groups[3].Value $fixture.sha256
            Assert-Equal (Hash (Join-Path $exportCorpus $fixture.relative_path)) $fixture.sha256
            Assert-Equal (Get-Item -LiteralPath (Join-Path $exportCorpus $fixture.relative_path)).Length $fixture.size_bytes
        }
    }
    Test-Case 'the sole reader export imports from a product subset without experiments' {
        $source = Join-Path $repository 'packaging/windows/RcMusicFormatMatrix.psm1'
        Copy-Item -LiteralPath $source -Destination $exportPackaging
        $script:module = Import-Module (Join-Path $exportPackaging 'RcMusicFormatMatrix.psm1') -Force -PassThru
        Assert-Equal ($script:module.ExportedFunctions.Keys -join ',') 'Read-RcMusicFormatMatrix'
        $script:reader = $script:module.ExportedFunctions['Read-RcMusicFormatMatrix']
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
    }
    Test-Case 'product declaration returns only requested format and exact provenance without output' {
        $s = New-Scenario
        $result = Read-Matrix $s
        Assert-Equal $result.formats.Count 1
        Assert-Equal $result.formats[0].extension 'wav'
        Assert-Equal $result.formats[0].container 'WAV'
        Assert-Equal $result.formats[0].profiles[0] 'PCM signed 16-bit little-endian'
        Assert-Equal $result.sha256 (Hash $s.Path)
        Assert-Equal $result.validation.fixture_catalog_sha256 (Hash $exportCatalog)
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'MP4 and OGA aliases preserve the AAC and Vorbis fixture identities' {
        $s = New-Scenario
        $s.Data.formats = @(
            @{ extension = 'mp4'; description = 'Áudio MP4 (AAC)'; fixture_ids = @('m4a-aac-lc') },
            @{ extension = 'oga'; description = 'Áudio OGA (Vorbis)'; fixture_ids = @('ogg-vorbis') })
        Save-Matrix $s
        $result = Read-Matrix $s
        Assert-Equal ($result.formats.extension -join ',') 'mp4,oga'
        Assert-Equal ($result.formats[0].fixture_ids -join ',') 'm4a-aac-lc'
        Assert-Equal ($result.formats[1].fixture_ids -join ',') 'ogg-vorbis'
    }
    Test-Case 'schema one is rejected by default without reading a historical corpus' {
        $s = New-Scenario
        $s.Data.schema_version = 1
        $s.Data.scope = 'windows-x64-local-development'
        Save-Matrix $s
        Assert-Rejected { Read-Matrix $s }
        Assert-Equal (Test-Path -LiteralPath (Join-Path $exportRoot 'experiments')) $false
        Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
    }
    Test-Case 'stale or unvalidated declarations fail before output' {
        foreach ($field in @('runtime_manifest_sha256', 'fixture_catalog_sha256', 'test_source_sha256', 'validation_status', 'scope', 'backend')) {
            $s = New-Scenario
            $s.Data[$field] = 'invalid-or-stale'
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s }
            Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
        }
    }
    Test-Case 'schema version is an integer rather than a coercible string array float or null' {
        foreach ($change in @('string', 'array', 'empty-array', 'float', 'null')) {
            $s = New-Scenario
            switch ($change) {
                'string' { $s.Data.schema_version = '2' }
                'array' { $s.Data.schema_version = @(2, 2) }
                'empty-array' { $s.Data.schema_version = @() }
                'float' { $s.Data.schema_version = 2.0 }
                'null' { $s.Data.schema_version = $null }
            }
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s }
            Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
        }
    }
    Test-Case 'declaration strings and hashes reject arrays numbers and null values' {
        foreach ($field in @('scope', 'fixture_catalog_sha256', 'runtime_manifest_sha256', 'test_source_sha256',
                'backend', 'validation_status', 'result_sha256')) {
            foreach ($change in @('array', 'empty-array', 'number', 'null')) {
                $s = New-Scenario
                switch ($change) {
                    'array' { $s.Data[$field] = @($s.Data[$field], $s.Data[$field]) }
                    'empty-array' { $s.Data[$field] = @() }
                    'number' { $s.Data[$field] = 7 }
                    'null' { $s.Data[$field] = $null }
                }
                Save-Matrix $s
                Assert-Rejected { Read-Matrix $s }
                Assert-Equal (Test-Path -LiteralPath $s.Destination) $false
            }
        }
    }
    Test-Case 'unsafe extensions descriptions duplicate formats and non-baseline fixtures are rejected' {
        foreach ($change in @('unsafe-extension', 'unsafe-description', 'duplicate', 'opus', 'alac', 'edge', 'missing-fixture', 'empty')) {
            $s = New-Scenario
            switch ($change) {
                'unsafe-extension' { $s.Data.formats[0].extension = 'exe' }
                'unsafe-description' { $s.Data.formats[0].description = '${PAYLOAD}' }
                'duplicate' { $s.Data.formats += $s.Data.formats[0] }
                'opus' { $s.Data.formats[0].fixture_ids = @('opus-candidate') }
                'alac' { $s.Data.formats[0].fixture_ids = @('m4a-alac-candidate') }
                'edge' { $s.Data.formats[0].fixture_ids = @('wrong-extension-wav-as-mp3') }
                'missing-fixture' { $s.Data.formats[0].fixture_ids = @('not-in-corpus') }
                'empty' { $s.Data.formats = @() }
            }
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s }
        }
    }
    Test-Case 'scalar types trailing newlines wrong container and duplicate fixture IDs are rejected' {
        foreach ($change in @('description-lf', 'extension-array', 'fixture-id-array', 'result-hash-array', 'result-hash-lf', 'wrong-container', 'duplicate-id')) {
            $s = New-Scenario
            switch ($change) {
                'description-lf' { $s.Data.formats[0].description = "Áudio WAV`n" }
                'extension-array' { $s.Data.formats[0].extension = @('wav', 'mp3') }
                'fixture-id-array' { $s.Data.formats[0].fixture_ids = @(, @('wav-pcm-mono', 'mp3-cbr-stereo')) }
                'result-hash-array' { $s.Data.result_sha256 = @(('a' * 64), ('b' * 64)) }
                'result-hash-lf' { $s.Data.result_sha256 = ('b' * 64) + "`n" }
                'wrong-container' { $s.Data.formats[0].fixture_ids = @('m4a-aac-lc') }
                'duplicate-id' { $s.Data.formats[0].fixture_ids = @('wav-pcm-mono', 'wav-pcm-mono') }
            }
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s }
        }
    }
    Test-Case 'a declaration larger than 64 KiB is rejected' {
        $s = New-Scenario
        $s.Data.padding = 'x' * 65536
        Save-Matrix $s
        Assert-Rejected { Read-Matrix $s }
    }
    Test-Case 'modified promoted fixture bytes cannot retain a valid declaration' {
        $s = New-Scenario
        $target = Join-Path $exportCorpus 'fixtures/valid/wav-pcm-mono.wav'
        $original = [IO.File]::ReadAllBytes($target)
        try {
            $changed = [byte[]]$original.Clone()
            $changed[-1] = $changed[-1] -bxor 1
            [IO.File]::WriteAllBytes($target, $changed)
            Assert-Rejected { Read-Matrix $s }
        } finally { [IO.File]::WriteAllBytes($target, $original) }
    }
    Test-Case 'catalog classification capabilities and traversal cannot widen the baseline' {
        $original = [IO.File]::ReadAllBytes($exportCatalog)
        try {
            foreach ($change in @('candidate', 'capability', 'traversal')) {
                $altered = [Text.Encoding]::UTF8.GetString($original) | ConvertFrom-Json
                $fixture = @($altered.fixtures | Where-Object { $_.id -ceq 'wav-pcm-mono' })[0]
                switch ($change) {
                    'candidate' { $fixture.classification = 'candidate' }
                    'capability' { $fixture.required_capabilities.seekable = $false }
                    'traversal' { $fixture.relative_path = '../foreign.wav' }
                }
                [IO.File]::WriteAllText($exportCatalog, ($altered | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
                $s = New-Scenario
                Assert-Rejected { Read-Matrix $s }
            }
        } finally { [IO.File]::WriteAllBytes($exportCatalog, $original) }
    }
    if ($IncludeLegacy) {
        Test-Case 'legacy schema one requires explicit compatibility and keeps the old corpus anchor' {
            $legacyPath = Join-Path $exportRoot 'experiments/m0/shared/audio-corpus/manifest.v1.json'
            $null = New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($legacyPath))
            Copy-Item -LiteralPath (Join-Path $repository 'experiments/m0/shared/audio-corpus/manifest.v1.json') -Destination $legacyPath
            $s = New-Scenario
            $s.Data.schema_version = 1
            $s.Data.scope = 'windows-x64-local-development'
            $s.Data.Remove('fixture_catalog_sha256')
            $s.Data.corpus_manifest_sha256 = Hash $legacyPath
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s }
            $result = Read-Matrix $s -LegacyBaseline
            Assert-Equal $result.formats[0].extension 'wav'
            Assert-Equal $result.validation.corpus_manifest_sha256 (Hash $legacyPath)
            $s.Data.schema_version = '1'
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s -LegacyBaseline }
            $s.Data.schema_version = 1
            $s.Data.corpus_manifest_sha256 = 'c' * 64
            Save-Matrix $s
            Assert-Rejected { Read-Matrix $s -LegacyBaseline }
        }
    }
} finally {
    if ($null -ne $module) { Remove-Module $module -Force }
    Assert-SafeRoot
    foreach ($junction in $exportJunctions) { [IO.Directory]::Delete($junction) }
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($testRoot)) -Recurse -Force
}
Write-Host "Format matrix: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
