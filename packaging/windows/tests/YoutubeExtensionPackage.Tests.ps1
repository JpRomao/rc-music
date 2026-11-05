#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testParent = Join-Path $repository 'out/product/test-runs/youtube-extension-package'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
$assets = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs', 'popup.css', 'popup.html', 'popup.mjs')
$recipes = @('New-RcMusicYoutubeExtensionPackage.ps1', 'RcMusicYoutubeExtensionPackage.psm1')
$inputs = @('extensions/chromium/youtube', 'packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1',
    'packaging/windows/RcMusicYoutubeExtensionPackage.psm1')

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected, [string] $Message = 'Values differ') {
    if ($Actual -cne $Expected) { throw $Message }
}
function Assert-Throws([scriptblock] $Body, [string] $Message) {
    $thrown = $false
    try { & $Body | Out-Null } catch { $thrown = $true }
    if (-not $thrown) { throw $Message }
}
function Assert-SafeTestRoot {
    if ($testId -cnotmatch '^[0-9a-f]{32}$' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe owned fixture root'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe fixture ancestor' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Utf8([string] $Path, [string] $Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-OwnedGit([string] $Root, [string[]] $Arguments) {
    if (-not [IO.Path]::GetFullPath($Root).StartsWith([IO.Path]::GetFullPath($testRoot) + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Git fixture must stay inside the owned test root' }
    $result = @(& git --no-optional-locks -C $Root @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Owned Git fixture operation failed' }
    return ($result -join "`n").Trim()
}
function New-Fixture([switch] $WithoutGit) {
    $state.Fixtures++
    $root = Join-Path $testRoot ('source exportada 日本 ' + $state.Fixtures)
    $extension = Join-Path $root 'extensions/chromium/youtube'
    $main = Join-Path $root 'extensions/chromium/main'
    $packaging = Join-Path $root 'packaging/windows'
    $null = New-Item -ItemType Directory -Path $extension, $main, $packaging
    foreach ($name in $assets) {
        Copy-Item -LiteralPath (Join-Path $repository ('extensions/chromium/youtube/' + $name)) -Destination $extension
    }
    Write-Utf8 (Join-Path $extension 'README.md') 'Owned non-runtime fixture documentation.'
    Write-Utf8 (Join-Path $extension 'extra-sensitive.pem') 'Synthetic exclusion marker, not a key.'
    $mainSentinel = Join-Path $main 'preserve.keep'
    Write-Utf8 $mainSentinel 'Principal package bytes must remain untouched.'
    foreach ($name in $recipes) {
        Copy-Item -LiteralPath (Join-Path $repository ('packaging/windows/' + $name)) -Destination $packaging
    }
    $commit = ''
    if (-not $WithoutGit) {
        $emptyHooks = Join-Path $root '.owned-empty-hooks'
        $null = New-Item -ItemType Directory -Path $emptyHooks
        $null = Invoke-OwnedGit $root @('init', '-q')
        $null = Invoke-OwnedGit $root @('config', 'core.autocrlf', 'false')
        $null = Invoke-OwnedGit $root @('add', '-f', '--', '.')
        $null = Invoke-OwnedGit $root @('-c', ('core.hooksPath=' + $emptyHooks), '-c', 'commit.gpgsign=false',
            '-c', 'user.name=RC Music Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-q', '-m', 'Seed owned packaging fixture')
        $commit = Invoke-OwnedGit $root @('rev-parse', '--verify', 'HEAD')
    }
    Remove-Module RcMusicYoutubeExtensionPackage -Force -ErrorAction SilentlyContinue
    Import-Module (Join-Path $packaging 'RcMusicYoutubeExtensionPackage.psm1') -Global -Force
    return [pscustomobject]@{ Root = $root; Extension = $extension; Main = $mainSentinel; MainHash = (Hash $mainSentinel)
        Packaging = $packaging; Entry = (Join-Path $packaging 'New-RcMusicYoutubeExtensionPackage.ps1')
        Output = (Join-Path $root 'out/product/youtube candidate'); Commit = $commit }
}
function New-Plan($Fixture) {
    return New-RcMusicYoutubeExtensionPackagePlan -RepositoryRoot $Fixture.Root -OutputDirectory $Fixture.Output
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Test root must be fresh' }
$null = New-Item -ItemType Directory -Path $testRoot
try {
    Test-Case 'planning is read-only with a fixed seven-file YouTube inventory and both recipe hashes' {
        $f = New-Fixture
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'Planner returned no candidate'
        Assert-Equal $plan.schema_version 1
        Assert-Equal $plan.component 'youtube-extension-source'
        Assert-Equal $plan.identity_state 'identity_pending'
        Assert-Equal $plan.native_host_name 'com.rcmusic.youtube'
        Assert-True ($null -eq $plan.native_manifest) 'A native host manifest was invented'
        Assert-Equal $plan.source_commit $f.Commit
        Assert-True ($plan.extension_dirty -is [bool] -and -not $plan.extension_dirty) 'Clean extension was not observed'
        Assert-True ($plan.recipe_dirty -is [bool] -and -not $plan.recipe_dirty) 'Clean recipes were not observed'
        Assert-True ($plan.source_dirty -is [bool] -and -not $plan.source_dirty) 'Clean source state was not observed'
        Assert-Equal (@($plan.source_inputs | Sort-Object) -join '|') (($inputs | Sort-Object) -join '|')
        Assert-Equal $plan.files.Count 7
        Assert-Equal (@($plan.files.relative_path | Sort-Object) -join '|') (($assets | ForEach-Object { 'extension/' + $_ } | Sort-Object) -join '|')
        foreach ($entry in $plan.files) {
            Assert-Equal $entry.sha256 (Hash $entry.source)
            Assert-Equal $entry.size_bytes ([IO.FileInfo]::new($entry.source).Length)
        }
        Assert-Equal $plan.recipe_files.Count 2
        foreach ($entry in $plan.recipe_files) {
            Assert-True ($entry.path -in @($recipes | ForEach-Object { 'packaging/windows/' + $_ })) 'Unexpected recipe recorded'
            Assert-Equal $entry.sha256 (Hash (Join-Path $f.Root $entry.path))
        }
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Planning wrote an output directory'
        Assert-Equal (Hash $f.Main) $f.MainHash 'Planning changed principal bytes'
    }
    Test-Case 'materialized source candidate stays identity pending without native registration or an invented key' {
        $f = New-Fixture
        $before = @{}
        foreach ($name in $assets) { $before[$name] = Hash (Join-Path $f.Extension $name) }
        $result = Copy-RcMusicYoutubeExtensionPackagePlan -Plan (New-Plan $f)
        Assert-True ($null -ne $result) 'Materializer returned no artifact'
        Assert-Equal $result.package_directory $f.Output
        Assert-Equal $result.extension_directory (Join-Path $f.Output 'extension')
        Assert-Equal $result.receipt_path (Join-Path $f.Output 'package-receipt.json')
        $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json
        Assert-Equal $receipt.schema_version 1
        Assert-Equal $receipt.component 'youtube-extension-source'
        Assert-Equal $receipt.identity_state 'identity_pending'
        Assert-Equal $receipt.native_host_name 'com.rcmusic.youtube'
        Assert-True ($null -eq $receipt.native_manifest) 'Source-only package gained an activatable native manifest'
        Assert-Equal $receipt.source_commit $f.Commit
        Assert-Equal $receipt.recipe_files.Count 2
        Assert-Equal $receipt.files.Count 7
        $physical = @(Get-ChildItem -LiteralPath $f.Output -File -Recurse | ForEach-Object {
            [IO.Path]::GetRelativePath($f.Output, $_.FullName).Replace('\', '/')
        } | Sort-Object)
        $expected = @('package-receipt.json') + @($assets | ForEach-Object { 'extension/' + $_ })
        Assert-Equal ($physical -join '|') (($expected | Sort-Object) -join '|') 'Candidate contains undeclared files'
        foreach ($name in $assets) {
            Assert-Equal (Hash (Join-Path $result.extension_directory $name)) $before[$name]
            Assert-Equal (Hash (Join-Path $f.Extension $name)) $before[$name] 'Original extension changed'
        }
        foreach ($entry in $receipt.files) {
            Assert-Equal $entry.sha256 (Hash (Join-Path $f.Output $entry.relative_path))
            Assert-Equal $entry.size_bytes ([IO.FileInfo]::new((Join-Path $f.Output $entry.relative_path)).Length)
        }
        $manifest = Get-Content -LiteralPath (Join-Path $result.extension_directory 'manifest.json') -Raw | ConvertFrom-Json
        Assert-True ($null -eq $manifest.PSObject.Properties['key']) 'An extension key was invented'
        Assert-True (-not ((Get-Content -LiteralPath $result.receipt_path -Raw).Contains('allowed_origins'))) 'Receipt became a native manifest'
        Assert-Equal (Hash $f.Main) $f.MainHash 'Materialization changed the principal'
    }
    Test-Case 'principal-only changes do not dirty the separate YouTube component' {
        $f = New-Fixture
        [IO.File]::AppendAllText($f.Main, ' Principal-only edit.')
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        Assert-True (-not $plan.extension_dirty -and -not $plan.recipe_dirty -and -not $plan.source_dirty) 'Principal edit polluted YouTube provenance'
    }
    Test-Case 'extension changes are recorded independently of clean recipe inputs' {
        $f = New-Fixture
        [IO.File]::AppendAllText((Join-Path $f.Extension 'popup.css'), ' /* own dirty fixture */')
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        Assert-True ($plan.extension_dirty -and -not $plan.recipe_dirty -and $plan.source_dirty) 'Extension dirty state was hidden'
    }
    Test-Case 'recipe changes are recorded instead of claiming a clean extension commit for dirty packaging' {
        $f = New-Fixture
        [IO.File]::AppendAllText($f.Entry, "`n# Owned dirty recipe fixture`n")
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        Assert-True (-not $plan.extension_dirty -and $plan.recipe_dirty -and $plan.source_dirty) 'Recipe dirty state was hidden'
        $entry = @($plan.recipe_files | Where-Object path -eq 'packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1')[0]
        Assert-Equal $entry.sha256 (Hash $f.Entry)
    }
    Test-Case 'an untracked YouTube file changes provenance without entering the fixed payload' {
        $f = New-Fixture
        Write-Utf8 (Join-Path $f.Extension 'untracked-note.txt') 'Owned extra file remains source only.'
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        Assert-True ($plan.extension_dirty -and $plan.source_dirty) 'Untracked extension input was ignored by provenance'
        Assert-Equal $plan.files.Count 7
        Assert-True (-not ($plan.files.relative_path -match 'untracked|README|pem|main')) 'Extra source entered payload'
    }
    Test-Case 'an ignored untracked critical asset cannot be labelled clean by porcelain alone' {
        $f = New-Fixture
        $relative = 'extensions/chromium/youtube/popup.css'
        [IO.File]::AppendAllText((Join-Path $f.Root '.git/info/exclude'), "`n$relative`n")
        $null = Invoke-OwnedGit $f.Root @('rm', '--cached', '--', $relative)
        $null = Invoke-OwnedGit $f.Root @('-c', ('core.hooksPath=' + (Join-Path $f.Root '.owned-empty-hooks')),
            '-c', 'commit.gpgsign=false', '-c', 'user.name=RC Music Fixture',
            '-c', 'user.email=fixture@example.invalid', 'commit', '-q', '-m', 'Untrack owned critical fixture')
        Assert-Equal (Invoke-OwnedGit $f.Root @('status', '--porcelain=v1', '--untracked-files=all', '--',
            'extensions/chromium/youtube')) '' 'Fixture did not hide its untracked critical asset'
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        Assert-True ($plan.extension_dirty -and $plan.source_dirty) 'Ignored critical asset was falsely labelled clean'
        Assert-Equal $plan.files.Count 7
    }
    Test-Case 'a missing runtime asset fails before producing any output' {
        $f = New-Fixture
        Remove-Item -LiteralPath (Join-Path $f.Extension 'native-client.mjs')
        Assert-Throws { New-Plan $f } 'Missing runtime asset was accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected source created output'
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
    Test-Case 'asset hash drift is rejected even when the extension was already dirty' {
        $f = New-Fixture
        $path = Join-Path $f.Extension 'popup.css'
        [IO.File]::AppendAllText($path, ' /* first own edit */')
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        [IO.File]::AppendAllText($path, ' /* second own edit */')
        $changed = Hash $path
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan } 'Changed asset was copied from a stale plan'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Drift was discovered only after output creation'
        Assert-Equal (Hash $path) $changed 'Changed source was overwritten'
    }
    Test-Case 'recipe hash drift is rejected even when packaging was already dirty' {
        $f = New-Fixture
        [IO.File]::AppendAllText($f.Entry, "`n# First owned edit`n")
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        [IO.File]::AppendAllText($f.Entry, "`n# Second owned edit`n")
        $changed = Hash $f.Entry
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan } 'Changed recipe was copied from a stale plan'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Recipe drift created output'
        Assert-Equal (Hash $f.Entry) $changed
    }
    Test-Case 'existing outputs are preserved during planning and stale-plan materialization' {
        $f = New-Fixture
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        $null = New-Item -ItemType Directory -Path $f.Output
        $sentinel = Join-Path $f.Output 'preserve.keep'
        Write-Utf8 $sentinel 'Existing candidate must survive.'
        $before = Hash $sentinel
        Assert-Throws { New-Plan $f } 'Existing output was accepted by planning'
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan } 'Stale plan overwrote existing output'
        Assert-Equal (Hash $sentinel) $before
        Assert-Equal (@(Get-ChildItem -LiteralPath $f.Output).Count) 1
    }
    Test-Case 'relative output and source overlap are rejected without changing originals' {
        $f = New-Fixture
        foreach ($bad in @($f.Extension, $f.Root, (Join-Path $f.Extension 'candidate'), 'relative-candidate')) {
            Assert-Throws { New-RcMusicYoutubeExtensionPackagePlan -RepositoryRoot $f.Root -OutputDirectory $bad } 'Unsafe destination was accepted'
        }
        Assert-Equal (Hash $f.Main) $f.MainHash
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $f.Extension 'candidate'))) 'Candidate polluted its source'
    }
    Test-Case 'tampered plans cannot add principal files or native manifest data' {
        $f = New-Fixture
        $plan = New-Plan $f
        Assert-True ($null -ne $plan) 'No plan'
        $plan.files += [pscustomobject]@{ source = $f.Main; relative_path = 'extension/principal.keep'
            sha256 = $f.MainHash; size_bytes = [IO.FileInfo]::new($f.Main).Length }
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan } 'A forged principal mapping was accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Forged plan created output'
        $plan = New-Plan $f
        $plan.native_manifest = [pscustomobject]@{ name = 'com.rcmusic.youtube'; allowed_origins = @('chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/') }
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $plan } 'Unattested native manifest was accepted'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Unattested manifest created output'
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
    Test-Case 'a directory inside another checkout cannot impersonate the recipe repository' {
        $f = New-Fixture -WithoutGit
        Assert-Throws { New-Plan $f } 'An unversioned nested export was labelled with a parent commit'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Unversioned source created output'
    }
    Test-Case 'the entrypoint resolves relative output against its own repository from a foreign working directory' {
        $f = New-Fixture
        Push-Location $testRoot
        try { $result = & $f.Entry -OutputDirectory 'out/product/cli-source' }
        finally { Pop-Location }
        Assert-True ($null -ne $result) 'Entrypoint returned no candidate'
        $output = Join-Path $f.Root 'out/product/cli-source'
        Assert-Equal $result.package_directory $output
        $receipt = Get-Content -LiteralPath (Join-Path $output 'package-receipt.json') -Raw | ConvertFrom-Json
        Assert-Equal $receipt.identity_state 'identity_pending'
        Assert-Equal $receipt.source_commit $f.Commit
        Assert-Equal $receipt.files.Count 7
        Assert-Equal (Hash $f.Main) $f.MainHash
    }
} finally {
    Remove-Module RcMusicYoutubeExtensionPackage -Force -ErrorAction SilentlyContinue
    if ($state.Failed -eq 0) {
        Assert-SafeTestRoot
        if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
    } else {
        Write-Host "[INFO] Preserved owned failed fixtures: $testRoot"
    }
}
Write-Host "YouTube source candidate: $($state.Passed) passed / $($state.Failed) failed"
if ($state.Failed -gt 0) { exit 1 }
