#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'RcMusicRuntimePlan.psm1') -Force

function Test-RcMusicFormatHash($Value) {
    return $Value -is [string] -and $Value -cmatch '\A[0-9a-f]{64}\z'
}

# A declaration records reviewed evidence for one runtime. This reader never
# runs codecs or manufactures approval from the factual fixture catalog.
function Read-RcMusicFormatMatrix {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $RuntimeManifestHash,
        [Parameter(Mandatory)][string] $Destination,
        [switch] $LegacyBaseline
    )
    $repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
    $path = (Resolve-Path -LiteralPath $Path).ProviderPath
    $matrixInput = New-RcMusicRuntimePlan -Files @(@{ source = $path; relative_path = 'formats.json' }) `
        -AllowedSourceRoots @([IO.Path]::GetDirectoryName($path)) -Destination $Destination
    if ($matrixInput.files[0].size_bytes -gt 65536) { throw 'Format matrix exceeds 64 KiB' }
    $matrix = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    $schema = if ($LegacyBaseline) { 1 } else { 2 }
    $scope = if ($LegacyBaseline) { 'windows-x64-local-development' } else { 'windows-x64-product-local-development' }
    # Reject the historical schema before touching any historical path. Product
    # exports work without experiments, even when a schema-one input is refused.
    if (($matrix.schema_version -isnot [int] -and $matrix.schema_version -isnot [long]) -or
        $matrix.schema_version -ne $schema -or $matrix.scope -isnot [string] -or $matrix.scope -cne $scope -or
        $matrix.validation_status -isnot [string] -or $matrix.validation_status -cne 'passed-local' -or
        $matrix.backend -isnot [string] -or $matrix.backend -cne 'QtMultimedia/FFmpeg' -or
        -not (Test-RcMusicFormatHash $RuntimeManifestHash) -or
        -not (Test-RcMusicFormatHash $matrix.runtime_manifest_sha256) -or
        $matrix.runtime_manifest_sha256 -cne $RuntimeManifestHash -or
        -not (Test-RcMusicFormatHash $matrix.test_source_sha256) -or
        -not (Test-RcMusicFormatHash $matrix.result_sha256) -or
        $matrix.formats -isnot [array] -or $matrix.formats.Count -lt 1 -or $matrix.formats.Count -gt 7) {
        throw 'Format matrix is unvalidated, stale or outside the selected local Windows baseline'
    }
    $testPath = Join-Path $repository 'tests/qt/LocalAudioIntegrationTests.cpp'
    if ($matrix.test_source_sha256 -cne (Get-FileHash -LiteralPath $testPath -Algorithm SHA256).Hash.ToLowerInvariant()) {
        throw 'Format matrix test source is stale'
    }
    if ($LegacyBaseline) {
        $catalogPath = Join-Path $repository 'experiments/m0/shared/audio-corpus/manifest.v1.json'
        $expectedCatalogHash = $matrix.corpus_manifest_sha256
    } else {
        $catalogPath = Join-Path $repository 'tests/audio/corpus/catalog.v1.json'
        $expectedCatalogHash = $matrix.fixture_catalog_sha256
    }
    $catalogInput = New-RcMusicRuntimePlan -Files @(@{ source = $catalogPath; relative_path = 'catalog.json' }) `
        -AllowedSourceRoots @([IO.Path]::GetDirectoryName($catalogPath)) -Destination $Destination
    if (-not (Test-RcMusicFormatHash $expectedCatalogHash) -or
        $catalogInput.files[0].sha256 -cne $expectedCatalogHash) { throw 'Format matrix catalog is stale' }
    $catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
    if (-not $LegacyBaseline -and ($catalog.schema_version -ne 1 -or
            $catalog.purpose -cne 'synthetic-validation-inputs-not-runtime-approval' -or
            $catalog.license -cne 'LicenseRef-MusicPlayer-M0-Private' -or
            $catalog.fixtures -isnot [array] -or $catalog.fixtures.Count -ne 7)) {
        throw 'Invalid product fixture catalog'
    }
    # Only explicitly declared aliases are eligible, never every codec sharing
    # a container. Candidate and edge inputs do not enter the basic catalog.
    $aliases = @{ mp3 = @('mp3'); m4a = @('m4a', 'mp4'); flac = @('flac'); wav = @('wav'); ogg = @('ogg', 'oga') }
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $formats = @(foreach ($format in $matrix.formats) {
        if ($format.extension -isnot [string] -or $format.extension -cnotmatch '\A(mp3|m4a|mp4|flac|wav|ogg|oga)\z' -or
            -not $seen.Add($format.extension) -or $format.description -isnot [string] -or
            $format.description -notmatch '\A[\p{L}\p{N} .()+/-]{1,96}\z' -or
            $format.fixture_ids -isnot [array] -or $format.fixture_ids.Count -lt 1 -or $format.fixture_ids.Count -gt 7) {
            throw 'Invalid or duplicate association in format matrix'
        }
        $fixtures = @(foreach ($id in $format.fixture_ids) {
            if ($id -isnot [string]) { throw 'Fixture ID must be a string' }
            $matches = @($catalog.fixtures | Where-Object { $_.id -ceq $id })
            if ($matches.Count -ne 1) { throw 'Format matrix references an unknown or duplicate fixture' }
            $fixture = $matches[0]
            if ($fixture.relative_path -isnot [string] -or
                -not $fixture.relative_path.StartsWith('fixtures/valid/', [StringComparison]::Ordinal) -or
                ($LegacyBaseline -and $fixture.expected_outcome -cne 'valid') -or
                (-not $LegacyBaseline -and $fixture.classification -cne 'baseline')) {
                throw 'Format matrix references an unknown, hostile or candidate fixture'
            }
            $capabilities = if ($LegacyBaseline) { $fixture.expected_capabilities } else { $fixture.required_capabilities }
            $sourceExtension = [IO.Path]::GetExtension($fixture.relative_path).Substring(1)
            if (-not $aliases.ContainsKey($sourceExtension) -or $aliases[$sourceExtension] -cnotcontains $format.extension -or
                -not $capabilities.decodable -or -not $capabilities.duration_known -or
                -not $capabilities.seekable -or -not $capabilities.play_to_end) {
                throw 'Format declaration does not match its tested container/capabilities'
            }
            if (-not $LegacyBaseline) {
                foreach ($capability in @('decodable', 'duration_known', 'seekable', 'play_to_end')) {
                    if ($capabilities.$capability -isnot [bool] -or -not $capabilities.$capability) {
                        throw 'Invalid required fixture capability'
                    }
                }
                $corpusRoot = Join-Path $repository 'tests/audio/corpus'
                $actual = New-RcMusicRuntimePlan -Files @(@{
                    source = Join-Path $corpusRoot $fixture.relative_path; relative_path = $fixture.relative_path
                }) -AllowedSourceRoots @($corpusRoot) -Destination $Destination
                if (-not (Test-RcMusicFormatHash $fixture.sha256) -or $fixture.size_bytes -lt 1 -or
                    $actual.files[0].sha256 -cne $fixture.sha256 -or $actual.files[0].size_bytes -ne $fixture.size_bytes) {
                    throw 'Promoted fixture bytes do not match the catalog'
                }
            }
            $fixture
        })
        $containers = @($fixtures.container | Sort-Object -Unique)
        if ($containers.Count -ne 1 -or @($format.fixture_ids | Sort-Object -Unique).Count -ne $format.fixture_ids.Count) {
            throw 'Format matrix has mixed containers or duplicate fixture references'
        }
        [pscustomobject]@{ extension = $format.extension; description = $format.description; container = $containers[0]
            profiles = @($fixtures.codec_and_profile | Sort-Object -Unique)
            fixture_ids = @($format.fixture_ids | Sort-Object -CaseSensitive) }
    })
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $matrixInput.files[0].sha256 -or
        (Get-FileHash -LiteralPath $catalogPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $catalogInput.files[0].sha256) {
        throw 'Format matrix or catalog changed during planning'
    }
    return [pscustomobject]@{ path = $path; sha256 = $matrixInput.files[0].sha256
        formats = @($formats | Sort-Object -Property extension -CaseSensitive); validation = $matrix }
}

Export-ModuleMember -Function Read-RcMusicFormatMatrix
