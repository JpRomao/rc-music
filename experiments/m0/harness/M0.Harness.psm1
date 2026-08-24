Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:M0HarnessUtf8 = [Text.UTF8Encoding]::new($false, $true)

function Get-M0HarnessTrustPolicy {
    $runtimeFiles = @(
        'experiments/m0/harness/M0.Evidence.psm1',
        'experiments/m0/harness/M0.Fixture.psm1',
        'experiments/m0/harness/M0.Harness.psd1',
        'experiments/m0/harness/M0.Harness.psm1',
        'experiments/m0/harness/M0.Markers.psm1',
        'experiments/m0/harness/M0.Statistics.psm1',
        'experiments/m0/harness/M0.Visual.psm1',
        'scripts/m0/Test-M0Evidence.ps1',
        'scripts/m0/Test-M0Markers.ps1',
        'scripts/m0/Test-M0Visual.ps1'
    )
    $schemaFiles = @(
        'experiments/m0/protocol/schemas/batch.schema.json',
        'experiments/m0/protocol/schemas/environment-bundle.schema.json',
        'experiments/m0/protocol/schemas/environment.schema.json',
        'experiments/m0/protocol/schemas/event.schema.json',
        'experiments/m0/protocol/schemas/evidence-intent.schema.json',
        'experiments/m0/protocol/schemas/evidence-snapshot-set.schema.json',
        'experiments/m0/protocol/schemas/evidence-snapshot.schema.json',
        'experiments/m0/protocol/schemas/fixture-manifest.schema.json',
        'experiments/m0/protocol/schemas/harness-manifest.schema.json',
        'experiments/m0/protocol/schemas/marker-catalog.schema.json',
        'experiments/m0/protocol/schemas/protocol.schema.json',
        'experiments/m0/protocol/schemas/run.schema.json',
        'experiments/m0/protocol/schemas/scenario.schema.json',
        'experiments/m0/protocol/schemas/schedule.schema.json',
        'experiments/m0/protocol/schemas/shell-fixture.schema.json',
        'experiments/m0/protocol/schemas/shell-queue.schema.json',
        'experiments/m0/protocol/schemas/statistics-golden.schema.json',
        'experiments/m0/protocol/schemas/statistics.schema.json',
        'experiments/m0/protocol/schemas/visual-golden.schema.json',
        'experiments/m0/protocol/schemas/visual.schema.json'
    )
    $verificationFiles = @(
        'experiments/m0/harness/tests/Contract.Tests.ps1',
        'experiments/m0/harness/tests/Evidence.Tests.ps1',
        'experiments/m0/harness/tests/EvidenceSnapshotSet.Tests.ps1',
        'experiments/m0/harness/tests/EvidenceWriter.Tests.ps1',
        'experiments/m0/harness/tests/Fixture.Tests.ps1',
        'experiments/m0/harness/tests/HarnessManifest.Tests.ps1',
        'experiments/m0/harness/tests/Markers.Tests.ps1',
        'experiments/m0/harness/tests/Statistics.Tests.ps1',
        'experiments/m0/harness/tests/Visual.Tests.ps1',
        'scripts/m0/Invoke-M0HarnessTests.ps1',
        'scripts/m0/New-M0HarnessManifest.ps1',
        'scripts/m0/New-M0ShellFixture.ps1',
        'scripts/m0/Test-M0Fixture.ps1',
        'scripts/m0/Test-M0Layout.ps1',
        'scripts/m0/Test-M0Statistics.ps1'
    )
    $verificationSchemaFiles = @(
        'experiments/m0/protocol/schemas/candidate.schema.json'
    )
    $bootstrapFiles = @(
        'experiments/m0/harness/manifest.v1.json',
        'experiments/m0/harness/manifest.v1.sha256'
    )
    return [pscustomobject][ordered]@{
        runtime_files = $runtimeFiles
        schema_files = $schemaFiles
        verification_files = $verificationFiles
        verification_schema_files = $verificationSchemaFiles
        bootstrap_files = $bootstrapFiles
    }
}

function Assert-M0HarnessOrdinalSequence {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [IO.InvalidDataException]::new("M0H006 '$Name' coverage has $($Actual.Count) paths; expected $($Expected.Count).")
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ([string]$Actual[$index] -cne [string]$Expected[$index]) {
            throw [IO.InvalidDataException]::new("M0H006 '$Name' ordinal order/coverage differs at index ${index}: expected '$($Expected[$index])', got '$($Actual[$index])'.")
        }
    }
}

function Assert-M0HarnessRelativePath {
    param([Parameter(Mandatory)] [string] $RelativePath)

    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Contains('\') -or
        $RelativePath.Contains(':') -or
        $RelativePath.StartsWith('/', [StringComparison]::Ordinal) -or
        @($RelativePath.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count -gt 0) {
        throw [IO.InvalidDataException]::new("M0H005 unsafe manifest relative path '$RelativePath'.")
    }
}

function Assert-M0HarnessUnlinkedItem {
    param(
        [Parameter(Mandatory)] [IO.FileSystemInfo] $Item,
        [Parameter(Mandatory)] [string] $Context
    )

    $linkTypeProperty = $Item.PSObject.Properties['LinkType']
    if ($null -eq $linkTypeProperty) {
        throw [PlatformNotSupportedException]::new("M0H007 cannot prove filesystem link identity for '$Context'.")
    }
    $linkType = [string]$linkTypeProperty.Value
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
        -not [string]::IsNullOrEmpty($linkType)) {
        throw [IO.InvalidDataException]::new("M0H007 reparse points, symbolic links and hardlinks are forbidden in trusted paths: '$Context' (link type '$linkType').")
    }
}

function Get-M0HarnessLockedFile {
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [string] $RelativePath
    )

    Assert-M0HarnessRelativePath -RelativePath $RelativePath
    $path = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw [IO.FileNotFoundException]::new("M0H004 required manifest file is missing: '$RelativePath'.")
    }
    $stream = [IO.FileStream]::new($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Assert-M0HarnessUnlinkedItem -Item (Get-Item -LiteralPath $path -Force) -Context $RelativePath
        if ($stream.Length -gt 33554432) {
            throw [IO.InvalidDataException]::new("M0H004 manifest file exceeds 32 MiB: '$RelativePath'.")
        }
        $bytes = [byte[]]::new($stream.Length)
        if ($bytes.Length -gt 0) {
            $stream.ReadExactly($bytes, 0, $bytes.Length)
        }
    }
    finally {
        $stream.Dispose()
    }
    return [pscustomobject][ordered]@{
        relative_path = $RelativePath
        path = $path
        bytes = $bytes
        size_bytes = [long]$bytes.Length
        sha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    }
}

function ConvertFrom-M0HarnessUtf8 {
    param(
        [Parameter(Mandatory)] [byte[]] $Bytes,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF) {
        throw [IO.InvalidDataException]::new("M0H002 '$Name' must not contain a UTF-8 BOM.")
    }
    try {
        return $script:M0HarnessUtf8.GetString($Bytes)
    }
    catch {
        throw [IO.InvalidDataException]::new("M0H002 '$Name' is not strict UTF-8.", $_.Exception)
    }
}

function ConvertTo-M0HarnessCanonicalJson {
    param([Parameter(Mandatory)] $Value)

    return (($Value | ConvertTo-Json -Depth 100).Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n")
}

function Get-M0HarnessManagedFilePaths {
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    $paths = [Collections.Generic.List[string]]::new()
    foreach ($relativeRoot in @('experiments/m0/harness', 'scripts/m0')) {
        $scanRoot = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath $relativeRoot
        if (-not (Test-Path -LiteralPath $scanRoot -PathType Container)) {
            throw [IO.DirectoryNotFoundException]::new("M0H007 executable inventory root is missing: '$relativeRoot'.")
        }
        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($scanRoot)
        while ($pending.Count -gt 0) {
            $directory = $pending.Pop()
            foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
                Assert-M0HarnessUnlinkedItem -Item $item -Context $item.FullName
                if ($item.PSIsContainer) {
                    $pending.Push($item.FullName)
                }
                else {
                    $paths.Add([IO.Path]::GetRelativePath([IO.Path]::GetFullPath($RepositoryRoot), $item.FullName).Replace('\', '/'))
                }
            }
        }
    }
    [string[]]$ordered = @($paths)
    [Array]::Sort($ordered, [StringComparer]::Ordinal)
    return @($ordered)
}

function Get-M0HarnessSchemaPaths {
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    $schemaRoot = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments/m0/protocol/schemas'
    if (-not (Test-Path -LiteralPath $schemaRoot -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0H007 schema inventory root is missing: 'experiments/m0/protocol/schemas'.")
    }
    $paths = [Collections.Generic.List[string]]::new()
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($schemaRoot)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
            Assert-M0HarnessUnlinkedItem -Item $item -Context $item.FullName
            if ($item.PSIsContainer) {
                $pending.Push($item.FullName)
            }
            else {
                $paths.Add([IO.Path]::GetRelativePath([IO.Path]::GetFullPath($RepositoryRoot), $item.FullName).Replace('\', '/'))
            }
        }
    }
    [string[]]$ordered = @($paths)
    [Array]::Sort($ordered, [StringComparer]::Ordinal)
    return @($ordered)
}

function Get-M0HarnessTrustTreeReadLocks {
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    $locks = [Collections.Generic.List[IDisposable]]::new()
    try {
        foreach ($relativeRoot in @('docs/m0', 'experiments/m0/harness', 'experiments/m0/protocol', 'experiments/m0/shared', 'scripts/m0')) {
            $scanRoot = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath $relativeRoot
            if (-not (Test-Path -LiteralPath $scanRoot -PathType Container)) {
                throw [IO.DirectoryNotFoundException]::new("M0H007 trusted tree root is missing: '$relativeRoot'.")
            }
            $pending = [Collections.Generic.Stack[string]]::new()
            $pending.Push($scanRoot)
            while ($pending.Count -gt 0) {
                $directory = $pending.Pop()
                foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
                    Assert-M0HarnessUnlinkedItem -Item $item -Context $item.FullName
                    if ($item.PSIsContainer) {
                        $pending.Push($item.FullName)
                        continue
                    }
                    $stream = [IO.FileStream]::new($item.FullName, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
                    try {
                        Assert-M0HarnessUnlinkedItem -Item (Get-Item -LiteralPath $item.FullName -Force) -Context $item.FullName
                        $locks.Add($stream)
                        $stream = $null
                    }
                    finally {
                        if ($null -ne $stream) {
                            $stream.Dispose()
                        }
                    }
                }
            }
        }
        return @($locks)
    }
    catch {
        foreach ($lock in $locks) {
            $lock.Dispose()
        }
        throw
    }
}

function Resolve-M0ScopedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $BasePath,
        [Parameter(Mandatory)] [string] $CandidatePath
    )

    $baseFull = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/')
    if (Test-Path -LiteralPath $baseFull) {
        $baseItem = Get-Item -LiteralPath $baseFull -Force
        Assert-M0HarnessUnlinkedItem -Item $baseItem -Context $baseFull
    }
    $candidateFull = if ([IO.Path]::IsPathRooted($CandidatePath)) {
        [IO.Path]::GetFullPath($CandidatePath)
    }
    else {
        [IO.Path]::GetFullPath((Join-Path $baseFull $CandidatePath))
    }

    $rootPart = [IO.Path]::GetPathRoot($candidateFull)
    $pathWithoutRoot = $candidateFull.Substring($rootPart.Length)
    if ($pathWithoutRoot.Contains(':')) {
        throw [ArgumentException]::new("Alternate data streams and colon-bearing M0 paths are forbidden: '$CandidatePath'.")
    }

    $comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
    $prefix = $baseFull + [IO.Path]::DirectorySeparatorChar
    if ($candidateFull -ne $baseFull -and -not $candidateFull.StartsWith($prefix, $comparison)) {
        throw [ArgumentException]::new("Resolved path '$candidateFull' is outside '$baseFull'.")
    }

    $relative = [IO.Path]::GetRelativePath($baseFull, $candidateFull)
    $current = $baseFull
    foreach ($segment in $relative.Split([char[]]@([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar), [StringSplitOptions]::RemoveEmptyEntries)) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) {
            break
        }
        $item = Get-Item -LiteralPath $current -Force
        Assert-M0HarnessUnlinkedItem -Item $item -Context $current
    }

    return $candidateFull
}

function Get-M0Sha256 {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath
    )

    $path = (Resolve-Path -LiteralPath $LiteralPath -ErrorAction Stop).Path
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-M0FileHash {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedHash
    )

    $actual = Get-M0Sha256 -LiteralPath $LiteralPath
    if ($actual -ne $ExpectedHash.ToUpperInvariant()) {
        throw [IO.InvalidDataException]::new("File hash mismatch for '$(Split-Path $LiteralPath -Leaf)': expected $ExpectedHash, got $actual.")
    }
}

function Assert-M0JsonDocument {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $DocumentPath,
        [Parameter(Mandatory)] [string] $SchemaPath
    )

    $document = (Resolve-Path -LiteralPath $DocumentPath -ErrorAction Stop).Path
    $schema = (Resolve-Path -LiteralPath $SchemaPath -ErrorAction Stop).Path
    $json = Get-Content -LiteralPath $document -Raw -Encoding UTF8
    $valid = Test-Json -Json $json -SchemaFile $schema -ErrorAction SilentlyContinue
    if (-not $valid) {
        throw [IO.InvalidDataException]::new("JSON document '$(Split-Path $document -Leaf)' failed schema '$(Split-Path $schema -Leaf)'.")
    }

    return ($json | ConvertFrom-Json -Depth 100)
}

function Assert-M0Sequence {
    param(
        [Parameter(Mandatory)] [object[]] $Actual,
        [Parameter(Mandatory)] [object[]] $Expected,
        [Parameter(Mandatory)] [string] $Name
    )

    if ($Actual.Count -ne $Expected.Count) {
        throw [IO.InvalidDataException]::new("Protocol '$Name' has $($Actual.Count) values; expected $($Expected.Count).")
    }
    for ($index = 0; $index -lt $Expected.Count; $index++) {
        if ($Actual[$index] -ne $Expected[$index]) {
            throw [IO.InvalidDataException]::new("Protocol '$Name' differs at index ${index}: expected '$($Expected[$index])', got '$($Actual[$index])'.")
        }
    }
}

function Assert-M0HarnessManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [ValidatePattern('^[A-Fa-f0-9]{64}$')] [string] $ExpectedManifestSha256
    )

    $policy = Get-M0HarnessTrustPolicy
    $manifestFile = Get-M0HarnessLockedFile -RepositoryRoot $RepositoryRoot -RelativePath 'experiments/m0/harness/manifest.v1.json'
    $sidecarFile = Get-M0HarnessLockedFile -RepositoryRoot $RepositoryRoot -RelativePath 'experiments/m0/harness/manifest.v1.sha256'
    $schemaFile = Get-M0HarnessLockedFile -RepositoryRoot $RepositoryRoot -RelativePath 'experiments/m0/protocol/schemas/harness-manifest.schema.json'
    if ($manifestFile.sha256 -cne $ExpectedManifestSha256.ToLowerInvariant()) {
        throw [IO.InvalidDataException]::new("M0H003 harness manifest differs from the protocol anchor: expected $ExpectedManifestSha256, got $($manifestFile.sha256).")
    }

    $manifestText = ConvertFrom-M0HarnessUtf8 -Bytes $manifestFile.bytes -Name 'manifest.v1.json'
    $sidecarText = ConvertFrom-M0HarnessUtf8 -Bytes $sidecarFile.bytes -Name 'manifest.v1.sha256'
    $schemaText = ConvertFrom-M0HarnessUtf8 -Bytes $schemaFile.bytes -Name 'harness-manifest.schema.json'
    if ($manifestText.Contains("`r") -or -not $manifestText.EndsWith("`n", [StringComparison]::Ordinal) -or
        ($manifestText.Length -ge 2 -and $manifestText[$manifestText.Length - 2] -eq "`n")) {
        throw [IO.InvalidDataException]::new('M0H002 harness manifest must use LF with exactly one final newline.')
    }
    $expectedSidecar = "$($manifestFile.sha256)  manifest.v1.json`n"
    if ($sidecarText -cne $expectedSidecar) {
        throw [IO.InvalidDataException]::new('M0H003 harness manifest sidecar format/hash differs.')
    }
    if (-not (Test-Json -Json $manifestText -Schema $schemaText -ErrorAction SilentlyContinue)) {
        throw [IO.InvalidDataException]::new('M0H002 harness manifest failed its closed schema.')
    }
    try {
        $manifest = $manifestText | ConvertFrom-Json -Depth 100
    }
    catch {
        throw [IO.InvalidDataException]::new('M0H002 harness manifest JSON could not be parsed.', $_.Exception)
    }
    if ((ConvertTo-M0HarnessCanonicalJson -Value $manifest) -cne $manifestText) {
        throw [IO.InvalidDataException]::new('M0H002 harness manifest bytes are not canonical pretty JSON/LF.')
    }

    Assert-M0HarnessOrdinalSequence -Actual @($manifest.runtime_files.relative_path) -Expected @($policy.runtime_files) -Name 'runtime_files'
    Assert-M0HarnessOrdinalSequence -Actual @($manifest.schema_files.relative_path) -Expected @($policy.schema_files) -Name 'schema_files'
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($entry in @($manifest.runtime_files) + @($manifest.schema_files)) {
        Assert-M0HarnessRelativePath -RelativePath $entry.relative_path
        if (-not $seen.Add([string]$entry.relative_path)) {
            throw [IO.InvalidDataException]::new("M0H006 duplicate harness manifest path '$($entry.relative_path)'.")
        }
        $actual = Get-M0HarnessLockedFile -RepositoryRoot $RepositoryRoot -RelativePath $entry.relative_path
        if ([long]$entry.size_bytes -ne $actual.size_bytes -or $entry.sha256 -cne $actual.sha256) {
            throw [IO.InvalidDataException]::new("M0H004 hash/size mismatch for harness file '$($entry.relative_path)'.")
        }
    }

    [string[]]$expectedManagedFiles = @($policy.runtime_files) + @($policy.verification_files) + @($policy.bootstrap_files)
    [Array]::Sort($expectedManagedFiles, [StringComparer]::Ordinal)
    $actualManagedFiles = @(Get-M0HarnessManagedFilePaths -RepositoryRoot $RepositoryRoot)
    Assert-M0HarnessOrdinalSequence -Actual $actualManagedFiles -Expected $expectedManagedFiles -Name 'harness/script file classification'
    [string[]]$expectedSchemas = @($policy.schema_files) + @($policy.verification_schema_files)
    [Array]::Sort($expectedSchemas, [StringComparer]::Ordinal)
    $actualSchemas = @(Get-M0HarnessSchemaPaths -RepositoryRoot $RepositoryRoot)
    Assert-M0HarnessOrdinalSequence -Actual $actualSchemas -Expected $expectedSchemas -Name 'schema classification'

    return [pscustomobject][ordered]@{
        manifest_id = $manifest.manifest_id
        manifest_sha256 = $manifestFile.sha256
        runtime_file_count = $manifest.runtime_files.Count
        schema_file_count = $manifest.schema_files.Count
        verification_file_count = $policy.verification_files.Count
    }
}

function Write-M0HarnessNewFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [byte[]] $Bytes
    )

    $stream = [IO.FileStream]::new($LiteralPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try {
        $stream.Write($Bytes, 0, $Bytes.Length)
        $stream.Flush($true)
    }
    finally {
        $stream.Dispose()
    }
}

function New-M0HarnessManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [string] $DestinationRoot,
        [switch] $AllowTestDestination
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    if (-not (Test-Path -LiteralPath $root -PathType Container)) {
        throw [IO.DirectoryNotFoundException]::new("M0H008 repository root is missing: '$root'.")
    }
    $null = Resolve-M0ScopedPath -BasePath $root -CandidatePath $root
    $policy = Get-M0HarnessTrustPolicy
    [string[]]$expectedManagedFiles = @($policy.runtime_files) + @($policy.verification_files) + @($policy.bootstrap_files)
    [Array]::Sort($expectedManagedFiles, [StringComparer]::Ordinal)
    Assert-M0HarnessOrdinalSequence -Actual @(Get-M0HarnessManagedFilePaths -RepositoryRoot $root) -Expected $expectedManagedFiles -Name 'harness/script file classification'
    [string[]]$expectedSchemas = @($policy.schema_files) + @($policy.verification_schema_files)
    [Array]::Sort($expectedSchemas, [StringComparer]::Ordinal)
    Assert-M0HarnessOrdinalSequence -Actual @(Get-M0HarnessSchemaPaths -RepositoryRoot $root) -Expected $expectedSchemas -Name 'schema classification'

    if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
        $destination = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/harness'
    }
    else {
        if (-not $AllowTestDestination) {
            throw [UnauthorizedAccessException]::new('M0H008 a custom destination requires -AllowTestDestination authority.')
        }
        $testOutputRoot = Join-Path $root 'out\m0'
        if (-not (Test-Path -LiteralPath $testOutputRoot -PathType Container)) {
            New-Item -ItemType Directory -Path $testOutputRoot -Force | Out-Null
        }
        $destination = Resolve-M0ScopedPath -BasePath $testOutputRoot -CandidatePath $DestinationRoot
        if ($destination -eq [IO.Path]::GetFullPath($testOutputRoot).TrimEnd('\', '/')) {
            throw [ArgumentException]::new('M0H008 custom manifest destination must be below out/m0, not the output root itself.')
        }
    }
    if (Test-Path -LiteralPath $destination) {
        $destinationItem = Get-Item -LiteralPath $destination -Force
        if (-not $destinationItem.PSIsContainer -or ($destinationItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [IO.InvalidDataException]::new("M0H008 manifest destination is not a safe directory: '$destination'.")
        }
    }
    else {
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
    }
    $manifestPath = Join-Path $destination 'manifest.v1.json'
    $sidecarPath = Join-Path $destination 'manifest.v1.sha256'
    if ((Test-Path -LiteralPath $manifestPath) -or (Test-Path -LiteralPath $sidecarPath)) {
        throw [IO.IOException]::new('M0H008 harness manifest output already exists; silent overwrite is forbidden.')
    }

    $runtimeEntries = foreach ($relativePath in $policy.runtime_files) {
        $file = Get-M0HarnessLockedFile -RepositoryRoot $root -RelativePath $relativePath
        [ordered]@{ relative_path = $relativePath; sha256 = $file.sha256; size_bytes = $file.size_bytes }
    }
    $schemaEntries = foreach ($relativePath in $policy.schema_files) {
        $file = Get-M0HarnessLockedFile -RepositoryRoot $root -RelativePath $relativePath
        [ordered]@{ relative_path = $relativePath; sha256 = $file.sha256; size_bytes = $file.size_bytes }
    }
    $manifest = [ordered]@{
        schema_id = 'm0.harness-manifest.v1'
        schema_version = 1
        manifest_id = 'M0-HARNESS-1'
        hash_algorithm = 'sha256'
        runtime_files = @($runtimeEntries)
        schema_files = @($schemaEntries)
    }
    $manifestBytes = $script:M0HarnessUtf8.GetBytes((ConvertTo-M0HarnessCanonicalJson -Value $manifest))
    $manifestHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($manifestBytes)).ToLowerInvariant()
    $sidecarBytes = $script:M0HarnessUtf8.GetBytes("$manifestHash  manifest.v1.json`n")
    $manifestCreated = $false
    try {
        Write-M0HarnessNewFile -LiteralPath $manifestPath -Bytes $manifestBytes
        $manifestCreated = $true
        Write-M0HarnessNewFile -LiteralPath $sidecarPath -Bytes $sidecarBytes
    }
    catch {
        if ($manifestCreated -and (Test-Path -LiteralPath $manifestPath -PathType Leaf) -and -not (Test-Path -LiteralPath $sidecarPath)) {
            [IO.File]::Delete($manifestPath)
        }
        throw
    }
    return [pscustomobject][ordered]@{
        manifest_path = $manifestPath
        sidecar_path = $sidecarPath
        manifest_sha256 = $manifestHash
        runtime_file_count = $runtimeEntries.Count
        schema_file_count = $schemaEntries.Count
    }
}

function Assert-M0ProtocolContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [psobject] $Protocol,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    $expected = [ordered]@{
        protocol_id = 'M0-P1'
        environment_id = 'M0-E1'
        seed = 20260823
        planned = 22
        minimum_valid = 20
        maximum_discarded = 2
        quantile_type = 7
        bootstrap_resamples = 10000
    }

    if ($Protocol.protocol_id -ne $expected.protocol_id -or
        $Protocol.environment_id -ne $expected.environment_id -or
        [int]$Protocol.seed -ne $expected.seed -or
        [int]$Protocol.attempt_policy.planned_per_candidate_scenario -ne $expected.planned -or
        [int]$Protocol.attempt_policy.minimum_valid -ne $expected.minimum_valid -or
        [int]$Protocol.attempt_policy.maximum_discarded -ne $expected.maximum_discarded -or
        [int]$Protocol.statistics.quantile_type -ne $expected.quantile_type -or
        [int]$Protocol.statistics.bootstrap_resamples -ne $expected.bootstrap_resamples) {
        throw [IO.InvalidDataException]::new('Protocol constants differ from M0-P1.')
    }

    Assert-M0Sequence -Actual @($Protocol.candidate_codes) -Expected @('A', 'B') -Name 'candidate_codes'
    Assert-M0Sequence -Actual @($Protocol.launch_classes) -Expected @('profile-first', 'process-cold', 'warm') -Name 'launch_classes'
    Assert-M0Sequence -Actual @($Protocol.statuses) -Expected @('valid', 'discarded', 'failed', 'inconclusive') -Name 'statuses'
    Assert-M0Sequence -Actual @($Protocol.discard_reasons) -Expected @('harness_failure', 'external_os_event', 'operator_protocol_breach', 'audio_endpoint_changed') -Name 'discard_reasons'

    $contractFiles = [ordered]@{
        environment_md = 'docs\m0\environment.md'
        protocol_md = 'docs\m0\protocol.md'
        layout_md = 'docs\m0\layout.md'
    }
    foreach ($name in $contractFiles.Keys) {
        $value = $Protocol.contract_sha256.$name
        if ($value -notmatch '^[A-F0-9]{64}$') {
            throw [IO.InvalidDataException]::new("Protocol contract hash '$name' is missing or invalid.")
        }
        $contractPath = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath $contractFiles[$name]
        Assert-M0FileHash -LiteralPath $contractPath -ExpectedHash $value
    }

    $environmentBundle = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\environments\environment.v1.json'
    Assert-M0FileHash -LiteralPath $environmentBundle -ExpectedHash $Protocol.environment_bundle_sha256
    $fixtureManifest = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\shared\shell-fixture\manifest.v1.json'
    Assert-M0FileHash -LiteralPath $fixtureManifest -ExpectedHash $Protocol.fixture_manifest_sha256
    $null = Assert-M0HarnessManifest -RepositoryRoot $RepositoryRoot -ExpectedManifestSha256 $Protocol.harness_manifest_sha256
    $shellScenario = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\scenarios\shell.v1.json'
    Assert-M0FileHash -LiteralPath $shellScenario -ExpectedHash $Protocol.shell_scenario_sha256
    $statisticsDescriptor = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\statistics.v1.json'
    Assert-M0FileHash -LiteralPath $statisticsDescriptor -ExpectedHash $Protocol.statistics_descriptor_sha256
    $markerCatalog = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\markers.v1.json'
    Assert-M0FileHash -LiteralPath $markerCatalog -ExpectedHash $Protocol.marker_catalog_sha256
    $visualDescriptor = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\visual.v1.json'
    Assert-M0FileHash -LiteralPath $visualDescriptor -ExpectedHash $Protocol.visual_descriptor_sha256
}

function Assert-M0EnvironmentContract {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [psobject] $Environment,
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    if ($Environment.environment_id -ne 'M0-E1') {
        throw [IO.InvalidDataException]::new("Unexpected environment '$($Environment.environment_id)'.")
    }

    $environmentDoc = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'docs\m0\environment.md'
    Assert-M0FileHash -LiteralPath $environmentDoc -ExpectedHash $Environment.contract_sha256

    $expected = @(
        [pscustomobject]@{ component_id = 'M0-E1-WIN'; relative_path = 'windows-benchmark.v1.json' },
        [pscustomobject]@{ component_id = 'M0-E1-LINUX'; relative_path = 'linux-reference.v1.json' }
    )
    if ($Environment.components.Count -ne $expected.Count) {
        throw [IO.InvalidDataException]::new("Environment M0-E1 must contain exactly $($expected.Count) components.")
    }

    $componentRoot = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\environments'
    $componentSchema = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\schemas\environment.schema.json'
    for ($index = 0; $index -lt $expected.Count; $index++) {
        $actual = $Environment.components[$index]
        if ($actual.component_id -ne $expected[$index].component_id -or $actual.relative_path -ne $expected[$index].relative_path) {
            throw [IO.InvalidDataException]::new("Environment component at index $index does not match the frozen M0-E1 order.")
        }
        $componentPath = Resolve-M0ScopedPath -BasePath $componentRoot -CandidatePath $actual.relative_path
        Assert-M0FileHash -LiteralPath $componentPath -ExpectedHash $actual.sha256
        $component = Assert-M0JsonDocument -DocumentPath $componentPath -SchemaPath $componentSchema
        if ($component.environment_id -ne 'M0-E1' -or $component.component_id -ne $actual.component_id) {
            throw [IO.InvalidDataException]::new("Environment component '$($actual.relative_path)' has inconsistent identity.")
        }
    }
}

function Get-M0TrustedCommonInputs {
    [CmdletBinding()]
    param([Parameter(Mandatory)] [string] $RepositoryRoot)

    $root = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/')
    $trustTreeLocks = @(Get-M0HarnessTrustTreeReadLocks -RepositoryRoot $root)
    try {
        $protocolRoot = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/protocol'
        $schemaRoot = Resolve-M0ScopedPath -BasePath $protocolRoot -CandidatePath 'schemas'
        $protocolPath = Resolve-M0ScopedPath -BasePath $protocolRoot -CandidatePath 'protocol.v1.json'
        $protocol = Assert-M0JsonDocument -DocumentPath $protocolPath -SchemaPath (Join-Path $schemaRoot 'protocol.schema.json')
        $null = Assert-M0ProtocolContract -Protocol $protocol -RepositoryRoot $root

        $environmentRoot = Resolve-M0ScopedPath -BasePath $protocolRoot -CandidatePath 'environments'
        $environment = Assert-M0JsonDocument -DocumentPath (Join-Path $environmentRoot 'environment.v1.json') -SchemaPath (Join-Path $schemaRoot 'environment-bundle.schema.json')
        $null = Assert-M0EnvironmentContract -Environment $environment -RepositoryRoot $root

        $fixtureModulePath = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/harness/M0.Fixture.psm1'
        $markersModulePath = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/harness/M0.Markers.psm1'
        $statisticsModulePath = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/harness/M0.Statistics.psm1'
        $visualModulePath = Resolve-M0ScopedPath -BasePath $root -CandidatePath 'experiments/m0/harness/M0.Visual.psm1'
        Import-Module $fixtureModulePath -Force -ErrorAction Stop
        Import-Module $markersModulePath -Force -ErrorAction Stop
        Import-Module $statisticsModulePath -Force -ErrorAction Stop
        Import-Module $visualModulePath -Force -ErrorAction Stop
        $fixtureResult = Assert-M0FixtureSet -FixtureRoot (Join-Path $root 'experiments/m0/shared/shell-fixture') -SchemaRoot $schemaRoot
        $markersResult = Assert-M0MarkerArtifacts -ProtocolRoot $protocolRoot
        $statisticsResult = Assert-M0StatisticsArtifacts -ProtocolRoot $protocolRoot
        $visualResult = Assert-M0VisualArtifacts -ProtocolRoot $protocolRoot

        $scenarioPath = Resolve-M0ScopedPath -BasePath $protocolRoot -CandidatePath 'scenarios/shell.v1.json'
        $scenario = Assert-M0JsonDocument -DocumentPath $scenarioPath -SchemaPath (Join-Path $schemaRoot 'scenario.schema.json')
        if ($scenario.protocol_id -cne $protocol.protocol_id -or
            $scenario.fixture_id -cne $fixtureResult.fixture_id -or
            $scenario.scenario_set_id -cne 'M0-SHELL-1') {
            throw [IO.InvalidDataException]::new('M0H010 trusted shell scenario identity differs from protocol/fixture.')
        }
        if ($fixtureResult.fixture_sha256.ToUpperInvariant() -cne $protocol.fixture_manifest_sha256 -or
            $markersResult.catalog_sha256.ToUpperInvariant() -cne $protocol.marker_catalog_sha256 -or
            $statisticsResult.descriptor_sha256.ToUpperInvariant() -cne $protocol.statistics_descriptor_sha256 -or
            $visualResult.descriptor_sha256.ToUpperInvariant() -cne $protocol.visual_descriptor_sha256) {
            throw [IO.InvalidDataException]::new('M0H010 transitive fixture/marker/statistics/visual root hash differs from protocol anchors.')
        }

        return [pscustomobject][ordered]@{
            protocol_sha256 = (Get-M0Sha256 -LiteralPath $protocolPath).ToLowerInvariant()
            environment_sha256 = $protocol.environment_bundle_sha256.ToLowerInvariant()
            fixture_sha256 = $protocol.fixture_manifest_sha256.ToLowerInvariant()
            harness_sha256 = $protocol.harness_manifest_sha256.ToLowerInvariant()
            scenario_sha256 = $protocol.shell_scenario_sha256.ToLowerInvariant()
        }
    }
    finally {
        foreach ($lock in $trustTreeLocks) {
            $lock.Dispose()
        }
    }
}

function Get-M0CandidateRoot {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot,
        [Parameter(Mandatory)] [ValidateSet('Qt', 'Tauri')] [string] $Candidate
    )

    $relative = switch ($Candidate) {
        'Qt' { 'experiments\m0\candidates\qt' }
        'Tauri' { 'experiments\m0\candidates\tauri' }
        default { throw [ArgumentException]::new("Unknown candidate '$Candidate'.") }
    }
    return Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath $relative
}

function Assert-M0NoDynamicExecution {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    $roots = @(
        (Join-Path $RepositoryRoot 'scripts\m0'),
        (Join-Path $RepositoryRoot 'experiments\m0\harness')
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root)) {
            continue
        }
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object Extension -in '.ps1', '.psm1', '.psd1') {
            $tokens = $null
            $parseErrors = $null
            $ast = [Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors)
            if ($parseErrors.Count -gt 0) {
                throw [IO.InvalidDataException]::new("PowerShell parse failure in '$($file.FullName)'.")
            }

            $dynamicCommands = @($ast.FindAll({
                param($node)
                if ($node -isnot [Management.Automation.Language.CommandAst]) {
                    return $false
                }
                $name = $node.GetCommandName()
                return $name -in @('Invoke-Expression', 'iex')
            }, $true))
            $dynamicScriptBlocks = @($ast.FindAll({
                param($node)
                if ($node -isnot [Management.Automation.Language.InvokeMemberExpressionAst] -or -not $node.Static) {
                    return $false
                }
                $typeName = if ($node.Expression -is [Management.Automation.Language.TypeExpressionAst]) {
                    $node.Expression.TypeName.FullName
                }
                else {
                    ''
                }
                return $typeName -in @('scriptblock', 'System.Management.Automation.ScriptBlock') -and $node.Member.Value -eq 'Create'
            }, $true))
            if ($dynamicCommands.Count -gt 0 -or $dynamicScriptBlocks.Count -gt 0) {
                throw [IO.InvalidDataException]::new("Dynamic expression execution is forbidden in '$($file.FullName)'.")
            }
        }
    }
}

function Assert-M0Layout {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $RepositoryRoot
    )

    $root = [IO.Path]::GetFullPath($RepositoryRoot)
    $required = @(
        '.gitignore',
        '.gitattributes',
        'docs\m0\environment.md',
        'docs\m0\protocol.md',
        'docs\m0\layout.md',
        'experiments\m0\README.md',
        'experiments\m0\protocol\protocol.v1.json',
        'experiments\m0\protocol\environments\environment.v1.json',
        'experiments\m0\protocol\environments\windows-benchmark.v1.json',
        'experiments\m0\protocol\environments\linux-reference.v1.json',
        'experiments\m0\protocol\scenarios\shell.v1.json',
        'experiments\m0\protocol\schemas\protocol.schema.json',
        'experiments\m0\protocol\schemas\environment-bundle.schema.json',
        'experiments\m0\protocol\schemas\environment.schema.json',
        'experiments\m0\protocol\schemas\candidate.schema.json',
        'experiments\m0\protocol\schemas\scenario.schema.json',
        'experiments\m0\protocol\schemas\harness-manifest.schema.json',
        'experiments\m0\protocol\schemas\marker-catalog.schema.json',
        'experiments\m0\protocol\schemas\fixture-manifest.schema.json',
        'experiments\m0\protocol\schemas\shell-fixture.schema.json',
        'experiments\m0\protocol\schemas\shell-queue.schema.json',
        'experiments\m0\protocol\schemas\schedule.schema.json',
        'experiments\m0\protocol\schemas\statistics.schema.json',
        'experiments\m0\protocol\schemas\statistics-golden.schema.json',
        'experiments\m0\protocol\schemas\visual-golden.schema.json',
        'experiments\m0\protocol\schemas\visual.schema.json',
        'experiments\m0\protocol\schemas\event.schema.json',
        'experiments\m0\protocol\schemas\evidence-intent.schema.json',
        'experiments\m0\protocol\schemas\evidence-snapshot-set.schema.json',
        'experiments\m0\protocol\schemas\evidence-snapshot.schema.json',
        'experiments\m0\protocol\schemas\run.schema.json',
        'experiments\m0\protocol\schemas\batch.schema.json',
        'experiments\m0\protocol\schedule.v1.json',
        'experiments\m0\protocol\schedule.v1.sha256',
        'experiments\m0\protocol\statistics.v1.json',
        'experiments\m0\protocol\statistics.v1.sha256',
        'experiments\m0\protocol\markers.v1.json',
        'experiments\m0\protocol\markers.v1.sha256',
        'experiments\m0\protocol\goldens\statistics.v1.json',
        'experiments\m0\protocol\goldens\visual.v1.json',
        'experiments\m0\protocol\visual.v1.json',
        'experiments\m0\protocol\visual.v1.sha256',
        'experiments\m0\candidates\qt\README.md',
        'experiments\m0\candidates\qt\candidate.v1.json',
        'experiments\m0\candidates\tauri\README.md',
        'experiments\m0\candidates\tauri\candidate.v1.json',
        'experiments\m0\harness\M0.Harness.psd1',
        'experiments\m0\harness\M0.Harness.psm1',
        'experiments\m0\harness\M0.Fixture.psm1',
        'experiments\m0\harness\M0.Statistics.psm1',
        'experiments\m0\harness\M0.Markers.psm1',
        'experiments\m0\harness\M0.Evidence.psm1',
        'experiments\m0\harness\M0.Visual.psm1',
        'experiments\m0\harness\manifest.v1.json',
        'experiments\m0\harness\manifest.v1.sha256',
        'experiments\m0\harness\tests\Contract.Tests.ps1',
        'experiments\m0\harness\tests\Fixture.Tests.ps1',
        'experiments\m0\harness\tests\HarnessManifest.Tests.ps1',
        'experiments\m0\harness\tests\Statistics.Tests.ps1',
        'experiments\m0\harness\tests\Markers.Tests.ps1',
        'experiments\m0\harness\tests\Evidence.Tests.ps1',
        'experiments\m0\harness\tests\EvidenceSnapshotSet.Tests.ps1',
        'experiments\m0\harness\tests\EvidenceWriter.Tests.ps1',
        'experiments\m0\harness\tests\Visual.Tests.ps1',
        'experiments\m0\shared\shell-fixture\LICENSE.assets.txt',
        'experiments\m0\shared\shell-fixture\assets\cover-placeholder.svg',
        'experiments\m0\shared\shell-fixture\assets\cover-track.svg',
        'experiments\m0\shared\shell-fixture\fixture.v1.json',
        'experiments\m0\shared\shell-fixture\queue.v1.json',
        'experiments\m0\shared\shell-fixture\manifest.v1.json',
        'experiments\m0\shared\shell-fixture\manifest.v1.sha256',
        'scripts\m0\New-M0ShellFixture.ps1',
        'scripts\m0\New-M0HarnessManifest.ps1',
        'scripts\m0\Invoke-M0HarnessTests.ps1',
        'scripts\m0\Test-M0Fixture.ps1',
        'scripts\m0\Test-M0Statistics.ps1',
        'scripts\m0\Test-M0Markers.ps1',
        'scripts\m0\Test-M0Evidence.ps1',
        'scripts\m0\Test-M0Visual.ps1',
        'scripts\m0\Test-M0Layout.ps1'
    )
    foreach ($relative in $required) {
        $path = Resolve-M0ScopedPath -BasePath $root -CandidatePath $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw [IO.FileNotFoundException]::new("Required M0 file is missing: $relative")
        }
    }

    foreach ($relative in @('CMakeLists.txt', 'Cargo.toml', 'package.json', 'src', 'src-tauri')) {
        $path = Join-Path $root $relative
        if (Test-Path -LiteralPath $path) {
            throw [IO.InvalidDataException]::new("Product-root scaffold '$relative' is forbidden before M0.10.")
        }
    }

    $ignore = Get-Content -LiteralPath (Join-Path $root '.gitignore') -Raw
    if ($ignore -notmatch '(?m)^/out/m0/$') {
        throw [IO.InvalidDataException]::new(".gitignore must contain '/out/m0/'.")
    }

    Assert-M0NoDynamicExecution -RepositoryRoot $root
}

Export-ModuleMember -Function @(
    'Resolve-M0ScopedPath',
    'Get-M0Sha256',
    'Assert-M0FileHash',
    'Assert-M0HarnessManifest',
    'Assert-M0JsonDocument',
    'Assert-M0ProtocolContract',
    'Assert-M0EnvironmentContract',
    'Get-M0CandidateRoot',
    'Assert-M0NoDynamicExecution',
    'Assert-M0Layout',
    'Get-M0TrustedCommonInputs',
    'New-M0HarnessManifest'
)
