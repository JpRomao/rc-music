$modulePath = Join-Path $RepositoryRoot 'experiments\m0\audio-corpus\M0.AudioCorpus.psm1'
$schemaRoot = Join-Path $RepositoryRoot 'experiments\m0\audio-corpus\schemas'
$corpusRoot = Join-Path $RepositoryRoot 'experiments\m0\shared\audio-corpus'
$generator = Join-Path $RepositoryRoot 'scripts\m0\New-M0AudioCorpus.ps1'
$codecGenerator = Join-Path $RepositoryRoot 'scripts\m0\New-M0AudioCodecFixtures.ps1'

Import-Module $modulePath -Force -ErrorAction Stop

function Get-TestRelativeFiles {
    param([Parameter(Mandatory)] [string] $Root)

    [string[]] $files = @(Get-ChildItem -LiteralPath $Root -Recurse -File | ForEach-Object {
        [IO.Path]::GetRelativePath($Root, $_.FullName).Replace('\', '/')
    })
    [Array]::Sort($files, [StringComparer]::Ordinal)
    return $files
}

function Copy-TestCorpus {
    param([Parameter(Mandatory)] [string] $Name)

    $destination = Join-Path $TestTempRoot $Name
    Copy-Item -LiteralPath $corpusRoot -Destination $destination -Recurse
    return $destination
}

function Update-TestManifestSidecar {
    param([Parameter(Mandatory)] [string] $Root)

    $manifestPath = Join-Path $Root 'manifest.v1.json'
    $hash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(
        (Join-Path $Root 'manifest.v1.sha256'),
        "$hash  manifest.v1.json`n",
        [Text.UTF8Encoding]::new($false)
    )
}

function Assert-ByteSequenceEqual {
    param(
        [Parameter(Mandatory)] [byte[]] $Actual,
        [Parameter(Mandatory)] [byte[]] $Expected,
        [string] $Message = 'Byte sequences differ.'
    )

    Assert-Equal $Actual.Length $Expected.Length $Message
    for ($index = 0; $index -lt $Expected.Length; $index++) {
        if ($Actual[$index] -ne $Expected[$index]) {
            throw [InvalidOperationException]::new("$Message Difference at byte $index.")
        }
    }
}

Test-Case 'The authenticated corpus satisfies its public contract' {
    $result = Assert-M0AudioCorpus -CorpusRoot $corpusRoot -SchemaRoot $schemaRoot
    Assert-Equal $result.corpus_id 'M0-AUDIO-CORPUS-1'
    Assert-Equal $result.fixture_count 12
    Assert-Equal $result.valid_fixture_count 10
    Assert-Equal $result.hostile_fixture_count 2
    Assert-Equal $result.catalog_status 'partial'
    Assert-True ($result.manifest_sha256 -match '^[a-f0-9]{64}$')
}

Test-Case 'The corpus includes the real P0 codecs and records Opus and ALAC as candidates' {
    $manifest = Get-Content -LiteralPath (Join-Path $corpusRoot 'manifest.v1.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json -Depth 100
    [string[]] $fixtureIds = @($manifest.fixtures | ForEach-Object { [string]$_.id })
    [string[]] $requiredCodecFixtureIds = @(
        'flac-16-stereo'
        'm4a-aac-lc'
        'm4a-alac-candidate'
        'mp3-cbr-stereo'
        'mp3-unicode-tags'
        'mp3-vbr-stereo'
        'ogg-vorbis'
        'opus-candidate'
    )
    foreach ($fixtureId in $requiredCodecFixtureIds) {
        Assert-True ($fixtureIds -ccontains $fixtureId) "Missing real codec fixture '$fixtureId'."
    }
    Assert-Equal $manifest.completion.catalog_status 'partial'
    Assert-True (-not $manifest.completion.published_format_support) 'Codec bytes alone must not publish format support.'
}

Test-Case 'The codec probe cannot mislabel a fixture even when outer hashes are refreshed' {
    $tampered = Copy-TestCorpus 'mislabelled-codec-probe'
    $probePath = Join-Path $tampered 'codec-probe.v1.json'
    $probeText = [IO.File]::ReadAllText($probePath, [Text.UTF8Encoding]::new($false))
    $probeText = $probeText.Replace('"codec_name": "mp3"', '"codec_name": "aac"')
    [IO.File]::WriteAllText($probePath, $probeText, [Text.UTF8Encoding]::new($false))

    $manifestPath = Join-Path $tampered 'manifest.v1.json'
    $manifestText = [IO.File]::ReadAllText($manifestPath, [Text.UTF8Encoding]::new($false))
    $manifestText = $manifestText.Replace(
        '32e92e09a352d675a7cbd4f89dac05c885ea98cecc72b823877bfb76fe4254e1',
        (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash.ToLowerInvariant()
    )
    [IO.File]::WriteAllText($manifestPath, $manifestText, [Text.UTF8Encoding]::new($false))
    Update-TestManifestSidecar $tampered

    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $tampered -SchemaRoot $schemaRoot } `
        'codec recipe/probe/manifest'
}

Test-Case 'The codec recipe cannot change encoder arguments even when all outer hashes are refreshed' {
    $tampered = Copy-TestCorpus 'misdescribed-codec-recipe'
    $recipePath = Join-Path $tampered 'codec-recipe.v1.json'
    $recipeText = [IO.File]::ReadAllText($recipePath, [Text.UTF8Encoding]::new($false))
    $recipeText = $recipeText.Replace('"aac_low"', '"aac_main"')
    [IO.File]::WriteAllText($recipePath, $recipeText, [Text.UTF8Encoding]::new($false))
    $recipeSize = (Get-Item -LiteralPath $recipePath).Length
    $recipeHash = (Get-FileHash -LiteralPath $recipePath -Algorithm SHA256).Hash.ToLowerInvariant()

    $probePath = Join-Path $tampered 'codec-probe.v1.json'
    $probeText = [IO.File]::ReadAllText($probePath, [Text.UTF8Encoding]::new($false))
    $probeText = $probeText.Replace('"size_bytes": 7810', "`"size_bytes`": $recipeSize")
    $probeText = $probeText.Replace(
        'd3ac52181be002560fbe750d999943ceb5b62e25c203be9eb5863a50474bb884',
        $recipeHash
    )
    [IO.File]::WriteAllText($probePath, $probeText, [Text.UTF8Encoding]::new($false))
    $probeSize = (Get-Item -LiteralPath $probePath).Length
    $probeHash = (Get-FileHash -LiteralPath $probePath -Algorithm SHA256).Hash.ToLowerInvariant()

    $manifestPath = Join-Path $tampered 'manifest.v1.json'
    $manifestText = [IO.File]::ReadAllText($manifestPath, [Text.UTF8Encoding]::new($false))
    $manifestText = $manifestText.Replace('"size_bytes": 7810', "`"size_bytes`": $recipeSize")
    $manifestText = $manifestText.Replace(
        'd3ac52181be002560fbe750d999943ceb5b62e25c203be9eb5863a50474bb884',
        $recipeHash
    )
    $manifestText = $manifestText.Replace('"size_bytes": 9103', "`"size_bytes`": $probeSize")
    $manifestText = $manifestText.Replace(
        '32e92e09a352d675a7cbd4f89dac05c885ea98cecc72b823877bfb76fe4254e1',
        $probeHash
    )
    [IO.File]::WriteAllText($manifestPath, $manifestText, [Text.UTF8Encoding]::new($false))
    Update-TestManifestSidecar $tampered

    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $tampered -SchemaRoot $schemaRoot } `
        'codec recipe arguments'
}

Test-Case 'The integer WAV foundation recipe is byte-identical across roots and matches its committed subset' {
    $first = Join-Path $RepositoryRoot "out\m0\audio-corpus-test-runs\$([Guid]::NewGuid().ToString('N'))-first"
    $second = Join-Path $RepositoryRoot "out\m0\audio-corpus-test-runs\$([Guid]::NewGuid().ToString('N'))-second"
    try {
        & $generator -DestinationRoot $first | Out-Null
        & $generator -DestinationRoot $second | Out-Null
        [string[]] $foundationFiles = @(
            'fixtures/edge/wrong-extension-wav-as-mp3.mp3'
            'fixtures/hostile/empty.bin'
            'fixtures/hostile/truncated-wav-pcm-mono.wav'
            'fixtures/valid/wav-pcm-mono.wav'
        )
        Assert-SequenceEqual (Get-TestRelativeFiles $first) (Get-TestRelativeFiles $second)
        foreach ($relativePath in $foundationFiles) {
            $expectedHash = (Get-FileHash -LiteralPath (Join-Path $corpusRoot $relativePath) -Algorithm SHA256).Hash
            Assert-Equal (Get-FileHash -LiteralPath (Join-Path $first $relativePath) -Algorithm SHA256).Hash $expectedHash "First generated hash differs for $relativePath."
            Assert-Equal (Get-FileHash -LiteralPath (Join-Path $second $relativePath) -Algorithm SHA256).Hash $expectedHash "Second generated hash differs for $relativePath."
        }
    }
    finally {
        foreach ($path in @($first, $second)) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
        }
    }
}

Test-Case 'The committed WAV has exact PCM properties and the edge fixtures derive from it' {
    $wavPath = Join-Path $corpusRoot 'fixtures\valid\wav-pcm-mono.wav'
    $wav = Read-M0WavePcmContract -LiteralPath $wavPath
    Assert-Equal $wav.audio_format 1
    Assert-Equal $wav.channels 1
    Assert-Equal $wav.sample_rate_hz 48000
    Assert-Equal $wav.bits_per_sample 16
    Assert-Equal $wav.sample_count 48000
    Assert-Equal $wav.duration_ms 1000

    $wavBytes = [IO.File]::ReadAllBytes($wavPath)
    $wrongExtension = [IO.File]::ReadAllBytes((Join-Path $corpusRoot 'fixtures\edge\wrong-extension-wav-as-mp3.mp3'))
    Assert-ByteSequenceEqual $wrongExtension $wavBytes 'Wrong-extension fixture must preserve the exact WAV bytes.'
    $truncated = [IO.File]::ReadAllBytes((Join-Path $corpusRoot 'fixtures\hostile\truncated-wav-pcm-mono.wav'))
    Assert-Equal $truncated.Length 40
    Assert-ByteSequenceEqual $truncated ([byte[]]$wavBytes[0..39]) 'Truncated fixture must be the exact 40-byte prefix.'
    Assert-Equal (Get-Item -LiteralPath (Join-Path $corpusRoot 'fixtures\hostile\empty.bin')).Length 0
}

Test-Case 'A one-byte fixture tamper and an extra leaf are rejected' {
    $tampered = Copy-TestCorpus 'tampered'
    $fixturePath = Join-Path $tampered 'fixtures\valid\wav-pcm-mono.wav'
    $bytes = [IO.File]::ReadAllBytes($fixturePath)
    $bytes[100] = $bytes[100] -bxor 1
    [IO.File]::WriteAllBytes($fixturePath, $bytes)
    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $tampered -SchemaRoot $schemaRoot } 'hash mismatch'

    $extra = Copy-TestCorpus 'extra-leaf'
    [IO.File]::WriteAllText((Join-Path $extra 'unexpected.txt'), "unexpected`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $extra -SchemaRoot $schemaRoot } 'inventory'
}

Test-Case 'Duplicate JSON properties and unsafe manifest paths are rejected before use' {
    $duplicate = Copy-TestCorpus 'duplicate-json'
    $manifestPath = Join-Path $duplicate 'manifest.v1.json'
    $text = [IO.File]::ReadAllText($manifestPath, [Text.UTF8Encoding]::new($false))
    $text = $text.Replace('"schema_version": 1,', '"schema_version": 1,' + "`n  `"schema_version`": 1,")
    [IO.File]::WriteAllText($manifestPath, $text, [Text.UTF8Encoding]::new($false))
    Update-TestManifestSidecar $duplicate
    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $duplicate -SchemaRoot $schemaRoot } 'duplicate'

    $unsafe = Copy-TestCorpus 'unsafe-path'
    $manifestPath = Join-Path $unsafe 'manifest.v1.json'
    $text = [IO.File]::ReadAllText($manifestPath, [Text.UTF8Encoding]::new($false))
    $text = $text.Replace('fixtures/hostile/empty.bin', '../empty.bin')
    [IO.File]::WriteAllText($manifestPath, $text, [Text.UTF8Encoding]::new($false))
    Update-TestManifestSidecar $unsafe
    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $unsafe -SchemaRoot $schemaRoot } 'schema|unsafe path'
}

Test-Case 'The standalone schemas reject empty, dot, and trailing path segments' {
    $manifestSchema = Join-Path $schemaRoot 'audio-corpus-manifest.schema.json'
    $manifestText = Get-Content -LiteralPath (Join-Path $corpusRoot 'manifest.v1.json') -Raw -Encoding UTF8
    foreach ($unsafePath in @('fixtures//empty.bin', 'fixtures/./empty.bin', 'fixtures/../empty.bin', 'fixtures/hostile/')) {
        $mutated = $manifestText.Replace('fixtures/hostile/empty.bin', $unsafePath)
        Assert-True (-not (Test-Json -Json $mutated -SchemaFile $manifestSchema -ErrorAction SilentlyContinue)) "Manifest schema accepted '$unsafePath'."
    }

    $recipeSchema = Join-Path $schemaRoot 'audio-corpus-recipe.schema.json'
    $recipeText = Get-Content -LiteralPath (Join-Path $corpusRoot 'recipe.v1.json') -Raw -Encoding UTF8
    foreach ($unsafePath in @('fixtures//empty.bin', 'fixtures/./empty.bin', 'fixtures/../empty.bin', 'fixtures/hostile/')) {
        $mutated = $recipeText.Replace('fixtures/hostile/empty.bin', $unsafePath)
        Assert-True (-not (Test-Json -Json $mutated -SchemaFile $recipeSchema -ErrorAction SilentlyContinue)) "Recipe schema accepted '$unsafePath'."
    }
}

Test-Case 'A linked corpus root is rejected' {
    $linkPath = Join-Path $TestTempRoot 'linked-corpus'
    $linkType = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
    New-Item -ItemType $linkType -Path $linkPath -Target $corpusRoot | Out-Null
    Assert-Throws { Assert-M0AudioCorpus -CorpusRoot $linkPath -SchemaRoot $schemaRoot } 'link|reparse'
}

Test-Case 'The generator rejects a linked destination before writing any bytes' {
    $target = Join-Path $TestTempRoot 'generator-link-target'
    $linkPath = Join-Path $TestTempRoot 'generator-linked-destination'
    New-Item -ItemType Directory -Path $target | Out-Null
    $linkType = if ($IsWindows) { 'Junction' } else { 'SymbolicLink' }
    New-Item -ItemType $linkType -Path $linkPath -Target $target | Out-Null

    Assert-Throws { & $generator -DestinationRoot $linkPath | Out-Null } 'link|reparse'
    Assert-Equal @(Get-ChildItem -LiteralPath $target -Force).Count 0 'The linked target must remain untouched.'
}

Test-Case 'The generator exposes no in-place committed corpus rewrite capability' {
    $command = Get-Command -Name $generator -CommandType ExternalScript
    Assert-True (-not $command.Parameters.ContainsKey('AllowCommittedCorpusWrite')) 'Committed corpus rewrite must not be a generator capability.'
    Assert-Throws { & $generator -DestinationRoot $corpusRoot | Out-Null } 'out/m0'
}

Test-Case 'The generator refuses overwrite and destinations outside out/m0' {
    $existing = Join-Path $TestTempRoot 'existing'
    New-Item -ItemType Directory -Path $existing | Out-Null
    [IO.File]::WriteAllText((Join-Path $existing 'sentinel.txt'), "keep`n", [Text.UTF8Encoding]::new($false))
    Assert-Throws { & $generator -DestinationRoot $existing | Out-Null } 'M0ACG001|out/m0'

    $outside = Join-Path $RepositoryRoot 'experiments\m0\audio-corpus-generator-probe'
    try {
        Assert-Throws { & $generator -DestinationRoot $outside | Out-Null } 'out/m0|committed corpus'
    }
    finally {
        if (Test-Path -LiteralPath $outside) { Remove-Item -LiteralPath $outside -Recurse -Force }
    }
}

Test-Case 'The codec generator requires explicit tools and rejects unsafe stages before execution' {
    $command = Get-Command -Name $codecGenerator -CommandType ExternalScript
    Assert-True $command.Parameters.ContainsKey('FfmpegPath')
    Assert-True $command.Parameters.ContainsKey('FfprobePath')
    Assert-True $command.Parameters.ContainsKey('DestinationRoot')
    Assert-True (-not $command.Parameters.ContainsKey('AllowCommittedCorpusWrite'))

    $fakeFfmpeg = Join-Path $TestTempRoot 'fake-ffmpeg.exe'
    $fakeFfprobe = Join-Path $TestTempRoot 'fake-ffprobe.exe'
    [IO.File]::WriteAllBytes($fakeFfmpeg, [byte[]]@(0))
    [IO.File]::WriteAllBytes($fakeFfprobe, [byte[]]@(0))
    $outside = Join-Path $RepositoryRoot 'experiments\m0\unsafe-codec-stage'
    Assert-Throws {
        & $codecGenerator -FfmpegPath $fakeFfmpeg -FfprobePath $fakeFfprobe `
            -DestinationRoot $outside | Out-Null
    } 'out/m0'
    Assert-True (-not (Test-Path -LiteralPath $outside)) 'Unsafe codec stage must not be created.'
}
