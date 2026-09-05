#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '../WindowsInstallerPlan.psm1') -Force
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$runId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $repository ('out/m0/test-runs/rc-music-installer-plan/' + $runId)
$null = New-Item -ItemType Directory -Path $testRoot
$state = @{ Passed = 0; Failed = 0 }
function Assert-True([bool] $Condition, [string] $Message) { if (-not $Condition) { throw $Message } }
function Assert-Rejected([scriptblock] $Body) {
    $rejected = $false
    try { & $Body | Out-Null } catch { $rejected = $true }
    Assert-True $rejected 'Expected rejection'
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
try {
    Test-Case 'installer-only corrections produce a new package identity' {
        $before = Get-RcMusicInstallerIdentity -InventoryJson '[]' -RecipeHashes @(@{ path = 'template.nsi'; sha256 = ('a' * 64) })
        $after = Get-RcMusicInstallerIdentity -InventoryJson '[]' -RecipeHashes @(@{ path = 'template.nsi'; sha256 = ('b' * 64) })
        Assert-True ($before -cne $after) 'Installer-only fix reused the obsolete uninstaller identity'
    }
    Test-Case 'plans a private per-user installer without creating its destination' {
        $f = New-Fixture
        $p = New-RcMusicInstallerPlan -RuntimeDirectory $f.App -OutputDirectory $f.Output
        Assert-True ($p.payload_id -cmatch '^[0-9a-f]{64}$') 'Invalid immutable payload ID'
        Assert-True ($p.recipe_hashes.path -ccontains 'RcMusicInstallerFiles.nsh') 'Native guard missing from package identity'
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
    }
} finally {
    # Exact fresh GUID owned by this test; no junction fixtures are created here.
    $expectedRoot = Join-Path $repository ('out/m0/test-runs/rc-music-installer-plan/' + $runId)
    if ($runId -cmatch '^[0-9a-f]{32}$' -and [IO.Path]::GetFullPath($testRoot) -ceq [IO.Path]::GetFullPath($expectedRoot)) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    } else { throw 'Unsafe installer-plan test cleanup path' }
}
Write-Host "Windows installer plan: $($state.Passed) passed, $($state.Failed) failed."
if ($state.Failed -gt 0) { exit 1 }
