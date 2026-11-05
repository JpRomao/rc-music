#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
Import-Module (Join-Path $PSScriptRoot '../RcMusicInstallerPlan.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ExtensionInstallerFixture.psm1') -Force
$parent=Join-Path $repository 'out/product/test-runs/installer-extension-plan'
$root=Join-Path $parent ([guid]::NewGuid().ToString('N'))
$state=@{Passed=0;Failed=0;Next=0}
function Case([string]$Name,[scriptblock]$Body){try{&$Body;$state.Passed++;Write-Host "[PASS] $Name"}catch{$state.Failed++;Write-Host "[FAIL] $Name :: $($_.Exception.Message)"}}
function Require([bool]$Value,[string]$Message){if(-not $Value){throw $Message}}
function Reject([scriptblock]$Body){try{&$Body|Out-Null}catch{return};throw 'Expected rejection'}
function Fixture([string]$Edition='Lite'){$state.Next++;New-RcMusicExtensionInstallerFixture -Directory (Join-Path $root ('fixture-'+$state.Next)) -RepositoryRoot $repository -Edition $Edition}
function Plan($Fixture,[hashtable]$Extra=@{}){
    $args=@{RuntimeDirectory=$Fixture.App;OutputDirectory=$Fixture.Output}
    foreach($key in $Extra.Keys){$args[$key]=$Extra[$key]}
    New-RcMusicInstallerPlan @args
}
function Packages($Fixture){@{MainExtensionPackageReceipt=$Fixture.MainReceipt;MainExtensionPackageReceiptSha256=$Fixture.MainReceiptSha256;YoutubeExtensionPackageReceipt=$Fixture.YoutubeReceipt;YoutubeExtensionPackageReceiptSha256=$Fixture.YoutubeReceiptSha256}}

foreach($edition in @('Lite','CompleteExperimental')){
    Case "$edition selects five explicit variants with independent extension inventories" {
        $f=Fixture $edition;$args=Packages $f;$args.Edition=$edition;$p=Plan $f $args
        Require ($p.payload_variants.Count -eq 5) 'Expected base/tools/main/youtube/both variants'
        Require ((@($p.payload_variants.mask|Sort-Object)-join ',') -ceq '0,1,3,5,7') 'Invalid dependency masks'
        Require (@($p.payload_variants.payload_id|Sort-Object -Unique).Count -eq 5) 'Variant IDs collide'
        Require ($p.browser_extensions.Count -eq 2) 'Both explicit packages must be retained'
        Require ($p.config.Contains('!define RCMUSIC_BROWSER_EXTENSIONS_AVAILABLE 1')) 'Browser options unavailable'
        Require ($p.config.Contains('!macro RCMUSIC_SELECT_VARIANT')) 'Selector macro missing'
        Require ($p.config.Contains('!macro RCMUSIC_DETECT_VARIANT')) 'Uninstall directory selector missing'
        foreach($channel in @('main','youtube')){
            Require (@($p.files|Where-Object relative_path -CLike "browser-extensions/$channel/*").Count -eq $(if($channel -ceq 'main'){9}else{7})) 'Wrong source inventory'
        }
        Require (-not(Test-Path -LiteralPath $f.Output)) 'Planning wrote installer output'
    }
}
Case 'one package only enables its own optional section and its three variants' {
    foreach($channel in @('Main','Youtube')){
        $f=Fixture;$args=@{};$args[$channel+'ExtensionPackageReceipt']=$f.($channel+'Receipt');$args[$channel+'ExtensionPackageReceiptSha256']=$f.($channel+'ReceiptSha256')
        $p=Plan $f $args
        Require ($p.payload_variants.Count -eq 3) 'Unavailable extension got a variant'
        Require ($p.browser_extensions.Count -eq 1 -and $p.browser_extensions[0].channel -ceq $channel) 'Wrong package enabled'
    }
}
Case 'ordinary tools-only plans retain two variants and no browser payload' {
    $f=Fixture;$p=Plan $f
    Require ($p.payload_ids.base -cne $p.payload_ids.downloads) 'Existing choices collide'
    Require (-not($p.files.relative_path -cmatch '^browser-extensions/')) 'Unrequested extension was included'
}
Case 'extension receipt pins are paired and exact before output' {
    foreach($args in @(@{MainExtensionPackageReceipt='C:/missing/package-receipt.json'},@{MainExtensionPackageReceiptSha256=('a'*64)},@{MainExtensionPackageReceipt='';MainExtensionPackageReceiptSha256=('a'*64)},@{MainExtensionPackageReceipt='C:/missing/package-receipt.json';MainExtensionPackageReceiptSha256=('A'*64)})){
        $f=Fixture;Reject {Plan $f $args};Require (-not(Test-Path -LiteralPath $f.Output)) 'Invalid opt-in wrote output'
    }
}
Case 'packages cannot be paired with an absent or mismatched native channel' {
    foreach($variant in @('missing','origin')){
        $f=Fixture;$m=Get-Content -LiteralPath $f.ManifestPath -Raw|ConvertFrom-Json
        if($variant -ceq 'missing'){$m.download_component.PSObject.Properties.Remove('native_hosts');$m.files=@($m.files|Where-Object relative_path -CNotMatch '^(rc_music_.*native_host\.exe|com\.rcmusic\..*\.json)$');$m.download_component.files=@($m.download_component.files|Where-Object relative_path -CNotMatch '^(rc_music_.*native_host\.exe|com\.rcmusic\..*\.json)$')}
        else{$m.download_component.native_hosts.channels[0].origin='chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/'}
        [IO.File]::WriteAllText($f.ManifestPath,($m|ConvertTo-Json -Depth 15));Reject {Plan $f (Packages $f)}
    }
}
Case 'browser assets declared inside an ordinary runtime cannot become mandatory files' {
    $f=Fixture;$m=Get-Content -LiteralPath $f.ManifestPath -Raw|ConvertFrom-Json
    $path=Join-Path $f.App 'browser-extensions/main/manifest.json';$null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path));[IO.File]::WriteAllText($path,'{}')
    $m.files += [pscustomobject]@{relative_path='browser-extensions/main/manifest.json';size_bytes=2;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
    [IO.File]::WriteAllText($f.ManifestPath,($m|ConvertTo-Json -Depth 15));Reject {Plan $f}
}
Case 'a package snapshot changed after planning cannot retain its previous identity' {
    $f=Fixture;$p=Plan $f (Packages $f)
    $path=Join-Path ([IO.Path]::GetDirectoryName($f.MainReceipt)) 'extension/popup.css';[IO.File]::AppendAllText($path,'changed')
    Reject {Plan $f (Packages $f)}
    Require ($p.browser_extensions.Count -eq 2) 'Initial plan was not valid'
}
Write-Host "Installer extensions: $($state.Passed) passed, $($state.Failed) failed. Fixtures: $root"
if($state.Failed){exit 1}
