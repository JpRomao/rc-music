#Requires -Version 7.0
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-RcMusicExtensionInstallerFixture {
    param([Parameter(Mandatory)][string] $Directory, [Parameter(Mandatory)][string] $RepositoryRoot,
        [ValidateSet('Lite', 'CompleteExperimental')][string] $Edition = 'Lite')
    $prefix = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'out/product/test-runs')).TrimEnd('\') + '\'
    $root = [IO.Path]::GetFullPath($Directory)
    if (-not $root.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $root)) {
        throw 'Extension installer fixtures require a fresh owned test directory'
    }
    for ($ancestor = [IO.Path]::GetDirectoryName($root); $ancestor; $ancestor = [IO.Path]::GetDirectoryName($ancestor)) {
        if ((Test-Path -LiteralPath $ancestor) -and ([IO.File]::GetAttributes($ancestor) -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Unsafe fixture ancestor'
        }
    }
    Import-Module (Join-Path $RepositoryRoot 'packaging/windows/RcMusicChromiumIdentity.psm1')
    Import-Module (Join-Path $RepositoryRoot 'packaging/windows/RcMusicDownloadComponentPe.psm1')
    $app = Join-Path $root 'app'
    $null = [IO.Directory]::CreateDirectory($app)
    $files = [Collections.Generic.List[object]]::new()
    $tools = [Collections.Generic.List[object]]::new()
    $reports = [Collections.Generic.List[object]]::new()
    $writeFile = {
        param([string] $Relative, [string] $Text)
        $path = Join-Path $app $Relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllText($path, $Text, [Text.UTF8Encoding]::new($false))
        $row = [pscustomobject]@{ relative_path = $Relative; source = 'Fixture provenance only'
            size_bytes = [IO.FileInfo]::new($path).Length; sha256 = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
        $files.Add($row)
        return $row
    }
    $base = @('RCMusic.exe', 'qt.conf', 'LOCAL-DEVELOPMENT.txt')
    if ($Edition -ceq 'CompleteExperimental') {
        $base += 'WEBVIEW2-DEVELOPMENT.txt', 'notices/WebView2-SDK-LICENSE.txt', 'notices/WebView2-SDK-NOTICE.txt'
    }
    foreach ($name in $base) { $null = & $writeFile $name ('Fixture only; never execute: ' + $name) }
    $network = [ordered]@{ Bootstrap='RCMusicDownloadWorker.exe'; Payload='rcmusic-download-network.dll'
        Core='Qt6Core.dll'; Network='Qt6Network.dll'; Msvcp140='msvcp140.dll'; Msvcp140_1='msvcp140_1.dll'
        Vcruntime140='vcruntime140.dll'; Vcruntime140_1='vcruntime140_1.dll'; Plugin='qrcmusicschanneloffline.dll' }
    $toolRows = @(foreach ($role in $network.Keys) { ,@('network', $role, ('network/' + $network[$role])) })
    $toolRows += ,@('media', 'Media', 'media/RCMusicMediaWorker.exe')
    $toolRows += ,@('youtube', 'Extractor', 'youtube/extractor/yt-dlp.exe')
    $toolRows += ,@('youtube', 'Node', 'youtube/javascript/node.exe')
    $toolRows += ,@('youtube', 'EjsCore', 'youtube/extractor/_internal/yt_dlp_ejs/yt/solver/core.min.js')
    $toolRows += ,@('youtube', 'EjsLibrary', 'youtube/extractor/_internal/yt_dlp_ejs/yt/solver/lib.min.js')
    foreach ($notice in @('python-LICENSE.txt','node-LICENSE.txt','yt-dlp-LICENSE.txt','ejs-LICENSE.txt','pyinstaller-COPYING.txt')) {
        $toolRows += ,@('youtube','Support', ('youtube/notices/' + $notice))
    }
    foreach ($tool in $toolRows) {
        $relative = 'download-tools/' + $tool[2]
        $row = & $writeFile $relative ('Fixture tool, never execute: ' + $relative)
        $tools.Add([pscustomobject]@{ source = Join-Path $app $relative; relative_path=$relative
            component=$tool[0]; role=$tool[1]; size_bytes=$row.size_bytes; sha256=$row.sha256 })
        if ([IO.Path]::GetExtension($relative) -in @('.exe','.dll','.pyd')) {
            $reports.Add([pscustomobject]@{ relative_path=$relative; machine='x64'; imports=@('KERNEL32.dll') })
        }
    }
    $channels = [Collections.Generic.List[object]]::new()
    $packages = @{}
    foreach ($channel in @('Main','Youtube')) {
        $youtube = $channel -ceq 'Youtube'
        $folder = $channel.ToLowerInvariant()
        $packageRoot = Join-Path $root ('package-' + $folder)
        $extension = Join-Path $packageRoot 'extension'
        $null = [IO.Directory]::CreateDirectory($extension)
        $assets = @('manifest.json','native-client.mjs','policy.mjs','popup-controller.mjs','popup.css','popup.html','popup.mjs')
        if (-not $youtube) { $assets += 'detector.js','media-collector.js' }
        else { $assets += 'page-video.mjs' }
        $packageFiles = @(foreach ($name in $assets) {
            $target = Join-Path $extension $name
            [IO.File]::Copy((Join-Path $RepositoryRoot ('extensions/chromium/' + $folder + '/' + $name)), $target, $false)
            [pscustomobject]@{ relative_path='extension/' + $name; size_bytes=[IO.FileInfo]::new($target).Length
                sha256=(Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() }
        })
        $identity = Get-RcMusicChromiumManifestIdentity -ManifestBytes ([IO.File]::ReadAllBytes((Join-Path $extension 'manifest.json')))
        $hostName = if ($youtube) { 'com.rcmusic.youtube' } else { 'com.rcmusic.media' }
        $exe = if ($youtube) { 'rc_music_youtube_native_host.exe' } else { 'rc_music_native_host.exe' }
        $null = & $writeFile $exe ('Fixture host; never execute: ' + $channel)
        $description = if ($youtube) { 'RC Music YouTube audio extension host' } else { 'RC Music media extension host' }
        $text = (ConvertTo-Json -InputObject ([ordered]@{name=$hostName;description=$description;path=$exe;type='stdio';allowed_origins=@($identity.origin)}) -Depth 4) + "`n"
        $null = & $writeFile ($hostName + '.json') $text
        $channels.Add([pscustomobject]@{ protocol=$(if ($youtube) {'youtube.audio.v1'}else{'direct.media.v1'});host_name=$hostName
            origin=$identity.origin;endpoint=('rcmusic-fixture-' + $folder);receipt_sha256=('b'*64);configuration_sha256=('c'*64)
            executable=$exe;manifest=$hostName + '.json' })
        $recipes = @('New-RcMusicChromiumExtensionPackage.ps1','RcMusicYoutubeExtensionPackage.psm1','RcMusicChromiumIdentity.psm1')
        if ($youtube) { $recipes = @('New-RcMusicYoutubeExtensionPackage.ps1') + $recipes }
        $recipeFiles = @($recipes | ForEach-Object { [pscustomobject]@{path='packaging/windows/' + $_;size_bytes=1;sha256=('d'*64)} })
        $receipt = [ordered]@{schema_version=1;component=$(if($youtube){'youtube-extension-source'}else{'main-extension-source'})
            channel=$channel;extension_identity=$identity;identity_state='public_key_bound';native_host_name=$hostName;native_manifest=$null
            source_commit=('a'*40);extension_dirty=$false;recipe_dirty=$false;source_dirty=$false
            source_inputs=@('extensions/chromium/' + $folder) + @($recipeFiles.path);recipe_files=$recipeFiles;files=$packageFiles}
        $receiptPath = Join-Path $packageRoot 'package-receipt.json'
        [IO.File]::WriteAllText($receiptPath,(ConvertTo-Json -InputObject $receipt -Depth 10),[Text.UTF8Encoding]::new($false))
        $packages[$channel] = [pscustomobject]@{path=$receiptPath;sha256=(Get-FileHash -LiteralPath $receiptPath).Hash.ToLowerInvariant()}
    }
    $componentFiles = @($files | Where-Object { $_.relative_path.StartsWith('download-tools/') -or $_.relative_path -in @(
        'rc_music_native_host.exe','rc_music_youtube_native_host.exe','com.rcmusic.media.json','com.rcmusic.youtube.json') } |
        Select-Object relative_path,size_bytes,sha256)
    $manifest = [ordered]@{schema_version=1;name='RC Music';architecture='x64';build_testing=$false;recipe_dirty=$false
        source_commit=('a'*40);distribution='private-local-development-only';edition=$Edition
        browser_runtime_requirement=$(if($Edition -ceq 'Lite'){'none'}else{'existing-webview2-evergreen-x64'});files=$files.ToArray()
        download_component=[ordered]@{schema_version=1;selection_default=$false;receipt_sha256=('e'*64);header_sha256=('f'*64)
            activation_marker=[ordered]@{relative_path='download-tools/installed-v1';content="RCMusicDownloadTools-v1`n"}
            files=$componentFiles;pe_closure=(Get-RcMusicDownloadComponentPeClosure -Files $tools.ToArray() -Reports $reports.ToArray())
            native_hosts=[ordered]@{schema_version=1;shared_header_sha256=('a'*64);channels=$channels.ToArray()}}}
    $manifestPath = Join-Path $root 'runtime-manifest.json'
    [IO.File]::WriteAllText($manifestPath,(ConvertTo-Json -InputObject $manifest -Depth 15),[Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{App=$app;Output=(Join-Path $root 'installer');ManifestPath=$manifestPath
        MainReceipt=$packages.Main.path;MainReceiptSha256=$packages.Main.sha256
        YoutubeReceipt=$packages.Youtube.path;YoutubeReceiptSha256=$packages.Youtube.sha256}
}
Export-ModuleMember -Function New-RcMusicExtensionInstallerFixture
