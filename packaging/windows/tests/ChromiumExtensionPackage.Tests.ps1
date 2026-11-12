#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$testParent = Join-Path $repository 'out/product/test-runs/chromium-extension-package'
$testId = [Guid]::NewGuid().ToString('N')
$testRoot = Join-Path $testParent $testId
$state = @{ Passed = 0; Failed = 0; Fixtures = 0 }
$commonAssets = @('manifest.json', 'native-client.mjs', 'policy.mjs', 'popup-controller.mjs', 'popup.css', 'popup.html', 'popup.mjs')
$commonRecipes = @('New-RcMusicChromiumExtensionPackage.ps1', 'RcMusicYoutubeExtensionPackage.psm1', 'RcMusicChromiumIdentity.psm1')
# Public-only 2048-bit SPKI fixtures. Expected IDs/hashes were calculated
# independently with Python hashlib; no private key or generator is used here.
$mainPublicKey = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAqX9/7jyIe6knu4ZGOK+J3L7DrO16UrFM7AjYtt3tBD7ROepgbUEnrkg1RFLKiUcuAZIIF3qfvLdwZiY3ZebHReloePi/ZhmwKNoey4JeBnlZpZhrFdzI+WBFWTr92Ams1x6LwU6ugdEwfr5uZxUX/hEYUqqcdblF06YntRwu8rPr8tmsGl9s+rZw6L5arTz3Mn1Pw4Y1Wt4q+NRlWYso14doQZ8s/OHuJFRY7kc8MhpIN05ayPlcv2VQIe+xWdm67nR8nWlbsr08c0bQNrgAqCmmnCrWUPW3IXvbcb+4rnL4xiyYnXnxLRPJMpz3fNkpIu7aziR1869t9Hiz2hJ5cQIDAQAB'
$otherPublicKey = 'MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsJmJ6eCY4G5chs/I8sD1xnEmue2mffsMr7Ya5gymCGYNpufwaECbeRMh3cAz0xjy7VB0+m25bVWrWAll9xJdPqPahx7aJ0on4T2666fSrzrhGm2Rq5RxVUFcHAmV+GZF1W+1evt1TO3EuNT8WW9+74Ysph6cbo2nm7JEwurhsZ4uBb8vtEQyg1AvE/osIL6qUEW0SRwXabhVL1H911jDHM+tocePeX4BsFfoZMRTaAO94MlWFPniUskPmSQ0o80q+9HP9h5ovZidDfEkB2Ifq+sMFKPJOhJUAHPZhD7HmTFymwfJlQg6XOIQNYUOe6Xm1Vl7gdRfjl4W9XM08yu5tQIDAQAB'
$mainExtensionId = 'kggjfjbjhklkbpelddffdjelbiomoolc'
$mainPublicKeySha = 'a66959197aba1f4b3355394b18eceeb25dd21767a74a5d2fd6e8dc8a711dd953'

function Assert-True([bool] $Value, [string] $Message) { if (-not $Value) { throw $Message } }
function Assert-Equal($Actual, $Expected, [string] $Message = 'Values differ') {
    if ($Actual -cne $Expected) { throw $Message }
}
function Assert-Throws([scriptblock] $Body, [string] $Message) {
    $thrown = $false
    try { & $Body | Out-Null } catch { $thrown = $true }
    if (-not $thrown) { throw $Message }
}
function Test-Case([string] $Name, [scriptblock] $Body) {
    try { & $Body; $state.Passed++; Write-Host "[PASS] $Name" }
    catch { $state.Failed++; Write-Host "[FAIL] $Name :: $($_.Exception.Message)" }
}
function Hash([string] $Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Write-Utf8([string] $Path, [string] $Text) { [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false)) }
function Assert-SafeTestRoot {
    if ($testId -cnotmatch '\A[0-9a-f]{32}\z' -or
        [IO.Path]::GetFullPath($testRoot) -cne [IO.Path]::GetFullPath((Join-Path $testParent $testId))) {
        throw 'Unsafe owned Chromium fixture root'
    }
    $ancestor = [IO.Path]::GetFullPath($testRoot)
    while ($ancestor) {
        try { $attributes = [IO.File]::GetAttributes($ancestor) }
        catch [IO.FileNotFoundException] { $attributes = 0 }
        catch [IO.DirectoryNotFoundException] { $attributes = 0 }
        if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe fixture reparse ancestor' }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
}
function Invoke-OwnedGit([string] $Root, [string[]] $Arguments) {
    if (-not [IO.Path]::GetFullPath($Root).StartsWith($testRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Git must stay inside its owned fixture' }
    foreach ($name in @('GIT_DIR', 'GIT_WORK_TREE', 'GIT_COMMON_DIR', 'GIT_INDEX_FILE', 'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES')) {
        if (-not [string]::IsNullOrEmpty([Environment]::GetEnvironmentVariable($name))) { throw 'External Git overrides are unsupported in this fixture' }
    }
    $empty = Join-Path $testRoot 'empty-git-configuration'
    $result = @(& git --no-optional-locks -c core.fsmonitor=false -c "core.hooksPath=$empty" -c "init.templateDir=$empty" `
        -c commit.gpgsign=false -c tag.gpgsign=false -C $Root @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw 'Owned Chromium Git fixture operation failed' }
    return ($result -join "`n").Trim()
}
function New-Fixture([ValidateSet('Main', 'Youtube')][string] $Channel = 'Main', [string] $PublicKey) {
    $state.Fixtures++
    $root = Join-Path $testRoot ('source exportada 日本 ' + $state.Fixtures)
    $relative = 'extensions/chromium/' + $Channel.ToLowerInvariant()
    $extension = Join-Path $root $relative
    $other = Join-Path $root ('extensions/chromium/' + $(if ($Channel -ceq 'Main') { 'youtube' } else { 'main' }))
    $packaging = Join-Path $root 'packaging/windows'
    $null = New-Item -ItemType Directory -Path $extension, $other, $packaging
    $assets = @($commonAssets)
    if ($Channel -ceq 'Main') { $assets += @('detector.js', 'media-collector.js') }
    else { $assets += 'page-video.mjs' }
    foreach ($name in $assets) { Copy-Item -LiteralPath (Join-Path $repository ($relative + '/' + $name)) -Destination $extension }
    $manifestPath = Join-Path $extension 'manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -AsHashtable
    # Production may have an authorized public key. Explicitly remove it before
    # seeding the compatibility fixture, or insert only this public test vector.
    if ($manifest.Contains('key') -or -not [string]::IsNullOrEmpty($PublicKey)) {
        $manifest.Remove('key')
        if (-not [string]::IsNullOrEmpty($PublicKey)) { $manifest['key'] = $PublicKey }
        Write-Utf8 $manifestPath ((ConvertTo-Json -InputObject $manifest -Depth 12) + "`n")
    }
    Write-Utf8 (Join-Path $extension 'README.md') 'Owned fixture documentation, excluded from runtime assets.'
    Write-Utf8 (Join-Path $extension 'private-do-not-package.pem') 'Synthetic exclusion marker; this is not a private key.'
    $sentinel = Join-Path $other 'preserve.keep'
    Write-Utf8 $sentinel 'Other extension source must remain untouched.'
    foreach ($name in (@($commonRecipes) + @('New-RcMusicYoutubeExtensionPackage.ps1'))) {
        Copy-Item -LiteralPath (Join-Path $repository ('packaging/windows/' + $name)) -Destination $packaging
    }
    $recipes = @($commonRecipes)
    if ($Channel -ceq 'Youtube') { $recipes += 'New-RcMusicYoutubeExtensionPackage.ps1' }
    $null = Invoke-OwnedGit $root @('init', '-q')
    $null = Invoke-OwnedGit $root @('config', 'core.autocrlf', 'false')
    $null = Invoke-OwnedGit $root @('add', '-f', '--', '.')
    $null = Invoke-OwnedGit $root @('-c', 'user.name=RC Music Fixture', '-c', 'user.email=fixture@example.invalid',
        'commit', '-q', '-m', 'Seed owned Chromium source fixture')
    $commit = Invoke-OwnedGit $root @('rev-parse', '--verify', 'HEAD')
    Remove-Module RcMusicYoutubeExtensionPackage, RcMusicChromiumIdentity -Force -ErrorAction SilentlyContinue
    Import-Module (Join-Path $packaging 'RcMusicYoutubeExtensionPackage.psm1') -Global -Force
    return [pscustomobject]@{ Root = $root; Channel = $Channel; Relative = $relative; Extension = $extension
        Assets = $assets; Recipes = $recipes; Other = $sentinel; OtherHash = (Hash $sentinel); Commit = $commit
        Entry = (Join-Path $packaging 'New-RcMusicChromiumExtensionPackage.ps1')
        Output = (Join-Path $root 'out/product/chromium candidate') }
}
function New-Plan($Fixture) {
    return New-RcMusicChromiumExtensionPackagePlan -RepositoryRoot $Fixture.Root -OutputDirectory $Fixture.Output -Channel $Fixture.Channel.ToLowerInvariant()
}

Assert-SafeTestRoot
if (Test-Path -LiteralPath $testRoot) { throw 'Chromium test root must be fresh' }
$null = New-Item -ItemType Directory -Path (Join-Path $testRoot 'empty-git-configuration')
try {
    foreach ($channel in @('Main', 'Youtube')) {
        Test-Case "$channel planning inventories only its keyless runtime assets and exact recipe inputs" {
            $f = New-Fixture $channel
            $p = New-Plan $f
            Assert-True ($null -ne $p) 'No Chromium plan'
            Assert-Equal $p.channel $channel
            Assert-Equal $p.component $(if ($channel -ceq 'Main') { 'main-extension-source' } else { 'youtube-extension-source' })
            Assert-Equal $p.native_host_name $(if ($channel -ceq 'Main') { 'com.rcmusic.media' } else { 'com.rcmusic.youtube' })
            Assert-Equal $p.identity_state 'identity_pending'
            Assert-True ($null -eq $p.extension_identity -and $null -eq $p.native_manifest) 'Keyless source invented an identity or registration manifest'
            Assert-Equal $p.source_commit $f.Commit
            Assert-True (-not $p.source_dirty -and -not $p.extension_dirty -and -not $p.recipe_dirty) 'Clean fixture was labelled dirty'
            Assert-Equal $p.files.Count $(if ($channel -ceq 'Main') { 9 } else { 8 })
            Assert-Equal (@($p.files.relative_path | Sort-Object) -join '|') (@($f.Assets | ForEach-Object { 'extension/' + $_ } | Sort-Object) -join '|')
            Assert-Equal (@($p.recipe_files.path | Sort-Object) -join '|') (@($f.Recipes | ForEach-Object { 'packaging/windows/' + $_ } | Sort-Object) -join '|')
            foreach ($row in $p.files) { Assert-Equal $row.sha256 (Hash $row.source) }
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Planning created output'
        }
    }
    Test-Case 'Main materialization preserves all nine source assets and excludes docs private markers and YouTube' {
        $f = New-Fixture Main
        $before = @{}
        foreach ($name in $f.Assets) { $before[$name] = Hash (Join-Path $f.Extension $name) }
        $result = Copy-RcMusicChromiumExtensionPackagePlan -Plan (New-Plan $f)
        Assert-True ($null -ne $result) 'Main copy returned no result'
        $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json
        Assert-Equal $receipt.channel 'Main'
        Assert-Equal $receipt.component 'main-extension-source'
        Assert-Equal $receipt.identity_state 'identity_pending'
        Assert-True ($null -eq $receipt.extension_identity) 'Main keyless copy invented identity'
        Assert-Equal $receipt.files.Count 9
        Assert-Equal $receipt.recipe_files.Count 3
        $physical = @(Get-ChildItem -LiteralPath $result.package_directory -File -Recurse | ForEach-Object {
            [IO.Path]::GetRelativePath($result.package_directory, $_.FullName).Replace('\', '/')
        } | Sort-Object)
        $expected = @('package-receipt.json') + @($f.Assets | ForEach-Object { 'extension/' + $_ })
        Assert-Equal ($physical -join '|') (($expected | Sort-Object) -join '|') 'Main package contains undeclared or sensitive files'
        foreach ($name in $f.Assets) {
            Assert-Equal (Hash (Join-Path $result.extension_directory $name)) $before[$name]
            Assert-Equal (Hash (Join-Path $f.Extension $name)) $before[$name]
        }
        Assert-Equal (Hash $f.Other) $f.OtherHash
    }
    Test-Case 'the generic CLI resolves Main output from its own repository when invoked from a foreign directory' {
        $f = New-Fixture Main
        Push-Location $testRoot
        try { $result = & $f.Entry -Channel Main -OutputDirectory 'out/product/cli-main' }
        finally { Pop-Location }
        Assert-True ($null -ne $result) 'Generic CLI returned no Main package'
        Assert-Equal $result.package_directory (Join-Path $f.Root 'out/product/cli-main')
        $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json
        Assert-Equal $receipt.channel 'Main'
        Assert-Equal $receipt.files.Count 9
        Assert-Equal $receipt.identity_state 'identity_pending'
        Assert-Equal (Hash $f.Other) $f.OtherHash
    }
    Test-Case 'a Main public manifest key binds the independent fixed identity without rewriting its bytes' {
        $f = New-Fixture Main -PublicKey $mainPublicKey
        $before = Hash (Join-Path $f.Extension 'manifest.json')
        $p = New-Plan $f
        Assert-True ($null -ne $p) 'No Main public-key plan'
        Assert-Equal $p.identity_state 'public_key_bound'
        Assert-Equal $p.extension_identity.extension_id $mainExtensionId
        Assert-Equal $p.extension_identity.origin ('chrome-extension://' + $mainExtensionId + '/')
        Assert-Equal $p.extension_identity.public_key_sha256 $mainPublicKeySha
        $result = Copy-RcMusicChromiumExtensionPackagePlan -Plan $p
        $receipt = Get-Content -LiteralPath $result.receipt_path -Raw | ConvertFrom-Json
        Assert-Equal $receipt.identity_state 'public_key_bound'
        Assert-Equal $receipt.extension_identity.extension_id $mainExtensionId
        Assert-Equal $receipt.extension_identity.public_key_sha256 $mainPublicKeySha
        Assert-Equal (Hash (Join-Path $result.extension_directory 'manifest.json')) $before
        Assert-Equal (Hash (Join-Path $f.Extension 'manifest.json')) $before
    }
    Test-Case 'Main source provenance ignores changes confined to YouTube and its unused legacy CLI' {
        $f = New-Fixture Main
        [IO.File]::AppendAllText($f.Other, ' Owned other-channel edit.')
        [IO.File]::AppendAllText((Join-Path $f.Root 'packaging/windows/New-RcMusicYoutubeExtensionPackage.ps1'), "`n# Unused legacy CLI edit.`n")
        $p = New-Plan $f
        Assert-True ($null -ne $p -and -not $p.source_dirty -and -not $p.extension_dirty -and -not $p.recipe_dirty) 'Main provenance includes unrelated YouTube inputs'
    }
    Test-Case 'invalid channels and the legacy YouTube copier cannot adopt a Main plan' {
        $f = New-Fixture Main
        foreach ($bad in @('Other', '', 'Main/Youtube')) {
            Assert-Throws { New-RcMusicChromiumExtensionPackagePlan -RepositoryRoot $f.Root -OutputDirectory $f.Output -Channel $bad } 'Unknown channel was accepted'
        }
        $p = New-Plan $f
        Assert-True ($null -ne $p) 'No Main plan'
        Assert-Throws { Copy-RcMusicYoutubeExtensionPackagePlan -Plan $p } 'Legacy YouTube copier accepted a Main plan'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Rejected channel created output'
    }
    Test-Case 'Main stale sources and forged inventory or identity are rejected before creating output' {
        foreach ($kind in @('stale', 'file', 'identity', 'channel')) {
            $f = New-Fixture Main
            $p = New-Plan $f
            Assert-True ($null -ne $p) 'No Main plan'
            switch ($kind) {
                stale { [IO.File]::AppendAllText((Join-Path $f.Extension 'popup.css'), ' /* owned source drift */') }
                file { $p.files += [pscustomobject]@{ source = $f.Other; relative_path = 'extension/youtube.keep'; sha256 = $f.OtherHash; size_bytes = [IO.FileInfo]::new($f.Other).Length } }
                identity { $p.identity_state = 'public_key_bound'; $p.extension_identity = [pscustomobject]@{ extension_id = $mainExtensionId; origin = ('chrome-extension://' + $mainExtensionId + '/'); public_key_sha256 = $mainPublicKeySha } }
                channel { $p.channel = 'Youtube' }
            }
            Assert-Throws { Copy-RcMusicChromiumExtensionPackagePlan -Plan $p } 'Stale or forged Main plan was copied'
            Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Invalid plan created output'
        }
    }
    Test-Case 'a changed Main source public key cannot be copied under the previously planned identity' {
        $f = New-Fixture Main -PublicKey $mainPublicKey
        $p = New-Plan $f
        Assert-True ($null -ne $p) 'No bound Main plan'
        $path = Join-Path $f.Extension 'manifest.json'
        $manifest = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
        $manifest['key'] = $otherPublicKey
        Write-Utf8 $path ((ConvertTo-Json -InputObject $manifest -Depth 12) + "`n")
        $changed = Hash $path
        Assert-Throws { Copy-RcMusicChromiumExtensionPackagePlan -Plan $p } 'Changed source identity was copied as its previous ID'
        Assert-True (-not (Test-Path -LiteralPath $f.Output)) 'Mismatched source identity created output'
        Assert-Equal (Hash $path) $changed
    }
} finally {
    Remove-Module RcMusicYoutubeExtensionPackage, RcMusicChromiumIdentity -Force -ErrorAction SilentlyContinue
    if ($state.Failed -eq 0) {
        Assert-SafeTestRoot
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    } else { Write-Host "[INFO] Preserved owned Chromium failed fixtures: $testRoot" }
}
Write-Host "Chromium source candidates: $($state.Passed) passed / $($state.Failed) failed"
if ($state.Failed -gt 0) { exit 1 }
