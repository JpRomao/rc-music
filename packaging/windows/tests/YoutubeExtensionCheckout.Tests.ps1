#Requires -Version 7.0
# Real Git checkout + real package planner; no browser, registry or network.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$parent = Join-Path $repository 'out/product/test-runs/youtube-extension-checkout'
$id = [Guid]::NewGuid().ToString('N')
$root = Join-Path $parent $id
$source = Join-Path $root 'source'
if ($id -cnotmatch '\A[0-9a-f]{32}\z' -or (Test-Path -LiteralPath $root)) { throw 'Fixture must be a fresh owned root.' }
$ancestor = $root
while ($ancestor) {
    try { $attributes = [IO.File]::GetAttributes($ancestor) }
    catch [IO.FileNotFoundException] { $attributes = 0 }
    catch [IO.DirectoryNotFoundException] { $attributes = 0 }
    if ($attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fixture ancestors must be ordinary directories.' }
    $ancestor = [IO.Path]::GetDirectoryName($ancestor)
}
function Owned-Git([string[]] $Arguments) {
    $result = @(& git --no-optional-locks -c core.fsmonitor=false -c core.autocrlf=true -c core.eol=crlf `
        -c ('core.hooksPath=' + (Join-Path $root 'empty-hooks')) -c ('init.templateDir=' + (Join-Path $root 'empty-hooks')) `
        -c commit.gpgsign=false -C $source @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw ('Owned Git failed: ' + ($result -join "`n")) }
}
$null = [IO.Directory]::CreateDirectory((Join-Path $root 'empty-hooks'))
$assets = @('manifest.json','native-client.mjs','policy.mjs','popup-controller.mjs','popup.css','popup.html','popup.mjs','page-video.mjs')
$recipes = @('New-RcMusicYoutubeExtensionPackage.ps1','New-RcMusicChromiumExtensionPackage.ps1',
    'RcMusicYoutubeExtensionPackage.psm1','RcMusicChromiumIdentity.psm1')
$extension = Join-Path $source 'extensions/chromium/youtube'
$packaging = Join-Path $source 'packaging/windows'
$null = [IO.Directory]::CreateDirectory($extension)
$null = [IO.Directory]::CreateDirectory($packaging)
Copy-Item -LiteralPath (Join-Path $repository '.gitattributes') -Destination $source
foreach ($name in $assets) { Copy-Item -LiteralPath (Join-Path $repository ('extensions/chromium/youtube/' + $name)) -Destination $extension }
foreach ($name in $recipes) { Copy-Item -LiteralPath (Join-Path $repository ('packaging/windows/' + $name)) -Destination $packaging }
Owned-Git @('init','-q')
Owned-Git @('add','--','.')
Owned-Git @('-c','user.name=RC Music Fixture','-c','user.email=fixture@example.invalid','commit','-q','-m','Seed owned checkout fixture')
# Only fixture copies are removed to force actual checkout conversion, not Git's stat-cache fast path.
foreach ($name in $assets) { [IO.File]::Delete((Join-Path $extension $name)) }
Owned-Git @('checkout','HEAD','--','extensions/chromium/youtube')
Import-Module (Join-Path $packaging 'RcMusicYoutubeExtensionPackage.psm1') -Force
$plan = New-RcMusicYoutubeExtensionPackagePlan -RepositoryRoot $source -OutputDirectory (Join-Path $source 'out/product/candidate')
if ($null -eq $plan -or $plan.files.Count -ne 8) { throw 'The complete YouTube family was not planned.' }
if ($plan.source_dirty -or $plan.extension_dirty -or $plan.recipe_dirty) {
    throw 'A clean Windows CRLF checkout changed critical YouTube bytes relative to HEAD.'
}
Write-Host 'PASS: Windows CRLF checkout preserves all eight canonical YouTube assets and clean provenance.'
Write-Host ('Owned fixture: ' + $root)
