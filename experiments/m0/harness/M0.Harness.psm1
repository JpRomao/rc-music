Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-M0ScopedPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string] $BasePath,
        [Parameter(Mandatory)] [string] $CandidatePath
    )

    $baseFull = [IO.Path]::GetFullPath($BasePath).TrimEnd('\', '/')
    if (Test-Path -LiteralPath $baseFull) {
        $baseItem = Get-Item -LiteralPath $baseFull -Force
        if (($baseItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [ArgumentException]::new("Reparse points and links are forbidden as scoped M0 roots: '$baseFull'.")
        }
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
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw [ArgumentException]::new("Reparse points and links are forbidden in scoped M0 paths: '$current'.")
        }
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
    $shellScenario = Resolve-M0ScopedPath -BasePath $RepositoryRoot -CandidatePath 'experiments\m0\protocol\scenarios\shell.v1.json'
    Assert-M0FileHash -LiteralPath $shellScenario -ExpectedHash $Protocol.shell_scenario_sha256
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
        'experiments\m0\protocol\schemas\fixture-manifest.schema.json',
        'experiments\m0\protocol\schemas\shell-fixture.schema.json',
        'experiments\m0\protocol\schemas\shell-queue.schema.json',
        'experiments\m0\candidates\qt\README.md',
        'experiments\m0\candidates\qt\candidate.v1.json',
        'experiments\m0\candidates\tauri\README.md',
        'experiments\m0\candidates\tauri\candidate.v1.json',
        'experiments\m0\harness\M0.Harness.psd1',
        'experiments\m0\harness\M0.Harness.psm1',
        'experiments\m0\harness\M0.Fixture.psm1',
        'experiments\m0\harness\tests\Fixture.Tests.ps1',
        'experiments\m0\shared\shell-fixture\LICENSE.assets.txt',
        'experiments\m0\shared\shell-fixture\assets\cover-placeholder.svg',
        'experiments\m0\shared\shell-fixture\assets\cover-track.svg',
        'experiments\m0\shared\shell-fixture\fixture.v1.json',
        'experiments\m0\shared\shell-fixture\queue.v1.json',
        'experiments\m0\shared\shell-fixture\manifest.v1.json',
        'experiments\m0\shared\shell-fixture\manifest.v1.sha256',
        'scripts\m0\New-M0ShellFixture.ps1',
        'scripts\m0\Invoke-M0HarnessTests.ps1',
        'scripts\m0\Test-M0Fixture.ps1',
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
    'Assert-M0JsonDocument',
    'Assert-M0ProtocolContract',
    'Assert-M0EnvironmentContract',
    'Get-M0CandidateRoot',
    'Assert-M0NoDynamicExecution',
    'Assert-M0Layout'
)
